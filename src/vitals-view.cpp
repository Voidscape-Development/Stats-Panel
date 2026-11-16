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

#include "vitals-view.hpp"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QScrollBar>
#include <QToolTip>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace vitals {

namespace {

constexpr int kPad = 8;
constexpr int kButton = 22;
constexpr int kStripMaxHeight = 130;
constexpr int kListMaxWidth = 230;
constexpr int kMinTileWidth = 112;
constexpr int kGroupGap = 10;
constexpr int kPillHeight = 20;
constexpr int kRowValueWidth = 56;
constexpr int kRowRateWidth = 76;
constexpr double kPi = 3.14159265358979323846;

QColor mix(const QColor &a, const QColor &b, float t)
{
	return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
				a.blueF() + (b.blueF() - a.blueF()) * t, 1.0f);
}

QColor withAlpha(QColor c, float alpha)
{
	c.setAlphaF(alpha);
	return c;
}

QFont scaled(const QFont &base, double factor, QFont::Weight weight)
{
	QFont f = base;
	if (base.pointSizeF() > 0.0)
		f.setPointSizeF(base.pointSizeF() * factor);
	else if (base.pixelSize() > 0)
		f.setPixelSize(std::max(1, static_cast<int>(std::lround(base.pixelSize() * factor))));
	f.setWeight(weight);
	return f;
}

void useTabularDigits(QFont &f)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	f.setFeature(QFont::Tag("tnum"), 1);
#else
	Q_UNUSED(f);
#endif
}

bool sameTile(const Tile &a, const Tile &b)
{
	/* A sparkline moves on every sample, so its tile always repaints. */
	if (a.history || b.history)
		return false;
	return a.level == b.level && std::fabs(a.meter - b.meter) < 0.004f && a.value == b.value && a.unit == b.unit &&
	       a.foot == b.foot && a.label == b.label;
}

bool sameRow(const OutputRow &a, const OutputRow &b)
{
	return a.level == b.level && a.right == b.right && a.state == b.state && a.rate == b.rate && a.name == b.name &&
	       a.kind == b.kind;
}

bool sameChips(const std::vector<Chip> &a, const std::vector<Chip> &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (a[i].level != b[i].level || a[i].value != b[i].value || a[i].label != b[i].label)
			return false;
	}
	return true;
}

} // namespace

VitalsView::VitalsView(QWidget *parent) : QAbstractScrollArea(parent)
{
	setObjectName(QStringLiteral("VitalsView"));
	setFrameShape(QFrame::NoFrame);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	verticalScrollBar()->setSingleStep(20);
	viewport()->setMouseTracking(true);
	viewport()->setAutoFillBackground(false);
	setAccessibleName(loc("Vitals.Title"));
	updateStyle();
}

QSize VitalsView::sizeHint() const
{
	return QSize(280, 420);
}

QSize VitalsView::minimumSizeHint() const
{
	return QSize(150, 40);
}

/* ---------------------------------------------------------------------- */
/* Style                                                                    */

void VitalsView::updateStyle()
{
	/* OBS themes set the application palette, so these follow the theme. */
	const QPalette &pal = palette();
	c_.bg = pal.color(QPalette::Window);
	c_.fg = pal.color(QPalette::WindowText);
	c_.light = c_.bg.lightnessF() > 0.5f;
	/* Theme highlights are tuned for selection backgrounds, not thin lines,
	 * so nudge the accent toward readable contrast against the dock. */
	c_.accent = pal.color(QPalette::Highlight);
	if (!c_.light && c_.accent.lightnessF() < 0.6f)
		c_.accent = mix(c_.accent, QColor(255, 255, 255), 0.4f);
	else if (c_.light && c_.accent.lightnessF() > 0.45f)
		c_.accent = mix(c_.accent, QColor(0, 0, 0), 0.35f);
	c_.muted = mix(c_.fg, c_.bg, 0.42f);
	c_.surface = c_.light ? mix(c_.bg, QColor(255, 255, 255), 0.75f) : mix(c_.bg, c_.fg, 0.06f);
	c_.line = mix(c_.bg, c_.fg, c_.light ? 0.16f : 0.14f);

	/* Status colors are fixed, with a darker set for light themes so they
	 * keep enough contrast. */
	if (c_.light) {
		c_.ok = QColor(0x1f, 0x88, 0x3d);
		c_.warn = QColor(0xa9, 0x65, 0x00);
		c_.crit = QColor(0xcf, 0x22, 0x2e);
		c_.live = QColor(0xd1, 0x24, 0x2f);
	} else {
		c_.ok = QColor(0x3f, 0xb9, 0x50);
		c_.warn = QColor(0xe8, 0xa9, 0x0f);
		c_.crit = QColor(0xf2, 0x58, 0x4d);
		c_.live = QColor(0xe5, 0x48, 0x4d);
	}

	f_.base = font();
	f_.small = scaled(f_.base, 0.92, QFont::Normal);
	f_.foot = scaled(f_.base, 0.86, QFont::Normal);
	useTabularDigits(f_.foot);
	f_.caps = scaled(f_.base, 0.84, QFont::DemiBold);
	f_.caps.setCapitalization(QFont::AllUppercase);
	f_.caps.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
	f_.value = scaled(f_.base, d_.comfortable ? 1.7 : 1.45, QFont::DemiBold);
	useTabularDigits(f_.value);
	f_.listValue = scaled(f_.base, 1.05, QFont::DemiBold);
	useTabularDigits(f_.listValue);
	f_.pill = scaled(f_.base, 0.86, QFont::DemiBold);
	useTabularDigits(f_.pill);

	f_.baseH = QFontMetrics(f_.base).height();
	f_.smallH = QFontMetrics(f_.small).height();
	f_.footH = QFontMetrics(f_.foot).height();
	f_.capsH = QFontMetrics(f_.caps).height();
	const QFontMetrics valueMetrics(f_.value);
	f_.valueH = valueMetrics.height();
	f_.valueAscent = valueMetrics.ascent();
	f_.listValueH = QFontMetrics(f_.listValue).height();
	f_.pillH = QFontMetrics(f_.pill).height();
}

