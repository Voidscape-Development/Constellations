/*
Constellations - Text Tiles Background / Filter
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>
#include <math.h>
#include <string.h>
#include <wctype.h>

#include "shape-defs.h"
#include "common.h"
#include "text-tiles-math.h"

/* A phrase repeated along rows that each slide sideways at their own speed.
 *
 * The letters are drawn by a private OBS text source (GDI+ on Windows,
 * FreeType elsewhere), so any installed font works through the standard font
 * picker. Every frame that source is rendered into an alpha mask, and one
 * full-canvas pass of text_tiles.effect repeats the mask along every row.
 * The rows' positions are integrated here in double precision and handed to
 * the shader one texel per row. */

enum ctt_fill_mode {
	CTT_FILL_SOLID = 0,
	CTT_FILL_TONE = 1,
	CTT_FILL_GRADIENT = 2,
};

enum ctt_gradient_span {
	CTT_SPAN_CANVAS = 0,
	CTT_SPAN_WORD = 1,
};

enum {
	P_CANVAS_SIZE,
	P_ROT_CS,
	P_ROW_PITCH,
	P_ROW_FIRST,
	P_ROW_COUNT,
	P_ROW_TEX_W,
	P_TEXT_SIZE,
	P_SEP_SIZE,
	P_SEP_POS,
	P_PERIOD,
	P_HAS_SEP,
	P_OUTLINE_PX,
	P_FILL_MODE,
	P_TEXT_COLOR,
	P_TEXT_COLOR2,
	P_GRAD_SPAN,
	P_GRAD_DIR,
	P_TONE_AMOUNT,
	P_BG_COLOR,
	P_BG_COLOR2,
	P_BG_TYPE,
	P_BG_DIR,
	P_TEXT_MASK,
	P_SEP_MASK,
	P_ROW_OFFSETS,
	P_COUNT,
};

static const char *const param_names[] = {
	"canvas_size", "rot_cs",    "row_pitch", "row_first",   "row_count",  "row_tex_w", "text_size",
	"sep_size",    "sep_pos",   "period",    "has_sep",     "outline_px", "fill_mode", "text_color",
	"text_color2", "grad_span", "grad_dir",  "tone_amount", "bg_color",   "bg_color2", "bg_type",
	"bg_dir",      "text_mask", "sep_mask",  "row_offsets",
};
_Static_assert(OBS_COUNTOF(param_names) == P_COUNT, "param_names must list every P_ uniform");

/* Text sources to draw the letters with, best first. */
static const char *const text_source_ids[] = {
	"text_gdiplus_v3",
	"text_gdiplus_v2",
	"text_ft2_source_v2",
};

#if defined(_WIN32)
#define CTT_DEFAULT_FACE "Arial"
#elif defined(__APPLE__)
#define CTT_DEFAULT_FACE "Helvetica"
#else
#define CTT_DEFAULT_FACE "Sans Serif"
#endif

struct ctt_config {
	uint32_t width, height;

	bool has_sep;
	float word_gap;     /* px between a phrase and the separator (or next phrase) */
	float line_spacing; /* px added between rows; negative pulls them together */

	int fill_mode;
	struct vec4 color, color2; /* sRGB-encoded, straight alpha, opacity applied */
	int grad_span;
	float grad_angle_deg;
	float tone_amount; /* -1..1: negative darkens, positive lightens */
	float outline_px;  /* 0 = filled letters */

	float rotation_deg;
	struct ctt_motion motion;
	double stagger;
	bool random_offset;

	int bg_type;
	struct vec4 bg_color, bg_color2; /* sRGB-encoded, straight alpha */
	float bg_angle_deg;
	bool has_bg;
};

/* This frame's text layout, worked out before drawing. */
struct ctt_frame {
	gs_texture_t *text_tex, *sep_tex;
	float pitch, first, count;
	float text_w, text_h, sep_w, sep_h, sep_x, period;
	bool has_sep;
};

struct ctt_source {
	obs_source_t *self;
	bool is_filter;
	struct cfx_effect fx;

	/* Created once; their settings change from update() on the UI thread,
	 * and OBS applies that on their next tick. */
	obs_source_t *text_src;
	obs_source_t *sep_src;
	bool text_is_ft2;
	char *text_applied, *sep_applied; /* UI thread */

	pthread_mutex_t mutex;
	struct ctt_config pending;
	bool has_pending;
	volatile long width, height;
	volatile long reset_motion;

	/* Graphics thread. */
	struct ctt_config cfg;
	double clock;
	double last_period;
	struct ctt_frame frame;
	double *row_pos; /* px each row has moved right, index row + CTT_ROW_HALF */
	float *row_buf;
	gs_texture_t *row_tex;
	gs_texrender_t *text_tr;
	gs_texrender_t *sep_tr;
	/* Tone-on-tone blends with what is beneath the text, so it is drawn
	 * with its base into this first; otherwise it would tint whatever the
	 * scene has behind a transparent background. */
	gs_texrender_t *tone_tr;
	enum gs_color_format tone_format;
};

