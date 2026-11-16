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

#include "vitals-settings-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <set>

namespace vitals {

namespace {

constexpr int kMetricRole = Qt::UserRole;
constexpr int kOutputNameRole = Qt::UserRole;

enum TreeColumn { ColMetric = 0, ColShow = 1, ColStrip = 2, ColDescription = 3 };
enum OutputColumn { ColOutput = 0, ColAlias = 1, ColHidden = 2 };

constexpr std::array<Metric, 7> kThresholdMetrics = {
	Metric::Cpu, Metric::Gpu, Metric::FrameTime, Metric::Missed, Metric::Skipped, Metric::Dropped, Metric::Disk};

QLabel *hintLabel(const QString &text)
{
	auto *label = new QLabel(text);
	label->setWordWrap(true);
	QPalette pal = label->palette();
	pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
	label->setPalette(pal);
	return label;
}

void selectData(QComboBox *combo, const QVariant &value)
{
	const int index = combo->findData(value);
	combo->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace

SettingsDialog::SettingsDialog(const Settings &current, std::map<std::string, QString> knownOutputs, QWidget *parent)
	: QDialog(parent),
	  base_(current),
	  knownOutputs_(std::move(knownOutputs))
{
	setWindowTitle(loc("Settings.Title"));
	setMinimumSize(660, 480);

	nav_ = new QListWidget(this);
	nav_->addItem(loc("Settings.Page.Metrics"));
	nav_->addItem(loc("Settings.Page.Alerts"));
	nav_->addItem(loc("Settings.Page.Display"));
	nav_->addItem(loc("Settings.Page.Advanced"));
	nav_->setFixedWidth(150);

	pages_ = new QStackedWidget(this);
	pages_->addWidget(buildMetricsPage());
	pages_->addWidget(buildAlertsPage());
	pages_->addWidget(buildDisplayPage());
	pages_->addWidget(buildAdvancedPage());
	connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
	nav_->setCurrentRow(0);

	auto *buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
	buttons->button(QDialogButtonBox::RestoreDefaults)->setText(loc("Settings.RestoreDefaults"));
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
		/* Output names are the user's own labels, so they survive a reset. */
		Settings defaults = Settings::defaults();
		defaults.outputs = result().outputs;
		load(defaults);
	});

	auto *main = new QHBoxLayout;
	main->addWidget(nav_);
	main->addWidget(pages_, 1);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(main, 1);
	layout->addWidget(buttons);

	load(base_);
}

QWidget *SettingsDialog::buildMetricsPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(hintLabel(loc("Settings.Metrics.Intro")));

	tree_ = new QTreeWidget(page);
	tree_->setColumnCount(4);
	tree_->setHeaderLabels({loc("Settings.Metrics.Metric"), loc("Settings.Metrics.Show"),
				loc("Settings.Metrics.Strip"), loc("Settings.Metrics.Description")});
	tree_->setRootIsDecorated(false);
	tree_->setItemsExpandable(false);
	tree_->setSelectionMode(QAbstractItemView::SingleSelection);
	tree_->header()->setStretchLastSection(true);
	tree_->header()->setSectionResizeMode(ColMetric, QHeaderView::ResizeToContents);
	tree_->header()->setSectionResizeMode(ColShow, QHeaderView::ResizeToContents);
	tree_->header()->setSectionResizeMode(ColStrip, QHeaderView::ResizeToContents);
	layout->addWidget(tree_, 1);

	auto *buttons = new QHBoxLayout;
	up_ = new QPushButton(loc("Settings.Metrics.MoveUp"), page);
	down_ = new QPushButton(loc("Settings.Metrics.MoveDown"), page);
	buttons->addWidget(up_);
	buttons->addWidget(down_);
	buttons->addStretch(1);
	layout->addLayout(buttons);

	connect(up_, &QPushButton::clicked, this, [this] { moveSelected(-1); });
	connect(down_, &QPushButton::clicked, this, [this] { moveSelected(1); });
	connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { updateMoveButtons(); });
	return page;
}

