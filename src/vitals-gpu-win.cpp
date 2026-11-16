/*
Vitals - a compact, adaptive stats dock for OBS Studio
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "vitals-gpu.hpp"

/* util/base.h must come before plugin-support.h: it declares blogva with
 * dllimport, and MSVC rejects the reverse order (C2375). */
#include <util/base.h>
#include <plugin-support.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

namespace vitals {

namespace {

/* GPU Engine performance counters, the source Task Manager uses.
 *
 * Each instance is one engine used by one process, named like
 *   pid_1234_luid_0x00000000_0x0000D1C2_phys_0_eng_3_engtype_VideoDecode
 * Task Manager sums each engine across processes and reports the busiest
 * engine as the GPU's utilization. We do the same. */
class PdhGpuMonitor final : public GpuMonitor {
public:
	~PdhGpuMonitor() override
	{
		if (query_)
			PdhCloseQuery(query_);
	}

	bool init()
	{
		if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) {
			query_ = nullptr;
			return false;
		}
		if (PdhAddEnglishCounterW(query_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter_) !=
		    ERROR_SUCCESS)
			return false;
		/* Rate counters need two samples; this is the first. */
		return PdhCollectQueryData(query_) == ERROR_SUCCESS;
	}

	double sample() override
	{
		if (PdhCollectQueryData(query_) != ERROR_SUCCESS)
			return NAN;

		DWORD size = 0;
		DWORD count = 0;
		PDH_STATUS status = PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size,
								 &count, nullptr);
		if (status == ERROR_SUCCESS)
			return 0.0; /* no GPU work at all */
		if (static_cast<DWORD>(status) != static_cast<DWORD>(PDH_MORE_DATA))
			return NAN;

		buffer_.resize(size);
		auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer_.data());
		status =
			PdhGetFormattedCounterArrayW(counter_, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items);
		if (status != ERROR_SUCCESS)
			return NAN;

		/* Engine keys are stable across samples, so the map stops
		 * allocating once every engine has been seen. */
		for (auto &engine : engines_)
			engine.second = 0.0;

		for (DWORD i = 0; i < count; i++) {
			const PDH_FMT_COUNTERVALUE &value = items[i].FmtValue;
			if (value.CStatus != PDH_CSTATUS_VALID_DATA && value.CStatus != PDH_CSTATUS_NEW_DATA)
				continue;
			const wchar_t *engine = items[i].szName ? wcsstr(items[i].szName, L"luid_") : nullptr;
			if (!engine)
				continue;
			key_.assign(engine);
			engines_[key_] += value.doubleValue;
		}

		double busiest = 0.0;
		for (const auto &engine : engines_)
			busiest = std::max(busiest, engine.second);
		return std::min(busiest, 100.0);
	}

	const char *name() const override { return "Windows performance counters"; }

private:
	PDH_HQUERY query_ = nullptr;
	PDH_HCOUNTER counter_ = nullptr;
	std::vector<unsigned char> buffer_;
	std::unordered_map<std::wstring, double> engines_;
	std::wstring key_;
};

/* NVIDIA Management Library, loaded at runtime so the plugin has no hard
 * dependency on an NVIDIA driver. */
class NvmlGpuMonitor final : public GpuMonitor {
public:
	~NvmlGpuMonitor() override
	{
		if (shutdown_ && initialized_)
			shutdown_();
		if (lib_)
			FreeLibrary(lib_);
	}

	bool init()
	{
		lib_ = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!lib_) {
			wchar_t path[MAX_PATH];
			const DWORD len = ExpandEnvironmentStringsW(
				L"%ProgramFiles%\\NVIDIA Corporation\\NVSMI\\nvml.dll", path, MAX_PATH);
			if (len > 0 && len <= MAX_PATH)
				lib_ = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		}
		if (!lib_)
			return false;

		init_ = reinterpret_cast<InitFn>(GetProcAddress(lib_, "nvmlInit_v2"));
		shutdown_ = reinterpret_cast<ShutdownFn>(GetProcAddress(lib_, "nvmlShutdown"));
		getCount_ = reinterpret_cast<CountFn>(GetProcAddress(lib_, "nvmlDeviceGetCount_v2"));
		getHandle_ = reinterpret_cast<HandleFn>(GetProcAddress(lib_, "nvmlDeviceGetHandleByIndex_v2"));
		getUtil_ = reinterpret_cast<UtilFn>(GetProcAddress(lib_, "nvmlDeviceGetUtilizationRates"));
		if (!init_ || !shutdown_ || !getCount_ || !getHandle_ || !getUtil_)
			return false;

		if (init_() != kSuccess)
			return false;
		initialized_ = true;

		unsigned int count = 0;
		if (getCount_(&count) != kSuccess || count == 0)
			return false;
		for (unsigned int i = 0; i < count; i++) {
			Device device = nullptr;
			if (getHandle_(i, &device) == kSuccess)
				devices_.push_back(device);
		}
		return !devices_.empty();
	}

	double sample() override
	{
		double busiest = NAN;
		for (Device device : devices_) {
			Utilization util = {};
			if (getUtil_(device, &util) != kSuccess)
				continue;
			const double gpu = static_cast<double>(util.gpu);
			busiest = std::isnan(busiest) ? gpu : std::max(busiest, gpu);
		}
		return busiest;
	}

	const char *name() const override { return "NVML"; }

private:
	struct DeviceStruct;
	using Device = DeviceStruct *;
	struct Utilization {
		unsigned int gpu;
		unsigned int memory;
	};
	using Result = int;
	static constexpr Result kSuccess = 0;

	using InitFn = Result(__cdecl *)();
	using ShutdownFn = Result(__cdecl *)();
	using CountFn = Result(__cdecl *)(unsigned int *);
	using HandleFn = Result(__cdecl *)(unsigned int, Device *);
	using UtilFn = Result(__cdecl *)(Device, Utilization *);

	HMODULE lib_ = nullptr;
	bool initialized_ = false;
	InitFn init_ = nullptr;
	ShutdownFn shutdown_ = nullptr;
	CountFn getCount_ = nullptr;
	HandleFn getHandle_ = nullptr;
	UtilFn getUtil_ = nullptr;
	std::vector<Device> devices_;
};

template<typename T> std::unique_ptr<GpuMonitor> tryCreate()
{
	auto monitor = std::make_unique<T>();
	if (!monitor->init())
		return nullptr;
	obs_log(LOG_INFO, "GPU usage source: %s", monitor->name());
	return monitor;
}

} // namespace

std::unique_ptr<GpuMonitor> GpuMonitor::create(GpuSource source)
{
	std::unique_ptr<GpuMonitor> monitor;
	switch (source) {
	case GpuSource::Off:
		return nullptr;
	case GpuSource::Pdh:
		monitor = tryCreate<PdhGpuMonitor>();
		break;
	case GpuSource::Nvml:
		monitor = tryCreate<NvmlGpuMonitor>();
		break;
	case GpuSource::Auto:
		monitor = tryCreate<PdhGpuMonitor>();
		if (!monitor)
			monitor = tryCreate<NvmlGpuMonitor>();
		break;
	}
	if (!monitor)
		obs_log(LOG_WARNING, "GPU usage is unavailable with the selected source");
	return monitor;
}

} // namespace vitals