static const char *ctt_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.TextTilesSource.Name");
}

static const char *ctt_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.TextTilesFilter.Name");
}

/* ------------------------------------------------------------ text sources */

static const char *find_text_source_id(void)
{
	for (size_t i = 0; i < OBS_COUNTOF(text_source_ids); ++i)
		if (obs_source_get_display_name(text_source_ids[i]))
			return text_source_ids[i];
	return NULL;
}

/* UTF-8 uppercase, or a copy when `upper` is false. bfree() the result. */
static char *text_transform(const char *in, bool upper)
{
	if (!in)
		in = "";
	if (!upper)
		return bstrdup(in);
	/* Allocates even for an empty string, returning length 0. */
	wchar_t *w = NULL;
	os_utf8_to_wcs_ptr(in, 0, &w);
	if (!w)
		return bstrdup(in);
	for (wchar_t *c = w; *c; ++c)
		*c = (wchar_t)towupper((wint_t)*c);
	char *out = NULL;
	os_wcs_to_utf8_ptr(w, 0, &out);
	bfree(w);
	return out ? out : bstrdup(in);
}

static bool is_blank(const char *s)
{
	for (; s && *s; ++s)
		if (*s != ' ' && *s != '\t')
			return false;
	return true;
}

/* White, unadorned, single-line text: only its coverage is used. `last`
 * remembers what was applied, so changing an unrelated setting (dragging the
 * speed slider, say) does not make OBS lay the text out again. */
static void text_source_set(struct ctt_source *s, obs_source_t *src, char **last, obs_data_t *font, const char *text)
{
	if (!src)
		return;
	obs_data_t *d = obs_data_create();
	obs_data_set_obj(d, "font", font);
	obs_data_set_string(d, "text", text);
	obs_data_set_bool(d, "antialiasing", true);
	obs_data_set_bool(d, "outline", false);
	if (s->text_is_ft2) {
		obs_data_set_int(d, "color1", 0xFFFFFFFF);
		obs_data_set_int(d, "color2", 0xFFFFFFFF);
		obs_data_set_bool(d, "drop_shadow", false);
		obs_data_set_bool(d, "word_wrap", false);
		obs_data_set_bool(d, "from_file", false);
	} else {
		obs_data_set_int(d, "color", 0xFFFFFF);
		obs_data_set_int(d, "opacity", 100);
		obs_data_set_bool(d, "gradient", false);
		obs_data_set_int(d, "bk_opacity", 0);
		obs_data_set_bool(d, "extents", false);
		obs_data_set_bool(d, "read_from_file", false);
		obs_data_set_bool(d, "vertical", false);
	}
	const char *json = obs_data_get_json(d);
	if (!*last || strcmp(*last, json) != 0) {
		bfree(*last);
		*last = bstrdup(json);
		obs_source_update(src, d);
	}
	obs_data_release(d);
}

/* ------------------------------------------------------------ settings */

