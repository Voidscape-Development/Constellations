/*
Constellations - Shapes Transition
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>

#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#include <math.h>

#include "shape-defs.h"

static void cshape_update(void *data, obs_data_t *settings);

struct cshape_transition {
	obs_source_t *self;

	int transition_mode;
	int transition_type;
	float directional_angle_deg;
	float anchor_x_pct;
	float anchor_y_pct;

	int shape_kind;
	int polygon_sides;
	float stroke_thickness;
	struct vec4 shape_color;

	bool use_source_texture;
	char *texture_source_name;
	obs_weak_source_t *texture_source_ref;
	gs_texrender_t *texture_source_texrender;

	bool gap_enabled;
	float gap_distance;
	struct vec4 gap_color;

	float cell_size;
	int tiling_mode;
	float fade_min;
	float fade_max;

	int dissolve_type;
	int zoom_dir;

	bool add_logo;
	char *logo_path;
	gs_image_file_ex_t logo_image;
	bool logo_loaded;
	int logo_display;
	float logo_slide_angle_deg;
	float logo_scale;

	gs_effect_t *effect;
	float elapsed_seconds;
};

static const char *cshape_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.ShapesTransition.Name");
}

static void cshape_load_effect(struct cshape_transition *t)
{
	char *path = obs_module_file("effects/shapes_transition.effect");
	if (!path) {
		obs_log(LOG_ERROR, "Failed to find shapes_transition.effect");
		return;
	}
	obs_enter_graphics();
	if (t->effect) {
		gs_effect_destroy(t->effect);
		t->effect = NULL;
	}
	char *errors = NULL;
	t->effect = gs_effect_create_from_file(path, &errors);
	if (!t->effect) {
		obs_log(LOG_ERROR, "Failed to load shapes_transition.effect: %s", errors ? errors : "(no detail)");
	}
	bfree(errors);
	obs_leave_graphics();
	bfree(path);
}

static void cshape_release_logo(struct cshape_transition *t)
{
	if (t->logo_loaded) {
		obs_enter_graphics();
		gs_image_file_ex_free(&t->logo_image);
		obs_leave_graphics();
		t->logo_loaded = false;
	}
}

static void cshape_load_logo(struct cshape_transition *t)
{
	cshape_release_logo(t);
	if (!t->logo_path || !*t->logo_path)
		return;
	gs_image_file_ex_init(&t->logo_image, t->logo_path, GS_IMAGE_ALPHA_STRAIGHT);
	obs_enter_graphics();
	gs_image_file_ex_init_texture(&t->logo_image);
	obs_leave_graphics();
	t->logo_loaded = t->logo_image.loaded;
}

static void cshape_release_texture_source(struct cshape_transition *t)
{
	if (t->texture_source_ref) {
		obs_weak_source_release(t->texture_source_ref);
		t->texture_source_ref = NULL;
	}
	if (t->texture_source_texrender) {
		obs_enter_graphics();
		gs_texrender_destroy(t->texture_source_texrender);
		obs_leave_graphics();
		t->texture_source_texrender = NULL;
	}
}

static void cshape_acquire_texture_source(struct cshape_transition *t)
{
	cshape_release_texture_source(t);
	if (!t->texture_source_name || !*t->texture_source_name)
		return;
	obs_source_t *src = obs_get_source_by_name(t->texture_source_name);
	if (!src)
		return;
	t->texture_source_ref = obs_source_get_weak_source(src);
	obs_source_release(src);

	obs_enter_graphics();
	t->texture_source_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();
}

static void *cshape_create(obs_data_t *settings, obs_source_t *source)
{
	struct cshape_transition *t = bzalloc(sizeof(*t));
	t->self = source;
	cshape_load_effect(t);
	cshape_update(t, settings);
	return t;
}

static void cshape_destroy(void *data)
{
	struct cshape_transition *t = data;
	cshape_release_logo(t);
	cshape_release_texture_source(t);
	bfree(t->texture_source_name);
	bfree(t->logo_path);
	if (t->effect) {
		obs_enter_graphics();
		gs_effect_destroy(t->effect);
		obs_leave_graphics();
	}
	bfree(t);
}

static void cshape_update(void *data, obs_data_t *settings)
{
	struct cshape_transition *t = data;
	t->transition_mode = (int)obs_data_get_int(settings, "transition_mode");
	t->transition_type = (int)obs_data_get_int(settings, "transition_type");
	t->directional_angle_deg = (float)obs_data_get_double(settings, "directional_angle");
	t->anchor_x_pct = (float)obs_data_get_double(settings, "anchor_x_pct");
	t->anchor_y_pct = (float)obs_data_get_double(settings, "anchor_y_pct");

	t->shape_kind = (int)obs_data_get_int(settings, "shape");
	t->polygon_sides = (int)obs_data_get_int(settings, "polygon_sides");
	t->stroke_thickness = (float)obs_data_get_double(settings, "stroke_thickness");

	uint32_t shape_col = (uint32_t)obs_data_get_int(settings, "shape_color");
	vec4_from_rgba(&t->shape_color, shape_col);

	t->use_source_texture = obs_data_get_bool(settings, "use_source_texture");
	const char *tex_name = obs_data_get_string(settings, "texture_source_name");
	bool tex_changed = !t->texture_source_name || strcmp(tex_name, t->texture_source_name) != 0;
	if (tex_changed) {
		bfree(t->texture_source_name);
		t->texture_source_name = bstrdup(tex_name);
		if (t->use_source_texture)
			cshape_acquire_texture_source(t);
		else
			cshape_release_texture_source(t);
	} else if (t->use_source_texture && !t->texture_source_ref) {
		cshape_acquire_texture_source(t);
	} else if (!t->use_source_texture && t->texture_source_ref) {
		cshape_release_texture_source(t);
	}

	t->gap_enabled = obs_data_get_bool(settings, "shape_gap_enabled");
	t->gap_distance = (float)obs_data_get_double(settings, "gap_distance");
	uint32_t gap_col = (uint32_t)obs_data_get_int(settings, "gap_color");
	vec4_from_rgba(&t->gap_color, gap_col);

	t->cell_size = (float)obs_data_get_double(settings, "cell_size");
	if (t->cell_size < 4.0f)
		t->cell_size = 4.0f;
	t->tiling_mode = (int)obs_data_get_int(settings, "tiling_mode");
	t->fade_min = (float)obs_data_get_double(settings, "fade_min");
	t->fade_max = (float)obs_data_get_double(settings, "fade_max");
	if (t->fade_max < t->fade_min)
		t->fade_max = t->fade_min;

	t->dissolve_type = (int)obs_data_get_int(settings, "dissolve_type");
	t->zoom_dir = (int)obs_data_get_int(settings, "zoom_dir");

	t->add_logo = obs_data_get_bool(settings, "add_logo");
	const char *new_logo = obs_data_get_string(settings, "logo_path");
	bool logo_changed = !t->logo_path || strcmp(new_logo, t->logo_path) != 0;
	if (logo_changed) {
		bfree(t->logo_path);
		t->logo_path = bstrdup(new_logo);
	}
	if (t->add_logo && (logo_changed || !t->logo_loaded))
		cshape_load_logo(t);
	else if (!t->add_logo && t->logo_loaded)
		cshape_release_logo(t);

	t->logo_display = (int)obs_data_get_int(settings, "logo_display");
	t->logo_slide_angle_deg = (float)obs_data_get_double(settings, "logo_slide_angle");
	t->logo_scale = (float)obs_data_get_double(settings, "logo_scale");
	if (t->logo_scale <= 0.0f)
		t->logo_scale = 1.0f;
}

static void cshape_render_texture_source(struct cshape_transition *t, uint32_t cx, uint32_t cy)
{
	if (!t->texture_source_ref || !t->texture_source_texrender)
		return;
	obs_source_t *src = obs_weak_source_get_source(t->texture_source_ref);
	if (!src)
		return;
	uint32_t sw = obs_source_get_base_width(src);
	uint32_t sh = obs_source_get_base_height(src);
	if (sw == 0 || sh == 0) {
		obs_source_release(src);
		return;
	}
	gs_texrender_reset(t->texture_source_texrender);
	if (gs_texrender_begin(t->texture_source_texrender, cx, cy)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		/* Center-crop the source to the canvas aspect ratio so it covers
		 * the full transition region without stretching. */
		float src_aspect = (float)sw / (float)sh;
		float dst_aspect = (float)cx / (float)cy;
		float ow = (float)sw;
		float oh = (float)sh;
		float ox = 0.0f;
		float oy = 0.0f;
		if (src_aspect > dst_aspect) {
			ow = (float)sh * dst_aspect;
			ox = ((float)sw - ow) * 0.5f;
		} else if (src_aspect < dst_aspect) {
			oh = (float)sw / dst_aspect;
			oy = ((float)sh - oh) * 0.5f;
		}
		gs_ortho(ox, ox + ow, oy, oy + oh, -100.0f, 100.0f);
		obs_source_video_render(src);
		gs_texrender_end(t->texture_source_texrender);
	}
	obs_source_release(src);
}

