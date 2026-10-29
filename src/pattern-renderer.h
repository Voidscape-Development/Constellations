/*
Constellations - Shared pattern renderer
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include <obs-module.h>
#include "shape-defs.h"

/* The pattern renderer behind Pattern Background, Pattern Filter and Pattern
 * Border.
 *
 * Threading: update() and the properties callbacks run on the UI thread,
 * tick() and the render calls on the graphics thread. update() never touches
 * render state. It parses the settings into a new configuration, decodes any
 * new image or SVG off the graphics thread, and queues the result; tick()
 * swaps it in and uploads textures, so render() always sees one consistent
 * configuration and never a texture that is being replaced. */

/* Which plugin type is hosting the renderer. It decides the defaults and
 * which property groups are shown. */
enum cpat_host {
	CPAT_HOST_SOURCE = 0,
	CPAT_HOST_FILTER = 1,
	CPAT_HOST_BORDER = 2,
};

/* Frame geometry for the Pattern Border filter. The frame always spans the
 * whole output; the band is the area between its outer edge and the inner
 * edge inset by the per-side widths. */
struct cpat_border {
	bool active;
	int position;
	bool round_source;
	int layout;
	float left, top, right, bottom;
	float radius;
	float softness;
	bool clip;
	int rows;
	bool stagger_rows;
	bool align;
};

struct cpat_renderer;

struct cpat_renderer *cpat_renderer_create(obs_source_t *owner, enum cpat_host host, obs_data_t *settings);
void cpat_renderer_destroy(struct cpat_renderer *r);

void cpat_renderer_set_defaults(obs_data_t *settings, enum cpat_host host);
/* Adds the pattern's properties to `props`, after anything the host added.
 * `r` is NULL when OBS asks for properties without an instance. */
void cpat_renderer_add_properties(struct cpat_renderer *r, obs_properties_t *props, enum cpat_host host);
void cpat_renderer_update(struct cpat_renderer *r, obs_data_t *settings);
/* Called from the host's load callback, once every source of the scene
 * collection exists: links items saved by name to the source's UUID. */
void cpat_renderer_load(struct cpat_renderer *r, obs_data_t *settings);

/* Graphics thread. */
void cpat_renderer_tick(struct cpat_renderer *r, float seconds);
void cpat_renderer_show(struct cpat_renderer *r, bool showing);
void cpat_renderer_activate(struct cpat_renderer *r, bool active);
void cpat_renderer_render_background(struct cpat_renderer *r, uint32_t w, uint32_t h);
void cpat_renderer_render_items(struct cpat_renderer *r, uint32_t w, uint32_t h);

/* Pattern Background's canvas size (any thread). */
uint32_t cpat_renderer_width(const struct cpat_renderer *r);
uint32_t cpat_renderer_height(const struct cpat_renderer *r);

/* Pattern Border helpers. The geometry comes from the settings, so the host
 * reads its own copy for sizing; the renderer reads another for drawing. */
void cpat_border_read_settings(obs_data_t *settings, struct cpat_border *b);
void cpat_border_corner_radii(const struct cpat_border *b, uint32_t w, uint32_t h, struct vec4 *outer,
			      struct vec4 *inner);
/* Finishes the filter by drawing its source clipped to a rounded box. */
void cpat_renderer_draw_source_rounded(struct cpat_renderer *r, obs_source_t *filter, uint32_t w, uint32_t h,
				       const struct vec4 *radii);
