/*
Constellations - Skybox Background / Filter
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <util/threading.h>

#include "shape-defs.h"
#include "skybox-model.h"

struct csb_source {
	obs_source_t *self;
	bool is_filter;
	uint32_t width, height;

	/* update() runs on the UI thread while tick/render run on the graphics
	 * thread, so the settings are swapped in whole under a lock instead of
	 * being read half-written mid-frame. The model state is only ever
	 * touched from the graphics thread. */
	pthread_mutex_t lock;
	struct skybox_settings pending;
	struct skybox_settings settings;
	struct skybox_state state;

	gs_effect_t *effect;
};

/* ------------------------------------------------------------ names ---- */

static const char *csb_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.SkyboxSource.Name");
}

static const char *csb_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.SkyboxFilter.Name");
}

/* --------------------------------------------------------- settings ---- */

static struct skybox_rgb rgb_from_obs(long long c)
{
	/* OBS stores colors as 0xAABBGGRR. */
	struct skybox_rgb o;
	o.r = (float)(c & 0xFF) / 255.0f;
	o.g = (float)((c >> 8) & 0xFF) / 255.0f;
	o.b = (float)((c >> 16) & 0xFF) / 255.0f;
	return o;
}

static long long rgb_to_obs(struct skybox_rgb c)
{
	long long r = (long long)(c.r * 255.0f + 0.5f);
	long long g = (long long)(c.g * 255.0f + 0.5f);
	long long b = (long long)(c.b * 255.0f + 0.5f);
	return 0xFF000000LL | (b << 16) | (g << 8) | r;
}

static const char *const custom_color_keys[SKYBOX_PAL_COUNT] = {
	"color_day_top",   "color_day_horizon",   "color_sunset_top", "color_sunset_horizon",
	"color_night_top", "color_night_horizon", "color_sun",        "color_cloud",
};

static const char *const custom_color_labels[SKYBOX_PAL_COUNT] = {
	"Constellations.Skybox.Color.DayTop",    "Constellations.Skybox.Color.DayHorizon",
	"Constellations.Skybox.Color.SunsetTop", "Constellations.Skybox.Color.SunsetHorizon",
	"Constellations.Skybox.Color.NightTop",  "Constellations.Skybox.Color.NightHorizon",
	"Constellations.Skybox.Color.Sun",       "Constellations.Skybox.Color.Cloud",
};

static float get_f(obs_data_t *settings, const char *key)
{
	return (float)obs_data_get_double(settings, key);
}

static void csb_update(void *data, obs_data_t *settings)
{
	struct csb_source *s = data;
	struct skybox_settings k;

	if (!s->is_filter) {
		s->width = (uint32_t)obs_data_get_int(settings, "width");
		s->height = (uint32_t)obs_data_get_int(settings, "height");
		if (s->width == 0)
			s->width = 1920;
		if (s->height == 0)
			s->height = 1080;
	}

	k.seed = (int)obs_data_get_int(settings, "seed");
	k.flat_style = obs_data_get_int(settings, "style") == 1;
	k.sky_bands = (int)obs_data_get_int(settings, "sky_bands");
	k.weather_only = obs_data_get_int(settings, "layers") == 1;

	k.time_mode = (int)obs_data_get_int(settings, "time_mode");
	k.time_preset = (int)obs_data_get_int(settings, "time_preset");
	k.custom_time = get_f(settings, "custom_time");
	k.cycle_minutes = get_f(settings, "cycle_minutes");
	k.clock_offset = get_f(settings, "clock_offset");
	k.sunrise = get_f(settings, "sunrise");
	k.sunset = get_f(settings, "sunset");

	k.weather = (int)obs_data_get_int(settings, "weather");
	k.transition_secs = get_f(settings, "transition_secs");
	k.cloud_adjust = get_f(settings, "cloud_adjust");
	k.precip = get_f(settings, "precip");
	k.lightning = get_f(settings, "lightning");
	k.fog = get_f(settings, "fog_density");
	k.cloud_scale = get_f(settings, "cloud_scale");

	k.wind_dir_deg = get_f(settings, "wind_dir");
	k.wind_strength = get_f(settings, "wind_strength");
	k.anim_speed = get_f(settings, "anim_speed");

	k.show_sun = obs_data_get_bool(settings, "show_sun");
	k.sun_size = get_f(settings, "sun_size");
	k.show_moon = obs_data_get_bool(settings, "show_moon");
	k.moon_size = get_f(settings, "moon_size");
	k.real_moon_phase = obs_data_get_bool(settings, "real_moon_phase");
	k.moon_phase = get_f(settings, "moon_phase");
	k.arc_height = get_f(settings, "arc_height");
	k.arc_width = get_f(settings, "arc_width");
	k.arc_offset = get_f(settings, "arc_offset");

	k.stars = obs_data_get_bool(settings, "stars");
	k.star_density = get_f(settings, "star_density");
	k.twinkle_speed = get_f(settings, "twinkle_speed");
	k.shooting_stars = obs_data_get_bool(settings, "shooting_stars");
	k.shooting_per_min = get_f(settings, "shooting_per_min");

	k.aurora = obs_data_get_bool(settings, "aurora");
	k.aurora_intensity = get_f(settings, "aurora_intensity");
	k.aurora_color1 = rgb_from_obs(obs_data_get_int(settings, "aurora_color1"));
	k.aurora_color2 = rgb_from_obs(obs_data_get_int(settings, "aurora_color2"));

	k.rainbow_mode = (int)obs_data_get_int(settings, "rainbow_mode");
	k.rainbow_intensity = get_f(settings, "rainbow_intensity");
	k.rainbow_secs = get_f(settings, "rainbow_secs");

	k.birds = obs_data_get_bool(settings, "birds");
	k.birds_per_min = get_f(settings, "birds_per_min");
	k.bird_count = (int)obs_data_get_int(settings, "bird_count");
	k.bird_size = get_f(settings, "bird_size");
	k.bird_color = rgb_from_obs(obs_data_get_int(settings, "bird_color"));

	k.theme = (int)obs_data_get_int(settings, "theme");
	for (int i = 0; i < SKYBOX_PAL_COUNT; ++i)
		k.custom[i] = rgb_from_obs(obs_data_get_int(settings, custom_color_keys[i]));
	k.mono_tint = rgb_from_obs(obs_data_get_int(settings, "mono_tint"));
	k.saturation = get_f(settings, "saturation");
	k.brightness = get_f(settings, "brightness");

	pthread_mutex_lock(&s->lock);
	s->pending = k;
	pthread_mutex_unlock(&s->lock);
}

