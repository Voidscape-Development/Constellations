/*
Constellations - Pixel Sort / Glitch transition
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* The look is in data/effects/glitch_transition.effect; this file turns
 * settings into uniforms. */

#include <obs-module.h>
#include <stdio.h>
#include <util/threading.h>
#include <math.h>

#include "palette.h"
#include "transition-common.h"

struct glitch_info {
	struct transition_base base;

	gs_eparam_t *ep_a_tex, *ep_b_tex, *ep_canvas, *ep_progress, *ep_style, *ep_intensity, *ep_block,
		*ep_sort_threshold, *ep_sort_length, *ep_sort_vertical, *ep_split, *ep_tint, *ep_tint_color, *ep_seed;

	pthread_mutex_t mutex;
	float style, intensity, block, threshold, length, vertical, split, tint, seed;
	uint32_t tint_rgba;
	char palette[64];
};

static const struct tc_param glitch_params[] = {
	TC_PARAM(struct glitch_info, ep_a_tex, "a_tex"),
	TC_PARAM(struct glitch_info, ep_b_tex, "b_tex"),
	TC_PARAM(struct glitch_info, ep_canvas, "canvas"),
	TC_PARAM(struct glitch_info, ep_progress, "progress"),
	TC_PARAM(struct glitch_info, ep_style, "style"),
	TC_PARAM(struct glitch_info, ep_intensity, "intensity"),
	TC_PARAM(struct glitch_info, ep_block, "block"),
	TC_PARAM(struct glitch_info, ep_sort_threshold, "sort_threshold"),
	TC_PARAM(struct glitch_info, ep_sort_length, "sort_length"),
	TC_PARAM(struct glitch_info, ep_sort_vertical, "sort_vertical"),
	TC_PARAM(struct glitch_info, ep_split, "split"),
	TC_PARAM(struct glitch_info, ep_tint, "tint"),
	TC_PARAM(struct glitch_info, ep_tint_color, "tint_color"),
	TC_PARAM(struct glitch_info, ep_seed, "seed"),
};

static const char *glitch_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Glitch.Transition");
}

static void glitch_update(void *data, obs_data_t *settings)
{
	struct glitch_info *g = data;
	pthread_mutex_lock(&g->mutex);
	g->style = (float)obs_data_get_int(settings, "style");
	g->intensity = (float)obs_data_get_double(settings, "intensity") / 100.0f;
	g->block = fmaxf((float)obs_data_get_double(settings, "block"), 2.0f);
	g->threshold = (float)obs_data_get_double(settings, "sort_threshold") / 100.0f;
	g->length = (float)obs_data_get_double(settings, "sort_length") / 100.0f;
	g->vertical = (float)obs_data_get_int(settings, "sort_direction");
	g->split = (float)obs_data_get_double(settings, "split");
	g->tint = (float)obs_data_get_double(settings, "tint") / 100.0f;
	g->seed = (float)obs_data_get_int(settings, "seed");
	g->tint_rgba = (uint32_t)obs_data_get_int(settings, "tint_color") | 0xFF000000;
	snprintf(g->palette, sizeof(g->palette), "%s", obs_data_get_string(settings, CPAL_KEY));
	pthread_mutex_unlock(&g->mutex);
}

static void *glitch_create(obs_data_t *settings, obs_source_t *source)
{
	struct glitch_info *g = bzalloc(sizeof(*g));
	if (!tc_base_init(&g->base, source, "glitch_transition.effect", glitch_params, TC_COUNT(glitch_params))) {
		bfree(g);
		return NULL;
	}
	pthread_mutex_init(&g->mutex, NULL);
	glitch_update(g, settings);
	return g;
}

static void glitch_destroy(void *data)
{
	struct glitch_info *g = data;
	tc_base_free(&g->base);
	pthread_mutex_destroy(&g->mutex);
	bfree(g);
}

