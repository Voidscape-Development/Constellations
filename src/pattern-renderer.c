/*
Constellations - Shared pattern renderer
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "pattern-internal.h"
#include "pattern-media.h"
#include "common.h"
#include "lattice.h"

#include <plugin-support.h>
#include <util/dstr.h>
#include <util/threading.h>
#include <math.h>

#define MAX_ITEMS CONSTELLATIONS_MAX_ITEMS
/* Vertex ids must stay exact in float. */
#define MAX_INSTANCES (16777216u / 6u)
/* Skip (and log) an item whose quads would cover the canvas this many times
 * over: huge shapes at the highest densities, where every pixel would be
 * drawn thousands of times and the GPU could stall. */
#define MAX_OVERDRAW 2048.0

/* Effect uniforms, in the order of param_names. */
enum {
	P_CANVAS_SIZE,
	P_ORIGIN,
	P_LATTICE_DIR,
	P_CELL_SIZE,
	P_ROW_FIRST,
	P_COL_COUNT,
	P_ALONG_MIN,
	P_ROW_WRAP,
	P_PERP_FRAC,
	P_COL_CELLS,
	P_COL_FRAC,
	P_COL_PERIOD,
	P_ALT_LINES,
	P_BRICK,
	P_DRIFT_SPEED_AMT,
	P_DRIFT_SPEED_SEED,
	P_BORDER_MODE,
	P_ROW_BOX,
	P_ROW_RAD,
	P_ROW_NS,
	P_ROW_NA,
	P_ROW_TOTAL,
	P_ROW_INDEX,
	P_ROW_PHASE,
	P_BORDER_ALIGN,
	P_OFFSET_INWARD,
	P_RING_MODE,
	P_ITEM_SEED,
	P_WANDER_AMT,
	P_WANDER_TIME,
	P_TWINKLE_AMOUNT,
	P_TWINKLE_PHASE,
	P_AUTO_ROT_ACTIVE,
	P_AUTO_ROT_PHASE,
	P_AUTO_ROT_SEED,
	P_LAYOUT_MODE,
	P_GRID_RANDOM,
	P_STEP_INDEX,
	P_STEP_COUNT,
	P_SHAPE_KIND,
	P_POLYGON_SIDES,
	P_STROKE_THICKNESS,
	P_OUTLINE_ONLY,
	P_SHAPE_SIZE,
	P_SHAPE_ROTATION,
	P_QUAD_HALF,
	P_LINE_AXIS,
	P_SHAPE_COLOR,
	P_COLOR_VARIATION,
	P_COLOR_B,
	P_COLOR_C,
	P_IMAGE_COLOR_MODE,
	P_VIGNETTE_ENABLED,
	P_VIGNETTE_CENTER,
	P_VIGNETTE_SIZE_PX,
	P_VIGNETTE_DIRECTION,
	P_VIGNETTE_SHAPE,
	P_VIGNETTE_POLYGON_SIDES,
	P_VIGNETTE_SOFTNESS,
	P_VIGNETTE_INVERTED,
	P_BORDER_ACTIVE,
	P_BORDER_CLIP,
	P_BORDER_WIDTHS,
	P_BORDER_RADIUS,
	P_BORDER_INNER_RADII,
	P_BORDER_SOFTNESS,
	P_BG_COLOR,
	P_BG_COLOR2,
	P_BG_TYPE,
	P_BG_DIR,
	P_SRC_SIZE,
	P_SRC_RADII,
	P_CELL_TEXTURE,
	P_COUNT,
};

static const char *const param_names[] = {
	"canvas_size",
	"origin",
	"lattice_dir",
	"cell_size",
	"row_first",
	"col_count",
	"along_min",
	"row_wrap",
	"perp_frac",
	"col_cells",
	"col_frac",
	"col_period",
	"alt_lines",
	"brick",
	"drift_speed_amt",
	"drift_speed_seed",
	"border_mode",
	"row_box",
	"row_rad",
	"row_ns",
	"row_na",
	"row_total",
	"row_index",
	"row_phase",
	"border_align",
	"offset_inward",
	"ring_mode",
	"item_seed",
	"wander_amt",
	"wander_time",
	"twinkle_amount",
	"twinkle_phase",
	"auto_rot_active",
	"auto_rot_phase",
	"auto_rot_seed",
	"layout_mode",
	"grid_random",
	"step_index",
	"step_count",
	"shape_kind",
	"polygon_sides",
	"stroke_thickness",
	"outline_only",
	"shape_size",
	"shape_rotation",
	"quad_half",
	"line_axis",
	"shape_color",
	"color_variation",
	"color_b",
	"color_c",
	"image_color_mode",
	"vignette_enabled",
	"vignette_center",
	"vignette_size_px",
	"vignette_direction",
	"vignette_shape",
	"vignette_polygon_sides",
	"vignette_softness",
	"vignette_inverted",
	"border_active",
	"border_clip",
	"border_widths",
	"border_radius",
	"border_inner_radii",
	"border_softness",
	"bg_color",
	"bg_color2",
	"bg_type",
	"bg_dir",
	"src_size",
	"src_radii",
	"cell_texture",
};
_Static_assert(OBS_COUNTOF(param_names) == P_COUNT, "param_names must list every P_ uniform");

/* Built-in SVGs shipped in data/images/<id>.svg. */
static const char *const preset_ids[] = {"sparkle", "star4", "moon", "planet", "snowflake", "comet"};

struct cpat_item_cfg {
	bool enabled;
	int kind;
	int shape;
	float polygon_sides;
	float stroke_thickness;
	bool outline_only;
	struct vec4 color; /* linear */

	int image_color_mode;
	struct vec4 image_tint; /* linear */
	float image_opacity;
	bool keep_aspect;
	bool random_colors;
	struct vec4 color_b, color_c; /* linear */

	/* Image items: what the texture should hold, and the decoded pixels
	 * when that changed since the last configuration (NULL otherwise). */
	char *media_key;
	struct cpat_pixels *pixels;

	/* Source items: a UUID, or a name for settings saved before UUIDs. */
	char *source_uuid;
	char *source_name;

	float size;
	float rotation_deg;
	float offset_x, offset_y;
	float density;

	int twinkle_mode;
	float twinkle_amount, twinkle_speed;
	int speed_drift_mode;
	float speed_drift_amount;
	int autorot_mode;
	float autorot_speed;
	int autorot_style;
	float autorot_seed;
};

struct cpat_config {
	uint32_t width, height;
	int background_type;
	struct vec4 background_color, background_color2; /* sRGB-encoded */
	float background_angle_deg;
	bool has_background;
	float anchor_x_pct, anchor_y_pct;
	float canvas_rotation_deg;

	uint32_t item_count;
	struct cpat_item_cfg items[MAX_ITEMS];
	int layout_mode, grid_order;

	float motion_angle_deg, motion_speed;
	bool alternating_lines;
	bool speed_drift;
	float speed_drift_amount;
	bool location_drift;
	float location_drift_amount;

	bool twinkle_enabled;
	float twinkle_amount, twinkle_speed;

	bool autorot_enabled;
	float autorot_speed;
	int autorot_style;
	float autorot_seed;

