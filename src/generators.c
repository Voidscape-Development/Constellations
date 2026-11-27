/*
Constellations - Flow Field, Starfield and Aurora backgrounds
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "gen-source.h"

#define L(k) "Constellations." k

/* clang-format off */

static const struct cgen_param flow_params[] = {
	{"field_scale", L("Flow.FieldScale"), CGEN_SLIDER, 50.0, 2000.0, 1.0, 420.0, " px"},
	{"turbulence",  L("Flow.Turbulence"), CGEN_SLIDER, 0.0, 3.0, 0.01, 0.8, NULL},
	{"evolve",      L("Flow.Evolve"),     CGEN_SLIDER, 0.0, 2.0, 0.01, 0.3, NULL},
	{"spacing",     L("Flow.Spacing"),    CGEN_SLIDER, 8.0, 120.0, 1.0, 26.0, " px"},
	{"density",     L("Flow.Density"),    CGEN_SLIDER, 0.0, 1.0, 0.01, 0.55, NULL},
	{"trail",       L("Flow.Trail"),      CGEN_SLIDER, 20.0, 600.0, 1.0, 220.0, " px"},
	{"tail",        L("Flow.Tail"),       CGEN_SLIDER, 0.05, 1.0, 0.01, 0.45, NULL},
	{"thickness",   L("Flow.Thickness"),  CGEN_SLIDER, 0.5, 8.0, 0.1, 1.6, " px"},
	{"glow",        L("Flow.Glow"),       CGEN_SLIDER, 0.0, 1.0, 0.01, 0.35, NULL},
	{"color_mode",  L("Flow.ColorMode"),  CGEN_LIST, 0.0, 2.0, 1.0, 0.0, NULL},
};

static const struct cgen_param star_params[] = {
	{"density",    L("Stars.Density"),    CGEN_SLIDER, 40.0, 600.0, 1.0, 220.0, NULL},
	{"star_size",  L("Stars.Size"),       CGEN_SLIDER, 0.5, 8.0, 0.1, 1.6, " px"},
	{"warp",       L("Stars.Warp"),       CGEN_SLIDER, 0.0, 1.0, 0.01, 0.05, NULL},
	{"twinkle",    L("Stars.Twinkle"),    CGEN_SLIDER, 0.0, 1.0, 0.01, 0.4, NULL},
	{"color_mix",  L("Stars.ColorMix"),   CGEN_SLIDER, 0.0, 1.0, 0.01, 0.35, NULL},
	{"brightness", L("Stars.Brightness"), CGEN_SLIDER, 0.1, 3.0, 0.01, 1.2, NULL},
	{"centre_x",   L("Stars.CentreX"),    CGEN_SLIDER, -50.0, 150.0, 0.5, 50.0, " %"},
	{"centre_y",   L("Stars.CentreY"),    CGEN_SLIDER, -50.0, 150.0, 0.5, 50.0, " %"},
};

static const struct cgen_param aurora_params[] = {
	{"mode",      L("Aurora.Mode"),      CGEN_LIST, 0.0, 1.0, 1.0, 0.0, NULL},
	{"scale",     L("Aurora.Scale"),     CGEN_SLIDER, 0.2, 3.0, 0.01, 1.0, NULL},
	{"sharpness", L("Aurora.Sharpness"), CGEN_SLIDER, 0.5, 6.0, 0.01, 2.2, NULL},
	{"warp",      L("Aurora.Warp"),      CGEN_SLIDER, 0.0, 2.0, 0.01, 0.6, NULL},
	{"height",    L("Aurora.Height"),    CGEN_SLIDER, 0.0, 1.0, 0.01, 0.6, NULL},
	{"intensity", L("Aurora.Intensity"), CGEN_SLIDER, 0.0, 3.0, 0.01, 1.2, NULL},
	{"grain",     L("Aurora.Grain"),     CGEN_SLIDER, 0.0, 1.0, 0.01, 0.15, NULL},
	{"opacity",   L("Aurora.Opacity"),   CGEN_SLIDER, 0.0, 1.0, 0.01, 1.0, NULL},
};

static const struct cgen_desc generators[] = {
	{"constellations_flowfield_source", "constellations_flowfield_filter",
	 L("FlowFieldSource.Name"), L("FlowFieldFilter.Name"), "flow_field.effect",
	 flow_params, OBS_COUNTOF(flow_params),
	 {0x070A14, 0x5EEAD4, 0x60A5FA, 0xA78BFA, 0xF472B6, 0xFDE68A}, 1.0},
	{"constellations_starfield_source", "constellations_starfield_filter",
	 L("StarfieldSource.Name"), L("StarfieldFilter.Name"), "starfield.effect",
	 star_params, OBS_COUNTOF(star_params),
	 {0x02030A, 0xBFD7FF, 0xFFE3B0, 0x9AD0FF, 0xFFB3C7, 0xD9C4FF}, 1.0},
	{"constellations_aurora_source", "constellations_aurora_filter",
	 L("AuroraSource.Name"), L("AuroraFilter.Name"), "aurora.effect",
	 aurora_params, OBS_COUNTOF(aurora_params),
	 {0x050B16, 0x22D3A6, 0x3B82F6, 0xA855F7, 0xEC4899, 0xF59E0B}, 0.3},
};

/* clang-format on */

void constellations_register_generators(void)
{
	for (size_t i = 0; i < OBS_COUNTOF(generators); ++i)
		cgen_register(&generators[i]);
}