static void cshape_tick(void *data, float seconds)
{
	struct cshape_transition *t = data;
	t->elapsed_seconds += seconds;
}

struct cshape_render_ctx {
	struct cshape_transition *t;
	float progress;
	gs_texture_t *a_tex;
	gs_texture_t *b_tex;
};

static bool cshape_audio_render(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio_output,
				uint32_t mixers, size_t channels, size_t sample_rate)
{
	struct cshape_transition *t = data;
	return obs_transition_audio_render(t->self, ts_out, audio_output, mixers, channels, sample_rate, NULL, NULL);
}

static void cshape_video_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t_progress, uint32_t cx,
				  uint32_t cy)
{
	struct cshape_transition *t = data;
	if (!t->effect) {
		gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_effect_set_texture(gs_effect_get_param_by_name(def, "image"), b ? b : a);
		while (gs_effect_loop(def, "Draw"))
			gs_draw_sprite(NULL, 0, cx, cy);
		return;
	}

	if (t->use_source_texture)
		cshape_render_texture_source(t, cx, cy);

	gs_eparam_t *p;
	p = gs_effect_get_param_by_name(t->effect, "tex_a");
	if (p)
		gs_effect_set_texture(p, a);
	p = gs_effect_get_param_by_name(t->effect, "tex_b");
	if (p)
		gs_effect_set_texture(p, b);

	gs_texture_t *cell_tex = NULL;
	if (t->use_source_texture && t->texture_source_texrender)
		cell_tex = gs_texrender_get_texture(t->texture_source_texrender);
	if (!cell_tex && t->transition_mode == CTRANS_MODE_DISSOLVE)
		cell_tex = a;
	p = gs_effect_get_param_by_name(t->effect, "cell_texture");
	if (p)
		gs_effect_set_texture(p, cell_tex ? cell_tex : a);

	struct vec2 canvas = {(float)cx, (float)cy};
	p = gs_effect_get_param_by_name(t->effect, "canvas_size");
	if (p)
		gs_effect_set_vec2(p, &canvas);

	float ang = t->directional_angle_deg * (float)(M_PI / 180.0);
	struct vec2 dir = {cosf(ang), sinf(ang)};
	p = gs_effect_get_param_by_name(t->effect, "directional_dir");
	if (p)
		gs_effect_set_vec2(p, &dir);

	struct vec2 anchor = {canvas.x * (t->anchor_x_pct / 100.0f), canvas.y * (t->anchor_y_pct / 100.0f)};
	p = gs_effect_get_param_by_name(t->effect, "anchor");
	if (p)
		gs_effect_set_vec2(p, &anchor);

	p = gs_effect_get_param_by_name(t->effect, "transition_mode");
	if (p)
		gs_effect_set_int(p, t->transition_mode);
	p = gs_effect_get_param_by_name(t->effect, "transition_type");
	if (p)
		gs_effect_set_int(p, t->transition_type);
	p = gs_effect_get_param_by_name(t->effect, "dissolve_type");
	if (p)
		gs_effect_set_int(p, t->dissolve_type);
	p = gs_effect_get_param_by_name(t->effect, "zoom_dir");
	if (p)
		gs_effect_set_int(p, t->zoom_dir);
	p = gs_effect_get_param_by_name(t->effect, "shape_kind");
	if (p)
		gs_effect_set_int(p, t->shape_kind);
	p = gs_effect_get_param_by_name(t->effect, "polygon_sides");
	if (p)
		gs_effect_set_int(p, t->polygon_sides);
	p = gs_effect_get_param_by_name(t->effect, "stroke_thickness");
	if (p)
		gs_effect_set_float(p, t->stroke_thickness);
	p = gs_effect_get_param_by_name(t->effect, "use_source_texture");
	if (p)
		gs_effect_set_int(p, t->use_source_texture ? 1 : 0);
	p = gs_effect_get_param_by_name(t->effect, "cell_size");
	if (p)
		gs_effect_set_float(p, t->cell_size);
	p = gs_effect_get_param_by_name(t->effect, "tiling_mode");
	if (p)
		gs_effect_set_int(p, t->tiling_mode);
	p = gs_effect_get_param_by_name(t->effect, "gap_distance");
	if (p)
		gs_effect_set_float(p, t->gap_enabled ? t->gap_distance : 0.0f);
	p = gs_effect_get_param_by_name(t->effect, "gap_color");
	if (p)
		gs_effect_set_vec4(p, &t->gap_color);
	p = gs_effect_get_param_by_name(t->effect, "shape_color");
	if (p)
		gs_effect_set_vec4(p, &t->shape_color);
	p = gs_effect_get_param_by_name(t->effect, "fade_min");
	if (p)
		gs_effect_set_float(p, t->fade_min);
	p = gs_effect_get_param_by_name(t->effect, "fade_max");
	if (p)
		gs_effect_set_float(p, t->fade_max);
	p = gs_effect_get_param_by_name(t->effect, "progress");
	if (p)
		gs_effect_set_float(p, t_progress);

	while (gs_effect_loop(t->effect, "Draw"))
		gs_draw_sprite(NULL, 0, cx, cy);

	if (t->add_logo && t->logo_loaded && t->logo_image.texture) {
		gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *img = gs_effect_get_param_by_name(def, "image");

		float lw = (float)t->logo_image.cx * t->logo_scale;
		float lh = (float)t->logo_image.cy * t->logo_scale;
		float center_x = (float)cx * 0.5f - lw * 0.5f;
		float center_y = (float)cy * 0.5f - lh * 0.5f;
		float draw_x = center_x;
		float draw_y = center_y;
		float alpha = 1.0f;

		if (t->logo_display == CLOGO_FADE) {
			alpha = sinf(t_progress * (float)M_PI);
			if (alpha < 0.0f)
				alpha = 0.0f;
		} else {
			float hold_start = 0.4f;
			float hold_end = 0.6f;
			float lang = t->logo_slide_angle_deg * (float)(M_PI / 180.0);
			float off = (float)cx + lw;
			float dx = cosf(lang) * off;
			float dy = sinf(lang) * off;
			if (t_progress < hold_start) {
				float k = t_progress / hold_start;
				k = 1.0f - k;
				draw_x = center_x - dx * k;
				draw_y = center_y - dy * k;
			} else if (t_progress > hold_end) {
				float k = (t_progress - hold_end) / (1.0f - hold_end);
				draw_x = center_x + dx * k;
				draw_y = center_y + dy * k;
			}
		}

		struct vec4 mod;
		vec4_set(&mod, 1.0f, 1.0f, 1.0f, alpha);
		gs_eparam_t *mp = gs_effect_get_param_by_name(def, "color");
		if (mp)
			gs_effect_set_vec4(mp, &mod);
		gs_effect_set_texture(img, t->logo_image.texture);

		gs_matrix_push();
		gs_matrix_translate3f(draw_x, draw_y, 0.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
		while (gs_effect_loop(def, "Draw"))
			gs_draw_sprite(t->logo_image.texture, 0, (uint32_t)lw, (uint32_t)lh);
		gs_blend_state_pop();
		gs_matrix_pop();
	}
}

static void cshape_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cshape_transition *t = data;
	obs_transition_video_render(t->self, cshape_video_callback);
}

