/*
Constellations - Text Crawl and Text Cover transitions
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* The look is in data/effects/crawl_transition.effect. The phrase and its
 * separator are laid out with Qt by Text Tiles' rasterizer, so this is only
 * built with Qt. */

#include <obs-module.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <util/threading.h>

#include "palette.h"
#include "text-tiles-raster.h"
#include "transition-common.h"

enum { FIELD_TEXT, FIELD_SEP, FIELDS };

struct crawl_info {
	struct transition_base base;

	gs_eparam_t *ep_a_tex, *ep_b_tex, *ep_text_sdf, *ep_sep_sdf, *ep_canvas, *ep_progress, *ep_easing, *ep_rows,
		*ep_bar_w, *ep_stagger, *ep_hold, *ep_direction, *ep_has_text, *ep_has_sep, *ep_text_size,
		*ep_text_rect, *ep_sep_size, *ep_sep_rect, *ep_sep_x, *ep_period, *ep_text_scale, *ep_sdf_spread,
		*ep_offset_mode, *ep_offset, *ep_seed, *ep_drift, *ep_drift_var, *ep_bar_color, *ep_text_color;

	pthread_mutex_t mutex;
	int rows, easing, direction, offset_mode;
	bool cover; /* Text Cover: whole lines slide in, hold, slide out */
	float bar_w, stagger, fill, hold, gap, offset, seed, drift, drift_var;
	uint32_t bar_rgba, text_rgba;
	char palette[64];
	/* pending is uploaded on the next frame */
	struct ctt_glyphs pending[FIELDS], glyphs[FIELDS];
	bool dirty;
	gs_texture_t *tex[FIELDS];
};

static const struct tc_param crawl_params[] = {
	TC_PARAM(struct crawl_info, ep_a_tex, "a_tex"),
	TC_PARAM(struct crawl_info, ep_b_tex, "b_tex"),
	TC_PARAM(struct crawl_info, ep_text_sdf, "text_sdf"),
	TC_PARAM(struct crawl_info, ep_sep_sdf, "sep_sdf"),
	TC_PARAM(struct crawl_info, ep_canvas, "canvas"),
	TC_PARAM(struct crawl_info, ep_progress, "progress"),
	TC_PARAM(struct crawl_info, ep_easing, "easing"),
	TC_PARAM(struct crawl_info, ep_rows, "rows"),
	TC_PARAM(struct crawl_info, ep_bar_w, "bar_w"),
	TC_PARAM(struct crawl_info, ep_stagger, "stagger"),
	TC_PARAM(struct crawl_info, ep_hold, "hold"),
	TC_PARAM(struct crawl_info, ep_direction, "direction"),
	TC_PARAM(struct crawl_info, ep_has_text, "has_text"),
	TC_PARAM(struct crawl_info, ep_has_sep, "has_sep"),
	TC_PARAM(struct crawl_info, ep_text_size, "text_size"),
	TC_PARAM(struct crawl_info, ep_text_rect, "text_rect"),
	TC_PARAM(struct crawl_info, ep_sep_size, "sep_size"),
	TC_PARAM(struct crawl_info, ep_sep_rect, "sep_rect"),
	TC_PARAM(struct crawl_info, ep_sep_x, "sep_x"),
	TC_PARAM(struct crawl_info, ep_period, "period"),
	TC_PARAM(struct crawl_info, ep_text_scale, "text_scale"),
	TC_PARAM(struct crawl_info, ep_sdf_spread, "sdf_spread"),
	TC_PARAM(struct crawl_info, ep_offset_mode, "offset_mode"),
	TC_PARAM(struct crawl_info, ep_offset, "offset"),
	TC_PARAM(struct crawl_info, ep_seed, "seed"),
	TC_PARAM(struct crawl_info, ep_drift, "drift"),
	TC_PARAM(struct crawl_info, ep_drift_var, "drift_var"),
	TC_PARAM(struct crawl_info, ep_bar_color, "bar_color"),
	TC_PARAM(struct crawl_info, ep_text_color, "text_color"),
};