/* -------------------------------------------------------- lifecycle ---- */

static void *csb_create_common(obs_data_t *settings, obs_source_t *source, bool is_filter)
{
	struct csb_source *s = bzalloc(sizeof(*s));
	s->self = source;
	s->is_filter = is_filter;
	pthread_mutex_init(&s->lock, NULL);

	char *path = obs_module_file("effects/skybox.effect");
	if (path) {
		obs_enter_graphics();
		char *errors = NULL;
		s->effect = gs_effect_create_from_file(path, &errors);
		obs_leave_graphics();
		if (!s->effect)
			obs_log(LOG_ERROR, "Constellations: failed to compile skybox.effect: %s",
				errors ? errors : "(no detail)");
		bfree(errors);
		bfree(path);
	} else {
		obs_log(LOG_ERROR, "Constellations: skybox.effect not found");
	}

	csb_update(s, settings);
	s->settings = s->pending;
	skybox_state_init(&s->state, &s->settings);
	return s;
}

static void *csb_source_create(obs_data_t *settings, obs_source_t *source)
{
	return csb_create_common(settings, source, false);
}

static void *csb_filter_create(obs_data_t *settings, obs_source_t *source)
{
	return csb_create_common(settings, source, true);
}

static void csb_destroy(void *data)
{
	struct csb_source *s = data;
	if (s->effect) {
		obs_enter_graphics();
		gs_effect_destroy(s->effect);
		obs_leave_graphics();
	}
	pthread_mutex_destroy(&s->lock);
	bfree(s);
}

static void csb_canvas_size(struct csb_source *s, uint32_t *w, uint32_t *h)
{
	if (!s->is_filter) {
		*w = s->width;
		*h = s->height;
		return;
	}
	obs_source_t *target = obs_filter_get_target(s->self);
	*w = target ? obs_source_get_base_width(target) : 0;
	*h = target ? obs_source_get_base_height(target) : 0;
}

static void csb_tick(void *data, float seconds)
{
	struct csb_source *s = data;

	pthread_mutex_lock(&s->lock);
	s->settings = s->pending;
	pthread_mutex_unlock(&s->lock);

	uint32_t w, h;
	csb_canvas_size(s, &w, &h);
	float aspect = (w && h) ? (float)w / (float)h : 16.0f / 9.0f;
	double clock = s->settings.time_mode == SKYBOX_TIME_CLOCK ? skybox_clock_hours() : 0.0;
	skybox_tick(&s->state, &s->settings, seconds, clock, aspect);
}

/* ------------------------------------------------------------ render ---- */

static void set_f(gs_effect_t *e, const char *name, float v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p)
		gs_effect_set_float(p, v);
}

static void set_v2(gs_effect_t *e, const char *name, const float *v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p) {
		struct vec2 x;
		vec2_set(&x, v[0], v[1]);
		gs_effect_set_vec2(p, &x);
	}
}

