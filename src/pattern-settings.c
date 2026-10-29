/*
Constellations - Pattern renderer settings: defaults, properties, migration
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "pattern-internal.h"
#include "common.h"

#include <plugin-support.h>

#define MAX_ITEMS CONSTELLATIONS_MAX_ITEMS

/* Colors in OBS's 0xAABBGGRR layout. */
#define CPAT_GOLD 0xFF37AFD4       /* #D4AF37 */
#define CPAT_CHAMPAGNE 0xFFCEE7F7  /* #F7E7CE */
#define CPAT_IVORY 0xFFE7F8FF      /* #FFF8E7 */
#define CPAT_CHARCOAL 0xFF1F1C1C   /* #1C1C1F */
#define CPAT_NEAR_BLACK 0xFF0C0A0A /* #0A0A0C */

static const char *const image_presets[][2] = {
	{"sparkle", "Constellations.Item.ImagePreset.Sparkle"},
	{"star4", "Constellations.Item.ImagePreset.Star4"},
	{"moon", "Constellations.Item.ImagePreset.Moon"},
	{"planet", "Constellations.Item.ImagePreset.Planet"},
	{"snowflake", "Constellations.Item.ImagePreset.Snowflake"},
	{"comet", "Constellations.Item.ImagePreset.Comet"},
};

/* ------------------------------------------------------------ defaults */

