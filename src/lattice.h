/*
Constellations - Pattern placement math
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

/* Placement math shared by the renderer and the unit tests. Nothing here
 * touches libobs, so tests/test-lattice.c can exercise it on its own.
 *
 * Every shape is drawn as its own quad. The CPU only works out which cells
 * can be visible and how far the pattern has scrolled; pattern.effect turns
 * each vertex id into a cell and computes that cell's position, color and
 * animation. Several functions below mirror the shader so the tests can
 * check the arithmetic the GPU relies on.
 *
 * Scrolling never feeds an unbounded value to the GPU. Rows wrap every
 * CLAT_ROW_PERIOD cells and columns every clat_col_period() cells, and
 * per-cell randomness is keyed on the wrapped indices, so a wrap is
 * invisible and float precision stays the same after hours of motion. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rows (and cell identities across rows) repeat after this many cells. */
#define CLAT_ROW_PERIOD 4096.0f
/* Per-row speed multipliers are quantized to 1/CLAT_RATE_STEPS and capped at
 * 2x, so a whole multiple of the column period wraps every row at once. */
#define CLAT_RATE_STEPS 64.0f
#define CLAT_RATE_MAX 128.0f
/* Twinkle and wander rates are quantized to 1/64 as well; the phases the
 * CPU hands over wrap at these periods without a visible jump. */
#define CLAT_TWINKLE_PERIOD 64.0      /* cycles */
#define CLAT_WANDER_PERIOD 402.1238597 /* 64 * 2 pi, radians */
#define CLAT_TWO_PI 6.28318530717958647692

/* Same value as the shader's hash for the same inputs (both are IEEE float
 * arithmetic); returns [0, 1). Inputs are wrapped cell indices and seeds,
 * which keeps them small enough for the hash to stay well distributed. */
float clat_hash(float x, float y, float seed);

/* Floor-based modulo for integer-valued floats, exact below 2^24 even when
 * the division rounds. */
float clat_mod(float x, float m);
double clat_fmod_pos(double x, double m);

/* Column period for a layout cycling through `step_count` items: a multiple
 * of the step count no larger than 2048, so column ownership survives a
 * wrap and the shift arithmetic stays exact in float. */
float clat_col_period(uint32_t step_count);

/* Speed drift multiplier of a row, times CLAT_RATE_STEPS, in 0..128. */
float clat_rate_q(float row_id, float amount, float seed);

/* Splits the accumulated cross-row motion into whole rows (mod
 * CLAT_ROW_PERIOD) and the remaining px in [0, cell). */
void clat_split_rows(double motion_perp, float cell, float *row_wrap, float *perp_frac);

/* Splits the accumulated along-row motion into whole cells (mod 64 * period)
 * and the remaining px in [0, cell). */
void clat_split_cols(double motion_along, float cell, float col_period, float *cells, float *frac);

/* Mirror of the shader: how far a row with multiplier q (from
 * clat_rate_q, negative for reversed rows) has shifted, as whole cells mod
 * col_period plus a px remainder. */
void clat_row_shift(float cells, float frac, float cell, float q, float col_period, float *whole, float *sub_px);

/* Rows and columns of lattice cells that can touch a w x h canvas. The
 * lattice is anchored at origin with its rows running along dir; extent is
 * how far a shape reaches from its cell center. */
struct clat_range {
	float row_first;
	float row_count;
	float along_min;
	float col_count;
};

void clat_visible_range(float w, float h, float origin_x, float origin_y, float dir_x, float dir_y, float cell,
			float extent, float perp_frac, struct clat_range *out);

/* One row of the Pattern Border perimeter layout: a rounded rectangle walked
 * clockwise from the top-left as eight segments (top, top-right corner,
 * right, bottom-right corner, bottom, bottom-left corner, left, top-left
 * corner). Each segment holds a whole number of shapes so they sit evenly on
 * every side and one lands on each sharp corner. */
struct clat_border_row {
	float box[4]; /* x0, y0, x1, y1 */
	float rad[4]; /* corner radii tl, tr, br, bl */
	float ns[4];  /* shapes on the top, right, bottom, left sides */
	float na[4];  /* shapes on the tr, br, bl, tl corners */
	float total;
};

struct clat_border_frame {
	float w, h;
	float widths[4]; /* left, top, right, bottom */
	float radius;
	float inner_radii[4]; /* tl, tr, br, bl */
};

/* Corner radii of the frame's outer and inner edges. */
void clat_border_radii(float w, float h, const float widths[4], float radius, float *outer, float inner[4]);

/* Row at fraction f across the band (0 outer edge, 1 inner edge), with shapes
 * spaced about `cell` px apart. */
void clat_border_make_row(const struct clat_border_frame *fr, float f, float cell, struct clat_border_row *row);

/* Mirror of the shader: point at path parameter u in [0, total) and its
 * clockwise heading (radians). */
void clat_border_point(const struct clat_border_row *row, float u, float *x, float *y, float *angle);

/* Shapes marched along row `row` so far, in [0, total). Alternating rows run
 * backwards, Speed Drift varies the pace per row, staggered odd rows sit
 * half a spacing along. */
double clat_border_row_phase(double march, float row, float cell, float offset_along, bool alternating,
			     float drift_amount, float drift_seed, bool stagger, float total);

/* Topography Lines: octave k of the height field samples the noise lattice
 * at M^k * p + C_k, with M a rotate-and-scale by (1.6, 1.2) and C_k the
 * accumulated (31.4, 31.4) shifts. For a map offset (dx, dy) this returns,
 * per octave, the offset's whole lattice index wrapped at `period` (xy) and
 * its fraction (zw), computed in double so the shader only adds small
 * numbers however far the terrain has drifted. */
#define CLAT_TOPO_OCTAVES 5
void clat_topo_octave_offsets(double dx, double dy, double period, float out[CLAT_TOPO_OCTAVES][4]);

#ifdef __cplusplus
}
#endif
