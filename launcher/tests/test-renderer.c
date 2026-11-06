/* SPDX-License-Identifier: GPL-3.0-only */
#include "renderer.h"
#ifdef LAUNCHER_RENDER_GL
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#endif
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>

static IconRenderer *current_renderer;

static void read_pixels(int x, int y, int w, int h, guchar *pixels)
{
#ifdef LAUNCHER_RENDER_GL
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#else
    cairo_surface_t *surface = icon_renderer_surface(current_renderer);
    const guint32 *data = (const guint32 *)cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;
    int height = cairo_image_surface_get_height(surface);
    for (int row = 0; row < h; row++)
        for (int column = 0; column < w; column++) {
            guint32 pixel = data[(height - 1 - y - row) * stride + x + column];
            guchar *out = pixels + (row * w + column) * 4;
            out[0] = pixel >> 16;
            out[1] = pixel >> 8;
            out[2] = pixel;
            out[3] = pixel >> 24;
        }
#endif
}

static void finish(void)
{
#ifdef LAUNCHER_RENDER_GL
    glFinish();
#endif
}

static guint rendering_error(void)
{
#ifdef LAUNCHER_RENDER_GL
    return glGetError();
#else
    return cairo_surface_status(icon_renderer_surface(current_renderer));
#endif
}

static void draw(IconRenderer *renderer, GdkPixbuf *icon, RenderCache *cache, double fold,
                 gboolean badge, int width, int height)
{
    LayoutIcon layout[13] = {0};
    for (guint i = 0; i < 13; i++) {
        layout[i].visible = 1;
        layout[i].shelf = i == 12;
    }
    layout[3].active = 1;
    launcher_layout(layout, 13, 54, height, fold, 0);
    icon_renderer_begin(renderer, width, height, 0);
    for (guint i = 0; i < 13; i++) {
        RenderArg arg = {.image = icon,
                         .background = {.3, .6, .9},
                         .glow = {.35, .65, 1},
                         .alpha = i == 7 ? .5 : 1,
                         .rotation_z = i == 2 ? .13 : 0,
                         .selected = i == 8,
                         .system = i == 12,
                         .glow_intensity = i == 6 ? .8 : 0,
                         .urgent = i == 6,
                         .backlight = i ? .9 : 0,
                         .running = i ? i % 4 : 0,
                         .active = i == 3,
                         .running_here = TRUE,
                         .count = 12,
                         .count_visible = badge && i == 3,
                         .progress_visible = i == 4,
                         .progress = .6,
                         .shortcut = i == 5 ? '5' : 0};
        if (i == 12)
            icon_renderer_shelf(renderer, layout[i].hit_start - 5);
        icon_renderer_draw(renderer, &arg, &cache[i], &layout[i], (width - 1) / 2.0, height, width);
    }
    icon_renderer_end(renderer);
}

static void shelf_clipping(IconRenderer *renderer, int scale)
{
    RenderStyle style = {.color = {0, 0, 0}, .opacity = 1};
    icon_renderer_set_style(renderer, &style);
    GdkPixbuf *icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48, 48);
    gdk_pixbuf_fill(icon, 0xffffffff);
    RenderArg arg = {.image = icon, .alpha = 1};
    RenderCache cache = {0};
    LayoutIcon layout = {.visible = 1, .center = 400};
    icon_renderer_begin(renderer, 61, 480, 0);
    icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
    icon_renderer_shelf(renderer, 390);
    icon_renderer_end(renderer);
    finish();
    /* The app crosses the shelf boundary: the upper part remains, the lower
     * part must show only the black dock background, including on HiDPI. */
    guchar pixel[4];
    read_pixels(30 * scale, (480 - 385) * scale, 1, 1, pixel);
    g_assert_cmpuint(pixel[0], >, 240);
    read_pixels(30 * scale, (480 - 400) * scale, 1, 1, pixel);
    g_assert_cmpuint(pixel[0], ==, 0);
    g_assert_cmpuint(rendering_error(), ==, 0);
    icon_renderer_cache_clear(renderer, &cache);
    icon_renderer_forget(renderer, icon);
    g_object_unref(icon);
}

