/*
Constellations - Text Tiles row motion math
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

/* The CPU side of Text Tiles. Needs no libobs, so it is unit tested on its
 * own (tests/test-text-tiles.c).
 *
 * The canvas is cut into horizontal rows (horizontal after the pattern's
 * rotation). Row 0 is centred on the canvas centre, rows above it are
 * negative. Every row repeats one period (phrase, gap, separator, gap)
 * along its length and slides sideways at its own speed. */

enum ctt_direction {
	CTT_DIR_SAME = 0,      /* every row moves the way Speed says */
	CTT_DIR_ALTERNATE = 1, /* odd rows run backwards */
	CTT_DIR_RANDOM = 2,    /* each row picks a direction from the seed */
};

/* Rows are kept for indices -CTT_ROW_HALF..CTT_ROW_HALF; a row outside that
 * (only reachable on huge canvases with tiny text) draws nothing. */
#define CTT_ROW_HALF 2048
#define CTT_ROW_SLOTS (2 * CTT_ROW_HALF + 1)

struct ctt_motion {
	double speed;         /* px/s; positive moves row 0 to the right */
	int direction;        /* enum ctt_direction */
	double variation;     /* 0..1: each row's speed is speed * (1 +- variation) */
	bool wander;          /* speeds ease up and down over time */
	double wander_amount; /* 0..1: how far the speed swings either side */
	double wander_rate;   /* swings per second, roughly */
	uint32_t seed;
};

/* Floor modulo into [0, m). Returns 0 for m <= 0. */
double ctt_wrap(double x, double m);

/* Stable per-row random number in [0, 1); `salt` picks independent streams. */
double ctt_row_hash(int row, uint32_t seed, uint32_t salt);

/* +1 or -1: the direction the row moves in. */
int ctt_row_sign(const struct ctt_motion *m, int row);

/* The row's steady speed in px/s (signed), before any wander. */
double ctt_row_base_speed(const struct ctt_motion *m, int row);

/* The row's speed at time t (seconds), wander included. Wander multiplies
 * the base speed by a factor that averages 1 over time, so rows keep their
 * overall pace while easing past each other. */
double ctt_row_speed(const struct ctt_motion *m, int row, double t);

/* Where the row starts, in px to the right: the stagger shifts each row by
 * that fraction of a period relative to the row above, and the random offset
 * adds a seeded position anywhere in the period. */
double ctt_row_start(int row, double stagger, bool random_offset, uint32_t seed, double period);

/* The rows that can touch a width x height canvas whose rows are `pitch` px
 * apart and `text_h` px tall, at any rotation. Row r covers rotated y from
 * r * pitch - text_h / 2 to r * pitch + text_h / 2. The range is clamped to
 * the rows that are kept. Returns the row count (0 when nothing to draw). */
int ctt_row_range(double width, double height, double pitch, double text_h, int *first);

/* Signed distance fields ----------------------------------------------------
 *
 * The letters are drawn as a signed distance field: each texel holds how far
 * its centre is from the nearest letter edge, negative inside the letters.
 * Coverage at any scale or rotation, and an outline of any width, are then
 * one texture read each. */

/* Builds the field from an 8-bit coverage image (255 = inside) of w x h
 * texels, each `texel_px` canvas px across. Distances are in canvas px,
 * clamped to +-spread and encoded into 16 bits as 0.5 - d / (2 * spread), so
 * 0.5 is the edge. Returns false when out of memory. */
bool ctt_sdf_build(const uint8_t *coverage, int w, int h, float texel_px, float spread, uint16_t *out);

/* Decodes one encoded value back to a distance in canvas px. */
float ctt_sdf_decode(uint16_t v, float spread);

#ifdef __cplusplus
}
#endif
