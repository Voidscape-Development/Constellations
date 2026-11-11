/*
Constellations - Text Tiles glyph rasterizer
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lays out and rasterizes one line of text with Qt, which finds the font the
 * OBS font picker names on every platform and shapes the text (kerning,
 * complex scripts, fallback fonts for missing characters). The result is a
 * signed distance field (see ctt_sdf_build) of the line.
 *
 * Safe to call from any thread once the application exists: it only paints
 * into an image. */

struct ctt_font {
	const char *face;     /* family, as the OBS font picker saves it */
	const char *style;    /* style name ("Black", "Bold Italic"), may be empty */
	int size;             /* px */
	uint32_t flags;       /* OBS_FONT_* */
	float letter_spacing; /* px added after each character */
};

/* Layout box: x from 0 to box_w along the baseline's advance, y from 0 (the
 * font's ascent above the baseline) to box_h (its descent below). The field
 * covers tex_x..tex_x + tex_wpx by tex_y..tex_y + tex_hpx of that box, in
 * canvas px, at tex_w x tex_h texels; it reaches past the box wherever the
 * ink does, plus the spread. */
struct ctt_glyphs {
	float box_w, box_h;
	float tex_x, tex_y, tex_wpx, tex_hpx;
	uint32_t tex_w, tex_h;
	uint16_t *sdf; /* bfree() */
};

/* Rasterizes `text`, in capitals when `upper` is set (Unicode-aware). False
 * when there is nothing to draw (empty text, no application, out of memory);
 * `out` is then zeroed. */
bool ctt_raster_text(const struct ctt_font *font, const char *text, bool upper, float spread, struct ctt_glyphs *out);

void ctt_glyphs_free(struct ctt_glyphs *g);

#ifdef __cplusplus
}
#endif