static void screenshot(const char *filename, int w, int h)
{
    guchar *pixels = g_malloc((gsize)w * h * 4);
    read_pixels(0, 0, w, h, pixels);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    guint32 *out = (guint32 *)cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const guchar *v = pixels + ((gsize)(h - 1 - y) * w + x) * 4;
            out[y * stride + x] =
                ((guint32)v[3] << 24) | ((guint32)v[0] << 16) | ((guint32)v[1] << 8) | v[2];
        }
    cairo_surface_mark_dirty(surface);
    g_assert_cmpint(cairo_surface_write_to_png(surface, filename), ==, CAIRO_STATUS_SUCCESS);
    cairo_surface_destroy(surface);
    g_free(pixels);
}

static void style_tests(IconRenderer *renderer, int scale)
{
    guchar p[4];
    for (int right = 0; right <= 1; right++)
        for (int animation = HIDE_SLIDE; animation <= HIDE_FADE_SLIDE; animation++) {
            RenderStyle style = {
                .color = {.2, .6, .8}, .opacity = .25, .right = right, .hide_animation = animation};
            icon_renderer_set_style(renderer, &style);
            icon_renderer_begin(renderer, 61, 480, 0);
            icon_renderer_end(renderer);
            read_pixels(30 * scale, 240 * scale, 1, 1, p);
            g_assert_cmpint(p[3], >=, 63);
            g_assert_cmpint(p[3], <=, 65);
            g_assert_cmpint(p[0], >=, 11);
            g_assert_cmpint(p[0], <=, 14);
            icon_renderer_begin(renderer, 61, 480, .5);
            icon_renderer_end(renderer);
            int inside = animation == HIDE_FADE ? 30 : right ? 50 : 10;
            read_pixels(inside * scale, 240 * scale, 1, 1, p);
            int expected = animation == HIDE_SLIDE ? 64 : animation == HIDE_FADE ? 32 : 48;
            g_assert_cmpint(abs(p[3] - expected), <=, 1);
            if (animation != HIDE_FADE) {
                read_pixels((right ? 10 : 50) * scale, 240 * scale, 1, 1, p);
                g_assert_cmpint(p[3], ==, 0);
            }
            icon_renderer_begin(renderer, 61, 480, 1);
            icon_renderer_end(renderer);
            read_pixels(30 * scale, 240 * scale, 1, 1, p);
            g_assert_cmpint(p[3], ==, 0);
            /* Beacon survives all hide transforms, on either monitor edge.
             * The remainder of a hidden dock stays transparent. */
            const double amber[3] = {1, .65, .15};
            for (int frame = 0; frame < 2; frame++) {
                icon_renderer_begin(renderer, 61, 480, 1);
                icon_renderer_beacon(renderer, 240, amber, .8);
                icon_renderer_end(renderer);
                read_pixels((right ? 60 : 0) * scale, 240 * scale, 1, 1, p);
                g_assert_cmpint(p[3], >, 150);
                g_assert_cmpint(p[0], >, p[1]);
                g_assert_cmpint(p[1], >, p[2]);
                read_pixels(30 * scale, 240 * scale, 1, 1, p);
                g_assert_cmpint(p[3], ==, 0);
                if (frame)
                    g_assert_cmpuint(icon_renderer_stats(renderer).image_updates, ==, 0);
            }
        }
    RenderStyle reset = {.color = {95 / 255.0, 18 / 255.0, 45 / 255.0},
                         .opacity = .6667,
                         .hide_animation = HIDE_FADE_SLIDE};
    icon_renderer_set_style(renderer, &reset);
}

