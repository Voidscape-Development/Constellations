/*
Constellations - Skylines Background / Filter
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <util/threading.h>
#include <math.h>

#include "shape-defs.h"
#include "common.h"
#include "lattice.h"

/* Buildings repeat after this many slots, matching the shader. */
#define CSKY_SLOT_PERIOD 4096.0

enum {
	P_CANVAS_SIZE,
	P_NOISE_SEED,
	P_DRIFT_SLOTS,
	P_DRIFT_FRAC_PX,
	P_HEIGHT_VARIATION,
	P_MIN_HEIGHT,
	P_WIDTH_VARIATION,
	P_MIN_WIDTH,
	P_SHADE_VARIATION,
	P_WINDOW_OCCUPANCY,
	P_BUILDING_COLOR,
	P_WINDOW_ON_COLOR,
	P_WINDOW_OFF_COLOR,
	P_TWINKLE_ACTIVE,
	P_TWINKLE_PHASE,
	P_COUNT,
};

static const char *const param_names[] = {
	"canvas_size",    "noise_seed",      "drift_slots",      "drift_frac_px",   "height_variation",
	"min_height",     "width_variation", "min_width",        "shade_variation", "window_occupancy",
	"building_color", "window_on_color", "window_off_color", "twinkle_active",  "twinkle_phase",
};
_Static_assert(OBS_COUNTOF(param_names) == P_COUNT, "param_names must list every P_ uniform");

struct csky_config {
	uint32_t width, height;
	int seed;
	float height_variation;
	float min_height;
	float width_variation;
	float min_width;
	float shade_variation;
	float window_occupancy;
	/* sRGB-encoded; the shader mixes in sRGB */
	struct vec4 building_color;
	struct vec4 window_on_color;
	struct vec4 window_off_color;
	bool drift;
	float drift_speed;
	bool twinkle;
	float twinkle_speed;
};

struct csky_source {
	obs_source_t *self;
	bool is_filter;
	struct cfx_effect fx;

	/* update() runs on the UI thread: it fills `pending`, and tick()
	 * copies it to `cfg`, so a frame never mixes old and new settings. */
	pthread_mutex_t mutex;
	struct csky_config pending;
	bool has_pending;
	volatile long width, height;

	/* Graphics thread. */
	struct csky_config cfg;
	/* Integrated each tick so speed changes steer the motion instead of
	 * teleporting the skyline or snapping every window's fade. */
	double drift_px;
	double twinkle_phase; /* cycles, wrapped */

	/* A static skyline (no drift, no twinkle) is rendered once into this
	 * cache and blitted afterwards, instead of re-evaluating the
	 * procedural shader for every pixel every frame. cfg_rev counts
	 * applied configurations; the cache remembers the one it holds. */
	gs_texrender_t *cache;
	uint64_t cfg_rev, cache_rev;
	uint32_t cache_w, cache_h;
};

static const char *csky_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.SkylineSource.Name");
}

static const char *csky_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.SkylineFilter.Name");
}

static void csky_update(void *data, obs_data_t *settings)
{
	struct csky_source *s = data;
	struct csky_config c = {0};
	if (!s->is_filter) {
		c.width = (uint32_t)obs_data_get_int(settings, "width");
		c.height = (uint32_t)obs_data_get_int(settings, "height");
		if (c.width == 0)
			c.width = 1920;
		if (c.height == 0)
			c.height = 1080;
	}
	c.seed = (int)obs_data_get_int(settings, "seed");
	c.height_variation = (float)obs_data_get_double(settings, "height_variation");
	c.min_height = (float)obs_data_get_double(settings, "min_height");
	c.width_variation = (float)obs_data_get_double(settings, "width_variation");
	c.min_width = (float)obs_data_get_double(settings, "min_width");
	c.shade_variation = (float)obs_data_get_double(settings, "shade_variation");
	c.window_occupancy = (float)obs_data_get_double(settings, "window_occupancy");
	vec4_from_rgba(&c.building_color, (uint32_t)obs_data_get_int(settings, "building_color"));
	vec4_from_rgba(&c.window_on_color, (uint32_t)obs_data_get_int(settings, "window_on_color"));
	vec4_from_rgba(&c.window_off_color, (uint32_t)obs_data_get_int(settings, "window_off_color"));
	c.drift = obs_data_get_bool(settings, "location_drift");
	c.drift_speed = (float)obs_data_get_double(settings, "location_drift_speed");
	c.twinkle = obs_data_get_bool(settings, "window_twinkle");
	c.twinkle_speed = (float)obs_data_get_double(settings, "twinkle_speed");
	if (c.twinkle_speed < 0.0f)
		c.twinkle_speed = 0.0f;

	os_atomic_set_long(&s->width, (long)c.width);
	os_atomic_set_long(&s->height, (long)c.height);
	pthread_mutex_lock(&s->mutex);
	s->pending = c;
	s->has_pending = true;
	pthread_mutex_unlock(&s->mutex);
}

