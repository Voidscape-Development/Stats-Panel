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

#include "vitals-sampler.hpp"
#include "vitals-settings.hpp"

#include <QString>

#include <vector>

namespace vitals {

struct Tile {
	Metric id = Metric::Cpu;
	QString label;
	QString value;
	QString unit;
	QString foot;    /* small text under the value, used when there is no graphic */
	QString compact; /* value with unit, for the single-line layout and the health summary */
	Level level = Level::Ok;
	float meter = -1.0f;              /* 0..1, negative for no meter */
	const History *history = nullptr; /* sparkline, nullptr for none */
};

struct OutputRow {
	std::string key; /* OBS output name */
	QString name;
	QString kind;
	QString state;
	QString rate;
	QString right;
	Level level = Level::Ok;
};

struct Block {
	Group group = Group::System;
	QString title;
	QString meta;
	bool offline = false;
	QString offlineText;
	std::vector<Tile> tiles;
	std::vector<OutputRow> rows;
};

struct Chip {
	QString label;
	QString value;
	Level level = Level::Ok;
};

/* Everything the dock paints for one update, already formatted. */
struct Display {
	Level health = Level::Ok;
	QString healthText;
	QString moreText;

	QString livePill; /* empty when not streaming */
	bool liveAlert = false;
	QString recPill; /* empty when not recording */
	bool recPaused = false;
	QString idleText; /* shown when neither pill is */

	std::vector<Block> blocks;
	std::vector<Chip> chips;

	bool groupHeaders = true;
	bool comfortable = false;
	bool colorAlways = false;
	size_t sparkSamples = 30;
};

Display buildDisplay(const Snapshot &snapshot, const Settings &settings);

QString outputDisplayName(const OutputStat &stat, const Settings &settings);
QString outputDefaultName(const std::string &name, OutputKind kind);

} // namespace vitals
