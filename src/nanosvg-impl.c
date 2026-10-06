/*
Constellations - nanosvg implementation unit
Copyright (C) 2026 Eion Dailey <Eiondailey@live.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

/* The vendored nanosvg headers are compiled here, on their own and with
 * warnings disabled (-w from CMakeLists.txt on GCC/Clang, the pragma below
 * on MSVC), so third-party code never trips the warnings-as-errors build. */

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif
