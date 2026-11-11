/*
Constellations - unit tests for the Text Tiles row motion math
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "text-tiles-math.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
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

static struct ctt_motion motion(int direction)
{
	struct ctt_motion m = {0};
	m.speed = 60.0;
	m.direction = direction;
	m.seed = 1234;
	return m;
}

static void test_wrap(void)
{
	CHECK(ctt_wrap(5.0, 3.0) == 2.0, "5 mod 3");
	CHECK(ctt_wrap(-1.0, 3.0) == 2.0, "-1 mod 3");
	CHECK(ctt_wrap(-3.0, 3.0) == 0.0, "-3 mod 3");
	CHECK(ctt_wrap(7.0, 0.0) == 0.0, "mod 0");
	double r = ctt_wrap(-1e-300, 10.0);
	CHECK(r >= 0.0 && r < 10.0, "tiny negative wraps into range: %g", r);
}

static void test_hash(void)
{
	double sum = 0.0;
	int buckets[10] = {0};
	const int n = 20000;
	for (int row = -n / 2; row < n / 2; ++row) {
		double h = ctt_row_hash(row, 99, 2);
		CHECK(h >= 0.0 && h < 1.0, "hash out of range: %f", h);
		sum += h;
		buckets[(int)(h * 10.0)]++;
	}
	CHECK(fabs(sum / n - 0.5) < 0.01, "hash mean %f", sum / n);
	for (int i = 0; i < 10; ++i)
		CHECK(abs(buckets[i] - n / 10) < n / 50, "bucket %d has %d of %d", i, buckets[i], n);
	CHECK(ctt_row_hash(5, 1, 2) == ctt_row_hash(5, 1, 2), "hash is stable");
	CHECK(ctt_row_hash(5, 1, 2) != ctt_row_hash(5, 2, 2), "seed changes the hash");
	CHECK(ctt_row_hash(5, 1, 2) != ctt_row_hash(5, 1, 3), "salt changes the hash");
}

static void test_directions(void)
{
	struct ctt_motion same = motion(CTT_DIR_SAME);
	struct ctt_motion alt = motion(CTT_DIR_ALTERNATE);
	struct ctt_motion rnd = motion(CTT_DIR_RANDOM);
	int forward = 0;
	for (int row = -50; row <= 50; ++row) {
		CHECK(ctt_row_base_speed(&same, row) == 60.0, "same row %d", row);
		double expect = (row & 1) ? -60.0 : 60.0;
		CHECK(ctt_row_base_speed(&alt, row) == expect, "alternating row %d: %f", row,
		      ctt_row_base_speed(&alt, row));
		double s = ctt_row_base_speed(&rnd, row);
		CHECK(fabs(s) == 60.0, "random row %d keeps its speed: %f", row, s);
		forward += s > 0.0;
	}
	CHECK(forward > 30 && forward < 71, "random directions are mixed: %d of 101 forward", forward);

	/* A negative speed reverses every row. */
	alt.speed = -60.0;
	CHECK(ctt_row_base_speed(&alt, 0) == -60.0 && ctt_row_base_speed(&alt, 1) == 60.0, "negative speed");
}

static void test_variation(void)
{
	struct ctt_motion m = motion(CTT_DIR_SAME);
	m.variation = 0.4;
	double lo = 1e9, hi = -1e9;
	for (int row = -500; row <= 500; ++row) {
		double s = ctt_row_base_speed(&m, row);
		lo = fmin(lo, s);
		hi = fmax(hi, s);
	}
	CHECK(lo >= 60.0 * 0.6 - 1e-9 && hi <= 60.0 * 1.4 + 1e-9, "variation bounds [%f, %f]", lo, hi);
	CHECK(hi - lo > 60.0 * 0.7, "variation spreads the speeds [%f, %f]", lo, hi);

	m.variation = 5.0;
	for (int row = -100; row <= 100; ++row)
		CHECK(ctt_row_base_speed(&m, row) >= 0.0, "variation is clamped, so no row turns around");
}

static void test_wander(void)
{
	struct ctt_motion m = motion(CTT_DIR_ALTERNATE);
	m.variation = 0.3;
	CHECK(ctt_row_speed(&m, 3, 12.5) == ctt_row_base_speed(&m, 3), "no wander, steady speed");

	m.wander = true;
	m.wander_amount = 0.5;
	m.wander_rate = 0.2;
	for (int row = -20; row <= 20; ++row) {
		double base = ctt_row_base_speed(&m, row);
		double dist = 0.0, lo = 1e9, hi = -1e9;
		const double dt = 1.0 / 60.0;
		const int steps = 60 * 600;
		for (int i = 0; i < steps; ++i) {
			double s = ctt_row_speed(&m, row, i * dt);
			dist += s * dt;
			lo = fmin(lo, s / base);
			hi = fmax(hi, s / base);
		}
		double mean = dist / (steps * dt);
		CHECK(fabs(mean / base - 1.0) < 0.03, "row %d wander keeps the pace: %f vs %f", row, mean, base);
		CHECK(lo >= 0.5 - 1e-9 && hi <= 1.5 + 1e-9, "row %d wander bounds [%f, %f]", row, lo, hi);
		CHECK(hi - lo > 0.5, "row %d actually wanders [%f, %f]", row, lo, hi);
	}
}