static void attention_visibility(IconRenderer *renderer, int scale, const char *directory)
{
    GdkPixbuf *opaque = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48 * scale, 48 * scale);
    gdk_pixbuf_fill(opaque, 0x303030ff);
    LayoutIcon layout = {.visible = 1, .center = 240};
    RenderCache cache = {0};
    RenderArg arg = {.image = opaque,
                     .background = {.2, .3, .4},
                     .glow = {1, .65, .15},
                     .alpha = 1,
                     .backlight = .9,
                     .urgent = TRUE,
                     .running = 1,
                     .running_here = TRUE};
    guchar low[4], high[4];
    for (int peak = 0; peak < 2; peak++) {
        arg.glow_intensity = peak ? 1 : .55;
        icon_renderer_begin(renderer, 61, 480, 0);
        icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
        icon_renderer_end(renderer);
        /* Sample inside an opaque icon, where the previous draw order hid the
         * pulse entirely. The ring must produce a visible brightness change. */
        read_pixels(11 * scale, (480 - 1 - 221) * scale, 1, 1, peak ? high : low);
        char *path =
            g_strdup_printf("%s/attention-%s-%dx.png", directory, peak ? "peak" : "rest", scale);
        screenshot(path, 61 * scale, 480 * scale);
        g_free(path);
    }
    g_assert_cmpint(high[0] - low[0], >, 25);
    icon_renderer_forget(renderer, opaque);
    icon_renderer_cache_clear(renderer, &cache);
    g_object_unref(opaque);
}

static void bounce_badges(IconRenderer *renderer, int scale, const char *directory)
{
    GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48 * scale, 48 * scale);
    gdk_pixbuf_fill(image, 0x20c040ff);
    LayoutIcon layout = {.visible = 1, .center = 240};
    RenderCache cache = {0};
    for (int right = 0; right < 2; right++) {
        RenderStyle style = {.color = {.2, .4, .6},
                             .opacity = .7,
                             .right = right,
                             .dock_width = 61,
                             .hide_animation = HIDE_SLIDE};
        icon_renderer_set_style(renderer, &style);
        RenderArg arg = {.image = image,
                         .alpha = 1,
                         .background = {.2, .4, .6},
                         .backlight = .9,
                         .running = 3,
                         .running_here = TRUE,
                         .count_visible = TRUE,
                         .count = 1};
        guchar pixel[4];
        icon_renderer_begin(renderer, 85, 480, 0);
        icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
        icon_renderer_end(renderer);
        read_pixels((right ? 14 : 70) * scale, 240 * scale, 1, 1, pixel);
        g_assert_cmpint(pixel[3], ==, 0); /* Idle gutter is transparent. */
        const gint64 counts[] = {1, 12, 99, 1000, 0};
        for (guint i = 0; i < G_N_ELEMENTS(counts); i++) {
            arg.bounce = 18;
            arg.count = counts[i];
            icon_renderer_begin(renderer, 85, 480, 0);
            icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
            icon_renderer_end(renderer);
            read_pixels((right ? 14 : 70) * scale, 240 * scale, 1, 1, pixel);
            g_assert_cmpint(pixel[1], >, 150); /* Icon reaches the outward gutter. */
            if (arg.count) {
                g_assert_nonnull(cache.badge);
                g_assert_cmpint(gdk_pixbuf_get_height(cache.badge), >=, 28 * scale);
                if (arg.count < 10)
                    g_assert_cmpint(gdk_pixbuf_get_width(cache.badge), ==,
                                    gdk_pixbuf_get_height(cache.badge));
                g_assert_cmpint(gdk_pixbuf_get_width(cache.badge), >=,
                                gdk_pixbuf_get_height(cache.badge));
                /* Red fill is baked into the badge rather than tinting the icon. */
                const guchar *data = gdk_pixbuf_read_pixels(cache.badge);
                int stride = gdk_pixbuf_get_rowstride(cache.badge);
                int channels = gdk_pixbuf_get_n_channels(cache.badge);
                int reds = 0;
                for (int y = 0; y < gdk_pixbuf_get_height(cache.badge); y++)
                    for (int x = 0; x < gdk_pixbuf_get_width(cache.badge); x++) {
                        const guchar *p = data + y * stride + x * channels;
                        reds += p[0] > 150 && p[1] < 80 && p[2] < 80;
                    }
                g_assert_cmpint(reds, >, 20);
                GdkPixbuf *cached = cache.badge;
                icon_renderer_begin(renderer, 85, 480, 0);
                icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
                icon_renderer_end(renderer);
                g_assert_true(cache.badge == cached);
                g_assert_cmpuint(icon_renderer_stats(renderer).image_updates, ==, 0);
            } else {
                g_assert_null(cache.badge);
            }
            char *path =
                g_strdup_printf("%s/bounce-badge-%d-%u-%dx.png", directory, right, i, scale);
            screenshot(path, 85 * scale, 480 * scale);
            g_free(path);
        }
        /* Hiding uses the dock width rather than the enlarged window width. */
        arg.bounce = 0;
        arg.count = 0;
        for (int mode = HIDE_SLIDE; mode <= HIDE_FADE_SLIDE; mode++) {
            style.hide_animation = mode;
            icon_renderer_set_style(renderer, &style);
            icon_renderer_begin(renderer, 85, 480, 1);
            icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
            icon_renderer_end(renderer);
            read_pixels((right ? 54 : 30) * scale, 240 * scale, 1, 1, pixel);
            g_assert_cmpint(pixel[3], ==, 0);
        }
    }
    icon_renderer_cache_clear(renderer, &cache);
    icon_renderer_forget(renderer, image);
    g_object_unref(image);
}

