/*
Constellations - SVG loading for pattern items
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct NSVGimage;

bool cpat_svg_is_svg_path(const char *path);
struct NSVGimage *cpat_svg_load(const char *path);
void cpat_svg_free(struct NSVGimage *svg);

/* Rasterizes the SVG into a px * px RGBA buffer (straight alpha), scaled to
 * fit and centered so non-square artwork keeps its aspect ratio. The caller
 * bfree()s the result. Returns NULL on failure. */
uint8_t *cpat_svg_rasterize(struct NSVGimage *svg, uint32_t px);
