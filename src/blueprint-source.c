/*
Constellations - Blueprint Background / Filter
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

enum {
	P_CANVAS_SIZE,
	P_GRID_ROT,
	P_GRID_OFFSET,
	P_CELL_SIZE,
	P_MAJOR_EVERY,
	P_GRID_STYLE,
	P_MINOR_WIDTH,
	P_MAJOR_WIDTH,
	P_BG_COLOR,
	P_MINOR_COLOR,
	P_MAJOR_COLOR,
	P_GRAIN,
	P_VIGNETTE,
	P_COUNT,
};

static const char *const param_names[] = {
	"canvas_size", "grid_rot", "grid_offset", "cell_size",   "major_every", "grid_style", "minor_width",
	"major_width", "bg_color", "minor_color", "major_color", "grain",       "vignette",
};
_Static_assert(OBS_COUNTOF(param_names) == P_COUNT, "param_names must list every P_ uniform");

struct cbp_config {
	uint32_t width, height;
	float cell_size, major_every;
	int style;
	float minor_width, major_width;
	struct vec4 bg_color, minor_color, major_color; /* sRGB-encoded */
	float rotation_deg;
	float grain, vignette;
	float speed, angle_deg;
};

struct cbp_source {
	obs_source_t *self;
	bool is_filter;
	struct cfx_effect fx;

	/* update() fills `pending` on the UI thread; tick() applies it. */
	pthread_mutex_t mutex;
	struct cbp_config pending;
	bool has_pending;
	volatile long width, height;

	/* Graphics thread. Scroll is integrated in double and wrapped at one
	 * major period, so speed changes steer and nothing grows unbounded. */
	struct cbp_config cfg;
	double off_x, off_y;
};

static const char *cbp_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.BlueprintSource.Name");
}

static const char *cbp_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.BlueprintFilter.Name");
}

static void cbp_update(void *data, obs_data_t *settings)
{
	struct cbp_source *s = data;
	struct cbp_config c = {0};
	if (!s->is_filter) {
		c.width = (uint32_t)obs_data_get_int(settings, "width");
		c.height = (uint32_t)obs_data_get_int(settings, "height");
		if (c.width == 0)
			c.width = 1920;
		if (c.height == 0)
			c.height = 1080;
	}
	c.cell_size = fmaxf((float)obs_data_get_double(settings, "cell_size"), 2.0f);
	c.major_every = (float)obs_data_get_int(settings, "major_every");
	c.style = (int)obs_data_get_int(settings, "grid_style");
	c.minor_width = (float)obs_data_get_double(settings, "minor_width");
	c.major_width = (float)obs_data_get_double(settings, "major_width");
	vec4_from_rgba(&c.bg_color, (uint32_t)obs_data_get_int(settings, "background_color"));
	vec4_from_rgba(&c.minor_color, (uint32_t)obs_data_get_int(settings, "minor_color"));
	vec4_from_rgba(&c.major_color, (uint32_t)obs_data_get_int(settings, "major_color"));
	c.rotation_deg = (float)obs_data_get_double(settings, "rotation");
	c.grain = (float)obs_data_get_double(settings, "grain");
	c.vignette = (float)obs_data_get_double(settings, "vignette");
	c.speed = (float)obs_data_get_double(settings, "scroll_speed");
	c.angle_deg = (float)obs_data_get_double(settings, "scroll_angle");

	os_atomic_set_long(&s->width, (long)c.width);
	os_atomic_set_long(&s->height, (long)c.height);
	pthread_mutex_lock(&s->mutex);
	s->pending = c;
	s->has_pending = true;
	pthread_mutex_unlock(&s->mutex);
}

static void *cbp_create_common(obs_data_t *settings, obs_source_t *source, bool is_filter)
{
	struct cbp_source *s = bzalloc(sizeof(*s));
	s->self = source;
	s->is_filter = is_filter;
	pthread_mutex_init_value(&s->mutex);
	if (pthread_mutex_init(&s->mutex, NULL) != 0) {
		bfree(s);
		return NULL;
	}
	cfx_effect_load(&s->fx, "blueprint.effect", param_names, P_COUNT);
	cbp_update(s, settings);
	return s;
}