	bool vignette_enabled;
	float vignette_size;
	float vignette_anchor_x_pct, vignette_anchor_y_pct;
	float vignette_direction_deg;
	int vignette_shape;
	float vignette_polygon_sides;
	float vignette_softness;
	bool vignette_inverted;

	struct cpat_border border;
};

/* Graphics-thread state of one item. */
struct cpat_gpu_item {
	char *media_key;
	gs_texture_t *texture;

	char *source_uuid;
	char *source_name;
	obs_weak_source_t *source;
	gs_texrender_t *texrender;
	gs_texture_t *frame_texture; /* the source's image this frame */
	bool shown, activated;       /* refs we hold on the source */
	bool cycle_warned;
	float resolve_wait;

	double twinkle_phase; /* cycles */
	double autorot_phase; /* radians */
};

struct cpat_renderer {
	obs_source_t *owner;
	enum cpat_host host;
	struct cfx_effect fx;

	/* UI side. update() is serialized by update_mutex; it caches parsed
	 * SVGs (to re-rasterize on resize) and the media keys it last
	 * submitted, so unchanged artwork is never decoded twice. */
	pthread_mutex_t update_mutex;
	struct {
		char *path;
		struct NSVGimage *svg;
	} svg_cache[MAX_ITEMS];
	char *submitted_keys[MAX_ITEMS];

	/* Hand-off from update() to tick(). */
	pthread_mutex_t pending_mutex;
	struct cpat_config *pending;

	volatile long width, height;
	volatile long reset_motion;

	/* Graphics thread only. */
	struct cpat_config *cfg;
	struct cpat_gpu_item gpu[MAX_ITEMS];
	bool showing, active;
	bool rendering_sources;
	double motion_along, motion_perp;
	double border_march;
	double wander_time;
	uint64_t overdraw_warned;
};

/* ------------------------------------------------------------ settings */

static char *preset_path(const char *preset)
{
	for (size_t i = 0; i < OBS_COUNTOF(preset_ids); ++i) {
		if (strcmp(preset, preset_ids[i]) == 0) {
			struct dstr rel = {0};
			dstr_printf(&rel, "images/%s.svg", preset);
			char *path = obs_module_file(rel.array);
			dstr_free(&rel);
			return path;
		}
	}
	return NULL;
}

static void item_cfg_free(struct cpat_item_cfg *it)
{
	bfree(it->media_key);
	bfree(it->source_uuid);
	bfree(it->source_name);
	cpat_pixels_free(it->pixels);
}

static void config_free(struct cpat_config *cfg)
{
	if (!cfg)
		return;
	for (int i = 0; i < MAX_ITEMS; ++i)
		item_cfg_free(&cfg->items[i]);
	bfree(cfg);
}

void cpat_border_read_settings(obs_data_t *settings, struct cpat_border *b)
{
	b->active = true;
	b->position = (int)obs_data_get_int(settings, "border_position");
	b->round_source = obs_data_get_bool(settings, "border_round_source");
	b->layout = (int)obs_data_get_int(settings, "border_layout");
	if (obs_data_get_bool(settings, "border_per_side")) {
		b->left = (float)obs_data_get_int(settings, "border_left");
		b->top = (float)obs_data_get_int(settings, "border_top");
		b->right = (float)obs_data_get_int(settings, "border_right");
		b->bottom = (float)obs_data_get_int(settings, "border_bottom");
	} else {
		float w = (float)obs_data_get_int(settings, "border_width");
		b->left = b->top = b->right = b->bottom = w;
	}
	b->radius = (float)obs_data_get_int(settings, "border_radius");
	b->softness = (float)obs_data_get_int(settings, "border_softness");
	b->clip = obs_data_get_bool(settings, "border_clip");
	b->rows = (int)obs_data_get_int(settings, "border_rows");
	if (b->rows < 1)
		b->rows = 1;
	if (b->rows > 8)
		b->rows = 8;
	b->stagger_rows = obs_data_get_bool(settings, "border_stagger");
	b->align = (int)obs_data_get_int(settings, "border_orientation") == CBORDER_ORIENT_FOLLOW_EDGE;
}

void cpat_border_corner_radii(const struct cpat_border *b, uint32_t w, uint32_t h, struct vec4 *outer,
			      struct vec4 *inner)
{
	const float widths[4] = {b->left, b->top, b->right, b->bottom};
	float rc, in[4];
	clat_border_radii((float)w, (float)h, widths, b->radius, &rc, in);
	vec4_set(outer, rc, rc, rc, rc);
	vec4_set(inner, in[0], in[1], in[2], in[3]);
}

static void read_item(struct cpat_item_cfg *it, obs_data_t *settings, int i)
{
	char key[64];
#define IK(name) cpat_item_key(key, sizeof(key), i, name)
	it->enabled = obs_data_get_bool(settings, IK("enabled"));
	it->kind = (int)obs_data_get_int(settings, IK("kind"));
	it->shape = (int)obs_data_get_int(settings, IK("shape"));
	it->outline_only = obs_data_get_bool(settings, IK("outline_only"));
	it->polygon_sides = (float)obs_data_get_int(settings, IK("polygon_sides"));
	it->stroke_thickness = (float)obs_data_get_double(settings, IK("stroke_thickness"));
	cfx_color_linear(&it->color, (uint32_t)obs_data_get_int(settings, IK("color")));

	it->image_color_mode = (int)obs_data_get_int(settings, IK("image_color_mode"));
	cfx_color_linear(&it->image_tint, (uint32_t)obs_data_get_int(settings, IK("image_tint")));
	it->image_opacity = (float)obs_data_get_double(settings, IK("image_opacity")) / 100.0f;
	it->keep_aspect = obs_data_get_bool(settings, IK("keep_aspect"));
	it->random_colors = obs_data_get_bool(settings, IK("random_colors"));
	cfx_color_linear(&it->color_b, (uint32_t)obs_data_get_int(settings, IK("color_b")));
	cfx_color_linear(&it->color_c, (uint32_t)obs_data_get_int(settings, IK("color_c")));

	it->source_uuid = bstrdup(obs_data_get_string(settings, IK("source")));
	/* Once an item has a UUID setting (even "None"), the name saved by
	 * older versions no longer applies. */
	if (!obs_data_has_user_value(settings, IK("source")))
		it->source_name = bstrdup(obs_data_get_string(settings, IK("source_name")));
	else
		it->source_name = bstrdup("");

	it->size = (float)obs_data_get_double(settings, IK("size"));
	if (it->size < 1.0f)
		it->size = 1.0f;
	it->rotation_deg = (float)obs_data_get_double(settings, IK("rotation"));
	it->offset_x = (float)obs_data_get_double(settings, IK("offset_x"));
	it->offset_y = (float)obs_data_get_double(settings, IK("offset_y"));
	it->density = (float)obs_data_get_double(settings, IK("density"));
	if (it->density < 0.1f)
		it->density = 0.1f;
	it->twinkle_mode = (int)obs_data_get_int(settings, IK("twinkle_mode"));
	it->twinkle_amount = (float)obs_data_get_double(settings, IK("twinkle_amount"));
	it->twinkle_speed = (float)obs_data_get_double(settings, IK("twinkle_speed"));
	it->speed_drift_mode = (int)obs_data_get_int(settings, IK("speed_drift_mode"));
	it->speed_drift_amount = (float)obs_data_get_double(settings, IK("speed_drift_amount"));
	it->autorot_mode = (int)obs_data_get_int(settings, IK("autorot_mode"));
	it->autorot_speed = (float)obs_data_get_double(settings, IK("autorot_speed"));
	it->autorot_style = (int)obs_data_get_int(settings, IK("autorot_style"));
	it->autorot_seed = (float)obs_data_get_int(settings, IK("autorot_seed"));
#undef IK
}