QColor VitalsView::levelColor(Level level) const
{
	switch (level) {
	case Level::Warn:
		return c_.warn;
	case Level::Crit:
		return c_.crit;
	case Level::Ok:
		break;
	}
	return c_.ok;
}

QColor VitalsView::titleTint(Level level) const
{
	if (level == Level::Ok)
		return QColor();
	return mix(c_.bg, levelColor(level), c_.light ? 0.22f : 0.32f);
}

/* ---------------------------------------------------------------------- */
/* Layout                                                                   */

int VitalsView::tileHeight(const Tile &tile) const
{
	if (mode_ == Mode::List) {
		const bool graphic = tile.meter >= 0.0f || tile.history;
		return 5 + std::max(f_.smallH, f_.listValueH) + (graphic ? 7 : 0) + 5;
	}
	const int pad = d_.comfortable ? 10 : 7;
	return pad + f_.smallH + 1 + f_.valueH + 2 + std::max(14, f_.footH) + pad - 1;
}

int VitalsView::rowHeight() const
{
	return f_.baseH + 12;
}

void VitalsView::relayout()
{
	/* Changing the scroll range can show the scroll bar, which resizes the
	 * viewport and lands back here. The layout already accounts for the
	 * bar, so the nested call has nothing to add. */
	if (inRelayout_)
		return;
	inRelayout_ = true;
	relayoutNow();
	inRelayout_ = false;
}

void VitalsView::relayoutNow()
{
	const QSize vs = viewport()->size();
	geom_.assign(d_.blocks.size(), BlockGeom());

	if (vs.height() <= kStripMaxHeight) {
		mode_ = Mode::Strip;
		header_ = QRect(QPoint(0, 0), vs);
		health_ = QRect();
		resetBtn_ = QRect();
		settingsBtn_ = QRect(vs.width() - kPad - kButton, (vs.height() - kButton) / 2, kButton, kButton);
		contentHeight_ = vs.height();
		verticalScrollBar()->setRange(0, 0);
		return;
	}

	/* Decide from the full width whether a scroll bar will be needed, so
	 * showing or hiding it can never make the layout flip back and forth. */
	QScrollBar *bar = verticalScrollBar();
	const bool barShown = bar->isVisibleTo(this);
	const int fullWidth = vs.width() + (barShown ? bar->width() : 0);
	const int barWidth = barShown ? bar->width() : bar->sizeHint().width();

	auto layoutAt = [this](int width) {
		mode_ = width <= kListMaxWidth ? Mode::List : Mode::Grid;
		geom_.assign(d_.blocks.size(), BlockGeom());

		const int inner = std::max(1, width - 2 * kPad);
		int y = kPad;
		header_ = QRect(kPad, y, inner, kButton);
		settingsBtn_ = QRect(kPad + inner - kButton, y, kButton, kButton);
		resetBtn_ = settingsBtn_.translated(-(kButton + 2), 0);
		y += kButton + 6;
		health_ = QRect(kPad, y, inner, f_.baseH + 10);
		y += health_.height() + kGroupGap;

		const bool grid = mode_ == Mode::Grid;
		const int gap = grid ? (d_.comfortable ? 7 : 5) : 2;
		const int cols = grid ? std::max(1, (inner + gap) / (kMinTileWidth + gap)) : 1;
		const int colWidth = std::max(1, (inner - (cols - 1) * gap) / cols);
		const int rowH = rowHeight();

		int col = 0;
		int lineTop = y;
		int lineHeight = 0;
		auto beginGrid = [&] {
			col = 0;
			lineTop = y;
			lineHeight = 0;
		};
		auto placeTile = [&](BlockGeom &g, const Tile &t) {
			const int h = tileHeight(t);
			if (col == cols) {
				lineTop += lineHeight + gap;
				col = 0;
				lineHeight = 0;
			}
			g.tiles.push_back(QRect(kPad + col * (colWidth + gap), lineTop, colWidth, h));
			lineHeight = std::max(lineHeight, h);
			++col;
		};
		auto endGrid = [&] {
			if (col > 0)
				y = lineTop + lineHeight;
		};
		auto placeRows = [&](BlockGeom &g, const Block &b) {
			for (size_t r = 0; r < b.rows.size(); r++) {
				g.rows.push_back(QRect(kPad, y, inner, rowH));
				y += rowH + 2;
			}
			if (!b.rows.empty())
				y -= 2;
		};

		if (d_.groupHeaders) {
			for (size_t i = 0; i < d_.blocks.size(); i++) {
				const Block &b = d_.blocks[i];
				BlockGeom &g = geom_[i];
				if (b.offline) {
					g.offline = QRect(kPad, y, inner, rowH);
					y += rowH + kGroupGap;
					continue;
				}
				g.title = QRect(kPad, y, inner, f_.capsH);
				y += f_.capsH + 5;
				if (!b.rows.empty()) {
					placeRows(g, b);
				} else {
					beginGrid();
					for (const Tile &t : b.tiles)
						placeTile(g, t);
					endGrid();
				}
				y += kGroupGap;
			}
		} else {
			/* Without headings every tile shares one grid, followed by
			 * output rows and then the offline lines. */
			beginGrid();
			bool anyTiles = false;
			for (size_t i = 0; i < d_.blocks.size(); i++) {
				const Block &b = d_.blocks[i];
				if (b.offline)
					continue;
				for (const Tile &t : b.tiles) {
					placeTile(geom_[i], t);
					anyTiles = true;
				}
			}
			if (anyTiles) {
				endGrid();
				y += kGroupGap;
			}
			for (size_t i = 0; i < d_.blocks.size(); i++) {
				const Block &b = d_.blocks[i];
				if (b.offline || b.rows.empty())
					continue;
				placeRows(geom_[i], b);
				y += kGroupGap;
			}
			for (size_t i = 0; i < d_.blocks.size(); i++) {
				if (!d_.blocks[i].offline)
					continue;
				geom_[i].offline = QRect(kPad, y, inner, rowH);
				y += rowH + kGroupGap;
			}
		}
		contentHeight_ = y - kGroupGap + kPad;
	};

	layoutAt(fullWidth);
	if (contentHeight_ > vs.height())
		layoutAt(fullWidth - barWidth);

	bar->setPageStep(vs.height());
	bar->setRange(0, std::max(0, contentHeight_ - vs.height()));
}

