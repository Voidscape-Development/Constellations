/*
Constellations - Image and SVG loading for pattern items
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <obs-module.h>

/* Item artwork is decoded and prepared on the UI thread, then uploaded on the
 * graphics thread. Pixels are RGBA8, premultiplied in linear light and stored
 * sRGB-encoded (OBS's GS_IMAGE_ALPHA_PREMULTIPLY_SRGB convention), so the
 * texture is sampled through its sRGB view. Every texture carries a full mip
 * chain built in linear light: items are often drawn far smaller than their
 * artwork, and without mips they shimmer while moving. */

#define CPAT_MEDIA_MAX_LEVELS 16

struct cpat_pixels {
	uint32_t width, height;
	uint32_t levels;
	uint8_t *data[CPAT_MEDIA_MAX_LEVELS];
};

struct NSVGimage;

/* Builds the sRGB tables; called once from obs_module_load. */
void cpat_media_init(void);

bool cpat_media_is_svg(const char *path);
struct NSVGimage *cpat_media_svg_load(const char *path);
void cpat_media_svg_free(struct NSVGimage *svg);
/* Raster size used for an SVG drawn `size` px wide: the next power of two,
 * so dragging the size slider only re-rasterizes at each doubling. */
uint32_t cpat_media_svg_px(float size);

struct cpat_pixels *cpat_pixels_from_file(const char *path);
struct cpat_pixels *cpat_pixels_from_svg(struct NSVGimage *svg, uint32_t px);
void cpat_pixels_free(struct cpat_pixels *px);

/* Graphics thread, inside obs_enter_graphics(). */
gs_texture_t *cpat_pixels_upload(const struct cpat_pixels *px);
