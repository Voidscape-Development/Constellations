/*
Constellations - Shared helpers
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "common.h"

#include <plugin-support.h>
#include <util/darray.h>
#include <util/dstr.h>
#include <stdlib.h>

bool cfx_effect_load(struct cfx_effect *fx, const char *file, const char *const *names, size_t count)
{
	memset(fx, 0, sizeof(*fx));
	fx->count = count;
	fx->params = bzalloc(sizeof(gs_eparam_t *) * (count ? count : 1));

	struct dstr rel = {0};
	dstr_printf(&rel, "effects/%s", file);
	char *path = obs_module_file(rel.array);
	dstr_free(&rel);
	if (!path) {
		obs_log(LOG_ERROR, "%s not found", file);
		return false;
	}

	obs_enter_graphics();
	char *errors = NULL;
	fx->effect = gs_effect_create_from_file(path, &errors);
	if (fx->effect) {
		for (size_t i = 0; i < count; ++i) {
			fx->params[i] = gs_effect_get_param_by_name(fx->effect, names[i]);
			if (!fx->params[i])
				obs_log(LOG_WARNING,
					"%s has no uniform '%s'; the installed data files are older than the "
					"plugin binary. Reinstall the plugin to update them.",
					file, names[i]);
		}
	}
	obs_leave_graphics();

	if (!fx->effect)
		obs_log(LOG_ERROR, "failed to compile %s: %s", file, errors ? errors : "(no detail)");
	bfree(errors);
	bfree(path);
	return fx->effect != NULL;
}

void cfx_effect_free(struct cfx_effect *fx)
{
	if (fx->effect) {
		obs_enter_graphics();
		gs_effect_destroy(fx->effect);
		obs_leave_graphics();
	}
	bfree(fx->params);
	memset(fx, 0, sizeof(*fx));
}

void cfx_draw_begin(struct cfx_draw_state *st)
{
	st->prev_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
}

void cfx_draw_end(const struct cfx_draw_state *st)
{
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(st->prev_srgb);
}

void cfx_color_linear(struct vec4 *out, uint32_t rgba)
{
	vec4_from_rgba_srgb(out, rgba);
}

enum gs_color_space cfx_filter_color_space(obs_source_t *filter)
{
	static const enum gs_color_space spaces[] = {GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED};
	obs_source_t *target = obs_filter_get_target(filter);
	if (!target)
		return GS_CS_SRGB;
	return obs_source_get_color_space(target, OBS_COUNTOF(spaces), spaces);
}

bool cfx_filter_begin(obs_source_t *filter, enum obs_allow_direct_render allow_direct)
{
	const enum gs_color_space space = cfx_filter_color_space(filter);
	return obs_source_process_filter_begin_with_color_space(filter, gs_get_format_from_space(space), space,
								allow_direct);
}

bool cfx_set_visible(obs_properties_t *props, const char *name, bool visible)
{
	obs_property_t *p = obs_properties_get(props, name);
	if (!p || obs_property_visible(p) == visible)
		return false;
	obs_property_set_visible(p, visible);
	return true;
}

struct source_entry {
	const char *name;
	const char *uuid;
};

struct enum_ctx {
	DARRAY(struct source_entry) entries;
	obs_source_t *exclude_a;
	obs_source_t *exclude_b;
};

static bool collect_source(void *data, obs_source_t *src)
{
	struct enum_ctx *ctx = data;
	if (src == ctx->exclude_a || src == ctx->exclude_b)
		return true;
	if ((obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO) == 0)
		return true;
	if (obs_source_get_type(src) == OBS_SOURCE_TYPE_TRANSITION)
		return true;
	struct source_entry e = {obs_source_get_name(src), obs_source_get_uuid(src)};
	if (e.name && e.uuid)
		da_push_back(ctx->entries, &e);
	return true;
}

static int compare_entries(const void *a, const void *b)
{
	const struct source_entry *ea = a;
	const struct source_entry *eb = b;
	return astrcmpi(ea->name, eb->name);
}

void cfx_fill_source_list(obs_property_t *list, obs_source_t *exclude_a, obs_source_t *exclude_b)
{
	obs_property_list_clear(list);
	obs_property_list_add_string(list, obs_module_text("Constellations.Source.None"), "");
	struct enum_ctx ctx = {0};
	ctx.exclude_a = exclude_a;
	ctx.exclude_b = exclude_b;
	da_init(ctx.entries);
	obs_enum_sources(collect_source, &ctx);
	if (ctx.entries.num)
		qsort(ctx.entries.array, ctx.entries.num, sizeof(struct source_entry), compare_entries);
	for (size_t i = 0; i < ctx.entries.num; ++i)
		obs_property_list_add_string(list, ctx.entries.array[i].name, ctx.entries.array[i].uuid);
	da_free(ctx.entries);
}

struct contains_ctx {
	obs_source_t *needle;
	bool found;
};

static void contains_cb(obs_source_t *parent, obs_source_t *child, void *param)
{
	UNUSED_PARAMETER(parent);
	struct contains_ctx *ctx = param;
	if (child == ctx->needle)
		ctx->found = true;
}

bool cfx_source_contains(obs_source_t *source, obs_source_t *ancestor)
{
	if (!source || !ancestor)
		return false;
	if (source == ancestor)
		return true;
	struct contains_ctx ctx = {ancestor, false};
	obs_source_enum_full_tree(source, contains_cb, &ctx);
	return ctx.found;
}