void cpat_renderer_set_defaults(obs_data_t *settings, enum cpat_host host)
{
	const bool include_canvas = host == CPAT_HOST_SOURCE;
	const bool border = host == CPAT_HOST_BORDER;
	if (include_canvas) {
		obs_data_set_default_int(settings, "width", 1920);
		obs_data_set_default_int(settings, "height", 1080);
		/* Charcoal center fading to near-black corners. */
		obs_data_set_default_int(settings, "background_type", CBG_RADIAL);
		obs_data_set_default_int(settings, "background_color", CPAT_CHARCOAL);
		obs_data_set_default_int(settings, "background_color2", CPAT_NEAR_BLACK);
	} else if (border) {
		/* A solid charcoal frame. */
		obs_data_set_default_int(settings, "background_type", CBG_SOLID);
		obs_data_set_default_int(settings, "background_color", CPAT_CHARCOAL);
		obs_data_set_default_int(settings, "background_color2", CPAT_NEAR_BLACK);
	} else {
		obs_data_set_default_int(settings, "background_type", CBG_SOLID);
		obs_data_set_default_int(settings, "background_color", 0x00000000);
		obs_data_set_default_int(settings, "background_color2", 0x00000000);
	}
	obs_data_set_default_double(settings, "background_angle", 90.0);
	obs_data_set_default_double(settings, "anchor_x_pct", 50.0);
	obs_data_set_default_double(settings, "anchor_y_pct", 50.0);
	obs_data_set_default_double(settings, "canvas_rotation", 0.0);
	obs_data_set_default_int(settings, "item_count", 1);
	obs_data_set_default_int(settings, "layout_mode", CLAYOUT_LAYERED);
	obs_data_set_default_int(settings, "grid_order", CGRID_RANDOM);

	/* The standalone source starts as slowly drifting, twinkling gold stars
	 * on charcoal; the filter keeps plain white dots over the video; the
	 * border is a still row of gold stars around a charcoal frame. */
	const bool gold_stars = include_canvas || border;
	const int default_shape = gold_stars ? CSHAPE_STAR : CSHAPE_CIRCLE;
	const long long default_color = gold_stars ? CPAT_GOLD : 0xFFFFFFFF;
	const double default_size = border ? 20.0 : 24.0;
	const double default_density = border ? 3.0 : 2.0;

	char key[64];
	for (int i = 0; i < MAX_ITEMS; ++i) {
#define DK(name) cpat_item_key(key, sizeof(key), i, name)
		obs_data_set_default_bool(settings, DK("enabled"), i == 0);
		obs_data_set_default_int(settings, DK("kind"), CITEM_SHAPE);
		obs_data_set_default_int(settings, DK("shape"), default_shape);
		obs_data_set_default_int(settings, DK("polygon_sides"), 6);
		obs_data_set_default_double(settings, DK("stroke_thickness"), 0.2);
		obs_data_set_default_bool(settings, DK("outline_only"), false);
		obs_data_set_default_int(settings, DK("color"), default_color);
		obs_data_set_default_string(settings, DK("image_preset"), "");
		obs_data_set_default_string(settings, DK("image_path"), "");
		obs_data_set_default_bool(settings, DK("keep_aspect"), true);
		obs_data_set_default_bool(settings, DK("random_colors"), false);
		obs_data_set_default_int(settings, DK("color_b"), CPAT_CHAMPAGNE);
		obs_data_set_default_int(settings, DK("color_c"), CPAT_IVORY);
		obs_data_set_default_int(settings, DK("image_color_mode"), CIMGCOLOR_ORIGINAL);
		obs_data_set_default_int(settings, DK("image_tint"), CPAT_GOLD);
		obs_data_set_default_double(settings, DK("image_opacity"), 100.0);
		obs_data_set_default_string(settings, DK("source"), "");
		obs_data_set_default_string(settings, DK("source_name"), "");
		obs_data_set_default_double(settings, DK("size"), default_size);
		obs_data_set_default_double(settings, DK("rotation"), 0.0);
		obs_data_set_default_double(settings, DK("offset_x"), 0.0);
		obs_data_set_default_double(settings, DK("offset_y"), 0.0);
		obs_data_set_default_double(settings, DK("density"), default_density);
		obs_data_set_default_int(settings, DK("twinkle_mode"), CTWINKLE_GLOBAL);
		obs_data_set_default_double(settings, DK("twinkle_amount"), 0.5);
		obs_data_set_default_double(settings, DK("twinkle_speed"), 1.0);
		obs_data_set_default_int(settings, DK("speed_drift_mode"), CSDRIFT_GLOBAL);
		obs_data_set_default_double(settings, DK("speed_drift_amount"), 0.25);
		obs_data_set_default_int(settings, DK("autorot_mode"), CAROT_GLOBAL);
		obs_data_set_default_double(settings, DK("autorot_speed"), 45.0);
		obs_data_set_default_int(settings, DK("autorot_style"), CAROT_STYLE_RANDOM);
		obs_data_set_default_int(settings, DK("autorot_seed"), 0);
#undef DK
	}

	obs_data_set_default_double(settings, "motion_angle", include_canvas ? 315.0 : 0.0);
	obs_data_set_default_double(settings, "motion_speed", include_canvas ? 12.0 : 0.0);
	obs_data_set_default_bool(settings, "alternating_lines", false);
	obs_data_set_default_bool(settings, "speed_drift", false);
	obs_data_set_default_double(settings, "speed_drift_amount", 0.25);
	obs_data_set_default_bool(settings, "location_drift", false);
	obs_data_set_default_double(settings, "location_drift_amount", 0.15);

	obs_data_set_default_bool(settings, "twinkle_enabled", include_canvas);
	obs_data_set_default_double(settings, "twinkle_amount", include_canvas ? 0.3 : 0.5);
	obs_data_set_default_double(settings, "twinkle_speed", include_canvas ? 0.5 : 1.0);

	obs_data_set_default_bool(settings, "autorot_enabled", false);
	obs_data_set_default_double(settings, "autorot_speed", 45.0);
	obs_data_set_default_int(settings, "autorot_style", CAROT_STYLE_RANDOM);
	obs_data_set_default_int(settings, "autorot_seed", 0);

	obs_data_set_default_bool(settings, "vignette_enabled", false);
	obs_data_set_default_double(settings, "vignette_size", 0.7);
	obs_data_set_default_double(settings, "vignette_anchor_x_pct", 50.0);
	obs_data_set_default_double(settings, "vignette_anchor_y_pct", 50.0);
	obs_data_set_default_double(settings, "vignette_direction", 0.0);
	obs_data_set_default_int(settings, "vignette_shape", CVIGN_CIRCLE);
	obs_data_set_default_int(settings, "vignette_polygon_sides", 6);
	obs_data_set_default_double(settings, "vignette_softness", 0.35);
	obs_data_set_default_bool(settings, "vignette_inverted", false);
}

/* ----------------------------------------------------------- migration */

void cpat_settings_migrate(obs_data_t *settings)
{
	char key[64];
	for (int i = 0; i < MAX_ITEMS; ++i) {
		if (obs_data_has_user_value(settings, cpat_item_key(key, sizeof(key), i, "source")))
			continue;
		const char *name = obs_data_get_string(settings, cpat_item_key(key, sizeof(key), i, "source_name"));
		if (!*name)
			continue;
		obs_source_t *src = obs_get_source_by_name(name);
		if (!src)
			continue; /* not loaded yet; tried again from load() */
		obs_data_set_string(settings, cpat_item_key(key, sizeof(key), i, "source"), obs_source_get_uuid(src));
		obs_source_release(src);
	}
	obs_data_set_int(settings, "settings_version", CPAT_SETTINGS_VERSION);
}

/* ---------------------------------------------------------- visibility */

/* Context stored on the properties object, for callbacks that add items. */
struct props_ctx {
	struct cpat_renderer *r;
	enum cpat_host host;
};

