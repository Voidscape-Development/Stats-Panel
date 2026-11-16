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

#include "vitals-settings.hpp"

#include <QDialog>

#include <array>
#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;

namespace vitals {

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	/* knownOutputs maps OBS output names seen this session to the name the
	 * dock would show for them by default. */
	SettingsDialog(const Settings &current, std::map<std::string, QString> knownOutputs, QWidget *parent);

	Settings result() const;

private:
	QWidget *buildMetricsPage();
	QWidget *buildAlertsPage();
	QWidget *buildDisplayPage();
	QWidget *buildAdvancedPage();

	void load(const Settings &s);
	void moveSelected(int delta);
	void updateMoveButtons();

	Settings base_;
	std::map<std::string, QString> knownOutputs_;

	QListWidget *nav_ = nullptr;
	QStackedWidget *pages_ = nullptr;

	QTreeWidget *tree_ = nullptr;
	QPushButton *up_ = nullptr;
	QPushButton *down_ = nullptr;

	QCheckBox *tint_ = nullptr;
	std::array<QDoubleSpinBox *, kMetricCount> warn_{};
	std::array<QDoubleSpinBox *, kMetricCount> crit_{};

	QComboBox *interval_ = nullptr;
	QComboBox *density_ = nullptr;
	QComboBox *colors_ = nullptr;
	QCheckBox *headings_ = nullptr;
	QCheckBox *trend_ = nullptr;

	QComboBox *offline_ = nullptr;
	QCheckBox *resetOnStart_ = nullptr;
	QComboBox *gpu_ = nullptr;
	QTableWidget *outputs_ = nullptr;
};

} // namespace vitals