static void count_markers(IconRenderer *renderer, int scale, const char *directory)
{
    RenderCache cache = {0};
    LayoutIcon layout = {.visible = 1, .center = 240};
    for (int right = 0; right < 2; right++) {
        RenderStyle style = {.color = {.2, .3, .4}, .opacity = .7, .right = right};
        icon_renderer_set_style(renderer, &style);
        for (guint windows = 1; windows <= 7; windows++) {
            RenderArg arg = {.alpha = 1, .running = windows, .running_here = TRUE};
            icon_renderer_begin(renderer, 61, 480, 0);
            icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
            icon_renderer_end(renderer);
            if (windows <= 6) {
                int bars = 0, minimum_gap = 1000, last_end = -1;
                gboolean in_bar = FALSE;
                for (int y = 190 * scale; y < 290 * scale; y++) {
                    guchar p[4];
                    read_pixels((right ? 58 : 2) * scale, 480 * scale - 1 - y, 1, 1, p);
                    gboolean bright = p[0] > 150 && p[1] > 150 && p[2] > 150;
                    if (bright && !in_bar) {
                        bars++;
                        if (last_end >= 0)
                            minimum_gap = MIN(minimum_gap, y - last_end);
                    }
                    if (!bright && in_bar)
                        last_end = y;
                    in_bar = bright;
                }
                g_assert_cmpint(bars, ==, windows);
                if (windows > 1)
                    g_assert_cmpint(minimum_gap, >=, 3 * scale);
                g_assert_null(cache.window_count);
            } else {
                g_assert_nonnull(cache.window_count);
                g_assert_cmpuint(cache.windows, ==, 7);
                GdkPixbuf *counter = cache.window_count;
                /* Updating or hiding an app's red badge must not invalidate
                 * the separate, anchored window-count cache. */
                arg.count = 5;
                arg.count_visible = TRUE;
                icon_renderer_begin(renderer, 61, 480, 0);
                icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
                icon_renderer_end(renderer);
                g_assert_true(cache.window_count == counter);
                arg.count_visible = FALSE;
                icon_renderer_begin(renderer, 61, 480, 0);
                icon_renderer_draw(renderer, &arg, &cache, &layout, 30, 480, 61);
                icon_renderer_end(renderer);
                g_assert_true(cache.window_count == counter);
                g_assert_null(cache.badge);
                g_assert_cmpuint(icon_renderer_stats(renderer).image_updates, ==, 0);
            }
            char *path =
                g_strdup_printf("%s/windows-%d-%u-%dx.png", directory, right, windows, scale);
            screenshot(path, 61 * scale, 480 * scale);
            g_free(path);
        }
    }
    icon_renderer_cache_clear(renderer, &cache);
}