static bool vis(obs_properties_t *props, int i, const char *name, bool visible)
{
	char key[64];
	return cfx_set_visible(props, cpat_item_key(key, sizeof(key), i, name), visible);
}

static bool item_bool(obs_data_t *settings, int i, const char *name)
{
	char key[64];
	return obs_data_get_bool(settings, cpat_item_key(key, sizeof(key), i, name));
}

static int item_int(obs_data_t *settings, int i, const char *name)
{
	char key[64];
	return (int)obs_data_get_int(settings, cpat_item_key(key, sizeof(key), i, name));
}

static bool apply_item_visibility(obs_properties_t *props, obs_data_t *settings, int i)
{
	const bool en = item_bool(settings, i, "enabled");
	const int kind = item_int(settings, i, "kind");
	const int shape = item_int(settings, i, "shape");
	const bool outline = item_bool(settings, i, "outline_only");
	const bool is_shape = en && kind == CITEM_SHAPE;
	const bool textured = en && (kind == CITEM_IMAGE || kind == CITEM_SOURCE);
	const bool tinted = item_int(settings, i, "image_color_mode") != CIMGCOLOR_ORIGINAL;
	char key[64];
	const bool custom_file = !*obs_data_get_string(settings, cpat_item_key(key, sizeof(key), i, "image_preset"));
	/* Random colors replace the shape color or the image tint, so they
	 * mean nothing for an image shown in its original colors. */
	const bool colorable = is_shape || (textured && tinted);
	const bool random = item_bool(settings, i, "random_colors");

	bool changed = false;
	changed |= vis(props, i, "kind", en);
	changed |= vis(props, i, "shape", is_shape);
	changed |= vis(props, i, "outline_only", is_shape);
	changed |= vis(props, i, "polygon_sides", is_shape && shape == CSHAPE_POLYGON);
	changed |= vis(props, i, "stroke_thickness", is_shape && (shape == CSHAPE_CROSS || outline));
	changed |= vis(props, i, "color", is_shape);
	changed |= vis(props, i, "image_preset", en && kind == CITEM_IMAGE);
	changed |= vis(props, i, "image_path", en && kind == CITEM_IMAGE && custom_file);
	changed |= vis(props, i, "source", en && kind == CITEM_SOURCE);
	changed |= vis(props, i, "image_color_mode", textured);
	changed |= vis(props, i, "image_tint", textured && tinted);
	changed |= vis(props, i, "image_opacity", textured);
	changed |= vis(props, i, "keep_aspect", textured);
	changed |= vis(props, i, "random_colors", colorable);
	changed |= vis(props, i, "color_b", colorable && random);
	changed |= vis(props, i, "color_c", colorable && random);
	static const char *const always[] = {"size",         "rotation",         "offset_x",    "offset_y",
					     "twinkle_mode", "speed_drift_mode", "autorot_mode"};
	for (size_t k = 0; k < OBS_COUNTOF(always); ++k)
		changed |= vis(props, i, always[k], en);

	const bool tw_custom = item_int(settings, i, "twinkle_mode") == CTWINKLE_CUSTOM;
	changed |= vis(props, i, "twinkle_amount", en && tw_custom);
	changed |= vis(props, i, "twinkle_speed", en && tw_custom);
	changed |=
		vis(props, i, "speed_drift_amount", en && item_int(settings, i, "speed_drift_mode") == CSDRIFT_CUSTOM);
	const bool ar_custom = en && item_int(settings, i, "autorot_mode") == CAROT_CUSTOM;
	const bool unison = item_int(settings, i, "autorot_style") == CAROT_STYLE_UNISON;
	changed |= vis(props, i, "autorot_style", ar_custom);
	changed |= vis(props, i, "autorot_speed", ar_custom && unison);
	changed |= vis(props, i, "autorot_seed", ar_custom);
	return changed;
}

/* Step and Repeat and Grid share the first enabled item's density. */
static bool apply_density_visibility(obs_properties_t *props, obs_data_t *settings)
{
	const int mode = (int)obs_data_get_int(settings, "layout_mode");
	bool changed = cfx_set_visible(props, "grid_order", mode == CLAYOUT_GRID);
	int first_enabled = -1;
	for (int i = 0; i < MAX_ITEMS && first_enabled < 0; i++)
		if (item_bool(settings, i, "enabled"))
			first_enabled = i;
	for (int i = 0; i < MAX_ITEMS; i++) {
		bool show = item_bool(settings, i, "enabled");
		if (mode != CLAYOUT_LAYERED)
			show = show && i == first_enabled;
		changed |= vis(props, i, "density", show);
	}
	return changed;
}

