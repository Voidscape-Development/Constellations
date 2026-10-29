/*
Constellations - Topography Lines Source
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

#define CTOPO_CONTOUR_LEVELS 12.0f
/* Lattice indices wrap here, matching the shader's periodic hash. */
#define CTOPO_HASH_PERIOD 4096.0

enum {
	P_CANVAS_SIZE,
	P_NOISE_SEED,
	P_ZOOM_AMOUNT,
	P_SMOOTHNESS,
	P_OCT_OFF0,
	P_OCT_OFF1,
	P_OCT_OFF2,
	P_OCT_OFF3,
	P_OCT_OFF4,
	P_GRID_OFFSET,
	P_CONTOUR_LEVELS,
	P_LINE_HALF_PX,
	P_LINE_COLOR,
	P_GLOW_ENABLED,
	P_GLOW_SIZE_PX,
	P_GLOW_LEVEL,
	P_GLOW_COLOR,
	P_GRID_ENABLED,
	P_GRID_FOLLOW,
	P_GRID_SIZE_PX,
	P_GRID_LINE_HALF_PX,
	P_GRID_COLOR,
	P_COUNT,
};

static const char *const param_names[] = {
	"canvas_size", "noise_seed",   "zoom_amount",       "smoothness",  "oct_off0",       "oct_off1",
	"oct_off2",    "oct_off3",     "oct_off4",          "grid_offset", "contour_levels", "line_half_px",
	"line_color",  "glow_enabled", "glow_size_px",      "glow_level",  "glow_color",     "grid_enabled",
	"grid_follow", "grid_size_px", "grid_line_half_px", "grid_color",
};
_Static_assert(OBS_COUNTOF(param_names) == P_COUNT, "param_names must list every P_ uniform");

struct ctopo_config {
	uint32_t width, height;
	int seed;

	float line_size;
	struct vec4 line_color; /* sRGB-encoded; the shader mixes in sRGB */

	bool glow;
	struct vec4 glow_color;
	float glow_size;
	bool glow_pulse;
	float glow_pulse_speed;

	bool drift;
	float drift_speed;
	float drift_angle_deg;

	float smoothness;
	float zoom;

	bool grid;
	bool grid_follow;
	struct vec4 grid_color;
	float grid_size;
	float grid_line_size;
};

struct ctopo_source {
	obs_source_t *self;
	bool is_filter;
	struct cfx_effect fx;

	/* update() runs on the UI thread: it fills `pending`, and tick()
	 * copies it to `cfg`, so a frame never mixes old and new settings. */
	pthread_mutex_t mutex;
	struct ctopo_config pending;
	bool has_pending;
	volatile long width, height;

	/* Graphics thread. */
	struct ctopo_config cfg;
	/* Accumulated map-space slide in double; integrating each tick means
	 * speed and angle changes steer the drift instead of teleporting the
	 * terrain. */
	double drift_x, drift_y;
	double pulse_phase; /* cycles, wrapped */
};

static const char *ctopo_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.TopoSource.Name");
}

static const char *ctopo_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.TopoFilter.Name");
}

static void ctopo_update(void *data, obs_data_t *settings)
{
	struct ctopo_source *s = data;
	struct ctopo_config c = {0};
	if (!s->is_filter) {
		c.width = (uint32_t)obs_data_get_int(settings, "width");
		c.height = (uint32_t)obs_data_get_int(settings, "height");
		if (c.width == 0)
			c.width = 1920;
		if (c.height == 0)
			c.height = 1080;
	}
	c.seed = (int)obs_data_get_int(settings, "seed");
	c.line_size = (float)obs_data_get_double(settings, "line_size");
	if (c.line_size < 0.5f)
		c.line_size = 0.5f;
	vec4_from_rgba(&c.line_color, (uint32_t)obs_data_get_int(settings, "line_color"));
	c.glow = obs_data_get_bool(settings, "glow");
	vec4_from_rgba(&c.glow_color, (uint32_t)obs_data_get_int(settings, "glow_color"));
	c.glow_size = (float)obs_data_get_double(settings, "glow_size");
	c.glow_pulse = obs_data_get_bool(settings, "glow_pulse");
	c.glow_pulse_speed = (float)obs_data_get_double(settings, "glow_pulse_speed");
	c.drift = obs_data_get_bool(settings, "location_drift");
	c.drift_speed = (float)obs_data_get_double(settings, "location_drift_speed");
	c.drift_angle_deg = (float)obs_data_get_double(settings, "location_drift_angle");
	c.smoothness = (float)obs_data_get_double(settings, "smoothness");
	c.zoom = (float)obs_data_get_double(settings, "zoom");
	if (c.zoom < 0.05f)
		c.zoom = 0.05f;
	c.grid = obs_data_get_bool(settings, "grid_backdrop");
	c.grid_follow = obs_data_get_bool(settings, "grid_follow");
	vec4_from_rgba(&c.grid_color, (uint32_t)obs_data_get_int(settings, "grid_color"));
	c.grid_size = (float)obs_data_get_double(settings, "grid_size");
	c.grid_line_size = (float)obs_data_get_double(settings, "grid_line_size");
	if (c.grid_line_size < 0.5f)
		c.grid_line_size = 0.5f;

	os_atomic_set_long(&s->width, (long)c.width);
	os_atomic_set_long(&s->height, (long)c.height);
	pthread_mutex_lock(&s->mutex);
	s->pending = c;
	s->has_pending = true;
	pthread_mutex_unlock(&s->mutex);
}