/* The artwork an image item needs, as a key: decoding happens only when it
 * changes. SVGs include their raster size. *path receives the file. */
static char *item_media_key(const struct cpat_item_cfg *it, obs_data_t *settings, int i, char **path)
{
	*path = NULL;
	if (it->kind != CITEM_IMAGE)
		return bstrdup("");
	char key[64];
	const char *preset = obs_data_get_string(settings, cpat_item_key(key, sizeof(key), i, "image_preset"));
	char *p = *preset ? preset_path(preset) : NULL;
	if (!p) {
		const char *custom = obs_data_get_string(settings, cpat_item_key(key, sizeof(key), i, "image_path"));
		if (!*custom)
			return bstrdup("");
		p = bstrdup(custom);
	}
	struct dstr k = {0};
	if (cpat_media_is_svg(p))
		dstr_printf(&k, "svg:%u:%s", cpat_media_svg_px(it->size), p);
	else
		dstr_printf(&k, "img:%s", p);
	*path = p;
	return k.array;
}

static struct cpat_pixels *load_item_media(struct cpat_renderer *r, int i, const char *path, float size)
{
	if (!cpat_media_is_svg(path))
		return cpat_pixels_from_file(path);
	if (!r->svg_cache[i].path || strcmp(r->svg_cache[i].path, path) != 0) {
		cpat_media_svg_free(r->svg_cache[i].svg);
		bfree(r->svg_cache[i].path);
		r->svg_cache[i].path = bstrdup(path);
		r->svg_cache[i].svg = cpat_media_svg_load(path);
	}
	return cpat_pixels_from_svg(r->svg_cache[i].svg, cpat_media_svg_px(size));
}

static void read_config(struct cpat_renderer *r, struct cpat_config *cfg, obs_data_t *settings)
{
	if (r->host == CPAT_HOST_SOURCE) {
		cfg->width = (uint32_t)obs_data_get_int(settings, "width");
		cfg->height = (uint32_t)obs_data_get_int(settings, "height");
		if (cfg->width == 0)
			cfg->width = 1920;
		if (cfg->height == 0)
			cfg->height = 1080;
	}
	cfg->background_type = (int)obs_data_get_int(settings, "background_type");
	vec4_from_rgba(&cfg->background_color, (uint32_t)obs_data_get_int(settings, "background_color"));
	vec4_from_rgba(&cfg->background_color2, (uint32_t)obs_data_get_int(settings, "background_color2"));
	cfg->background_angle_deg = (float)obs_data_get_double(settings, "background_angle");
	cfg->has_background = cfg->background_type != CBG_NONE &&
			      (cfg->background_color.w > 0.0001f ||
			       (cfg->background_type != CBG_SOLID && cfg->background_color2.w > 0.0001f));
	cfg->anchor_x_pct = (float)obs_data_get_double(settings, "anchor_x_pct");
	cfg->anchor_y_pct = (float)obs_data_get_double(settings, "anchor_y_pct");
	cfg->canvas_rotation_deg = (float)obs_data_get_double(settings, "canvas_rotation");

	int ic = (int)obs_data_get_int(settings, "item_count");
	cfg->item_count = (uint32_t)(ic < 1 ? 1 : (ic > MAX_ITEMS ? MAX_ITEMS : ic));
	cfg->layout_mode = (int)obs_data_get_int(settings, "layout_mode");
	cfg->grid_order = (int)obs_data_get_int(settings, "grid_order");
	for (int i = 0; i < MAX_ITEMS; ++i)
		read_item(&cfg->items[i], settings, i);

	cfg->motion_angle_deg = (float)obs_data_get_double(settings, "motion_angle");
	cfg->motion_speed = (float)obs_data_get_double(settings, "motion_speed");
	cfg->alternating_lines = obs_data_get_bool(settings, "alternating_lines");
	cfg->speed_drift = obs_data_get_bool(settings, "speed_drift");
	cfg->speed_drift_amount = (float)obs_data_get_double(settings, "speed_drift_amount");
	cfg->location_drift = obs_data_get_bool(settings, "location_drift");
	cfg->location_drift_amount = (float)obs_data_get_double(settings, "location_drift_amount");

	cfg->twinkle_enabled = obs_data_get_bool(settings, "twinkle_enabled");
	cfg->twinkle_amount = (float)obs_data_get_double(settings, "twinkle_amount");
	cfg->twinkle_speed = (float)obs_data_get_double(settings, "twinkle_speed");

	cfg->autorot_enabled = obs_data_get_bool(settings, "autorot_enabled");
	cfg->autorot_speed = (float)obs_data_get_double(settings, "autorot_speed");
	cfg->autorot_style = (int)obs_data_get_int(settings, "autorot_style");
	cfg->autorot_seed = (float)obs_data_get_int(settings, "autorot_seed");

	/* The border's inner edge softness covers what a vignette would do. */
	cfg->vignette_enabled = r->host != CPAT_HOST_BORDER && obs_data_get_bool(settings, "vignette_enabled");
	cfg->vignette_size = (float)obs_data_get_double(settings, "vignette_size");
	cfg->vignette_anchor_x_pct = (float)obs_data_get_double(settings, "vignette_anchor_x_pct");
	cfg->vignette_anchor_y_pct = (float)obs_data_get_double(settings, "vignette_anchor_y_pct");
	cfg->vignette_direction_deg = (float)obs_data_get_double(settings, "vignette_direction");
	cfg->vignette_shape = (int)obs_data_get_int(settings, "vignette_shape");
	cfg->vignette_polygon_sides = (float)obs_data_get_int(settings, "vignette_polygon_sides");
	cfg->vignette_softness = (float)obs_data_get_double(settings, "vignette_softness");
	cfg->vignette_inverted = obs_data_get_bool(settings, "vignette_inverted");

	if (r->host == CPAT_HOST_BORDER)
		cpat_border_read_settings(settings, &cfg->border);
}

void cpat_renderer_update(struct cpat_renderer *r, obs_data_t *settings)
{
	struct cpat_config *cfg = bzalloc(sizeof(*cfg));

	pthread_mutex_lock(&r->update_mutex);
	read_config(r, cfg, settings);
	for (int i = 0; i < MAX_ITEMS; ++i) {
		struct cpat_item_cfg *it = &cfg->items[i];
		char *path;
		it->media_key = item_media_key(it, settings, i, &path);
		if (!r->submitted_keys[i] || strcmp(r->submitted_keys[i], it->media_key) != 0) {
			if (path)
				it->pixels = load_item_media(r, i, path, it->size);
			bfree(r->submitted_keys[i]);
			r->submitted_keys[i] = bstrdup(it->media_key);
		}
		bfree(path);
	}
	pthread_mutex_unlock(&r->update_mutex);

	os_atomic_set_long(&r->width, (long)cfg->width);
	os_atomic_set_long(&r->height, (long)cfg->height);

	pthread_mutex_lock(&r->pending_mutex);
	struct cpat_config *old = r->pending;
	if (old) {
		/* The previous configuration was never applied; keep any
		 * artwork it decoded that this one still uses. */
		for (int i = 0; i < MAX_ITEMS; ++i) {
			struct cpat_item_cfg *it = &cfg->items[i];
			if (!it->pixels && old->items[i].pixels &&
			    strcmp(old->items[i].media_key, it->media_key) == 0) {
				it->pixels = old->items[i].pixels;
				old->items[i].pixels = NULL;
			}
		}
	}
	r->pending = cfg;
	pthread_mutex_unlock(&r->pending_mutex);
	config_free(old);
}