static void ctt_update(void *data, obs_data_t *settings)
{
	struct ctt_source *s = data;
	struct ctt_config c = {0};

	if (!s->is_filter) {
		c.width = (uint32_t)obs_data_get_int(settings, "width");
		c.height = (uint32_t)obs_data_get_int(settings, "height");
		if (c.width == 0)
			c.width = 1920;
		if (c.height == 0)
			c.height = 1080;
	}

	bool upper = obs_data_get_bool(settings, "uppercase");
	/* The font picker's value, with every field written out: OBS copies
	 * only explicitly set values into the text source's settings, so
	 * fields that are just defaults would be lost on the way. */
	obs_data_t *picked = obs_data_get_obj(settings, "font");
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", obs_data_get_string(picked, "face"));
	obs_data_set_string(font, "style", obs_data_get_string(picked, "style"));
	obs_data_set_int(font, "size", obs_data_get_int(picked, "size"));
	obs_data_set_int(font, "flags", obs_data_get_int(picked, "flags"));
	obs_data_release(picked);
	char *text = text_transform(obs_data_get_string(settings, "text"), upper);
	char *sep = text_transform(obs_data_get_string(settings, "separator"), upper);
	c.has_sep = !is_blank(sep);
	text_source_set(s, s->text_src, &s->text_applied, font, text);
	text_source_set(s, s->sep_src, &s->sep_applied, font, c.has_sep ? sep : "");
	bfree(text);
	bfree(sep);
	obs_data_release(font);

	c.word_gap = (float)obs_data_get_double(settings, "word_gap");
	if (c.word_gap < 0.0f)
		c.word_gap = 0.0f;
	c.line_spacing = (float)obs_data_get_double(settings, "line_spacing");

	c.fill_mode = (int)obs_data_get_int(settings, "fill_mode");
	float opacity = (float)obs_data_get_double(settings, "opacity") / 100.0f;
	vec4_from_rgba(&c.color, (uint32_t)obs_data_get_int(settings, "text_color"));
	vec4_from_rgba(&c.color2, (uint32_t)obs_data_get_int(settings, "text_color2"));
	c.color.w *= opacity;
	c.color2.w *= opacity;
	c.grad_span = (int)obs_data_get_int(settings, "gradient_span");
	c.grad_angle_deg = (float)obs_data_get_double(settings, "gradient_angle");
	c.tone_amount = (float)obs_data_get_double(settings, "tone_amount") / 100.0f;
	c.outline_px = obs_data_get_bool(settings, "outline_only")
			       ? (float)obs_data_get_double(settings, "outline_width")
			       : 0.0f;

	c.rotation_deg = (float)obs_data_get_double(settings, "rotation");
	c.motion.speed = obs_data_get_double(settings, "speed");
	c.motion.direction = (int)obs_data_get_int(settings, "direction");
	c.motion.variation = obs_data_get_double(settings, "speed_variation");
	c.motion.wander = obs_data_get_bool(settings, "speed_wander");
	c.motion.wander_amount = obs_data_get_double(settings, "wander_amount");
	c.motion.wander_rate = obs_data_get_double(settings, "wander_rate");
	c.motion.seed = (uint32_t)obs_data_get_int(settings, "seed");
	c.stagger = obs_data_get_double(settings, "stagger");
	c.random_offset = obs_data_get_bool(settings, "random_offset");

	if (!s->is_filter) {
		c.bg_type = (int)obs_data_get_int(settings, "background_type");
		vec4_from_rgba(&c.bg_color, (uint32_t)obs_data_get_int(settings, "background_color"));
		vec4_from_rgba(&c.bg_color2, (uint32_t)obs_data_get_int(settings, "background_color2"));
		c.bg_angle_deg = (float)obs_data_get_double(settings, "background_angle");
		c.has_bg = c.bg_type != CBG_NONE &&
			   (c.bg_color.w > 0.0001f || (c.bg_type != CBG_SOLID && c.bg_color2.w > 0.0001f));
	}

	os_atomic_set_long(&s->width, (long)c.width);
	os_atomic_set_long(&s->height, (long)c.height);
	pthread_mutex_lock(&s->mutex);
	s->pending = c;
	s->has_pending = true;
	pthread_mutex_unlock(&s->mutex);
}

/* ------------------------------------------------------------ lifetime */

static void *ctt_create_common(obs_data_t *settings, obs_source_t *source, bool is_filter)
{
	struct ctt_source *s = bzalloc(sizeof(*s));
	s->self = source;
	s->is_filter = is_filter;
	pthread_mutex_init_value(&s->mutex);
	if (pthread_mutex_init(&s->mutex, NULL) != 0) {
		bfree(s);
		return NULL;
	}
	s->row_pos = bzalloc(sizeof(double) * CTT_ROW_SLOTS);
	s->row_buf = bzalloc(sizeof(float) * CTT_ROW_SLOTS);

	const char *id = find_text_source_id();
	if (id) {
		s->text_is_ft2 = strncmp(id, "text_ft2", 8) == 0;
		s->text_src = obs_source_create_private(id, "constellations-text-tiles-phrase", NULL);
		s->sep_src = obs_source_create_private(id, "constellations-text-tiles-separator", NULL);
	} else {
		obs_log(LOG_WARNING, "Text Tiles: no OBS text source is available, so only the background is drawn");
	}

	if (cfx_effect_load(&s->fx, "text_tiles.effect", param_names, P_COUNT)) {
		obs_enter_graphics();
		s->text_tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->sep_tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		s->row_tex = gs_texture_create(CTT_ROW_SLOTS, 1, GS_R32F, 1, NULL, GS_DYNAMIC);
		obs_leave_graphics();
	}
	ctt_update(s, settings);
	return s;
}

static void *ctt_source_create(obs_data_t *settings, obs_source_t *source)
{
	return ctt_create_common(settings, source, false);
}

static void *ctt_filter_create(obs_data_t *settings, obs_source_t *source)
{
	return ctt_create_common(settings, source, true);
}

static void ctt_destroy(void *data)
{
	struct ctt_source *s = data;
	obs_source_release(s->text_src);
	obs_source_release(s->sep_src);
	obs_enter_graphics();
	gs_texrender_destroy(s->text_tr);
	gs_texrender_destroy(s->sep_tr);
	gs_texrender_destroy(s->tone_tr);
	gs_texture_destroy(s->row_tex);
	obs_leave_graphics();
	cfx_effect_free(&s->fx);
	bfree(s->text_applied);
	bfree(s->sep_applied);
	bfree(s->row_pos);
	bfree(s->row_buf);
	pthread_mutex_destroy(&s->mutex);
	bfree(s);
}