static int item_index_from_prop(obs_property_t *p)
{
	const char *name = obs_property_name(p);
	int n = 0;
	if (!name || sscanf(name, "item_%d_", &n) != 1 || n < 1 || n > MAX_ITEMS)
		return -1;
	return n - 1;
}

static bool item_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int i = item_index_from_prop(p);
	if (i < 0)
		return false;
	bool changed = apply_item_visibility(props, settings, i);
	changed |= apply_density_visibility(props, settings);
	return changed;
}

static bool layout_mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return apply_density_visibility(props, settings);
}

static bool background_type_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	int type = (int)obs_data_get_int(settings, "background_type");
	bool changed = cfx_set_visible(props, "background_color", type != CBG_NONE);
	changed |= cfx_set_visible(props, "background_color2", type != CBG_SOLID && type != CBG_NONE);
	changed |= cfx_set_visible(props, "background_angle", type == CBG_LINEAR);
	return changed;
}

static bool motion_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	/* Never gate visibility on motion_speed: toggling rows while the user
	 * drags the slider through zero makes the whole dialog jitter. */
	bool changed = cfx_set_visible(props, "speed_drift_amount", obs_data_get_bool(settings, "speed_drift"));
	changed |= cfx_set_visible(props, "location_drift_amount", obs_data_get_bool(settings, "location_drift"));
	return changed;
}

static bool twinkle_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool en = obs_data_get_bool(settings, "twinkle_enabled");
	bool changed = cfx_set_visible(props, "twinkle_amount", en);
	changed |= cfx_set_visible(props, "twinkle_speed", en);
	return changed;
}

static bool autorot_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool en = obs_data_get_bool(settings, "autorot_enabled");
	bool unison = (int)obs_data_get_int(settings, "autorot_style") == CAROT_STYLE_UNISON;
	bool changed = cfx_set_visible(props, "autorot_style", en);
	changed |= cfx_set_visible(props, "autorot_speed", en && unison);
	changed |= cfx_set_visible(props, "autorot_seed", en);
	return changed;
}

static bool vignette_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	bool en = obs_data_get_bool(settings, "vignette_enabled");
	int shape = (int)obs_data_get_int(settings, "vignette_shape");
	static const char *const names[] = {"vignette_size",  "vignette_anchor_x_pct", "vignette_anchor_y_pct",
					    "vignette_shape", "vignette_softness",     "vignette_inverted"};
	bool changed = false;
	for (size_t k = 0; k < OBS_COUNTOF(names); k++)
		changed |= cfx_set_visible(props, names[k], en);
	changed |= cfx_set_visible(props, "vignette_polygon_sides", en && shape == CVIGN_POLYGON);
	changed |= cfx_set_visible(props, "vignette_direction", en && shape != CVIGN_CIRCLE);
	return changed;
}

static bool motion_reset_clicked(obs_properties_t *props, obs_property_t *p, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(p);
	cpat_renderer_request_motion_reset(data);
	return false;
}

/* ---------------------------------------------------------- properties */

