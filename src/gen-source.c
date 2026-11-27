/*
Constellations - Generated backgrounds
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <stdio.h>
#include <util/threading.h>
#include <math.h>

#include "common.h"
#include "gen-source.h"

enum { U_CANVAS, U_TIME, U_CLOCK, U_BG, U_COLOR1, U_FIXED = U_COLOR1 + 5 };

static const char *const fixed_names[U_FIXED] = {
	"canvas_size", "time", "clock", "bg_color", "color1", "color2", "color3", "color4", "color5",
};

/* Wall and scaled time wrap here: a day of streaming at full precision. */
#define CGEN_WRAP 100000.0

struct cgen_cfg {
	uint32_t width, height;
	float vals[CGEN_MAX_PARAMS];
	uint32_t colors[CPAL_SLOTS];
	float speed;
	char palette[64];
};

struct cgen {
	obs_source_t *self;
	const struct cgen_desc *d;
	bool is_filter;
	struct cfx_effect fx;
	const char *names[U_FIXED + CGEN_MAX_PARAMS];

	pthread_mutex_t mutex;
	struct cgen_cfg pending, cfg;
	bool has_pending;
	volatile long width, height;
	double time, clock;
};

static const char *color_key(char *buf, size_t n, int slot)
{
	if (slot == 0)
		return "background_color";
	snprintf(buf, n, "color_%d", slot);
	return buf;
}

static const char *cgen_get_name(void *type_data)
{
	const struct cgen_desc *d = type_data;
	return obs_module_text(d->name_source);
}

static const char *cgen_get_filter_name(void *type_data)
{
	const struct cgen_desc *d = type_data;
	return obs_module_text(d->name_filter);
}

static void cgen_update(void *data, obs_data_t *settings)
{
	struct cgen *s = data;
	struct cgen_cfg c = {0};
	char key[16];
	if (!s->is_filter) {
		c.width = (uint32_t)obs_data_get_int(settings, "width");
		c.height = (uint32_t)obs_data_get_int(settings, "height");
	}
	for (size_t i = 0; i < s->d->count; ++i) {
		const struct cgen_param *p = &s->d->params[i];
		c.vals[i] = p->kind == CGEN_BOOL ? (obs_data_get_bool(settings, p->key) ? 1.0f : 0.0f)
						 : (float)obs_data_get_double(settings, p->key);
	}
	for (int i = 0; i < CPAL_SLOTS; ++i)
		c.colors[i] = (uint32_t)obs_data_get_int(settings, color_key(key, sizeof(key), i));
	c.speed = (float)obs_data_get_double(settings, "speed");
	snprintf(c.palette, sizeof(c.palette), "%s", obs_data_get_string(settings, CPAL_KEY));

	os_atomic_set_long(&s->width, (long)c.width);
	os_atomic_set_long(&s->height, (long)c.height);
	pthread_mutex_lock(&s->mutex);
	s->pending = c;
	s->has_pending = true;
	pthread_mutex_unlock(&s->mutex);
}

static void *cgen_create(obs_data_t *settings, obs_source_t *source)
{
	struct cgen *s = bzalloc(sizeof(*s));
	s->self = source;
	s->d = obs_source_get_type_data(source);
	s->is_filter = obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER;
	if (pthread_mutex_init(&s->mutex, NULL) != 0) {
		bfree(s);
		return NULL;
	}
	for (size_t i = 0; i < U_FIXED; ++i)
		s->names[i] = fixed_names[i];
	for (size_t i = 0; i < s->d->count; ++i)
		s->names[U_FIXED + i] = s->d->params[i].key;
	cfx_effect_load(&s->fx, s->d->effect, s->names, U_FIXED + s->d->count);
	cgen_update(s, settings);
	return s;
}

static void cgen_destroy(void *data)
{
	struct cgen *s = data;
	cfx_effect_free(&s->fx);
	pthread_mutex_destroy(&s->mutex);
	bfree(s);
}

