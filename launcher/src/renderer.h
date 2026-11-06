/* C port of unity-shared/IconRenderer.cpp. SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "launcher-core.h"
#include "settings.h"
#include <gtk/gtk.h>

typedef struct {
    void *state;
} IconRenderer;
typedef struct {
    double color[3], opacity;
    gboolean right;
    int dock_width; /* 0: use full frame; otherwise remaining pixels are click-through gutter. */
    LauncherHideAnimation hide_animation;
} RenderStyle;
typedef struct {
    GdkPixbuf *badge, *window_count;
    gint64 count;
    guint windows;
} RenderCache;
typedef struct {
    guint draw_calls, image_updates, vertices;
    gsize atlas_bytes, buffer_bytes, image_bytes, frame_bytes;
    guint cached_images;
} RenderStats;
typedef struct {
    GdkPixbuf *image;
    double background[3], glow[3];
    double backlight, glow_intensity, alpha, rotation_z, bounce;
    gboolean active, urgent, running_here, selected, system;
    guint running;
    gint64 count;
    gboolean count_visible, progress_visible;
    double progress;
    char shortcut;
} RenderArg;

gboolean icon_renderer_init(IconRenderer *renderer, int tile, int image, int scale, GError **error);
gboolean icon_renderer_realize(IconRenderer *renderer, GError **error);
void icon_renderer_unrealize(IconRenderer *renderer);
void icon_renderer_clear(IconRenderer *renderer);
void icon_renderer_forget(IconRenderer *renderer, GdkPixbuf *image);
void icon_renderer_cache_clear(IconRenderer *renderer, RenderCache *cache);
void icon_renderer_begin(IconRenderer *renderer, int width, int height, double hide_progress);
void icon_renderer_set_style(IconRenderer *renderer, const RenderStyle *style);
void icon_renderer_draw(IconRenderer *renderer, const RenderArg *arg, RenderCache *cache,
                        const LayoutIcon *layout, double center_x, double extent, double width);
/* Finish app commands and reserve the remaining area for fixed system icons. */
void icon_renderer_shelf(IconRenderer *renderer, double top);
void icon_renderer_beacon(IconRenderer *renderer, double center_y, const double color[3],
                          double intensity);
void icon_renderer_end(IconRenderer *renderer);
#ifndef LAUNCHER_RENDER_GL
void icon_renderer_paint(IconRenderer *renderer, cairo_t *cr);
cairo_surface_t *icon_renderer_surface(IconRenderer *renderer);
#endif
RenderStats icon_renderer_stats(IconRenderer *renderer);
GdkPixbuf *launcher_resource_icon(const char *name, int size, GError **error);