/* ------------------------------------------------------- source items */

static obs_source_t *self_for_cycles(const struct cpat_renderer *r)
{
	/* A filter draws as part of its parent, so the parent is what must
	 * not end up inside one of its items. */
	return r->host == CPAT_HOST_SOURCE ? r->owner : obs_filter_get_parent(r->owner);
}

/* Takes or drops the showing/active refs a used source needs, so media
 * plays and browser sources render inside the pattern. */
static void sync_child_refs(struct cpat_renderer *r, int i)
{
	struct cpat_gpu_item *g = &r->gpu[i];
	const struct cpat_config *cfg = r->cfg;
	bool used = g->source && cfg && (uint32_t)i < cfg->item_count && cfg->items[i].enabled &&
		    cfg->items[i].kind == CITEM_SOURCE;
	bool want_show = used && r->showing;
	bool want_active = used && r->active;
	if (want_show == g->shown && want_active == g->activated)
		return;
	obs_source_t *src = g->source ? obs_weak_source_get_source(g->source) : NULL;
	if (src) {
		if (want_show != g->shown) {
			if (want_show)
				obs_source_inc_showing(src);
			else
				obs_source_dec_showing(src);
		}
		if (want_active != g->activated) {
			if (want_active)
				obs_source_inc_active(src);
			else
				obs_source_dec_active(src);
		}
		obs_source_release(src);
	}
	g->shown = want_show;
	g->activated = want_active;
}

static void release_item_source(struct cpat_renderer *r, int i)
{
	struct cpat_gpu_item *g = &r->gpu[i];
	if (g->source) {
		obs_source_t *src = obs_weak_source_get_source(g->source);
		if (src) {
			if (g->shown)
				obs_source_dec_showing(src);
			if (g->activated)
				obs_source_dec_active(src);
			obs_source_release(src);
		}
		obs_weak_source_release(g->source);
		g->source = NULL;
	}
	g->shown = g->activated = false;
	if (g->texrender) {
		gs_texrender_destroy(g->texrender);
		g->texrender = NULL;
	}
}

static void resolve_item_source(struct cpat_renderer *r, int i)
{
	struct cpat_gpu_item *g = &r->gpu[i];
	obs_source_t *src = NULL;
	if (g->source_uuid && *g->source_uuid)
		src = obs_get_source_by_uuid(g->source_uuid);
	else if (g->source_name && *g->source_name)
		src = obs_get_source_by_name(g->source_name);
	if (!src)
		return;
	if (cfx_source_contains(src, self_for_cycles(r)) || src == r->owner) {
		if (!g->cycle_warned)
			obs_log(LOG_WARNING,
				"'%s' item %d: '%s' contains this pattern, so it cannot be drawn inside it",
				obs_source_get_name(r->owner), i + 1, obs_source_get_name(src));
		g->cycle_warned = true;
		obs_source_release(src);
		return;
	}
	g->source = obs_source_get_weak_source(src);
	obs_source_release(src);
	g->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
}

static bool str_differs(const char *a, const char *b)
{
	return strcmp(a ? a : "", b ? b : "") != 0;
}

/* --------------------------------------------------------- tick/apply */

/* Graphics thread, inside obs_enter_graphics(). */
static void apply_config(struct cpat_renderer *r, struct cpat_config *cfg)
{
	struct cpat_config *old = r->cfg;
	r->cfg = cfg;
	for (int i = 0; i < MAX_ITEMS; ++i) {
		struct cpat_item_cfg *it = &cfg->items[i];
		struct cpat_gpu_item *g = &r->gpu[i];

		if (str_differs(g->media_key, it->media_key)) {
			if (g->texture)
				gs_texture_destroy(g->texture);
			g->texture = cpat_pixels_upload(it->pixels);
			bfree(g->media_key);
			g->media_key = bstrdup(it->media_key);
		}
		cpat_pixels_free(it->pixels);
		it->pixels = NULL;

		const char *uuid = it->kind == CITEM_SOURCE ? it->source_uuid : "";
		const char *name = it->kind == CITEM_SOURCE ? it->source_name : "";
		if (str_differs(g->source_uuid, uuid) || str_differs(g->source_name, name)) {
			release_item_source(r, i);
			bfree(g->source_uuid);
			bfree(g->source_name);
			g->source_uuid = bstrdup(uuid);
			g->source_name = bstrdup(name);
			g->cycle_warned = false;
			g->resolve_wait = 0.0f;
		}
		sync_child_refs(r, i);
	}
	config_free(old);
}

static void effective_speeds(const struct cpat_config *cfg, const struct cpat_item_cfg *it, float *twinkle_cps,
			     float *spin_dps)
{
	*twinkle_cps = 0.0f;
	if (it->twinkle_mode == CTWINKLE_CUSTOM)
		*twinkle_cps = it->twinkle_speed;
	else if (it->twinkle_mode == CTWINKLE_GLOBAL && cfg->twinkle_enabled)
		*twinkle_cps = cfg->twinkle_speed;

	*spin_dps = 0.0f;
	if (it->autorot_mode == CAROT_CUSTOM && it->autorot_style == CAROT_STYLE_UNISON)
		*spin_dps = it->autorot_speed;
	else if (it->autorot_mode == CAROT_GLOBAL && cfg->autorot_enabled && cfg->autorot_style == CAROT_STYLE_UNISON)
		*spin_dps = cfg->autorot_speed;
}