static void memory_snapshot(const char *stage)
{
    FILE *file = fopen("/proc/self/smaps_rollup", "r");
    if (!file)
        return;
    char line[256];
    unsigned long rss = 0, pss = 0, private_clean = 0, private_dirty = 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "Rss: %lu kB", &rss) == 1)
            continue;
        if (sscanf(line, "Pss: %lu kB", &pss) == 1)
            continue;
        if (sscanf(line, "Private_Clean: %lu kB", &private_clean) == 1)
            continue;
        (void)sscanf(line, "Private_Dirty: %lu kB", &private_dirty);
    }
    fclose(file);
    printf("Memory %s: RSS=%lu KiB PSS=%lu KiB private=%lu KiB\n", stage, rss, pss,
           private_clean + private_dirty);
}

static guchar *frame_pixels(int width, int height)
{
    gsize length = (gsize)width * height * 4;
    guchar *pixels = g_malloc(length);
    read_pixels(0, 0, width, height, pixels);
    return pixels;
}

#ifndef LAUNCHER_RENDER_GL
static void paint_test(IconRenderer *renderer, int scale)
{
    cairo_surface_t *frame = icon_renderer_surface(renderer);
    int width = cairo_image_surface_get_width(frame),
        height = cairo_image_surface_get_height(frame);
    cairo_surface_t *target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_surface_set_device_scale(target, scale, scale);
    cairo_t *cr = cairo_create(target);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_paint(cr);
    icon_renderer_paint(renderer, cr);
    cairo_surface_flush(target);
    g_assert_cmpmem(cairo_image_surface_get_data(target),
                    (gsize)cairo_image_surface_get_stride(target) * height,
                    cairo_image_surface_get_data(frame),
                    (gsize)cairo_image_surface_get_stride(frame) * height);
    cairo_destroy(cr);
    cairo_surface_destroy(target);
}
#endif

int main(void)
{
    memory_snapshot("process baseline");
#ifdef LAUNCHER_RENDER_GL
    g_setenv("EGL_PLATFORM", "surfaceless", TRUE);
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "No surfaceless EGL display available\n");
        return 77;
    }
    g_assert_true(eglBindAPI(EGL_OPENGL_API));
    EGLint attributes[] = {EGL_SURFACE_TYPE,
                           EGL_PBUFFER_BIT,
                           EGL_RENDERABLE_TYPE,
                           EGL_OPENGL_BIT,
                           EGL_RED_SIZE,
                           8,
                           EGL_GREEN_SIZE,
                           8,
                           EGL_BLUE_SIZE,
                           8,
                           EGL_ALPHA_SIZE,
                           8,
                           EGL_NONE};
    EGLConfig config;
    EGLint count;
    g_assert_true(eglChooseConfig(display, attributes, &config, 1, &count));
    g_assert_cmpint(count, >, 0);
    EGLint ctx_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,
                               3,
                               EGL_CONTEXT_MINOR_VERSION_KHR,
                               2,
                               EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                               EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                               EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attributes);
    g_assert_true(context != EGL_NO_CONTEXT);
    EGLint surface_attributes[] = {EGL_WIDTH, 256, EGL_HEIGHT, 1440, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attributes);
    g_assert_true(surface != EGL_NO_SURFACE);
    g_assert_true(eglMakeCurrent(display, surface, surface, context));
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    memory_snapshot("context baseline");
    const char *backend = "gl";
#else
    const char *backend = "cpu";
    printf("Renderer: CPU, no OpenGL context\n");