static void *csky_create_common(obs_data_t *settings, obs_source_t *source, bool is_filter)
{
	struct csky_source *s = bzalloc(sizeof(*s));
	s->self = source;
	s->is_filter = is_filter;
	pthread_mutex_init_value(&s->mutex);
	if (pthread_mutex_init(&s->mutex, NULL) != 0) {
		bfree(s);
		return NULL;
	}
	if (cfx_effect_load(&s->fx, "skylines.effect", param_names, P_COUNT)) {
		obs_enter_graphics();
		s->cache = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		obs_leave_graphics();
	}
	csky_update(s, settings);
	return s;
}

static void *csky_source_create(obs_data_t *settings, obs_source_t *source)
{
	return csky_create_common(settings, source, false);
}

static void *csky_filter_create(obs_data_t *settings, obs_source_t *source)
{
	return csky_create_common(settings, source, true);
}

static void csky_destroy(void *data)
{
	struct csky_source *s = data;
	if (s->cache) {
		obs_enter_graphics();
		gs_texrender_destroy(s->cache);
		obs_leave_graphics();
	}
	cfx_effect_free(&s->fx);
	pthread_mutex_destroy(&s->mutex);
	bfree(s);
}

static void csky_tick(void *data, float seconds)
{
	struct csky_source *s = data;
	pthread_mutex_lock(&s->mutex);
	if (s->has_pending) {
		s->cfg = s->pending;
		s->has_pending = false;
		s->cfg_rev++;
	}
	pthread_mutex_unlock(&s->mutex);

	const struct csky_config *c = &s->cfg;
	/* Positive drift speed slides the buildings to the right. */
	if (c->drift && c->drift_speed != 0.0f)
		s->drift_px -= (double)c->drift_speed * (double)seconds;
	if (c->twinkle && c->twinkle_speed > 0.0f)
		s->twinkle_phase =
			clat_fmod_pos(s->twinkle_phase + (double)c->twinkle_speed * seconds, CLAT_TWINKLE_PERIOD);
}

static void csky_draw(struct csky_source *s, uint32_t width, uint32_t height)
{
	const struct cfx_effect *fx = &s->fx;
	const struct csky_config *c = &s->cfg;
	if (!fx->effect || width == 0 || height == 0)
		return;
	bool twinkle_active = c->twinkle && c->twinkle_speed > 0.0f;

	/* Slots are 8.5% of the canvas height wide (see the shader). */
	double slot_w = 0.085 * (double)height;
	double slots = floor(s->drift_px / slot_w);

	cfx_set_vec2(fx, P_CANVAS_SIZE, (float)width, (float)height);
	cfx_set_float(fx, P_NOISE_SEED, (float)c->seed);
	cfx_set_float(fx, P_DRIFT_SLOTS, (float)clat_fmod_pos(slots, CSKY_SLOT_PERIOD));
	cfx_set_float(fx, P_DRIFT_FRAC_PX, (float)(s->drift_px - slots * slot_w));
	cfx_set_float(fx, P_HEIGHT_VARIATION, c->height_variation);
	cfx_set_float(fx, P_MIN_HEIGHT, c->min_height);
	cfx_set_float(fx, P_WIDTH_VARIATION, c->width_variation);
	cfx_set_float(fx, P_MIN_WIDTH, c->min_width);
	cfx_set_float(fx, P_SHADE_VARIATION, c->shade_variation);
	cfx_set_float(fx, P_WINDOW_OCCUPANCY, c->window_occupancy);
	cfx_set_vec4(fx, P_BUILDING_COLOR, &c->building_color);
	cfx_set_vec4(fx, P_WINDOW_ON_COLOR, &c->window_on_color);
	cfx_set_vec4(fx, P_WINDOW_OFF_COLOR, &c->window_off_color);
	cfx_set_float(fx, P_TWINKLE_ACTIVE, twinkle_active ? 1.0f : 0.0f);
	cfx_set_float(fx, P_TWINKLE_PHASE, (float)s->twinkle_phase);

	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	while (gs_effect_loop(fx->effect, "Draw"))
		gs_draw_sprite(NULL, 0, width, height);
	cfx_draw_end(&st);
}