void cpat_renderer_tick(struct cpat_renderer *r, float seconds)
{
	pthread_mutex_lock(&r->pending_mutex);
	struct cpat_config *cfg = r->pending;
	r->pending = NULL;
	pthread_mutex_unlock(&r->pending_mutex);

	bool need_graphics = cfg != NULL;
	for (int i = 0; i < MAX_ITEMS && !need_graphics; ++i) {
		struct cpat_gpu_item *g = &r->gpu[i];
		if (!g->source && ((g->source_uuid && *g->source_uuid) || (g->source_name && *g->source_name)))
			need_graphics = g->resolve_wait <= 0.0f;
	}
	if (need_graphics) {
		obs_enter_graphics();
		if (cfg)
			apply_config(r, cfg);
		/* Sources saved by name, or created after this pattern, are
		 * looked up again about once a second until they appear. */
		for (int i = 0; i < MAX_ITEMS; ++i) {
			struct cpat_gpu_item *g = &r->gpu[i];
			if (g->source || g->resolve_wait > 0.0f)
				continue;
			if ((g->source_uuid && *g->source_uuid) || (g->source_name && *g->source_name)) {
				resolve_item_source(r, i);
				g->resolve_wait = 1.0f;
				sync_child_refs(r, i);
			}
		}
		obs_leave_graphics();
	}
	for (int i = 0; i < MAX_ITEMS; ++i)
		if (r->gpu[i].resolve_wait > 0.0f)
			r->gpu[i].resolve_wait -= seconds;

	cfg = r->cfg;
	if (!cfg)
		return;

	if (os_atomic_set_long(&r->reset_motion, 0)) {
		r->motion_along = r->motion_perp = 0.0;
		r->border_march = 0.0;
	}

	/* Integrated in double so speed and angle changes steer the motion
	 * instead of teleporting it. The components are kept in the lattice
	 * frame, so canvas rotation pivots on the anchor however far the
	 * pattern has scrolled. */
	double ang = (cfg->motion_angle_deg - cfg->canvas_rotation_deg) * (M_PI / 180.0);
	double step = (double)cfg->motion_speed * (double)seconds;
	r->motion_along += cos(ang) * step;
	r->motion_perp += sin(ang) * step;
	r->border_march += step;
	r->wander_time = clat_fmod_pos(r->wander_time + seconds, CLAT_WANDER_PERIOD);

	for (int i = 0; i < MAX_ITEMS; ++i) {
		float tw, spin;
		effective_speeds(cfg, &cfg->items[i], &tw, &spin);
		struct cpat_gpu_item *g = &r->gpu[i];
		g->twinkle_phase = clat_fmod_pos(g->twinkle_phase + (double)tw * seconds, CLAT_TWINKLE_PERIOD);
		g->autorot_phase =
			clat_fmod_pos(g->autorot_phase + (double)spin * (M_PI / 180.0) * seconds, CLAT_TWO_PI);
	}
}

void cpat_renderer_show(struct cpat_renderer *r, bool showing)
{
	r->showing = showing;
	for (int i = 0; i < MAX_ITEMS; ++i)
		sync_child_refs(r, i);
}

void cpat_renderer_activate(struct cpat_renderer *r, bool active)
{
	r->active = active;
	for (int i = 0; i < MAX_ITEMS; ++i)
		sync_child_refs(r, i);
}

/* ------------------------------------------------------------ drawing */

static void set_mask_uniforms(struct cpat_renderer *r, const struct cpat_config *cfg, uint32_t w, uint32_t h)
{
	const struct cfx_effect *fx = &r->fx;
	cfx_set_vec2(fx, P_CANVAS_SIZE, (float)w, (float)h);

	float min_dim = (float)(w < h ? w : h);
	cfx_set_float(fx, P_VIGNETTE_ENABLED, cfg->vignette_enabled ? 1.0f : 0.0f);
	cfx_set_vec2(fx, P_VIGNETTE_CENTER, cfg->vignette_anchor_x_pct / 100.0f * (float)w,
		     cfg->vignette_anchor_y_pct / 100.0f * (float)h);
	cfx_set_float(fx, P_VIGNETTE_SIZE_PX, cfg->vignette_size * 0.5f * min_dim);
	cfx_set_float(fx, P_VIGNETTE_DIRECTION, cfg->vignette_direction_deg * (float)(M_PI / 180.0));
	cfx_set_float(fx, P_VIGNETTE_SHAPE, (float)cfg->vignette_shape);
	cfx_set_float(fx, P_VIGNETTE_POLYGON_SIDES, cfg->vignette_polygon_sides);
	cfx_set_float(fx, P_VIGNETTE_SOFTNESS, cfg->vignette_softness);
	cfx_set_float(fx, P_VIGNETTE_INVERTED, cfg->vignette_inverted ? 1.0f : 0.0f);

	/* Set even when inactive: D3D11 refuses to draw until every uniform
	 * a shader references has a value. */
	const struct cpat_border *b = &cfg->border;
	struct vec4 outer, inner;
	cpat_border_corner_radii(b, w, h, &outer, &inner);
	cfx_set_float(fx, P_BORDER_ACTIVE, b->active ? 1.0f : 0.0f);
	/* A masked lattice would cover the whole source, so it always clips. */
	cfx_set_float(fx, P_BORDER_CLIP, (b->clip || b->layout == CBORDER_MASKED) ? 1.0f : 0.0f);
	cfx_set_vec4f(fx, P_BORDER_WIDTHS, b->left, b->top, b->right, b->bottom);
	cfx_set_float(fx, P_BORDER_RADIUS, outer.x);
	cfx_set_vec4(fx, P_BORDER_INNER_RADII, &inner);
	cfx_set_float(fx, P_BORDER_SOFTNESS, b->softness);
}

void cpat_renderer_render_background(struct cpat_renderer *r, uint32_t w, uint32_t h)
{
	const struct cpat_config *cfg = r->cfg;
	if (!cfg || !cfg->has_background || !r->fx.effect || w == 0 || h == 0)
		return;
	const struct cfx_effect *fx = &r->fx;
	set_mask_uniforms(r, cfg, w, h);
	float ang = cfg->background_angle_deg * (float)(M_PI / 180.0);
	cfx_set_vec4(fx, P_BG_COLOR, &cfg->background_color);
	cfx_set_vec4(fx, P_BG_COLOR2, &cfg->background_color2);
	cfx_set_float(fx, P_BG_TYPE, (float)cfg->background_type);
	cfx_set_vec2(fx, P_BG_DIR, cosf(ang), sinf(ang));

	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	while (gs_effect_loop(fx->effect, "DrawBackground"))
		gs_draw_sprite(NULL, 0, w, h);
	cfx_draw_end(&st);
}

/* Renders each used source once per frame, even when several items share
 * it. Skipped while this pattern is already drawing its sources: a source
 * that (through other patterns) contains this one would otherwise recurse
 * until the stack overflows. */
static void render_item_sources(struct cpat_renderer *r, const struct cpat_config *cfg, uint32_t count)
{
	for (uint32_t i = 0; i < MAX_ITEMS; ++i)
		r->gpu[i].frame_texture = NULL;
	if (r->rendering_sources)
		return;
	r->rendering_sources = true;
	for (uint32_t i = 0; i < count; ++i) {
		struct cpat_gpu_item *g = &r->gpu[i];
		if (!cfg->items[i].enabled || cfg->items[i].kind != CITEM_SOURCE || !g->source || !g->texrender)
			continue;
		for (uint32_t j = 0; j < i; ++j) {
			if (r->gpu[j].frame_texture && r->gpu[j].source == g->source) {
				g->frame_texture = r->gpu[j].frame_texture;
				break;
			}
		}
		if (g->frame_texture)
			continue;
		obs_source_t *src = obs_weak_source_get_source(g->source);
		if (!src)
			continue;
		uint32_t sw = obs_source_get_width(src);
		uint32_t sh = obs_source_get_height(src);
		if (sw && sh) {
			gs_texrender_reset(g->texrender);
			if (gs_texrender_begin(g->texrender, sw, sh)) {
				struct vec4 clear;
				vec4_zero(&clear);
				gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
				gs_ortho(0.0f, (float)sw, 0.0f, (float)sh, -100.0f, 100.0f);
				gs_blend_state_push();
				gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE,
							   GS_BLEND_INVSRCALPHA);
				obs_source_video_render(src);
				gs_blend_state_pop();
				gs_texrender_end(g->texrender);
				g->frame_texture = gs_texrender_get_texture(g->texrender);
			}
		}
		obs_source_release(src);
	}
	r->rendering_sources = false;
}

