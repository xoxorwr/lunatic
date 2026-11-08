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
    double inset = MIN(24, height / 2);
    return CLAMP(icon_y - menu_y, inset, MAX(inset, height - inset));
}

void quicklist_paint_frame(cairo_t *cr, double width, double height, gboolean pointer_left,
                           double arrow_y)
{
    double left = QUICKLIST_MARGIN + (pointer_left ? QUICKLIST_POINTER : 0);
    double right = width - QUICKLIST_MARGIN - (pointer_left ? 0 : QUICKLIST_POINTER);
    double top = QUICKLIST_MARGIN, bottom = height - QUICKLIST_MARGIN;
    double radius = MIN(9, MAX(3, (bottom - top) / 5));
    double arrow_half = MIN(10, MAX(1, (bottom - top - 2 * radius) / 2));
    double tip = CLAMP(quicklist_arrow_y(arrow_y, 0, height), top + radius + arrow_half,
                       bottom - radius - arrow_half);
    cairo_save(cr);
    cairo_move_to(cr, left + radius, top);
    cairo_line_to(cr, right - radius, top);
    cairo_arc(cr, right - radius, top + radius, radius, -G_PI / 2, 0);
    if (!pointer_left) {
        cairo_line_to(cr, right, tip - arrow_half);
        cairo_line_to(cr, width - QUICKLIST_MARGIN, tip);
        cairo_line_to(cr, right, tip + arrow_half);
    }
    cairo_line_to(cr, right, bottom - radius);
    cairo_arc(cr, right - radius, bottom - radius, radius, 0, G_PI / 2);
    cairo_line_to(cr, left + radius, bottom);
    cairo_arc(cr, left + radius, bottom - radius, radius, G_PI / 2, G_PI);
    if (pointer_left) {
        cairo_line_to(cr, left, tip + arrow_half);
        cairo_line_to(cr, QUICKLIST_MARGIN, tip);
        cairo_line_to(cr, left, tip - arrow_half);
    }
    cairo_line_to(cr, left, top + radius);
    cairo_arc(cr, left + radius, top + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, .18);
    cairo_set_line_width(cr, 6);
    cairo_stroke_preserve(cr);
    /* Ambiance gtk-3.20/gtk-main.css: dark_bg_color #3c3b37.
     * gtk-widgets.css uses shade(dark_bg_color, 1.08) for native menus. */
    cairo_set_source_rgb(cr, 65 / 255.0, 64 / 255.0, 59 / 255.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 48 / 255.0, 47 / 255.0, 44 / 255.0);
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
    /* Ambiance selected_bg_color, with a quiet persistent active marker. */
    if (hovered)
        cairo_set_source_rgb(cr, 240 / 255.0, 119 / 255.0, 70 / 255.0);
    else
        cairo_set_source_rgba(cr, 1, 1, 1, .035);
    cairo_fill_preserve(cr);
    if (active) {
        cairo_new_path(cr);
        rounded_rect(cr, 6, 5, 3, MAX(3, height - 10), 1.5);
        cairo_set_source_rgba(cr, 240 / 255.0, 119 / 255.0, 70 / 255.0, .85);
        cairo_fill(cr);
    } else {
        cairo_new_path(cr);
    }
    cairo_restore(cr);
}
