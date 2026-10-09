/*
Constellations - Skybox model
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* Everything the Skybox decides on the CPU: time of day, weather crossfades,
 * color themes, sun/moon arcs and the timed events (lightning, shooting
 * stars, bird flocks, after-rain rainbows). It has no OBS dependency, so the
 * OBS glue in skybox-source.c only copies settings in and uniforms out. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum skybox_time_mode {
	SKYBOX_TIME_PRESET = 0,
	SKYBOX_TIME_CUSTOM,
	SKYBOX_TIME_CYCLE,
	SKYBOX_TIME_CLOCK,
};

enum skybox_time_preset {
	SKYBOX_PRESET_DAWN = 0,
	SKYBOX_PRESET_MORNING,
	SKYBOX_PRESET_NOON,
	SKYBOX_PRESET_AFTERNOON,
	SKYBOX_PRESET_GOLDEN,
	SKYBOX_PRESET_SUNSET,
	SKYBOX_PRESET_DUSK,
	SKYBOX_PRESET_NIGHT,
	SKYBOX_PRESET_MIDNIGHT,
	SKYBOX_PRESET_COUNT,
};

enum skybox_weather {
	SKYBOX_WEATHER_CLEAR = 0,
	SKYBOX_WEATHER_PARTLY,
	SKYBOX_WEATHER_CLOUDY,
	SKYBOX_WEATHER_OVERCAST,
	SKYBOX_WEATHER_RAIN,
	SKYBOX_WEATHER_STORM,
	SKYBOX_WEATHER_SNOW,
	SKYBOX_WEATHER_FOG,
	SKYBOX_WEATHER_COUNT,
};

enum skybox_theme {
	SKYBOX_THEME_NATURAL = 0,
	SKYBOX_THEME_PASTEL,
	SKYBOX_THEME_SYNTHWAVE,
	SKYBOX_THEME_ALIEN,
	SKYBOX_THEME_MONOCHROME,
	SKYBOX_THEME_CUSTOM,
	SKYBOX_THEME_COUNT,
};

enum skybox_rainbow_mode {
	SKYBOX_RAINBOW_OFF = 0,
	SKYBOX_RAINBOW_AFTER_RAIN,
	SKYBOX_RAINBOW_ALWAYS,
};

/* The eight colors a theme is made of. */
enum skybox_palette_slot {
	SKYBOX_PAL_DAY_TOP = 0,
	SKYBOX_PAL_DAY_HORIZON,
	SKYBOX_PAL_SUNSET_TOP,
	SKYBOX_PAL_SUNSET_HORIZON,
	SKYBOX_PAL_NIGHT_TOP,
	SKYBOX_PAL_NIGHT_HORIZON,
	SKYBOX_PAL_SUN,
	SKYBOX_PAL_CLOUD,
	SKYBOX_PAL_COUNT,
};

struct skybox_rgb {
	float r, g, b;
};

struct skybox_settings {
	int seed;
	bool flat_style;
	int sky_bands;
	bool weather_only; /* transparent output with only rain/snow/fog/lightning */

	int time_mode;
	int time_preset;
	float custom_time; /* hours 0..24; also the start time of the auto cycle */
	float cycle_minutes;
	float clock_offset; /* hours added to the real clock */
	float sunrise, sunset;

	int weather;
	float transition_secs;
	float cloud_adjust; /* -1..1 added to the weather's coverage */
	float precip;       /* rain/snow intensity multiplier */
	float lightning;    /* flash frequency multiplier */
	float fog;          /* fog density multiplier */
	float cloud_scale;

	float wind_dir_deg;  /* 0 = clouds move right, 90 = toward the viewer */
	float wind_strength; /* 0..100 */
	float anim_speed;    /* master speed for everything animated */

	bool show_sun;
	float sun_size;
	bool show_moon;
	float moon_size;
	bool real_moon_phase;
	float moon_phase;
	float arc_height, arc_width, arc_offset;

	bool stars;
	float star_density;
	float twinkle_speed;
	bool shooting_stars;
	float shooting_per_min;