/* Quad half extents of the item's shape in its own frame, the axis a line
 * runs along (lines span their whole cell), and how far the quad reaches. */
static void item_quad(const struct cpat_item_cfg *it, gs_texture_t *tex, float cell, struct vec2 *half,
		      float *line_axis, float *reach)
{
	*line_axis = 0.0f;
	if (it->kind != CITEM_SHAPE) {
		float hx = it->size * 0.5f, hy = it->size * 0.5f;
		uint32_t tw = tex ? gs_texture_get_width(tex) : 0;
		uint32_t th = tex ? gs_texture_get_height(tex) : 0;
		if (it->keep_aspect && tw && th) {
			float aspect = (float)tw / (float)th;
			if (aspect >= 1.0f)
				hy /= aspect;
			else
				hx *= aspect;
		}
		vec2_set(half, hx, hy);
	} else {
		/* The heart reaches a little past the half size; everything
		 * else fits inside it. Outlines add half their stroke. */
		float r = it->size * (it->shape == CSHAPE_HEART ? 0.6f : 0.5f);
		if (it->outline_only)
			r += it->stroke_thickness * it->size * 0.5f;
		r += 2.0f;
		vec2_set(half, r, r);
		if (it->shape == CSHAPE_HLINE) {
			*line_axis = 1.0f;
			half->x = cell * 0.5f;
		} else if (it->shape == CSHAPE_VLINE) {
			*line_axis = 2.0f;
			half->y = cell * 0.5f;
		}
	}
	*reach = sqrtf(half->x * half->x + half->y * half->y);
}

static void set_item_uniforms(struct cpat_renderer *r, const struct cpat_config *cfg, int i, float cell,
			      const struct vec2 *half, float line_axis, uint32_t step_index, uint32_t step_count)
{
	const struct cfx_effect *fx = &r->fx;
	const struct cpat_item_cfg *it = &cfg->items[i];
	const struct cpat_gpu_item *g = &r->gpu[i];
	float item_seed = (float)i * 17.31f;

	cfx_set_float(fx, P_CELL_SIZE, cell);
	cfx_set_float(fx, P_ITEM_SEED, item_seed);
	cfx_set_float(fx, P_WANDER_AMT, cfg->location_drift ? cfg->location_drift_amount : 0.0f);
	cfx_set_float(fx, P_WANDER_TIME, (float)r->wander_time);

	float tw_amount = 0.0f;
	if (it->twinkle_mode == CTWINKLE_CUSTOM)
		tw_amount = it->twinkle_amount;
	else if (it->twinkle_mode == CTWINKLE_GLOBAL && cfg->twinkle_enabled)
		tw_amount = cfg->twinkle_amount;
	cfx_set_float(fx, P_TWINKLE_AMOUNT, tw_amount);
	cfx_set_float(fx, P_TWINKLE_PHASE, (float)g->twinkle_phase);

	/* Auto rotation always scatters random per-cell angles when active;
	 * only Unison adds the shared spin (integrated in tick). */
	bool ar_active = false;
	float ar_seed = 0.0f;
	if (it->autorot_mode == CAROT_CUSTOM) {
		ar_active = true;
		ar_seed = it->autorot_seed;
	} else if (it->autorot_mode == CAROT_GLOBAL && cfg->autorot_enabled) {
		ar_active = true;
		ar_seed = cfg->autorot_seed;
	}
	cfx_set_float(fx, P_AUTO_ROT_ACTIVE, ar_active ? 1.0f : 0.0f);
	cfx_set_float(fx, P_AUTO_ROT_PHASE, ar_active ? (float)g->autorot_phase : 0.0f);
	cfx_set_float(fx, P_AUTO_ROT_SEED, ar_seed);

	/* The shared seed keeps every item's rows drifting in unison when
	 * following the global setting; Custom uses the item's own seed. */
	float sd_amount = 0.0f, sd_seed = 0.0f;
	if (it->speed_drift_mode == CSDRIFT_CUSTOM) {
		sd_amount = it->speed_drift_amount;
		sd_seed = item_seed;
	} else if (it->speed_drift_mode == CSDRIFT_GLOBAL && cfg->speed_drift) {
		sd_amount = cfg->speed_drift_amount;
	}
	cfx_set_float(fx, P_DRIFT_SPEED_AMT, sd_amount);
	cfx_set_float(fx, P_DRIFT_SPEED_SEED, sd_seed);
	cfx_set_float(fx, P_ALT_LINES, cfg->alternating_lines ? 1.0f : 0.0f);
	cfx_set_float(fx, P_BRICK, cfg->layout_mode == CLAYOUT_STEP_REPEAT ? 1.0f : 0.0f);

	cfx_set_float(fx, P_LAYOUT_MODE, (float)cfg->layout_mode);
	cfx_set_float(fx, P_GRID_RANDOM, cfg->grid_order == CGRID_ORDERED ? 0.0f : 1.0f);
	cfx_set_float(fx, P_STEP_INDEX, (float)step_index);
	cfx_set_float(fx, P_STEP_COUNT, (float)step_count);

	cfx_set_float(fx, P_SHAPE_KIND, (float)it->shape);
	cfx_set_float(fx, P_POLYGON_SIDES, it->polygon_sides);
	cfx_set_float(fx, P_STROKE_THICKNESS, it->stroke_thickness);
	cfx_set_float(fx, P_OUTLINE_ONLY, it->outline_only ? 1.0f : 0.0f);
	cfx_set_float(fx, P_SHAPE_SIZE, it->size);
	cfx_set_float(fx, P_SHAPE_ROTATION, it->rotation_deg * (float)(M_PI / 180.0));
	cfx_set_vec2(fx, P_QUAD_HALF, half->x, half->y);
	cfx_set_float(fx, P_LINE_AXIS, line_axis);

	/* Shapes use their own color. Images and sources pass the tint color
	 * with the opacity as alpha; the shader applies the tint per
	 * image_color_mode. */
	struct vec4 color = it->color;
	if (it->kind != CITEM_SHAPE) {
		color = it->image_tint;
		color.w = it->image_opacity;
	}
	cfx_set_vec4(fx, P_SHAPE_COLOR, &color);
	cfx_set_float(fx, P_COLOR_VARIATION, it->random_colors ? 1.0f : 0.0f);
	cfx_set_vec4(fx, P_COLOR_B, &it->color_b);
	cfx_set_vec4(fx, P_COLOR_C, &it->color_c);
	cfx_set_float(fx, P_IMAGE_COLOR_MODE, it->kind == CITEM_SHAPE ? 0.0f : (float)it->image_color_mode);
}

static void draw_instances(struct cpat_renderer *r, const char *technique, uint32_t instances)
{
	gs_load_vertexbuffer(NULL);
	gs_load_indexbuffer(NULL);
	while (gs_effect_loop(r->fx.effect, technique))
		gs_draw(GS_TRIS, 0, instances * 6);
}

