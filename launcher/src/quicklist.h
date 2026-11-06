/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <cairo.h>
#include <glib.h>

#define QUICKLIST_POINTER 12
#define QUICKLIST_MARGIN 4

void quicklist_paint_frame(cairo_t *cr, double width, double height, gboolean pointer_left,
                           double arrow_y);
void quicklist_paint_row(cairo_t *cr, double width, double height, gboolean active,
                         gboolean hovered);
double quicklist_arrow_y(double icon_y, double menu_y, double height);
