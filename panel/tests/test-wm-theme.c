/* Copyright (C) 2026 xoxorwr. SPDX-License-Identifier: GPL-3.0-only */
#include "wm-theme.h"
#include <glib/gstdio.h>
#include <stdint.h>
#include <string.h>

static char *fixture(void)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp("lunatic-wm-theme-XXXXXX", &error);
    g_assert_no_error(error);
    return directory;
}
static void write_file(const char *directory, const char *name, const char *contents)
{
    char *path = g_build_filename(directory, name, NULL);
    GError *error = NULL;
    g_assert_true(g_file_set_contents(path, contents, -1, &error));
    g_assert_no_error(error);
    g_free(path);
}
static void remove_fixture(char *directory)
{
    GDir *dir = g_dir_open(directory, 0, NULL);
    const char *name;
    while ((name = g_dir_read_name(dir))) {
        char *path = g_build_filename(directory, name, NULL);
        g_assert_cmpint(g_unlink(path), ==, 0);
        g_free(path);
    }
    g_dir_close(dir);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(directory);
}
static guint32 pixel(cairo_surface_t *surface, gint x)
{
    g_assert_nonnull(surface);
    cairo_surface_flush(surface);
    guint32 value;
    memcpy(&value, cairo_image_surface_get_data(surface) + x * 4, 4);
    return value;
}
static const char *red = "/* XPM */\nstatic char *x[] = {\n\"2 1 1 1\",\n\"x c #ff0000\",\n\"xx\"};\n";
static const char *green = "/* XPM */\nstatic char *x[] = {\n\"2 1 1 1\",\n\"x c #00ff00\",\n\"xx\"};\n";

static void symbols_and_states(void)
{
    char *directory = fixture();
    write_file(directory, "themerc", "active_text_color=active_color_2\nbutton_spacing=4\n");
    write_file(directory, "close-active.xpm", "/* XPM */\nstatic char *x[] = {\n\"2 1 2 1\",\n\"x c #ffffff s active_text_color\",\n\". c None\",\n\"x.\"};\n");
    write_file(directory, "close-prelight.xpm", red);
    write_file(directory, "hide-active.xpm", green);
    write_file(directory, "maximize-active.xpm", green);
    write_file(directory, "maximize-toggled-active.xpm", red);
    GHashTable *colors = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(colors, "active_color_2", "#112233");
    PanelWmTheme *theme = panel_wm_theme_load(directory, colors, 1);
    g_assert_nonnull(theme);
    cairo_surface_t *normal = panel_wm_theme_frame(theme, PANEL_WINDOW_CLOSE, PANEL_WM_NORMAL);
    g_assert_cmphex(pixel(normal, 0), ==, 0xff112233);
    g_assert_cmphex(pixel(normal, 1), ==, 0);
    g_assert_cmphex(pixel(panel_wm_theme_frame(theme, PANEL_WINDOW_CLOSE, PANEL_WM_HOVER), 0), ==, 0xffff0000);
    g_assert_true(panel_wm_theme_frame(theme, PANEL_WINDOW_CLOSE, PANEL_WM_PRESSED) == normal);
    g_assert_true(panel_wm_theme_frame(theme, PANEL_WINDOW_CLOSE, PANEL_WM_DISABLED) == normal);
    g_assert_cmphex(pixel(panel_wm_theme_frame(theme, PANEL_WINDOW_RESTORE, PANEL_WM_NORMAL), 0), ==, 0xffff0000);
    g_assert_cmpint(panel_wm_theme_spacing(theme), ==, 4);
    panel_wm_theme_unref(theme);
    theme = panel_wm_theme_load(directory, colors, 2);
    double sx, sy;
    cairo_surface_get_device_scale(panel_wm_theme_frame(theme, PANEL_WINDOW_CLOSE, PANEL_WM_NORMAL), &sx, &sy);
    g_assert_cmpfloat(sx, ==, 2); g_assert_cmpfloat(sy, ==, 2);
    g_assert_cmpint(panel_wm_theme_spacing(theme), ==, 2);
    panel_wm_theme_unref(theme);
    g_hash_table_unref(colors);
    remove_fixture(directory);
}
static void image_overlay(void)
{
    char *directory = fixture();
    write_file(directory, "close-active.xpm", green);
    char *path = g_build_filename(directory, "close-active.png", NULL);
    cairo_surface_t *image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(image);
    cairo_set_source_rgb(cr, 1, 0, 0); cairo_paint(cr); cairo_destroy(cr);
    g_assert_cmpint(cairo_surface_write_to_png(image, path), ==, CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(image);
    PanelWmTheme *theme = panel_wm_theme_load(directory, NULL, 1);
    cairo_surface_t *frame = panel_wm_theme_frame(theme, PANEL_WINDOW_CLOSE, PANEL_WM_NORMAL);
    g_assert_cmphex(pixel(frame, 0), ==, 0xffff0000);
    g_assert_cmphex(pixel(frame, 1), ==, 0xff00ff00);
    panel_wm_theme_unref(theme);
    g_free(path);
    remove_fixture(directory);
}
static void malformed(void)
{
    char *directory = fixture();
    write_file(directory, "close-active.xpm", "\"99999999 1 1 1\"\n\"x c red\"\n\"x\"");
    g_assert_null(panel_wm_theme_load(directory, NULL, 1));
    write_file(directory, "close-active.xpm", "\"2 1 1 1\"\n\"x c red\"\n\"x\"");
    g_assert_null(panel_wm_theme_load(directory, NULL, 1));
    remove_fixture(directory);
}
static void ordering(void)
{
    PanelWindowCommand order[3];
    g_assert_cmpuint(panel_wm_button_order("O|SHMC", order), ==, 3);
    g_assert_cmpint(order[0], ==, PANEL_WINDOW_MINIMIZE);
    g_assert_cmpint(order[1], ==, PANEL_WINDOW_RESTORE);
    g_assert_cmpint(order[2], ==, PANEL_WINDOW_CLOSE);
    g_assert_cmpuint(panel_wm_button_order("CH|MCC", order), ==, 3);
    g_assert_cmpint(order[0], ==, PANEL_WINDOW_CLOSE);
    g_assert_cmpuint(panel_wm_button_order("O|S", order), ==, 0);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/wm-theme/symbols-states-scale", symbols_and_states);
    g_test_add_func("/wm-theme/image-overlay", image_overlay);
    g_test_add_func("/wm-theme/malformed", malformed);
    g_test_add_func("/wm-theme/order", ordering);
    return g_test_run();
}
