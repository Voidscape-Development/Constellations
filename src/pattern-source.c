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

static const char *cpat_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.PatternSource.Name");
}

static void *cpat_source_create(obs_data_t *settings, obs_source_t *source)
{
	return cpat_renderer_create(source, CPAT_HOST_SOURCE, settings);
}

static void cpat_source_destroy(void *data)
{
	cpat_renderer_destroy(data);
}

static void cpat_source_update(void *data, obs_data_t *settings)
{
	cpat_renderer_update(data, settings);
}

static void cpat_source_load(void *data, obs_data_t *settings)
{
	cpat_renderer_load(data, settings);
}

static void cpat_source_tick(void *data, float seconds)
{
	cpat_renderer_tick(data, seconds);
}

static void cpat_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cpat_renderer *r = data;
	uint32_t w = cpat_renderer_width(r);
	uint32_t h = cpat_renderer_height(r);
	cpat_renderer_render_background(r, w, h);
	cpat_renderer_render_items(r, w, h);
}

static uint32_t cpat_source_width(void *data)
{
	return cpat_renderer_width(data);
}

static uint32_t cpat_source_height(void *data)
{
	return cpat_renderer_height(data);
}

static void cpat_source_show(void *data)
{
	cpat_renderer_show(data, true);
}

static void cpat_source_hide(void *data)
{
	cpat_renderer_show(data, false);
}

static void cpat_source_activate(void *data)
{
	cpat_renderer_activate(data, true);
}

static void cpat_source_deactivate(void *data)
{
	cpat_renderer_activate(data, false);
}

static obs_properties_t *cpat_source_props(void *data)
{
	obs_properties_t *props = obs_properties_create();
	cpat_renderer_add_properties(data, props, CPAT_HOST_SOURCE);
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
	.load = cpat_source_load,
	.video_tick = cpat_source_tick,
	.video_render = cpat_source_render,
	.get_width = cpat_source_width,
	.get_height = cpat_source_height,
	.show = cpat_source_show,
	.hide = cpat_source_hide,
	.activate = cpat_source_activate,
	.deactivate = cpat_source_deactivate,
	.get_properties = cpat_source_props,
	.get_defaults = cpat_source_defaults,
};

void constellations_register_pattern_source(void)
{
	obs_register_source(&cpat_source_info);
}
