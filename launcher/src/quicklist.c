/* SPDX-License-Identifier: GPL-3.0-only
 * Unity-inspired quicklist decoration, shared by GTK and headless previews.
 */
#include "quicklist.h"
#include <math.h>

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, x + radius, y + h - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

double quicklist_arrow_y(double icon_y, double menu_y, double height)
{
    return CLAMP(icon_y - menu_y, 24, MAX(24, height - 24));
}

void quicklist_paint_frame(cairo_t *cr, double width, double height, gboolean pointer_left,
                           double arrow_y)
{
    double left = QUICKLIST_MARGIN + (pointer_left ? QUICKLIST_POINTER : 0);
    double right = width - QUICKLIST_MARGIN - (pointer_left ? 0 : QUICKLIST_POINTER);
    double top = QUICKLIST_MARGIN, bottom = height - QUICKLIST_MARGIN, radius = 9;
    double tip = quicklist_arrow_y(arrow_y, 0, height);
    cairo_save(cr);
    cairo_move_to(cr, left + radius, top);
    cairo_line_to(cr, right - radius, top);
    cairo_arc(cr, right - radius, top + radius, radius, -G_PI / 2, 0);
    if (!pointer_left) {
        cairo_line_to(cr, right, tip - 10);
        cairo_line_to(cr, width - QUICKLIST_MARGIN, tip);
        cairo_line_to(cr, right, tip + 10);
    }
    cairo_line_to(cr, right, bottom - radius);
    cairo_arc(cr, right - radius, bottom - radius, radius, 0, G_PI / 2);
    cairo_line_to(cr, left + radius, bottom);
    cairo_arc(cr, left + radius, bottom - radius, radius, G_PI / 2, G_PI);
    if (pointer_left) {
        cairo_line_to(cr, left, tip + 10);
        cairo_line_to(cr, QUICKLIST_MARGIN, tip);
        cairo_line_to(cr, left, tip - 10);
    }
    cairo_line_to(cr, left, top + radius);
    cairo_arc(cr, left + radius, top + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, .18);
    cairo_set_line_width(cr, 6);
    cairo_stroke_preserve(cr);
    cairo_pattern_t *fill = cairo_pattern_create_linear(0, top, 0, bottom);
    cairo_pattern_add_color_stop_rgba(fill, 0, .18, .15, .25, .99);
    cairo_pattern_add_color_stop_rgba(fill, 1, .10, .09, .15, .99);
    cairo_set_source(cr, fill);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(fill);
    cairo_set_source_rgba(cr, .76, .62, .94, .62);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_restore(cr);
}

void quicklist_paint_row(cairo_t *cr, double width, double height, gboolean active,
                         gboolean hovered)
{
    if (!active && !hovered)
        return;
    cairo_save(cr);
    rounded_rect(cr, 3, 1, MAX(1, width - 6), MAX(1, height - 2), 3);
    if (active)
        cairo_set_source_rgba(cr, .46, .32, .22, hovered ? .95 : .70);
    else
        cairo_set_source_rgba(cr, .57, .30, .76, .40);
    cairo_fill_preserve(cr);
    if (active) {
        cairo_set_source_rgba(cr, 1, .66, .32, .30);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        rounded_rect(cr, 6, 5, 3, MAX(3, height - 10), 1.5);
        cairo_set_source_rgb(cr, 1, .53, .20);
        cairo_fill(cr);
    } else {
        cairo_new_path(cr);
    }
    cairo_restore(cr);
}
