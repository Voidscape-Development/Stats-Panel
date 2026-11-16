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

#include "vitals-controller.hpp"
#include "vitals-display.hpp"
#include "vitals-settings-dialog.hpp"
#include "vitals-view.hpp"

#include <plugin-support.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QAction>
#include <QDockWidget>
#include <QWidget>

namespace vitals {

namespace {
constexpr const char *kDockId = "vitals-dock";
constexpr uint64_t kRescanNs = 2000000000ULL;
constexpr uint64_t kDiskPathNs = 10000000000ULL;
} // namespace

Controller::Controller() : settings_(Settings::load())
{
	auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());

	view_ = new VitalsView(mainWindow);
	const QByteArray title = loc("Vitals.Title").toUtf8();
	if (!obs_frontend_add_dock_by_id(kDockId, title.constData(), view_)) {
		obs_log(LOG_WARNING, "could not add the dock (id \"%s\" already in use?)", kDockId);
		delete view_;
	}

	const QByteArray menuText = loc("Vitals.Menu.Settings").toUtf8();
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(menuText.constData()));
	if (action)
		connect(action, &QAction::triggered, this, &Controller::openSettings);

	timer_.setTimerType(Qt::CoarseTimer);
	timer_.setInterval(settings_.intervalMs);
	connect(&timer_, &QTimer::timeout, this, &Controller::tick);

	worker_.setGpuSource(settings_.gpuSource);
	worker_.start();

	if (view_) {
		connect(view_, &VitalsView::visibilityChanged, this, &Controller::setRunning);
		connect(view_, &VitalsView::settingsRequested, this, &Controller::openSettings);
		connect(view_, &VitalsView::resetRequested, this, &Controller::resetCounters);
		if (view_->isVisible())
			setRunning(true);
	}
}

Controller::~Controller()
{
	shutdown();
}

void Controller::shutdown()
{
	if (shutDown_)
		return;
	shutDown_ = true;
	running_ = false;
	timer_.stop();
	worker_.stop();
	sampler_.clear();
	if (dialog_)
		dialog_->close();
}

void Controller::handleFrontendEvent(enum obs_frontend_event event)
{
	if (shutDown_)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		if (settings_.resetOnStreamStart)
			sampler_.resetRenderCounters();
		[[fallthrough]];
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STARTED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
		if (event == OBS_FRONTEND_EVENT_RECORDING_STARTED)
			refreshDiskPath();
		/* Show the change now instead of on the next timer tick. */
		if (running_) {
			lastRescanNs_ = 0;
			tick();
		}
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		refreshDiskPath();
		break;
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
	case OBS_FRONTEND_EVENT_EXIT:
		shutdown();
		break;
	default:
		break;
	}
}

void Controller::setRunning(bool running)
{
	if (shutDown_ || running == running_)
		return;
	running_ = running;
	worker_.setActive(running);
	if (running) {
		lastRescanNs_ = 0;
		timer_.start();
		tick();
	} else {
		timer_.stop();
	}
}

void Controller::rescan()
{
	sampler_.rescanOutputs();
	lastRescanNs_ = os_gettime_ns();
}

void Controller::refreshDiskPath()
{
	char *path = obs_frontend_get_current_record_output_path();
	worker_.setDiskPath(path ? path : "");
	bfree(path);
	lastPathNs_ = os_gettime_ns();
}

void Controller::tick()
{
	if (shutDown_ || !view_)
		return;

	/* Nothing is visible while OBS is minimized. */
	const QWidget *window = view_->window();
	if (window && window->isMinimized())
		return;

	const uint64_t now = os_gettime_ns();
	const bool rescanned = now - lastRescanNs_ >= kRescanNs;
	if (rescanned)
		rescan();
	if (now - lastPathNs_ >= kDiskPathNs)
		refreshDiskPath();

	sampler_.sample(worker_.gpuPercent(), worker_.diskFreeBytes());
	const Snapshot &snapshot = sampler_.snapshot();

	if (rescanned) {
		for (const OutputStat &o : snapshot.others)
			knownOutputs_[o.name] = outputDefaultName(o.name, o.kind);
	}

	Display display = buildDisplay(snapshot, settings_);
	updateTitleTint(settings_.tintTitle ? display.health : Level::Ok);
	view_->setDisplay(std::move(display));
}

void Controller::resetCounters()
{
	sampler_.resetCounters();
	if (running_)
		tick();
}

void Controller::openSettings()
{
	if (shutDown_)
		return;
	if (dialog_) {
		dialog_->raise();
		dialog_->activateWindow();
		return;
	}

	auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
	auto *dialog = new SettingsDialog(settings_, knownOutputs_, parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	connect(dialog, &QDialog::accepted, this, [this, dialog] { applySettings(dialog->result()); });
	dialog_ = dialog;
	dialog->show();
}

void Controller::applySettings(const Settings &settings)
{
	settings_ = settings;
	settings_.save();
	timer_.setInterval(settings_.intervalMs);
	worker_.setGpuSource(settings_.gpuSource);
	if (running_)
		tick();
}

QDockWidget *Controller::dock() const
{
	for (QWidget *w = view_ ? view_->parentWidget() : nullptr; w; w = w->parentWidget()) {
		if (auto *dockWidget = qobject_cast<QDockWidget *>(w))
			return dockWidget;
	}
	return nullptr;
}

void Controller::updateTitleTint(Level level)
{
	if (level == tint_ || !view_)
		return;
	QDockWidget *dockWidget = dock();
	if (!dockWidget)
		return;

	/* Restyling is comparatively expensive, so it only happens when the
	 * alert level changes, never on an ordinary tick. */
	tint_ = level;
	if (level == Level::Ok) {
		dockWidget->setStyleSheet(QString());
		return;
	}
	const QColor color = view_->titleTint(level);
	dockWidget->setStyleSheet(QStringLiteral("QDockWidget::title { background-color: %1; }").arg(color.name()));
}

} // namespace vitals