static void test_start(void)
{
	CHECK(ctt_row_start(0, 0.25, false, 1, 400.0) == 0.0, "row 0 has no stagger");
	CHECK(ctt_row_start(1, 0.25, false, 1, 400.0) == 100.0, "row 1 staggered");
	CHECK(ctt_row_start(-2, 0.25, false, 1, 400.0) == -200.0, "row -2 staggered");
	for (int row = -10; row <= 10; ++row) {
		double extra = ctt_row_start(row, 0.0, true, 7, 400.0);
		CHECK(extra >= 0.0 && extra < 400.0, "random offset within a period: %f", extra);
	}
}

static void test_range(void)
{
	int first;
	/* 1920x1080: half diagonal ~1101.5, plus half a 100 px row. */
	int count = ctt_row_range(1920.0, 1080.0, 120.0, 100.0, &first);
	CHECK(first == -9 && count == 19, "range first %d count %d", first, count);
	CHECK(ctt_row_range(1920.0, 1080.0, 0.0, 100.0, &first) == 0, "zero pitch");
	CHECK(ctt_row_range(0.0, 1080.0, 120.0, 100.0, &first) == 0, "zero width");
	count = ctt_row_range(8192.0, 8192.0, 1.0, 1.0, &first);
	CHECK(first == -CTT_ROW_HALF && count == CTT_ROW_SLOTS, "range is clamped: %d %d", first, count);
}

/* Coverage of a disc, 16x16 supersampled per texel. */
static void disc_coverage(uint8_t *cov, int w, int h, double cx, double cy, double r)
{
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			int in = 0;
			for (int sy = 0; sy < 16; ++sy)
				for (int sx = 0; sx < 16; ++sx) {
					double px = x + (sx + 0.5) / 16.0, py = y + (sy + 0.5) / 16.0;
					in += hypot(px - cx, py - cy) <= r;
				}
			cov[y * w + x] = (uint8_t)lrint(in * 255.0 / 256.0);
		}
	}
}

static void test_sdf_disc(void)
{
	enum { W = 96, H = 80 };
	static uint8_t cov[W * H];
	static uint16_t sdf[W * H];
	const double cx = 47.3, cy = 39.8, r = 24.6;
	const float texel_px = 0.5f, spread = 8.0f;
	disc_coverage(cov, W, H, cx, cy, r);
	CHECK(ctt_sdf_build(cov, W, H, texel_px, spread, sdf), "build");
	double worst = 0.0;
	int inside = 0, outside = 0, clamped = 0;
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			double truth = (hypot(x + 0.5 - cx, y + 0.5 - cy) - r) * texel_px;
			float d = ctt_sdf_decode(sdf[y * W + x], spread);
			if (fabs(truth) < spread - 0.5) {
				worst = fmax(worst, fabs(d - truth));
				inside += truth < -1.0;
				outside += truth > 1.0;
			} else {
				clamped++;
				double want = fmax(-spread, fmin(spread, truth));
				CHECK(fabs(d - want) < 0.25, "texel near the spread: %f vs %f", d, want);
			}
			if (truth < -1.0)
				CHECK(d < 0.0f, "inside is negative at %d,%d: %f", x, y, d);
			if (truth > 1.0)
				CHECK(d > 0.0f, "outside is positive at %d,%d: %f", x, y, d);
		}
	}
	/* A quarter of a canvas px: well under the one px the edge is
	 * anti-aliased over. */
	CHECK(worst < 0.25, "disc distance error %f px", worst);
	CHECK(inside > 100 && outside > 100 && clamped > 100, "samples %d %d %d", inside, outside, clamped);
}

static void test_sdf_edge(void)
{
	/* A vertical edge 20.25 texels in: distances along x are exact. */
	enum { W = 48, H = 8 };
	uint8_t cov[W * H];
	uint16_t sdf[W * H];
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			cov[y * W + x] = x < 20 ? 255 : (x == 20 ? 64 : 0);
	CHECK(ctt_sdf_build(cov, W, H, 1.0f, 16.0f, sdf), "build");
	for (int x = 0; x < W; ++x) {
		double truth = x + 0.5 - (20.0 + 64.0 / 255.0);
		if (fabs(truth) > 15.0)
			continue;
		float d = ctt_sdf_decode(sdf[3 * W + x], 16.0f);
		CHECK(fabs(d - truth) < 0.3, "edge x=%d: %f vs %f", x, d, truth);
	}
}

static void test_sdf_extremes(void)
{
	uint8_t empty[64] = {0}, full[64];
	uint16_t sdf[64];
	for (int i = 0; i < 64; ++i)
		full[i] = 255;
	CHECK(ctt_sdf_build(empty, 8, 8, 1.0f, 4.0f, sdf), "build empty");
	for (int i = 0; i < 64; ++i)
		CHECK(ctt_sdf_decode(sdf[i], 4.0f) > 3.99f, "empty is all outside");
	CHECK(ctt_sdf_build(full, 8, 8, 1.0f, 4.0f, sdf), "build full");
	for (int i = 0; i < 64; ++i)
		CHECK(ctt_sdf_decode(sdf[i], 4.0f) < -3.99f, "full is all inside");
	CHECK(ctt_sdf_build(empty, 0, 0, 1.0f, 4.0f, sdf), "zero size is fine");
}

int main(void)
{
	test_wrap();
	test_hash();
	test_directions();
	test_variation();
	test_wander();
	test_start();
	test_range();
	test_sdf_disc();
	test_sdf_edge();
	test_sdf_extremes();
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("all text tiles tests passed\n");
	return 0;
}
