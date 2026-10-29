/*
Constellations - unit tests for the pattern placement math
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "lattice.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(cond, ...)                                              \
	do {                                                          \
		if (!(cond)) {                                        \
			fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__);                 \
			fputc('\n', stderr);                          \
			failures++;                                   \
		}                                                     \
	} while (0)

static void test_hash(void)
{
	double sum = 0.0;
	int n = 0;
	int buckets[10] = {0};
	for (int y = 0; y < 200; ++y) {
		for (int x = 0; x < 200; ++x) {
			float h = clat_hash((float)x, (float)y, 17.31f);
			CHECK(h >= 0.0f && h < 1.0f, "hash out of range: %f", h);
			sum += h;
			buckets[(int)(h * 10.0f) % 10]++;
			n++;
		}
	}
	CHECK(fabs(sum / n - 0.5) < 0.01, "hash mean %f", sum / n);
	for (int i = 0; i < 10; ++i)
		CHECK(abs(buckets[i] - n / 10) < n / 50, "hash bucket %d has %d of %d", i, buckets[i], n);
	/* Still well distributed at the far end of the wrapped index range. */
	sum = 0.0;
	n = 0;
	for (int y = 4000; y < 4096; ++y)
		for (int x = 1950; x < 2048; ++x, ++n)
			sum += clat_hash((float)x, (float)y, 1000.0f + 31.0f * 17.31f);
	CHECK(fabs(sum / n - 0.5) < 0.02, "hash mean at high indices %f", sum / n);
}

static void test_mod(void)
{
	for (float m = 1.0f; m <= 2048.0f * 64.0f; m *= 3.0f) {
		for (float mag = 1.0f; mag < 16777216.0f; mag = mag * 1.7f + 12345.0f) {
			float x = (fmodf(mag, 2.0f) < 1.0f) ? -floorf(mag) : floorf(mag);
			float r = clat_mod(x, m);
			double ref = fmod(x, m);
			if (ref < 0.0)
				ref += m;
			CHECK(r == (float)ref, "mod(%f, %f) = %f, want %f", x, m, r, ref);
		}
	}
}

static void test_col_period(void)
{
	for (uint32_t n = 0; n <= 32; ++n) {
		float p = clat_col_period(n);
		float k = n ? (float)n : 1.0f;
		CHECK(p <= 2048.0f && p > 2048.0f - k, "period %f for %u", p, n);
		CHECK(fmodf(p, k) == 0.0f, "period %f not a multiple of %u", p, n);
	}
}

static void test_rate_q(void)
{
	for (int r = 0; r < 4096; ++r) {
		CHECK(clat_rate_q((float)r, 0.0f, 3.0f) == 64.0f, "no drift must give 64");
		float q = clat_rate_q((float)r, 1.0f, 3.0f);
		CHECK(q >= 0.0f && q <= 128.0f && q == floorf(q), "q %f", q);
		float q2 = clat_rate_q((float)r, 0.25f, 3.0f);
		CHECK(q2 >= 48.0f && q2 <= 80.0f, "q %f for 25%% drift", q2);
	}
}

/* The shader's exact-arithmetic row shift must match a double computation
 * of motion * q / 64, modulo the column period, at any distance scrolled. */
static void test_row_shift(void)
{
	const float cells_px[] = {5.0f, 13.7f, 50.0f, 333.3f, 1000.0f};
	const uint32_t steps[] = {1, 3, 7, 32};
	const float qs[] = {64.0f, -64.0f, 0.0f, 1.0f, 37.0f, -101.0f, 128.0f};
	double worst = 0.0;
	for (size_t ci = 0; ci < sizeof(cells_px) / sizeof(cells_px[0]); ++ci) {
		float cs = cells_px[ci];
		for (size_t si = 0; si < sizeof(steps) / sizeof(steps[0]); ++si) {
			float period = clat_col_period(steps[si]);
			double span = (double)period * cs;
			for (size_t qi = 0; qi < sizeof(qs) / sizeof(qs[0]); ++qi) {
				float q = qs[qi];
				for (int k = 0; k < 400; ++k) {
					/* -1e9 .. 1e9 px, sampled unevenly. */
					double m = -1.0e9 + 2.0e9 * (double)k / 400.0 + (double)(k * k) * 3.17;
					float cells, frac, whole, sub;
					clat_split_cols(m, cs, period, &cells, &frac);
					clat_row_shift(cells, frac, cs, q, period, &whole, &sub);
					CHECK(whole >= 0.0f && whole < period && whole == floorf(whole), "whole %f",
					      whole);
					double got = clat_fmod_pos((double)whole * cs + sub, span);
					double want = clat_fmod_pos(m * q / 64.0, span);
					double err = fabs(got - want);
					if (err > span * 0.5)
						err = span - err;
					if (err > worst)
						worst = err;
					CHECK(err < 0.01 + cs * 1e-5, "shift m=%g cs=%g q=%g period=%g: got %f want %f",
					      m, cs, q, period, got, want);
				}
			}
		}
	}
	printf("row shift: worst error %.6f px\n", worst);
}