#endif
    char *directory = g_strdup_printf("build/%s", backend);
    g_mkdir_with_parents(directory, 0755);
    for (int scale = 1; scale <= 2; scale++) {
        IconRenderer renderer = {0};
        current_renderer = &renderer;
        GError *error = NULL;
        RenderCache cache[13] = {{0}};
        gboolean initialized = icon_renderer_init(&renderer, 54, 48, scale, &error);
        g_assert_no_error(error);
        g_assert_true(initialized);
        g_assert_true(icon_renderer_realize(&renderer, &error));
        g_assert_no_error(error);
        GdkPixbuf *icon = launcher_resource_icon("launcher_mark.svg", 48 * scale, &error);
        g_assert_no_error(error);
        g_assert_nonnull(icon);
        draw(&renderer, icon, cache, 0, TRUE, 61, 480);
        g_assert_cmpuint(rendering_error(), ==, 0);
        RenderStats first = icon_renderer_stats(&renderer);
#ifdef LAUNCHER_RENDER_GL
        const guint expected_draws = 3; /* Background, clipped apps, fixed shelf. */
#else
        const guint expected_draws = 1;
#endif
        g_assert_cmpuint(first.draw_calls, ==, expected_draws);
        g_assert_cmpuint(first.image_updates, >, 0);
        guchar background[4];
        read_pixels(30 * scale, 479 * scale, 1, 1, background);
        g_assert_cmpint(background[3], >=, 169);
        g_assert_cmpint(background[3], <=, 171);
        char *path = g_strdup_printf("%s/renderer-%dx.png", directory, scale);
        screenshot(path, 61 * scale, 480 * scale);
        g_free(path);
        guchar *original = frame_pixels(61 * scale, 480 * scale);
#ifndef LAUNCHER_RENDER_GL
        paint_test(&renderer, scale);
#endif
        draw(&renderer, icon, cache, .5, TRUE, 61, 480);
        RenderStats warm = icon_renderer_stats(&renderer);
        g_assert_cmpuint(warm.image_updates, ==, 0);
        g_assert_cmpuint(warm.draw_calls, ==, expected_draws);
        finish();
        gint64 start = g_get_monotonic_time();
        for (int i = 0; i < 300; i++)
            draw(&renderer, icon, cache, (sin(i * .1) + 1) / 2, TRUE, 61, 480);
        finish();
        double ms = (g_get_monotonic_time() - start) / 300000.0;
        printf(
            "%dx: %.3f ms/frame, %u vertices, 1 submission/frame, %u warm image updates, %.2f MiB "
            "image storage\n",
            scale, ms, warm.vertices, warm.image_updates,
            (warm.atlas_bytes + warm.image_bytes) / (1024.0 * 1024));
        icon_renderer_begin(&renderer, 61, 480, 1);
        icon_renderer_end(&renderer);
        read_pixels(0, 0, 1, 1, background);
        g_assert_cmpint(background[3], ==, 0);
        /* Force atlas growth, release all dynamic images, then refill freed slots. */
        GdkPixbuf *images[90];
        LayoutIcon layout = {.visible = 1, .center = 32};
        RenderArg arg = {
            .background = {.3, .6, .9}, .glow = {.3, .6, .9}, .alpha = 1, .backlight = .9};
        RenderCache temporary = {0};
        gsize peak_atlas = 0;
        for (guint pass = 0; pass < 2; pass++) {
            icon_renderer_begin(&renderer, 61, 480, 0);
            for (guint i = 0; i < G_N_ELEMENTS(images); i++) {
                images[i] = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48 * scale, 48 * scale);
                gdk_pixbuf_fill(images[i], 0x5050ffff);
                arg.image = images[i];
                icon_renderer_draw(&renderer, &arg, &temporary, &layout, 30, 480, 61);
            }
            icon_renderer_end(&renderer);
            g_assert_cmpuint(rendering_error(), ==, 0);
            RenderStats grown = icon_renderer_stats(&renderer);
            peak_atlas = grown.atlas_bytes + grown.image_bytes;
            g_assert_cmpuint(grown.cached_images, ==, first.cached_images + 90);
            g_assert_cmpuint(grown.atlas_bytes + grown.image_bytes, >,
                             first.atlas_bytes + first.image_bytes);
            if (pass == 0)
                warm = grown;
            else
                g_assert_cmpuint(grown.atlas_bytes, ==, warm.atlas_bytes);
            for (guint i = 0; i < G_N_ELEMENTS(images); i++) {
                icon_renderer_forget(&renderer, images[i]);
                g_object_unref(images[i]);
            }
            draw(&renderer, icon, cache, 0, TRUE, 61, 480);
            RenderStats trimmed = icon_renderer_stats(&renderer);
            g_assert_cmpuint(trimmed.atlas_bytes, ==, first.atlas_bytes);
            g_assert_cmpuint(trimmed.image_bytes, ==, first.image_bytes);
            g_assert_cmpuint(trimmed.cached_images, ==, first.cached_images);
            g_assert_cmpuint(trimmed.image_updates, ==, 0);
            g_assert_cmpuint(trimmed.buffer_bytes, ==, first.buffer_bytes);
            guchar *repacked = frame_pixels(61 * scale, 480 * scale);
            int maximum_delta = 0;
            for (int pixel = 0; pixel < 61 * 480 * scale * scale * 4; pixel++)
                maximum_delta = MAX(maximum_delta, abs(repacked[pixel] - original[pixel]));
            printf("Cache reuse maximum channel delta: %d\n", maximum_delta);
            /* Relocated UVs can round linear filtering/blending differently by
             * up to two byte levels; larger differences indicate lost pixels. */
            g_assert_cmpint(maximum_delta, <=, 2);
            g_free(repacked);
            g_assert_cmpuint(rendering_error(), ==, 0);
        }
        RenderStats trimmed = icon_renderer_stats(&renderer);
        printf("%dx after releasing 90 icons: images %.2f -> %.2f MiB; commands %.1f KiB; "
               "%u cached images; rendering preserved\n",
               scale, peak_atlas / (1024.0 * 1024),
               (trimmed.atlas_bytes + trimmed.image_bytes) / (1024.0 * 1024),
               trimmed.buffer_bytes / 1024.0, trimmed.cached_images);
        g_free(original);
        for (int right = 0; right <= 1; right++) {
            RenderStyle style = {.color = {.2, .4, .6},
                                 .opacity = .7,
                                 .right = right,
                                 .hide_animation = HIDE_FADE_SLIDE};
            icon_renderer_set_style(&renderer, &style);
            for (int fold = 0; fold <= 2; fold++) {
                draw(&renderer, icon, cache, fold / 2.0, TRUE, 61, 480);
                char *scene =
                    g_strdup_printf("%s/scene-%d-%d-%d.png", directory, scale, right, fold);
                screenshot(scene, 61 * scale, 480 * scale);
                g_free(scene);
            }
        }
        const double amber[3] = {1, .65, .15};
        icon_renderer_begin(&renderer, 61, 480, 1);
        icon_renderer_beacon(&renderer, 240, amber, .8);
        icon_renderer_end(&renderer);
        char *beacon_path = g_strdup_printf("%s/beacon-%dx.png", directory, scale);
        screenshot(beacon_path, 61 * scale, 480 * scale);
        g_free(beacon_path);
        style_tests(&renderer, scale);
        attention_visibility(&renderer, scale, directory);
        bounce_badges(&renderer, scale, directory);
        count_markers(&renderer, scale, directory);
        shelf_clipping(&renderer, scale);
#ifndef LAUNCHER_RENDER_GL
        paint_test(&renderer, scale);
#endif
        memory_snapshot(scale == 1 ? "1x after stress" : "2x after stress");
        /* Releasing and reusing slots must not leak cache entries or GPU storage. */
        for (guint i = 0; i < 13; i++)
            icon_renderer_cache_clear(&renderer, &cache[i]);
        icon_renderer_forget(&renderer, icon);
        g_object_unref(icon);
        icon_renderer_unrealize(&renderer);
        icon_renderer_clear(&renderer);
    }
    g_free(directory);
#ifdef LAUNCHER_RENDER_GL
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
#endif
    return 0;
}
