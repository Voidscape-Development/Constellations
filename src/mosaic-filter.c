/*
Constellations - Tile Mosaic filter
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <stdio.h>
#include <util/threading.h>
#include <math.h>

#include "common.h"
#include "palette.h"
#include "tiles-math.h"

enum {
	P_CANVAS,
	P_ORIGIN,
	P_GRID_ROT,
	P_TILE_PX,
	P_EDGE,
	P_SHAPE,
	P_GROUT,
	P_GROUT_COLOR,
	P_BEVEL,
	P_VARIATION,
	P_SAMPLE,
	P_MIX,
	P_COUNT,
};

static const char *const param_names[] = {
	"canvas_size", "origin_px",   "grid_rot", "tile_px",   "edge",        "shape",
	"grout",       "grout_color", "bevel",    "variation", "sample_mode", "mix_amount",
};
_Static_assert(OBS_COUNTOF(param_names) == P_COUNT, "param_names must list every P_ uniform");

static const char *const shape_labels[TILES_SHAPE_COUNT] = {
	"Tiles.Shape.Square",  "Tiles.Shape.Circle",  "Tiles.Shape.Hexagon", "Tiles.Shape.Triangle",
	"Tiles.Shape.Diamond", "Tiles.Shape.Brick",   "Tiles.Shape.Octagon", "Tiles.Shape.Cairo",
	"Tiles.Shape.Mosaic",  "Tiles.Shape.Shatter",
};

struct cmos_config {
	int shape, sample;
	float tile_px, rotation_deg, grout_px, bevel, variation, mix;
	uint32_t grout_color;
	char palette[64];
};

struct cmos {
	obs_source_t *self;
	struct cfx_effect fx;
	pthread_mutex_t mutex;
	struct cmos_config cfg;
};

static const char *cmos_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.MosaicFilter.Name");
}

static void cmos_update(void *data, obs_data_t *settings)
{
	struct cmos *s = data;
	struct cmos_config c = {
		.shape = (int)obs_data_get_int(settings, "shape"),
		.sample = (int)obs_data_get_int(settings, "sample_mode"),
		.tile_px = fmaxf((float)obs_data_get_double(settings, "tile_size"), 2.0f),
		.rotation_deg = (float)obs_data_get_double(settings, "rotation"),
		.grout_px = (float)obs_data_get_double(settings, "grout"),
		.bevel = (float)obs_data_get_double(settings, "bevel"),
		.variation = (float)obs_data_get_double(settings, "variation"),
		.mix = (float)obs_data_get_double(settings, "mix") / 100.0f,
		.grout_color = (uint32_t)obs_data_get_int(settings, "grout_color"),
	};
	snprintf(c.palette, sizeof(c.palette), "%s", obs_data_get_string(settings, CPAL_KEY));
	pthread_mutex_lock(&s->mutex);
	s->cfg = c;
	pthread_mutex_unlock(&s->mutex);
}

static void *cmos_create(obs_data_t *settings, obs_source_t *source)
{
	struct cmos *s = bzalloc(sizeof(*s));
	s->self = source;
	if (pthread_mutex_init(&s->mutex, NULL) != 0) {
		bfree(s);
		return NULL;
	}
	cfx_effect_load(&s->fx, "mosaic.effect", param_names, P_COUNT);
	cmos_update(s, settings);
	return s;
}

static void cmos_destroy(void *data)
{
	struct cmos *s = data;
	cfx_effect_free(&s->fx);
	pthread_mutex_destroy(&s->mutex);
	bfree(s);
}

static void cmos_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cmos *s = data;
	const struct cfx_effect *fx = &s->fx;
	obs_source_t *target = obs_filter_get_target(s->self);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (!fx->effect || w == 0 || h == 0) {
		obs_source_skip_video_filter(s->self);
		return;
	}

	pthread_mutex_lock(&s->mutex);
	struct cmos_config c = s->cfg;
	pthread_mutex_unlock(&s->mutex);
	uint32_t pal[CPAL_SLOTS];
	if (cpal_get(c.palette, pal))
		c.grout_color = (pal[0] & 0x00FFFFFF) | (c.grout_color & 0xFF000000);

	if (!cfx_filter_begin(s->self, OBS_NO_DIRECT_RENDERING))
		return;

	float rot = c.rotation_deg * (float)(M_PI / 180.0);
	struct vec4 grout;
	cfx_color_linear(&grout, c.grout_color);
	vec4_set(&grout, grout.x * grout.w, grout.y * grout.w, grout.z * grout.w, grout.w);

	cfx_set_vec2(fx, P_CANVAS, (float)w, (float)h);
	cfx_set_vec2(fx, P_ORIGIN, (float)w * 0.5f, (float)h * 0.5f);
	cfx_set_vec2(fx, P_GRID_ROT, cosf(rot), sinf(rot));
	cfx_set_float(fx, P_TILE_PX, c.tile_px);
	cfx_set_float(fx, P_EDGE, tiles_edge_width(c.shape, c.tile_px));
	if (fx->params[P_SHAPE])
		gs_effect_set_int(fx->params[P_SHAPE], c.shape);
	/* the metric runs 0..1 from a tile's centre to its edge, half a tile */
	cfx_set_float(fx, P_GROUT, c.grout_px / (c.tile_px * 0.5f));
	cfx_set_vec4(fx, P_GROUT_COLOR, &grout);
	cfx_set_float(fx, P_BEVEL, c.bevel);
	cfx_set_float(fx, P_VARIATION, c.variation);
	cfx_set_float(fx, P_SAMPLE, (float)c.sample);
	cfx_set_float(fx, P_MIX, c.mix);
	obs_source_process_filter_end(s->self, fx->effect, w, h);
}

