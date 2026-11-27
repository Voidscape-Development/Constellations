/*
Constellations - Black Hole transition
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* The look is in data/effects/blackhole_transition.effect; this file turns
 * settings into uniforms and grows the horizon. */

#include <obs-module.h>
#include <stdio.h>
#include <util/threading.h>
#include <math.h>

#include "palette.h"
#include "transition-common.h"

struct blackhole_info {
	struct transition_base base;

	gs_eparam_t *ep_a_tex, *ep_b_tex, *ep_canvas, *ep_centre, *ep_horizon, *ep_feather, *ep_strength, *ep_lens,
		*ep_swirl, *ep_glow, *ep_stage, *ep_glow_color, *ep_horizon_color;

	pthread_mutex_t mutex;
	float origin_x, origin_y, lens, swirl, glow;
	uint32_t glow_rgba, horizon_rgba;
	char palette[64];
};

static const struct tc_param blackhole_params[] = {
	TC_PARAM(struct blackhole_info, ep_a_tex, "a_tex"),
	TC_PARAM(struct blackhole_info, ep_b_tex, "b_tex"),
	TC_PARAM(struct blackhole_info, ep_canvas, "canvas"),
	TC_PARAM(struct blackhole_info, ep_centre, "centre"),
	TC_PARAM(struct blackhole_info, ep_horizon, "horizon"),
	TC_PARAM(struct blackhole_info, ep_feather, "feather"),
	TC_PARAM(struct blackhole_info, ep_strength, "strength"),
	TC_PARAM(struct blackhole_info, ep_lens, "lens"),
	TC_PARAM(struct blackhole_info, ep_swirl, "swirl"),
	TC_PARAM(struct blackhole_info, ep_glow, "glow"),
	TC_PARAM(struct blackhole_info, ep_stage, "stage"),
	TC_PARAM(struct blackhole_info, ep_glow_color, "glow_color"),
	TC_PARAM(struct blackhole_info, ep_horizon_color, "horizon_color"),
};

static const char *blackhole_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("BlackHole.Transition");
}

static void blackhole_update(void *data, obs_data_t *settings)
{
	struct blackhole_info *bh = data;
	pthread_mutex_lock(&bh->mutex);
	bh->origin_x = (float)obs_data_get_double(settings, "origin_x") / 100.0f;
	bh->origin_y = (float)obs_data_get_double(settings, "origin_y") / 100.0f;
	bh->lens = (float)obs_data_get_double(settings, "lens") / 100.0f;
	bh->swirl = (float)obs_data_get_double(settings, "swirl") * 6.2831853f;
	bh->glow = (float)obs_data_get_double(settings, "glow") / 100.0f;
	bh->glow_rgba = (uint32_t)obs_data_get_int(settings, "glow_color") | 0xFF000000;
	bh->horizon_rgba = (uint32_t)obs_data_get_int(settings, "horizon_color") | 0xFF000000;
	snprintf(bh->palette, sizeof(bh->palette), "%s", obs_data_get_string(settings, CPAL_KEY));
	pthread_mutex_unlock(&bh->mutex);
}

static void *blackhole_create(obs_data_t *settings, obs_source_t *source)
{
	struct blackhole_info *bh = bzalloc(sizeof(*bh));
	if (!tc_base_init(&bh->base, source, "blackhole_transition.effect", blackhole_params,
			  TC_COUNT(blackhole_params))) {
		bfree(bh);
		return NULL;
	}
	pthread_mutex_init(&bh->mutex, NULL);
	blackhole_update(bh, settings);
	return bh;
}

static void blackhole_destroy(void *data)
{
	struct blackhole_info *bh = data;
	tc_base_free(&bh->base);
	pthread_mutex_destroy(&bh->mutex);
	bfree(bh);
}