/* ------------------------------------------------------------ motion */

static void ctt_tick(void *data, float seconds)
{
	struct ctt_source *s = data;
	pthread_mutex_lock(&s->mutex);
	if (s->has_pending) {
		s->cfg = s->pending;
		s->has_pending = false;
	}
	pthread_mutex_unlock(&s->mutex);

	if (os_atomic_set_long(&s->reset_motion, 0)) {
		memset(s->row_pos, 0, sizeof(double) * CTT_ROW_SLOTS);
		s->clock = 0.0;
	}

	const struct ctt_motion *m = &s->cfg.motion;
	if (m->speed == 0.0)
		return;
	/* Integrated rather than computed from the clock, so changing a speed
	 * steers the rows instead of making them jump. */
	double dt = (double)seconds;
	s->clock += dt;
	for (int i = 0; i < CTT_ROW_SLOTS; ++i) {
		double pos = s->row_pos[i] + ctt_row_speed(m, i - CTT_ROW_HALF, s->clock) * dt;
		/* Wrapping by the period changes nothing on screen and keeps
		 * the value small and precise. */
		s->row_pos[i] = s->last_period > 0.0 ? ctt_wrap(pos, s->last_period) : pos;
	}
}

/* ------------------------------------------------------------ rendering */

/* Renders a text source into its alpha mask. */
static gs_texture_t *render_mask(gs_texrender_t *tr, obs_source_t *src, uint32_t *w, uint32_t *h)
{
	*w = src ? obs_source_get_width(src) : 0;
	*h = src ? obs_source_get_height(src) : 0;
	if (!tr || *w == 0 || *h == 0 || *w > 16384 || *h > 16384)
		return NULL;
	gs_texrender_reset(tr);
	if (!gs_texrender_begin(tr, *w, *h))
		return NULL;
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)*w, 0.0f, (float)*h, -100.0f, 100.0f);
	/* OBS's standard blend, under which both text sources leave alpha
	 * equal to the letters' coverage. */
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	obs_source_video_render(src);
	gs_blend_state_pop();
	gs_texrender_end(tr);
	return gs_texrender_get_texture(tr);
}

/* Renders the text masks and works out this frame's row layout into
 * s->frame. False when there is no text to draw. */
static bool ctt_prepare(struct ctt_source *s, uint32_t width, uint32_t height)
{
	const struct ctt_config *c = &s->cfg;
	struct ctt_frame *f = &s->frame;
	memset(f, 0, sizeof(*f));

	uint32_t tw, th, sw = 0, sh = 0;
	f->text_tex = s->row_tex ? render_mask(s->text_tr, s->text_src, &tw, &th) : NULL;
	if (!f->text_tex)
		return false;
	f->sep_tex = c->has_sep ? render_mask(s->sep_tr, s->sep_src, &sw, &sh) : NULL;
	bool has_sep = f->sep_tex != NULL;

	double pitch = (double)th + (double)c->line_spacing;
	if (pitch < 0.5 * (double)th)
		pitch = 0.5 * (double)th;
	if (pitch < 1.0)
		pitch = 1.0;
	double gap = (double)c->word_gap;
	double sep_x = (double)tw + gap;
	double period = sep_x + (has_sep ? (double)sw + gap : 0.0);
	if (period < 1.0)
		period = 1.0;
	s->last_period = period;

	int first;
	int count = ctt_row_range((double)width, (double)height, pitch, (double)th, &first);
	if (count <= 0)
		return false;
	for (int i = 0; i < count; ++i) {
		int row = first + i;
		double pos = s->row_pos[row + CTT_ROW_HALF] +
			     ctt_row_start(row, c->stagger, c->random_offset, c->motion.seed, period);
		s->row_buf[i] = (float)ctt_wrap(pos, period);
	}
	gs_texture_set_image(s->row_tex, (const uint8_t *)s->row_buf, sizeof(float) * CTT_ROW_SLOTS, false);

	f->pitch = (float)pitch;
	f->first = (float)first;
	f->count = (float)count;
	f->text_w = (float)tw;
	f->text_h = (float)th;
	f->sep_w = (float)sw;
	f->sep_h = (float)sh;
	f->sep_x = (float)sep_x;
	f->period = (float)period;
	f->has_sep = has_sep;
	return true;
}

/* Uniforms are set right before each technique is drawn: ending a technique
 * clears every parameter of the effect. */