static enum gs_color_space cmos_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred);
	return cfx_filter_color_space(((struct cmos *)data)->self);
}

static obs_properties_t *cmos_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;
#define T(k) obs_module_text("Constellations.Mosaic." k)

	p = obs_properties_add_list(props, "shape", T("Shape"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < TILES_SHAPE_COUNT; ++i)
		obs_property_list_add_int(p, obs_module_text(shape_labels[i]), i);
	p = obs_properties_add_float_slider(props, "tile_size", T("TileSize"), 4.0, 400.0, 1.0);
	obs_property_float_set_suffix(p, " px");
	p = obs_properties_add_float_slider(props, "rotation", T("Rotation"), -180.0, 180.0, 0.5);
	obs_property_float_set_suffix(p, "°");
	p = obs_properties_add_list(props, "sample_mode", T("Sample"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Sample.Centre"), 0);
	obs_property_list_add_int(p, T("Sample.Average"), 1);
	p = obs_properties_add_float_slider(props, "grout", T("Grout"), 0.0, 40.0, 0.25);
	obs_property_float_set_suffix(p, " px");
	obs_properties_add_color_alpha(props, "grout_color", T("GroutColor"));
	cpal_add_list(props);
	obs_properties_add_float_slider(props, "bevel", T("Bevel"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, "variation", T("Variation"), 0.0, 1.0, 0.01);
	p = obs_properties_add_float_slider(props, "mix", T("Mix"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	obs_property_set_long_description(p, T("Mix.Tooltip"));
#undef T
	return props;
}

static void cmos_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "shape", TILES_SHAPE_HEXAGON);
	obs_data_set_default_double(settings, "tile_size", 32.0);
	obs_data_set_default_int(settings, "sample_mode", 1);
	obs_data_set_default_double(settings, "grout", 2.0);
	obs_data_set_default_int(settings, "grout_color", 0xFF141010);
	obs_data_set_default_double(settings, "bevel", 0.35);
	obs_data_set_default_double(settings, "variation", 0.1);
	obs_data_set_default_double(settings, "mix", 100.0);
}

static struct obs_source_info cmos_info = {
	.id = "constellations_mosaic_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = cmos_get_name,
	.create = cmos_create,
	.destroy = cmos_destroy,
	.update = cmos_update,
	.video_render = cmos_render,
	.video_get_color_space = cmos_color_space,
	.get_properties = cmos_properties,
	.get_defaults = cmos_defaults,
};

void constellations_register_mosaic(void)
{
	obs_register_source(&cmos_info);
}