QWidget *SettingsDialog::buildAlertsPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(hintLabel(loc("Settings.Alerts.Intro")));

	tint_ = new QCheckBox(loc("Settings.Alerts.Tint"), page);
	layout->addWidget(tint_);

	auto *grid = new QGridLayout;
	grid->addWidget(new QLabel(loc("Settings.Metrics.Metric")), 0, 0);
	grid->addWidget(new QLabel(loc("Settings.Alerts.Warning")), 0, 1);
	grid->addWidget(new QLabel(loc("Settings.Alerts.Critical")), 0, 2);

	int row = 1;
	for (Metric m : kThresholdMetrics) {
		const size_t i = idx(m);
		const bool percentOfFrames = m == Metric::Missed || m == Metric::Skipped || m == Metric::Dropped;
		const QString name = m == Metric::Disk ? loc("Settings.Alerts.DiskFree") : metricLabel(m);
		const QString unit = m == Metric::Disk        ? QStringLiteral("GB")
				     : m == Metric::FrameTime ? loc("Settings.Alerts.PercentBudget")
							      : QStringLiteral("%");

		auto makeSpin = [&] {
			auto *spin = new QDoubleSpinBox(page);
			spin->setDecimals(percentOfFrames ? 1 : 0);
			spin->setSingleStep(percentOfFrames ? 0.1 : 1.0);
			spin->setRange(0.0, m == Metric::Disk ? 10000.0 : m == Metric::FrameTime ? 500.0 : 100.0);
			spin->setSuffix(QLatin1Char(' ') + unit);
			return spin;
		};
		warn_[i] = makeSpin();
		crit_[i] = makeSpin();

		auto *label = new QLabel(name, page);
		label->setToolTip(metricHint(m));
		grid->addWidget(label, row, 0);
		grid->addWidget(warn_[i], row, 1);
		grid->addWidget(crit_[i], row, 2);
		row++;
	}
	grid->setColumnStretch(0, 1);
	layout->addLayout(grid);
	layout->addStretch(1);
	return page;
}

QWidget *SettingsDialog::buildDisplayPage()
{
	auto *page = new QWidget;
	auto *form = new QFormLayout(page);
	form->setContentsMargins(0, 0, 0, 0);

	interval_ = new QComboBox(page);
	interval_->addItem(QStringLiteral("250 ms"), 250);
	interval_->addItem(QStringLiteral("500 ms"), 500);
	interval_->addItem(QStringLiteral("1 s"), 1000);
	interval_->addItem(QStringLiteral("2 s"), 2000);
	form->addRow(loc("Settings.Display.Interval"), interval_);
	form->addRow(QString(), hintLabel(loc("Settings.Display.IntervalHint")));

	density_ = new QComboBox(page);
	density_->addItem(loc("Settings.Display.Compact"), false);
	density_->addItem(loc("Settings.Display.Comfortable"), true);
	form->addRow(loc("Settings.Display.Density"), density_);

	colors_ = new QComboBox(page);
	colors_->addItem(loc("Settings.Display.ColorsAttention"), false);
	colors_->addItem(loc("Settings.Display.ColorsAlways"), true);
	form->addRow(loc("Settings.Display.Colors"), colors_);
	form->addRow(QString(), hintLabel(loc("Settings.Display.ColorsHint")));

	headings_ = new QCheckBox(loc("Settings.Display.GroupHeadings"), page);
	form->addRow(QString(), headings_);
	trend_ = new QCheckBox(loc("Settings.Display.Trend"), page);
	form->addRow(QString(), trend_);
	return page;
}