static void add_background_props(obs_properties_t *grp, bool allow_none)
{
	obs_property_t *bt = obs_properties_add_list(grp, "background_type",
						     obs_module_text("Constellations.Background.Type"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	if (allow_none)
		obs_property_list_add_int(bt, obs_module_text("Constellations.Background.Type.None"), CBG_NONE);
	obs_property_list_add_int(bt, obs_module_text("Constellations.Background.Type.Solid"), CBG_SOLID);
	obs_property_list_add_int(bt, obs_module_text("Constellations.Background.Type.Radial"), CBG_RADIAL);
	obs_property_list_add_int(bt, obs_module_text("Constellations.Background.Type.Linear"), CBG_LINEAR);
	obs_property_set_modified_callback(bt, background_type_modified);
	obs_properties_add_color_alpha(grp, "background_color",
				       obs_module_text("Constellations.Canvas.BackgroundColor"));
	obs_properties_add_color_alpha(grp, "background_color2", obs_module_text("Constellations.Background.Color2"));
	obs_properties_add_float_slider(grp, "background_angle", obs_module_text("Constellations.Background.Angle"),
					0.0, 360.0, 1.0);
}

static obs_property_t *add_item_list(obs_properties_t *grp, int i, const char *name, const char *label,
				     enum obs_combo_format format)
{
	char key[64];
	obs_property_t *p = obs_properties_add_list(grp, cpat_item_key(key, sizeof(key), i, name),
						    obs_module_text(label), OBS_COMBO_TYPE_LIST, format);
	obs_property_set_modified_callback(p, item_modified);
	return p;
}

static void add_item_group(obs_properties_t *root, const struct props_ctx *ctx, int i)
{
	char key[64];
#define IK(name) cpat_item_key(key, sizeof(key), i, name)
	obs_properties_t *grp = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_bool(grp, IK("enabled"), obs_module_text("Constellations.Item.Enabled"));
	obs_property_set_modified_callback(p, item_modified);

	p = add_item_list(grp, i, "kind", "Constellations.Item.Kind", OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.Kind.Shape"), CITEM_SHAPE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.Kind.Image"), CITEM_IMAGE);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.Kind.Source"), CITEM_SOURCE);

	p = add_item_list(grp, i, "shape", "Constellations.Shape", OBS_COMBO_FORMAT_INT);
	static const struct {
		const char *label;
		int value;
	} shapes[] = {
		{"Constellations.Shape.Circle", CSHAPE_CIRCLE},     {"Constellations.Shape.Cross", CSHAPE_CROSS},
		{"Constellations.Shape.Square", CSHAPE_SQUARE},     {"Constellations.Shape.Polygon", CSHAPE_POLYGON},
		{"Constellations.Shape.Squircle", CSHAPE_SQUIRCLE}, {"Constellations.Shape.HLine", CSHAPE_HLINE},
		{"Constellations.Shape.VLine", CSHAPE_VLINE},       {"Constellations.Shape.Star", CSHAPE_STAR},
		{"Constellations.Shape.Heart", CSHAPE_HEART},
	};
	for (size_t k = 0; k < OBS_COUNTOF(shapes); ++k)
		obs_property_list_add_int(p, obs_module_text(shapes[k].label), shapes[k].value);

	p = obs_properties_add_bool(grp, IK("outline_only"), obs_module_text("Constellations.Item.OutlineOnly"));
	obs_property_set_modified_callback(p, item_modified);
	obs_properties_add_int_slider(grp, IK("polygon_sides"), obs_module_text("Constellations.Shape.PolySides"), 3,
				      24, 1);
	obs_properties_add_float_slider(grp, IK("stroke_thickness"),
					obs_module_text("Constellations.Shape.StrokeThickness"), 0.02, 1.0, 0.01);
	obs_properties_add_color_alpha(grp, IK("color"), obs_module_text("Constellations.Shape.Color"));

	p = add_item_list(grp, i, "image_preset", "Constellations.Item.ImagePreset", OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("Constellations.Item.ImagePreset.Custom"), "");
	for (size_t k = 0; k < OBS_COUNTOF(image_presets); ++k)
		obs_property_list_add_string(p, obs_module_text(image_presets[k][1]), image_presets[k][0]);
	obs_properties_add_path(grp, IK("image_path"), obs_module_text("Constellations.Item.ImagePath"), OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.bmp *.tga *.gif *.svg);;SVG (*.svg);;All Files (*.*)",
				NULL);

	p = obs_properties_add_list(grp, IK("source"), obs_module_text("Constellations.Item.Source"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_source_t *owner = cpat_renderer_owner(ctx->r);
	cfx_fill_source_list(p, owner, owner && ctx->host != CPAT_HOST_SOURCE ? obs_filter_get_parent(owner) : NULL);

	p = add_item_list(grp, i, "image_color_mode", "Constellations.Item.ImageColor", OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.ImageColor.Original"), CIMGCOLOR_ORIGINAL);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.ImageColor.Tint"), CIMGCOLOR_TINT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.ImageColor.Recolor"), CIMGCOLOR_RECOLOR);
	obs_property_set_long_description(p, obs_module_text("Constellations.Item.ImageColor.Tooltip"));
	obs_properties_add_color(grp, IK("image_tint"), obs_module_text("Constellations.Item.ImageTint"));
	obs_properties_add_float_slider(grp, IK("image_opacity"), obs_module_text("Constellations.Item.ImageOpacity"),
					0.0, 100.0, 1.0);
	obs_properties_add_bool(grp, IK("keep_aspect"), obs_module_text("Constellations.Item.KeepAspect"));

	p = obs_properties_add_bool(grp, IK("random_colors"), obs_module_text("Constellations.Item.RandomColors"));
	obs_property_set_long_description(p, obs_module_text("Constellations.Item.RandomColors.Tooltip"));
	obs_property_set_modified_callback(p, item_modified);
	obs_properties_add_color(grp, IK("color_b"), obs_module_text("Constellations.Item.ColorB"));
	obs_properties_add_color(grp, IK("color_c"), obs_module_text("Constellations.Item.ColorC"));

	obs_properties_add_float_slider(grp, IK("size"), obs_module_text("Constellations.Item.Size"), 1.0, 1024.0, 1.0);
	obs_properties_add_float_slider(grp, IK("rotation"), obs_module_text("Constellations.Item.Rotation"), 0.0,
					360.0, 1.0);
	obs_property_t *ox = obs_properties_add_float_slider(
		grp, IK("offset_x"), obs_module_text("Constellations.Item.OffsetX"), -1000.0, 1000.0, 1.0);
	obs_property_t *oy = obs_properties_add_float_slider(
		grp, IK("offset_y"), obs_module_text("Constellations.Item.OffsetY"), -1000.0, 1000.0, 1.0);
	if (ctx->host == CPAT_HOST_BORDER) {
		obs_property_set_long_description(ox, obs_module_text("Constellations.Border.OffsetX.Tooltip"));
		obs_property_set_long_description(oy, obs_module_text("Constellations.Border.OffsetY.Tooltip"));
	}
	obs_properties_add_float_slider(grp, IK("density"), obs_module_text("Constellations.Item.Density"), 0.1, 20.0,
					0.1);

	p = add_item_list(grp, i, "twinkle_mode", "Constellations.Item.Twinkle", OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.Twinkle.Global"), CTWINKLE_GLOBAL);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.Twinkle.Custom"), CTWINKLE_CUSTOM);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.Twinkle.Off"), CTWINKLE_OFF);
	obs_properties_add_float_slider(grp, IK("twinkle_amount"), obs_module_text("Constellations.Item.TwinkleAmount"),
					0.0, 1.0, 0.01);
	obs_properties_add_float_slider(grp, IK("twinkle_speed"), obs_module_text("Constellations.Item.TwinkleSpeed"),
					0.05, 5.0, 0.05);

	p = add_item_list(grp, i, "speed_drift_mode", "Constellations.Item.SpeedDrift", OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.SpeedDrift.Global"), CSDRIFT_GLOBAL);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.SpeedDrift.Custom"), CSDRIFT_CUSTOM);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.SpeedDrift.Off"), CSDRIFT_OFF);
	obs_properties_add_float_slider(grp, IK("speed_drift_amount"),
					obs_module_text("Constellations.Item.SpeedDriftAmount"), 0.0, 1.0, 0.01);

	p = add_item_list(grp, i, "autorot_mode", "Constellations.Item.AutoRot", OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.AutoRot.Global"), CAROT_GLOBAL);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.AutoRot.Custom"), CAROT_CUSTOM);
	obs_property_list_add_int(p, obs_module_text("Constellations.Item.AutoRot.Off"), CAROT_OFF);
	p = add_item_list(grp, i, "autorot_style", "Constellations.Item.AutoRotStyle", OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.AutoRot.Style.Random"), CAROT_STYLE_RANDOM);
	obs_property_list_add_int(p, obs_module_text("Constellations.AutoRot.Style.Unison"), CAROT_STYLE_UNISON);
	obs_properties_add_float_slider(grp, IK("autorot_speed"), obs_module_text("Constellations.Item.AutoRotSpeed"),
					-360.0, 360.0, 1.0);
	obs_properties_add_int_slider(grp, IK("autorot_seed"), obs_module_text("Constellations.Item.AutoRotSeed"), 0,
				      1000, 1);
#undef IK

	char label[128];
	char gid[64];
	snprintf(label, sizeof(label), "%s %d", obs_module_text("Constellations.Item"), i + 1);
	snprintf(gid, sizeof(gid), "item_%d_group", i + 1);
	obs_properties_add_group(root, gid, label, OBS_GROUP_NORMAL, grp);
}

