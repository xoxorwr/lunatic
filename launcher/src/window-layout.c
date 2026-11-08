/* SPDX-License-Identifier: GPL-3.0-only */
#include "window-layout.h"
#include <math.h>

gboolean window_layout_plan(TileLayout layout, const GdkRectangle *area, guint count,
                            GdkRectangle *rects)
{
    if (!count || area->width < 1 || area->height < 1)
        return FALSE;
    if (layout == TILE_SMART)
        layout = count <= 2 ? (area->width >= area->height ? TILE_COLUMNS : TILE_ROWS) : TILE_GRID;
    if (layout == TILE_FOCUS && count > 1) {
        int width = area->width * .65;
        rects[0] = (GdkRectangle){area->x, area->y, width, area->height};
        GdkRectangle stack = {area->x + width, area->y, area->width - width, area->height};
        return window_layout_plan(TILE_ROWS, &stack, count - 1, rects + 1);
    }
    guint columns = layout == TILE_COLUMNS ? count : 1;
    if (layout == TILE_GRID) {
        /* Prefer cells close to a 4:3 window, including portrait monitors. */
        double best = G_MAXDOUBLE;
        for (guint c = 1; c <= count; c++) {
            guint rows = (count + c - 1) / c;
            double ratio = (double)area->width / c / ((double)area->height / rows);
            double score = fabs(log(ratio / (4.0 / 3))) + .15 * (rows * c - count);
            if (score < best) {
                best = score;
                columns = c;
            }
        }
    }
    guint rows = (count + columns - 1) / columns;
    if (columns > (guint)area->width || rows > (guint)area->height)
        return FALSE;
    for (guint i = 0; i < count; i++) {
        guint row = i / columns, col = i % columns;
        guint in_row = MIN(columns, count - row * columns);
        int x = (gint64)area->width * col / in_row;
        int y = (gint64)area->height * row / rows;
        rects[i] =
            (GdkRectangle){area->x + x, area->y + y, (gint64)area->width * (col + 1) / in_row - x,
                           (gint64)area->height * (row + 1) / rows - y};
    }
    return TRUE;
}
