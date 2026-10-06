/*
Constellations - SVG loading for pattern items
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "svg-image.h"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nanosvg.h"
#include "nanosvgrast.h"

bool cpat_svg_is_svg_path(const char *path)
{
	if (!path)
		return false;
	const char *ext = os_get_path_extension(path);
	return ext && astrcmpi(ext, ".svg") == 0;
}

struct NSVGimage *cpat_svg_load(const char *path)
{
	/* Read through libobs rather than nsvgParseFromFile so UTF-8 paths
	 * work on Windows, where nanosvg's fopen() would choke on them. */
	char *text = os_quick_read_utf8_file(path);
	if (!text) {
		obs_log(LOG_WARNING, "Constellations: could not read SVG '%s'", path);
		return NULL;
	}
	NSVGimage *svg = nsvgParse(text, "px", 96.0f);
	bfree(text);
	if (!svg)
		return NULL;
	if (svg->width <= 0.0f || svg->height <= 0.0f) {
		obs_log(LOG_WARNING, "Constellations: SVG '%s' has no usable size", path);
		nsvgDelete(svg);
		return NULL;
	}
	return svg;
}

void cpat_svg_free(struct NSVGimage *svg)
{
	if (svg)
		nsvgDelete(svg);
}

uint8_t *cpat_svg_rasterize(struct NSVGimage *svg, uint32_t px)
{
	if (!svg || px == 0)
		return NULL;
	NSVGrasterizer *rast = nsvgCreateRasterizer();
	if (!rast)
		return NULL;
	uint8_t *data = bzalloc((size_t)px * px * 4);
	float longest = fmaxf(svg->width, svg->height);
	float scale = (float)px / longest;
	float tx = ((float)px - svg->width * scale) * 0.5f;
	float ty = ((float)px - svg->height * scale) * 0.5f;
	nsvgRasterize(rast, svg, tx, ty, scale, data, (int)px, (int)px, (int)px * 4);
	nsvgDeleteRasterizer(rast);
	return data;
}
