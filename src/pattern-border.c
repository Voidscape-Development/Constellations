/*
Constellations - Pattern Border Filter
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

/* Frames the source with a band of tiled shapes. Inside draws the band over
 * the source's edges; Outside grows the output by the border widths and puts
 * the source in the middle. The shapes either follow the frame (perimeter)
 * or are the regular pattern lattice masked to the band. */
struct cpat_border_filter {
	obs_source_t *self;
	struct cpat_renderer r;
	int position;
	bool round_source;
};

static const char *cpat_border_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.PatternBorder.Name");
}

static void cpat_border_read_settings(struct cpat_border_filter *f, obs_data_t *settings)
{
	struct cpat_border *b = &f->r.border;
	f->position = (int)obs_data_get_int(settings, "border_position");
	f->round_source = obs_data_get_bool(settings, "border_round_source");

	b->active = true;
	b->layout = (int)obs_data_get_int(settings, "border_layout");
	if (obs_data_get_bool(settings, "border_per_side")) {
		b->left = (float)obs_data_get_int(settings, "border_left");
		b->top = (float)obs_data_get_int(settings, "border_top");
		b->right = (float)obs_data_get_int(settings, "border_right");
		b->bottom = (float)obs_data_get_int(settings, "border_bottom");
	} else {
		float w = (float)obs_data_get_int(settings, "border_width");
		b->left = b->top = b->right = b->bottom = w;
	}
	b->radius = (float)obs_data_get_int(settings, "border_radius");
	b->softness = (float)obs_data_get_int(settings, "border_softness");
	b->clip = obs_data_get_bool(settings, "border_clip");
	b->rows = (int)obs_data_get_int(settings, "border_rows");
	if (b->rows < 1)
		b->rows = 1;
	if (b->rows > 8)
		b->rows = 8;
	b->stagger_rows = obs_data_get_bool(settings, "border_stagger");
	b->align = (int)obs_data_get_int(settings, "border_orientation") == CBORDER_ORIENT_FOLLOW_EDGE;
}

static void *cpat_border_create(obs_data_t *settings, obs_source_t *source)
{
	struct cpat_border_filter *f = bzalloc(sizeof(*f));
	f->self = source;
	cpat_renderer_init(&f->r, source, CPAT_HOST_BORDER);
	cpat_border_read_settings(f, settings);
	cpat_renderer_update(&f->r, settings);
	return f;
}

static void cpat_border_destroy(void *data)
{
	struct cpat_border_filter *f = data;
	cpat_renderer_free(&f->r);
	bfree(f);
}

static void cpat_border_update(void *data, obs_data_t *settings)
{
	struct cpat_border_filter *f = data;
	cpat_border_read_settings(f, settings);
	cpat_renderer_update(&f->r, settings);
}

static void cpat_border_tick(void *data, float seconds)
{
	struct cpat_border_filter *f = data;
	cpat_renderer_tick(&f->r, seconds);
}

static bool cpat_border_outside(const struct cpat_border_filter *f)
{
	return f->position == CBORDER_OUTSIDE;
}

static uint32_t cpat_border_width(void *data)
{
	struct cpat_border_filter *f = data;
	obs_source_t *target = obs_filter_get_target(f->self);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	if (w && cpat_border_outside(f))
		w += (uint32_t)(f->r.border.left + f->r.border.right);
	return w;
}

static uint32_t cpat_border_height(void *data)
{
	struct cpat_border_filter *f = data;
	obs_source_t *target = obs_filter_get_target(f->self);
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (h && cpat_border_outside(f))
		h += (uint32_t)(f->r.border.top + f->r.border.bottom);
	return h;
}

