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
#include <graphics/image-file.h>
#include "shape-defs.h"

struct cpat_item {
	bool enabled;
	int kind;
	int shape;
	int polygon_sides;
	float stroke_thickness;
	bool outline_only;
	struct vec4 color;

	char *image_path;
	gs_image_file_t image;
	bool image_loaded;
	int image_color_mode;
	struct vec4 image_tint;
	float image_opacity;
	bool keep_aspect;

	/* Random Color Per Cell: each cell picks its color from the item's
	 * main color (shape color or image tint) and these two extras. */
	bool random_colors;
	struct vec4 color_b;
	struct vec4 color_c;

	/* SVG images are kept as vector data and rasterized at the item's
	 * size, so they stay sharp however large the item is drawn. */
	struct NSVGimage *svg;
	gs_texture_t *svg_texture;
	uint32_t svg_px;

	char *source_name;
	obs_weak_source_t *source_ref;
	gs_texrender_t *source_texrender;
	uint32_t source_w, source_h;

	float size;
	float rotation_deg;
	float offset_x;
	float offset_y;
	float density;

	int twinkle_mode;
	float twinkle_amount;
	float twinkle_speed;

	int speed_drift_mode;
	float speed_drift_amount;

	int autorot_mode;
	float autorot_speed;
	int autorot_style;
	float autorot_seed;
};

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
	int layout;
	float left, top, right, bottom;
	float radius;
	float softness;
	bool clip;
	int rows;
	bool stagger_rows;
	bool align;
};

struct cpat_renderer {
	obs_source_t *owner;
	enum cpat_host host;

	uint32_t width, height;
	bool has_background;
	int background_type;
	struct vec4 background_color;
	struct vec4 background_color2;
	float background_angle_deg;
	float anchor_x_pct;
	float anchor_y_pct;
	float canvas_rotation_deg;

	uint32_t item_count;
	struct cpat_item items[CONSTELLATIONS_MAX_ITEMS];

	int layout_mode;
	int grid_order;

	float motion_angle_deg;
	float motion_speed;
	bool alternating_lines;
	bool speed_drift;
	float speed_drift_amount;
	bool location_drift;
	float location_drift_amount;

	bool twinkle_enabled;
	float twinkle_amount;
	float twinkle_speed;

	bool autorot_enabled;
	float autorot_speed;
	int autorot_style;
	float autorot_seed;

	bool vignette_enabled;
	float vignette_size;
	float vignette_anchor_x_pct;
	float vignette_anchor_y_pct;
	float vignette_direction_deg;
	int vignette_shape;
	int vignette_polygon_sides;
	float vignette_softness;
	bool vignette_inverted;

	/* Accumulated motion in lattice coordinates (along-row, cross-row).
	 * Integrating each tick means speed and angle changes steer the pattern
	 * instead of teleporting it, and keeping the components in the lattice
	 * frame means canvas rotation pivots on the anchor no matter how far
	 * the pattern has scrolled. */
	double motion_off_along, motion_off_perp;
	double elapsed_time;

	/* Pattern Border only: the band geometry, and how far (px) the shapes
	 * have marched along the frame (positive is clockwise). */
	struct cpat_border border;
	double border_march;

	gs_effect_t *effect;
};

void cpat_renderer_init(struct cpat_renderer *r, obs_source_t *owner, enum cpat_host host);
void cpat_renderer_free(struct cpat_renderer *r);

void cpat_renderer_set_defaults(obs_data_t *settings, enum cpat_host host);
void cpat_renderer_get_properties(struct cpat_renderer *r, obs_properties_t *props, enum cpat_host host);
void cpat_renderer_update(struct cpat_renderer *r, obs_data_t *settings);

void cpat_renderer_tick(struct cpat_renderer *r, float seconds);
void cpat_renderer_render_background(struct cpat_renderer *r, uint32_t w, uint32_t h);
void cpat_renderer_render_items(struct cpat_renderer *r, uint32_t w, uint32_t h);

/* Corner radii (top-left, top-right, bottom-right, bottom-left) of the
 * border's outer and inner edges for a w x h frame. */
void cpat_border_corner_radii(const struct cpat_renderer *r, uint32_t w, uint32_t h, struct vec4 *outer,
			      struct vec4 *inner);
