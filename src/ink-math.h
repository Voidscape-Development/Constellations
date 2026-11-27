/*
Constellations - Ink Bleed transition schedule
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/*
 * The shader's field is g = d * scale + rough * 0.25 * n, where d is the pixel
 * distance to the nearest drop and n is noise in -1..1. Behind the front r
 * (g < r - edge) is the incoming scene, ahead of it (g > r) the outgoing one
 * with a halo reaching `edge` further, and between them the ink.
 *
 * ink_reach() bounds d over the canvas so that scale = 1 / reach keeps g
 * under 1 + rough / 4, and ink_front() runs r from where nothing (halo
 * included) has started to where everything is past the ink. So the first and
 * last frames are the two scenes exactly. Kept free of libobs for
 * tests/test-ink.c.
 */

#pragma once

#include <math.h>

#define INK_MAX_DROPS 8
#define INK_GRID 32

/* Upper bound on the distance from any canvas point to its nearest drop. The
 * nearest-drop distance moves at most one pixel per pixel, so its largest
 * value on a grid plus half a grid cell's diagonal is a safe bound. */
static inline float ink_reach(const float *xy, int count, float w, float h)
{
	float best = 1.0f;
	for (int j = 0; j <= INK_GRID; ++j)
		for (int i = 0; i <= INK_GRID; ++i) {
			float px = w * (float)i / INK_GRID, py = h * (float)j / INK_GRID, near = 1e30f;
			for (int k = 0; k < count; ++k) {
				float dx = px - xy[2 * k], dy = py - xy[2 * k + 1];
				near = fminf(near, sqrtf(dx * dx + dy * dy));
			}
			best = fmaxf(best, near);
		}
	return best + 0.5f * sqrtf(w * w + h * h) / INK_GRID;
}

static inline float ink_ease(int easing, float t)
{
	if (easing == 1)
		return t * t * (3.0f - 2.0f * t);
	if (easing == 2)
		return 1.0f - (1.0f - t) * (1.0f - t);
	return t;
}

/* The front for eased progress e (0..1); edge and aa in field units. */
static inline float ink_front(float e, float rough, float edge, float aa)
{
	float lo = -0.25f * rough - edge - 2.0f * aa;
	float hi = 1.0f + 0.25f * rough + edge + 2.0f * aa;
	return lo + (hi - lo) * e;
}
