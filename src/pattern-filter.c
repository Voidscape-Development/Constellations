/*
Constellations - Pattern Filter
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>

#include "shape-defs.h"
#include "pattern-renderer.h"

struct cpat_filter {
	obs_source_t *self;
	struct cpat_renderer r;
};

static const char *cpat_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.PatternFilter.Name");
}

static void *cpat_filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct cpat_filter *f = bzalloc(sizeof(*f));
	f->self = source;
	cpat_renderer_init(&f->r, source, CPAT_HOST_FILTER);
	cpat_renderer_update(&f->r, settings);
	return f;
}

static void cpat_filter_destroy(void *data)
{
	struct cpat_filter *f = data;
	cpat_renderer_free(&f->r);
	bfree(f);
}

static void cpat_filter_update(void *data, obs_data_t *settings)
{
	struct cpat_filter *f = data;
	cpat_renderer_update(&f->r, settings);
}

static void cpat_filter_tick(void *data, float seconds)
{
	struct cpat_filter *f = data;
	cpat_renderer_tick(&f->r, seconds);
}

static void cpat_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cpat_filter *f = data;

	obs_source_t *target = obs_filter_get_target(f->self);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(f->self);
		return;
	}

	if (!obs_source_process_filter_begin(f->self, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;
	obs_source_process_filter_end(f->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), w, h);

	cpat_renderer_render_background(&f->r, w, h);
	cpat_renderer_render_items(&f->r, w, h);
}

static obs_properties_t *cpat_filter_props(void *data)
{
	struct cpat_filter *f = data;
	obs_properties_t *props = obs_properties_create();
	cpat_renderer_get_properties(f ? &f->r : NULL, props, CPAT_HOST_FILTER);
	return props;
}

static void cpat_filter_defaults(obs_data_t *settings)
{
	cpat_renderer_set_defaults(settings, CPAT_HOST_FILTER);
}

static struct obs_source_info cpat_filter_info = {
	.id = "constellations_pattern_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = cpat_filter_get_name,
	.create = cpat_filter_create,
	.destroy = cpat_filter_destroy,
	.update = cpat_filter_update,
	.video_tick = cpat_filter_tick,
	.video_render = cpat_filter_render,
	.get_properties = cpat_filter_props,
	.get_defaults = cpat_filter_defaults,
};

void constellations_register_pattern_filter(void)
{
	obs_register_source(&cpat_filter_info);
}
