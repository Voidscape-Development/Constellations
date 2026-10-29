/*
Constellations - Pattern placement math
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "lattice.h"

#include <math.h>

#define CLAT_HALF_PI 1.57079632679489661923f
#define CLAT_PI 3.14159265358979323846f

static float frac_f(float x)
{
	return x - floorf(x);
}

float clat_hash(float x, float y, float seed)
{
	/* Dave Hoskins' sine-free hash12. A frac(sin(x) * big) hash loses
	 * precision as its input grows and differs between GPU vendors; this
	 * one only multiplies and adds. */
	x += seed * 0.7548777f;
	y += seed * 0.5698403f;
	float p0 = frac_f(x * 0.1031f);
	float p1 = frac_f(y * 0.1031f);
	float p2 = frac_f(x * 0.1031f);
	float d = p0 * (p1 + 33.33f) + p1 * (p2 + 33.33f) + p2 * (p0 + 33.33f);
	p0 += d;
	p1 += d;
	p2 += d;
	return frac_f((p0 + p1) * p2);
}

float clat_mod(float x, float m)
{
	float r = x - floorf(x / m) * m;
	if (r < 0.0f)
		r += m;
	if (r >= m)
		r -= m;
	return r;
}

double clat_fmod_pos(double x, double m)
{
	double r = fmod(x, m);
	if (r < 0.0)
		r += m;
	if (r >= m)
		r = 0.0;
	return r;
}

float clat_col_period(uint32_t step_count)
{
	float n = step_count < 1 ? 1.0f : (float)step_count;
	return n * floorf(2048.0f / n);
}

float clat_rate_q(float row_id, float amount, float seed)
{
	if (amount <= 0.0f)
		return CLAT_RATE_STEPS;
	float s = (clat_hash(row_id, 7.0f, seed) - 0.5f) * 2.0f * amount;
	float q = floorf(CLAT_RATE_STEPS * (1.0f + s) + 0.5f);
	if (q < 0.0f)
		q = 0.0f;
	if (q > CLAT_RATE_MAX)
		q = CLAT_RATE_MAX;
	return q;
}

void clat_split_rows(double motion_perp, float cell, float *row_wrap, float *perp_frac)
{
	double cs = cell;
	double rows = floor(motion_perp / cs);
	double rem = motion_perp - rows * cs;
	if (rem < 0.0)
		rem = 0.0;
	if (rem >= cs)
		rem = 0.0;
	*row_wrap = (float)clat_fmod_pos(rows, CLAT_ROW_PERIOD);
	*perp_frac = (float)rem;
}

void clat_split_cols(double motion_along, float cell, float col_period, float *cells, float *frac)
{
	double cs = cell;
	double span = (double)CLAT_RATE_STEPS * col_period;
	double m = clat_fmod_pos(motion_along, span * cs);
	double whole = floor(m / cs);
	double rem = m - whole * cs;
	if (whole >= span)
		whole -= span;
	if (rem < 0.0)
		rem = 0.0;
	*cells = (float)whole;
	*frac = (float)rem;
}

void clat_row_shift(float cells, float frac, float cell, float q, float col_period, float *whole, float *sub_px)
{
	/* cells < 64 * 2048 = 2^17 and |q| <= 128, so the product stays below
	 * 2^24 and every step here is exact in float. */
	float aq = fabsf(q);
	float p = clat_mod(cells * aq, CLAT_RATE_STEPS * col_period);
	float w = floorf(p / CLAT_RATE_STEPS);
	float rem = p - w * CLAT_RATE_STEPS;
	float sub = rem / CLAT_RATE_STEPS * cell + frac * aq / CLAT_RATE_STEPS;
	if (q < 0.0f) {
		w = clat_mod(-w, col_period);
		sub = -sub;
	}
	*whole = w;
	*sub_px = sub;
}

