/*
Constellations - Image and SVG loading for pattern items
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "pattern-media.h"

#include <plugin-support.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <math.h>

#include "nanosvg.h"
#include "nanosvgrast.h"

/* sRGB transfer tables. Decoding maps every 8-bit code; encoding maps linear
 * light quantized to 16 bits, fine enough that the darkest codes survive. */
#define ENC_STEPS 65536
static float dec_lut[256];
static uint8_t enc_lut[ENC_STEPS];
void cpat_media_init(void)
{
	for (int i = 0; i < 256; ++i) {
		float c = (float)i / 255.0f;
		dec_lut[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
	}
	for (int i = 0; i < ENC_STEPS; ++i) {
		float l = (float)i / (float)(ENC_STEPS - 1);
		float c = l <= 0.0031308f ? l * 12.92f : 1.055f * powf(l, 1.0f / 2.4f) - 0.055f;
		enc_lut[i] = (uint8_t)(c * 255.0f + 0.5f);
	}
}

static inline uint8_t encode(float linear)
{
	if (linear <= 0.0f)
		return 0;
	if (linear >= 1.0f)
		return 255;
	return enc_lut[(int)(linear * (float)(ENC_STEPS - 1) + 0.5f)];
}

bool cpat_media_is_svg(const char *path)
{
	if (!path)
		return false;
	const char *ext = os_get_path_extension(path);
	return ext && astrcmpi(ext, ".svg") == 0;
}

struct NSVGimage *cpat_media_svg_load(const char *path)
{
	/* Read through libobs rather than nsvgParseFromFile so UTF-8 paths
	 * work on Windows, where nanosvg's fopen() would choke on them. */
	char *text = os_quick_read_utf8_file(path);
	if (!text) {
		obs_log(LOG_WARNING, "could not read SVG '%s'", path);
		return NULL;
	}
	NSVGimage *svg = nsvgParse(text, "px", 96.0f);
	bfree(text);
	if (!svg)
		return NULL;
	if (svg->width <= 0.0f || svg->height <= 0.0f) {
		obs_log(LOG_WARNING, "SVG '%s' has no usable size", path);
		nsvgDelete(svg);
		return NULL;
	}
	return svg;
}

void cpat_media_svg_free(struct NSVGimage *svg)
{
	if (svg)
		nsvgDelete(svg);
}

uint32_t cpat_media_svg_px(float size)
{
	uint32_t px = 32;
	while (px < 2048 && (float)px < size)
		px <<= 1;
	return px;
}

/* Premultiplies straight-alpha RGBA8 in place, in linear light. */
static void premultiply(uint8_t *rgba, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		uint8_t *p = rgba + i * 4;
		if (p[3] == 255)
			continue;
		float a = (float)p[3] / 255.0f;
		p[0] = encode(dec_lut[p[0]] * a);
		p[1] = encode(dec_lut[p[1]] * a);
		p[2] = encode(dec_lut[p[2]] * a);
	}
}

/* Box-filters `src` (w x h) down to the next level, averaging in linear
 * light. Odd edges fold their last row/column into the neighbor. */
static uint8_t *downsample(const uint8_t *src, uint32_t w, uint32_t h, uint32_t nw, uint32_t nh)
{
	uint8_t *dst = bmalloc((size_t)nw * nh * 4);
	for (uint32_t y = 0; y < nh; ++y) {
		uint32_t y0 = y * 2;
		uint32_t y1 = y0 + 1 < h ? y0 + 1 : y0;
		for (uint32_t x = 0; x < nw; ++x) {
			uint32_t x0 = x * 2;
			uint32_t x1 = x0 + 1 < w ? x0 + 1 : x0;
			const uint8_t *s[4] = {src + ((size_t)y0 * w + x0) * 4, src + ((size_t)y0 * w + x1) * 4,
					       src + ((size_t)y1 * w + x0) * 4, src + ((size_t)y1 * w + x1) * 4};
			uint8_t *d = dst + ((size_t)y * nw + x) * 4;
			for (int c = 0; c < 3; ++c)
				d[c] = encode(0.25f * (dec_lut[s[0][c]] + dec_lut[s[1][c]] + dec_lut[s[2][c]] +
						       dec_lut[s[3][c]]));
			d[3] = (uint8_t)((s[0][3] + s[1][3] + s[2][3] + s[3][3] + 2) / 4);
		}
	}
	return dst;
}

/* Takes ownership of a straight-alpha RGBA8 buffer. */
static struct cpat_pixels *build(uint8_t *rgba, uint32_t w, uint32_t h)
{
	premultiply(rgba, (size_t)w * h);
	struct cpat_pixels *px = bzalloc(sizeof(*px));
	px->width = w;
	px->height = h;
	px->data[0] = rgba;
	px->levels = 1;
	while ((w > 1 || h > 1) && px->levels < CPAT_MEDIA_MAX_LEVELS) {
		uint32_t nw = w > 1 ? w / 2 : 1;
		uint32_t nh = h > 1 ? h / 2 : 1;
		px->data[px->levels] = downsample(px->data[px->levels - 1], w, h, nw, nh);
		px->levels++;
		w = nw;
		h = nh;
	}
	return px;
}

struct cpat_pixels *cpat_pixels_from_file(const char *path)
{
	enum gs_color_format fmt = GS_UNKNOWN;
	uint32_t w = 0, h = 0;
	uint8_t *data = gs_create_texture_file_data2(path, GS_IMAGE_ALPHA_STRAIGHT, &fmt, &w, &h);
	if (!data || !w || !h) {
		obs_log(LOG_WARNING, "could not load image '%s'", path);
		bfree(data);
		return NULL;
	}
	size_t count = (size_t)w * h;
	switch (fmt) {
	case GS_RGBA:
		break;
	case GS_BGRA:
	case GS_BGRX:
		for (size_t i = 0; i < count; ++i) {
			uint8_t *p = data + i * 4;
			uint8_t t = p[0];
			p[0] = p[2];
			p[2] = t;
			if (fmt == GS_BGRX)
				p[3] = 255;
		}
		break;
	default:
		obs_log(LOG_WARNING, "image '%s' has an unsupported pixel format (%d)", path, (int)fmt);
		bfree(data);
		return NULL;
	}
	return build(data, w, h);
}

struct cpat_pixels *cpat_pixels_from_svg(struct NSVGimage *svg, uint32_t px)
{
	if (!svg || px == 0)
		return NULL;
	NSVGrasterizer *rast = nsvgCreateRasterizer();
	if (!rast)
		return NULL;
	uint8_t *data = bzalloc((size_t)px * px * 4);
	/* Scaled to fit and centered, so non-square artwork keeps its aspect. */
	float longest = fmaxf(svg->width, svg->height);
	float scale = (float)px / longest;
	float tx = ((float)px - svg->width * scale) * 0.5f;
	float ty = ((float)px - svg->height * scale) * 0.5f;
	nsvgRasterize(rast, svg, tx, ty, scale, data, (int)px, (int)px, (int)px * 4);
	nsvgDeleteRasterizer(rast);
	return build(data, px, px);
}

void cpat_pixels_free(struct cpat_pixels *px)
{
	if (!px)
		return;
	for (uint32_t i = 0; i < px->levels; ++i)
		bfree(px->data[i]);
	bfree(px);
}

gs_texture_t *cpat_pixels_upload(const struct cpat_pixels *px)
{
	if (!px)
		return NULL;
	return gs_texture_create(px->width, px->height, GS_RGBA, px->levels, (const uint8_t **)px->data, 0);
}