static void glitch_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	struct glitch_info *g = data;
	struct tc_frame frame;
	struct tc_color tint;
	struct vec2 canvas;
	uint32_t pal[CPAL_SLOTS];

	pthread_mutex_lock(&g->mutex);
	tc_color_from_rgba(&tint, cpal_get(g->palette, pal) ? pal[1] | 0xFF000000 : g->tint_rgba);
	tc_frame_begin(&frame);
	tc_set_texture(&frame, g->ep_a_tex, a);
	tc_set_texture(&frame, g->ep_b_tex, b);
	tc_set_color(&frame, g->ep_tint_color, &tint);
	vec2_set(&canvas, (float)cx, (float)cy);
	gs_effect_set_vec2(g->ep_canvas, &canvas);
	gs_effect_set_float(g->ep_progress, t);
	gs_effect_set_float(g->ep_style, g->style);
	gs_effect_set_float(g->ep_intensity, g->intensity);
	gs_effect_set_float(g->ep_block, g->block);
	gs_effect_set_float(g->ep_sort_threshold, g->threshold);
	gs_effect_set_float(g->ep_sort_length, g->length);
	gs_effect_set_float(g->ep_sort_vertical, g->vertical);
	gs_effect_set_float(g->ep_split, g->split);
	gs_effect_set_float(g->ep_tint, g->tint);
	gs_effect_set_float(g->ep_seed, g->seed);
	pthread_mutex_unlock(&g->mutex);

	while (gs_effect_loop(g->base.effect, "Glitch"))
		gs_draw_sprite(NULL, 0, cx, cy);
	tc_frame_end(&frame);
}

static void glitch_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct glitch_info *g = data;
	obs_transition_video_render(g->base.source, glitch_callback);
}

static obs_properties_t *glitch_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;
#define T(k) obs_module_text("Glitch." k)

	p = obs_properties_add_list(props, "style", T("Style"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Style.Both"), 0);
	obs_property_list_add_int(p, T("Style.Sort"), 1);
	obs_property_list_add_int(p, T("Style.Glitch"), 2);
	p = obs_properties_add_float_slider(props, "intensity", T("Intensity"), 0.0, 200.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, "block", T("Block"), 4.0, 200.0, 1.0);
	obs_property_float_set_suffix(p, " px");
	p = obs_properties_add_list(props, "sort_direction", T("SortDirection"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("SortDirection.Horizontal"), 0);
	obs_property_list_add_int(p, T("SortDirection.Vertical"), 1);
	p = obs_properties_add_float_slider(props, "sort_threshold", T("SortThreshold"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	obs_property_set_long_description(p, T("SortThreshold.Description"));
	p = obs_properties_add_float_slider(props, "sort_length", T("SortLength"), 1.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, "split", T("Split"), 0.0, 60.0, 0.5);
	obs_property_float_set_suffix(p, " px");
	p = obs_properties_add_float_slider(props, "tint", T("Tint"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	obs_properties_add_color(props, "tint_color", T("TintColor"));
	p = cpal_add_list(props);
	obs_property_set_long_description(p, T("Palette.Description"));
	obs_properties_add_int(props, "seed", T("Seed"), 0, 99999, 1);
#undef T
	return props;
}

static void glitch_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "style", 0);
	obs_data_set_default_double(settings, "intensity", 100.0);
	obs_data_set_default_double(settings, "block", 40.0);
	obs_data_set_default_double(settings, "sort_threshold", 35.0);
	obs_data_set_default_double(settings, "sort_length", 30.0);
	obs_data_set_default_double(settings, "split", 12.0);
	obs_data_set_default_double(settings, "tint", 35.0);
	obs_data_set_default_int(settings, "tint_color", 0xFFFF2EE6); /* #E62EFF */
	obs_data_set_default_int(settings, "seed", 1);
}

struct obs_source_info glitch_transition = {
	.id = "constellations_glitch_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = glitch_get_name,
	.create = glitch_create,
	.destroy = glitch_destroy,
	.update = glitch_update,
	.video_render = glitch_video_render,
	.audio_render = tc_audio_render,
	.get_properties = glitch_properties,
	.get_defaults = glitch_defaults,
	.video_get_color_space = tc_video_get_color_space,
};