static void *ctopo_create_common(obs_data_t *settings, obs_source_t *source, bool is_filter)
{
	struct ctopo_source *s = bzalloc(sizeof(*s));
	s->self = source;
	s->is_filter = is_filter;
	pthread_mutex_init_value(&s->mutex);
	if (pthread_mutex_init(&s->mutex, NULL) != 0) {
		bfree(s);
		return NULL;
	}
	cfx_effect_load(&s->fx, "topography.effect", param_names, P_COUNT);
	ctopo_update(s, settings);
	return s;
}

static void *ctopo_create(obs_data_t *settings, obs_source_t *source)
{
	return ctopo_create_common(settings, source, false);
}

static void *ctopo_filter_create(obs_data_t *settings, obs_source_t *source)
{
	return ctopo_create_common(settings, source, true);
}

static void ctopo_destroy(void *data)
{
	struct ctopo_source *s = data;
	cfx_effect_free(&s->fx);
	pthread_mutex_destroy(&s->mutex);
	bfree(s);
}

static void ctopo_tick(void *data, float seconds)
{
	struct ctopo_source *s = data;
	pthread_mutex_lock(&s->mutex);
	if (s->has_pending) {
		s->cfg = s->pending;
		s->has_pending = false;
	}
	pthread_mutex_unlock(&s->mutex);

	const struct ctopo_config *c = &s->cfg;
	if (c->drift && c->drift_speed != 0.0f) {
		/* Speed is on-screen px/s at zoom 1; one map unit spans 100 px.
		 * Subtracting makes the terrain slide toward the drift angle. */
		double ang = (double)c->drift_angle_deg * (M_PI / 180.0);
		double step = (double)c->drift_speed * (double)seconds / 100.0;
		s->drift_x -= cos(ang) * step;
		s->drift_y -= sin(ang) * step;
	}
	if (c->glow_pulse)
		s->pulse_phase = clat_fmod_pos(s->pulse_phase + (double)c->glow_pulse_speed * seconds, 1.0);
}

static void ctopo_draw(struct ctopo_source *s, uint32_t width, uint32_t height)
{
	const struct cfx_effect *fx = &s->fx;
	const struct ctopo_config *c = &s->cfg;
	if (!fx->effect || width == 0 || height == 0)
		return;

	float glow_level = 1.0f;
	if (c->glow_pulse)
		glow_level = 0.15f + 0.85f * (0.5f + 0.5f * sinf((float)(s->pulse_phase * CLAT_TWO_PI)));

	cfx_set_vec2(fx, P_CANVAS_SIZE, (float)width, (float)height);
	cfx_set_float(fx, P_NOISE_SEED, (float)c->seed);
	cfx_set_float(fx, P_ZOOM_AMOUNT, c->zoom);
	cfx_set_float(fx, P_SMOOTHNESS, c->smoothness);

	/* Octave k samples the lattice at M^k * map + C_k, with M a
	 * rotate-and-scale and C_k its accumulated constant shifts. The drift
	 * part of that offset is computed here in double and handed over as a
	 * wrapped whole index plus a fraction. */
	float offs[CLAT_TOPO_OCTAVES][4];
	clat_topo_octave_offsets(s->drift_x, s->drift_y, CTOPO_HASH_PERIOD, offs);
	for (int k = 0; k < CLAT_TOPO_OCTAVES; ++k)
		cfx_set_vec4f(fx, P_OCT_OFF0 + k, offs[k][0], offs[k][1], offs[k][2], offs[k][3]);
	double gs_world = fmax((double)c->grid_size, 4.0) / 100.0;
	cfx_set_vec2(fx, P_GRID_OFFSET, (float)clat_fmod_pos(s->drift_x, gs_world),
		     (float)clat_fmod_pos(s->drift_y, gs_world));

	cfx_set_float(fx, P_CONTOUR_LEVELS, CTOPO_CONTOUR_LEVELS);
	cfx_set_float(fx, P_LINE_HALF_PX, c->line_size * 0.5f);
	cfx_set_vec4(fx, P_LINE_COLOR, &c->line_color);
	cfx_set_float(fx, P_GLOW_ENABLED, c->glow ? 1.0f : 0.0f);
	cfx_set_float(fx, P_GLOW_SIZE_PX, c->glow_size);
	cfx_set_float(fx, P_GLOW_LEVEL, glow_level);
	cfx_set_vec4(fx, P_GLOW_COLOR, &c->glow_color);
	cfx_set_float(fx, P_GRID_ENABLED, c->grid ? 1.0f : 0.0f);
	cfx_set_float(fx, P_GRID_FOLLOW, c->grid_follow ? 1.0f : 0.0f);
	cfx_set_float(fx, P_GRID_SIZE_PX, c->grid_size);
	cfx_set_float(fx, P_GRID_LINE_HALF_PX, c->grid_line_size * 0.5f);
	cfx_set_vec4(fx, P_GRID_COLOR, &c->grid_color);

	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	while (gs_effect_loop(fx->effect, "Draw"))
		gs_draw_sprite(NULL, 0, width, height);
	cfx_draw_end(&st);
}

