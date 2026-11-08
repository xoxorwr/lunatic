/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gdk/gdk.h>

typedef enum { TILE_COLUMNS, TILE_ROWS, TILE_GRID, TILE_FOCUS, TILE_SMART } TileLayout;

/* Rectangles describe outer window frames; focus is the first window. */
gboolean window_layout_plan(TileLayout layout, const GdkRectangle *area, guint count,
                            GdkRectangle *rects);