static const char *crawl_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Crawl.Transition");
}

static const char *cover_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Cover.Transition");
}

static void crawl_update(void *data, obs_data_t *settings)
{
	struct crawl_info *c = data;
	obs_data_t *font = obs_data_get_obj(settings, "font");
	struct ctt_font f = {
		.face = obs_data_get_string(font, "face"),
		.style = obs_data_get_string(font, "style"),
		.size = (int)obs_data_get_int(font, "size"),
		.flags = (uint32_t)obs_data_get_int(font, "flags"),
		.letter_spacing = (float)obs_data_get_double(settings, "letter_spacing"),
	};
	bool upper = obs_data_get_bool(settings, "uppercase");
	struct ctt_glyphs g[FIELDS];
	ctt_raster_text(&f, obs_data_get_string(settings, "text"), upper, CTT_SDF_SPREAD, &g[FIELD_TEXT]);
	ctt_raster_text(&f, obs_data_get_string(settings, "separator"), upper, CTT_SDF_SPREAD, &g[FIELD_SEP]);
	obs_data_release(font);

	pthread_mutex_lock(&c->mutex);
	for (int i = 0; i < FIELDS; i++) {
		ctt_glyphs_free(&c->pending[i]);
		c->pending[i] = g[i];
	}
	c->dirty = true;
	c->rows = (int)obs_data_get_int(settings, "rows");
	c->easing = (int)obs_data_get_int(settings, "easing");
	c->direction = (int)obs_data_get_int(settings, "direction");
	c->offset_mode = (int)obs_data_get_int(settings, "offset_mode");
	c->bar_w = (float)obs_data_get_double(settings, "bar_width") / 100.0f;
	c->stagger = (float)obs_data_get_double(settings, "stagger") / 100.0f;
	c->hold = (float)obs_data_get_double(settings, "hold") / 100.0f;
	c->fill = (float)obs_data_get_double(settings, "text_fill") / 100.0f;
	c->gap = (float)obs_data_get_double(settings, "word_gap") / 100.0f;
	c->offset = (float)obs_data_get_double(settings, "row_offset") / 100.0f;
	c->seed = (float)obs_data_get_int(settings, "seed");
	c->drift = (float)obs_data_get_double(settings, "drift") / 100.0f;
	c->drift_var = (float)obs_data_get_double(settings, "drift_variation") / 100.0f;
	c->bar_rgba = (uint32_t)obs_data_get_int(settings, "bar_color") | 0xFF000000;
	c->text_rgba = (uint32_t)obs_data_get_int(settings, "text_color") | 0xFF000000;
	snprintf(c->palette, sizeof(c->palette), "%s", obs_data_get_string(settings, CPAL_KEY));
	pthread_mutex_unlock(&c->mutex);
}

static void *crawl_create(obs_data_t *settings, obs_source_t *source)
{
	struct crawl_info *c = bzalloc(sizeof(*c));
	if (!tc_base_init(&c->base, source, "crawl_transition.effect", crawl_params, TC_COUNT(crawl_params))) {
		bfree(c);
		return NULL;
	}
	pthread_mutex_init(&c->mutex, NULL);
	c->cover = strcmp(obs_source_get_id(source), "constellations_cover_transition") == 0;
	crawl_update(c, settings);
	return c;
}

static void crawl_destroy(void *data)
{
	struct crawl_info *c = data;
	obs_enter_graphics();
	for (int i = 0; i < FIELDS; i++)
		gs_texture_destroy(c->tex[i]);
	obs_leave_graphics();
	tc_base_free(&c->base);
	for (int i = 0; i < FIELDS; i++) {
		ctt_glyphs_free(&c->pending[i]);
		ctt_glyphs_free(&c->glyphs[i]);
	}
	pthread_mutex_destroy(&c->mutex);
	bfree(c);
}

