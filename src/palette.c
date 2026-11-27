/*
Constellations - Shared palettes
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <util/threading.h>
#include <time.h>

#include "common.h"
#include "palette.h"
#include "palette-math.h"

#define CPAL_ID "constellations_palette"

/* 0xRRGGBB, as anyone writing a palette down would, made 0xAABBGGRR. */
#define HEX(c) (0xFF000000u | (((c) & 0xFF) << 16) | ((c) & 0xFF00) | (((c) >> 16) & 0xFF))

struct cpal_preset {
	const char *label;
	uint32_t colors[CPAL_SLOTS];
};

/* clang-format off */
static const struct cpal_preset presets[] = {
	{"Constellations.Palette.Preset.Night",     {0x0B0E1A, 0xF5C66B, 0xFFF1D0, 0x7FA7FF, 0xB48CFF, 0xFFFFFF}},
	{"Constellations.Palette.Preset.Synthwave", {0x14092B, 0xFF2E97, 0xFF9F1C, 0x00E5FF, 0x8F5BFF, 0xFFE66D}},
	{"Constellations.Palette.Preset.Pastel",    {0xFDF6F0, 0xF7B2BD, 0xB5D8F7, 0xC8F2C2, 0xFFE3A3, 0xD7C3F7}},
	{"Constellations.Palette.Preset.Ember",     {0x160805, 0xFF4E1A, 0xFF9E2C, 0xFFD166, 0xB3122E, 0xFFF3E0}},
	{"Constellations.Palette.Preset.Forest",    {0x0D1A12, 0x2E7D4F, 0x8BC34A, 0xD4E157, 0x6D4C41, 0xE8F5E9}},
	{"Constellations.Palette.Preset.Ocean",     {0x031926, 0x00A6A6, 0x2E86DE, 0x9AD1D4, 0x48CAE4, 0xEAF6FF}},
	{"Constellations.Palette.Preset.Mono",      {0x0E0E0E, 0xFFFFFF, 0xBDBDBD, 0x8A8A8A, 0x5C5C5C, 0xE0E0E0}},
};
/* clang-format on */

struct cpal {
	obs_source_t *self;
	pthread_mutex_t mutex;
	uint32_t day[CPAL_SLOTS], night[CPAL_SLOTS];
	bool drift;
	float sunrise, sunset, fade, preview_hour;
	volatile long width, height;
};

static const char *slot_key(char *buf, size_t n, const char *set, int i)
{
	snprintf(buf, n, "%s_%d", set, i);
	return buf;
}

static float local_hour(void)
{
	time_t now = time(NULL);
	struct tm t;
#ifdef _WIN32
	localtime_s(&t, &now);
#else
	localtime_r(&now, &t);
#endif
	return (float)t.tm_hour + (float)t.tm_min / 60.0f + (float)t.tm_sec / 3600.0f;
}

static void cpal_current(struct cpal *p, uint32_t out[CPAL_SLOTS])
{
	pthread_mutex_lock(&p->mutex);
	float w = 1.0f;
	if (p->drift) {
		float hour = p->preview_hour >= 0.0f ? p->preview_hour : local_hour();
		w = cpal_day_weight(hour, p->sunrise, p->sunset, p->fade);
	}
	for (int i = 0; i < CPAL_SLOTS; ++i)
		out[i] = cpal_mix(p->night[i], p->day[i], w);
	pthread_mutex_unlock(&p->mutex);
}

bool cpal_get(const char *uuid, uint32_t out[CPAL_SLOTS])
{
	if (!uuid || !*uuid)
		return false;
	obs_source_t *src = obs_get_source_by_uuid(uuid);
	if (!src)
		return false;
	bool ok = strcmp(obs_source_get_unversioned_id(src), CPAL_ID) == 0;
	if (ok)
		cpal_current(obs_obj_get_data(src), out);
	obs_source_release(src);
	return ok;
}

void cpal_apply(const uint32_t pal[CPAL_SLOTS], int slot, struct vec4 *color, bool linear)
{
	float a = color->w;
	uint32_t rgba = slot == CPAL_BG_SHADE ? cpal_mix(0xFF000000, pal[0], 0.5f) : pal[slot];
	if (linear)
		cfx_color_linear(color, rgba);
	else
		vec4_from_rgba(color, rgba);
	color->w = a;
}

static bool add_palette_source(void *data, obs_source_t *src)
{
	if (strcmp(obs_source_get_unversioned_id(src), CPAL_ID) == 0)
		obs_property_list_add_string(data, obs_source_get_name(src), obs_source_get_uuid(src));
	return true;
}