static void set_v4(gs_effect_t *e, const char *name, const float *v)
{
	gs_eparam_t *p = gs_effect_get_param_by_name(e, name);
	if (p) {
		struct vec4 x;
		vec4_set(&x, v[0], v[1], v[2], v[3]);
		gs_effect_set_vec4(p, &x);
	}
}

static void csb_draw(struct csb_source *s, uint32_t width, uint32_t height)
{
	if (!s->effect || width == 0 || height == 0)
		return;

	struct skybox_uniforms u;
	double moon = s->settings.real_moon_phase ? skybox_real_moon_phase() : 0.0;
	skybox_compute(&s->state, &s->settings, width, height, moon, &u);

	gs_effect_t *e = s->effect;
	float canvas[2] = {(float)width, (float)height};
	set_v2(e, "canvas_size", canvas);
	set_f(e, "noise_seed", u.noise_seed);
	set_f(e, "style_flat", u.style_flat);
	set_f(e, "sky_bands", u.sky_bands);
	set_f(e, "draw_sky", u.draw_sky);
	set_v4(e, "sky_top", u.sky_top);
	set_v4(e, "sky_horizon", u.sky_horizon);

	set_v2(e, "sun_pos", u.sun_pos);
	set_f(e, "sun_radius", u.sun_radius);
	set_v4(e, "sun_color", u.sun_color);
	set_f(e, "sun_alpha", u.sun_alpha);
	set_f(e, "sun_glow", u.sun_glow);

	set_v2(e, "moon_pos", u.moon_pos);
	set_f(e, "moon_radius", u.moon_radius);
	set_f(e, "moon_phase", u.moon_phase);
	set_f(e, "moon_alpha", u.moon_alpha);
	set_v4(e, "moon_color", u.moon_color);

	set_f(e, "star_alpha", u.star_alpha);
	set_f(e, "star_density", u.star_density);
	set_f(e, "star_twinkle", u.star_twinkle);
	set_v4(e, "shoot_a", u.shoot_a);
	set_v4(e, "shoot_b", u.shoot_b);

	set_f(e, "aurora_alpha", u.aurora_alpha);
	set_v4(e, "aurora_color1", u.aurora_color1);
	set_v4(e, "aurora_color2", u.aurora_color2);
	set_f(e, "aurora_phase", u.aurora_phase);

	set_f(e, "rainbow_alpha", u.rainbow_alpha);
	set_v2(e, "rainbow_center", u.rainbow_center);
	set_f(e, "rainbow_radius", u.rainbow_radius);

	set_f(e, "cloud_cover", u.cloud_cover);
	set_f(e, "cloud_scale", u.cloud_scale);
	set_v2(e, "cloud_offset", u.cloud_offset);
	set_v2(e, "cloud_light_dir", u.cloud_light_dir);
	set_v4(e, "cloud_lit", u.cloud_lit);
	set_v4(e, "cloud_shade", u.cloud_shade);

	set_v4(e, "flock_a", u.flock_a);
	set_v4(e, "flock_b", u.flock_b);
	set_v4(e, "bird_color", u.bird_color);

	set_v4(e, "bolt", u.bolt);
	set_f(e, "flash", u.flash);

	set_f(e, "rain_amount", u.rain_amount);
	set_f(e, "rain_slant", u.rain_slant);
	set_v4(e, "rain_off", u.rain_off);
	set_v4(e, "rain_color", u.rain_color);

	set_f(e, "snow_amount", u.snow_amount);
	set_v4(e, "snow_off_y", u.snow_off_y);
	set_v4(e, "snow_off_x", u.snow_off_x);
	set_f(e, "snow_phase", u.snow_phase);

	set_f(e, "fog_amount", u.fog_amount);
	set_v4(e, "fog_color", u.fog_color);
	set_v2(e, "fog_offset", u.fog_offset);

	set_v4(e, "grade", u.grade);
	set_v4(e, "mono_tint", u.mono_tint);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(e, "Draw"))
		gs_draw_sprite(NULL, 0, width, height);
	gs_blend_state_pop();
}

static void csb_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct csb_source *s = data;
	csb_draw(s, s->width, s->height);
}

static void csb_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct csb_source *s = data;

	uint32_t w, h;
	csb_canvas_size(s, &w, &h);
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(s->self);
		return;
	}

	/* begin() renders the target off-screen, so the sky can be drawn
	 * first and the source composited on top of it. Weather-only output
	 * goes the other way round: rain, snow and fog fall in front. */
	if (!obs_source_process_filter_begin(s->self, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;
	if (s->settings.weather_only) {
		obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), w, h);
		csb_draw(s, w, h);
	} else {
		csb_draw(s, w, h);
		obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), w, h);
	}
}