static void ctt_draw_background(struct ctt_source *s, uint32_t width, uint32_t height)
{
	const struct ctt_config *c = &s->cfg;
	const struct cfx_effect *fx = &s->fx;
	if (s->is_filter || !c->has_bg)
		return;
	double a = (double)c->bg_angle_deg * (M_PI / 180.0);
	cfx_set_vec2(fx, P_CANVAS_SIZE, (float)width, (float)height);
	cfx_set_vec4(fx, P_BG_COLOR, &c->bg_color);
	cfx_set_vec4(fx, P_BG_COLOR2, &c->bg_color2);
	cfx_set_float(fx, P_BG_TYPE, (float)c->bg_type);
	cfx_set_vec2(fx, P_BG_DIR, (float)cos(a), (float)sin(a));
	while (gs_effect_loop(fx->effect, "Background"))
		gs_draw_sprite(NULL, 0, width, height);
}

/* Draws the prepared text, within cfx_draw_begin/end. */
static void ctt_draw_text(struct ctt_source *s, uint32_t width, uint32_t height)
{
	const struct ctt_config *c = &s->cfg;
	const struct ctt_frame *f = &s->frame;
	const struct cfx_effect *fx = &s->fx;

	double rot = (double)c->rotation_deg * (M_PI / 180.0);
	double ga = (double)c->grad_angle_deg * (M_PI / 180.0);
	cfx_set_vec2(fx, P_CANVAS_SIZE, (float)width, (float)height);
	cfx_set_vec2(fx, P_ROT_CS, (float)cos(rot), (float)sin(rot));
	cfx_set_float(fx, P_ROW_PITCH, f->pitch);
	cfx_set_float(fx, P_ROW_FIRST, f->first);
	cfx_set_float(fx, P_ROW_COUNT, f->count);
	cfx_set_float(fx, P_ROW_TEX_W, (float)CTT_ROW_SLOTS);
	cfx_set_vec2(fx, P_TEXT_SIZE, f->text_w, f->text_h);
	cfx_set_vec2(fx, P_SEP_SIZE, f->sep_w, f->sep_h);
	cfx_set_vec2(fx, P_SEP_POS, f->sep_x, 0.5f * (f->text_h - f->sep_h));
	cfx_set_float(fx, P_PERIOD, f->period);
	cfx_set_float(fx, P_HAS_SEP, f->has_sep ? 1.0f : 0.0f);
	cfx_set_float(fx, P_OUTLINE_PX, c->outline_px);
	cfx_set_float(fx, P_FILL_MODE, (float)c->fill_mode);
	cfx_set_vec4(fx, P_TEXT_COLOR, &c->color);
	cfx_set_vec4(fx, P_TEXT_COLOR2, &c->color2);
	cfx_set_float(fx, P_GRAD_SPAN, (float)c->grad_span);
	cfx_set_vec2(fx, P_GRAD_DIR, (float)cos(ga), (float)sin(ga));
	cfx_set_float(fx, P_TONE_AMOUNT, fabsf(c->tone_amount));
	if (fx->params[P_TEXT_MASK])
		gs_effect_set_texture(fx->params[P_TEXT_MASK], f->text_tex);
	if (fx->params[P_SEP_MASK])
		gs_effect_set_texture(fx->params[P_SEP_MASK], f->has_sep ? f->sep_tex : f->text_tex);
	if (fx->params[P_ROW_OFFSETS])
		gs_effect_set_texture(fx->params[P_ROW_OFFSETS], s->row_tex);

	gs_blend_state_push();
	if (c->fill_mode == CTT_FILL_TONE) {
		/* The shader outputs amount * coverage in rgb. Darken scales
		 * what is there down; lighten moves premultiplied color toward
		 * its own alpha (a screen blend that keeps alpha valid). Alpha
		 * is left as it is. */
		if (c->tone_amount < 0.0f)
			gs_blend_function_separate(GS_BLEND_ZERO, GS_BLEND_INVSRCCOLOR, GS_BLEND_ZERO, GS_BLEND_ONE);
		else
			gs_blend_function_separate(GS_BLEND_DSTALPHA, GS_BLEND_INVSRCCOLOR, GS_BLEND_ZERO,
						   GS_BLEND_ONE);
	}
	while (gs_effect_loop(fx->effect, "Text"))
		gs_draw_sprite(NULL, 0, width, height);
	gs_blend_state_pop();
}

/* Renders into tone_tr, at `format`; the base is the background, or the
 * filtered source when `filter_base` is set. Returns the texture or NULL.
 * The filtered source is drawn exactly once either way: straight to the
 * output, untoned, if the texrender cannot be used. */
static gs_texture_t *ctt_render_tone(struct ctt_source *s, uint32_t width, uint32_t height, enum gs_color_format format,
				     bool filter_base)
{
	if (!s->tone_tr || s->tone_format != format) {
		gs_texrender_destroy(s->tone_tr);
		s->tone_tr = gs_texrender_create(format, GS_ZS_NONE);
		s->tone_format = format;
	}
	if (s->tone_tr)
		gs_texrender_reset(s->tone_tr);
	if (!s->tone_tr || !gs_texrender_begin(s->tone_tr, width, height)) {
		if (filter_base)
			obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), width, height);
		return NULL;
	}
	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	if (filter_base)
		obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), width, height);
	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	ctt_draw_background(s, width, height);
	ctt_draw_text(s, width, height);
	cfx_draw_end(&st);
	gs_texrender_end(s->tone_tr);
	return gs_texrender_get_texture(s->tone_tr);
}

