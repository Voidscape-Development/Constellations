/*
Constellations - Liquid Ripple transition
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* The look is in data/effects/ripple_transition.effect; this file turns
 * settings into uniforms and runs the front. */

#include <obs-module.h>
#include <stdio.h>
#include <util/threading.h>
#include <math.h>

#include "ink-math.h"
#include "palette.h"
#include "transition-common.h"

struct ripple_info {
	struct transition_base base;

	gs_eparam_t *ep_a_tex, *ep_b_tex, *ep_canvas, *ep_centre, *ep_front, *ep_env, *ep_wavelength, *ep_amplitude,
		*ep_damping, *ep_soft, *ep_highlight, *ep_highlight_color;

	pthread_mutex_t mutex;
	int easing;
	float origin_x, origin_y, wavelength, amplitude, damping, soft, highlight;
	uint32_t highlight_rgba;
	char palette[64];
};

static const struct tc_param ripple_params[] = {
	TC_PARAM(struct ripple_info, ep_a_tex, "a_tex"),
	TC_PARAM(struct ripple_info, ep_b_tex, "b_tex"),
	TC_PARAM(struct ripple_info, ep_canvas, "canvas"),
	TC_PARAM(struct ripple_info, ep_centre, "centre"),
	TC_PARAM(struct ripple_info, ep_front, "front"),
	TC_PARAM(struct ripple_info, ep_env, "env"),
	TC_PARAM(struct ripple_info, ep_wavelength, "wavelength"),
	TC_PARAM(struct ripple_info, ep_amplitude, "amplitude"),
	TC_PARAM(struct ripple_info, ep_damping, "damping"),
	TC_PARAM(struct ripple_info, ep_soft, "soft"),
	TC_PARAM(struct ripple_info, ep_highlight, "highlight"),
	TC_PARAM(struct ripple_info, ep_highlight_color, "highlight_color"),
};

static const char *ripple_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Ripple.Transition");
}

static void ripple_update(void *data, obs_data_t *settings)
{
	struct ripple_info *r = data;
	pthread_mutex_lock(&r->mutex);
	r->easing = (int)obs_data_get_int(settings, "easing");
	r->origin_x = (float)obs_data_get_double(settings, "origin_x") / 100.0f;
	r->origin_y = (float)obs_data_get_double(settings, "origin_y") / 100.0f;
	r->wavelength = fmaxf((float)obs_data_get_double(settings, "wavelength"), 4.0f);
	r->amplitude = (float)obs_data_get_double(settings, "amplitude");
	r->damping = fmaxf((float)obs_data_get_double(settings, "damping"), 1.0f);
	r->soft = fmaxf((float)obs_data_get_double(settings, "soft"), 1.0f);
	r->highlight = (float)obs_data_get_double(settings, "highlight") / 100.0f;
	r->highlight_rgba = (uint32_t)obs_data_get_int(settings, "highlight_color") | 0xFF000000;
	snprintf(r->palette, sizeof(r->palette), "%s", obs_data_get_string(settings, CPAL_KEY));
	pthread_mutex_unlock(&r->mutex);
}

static void *ripple_create(obs_data_t *settings, obs_source_t *source)
{
	struct ripple_info *r = bzalloc(sizeof(*r));
	if (!tc_base_init(&r->base, source, "ripple_transition.effect", ripple_params, TC_COUNT(ripple_params))) {
		bfree(r);
		return NULL;
	}
	pthread_mutex_init(&r->mutex, NULL);
	ripple_update(r, settings);
	return r;
}

static void ripple_destroy(void *data)
{
	struct ripple_info *r = data;
	tc_base_free(&r->base);
	pthread_mutex_destroy(&r->mutex);
	bfree(r);
}