bool VitalsView::sameStructure(const Display &a, const Display &b) const
{
	if (a.groupHeaders != b.groupHeaders || a.comfortable != b.comfortable || a.colorAlways != b.colorAlways ||
	    a.blocks.size() != b.blocks.size())
		return false;
	for (size_t i = 0; i < a.blocks.size(); i++) {
		const Block &x = a.blocks[i];
		const Block &y = b.blocks[i];
		if (x.group != y.group || x.offline != y.offline || x.tiles.size() != y.tiles.size() ||
		    x.rows.size() != y.rows.size())
			return false;
		for (size_t j = 0; j < x.tiles.size(); j++) {
			const Tile &p = x.tiles[j];
			const Tile &q = y.tiles[j];
			/* Graphics change tile height in the list layout. */
			if (p.id != q.id || (p.history != nullptr) != (q.history != nullptr) ||
			    (p.meter >= 0.0f) != (q.meter >= 0.0f))
				return false;
		}
	}
	return true;
}

/* ---------------------------------------------------------------------- */
/* Updates                                                                  */

void VitalsView::setDisplay(Display &&next)
{
	const bool healthChanged = !hasDisplay_ || d_.healthText != next.healthText || d_.livePill != next.livePill ||
				   d_.recPill != next.recPill;

	if (!hasDisplay_ || !sameStructure(d_, next)) {
		const bool restyle = !hasDisplay_ || d_.comfortable != next.comfortable;
		d_ = std::move(next);
		hasDisplay_ = true;
		if (restyle)
			updateStyle();
		relayout();
		viewport()->update();
		if (healthChanged)
			updateAccessibility();
		return;
	}

	if (mode_ == Mode::Strip) {
		/* The single line is tiny; repaint it whole when anything moved. */
		const bool changed = healthChanged || d_.health != next.health || d_.idleText != next.idleText ||
				     d_.liveAlert != next.liveAlert || !sameChips(d_.chips, next.chips);
		d_ = std::move(next);
		if (changed)
			viewport()->update();
		if (healthChanged)
			updateAccessibility();
		return;
	}

	QRegion dirty;
	auto mark = [&](const QRect &r) {
		if (!r.isNull())
			dirty += contentToViewport(r);
	};

	if (d_.livePill != next.livePill || d_.recPill != next.recPill || d_.liveAlert != next.liveAlert ||
	    d_.recPaused != next.recPaused || d_.idleText != next.idleText)
		mark(header_);
	if (d_.health != next.health || d_.healthText != next.healthText || d_.moreText != next.moreText)
		mark(health_);

	for (size_t i = 0; i < d_.blocks.size(); i++) {
		const Block &a = d_.blocks[i];
		const Block &b = next.blocks[i];
		const BlockGeom &g = geom_[i];
		if (a.meta != b.meta)
			mark(g.title);
		for (size_t j = 0; j < a.tiles.size() && j < g.tiles.size(); j++) {
			if (!sameTile(a.tiles[j], b.tiles[j]))
				mark(g.tiles[j]);
		}
		for (size_t j = 0; j < a.rows.size() && j < g.rows.size(); j++) {
			if (!sameRow(a.rows[j], b.rows[j]))
				mark(g.rows[j]);
		}
	}

	d_ = std::move(next);
	if (!dirty.isEmpty())
		viewport()->update(dirty);
	if (healthChanged)
		updateAccessibility();
}

