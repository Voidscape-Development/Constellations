/*
Constellations - Shared pattern renderer
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "pattern-renderer.h"
#include "svg-image.h"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Gold (#D4AF37) in OBS's 0xAABBGGRR color layout. */
#define CPAT_GOLD 0xFF37AFD4

static void cpat_item_release_source(struct cpat_item *it)
{
	if (it->source_ref) {
		obs_weak_source_release(it->source_ref);
		it->source_ref = NULL;
	}
	if (it->source_texrender) {
		obs_enter_graphics();
		gs_texrender_destroy(it->source_texrender);
		obs_leave_graphics();
		it->source_texrender = NULL;
	}
}

static void cpat_item_release_image(struct cpat_item *it)
{
	if (it->svg) {
		if (it->svg_texture) {
			obs_enter_graphics();
			gs_texture_destroy(it->svg_texture);
			obs_leave_graphics();
			it->svg_texture = NULL;
		}
		it->svg_px = 0;
		cpat_svg_free(it->svg);
		it->svg = NULL;
		it->image_loaded = false;
	} else if (it->image_loaded) {
		obs_enter_graphics();
		gs_image_file_free(&it->image);
		obs_leave_graphics();
		it->image_loaded = false;
	}
}

/* Rasterizes the item's SVG at its current size. The pixel size is rounded
 * up to a multiple of 32 so dragging the size slider only re-rasterizes
 * every few steps instead of on every tick. */
static void cpat_item_refresh_svg(struct cpat_item *it)
{
	if (!it->svg)
		return;
	uint32_t px = ((uint32_t)ceilf(it->size) + 31u) & ~31u;
	if (px < 32)
		px = 32;
	if (px > 2048)
		px = 2048;
	if (px == it->svg_px && it->svg_texture)
		return;
	uint8_t *data = cpat_svg_rasterize(it->svg, px);
	if (!data)
		return;
	const uint8_t *planes[] = {data};
	obs_enter_graphics();
	if (it->svg_texture)
		gs_texture_destroy(it->svg_texture);
	it->svg_texture = gs_texture_create(px, px, GS_RGBA, 1, planes, 0);
	obs_leave_graphics();
	bfree(data);
	it->svg_px = it->svg_texture ? px : 0;
}

static gs_texture_t *cpat_item_image_texture(struct cpat_item *it)
{
	if (!it->image_loaded)
		return NULL;
	return it->svg ? it->svg_texture : it->image.texture;
}

static void cpat_item_acquire_image(struct cpat_item *it)
{
	cpat_item_release_image(it);
	if (!it->image_path || !*it->image_path)
		return;
	if (cpat_svg_is_svg_path(it->image_path)) {
		it->svg = cpat_svg_load(it->image_path);
		it->image_loaded = it->svg != NULL;
		cpat_item_refresh_svg(it);
		return;
	}
	gs_image_file_init(&it->image, it->image_path);
	obs_enter_graphics();
	gs_image_file_init_texture(&it->image);
	obs_leave_graphics();
	it->image_loaded = it->image.loaded;
}

static void cpat_item_acquire_source(struct cpat_item *it, obs_source_t *exclude)
{
	cpat_item_release_source(it);
	if (!it->source_name || !*it->source_name)
		return;
	obs_source_t *src = obs_get_source_by_name(it->source_name);
	if (!src)
		return;
	if (src == exclude) {
		obs_source_release(src);
		return;
	}
	it->source_ref = obs_source_get_weak_source(src);
	obs_source_release(src);

	obs_enter_graphics();
	it->source_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();
}

static void cpat_item_clear(struct cpat_item *it)
{
	cpat_item_release_image(it);
	cpat_item_release_source(it);
	bfree(it->image_path);
	bfree(it->source_name);
	memset(it, 0, sizeof(*it));
}

void cpat_renderer_init(struct cpat_renderer *r, obs_source_t *owner)
{
	memset(r, 0, sizeof(*r));
	r->owner = owner;

	char *path = obs_module_file("effects/pattern.effect");
	if (!path) {
		obs_log(LOG_ERROR, "Constellations: pattern.effect not found");
		return;
	}
	obs_enter_graphics();
	char *errors = NULL;
	r->effect = gs_effect_create_from_file(path, &errors);
	obs_leave_graphics();
	if (!r->effect)
		obs_log(LOG_ERROR, "Constellations: failed to compile pattern.effect: %s",
			errors ? errors : "(no detail)");
	bfree(errors);
	bfree(path);
}

void cpat_renderer_free(struct cpat_renderer *r)
{
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i)
		cpat_item_clear(&r->items[i]);
	if (r->effect) {
		obs_enter_graphics();
		gs_effect_destroy(r->effect);
		obs_leave_graphics();
		r->effect = NULL;
	}
}

void cpat_renderer_set_defaults(obs_data_t *settings, bool include_canvas)
{
	if (include_canvas) {
		obs_data_set_default_int(settings, "width", 1920);
		obs_data_set_default_int(settings, "height", 1080);
		/* Charcoal (#1C1C1F); OBS stores colors as 0xAABBGGRR. */
		obs_data_set_default_int(settings, "background_color", 0xFF1F1C1C);
	} else {
		obs_data_set_default_int(settings, "background_color", 0x00000000);
	}
	obs_data_set_default_double(settings, "anchor_x_pct", 50.0);
	obs_data_set_default_double(settings, "anchor_y_pct", 50.0);
	obs_data_set_default_double(settings, "canvas_rotation", 0.0);
	obs_data_set_default_int(settings, "item_count", 1);
	obs_data_set_default_int(settings, "layout_mode", CLAYOUT_LAYERED);
	obs_data_set_default_int(settings, "grid_order", CGRID_RANDOM);

	/* The standalone source starts as slowly drifting, twinkling gold stars
	 * on charcoal; the filter keeps plain white dots over the video. */
	const int default_shape = include_canvas ? CSHAPE_STAR : CSHAPE_CIRCLE;
	const long long default_color = include_canvas ? CPAT_GOLD : 0xFFFFFFFF;

	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i) {
		char key[64];
#define DK(name) (snprintf(key, sizeof(key), "item_%d_%s", i + 1, name), key)
		obs_data_set_default_bool(settings, DK("enabled"), i == 0);
		obs_data_set_default_int(settings, DK("kind"), CITEM_SHAPE);
		obs_data_set_default_int(settings, DK("shape"), default_shape);
		obs_data_set_default_int(settings, DK("polygon_sides"), 6);
		obs_data_set_default_double(settings, DK("stroke_thickness"), 0.2);
		obs_data_set_default_bool(settings, DK("outline_only"), false);
		obs_data_set_default_int(settings, DK("color"), default_color);
		obs_data_set_default_string(settings, DK("image_path"), "");
		obs_data_set_default_int(settings, DK("image_color_mode"), CIMGCOLOR_ORIGINAL);
		obs_data_set_default_int(settings, DK("image_tint"), CPAT_GOLD);
		obs_data_set_default_double(settings, DK("image_opacity"), 100.0);
		obs_data_set_default_string(settings, DK("source_name"), "");
		obs_data_set_default_double(settings, DK("size"), 24.0);
		obs_data_set_default_double(settings, DK("rotation"), 0.0);
		obs_data_set_default_double(settings, DK("offset_x"), 0.0);
		obs_data_set_default_double(settings, DK("offset_y"), 0.0);
		obs_data_set_default_double(settings, DK("density"), 2.0);
		obs_data_set_default_int(settings, DK("twinkle_mode"), CTWINKLE_GLOBAL);
		obs_data_set_default_double(settings, DK("twinkle_amount"), 0.5);
		obs_data_set_default_double(settings, DK("twinkle_speed"), 1.0);
		obs_data_set_default_int(settings, DK("speed_drift_mode"), CSDRIFT_GLOBAL);
		obs_data_set_default_double(settings, DK("speed_drift_amount"), 0.25);
		obs_data_set_default_int(settings, DK("autorot_mode"), CAROT_GLOBAL);
		obs_data_set_default_double(settings, DK("autorot_speed"), 45.0);
		obs_data_set_default_int(settings, DK("autorot_style"), CAROT_STYLE_RANDOM);
		obs_data_set_default_int(settings, DK("autorot_seed"), 0);
#undef DK
	}

	obs_data_set_default_double(settings, "motion_angle", include_canvas ? 315.0 : 0.0);
	obs_data_set_default_double(settings, "motion_speed", include_canvas ? 12.0 : 0.0);
	obs_data_set_default_bool(settings, "alternating_lines", false);
	obs_data_set_default_bool(settings, "speed_drift", false);
	obs_data_set_default_double(settings, "speed_drift_amount", 0.25);
	obs_data_set_default_bool(settings, "location_drift", false);
	obs_data_set_default_double(settings, "location_drift_amount", 0.15);

	obs_data_set_default_bool(settings, "twinkle_enabled", include_canvas);
	obs_data_set_default_double(settings, "twinkle_amount", include_canvas ? 0.3 : 0.5);
	obs_data_set_default_double(settings, "twinkle_speed", include_canvas ? 0.5 : 1.0);

	obs_data_set_default_bool(settings, "autorot_enabled", false);
	obs_data_set_default_double(settings, "autorot_speed", 45.0);
	obs_data_set_default_int(settings, "autorot_style", CAROT_STYLE_RANDOM);
	obs_data_set_default_int(settings, "autorot_seed", 0);

	obs_data_set_default_bool(settings, "vignette_enabled", false);
	obs_data_set_default_double(settings, "vignette_size", 0.7);
	obs_data_set_default_double(settings, "vignette_anchor_x_pct", 50.0);
	obs_data_set_default_double(settings, "vignette_anchor_y_pct", 50.0);
	obs_data_set_default_double(settings, "vignette_direction", 0.0);
	obs_data_set_default_int(settings, "vignette_shape", CVIGN_CIRCLE);
	obs_data_set_default_int(settings, "vignette_polygon_sides", 6);
	obs_data_set_default_double(settings, "vignette_softness", 0.35);
	obs_data_set_default_bool(settings, "vignette_inverted", false);
}

