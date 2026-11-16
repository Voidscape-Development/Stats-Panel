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

#include "vitals-settings.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <plugin-support.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace vitals {

namespace {

constexpr const char *kFileName = "settings.json";
constexpr int kVersion = 1;

struct BFree {
	void operator()(char *p) const { bfree(p); }
};
using BString = std::unique_ptr<char, BFree>;

struct DataRelease {
	void operator()(obs_data_t *d) const { obs_data_release(d); }
};
using Data = std::unique_ptr<obs_data_t, DataRelease>;

struct ArrayRelease {
	void operator()(obs_data_array_t *a) const { obs_data_array_release(a); }
};
using DataArray = std::unique_ptr<obs_data_array_t, ArrayRelease>;

const char *gpuSourceKey(GpuSource s)
{
	switch (s) {
	case GpuSource::Pdh:
		return "pdh";
	case GpuSource::Nvml:
		return "nvml";
	case GpuSource::Off:
		return "off";
	case GpuSource::Auto:
		break;
	}
	return "auto";
}

GpuSource gpuSourceFromKey(const char *key)
{
	if (!key)
		return GpuSource::Auto;
	if (strcmp(key, "pdh") == 0)
		return GpuSource::Pdh;
	if (strcmp(key, "nvml") == 0)
		return GpuSource::Nvml;
	if (strcmp(key, "off") == 0)
		return GpuSource::Off;
	return GpuSource::Auto;
}

const MetricInfo *metricFromKey(const char *key)
{
	if (!key)
		return nullptr;
	for (const MetricInfo &mi : metricTable()) {
		if (strcmp(mi.key, key) == 0)
			return &mi;
	}
	return nullptr;
}

int nearestInterval(long long ms)
{
	int best = kIntervalChoices[0];
	for (int choice : kIntervalChoices) {
		if (std::llabs(ms - choice) < std::llabs(ms - best))
			best = choice;
	}
	return best;
}

} // namespace

Settings Settings::defaults()
{
	Settings s;
	for (const MetricInfo &mi : metricTable()) {
		const size_t i = idx(mi.id);
		s.show[i] = true;
		s.strip[i] = mi.defaultStrip;
		s.warn[i] = mi.defaultWarn;
		s.crit[i] = mi.defaultCrit;
		s.order[idx(mi.group)].push_back(mi.id);
	}
	return s;
}

Settings Settings::load()
{
	Settings s = defaults();

	BString path(obs_module_config_path(kFileName));
	if (!path || !os_file_exists(path.get()))
		return s;

	Data data(obs_data_create_from_json_file_safe(path.get(), "bak"));
	if (!data) {
		obs_log(LOG_WARNING, "could not read %s, using defaults", path.get());
		return s;
	}

	obs_data_t *d = data.get();
	auto getBool = [d](const char *name, bool fallback) {
		return obs_data_has_user_value(d, name) ? obs_data_get_bool(d, name) : fallback;
	};

	if (obs_data_has_user_value(d, "interval_ms"))
		s.intervalMs = nearestInterval(obs_data_get_int(d, "interval_ms"));
	s.comfortable = getBool("comfortable", s.comfortable);
	s.colorAlways = getBool("color_always", s.colorAlways);
	s.groupHeaders = getBool("group_headers", s.groupHeaders);
	s.trendLines = getBool("trend_lines", s.trendLines);
	s.tintTitle = getBool("tint_title", s.tintTitle);
	s.hideOffline = getBool("hide_offline", s.hideOffline);
	s.resetOnStreamStart = getBool("reset_on_stream_start", s.resetOnStreamStart);
	if (obs_data_has_user_value(d, "gpu_source"))
		s.gpuSource = gpuSourceFromKey(obs_data_get_string(d, "gpu_source"));

	/* The metrics array is stored in display order. Metrics missing from the
	 * file (for example ones added in a later version) keep their defaults and
	 * are appended to the end of their group. */
	DataArray metrics(obs_data_get_array(d, "metrics"));
	if (metrics) {
		std::array<std::vector<Metric>, kGroupCount> order;
		const size_t count = obs_data_array_count(metrics.get());
		for (size_t i = 0; i < count; i++) {
			Data item(obs_data_array_item(metrics.get(), i));
			const MetricInfo *mi = metricFromKey(obs_data_get_string(item.get(), "key"));
			if (!mi)
				continue;
			std::vector<Metric> &groupOrder = order[idx(mi->group)];
			if (std::find(groupOrder.begin(), groupOrder.end(), mi->id) != groupOrder.end())
				continue;
			groupOrder.push_back(mi->id);

			const size_t m = idx(mi->id);
			obs_data_t *it = item.get();
			if (obs_data_has_user_value(it, "show"))
				s.show[m] = obs_data_get_bool(it, "show");
			if (obs_data_has_user_value(it, "strip"))
				s.strip[m] = obs_data_get_bool(it, "strip");
			if (mi->hasThreshold) {
				if (obs_data_has_user_value(it, "warn"))
					s.warn[m] = std::max(0.0, obs_data_get_double(it, "warn"));
				if (obs_data_has_user_value(it, "crit"))
					s.crit[m] = std::max(0.0, obs_data_get_double(it, "crit"));
			}
		}
		for (const MetricInfo &mi : metricTable()) {
			std::vector<Metric> &groupOrder = order[idx(mi.group)];
			if (std::find(groupOrder.begin(), groupOrder.end(), mi.id) == groupOrder.end())
				groupOrder.push_back(mi.id);
		}
		s.order = std::move(order);
	}

	DataArray outputs(obs_data_get_array(d, "outputs"));
	if (outputs) {
		const size_t count = obs_data_array_count(outputs.get());
		for (size_t i = 0; i < count; i++) {
			Data item(obs_data_array_item(outputs.get(), i));
			const char *name = obs_data_get_string(item.get(), "name");
			if (!name || !*name)
				continue;
			OutputPref pref;
			pref.alias = obs_data_get_string(item.get(), "alias");
			pref.hidden = obs_data_get_bool(item.get(), "hidden");
			if (!pref.alias.empty() || pref.hidden)
				s.outputs[name] = std::move(pref);
		}
	}

	return s;
}