static bool prop_mode_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	int mode = (int)obs_data_get_int(settings, "transition_mode");
	obs_property_t *dt = obs_properties_get(props, "dissolve_type");
	obs_property_t *zd = obs_properties_get(props, "zoom_dir");
	if (dt)
		obs_property_set_visible(dt, mode == CTRANS_MODE_DISSOLVE);
	if (zd)
		obs_property_set_visible(zd, mode == CTRANS_MODE_SHAPE_ZOOM);
	return true;
}

static bool prop_type_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	int type = (int)obs_data_get_int(settings, "transition_type");
	bool show_angle = (type == CTRANS_TYPE_DIRECTIONAL || type == CTRANS_TYPE_MIRRORED);
	bool show_anchor = (type != CTRANS_TYPE_DIRECTIONAL);
	obs_property_t *a = obs_properties_get(props, "directional_angle");
	obs_property_t *ax = obs_properties_get(props, "anchor_x_pct");
	obs_property_t *ay = obs_properties_get(props, "anchor_y_pct");
	if (a)
		obs_property_set_visible(a, show_angle);
	if (ax)
		obs_property_set_visible(ax, show_anchor);
	if (ay)
		obs_property_set_visible(ay, show_anchor);
	return true;
}

static bool prop_shape_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	int s = (int)obs_data_get_int(settings, "shape");
	obs_property_t *poly = obs_properties_get(props, "polygon_sides");
	obs_property_t *stroke = obs_properties_get(props, "stroke_thickness");
	if (poly)
		obs_property_set_visible(poly, s == CSHAPE_POLYGON);
	if (stroke)
		obs_property_set_visible(stroke, s == CSHAPE_CROSS);
	return true;
}