static void ctopo_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct ctopo_source *s = data;
	ctopo_draw(s, s->cfg.width, s->cfg.height);
}

static void ctopo_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct ctopo_source *s = data;
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
	ctopo_draw(s, w, h);
}

static enum gs_color_space ctopo_filter_color_space(void *data, size_t count,
						    const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);
	struct ctopo_source *s = data;
	return cfx_filter_color_space(s->self);
}

static uint32_t ctopo_width(void *data)
{
	struct ctopo_source *s = data;
	return (uint32_t)os_atomic_load_long(&s->width);
}

static uint32_t ctopo_height(void *data)
{
	struct ctopo_source *s = data;
	return (uint32_t)os_atomic_load_long(&s->height);
}

static bool ctopo_glow_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool glow = obs_data_get_bool(settings, "glow");
	bool pulse = obs_data_get_bool(settings, "glow_pulse");
	bool changed = cfx_set_visible(props, "glow_color", glow);
	changed |= cfx_set_visible(props, "glow_size", glow);
	changed |= cfx_set_visible(props, "glow_pulse", glow);
	changed |= cfx_set_visible(props, "glow_pulse_speed", glow && pulse);
	return changed;
}

static bool ctopo_drift_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool drift = obs_data_get_bool(settings, "location_drift");
	bool changed = cfx_set_visible(props, "location_drift_speed", drift);
	changed |= cfx_set_visible(props, "location_drift_angle", drift);
	return changed;
}

static bool ctopo_grid_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool grid = obs_data_get_bool(settings, "grid_backdrop");
	bool changed = cfx_set_visible(props, "grid_follow", grid);
	changed |= cfx_set_visible(props, "grid_color", grid);
	changed |= cfx_set_visible(props, "grid_size", grid);
	changed |= cfx_set_visible(props, "grid_line_size", grid);
	return changed;
}