void clat_visible_range(float w, float h, float origin_x, float origin_y, float dir_x, float dir_y, float cell,
			float extent, float perp_frac, struct clat_range *out)
{
	const float cx[4] = {0.0f, w, 0.0f, w};
	const float cy[4] = {0.0f, 0.0f, h, h};
	float a_min = INFINITY, a_max = -INFINITY, p_min = INFINITY, p_max = -INFINITY;
	for (int i = 0; i < 4; ++i) {
		float px = cx[i] - origin_x;
		float py = cy[i] - origin_y;
		float a = px * dir_x + py * dir_y;
		float p = -px * dir_y + py * dir_x;
		a_min = fminf(a_min, a);
		a_max = fmaxf(a_max, a);
		p_min = fminf(p_min, p);
		p_max = fmaxf(p_max, p);
	}
	a_min -= extent;
	a_max += extent;
	p_min -= extent;
	p_max += extent;
	out->row_first = floorf((p_min - perp_frac) / cell);
	out->row_count = floorf((p_max - perp_frac) / cell) - out->row_first + 2.0f;
	out->along_min = a_min;
	out->col_count = floorf((a_max - a_min) / cell) + 2.0f;
}

void clat_border_radii(float w, float h, const float widths[4], float radius, float *outer, float inner[4])
{
	float half_min = 0.5f * (w < h ? w : h);
	float rc = radius < 0.0f ? 0.0f : radius;
	if (rc > half_min)
		rc = half_min;
	*outer = rc;
	/* Each inner corner stays concentric with the outer one, shrunk by the
	 * wider of the two sides meeting there. */
	inner[0] = fmaxf(rc - fmaxf(widths[0], widths[1]), 0.0f);
	inner[1] = fmaxf(rc - fmaxf(widths[2], widths[1]), 0.0f);
	inner[2] = fmaxf(rc - fmaxf(widths[2], widths[3]), 0.0f);
	inner[3] = fmaxf(rc - fmaxf(widths[0], widths[3]), 0.0f);
}

void clat_border_make_row(const struct clat_border_frame *fr, float f, float cell, struct clat_border_row *row)
{
	float x0 = f * fr->widths[0];
	float y0 = f * fr->widths[1];
	float x1 = fr->w - f * fr->widths[2];
	float y1 = fr->h - f * fr->widths[3];
	if (x1 < x0) {
		x0 = (x0 + x1) * 0.5f;
		x1 = x0;
	}
	if (y1 < y0) {
		y0 = (y0 + y1) * 0.5f;
		y1 = y0;
	}
	float lim = fminf(x1 - x0, y1 - y0) * 0.5f;
	row->box[0] = x0;
	row->box[1] = y0;
	row->box[2] = x1;
	row->box[3] = y1;
	for (int i = 0; i < 4; ++i) {
		float r = fr->radius + (fr->inner_radii[i] - fr->radius) * f;
		row->rad[i] = fminf(r, lim);
	}
	const float *r = row->rad;
	float ls[4] = {x1 - x0 - r[0] - r[1], y1 - y0 - r[1] - r[2], x1 - x0 - r[2] - r[3], y1 - y0 - r[3] - r[0]};
	float la[4] = {r[1], r[2], r[3], r[0]};
	row->total = 0.0f;
	for (int i = 0; i < 4; ++i) {
		ls[i] = fmaxf(ls[i], 0.0f);
		row->ns[i] = ls[i] >= 0.5f ? fmaxf(floorf(ls[i] / cell + 0.5f), 1.0f) : 0.0f;
		row->na[i] = floorf(la[i] * CLAT_HALF_PI / cell + 0.5f);
		row->total += row->ns[i] + row->na[i];
	}
}

/* Heading along a side at v shapes into it. Next to a sharp corner it turns
 * over the half spacing on either side, so the corner shape points
 * diagonally and moving shapes swing around the corner. */
static float side_angle(float base, float v, float n, float r_prev, float r_next)
{
	float a = base;
	if (r_prev < 0.5f && v < 0.5f)
		a -= (0.5f - v) * CLAT_HALF_PI;
	if (r_next < 0.5f && v > n - 0.5f)
		a += (v - n + 0.5f) * CLAT_HALF_PI;
	return a;
}

static void lerp2(float ax, float ay, float bx, float by, float t, float *x, float *y)
{
	*x = ax + (bx - ax) * t;
	*y = ay + (by - ay) * t;
}