static uint32_t csb_width(void *data)
{
	struct csb_source *s = data;
	return s->width;
}

static uint32_t csb_height(void *data)
{
	struct csb_source *s = data;
	return s->height;
}

/* -------------------------------------------------------- properties ---- */

static void set_visible(obs_properties_t *props, const char *name, bool visible)
{
	obs_property_t *p = obs_properties_get(props, name);
	if (p)
		obs_property_set_visible(p, visible);
}

/* One callback keeps every dependent control in step. It is only attached
 * to toggles and dropdowns, never to sliders: returning true rebuilds the
 * whole dialog, which would break a slider mid-drag. */
static bool csb_refresh(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	set_visible(props, "sky_bands", obs_data_get_int(settings, "style") == 1);

	int mode = (int)obs_data_get_int(settings, "time_mode");
	set_visible(props, "time_preset", mode == SKYBOX_TIME_PRESET);
	set_visible(props, "custom_time", mode == SKYBOX_TIME_CUSTOM || mode == SKYBOX_TIME_CYCLE);
	set_visible(props, "cycle_minutes", mode == SKYBOX_TIME_CYCLE);
	set_visible(props, "clock_offset", mode == SKYBOX_TIME_CLOCK);

	bool sun = obs_data_get_bool(settings, "show_sun");
	set_visible(props, "sun_size", sun);
	bool moon = obs_data_get_bool(settings, "show_moon");
	set_visible(props, "moon_size", moon);
	set_visible(props, "real_moon_phase", moon);
	set_visible(props, "moon_phase", moon && !obs_data_get_bool(settings, "real_moon_phase"));

	bool stars = obs_data_get_bool(settings, "stars");
	set_visible(props, "star_density", stars);
	set_visible(props, "twinkle_speed", stars);
	set_visible(props, "shooting_stars", stars);
	set_visible(props, "shooting_per_min", stars && obs_data_get_bool(settings, "shooting_stars"));

	bool aurora = obs_data_get_bool(settings, "aurora");
	set_visible(props, "aurora_intensity", aurora);
	set_visible(props, "aurora_color1", aurora);
	set_visible(props, "aurora_color2", aurora);

	int rainbow = (int)obs_data_get_int(settings, "rainbow_mode");
	set_visible(props, "rainbow_intensity", rainbow != SKYBOX_RAINBOW_OFF);
	set_visible(props, "rainbow_secs", rainbow == SKYBOX_RAINBOW_AFTER_RAIN);

	bool birds = obs_data_get_bool(settings, "birds");
	set_visible(props, "birds_per_min", birds);
	set_visible(props, "bird_count", birds);
	set_visible(props, "bird_size", birds);
	set_visible(props, "bird_color", birds);

	int theme = (int)obs_data_get_int(settings, "theme");
	set_visible(props, "mono_tint", theme == SKYBOX_THEME_MONOCHROME);
	for (int i = 0; i < SKYBOX_PAL_COUNT; ++i)
		set_visible(props, custom_color_keys[i], theme == SKYBOX_THEME_CUSTOM);

	return true;
}

static obs_property_t *add_toggle(obs_properties_t *grp, const char *key, const char *label)
{
	obs_property_t *p = obs_properties_add_bool(grp, key, obs_module_text(label));
	obs_property_set_modified_callback(p, csb_refresh);
	return p;
}

