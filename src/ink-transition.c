/*
Constellations - Ink Bleed transition
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/*
 * Ink spreads from drops to carry the outgoing scene away. The look is in
 * data/effects/ink_transition.effect; the schedule that keeps the first and
 * last frames exact is in src/ink-math.h. This file turns settings into
 * uniforms.
 */

#include <obs-module.h>
#include <stdio.h>
#include <util/threading.h>
#include <math.h>

#include "common.h"
#include "ink-math.h"
#include "palette.h"
#include "transition-common.h"

struct ink_info {
	struct transition_base base;

	gs_eparam_t *ep_a_tex, *ep_b_tex, *ep_canvas, *ep_drops[4], *ep_drop_count, *ep_scale, *ep_noise_offset,
		*ep_tendril_px, *ep_roughness, *ep_front, *ep_edge, *ep_aa, *ep_stage, *ep_wet, *ep_ink_color;

	pthread_mutex_t mutex;
	int drops, easing;
	bool flood;
	float origin_x, origin_y, rough, tendril_px, edge_px, wet;
	uint32_t seed, ink_rgba;
	char palette[64];
};

static const struct tc_param ink_params[] = {
	TC_PARAM(struct ink_info, ep_a_tex, "a_tex"),
	TC_PARAM(struct ink_info, ep_b_tex, "b_tex"),
	TC_PARAM(struct ink_info, ep_canvas, "canvas"),
	TC_PARAM(struct ink_info, ep_drops[0], "drops01"),
	TC_PARAM(struct ink_info, ep_drops[1], "drops23"),
	TC_PARAM(struct ink_info, ep_drops[2], "drops45"),
	TC_PARAM(struct ink_info, ep_drops[3], "drops67"),
	TC_PARAM(struct ink_info, ep_drop_count, "drop_count"),
	TC_PARAM(struct ink_info, ep_scale, "scale"),
	TC_PARAM(struct ink_info, ep_noise_offset, "noise_offset"),
	TC_PARAM(struct ink_info, ep_tendril_px, "tendril_px"),
	TC_PARAM(struct ink_info, ep_roughness, "roughness"),
	TC_PARAM(struct ink_info, ep_front, "front"),
	TC_PARAM(struct ink_info, ep_edge, "edge"),
	TC_PARAM(struct ink_info, ep_aa, "aa"),
	TC_PARAM(struct ink_info, ep_stage, "stage"),
	TC_PARAM(struct ink_info, ep_wet, "wet"),
	TC_PARAM(struct ink_info, ep_ink_color, "ink_color"),
};

static const char *ink_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Ink.Transition");
}

static void ink_update(void *data, obs_data_t *settings)
{
	struct ink_info *ink = data;
	pthread_mutex_lock(&ink->mutex);
	ink->drops = (int)obs_data_get_int(settings, "drops");
	ink->easing = (int)obs_data_get_int(settings, "easing");
	ink->flood = obs_data_get_bool(settings, "flood");
	ink->origin_x = (float)obs_data_get_double(settings, "origin_x") / 100.0f;
	ink->origin_y = (float)obs_data_get_double(settings, "origin_y") / 100.0f;
	ink->rough = (float)obs_data_get_double(settings, "roughness") / 100.0f;
	ink->tendril_px = (float)obs_data_get_double(settings, "tendril_px");
	ink->edge_px = fmaxf((float)obs_data_get_double(settings, "edge"), 1.0f);
	ink->wet = (float)obs_data_get_double(settings, "wet") / 100.0f;
	ink->seed = (uint32_t)obs_data_get_int(settings, "seed");
	ink->ink_rgba = (uint32_t)obs_data_get_int(settings, "ink_color") | 0xFF000000;
	snprintf(ink->palette, sizeof(ink->palette), "%s", obs_data_get_string(settings, CPAL_KEY));
	pthread_mutex_unlock(&ink->mutex);
}

static void *ink_create(obs_data_t *settings, obs_source_t *source)
{
	struct ink_info *ink = bzalloc(sizeof(*ink));
	if (!tc_base_init(&ink->base, source, "ink_transition.effect", ink_params, TC_COUNT(ink_params))) {
		bfree(ink);
		return NULL;
	}
	pthread_mutex_init(&ink->mutex, NULL);
	ink_update(ink, settings);
	return ink;
}

static void ink_destroy(void *data)
{
	struct ink_info *ink = data;
	tc_base_free(&ink->base);
	pthread_mutex_destroy(&ink->mutex);
	bfree(ink);
}

/* A reproducible 0..1 from the seed and an index. */
static float seeded(uint32_t seed, uint32_t i)
{
	uint32_t h = seed * 747796405u + i * 2891336453u + 1u;
	h = ((h >> ((h >> 28) + 4)) ^ h) * 277803737u;
	return (float)((h >> 22) ^ h) / 4294967296.0f;
}