static void ripple_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	struct ripple_info *r = data;
	float w = (float)cx, h = (float)cy;
	struct tc_frame frame;
	struct tc_color hl;
	struct vec2 v;
	uint32_t pal[CPAL_SLOTS];

	pthread_mutex_lock(&r->mutex);
	float x = r->origin_x * w, y = r->origin_y * h;
	float dx = fmaxf(fabsf(x), fabsf(w - x)), dy = fmaxf(fabsf(y), fabsf(h - y));
	/* from where nothing has changed to where everything has */
	float front = -r->soft + (sqrtf(dx * dx + dy * dy) + 2.0f * r->soft) * ink_ease(r->easing, t);
	tc_color_from_rgba(&hl, cpal_get(r->palette, pal) ? pal[1] | 0xFF000000 : r->highlight_rgba);
	tc_frame_begin(&frame);
	tc_set_texture(&frame, r->ep_a_tex, a);
	tc_set_texture(&frame, r->ep_b_tex, b);
	tc_set_color(&frame, r->ep_highlight_color, &hl);
	vec2_set(&v, w, h);
	gs_effect_set_vec2(r->ep_canvas, &v);
	vec2_set(&v, x, y);
	gs_effect_set_vec2(r->ep_centre, &v);
	gs_effect_set_float(r->ep_front, front);
	gs_effect_set_float(r->ep_env, 4.0f * t * (1.0f - t));
	gs_effect_set_float(r->ep_wavelength, r->wavelength);
	gs_effect_set_float(r->ep_amplitude, r->amplitude);
	gs_effect_set_float(r->ep_damping, r->damping);
	gs_effect_set_float(r->ep_soft, r->soft);
	gs_effect_set_float(r->ep_highlight, r->highlight);
	pthread_mutex_unlock(&r->mutex);

	while (gs_effect_loop(r->base.effect, "Ripple"))
		gs_draw_sprite(NULL, 0, cx, cy);
	tc_frame_end(&frame);
}

static void ripple_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct ripple_info *r = data;
	obs_transition_video_render(r->base.source, ripple_callback);
}

static obs_properties_t *ripple_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;
#define T(k) obs_module_text("Ripple." k)
#define SLIDER(key, name, lo, hi, step, suffix)                                                \
	p = obs_properties_add_float_slider(props, key, T(name), lo, hi, step); \
	obs_property_float_set_suffix(p, suffix)

	SLIDER("origin_x", "OriginX", -50.0, 150.0, 0.5, " %");
	SLIDER("origin_y", "OriginY", -50.0, 150.0, 0.5, " %");
	p = obs_properties_add_list(props, "easing", obs_module_text("Tiles.Easing"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.Linear"), 0);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.InOut"), 1);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.Out"), 2);
	SLIDER("wavelength", "Wavelength", 8.0, 400.0, 1.0, " px");
	SLIDER("amplitude", "Amplitude", 0.0, 120.0, 0.5, " px");
	obs_property_set_long_description(p, T("Amplitude.Description"));
	SLIDER("damping", "Damping", 10.0, 1000.0, 1.0, " px");
	obs_property_set_long_description(p, T("Damping.Description"));
	SLIDER("soft", "Soft", 1.0, 200.0, 1.0, " px");
	SLIDER("highlight", "Highlight", 0.0, 100.0, 1.0, " %");
	obs_properties_add_color(props, "highlight_color", T("HighlightColor"));
	p = cpal_add_list(props);
	obs_property_set_long_description(p, T("Palette.Description"));
#undef SLIDER
#undef T
	return props;
}

static void ripple_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "origin_x", 50.0);
	obs_data_set_default_double(settings, "origin_y", 50.0);
	obs_data_set_default_int(settings, "easing", 2);
	obs_data_set_default_double(settings, "wavelength", 60.0);
	obs_data_set_default_double(settings, "amplitude", 18.0);
	obs_data_set_default_double(settings, "damping", 160.0);
	obs_data_set_default_double(settings, "soft", 24.0);
	obs_data_set_default_double(settings, "highlight", 50.0);
	obs_data_set_default_int(settings, "highlight_color", 0xFFFFFFFF);
}

struct obs_source_info ripple_transition = {
	.id = "constellations_ripple_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = ripple_get_name,
	.create = ripple_create,
	.destroy = ripple_destroy,
	.update = ripple_update,
	.video_render = ripple_video_render,
	.audio_render = tc_audio_render,
	.get_properties = ripple_properties,
	.get_defaults = ripple_defaults,
	.video_get_color_space = tc_video_get_color_space,
};