static bool overdraw_ok(struct cpat_renderer *r, int i, double instances, const struct vec2 *half, uint32_t w,
			uint32_t h)
{
	double area = instances * 4.0 * half->x * half->y;
	if (instances <= MAX_INSTANCES && area <= MAX_OVERDRAW * (double)w * (double)h)
		return true;
	uint64_t bit = 1ull << i;
	if (!(r->overdraw_warned & bit))
		obs_log(LOG_WARNING,
			"'%s' item %d: its shapes are too large for its density and would cover the canvas about "
			"%.0f times over; skipping it. Lower the size or the density.",
			obs_source_get_name(r->owner), i + 1, area / ((double)w * (double)h));
	r->overdraw_warned |= bit;
	return false;
}

/* Everything one item's draws need. */
struct item_draw {
	const struct cpat_config *cfg;
	int index;
	float cell;
	struct vec2 half;
	float line_axis;
	float reach;
	uint32_t step_index, step_count;
	gs_texture_t *texture;
	const char *technique;
	uint32_t w, h;
};

/* Unused by one draw path but referenced by the shared vertex shader, so
 * they need a value too. */
static void set_placement_defaults(struct cpat_renderer *r)
{
	const struct cfx_effect *fx = &r->fx;
	struct vec4 zero;
	vec4_zero(&zero);
	cfx_set_float(fx, P_BORDER_MODE, 0.0f);
	cfx_set_vec2(fx, P_ORIGIN, 0.0f, 0.0f);
	cfx_set_vec2(fx, P_LATTICE_DIR, 1.0f, 0.0f);
	cfx_set_float(fx, P_ROW_FIRST, 0.0f);
	cfx_set_float(fx, P_COL_COUNT, 1.0f);
	cfx_set_float(fx, P_ALONG_MIN, 0.0f);
	cfx_set_float(fx, P_ROW_WRAP, 0.0f);
	cfx_set_float(fx, P_PERP_FRAC, 0.0f);
	cfx_set_float(fx, P_COL_CELLS, 0.0f);
	cfx_set_float(fx, P_COL_FRAC, 0.0f);
	cfx_set_float(fx, P_COL_PERIOD, 2048.0f);
	cfx_set_vec4(fx, P_ROW_BOX, &zero);
	cfx_set_vec4(fx, P_ROW_RAD, &zero);
	cfx_set_vec4(fx, P_ROW_NS, &zero);
	cfx_set_vec4(fx, P_ROW_NA, &zero);
	cfx_set_float(fx, P_ROW_TOTAL, 1.0f);
	cfx_set_float(fx, P_ROW_INDEX, 0.0f);
	cfx_set_float(fx, P_ROW_PHASE, 0.0f);
	cfx_set_float(fx, P_BORDER_ALIGN, 0.0f);
	cfx_set_float(fx, P_OFFSET_INWARD, 0.0f);
	cfx_set_float(fx, P_RING_MODE, 0.0f);
}

/* Every effect parameter is cleared when a technique ends (each
 * gs_effect_loop), so each draw uploads the complete state again: a
 * parameter left unset either fails the draw (D3D11, OpenGL) or silently
 * keeps another draw's value. */
static void prepare_draw(struct cpat_renderer *r, const struct item_draw *d)
{
	set_mask_uniforms(r, d->cfg, d->w, d->h);
	set_placement_defaults(r);
	set_item_uniforms(r, d->cfg, d->index, d->cell, &d->half, d->line_axis, d->step_index, d->step_count);
	if (d->texture && r->fx.params[P_CELL_TEXTURE])
		gs_effect_set_texture_srgb(r->fx.params[P_CELL_TEXTURE], d->texture);
}

static void draw_lattice_item(struct cpat_renderer *r, const struct item_draw *d)
{
	const struct cfx_effect *fx = &r->fx;
	const struct cpat_config *cfg = d->cfg;
	const struct cpat_item_cfg *it = &cfg->items[d->index];
	const float cell = d->cell;

	float crot = cfg->canvas_rotation_deg * (float)(M_PI / 180.0);
	float dir_x = cosf(crot), dir_y = sinf(crot);
	float ox = cfg->anchor_x_pct / 100.0f * (float)d->w + it->offset_x;
	float oy = cfg->anchor_y_pct / 100.0f * (float)d->h + it->offset_y;

	float row_wrap, perp_frac, col_cells, col_frac;
	float period = clat_col_period(cfg->layout_mode == CLAYOUT_LAYERED ? 1 : d->step_count);
	clat_split_rows(r->motion_perp, cell, &row_wrap, &perp_frac);
	clat_split_cols(r->motion_along, cell, period, &col_cells, &col_frac);

	float wander = cfg->location_drift ? cfg->location_drift_amount * cell : 0.0f;
	struct clat_range rg;
	clat_visible_range((float)d->w, (float)d->h, ox, oy, dir_x, dir_y, cell, d->reach + wander + 1.0f, perp_frac,
			   &rg);

	double instances = (double)rg.row_count * (double)rg.col_count;
	double owned = cfg->layout_mode == CLAYOUT_LAYERED ? instances : instances / (double)d->step_count;
	if (instances < 1.0 || instances > MAX_INSTANCES || !overdraw_ok(r, d->index, owned, &d->half, d->w, d->h))
		return;

	prepare_draw(r, d);
	cfx_set_vec2(fx, P_ORIGIN, ox, oy);
	cfx_set_vec2(fx, P_LATTICE_DIR, dir_x, dir_y);
	cfx_set_float(fx, P_ROW_FIRST, rg.row_first);
	cfx_set_float(fx, P_COL_COUNT, rg.col_count);
	cfx_set_float(fx, P_ALONG_MIN, rg.along_min);
	cfx_set_float(fx, P_ROW_WRAP, row_wrap);
	cfx_set_float(fx, P_PERP_FRAC, perp_frac);
	cfx_set_float(fx, P_COL_CELLS, col_cells);
	cfx_set_float(fx, P_COL_FRAC, col_frac);
	cfx_set_float(fx, P_COL_PERIOD, period);
	draw_instances(r, d->technique, (uint32_t)instances);
}