static void *cbp_source_create(obs_data_t *settings, obs_source_t *source)
{
	return cbp_create_common(settings, source, false);
}

static void *cbp_filter_create(obs_data_t *settings, obs_source_t *source)
{
	return cbp_create_common(settings, source, true);
}

static void cbp_destroy(void *data)
{
	struct cbp_source *s = data;
	cfx_effect_free(&s->fx);
	pthread_mutex_destroy(&s->mutex);
	bfree(s);
}

static void cbp_tick(void *data, float seconds)
{
	struct cbp_source *s = data;
	pthread_mutex_lock(&s->mutex);
	if (s->has_pending) {
		s->cfg = s->pending;
		s->has_pending = false;
	}
	pthread_mutex_unlock(&s->mutex);

	const struct cbp_config *c = &s->cfg;
	double period = (double)c->cell_size * (c->major_every >= 2.0f ? c->major_every : 1.0f);
	double ang = c->angle_deg * (M_PI / 180.0);
	double step = (double)c->speed * seconds;
	s->off_x = clat_fmod_pos(s->off_x + cos(ang) * step, period);
	s->off_y = clat_fmod_pos(s->off_y + sin(ang) * step, period);
}

static void cbp_draw(struct cbp_source *s, uint32_t w, uint32_t h)
{
	const struct cfx_effect *fx = &s->fx;
	const struct cbp_config *c = &s->cfg;
	if (!fx->effect || w == 0 || h == 0)
		return;
	float rot = c->rotation_deg * (float)(M_PI / 180.0);
	cfx_set_vec2(fx, P_CANVAS_SIZE, (float)w, (float)h);
	cfx_set_vec2(fx, P_GRID_ROT, cosf(rot), sinf(rot));
	cfx_set_vec2(fx, P_GRID_OFFSET, (float)s->off_x, (float)s->off_y);
	cfx_set_float(fx, P_CELL_SIZE, c->cell_size);
	cfx_set_float(fx, P_MAJOR_EVERY, c->major_every);
	cfx_set_float(fx, P_GRID_STYLE, (float)c->style);
	cfx_set_float(fx, P_MINOR_WIDTH, c->minor_width);
	cfx_set_float(fx, P_MAJOR_WIDTH, c->major_width);
	cfx_set_vec4(fx, P_BG_COLOR, &c->bg_color);
	cfx_set_vec4(fx, P_MINOR_COLOR, &c->minor_color);
	cfx_set_vec4(fx, P_MAJOR_COLOR, &c->major_color);
	cfx_set_float(fx, P_GRAIN, c->grain);
	cfx_set_float(fx, P_VIGNETTE, c->vignette);

	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	while (gs_effect_loop(fx->effect, "Draw"))
		gs_draw_sprite(NULL, 0, w, h);
	cfx_draw_end(&st);
}

static void cbp_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cbp_source *s = data;
	cbp_draw(s, s->cfg.width, s->cfg.height);
}

static void cbp_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cbp_source *s = data;
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
	cbp_draw(s, w, h);
}

static enum gs_color_space cbp_filter_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred);
	struct cbp_source *s = data;
	return cfx_filter_color_space(s->self);
}

static uint32_t cbp_width(void *data)
{
	return (uint32_t)os_atomic_load_long(&((struct cbp_source *)data)->width);
}

static uint32_t cbp_height(void *data)
{
	return (uint32_t)os_atomic_load_long(&((struct cbp_source *)data)->height);
}