QWidget *SettingsDialog::buildAdvancedPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);

	auto *form = new QFormLayout;
	offline_ = new QComboBox(page);
	offline_->addItem(loc("Settings.Advanced.OfflineShow"), false);
	offline_->addItem(loc("Settings.Advanced.OfflineHide"), true);
	form->addRow(loc("Settings.Advanced.Offline"), offline_);

	gpu_ = new QComboBox(page);
	gpu_->addItem(loc("Settings.Advanced.GpuAuto"), static_cast<int>(GpuSource::Auto));
	gpu_->addItem(loc("Settings.Advanced.GpuPdh"), static_cast<int>(GpuSource::Pdh));
	gpu_->addItem(loc("Settings.Advanced.GpuNvml"), static_cast<int>(GpuSource::Nvml));
	gpu_->addItem(loc("Settings.Advanced.GpuOff"), static_cast<int>(GpuSource::Off));
	form->addRow(loc("Settings.Advanced.Gpu"), gpu_);
	form->addRow(QString(), hintLabel(loc("Settings.Advanced.GpuHint")));
	layout->addLayout(form);

	resetOnStart_ = new QCheckBox(loc("Settings.Advanced.ResetOnStart"), page);
	layout->addWidget(resetOnStart_);

	auto *outputsTitle = new QLabel(loc("Settings.Advanced.Outputs"), page);
	QFont bold = outputsTitle->font();
	bold.setBold(true);
	outputsTitle->setFont(bold);
	layout->addSpacing(8);
	layout->addWidget(outputsTitle);
	layout->addWidget(hintLabel(loc("Settings.Advanced.OutputsHint")));

	outputs_ = new QTableWidget(0, 3, page);
	outputs_->setHorizontalHeaderLabels({loc("Settings.Advanced.OutputName"), loc("Settings.Advanced.OutputAlias"),
					     loc("Settings.Advanced.OutputHidden")});
	outputs_->verticalHeader()->setVisible(false);
	outputs_->horizontalHeader()->setSectionResizeMode(ColOutput, QHeaderView::Stretch);
	outputs_->horizontalHeader()->setSectionResizeMode(ColAlias, QHeaderView::Stretch);
	outputs_->horizontalHeader()->setSectionResizeMode(ColHidden, QHeaderView::ResizeToContents);
	outputs_->setSelectionMode(QAbstractItemView::NoSelection);
	layout->addWidget(outputs_, 1);
	return page;
}

void SettingsDialog::load(const Settings &s)
{
	/* Metrics */
	tree_->clear();
	for (Group g : kGroups) {
		auto *group = new QTreeWidgetItem(tree_);
		group->setText(ColMetric, groupLabel(g));
		group->setFlags(Qt::ItemIsEnabled);
		QFont bold = group->font(ColMetric);
		bold.setBold(true);
		group->setFont(ColMetric, bold);
		group->setFirstColumnSpanned(true);

		for (Metric m : s.order[idx(g)]) {
			auto *item = new QTreeWidgetItem(group);
			item->setText(ColMetric, metricLabel(m));
			item->setData(ColMetric, kMetricRole, static_cast<int>(m));
			item->setText(ColDescription, metricHint(m));
			item->setToolTip(ColDescription, metricHint(m));
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
			item->setCheckState(ColShow, s.show[idx(m)] ? Qt::Checked : Qt::Unchecked);
			item->setCheckState(ColStrip, s.strip[idx(m)] ? Qt::Checked : Qt::Unchecked);
		}
	}
	tree_->expandAll();
	updateMoveButtons();

	/* Alerts */
	tint_->setChecked(s.tintTitle);
	for (Metric m : kThresholdMetrics) {
		warn_[idx(m)]->setValue(s.warn[idx(m)]);
		crit_[idx(m)]->setValue(s.crit[idx(m)]);
	}

	/* Display */
	selectData(interval_, s.intervalMs);
	selectData(density_, s.comfortable);
	selectData(colors_, s.colorAlways);
	headings_->setChecked(s.groupHeaders);
	trend_->setChecked(s.trendLines);

	/* Advanced */
	selectData(offline_, s.hideOffline);
	selectData(gpu_, static_cast<int>(s.gpuSource));
	resetOnStart_->setChecked(s.resetOnStreamStart);

	std::set<std::string> names;
	for (const auto &entry : knownOutputs_)
		names.insert(entry.first);
	for (const auto &entry : s.outputs)
		names.insert(entry.first);

	outputs_->setRowCount(0);
	for (const std::string &name : names) {
		const int row = outputs_->rowCount();
		outputs_->insertRow(row);

		auto known = knownOutputs_.find(name);
		const QString obsName = QString::fromStdString(name);
		const QString shown = known != knownOutputs_.end() ? known->second : obsName;
		auto *nameItem = new QTableWidgetItem(shown);
		nameItem->setData(kOutputNameRole, obsName);
		nameItem->setToolTip(loc("Output.Tooltip").arg(obsName));
		nameItem->setFlags(Qt::ItemIsEnabled);
		outputs_->setItem(row, ColOutput, nameItem);

		const OutputPref *pref = s.outputPref(name);
		auto *aliasItem = new QTableWidgetItem(pref ? QString::fromStdString(pref->alias) : QString());
		aliasItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsEditable);
		outputs_->setItem(row, ColAlias, aliasItem);

		auto *hiddenItem = new QTableWidgetItem;
		hiddenItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		hiddenItem->setCheckState(pref && pref->hidden ? Qt::Checked : Qt::Unchecked);
		outputs_->setItem(row, ColHidden, hiddenItem);
	}
}

