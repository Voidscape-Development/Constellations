/*
Constellations - Skybox model
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "skybox-model.h"

#include <math.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Matches the lattice period of the shader's noise: every scrolling offset
 * is wrapped to it so the pattern never jumps and floats never grow. */
#define NOISE_PERIOD 64.0

#define HEX(c) {(float)(((c) >> 16) & 0xFF) / 255.0f, (float)(((c) >> 8) & 0xFF) / 255.0f, (float)((c) & 0xFF) / 255.0f}

/* ------------------------------------------------------------ math ---- */

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static float saturatef(float v)
{
	return clampf(v, 0.0f, 1.0f);
}

static float lerpf(float a, float b, float t)
{
	return a + (b - a) * t;
}

static float smoothstepf(float e0, float e1, float x)
{
	float t = saturatef((x - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}

static double wrapd(double v, double period)
{
	v = fmod(v, period);
	if (v < 0.0)
		v += period;
	return v;
}

static struct skybox_rgb rgb(float r, float g, float b)
{
	struct skybox_rgb c = {r, g, b};
	return c;
}

static struct skybox_rgb mix(struct skybox_rgb a, struct skybox_rgb b, float t)
{
	return rgb(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t));
}

static struct skybox_rgb scale(struct skybox_rgb a, float k)
{
	return rgb(a.r * k, a.g * k, a.b * k);
}

static struct skybox_rgb add(struct skybox_rgb a, struct skybox_rgb b)
{
	return rgb(a.r + b.r, a.g + b.g, a.b + b.b);
}

static void put4(float out[4], struct skybox_rgb c, float a)
{
	out[0] = saturatef(c.r);
	out[1] = saturatef(c.g);
	out[2] = saturatef(c.b);
	out[3] = a;
}

static uint32_t rng_next(struct skybox_state *st)
{
	uint32_t x = st->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	st->rng = x ? x : 0x9E3779B9u;
	return st->rng;
}

static float rng_float(struct skybox_state *st)
{
	return (float)(rng_next(st) >> 8) / 16777216.0f;
}

/* Exponentially distributed wait with the given mean, so events arrive at a
 * steady average rate without a visible rhythm. */
static float rng_wait(struct skybox_state *st, float mean)
{
	float u = rng_float(st);
	return -logf(1.0f - u * 0.999f) * mean;
}

/* ---------------------------------------------------------- tables ---- */

static const struct skybox_weather_mix weather_table[SKYBOX_WEATHER_COUNT] = {
	/*               cover  dark   rain   snow  storm  fog    grey */
	[SKYBOX_WEATHER_CLEAR] = {0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
	[SKYBOX_WEATHER_PARTLY] = {0.42f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
	[SKYBOX_WEATHER_CLOUDY] = {0.66f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f},
	[SKYBOX_WEATHER_OVERCAST] = {0.92f, 0.45f, 0.0f, 0.0f, 0.0f, 0.05f, 0.65f},
	[SKYBOX_WEATHER_RAIN] = {0.88f, 0.6f, 1.0f, 0.0f, 0.0f, 0.15f, 0.65f},
	[SKYBOX_WEATHER_STORM] = {0.96f, 0.85f, 1.25f, 0.0f, 1.0f, 0.1f, 0.8f},
	[SKYBOX_WEATHER_SNOW] = {0.82f, 0.25f, 0.0f, 1.0f, 0.0f, 0.2f, 0.55f},
	[SKYBOX_WEATHER_FOG] = {0.3f, 0.1f, 0.0f, 0.0f, 0.0f, 1.0f, 0.45f},
};

static const struct skybox_rgb theme_table[SKYBOX_THEME_COUNT][SKYBOX_PAL_COUNT] = {
	/* day top, day horizon, sunset top, sunset horizon,
	 * night top, night horizon, sun, cloud */
	[SKYBOX_THEME_NATURAL] = {HEX(0x2E6FD8), HEX(0xA9D3F5), HEX(0x3A3F8F), HEX(0xFF8A4C), HEX(0x050B1E),
				  HEX(0x1A2850), HEX(0xFFF3D6), HEX(0xFFFFFF)},
	[SKYBOX_THEME_PASTEL] = {HEX(0x8EC5FF), HEX(0xFDE2F3), HEX(0xB9A6E8), HEX(0xFFC3A0), HEX(0x3B3366),
				 HEX(0x7A6FA8), HEX(0xFFF6E0), HEX(0xFFF4FA)},
	[SKYBOX_THEME_SYNTHWAVE] = {HEX(0x3A1C71), HEX(0xFF6AD5), HEX(0x1A0B3D), HEX(0xFF8C42), HEX(0x0B0221),
				    HEX(0x4B1A6B), HEX(0xFFD319), HEX(0xC774E8)},
	[SKYBOX_THEME_ALIEN] = {HEX(0x1E8C6E), HEX(0xC8F27A), HEX(0x3B1E6E), HEX(0xFF5E8A), HEX(0x021A14),
				HEX(0x0F4A3A), HEX(0xE9FFB0), HEX(0xE0FFE8)},
	/* Monochrome grades the Natural sky down to one tint in the shader. */
	[SKYBOX_THEME_MONOCHROME] = {HEX(0x2E6FD8), HEX(0xA9D3F5), HEX(0x3A3F8F), HEX(0xFF8A4C), HEX(0x050B1E),
				     HEX(0x1A2850), HEX(0xFFF3D6), HEX(0xFFFFFF)},
	[SKYBOX_THEME_CUSTOM] = {HEX(0x2E6FD8), HEX(0xA9D3F5), HEX(0x3A3F8F), HEX(0xFF8A4C), HEX(0x050B1E),
				 HEX(0x1A2850), HEX(0xFFF3D6), HEX(0xFFFFFF)},
};

void skybox_theme_palette(int theme, struct skybox_rgb out[SKYBOX_PAL_COUNT])
{
	if (theme < 0 || theme >= SKYBOX_THEME_COUNT)
		theme = SKYBOX_THEME_NATURAL;
	memcpy(out, theme_table[theme], sizeof(theme_table[theme]));
}

static bool is_rainy(int weather)
{
	return weather == SKYBOX_WEATHER_RAIN || weather == SKYBOX_WEATHER_STORM;
}

static struct skybox_weather_mix weather_lookup(int weather)
{
	if (weather < 0 || weather >= SKYBOX_WEATHER_COUNT)
		weather = SKYBOX_WEATHER_CLEAR;
	return weather_table[weather];
}

static struct skybox_weather_mix weather_lerp(struct skybox_weather_mix a, struct skybox_weather_mix b, float t)
{
	struct skybox_weather_mix m;
	m.cover = lerpf(a.cover, b.cover, t);
	m.dark = lerpf(a.dark, b.dark, t);
	m.rain = lerpf(a.rain, b.rain, t);
	m.snow = lerpf(a.snow, b.snow, t);
	m.storm = lerpf(a.storm, b.storm, t);
	m.fog = lerpf(a.fog, b.fog, t);
	m.grey = lerpf(a.grey, b.grey, t);
	return m;
}

/* ------------------------------------------------------------ time ---- */

struct solar {
	float elev;     /* -1 midnight .. 0 horizon .. 1 noon */
	float sun_f;    /* 0 at sunrise, 1 at sunset (extends past both) */
	float moon_f;   /* 0 at sunset, 1 at sunrise */
	float daylight; /* 0 night .. 1 day */
};

static void day_bounds(const struct skybox_settings *s, double *rise, double *len)
{
	double r = s->sunrise;
	double l = (double)s->sunset - r;
	if (l < 2.0)
		l = 2.0;
	if (l > 22.0)
		l = 22.0;
	*rise = r;
	*len = l;
}

static struct solar solar_at(const struct skybox_settings *s, double hours)
{
	double rise, daylen;
	day_bounds(s, &rise, &daylen);
	double nightlen = 24.0 - daylen;
	double set = rise + daylen;

	/* Center each arc on its own half of the clock so the sun keeps
	 * sinking past sunset (instead of wrapping to the other side). */
	double ds = wrapd(hours - rise + nightlen * 0.5, 24.0) - nightlen * 0.5;
	double dm = wrapd(hours - set + daylen * 0.5, 24.0) - daylen * 0.5;

	struct solar o;
	o.sun_f = (float)(ds / daylen);
	o.moon_f = (float)(dm / nightlen);
	if (o.sun_f >= 0.0f && o.sun_f <= 1.0f)
		o.elev = sinf((float)M_PI * o.sun_f);
	else
		o.elev = -sinf((float)M_PI * saturatef(o.moon_f));
	o.daylight = smoothstepf(-0.18f, 0.25f, o.elev);
	return o;
}

double skybox_preset_hours(int preset, float sunrise, float sunset)
{
	double rise = sunrise;
	double daylen = (double)sunset - rise;
	if (daylen < 2.0)
		daylen = 2.0;
	if (daylen > 22.0)
		daylen = 22.0;
	double set = rise + daylen;
	double nightlen = 24.0 - daylen;

	double h;
	switch (preset) {
	case SKYBOX_PRESET_DAWN:
		h = rise - 0.2;
		break;
	case SKYBOX_PRESET_MORNING:
		h = rise + daylen * 0.2;
		break;
	case SKYBOX_PRESET_AFTERNOON:
		h = rise + daylen * 0.72;
		break;
	case SKYBOX_PRESET_GOLDEN:
		h = set - 0.6;
		break;
	case SKYBOX_PRESET_SUNSET:
		h = set - 0.05;
		break;
	case SKYBOX_PRESET_DUSK:
		h = set + 0.45;
		break;
	case SKYBOX_PRESET_NIGHT:
		h = set + nightlen * 0.25;
		break;
	case SKYBOX_PRESET_MIDNIGHT:
		h = set + nightlen * 0.5;
		break;
	case SKYBOX_PRESET_NOON:
	default:
		h = rise + daylen * 0.5;
		break;
	}
	return wrapd(h, 24.0);
}

double skybox_clock_hours(void)
{
	time_t now = time(NULL);
	struct tm tmv;
#ifdef _WIN32
	localtime_s(&tmv, &now);
#else
	localtime_r(&now, &tmv);
#endif
	return (double)tmv.tm_hour + (double)tmv.tm_min / 60.0 + (double)tmv.tm_sec / 3600.0;
}

double skybox_real_moon_phase(void)
{
	/* Days since the new moon of 2000-01-06 18:14 UTC over the mean
	 * synodic month: accurate to within about a day. */
	double days = ((double)time(NULL) - 947182440.0) / 86400.0;
	return wrapd(days / 29.530588853, 1.0);
}

static double target_hours(const struct skybox_state *st, const struct skybox_settings *s, double clock_hours)
{
	switch (s->time_mode) {
	case SKYBOX_TIME_PRESET:
		return skybox_preset_hours(s->time_preset, s->sunrise, s->sunset);
	case SKYBOX_TIME_CYCLE:
		return st->cycle_hours;
	case SKYBOX_TIME_CLOCK:
		return wrapd(clock_hours + s->clock_offset, 24.0);
	case SKYBOX_TIME_CUSTOM:
	default:
		return wrapd(s->custom_time, 24.0);
	}
}

/* ----------------------------------------------------------- state ---- */

void skybox_state_init(struct skybox_state *st, const struct skybox_settings *s)
{
	memset(st, 0, sizeof(*st));
	st->rng = 0x6D2B79F5u ^ ((uint32_t)s->seed * 2654435761u);
	if (!st->rng)
		st->rng = 1;
	st->cycle_hours = wrapd(s->custom_time, 24.0);
	st->last_mode = s->time_mode;
	st->last_custom = s->custom_time;
	st->hours = target_hours(st, s, skybox_clock_hours());
	st->target_hours = st->hours;
	st->tt_t = 1.0;

	st->weather_target = s->weather;
	st->w_from = st->w_to = st->w_now = weather_lookup(s->weather);
	st->w_t = 1.0f;

	st->next_strike = 1.0f + rng_float(st) * 3.0f;
	st->shoot_wait = rng_wait(st, 60.0f / fmaxf(s->shooting_per_min, 0.05f));
	st->flock_wait = 2.0f + rng_float(st) * 6.0f;
	st->initialized = true;
}

static void tick_time(struct skybox_state *st, const struct skybox_settings *s, float seconds, double clock_hours)
{
	bool mode_changed = s->time_mode != st->last_mode;

	if (s->time_mode == SKYBOX_TIME_CYCLE) {
		if (mode_changed)
			st->cycle_hours = st->hours;
		else if (s->custom_time != st->last_custom)
			st->cycle_hours = wrapd(s->custom_time, 24.0);
		double minutes = s->cycle_minutes > 0.05f ? s->cycle_minutes : 0.05;
		st->cycle_hours = wrapd(st->cycle_hours + (double)seconds * 24.0 / (minutes * 60.0), 24.0);
	}
	st->last_mode = s->time_mode;
	st->last_custom = s->custom_time;

	double target = target_hours(st, s, clock_hours);

	/* Only preset picks glide; a dragged slider, the cycle and the clock
	 * already move smoothly on their own. */
	if (s->time_mode != SKYBOX_TIME_PRESET || s->transition_secs <= 0.0f) {
		st->hours = target;
		st->target_hours = target;
		st->tt_t = 1.0;
		return;
	}

	if (fabs(target - st->target_hours) > 1e-6) {
		st->target_hours = target;
		st->tt_from = st->hours;
		st->tt_delta = wrapd(target - st->hours + 12.0, 24.0) - 12.0; /* shortest way round */
		st->tt_t = 0.0;
	}
	if (st->tt_t < 1.0) {
		st->tt_t += seconds / s->transition_secs;
		if (st->tt_t > 1.0)
			st->tt_t = 1.0;
		float k = smoothstepf(0.0f, 1.0f, (float)st->tt_t);
		st->hours = wrapd(st->tt_from + st->tt_delta * k, 24.0);
	} else {
		st->hours = target;
	}
}

static void tick_weather(struct skybox_state *st, const struct skybox_settings *s, float seconds)
{
	if (s->weather != st->weather_target) {
		if (is_rainy(st->weather_target) && !is_rainy(s->weather)) {
			st->rainbow_total = s->rainbow_secs + s->transition_secs;
			st->rainbow_left = st->rainbow_total;
		} else if (is_rainy(s->weather)) {
			st->rainbow_left = 0.0f;
		}
		st->weather_target = s->weather;
		st->w_from = st->w_now;
		st->w_to = weather_lookup(s->weather);
		st->w_t = 0.0f;
	}

	if (st->w_t < 1.0f) {
		st->w_t = s->transition_secs > 0.0f ? st->w_t + seconds / s->transition_secs : 1.0f;
		if (st->w_t > 1.0f)
			st->w_t = 1.0f;
	}
	st->w_now = weather_lerp(st->w_from, st->w_to, smoothstepf(0.0f, 1.0f, st->w_t));

	if (st->rainbow_left > 0.0f) {
		st->rainbow_left -= seconds;
		if (st->rainbow_left < 0.0f)
			st->rainbow_left = 0.0f;
	}
}

static float wind_x(const struct skybox_settings *s)
{
	return cosf(s->wind_dir_deg * (float)M_PI / 180.0f) * saturatef(s->wind_strength / 100.0f);
}

static float wind_z(const struct skybox_settings *s)
{
	return sinf(s->wind_dir_deg * (float)M_PI / 180.0f) * saturatef(s->wind_strength / 100.0f);
}

/* Per-layer constants shared with the shader's rain_layer()/snow_layer()
 * calls (near, mid, far). Speeds are in cells per second. */
static const float rain_cell_h[3] = {0.22f, 0.15f, 0.10f};
static const float rain_speed_hu[3] = {1.55f, 1.0f, 0.62f};
static const float snow_cell[3] = {0.09f, 0.06f, 0.04f};
static const float snow_speed_hu[3] = {0.12f, 0.09f, 0.06f};

static float flash_envelope(float t)
{
	float e = expf(-t * 7.0f);
	if (t > 0.16f)
		e += 0.7f * expf(-(t - 0.16f) * 9.0f);
	return e;
}

static void tick_events(struct skybox_state *st, const struct skybox_settings *s, float dt, float aspect)
{
	struct solar sol = solar_at(s, st->hours);
	const struct skybox_weather_mix *w = &st->w_now;

	/* Lightning: average strike every 7 s at full storm and 1x frequency. */
	float rate = w->storm * s->lightning;
	if (st->flash_active) {
		st->flash_t += dt;
		if (st->flash_t > 1.2f)
			st->flash_active = false;
	}
	if (rate > 0.01f) {
		st->next_strike -= dt;
		if (st->next_strike <= 0.0f) {
			st->flash_active = true;
			st->flash_t = 0.0f;
			st->bolt_visible = rng_float(st) < 0.65f;
			st->bolt_x = 0.08f + 0.84f * rng_float(st);
			st->bolt_seed = floorf(rng_float(st) * 60.0f);
			st->next_strike = 1.5f + rng_wait(st, 7.0f / rate);
		}
	} else if (st->next_strike < 1.0f) {
		st->next_strike = 1.0f;
	}

	/* Shooting stars, only once the stars are out. */
	if (st->shoot_active) {
		st->shoot_t += dt;
		if (st->shoot_t > 1.0f)
			st->shoot_active = false;
	} else if (s->stars && s->shooting_stars && s->shooting_per_min > 0.0f && sol.elev < -0.15f) {
		st->shoot_wait -= dt;
		if (st->shoot_wait <= 0.0f) {
			st->shoot_active = true;
			st->shoot_t = 0.0f;
			st->shoot_x = (0.1f + 0.8f * rng_float(st)) * aspect;
			st->shoot_y = 0.05f + 0.35f * rng_float(st);
			float ang = (15.0f + 30.0f * rng_float(st)) * (float)M_PI / 180.0f;
			float dir = rng_float(st) < 0.5f ? -1.0f : 1.0f;
			st->shoot_dx = cosf(ang) * dir;
			st->shoot_dy = sinf(ang);
			st->shoot_wait = rng_wait(st, 60.0f / s->shooting_per_min);
		}
	}

	/* Bird flocks: daytime, and not in the middle of a downpour. */
	if (st->flock_active) {
		st->flock_x += st->flock_dir * st->flock_speed * dt;
		st->flock_flap += dt;
		float margin = 0.35f + 0.04f * (float)s->bird_count * s->bird_size;
		if ((st->flock_dir > 0.0f && st->flock_x > aspect + margin) ||
		    (st->flock_dir < 0.0f && st->flock_x < -margin))
			st->flock_active = false;
	} else if (s->birds && s->birds_per_min > 0.0f && sol.daylight > 0.4f && w->rain + w->snow < 0.3f) {
		st->flock_wait -= dt;
		if (st->flock_wait <= 0.0f) {
			st->flock_active = true;
			st->flock_dir = rng_float(st) < 0.5f ? -1.0f : 1.0f;
			st->flock_x = st->flock_dir > 0.0f ? -0.3f : aspect + 0.3f;
			st->flock_y = 0.15f + 0.35f * rng_float(st);
			st->flock_speed = 0.07f + 0.04f * rng_float(st);
			st->flock_flap = rng_float(st);
			st->flock_seed = floorf(rng_float(st) * 50.0f);
			st->flock_wait = rng_wait(st, 60.0f / s->birds_per_min);
		}
	}
}

void skybox_tick(struct skybox_state *st, const struct skybox_settings *s, float seconds, double clock_hours,
		 float aspect)
{
	if (!st->initialized)
		skybox_state_init(st, s);
	if (seconds < 0.0f)
		seconds = 0.0f;
	if (seconds > 1.0f)
		seconds = 1.0f; /* don't let a stall fast-forward the sky */

	tick_time(st, s, seconds, clock_hours);
	tick_weather(st, s, seconds);

	float dt = seconds * (s->anim_speed > 0.0f ? s->anim_speed : 0.0f);
	float wx = wind_x(s);
	float wz = wind_z(s);

	st->cloud_off[0] = wrapd(st->cloud_off[0] + wx * 0.15 * dt, NOISE_PERIOD);
	st->cloud_off[1] = wrapd(st->cloud_off[1] - wz * 0.15 * dt, NOISE_PERIOD);
	st->fog_off[0] = wrapd(st->fog_off[0] + (wx * 0.25 + 0.01) * dt, NOISE_PERIOD);
	st->fog_off[1] = wrapd(st->fog_off[1] + 0.004 * dt, NOISE_PERIOD);

	for (int i = 0; i < 3; ++i) {
		st->rain_off[i] = wrapd(st->rain_off[i] + rain_speed_hu[i] / rain_cell_h[i] * dt, NOISE_PERIOD);
		st->snow_off_y[i] = wrapd(st->snow_off_y[i] + snow_speed_hu[i] / snow_cell[i] * dt, NOISE_PERIOD);
		st->snow_off_x[i] = wrapd(st->snow_off_x[i] + wx * 0.25 / snow_cell[i] * dt, NOISE_PERIOD);
	}
	st->snow_phase += 0.25 * dt;
	st->twinkle += (double)s->twinkle_speed * dt;
	st->aurora_phase = wrapd(st->aurora_phase + dt / 400.0, 1.0);

	tick_events(st, s, dt, aspect > 0.0f ? aspect : 16.0f / 9.0f);
}

/* --------------------------------------------------------- compute ---- */

static void sky_colors(const struct skybox_rgb *pal, float e, struct skybox_rgb *top, struct skybox_rgb *hor)
{
	if (e >= 0.0f) {
		*top = mix(pal[SKYBOX_PAL_SUNSET_TOP], pal[SKYBOX_PAL_DAY_TOP], smoothstepf(0.0f, 0.3f, e));
		*hor = mix(pal[SKYBOX_PAL_SUNSET_HORIZON], pal[SKYBOX_PAL_DAY_HORIZON], smoothstepf(0.02f, 0.4f, e));
	} else {
		float n = -e;
		*top = mix(pal[SKYBOX_PAL_SUNSET_TOP], pal[SKYBOX_PAL_NIGHT_TOP], smoothstepf(0.02f, 0.3f, n));
		/* Pass through the violet blue hour rather than straight from
		 * orange to navy, which would go muddy brown. */
		struct skybox_rgb blue_hour = mix(pal[SKYBOX_PAL_SUNSET_TOP], pal[SKYBOX_PAL_SUNSET_HORIZON], 0.35f);
		struct skybox_rgb h1 = mix(pal[SKYBOX_PAL_SUNSET_HORIZON], blue_hour, smoothstepf(0.0f, 0.12f, n));
		*hor = mix(h1, pal[SKYBOX_PAL_NIGHT_HORIZON], smoothstepf(0.08f, 0.3f, n));
		*top = scale(*top, lerpf(1.0f, 0.8f, smoothstepf(0.5f, 1.0f, n)));
	}
}

void skybox_compute(const struct skybox_state *st, const struct skybox_settings *s, uint32_t width, uint32_t height,
		    double real_moon_phase, struct skybox_uniforms *u)
{
	memset(u, 0, sizeof(*u));
	float aspect = height ? (float)width / (float)height : 16.0f / 9.0f;

	struct skybox_rgb pal[SKYBOX_PAL_COUNT];
	if (s->theme == SKYBOX_THEME_CUSTOM)
		memcpy(pal, s->custom, sizeof(pal));
	else
		skybox_theme_palette(s->theme, pal);

	struct solar sol = solar_at(s, st->hours);
	float e = sol.elev;
	float dl = sol.daylight;
	const struct skybox_weather_mix *w = &st->w_now;
	float grey = saturatef(w->grey);
	float golden = 1.0f - smoothstepf(0.03f, 0.35f, fabsf(e));

	u->noise_seed = (float)(((unsigned)s->seed * 7919u) % 1000u);
	u->style_flat = s->flat_style ? 1.0f : 0.0f;
	u->sky_bands = (float)(s->sky_bands < 2 ? 2 : s->sky_bands);
	u->draw_sky = s->weather_only ? 0.0f : 1.0f;

	/* Sky gradient, pulled toward a flat grey by heavy weather. */
	struct skybox_rgb top, hor;
	sky_colors(pal, e, &top, &hor);
	struct skybox_rgb overcast = mix(rgb(0.07f, 0.075f, 0.09f), rgb(0.6f, 0.63f, 0.68f), dl);
	overcast = mix(overcast, hor, 0.15f * golden);
	top = mix(top, scale(overcast, 0.9f), grey);
	hor = mix(hor, scale(overcast, 1.08f), grey);
	float storm_dim = 1.0f - 0.45f * saturatef(w->storm);
	top = scale(top, storm_dim);
	hor = scale(hor, storm_dim);
	put4(u->sky_top, top, 1.0f);
	put4(u->sky_horizon, hor, 1.0f);

	/* Sun along its arc. */
	float sf = clampf(sol.sun_f, -0.4f, 1.4f);
	float sun_x = (0.5f + s->arc_offset + (sf - 0.5f) * s->arc_width) * aspect;
	float sun_y = 1.0f - sinf((float)M_PI * sf) * s->arc_height;
	u->sun_pos[0] = sun_x;
	u->sun_pos[1] = sun_y;
	u->sun_radius = 0.045f * s->sun_size * (1.0f + 0.15f * (1.0f - smoothstepf(0.0f, 0.3f, e)));
	struct skybox_rgb low_sun = scale(mix(pal[SKYBOX_PAL_SUN], pal[SKYBOX_PAL_SUNSET_HORIZON], 0.55f), 1.1f);
	struct skybox_rgb sun_col = mix(low_sun, pal[SKYBOX_PAL_SUN], smoothstepf(0.02f, 0.35f, e));
	put4(u->sun_color, sun_col, 1.0f);
	float fog = saturatef(w->fog * s->fog);
	u->sun_alpha = s->show_sun ? (1.0f - 0.9f * grey) * (1.0f - 0.5f * fog) * smoothstepf(-0.1f, 0.0f, e) : 0.0f;
	u->sun_glow = (0.35f + 0.75f * (1.0f - smoothstepf(0.0f, 0.45f, e))) * smoothstepf(-0.3f, -0.02f, e) *
		      (1.0f - 0.85f * grey);
	if (!s->show_sun)
		u->sun_glow *= 0.5f;

	/* Moon along the night arc. */
	float mf = clampf(sol.moon_f, -0.4f, 1.4f);
	u->moon_pos[0] = (0.5f + s->arc_offset + (mf - 0.5f) * s->arc_width) * aspect;
	u->moon_pos[1] = 1.0f - sinf((float)M_PI * mf) * s->arc_height;
	u->moon_radius = 0.035f * s->moon_size;
	u->moon_phase = (float)(s->real_moon_phase ? real_moon_phase : wrapd(s->moon_phase, 1.0));
	u->moon_alpha = s->show_moon ? (1.0f - 0.9f * grey) * (1.0f - 0.75f * dl) : 0.0f;
	put4(u->moon_color, rgb(0.95f, 0.94f, 0.86f), 1.0f);

	/* Stars and shooting stars. */
	float night = smoothstepf(0.04f, 0.22f, -e);
	u->star_alpha = s->stars ? night * (1.0f - 0.95f * grey) : 0.0f;
	u->star_density = saturatef(s->star_density);
	u->star_twinkle = (float)fmod(st->twinkle, 10000.0);
	if (st->shoot_active && u->star_alpha > 0.0f) {
		float t = st->shoot_t;
		float speed = 1.1f;
		u->shoot_a[0] = st->shoot_x + st->shoot_dx * speed * t;
		u->shoot_a[1] = st->shoot_y + st->shoot_dy * speed * t;
		u->shoot_a[2] = st->shoot_dx;
		u->shoot_a[3] = st->shoot_dy;
		u->shoot_b[0] = 0.18f * saturatef(t / 0.2f);
		u->shoot_b[1] = sinf((float)M_PI * saturatef(t)) * u->star_alpha;
	}

	/* Aurora. */
	u->aurora_alpha = s->aurora ? s->aurora_intensity * night * (1.0f - 0.8f * grey) : 0.0f;
	put4(u->aurora_color1, s->aurora_color1, 1.0f);
	put4(u->aurora_color2, s->aurora_color2, 1.0f);
	u->aurora_phase = (float)st->aurora_phase;

	/* Rainbow opposite the sun. */
	float rb = 0.0f;
	if (s->rainbow_mode == SKYBOX_RAINBOW_ALWAYS) {
		rb = 1.0f;
	} else if (s->rainbow_mode == SKYBOX_RAINBOW_AFTER_RAIN && st->rainbow_left > 0.0f) {
		float elapsed = st->rainbow_total - st->rainbow_left;
		rb = smoothstepf(0.0f, fmaxf(s->transition_secs, 1.0f) + 4.0f, elapsed) *
		     smoothstepf(0.0f, 6.0f, st->rainbow_left);
	}
	u->rainbow_alpha = rb * s->rainbow_intensity * smoothstepf(0.03f, 0.15f, e) *
			   (1.0f - 0.8f * saturatef(w->rain)) * (1.0f - 0.6f * grey);
	u->rainbow_center[0] = aspect - sun_x;
	u->rainbow_center[1] = 1.0f + (1.0f - sun_y) * 0.6f;
	u->rainbow_radius = 0.75f;

	/* Clouds. Lit by the sun by day (gold near the horizon) and by a cold
	 * moonlight at night; heavier weather darkens both faces. */
	u->cloud_cover = saturatef(w->cover + s->cloud_adjust);
	u->cloud_scale = 0.9f * (s->cloud_scale > 0.05f ? s->cloud_scale : 0.05f);
	u->cloud_offset[0] = (float)st->cloud_off[0];
	u->cloud_offset[1] = (float)st->cloud_off[1];
	float lx = dl > 0.3f ? sun_x / aspect : u->moon_pos[0] / aspect;
	float lelev = dl > 0.3f ? e : -e;
	float ldx = (lx - 0.5f) * 2.0f;
	float ldy = 1.0f - 0.7f * saturatef(lelev);
	float ll = sqrtf(ldx * ldx + ldy * ldy);
	u->cloud_light_dir[0] = ldx / ll;
	u->cloud_light_dir[1] = ldy / ll;
	struct skybox_rgb cbase = pal[SKYBOX_PAL_CLOUD];
	struct skybox_rgb day_lit =
		mix(cbase, add(scale(pal[SKYBOX_PAL_SUNSET_HORIZON], 0.9f), scale(sun_col, 0.2f)), 0.7f * golden);
	float moon_lit = 0.5f - 0.5f * cosf(u->moon_phase * 2.0f * (float)M_PI);
	struct skybox_rgb night_lit = add(scale(pal[SKYBOX_PAL_NIGHT_HORIZON], 1.9f),
					  scale(rgb(0.12f, 0.13f, 0.16f), 0.5f + 0.5f * moon_lit));
	night_lit = mix(night_lit, scale(rgb(0.2f, 0.21f, 0.24f), 0.6f + 0.4f * moon_lit), 0.5f * saturatef(w->dark));
	struct skybox_rgb lit = scale(mix(night_lit, day_lit, dl), 1.0f - lerpf(0.3f, 0.6f, dl) * saturatef(w->dark));
	struct skybox_rgb shade = mix(scale(lit, 0.58f), mix(top, hor, 0.5f), 0.3f);
	shade = mix(shade, scale(pal[SKYBOX_PAL_SUNSET_TOP], 0.8f), 0.35f * golden * dl);
	shade = scale(shade, 1.0f - 0.35f * saturatef(w->dark));
	put4(u->cloud_lit, lit, s->flat_style ? 1.0f : 0.95f);
	put4(u->cloud_shade, shade, 1.0f);

	/* Birds. */
	if (st->flock_active && s->birds) {
		u->flock_a[0] = st->flock_x;
		u->flock_a[1] = st->flock_y;
		u->flock_a[2] = st->flock_dir;
		u->flock_a[3] = st->flock_flap;
		u->flock_b[0] = (float)(s->bird_count < 1 ? 1 : (s->bird_count > 9 ? 9 : s->bird_count));
		u->flock_b[1] = 0.009f * s->bird_size;
		u->flock_b[2] = smoothstepf(0.3f, 0.6f, dl) * (1.0f - saturatef((w->rain + w->snow) * 2.0f));
		u->flock_b[3] = st->flock_seed;
	}
	put4(u->bird_color, s->bird_color, 1.0f);

	/* Lightning. */
	if (st->flash_active) {
		float env = flash_envelope(st->flash_t) * saturatef(w->storm * 1.5f);
		u->flash = 0.6f * saturatef(env);
		if (st->bolt_visible) {
			u->bolt[0] = st->bolt_x * aspect;
			u->bolt[1] = st->bolt_seed;
			u->bolt[2] = saturatef(env * 1.2f);
		}
	}

	/* Precipitation. */
	float wx = wind_x(s);
	u->rain_amount = saturatef(w->rain * s->precip) * 0.28f;
	u->rain_slant = wx * 0.6f;
	for (int i = 0; i < 3; ++i) {
		u->rain_off[i] = (float)st->rain_off[i];
		u->snow_off_y[i] = (float)st->snow_off_y[i];
		u->snow_off_x[i] = (float)st->snow_off_x[i];
	}
	put4(u->rain_color, mix(rgb(0.5f, 0.55f, 0.65f), rgb(0.78f, 0.82f, 0.9f), dl), 0.8f);
	u->snow_amount = saturatef(w->snow * s->precip) * 0.5f;
	u->snow_phase = (float)fmod(st->snow_phase, 10000.0);

	/* Fog. */
	u->fog_amount = fog * 0.9f;
	struct skybox_rgb fog_col = add(scale(mix(hor, overcast, 0.6f), 0.6f), scale(rgb(0.36f, 0.37f, 0.38f), dl));
	put4(u->fog_color, fog_col, 1.0f);
	u->fog_offset[0] = (float)st->fog_off[0];
	u->fog_offset[1] = (float)st->fog_off[1];

	/* Grade. */
	u->grade[0] = s->saturation;
	u->grade[1] = s->brightness;
	u->grade[2] = s->theme == SKYBOX_THEME_MONOCHROME ? 1.0f : 0.0f;
	put4(u->mono_tint, s->mono_tint, 1.0f);
}