static bool prop_use_tex_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool use = obs_data_get_bool(settings, "use_source_texture");
	obs_property_t *ts = obs_properties_get(props, "texture_source_name");
	obs_property_t *col = obs_properties_get(props, "shape_color");
	if (ts)
		obs_property_set_visible(ts, use);
	if (col)
		obs_property_set_visible(col, !use);
	return true;
}

static bool prop_gap_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool g = obs_data_get_bool(settings, "shape_gap_enabled");
	obs_property_t *gd = obs_properties_get(props, "gap_distance");
	obs_property_t *gc = obs_properties_get(props, "gap_color");
	if (gd)
		obs_property_set_visible(gd, g);
	if (gc)
		obs_property_set_visible(gc, g);
	return true;
}

static bool prop_logo_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool a = obs_data_get_bool(settings, "add_logo");
	const char *names[] = {"logo_path", "logo_display", "logo_slide_angle", "logo_scale"};
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		obs_property_t *pp = obs_properties_get(props, names[i]);
		if (pp)
			obs_property_set_visible(pp, a);
	}
	if (a) {
		int disp = (int)obs_data_get_int(settings, "logo_display");
		obs_property_t *ang = obs_properties_get(props, "logo_slide_angle");
		if (ang)
			obs_property_set_visible(ang, disp == CLOGO_SLIDE);
	}
	return true;
}

