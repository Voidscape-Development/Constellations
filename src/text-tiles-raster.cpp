/*
Constellations - Text Tiles glyph rasterizer
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "text-tiles-raster.h"
#include "text-tiles-math.h"

#include <obs-module.h>

#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>

/* Texels per canvas px. Two keeps the field accurate at the anti-aliased
 * edge; larger text drops it to stay inside the texture size limit. */
static constexpr float k_supersample = 2.0f;
static constexpr float k_max_texels = 8192.0f;

static QFont make_font(const ctt_font *f)
{
	QString family = QString::fromUtf8(f->face ? f->face : "");
	QString style = QString::fromUtf8(f->style ? f->style : "");
	QFont font;
	if (!style.isEmpty() && QFontDatabase::styles(family).contains(style)) {
		/* The picker saved a named style ("Black", "SemiBold Italic"),
		 * which says more than the bold/italic flags can. */
		font = QFontDatabase::font(family, style, 12);
	} else {
		/* No such style: a generic family ("Sans Serif") the system
		 * resolves itself, or a font that has since changed. */
		font = QFont(family);
		font.setBold((f->flags & OBS_FONT_BOLD) != 0);
		font.setItalic((f->flags & OBS_FONT_ITALIC) != 0);
	}
	font.setUnderline((f->flags & OBS_FONT_UNDERLINE) != 0);
	font.setStrikeOut((f->flags & OBS_FONT_STRIKEOUT) != 0);
	font.setPixelSize(f->size > 0 ? f->size : 1);
	font.setLetterSpacing(QFont::AbsoluteSpacing, f->letter_spacing);
	/* Hinting snaps outlines to whole pixels at one size; the field is
	 * drawn at any scale, so take the true outlines. */
	font.setHintingPreference(QFont::PreferNoHinting);
	font.setStyleStrategy(QFont::PreferAntialias);
	return font;
}

extern "C" bool ctt_raster_text(const ctt_font *f, const char *text, bool upper, float spread, ctt_glyphs *out)
{
	std::memset(out, 0, sizeof(*out));
	QCoreApplication *app = QCoreApplication::instance();
	if (!app || !app->inherits("QGuiApplication") || !text || !*text)
		return false;

	QString str = QString::fromUtf8(text);
	if (upper)
		str = str.toUpper();
	QFont font = make_font(f);
	QFontMetricsF fm(font);
	qreal ascent = fm.ascent();
	qreal descent = fm.descent();
	qreal advance = fm.horizontalAdvance(str);
	if (advance <= 0.0 || ascent + descent <= 0.0)
		return false;

	/* Everything the ink reaches, in box coordinates, plus room for the
	 * field to fall off to the spread. */
	QRectF box(0.0, 0.0, advance, ascent + descent);
	QRectF ink = fm.boundingRect(str).translated(0.0, ascent);
	QRectF area = box.united(ink);
	qreal pad = (qreal)spread + 2.0;
	qreal x0 = std::floor(area.left() - pad), y0 = std::floor(area.top() - pad);
	qreal x1 = std::ceil(area.right() + pad), y1 = std::ceil(area.bottom() + pad);
	qreal wpx = x1 - x0, hpx = y1 - y0;

	float scale = k_supersample;
	float longest = (float)std::max(wpx, hpx);
	if (longest * scale > k_max_texels)
		scale = k_max_texels / longest;
	int tw = (int)std::ceil(wpx * scale), th = (int)std::ceil(hpx * scale);
	if (tw <= 0 || th <= 0)
		return false;

	QImage img(tw, th, QImage::Format_Alpha8);
	if (img.isNull())
		return false;
	img.fill(0);
	{
		QPainter p(&img);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setRenderHint(QPainter::TextAntialiasing, true);
		p.scale(scale, scale);
		p.translate(-x0, -y0);
		p.setFont(font);
		p.setPen(QColor(255, 255, 255, 255));
		p.drawText(QPointF(0.0, ascent), str);
	}

	size_t n = (size_t)tw * (size_t)th;
	uint8_t *cov = (uint8_t *)bmalloc(n);
	uint16_t *sdf = (uint16_t *)bmalloc(n * sizeof(uint16_t));
	for (int y = 0; y < th; ++y)
		std::memcpy(cov + (size_t)y * (size_t)tw, img.constScanLine(y), (size_t)tw);
	bool ok = ctt_sdf_build(cov, tw, th, 1.0f / scale, spread, sdf);
	bfree(cov);
	if (!ok) {
		bfree(sdf);
		return false;
	}

	out->box_w = (float)advance;
	out->box_h = (float)(ascent + descent);
	out->tex_x = (float)x0;
	out->tex_y = (float)y0;
	out->tex_wpx = (float)tw / scale;
	out->tex_hpx = (float)th / scale;
	out->tex_w = (uint32_t)tw;
	out->tex_h = (uint32_t)th;
	out->sdf = sdf;
	return true;
}

extern "C" void ctt_glyphs_free(ctt_glyphs *g)
{
	bfree(g->sdf);
	std::memset(g, 0, sizeof(*g));
}