static obs_property_t *add_list(obs_properties_t *grp, const char *key, const char *label)
{
	obs_property_t *p =
		obs_properties_add_list(grp, key, obs_module_text(label), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_modified_callback(p, csb_refresh);
	return p;
}

static void list_item(obs_property_t *p, const char *label, long long v)
{
	obs_property_list_add_int(p, obs_module_text(label), v);
}

static obs_properties_t *csb_props_common(bool is_filter)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	if (!is_filter) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	}

	/* Look */
	obs_properties_t *look = obs_properties_create();
	p = add_list(look, "layers", "Constellations.Skybox.Layers");
	list_item(p, "Constellations.Skybox.Layers.Full", 0);
	list_item(p, "Constellations.Skybox.Layers.WeatherOnly", 1);
	obs_property_set_long_description(p, obs_module_text(is_filter ? "Constellations.Skybox.Layers.FilterDesc"
								       : "Constellations.Skybox.Layers.SourceDesc"));
	p = add_list(look, "style", "Constellations.Skybox.Style");
	list_item(p, "Constellations.Skybox.Style.Soft", 0);
	list_item(p, "Constellations.Skybox.Style.Flat", 1);
	obs_properties_add_int_slider(look, "sky_bands", obs_module_text("Constellations.Skybox.Bands"), 2, 24, 1);
	obs_properties_add_int_slider(look, "seed", obs_module_text("Constellations.Skybox.Seed"), 0, 10000, 1);
	obs_properties_add_group(props, "look_group", obs_module_text("Constellations.Skybox.Group.Look"),
				 OBS_GROUP_NORMAL, look);

	/* Time of day */
	obs_properties_t *tod = obs_properties_create();
	p = add_list(tod, "time_mode", "Constellations.Skybox.TimeMode");
	list_item(p, "Constellations.Skybox.TimeMode.Preset", SKYBOX_TIME_PRESET);
	list_item(p, "Constellations.Skybox.TimeMode.Custom", SKYBOX_TIME_CUSTOM);
	list_item(p, "Constellations.Skybox.TimeMode.Cycle", SKYBOX_TIME_CYCLE);
	list_item(p, "Constellations.Skybox.TimeMode.Clock", SKYBOX_TIME_CLOCK);
	p = add_list(tod, "time_preset", "Constellations.Skybox.Preset");
	list_item(p, "Constellations.Skybox.Preset.Dawn", SKYBOX_PRESET_DAWN);
	list_item(p, "Constellations.Skybox.Preset.Morning", SKYBOX_PRESET_MORNING);
	list_item(p, "Constellations.Skybox.Preset.Noon", SKYBOX_PRESET_NOON);
	list_item(p, "Constellations.Skybox.Preset.Afternoon", SKYBOX_PRESET_AFTERNOON);
	list_item(p, "Constellations.Skybox.Preset.Golden", SKYBOX_PRESET_GOLDEN);
	list_item(p, "Constellations.Skybox.Preset.Sunset", SKYBOX_PRESET_SUNSET);
	list_item(p, "Constellations.Skybox.Preset.Dusk", SKYBOX_PRESET_DUSK);
	list_item(p, "Constellations.Skybox.Preset.Night", SKYBOX_PRESET_NIGHT);
	list_item(p, "Constellations.Skybox.Preset.Midnight", SKYBOX_PRESET_MIDNIGHT);
	p = obs_properties_add_float_slider(tod, "custom_time", obs_module_text("Constellations.Skybox.Time"), 0.0,
					    24.0, 0.05);
	obs_property_float_set_suffix(p, " h");
	obs_property_set_long_description(p, obs_module_text("Constellations.Skybox.Time.Desc"));
	p = obs_properties_add_float_slider(tod, "cycle_minutes", obs_module_text("Constellations.Skybox.CycleMinutes"),
					    0.5, 240.0, 0.5);
	obs_property_float_set_suffix(p, " min");
	p = obs_properties_add_float_slider(tod, "clock_offset", obs_module_text("Constellations.Skybox.ClockOffset"),
					    -12.0, 12.0, 0.25);
	obs_property_float_set_suffix(p, " h");
	p = obs_properties_add_float_slider(tod, "sunrise", obs_module_text("Constellations.Skybox.Sunrise"), 3.0, 11.0,
					    0.05);
	obs_property_float_set_suffix(p, " h");
	p = obs_properties_add_float_slider(tod, "sunset", obs_module_text("Constellations.Skybox.Sunset"), 13.0, 23.0,
					    0.05);
	obs_property_float_set_suffix(p, " h");
	obs_properties_add_group(props, "time_group", obs_module_text("Constellations.Skybox.Group.Time"),
				 OBS_GROUP_NORMAL, tod);

	/* Weather */
	obs_properties_t *wx = obs_properties_create();
	p = add_list(wx, "weather", "Constellations.Skybox.Weather");
	list_item(p, "Constellations.Skybox.Weather.Clear", SKYBOX_WEATHER_CLEAR);
	list_item(p, "Constellations.Skybox.Weather.Partly", SKYBOX_WEATHER_PARTLY);
	list_item(p, "Constellations.Skybox.Weather.Cloudy", SKYBOX_WEATHER_CLOUDY);
	list_item(p, "Constellations.Skybox.Weather.Overcast", SKYBOX_WEATHER_OVERCAST);
	list_item(p, "Constellations.Skybox.Weather.Rain", SKYBOX_WEATHER_RAIN);
	list_item(p, "Constellations.Skybox.Weather.Storm", SKYBOX_WEATHER_STORM);
	list_item(p, "Constellations.Skybox.Weather.Snow", SKYBOX_WEATHER_SNOW);
	list_item(p, "Constellations.Skybox.Weather.Fog", SKYBOX_WEATHER_FOG);
	p = obs_properties_add_float_slider(wx, "transition_secs", obs_module_text("Constellations.Skybox.Transition"),
					    0.0, 60.0, 0.5);
	obs_property_float_set_suffix(p, " s");
	obs_property_set_long_description(p, obs_module_text("Constellations.Skybox.Transition.Desc"));
	obs_properties_add_float_slider(wx, "cloud_adjust", obs_module_text("Constellations.Skybox.CloudAdjust"), -1.0,
					1.0, 0.01);
	obs_properties_add_float_slider(wx, "precip", obs_module_text("Constellations.Skybox.Precip"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(wx, "lightning", obs_module_text("Constellations.Skybox.Lightning"), 0.0, 4.0,
					0.05);
	obs_properties_add_float_slider(wx, "fog_density", obs_module_text("Constellations.Skybox.FogDensity"), 0.0,
					2.0, 0.01);
	obs_properties_add_group(props, "weather_group", obs_module_text("Constellations.Skybox.Group.Weather"),
				 OBS_GROUP_NORMAL, wx);

	/* Clouds & wind */
	obs_properties_t *wind = obs_properties_create();
	obs_properties_add_float_slider(wind, "cloud_scale", obs_module_text("Constellations.Skybox.CloudScale"), 0.25,
					4.0, 0.05);
	p = obs_properties_add_float_slider(wind, "wind_dir", obs_module_text("Constellations.Skybox.WindDir"), -180.0,
					    180.0, 1.0);
	obs_property_float_set_suffix(p, "\xC2\xB0");
	obs_property_set_long_description(p, obs_module_text("Constellations.Skybox.WindDir.Desc"));
	obs_properties_add_float_slider(wind, "wind_strength", obs_module_text("Constellations.Skybox.WindStrength"),
					0.0, 100.0, 1.0);
	obs_properties_add_float_slider(wind, "anim_speed", obs_module_text("Constellations.Skybox.AnimSpeed"), 0.0,
					4.0, 0.05);
	obs_properties_add_group(props, "wind_group", obs_module_text("Constellations.Skybox.Group.Wind"),
				 OBS_GROUP_NORMAL, wind);

	/* Sun & moon */
	obs_properties_t *cel = obs_properties_create();
	add_toggle(cel, "show_sun", "Constellations.Skybox.ShowSun");
	obs_properties_add_float_slider(cel, "sun_size", obs_module_text("Constellations.Skybox.SunSize"), 0.25, 3.0,
					0.05);
	add_toggle(cel, "show_moon", "Constellations.Skybox.ShowMoon");
	obs_properties_add_float_slider(cel, "moon_size", obs_module_text("Constellations.Skybox.MoonSize"), 0.25, 3.0,
					0.05);
	add_toggle(cel, "real_moon_phase", "Constellations.Skybox.RealMoonPhase");
	p = obs_properties_add_float_slider(cel, "moon_phase", obs_module_text("Constellations.Skybox.MoonPhase"), 0.0,
					    1.0, 0.01);
	obs_property_set_long_description(p, obs_module_text("Constellations.Skybox.MoonPhase.Desc"));
	obs_properties_add_float_slider(cel, "arc_height", obs_module_text("Constellations.Skybox.ArcHeight"), 0.1, 1.0,
					0.01);
	obs_properties_add_float_slider(cel, "arc_width", obs_module_text("Constellations.Skybox.ArcWidth"), 0.2, 1.6,
					0.01);
	obs_properties_add_float_slider(cel, "arc_offset", obs_module_text("Constellations.Skybox.ArcOffset"), -0.5,
					0.5, 0.01);
	obs_properties_add_group(props, "celestial_group", obs_module_text("Constellations.Skybox.Group.Celestial"),
				 OBS_GROUP_NORMAL, cel);

	/* Stars */
	obs_properties_t *stars = obs_properties_create();
	add_toggle(stars, "stars", "Constellations.Skybox.Stars");
	obs_properties_add_float_slider(stars, "star_density", obs_module_text("Constellations.Skybox.StarDensity"),
					0.0, 1.0, 0.01);
	obs_properties_add_float_slider(stars, "twinkle_speed", obs_module_text("Constellations.Skybox.TwinkleSpeed"),
					0.0, 5.0, 0.05);
	add_toggle(stars, "shooting_stars", "Constellations.Skybox.ShootingStars");
	obs_properties_add_float_slider(stars, "shooting_per_min",
					obs_module_text("Constellations.Skybox.ShootingRate"), 0.1, 30.0, 0.1);
	obs_properties_add_group(props, "stars_group", obs_module_text("Constellations.Skybox.Group.Stars"),
				 OBS_GROUP_NORMAL, stars);

	/* Aurora */
	obs_properties_t *aur = obs_properties_create();
	add_toggle(aur, "aurora", "Constellations.Skybox.Aurora");
	obs_properties_add_float_slider(aur, "aurora_intensity",
					obs_module_text("Constellations.Skybox.AuroraIntensity"), 0.0, 2.0, 0.01);
	obs_properties_add_color(aur, "aurora_color1", obs_module_text("Constellations.Skybox.AuroraColor1"));
	obs_properties_add_color(aur, "aurora_color2", obs_module_text("Constellations.Skybox.AuroraColor2"));
	obs_properties_add_group(props, "aurora_group", obs_module_text("Constellations.Skybox.Group.Aurora"),
				 OBS_GROUP_NORMAL, aur);

	/* Rainbow & birds */
	obs_properties_t *extra = obs_properties_create();
	p = add_list(extra, "rainbow_mode", "Constellations.Skybox.Rainbow");
	list_item(p, "Constellations.Skybox.Rainbow.Off", SKYBOX_RAINBOW_OFF);
	list_item(p, "Constellations.Skybox.Rainbow.AfterRain", SKYBOX_RAINBOW_AFTER_RAIN);
	list_item(p, "Constellations.Skybox.Rainbow.Always", SKYBOX_RAINBOW_ALWAYS);
	obs_property_set_long_description(p, obs_module_text("Constellations.Skybox.Rainbow.Desc"));
	obs_properties_add_float_slider(extra, "rainbow_intensity",
					obs_module_text("Constellations.Skybox.RainbowIntensity"), 0.0, 1.5, 0.01);
	p = obs_properties_add_float_slider(extra, "rainbow_secs", obs_module_text("Constellations.Skybox.RainbowSecs"),
					    5.0, 600.0, 1.0);
	obs_property_float_set_suffix(p, " s");
	add_toggle(extra, "birds", "Constellations.Skybox.Birds");
	obs_properties_add_float_slider(extra, "birds_per_min", obs_module_text("Constellations.Skybox.BirdRate"), 0.1,
					20.0, 0.1);
	obs_properties_add_int_slider(extra, "bird_count", obs_module_text("Constellations.Skybox.BirdCount"), 1, 9, 1);
	obs_properties_add_float_slider(extra, "bird_size", obs_module_text("Constellations.Skybox.BirdSize"), 0.3, 3.0,
					0.05);
	obs_properties_add_color(extra, "bird_color", obs_module_text("Constellations.Skybox.BirdColor"));
	obs_properties_add_group(props, "extras_group", obs_module_text("Constellations.Skybox.Group.Extras"),
				 OBS_GROUP_NORMAL, extra);

	/* Colors */
	obs_properties_t *colors = obs_properties_create();
	p = add_list(colors, "theme", "Constellations.Skybox.Theme");
	list_item(p, "Constellations.Skybox.Theme.Natural", SKYBOX_THEME_NATURAL);
	list_item(p, "Constellations.Skybox.Theme.Pastel", SKYBOX_THEME_PASTEL);
	list_item(p, "Constellations.Skybox.Theme.Synthwave", SKYBOX_THEME_SYNTHWAVE);
	list_item(p, "Constellations.Skybox.Theme.Alien", SKYBOX_THEME_ALIEN);
	list_item(p, "Constellations.Skybox.Theme.Monochrome", SKYBOX_THEME_MONOCHROME);
	list_item(p, "Constellations.Skybox.Theme.Custom", SKYBOX_THEME_CUSTOM);
	obs_property_set_long_description(p, obs_module_text("Constellations.Skybox.Theme.Desc"));
	obs_properties_add_color(colors, "mono_tint", obs_module_text("Constellations.Skybox.MonoTint"));
	for (int i = 0; i < SKYBOX_PAL_COUNT; ++i)
		obs_properties_add_color(colors, custom_color_keys[i], obs_module_text(custom_color_labels[i]));
	obs_properties_add_float_slider(colors, "saturation", obs_module_text("Constellations.Skybox.Saturation"), 0.0,
					2.0, 0.01);
	obs_properties_add_float_slider(colors, "brightness", obs_module_text("Constellations.Skybox.Brightness"), 0.2,
					2.0, 0.01);
	obs_properties_add_group(props, "colors_group", obs_module_text("Constellations.Skybox.Group.Colors"),
				 OBS_GROUP_NORMAL, colors);

	return props;
}

static obs_properties_t *csb_source_props(void *data)
{
	UNUSED_PARAMETER(data);
	return csb_props_common(false);
}

static obs_properties_t *csb_filter_props(void *data)
{
	UNUSED_PARAMETER(data);
	return csb_props_common(true);
}

static void csb_defaults_common(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "layers", 0);
	obs_data_set_default_int(settings, "style", 0);
	obs_data_set_default_int(settings, "sky_bands", 8);
	obs_data_set_default_int(settings, "seed", 1234);

	obs_data_set_default_int(settings, "time_mode", SKYBOX_TIME_PRESET);
	obs_data_set_default_int(settings, "time_preset", SKYBOX_PRESET_AFTERNOON);
	obs_data_set_default_double(settings, "custom_time", 15.0);
	obs_data_set_default_double(settings, "cycle_minutes", 10.0);
	obs_data_set_default_double(settings, "clock_offset", 0.0);
	obs_data_set_default_double(settings, "sunrise", 6.0);
	obs_data_set_default_double(settings, "sunset", 18.5);

	obs_data_set_default_int(settings, "weather", SKYBOX_WEATHER_PARTLY);
	obs_data_set_default_double(settings, "transition_secs", 8.0);
	obs_data_set_default_double(settings, "cloud_adjust", 0.0);
	obs_data_set_default_double(settings, "precip", 1.0);
	obs_data_set_default_double(settings, "lightning", 1.0);
	obs_data_set_default_double(settings, "fog_density", 1.0);

	obs_data_set_default_double(settings, "cloud_scale", 1.0);
	obs_data_set_default_double(settings, "wind_dir", 0.0);
	obs_data_set_default_double(settings, "wind_strength", 20.0);
	obs_data_set_default_double(settings, "anim_speed", 1.0);

	obs_data_set_default_bool(settings, "show_sun", true);
	obs_data_set_default_double(settings, "sun_size", 1.0);
	obs_data_set_default_bool(settings, "show_moon", true);
	obs_data_set_default_double(settings, "moon_size", 1.0);
	obs_data_set_default_bool(settings, "real_moon_phase", false);
	obs_data_set_default_double(settings, "moon_phase", 0.4);
	obs_data_set_default_double(settings, "arc_height", 0.75);
	obs_data_set_default_double(settings, "arc_width", 0.8);
	obs_data_set_default_double(settings, "arc_offset", 0.0);

	obs_data_set_default_bool(settings, "stars", true);
	obs_data_set_default_double(settings, "star_density", 0.35);
	obs_data_set_default_double(settings, "twinkle_speed", 1.0);
	obs_data_set_default_bool(settings, "shooting_stars", true);
	obs_data_set_default_double(settings, "shooting_per_min", 2.0);

	obs_data_set_default_bool(settings, "aurora", false);
	obs_data_set_default_double(settings, "aurora_intensity", 0.8);
	obs_data_set_default_int(settings, "aurora_color1", 0xFFA0FF3C);
	obs_data_set_default_int(settings, "aurora_color2", 0xFFFF5C8A);

	obs_data_set_default_int(settings, "rainbow_mode", SKYBOX_RAINBOW_AFTER_RAIN);
	obs_data_set_default_double(settings, "rainbow_intensity", 1.0);
	obs_data_set_default_double(settings, "rainbow_secs", 60.0);

	obs_data_set_default_bool(settings, "birds", true);
	obs_data_set_default_double(settings, "birds_per_min", 1.0);
	obs_data_set_default_int(settings, "bird_count", 5);
	obs_data_set_default_double(settings, "bird_size", 1.0);
	obs_data_set_default_int(settings, "bird_color", 0xFF221A1A);

	obs_data_set_default_int(settings, "theme", SKYBOX_THEME_NATURAL);
	obs_data_set_default_int(settings, "mono_tint", 0xFFFFC49F);
	struct skybox_rgb natural[SKYBOX_PAL_COUNT];
	skybox_theme_palette(SKYBOX_THEME_NATURAL, natural);
	for (int i = 0; i < SKYBOX_PAL_COUNT; ++i)
		obs_data_set_default_int(settings, custom_color_keys[i], rgb_to_obs(natural[i]));
	obs_data_set_default_double(settings, "saturation", 1.0);
	obs_data_set_default_double(settings, "brightness", 1.0);
}

static void csb_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	csb_defaults_common(settings);
}

static void csb_filter_defaults(obs_data_t *settings)
{
	csb_defaults_common(settings);
}

static struct obs_source_info csb_source_info = {
	.id = "constellations_skybox_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = csb_source_get_name,
	.create = csb_source_create,
	.destroy = csb_destroy,
	.update = csb_update,
	.video_tick = csb_tick,
	.video_render = csb_source_render,
	.get_width = csb_width,
	.get_height = csb_height,
	.get_properties = csb_source_props,
	.get_defaults = csb_source_defaults,
};

static struct obs_source_info csb_filter_info = {
	.id = "constellations_skybox_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = csb_filter_get_name,
	.create = csb_filter_create,
	.destroy = csb_destroy,
	.update = csb_update,
	.video_tick = csb_tick,
	.video_render = csb_filter_render,
	.get_properties = csb_filter_props,
	.get_defaults = csb_filter_defaults,
};

void constellations_register_skybox(void)
{
	obs_register_source(&csb_source_info);
	obs_register_source(&csb_filter_info);
}
