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

#include <obs.h>
#include <util/platform.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace vitals {

enum class OutputKind : uint8_t { Stream, Recording, ReplayBuffer, VirtualCam, Other };

struct OutputStat {
	std::string name; /* OBS output name, key for user preferences */
	OutputKind kind = OutputKind::Other;
	bool reconnecting = false;
	bool paused = false;
	double kbps = 0.0;
	uint64_t bytes = 0;
	uint64_t dropped = 0;
	uint64_t frames = 0;
	double activeSec = 0.0;
	uint32_t width = 0;
	uint32_t height = 0;
};

struct Snapshot {
	double cpu = 0.0;
	double gpu = NAN; /* NaN when unavailable */
	double memoryBytes = 0.0;
	int64_t diskFreeBytes = -1; /* -1 when unknown */
	double sessionSec = 0.0;

	double fps = 0.0;
	double targetFps = 0.0;
	double frameMs = 0.0;
	double budgetMs = 0.0;
	uint64_t missed = 0;
	uint64_t rendered = 0;
	uint64_t skipped = 0;
	uint64_t encoded = 0;

	bool streaming = false;
	bool recording = false;
	OutputStat stream;
	OutputStat record;
	const History *streamHistory = nullptr;
	const History *recordHistory = nullptr;

	std::vector<OutputStat> others;
};

/* Reads statistics from libobs. Must only be used on the UI thread.
 * Every call here is a cheap getter, so it runs on the dock's timer. */
class Sampler {
public:
	Sampler();
	~Sampler();
	Sampler(const Sampler &) = delete;
	Sampler &operator=(const Sampler &) = delete;

	/* Re-enumerates active outputs. Call on frontend output events and every
	 * couple of seconds to catch outputs created by other plugins. */
	void rescanOutputs();

	void sample(double gpuPercent, int64_t diskFreeBytes);

	/* Zeroes missed, skipped and dropped frame counters. */
	void resetCounters();
	/* Zeroes only the render and encoder counters. */
	void resetRenderCounters();

	/* Drops every output reference. Call before OBS shuts down. */
	void clear();

	const Snapshot &snapshot() const { return snap_; }

private:
	struct Tracked {
		~Tracked();
		obs_weak_output_t *weak = nullptr;
		const void *identity = nullptr; /* compared only, never dereferenced */
		bool main = false;
		bool seen = false;
		uint64_t startNs = 0;
		uint64_t lastBytes = 0;
		uint64_t lastNs = 0;
		uint64_t pausedNs = 0;
		uint64_t pauseStartNs = 0;
		int droppedBase = 0;
		int framesBase = 0;
		bool rebase = false;
		History history;
		OutputStat stat;
	};

	void sampleOutput(Tracked &t, uint64_t now);

	os_cpu_usage_info_t *cpuInfo_ = nullptr;
	uint64_t startNs_ = 0;
	uint32_t firstRendered_ = 0;
	uint32_t firstLagged_ = 0;
	uint32_t firstEncoded_ = 0;
	uint32_t firstSkipped_ = 0;
	bool rebaseRender_ = true;

	std::vector<std::unique_ptr<Tracked>> tracked_;
	Snapshot snap_;
};

} // namespace vitals
