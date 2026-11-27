/*
Constellations - Shared palettes
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/*
 * A palette is a source of its own ("Constellations Palette") holding a
 * background and five colors, optionally drifting to a night set by the local
 * clock. Anything else picks one by UUID in its "palette" setting and reads
 * the current colors with cpal_get() whenever it draws, so editing the palette
 * (or the clock moving on) restyles every user at once.
 */

#pragma once

#include <obs-module.h>

#define CPAL_SLOTS 6 /* 0 is the background, 1..5 the colors */
#define CPAL_KEY "palette"

void constellations_register_palette(void);

/* Fills `out` (0xAABBGGRR) from the palette with this UUID. False, leaving
 * `out` alone, when the UUID is empty or names no palette. */
bool cpal_get(const char *uuid, uint32_t out[CPAL_SLOTS]);

/* Adds the "palette" list: None, then every palette source by name. */
obs_property_t *cpal_add_list(obs_properties_t *props);

/* Not a stored slot: the background half way to black, for gradient ends. */
#define CPAL_BG_SHADE CPAL_SLOTS

/* Replaces the RGB of a color with a palette slot (or CPAL_BG_SHADE), keeping
 * its alpha. `linear` says which vec4 the caller keeps: vec4_from_rgba's sRGB
 * encoding, or cfx_color_linear's linear light. */
void cpal_apply(const uint32_t pal[CPAL_SLOTS], int slot, struct vec4 *color, bool linear);