Settings SettingsDialog::result() const
{
	Settings out = base_;

	for (int g = 0; g < tree_->topLevelItemCount() && g < static_cast<int>(kGroupCount); g++) {
		QTreeWidgetItem *group = tree_->topLevelItem(g);
		std::vector<Metric> order;
		for (int c = 0; c < group->childCount(); c++) {
			QTreeWidgetItem *item = group->child(c);
			const auto m = static_cast<Metric>(item->data(ColMetric, kMetricRole).toInt());
			order.push_back(m);
			out.show[idx(m)] = item->checkState(ColShow) == Qt::Checked;
			out.strip[idx(m)] = item->checkState(ColStrip) == Qt::Checked;
		}
		out.order[static_cast<size_t>(g)] = std::move(order);
	}

	out.tintTitle = tint_->isChecked();
	for (Metric m : kThresholdMetrics) {
		out.warn[idx(m)] = warn_[idx(m)]->value();
		out.crit[idx(m)] = crit_[idx(m)]->value();
	}

	out.intervalMs = interval_->currentData().toInt();
	out.comfortable = density_->currentData().toBool();
	out.colorAlways = colors_->currentData().toBool();
	out.groupHeaders = headings_->isChecked();
	out.trendLines = trend_->isChecked();

	out.hideOffline = offline_->currentData().toBool();
	out.gpuSource = static_cast<GpuSource>(gpu_->currentData().toInt());
	out.resetOnStreamStart = resetOnStart_->isChecked();

	out.outputs.clear();
	for (int row = 0; row < outputs_->rowCount(); row++) {
		const std::string name = outputs_->item(row, ColOutput)->data(kOutputNameRole).toString().toStdString();
		OutputPref pref;
		pref.alias = outputs_->item(row, ColAlias)->text().trimmed().toStdString();
		pref.hidden = outputs_->item(row, ColHidden)->checkState() == Qt::Checked;
		if (!pref.alias.empty() || pref.hidden)
			out.outputs[name] = std::move(pref);
	}
	return out;
}

void SettingsDialog::moveSelected(int delta)
{
	QTreeWidgetItem *item = tree_->currentItem();
	QTreeWidgetItem *parent = item ? item->parent() : nullptr;
	if (!parent)
		return;
	const int from = parent->indexOfChild(item);
	const int to = from + delta;
	if (to < 0 || to >= parent->childCount())
		return;
	parent->takeChild(from);
	parent->insertChild(to, item);
	tree_->setCurrentItem(item);
	updateMoveButtons();
}

void SettingsDialog::updateMoveButtons()
{
	QTreeWidgetItem *item = tree_->currentItem();
	QTreeWidgetItem *parent = item ? item->parent() : nullptr;
	const int index = parent ? parent->indexOfChild(item) : -1;
	up_->setEnabled(parent && index > 0);
	down_->setEnabled(parent && index >= 0 && index < parent->childCount() - 1);
}

} // namespace vitals
