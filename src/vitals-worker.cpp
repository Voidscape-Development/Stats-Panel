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

#include "vitals-worker.hpp"
#include "vitals-gpu.hpp"

#include <util/platform.h>
#include <util/threading.h>

#include <chrono>
#include <cmath>

namespace vitals {

namespace {
constexpr auto kGpuPeriod = std::chrono::seconds(1);
constexpr int kDiskEveryNthTick = 5;
} // namespace

Worker::~Worker()
{
	stop();
}

void Worker::start()
{
	if (thread_.joinable())
		return;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = false;
	}
	thread_ = std::thread(&Worker::run, this);
}

void Worker::stop()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = true;
	}
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

void Worker::setActive(bool active)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (active_ == active)
			return;
		active_ = active;
	}
	cv_.notify_all();
}

void Worker::setGpuSource(GpuSource source)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (source_ == source)
			return;
		source_ = source;
		sourceChanged_ = true;
	}
	cv_.notify_all();
}

void Worker::setDiskPath(const std::string &path)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (diskPath_ == path)
			return;
		diskPath_ = path;
		diskChanged_ = true;
	}
	cv_.notify_all();
}

void Worker::run()
{
	os_set_thread_name("vitals: stats worker");

	std::unique_ptr<GpuMonitor> gpu;
	int tick = 0;
	std::string path;

	std::unique_lock<std::mutex> lock(mutex_);
	while (!stop_) {
		if (!active_) {
			cv_.wait(lock, [this] { return stop_ || active_; });
			continue;
		}

		const bool sourceChanged = sourceChanged_;
		const GpuSource source = source_;
		sourceChanged_ = false;
		const bool sampleDisk = diskChanged_ || tick % kDiskEveryNthTick == 0;
		if (sampleDisk)
			path = diskPath_;
		diskChanged_ = false;
		lock.unlock();

		/* PDH and NVML calls can take a few milliseconds, which is why
		 * they live here and not on the UI thread. */
		if (sourceChanged) {
			gpu.reset();
			gpu_.store(NAN, std::memory_order_relaxed);
			gpu = GpuMonitor::create(source);
		}
		if (gpu)
			gpu_.store(gpu->sample(), std::memory_order_relaxed);

		if (sampleDisk) {
			int64_t free = -1;
			if (!path.empty()) {
				const uint64_t bytes = os_get_free_disk_space(path.c_str());
				if (bytes > 0)
					free = static_cast<int64_t>(bytes);
			}
			disk_.store(free, std::memory_order_relaxed);
		}
		tick++;

		lock.lock();
		cv_.wait_for(lock, kGpuPeriod, [this] { return stop_ || sourceChanged_ || diskChanged_; });
	}
	lock.unlock();

	gpu.reset();
}

} // namespace vitals
