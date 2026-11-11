/*
Constellations - Text Tiles row motion math
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "text-tiles-math.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double ctt_wrap(double x, double m)
{
	if (!(m > 0.0))
		return 0.0;
	double r = fmod(x, m);
	if (r < 0.0)
		r += m;
	/* fmod of a tiny negative x can round up to exactly m. */
	if (r >= m)
		r = 0.0;
	return r;
}

/* Chris Wellons' lowbias32: a cheap integer hash with good avalanche. */
static uint32_t hash32(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

double ctt_row_hash(int row, uint32_t seed, uint32_t salt)
{
	uint32_t h = hash32((uint32_t)row ^ hash32(seed ^ hash32(salt * 0x9e3779b9U + 0x632be5abU)));
	return (double)(h >> 8) / 16777216.0;
}

int ctt_row_sign(const struct ctt_motion *m, int row)
{
	switch (m->direction) {
	case CTT_DIR_ALTERNATE:
		return (row & 1) ? -1 : 1;
	case CTT_DIR_RANDOM:
		return ctt_row_hash(row, m->seed, 1) < 0.5 ? -1 : 1;
	default:
		return 1;
	}
}

static double clamp01(double v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

double ctt_row_base_speed(const struct ctt_motion *m, int row)
{
	double v = clamp01(m->variation);
	double mult = 1.0 + v * (2.0 * ctt_row_hash(row, m->seed, 2) - 1.0);
	return m->speed * (double)ctt_row_sign(m, row) * mult;
}

double ctt_row_speed(const struct ctt_motion *m, int row, double t)
{
	double base = ctt_row_base_speed(m, row);
	if (!m->wander || m->wander_rate <= 0.0)
		return base;
	double amount = clamp01(m->wander_amount);
	/* Two sines at an irrational ratio so the swing never visibly
	 * repeats; each row gets its own rate and phases. Both average to
	 * zero, so the factor averages 1 and stays within [1 - a, 1 + a]. */
	double w1 = 2.0 * M_PI * m->wander_rate * (0.75 + 0.5 * ctt_row_hash(row, m->seed, 3));
	double w2 = w1 * 1.6180339887;
	double p1 = 2.0 * M_PI * ctt_row_hash(row, m->seed, 4);
	double p2 = 2.0 * M_PI * ctt_row_hash(row, m->seed, 5);
	double swing = 0.6 * sin(w1 * t + p1) + 0.4 * sin(w2 * t + p2);
	return base * (1.0 + amount * swing);
}

double ctt_row_start(int row, double stagger, bool random_offset, uint32_t seed, double period)
{
	double start = stagger * (double)row * period;
	if (random_offset)
		start += ctt_row_hash(row, seed, 6) * period;
	return start;
}

int ctt_row_range(double width, double height, double pitch, double text_h, int *first)
{
	*first = 0;
	if (!(pitch > 0.0) || !(text_h > 0.0) || !(width > 0.0) || !(height > 0.0))
		return 0;
	/* Half the canvas diagonal reaches every corner at any rotation. */
	double reach = 0.5 * sqrt(width * width + height * height) + 0.5 * text_h;
	double lo = ceil(-reach / pitch);
	double hi = floor(reach / pitch);
	if (lo < -CTT_ROW_HALF)
		lo = -CTT_ROW_HALF;
	if (hi > CTT_ROW_HALF)
		hi = CTT_ROW_HALF;
	if (hi < lo)
		return 0;
	*first = (int)lo;
	return (int)(hi - lo) + 1;
}

/* Felzenszwalb and Huttenlocher's exact 1D squared distance transform of f,
 * n samples `stride` apart, in place. v (n ints) and z (n + 1 floats) and d
 * (n floats) are scratch. */
static void edt_1d(float *f, int n, int stride, int *v, float *z, float *d)
{
	int k = 0;
	v[0] = 0;
	z[0] = -INFINITY;
	z[1] = INFINITY;
	for (int q = 1; q < n; ++q) {
		/* Where the parabola rooted at q overtakes the lowest one so far;
		 * z[0] is -inf, so this stops at k = 0 at the latest. */
		float fq = f[q * stride] + (float)q * (float)q;
		float s;
		for (;;) {
			int p = v[k];
			s = (fq - (f[p * stride] + (float)p * (float)p)) / (float)(2 * (q - p));
			if (s > z[k])
				break;
			--k;
		}
		++k;
		v[k] = q;
		z[k] = s;
		z[k + 1] = INFINITY;
	}
	k = 0;
	for (int q = 0; q < n; ++q) {
		while (z[k + 1] < (float)q)
			++k;
		float dq = (float)(q - v[k]);
		d[q] = dq * dq + f[v[k] * stride];
	}
	for (int q = 0; q < n; ++q)
		f[q * stride] = d[q];
}

/* Squared distance from every texel to the nearest texel holding 0. */
static void edt_2d(float *grid, int w, int h, int *v, float *z, float *d)
{
	for (int x = 0; x < w; ++x)
		edt_1d(grid + x, h, w, v, z, d);
	for (int y = 0; y < h; ++y)
		edt_1d(grid + (size_t)y * (size_t)w, w, 1, v, z, d);
}

bool ctt_sdf_build(const uint8_t *coverage, int w, int h, float texel_px, float spread, uint16_t *out)
{
	if (w <= 0 || h <= 0)
		return true;
	size_t n = (size_t)w * (size_t)h;
	int m = w > h ? w : h;
	/* Larger than any real squared distance, small enough that sums stay
	 * exact in float. */
	const float far = 1e20f;
	float *to_in = malloc(sizeof(float) * n);
	float *to_out = malloc(sizeof(float) * n);
	int *v = malloc(sizeof(int) * (size_t)m);
	float *z = malloc(sizeof(float) * (size_t)(m + 1));
	float *d = malloc(sizeof(float) * (size_t)m);
	bool ok = to_in && to_out && v && z && d;
	if (ok) {
		for (size_t i = 0; i < n; ++i) {
			bool inside = coverage[i] >= 128;
			to_in[i] = inside ? 0.0f : far;
			to_out[i] = inside ? far : 0.0f;
		}
		edt_2d(to_in, w, h, v, z, d);
		edt_2d(to_out, w, h, v, z, d);
		for (size_t i = 0; i < n; ++i) {
			uint8_t c = coverage[i];
			float sd; /* texels, positive outside */
			if (c > 0 && c < 255) {
				/* An anti-aliased texel: its coverage places the
				 * edge more precisely than the binary transform. */
				sd = 0.5f - (float)c / 255.0f;
			} else if (c >= 128) {
				sd = -(sqrtf(to_out[i]) - 0.5f);
			} else {
				sd = sqrtf(to_in[i]) - 0.5f;
			}
			float dpx = sd * texel_px;
			if (dpx > spread)
				dpx = spread;
			if (dpx < -spread)
				dpx = -spread;
			float e = 0.5f - dpx / (2.0f * spread);
			out[i] = (uint16_t)lrintf(e * 65535.0f);
		}
	}
	free(to_in);
	free(to_out);
	free(v);
	free(z);
	free(d);
	return ok;
}

float ctt_sdf_decode(uint16_t v, float spread)
{
	return (0.5f - (float)v / 65535.0f) * 2.0f * spread;
}