static void set_field(gs_eparam_t *size, gs_eparam_t *rect, const struct ctt_glyphs *g)
{
	struct vec2 v;
	struct vec4 r;
	vec2_set(&v, g->box_w, g->box_h);
	gs_effect_set_vec2(size, &v);
	vec4_set(&r, g->tex_x, g->tex_y, g->tex_wpx, g->tex_hpx);
	gs_effect_set_vec4(rect, &r);
}

static void crawl_callback(void *data, gs_texture_t *a, gs_texture_t *b, float t, uint32_t cx, uint32_t cy)
{
	struct crawl_info *c = data;
	struct tc_frame frame;
	struct tc_color bar, text;
	struct vec2 v;
	uint32_t pal[CPAL_SLOTS];

	pthread_mutex_lock(&c->mutex);
	if (c->dirty) {
		for (int i = 0; i < FIELDS; i++) {
			gs_texture_destroy(c->tex[i]);
			ctt_glyphs_free(&c->glyphs[i]);
			c->glyphs[i] = c->pending[i];
			memset(&c->pending[i], 0, sizeof(c->pending[i]));
			const uint8_t *sdf = (const uint8_t *)c->glyphs[i].sdf;
			c->tex[i] = sdf ? gs_texture_create(c->glyphs[i].tex_w, c->glyphs[i].tex_h, GS_R16, 1, &sdf, 0)
					: NULL;
		}
		c->dirty = false;
	}
	const struct ctt_glyphs *g = &c->glyphs[FIELD_TEXT], *sep = &c->glyphs[FIELD_SEP];
	bool has_text = c->tex[FIELD_TEXT] && g->box_h > 0.0f, has_sep = has_text && c->tex[FIELD_SEP];
	/* the period: phrase, gap, then separator and another gap if there is one */
	float gap = c->gap * g->box_h, sep_x = g->box_w + gap;
	float period = has_sep ? sep_x + sep->box_w + gap : sep_x;
	float rows = (float)(c->rows < 1 ? 1 : c->rows), row_h = (float)cy / rows;
	bool has_pal = cpal_get(c->palette, pal);
	tc_color_from_rgba(&bar, has_pal ? pal[0] | 0xFF000000 : c->bar_rgba);
	tc_color_from_rgba(&text, has_pal ? pal[1] | 0xFF000000 : c->text_rgba);
	tc_frame_begin(&frame);
	tc_set_texture(&frame, c->ep_a_tex, a);
	tc_set_texture(&frame, c->ep_b_tex, b);
	gs_effect_set_texture(c->ep_text_sdf, c->tex[FIELD_TEXT]);
	gs_effect_set_texture(c->ep_sep_sdf, c->tex[FIELD_SEP]);
	tc_set_color(&frame, c->ep_bar_color, &bar);
	tc_set_color(&frame, c->ep_text_color, &text);
	vec2_set(&v, (float)cx, (float)cy);
	gs_effect_set_vec2(c->ep_canvas, &v);
	gs_effect_set_float(c->ep_progress, t);
	gs_effect_set_float(c->ep_easing, (float)c->easing);
	gs_effect_set_float(c->ep_rows, rows);
	gs_effect_set_float(c->ep_bar_w, c->cover ? (float)cx : c->bar_w * (float)cx);
	gs_effect_set_float(c->ep_hold, c->cover ? clampf(c->hold, 0.0f, 0.9f) : -1.0f);
	gs_effect_set_float(c->ep_stagger, clampf(c->stagger, 0.0f, 0.9f));
	gs_effect_set_float(c->ep_direction, (float)c->direction);
	gs_effect_set_float(c->ep_has_text, has_text ? 1.0f : 0.0f);
	gs_effect_set_float(c->ep_has_sep, has_sep ? 1.0f : 0.0f);
	set_field(c->ep_text_size, c->ep_text_rect, g);
	set_field(c->ep_sep_size, c->ep_sep_rect, sep);
	gs_effect_set_float(c->ep_sep_x, sep_x);
	gs_effect_set_float(c->ep_period, fmaxf(period, 1.0f));
	gs_effect_set_float(c->ep_text_scale, has_text ? c->fill * row_h / g->box_h : 1.0f);
	gs_effect_set_float(c->ep_sdf_spread, CTT_SDF_SPREAD);
	gs_effect_set_float(c->ep_offset_mode, (float)c->offset_mode);
	gs_effect_set_float(c->ep_offset, c->offset);
	gs_effect_set_float(c->ep_seed, c->seed);
	gs_effect_set_float(c->ep_drift, c->drift * (float)cx);
	gs_effect_set_float(c->ep_drift_var, c->drift_var);
	pthread_mutex_unlock(&c->mutex);

	while (gs_effect_loop(c->base.effect, "Crawl"))
		gs_draw_sprite(NULL, 0, cx, cy);
	tc_frame_end(&frame);
}

