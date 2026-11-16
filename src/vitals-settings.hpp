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

#pragma once

#include "vitals-common.hpp"

#include <map>
#include <string>
#include <vector>

namespace vitals {

struct OutputPref {
	std::string alias;
	bool hidden = false;
};

struct Settings {
	int intervalMs = 1000;
	bool comfortable = false;
	bool colorAlways = false;
	bool groupHeaders = true;
	bool trendLines = true;
	bool tintTitle = true;
	bool hideOffline = false;
	bool resetOnStreamStart = true;
	GpuSource gpuSource = GpuSource::Auto;

	std::array<bool, kMetricCount> show{};
	std::array<bool, kMetricCount> strip{};
	std::array<double, kMetricCount> warn{};
	std::array<double, kMetricCount> crit{};
	std::array<std::vector<Metric>, kGroupCount> order;

	/* Keyed by the OBS output name. */
	std::map<std::string, OutputPref> outputs;

	static Settings defaults();
	static Settings load();
	void save() const;

	Level level(Metric m, double value) const;
	const OutputPref *outputPref(const std::string &name) const;
};

constexpr int kIntervalChoices[] = {250, 500, 1000, 2000};

} // namespace vitals