void Settings::save() const
{
	BString dir(obs_module_config_path(""));
	if (dir)
		os_mkdirs(dir.get());

	BString path(obs_module_config_path(kFileName));
	if (!path)
		return;

	Data data(obs_data_create());
	obs_data_t *d = data.get();
	obs_data_set_int(d, "version", kVersion);
	obs_data_set_int(d, "interval_ms", intervalMs);
	obs_data_set_bool(d, "comfortable", comfortable);
	obs_data_set_bool(d, "color_always", colorAlways);
	obs_data_set_bool(d, "group_headers", groupHeaders);
	obs_data_set_bool(d, "trend_lines", trendLines);
	obs_data_set_bool(d, "tint_title", tintTitle);
	obs_data_set_bool(d, "hide_offline", hideOffline);
	obs_data_set_bool(d, "reset_on_stream_start", resetOnStreamStart);
	obs_data_set_string(d, "gpu_source", gpuSourceKey(gpuSource));

	DataArray metrics(obs_data_array_create());
	for (Group g : kGroups) {
		for (Metric m : order[idx(g)]) {
			const MetricInfo &mi = info(m);
			Data item(obs_data_create());
			obs_data_set_string(item.get(), "key", mi.key);
			obs_data_set_bool(item.get(), "show", show[idx(m)]);
			obs_data_set_bool(item.get(), "strip", strip[idx(m)]);
			if (mi.hasThreshold) {
				obs_data_set_double(item.get(), "warn", warn[idx(m)]);
				obs_data_set_double(item.get(), "crit", crit[idx(m)]);
			}
			obs_data_array_push_back(metrics.get(), item.get());
		}
	}
	obs_data_set_array(d, "metrics", metrics.get());

	DataArray outputArray(obs_data_array_create());
	for (const auto &[name, pref] : outputs) {
		if (pref.alias.empty() && !pref.hidden)
			continue;
		Data item(obs_data_create());
		obs_data_set_string(item.get(), "name", name.c_str());
		obs_data_set_string(item.get(), "alias", pref.alias.c_str());
		obs_data_set_bool(item.get(), "hidden", pref.hidden);
		obs_data_array_push_back(outputArray.get(), item.get());
	}
	obs_data_set_array(d, "outputs", outputArray.get());

	if (!obs_data_save_json_safe(d, path.get(), "tmp", "bak"))
		obs_log(LOG_WARNING, "could not save %s", path.get());
}

Level Settings::level(Metric m, double value) const
{
	const MetricInfo &mi = info(m);
	if (!mi.hasThreshold)
		return Level::Ok;
	const size_t i = idx(m);
	if (mi.lowerIsWorse)
		return value <= crit[i] ? Level::Crit : value <= warn[i] ? Level::Warn : Level::Ok;
	return value >= crit[i] ? Level::Crit : value >= warn[i] ? Level::Warn : Level::Ok;
}

const OutputPref *Settings::outputPref(const std::string &name) const
{
	auto it = outputs.find(name);
	return it == outputs.end() ? nullptr : &it->second;
}

} // namespace vitals
