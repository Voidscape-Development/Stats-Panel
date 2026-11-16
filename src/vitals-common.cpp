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

#include "vitals-common.hpp"

#include <obs-module.h>

#include <string>

namespace vitals {

const std::array<MetricInfo, kMetricCount> &metricTable()
{
	/* Order must match the Metric enum. Thresholds:
	 *   cpu, gpu: percent
	 *   disk: GB free (lower is worse)
	 *   frametime: percent of the frame budget
	 *   missed, skipped, dropped: percent of frames */
	static const std::array<MetricInfo, kMetricCount> table = {{
		{Metric::Cpu, Group::System, "cpu", true, false, 70.0, 90.0, true},
		{Metric::Gpu, Group::System, "gpu", true, false, 85.0, 95.0, false},
		{Metric::Memory, Group::System, "memory", false, false, 0.0, 0.0, false},
		{Metric::Disk, Group::System, "disk", true, true, 20.0, 5.0, false},
		{Metric::Session, Group::System, "session", false, false, 0.0, 0.0, false},
		{Metric::Fps, Group::Render, "fps", false, false, 0.0, 0.0, true},
		{Metric::FrameTime, Group::Render, "frametime", true, false, 85.0, 100.0, true},
		{Metric::Missed, Group::Render, "missed", true, false, 0.5, 2.0, false},
		{Metric::Skipped, Group::Render, "skipped", true, false, 0.5, 2.0, false},
		{Metric::StreamBitrate, Group::Stream, "stream_bitrate", false, false, 0.0, 0.0, true},
		{Metric::Dropped, Group::Stream, "dropped", true, false, 0.5, 2.0, true},
		{Metric::Sent, Group::Stream, "sent", false, false, 0.0, 0.0, false},
		{Metric::RecBitrate, Group::Record, "rec_bitrate", false, false, 0.0, 0.0, false},
		{Metric::RecSize, Group::Record, "rec_size", false, false, 0.0, 0.0, false},
		{Metric::Outputs, Group::Outputs, "outputs", false, false, 0.0, 0.0, true},
	}};
	return table;
}

QString loc(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

namespace {

struct Labels {
	std::array<QString, kMetricCount> label;
	std::array<QString, kMetricCount> shortLabel;
	std::array<QString, kMetricCount> hint;
	std::array<QString, kGroupCount> group;
};

const Labels &labels()
{
	static const Labels cached = [] {
		Labels l;
		for (const MetricInfo &mi : metricTable()) {
			const std::string base = std::string("Metric.") + mi.key;
			l.label[idx(mi.id)] = loc(base.c_str());
			l.shortLabel[idx(mi.id)] = loc((base + ".Short").c_str());
			l.hint[idx(mi.id)] = loc((base + ".Hint").c_str());
		}
		l.group[idx(Group::System)] = loc("Group.System");
		l.group[idx(Group::Render)] = loc("Group.Rendering");
		l.group[idx(Group::Stream)] = loc("Group.Stream");
		l.group[idx(Group::Record)] = loc("Group.Recording");
		l.group[idx(Group::Outputs)] = loc("Group.Outputs");
		return l;
	}();
	return cached;
}

} // namespace

const QString &metricLabel(Metric m)
{
	return labels().label[idx(m)];
}

const QString &metricShortLabel(Metric m)
{
	return labels().shortLabel[idx(m)];
}

const QString &metricHint(Metric m)
{
	return labels().hint[idx(m)];
}

const QString &groupLabel(Group g)
{
	return labels().group[idx(g)];
}

} // namespace vitals