static void test_split_continuity(void)
{
	/* A whole period later the split must be identical, so wrapping is
	 * invisible. */
	float cs = 37.0f;
	float period = clat_col_period(3);
	double span = 64.0 * period * cs;
	for (double m = 0.0; m < 1.0e7; m += 123457.89) {
		float c1, f1, c2, f2;
		clat_split_cols(m, cs, period, &c1, &f1);
		clat_split_cols(m + 5.0 * span, cs, period, &c2, &f2);
		CHECK(c1 == c2 && fabsf(f1 - f2) < 1e-3f, "cols differ after wrap at %f", m);
		float r1, p1, r2, p2;
		clat_split_rows(m, cs, &r1, &p1);
		clat_split_rows(m + 7.0 * CLAT_ROW_PERIOD * cs, cs, &r2, &p2);
		CHECK(r1 == r2 && fabsf(p1 - p2) < 1e-3f, "rows differ after wrap at %f", m);
		CHECK(p1 >= 0.0f && p1 < cs, "perp frac %f", p1);
	}
}

static void test_visible_range(void)
{
	const float w = 1280.0f, h = 720.0f;
	for (float ang = 0.0f; ang < 6.3f; ang += 0.37f) {
		float dx = cosf(ang), dy = sinf(ang);
		float ox = 300.0f, oy = -50.0f, cs = 41.0f, ext = 30.0f, pf = 12.5f;
		struct clat_range rg;
		clat_visible_range(w, h, ox, oy, dx, dy, cs, ext, pf, &rg);
		/* Every cell whose shape can touch the canvas must be in range. */
		for (int row = -200; row < 200; ++row) {
			for (int col = -200; col < 200; ++col) {
				float a = (float)col * cs + 3.3f; /* arbitrary along shift */
				float p = (float)row * cs + pf;
				float sx = ox + dx * a - dy * p;
				float sy = oy + dy * a + dx * p;
				float ddx = fmaxf(fmaxf(-sx, sx - w), 0.0f);
				float ddy = fmaxf(fmaxf(-sy, sy - h), 0.0f);
				bool touches = hypotf(ddx, ddy) < ext;
				if (!touches)
					continue;
				CHECK(row >= rg.row_first && row < rg.row_first + rg.row_count, "row %d outside", row);
				float first_col = ceilf((rg.along_min - 3.3f) / cs);
				CHECK(col >= first_col && col < first_col + rg.col_count, "col %d outside", col);
			}
		}
	}
}

