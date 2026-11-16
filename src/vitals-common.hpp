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

#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>

namespace vitals {

enum class Level : uint8_t { Ok, Warn, Crit };

enum class Group : uint8_t { System, Render, Stream, Record, Outputs };
constexpr size_t kGroupCount = 5;
constexpr std::array<Group, kGroupCount> kGroups = {Group::System, Group::Render, Group::Stream, Group::Record,
						    Group::Outputs};

enum class Metric : uint8_t {
	Cpu,
	Gpu,
	Memory,
	Disk,
	Session,
	Fps,
	FrameTime,
	Missed,
	Skipped,
	StreamBitrate,
	Dropped,
	Sent,
	RecBitrate,
	RecSize,
	Outputs,
};
constexpr size_t kMetricCount = 15;

enum class GpuSource : uint8_t { Auto, Pdh, Nvml, Off };

struct MetricInfo {
	Metric id;
	Group group;
	const char *key; /* stable id used in the settings file and locale keys */
	bool hasThreshold;
	bool lowerIsWorse;
	double defaultWarn;
	double defaultCrit;
	bool defaultStrip;
};

const std::array<MetricInfo, kMetricCount> &metricTable();

inline size_t idx(Metric m)
{
	return static_cast<size_t>(m);
}

inline size_t idx(Group g)
{
	return static_cast<size_t>(g);
}

inline const MetricInfo &info(Metric m)
{
	return metricTable()[idx(m)];
}

/* Localized text from data/locale. */
QString loc(const char *key);

/* Cached, localized labels. Locale cannot change while OBS runs. */
const QString &metricLabel(Metric m);
const QString &metricShortLabel(Metric m);
const QString &metricHint(Metric m);
const QString &groupLabel(Group g);

/* Fixed-capacity ring buffer. Never allocates after construction. */
template<size_t N> class Ring {
public:
	void push(float value)
	{
		data_[head_] = value;
		head_ = (head_ + 1) % N;
		if (size_ < N)
			++size_;
	}

	void clear()
	{
		head_ = 0;
		size_ = 0;
	}

	size_t size() const { return size_; }

	/* i = 0 is the oldest retained sample. */
	float at(size_t i) const { return data_[(head_ + N - size_ + i) % N]; }

private:
	std::array<float, N> data_{};
	size_t head_ = 0;
	size_t size_ = 0;
};

constexpr size_t kHistoryCapacity = 120;
using History = Ring<kHistoryCapacity>;

} // namespace vitals