struct enum_src_ctx {
	obs_property_t *list;
	DARRAY(char *) names;
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
	for (size_t i = 0; i < ctx.names.num; ++i) {
		obs_property_list_add_string(list, ctx.names.array[i], ctx.names.array[i]);
	}
	da_free(ctx.names);
}

static obs_properties_t *cshape_properties(void *data)
{
	struct cshape_transition *t = data;
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p;

	p = obs_properties_add_list(props, "transition_mode", obs_module_text("Constellations.Transition.Mode"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Mode.Cover"), CTRANS_MODE_COVER);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Mode.Dissolve"), CTRANS_MODE_DISSOLVE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Mode.ShapeZoom"),
				  CTRANS_MODE_SHAPE_ZOOM);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Mode.Keyhole"), CTRANS_MODE_KEYHOLE);
	obs_property_set_modified_callback(p, prop_mode_changed);

	p = obs_properties_add_list(props, "transition_type", obs_module_text("Constellations.Transition.Type"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Type.Directional"),
				  CTRANS_TYPE_DIRECTIONAL);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Type.InsideOut"),
				  CTRANS_TYPE_INSIDE_OUT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Type.OutsideIn"),
				  CTRANS_TYPE_OUTSIDE_IN);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Type.Mirrored"), CTRANS_TYPE_MIRRORED);
	obs_property_set_modified_callback(p, prop_type_changed);

	obs_properties_add_float_slider(props, "directional_angle", obs_module_text("Constellations.Transition.Angle"),
					0.0, 360.0, 1.0);

	obs_properties_add_float_slider(props, "anchor_x_pct", obs_module_text("Constellations.Anchor.X"), 0.0, 100.0,
					0.5);
	obs_properties_add_float_slider(props, "anchor_y_pct", obs_module_text("Constellations.Anchor.Y"), 0.0, 100.0,
					0.5);

	obs_properties_add_float_slider(props, "cell_size", obs_module_text("Constellations.Transition.CellSize"), 4.0,
					512.0, 1.0);

	p = obs_properties_add_list(props, "tiling_mode", obs_module_text("Constellations.Transition.Tiling"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Tiling.Grid"), CTILE_GRID);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.Tiling.Tight"), CTILE_TIGHT);

	p = obs_properties_add_list(props, "shape", obs_module_text("Constellations.Shape"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.Circle"), CSHAPE_CIRCLE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.Cross"), CSHAPE_CROSS);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.Square"), CSHAPE_SQUARE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.Polygon"), CSHAPE_POLYGON);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.Squircle"), CSHAPE_SQUIRCLE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.HLine"), CSHAPE_HLINE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.VLine"), CSHAPE_VLINE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.Star"), CSHAPE_STAR);
	obs_property_list_add_int(p, obs_module_text("Constellations.Shape.Heart"), CSHAPE_HEART);
	obs_property_set_modified_callback(p, prop_shape_changed);

	obs_properties_add_int_slider(props, "polygon_sides", obs_module_text("Constellations.Shape.PolySides"), 3, 24,
				      1);
	obs_properties_add_float_slider(props, "stroke_thickness",
					obs_module_text("Constellations.Shape.StrokeThickness"), 0.02, 1.0, 0.01);

	obs_properties_add_color_alpha(props, "shape_color", obs_module_text("Constellations.Shape.Color"));

	p = obs_properties_add_bool(props, "use_source_texture",
				    obs_module_text("Constellations.Transition.UseSourceTexture"));
	obs_property_set_modified_callback(p, prop_use_tex_changed);

	p = obs_properties_add_list(props, "texture_source_name",
				    obs_module_text("Constellations.Transition.TextureSource"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_STRING);
	fill_source_dropdown(p, t ? t->self : NULL);

	p = obs_properties_add_bool(props, "shape_gap_enabled", obs_module_text("Constellations.Transition.Gap"));
	obs_property_set_modified_callback(p, prop_gap_changed);
	obs_properties_add_float_slider(props, "gap_distance", obs_module_text("Constellations.Transition.GapDistance"),
					0.0, 64.0, 0.5);
	obs_properties_add_color_alpha(props, "gap_color", obs_module_text("Constellations.Transition.GapColor"));

	p = obs_properties_add_list(props, "dissolve_type", obs_module_text("Constellations.Transition.DissolveType"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.DissolveType.CropShrink"),
				  CDISSOLVE_CROP_SHRINK);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.DissolveType.ScaleShrink"),
				  CDISSOLVE_SCALE_SHRINK);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.DissolveType.Fade"), CDISSOLVE_FADE);

	p = obs_properties_add_list(props, "zoom_dir", obs_module_text("Constellations.Transition.ZoomDir"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.ZoomDir.In"), CZOOM_IN);
	obs_property_list_add_int(p, obs_module_text("Constellations.Transition.ZoomDir.Out"), CZOOM_OUT);

	obs_properties_add_float_slider(props, "fade_min", obs_module_text("Constellations.Transition.FadeMin"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(props, "fade_max", obs_module_text("Constellations.Transition.FadeMax"), 0.0,
					1.0, 0.01);

	p = obs_properties_add_bool(props, "add_logo", obs_module_text("Constellations.Logo.Add"));
	obs_property_set_modified_callback(p, prop_logo_changed);
	obs_properties_add_path(props, "logo_path", obs_module_text("Constellations.Logo.Path"), OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.bmp *.tga *.gif);;All Files (*.*)", NULL);
	p = obs_properties_add_list(props, "logo_display", obs_module_text("Constellations.Logo.Display"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Logo.Display.Fade"), CLOGO_FADE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Logo.Display.Slide"), CLOGO_SLIDE);
	obs_property_set_modified_callback(p, prop_logo_changed);
	obs_properties_add_float_slider(props, "logo_slide_angle", obs_module_text("Constellations.Logo.SlideAngle"),
					0.0, 360.0, 1.0);
	obs_properties_add_float_slider(props, "logo_scale", obs_module_text("Constellations.Logo.Scale"), 0.05, 8.0,
					0.01);

	return props;
}

static void cshape_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "transition_mode", CTRANS_MODE_COVER);
	obs_data_set_default_int(settings, "transition_type", CTRANS_TYPE_DIRECTIONAL);
	obs_data_set_default_double(settings, "directional_angle", 0.0);
	obs_data_set_default_double(settings, "anchor_x_pct", 50.0);
	obs_data_set_default_double(settings, "anchor_y_pct", 50.0);

	obs_data_set_default_int(settings, "shape", CSHAPE_CIRCLE);
	obs_data_set_default_int(settings, "polygon_sides", 6);
	obs_data_set_default_double(settings, "stroke_thickness", 0.2);
	obs_data_set_default_int(settings, "shape_color", 0xFFFFFFFF);
	obs_data_set_default_double(settings, "cell_size", 64.0);
	obs_data_set_default_int(settings, "tiling_mode", CTILE_GRID);

	obs_data_set_default_bool(settings, "use_source_texture", false);
	obs_data_set_default_string(settings, "texture_source_name", "");
	obs_data_set_default_bool(settings, "shape_gap_enabled", false);
	obs_data_set_default_double(settings, "gap_distance", 4.0);
	obs_data_set_default_int(settings, "gap_color", 0xFF000000);

	obs_data_set_default_int(settings, "dissolve_type", CDISSOLVE_CROP_SHRINK);
	obs_data_set_default_int(settings, "zoom_dir", CZOOM_IN);

	obs_data_set_default_double(settings, "fade_min", 0.0);
	obs_data_set_default_double(settings, "fade_max", 0.0);

	obs_data_set_default_bool(settings, "add_logo", false);
	obs_data_set_default_string(settings, "logo_path", "");
	obs_data_set_default_int(settings, "logo_display", CLOGO_FADE);
	obs_data_set_default_double(settings, "logo_slide_angle", 0.0);
	obs_data_set_default_double(settings, "logo_scale", 1.0);
}

static struct obs_source_info cshape_transition_info = {
	.id = "constellations_shapes_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = cshape_get_name,
	.create = cshape_create,
	.destroy = cshape_destroy,
	.update = cshape_update,
	.video_tick = cshape_tick,
	.video_render = cshape_video_render,
	.audio_render = cshape_audio_render,
	.get_properties = cshape_properties,
	.get_defaults = cshape_defaults,
};

void constellations_register_shapes_transition(void)
{
	obs_register_source(&cshape_transition_info);
}
