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

#include "vitals-display.hpp"

#include <QLocale>

#include <algorithm>
#include <cmath>

namespace vitals {

namespace {

constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
constexpr double kMiB = 1024.0 * 1024.0;
constexpr double kSparkWindowMs = 30000.0;

/* Localized strings used on every update, looked up once. */
struct Text {
	QString allGood = loc("Status.AllGood");
	QString more = loc("Status.More");
	QString live = loc("Status.Live");
	QString notLive = loc("Status.NotLive");
	QString paused = loc("Status.Paused");
	QString offline = loc("Status.Offline");
	QString noneActive = loc("Status.NoneActive");
	QString reconnecting = loc("Status.Reconnecting");
	QString retrying = loc("Status.Retrying");
	QString unavailable = loc("Status.Unavailable");
	QString on = loc("Status.On");
	QString streamReconnecting = loc("Status.StreamReconnecting");
	QString outputReconnecting = loc("Status.OutputReconnecting");
	QString outputDropped = loc("Status.OutputDropped");
	QString outputsOk = loc("Status.OutputsOk");
	QString obsProcess = loc("Foot.ObsProcess");
	QString sinceStart = loc("Foot.SinceStart");
	QString target = loc("Foot.Target");
	QString ofTotal = loc("Foot.OfTotal");
	QString sinceStream = loc("Foot.SinceStream");
	QString currentFile = loc("Foot.CurrentFile");
	QString uploadRate = loc("Foot.UploadRate");
	QString writeRate = loc("Foot.WriteRate");
	QString hoursLeft = loc("Foot.HoursLeft");
	QString minutesLeft = loc("Foot.MinutesLeft");
	QString recordingDrive = loc("Foot.RecordingDrive");
	QString kindStream = loc("Output.Stream");
	QString kindRecording = loc("Output.Recording");
	QString replayBuffer = loc("Output.ReplayBuffer");
	QString virtualCam = loc("Output.VirtualCam");
	QString dash = QStringLiteral("—");
};

const Text &text()
{
	static const Text t;
	return t;
}

const QLocale &locale()
{
	static const QLocale l;
	return l;
}

QString num(double value, int precision)
{
	return locale().toString(value, 'f', precision);
}

QString count(uint64_t value)
{
	return locale().toString(static_cast<qulonglong>(value));
}

QString duration(double seconds)
{
	const auto total = static_cast<long long>(std::max(0.0, seconds));
	const long long h = total / 3600;
	const long long m = (total / 60) % 60;
	const long long s = total % 60;
	return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

QString percent(double p)
{
	if (p <= 0.0)
		return num(0.0, 1);
	return num(p, p < 0.1 ? 2 : 1);
}

double ratio(uint64_t part, uint64_t whole)
{
	return whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
}

void setBytes(Tile &t, double bytes)
{
	if (bytes >= kGiB) {
		t.value = num(bytes / kGiB, 2);
		t.unit = QStringLiteral("GB");
	} else {
		t.value = num(bytes / kMiB, 0);
		t.unit = QStringLiteral("MB");
	}
}

QString joinCompact(const QString &value, const QString &unit)
{
	if (unit.isEmpty())
		return value;
	if (unit == QLatin1String("%"))
		return value + unit;
	return value + QLatin1Char(' ') + unit;
}

/* When several metrics share the worst level, the headline names the one
 * that hurts the stream most. */
int priority(Metric m)
{
	switch (m) {
	case Metric::Dropped:
	case Metric::Outputs:
		return 5;
	case Metric::Missed:
	case Metric::Skipped:
	case Metric::FrameTime:
	case Metric::Disk:
		return 4;
	case Metric::Fps:
		return 3;
	case Metric::Cpu:
	case Metric::Gpu:
		return 2;
	default:
		return 1;
	}
}

float clampMeter(double v)
{
	return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

Tile makeTile(Metric m, const Snapshot &s, const Settings &cfg)
{
	const Text &t = text();
	Tile tile;
	tile.id = m;
	tile.label = metricLabel(m);

	switch (m) {
	case Metric::Cpu:
		tile.value = num(s.cpu, 1);
		tile.unit = QStringLiteral("%");
		tile.meter = clampMeter(s.cpu / 100.0);
		tile.level = cfg.level(m, s.cpu);
		tile.compact = num(s.cpu, 0) + tile.unit;
		break;

	case Metric::Gpu:
		if (std::isnan(s.gpu)) {
			tile.value = t.dash;
			tile.foot = t.unavailable;
			tile.compact = t.dash;
		} else {
			tile.value = num(s.gpu, 0);
			tile.unit = QStringLiteral("%");
			tile.meter = clampMeter(s.gpu / 100.0);
			tile.level = cfg.level(m, s.gpu);
			tile.compact = tile.value + tile.unit;
		}
		break;

	case Metric::Memory:
		tile.value = num(s.memoryBytes / kMiB, 0);
		tile.unit = QStringLiteral("MB");
		tile.foot = t.obsProcess;
		tile.compact = joinCompact(tile.value, tile.unit);
		break;

	case Metric::Disk:
		if (s.diskFreeBytes < 0) {
			tile.value = t.dash;
			tile.foot = t.unavailable;
			tile.compact = t.dash;
		} else {
			const double bytes = static_cast<double>(s.diskFreeBytes);
			const double gb = bytes / kGiB;
			if (gb >= 1.0) {
				tile.value = num(gb, gb >= 100.0 ? 0 : 1);
				tile.unit = QStringLiteral("GB");
			} else {
				tile.value = num(bytes / kMiB, 0);
				tile.unit = QStringLiteral("MB");
			}
			tile.level = cfg.level(m, gb);
			tile.compact = joinCompact(tile.value, tile.unit);
			if (s.recording && s.record.kbps > 0.0) {
				const double hours = bytes * 8.0 / (s.record.kbps * 1000.0) / 3600.0;
				tile.foot = hours >= 1.0 ? t.hoursLeft.arg(num(hours, 0))
							 : t.minutesLeft.arg(num(hours * 60.0, 0));
			} else {
				tile.foot = t.recordingDrive;
			}
		}
		break;

	case Metric::Session:
		tile.value = duration(s.sessionSec);
		tile.foot = t.sinceStart;
		tile.compact = tile.value;
		break;

	case Metric::Fps:
		tile.value = num(s.fps, 2);
		tile.foot = t.target.arg(QString::number(s.targetFps, 'g', 4));
		if (s.targetFps > 0.0 && s.fps < s.targetFps * 0.95)
			tile.level = Level::Warn;
		tile.compact = num(s.fps, 0) + QStringLiteral(" fps");
		break;

	case Metric::FrameTime: {
		tile.value = num(s.frameMs, 1);
		tile.unit = QStringLiteral("ms");
		const double budgetPct = s.budgetMs > 0.0 ? s.frameMs / s.budgetMs * 100.0 : 0.0;
		tile.meter = clampMeter(budgetPct / 100.0);
		tile.level = cfg.level(m, budgetPct);
		tile.compact = joinCompact(tile.value, tile.unit);
		break;
	}

	case Metric::Missed:
	case Metric::Skipped: {
		const bool missed = m == Metric::Missed;
		const uint64_t part = missed ? s.missed : s.skipped;
		const uint64_t whole = missed ? s.rendered : s.encoded;
		const double p = ratio(part, whole);
		tile.value = percent(p);
		tile.unit = QStringLiteral("%");
		tile.foot = t.ofTotal.arg(count(part), count(whole));
		tile.level = cfg.level(m, p);
		tile.compact = tile.value + tile.unit;
		break;
	}

	case Metric::StreamBitrate:
		tile.value = num(s.stream.kbps, 0);
		tile.unit = QStringLiteral("kb/s");
		if (s.stream.reconnecting)
			tile.foot = t.reconnecting;
		else if (cfg.trendLines)
			tile.history = s.streamHistory;
		else
			tile.foot = t.uploadRate;
		tile.compact = joinCompact(tile.value, tile.unit);
		break;

	case Metric::Dropped: {
		const double p = ratio(s.stream.dropped, s.stream.frames);
		tile.value = percent(p);
		tile.unit = QStringLiteral("%");
		tile.foot = t.ofTotal.arg(count(s.stream.dropped), count(s.stream.frames));
		tile.level = cfg.level(m, p);
		tile.compact = tile.value + tile.unit;
		break;
	}

	case Metric::Sent:
		setBytes(tile, static_cast<double>(s.stream.bytes));
		tile.foot = t.sinceStream;
		tile.compact = joinCompact(tile.value, tile.unit);
		break;

	case Metric::RecBitrate:
		tile.value = num(s.record.kbps, 0);
		tile.unit = QStringLiteral("kb/s");
		if (cfg.trendLines)
			tile.history = s.recordHistory;
		else
			tile.foot = t.writeRate;
		tile.compact = joinCompact(tile.value, tile.unit);
		break;

	case Metric::RecSize:
		setBytes(tile, static_cast<double>(s.record.bytes));
		tile.foot = t.currentFile;
		tile.compact = joinCompact(tile.value, tile.unit);
		break;

	case Metric::Outputs:
		break;
	}
	return tile;
}

OutputRow makeRow(const OutputStat &o, const Settings &cfg)
{
	const Text &t = text();
	OutputRow row;
	row.key = o.name;
	row.name = outputDisplayName(o, cfg);

	switch (o.kind) {
	case OutputKind::Stream: {
		row.kind = t.kindStream;
		const double p = ratio(o.dropped, o.frames);
		if (o.reconnecting) {
			row.state = t.reconnecting;
			row.right = t.retrying;
			row.level = Level::Crit;
		} else {
			row.state = duration(o.activeSec);
			row.right = percent(p) + QStringLiteral("%");
			row.level = cfg.level(Metric::Dropped, p);
		}
		break;
	}
	case OutputKind::Recording:
		row.kind = t.kindRecording;
		row.state = o.paused ? t.paused : duration(o.activeSec);
		row.right = t.on;
		break;
	case OutputKind::ReplayBuffer:
		row.state = duration(o.activeSec);
		row.right = t.on;
		break;
	case OutputKind::VirtualCam:
		if (o.width && o.height)
			row.state = QStringLiteral("%1×%2").arg(o.width).arg(o.height);
		row.right = t.on;
		break;
	case OutputKind::Other:
		row.state = duration(o.activeSec);
		row.right = t.on;
		break;
	}
	if (o.kbps > 0.0)
		row.rate = num(o.kbps, 0) + QStringLiteral(" kb/s");
	return row;
}

} // namespace

QString outputDefaultName(const std::string &name, OutputKind kind)
{
	if (kind == OutputKind::ReplayBuffer)
		return text().replayBuffer;
	if (kind == OutputKind::VirtualCam)
		return text().virtualCam;
	return QString::fromStdString(name);
}

QString outputDisplayName(const OutputStat &stat, const Settings &settings)
{
	const OutputPref *pref = settings.outputPref(stat.name);
	if (pref && !pref->alias.empty())
		return QString::fromStdString(pref->alias);
	return outputDefaultName(stat.name, stat.kind);
}

Display buildDisplay(const Snapshot &s, const Settings &cfg)
{
	const Text &t = text();
	Display d;
	d.groupHeaders = cfg.groupHeaders;
	d.comfortable = cfg.comfortable;
	d.colorAlways = cfg.colorAlways;
	d.sparkSamples = static_cast<size_t>(
		std::clamp(kSparkWindowMs / static_cast<double>(cfg.intervalMs), 8.0, double(kHistoryCapacity)));

	if (s.streaming) {
		d.liveAlert = s.stream.reconnecting;
		d.livePill = d.liveAlert ? t.reconnecting : t.live + QLatin1Char(' ') + duration(s.stream.activeSec);
	}
	if (s.recording) {
		d.recPaused = s.record.paused;
		d.recPill = d.recPaused ? t.paused : duration(s.record.activeSec);
	}
	if (!s.streaming && !s.recording)
		d.idleText = t.notLive;

	Level worst = Level::Ok;
	int worstPriority = 0;
	QString worstText;
	int issues = 0;
	auto consider = [&](Level level, int rank, const QString &why) {
		if (level == Level::Ok)
			return;
		++issues;
		if (level > worst || (level == worst && rank > worstPriority)) {
			worst = level;
			worstPriority = rank;
			worstText = why;
		}
	};

	if (s.streaming && s.stream.reconnecting)
		consider(Level::Crit, 6, t.streamReconnecting);

	bool anyOther = false;
	for (const OutputStat &o : s.others) {
		const OutputPref *pref = cfg.outputPref(o.name);
		if (!pref || !pref->hidden) {
			anyOther = true;
			break;
		}
	}

	for (Group g : kGroups) {
		const std::vector<Metric> &order = cfg.order[idx(g)];
		const bool anyShown =
			std::any_of(order.begin(), order.end(), [&cfg](Metric m) { return cfg.show[idx(m)]; });
		if (!anyShown)
			continue;

		bool active = true;
		if (g == Group::Stream)
			active = s.streaming;
		else if (g == Group::Record)
			active = s.recording;
		else if (g == Group::Outputs)
			active = anyOther;

		Block block;
		block.group = g;
		block.title = groupLabel(g);

		if (!active) {
			if (cfg.hideOffline)
				continue;
			block.offline = true;
			block.offlineText = g == Group::Outputs ? t.noneActive : t.offline;
			d.blocks.push_back(std::move(block));
			continue;
		}

		if (g == Group::Outputs) {
			int ok = 0;
			Level groupLevel = Level::Ok;
			for (const OutputStat &o : s.others) {
				const OutputPref *pref = cfg.outputPref(o.name);
				if (pref && pref->hidden)
					continue;
				OutputRow row = makeRow(o, cfg);
				if (row.level == Level::Ok)
					++ok;
				else if (o.reconnecting)
					consider(row.level, priority(Metric::Outputs),
						 t.outputReconnecting.arg(row.name));
				else
					consider(row.level, priority(Metric::Outputs),
						 t.outputDropped.arg(row.name, row.right.chopped(1)));
				groupLevel = std::max(groupLevel, row.level);
				block.rows.push_back(std::move(row));
			}
			block.meta = t.outputsOk.arg(ok).arg(static_cast<qulonglong>(block.rows.size()));
			if (cfg.strip[idx(Metric::Outputs)])
				d.chips.push_back({metricShortLabel(Metric::Outputs), block.meta, groupLevel});
		} else {
			for (Metric m : order) {
				if (!cfg.show[idx(m)])
					continue;
				Tile tile = makeTile(m, s, cfg);
				consider(tile.level, priority(m), tile.label + QLatin1Char(' ') + tile.compact);
				if (cfg.strip[idx(m)])
					d.chips.push_back({metricShortLabel(m), tile.compact, tile.level});
				block.tiles.push_back(std::move(tile));
			}
		}
		d.blocks.push_back(std::move(block));
	}

	d.health = worst;
	d.healthText = issues ? worstText : t.allGood;
	if (issues > 1)
		d.moreText = t.more.arg(issues - 1);
	return d;
}

} // namespace vitals