void VitalsView::updateAccessibility()
{
	QString description = d_.healthText;
	if (!d_.livePill.isEmpty())
		description += QStringLiteral(", ") + d_.livePill;
	if (!d_.recPill.isEmpty())
		description += QStringLiteral(", ") + d_.recPill;
	setAccessibleDescription(description);
}

int VitalsView::scrollOffset() const
{
	return mode_ == Mode::Strip ? 0 : verticalScrollBar()->value();
}

QRect VitalsView::contentToViewport(const QRect &r) const
{
	return r.translated(0, -scrollOffset());
}

/* ---------------------------------------------------------------------- */
/* Painting                                                                 */

void VitalsView::paintEvent(QPaintEvent *event)
{
	QPainter p(viewport());
	p.fillRect(event->rect(), c_.bg);
	if (!hasDisplay_)
		return;
	p.setRenderHint(QPainter::Antialiasing, true);

	if (mode_ == Mode::Strip) {
		paintStrip(p);
		return;
	}

	const int offset = scrollOffset();
	const QRegion dirty = event->region().translated(0, offset);
	p.translate(0, -offset);

	if (dirty.intersects(header_) || dirty.intersects(health_))
		paintHeader(p);

	for (size_t i = 0; i < d_.blocks.size() && i < geom_.size(); i++) {
		const Block &b = d_.blocks[i];
		const BlockGeom &g = geom_[i];
		if (b.offline) {
			if (dirty.intersects(g.offline))
				paintOffline(p, g.offline, b);
			continue;
		}
		if (!g.title.isNull() && dirty.intersects(g.title))
			paintGroupTitle(p, g.title, b);
		for (size_t j = 0; j < b.tiles.size() && j < g.tiles.size(); j++) {
			if (dirty.intersects(g.tiles[j]))
				paintTile(p, g.tiles[j], b.tiles[j]);
		}
		for (size_t j = 0; j < b.rows.size() && j < g.rows.size(); j++) {
			if (dirty.intersects(g.rows[j]))
				paintRow(p, g.rows[j], b.rows[j]);
		}
	}
}

int VitalsView::paintPills(QPainter &p, int x, int centerY, int limit) const
{
	const QFontMetrics fm(f_.pill);
	bool drewAny = false;

	/* marker: 0 none, 1 round dot, 2 square */
	auto pill = [&](const QString &text, const QColor &fg, const QColor &bg, const QColor &markerColor,
			int marker) {
		const int markerWidth = marker ? 6 + 5 : 0;
		const int w = 7 + markerWidth + fm.horizontalAdvance(text) + 7;
		if (x + w > limit)
			return;
		const QRectF r(x, centerY - kPillHeight / 2.0 + 0.5, w, kPillHeight);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(r, kPillHeight / 2.0, kPillHeight / 2.0);
		int tx = x + 7;
		if (marker) {
			const QRectF m(tx, centerY - 3.0 + 0.5, 6, 6);
			p.setBrush(markerColor);
			if (marker == 1)
				p.drawEllipse(m);
			else
				p.drawRoundedRect(m, 1.0, 1.0);
			tx += markerWidth;
		}
		p.setFont(f_.pill);
		p.setPen(fg);
		p.drawText(QRect(tx, static_cast<int>(r.top()), w, kPillHeight), Qt::AlignVCenter | Qt::AlignLeft,
			   text);
		x += w + 4;
		drewAny = true;
	};

	if (!d_.livePill.isEmpty()) {
		const QColor color = d_.liveAlert ? c_.crit : c_.live;
		pill(d_.livePill, color, withAlpha(color, 0.18f), color, 1);
	}
	if (!d_.recPill.isEmpty())
		pill(d_.recPill, d_.recPaused ? c_.muted : c_.fg, withAlpha(c_.fg, 0.09f), c_.live,
		     d_.recPaused ? 0 : 2);

	if (!drewAny && !d_.idleText.isEmpty()) {
		const QFontMetrics sfm(f_.small);
		const int needed = sfm.horizontalAdvance(d_.idleText) + 2;
		const int room = std::max(0, limit - x);
		/* Elide only when the text really doesn't fit; advance and elide
		 * widths can round differently by a pixel. */
		const QString text = needed <= room ? d_.idleText : sfm.elidedText(d_.idleText, Qt::ElideRight, room);
		const int w = std::min(needed, room);
		p.setFont(f_.small);
		p.setPen(c_.muted);
		p.drawText(QRect(x, centerY - kPillHeight / 2, w, kPillHeight), Qt::AlignVCenter | Qt::AlignLeft, text);
		x += w + 4;
	}
	return x;
}