static void cpat_border_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cpat_border_filter *f = data;

	obs_source_t *target = obs_filter_get_target(f->self);
	uint32_t sw = target ? obs_source_get_base_width(target) : 0;
	uint32_t sh = target ? obs_source_get_base_height(target) : 0;
	if (sw == 0 || sh == 0) {
		obs_source_skip_video_filter(f->self);
		return;
	}

	const bool outside = cpat_border_outside(f);
	const struct cpat_border *b = &f->r.border;
	uint32_t w = sw, h = sh;
	float ox = 0.0f, oy = 0.0f;
	if (outside) {
		w += (uint32_t)(b->left + b->right);
		h += (uint32_t)(b->top + b->bottom);
		ox = b->left;
		oy = b->top;
	}

	/* Round Source Corners clips the source to the frame: its outer edge
	 * when the band is drawn over the source, its inner edge when the
	 * band surrounds it. Either way the clip box is the source itself. */
	struct vec4 outer_r, inner_r;
	cpat_border_corner_radii(&f->r, w, h, &outer_r, &inner_r);
	struct vec4 src_r = outside ? inner_r : outer_r;
	const bool round = f->round_source && f->r.effect &&
			   (src_r.x > 0.0f || src_r.y > 0.0f || src_r.z > 0.0f || src_r.w > 0.0f);

	enum obs_allow_direct_render direct = (round || outside) ? OBS_NO_DIRECT_RENDERING : OBS_ALLOW_DIRECT_RENDERING;
	if (!obs_source_process_filter_begin(f->self, GS_RGBA, direct))
		return;

	/* Outside, the fill sits behind the source; inside, it covers the
	 * source's edges. */
	if (outside)
		cpat_renderer_render_background(&f->r, w, h);

	gs_matrix_push();
	gs_matrix_translate3f(ox, oy, 0.0f);
	if (round) {
		struct vec2 size = {(float)sw, (float)sh};
		gs_eparam_t *p = gs_effect_get_param_by_name(f->r.effect, "src_size");
		if (p)
			gs_effect_set_vec2(p, &size);
		p = gs_effect_get_param_by_name(f->r.effect, "src_radii");
		if (p)
			gs_effect_set_vec4(p, &src_r);
		obs_source_process_filter_tech_end(f->self, f->r.effect, sw, sh, "DrawSourceRounded");
	} else {
		obs_source_process_filter_end(f->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), sw, sh);
	}
	gs_matrix_pop();

	if (!outside)
		cpat_renderer_render_background(&f->r, w, h);
	cpat_renderer_render_items(&f->r, w, h);
}

static void set_visible(obs_properties_t *props, const char *name, bool visible)
{
	obs_property_t *p = obs_properties_get(props, name);
	if (p)
		obs_property_set_visible(p, visible);
}

static bool border_visibility_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool per_side = obs_data_get_bool(settings, "border_per_side");
	set_visible(props, "border_width", !per_side);
	set_visible(props, "border_top", per_side);
	set_visible(props, "border_bottom", per_side);
	set_visible(props, "border_left", per_side);
	set_visible(props, "border_right", per_side);

	/* The perimeter layout places shapes along the frame itself, so the
	 * lattice anchor, rotation and motion angle only apply when masked. */
	bool perimeter = (int)obs_data_get_int(settings, "border_layout") == CBORDER_PERIMETER;
	set_visible(props, "border_rows", perimeter);
	set_visible(props, "border_stagger", perimeter);
	set_visible(props, "border_orientation", perimeter);
	set_visible(props, "border_clip", perimeter);
	set_visible(props, "anchor_x_pct", !perimeter);
	set_visible(props, "anchor_y_pct", !perimeter);
	set_visible(props, "canvas_rotation", !perimeter);
	set_visible(props, "motion_angle", !perimeter);
	return true;
}

