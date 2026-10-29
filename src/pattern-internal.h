/*
Constellations - Pattern renderer internals shared with its settings code
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#pragma once

#include "pattern-renderer.h"

#include <stdio.h>

/* Settings keys of item i (0-based) are "item_<i+1>_<name>". */
static inline const char *cpat_item_key(char *buf, size_t size, int i, const char *name)
{
	snprintf(buf, size, "item_%d_%s", i + 1, name);
	return buf;
}

/* Current settings layout. 1: items referenced sources by name; 2: by UUID
 * ("item_N_source"), with the name kept as a fallback. */
#define CPAT_SETTINGS_VERSION 2

obs_source_t *cpat_renderer_owner(const struct cpat_renderer *r);
/* Restarts the pattern's motion from its anchor on the next tick. */
void cpat_renderer_request_motion_reset(struct cpat_renderer *r);
/* Links items saved by name (settings version 1) to their source's UUID. */
void cpat_settings_migrate(obs_data_t *settings);
