/*
Constellations
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

enum constellations_shape {
	CSHAPE_CIRCLE = 0,
	CSHAPE_CROSS = 1,
	CSHAPE_SQUARE = 2,
	CSHAPE_POLYGON = 3,
	CSHAPE_SQUIRCLE = 4,
	CSHAPE_HLINE = 5,
	CSHAPE_VLINE = 6,
	CSHAPE_STAR = 7,
	CSHAPE_HEART = 8,
};

enum constellations_item_kind {
	CITEM_SHAPE = 0,
	CITEM_IMAGE = 1,
	CITEM_SOURCE = 2,
};

/* How Image and Source items are colored. Tint multiplies the texture by the
 * chosen color (keeps shading); Recolor replaces it outright and keeps only
 * the alpha, which suits single-color icons and SVGs. */
enum constellations_image_color_mode {
	CIMGCOLOR_ORIGINAL = 0,
	CIMGCOLOR_TINT = 1,
	CIMGCOLOR_RECOLOR = 2,
};

enum constellations_background_type {
	CBG_SOLID = 0,
	CBG_RADIAL = 1,
	CBG_LINEAR = 2,
	CBG_NONE = 3,
};

enum constellations_vignette_shape {
	CVIGN_CIRCLE = 0,
	CVIGN_LINEAR = 1,
	CVIGN_SQUARE = 2,
	CVIGN_POLYGON = 3,
};

enum constellations_transition_mode {
	CTRANS_MODE_COVER = 0,
	CTRANS_MODE_DISSOLVE = 1,
	CTRANS_MODE_SHAPE_ZOOM = 2,
	CTRANS_MODE_KEYHOLE = 3,
};

enum constellations_transition_type {
	CTRANS_TYPE_DIRECTIONAL = 0,
	CTRANS_TYPE_INSIDE_OUT = 1,
	CTRANS_TYPE_OUTSIDE_IN = 2,
	CTRANS_TYPE_MIRRORED = 3,
};

enum constellations_dissolve_type {
	CDISSOLVE_CROP_SHRINK = 0,
	CDISSOLVE_SCALE_SHRINK = 1,
	CDISSOLVE_FADE = 2,
};

enum constellations_logo_display {
	CLOGO_FADE = 0,
	CLOGO_SLIDE = 1,
};

enum constellations_shape_zoom_dir {
	CZOOM_IN = 0,
	CZOOM_OUT = 1,
};

enum constellations_layout_mode {
	CLAYOUT_LAYERED = 0,
	CLAYOUT_STEP_REPEAT = 1,
	CLAYOUT_GRID = 2,
};

enum constellations_grid_order {
	CGRID_RANDOM = 0,
	CGRID_ORDERED = 1,
};

enum constellations_item_twinkle_mode {
	CTWINKLE_GLOBAL = 0,
	CTWINKLE_CUSTOM = 1,
	CTWINKLE_OFF = 2,
};

enum constellations_item_speed_drift_mode {
	CSDRIFT_GLOBAL = 0,
	CSDRIFT_CUSTOM = 1,
	CSDRIFT_OFF = 2,
};

enum constellations_item_autorot_mode {
	CAROT_GLOBAL = 0,
	CAROT_CUSTOM = 1,
	CAROT_OFF = 2,
};

enum constellations_autorot_style {
	CAROT_STYLE_UNISON = 0,
	CAROT_STYLE_RANDOM = 1,
};

enum constellations_tiling_mode {
	CTILE_GRID = 0,
	CTILE_TIGHT = 1,
};

enum constellations_border_position {
	CBORDER_INSIDE = 0,
	CBORDER_OUTSIDE = 1,
};

enum constellations_border_orientation {
	CBORDER_ORIENT_UNIFORM = 0,
	CBORDER_ORIENT_FOLLOW_EDGE = 1,
};

enum constellations_border_layout {
	CBORDER_PERIMETER = 0,
	CBORDER_MASKED = 1,
};

#define CONSTELLATIONS_MAX_ITEMS 32

void constellations_register_pattern_source(void);
void constellations_register_topography_source(void);
void constellations_register_topography_filter(void);
void constellations_register_skylines(void);
void constellations_register_pattern_filter(void);
void constellations_register_pattern_border(void);
void constellations_register_shapes_transition(void);
void constellations_register_skybox(void);