struct enum_src_ctx {
	obs_property_t *list;
	DARRAY(const char *) names;
	obs_source_t *self;
};

static bool enum_collect_source(void *data, obs_source_t *src)
{
	struct enum_src_ctx *ctx = data;
	if (src == ctx->self)
		return true;
	uint32_t flags = obs_source_get_output_flags(src);
	if ((flags & OBS_SOURCE_VIDEO) == 0)
		return true;
	enum obs_source_type type = obs_source_get_type(src);
	if (type == OBS_SOURCE_TYPE_TRANSITION)
		return true;
	const char *name = obs_source_get_name(src);
	if (!name)
		return true;
	da_push_back(ctx->names, &name);
	return true;
}

static int compare_strs_ci(const void *a, const void *b)
{
	const char *const *sa = a;
	const char *const *sb = b;
	return astrcmpi(*sa, *sb);
}

static void fill_source_dropdown(obs_property_t *list, obs_source_t *self_source)
{
	obs_property_list_clear(list);
	obs_property_list_add_string(list, obs_module_text("Constellations.Source.None"), "");
	struct enum_src_ctx ctx = {0};
	ctx.list = list;
	ctx.self = self_source;
	da_init(ctx.names);
	obs_enum_sources(enum_collect_source, &ctx);
	qsort(ctx.names.array, ctx.names.num, sizeof(char *), compare_strs_ci);
	for (size_t i = 0; i < ctx.names.num; ++i)
		obs_property_list_add_string(list, ctx.names.array[i], ctx.names.array[i]);
	da_free(ctx.names);
}

static int item_index_from_prop(obs_property_t *p)
{
	const char *name = obs_property_name(p);
	if (!name)
		return -1;
	int n = 0;
	if (sscanf(name, "item_%d_", &n) != 1)
		return -1;
	return n - 1;
}

static void apply_item_kind_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	char key[64];
	snprintf(key, sizeof(key), "item_%d_kind", i + 1);
	int kind = (int)obs_data_get_int(settings, key);
	snprintf(key, sizeof(key), "item_%d_shape", i + 1);
	int shape_v = (int)obs_data_get_int(settings, key);
	snprintf(key, sizeof(key), "item_%d_outline_only", i + 1);
	bool outline = obs_data_get_bool(settings, key);

#define VIS(name_, on)                                                \
	do {                                                          \
		snprintf(key, sizeof(key), "item_%d_%s", i + 1, name_); \
		obs_property_t *_pp = obs_properties_get(props, key); \
		if (_pp)                                              \
			obs_property_set_visible(_pp, on);            \
	} while (0)

	VIS("shape", kind == CITEM_SHAPE);
	VIS("outline_only", kind == CITEM_SHAPE);
	VIS("polygon_sides", kind == CITEM_SHAPE && shape_v == CSHAPE_POLYGON);
	VIS("stroke_thickness", kind == CITEM_SHAPE && (shape_v == CSHAPE_CROSS || outline));
	VIS("color", kind == CITEM_SHAPE);
	VIS("image_path", kind == CITEM_IMAGE);
	VIS("source_name", kind == CITEM_SOURCE);
	bool textured = kind == CITEM_IMAGE || kind == CITEM_SOURCE;
	snprintf(key, sizeof(key), "item_%d_image_color_mode", i + 1);
	bool tinted = (int)obs_data_get_int(settings, key) != CIMGCOLOR_ORIGINAL;
	VIS("image_color_mode", textured);
	VIS("image_tint", textured && tinted);
	VIS("image_opacity", textured);
#undef VIS
}

static void apply_item_twinkle_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	char key[64];
	snprintf(key, sizeof(key), "item_%d_twinkle_mode", i + 1);
	bool custom = (int)obs_data_get_int(settings, key) == CTWINKLE_CUSTOM;
	snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
	bool en = obs_data_get_bool(settings, key);
	const char *names[] = {"twinkle_amount", "twinkle_speed"};
	for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); ++k) {
		snprintf(key, sizeof(key), "item_%d_%s", i + 1, names[k]);
		obs_property_t *pp = obs_properties_get(props, key);
		if (pp)
			obs_property_set_visible(pp, en && custom);
	}
}

static void apply_item_speed_drift_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	char key[64];
	snprintf(key, sizeof(key), "item_%d_speed_drift_mode", i + 1);
	bool custom = (int)obs_data_get_int(settings, key) == CSDRIFT_CUSTOM;
	snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
	bool en = obs_data_get_bool(settings, key);
	snprintf(key, sizeof(key), "item_%d_speed_drift_amount", i + 1);
	obs_property_t *pp = obs_properties_get(props, key);
	if (pp)
		obs_property_set_visible(pp, en && custom);
}

