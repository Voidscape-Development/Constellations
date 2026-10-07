/*
Constellations
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

#include "shape-defs.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Constellations — tiled shape backgrounds, filters, and shape-based transitions for OBS.";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Constellations";
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "Constellations loading (version %s)", PLUGIN_VERSION);
	constellations_register_pattern_source();
	constellations_register_topography_source();
	constellations_register_topography_filter();
	constellations_register_skylines();
	constellations_register_pattern_filter();
	constellations_register_pattern_border();
	constellations_register_shapes_transition();
	obs_log(LOG_INFO, "Constellations loaded");
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "Constellations unloaded");
}