static int clamp_count(obs_data_t *settings)
{
	int count = settings ? (int)obs_data_get_int(settings, "item_count") : 1;
	return count < 1 ? 1 : (count > MAX_ITEMS ? MAX_ITEMS : count);
}

/* Only the first item_count groups exist: building all 32 (about a
 * thousand widgets) made the dialog slow to open and to refresh. Item
 * groups are the last properties, so they are added and removed at the
 * end and stay in order. */
static bool sync_item_groups(obs_properties_t *props, obs_data_t *settings)
{
	const struct props_ctx *ctx = obs_properties_get_param(props);
	const int count = clamp_count(settings);
	bool changed = false;
	char gid[64];
	for (int i = 0; i < MAX_ITEMS; ++i) {
		snprintf(gid, sizeof(gid), "item_%d_group", i + 1);
		bool present = obs_properties_get(props, gid) != NULL;
		if (i < count && !present && ctx) {
			add_item_group(props, ctx, i);
			if (settings)
				apply_item_visibility(props, settings, i);
			changed = true;
		} else if (i >= count && present) {
			obs_properties_remove_by_name(props, gid);
			changed = true;
		}
	}
	if (changed && settings)
		apply_density_visibility(props, settings);
	return changed;
}

static bool item_count_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return sync_item_groups(props, settings);
}