static void ink_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	struct ink_info *ink = data;
	float w = (float)cx, h = (float)cy, xy[INK_MAX_DROPS * 2] = {0};

	pthread_mutex_lock(&ink->mutex);
	int count = ink->drops < 1 ? 1 : (ink->drops > INK_MAX_DROPS ? INK_MAX_DROPS : ink->drops);
	xy[0] = ink->origin_x * w;
	xy[1] = ink->origin_y * h;
	for (int i = 1; i < count; ++i) {
		xy[2 * i] = (0.1f + 0.8f * seeded(ink->seed, 2u * i)) * w;
		xy[2 * i + 1] = (0.1f + 0.8f * seeded(ink->seed, 2u * i + 1u)) * h;
	}
	uint32_t pal[CPAL_SLOTS], ink_rgba = ink->ink_rgba;
	if (cpal_get(ink->palette, pal))
		ink_rgba = pal[1] | 0xFF000000;
	float reach = ink_reach(xy, count, w, h);
	float edge = ink->edge_px / reach, aa = 1.5f / reach;
	float stage = 0.0f, e = t;
	if (ink->flood) {
		stage = t < 0.5f ? 1.0f : 2.0f;
		e = t < 0.5f ? t * 2.0f : t * 2.0f - 1.0f;
	}
	float front = ink_front(ink_ease(ink->easing, e), ink->rough, edge, aa);

	struct tc_frame frame;
	struct tc_color color;
	struct vec2 v;
	tc_frame_begin(&frame);
	tc_set_texture(&frame, ink->ep_a_tex, a);
	tc_set_texture(&frame, ink->ep_b_tex, b);
	tc_color_from_rgba(&color, ink_rgba);
	tc_set_color(&frame, ink->ep_ink_color, &color);
	for (int i = 0; i < 4; ++i) {
		struct vec4 d;
		vec4_set(&d, xy[4 * i], xy[4 * i + 1], xy[4 * i + 2], xy[4 * i + 3]);
		gs_effect_set_vec4(ink->ep_drops[i], &d);
	}
	vec2_set(&v, w, h);
	gs_effect_set_vec2(ink->ep_canvas, &v);
	vec2_set(&v, seeded(ink->seed, 97u) * 512.0f, seeded(ink->seed, 98u) * 512.0f);
	gs_effect_set_vec2(ink->ep_noise_offset, &v);
	gs_effect_set_float(ink->ep_drop_count, (float)count);
	gs_effect_set_float(ink->ep_scale, 1.0f / reach);
	gs_effect_set_float(ink->ep_tendril_px, ink->tendril_px);
	gs_effect_set_float(ink->ep_roughness, ink->rough);
	gs_effect_set_float(ink->ep_front, front);
	gs_effect_set_float(ink->ep_edge, edge);
	gs_effect_set_float(ink->ep_aa, aa);
	gs_effect_set_float(ink->ep_stage, stage);
	gs_effect_set_float(ink->ep_wet, ink->wet);
	pthread_mutex_unlock(&ink->mutex);

	while (gs_effect_loop(ink->base.effect, "Ink"))
		gs_draw_sprite(NULL, 0, cx, cy);
	tc_frame_end(&frame);
}

static void ink_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct ink_info *ink = data;
	obs_transition_video_render(ink->base.source, ink_callback);
}

static obs_properties_t *ink_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;
#define T(k) obs_module_text("Ink." k)

	obs_properties_add_int_slider(props, "drops", T("Drops"), 1, INK_MAX_DROPS, 1);
	p = obs_properties_add_float_slider(props, "origin_x", T("OriginX"), -50.0, 150.0, 0.5);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, "origin_y", T("OriginY"), -50.0, 150.0, 0.5);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_int(props, "seed", T("Seed"), 0, 99999, 1);
	obs_property_set_long_description(p, T("Seed.Description"));
	p = obs_properties_add_bool(props, "flood", T("Flood"));
	obs_property_set_long_description(p, T("Flood.Description"));
	p = obs_properties_add_list(props, "easing", obs_module_text("Tiles.Easing"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.Linear"), 0);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.InOut"), 1);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.Out"), 2);
	p = obs_properties_add_float_slider(props, "roughness", T("Roughness"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, "tendril_px", T("TendrilScale"), 20.0, 800.0, 1.0);
	obs_property_float_set_suffix(p, " px");
	p = obs_properties_add_float_slider(props, "edge", T("Edge"), 1.0, 300.0, 1.0);
	obs_property_float_set_suffix(p, " px");
	p = obs_properties_add_float_slider(props, "wet", T("Wet"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	obs_property_set_long_description(p, T("Wet.Description"));
	obs_properties_add_color(props, "ink_color", T("Color"));
	p = cpal_add_list(props);
	obs_property_set_long_description(p, T("Palette.Description"));
#undef T
	return props;
}

static void ink_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "drops", 1);
	obs_data_set_default_double(settings, "origin_x", 50.0);
	obs_data_set_default_double(settings, "origin_y", 50.0);
	obs_data_set_default_int(settings, "seed", 7);
	obs_data_set_default_int(settings, "easing", 1);
	obs_data_set_default_double(settings, "roughness", 60.0);
	obs_data_set_default_double(settings, "tendril_px", 180.0);
	obs_data_set_default_double(settings, "edge", 60.0);
	obs_data_set_default_double(settings, "wet", 60.0);
	obs_data_set_default_int(settings, "ink_color", 0xFF3A1A14); /* #141A3A, indigo ink */
}

struct obs_source_info ink_transition = {
	.id = "constellations_ink_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = ink_get_name,
	.create = ink_create,
	.destroy = ink_destroy,
	.update = ink_update,
	.video_render = ink_video_render,
	.audio_render = tc_audio_render,
	.get_properties = ink_properties,
	.get_defaults = ink_defaults,
	.video_get_color_space = tc_video_get_color_space,
};
