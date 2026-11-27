/*
Constellations - Generated backgrounds
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/*
 * The backgrounds drawn entirely by one full-canvas shader (Flow Field,
 * Starfield, Aurora) share everything but their effect and their own
 * settings, so each is a table here and a source plus a filter are made from
 * it. Every effect gets canvas_size, time (scaled by the Speed setting),
 * clock (wall seconds), bg_color and color1..color5 (linear, straight alpha)
 * and one float uniform per table entry, named after its setting.
 */

#pragma once

#include <obs-module.h>

#include "palette.h"

#define CGEN_MAX_PARAMS 16

enum cgen_kind {
	CGEN_SLIDER,
	CGEN_LIST, /* int list 0..max, item labels are "<label>.<n>" */
	CGEN_BOOL,
};

struct cgen_param {
	const char *key;   /* setting and uniform name */
	const char *label; /* locale key */
	enum cgen_kind kind;
	double min, max, step, def;
	const char *suffix;
};

struct cgen_desc {
	const char *id_source, *id_filter;
	const char *name_source, *name_filter; /* locale keys */
	const char *effect;
	const struct cgen_param *params;
	size_t count;
	uint32_t colors[CPAL_SLOTS]; /* defaults, 0xRRGGBB; 0 is the background */
	double speed;
};

void cgen_register(const struct cgen_desc *d);
void constellations_register_generators(void);