/* Draws a premultiplied linear texture, sRGB-encoded when 8-bit. */
static void blit(gs_texture_t *tex, uint32_t width, uint32_t height)
{
	gs_effect_t *def = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	gs_effect_set_texture_srgb(gs_effect_get_param_by_name(def, "image"), tex);
	while (gs_effect_loop(def, "Draw"))
		gs_draw_sprite(tex, 0, width, height);
	cfx_draw_end(&st);
}

static void ctt_source_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct ctt_source *s = data;
	uint32_t width = s->cfg.width, height = s->cfg.height;
	if (!s->fx.effect || width == 0 || height == 0)
		return;
	bool text = ctt_prepare(s, width, height);

	if (text && s->cfg.fill_mode == CTT_FILL_TONE) {
		gs_texture_t *tex = ctt_render_tone(s, width, height, GS_RGBA, false);
		if (tex)
			blit(tex, width, height);
		return;
	}
	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	ctt_draw_background(s, width, height);
	if (text)
		ctt_draw_text(s, width, height);
	cfx_draw_end(&st);
}

static void ctt_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct ctt_source *s = data;
	obs_source_t *target = obs_filter_get_target(s->self);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0 || !s->fx.effect) {
		obs_source_skip_video_filter(s->self);
		return;
	}
	bool text = ctt_prepare(s, w, h);
	if (!cfx_filter_begin(s->self, OBS_ALLOW_DIRECT_RENDERING))
		return;

	if (text && s->cfg.fill_mode == CTT_FILL_TONE) {
		enum gs_color_format format = gs_get_format_from_space(cfx_filter_color_space(s->self));
		gs_texture_t *tex = ctt_render_tone(s, w, h, format, true);
		if (tex)
			blit(tex, w, h);
		return;
	}
	obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), w, h);
	if (text) {
		struct cfx_draw_state st;
		cfx_draw_begin(&st);
		ctt_draw_text(s, w, h);
		cfx_draw_end(&st);
	}
}

static enum gs_color_space ctt_filter_color_space(void *data, size_t count, const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);
	struct ctt_source *s = data;
	return cfx_filter_color_space(s->self);
}

static uint32_t ctt_width(void *data)
{
	struct ctt_source *s = data;
	return (uint32_t)os_atomic_load_long(&s->width);
}

static uint32_t ctt_height(void *data)
{
	struct ctt_source *s = data;
	return (uint32_t)os_atomic_load_long(&s->height);
}

/* ------------------------------------------------------------ properties */

static bool ctt_visibility(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	int fill = (int)obs_data_get_int(settings, "fill_mode");
	bool gradient = fill == CTT_FILL_GRADIENT;
	bool tone = fill == CTT_FILL_TONE;
	bool canvas_span = obs_data_get_int(settings, "gradient_span") == CTT_SPAN_CANVAS;
	int bg = (int)obs_data_get_int(settings, "background_type");
	bool changed = false;
	changed |= cfx_set_visible(props, "text_color", !tone);
	changed |= cfx_set_visible(props, "text_color2", gradient);
	changed |= cfx_set_visible(props, "gradient_span", gradient);
	changed |= cfx_set_visible(props, "gradient_angle", gradient && canvas_span);
	changed |= cfx_set_visible(props, "opacity", !tone);
	changed |= cfx_set_visible(props, "tone_amount", tone);
	changed |= cfx_set_visible(props, "outline_width", obs_data_get_bool(settings, "outline_only"));
	bool wander = obs_data_get_bool(settings, "speed_wander");
	changed |= cfx_set_visible(props, "wander_amount", wander);
	changed |= cfx_set_visible(props, "wander_rate", wander);
	changed |= cfx_set_visible(props, "background_color", bg != CBG_NONE);
	changed |= cfx_set_visible(props, "background_color2", bg == CBG_RADIAL || bg == CBG_LINEAR);
	changed |= cfx_set_visible(props, "background_angle", bg == CBG_LINEAR);
	return changed;
}

static bool ctt_reset_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	struct ctt_source *s = data;
	if (s)
		os_atomic_set_long(&s->reset_motion, 1);
	return false;
}

#define T(key) obs_module_text("Constellations.TextTiles." key)