static void blackhole_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	struct blackhole_info *bh = data;
	float w = (float)cx, h = (float)cy;
	struct tc_frame frame;
	struct tc_color glow, hz;
	struct vec2 v;
	uint32_t pal[CPAL_SLOTS];

	pthread_mutex_lock(&bh->mutex);
	float x = bh->origin_x * w, y = bh->origin_y * h;
	float dx = fmaxf(fabsf(x), fabsf(w - x)), dy = fmaxf(fabsf(y), fabsf(h - y));
	float reach = sqrtf(dx * dx + dy * dy), feather = 0.03f * reach + 1.0f;
	/* in to the singularity and back out: zero at both ends, and at the
	 * midpoint (where the scenes swap) a horizon past every corner */
	float s = t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t, k = s * s;
	tc_color_from_rgba(&glow, cpal_get(bh->palette, pal) ? pal[1] | 0xFF000000 : bh->glow_rgba);
	tc_color_from_rgba(&hz, bh->horizon_rgba);
	tc_frame_begin(&frame);
	tc_set_texture(&frame, bh->ep_a_tex, a);
	tc_set_texture(&frame, bh->ep_b_tex, b);
	tc_set_color(&frame, bh->ep_glow_color, &glow);
	tc_set_color(&frame, bh->ep_horizon_color, &hz);
	vec2_set(&v, w, h);
	gs_effect_set_vec2(bh->ep_canvas, &v);
	vec2_set(&v, x, y);
	gs_effect_set_vec2(bh->ep_centre, &v);
	gs_effect_set_float(bh->ep_horizon, k * (reach + feather + 1.0f));
	gs_effect_set_float(bh->ep_feather, feather);
	gs_effect_set_float(bh->ep_strength, k);
	gs_effect_set_float(bh->ep_lens, bh->lens);
	gs_effect_set_float(bh->ep_swirl, bh->swirl);
	gs_effect_set_float(bh->ep_glow, bh->glow);
	gs_effect_set_float(bh->ep_stage, t < 0.5f ? 0.0f : 1.0f);
	pthread_mutex_unlock(&bh->mutex);

	while (gs_effect_loop(bh->base.effect, "BlackHole"))
		gs_draw_sprite(NULL, 0, cx, cy);
	tc_frame_end(&frame);
}

static void blackhole_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct blackhole_info *bh = data;
	obs_transition_video_render(bh->base.source, blackhole_callback);
}

static obs_properties_t *blackhole_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;
#define T(k) obs_module_text("BlackHole." k)
#define SLIDER(key, name, lo, hi, step, suffix)                                \
	p = obs_properties_add_float_slider(props, key, T(name), lo, hi, step); \
	obs_property_float_set_suffix(p, suffix)

	SLIDER("origin_x", "OriginX", -50.0, 150.0, 0.5, " %");
	SLIDER("origin_y", "OriginY", -50.0, 150.0, 0.5, " %");
	SLIDER("lens", "Lens", 0.0, 300.0, 1.0, " %");
	obs_property_set_long_description(p, T("Lens.Description"));
	SLIDER("swirl", "Swirl", -3.0, 3.0, 0.05, "");
	obs_property_set_long_description(p, T("Swirl.Description"));
	SLIDER("glow", "Glow", 0.0, 300.0, 1.0, " %");
	obs_properties_add_color(props, "glow_color", T("GlowColor"));
	obs_properties_add_color(props, "horizon_color", T("HorizonColor"));
	p = cpal_add_list(props);
	obs_property_set_long_description(p, T("Palette.Description"));
#undef SLIDER
#undef T
	return props;
}

static void blackhole_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "origin_x", 50.0);
	obs_data_set_default_double(settings, "origin_y", 50.0);
	obs_data_set_default_double(settings, "lens", 120.0);
	obs_data_set_default_double(settings, "swirl", 0.75);
	obs_data_set_default_double(settings, "glow", 100.0);
	obs_data_set_default_int(settings, "glow_color", 0xFF3A9BFF); /* #FF9B3A */
	obs_data_set_default_int(settings, "horizon_color", 0xFF000000);
}

struct obs_source_info blackhole_transition = {
	.id = "constellations_blackhole_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = blackhole_get_name,
	.create = blackhole_create,
	.destroy = blackhole_destroy,
	.update = blackhole_update,
	.video_render = blackhole_video_render,
	.audio_render = tc_audio_render,
	.get_properties = blackhole_properties,
	.get_defaults = blackhole_defaults,
	.video_get_color_space = tc_video_get_color_space,
};