void VitalsView::paintButton(QPainter &p, const QRect &r, Button which) const
{
	if (r.isNull())
		return;
	const bool hovered = hover_ == which;
	if (hovered) {
		p.setPen(Qt::NoPen);
		p.setBrush(withAlpha(c_.fg, 0.10f));
		p.drawRoundedRect(QRectF(r), 4.0, 4.0);
	}

	QPen pen(hovered ? c_.fg : c_.muted, 1.4);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	const QPointF c = QRectF(r).center();

	if (which == Button::Reset) {
		/* Circular arrow: arc from 60 degrees, 270 degrees around, with the
		 * arrowhead at the start pointing clockwise. */
		constexpr double radius = 5.0;
		p.drawArc(QRectF(c.x() - radius, c.y() - radius, 2 * radius, 2 * radius), 60 * 16, 270 * 16);
		const double a = 60.0 * kPi / 180.0;
		const QPointF tip(c.x() + radius * std::cos(a), c.y() - radius * std::sin(a));
		const QPointF dir(std::sin(a), std::cos(a));
		const QPointF perp(-dir.y(), dir.x());
		QPainterPath head;
		head.moveTo(tip - dir * 3.0 + perp * 2.6);
		head.lineTo(tip);
		head.lineTo(tip - dir * 3.0 - perp * 2.6);
		p.drawPath(head);
	} else if (which == Button::Settings) {
		p.drawEllipse(c, 2.2, 2.2);
		for (int i = 0; i < 8; i++) {
			const double a = i * kPi / 4.0;
			const QPointF d(std::cos(a), std::sin(a));
			p.drawLine(c + d * 4.4, c + d * 6.2);
		}
	}
}

void VitalsView::paintHeader(QPainter &p) const
{
	const int centerY = header_.top() + header_.height() / 2;
	paintPills(p, header_.left(), centerY, resetBtn_.left() - 6);
	paintButton(p, resetBtn_, Button::Reset);
	paintButton(p, settingsBtn_, Button::Settings);

	const QColor color = levelColor(d_.health);
	p.setPen(Qt::NoPen);
	p.setBrush(withAlpha(color, d_.health == Level::Ok ? 0.10f : 0.14f));
	p.drawRoundedRect(QRectF(health_), 5.0, 5.0);
	p.setBrush(color);
	const double cy = health_.top() + health_.height() / 2.0;
	p.drawEllipse(QRectF(health_.left() + 8, cy - 3.5, 7, 7));

	int left = health_.left() + 21;
	int right = health_.right() - 8;
	if (!d_.moreText.isEmpty() && mode_ == Mode::Grid) {
		const QFontMetrics fm(f_.small);
		const int w = fm.horizontalAdvance(d_.moreText);
		if (right - w > left + 40) {
			p.setFont(f_.small);
			p.setPen(c_.muted);
			p.drawText(QRect(right - w, health_.top(), w, health_.height()),
				   Qt::AlignVCenter | Qt::AlignRight, d_.moreText);
			right -= w + 8;
		}
	}
	const QFontMetrics fm(f_.base);
	const int w = std::max(0, right - left);
	p.setFont(f_.base);
	p.setPen(d_.health == Level::Ok ? c_.fg : color);
	p.drawText(QRect(left, health_.top(), w, health_.height()), Qt::AlignVCenter | Qt::AlignLeft,
		   fm.elidedText(d_.healthText, Qt::ElideRight, w));
}

void VitalsView::paintGroupTitle(QPainter &p, const QRect &r, const Block &b) const
{
	p.setFont(f_.caps);
	p.setPen(c_.muted);
	const QFontMetrics fm(f_.caps);
	int right = r.right();
	if (!b.meta.isEmpty()) {
		const int w = fm.horizontalAdvance(b.meta);
		p.drawText(QRect(right - w, r.top(), w + 1, r.height()), Qt::AlignVCenter | Qt::AlignRight, b.meta);
		right -= w + 8;
	}
	const int w = std::max(0, right - r.left());
	p.drawText(QRect(r.left(), r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
		   fm.elidedText(b.title, Qt::ElideRight, w));
}

void VitalsView::paintOffline(QPainter &p, const QRect &r, const Block &b) const
{
	QPen pen(c_.line, 1.0, Qt::DashLine);
	p.setPen(pen);
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);
	p.setFont(f_.small);
	p.setPen(c_.muted);
	const QRect inner = r.adjusted(8, 0, -8, 0);
	p.drawText(inner, Qt::AlignVCenter | Qt::AlignLeft, b.title);
	p.drawText(inner, Qt::AlignVCenter | Qt::AlignRight, b.offlineText);
}