static void cgen_tick(void *data, float seconds)
{
	struct cgen *s = data;
	pthread_mutex_lock(&s->mutex);
	if (s->has_pending) {
		s->cfg = s->pending;
		s->has_pending = false;
	}
	pthread_mutex_unlock(&s->mutex);
	s->time = fmod(s->time + (double)s->cfg.speed * seconds, CGEN_WRAP);
	s->clock = fmod(s->clock + seconds, CGEN_WRAP);
}

static void cgen_draw(struct cgen *s, uint32_t w, uint32_t h)
{
	const struct cfx_effect *fx = &s->fx;
	if (!fx->effect || w == 0 || h == 0)
		return;
	uint32_t colors[CPAL_SLOTS];
	memcpy(colors, s->cfg.colors, sizeof(colors));
	uint32_t pal[CPAL_SLOTS];
	if (cpal_get(s->cfg.palette, pal))
		for (int i = 0; i < CPAL_SLOTS; ++i)
			colors[i] = (pal[i] & 0x00FFFFFF) | (colors[i] & 0xFF000000);

	cfx_set_vec2(fx, U_CANVAS, (float)w, (float)h);
	cfx_set_float(fx, U_TIME, (float)s->time);
	cfx_set_float(fx, U_CLOCK, (float)s->clock);
	for (int i = 0; i < CPAL_SLOTS; ++i) {
		struct vec4 c;
		cfx_color_linear(&c, colors[i]);
		cfx_set_vec4(fx, U_BG + i, &c);
	}
	for (size_t i = 0; i < s->d->count; ++i)
		cfx_set_float(fx, U_FIXED + i, s->cfg.vals[i]);

	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	while (gs_effect_loop(fx->effect, "Draw"))
		gs_draw_sprite(NULL, 0, w, h);
	cfx_draw_end(&st);
}

static void cgen_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cgen *s = data;
	cgen_draw(s, s->cfg.width, s->cfg.height);
}

static void cgen_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct cgen *s = data;
	obs_source_t *target = obs_filter_get_target(s->self);
	uint32_t w = target ? obs_source_get_base_width(target) : 0;
	uint32_t h = target ? obs_source_get_base_height(target) : 0;
	if (w == 0 || h == 0) {
		obs_source_skip_video_filter(s->self);
		return;
	}
	if (!cfx_filter_begin(s->self, OBS_ALLOW_DIRECT_RENDERING))
		return;
	obs_source_process_filter_end(s->self, obs_get_base_effect(OBS_EFFECT_DEFAULT), w, h);
	cgen_draw(s, w, h);
}

static enum gs_color_space cgen_filter_color_space(void *data, size_t count, const enum gs_color_space *preferred)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred);
	return cfx_filter_color_space(((struct cgen *)data)->self);
}

static uint32_t cgen_width(void *data)
{
	return (uint32_t)os_atomic_load_long(&((struct cgen *)data)->width);
}

static uint32_t cgen_height(void *data)
{
	return (uint32_t)os_atomic_load_long(&((struct cgen *)data)->height);
}