static obs_properties_t *ctt_props_common(void *data, bool is_filter)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	if (!is_filter) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	}

	obs_properties_t *text = obs_properties_create();
	obs_properties_add_text(text, "text", T("Text"), OBS_TEXT_DEFAULT);
	obs_properties_add_bool(text, "uppercase", T("Uppercase"));
	obs_properties_add_font(text, "font", T("Font"));
	p = obs_properties_add_text(text, "separator", T("Separator"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(p, T("Separator.Tooltip"));
	p = obs_properties_add_float_slider(text, "word_gap", T("WordGap"), 0.0, 1000.0, 1.0);
	obs_property_float_set_suffix(p, " px");
	p = obs_properties_add_float_slider(text, "line_spacing", T("LineSpacing"), -500.0, 1000.0, 1.0);
	obs_property_float_set_suffix(p, " px");
	obs_property_set_long_description(p, T("LineSpacing.Tooltip"));
	obs_properties_add_group(props, "text_group", T("Group.Text"), OBS_GROUP_NORMAL, text);

	obs_properties_t *style = obs_properties_create();
	p = obs_properties_add_list(style, "fill_mode", T("Fill"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Fill.Solid"), CTT_FILL_SOLID);
	obs_property_list_add_int(p, T("Fill.Tone"), CTT_FILL_TONE);
	obs_property_list_add_int(p, T("Fill.Gradient"), CTT_FILL_GRADIENT);
	obs_property_set_modified_callback(p, ctt_visibility);
	obs_properties_add_color(style, "text_color", T("Color"));
	obs_properties_add_color(style, "text_color2", T("Color2"));
	p = obs_properties_add_list(style, "gradient_span", T("GradientSpan"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("GradientSpan.Canvas"), CTT_SPAN_CANVAS);
	obs_property_list_add_int(p, T("GradientSpan.Word"), CTT_SPAN_WORD);
	obs_property_set_modified_callback(p, ctt_visibility);
	obs_properties_add_float_slider(style, "gradient_angle", T("GradientAngle"), -180.0, 180.0, 1.0);
	p = obs_properties_add_float_slider(style, "opacity", T("Opacity"), 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, "%");
	p = obs_properties_add_float_slider(style, "tone_amount", T("ToneAmount"), -100.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, "%");
	obs_property_set_long_description(p, is_filter ? T("ToneAmount.FilterTooltip") : T("ToneAmount.Tooltip"));
	p = obs_properties_add_bool(style, "outline_only", T("OutlineOnly"));
	obs_property_set_modified_callback(p, ctt_visibility);
	p = obs_properties_add_float_slider(style, "outline_width", T("OutlineWidth"), 1.0, 20.0, 0.5);
	obs_property_float_set_suffix(p, " px");
	obs_properties_add_group(props, "style_group", T("Group.Style"), OBS_GROUP_NORMAL, style);

	obs_properties_t *rows = obs_properties_create();
	obs_properties_add_float_slider(rows, "rotation", T("Rotation"), -180.0, 180.0, 1.0);
	p = obs_properties_add_float_slider(rows, "speed", T("Speed"), -1000.0, 1000.0, 1.0);
	obs_property_float_set_suffix(p, " px/s");
	p = obs_properties_add_list(rows, "direction", T("Direction"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Direction.Same"), CTT_DIR_SAME);
	obs_property_list_add_int(p, T("Direction.Alternate"), CTT_DIR_ALTERNATE);
	obs_property_list_add_int(p, T("Direction.Random"), CTT_DIR_RANDOM);
	p = obs_properties_add_float_slider(rows, "speed_variation", T("SpeedVariation"), 0.0, 1.0, 0.01);
	obs_property_set_long_description(p, T("SpeedVariation.Tooltip"));
	p = obs_properties_add_bool(rows, "speed_wander", T("SpeedWander"));
	obs_property_set_long_description(p, T("SpeedWander.Tooltip"));
	obs_property_set_modified_callback(p, ctt_visibility);
	obs_properties_add_float_slider(rows, "wander_amount", T("WanderAmount"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(rows, "wander_rate", T("WanderRate"), 0.01, 2.0, 0.01);
	p = obs_properties_add_float_slider(rows, "stagger", T("Stagger"), 0.0, 1.0, 0.01);
	obs_property_set_long_description(p, T("Stagger.Tooltip"));
	obs_properties_add_bool(rows, "random_offset", T("RandomOffset"));
	p = obs_properties_add_int(rows, "seed", T("Seed"), 0, 100000, 1);
	obs_property_set_long_description(p, T("Seed.Tooltip"));
	obs_properties_add_button2(rows, "motion_reset", obs_module_text("Constellations.Motion.Reset"),
				   ctt_reset_clicked, data);
	obs_properties_add_group(props, "rows_group", T("Group.Rows"), OBS_GROUP_NORMAL, rows);

	if (!is_filter) {
		obs_properties_t *bg = obs_properties_create();
		p = obs_properties_add_list(bg, "background_type", obs_module_text("Constellations.Background.Type"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("Constellations.Background.Type.Solid"), CBG_SOLID);
		obs_property_list_add_int(p, obs_module_text("Constellations.Background.Type.Linear"), CBG_LINEAR);
		obs_property_list_add_int(p, obs_module_text("Constellations.Background.Type.Radial"), CBG_RADIAL);
		obs_property_list_add_int(p, obs_module_text("Constellations.Background.Type.None"), CBG_NONE);
		obs_property_set_modified_callback(p, ctt_visibility);
		obs_properties_add_color_alpha(bg, "background_color",
					       obs_module_text("Constellations.Canvas.BackgroundColor"));
		obs_properties_add_color_alpha(bg, "background_color2",
					       obs_module_text("Constellations.Background.Color2"));
		obs_properties_add_float_slider(bg, "background_angle",
						obs_module_text("Constellations.Background.Angle"), -180.0, 180.0, 1.0);
		obs_properties_add_group(props, "background_group", T("Group.Background"), OBS_GROUP_NORMAL, bg);
	}

	return props;
}

#undef T

static obs_properties_t *ctt_source_props(void *data)
{
	return ctt_props_common(data, false);
}

static obs_properties_t *ctt_filter_props(void *data)
{
	return ctt_props_common(data, true);
}

static void ctt_defaults_common(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "text", "TEXT");
	obs_data_set_default_bool(settings, "uppercase", false);
	obs_data_t *font = obs_data_create();
	obs_data_set_default_string(font, "face", CTT_DEFAULT_FACE);
	obs_data_set_default_string(font, "style", "Bold");
	obs_data_set_default_int(font, "size", 96);
	obs_data_set_default_int(font, "flags", OBS_FONT_BOLD);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);
	obs_data_set_default_string(settings, "separator", "");
	obs_data_set_default_double(settings, "word_gap", 40.0);
	obs_data_set_default_double(settings, "line_spacing", 0.0);

	obs_data_set_default_int(settings, "fill_mode", CTT_FILL_SOLID);
	obs_data_set_default_int(settings, "text_color", 0xFFFFFFFF);
	obs_data_set_default_int(settings, "text_color2", 0xFF808080);
	obs_data_set_default_int(settings, "gradient_span", CTT_SPAN_CANVAS);
	obs_data_set_default_double(settings, "gradient_angle", 0.0);
	obs_data_set_default_double(settings, "opacity", 20.0);
	obs_data_set_default_double(settings, "tone_amount", -20.0);
	obs_data_set_default_bool(settings, "outline_only", false);
	obs_data_set_default_double(settings, "outline_width", 3.0);

	obs_data_set_default_double(settings, "rotation", 0.0);
	obs_data_set_default_double(settings, "speed", 40.0);
	obs_data_set_default_int(settings, "direction", CTT_DIR_SAME);
	obs_data_set_default_double(settings, "speed_variation", 0.0);
	obs_data_set_default_bool(settings, "speed_wander", false);
	obs_data_set_default_double(settings, "wander_amount", 0.5);
	obs_data_set_default_double(settings, "wander_rate", 0.1);
	obs_data_set_default_double(settings, "stagger", 0.0);
	obs_data_set_default_bool(settings, "random_offset", false);
	obs_data_set_default_int(settings, "seed", 1);
}

static void ctt_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	obs_data_set_default_int(settings, "background_type", CBG_SOLID);
	obs_data_set_default_int(settings, "background_color", 0xFF202020);
	obs_data_set_default_int(settings, "background_color2", 0xFF404040);
	obs_data_set_default_double(settings, "background_angle", 45.0);
	ctt_defaults_common(settings);
}

static void ctt_filter_defaults(obs_data_t *settings)
{
	ctt_defaults_common(settings);
}

static struct obs_source_info ctt_source_info = {
	.id = "constellations_text_tiles_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_TEXT,
	.get_name = ctt_source_get_name,
	.create = ctt_source_create,
	.destroy = ctt_destroy,
	.update = ctt_update,
	.video_tick = ctt_tick,
	.video_render = ctt_source_render,
	.get_width = ctt_width,
	.get_height = ctt_height,
	.get_properties = ctt_source_props,
	.get_defaults = ctt_source_defaults,
};

static struct obs_source_info ctt_filter_info = {
	.id = "constellations_text_tiles_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = ctt_filter_get_name,
	.create = ctt_filter_create,
	.destroy = ctt_destroy,
	.update = ctt_update,
	.video_tick = ctt_tick,
	.video_render = ctt_filter_render,
	.video_get_color_space = ctt_filter_color_space,
	.get_properties = ctt_filter_props,
	.get_defaults = ctt_filter_defaults,
};

void constellations_register_text_tiles(void)
{
	obs_register_source(&ctt_source_info);
	obs_register_source(&ctt_filter_info);
}
