/*
Constellations - Shared helpers
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <obs-module.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Effects ------------------------------------------------------------------
 *
 * libobs caches effects by file path, so every instance of a source shares
 * one gs_effect_t; its parameter handles are looked up once here instead of
 * by name on every draw. Callers list the uniform names they use; a name the
 * effect lacks (installed data files older than the binary) is logged once
 * and its handle stays NULL, which the setters below skip. */

struct cfx_effect {
	gs_effect_t *effect;
	gs_eparam_t **params;
	size_t count;
};

/* Loads data/effects/<file> and resolves `names`. Enters graphics itself. */
bool cfx_effect_load(struct cfx_effect *fx, const char *file, const char *const *names, size_t count);
void cfx_effect_free(struct cfx_effect *fx);

static inline void cfx_set_float(const struct cfx_effect *fx, size_t i, float v)
{
	if (fx->params[i])
		gs_effect_set_float(fx->params[i], v);
}

static inline void cfx_set_vec2(const struct cfx_effect *fx, size_t i, float x, float y)
{
	if (fx->params[i]) {
		struct vec2 v = {{{x, y}}};
		gs_effect_set_vec2(fx->params[i], &v);
	}
}

static inline void cfx_set_vec4(const struct cfx_effect *fx, size_t i, const struct vec4 *v)
{
	if (fx->params[i])
		gs_effect_set_vec4(fx->params[i], v);
}

static inline void cfx_set_vec4f(const struct cfx_effect *fx, size_t i, float x, float y, float z, float w)
{
	struct vec4 v;
	vec4_set(&v, x, y, z, w);
	cfx_set_vec4(fx, i, &v);
}

/* Drawing ------------------------------------------------------------------
 *
 * Everything the plugin draws is linear-light, premultiplied color. Begin
 * switches the framebuffer to sRGB encoding (a no-op on float targets) and
 * the blend to premultiplied over, the same convention OBS's own sources use,
 * so alpha stays correct inside texrenders and colors match OBS's linear
 * compositing. End restores the caller's state. */

struct cfx_draw_state {
	bool prev_srgb;
};

void cfx_draw_begin(struct cfx_draw_state *st);
void cfx_draw_end(const struct cfx_draw_state *st);

/* A color picker value as linear-light RGB with its alpha (not premultiplied). */
void cfx_color_linear(struct vec4 *out, uint32_t rgba);

/* Filters ------------------------------------------------------------------
 *
 * Overlay filters keep the target's color space (SDR, 16-bit SDR or HDR
 * extended) instead of letting OBS tonemap HDR sources to SDR around them.
 * The plugin only ever draws linear values, which are valid in all three. */

enum gs_color_space cfx_filter_color_space(obs_source_t *filter);
bool cfx_filter_begin(obs_source_t *filter, enum obs_allow_direct_render allow_direct);

/* Properties ---------------------------------------------------------------*/

/* Sets visibility and reports whether it changed. Modified callbacks return
 * the accumulated result, so OBS only rebuilds the dialog when something
 * actually appeared or disappeared. */
bool cfx_set_visible(obs_properties_t *props, const char *name, bool visible);

/* Fills a string list with every video source except `exclude_a` and
 * `exclude_b` (either may be NULL), sorted by name. Values are UUIDs, so a
 * renamed source stays linked. */
void cfx_fill_source_list(obs_property_t *list, obs_source_t *exclude_a, obs_source_t *exclude_b);

/* True when `ancestor` is `source` or somewhere in its tree, i.e. using
 * `source` inside `ancestor` would make it draw itself. */
bool cfx_source_contains(obs_source_t *source, obs_source_t *ancestor);