static obs_properties_t *cgen_props_common(const struct cgen_desc *d, bool is_filter)
{
	obs_properties_t *props = obs_properties_create();
	char key[16], label[96];

	if (!is_filter) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	}

	obs_properties_t *look = obs_properties_create();
	for (size_t i = 0; i < d->count; ++i) {
		const struct cgen_param *p = &d->params[i];
		obs_property_t *prop;
		if (p->kind == CGEN_BOOL) {
			obs_properties_add_bool(look, p->key, obs_module_text(p->label));
			continue;
		}
		if (p->kind == CGEN_LIST) {
			prop = obs_properties_add_list(look, p->key, obs_module_text(p->label), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
			for (int n = 0; n <= (int)p->max; ++n) {
				snprintf(label, sizeof(label), "%s.%d", p->label, n);
				obs_property_list_add_int(prop, obs_module_text(label), n);
			}
			continue;
		}
		prop = obs_properties_add_float_slider(look, p->key, obs_module_text(p->label), p->min, p->max,
						       p->step);
		if (p->suffix)
			obs_property_float_set_suffix(prop, p->suffix);
	}
	obs_properties_add_group(props, "look_group", obs_module_text("Constellations.Group.Look"), OBS_GROUP_NORMAL,
				 look);

	obs_properties_t *colors = obs_properties_create();
	cpal_add_list(colors);
	for (int i = 0; i < CPAL_SLOTS; ++i) {
		snprintf(label, sizeof(label), "Constellations.Palette.Slot%d", i);
		obs_properties_add_color_alpha(colors, color_key(key, sizeof(key), i), obs_module_text(label));
	}
	obs_properties_add_group(props, "colors_group", obs_module_text("Constellations.Group.Colors"),
				 OBS_GROUP_NORMAL, colors);

	obs_properties_t *motion = obs_properties_create();
	obs_properties_add_float_slider(motion, "speed", obs_module_text("Constellations.Gen.Speed"), -5.0, 5.0, 0.01);
	obs_properties_add_group(props, "motion_group", obs_module_text("Constellations.Group.Motion"),
				 OBS_GROUP_NORMAL, motion);
	return props;
}

static obs_properties_t *cgen_props(void *data, void *type_data)
{
	UNUSED_PARAMETER(data);
	return cgen_props_common(type_data, false);
}

static obs_properties_t *cgen_filter_props(void *data, void *type_data)
{
	UNUSED_PARAMETER(data);
	return cgen_props_common(type_data, true);
}

/* 0xRRGGBB to 0xAABBGGRR with the given alpha. */
static uint32_t rgb_to_obs(uint32_t c, uint32_t alpha)
{
	return (alpha << 24) | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

static void cgen_defaults_common(const struct cgen_desc *d, obs_data_t *settings, bool is_filter)
{
	char key[16];
	for (size_t i = 0; i < d->count; ++i) {
		const struct cgen_param *p = &d->params[i];
		if (p->kind == CGEN_BOOL)
			obs_data_set_default_bool(settings, p->key, p->def != 0.0);
		else if (p->kind == CGEN_LIST)
			obs_data_set_default_int(settings, p->key, (long long)p->def);
		else
			obs_data_set_default_double(settings, p->key, p->def);
	}
	for (int i = 0; i < CPAL_SLOTS; ++i)
		obs_data_set_default_int(settings, color_key(key, sizeof(key), i),
					 rgb_to_obs(d->colors[i], i == 0 && is_filter ? 0x00 : 0xFF));
	obs_data_set_default_double(settings, "speed", d->speed);
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
}

static void cgen_defaults(void *type_data, obs_data_t *settings)
{
	cgen_defaults_common(type_data, settings, false);
}

static void cgen_filter_defaults(void *type_data, obs_data_t *settings)
{
	cgen_defaults_common(type_data, settings, true);
}

void cgen_register(const struct cgen_desc *d)
{
	struct obs_source_info src = {
		.id = d->id_source,
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
		.icon_type = OBS_ICON_TYPE_COLOR,
		.type_data = (void *)d,
		.get_name = cgen_get_name,
		.create = cgen_create,
		.destroy = cgen_destroy,
		.update = cgen_update,
		.video_tick = cgen_tick,
		.video_render = cgen_render,
		.get_width = cgen_width,
		.get_height = cgen_height,
		.get_properties2 = cgen_props,
		.get_defaults2 = cgen_defaults,
	};
	obs_register_source(&src);

	struct obs_source_info filter = {
		.id = d->id_filter,
		.type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
		.type_data = (void *)d,
		.get_name = cgen_get_filter_name,
		.create = cgen_create,
		.destroy = cgen_destroy,
		.update = cgen_update,
		.video_tick = cgen_tick,
		.video_render = cgen_filter_render,
		.video_get_color_space = cgen_filter_color_space,
		.get_properties2 = cgen_filter_props,
		.get_defaults2 = cgen_filter_defaults,
	};
	obs_register_source(&filter);
}
