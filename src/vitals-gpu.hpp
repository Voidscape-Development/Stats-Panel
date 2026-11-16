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

#include <memory>

namespace vitals {

/* Reports whole-GPU utilization in percent. Created, used and destroyed on
 * the worker thread only. */
class GpuMonitor {
public:
	virtual ~GpuMonitor() = default;

	/* Returns NaN when no value is available yet. */
	virtual double sample() = 0;
	virtual const char *name() const = 0;

	/* Returns nullptr when the source is off or unavailable on this system. */
	static std::unique_ptr<GpuMonitor> create(GpuSource source);
};

} // namespace vitals
