// SPDX-License-Identifier: LGPL-2.1-or-later
// TheCAD standalone PlaneGCS integration, 2026-11-04.
#pragma once
#if defined(_WIN32)
# if defined(THECAD_PLANEGCS_BUILD)
#  define SketcherExport __declspec(dllexport)
# else
#  define SketcherExport __declspec(dllimport)
# endif
#else
# define SketcherExport
#endif
