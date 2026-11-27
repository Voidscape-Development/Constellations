/*
Constellations - Palette time-of-day math
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Kept free of libobs so tests/test-palette.c can check it on its own. */

#pragma once

#include <stdint.h>

static inline float cpal_wrap24(float h)
{
	h -= 24.0f * (float)(int)(h / 24.0f);
	return h < 0.0f ? h + 24.0f : h;
}

static inline float cpal_sat(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/*
 * How much of the day palette shows at `hour` (0..24, local time): 1 between
 * sunrise and sunset, 0 through the night, and a smooth fade of `fade` hours
 * centred on each. Measured from sunrise round the clock, so a day longer
 * than twelve hours, or one that wraps past midnight, works the same way.
 */
static inline float cpal_day_weight(float hour, float sunrise, float sunset, float fade)
{
	float x = cpal_wrap24(hour - sunrise);
	float len = cpal_wrap24(sunset - sunrise);
	float f = fade > 0.01f ? fade : 0.01f;
	/* past the middle of the night, count back towards the next sunrise */
	if (x > (len + 24.0f) * 0.5f)
		x -= 24.0f;
	float w = cpal_sat(x / f + 0.5f);
	float down = cpal_sat((len - x) / f + 0.5f);
	if (down < w)
		w = down;
	return w * w * (3.0f - 2.0f * w);
}

/* Mixes two 0xAABBGGRR colors channel by channel. */
static inline uint32_t cpal_mix(uint32_t a, uint32_t b, float t)
{
	uint32_t out = 0;
	for (int s = 0; s < 32; s += 8) {
		float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
		out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
	}
	return out;
}