static obs_properties_t *ctopo_props_common(bool is_filter)
{
	obs_properties_t *props = obs_properties_create();

	if (!is_filter) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	}

	obs_properties_t *terrain = obs_properties_create();
	obs_properties_add_int_slider(terrain, "seed", obs_module_text("Constellations.Topo.Seed"), 0, 10000, 1);
	obs_properties_add_float_slider(terrain, "zoom", obs_module_text("Constellations.Topo.Zoom"), 0.1, 10.0, 0.01);
	obs_properties_add_float_slider(terrain, "smoothness", obs_module_text("Constellations.Topo.Smoothness"), 0.0,
					1.0, 0.01);
	obs_property_t *ld =
		obs_properties_add_bool(terrain, "location_drift", obs_module_text("Constellations.Topo.Drift"));
	obs_property_set_modified_callback(ld, ctopo_drift_modified);
	obs_properties_add_float_slider(terrain, "location_drift_speed",
					obs_module_text("Constellations.Topo.DriftSpeed"), 0.0, 500.0, 1.0);
	obs_properties_add_float_slider(terrain, "location_drift_angle",
					obs_module_text("Constellations.Topo.DriftAngle"), 0.0, 360.0, 1.0);
	obs_properties_add_group(props, "terrain_group", obs_module_text("Constellations.Topo.Group.Terrain"),
				 OBS_GROUP_NORMAL, terrain);

	obs_properties_t *lines = obs_properties_create();
	obs_properties_add_float_slider(lines, "line_size", obs_module_text("Constellations.Topo.LineSize"), 0.5, 20.0,
					0.1);
	obs_properties_add_color_alpha(lines, "line_color", obs_module_text("Constellations.Topo.LineColor"));
	obs_property_t *gl = obs_properties_add_bool(lines, "glow", obs_module_text("Constellations.Topo.Glow"));
	obs_property_set_modified_callback(gl, ctopo_glow_modified);
	obs_properties_add_color_alpha(lines, "glow_color", obs_module_text("Constellations.Topo.GlowColor"));
	obs_properties_add_float_slider(lines, "glow_size", obs_module_text("Constellations.Topo.GlowSize"), 1.0, 100.0,
					0.5);
	obs_property_t *gp =
		obs_properties_add_bool(lines, "glow_pulse", obs_module_text("Constellations.Topo.GlowPulse"));
	obs_property_set_modified_callback(gp, ctopo_glow_modified);
	obs_properties_add_float_slider(lines, "glow_pulse_speed",
					obs_module_text("Constellations.Topo.GlowPulseSpeed"), 0.05, 5.0, 0.05);
	obs_properties_add_group(props, "lines_group", obs_module_text("Constellations.Topo.Group.Lines"),
				 OBS_GROUP_NORMAL, lines);

	obs_properties_t *grid = obs_properties_create();
	obs_property_t *ge =
		obs_properties_add_bool(grid, "grid_backdrop", obs_module_text("Constellations.Topo.GridEnabled"));
	obs_property_set_modified_callback(ge, ctopo_grid_modified);
	obs_properties_add_bool(grid, "grid_follow", obs_module_text("Constellations.Topo.GridFollow"));
	obs_properties_add_color_alpha(grid, "grid_color", obs_module_text("Constellations.Topo.GridColor"));
	obs_properties_add_float_slider(grid, "grid_size", obs_module_text("Constellations.Topo.GridSize"), 10.0, 500.0,
					1.0);
	obs_properties_add_float_slider(grid, "grid_line_size", obs_module_text("Constellations.Topo.GridLineSize"),
					0.5, 20.0, 0.1);
	obs_properties_add_group(props, "grid_group", obs_module_text("Constellations.Topo.Group.Grid"),
				 OBS_GROUP_NORMAL, grid);

	return props;
}

static obs_properties_t *ctopo_props(void *data)
{
	UNUSED_PARAMETER(data);
	return ctopo_props_common(false);
}

static obs_properties_t *ctopo_filter_props(void *data)
{
	UNUSED_PARAMETER(data);
	return ctopo_props_common(true);
}

static void ctopo_defaults_common(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "seed", 1234);
	obs_data_set_default_double(settings, "zoom", 1.0);
	obs_data_set_default_double(settings, "smoothness", 1.0);
	obs_data_set_default_bool(settings, "location_drift", false);
	obs_data_set_default_double(settings, "location_drift_speed", 20.0);
	obs_data_set_default_double(settings, "location_drift_angle", 0.0);
	obs_data_set_default_double(settings, "line_size", 2.0);
	obs_data_set_default_int(settings, "line_color", 0xFFFFFFFF);
	obs_data_set_default_bool(settings, "glow", false);
	obs_data_set_default_int(settings, "glow_color", 0xFFFF9632);
	obs_data_set_default_double(settings, "glow_size", 12.0);
	obs_data_set_default_bool(settings, "glow_pulse", false);
	obs_data_set_default_double(settings, "glow_pulse_speed", 0.5);
	obs_data_set_default_bool(settings, "grid_backdrop", false);
	obs_data_set_default_bool(settings, "grid_follow", true);
	obs_data_set_default_int(settings, "grid_color", 0x64808080);
	obs_data_set_default_double(settings, "grid_size", 100.0);
	obs_data_set_default_double(settings, "grid_line_size", 1.0);
}

static void ctopo_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	ctopo_defaults_common(settings);
}

static struct obs_source_info ctopo_source_info = {
	.id = "constellations_topography_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = ctopo_get_name,
	.create = ctopo_create,
	.destroy = ctopo_destroy,
	.update = ctopo_update,
	.video_tick = ctopo_tick,
	.video_render = ctopo_render,
	.get_width = ctopo_width,
	.get_height = ctopo_height,
	.get_properties = ctopo_props,
	.get_defaults = ctopo_defaults,
};

static struct obs_source_info ctopo_filter_info = {
	.id = "constellations_topography_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = ctopo_filter_get_name,
	.create = ctopo_filter_create,
	.destroy = ctopo_destroy,
	.update = ctopo_update,
	.video_tick = ctopo_tick,
	.video_render = ctopo_filter_render,
	.video_get_color_space = ctopo_filter_color_space,
	.get_properties = ctopo_filter_props,
	.get_defaults = ctopo_defaults_common,
};

void constellations_register_topography_source(void)
{
	obs_register_source(&ctopo_source_info);
}

void constellations_register_topography_filter(void)
{
	obs_register_source(&ctopo_filter_info);
}
