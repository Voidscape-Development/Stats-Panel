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

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace vitals {

/* Samples the slow sources (GPU counters, free disk space) off the UI thread.
 * GPU is sampled every second and disk space every five seconds, only while
 * the dock is visible. Results are read lock-free by the UI thread. */
class Worker {
public:
	Worker() = default;
	~Worker();
	Worker(const Worker &) = delete;
	Worker &operator=(const Worker &) = delete;

	void start();
	void stop();

	void setActive(bool active);
	void setGpuSource(GpuSource source);
	void setDiskPath(const std::string &path);

	double gpuPercent() const { return gpu_.load(std::memory_order_relaxed); }
	int64_t diskFreeBytes() const { return disk_.load(std::memory_order_relaxed); }

private:
	void run();

	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool stop_ = false;
	bool active_ = false;
	GpuSource source_ = GpuSource::Auto;
	bool sourceChanged_ = true;
	std::string diskPath_;
	bool diskChanged_ = false;

	std::atomic<double> gpu_{NAN};
	std::atomic<int64_t> disk_{-1};
};

} // namespace vitals