void VitalsView::paintFoot(QPainter &p, const QRect &r, const Tile &t) const
{
	if (t.history) {
		const History &h = *t.history;
		const size_t n = std::min(h.size(), d_.sparkSamples);
		if (n < 2)
			return;
		const size_t start = h.size() - n;
		float maxValue = 1.0f;
		for (size_t i = 0; i < n; i++)
			maxValue = std::max(maxValue, h.at(start + i));
		maxValue *= 1.15f;

		const QRectF area(r);
		QPainterPath line;
		for (size_t i = 0; i < n; i++) {
			const double x =
				area.left() + area.width() * static_cast<double>(i) / static_cast<double>(n - 1);
			const double y =
				area.bottom() - static_cast<double>(h.at(start + i) / maxValue) * (area.height() - 1.0);
			if (i == 0)
				line.moveTo(x, y);
			else
				line.lineTo(x, y);
		}
		QPainterPath fill = line;
		fill.lineTo(area.right(), area.bottom());
		fill.lineTo(area.left(), area.bottom());
		fill.closeSubpath();
		p.fillPath(fill, withAlpha(c_.accent, 0.18f));
		QPen pen(c_.accent, 1.25);
		pen.setJoinStyle(Qt::RoundJoin);
		p.strokePath(line, pen);
		return;
	}

	if (t.meter >= 0.0f) {
		const double height = 3.0;
		const QRectF track(r.left(), r.top() + (r.height() - height) / 2.0, r.width(), height);
		p.setPen(Qt::NoPen);
		p.setBrush(withAlpha(c_.fg, 0.12f));
		p.drawRoundedRect(track, 1.5, 1.5);
		QRectF fill = track;
		fill.setWidth(track.width() * static_cast<double>(t.meter));
		if (fill.width() >= 1.0) {
			const QColor color = t.level != Level::Ok ? levelColor(t.level)
					     : d_.colorAlways     ? c_.ok
								  : withAlpha(c_.fg, 0.55f);
			p.setBrush(color);
			p.drawRoundedRect(fill, 1.5, 1.5);
		}
		return;
	}

	if (!t.foot.isEmpty()) {
		const QFontMetrics fm(f_.foot);
		p.setFont(f_.foot);
		p.setPen(c_.muted);
		p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(t.foot, Qt::ElideRight, r.width()));
	}
}

void VitalsView::paintTile(QPainter &p, const QRect &r, const Tile &t) const
{
	QColor fill = c_.surface;
	QColor border = c_.light ? c_.line : QColor(Qt::transparent);
	QColor valueColor = c_.fg;
	if (t.level == Level::Warn) {
		fill = mix(c_.surface, c_.warn, 0.08f);
		border = withAlpha(c_.warn, 0.45f);
		valueColor = c_.warn;
	} else if (t.level == Level::Crit) {
		fill = mix(c_.surface, c_.crit, 0.10f);
		border = withAlpha(c_.crit, 0.55f);
		valueColor = c_.crit;
	}

	if (border.alpha() > 0)
		p.setPen(QPen(border, 1.0));
	else
		p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);

	const QFontMetrics small(f_.small);

	if (mode_ == Mode::List) {
		const int x = r.left() + 7;
		const int w = r.width() - 14;
		const int top = r.top() + 5;
		const int lineH = std::max(f_.smallH, f_.listValueH);
		const QFontMetrics value(f_.listValue);
		const int unitWidth = t.unit.isEmpty() ? 0 : small.horizontalAdvance(t.unit) + 2;
		const int valueWidth = value.horizontalAdvance(t.value);
		const int baseline = top + (lineH + value.ascent() - value.descent()) / 2;

		p.setFont(f_.listValue);
		p.setPen(valueColor);
		p.drawText(QPoint(x + w - unitWidth - valueWidth, baseline), t.value);
		if (unitWidth) {
			p.setFont(f_.small);
			p.setPen(c_.muted);
			p.drawText(QPoint(x + w - unitWidth + 2, baseline), t.unit);
		}

		const int labelWidth = std::max(0, w - valueWidth - unitWidth - 6);
		p.setFont(f_.small);
		p.setPen(c_.muted);
		p.drawText(QRect(x, top, labelWidth, lineH), Qt::AlignVCenter | Qt::AlignLeft,
			   small.elidedText(t.label, Qt::ElideRight, labelWidth));

		if (t.meter >= 0.0f || t.history)
			paintFoot(p, QRect(x, top + lineH + 2, w, 5), t);
		return;
	}

	const int padH = d_.comfortable ? 10 : 8;
	const int padV = d_.comfortable ? 10 : 7;
	const int x = r.left() + padH;
	const int w = r.width() - 2 * padH;
	int y = r.top() + padV;

	p.setFont(f_.small);
	p.setPen(c_.muted);
	p.drawText(QRect(x, y, w, f_.smallH), Qt::AlignVCenter | Qt::AlignLeft,
		   small.elidedText(t.label, Qt::ElideRight, w));
	y += f_.smallH + 1;

	const int baseline = y + f_.valueAscent;
	p.setFont(f_.value);
	p.setPen(valueColor);
	p.drawText(QPoint(x, baseline), t.value);
	if (!t.unit.isEmpty()) {
		const int unitX = x + QFontMetrics(f_.value).horizontalAdvance(t.value) + 3;
		p.setFont(f_.small);
		p.setPen(c_.muted);
		p.drawText(QPoint(unitX, baseline), t.unit);
	}
	y += f_.valueH + 2;

	paintFoot(p, QRect(x, y, w, std::max(14, f_.footH)), t);
}

