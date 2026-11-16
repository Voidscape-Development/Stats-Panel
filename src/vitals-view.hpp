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

#include "vitals-display.hpp"

#include <QAbstractScrollArea>
#include <QColor>
#include <QFont>
#include <QRect>

#include <vector>

class QPainter;

namespace vitals {

/* The dock body. One custom-painted widget instead of dozens of labels:
 * no layout passes on update, and only tiles whose text changed are
 * repainted. The layout follows the widget's own size:
 *   130 px tall or less  -> single line with the pinned metrics
 *   230 px wide or less  -> compact list, one row per metric
 *   otherwise            -> grouped tile grid */
class VitalsView : public QAbstractScrollArea {
	Q_OBJECT

public:
	explicit VitalsView(QWidget *parent = nullptr);

	void setDisplay(Display &&display);

	/* Title bar color for an alert level, blended into the current theme. */
	QColor titleTint(Level level) const;

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	void settingsRequested();
	void resetRequested();
	void visibilityChanged(bool visible);

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void scrollContentsBy(int dx, int dy) override;
	bool viewportEvent(QEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	enum class Mode { Grid, List, Strip };
	enum class Button { None, Reset, Settings };

	struct BlockGeom {
		QRect title;
		QRect offline;
		std::vector<QRect> tiles;
		std::vector<QRect> rows;
	};

	struct Colors {
		QColor bg, fg, muted, surface, line, accent, ok, warn, crit, live;
		bool light = false;
	};

	struct Fonts {
		QFont base, small, foot, caps, value, listValue, pill;
		int baseH = 0, smallH = 0, footH = 0, capsH = 0, valueH = 0, valueAscent = 0, listValueH = 0, pillH = 0;
	};

	void updateStyle();
	void relayout();
	void relayoutNow();
	bool sameStructure(const Display &a, const Display &b) const;
	int scrollOffset() const;
	Button buttonAt(const QPoint &viewportPos) const;
	QRect contentToViewport(const QRect &r) const;
	void updateAccessibility();

	QColor levelColor(Level level) const;
	int tileHeight(const Tile &tile) const;
	int rowHeight() const;

	void paintHeader(QPainter &p) const;
	void paintStrip(QPainter &p) const;
	void paintTile(QPainter &p, const QRect &r, const Tile &t) const;
	void paintFoot(QPainter &p, const QRect &r, const Tile &t) const;
	void paintRow(QPainter &p, const QRect &r, const OutputRow &row) const;
	void paintOffline(QPainter &p, const QRect &r, const Block &b) const;
	void paintGroupTitle(QPainter &p, const QRect &r, const Block &b) const;
	int paintPills(QPainter &p, int x, int centerY, int limit) const;
	void paintButton(QPainter &p, const QRect &r, Button which) const;

	Display d_;
	bool hasDisplay_ = false;
	Mode mode_ = Mode::Grid;
	Colors c_;
	Fonts f_;

	QRect header_;
	QRect health_;
	QRect resetBtn_;
	QRect settingsBtn_;
	std::vector<BlockGeom> geom_;
	int contentHeight_ = 0;
	Button hover_ = Button::None;
	bool inRelayout_ = false;
};

} // namespace vitals