static void apply_item_autorot_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	char key[64];
	snprintf(key, sizeof(key), "item_%d_autorot_mode", i + 1);
	bool custom = (int)obs_data_get_int(settings, key) == CAROT_CUSTOM;
	snprintf(key, sizeof(key), "item_%d_autorot_style", i + 1);
	bool unison = (int)obs_data_get_int(settings, key) == CAROT_STYLE_UNISON;
	snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
	bool en = obs_data_get_bool(settings, key);
	obs_property_t *pp;
	snprintf(key, sizeof(key), "item_%d_autorot_style", i + 1);
	pp = obs_properties_get(props, key);
	if (pp)
		obs_property_set_visible(pp, en && custom);
	snprintf(key, sizeof(key), "item_%d_autorot_speed", i + 1);
	pp = obs_properties_get(props, key);
	if (pp)
		obs_property_set_visible(pp, en && custom && unison);
	snprintf(key, sizeof(key), "item_%d_autorot_seed", i + 1);
	pp = obs_properties_get(props, key);
	if (pp)
		obs_property_set_visible(pp, en && custom);
}

static void apply_item_enabled_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	char key[64];
	snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
	bool en = obs_data_get_bool(settings, key);
	const char *names[] = {
		"kind",          "shape",        "outline_only",     "polygon_sides",    "stroke_thickness",
		"color",         "image_path",   "source_name",      "image_color_mode", "image_tint",
		"image_opacity", "size",         "rotation",         "offset_x",         "offset_y",
		"density",       "twinkle_mode", "speed_drift_mode", "autorot_mode"};
	for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); ++k) {
		snprintf(key, sizeof(key), "item_%d_%s", i + 1, names[k]);
		obs_property_t *pp = obs_properties_get(props, key);
		if (pp)
			obs_property_set_visible(pp, en);
	}
	if (en)
		apply_item_kind_visibility(props, settings, i);
	apply_item_twinkle_visibility(props, settings, i);
	apply_item_speed_drift_visibility(props, settings, i);
	apply_item_autorot_visibility(props, settings, i);
}

static void apply_item_count_visibility(obs_properties_t *props, obs_data_t *settings)
{
	int count = (int)obs_data_get_int(settings, "item_count");
	if (count < 1)
		count = 1;
	if (count > CONSTELLATIONS_MAX_ITEMS)
		count = CONSTELLATIONS_MAX_ITEMS;
	char gid[64];
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i) {
		snprintf(gid, sizeof(gid), "item_%d_group", i + 1);
		obs_property_t *gp = obs_properties_get(props, gid);
		if (gp)
			obs_property_set_visible(gp, i < count);
	}
}

static void apply_density_visibility(obs_properties_t *props, obs_data_t *settings)
{
	int mode = (int)obs_data_get_int(settings, "layout_mode");
	char key[64];
	int first_enabled = -1;
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; i++) {
		snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
		if (obs_data_get_bool(settings, key)) {
			first_enabled = i;
			break;
		}
	}
	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; i++) {
		snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
		bool en = obs_data_get_bool(settings, key);
		snprintf(key, sizeof(key), "item_%d_density", i + 1);
		obs_property_t *dp = obs_properties_get(props, key);
		if (!dp)
			continue;
		bool show = en;
		if (mode == CLAYOUT_STEP_REPEAT || mode == CLAYOUT_GRID)
			show = en && (i == first_enabled);
		obs_property_set_visible(dp, show);
	}
}

static void apply_motion_visibility(obs_properties_t *props, obs_data_t *settings)
{
	/* Never gate visibility on motion_speed: toggling rows while the user
	 * drags the slider through zero makes the whole dialog jitter. */
	bool sd = obs_data_get_bool(settings, "speed_drift");
	bool ld = obs_data_get_bool(settings, "location_drift");
	obs_property_t *pp;
	pp = obs_properties_get(props, "speed_drift_amount");
	if (pp)
		obs_property_set_visible(pp, sd);
	pp = obs_properties_get(props, "location_drift_amount");
	if (pp)
		obs_property_set_visible(pp, ld);
}

static void apply_vignette_visibility(obs_properties_t *props, obs_data_t *settings)
{
	bool en = obs_data_get_bool(settings, "vignette_enabled");
	int shape = (int)obs_data_get_int(settings, "vignette_shape");
	const char *names[] = {"vignette_size",      "vignette_anchor_x_pct", "vignette_anchor_y_pct",
			       "vignette_direction", "vignette_shape",        "vignette_polygon_sides",
			       "vignette_softness",  "vignette_inverted"};
	for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
		obs_property_t *pp = obs_properties_get(props, names[k]);
		if (pp)
			obs_property_set_visible(pp, en);
	}
	if (en) {
		obs_property_t *pp = obs_properties_get(props, "vignette_polygon_sides");
		if (pp)
			obs_property_set_visible(pp, shape == CVIGN_POLYGON);
		pp = obs_properties_get(props, "vignette_direction");
		if (pp)
			obs_property_set_visible(pp, shape != CVIGN_CIRCLE);
	}
}

static bool item_count_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_item_count_visibility(props, settings);
	apply_density_visibility(props, settings);
	return true;
}

static bool layout_mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_density_visibility(props, settings);
	int mode = (int)obs_data_get_int(settings, "layout_mode");
	obs_property_t *go = obs_properties_get(props, "grid_order");
	if (go)
		obs_property_set_visible(go, mode == CLAYOUT_GRID);
	return true;
}

static bool motion_reset_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct cpat_renderer *r = data;
	r->motion_off_along = 0.0;
	r->motion_off_perp = 0.0;
	return false;
}

static bool twinkle_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool en = obs_data_get_bool(settings, "twinkle_enabled");
	obs_property_t *pp;
	pp = obs_properties_get(props, "twinkle_amount");
	if (pp)
		obs_property_set_visible(pp, en);
	pp = obs_properties_get(props, "twinkle_speed");
	if (pp)
		obs_property_set_visible(pp, en);
	return true;
}

static bool autorot_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool en = obs_data_get_bool(settings, "autorot_enabled");
	bool unison = (int)obs_data_get_int(settings, "autorot_style") == CAROT_STYLE_UNISON;
	obs_property_t *pp;
	pp = obs_properties_get(props, "autorot_style");
	if (pp)
		obs_property_set_visible(pp, en);
	pp = obs_properties_get(props, "autorot_speed");
	if (pp)
		obs_property_set_visible(pp, en && unison);
	pp = obs_properties_get(props, "autorot_seed");
	if (pp)
		obs_property_set_visible(pp, en);
	return true;
}

static bool motion_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_motion_visibility(props, settings);
	return true;
}

static bool vignette_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	apply_vignette_visibility(props, settings);
	return true;
}

static bool item_kind_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	apply_item_kind_visibility(props, settings, i);
	return true;
}

static bool item_twinkle_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	apply_item_twinkle_visibility(props, settings, i);
	return true;
}

static bool item_speed_drift_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	apply_item_speed_drift_visibility(props, settings, i);
	return true;
}

static bool item_autorot_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	apply_item_autorot_visibility(props, settings, i);
	return true;
}

static bool item_enabled_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	apply_item_enabled_visibility(props, settings, i);
	apply_density_visibility(props, settings);
	return true;
}

