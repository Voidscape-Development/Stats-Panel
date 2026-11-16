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
#include "vitals-worker.hpp"

#include <obs-frontend-api.h>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <map>
#include <string>

class QDockWidget;

namespace vitals {

class SettingsDialog;
class VitalsView;

/* Owns the dock and drives it: one timer, running only while the dock is
 * visible, samples libobs and pushes a formatted display to the view. */
class Controller : public QObject {
	Q_OBJECT

public:
	Controller();
	~Controller() override;

	void handleFrontendEvent(enum obs_frontend_event event);

private:
	void tick();
	void rescan();
	void refreshDiskPath();
	void setRunning(bool running);
	void openSettings();
	void applySettings(const Settings &settings);
	void resetCounters();
	void updateTitleTint(Level level);
	void shutdown();
	QDockWidget *dock() const;

	Settings settings_;
	Sampler sampler_;
	Worker worker_;
	QPointer<VitalsView> view_;
	QPointer<SettingsDialog> dialog_;
	QTimer timer_;
	uint64_t lastRescanNs_ = 0;
	uint64_t lastPathNs_ = 0;
	Level tint_ = Level::Ok;
	bool running_ = false;
	bool shutDown_ = false;

	/* Outputs seen this session, for the settings dialog. */
	std::map<std::string, QString> knownOutputs_;
};

} // namespace vitals
