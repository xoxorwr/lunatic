/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <cairo.h>
#include <glib.h>

/* theme/gtk/Ambiance/gtk-3.20/gtk-main.css palette, scoped to our popups. */
#define QUICKLIST_PALETTE                                                                          \
    "@define-color quicklist_bg #3c3b37;"                                                          \
    "@define-color quicklist_fg #dfdbd2;"                                                          \
    "@define-color quicklist_selected #f07746;"                                                    \
    "@define-color quicklist_muted mix(@quicklist_bg,@quicklist_fg,0.85);"

#define QUICKLIST_POINTER 12
#define QUICKLIST_MARGIN 4

void quicklist_paint_frame(cairo_t *cr, double width, double height, gboolean pointer_left,
                           double arrow_y);
void quicklist_paint_row(cairo_t *cr, double width, double height, gboolean active,
                         gboolean hovered);
double quicklist_arrow_y(double icon_y, double menu_y, double height);