static void csky_render_cached(struct csky_source *s, uint32_t width, uint32_t height)
{
	const struct csky_config *c = &s->cfg;
	bool animated = (c->drift && c->drift_speed != 0.0f) || (c->twinkle && c->twinkle_speed > 0.0f);
	if (animated || !s->cache) {
		csky_draw(s, width, height);
		s->cache_rev = 0;
		return;
	}

	if (s->cache_rev != s->cfg_rev || s->cache_w != width || s->cache_h != height) {
		gs_texrender_reset(s->cache);
		if (!gs_texrender_begin(s->cache, width, height)) {
			csky_draw(s, width, height);
			return;
		}
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
		csky_draw(s, width, height);
		gs_texrender_end(s->cache);
		s->cache_rev = s->cfg_rev;
		s->cache_w = width;
		s->cache_h = height;
	}

	gs_texture_t *tex = gs_texrender_get_texture(s->cache);
	if (!tex) {
		csky_draw(s, width, height);
		return;
	}
	/* The cache holds premultiplied linear color, sRGB-encoded like any
	 * render target; blit it back through its sRGB view. */
	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");
	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	gs_effect_set_texture_srgb(img, tex);
	while (gs_effect_loop(def, "Draw"))
		gs_draw_sprite(tex, 0, width, height);
	cfx_draw_end(&st);
}

static void csky_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct csky_source *s = data;
	csky_render_cached(s, s->cfg.width, s->cfg.height);
}

static void csky_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct csky_source *s = data;
	obs_source_t *target = obs_filter_get_target(s->self);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(s->self);
		return;
	}
	if (!cfx_filter_begin(s->self, OBS_ALLOW_DIRECT_RENDERING))
		return;
	obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), w, h);
	csky_render_cached(s, w, h);
}

static enum gs_color_space csky_filter_color_space(void *data, size_t count,
						   const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);
	struct csky_source *s = data;
	return cfx_filter_color_space(s->self);
}

static uint32_t csky_width(void *data)
{
	struct csky_source *s = data;
	return (uint32_t)os_atomic_load_long(&s->width);
}

static uint32_t csky_height(void *data)
{
	struct csky_source *s = data;
	return (uint32_t)os_atomic_load_long(&s->height);
}

static bool csky_drift_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return cfx_set_visible(props, "location_drift_speed", obs_data_get_bool(settings, "location_drift"));
}

static bool csky_twinkle_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return cfx_set_visible(props, "twinkle_speed", obs_data_get_bool(settings, "window_twinkle"));
}

