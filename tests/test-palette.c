/*
 * Checks for the palette's time-of-day drift (src/palette-math.h) and the ink
 * transition's schedule (src/ink-math.h), neither of which needs libobs.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "palette-math.h"
#include "ink-math.h"

static int failures;

#define CHECK(cond, ...)                              \
	do {                                          \
		if (!(cond)) {                        \
			failures++;                   \
			fprintf(stderr, "FAIL: ");    \
			fprintf(stderr, __VA_ARGS__); \
			fputc('\n', stderr);          \
		}                                     \
	} while (0)

static void check_day_weight(void)
{
	/* a 7-19 day, one hour fades, and a day longer than twelve hours */
	CHECK(cpal_day_weight(12.0f, 7.0f, 19.0f, 1.0f) == 1.0f, "noon is not full day");
	CHECK(cpal_day_weight(0.0f, 7.0f, 19.0f, 1.0f) == 0.0f, "midnight is not full night");
	CHECK(fabsf(cpal_day_weight(7.0f, 7.0f, 19.0f, 1.0f) - 0.5f) < 1e-4f, "sunrise is not half way");
	CHECK(fabsf(cpal_day_weight(19.0f, 7.0f, 19.0f, 1.0f) - 0.5f) < 1e-4f, "sunset is not half way");
	CHECK(cpal_day_weight(18.0f, 5.0f, 21.0f, 1.0f) == 1.0f, "a long day loses its evening");
	CHECK(cpal_day_weight(3.0f, 5.0f, 21.0f, 1.0f) == 0.0f, "a long day loses its night");
	/* a night shift: day from 22 to 6, across midnight */
	CHECK(cpal_day_weight(1.0f, 22.0f, 6.0f, 1.0f) == 1.0f, "a day across midnight is dark at 1");
	CHECK(cpal_day_weight(12.0f, 22.0f, 6.0f, 1.0f) == 0.0f, "a day across midnight is light at noon");

	/* continuous and within 0..1 round the clock */
	float prev = cpal_day_weight(0.0f, 7.0f, 19.0f, 1.5f);
	for (int i = 1; i <= 24 * 600; ++i) {
		float w = cpal_day_weight((float)i / 600.0f, 7.0f, 19.0f, 1.5f);
		CHECK(w >= 0.0f && w <= 1.0f, "weight %f out of range", w);
		CHECK(fabsf(w - prev) < 0.01f, "weight jumps at %.3f h", (float)i / 600.0f);
		prev = w;
	}

	CHECK(cpal_mix(0xFF000000, 0xFFFFFFFF, 0.0f) == 0xFF000000, "mix at 0 is not the first color");
	CHECK(cpal_mix(0xFF000000, 0xFFFFFFFF, 1.0f) == 0xFFFFFFFF, "mix at 1 is not the second color");
	CHECK(cpal_mix(0xFF000000, 0xFF0000FF, 0.5f) == 0xFF000080, "mix is not channel by channel");
}

static void check_ink(void)
{
	const float w = 640.0f, h = 360.0f;
	srand(5);
	for (int trial = 0; trial < 200; ++trial) {
		float xy[INK_MAX_DROPS * 2];
		int count = 1 + trial % INK_MAX_DROPS;
		for (int k = 0; k < count * 2; ++k)
			xy[k] = ((float)rand() / RAND_MAX * 2.0f - 0.5f) * (k % 2 ? h : w);

		/* the bound holds on a grid much finer than the one it samples */
		float reach = ink_reach(xy, count, w, h), worst = 0.0f;
		for (float y = 0.0f; y <= h; y += 2.0f)
			for (float x = 0.0f; x <= w; x += 2.0f) {
				float near = 1e30f;
				for (int k = 0; k < count; ++k)
					near = fminf(near, hypotf(x - xy[2 * k], y - xy[2 * k + 1]));
				worst = fmaxf(worst, near);
			}
		CHECK(worst <= reach, "trial %d: a pixel %.2f from its drop is past the reach %.2f", trial, worst,
		      reach);

		/* the field is d / reach + rough / 4 * n with n in -1..1; the first
		 * front leaves every pixel (and its halo) untouched and the last
		 * leaves every pixel behind the ink */
		float rough = (float)(trial % 5) / 4.0f, edge = 40.0f / reach, aa = 1.5f / reach;
		float lo = ink_front(0.0f, rough, edge, aa), hi = ink_front(1.0f, rough, edge, aa);
		CHECK(-0.25f * rough - lo >= edge + aa, "trial %d: the first frame shows the halo", trial);
		CHECK(hi - edge - (1.0f + 0.25f * rough) >= aa, "trial %d: the last frame shows ink", trial);
	}
	for (int e = 0; e < 3; ++e)
		CHECK(ink_ease(e, 0.0f) == 0.0f && ink_ease(e, 1.0f) == 1.0f, "easing %d does not span 0..1", e);
}

int main(void)
{
	check_day_weight();
	check_ink();
	if (failures)
		fprintf(stderr, "%d failures\n", failures);
	return failures ? 1 : 0;
}