void clat_border_point(const struct clat_border_row *row, float u, float *x, float *y, float *angle)
{
	const float x0 = row->box[0], y0 = row->box[1], x1 = row->box[2], y1 = row->box[3];
	const float *r = row->rad;
	const float *ns = row->ns;
	const float *na = row->na;
	float u0 = 0.0f;

	if (u < u0 + ns[0]) {
		float v = u - u0;
		*angle = side_angle(0.0f, v, ns[0], r[0], r[1]);
		lerp2(x0 + r[0], y0, x1 - r[1], y0, v / ns[0], x, y);
		return;
	}
	u0 += ns[0];
	if (u < u0 + na[0]) {
		float a = -CLAT_HALF_PI + (u - u0) / na[0] * CLAT_HALF_PI;
		*angle = a + CLAT_HALF_PI;
		*x = x1 - r[1] + r[1] * cosf(a);
		*y = y0 + r[1] + r[1] * sinf(a);
		return;
	}
	u0 += na[0];
	if (u < u0 + ns[1]) {
		float v = u - u0;
		*angle = side_angle(CLAT_HALF_PI, v, ns[1], r[1], r[2]);
		lerp2(x1, y0 + r[1], x1, y1 - r[2], v / ns[1], x, y);
		return;
	}
	u0 += ns[1];
	if (u < u0 + na[1]) {
		float a = (u - u0) / na[1] * CLAT_HALF_PI;
		*angle = a + CLAT_HALF_PI;
		*x = x1 - r[2] + r[2] * cosf(a);
		*y = y1 - r[2] + r[2] * sinf(a);
		return;
	}
	u0 += na[1];
	if (u < u0 + ns[2]) {
		float v = u - u0;
		*angle = side_angle(CLAT_PI, v, ns[2], r[2], r[3]);
		lerp2(x1 - r[2], y1, x0 + r[3], y1, v / ns[2], x, y);
		return;
	}
	u0 += ns[2];
	if (u < u0 + na[2]) {
		float a = CLAT_HALF_PI + (u - u0) / na[2] * CLAT_HALF_PI;
		*angle = a + CLAT_HALF_PI;
		*x = x0 + r[3] + r[3] * cosf(a);
		*y = y1 - r[3] + r[3] * sinf(a);
		return;
	}
	u0 += na[2];
	if (u < u0 + ns[3]) {
		float v = u - u0;
		*angle = side_angle(3.0f * CLAT_HALF_PI, v, ns[3], r[3], r[0]);
		lerp2(x0, y1 - r[3], x0, y0 + r[0], v / ns[3], x, y);
		return;
	}
	u0 += ns[3];
	float a = CLAT_PI + (u - u0) / fmaxf(na[3], 1.0f) * CLAT_HALF_PI;
	*angle = a + CLAT_HALF_PI;
	*x = x0 + r[0] + r[0] * cosf(a);
	*y = y0 + r[0] + r[0] * sinf(a);
}

double clat_border_row_phase(double march, float row, float cell, float offset_along, bool alternating,
			     float drift_amount, float drift_seed, bool stagger, float total)
{
	if (total < 0.5f)
		return 0.0;
	bool odd = fmodf(row, 2.0f) > 0.5f;
	double m = march;
	if (alternating && odd)
		m = -m;
	if (drift_amount > 0.0f) {
		float s = (clat_hash(row, 7.0f, drift_seed) - 0.5f) * 2.0f * drift_amount;
		m *= 1.0 + (double)s;
	}
	double ph = (m + (double)offset_along) / (double)cell;
	if (stagger && odd)
		ph += 0.5;
	return clat_fmod_pos(ph, (double)total);
}

void clat_topo_octave_offsets(double dx, double dy, double period, float out[CLAT_TOPO_OCTAVES][4])
{
	double ox = dx, oy = dy;
	for (int k = 0; k < CLAT_TOPO_OCTAVES; ++k) {
		double fx = floor(ox), fy = floor(oy);
		out[k][0] = (float)clat_fmod_pos(fx, period);
		out[k][1] = (float)clat_fmod_pos(fy, period);
		out[k][2] = (float)(ox - fx);
		out[k][3] = (float)(oy - fy);
		double nx = ox * 1.6 - oy * 1.2 + 31.4;
		double ny = ox * 1.2 + oy * 1.6 + 31.4;
		/* M has entries in fifths, so M^j maps multiples of 5^4 periods
		 * to whole periods for every remaining octave: wrapping by that
		 * keeps the values bounded without changing any lattice index
		 * modulo the period. */
		ox = clat_fmod_pos(nx, period * 625.0);
		oy = clat_fmod_pos(ny, period * 625.0);
	}
}