static void add_item_group(obs_properties_t *root, struct cpat_renderer *r, int i)
{
	char key[64];
	char label[128];

	snprintf(label, sizeof(label), "%s %d", obs_module_text("Constellations.Item"), i + 1);
	obs_properties_t *grp = obs_properties_create();

	snprintf(key, sizeof(key), "item_%d_enabled", i + 1);
	obs_property_t *en = obs_properties_add_bool(grp, key, obs_module_text("Constellations.Item.Enabled"));
	obs_property_set_modified_callback(en, item_enabled_modified);

	snprintf(key, sizeof(key), "item_%d_kind", i + 1);
	obs_property_t *kind = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.Kind"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(kind, obs_module_text("Constellations.Item.Kind.Shape"), CITEM_SHAPE);
	obs_property_list_add_int(kind, obs_module_text("Constellations.Item.Kind.Image"), CITEM_IMAGE);
	obs_property_list_add_int(kind, obs_module_text("Constellations.Item.Kind.Source"), CITEM_SOURCE);
	obs_property_set_modified_callback(kind, item_kind_modified);

	snprintf(key, sizeof(key), "item_%d_shape", i + 1);
	obs_property_t *sh = obs_properties_add_list(grp, key, obs_module_text("Constellations.Shape"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Circle"), CSHAPE_CIRCLE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Cross"), CSHAPE_CROSS);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Square"), CSHAPE_SQUARE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Polygon"), CSHAPE_POLYGON);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Squircle"), CSHAPE_SQUIRCLE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.HLine"), CSHAPE_HLINE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.VLine"), CSHAPE_VLINE);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Star"), CSHAPE_STAR);
	obs_property_list_add_int(sh, obs_module_text("Constellations.Shape.Heart"), CSHAPE_HEART);
	obs_property_set_modified_callback(sh, item_kind_modified);

	snprintf(key, sizeof(key), "item_%d_outline_only", i + 1);
	obs_property_t *ol = obs_properties_add_bool(grp, key, obs_module_text("Constellations.Item.OutlineOnly"));
	obs_property_set_modified_callback(ol, item_kind_modified);

	snprintf(key, sizeof(key), "item_%d_polygon_sides", i + 1);
	obs_properties_add_int_slider(grp, key, obs_module_text("Constellations.Shape.PolySides"), 3, 24, 1);
	snprintf(key, sizeof(key), "item_%d_stroke_thickness", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Shape.StrokeThickness"), 0.02, 1.0,
					0.01);
	snprintf(key, sizeof(key), "item_%d_color", i + 1);
	obs_properties_add_color_alpha(grp, key, obs_module_text("Constellations.Shape.Color"));

	snprintf(key, sizeof(key), "item_%d_image_path", i + 1);
	obs_properties_add_path(grp, key, obs_module_text("Constellations.Item.ImagePath"), OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.bmp *.tga *.gif *.svg);;SVG (*.svg);;All Files (*.*)",
				NULL);

	snprintf(key, sizeof(key), "item_%d_source_name", i + 1);
	obs_property_t *src = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.Source"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	fill_source_dropdown(src, r ? r->owner : NULL);

	snprintf(key, sizeof(key), "item_%d_image_color_mode", i + 1);
	obs_property_t *icm = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.ImageColor"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(icm, obs_module_text("Constellations.Item.ImageColor.Original"), CIMGCOLOR_ORIGINAL);
	obs_property_list_add_int(icm, obs_module_text("Constellations.Item.ImageColor.Tint"), CIMGCOLOR_TINT);
	obs_property_list_add_int(icm, obs_module_text("Constellations.Item.ImageColor.Recolor"), CIMGCOLOR_RECOLOR);
	obs_property_set_long_description(icm, obs_module_text("Constellations.Item.ImageColor.Tooltip"));
	obs_property_set_modified_callback(icm, item_kind_modified);
	snprintf(key, sizeof(key), "item_%d_image_tint", i + 1);
	obs_properties_add_color(grp, key, obs_module_text("Constellations.Item.ImageTint"));
	snprintf(key, sizeof(key), "item_%d_image_opacity", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.ImageOpacity"), 0.0, 100.0, 1.0);

	snprintf(key, sizeof(key), "item_%d_size", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.Size"), 1.0, 1024.0, 1.0);
	snprintf(key, sizeof(key), "item_%d_rotation", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.Rotation"), 0.0, 360.0, 1.0);
	snprintf(key, sizeof(key), "item_%d_offset_x", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.OffsetX"), -1000.0, 1000.0, 1.0);
	snprintf(key, sizeof(key), "item_%d_offset_y", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.OffsetY"), -1000.0, 1000.0, 1.0);
	snprintf(key, sizeof(key), "item_%d_density", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.Density"), 0.1, 20.0, 0.1);

	snprintf(key, sizeof(key), "item_%d_twinkle_mode", i + 1);
	obs_property_t *twm = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.Twinkle"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(twm, obs_module_text("Constellations.Item.Twinkle.Global"), CTWINKLE_GLOBAL);
	obs_property_list_add_int(twm, obs_module_text("Constellations.Item.Twinkle.Custom"), CTWINKLE_CUSTOM);
	obs_property_list_add_int(twm, obs_module_text("Constellations.Item.Twinkle.Off"), CTWINKLE_OFF);
	obs_property_set_modified_callback(twm, item_twinkle_modified);
	snprintf(key, sizeof(key), "item_%d_twinkle_amount", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.TwinkleAmount"), 0.0, 1.0, 0.01);
	snprintf(key, sizeof(key), "item_%d_twinkle_speed", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.TwinkleSpeed"), 0.05, 5.0, 0.05);

	snprintf(key, sizeof(key), "item_%d_speed_drift_mode", i + 1);
	obs_property_t *sdm = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.SpeedDrift"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(sdm, obs_module_text("Constellations.Item.SpeedDrift.Global"), CSDRIFT_GLOBAL);
	obs_property_list_add_int(sdm, obs_module_text("Constellations.Item.SpeedDrift.Custom"), CSDRIFT_CUSTOM);
	obs_property_list_add_int(sdm, obs_module_text("Constellations.Item.SpeedDrift.Off"), CSDRIFT_OFF);
	obs_property_set_modified_callback(sdm, item_speed_drift_modified);
	snprintf(key, sizeof(key), "item_%d_speed_drift_amount", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.SpeedDriftAmount"), 0.0, 1.0,
					0.01);

	snprintf(key, sizeof(key), "item_%d_autorot_mode", i + 1);
	obs_property_t *arm = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.AutoRot"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(arm, obs_module_text("Constellations.Item.AutoRot.Global"), CAROT_GLOBAL);
	obs_property_list_add_int(arm, obs_module_text("Constellations.Item.AutoRot.Custom"), CAROT_CUSTOM);
	obs_property_list_add_int(arm, obs_module_text("Constellations.Item.AutoRot.Off"), CAROT_OFF);
	obs_property_set_modified_callback(arm, item_autorot_modified);
	snprintf(key, sizeof(key), "item_%d_autorot_style", i + 1);
	obs_property_t *ars = obs_properties_add_list(grp, key, obs_module_text("Constellations.Item.AutoRotStyle"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(ars, obs_module_text("Constellations.AutoRot.Style.Random"), CAROT_STYLE_RANDOM);
	obs_property_list_add_int(ars, obs_module_text("Constellations.AutoRot.Style.Unison"), CAROT_STYLE_UNISON);
	obs_property_set_modified_callback(ars, item_autorot_modified);
	snprintf(key, sizeof(key), "item_%d_autorot_speed", i + 1);
	obs_properties_add_float_slider(grp, key, obs_module_text("Constellations.Item.AutoRotSpeed"), -360.0, 360.0,
					1.0);
	snprintf(key, sizeof(key), "item_%d_autorot_seed", i + 1);
	obs_properties_add_int_slider(grp, key, obs_module_text("Constellations.Item.AutoRotSeed"), 0, 1000, 1);

	char gid[64];
	snprintf(gid, sizeof(gid), "item_%d_group", i + 1);
	obs_properties_add_group(root, gid, label, OBS_GROUP_NORMAL, grp);
}

void cpat_renderer_get_properties(struct cpat_renderer *r, obs_properties_t *props, bool include_canvas)
{
	if (include_canvas) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_color_alpha(canvas, "background_color",
					       obs_module_text("Constellations.Canvas.BackgroundColor"));
		obs_properties_add_float_slider(canvas, "anchor_x_pct", obs_module_text("Constellations.Anchor.X"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(canvas, "anchor_y_pct", obs_module_text("Constellations.Anchor.Y"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(canvas, "canvas_rotation",
						obs_module_text("Constellations.Canvas.Rotation"), 0.0, 360.0, 1.0);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	} else {
		obs_properties_add_color_alpha(props, "background_color",
					       obs_module_text("Constellations.Canvas.BackgroundColor"));
		obs_properties_add_float_slider(props, "anchor_x_pct", obs_module_text("Constellations.Anchor.X"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(props, "anchor_y_pct", obs_module_text("Constellations.Anchor.Y"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(props, "canvas_rotation",
						obs_module_text("Constellations.Canvas.Rotation"), 0.0, 360.0, 1.0);
	}

	obs_property_t *ic = obs_properties_add_int_slider(
		props, "item_count", obs_module_text("Constellations.ItemCount"), 1, CONSTELLATIONS_MAX_ITEMS, 1);
	obs_property_set_modified_callback(ic, item_count_modified);

	obs_property_t *lm = obs_properties_add_list(props, "layout_mode", obs_module_text("Constellations.Layout"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(lm, obs_module_text("Constellations.Layout.Layered"), CLAYOUT_LAYERED);
	obs_property_list_add_int(lm, obs_module_text("Constellations.Layout.StepRepeat"), CLAYOUT_STEP_REPEAT);
	obs_property_list_add_int(lm, obs_module_text("Constellations.Layout.Grid"), CLAYOUT_GRID);
	obs_property_set_modified_callback(lm, layout_mode_modified);

	obs_property_t *go = obs_properties_add_list(props, "grid_order",
						     obs_module_text("Constellations.Layout.GridOrder"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(go, obs_module_text("Constellations.Layout.GridOrder.Random"), CGRID_RANDOM);
	obs_property_list_add_int(go, obs_module_text("Constellations.Layout.GridOrder.Ordered"), CGRID_ORDERED);

	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i)
		add_item_group(props, r, i);

	obs_properties_t *motion = obs_properties_create();
	obs_properties_add_float_slider(motion, "motion_angle", obs_module_text("Constellations.Motion.Angle"), 0.0,
					360.0, 1.0);
	obs_properties_add_float_slider(motion, "motion_speed", obs_module_text("Constellations.Motion.Speed"), -500.0,
					500.0, 1.0);
	obs_properties_add_button2(motion, "motion_reset", obs_module_text("Constellations.Motion.Reset"),
				   motion_reset_clicked, r);
	obs_properties_add_bool(motion, "alternating_lines", obs_module_text("Constellations.Motion.Alternating"));
	obs_property_t *sd =
		obs_properties_add_bool(motion, "speed_drift", obs_module_text("Constellations.Motion.SpeedDrift"));
	obs_property_set_modified_callback(sd, motion_modified);
	obs_properties_add_float_slider(motion, "speed_drift_amount",
					obs_module_text("Constellations.Motion.SpeedDriftAmount"), 0.0, 1.0, 0.01);
	obs_property_t *ld = obs_properties_add_bool(motion, "location_drift",
						     obs_module_text("Constellations.Motion.LocationDrift"));
	obs_property_set_modified_callback(ld, motion_modified);
	obs_properties_add_float_slider(motion, "location_drift_amount",
					obs_module_text("Constellations.Motion.LocationDriftAmount"), 0.0, 1.0, 0.01);
	obs_properties_add_group(props, "motion_group", obs_module_text("Constellations.Group.Motion"),
				 OBS_GROUP_NORMAL, motion);

	obs_properties_t *tw = obs_properties_create();
	obs_property_t *te =
		obs_properties_add_bool(tw, "twinkle_enabled", obs_module_text("Constellations.Twinkle.Enabled"));
	obs_property_set_modified_callback(te, twinkle_modified);
	obs_properties_add_float_slider(tw, "twinkle_amount", obs_module_text("Constellations.Twinkle.Amount"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(tw, "twinkle_speed", obs_module_text("Constellations.Twinkle.Speed"), 0.05, 5.0,
					0.05);
	obs_properties_add_group(props, "twinkle_group", obs_module_text("Constellations.Group.Twinkle"),
				 OBS_GROUP_NORMAL, tw);

	obs_properties_t *ar = obs_properties_create();
	obs_property_t *are =
		obs_properties_add_bool(ar, "autorot_enabled", obs_module_text("Constellations.AutoRot.Enabled"));
	obs_property_set_modified_callback(are, autorot_modified);
	obs_property_t *arst = obs_properties_add_list(ar, "autorot_style",
						       obs_module_text("Constellations.AutoRot.Style"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(arst, obs_module_text("Constellations.AutoRot.Style.Random"), CAROT_STYLE_RANDOM);
	obs_property_list_add_int(arst, obs_module_text("Constellations.AutoRot.Style.Unison"), CAROT_STYLE_UNISON);
	obs_property_set_modified_callback(arst, autorot_modified);
	obs_properties_add_float_slider(ar, "autorot_speed", obs_module_text("Constellations.AutoRot.Speed"), -360.0,
					360.0, 1.0);
	obs_properties_add_int_slider(ar, "autorot_seed", obs_module_text("Constellations.AutoRot.Seed"), 0, 1000, 1);
	obs_properties_add_group(props, "autorot_group", obs_module_text("Constellations.Group.AutoRotation"),
				 OBS_GROUP_NORMAL, ar);

	obs_properties_t *vg = obs_properties_create();
	obs_property_t *ve =
		obs_properties_add_bool(vg, "vignette_enabled", obs_module_text("Constellations.Vignette.Enabled"));
	obs_property_set_modified_callback(ve, vignette_modified);
	obs_properties_add_float_slider(vg, "vignette_size", obs_module_text("Constellations.Vignette.Size"), 0.01, 2.0,
					0.01);
	obs_properties_add_float_slider(vg, "vignette_anchor_x_pct", obs_module_text("Constellations.Vignette.AnchorX"),
					0.0, 100.0, 0.5);
	obs_properties_add_float_slider(vg, "vignette_anchor_y_pct", obs_module_text("Constellations.Vignette.AnchorY"),
					0.0, 100.0, 0.5);
	obs_properties_add_float_slider(vg, "vignette_direction", obs_module_text("Constellations.Vignette.Direction"),
					0.0, 360.0, 1.0);
	obs_property_t *vs = obs_properties_add_list(vg, "vignette_shape",
						     obs_module_text("Constellations.Vignette.Shape"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Circle"), CVIGN_CIRCLE);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Linear"), CVIGN_LINEAR);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Square"), CVIGN_SQUARE);
	obs_property_list_add_int(vs, obs_module_text("Constellations.Vignette.Shape.Polygon"), CVIGN_POLYGON);
	obs_property_set_modified_callback(vs, vignette_modified);
	obs_properties_add_int_slider(vg, "vignette_polygon_sides",
				      obs_module_text("Constellations.Vignette.PolygonSides"), 3, 24, 1);
	obs_properties_add_float_slider(vg, "vignette_softness", obs_module_text("Constellations.Vignette.Falloff"),
					0.0, 4.0, 0.01);
	obs_properties_add_bool(vg, "vignette_inverted", obs_module_text("Constellations.Vignette.Inverted"));
	obs_properties_add_group(props, "vignette_group", obs_module_text("Constellations.Group.Vignette"),
				 OBS_GROUP_NORMAL, vg);
}

static void cpat_item_update(struct cpat_item *it, obs_data_t *settings, int index, obs_source_t *exclude)
{
	char key[64];
#define IK(name) (snprintf(key, sizeof(key), "item_%d_%s", index + 1, name), key)
	it->enabled = obs_data_get_bool(settings, IK("enabled"));
	it->kind = (int)obs_data_get_int(settings, IK("kind"));
	it->shape = (int)obs_data_get_int(settings, IK("shape"));
	it->outline_only = obs_data_get_bool(settings, IK("outline_only"));
	it->polygon_sides = (int)obs_data_get_int(settings, IK("polygon_sides"));
	it->stroke_thickness = (float)obs_data_get_double(settings, IK("stroke_thickness"));
	uint32_t c = (uint32_t)obs_data_get_int(settings, IK("color"));
	vec4_from_rgba(&it->color, c);

	const char *ip = obs_data_get_string(settings, IK("image_path"));
	bool image_changed = !it->image_path || strcmp(ip, it->image_path) != 0;
	if (image_changed) {
		bfree(it->image_path);
		it->image_path = bstrdup(ip);
		if (it->kind == CITEM_IMAGE)
			cpat_item_acquire_image(it);
	} else if (it->kind == CITEM_IMAGE && !it->image_loaded) {
		cpat_item_acquire_image(it);
	} else if (it->kind != CITEM_IMAGE && it->image_loaded) {
		cpat_item_release_image(it);
	}

	const char *sn = obs_data_get_string(settings, IK("source_name"));
	bool source_changed = !it->source_name || strcmp(sn, it->source_name) != 0;
	if (source_changed) {
		bfree(it->source_name);
		it->source_name = bstrdup(sn);
		if (it->kind == CITEM_SOURCE)
			cpat_item_acquire_source(it, exclude);
		else
			cpat_item_release_source(it);
	} else if (it->kind == CITEM_SOURCE && !it->source_ref) {
		cpat_item_acquire_source(it, exclude);
	} else if (it->kind != CITEM_SOURCE && it->source_ref) {
		cpat_item_release_source(it);
	}

	it->image_color_mode = (int)obs_data_get_int(settings, IK("image_color_mode"));
	vec4_from_rgba(&it->image_tint, (uint32_t)obs_data_get_int(settings, IK("image_tint")));
	it->image_opacity = (float)obs_data_get_double(settings, IK("image_opacity")) / 100.0f;

	it->size = (float)obs_data_get_double(settings, IK("size"));
	if (it->size < 1.0f)
		it->size = 1.0f;
	cpat_item_refresh_svg(it);
	it->rotation_deg = (float)obs_data_get_double(settings, IK("rotation"));
	it->offset_x = (float)obs_data_get_double(settings, IK("offset_x"));
	it->offset_y = (float)obs_data_get_double(settings, IK("offset_y"));
	it->density = (float)obs_data_get_double(settings, IK("density"));
	if (it->density <= 0.0f)
		it->density = 0.1f;
	it->twinkle_mode = (int)obs_data_get_int(settings, IK("twinkle_mode"));
	it->twinkle_amount = (float)obs_data_get_double(settings, IK("twinkle_amount"));
	it->twinkle_speed = (float)obs_data_get_double(settings, IK("twinkle_speed"));
	it->speed_drift_mode = (int)obs_data_get_int(settings, IK("speed_drift_mode"));
	it->speed_drift_amount = (float)obs_data_get_double(settings, IK("speed_drift_amount"));
	it->autorot_mode = (int)obs_data_get_int(settings, IK("autorot_mode"));
	it->autorot_speed = (float)obs_data_get_double(settings, IK("autorot_speed"));
	it->autorot_style = (int)obs_data_get_int(settings, IK("autorot_style"));
	it->autorot_seed = (float)obs_data_get_int(settings, IK("autorot_seed"));
#undef IK
}

void cpat_renderer_update(struct cpat_renderer *r, obs_data_t *settings, bool include_canvas)
{
	if (include_canvas) {
		r->width = (uint32_t)obs_data_get_int(settings, "width");
		r->height = (uint32_t)obs_data_get_int(settings, "height");
		if (r->width == 0)
			r->width = 1920;
		if (r->height == 0)
			r->height = 1080;
	}
	uint32_t bg = (uint32_t)obs_data_get_int(settings, "background_color");
	vec4_from_rgba(&r->background_color, bg);
	r->has_background = (r->background_color.w > 0.0001f);
	r->anchor_x_pct = (float)obs_data_get_double(settings, "anchor_x_pct");
	r->anchor_y_pct = (float)obs_data_get_double(settings, "anchor_y_pct");
	r->canvas_rotation_deg = (float)obs_data_get_double(settings, "canvas_rotation");

	int ic = (int)obs_data_get_int(settings, "item_count");
	if (ic < 1)
		ic = 1;
	if (ic > CONSTELLATIONS_MAX_ITEMS)
		ic = CONSTELLATIONS_MAX_ITEMS;
	r->item_count = (uint32_t)ic;

	r->layout_mode = (int)obs_data_get_int(settings, "layout_mode");
	r->grid_order = (int)obs_data_get_int(settings, "grid_order");

	for (int i = 0; i < CONSTELLATIONS_MAX_ITEMS; ++i)
		cpat_item_update(&r->items[i], settings, i, r->owner);

	r->motion_angle_deg = (float)obs_data_get_double(settings, "motion_angle");
	r->motion_speed = (float)obs_data_get_double(settings, "motion_speed");
	r->alternating_lines = obs_data_get_bool(settings, "alternating_lines");
	r->speed_drift = obs_data_get_bool(settings, "speed_drift");
	r->speed_drift_amount = (float)obs_data_get_double(settings, "speed_drift_amount");
	r->location_drift = obs_data_get_bool(settings, "location_drift");
	r->location_drift_amount = (float)obs_data_get_double(settings, "location_drift_amount");

	r->twinkle_enabled = obs_data_get_bool(settings, "twinkle_enabled");
	r->twinkle_amount = (float)obs_data_get_double(settings, "twinkle_amount");
	r->twinkle_speed = (float)obs_data_get_double(settings, "twinkle_speed");

	r->autorot_enabled = obs_data_get_bool(settings, "autorot_enabled");
	r->autorot_speed = (float)obs_data_get_double(settings, "autorot_speed");
	r->autorot_style = (int)obs_data_get_int(settings, "autorot_style");
	r->autorot_seed = (float)obs_data_get_int(settings, "autorot_seed");

	r->vignette_enabled = obs_data_get_bool(settings, "vignette_enabled");
	r->vignette_size = (float)obs_data_get_double(settings, "vignette_size");
	r->vignette_anchor_x_pct = (float)obs_data_get_double(settings, "vignette_anchor_x_pct");
	r->vignette_anchor_y_pct = (float)obs_data_get_double(settings, "vignette_anchor_y_pct");
	r->vignette_direction_deg = (float)obs_data_get_double(settings, "vignette_direction");
	r->vignette_shape = (int)obs_data_get_int(settings, "vignette_shape");
	r->vignette_polygon_sides = (int)obs_data_get_int(settings, "vignette_polygon_sides");
	r->vignette_softness = (float)obs_data_get_double(settings, "vignette_softness");
	r->vignette_inverted = obs_data_get_bool(settings, "vignette_inverted");
}

void cpat_renderer_tick(struct cpat_renderer *r, float seconds)
{
	/* Integrate motion each tick so speed and angle changes steer the
	 * pattern instead of teleporting it (same scheme as Topography drift).
	 * The screen-space velocity is decomposed onto the current lattice
	 * axes and accumulated in lattice coordinates: that keeps the on-screen
	 * travel direction equal to the motion angle while letting canvas
	 * rotation rigidly rotate the whole (scrolled) pattern around the
	 * anchor instead of around a point displaced by the accumulated
	 * offset. */
	double ang = (r->motion_angle_deg - r->canvas_rotation_deg) * (M_PI / 180.0);
	double step = (double)r->motion_speed * (double)seconds;
	r->motion_off_along += cos(ang) * step;
	r->motion_off_perp += sin(ang) * step;
	r->elapsed_time += (double)seconds;

	uint32_t count = r->item_count;
	if (count > CONSTELLATIONS_MAX_ITEMS)
		count = CONSTELLATIONS_MAX_ITEMS;
	for (uint32_t i = 0; i < count; ++i) {
		struct cpat_item *it = &r->items[i];
		if (!it->enabled || it->kind != CITEM_SOURCE || !it->source_ref || !it->source_texrender)
			continue;
		obs_source_t *src = obs_weak_source_get_source(it->source_ref);
		if (!src)
			continue;
		uint32_t sw = obs_source_get_base_width(src);
		uint32_t sh = obs_source_get_base_height(src);
		if (sw && sh) {
			it->source_w = sw;
			it->source_h = sh;
		}
		obs_source_release(src);
	}
}

void cpat_renderer_render_background(struct cpat_renderer *r, uint32_t w, uint32_t h)
{
	if (!r->has_background || w == 0 || h == 0)
		return;
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_vec4(color, &r->background_color);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);
	gs_draw_sprite(NULL, 0, w, h);
	gs_technique_end_pass(tech);
	gs_technique_end(tech);
	gs_blend_state_pop();
}

static void render_item_source_to_texrender(struct cpat_item *it)
{
	if (!it->source_ref || !it->source_texrender)
		return;
	obs_source_t *src = obs_weak_source_get_source(it->source_ref);
	if (!src)
		return;
	uint32_t sw = obs_source_get_base_width(src);
	uint32_t sh = obs_source_get_base_height(src);
	if (sw == 0 || sh == 0) {
		obs_source_release(src);
		return;
	}
	gs_texrender_reset(it->source_texrender);
	if (gs_texrender_begin(it->source_texrender, sw, sh)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, (float)sw, 0.0f, (float)sh, -100.0f, 100.0f);
		obs_source_video_render(src);
		gs_texrender_end(it->source_texrender);
		it->source_w = sw;
		it->source_h = sh;
	}
	obs_source_release(src);
}

static void set_common_uniforms(struct cpat_renderer *r, struct cpat_item *it, uint32_t w, uint32_t h, int index,
				uint32_t step_index, uint32_t step_count, float shared_density)
{
	gs_eparam_t *p;
	struct vec2 canvas = {(float)w, (float)h};
	p = gs_effect_get_param_by_name(r->effect, "canvas_size");
	if (p)
		gs_effect_set_vec2(p, &canvas);

	bool shared_grid = (r->layout_mode == CLAYOUT_STEP_REPEAT || r->layout_mode == CLAYOUT_GRID);
	float density = shared_grid ? shared_density : it->density;
	if (density <= 0.0f)
		density = 0.1f;
	float cell = 100.0f / density;
	if (cell < 2.0f)
		cell = 2.0f;
	/* Every item shares the same anchor so layered items stack on the same
	 * origin points instead of being offset from each other. */
	float anchor_x = (r->anchor_x_pct / 100.0f) * canvas.x;
	float anchor_y = (r->anchor_y_pct / 100.0f) * canvas.y;
	struct vec2 anchor = {anchor_x, anchor_y};
	p = gs_effect_get_param_by_name(r->effect, "anchor");
	if (p)
		gs_effect_set_vec2(p, &anchor);

	p = gs_effect_get_param_by_name(r->effect, "cell_size");
	if (p)
		gs_effect_set_float(p, cell);
	p = gs_effect_get_param_by_name(r->effect, "shape_size");
	if (p)
		gs_effect_set_float(p, it->size);
	float rot = it->rotation_deg * (float)(M_PI / 180.0);
	p = gs_effect_get_param_by_name(r->effect, "shape_rotation");
	if (p)
		gs_effect_set_float(p, rot);
	struct vec2 ioff = {it->offset_x, it->offset_y};
	p = gs_effect_get_param_by_name(r->effect, "item_offset");
	if (p)
		gs_effect_set_vec2(p, &ioff);
	/* Shapes use their own color. Images and sources pass the tint color
	 * with the opacity slider as alpha; the shader decides how the tint
	 * applies from image_color_mode. */
	struct vec4 color = it->color;
	if (it->kind != CITEM_SHAPE) {
		color = it->image_tint;
		color.w = it->image_opacity;
	}
	p = gs_effect_get_param_by_name(r->effect, "shape_color");
	if (p)
		gs_effect_set_vec4(p, &color);
	p = gs_effect_get_param_by_name(r->effect, "image_color_mode");
	if (p)
		gs_effect_set_float(p, (float)it->image_color_mode);
	p = gs_effect_get_param_by_name(r->effect, "shape_kind");
	if (p)
		gs_effect_set_int(p, it->shape);
	p = gs_effect_get_param_by_name(r->effect, "polygon_sides");
	if (p)
		gs_effect_set_int(p, it->polygon_sides);
	p = gs_effect_get_param_by_name(r->effect, "stroke_thickness");
	if (p)
		gs_effect_set_float(p, it->stroke_thickness);
	p = gs_effect_get_param_by_name(r->effect, "outline_only");
	if (p)
		gs_effect_set_float(p, it->outline_only ? 1.0f : 0.0f);

	/* The lattice axes come from the canvas rotation; motion is passed in
	 * lattice coordinates (along-row, cross-row) so the accumulated scroll
	 * rotates with the lattice around the anchor. */
	float crot = r->canvas_rotation_deg * (float)(M_PI / 180.0);
	struct vec2 ldir = {cosf(crot), sinf(crot)};
	p = gs_effect_get_param_by_name(r->effect, "lattice_dir");
	if (p)
		gs_effect_set_vec2(p, &ldir);
	struct vec2 moff = {(float)r->motion_off_along, (float)r->motion_off_perp};
	p = gs_effect_get_param_by_name(r->effect, "motion_offset");
	if (p)
		gs_effect_set_vec2(p, &moff);
	p = gs_effect_get_param_by_name(r->effect, "alt_lines");
	if (p)
		gs_effect_set_float(p, r->alternating_lines ? 1.0f : 0.0f);
	float item_seed = (float)index * 17.31f;
	/* The shared seed keeps every item's rows drifting in unison when
	 * following the global setting; Custom uses the item's own seed and
	 * amount so its lines drift independently of the other items. */
	float sd_amount = 0.0f;
	float sd_seed = 0.0f;
	if (it->speed_drift_mode == CSDRIFT_CUSTOM) {
		sd_amount = it->speed_drift_amount;
		sd_seed = item_seed;
	} else if (it->speed_drift_mode == CSDRIFT_GLOBAL && r->speed_drift) {
		sd_amount = r->speed_drift_amount;
	}
	p = gs_effect_get_param_by_name(r->effect, "drift_speed_amt");
	if (p)
		gs_effect_set_float(p, sd_amount);
	p = gs_effect_get_param_by_name(r->effect, "drift_speed_seed");
	if (p)
		gs_effect_set_float(p, sd_seed);
	p = gs_effect_get_param_by_name(r->effect, "drift_loc_amt");
	if (p)
		gs_effect_set_float(p, r->location_drift ? r->location_drift_amount : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "item_seed");
	if (p)
		gs_effect_set_float(p, item_seed);
	p = gs_effect_get_param_by_name(r->effect, "elapsed_time");
	if (p)
		gs_effect_set_float(p, (float)r->elapsed_time);

	float tw_amount = 0.0f;
	float tw_speed = 1.0f;
	if (it->twinkle_mode == CTWINKLE_CUSTOM) {
		tw_amount = it->twinkle_amount;
		tw_speed = it->twinkle_speed;
	} else if (it->twinkle_mode == CTWINKLE_GLOBAL && r->twinkle_enabled) {
		tw_amount = r->twinkle_amount;
		tw_speed = r->twinkle_speed;
	}
	p = gs_effect_get_param_by_name(r->effect, "twinkle_amount");
	if (p)
		gs_effect_set_float(p, tw_amount);
	p = gs_effect_get_param_by_name(r->effect, "twinkle_speed");
	if (p)
		gs_effect_set_float(p, tw_speed);

	/* Auto rotation always scatters random per-cell angles when active;
	 * only the Unison style adds a shared continuous spin, so Random
	 * passes a speed of zero and the shapes hold their random angles. */
	bool ar_active = false;
	float ar_speed = 0.0f;
	float ar_seed = 0.0f;
	if (it->autorot_mode == CAROT_CUSTOM) {
		ar_active = true;
		if (it->autorot_style == CAROT_STYLE_UNISON)
			ar_speed = it->autorot_speed;
		ar_seed = it->autorot_seed;
	} else if (it->autorot_mode == CAROT_GLOBAL && r->autorot_enabled) {
		ar_active = true;
		if (r->autorot_style == CAROT_STYLE_UNISON)
			ar_speed = r->autorot_speed;
		ar_seed = r->autorot_seed;
	}
	p = gs_effect_get_param_by_name(r->effect, "auto_rot_active");
	if (p)
		gs_effect_set_float(p, ar_active ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "auto_rot_speed");
	if (p)
		gs_effect_set_float(p, ar_speed * (float)(M_PI / 180.0));
	p = gs_effect_get_param_by_name(r->effect, "auto_rot_seed");
	if (p)
		gs_effect_set_float(p, ar_seed);

	p = gs_effect_get_param_by_name(r->effect, "layout_mode");
	if (p)
		gs_effect_set_float(p, (float)r->layout_mode);
	p = gs_effect_get_param_by_name(r->effect, "grid_random");
	if (p)
		gs_effect_set_float(p, (r->grid_order == CGRID_ORDERED) ? 0.0f : 1.0f);
	p = gs_effect_get_param_by_name(r->effect, "step_index");
	if (p)
		gs_effect_set_float(p, (float)step_index);
	p = gs_effect_get_param_by_name(r->effect, "step_count");
	if (p)
		gs_effect_set_float(p, (float)(step_count > 0 ? step_count : 1));

	float min_dim = (canvas.x < canvas.y) ? canvas.x : canvas.y;
	float vsz = r->vignette_size * 0.5f * min_dim;
	struct vec2 vcenter = {(r->vignette_anchor_x_pct / 100.0f) * canvas.x,
			       (r->vignette_anchor_y_pct / 100.0f) * canvas.y};
	float vang = r->vignette_direction_deg * (float)(M_PI / 180.0);
	p = gs_effect_get_param_by_name(r->effect, "vignette_enabled");
	if (p)
		gs_effect_set_float(p, r->vignette_enabled ? 1.0f : 0.0f);
	p = gs_effect_get_param_by_name(r->effect, "vignette_center");
	if (p)
		gs_effect_set_vec2(p, &vcenter);
	p = gs_effect_get_param_by_name(r->effect, "vignette_size_px");
	if (p)
		gs_effect_set_float(p, vsz);
	p = gs_effect_get_param_by_name(r->effect, "vignette_direction");
	if (p)
		gs_effect_set_float(p, vang);
	p = gs_effect_get_param_by_name(r->effect, "vignette_shape");
	if (p)
		gs_effect_set_int(p, r->vignette_shape);
	p = gs_effect_get_param_by_name(r->effect, "vignette_polygon_sides");
	if (p)
		gs_effect_set_int(p, r->vignette_polygon_sides);
	p = gs_effect_get_param_by_name(r->effect, "vignette_softness");
	if (p)
		gs_effect_set_float(p, r->vignette_softness);
	p = gs_effect_get_param_by_name(r->effect, "vignette_inverted");
	if (p)
		gs_effect_set_float(p, r->vignette_inverted ? 1.0f : 0.0f);
}

void cpat_renderer_render_items(struct cpat_renderer *r, uint32_t w, uint32_t h)
{
	if (!r->effect || w == 0 || h == 0)
		return;

	uint32_t count = r->item_count;
	if (count > CONSTELLATIONS_MAX_ITEMS)
		count = CONSTELLATIONS_MAX_ITEMS;

	uint32_t step_count = 0;
	float shared_density = 2.0f;
	if (r->layout_mode == CLAYOUT_STEP_REPEAT || r->layout_mode == CLAYOUT_GRID) {
		for (uint32_t i = 0; i < count; ++i) {
			if (r->items[i].enabled) {
				if (step_count == 0)
					shared_density = r->items[i].density;
				step_count++;
			}
		}
		if (step_count == 0)
			step_count = 1;
	}

	for (uint32_t i = 0; i < count; ++i) {
		struct cpat_item *it = &r->items[i];
		if (!it->enabled)
			continue;
		if (it->kind == CITEM_SOURCE)
			render_item_source_to_texrender(it);
	}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	uint32_t next_step_index = 0;
	for (uint32_t i = 0; i < count; ++i) {
		struct cpat_item *it = &r->items[i];
		if (!it->enabled)
			continue;
		uint32_t this_step_index = next_step_index++;

		gs_texture_t *tex = NULL;
		const char *technique = "DrawShape";
		if (it->kind == CITEM_IMAGE) {
			tex = cpat_item_image_texture(it);
			if (!tex)
				continue;
			technique = "DrawImage";
		} else if (it->kind == CITEM_SOURCE) {
			if (!it->source_texrender)
				continue;
			tex = gs_texrender_get_texture(it->source_texrender);
			if (!tex)
				continue;
			technique = "DrawImage";
		}

		set_common_uniforms(r, it, w, h, (int)i, this_step_index, step_count, shared_density);

		if (tex) {
			gs_eparam_t *tp = gs_effect_get_param_by_name(r->effect, "cell_texture");
			if (tp)
				gs_effect_set_texture(tp, tex);
		}

		while (gs_effect_loop(r->effect, technique))
			gs_draw_sprite(NULL, 0, w, h);
	}

	gs_blend_state_pop();
}