static void draw_border_item(struct cpat_renderer *r, const struct item_draw *d)
{
	const struct cfx_effect *fx = &r->fx;
	const struct cpat_config *cfg = d->cfg;
	const struct cpat_item_cfg *it = &cfg->items[d->index];
	const struct cpat_border *b = &cfg->border;
	const float cell = d->cell;

	struct clat_border_frame fr = {.w = (float)d->w,
				       .h = (float)d->h,
				       .widths = {b->left, b->top, b->right, b->bottom}};
	clat_border_radii(fr.w, fr.h, fr.widths, b->radius, &fr.radius, fr.inner_radii);

	float sd_amount = 0.0f, sd_seed = 0.0f;
	if (it->speed_drift_mode == CSDRIFT_CUSTOM) {
		sd_amount = it->speed_drift_amount;
		sd_seed = (float)d->index * 17.31f;
	} else if (it->speed_drift_mode == CSDRIFT_GLOBAL && cfg->speed_drift) {
		sd_amount = cfg->speed_drift_amount;
	}

	/* A horizontal line turned to follow the frame is drawn as one
	 * continuous ring per row, so it bends around rounded corners. */
	const bool ring = it->kind == CITEM_SHAPE && it->shape == CSHAPE_HLINE && b->align;
	struct item_draw rd = *d;
	if (ring) {
		vec2_set(&rd.half, (float)d->w * 0.5f + 2.0f, (float)d->h * 0.5f + 2.0f);
		rd.line_axis = 0.0f;
	}

	for (int j = 0; j < b->rows; ++j) {
		struct clat_border_row row;
		clat_border_make_row(&fr, ((float)j + 0.5f) / (float)b->rows, cell, &row);
		float instances = ring ? 1.0f : row.total;
		if (row.total < 0.5f || !overdraw_ok(r, d->index, instances, &rd.half, d->w, d->h))
			continue;
		double phase = clat_border_row_phase(r->border_march, (float)j, cell, it->offset_x,
						     cfg->alternating_lines, sd_amount, sd_seed, b->stagger_rows,
						     row.total);
		prepare_draw(r, &rd);
		cfx_set_float(fx, P_BORDER_MODE, 1.0f);
		cfx_set_float(fx, P_RING_MODE, ring ? 1.0f : 0.0f);
		cfx_set_float(fx, P_BORDER_ALIGN, b->align ? 1.0f : 0.0f);
		cfx_set_float(fx, P_OFFSET_INWARD, it->offset_y);
		cfx_set_vec4f(fx, P_ROW_BOX, row.box[0], row.box[1], row.box[2], row.box[3]);
		cfx_set_vec4f(fx, P_ROW_RAD, row.rad[0], row.rad[1], row.rad[2], row.rad[3]);
		cfx_set_vec4f(fx, P_ROW_NS, row.ns[0], row.ns[1], row.ns[2], row.ns[3]);
		cfx_set_vec4f(fx, P_ROW_NA, row.na[0], row.na[1], row.na[2], row.na[3]);
		cfx_set_float(fx, P_ROW_TOTAL, row.total);
		cfx_set_float(fx, P_ROW_INDEX, (float)j);
		cfx_set_float(fx, P_ROW_PHASE, (float)phase);
		draw_instances(r, d->technique, (uint32_t)instances);
	}
}

void cpat_renderer_render_items(struct cpat_renderer *r, uint32_t w, uint32_t h)
{
	const struct cpat_config *cfg = r->cfg;
	if (!cfg || !r->fx.effect || w == 0 || h == 0)
		return;
	uint32_t count = cfg->item_count;

	/* Step and Repeat and Grid share one lattice, sized by the first
	 * enabled item, and hand its cells out between the enabled items. */
	uint32_t step_count = 0;
	float shared_density = 2.0f;
	for (uint32_t i = 0; i < count; ++i) {
		if (cfg->items[i].enabled) {
			if (step_count == 0)
				shared_density = cfg->items[i].density;
			step_count++;
		}
	}
	if (step_count == 0)
		return;

	render_item_sources(r, cfg, count);

	const bool perimeter = cfg->border.active && cfg->border.layout == CBORDER_PERIMETER;
	const bool shared = cfg->layout_mode != CLAYOUT_LAYERED;

	struct cfx_draw_state st;
	cfx_draw_begin(&st);
	uint32_t step_index = 0;
	for (uint32_t i = 0; i < count; ++i) {
		const struct cpat_item_cfg *it = &cfg->items[i];
		if (!it->enabled)
			continue;
		struct item_draw d = {.cfg = cfg,
				      .index = (int)i,
				      .step_index = step_index++,
				      .step_count = step_count,
				      .technique = "DrawShape",
				      .w = w,
				      .h = h};
		if (it->kind == CITEM_IMAGE)
			d.texture = r->gpu[i].texture;
		else if (it->kind == CITEM_SOURCE)
			d.texture = r->gpu[i].frame_texture;
		if (it->kind != CITEM_SHAPE) {
			if (!d.texture)
				continue;
			d.technique = "DrawImage";
		}
		float density = shared ? shared_density : it->density;
		d.cell = fmaxf(100.0f / fmaxf(density, 0.1f), 2.0f);
		item_quad(it, d.texture, d.cell, &d.half, &d.line_axis, &d.reach);

		if (perimeter)
			draw_border_item(r, &d);
		else
			draw_lattice_item(r, &d);
	}
	cfx_draw_end(&st);
}

void cpat_renderer_draw_source_rounded(struct cpat_renderer *r, obs_source_t *filter, uint32_t w, uint32_t h,
				       const struct vec4 *radii)
{
	cfx_set_vec2(&r->fx, P_SRC_SIZE, (float)w, (float)h);
	cfx_set_vec4(&r->fx, P_SRC_RADII, radii);
	obs_source_process_filter_tech_end(filter, r->fx.effect, w, h, "DrawSourceRounded");
}

/* ---------------------------------------------------------- lifecycle */

struct cpat_renderer *cpat_renderer_create(obs_source_t *owner, enum cpat_host host, obs_data_t *settings)
{
	struct cpat_renderer *r = bzalloc(sizeof(*r));
	r->owner = owner;
	r->host = host;
	pthread_mutex_init_value(&r->update_mutex);
	pthread_mutex_init_value(&r->pending_mutex);
	if (pthread_mutex_init(&r->update_mutex, NULL) != 0 || pthread_mutex_init(&r->pending_mutex, NULL) != 0) {
		bfree(r);
		return NULL;
	}
	cfx_effect_load(&r->fx, "pattern.effect", param_names, P_COUNT);
	cpat_settings_migrate(settings);
	cpat_renderer_update(r, settings);
	return r;
}

void cpat_renderer_destroy(struct cpat_renderer *r)
{
	if (!r)
		return;
	obs_enter_graphics();
	for (int i = 0; i < MAX_ITEMS; ++i) {
		struct cpat_gpu_item *g = &r->gpu[i];
		release_item_source(r, i);
		if (g->texture)
			gs_texture_destroy(g->texture);
		bfree(g->media_key);
		bfree(g->source_uuid);
		bfree(g->source_name);
	}
	obs_leave_graphics();
	config_free(r->cfg);
	config_free(r->pending);
	for (int i = 0; i < MAX_ITEMS; ++i) {
		cpat_media_svg_free(r->svg_cache[i].svg);
		bfree(r->svg_cache[i].path);
		bfree(r->submitted_keys[i]);
	}
	cfx_effect_free(&r->fx);
	pthread_mutex_destroy(&r->update_mutex);
	pthread_mutex_destroy(&r->pending_mutex);
	bfree(r);
}

void cpat_renderer_load(struct cpat_renderer *r, obs_data_t *settings)
{
	char key[64];
	bool changed = false;
	for (int i = 0; i < MAX_ITEMS; ++i)
		changed |= !obs_data_has_user_value(settings, cpat_item_key(key, sizeof(key), i, "source")) &&
			   *obs_data_get_string(settings, cpat_item_key(key, sizeof(key), i, "source_name"));
	cpat_settings_migrate(settings);
	if (changed)
		cpat_renderer_update(r, settings);
}

uint32_t cpat_renderer_width(const struct cpat_renderer *r)
{
	return (uint32_t)os_atomic_load_long(&r->width);
}

uint32_t cpat_renderer_height(const struct cpat_renderer *r)
{
	return (uint32_t)os_atomic_load_long(&r->height);
}

obs_source_t *cpat_renderer_owner(const struct cpat_renderer *r)
{
	return r ? r->owner : NULL;
}

void cpat_renderer_request_motion_reset(struct cpat_renderer *r)
{
	if (r)
		os_atomic_set_long(&r->reset_motion, 1);
}
