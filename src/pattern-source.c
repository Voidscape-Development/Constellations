/*
Constellations - Pattern Background Source
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

struct cpat_source {
	obs_source_t *self;
	struct cpat_renderer r;
};

static const char *cpat_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.PatternSource.Name");
}

static void *cpat_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct cpat_source *s = bzalloc(sizeof(*s));
	s->self = source;
	cpat_renderer_init(&s->r, source, CPAT_HOST_SOURCE);
	cpat_renderer_update(&s->r, settings);
	return s;
}

static void cpat_source_destroy(void *data)
{
	struct cpat_source *s = data;
	cpat_renderer_free(&s->r);
	bfree(s);
}

static void cpat_source_update(void *data, obs_data_t *settings)
{
	struct cpat_source *s = data;
	cpat_renderer_update(&s->r, settings);
}

static void cpat_source_tick(void *data, float seconds)
{
	struct cpat_source *s = data;
	cpat_renderer_tick(&s->r, seconds);
}

static void cpat_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cpat_source *s = data;
	cpat_renderer_render_background(&s->r, s->r.width, s->r.height);
	cpat_renderer_render_items(&s->r, s->r.width, s->r.height);
}

static uint32_t cpat_source_width(void *data)
{
	struct cpat_source *s = data;
	return s->r.width;
}

static uint32_t cpat_source_height(void *data)
{
	struct cpat_source *s = data;
	return s->r.height;
}

static obs_properties_t *cpat_source_props(void *data)
{
	struct cpat_source *s = data;
	obs_properties_t *props = obs_properties_create();
	cpat_renderer_get_properties(s ? &s->r : NULL, props, CPAT_HOST_SOURCE);
	return props;
}

static void cpat_source_defaults(obs_data_t *settings)
{
	cpat_renderer_set_defaults(settings, CPAT_HOST_SOURCE);
}

static struct obs_source_info cpat_source_info = {
	.id = "constellations_pattern_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = cpat_source_get_name,
	.create = cpat_source_create,
	.destroy = cpat_source_destroy,
	.update = cpat_source_update,
	.video_tick = cpat_source_tick,
	.video_render = cpat_source_render,
	.get_width = cpat_source_width,
	.get_height = cpat_source_height,
	.get_properties = cpat_source_props,
	.get_defaults = cpat_source_defaults,
};

void constellations_register_pattern_source(void)
{
	obs_register_source(&cpat_source_info);
}