static void crawl_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct crawl_info *c = data;
	obs_transition_video_render(c->base.source, crawl_callback);
}

static bool offset_mode_changed(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	int mode = (int)obs_data_get_int(settings, "offset_mode");
	obs_property_set_visible(obs_properties_get(props, "row_offset"), mode == 1);
	obs_property_set_visible(obs_properties_get(props, "seed"), mode == 0);
	return true;
}

static obs_properties_t *props_common(bool cover)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;
#define T(k) obs_module_text("Crawl." k)
#define SLIDER(key, name, lo, hi, step, suffix)                                \
	p = obs_properties_add_float_slider(props, key, T(name), lo, hi, step); \
	obs_property_float_set_suffix(p, suffix)

	obs_properties_add_text(props, "text", T("Text"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(props, "uppercase", T("Uppercase"));
	obs_properties_add_font(props, "font", T("Font"));
	p = obs_properties_add_text(props, "separator", T("Separator"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(p, T("Separator.Description"));
	SLIDER("letter_spacing", "LetterSpacing", -50.0, 200.0, 0.5, " px");
	SLIDER("word_gap", "WordGap", 0.0, 300.0, 1.0, " %");
	obs_property_set_long_description(p, T("WordGap.Description"));
	obs_properties_add_int_slider(props, "rows", T("Rows"), 1, 16, 1);
	p = obs_properties_add_list(props, "offset_mode", T("RowOffset"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("RowOffset.Random"), 0);
	obs_property_list_add_int(p, T("RowOffset.Stagger"), 1);
	obs_property_list_add_int(p, T("RowOffset.None"), 2);
	obs_property_set_modified_callback(p, offset_mode_changed);
	SLIDER("row_offset", "RowOffset.Amount", 0.0, 100.0, 1.0, " %");
	obs_property_set_long_description(p, T("RowOffset.Amount.Description"));
	obs_properties_add_int(props, "seed", T("Seed"), 0, 99999, 1);
	p = obs_properties_add_list(props, "direction", T("Direction"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Direction.Right"), 0);
	obs_property_list_add_int(p, T("Direction.Left"), 1);
	obs_property_list_add_int(p, T("Direction.Alternate"), 2);
	p = obs_properties_add_list(props, "easing", obs_module_text("Tiles.Easing"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.Linear"), 0);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.InOut"), 1);
	obs_property_list_add_int(p, obs_module_text("Tiles.Easing.Out"), 2);
	SLIDER("stagger", "Stagger", 0.0, 90.0, 1.0, " %");
	obs_property_set_long_description(p, T("Stagger.Description"));
	if (cover) {
		p = obs_properties_add_float_slider(props, "hold", obs_module_text("Cover.Hold"), 0.0, 90.0, 1.0);
		obs_property_float_set_suffix(p, " %");
		obs_property_set_long_description(p, obs_module_text("Cover.Hold.Description"));
	} else {
		SLIDER("bar_width", "BarWidth", 5.0, 200.0, 1.0, " %");
		obs_property_set_long_description(p, T("BarWidth.Description"));
	}
	SLIDER("drift", "Drift", -100.0, 100.0, 1.0, " %");
	obs_property_set_long_description(p, T("Drift.Description"));
	SLIDER("drift_variation", "DriftVariation", 0.0, 100.0, 1.0, " %");
	obs_property_set_long_description(p, T("DriftVariation.Description"));
	SLIDER("text_fill", "TextFill", 10.0, 100.0, 1.0, " %");
	obs_property_set_long_description(p, T("TextFill.Description"));
	obs_properties_add_color(props, "bar_color", T("BarColor"));
	obs_properties_add_color(props, "text_color", T("TextColor"));
	p = cpal_add_list(props);
	obs_property_set_long_description(p, T("Palette.Description"));
#undef SLIDER
#undef T
	return props;
}

static obs_properties_t *crawl_properties(void *data)
{
	UNUSED_PARAMETER(data);
	return props_common(false);
}

static obs_properties_t *cover_properties(void *data)
{
	UNUSED_PARAMETER(data);
	return props_common(true);
}

static void crawl_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "text", "BE RIGHT BACK");
	obs_data_set_default_string(settings, "separator", "\xE2\x98\x85");
	/* OBS's transition properties window creates a preview clone on the
	 * same settings, which runs this again. Replacing the default font
	 * object there leaves the new one freed, so set it only once. */
	obs_data_t *font = obs_data_get_default_obj(settings, "font");
	if (!font) {
		font = obs_data_create();
		obs_data_set_default_string(font, "face", CTT_DEFAULT_FACE);
		obs_data_set_default_string(font, "style", "Bold");
		obs_data_set_default_int(font, "size", 96);
		obs_data_set_default_int(font, "flags", OBS_FONT_BOLD);
		obs_data_set_default_obj(settings, "font", font);
	}
	obs_data_release(font);
	obs_data_set_default_double(settings, "word_gap", 40.0);
	obs_data_set_default_int(settings, "rows", 4);
	obs_data_set_default_int(settings, "offset_mode", 0);
	obs_data_set_default_double(settings, "row_offset", 35.0);
	obs_data_set_default_int(settings, "seed", 1);
	obs_data_set_default_int(settings, "direction", 2);
	obs_data_set_default_int(settings, "easing", 1);
	obs_data_set_default_double(settings, "stagger", 30.0);
	obs_data_set_default_double(settings, "bar_width", 60.0);
	obs_data_set_default_double(settings, "drift", 0.0);
	obs_data_set_default_double(settings, "drift_variation", 0.0);
	obs_data_set_default_double(settings, "text_fill", 60.0);
	obs_data_set_default_int(settings, "bar_color", 0xFF3A1A14); /* #141A3A */
	obs_data_set_default_int(settings, "text_color", 0xFFFFFFFF);
}

static void cover_defaults(obs_data_t *settings)
{
	crawl_defaults(settings);
	obs_data_set_default_int(settings, "rows", 3);
	obs_data_set_default_double(settings, "stagger", 20.0);
	obs_data_set_default_double(settings, "hold", 35.0);
	obs_data_set_default_double(settings, "drift", 15.0);
	obs_data_set_default_double(settings, "drift_variation", 40.0);
}

struct obs_source_info crawl_transition = {
	.id = "constellations_crawl_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = crawl_get_name,
	.create = crawl_create,
	.destroy = crawl_destroy,
	.update = crawl_update,
	.video_render = crawl_video_render,
	.audio_render = tc_audio_render,
	.get_properties = crawl_properties,
	.get_defaults = crawl_defaults,
	.video_get_color_space = tc_video_get_color_space,
};

struct obs_source_info cover_transition = {
	.id = "constellations_cover_transition",
	.type = OBS_SOURCE_TYPE_TRANSITION,
	.get_name = cover_get_name,
	.create = crawl_create,
	.destroy = crawl_destroy,
	.update = crawl_update,
	.video_render = crawl_video_render,
	.audio_render = tc_audio_render,
	.get_properties = cover_properties,
	.get_defaults = cover_defaults,
	.video_get_color_space = tc_video_get_color_space,
};