obs_property_t *cpal_add_list(obs_properties_t *props)
{
	obs_property_t *p = obs_properties_add_list(props, CPAL_KEY, obs_module_text("Constellations.Palette"),
						    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("Constellations.Palette.None"), "");
	obs_enum_sources(add_palette_source, p);
	obs_property_set_long_description(p, obs_module_text("Constellations.Palette.Tooltip"));
	return p;
}

/* Source ------------------------------------------------------------------ */

static const char *cpal_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Constellations.PaletteSource.Name");
}

static void cpal_update(void *data, obs_data_t *settings)
{
	struct cpal *p = data;
	char key[16];
	float dim = (float)obs_data_get_double(settings, "night_dim") / 100.0f;
	bool custom_night = obs_data_get_int(settings, "night_mode") == 1;

	pthread_mutex_lock(&p->mutex);
	for (int i = 0; i < CPAL_SLOTS; ++i) {
		p->day[i] = (uint32_t)obs_data_get_int(settings, slot_key(key, sizeof(key), "day", i)) | 0xFF000000;
		p->night[i] = custom_night
				      ? (uint32_t)obs_data_get_int(settings, slot_key(key, sizeof(key), "night", i)) |
						0xFF000000
				      : cpal_mix(0xFF000000, p->day[i], dim);
	}
	p->drift = obs_data_get_bool(settings, "drift");
	p->sunrise = (float)obs_data_get_double(settings, "sunrise");
	p->sunset = (float)obs_data_get_double(settings, "sunset");
	p->fade = (float)obs_data_get_double(settings, "fade") / 60.0f;
	p->preview_hour = (float)obs_data_get_double(settings, "preview_hour");
	pthread_mutex_unlock(&p->mutex);

	os_atomic_set_long(&p->width, (long)obs_data_get_int(settings, "width"));
	os_atomic_set_long(&p->height, (long)obs_data_get_int(settings, "height"));
}

static void *cpal_create(obs_data_t *settings, obs_source_t *source)
{
	struct cpal *p = bzalloc(sizeof(*p));
	p->self = source;
	if (pthread_mutex_init(&p->mutex, NULL) != 0) {
		bfree(p);
		return NULL;
	}
	cpal_update(p, settings);
	return p;
}

static void cpal_destroy(void *data)
{
	struct cpal *p = data;
	pthread_mutex_destroy(&p->mutex);
	bfree(p);
}

static uint32_t cpal_width(void *data)
{
	return (uint32_t)os_atomic_load_long(&((struct cpal *)data)->width);
}

static uint32_t cpal_height(void *data)
{
	return (uint32_t)os_atomic_load_long(&((struct cpal *)data)->height);
}

static void fill_rect(gs_eparam_t *color, uint32_t rgba, float x, float y, float w, float h)
{
	struct vec4 c;
	cfx_color_linear(&c, rgba);
	gs_effect_set_vec4(color, &c);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_draw_sprite(NULL, 0, (uint32_t)w, (uint32_t)h);
	gs_matrix_pop();
}

/* The live colors as a swatch card: the background, and the five colors in a
 * row across it, so the palette (and its drift) can be seen in a scene. */
static void cpal_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct cpal *p = data;
	float w = (float)cpal_width(p), h = (float)cpal_height(p);
	uint32_t pal[CPAL_SLOTS];
	cpal_current(p, pal);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	while (gs_effect_loop(solid, "Solid")) {
		fill_rect(color, pal[0], 0.0f, 0.0f, w, h);
		float pad = fminf(w, h) * 0.12f, sw = (w - pad) / 5.0f;
		for (int i = 1; i < CPAL_SLOTS; ++i)
			fill_rect(color, pal[i], pad + sw * (float)(i - 1), pad, sw - pad, h - pad * 2.0f);
	}
	cfx_draw_end(&st);
}

static bool cpal_preset_picked(obs_properties_t *props, obs_property_t *prop, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(prop);
	long long id = obs_data_get_int(settings, "preset");
	if (id < 1 || id > (long long)OBS_COUNTOF(presets))
		return false;
	char key[16];
	for (int i = 0; i < CPAL_SLOTS; ++i)
		obs_data_set_int(settings, slot_key(key, sizeof(key), "day", i), HEX(presets[id - 1].colors[i]));
	obs_data_set_int(settings, "preset", 0);
	return true;
}

static bool cpal_layout(obs_properties_t *props, obs_property_t *prop, obs_data_t *settings)
{
	UNUSED_PARAMETER(prop);
	bool drift = obs_data_get_bool(settings, "drift");
	bool custom = drift && obs_data_get_int(settings, "night_mode") == 1;
	char key[16];
	bool changed = false;
	const char *drift_keys[] = {"sunrise", "sunset", "fade", "night_mode", "preview_hour"};
	for (size_t i = 0; i < OBS_COUNTOF(drift_keys); ++i)
		changed |= cfx_set_visible(props, drift_keys[i], drift);
	changed |= cfx_set_visible(props, "night_dim", drift && !custom);
	for (int i = 0; i < CPAL_SLOTS; ++i)
		changed |= cfx_set_visible(props, slot_key(key, sizeof(key), "night", i), custom);
	return changed;
}