static obs_properties_t *csky_props_common(bool is_filter)
{
	obs_properties_t *props = obs_properties_create();

	if (!is_filter) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	}

	obs_properties_t *buildings = obs_properties_create();
	obs_properties_add_int_slider(buildings, "seed", obs_module_text("Constellations.Skyline.Seed"), 0, 10000, 1);
	/* The variation sliders deliberately carry no modified callback. A
	 * callback that returns true makes OBS tear down and rebuild every
	 * widget in the dialog, and these are dragged continuously, so it fired
	 * on every step of the drag: the properties view stuttered and the
	 * rebuild destroyed the slider mid-drag, breaking the grab. The
	 * Minimum sliders therefore keep fixed limits covering their full
	 * useful travel, and the shader clamps each one against the floor the
	 * matching variation implies (max() for min_height, min() for
	 * min_width), so a minimum below that floor is a no-op rather than a
	 * wrong result. */
	obs_properties_add_float_slider(buildings, "height_variation",
					obs_module_text("Constellations.Skyline.HeightVariation"), 0.0, 1.0, 0.01);
	obs_property_t *mh = obs_properties_add_float_slider(
		buildings, "min_height", obs_module_text("Constellations.Skyline.MinHeight"), 0.0, 0.95, 0.01);
	obs_property_set_long_description(mh, obs_module_text("Constellations.Skyline.MinHeight.Desc"));
	obs_properties_add_float_slider(buildings, "width_variation",
					obs_module_text("Constellations.Skyline.WidthVariation"), 0.0, 1.0, 0.01);
	obs_property_t *mw = obs_properties_add_float_slider(
		buildings, "min_width", obs_module_text("Constellations.Skyline.MinWidth"), 0.1, 1.0, 0.01);
	obs_property_set_long_description(mw, obs_module_text("Constellations.Skyline.MinWidth.Desc"));
	obs_properties_add_color_alpha(buildings, "building_color",
				       obs_module_text("Constellations.Skyline.BuildingColor"));
	obs_properties_add_float_slider(buildings, "shade_variation",
					obs_module_text("Constellations.Skyline.ShadeVariation"), 0.0, 1.0, 0.01);
	obs_properties_add_group(props, "buildings_group", obs_module_text("Constellations.Skyline.Group.Buildings"),
				 OBS_GROUP_NORMAL, buildings);

	obs_properties_t *windows = obs_properties_create();
	obs_properties_add_float_slider(windows, "window_occupancy",
					obs_module_text("Constellations.Skyline.Occupancy"), 0.0, 1.0, 0.01);
	obs_properties_add_color_alpha(windows, "window_on_color",
				       obs_module_text("Constellations.Skyline.WindowOnColor"));
	obs_properties_add_color_alpha(windows, "window_off_color",
				       obs_module_text("Constellations.Skyline.WindowOffColor"));
	obs_property_t *tw =
		obs_properties_add_bool(windows, "window_twinkle", obs_module_text("Constellations.Skyline.Twinkle"));
	obs_property_set_modified_callback(tw, csky_twinkle_modified);
	obs_properties_add_float_slider(windows, "twinkle_speed",
					obs_module_text("Constellations.Skyline.TwinkleSpeed"), 0.0, 5.0, 0.05);
	obs_properties_add_group(props, "windows_group", obs_module_text("Constellations.Skyline.Group.Windows"),
				 OBS_GROUP_NORMAL, windows);

	obs_properties_t *motion = obs_properties_create();
	obs_property_t *ld =
		obs_properties_add_bool(motion, "location_drift", obs_module_text("Constellations.Skyline.Drift"));
	obs_property_set_modified_callback(ld, csky_drift_modified);
	obs_properties_add_float_slider(motion, "location_drift_speed",
					obs_module_text("Constellations.Skyline.DriftSpeed"), -500.0, 500.0, 1.0);
	obs_properties_add_group(props, "motion_group", obs_module_text("Constellations.Group.Motion"),
				 OBS_GROUP_NORMAL, motion);

	return props;
}

static obs_properties_t *csky_source_props(void *data)
{
	UNUSED_PARAMETER(data);
	return csky_props_common(false);
}

static obs_properties_t *csky_filter_props(void *data)
{
	UNUSED_PARAMETER(data);
	return csky_props_common(true);
}

static void csky_defaults_common(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "seed", 1234);
	obs_data_set_default_double(settings, "height_variation", 0.5);
	obs_data_set_default_double(settings, "min_height", 0.0);
	obs_data_set_default_double(settings, "width_variation", 0.65);
	obs_data_set_default_double(settings, "min_width", 0.1);
	obs_data_set_default_double(settings, "shade_variation", 0.2);
	obs_data_set_default_double(settings, "window_occupancy", 0.5);
	obs_data_set_default_int(settings, "building_color", 0xFF3A2A1E);
	obs_data_set_default_int(settings, "window_on_color", 0xFF66D9FF);
	obs_data_set_default_int(settings, "window_off_color", 0xFF241A12);
	obs_data_set_default_bool(settings, "location_drift", false);
	obs_data_set_default_double(settings, "location_drift_speed", 20.0);
	obs_data_set_default_bool(settings, "window_twinkle", false);
	obs_data_set_default_double(settings, "twinkle_speed", 1.0);
}

static void csky_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	csky_defaults_common(settings);
}

static void csky_filter_defaults(obs_data_t *settings)
{
	csky_defaults_common(settings);
}

static struct obs_source_info csky_source_info = {
	.id = "constellations_skylines_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = csky_source_get_name,
	.create = csky_source_create,
	.destroy = csky_destroy,
	.update = csky_update,
	.video_tick = csky_tick,
	.video_render = csky_source_render,
	.get_width = csky_width,
	.get_height = csky_height,
	.get_properties = csky_source_props,
	.get_defaults = csky_source_defaults,
};

static struct obs_source_info csky_filter_info = {
	.id = "constellations_skylines_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = csky_filter_get_name,
	.create = csky_filter_create,
	.destroy = csky_destroy,
	.update = csky_update,
	.video_tick = csky_tick,
	.video_render = csky_filter_render,
	.video_get_color_space = csky_filter_color_space,
	.get_properties = csky_filter_props,
	.get_defaults = csky_filter_defaults,
};

void constellations_register_skylines(void)
{
	obs_register_source(&csky_source_info);
	obs_register_source(&csky_filter_info);
}
