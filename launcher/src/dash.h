/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>

typedef struct {
    GDesktopAppInfo *app;
    char *name, *search;
} DashApp;
typedef struct {
    GPtrArray *apps;
    GArray *matches;
} DashModel;
typedef struct {
    int x, width, height, grid_x, grid_y, grid_width, grid_height, columns;
    double cell_width;
} DashGeometry;
#define DASH_CELL_HEIGHT 112
#define DASH_ICON_CACHE_LIMIT 96

DashModel *dash_model_new(GList *apps);
void dash_model_free(DashModel *model);
void dash_model_filter(DashModel *model, const char *query);
DashApp *dash_model_match(DashModel *model, guint index);
DashGeometry dash_geometry(int width, int height, gboolean right);
int dash_grid_hit(const DashGeometry *geometry, double x, double y, double scroll, guint count);
int dash_grid_navigate(const DashGeometry *geometry, int selected, guint count, int delta,
                       double *scroll);
double dash_scroll_limit(const DashGeometry *geometry, guint count);
/* Small cached backdrop; the caller owns the returned surface. */
cairo_surface_t *dash_blur_background(GdkPixbuf *source, int logical_width, int logical_height);
void dash_paint_frame(cairo_t *cr, int width, int height, const DashGeometry *geometry,
                      const double color[3], double opacity, cairo_surface_t *backdrop,
                      guint count);
void dash_paint_tile(cairo_t *cr, double width, const char *name, GdkPixbuf *icon, int scale,
                     gboolean selected, gboolean hovered, gboolean pinned);

typedef struct LauncherDash LauncherDash;
typedef void (*DashAppAction)(GDesktopAppInfo *app, gpointer data);
typedef gboolean (*DashAppPinned)(GDesktopAppInfo *app, gpointer data);
LauncherDash *launcher_dash_new(DashAppAction launch, DashAppAction pin, DashAppPinned pinned,
                                gpointer data);
GtkWidget *launcher_dash_window(LauncherDash *dash);
void launcher_dash_set_apps(LauncherDash *dash, GList *apps);
void launcher_dash_clear_icons(LauncherDash *dash);
void launcher_dash_set_background(LauncherDash *dash, const double color[3], double opacity);
void launcher_dash_toggle(LauncherDash *dash, GtkWindow *parent, const GdkRectangle *bounds,
                          gboolean right);
void launcher_dash_hide(LauncherDash *dash);
void launcher_dash_free(LauncherDash *dash);