static obs_properties_t *cpat_border_props(void *data)
{
	struct cpat_border_filter *f = data;
	obs_properties_t *props = obs_properties_create();
	obs_properties_t *grp = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_list(grp, "border_position", obs_module_text("Constellations.Border.Position"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Border.Position.Inside"), CBORDER_INSIDE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Border.Position.Outside"), CBORDER_OUTSIDE);

	obs_properties_add_int_slider(grp, "border_width", obs_module_text("Constellations.Border.Width"), 0, 1000, 1);
	p = obs_properties_add_bool(grp, "border_per_side", obs_module_text("Constellations.Border.PerSide"));
	obs_property_set_modified_callback(p, border_visibility_modified);
	obs_properties_add_int_slider(grp, "border_top", obs_module_text("Constellations.Border.Top"), 0, 1000, 1);
	obs_properties_add_int_slider(grp, "border_bottom", obs_module_text("Constellations.Border.Bottom"), 0, 1000,
				      1);
	obs_properties_add_int_slider(grp, "border_left", obs_module_text("Constellations.Border.Left"), 0, 1000, 1);
	obs_properties_add_int_slider(grp, "border_right", obs_module_text("Constellations.Border.Right"), 0, 1000, 1);

	obs_properties_add_int_slider(grp, "border_radius", obs_module_text("Constellations.Border.Radius"), 0, 1000,
				      1);
	obs_properties_add_int_slider(grp, "border_softness", obs_module_text("Constellations.Border.Softness"), 0, 500,
				      1);
	p = obs_properties_add_bool(grp, "border_round_source", obs_module_text("Constellations.Border.RoundSource"));
	obs_property_set_long_description(p, obs_module_text("Constellations.Border.RoundSource.Tooltip"));

	p = obs_properties_add_list(grp, "border_layout", obs_module_text("Constellations.Border.Layout"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Border.Layout.Perimeter"), CBORDER_PERIMETER);
	obs_property_list_add_int(p, obs_module_text("Constellations.Border.Layout.Masked"), CBORDER_MASKED);
	obs_property_set_long_description(p, obs_module_text("Constellations.Border.Layout.Tooltip"));
	obs_property_set_modified_callback(p, border_visibility_modified);

	obs_properties_add_int_slider(grp, "border_rows", obs_module_text("Constellations.Border.Rows"), 1, 8, 1);
	obs_properties_add_bool(grp, "border_stagger", obs_module_text("Constellations.Border.Stagger"));
	p = obs_properties_add_list(grp, "border_orientation", obs_module_text("Constellations.Border.Orientation"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Border.Orientation.Uniform"),
				  CBORDER_ORIENT_UNIFORM);
	obs_property_list_add_int(p, obs_module_text("Constellations.Border.Orientation.FollowEdge"),
				  CBORDER_ORIENT_FOLLOW_EDGE);
	obs_property_set_long_description(p, obs_module_text("Constellations.Border.Orientation.Tooltip"));
	p = obs_properties_add_bool(grp, "border_clip", obs_module_text("Constellations.Border.Clip"));
	obs_property_set_long_description(p, obs_module_text("Constellations.Border.Clip.Tooltip"));

	obs_properties_add_group(props, "border_group", obs_module_text("Constellations.Group.Border"),
				 OBS_GROUP_NORMAL, grp);

	cpat_renderer_get_properties(f ? &f->r : NULL, props, CPAT_HOST_BORDER);
	return props;
}

static void cpat_border_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "border_position", CBORDER_INSIDE);
	obs_data_set_default_int(settings, "border_width", 40);
	obs_data_set_default_bool(settings, "border_per_side", false);
	obs_data_set_default_int(settings, "border_top", 40);
	obs_data_set_default_int(settings, "border_bottom", 40);
	obs_data_set_default_int(settings, "border_left", 40);
	obs_data_set_default_int(settings, "border_right", 40);
	obs_data_set_default_int(settings, "border_radius", 0);
	obs_data_set_default_int(settings, "border_softness", 0);
	obs_data_set_default_bool(settings, "border_round_source", true);
	obs_data_set_default_int(settings, "border_layout", CBORDER_PERIMETER);
	obs_data_set_default_int(settings, "border_rows", 1);
	obs_data_set_default_bool(settings, "border_stagger", false);
	obs_data_set_default_int(settings, "border_orientation", CBORDER_ORIENT_UNIFORM);
	obs_data_set_default_bool(settings, "border_clip", true);
	cpat_renderer_set_defaults(settings, CPAT_HOST_BORDER);
}

static struct obs_source_info cpat_border_info = {
	.id = "constellations_pattern_border",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = cpat_border_get_name,
	.create = cpat_border_create,
	.destroy = cpat_border_destroy,
	.update = cpat_border_update,
	.video_tick = cpat_border_tick,
	.video_render = cpat_border_render,
	.get_width = cpat_border_width,
	.get_height = cpat_border_height,
	.get_properties = cpat_border_props,
	.get_defaults = cpat_border_defaults,
};

void constellations_register_pattern_border(void)
{
	obs_register_source(&cpat_border_info);
}