void cpat_renderer_add_properties(struct cpat_renderer *r, obs_properties_t *props, enum cpat_host host)
{
	struct props_ctx *ctx = bzalloc(sizeof(*ctx));
	ctx->r = r;
	ctx->host = host;
	obs_properties_set_param(props, ctx, bfree);

	if (host == CPAT_HOST_SOURCE) {
		obs_properties_t *canvas = obs_properties_create();
		obs_properties_add_int(canvas, "width", obs_module_text("Constellations.Canvas.Width"), 1, 8192, 1);
		obs_properties_add_int(canvas, "height", obs_module_text("Constellations.Canvas.Height"), 1, 8192, 1);
		add_background_props(canvas, false);
		obs_properties_add_float_slider(canvas, "anchor_x_pct", obs_module_text("Constellations.Anchor.X"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(canvas, "anchor_y_pct", obs_module_text("Constellations.Anchor.Y"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(canvas, "canvas_rotation",
						obs_module_text("Constellations.Canvas.Rotation"), 0.0, 360.0, 1.0);
		obs_properties_add_group(props, "canvas_group", obs_module_text("Constellations.Group.Canvas"),
					 OBS_GROUP_NORMAL, canvas);
	} else {
		if (host == CPAT_HOST_BORDER) {
			obs_properties_t *fill = obs_properties_create();
			add_background_props(fill, true);
			obs_properties_add_group(props, "border_fill_group",
						 obs_module_text("Constellations.Group.BorderFill"), OBS_GROUP_NORMAL,
						 fill);
		} else {
			add_background_props(props, false);
		}
		obs_properties_add_float_slider(props, "anchor_x_pct", obs_module_text("Constellations.Anchor.X"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(props, "anchor_y_pct", obs_module_text("Constellations.Anchor.Y"), 0.0,
						100.0, 0.5);
		obs_properties_add_float_slider(props, "canvas_rotation",
						obs_module_text("Constellations.Canvas.Rotation"), 0.0, 360.0, 1.0);
	}

	obs_property_t *p = obs_properties_add_int_slider(props, "item_count",
							  obs_module_text("Constellations.ItemCount"), 1, MAX_ITEMS, 1);
	obs_property_set_modified_callback(p, item_count_modified);

	p = obs_properties_add_list(props, "layout_mode", obs_module_text("Constellations.Layout"), OBS_COMBO_TYPE_LIST,
				    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Layout.Layered"), CLAYOUT_LAYERED);
	obs_property_list_add_int(p, obs_module_text("Constellations.Layout.StepRepeat"), CLAYOUT_STEP_REPEAT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Layout.Grid"), CLAYOUT_GRID);
	obs_property_set_modified_callback(p, layout_mode_modified);

	p = obs_properties_add_list(props, "grid_order", obs_module_text("Constellations.Layout.GridOrder"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.Layout.GridOrder.Random"), CGRID_RANDOM);
	obs_property_list_add_int(p, obs_module_text("Constellations.Layout.GridOrder.Ordered"), CGRID_ORDERED);

	obs_properties_t *motion = obs_properties_create();
	obs_properties_add_float_slider(motion, "motion_angle", obs_module_text("Constellations.Motion.Angle"), 0.0,
					360.0, 1.0);
	p = obs_properties_add_float_slider(motion, "motion_speed", obs_module_text("Constellations.Motion.Speed"),
					    -500.0, 500.0, 1.0);
	if (host == CPAT_HOST_BORDER)
		obs_property_set_long_description(p, obs_module_text("Constellations.Border.MotionSpeed.Tooltip"));
	obs_properties_add_button2(motion, "motion_reset", obs_module_text("Constellations.Motion.Reset"),
				   motion_reset_clicked, r);
	obs_properties_add_bool(motion, "alternating_lines", obs_module_text("Constellations.Motion.Alternating"));
	p = obs_properties_add_bool(motion, "speed_drift", obs_module_text("Constellations.Motion.SpeedDrift"));
	obs_property_set_modified_callback(p, motion_modified);
	obs_properties_add_float_slider(motion, "speed_drift_amount",
					obs_module_text("Constellations.Motion.SpeedDriftAmount"), 0.0, 1.0, 0.01);
	p = obs_properties_add_bool(motion, "location_drift", obs_module_text("Constellations.Motion.LocationDrift"));
	obs_property_set_modified_callback(p, motion_modified);
	obs_properties_add_float_slider(motion, "location_drift_amount",
					obs_module_text("Constellations.Motion.LocationDriftAmount"), 0.0, 1.0, 0.01);
	obs_properties_add_group(props, "motion_group", obs_module_text("Constellations.Group.Motion"),
				 OBS_GROUP_NORMAL, motion);

	obs_properties_t *tw = obs_properties_create();
	p = obs_properties_add_bool(tw, "twinkle_enabled", obs_module_text("Constellations.Twinkle.Enabled"));
	obs_property_set_modified_callback(p, twinkle_modified);
	obs_properties_add_float_slider(tw, "twinkle_amount", obs_module_text("Constellations.Twinkle.Amount"), 0.0,
					1.0, 0.01);
	obs_properties_add_float_slider(tw, "twinkle_speed", obs_module_text("Constellations.Twinkle.Speed"), 0.05, 5.0,
					0.05);
	obs_properties_add_group(props, "twinkle_group", obs_module_text("Constellations.Group.Twinkle"),
				 OBS_GROUP_NORMAL, tw);

	obs_properties_t *ar = obs_properties_create();
	p = obs_properties_add_bool(ar, "autorot_enabled", obs_module_text("Constellations.AutoRot.Enabled"));
	obs_property_set_modified_callback(p, autorot_modified);
	p = obs_properties_add_list(ar, "autorot_style", obs_module_text("Constellations.AutoRot.Style"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("Constellations.AutoRot.Style.Random"), CAROT_STYLE_RANDOM);
	obs_property_list_add_int(p, obs_module_text("Constellations.AutoRot.Style.Unison"), CAROT_STYLE_UNISON);
	obs_property_set_modified_callback(p, autorot_modified);
	obs_properties_add_float_slider(ar, "autorot_speed", obs_module_text("Constellations.AutoRot.Speed"), -360.0,
					360.0, 1.0);
	obs_properties_add_int_slider(ar, "autorot_seed", obs_module_text("Constellations.AutoRot.Seed"), 0, 1000, 1);
	obs_properties_add_group(props, "autorot_group", obs_module_text("Constellations.Group.AutoRotation"),
				 OBS_GROUP_NORMAL, ar);

	/* The border's inner edge softness covers what a vignette would do. */
	if (host != CPAT_HOST_BORDER) {
		obs_properties_t *vg = obs_properties_create();
		p = obs_properties_add_bool(vg, "vignette_enabled", obs_module_text("Constellations.Vignette.Enabled"));
		obs_property_set_modified_callback(p, vignette_modified);
		obs_properties_add_float_slider(vg, "vignette_size", obs_module_text("Constellations.Vignette.Size"),
						0.01, 2.0, 0.01);
		obs_properties_add_float_slider(vg, "vignette_anchor_x_pct",
						obs_module_text("Constellations.Vignette.AnchorX"), 0.0, 100.0, 0.5);
		obs_properties_add_float_slider(vg, "vignette_anchor_y_pct",
						obs_module_text("Constellations.Vignette.AnchorY"), 0.0, 100.0, 0.5);
		obs_properties_add_float_slider(vg, "vignette_direction",
						obs_module_text("Constellations.Vignette.Direction"), 0.0, 360.0, 1.0);
		p = obs_properties_add_list(vg, "vignette_shape", obs_module_text("Constellations.Vignette.Shape"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("Constellations.Vignette.Shape.Circle"), CVIGN_CIRCLE);
		obs_property_list_add_int(p, obs_module_text("Constellations.Vignette.Shape.Linear"), CVIGN_LINEAR);
		obs_property_list_add_int(p, obs_module_text("Constellations.Vignette.Shape.Square"), CVIGN_SQUARE);
		obs_property_list_add_int(p, obs_module_text("Constellations.Vignette.Shape.Polygon"), CVIGN_POLYGON);
		obs_property_set_modified_callback(p, vignette_modified);
		obs_properties_add_int_slider(vg, "vignette_polygon_sides",
					      obs_module_text("Constellations.Vignette.PolygonSides"), 3, 24, 1);
		obs_properties_add_float_slider(vg, "vignette_softness",
						obs_module_text("Constellations.Vignette.Falloff"), 0.0, 4.0, 0.01);
		obs_properties_add_bool(vg, "vignette_inverted", obs_module_text("Constellations.Vignette.Inverted"));
		obs_properties_add_group(props, "vignette_group", obs_module_text("Constellations.Group.Vignette"),
					 OBS_GROUP_NORMAL, vg);
	}

	obs_data_t *settings = r ? obs_source_get_settings(cpat_renderer_owner(r)) : NULL;
	sync_item_groups(props, settings);
	obs_data_release(settings);
}