static void test_border(void)
{
	struct clat_border_frame fr = {.w = 640.0f, .h = 360.0f, .widths = {40.0f, 40.0f, 40.0f, 40.0f}, .radius = 0.0f};
	float outer;
	clat_border_radii(fr.w, fr.h, fr.widths, 60.0f, &outer, fr.inner_radii);
	CHECK(outer == 60.0f && fr.inner_radii[0] == 20.0f, "radii %f %f", outer, fr.inner_radii[0]);
	clat_border_radii(fr.w, fr.h, fr.widths, 0.0f, &fr.radius, fr.inner_radii);

	struct clat_border_row row;
	clat_border_make_row(&fr, 0.5f, 25.0f, &row);
	CHECK(row.na[0] == 0.0f && row.ns[0] > 0.0f, "sharp corners hold no arc shapes");
	CHECK(row.total == row.ns[0] + row.ns[1] + row.ns[2] + row.ns[3], "total");

	/* Sharp corners: u = 0 is the top-left corner, pointing diagonally. */
	float x, y, a;
	clat_border_point(&row, 0.0f, &x, &y, &a);
	CHECK(fabsf(x - 20.0f) < 1e-3f && fabsf(y - 20.0f) < 1e-3f, "start %f %f", x, y);
	CHECK(fabsf(a + 0.785398f) < 1e-3f, "corner heading %f", a);
	/* Walking the path, consecutive shapes are evenly spaced and close. */
	float px, py;
	clat_border_point(&row, 0.0f, &px, &py, &a);
	for (float u = 1.0f; u < row.total; u += 1.0f) {
		clat_border_point(&row, u, &x, &y, &a);
		float d = hypotf(x - px, y - py);
		CHECK(d > 25.0f * 0.6f && d < 25.0f * 1.6f, "spacing %f at %f", d, u);
		px = x;
		py = y;
	}

	/* Rounded frame: corners get arc shapes and the path stays continuous. */
	clat_border_radii(fr.w, fr.h, fr.widths, 80.0f, &fr.radius, fr.inner_radii);
	clat_border_make_row(&fr, 0.5f, 20.0f, &row);
	CHECK(row.na[0] > 0.0f, "rounded corners hold arc shapes");
	clat_border_point(&row, 0.0f, &px, &py, &a);
	for (float u = 0.25f; u < row.total; u += 0.25f) {
		clat_border_point(&row, u, &x, &y, &a);
		CHECK(hypotf(x - px, y - py) < 20.0f, "jump at u=%f", u);
		px = x;
		py = y;
	}

	for (double m = -1e6; m < 1e6; m += 7919.3) {
		double ph = clat_border_row_phase(m, 1.0f, 20.0f, 3.0f, true, 0.3f, 2.0f, true, row.total);
		CHECK(ph >= 0.0 && ph < row.total, "phase %f", ph);
	}
	CHECK(clat_border_row_phase(100.0, 0.0f, 20.0f, 0.0f, false, 0.0f, 0.0f, false, 0.0f) == 0.0,
	      "empty row phase");
}

/* The split offsets must give the same lattice cell (mod the period) and
 * the same position inside it as evaluating each octave directly. */
static void test_topo_offsets(void)
{
	const double period = 4096.0;
	const double drifts[][2] = {{0.0, 0.0}, {3.7, -2.2}, {1234.5, -987.25}, {40000.3, 12345.6}, {-7.5e5, 3.1e5}};
	for (size_t d = 0; d < sizeof(drifts) / sizeof(drifts[0]); ++d) {
		float offs[CLAT_TOPO_OCTAVES][4];
		clat_topo_octave_offsets(drifts[d][0], drifts[d][1], period, offs);
		long double ox = drifts[d][0], oy = drifts[d][1];
		for (int k = 0; k < CLAT_TOPO_OCTAVES; ++k) {
			long double fx = floorl(ox), fy = floorl(oy);
			double ix = fmod((double)fx, period), iy = fmod((double)fy, period);
			if (ix < 0.0)
				ix += period;
			if (iy < 0.0)
				iy += period;
			CHECK(offs[k][0] == (float)ix && offs[k][1] == (float)iy, "octave %d cell (%f,%f) want (%f,%f)",
			      k, offs[k][0], offs[k][1], ix, iy);
			CHECK(fabs(offs[k][2] - (double)(ox - fx)) < 1e-4 &&
				      fabs(offs[k][3] - (double)(oy - fy)) < 1e-4,
			      "octave %d fraction", k);
			long double nx = ox * 1.6L - oy * 1.2L + 31.4L;
			long double ny = ox * 1.2L + oy * 1.6L + 31.4L;
			ox = nx;
			oy = ny;
		}
	}
}

int main(void)
{
	test_hash();
	test_mod();
	test_col_period();
	test_rate_q();
	test_row_shift();
	test_split_continuity();
	test_visible_range();
	test_border();
	test_topo_offsets();
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("all lattice tests passed\n");
	return 0;
}