	bool aurora;
	float aurora_intensity;
	struct skybox_rgb aurora_color1, aurora_color2;

	int rainbow_mode;
	float rainbow_intensity;
	float rainbow_secs;

	bool birds;
	float birds_per_min;
	int bird_count;
	float bird_size;
	struct skybox_rgb bird_color;

	int theme;
	struct skybox_rgb custom[SKYBOX_PAL_COUNT];
	struct skybox_rgb mono_tint;
	float saturation, brightness;
};

/* Weather is crossfaded as a vector of these amounts. */
struct skybox_weather_mix {
	float cover, dark, rain, snow, storm, fog, grey;
};

struct skybox_state {
	uint32_t rng;
	bool initialized;

	/* time of day */
	double hours; /* displayed time, 0..24 */
	double cycle_hours;
	int last_mode;
	float last_custom;
	double tt_from, tt_delta, tt_t; /* preset time transition */
	double target_hours;

	/* weather crossfade */
	int weather_target;
	struct skybox_weather_mix w_from, w_to, w_now;
	float w_t; /* 0..1 progress */
	float rainbow_left, rainbow_total;

	/* scrolling offsets, all wrapped to the shader's 64-cell period */
	double cloud_off[2];
	double fog_off[2];
	double rain_off[3];
	double snow_off_y[3], snow_off_x[3];
	double snow_phase, twinkle, aurora_phase;

	/* lightning */
	float next_strike, flash_t, bolt_x, bolt_seed;
	bool bolt_visible, flash_active;

	/* shooting star */
	float shoot_wait, shoot_t, shoot_x, shoot_y, shoot_dx, shoot_dy;
	bool shoot_active;

	/* bird flock */
	float flock_wait, flock_x, flock_y, flock_dir, flock_speed, flock_flap, flock_seed;
	bool flock_active;
};

struct skybox_uniforms {
	float noise_seed, style_flat, sky_bands, draw_sky;
	float sky_top[4], sky_horizon[4];
	float sun_pos[2], sun_radius, sun_color[4], sun_alpha, sun_glow;
	float moon_pos[2], moon_radius, moon_phase, moon_alpha, moon_color[4];
	float star_alpha, star_density, star_twinkle;
	float shoot_a[4], shoot_b[4];
	float aurora_alpha, aurora_color1[4], aurora_color2[4], aurora_phase;
	float rainbow_alpha, rainbow_center[2], rainbow_radius;
	float cloud_cover, cloud_scale, cloud_offset[2], cloud_light_dir[2], cloud_lit[4], cloud_shade[4];
	float flock_a[4], flock_b[4], bird_color[4];
	float bolt[4], flash;
	float rain_amount, rain_slant, rain_off[4], rain_color[4];
	float snow_amount, snow_off_y[4], snow_off_x[4], snow_phase;
	float fog_amount, fog_color[4], fog_offset[2];
	float grade[4], mono_tint[4];
};

/* Theme colors, also used as the Custom theme's defaults. */
void skybox_theme_palette(int theme, struct skybox_rgb out[SKYBOX_PAL_COUNT]);

/* Hour of day a preset maps to under the given sunrise/sunset. */
double skybox_preset_hours(int preset, float sunrise, float sunset);

/* Real local time as hours 0..24, and the current lunar phase 0..1. */
double skybox_clock_hours(void);
double skybox_real_moon_phase(void);

void skybox_state_init(struct skybox_state *st, const struct skybox_settings *s);

/* Advances everything time-based. clock_hours is the real local time (only
 * read in Follow Real Clock mode) so tests can drive it; aspect is the
 * canvas width over height, used to place events across the frame. */
void skybox_tick(struct skybox_state *st, const struct skybox_settings *s, float seconds, double clock_hours,
		 float aspect);

void skybox_compute(const struct skybox_state *st, const struct skybox_settings *s, uint32_t width, uint32_t height,
		    double real_moon_phase, struct skybox_uniforms *u);

#ifdef __cplusplus
}
#endif