static void add_slots(obs_properties_t *grp, const char *set)
{
	char key[16], label[48];
	for (int i = 0; i < CPAL_SLOTS; ++i) {
		snprintf(label, sizeof(label), "Constellations.Palette.Slot%d", i);
		obs_properties_add_color(grp, slot_key(key, sizeof(key), set, i), obs_module_text(label));
	}
}

static obs_properties_t *cpal_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
#define T(k) obs_module_text("Constellations.Palette." k)

	obs_property_t *p =
		obs_properties_add_list(props, "preset", T("Preset"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("Preset.Choose"), 0);
	for (size_t i = 0; i < OBS_COUNTOF(presets); ++i)
		obs_property_list_add_int(p, obs_module_text(presets[i].label), (long long)i + 1);
	obs_property_set_modified_callback(p, cpal_preset_picked);

	obs_properties_t *day = obs_properties_create();
	add_slots(day, "day");
	obs_properties_add_group(props, "day_group", T("Group.Day"), OBS_GROUP_NORMAL, day);

	obs_properties_t *tod = obs_properties_create();
	p = obs_properties_add_bool(tod, "drift", T("Drift"));
	obs_property_set_long_description(p, T("Drift.Tooltip"));
	obs_property_set_modified_callback(p, cpal_layout);
	p = obs_properties_add_float_slider(tod, "sunrise", T("Sunrise"), 0.0, 24.0, 0.25);
	obs_property_float_set_suffix(p, " h");
	p = obs_properties_add_float_slider(tod, "sunset", T("Sunset"), 0.0, 24.0, 0.25);
	obs_property_float_set_suffix(p, " h");
	p = obs_properties_add_int_slider(tod, "fade", T("Fade"), 1, 360, 1);
	obs_property_int_set_suffix(p, " min");
	p = obs_properties_add_float_slider(tod, "preview_hour", T("Preview"), -1.0, 24.0, 0.25);
	obs_property_set_long_description(p, T("Preview.Tooltip"));
	p = obs_properties_add_list(tod, "night_mode", T("NightMode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, T("NightMode.Dim"), 0);
	obs_property_list_add_int(p, T("NightMode.Custom"), 1);
	obs_property_set_modified_callback(p, cpal_layout);
	p = obs_properties_add_int_slider(tod, "night_dim", T("NightDim"), 0, 100, 1);
	obs_property_int_set_suffix(p, " %");
	add_slots(tod, "night");
	obs_properties_add_group(props, "drift_group", T("Group.Drift"), OBS_GROUP_NORMAL, tod);

	obs_properties_t *card = obs_properties_create();
	obs_properties_add_int(card, "width", obs_module_text("Constellations.Canvas.Width"), 16, 4096, 1);
	obs_properties_add_int(card, "height", obs_module_text("Constellations.Canvas.Height"), 16, 4096, 1);
	obs_properties_add_group(props, "card_group", T("Group.Card"), OBS_GROUP_NORMAL, card);
#undef T
	return props;
}

static void cpal_defaults(obs_data_t *settings)
{
	char key[16];
	for (int i = 0; i < CPAL_SLOTS; ++i) {
		obs_data_set_default_int(settings, slot_key(key, sizeof(key), "day", i), HEX(presets[0].colors[i]));
		obs_data_set_default_int(settings, slot_key(key, sizeof(key), "night", i), HEX(presets[1].colors[i]));
	}
	obs_data_set_default_double(settings, "sunrise", 7.0);
	obs_data_set_default_double(settings, "sunset", 19.0);
	obs_data_set_default_int(settings, "fade", 90);
	obs_data_set_default_double(settings, "preview_hour", -1.0);
	obs_data_set_default_int(settings, "night_dim", 45);
	obs_data_set_default_int(settings, "width", 600);
	obs_data_set_default_int(settings, "height", 140);
}

static struct obs_source_info cpal_info = {
	.id = CPAL_ID,
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB,
	.icon_type = OBS_ICON_TYPE_COLOR,
	.get_name = cpal_get_name,
	.create = cpal_create,
	.destroy = cpal_destroy,
	.update = cpal_update,
	.video_render = cpal_render,
	.get_width = cpal_width,
	.get_height = cpal_height,
	.get_properties = cpal_properties,
	.get_defaults = cpal_defaults,
};

void constellations_register_palette(void)
{
	obs_register_source(&cpal_info);
}