static obs_properties_t *cbp_props_common(bool is_filter)
{
	obs_properties_t *props = obs_properties_create();
#define T(k) obs_module_text("Constellations.Blueprint." k)

	if (!is_filter) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	}

	obs_properties_t *grid = obs_properties_create();
	obs_property_t *style =
		obs_properties_add_list(grid, "grid_style", T("Style"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(style, T("Style.Lines"), 0);
	obs_property_list_add_int(style, T("Style.Dots"), 1);
	obs_property_list_add_int(style, T("Style.Crosses"), 2);
	obs_properties_add_float_slider(grid, "cell_size", T("CellSize"), 4.0, 400.0, 1.0);
	obs_property_t *me = obs_properties_add_int_slider(grid, "major_every", T("MajorEvery"), 0, 20, 1);
	obs_property_set_long_description(me, T("MajorEvery.Tooltip"));
	obs_properties_add_float_slider(grid, "minor_width", T("MinorWidth"), 0.25, 20.0, 0.25);
	obs_properties_add_float_slider(grid, "major_width", T("MajorWidth"), 0.25, 30.0, 0.25);
	obs_properties_add_color_alpha(grid, "minor_color", T("MinorColor"));
	obs_properties_add_color_alpha(grid, "major_color", T("MajorColor"));
	obs_properties_add_float_slider(grid, "rotation", T("Rotation"), -180.0, 180.0, 0.5);
	obs_properties_add_group(props, "grid_group", T("Group.Grid"), OBS_GROUP_NORMAL, grid);

	obs_properties_t *paper = obs_properties_create();
	obs_properties_add_color_alpha(paper, "background_color",
				       obs_module_text("Constellations.Canvas.BackgroundColor"));
	obs_properties_add_float_slider(paper, "grain", T("Grain"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(paper, "vignette", T("Vignette"), 0.0, 1.0, 0.01);
	obs_properties_add_group(props, "paper_group", T("Group.Paper"), OBS_GROUP_NORMAL, paper);

	obs_properties_t *motion = obs_properties_create();
	obs_properties_add_float_slider(motion, "scroll_speed", T("Speed"), -500.0, 500.0, 1.0);
	obs_properties_add_float_slider(motion, "scroll_angle", T("Angle"), 0.0, 360.0, 1.0);
	obs_properties_add_group(props, "motion_group", obs_module_text("Constellations.Group.Motion"),
				 OBS_GROUP_NORMAL, motion);
#undef T
	return props;
}

static obs_properties_t *cbp_source_props(void *data)
{
	UNUSED_PARAMETER(data);
	return cbp_props_common(false);
}

static obs_properties_t *cbp_filter_props(void *data)
{
	UNUSED_PARAMETER(data);
	return cbp_props_common(true);
}

static void cbp_defaults_common(obs_data_t *settings, uint32_t bg)
{
	obs_data_set_default_int(settings, "grid_style", 0);
	obs_data_set_default_double(settings, "cell_size", 40.0);
	obs_data_set_default_int(settings, "major_every", 5);
	obs_data_set_default_double(settings, "minor_width", 1.0);
	obs_data_set_default_double(settings, "major_width", 2.0);
	obs_data_set_default_int(settings, "minor_color", 0x40FFFFFF);
	obs_data_set_default_int(settings, "major_color", 0x99FFFFFF);
	obs_data_set_default_int(settings, "background_color", bg);
	obs_data_set_default_double(settings, "grain", 0.3);
	obs_data_set_default_double(settings, "vignette", 0.35);
	obs_data_set_default_double(settings, "scroll_speed", 0.0);
	obs_data_set_default_double(settings, "scroll_angle", 45.0);
}

static void cbp_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	cbp_defaults_common(settings, 0xFF7A3D0F); /* #0F3D7A blueprint blue */
}

static void cbp_filter_defaults(obs_data_t *settings)
{
	cbp_defaults_common(settings, 0x007A3D0F); /* transparent: the grid over the source */
}

static struct obs_source_info cbp_source_info = {
	.id = "constellations_blueprint_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = cbp_source_get_name,
	.create = cbp_source_create,
	.destroy = cbp_destroy,
	.update = cbp_update,
	.video_tick = cbp_tick,
	.video_render = cbp_source_render,
	.get_width = cbp_width,
	.get_height = cbp_height,
	.get_properties = cbp_source_props,
	.get_defaults = cbp_source_defaults,
};

static struct obs_source_info cbp_filter_info = {
	.id = "constellations_blueprint_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = cbp_filter_get_name,
	.create = cbp_filter_create,
	.destroy = cbp_destroy,
	.update = cbp_update,
	.video_tick = cbp_tick,
	.video_render = cbp_filter_render,
	.video_get_color_space = cbp_filter_color_space,
	.get_properties = cbp_filter_props,
	.get_defaults = cbp_filter_defaults,
};

void constellations_register_blueprint(void)
{
	obs_register_source(&cbp_source_info);
	obs_register_source(&cbp_filter_info);
}