void VitalsView::paintRow(QPainter &p, const QRect &r, const OutputRow &row) const
{
	QColor fill = c_.surface;
	QColor border = c_.light ? c_.line : QColor(Qt::transparent);
	if (row.level == Level::Warn) {
		border = withAlpha(c_.warn, 0.45f);
	} else if (row.level == Level::Crit) {
		fill = mix(c_.surface, c_.crit, 0.10f);
		border = withAlpha(c_.crit, 0.55f);
	}
	if (border.alpha() > 0)
		p.setPen(QPen(border, 1.0));
	else
		p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);

	const double cy = r.top() + r.height() / 2.0;
	p.setPen(Qt::NoPen);
	p.setBrush(levelColor(row.level));
	p.drawEllipse(QRectF(r.left() + 8, cy - 3.5, 7, 7));

	int left = r.left() + 22;
	int right = r.right() - 8;

	const QFontMetrics value(f_.listValue);
	const int valueWidth = std::max(kRowValueWidth, value.horizontalAdvance(row.right));
	p.setFont(f_.listValue);
	p.setPen(row.level == Level::Ok ? c_.fg : levelColor(row.level));
	p.drawText(QRect(right - valueWidth, r.top(), valueWidth, r.height()), Qt::AlignVCenter | Qt::AlignRight,
		   row.right);
	right -= valueWidth + 8;

	const QFontMetrics small(f_.small);
	if (mode_ == Mode::Grid) {
		p.setFont(f_.small);
		p.setPen(c_.muted);
		if (right - left > kRowRateWidth + 140) {
			p.drawText(QRect(right - kRowRateWidth, r.top(), kRowRateWidth, r.height()),
				   Qt::AlignVCenter | Qt::AlignRight, row.rate);
			right -= kRowRateWidth + 8;
		}
		const int stateWidth = small.horizontalAdvance(row.state);
		if (!row.state.isEmpty() && right - left > stateWidth + 80) {
			p.drawText(QRect(right - stateWidth, r.top(), stateWidth, r.height()),
				   Qt::AlignVCenter | Qt::AlignRight, row.state);
			right -= stateWidth + 8;
		}
	}

	const QFontMetrics base(f_.base);
	const int available = std::max(0, right - left);
	const int nameWidth = base.horizontalAdvance(row.name);
	p.setFont(f_.base);
	p.setPen(c_.fg);
	p.drawText(QRect(left, r.top(), available, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
		   base.elidedText(row.name, Qt::ElideRight, available));

	if (mode_ == Mode::Grid && !row.kind.isEmpty()) {
		const int kindX = left + nameWidth + 6;
		const int kindWidth = small.horizontalAdvance(row.kind);
		if (kindX + kindWidth <= right) {
			p.setFont(f_.small);
			p.setPen(c_.muted);
			p.drawText(QRect(kindX, r.top(), kindWidth + 1, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
				   row.kind);
		}
	}
}

void VitalsView::paintStrip(QPainter &p) const
{
	const int height = viewport()->height();
	const int centerY = height / 2;
	int x = kPad;

	p.setPen(Qt::NoPen);
	p.setBrush(levelColor(d_.health));
	p.drawEllipse(QRectF(x, centerY - 3.5, 7, 7));
	x += 7 + 8;

	const int limit = settingsBtn_.left() - 8;
	x = paintPills(p, x, centerY, limit) + 6;

	const QFontMetrics label(f_.small);
	const QFontMetrics value(f_.listValue);
	constexpr int chipGap = 14;

	/* Chips that don't fit are dropped, never clipped. Healthy chips go
	 * first, from the end, so an alert is the last thing to disappear. */
	const size_t count = d_.chips.size();
	std::vector<int> widths(count);
	std::vector<bool> keep(count, true);
	int total = 0;
	for (size_t i = 0; i < count; i++) {
		widths[i] = label.horizontalAdvance(d_.chips[i].label) + 4 + value.horizontalAdvance(d_.chips[i].value);
		total += widths[i] + chipGap;
	}
	const int available = limit - x + chipGap;
	for (int pass = 0; pass < 2 && total > available; pass++) {
		for (size_t i = count; i-- > 0 && total > available;) {
			if (!keep[i] || (pass == 0 && d_.chips[i].level != Level::Ok))
				continue;
			keep[i] = false;
			total -= widths[i] + chipGap;
		}
	}

	for (size_t i = 0; i < count; i++) {
		if (!keep[i])
			continue;
		const Chip &chip = d_.chips[i];
		const int lw = label.horizontalAdvance(chip.label);
		const int vw = value.horizontalAdvance(chip.value);
		const int cw = widths[i];
		const QColor alert = levelColor(chip.level);
		p.setFont(f_.small);
		p.setPen(chip.level == Level::Ok ? c_.muted : alert);
		p.drawText(QRect(x, 0, lw + 1, height), Qt::AlignVCenter | Qt::AlignLeft, chip.label);
		p.setFont(f_.listValue);
		p.setPen(chip.level == Level::Ok ? c_.fg : alert);
		p.drawText(QRect(x + lw + 4, 0, vw + 1, height), Qt::AlignVCenter | Qt::AlignLeft, chip.value);
		x += cw + chipGap;
	}

	paintButton(p, settingsBtn_, Button::Settings);
}

/* ---------------------------------------------------------------------- */
/* Events                                                                   */

VitalsView::Button VitalsView::buttonAt(const QPoint &viewportPos) const
{
	const QPoint pos = viewportPos + QPoint(0, scrollOffset());
	if (resetBtn_.contains(pos))
		return Button::Reset;
	if (settingsBtn_.contains(pos))
		return Button::Settings;
	return Button::None;
}

void VitalsView::resizeEvent(QResizeEvent *event)
{
	/* Layout happens on the viewport's own resize, see viewportEvent(). */
	QAbstractScrollArea::resizeEvent(event);
}

void VitalsView::scrollContentsBy(int, int)
{
	viewport()->update();
}

bool VitalsView::viewportEvent(QEvent *event)
{
	switch (event->type()) {
	case QEvent::ToolTip: {
		auto *help = static_cast<QHelpEvent *>(event);
		const QPoint pos = help->pos() + QPoint(0, scrollOffset());
		QString tip;
		QRect area;

		const Button button = buttonAt(help->pos());
		if (button == Button::Reset) {
			tip = loc("Vitals.ResetCounters");
			area = resetBtn_;
		} else if (button == Button::Settings) {
			tip = loc("Vitals.OpenSettings");
			area = settingsBtn_;
		} else if (mode_ == Mode::Strip) {
			tip = d_.healthText;
			area = header_;
		} else {
			for (size_t i = 0; i < d_.blocks.size() && i < geom_.size() && tip.isEmpty(); i++) {
				const Block &b = d_.blocks[i];
				const BlockGeom &g = geom_[i];
				for (size_t j = 0; j < b.tiles.size() && j < g.tiles.size(); j++) {
					if (g.tiles[j].contains(pos)) {
						tip = metricHint(b.tiles[j].id);
						area = g.tiles[j];
						break;
					}
				}
				for (size_t j = 0; j < b.rows.size() && j < g.rows.size() && tip.isEmpty(); j++) {
					if (g.rows[j].contains(pos)) {
						tip = loc("Output.Tooltip").arg(QString::fromStdString(b.rows[j].key));
						area = g.rows[j];
					}
				}
			}
		}

		if (tip.isEmpty()) {
			QToolTip::hideText();
			event->ignore();
		} else {
			QToolTip::showText(help->globalPos(), tip, viewport(), contentToViewport(area));
		}
		return true;
	}
	case QEvent::Leave:
		if (hover_ != Button::None) {
			const QRect old = hover_ == Button::Reset ? resetBtn_ : settingsBtn_;
			hover_ = Button::None;
			viewport()->update(contentToViewport(old));
			viewport()->unsetCursor();
		}
		break;
	case QEvent::Resize:
		/* The viewport also resizes when the scroll bar appears. */
		relayout();
		break;
	default:
		break;
	}
	return QAbstractScrollArea::viewportEvent(event);
}

void VitalsView::mouseMoveEvent(QMouseEvent *event)
{
	const Button button = buttonAt(event->position().toPoint());
	if (button != hover_) {
		auto rectOf = [this](Button b) {
			return b == Button::Reset ? resetBtn_ : b == Button::Settings ? settingsBtn_ : QRect();
		};
		const QRect old = rectOf(hover_);
		hover_ = button;
		if (!old.isNull())
			viewport()->update(contentToViewport(old));
		if (button != Button::None) {
			viewport()->update(contentToViewport(rectOf(button)));
			viewport()->setCursor(Qt::PointingHandCursor);
		} else {
			viewport()->unsetCursor();
		}
	}
	QAbstractScrollArea::mouseMoveEvent(event);
}

void VitalsView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		switch (buttonAt(event->position().toPoint())) {
		case Button::Reset:
			emit resetRequested();
			return;
		case Button::Settings:
			emit settingsRequested();
			return;
		case Button::None:
			break;
		}
	}
	QAbstractScrollArea::mouseReleaseEvent(event);
}

void VitalsView::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);
	QAction *reset = menu.addAction(loc("Vitals.ResetCounters"));
	connect(reset, &QAction::triggered, this, [this] { emit resetRequested(); });
	QAction *settings = menu.addAction(loc("Vitals.OpenSettings"));
	connect(settings, &QAction::triggered, this, [this] { emit settingsRequested(); });
	menu.exec(event->globalPos());
}

void VitalsView::showEvent(QShowEvent *event)
{
	QAbstractScrollArea::showEvent(event);
	emit visibilityChanged(true);
}

void VitalsView::hideEvent(QHideEvent *event)
{
	QAbstractScrollArea::hideEvent(event);
	emit visibilityChanged(false);
}

void VitalsView::changeEvent(QEvent *event)
{
	QAbstractScrollArea::changeEvent(event);
	switch (event->type()) {
	case QEvent::PaletteChange:
	case QEvent::FontChange:
	case QEvent::StyleChange:
		updateStyle();
		relayout();
		viewport()->update();
		break;
	default:
		break;
	}
}

} // namespace vitals
