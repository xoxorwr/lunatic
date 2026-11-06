/* SPDX-License-Identifier: GPL-3.0-only
 * Exercise production search, layout, navigation and Cairo drawing without a display.
 */
#include "../src/dash.c"
#include <glib/gstdio.h>

static GDesktopAppInfo *app(const char *name, const char *command, const char *keywords,
                            gboolean hidden)
{
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(key, "Desktop Entry", "Name", name);
    g_key_file_set_string(key, "Desktop Entry", "Exec", command);
    g_key_file_set_string(key, "Desktop Entry", "Keywords", keywords);
    g_key_file_set_boolean(key, "Desktop Entry", "NoDisplay", hidden);
    GDesktopAppInfo *info = g_desktop_app_info_new_from_keyfile(key);
    g_key_file_unref(key);
    g_assert_nonnull(info);
    return info;
}

static void matching(void)
{
    GList *apps = NULL;
    apps = g_list_append(apps, app("A File Manager", "/bin/true", "files;folders;", FALSE));
    apps = g_list_append(apps, app("Files", "/bin/true", "folders;documents;", FALSE));
    apps = g_list_append(apps, app("Café", "/bin/true", "coffee;", FALSE));
    apps = g_list_append(apps, app("Hidden Tool", "/bin/true", "files;", TRUE));
    DashModel *model = dash_model_new(apps);
    /* The cached model owns its applications independently of catalog reloads. */
    g_list_free_full(apps, g_object_unref);
    g_assert_cmpuint(model->matches->len, ==, 3);
    dash_model_filter(model, "FILE");
    g_assert_cmpuint(model->matches->len, ==, 2);
    g_assert_cmpstr(g_app_info_get_name(G_APP_INFO(dash_model_match(model, 0)->app)), ==, "Files");
    dash_model_filter(model, "folders documents");
    g_assert_cmpuint(model->matches->len, ==, 1);
    g_assert_cmpstr(g_app_info_get_name(G_APP_INFO(dash_model_match(model, 0)->app)), ==, "Files");
    dash_model_filter(model, "CAFE\314\201");
    g_assert_cmpuint(model->matches->len, ==, 1);
    g_assert_cmpstr(g_app_info_get_name(G_APP_INFO(dash_model_match(model, 0)->app)), ==, "Café");
    dash_model_filter(model, " true ");
    g_assert_cmpuint(model->matches->len, ==, 3);
    dash_model_filter(model, "no such application");
    g_assert_cmpuint(model->matches->len, ==, 0);
    g_assert_null(dash_model_match(model, 0));
    dash_model_filter(model, " \t ");
    g_assert_cmpuint(model->matches->len, ==, 3);
    dash_model_free(model);
}

static void layout_and_navigation(void)
{
    const int widths[] = {240, 640, 1024, 1920};
    const int heights[] = {320, 480, 1080};
    for (guint w = 0; w < G_N_ELEMENTS(widths); w++)
        for (guint h = 0; h < G_N_ELEMENTS(heights); h++)
            for (int right = 0; right < 2; right++) {
                DashGeometry g = dash_geometry(widths[w], heights[h], right);
                g_assert_cmpint(g.width, <=, widths[w]);
                g_assert_cmpint(g.height, <=, heights[h]);
                g_assert_cmpint(g.grid_x, >=, g.x);
                g_assert_cmpint(g.grid_x + g.grid_width, <=, g.x + g.width);
                g_assert_cmpint(g.grid_y + g.grid_height, <=, g.height);
                g_assert_cmpint(g.x, ==, right ? widths[w] - g.width : 0);
                double scroll = 0;
                int selected = 0;
                for (int i = 0; i < 400; i++) {
                    selected = dash_grid_navigate(&g, selected, 101, 1, &scroll);
                    g_assert_cmpfloat(scroll, <=, dash_scroll_limit(&g, 101));
                    double x = (selected % g.columns + .5) * g.cell_width;
                    double y = (selected / g.columns + .5) * DASH_CELL_HEIGHT - scroll;
                    g_assert_cmpfloat(y, >=, 0);
                    g_assert_cmpfloat(y, <, g.grid_height);
                    g_assert_cmpint(dash_grid_hit(&g, x, y, scroll, 101), ==, selected);
                }
                g_assert_cmpint(selected, ==, 100);
                selected = dash_grid_navigate(&g, selected, 101, -200, &scroll);
                g_assert_cmpint(selected, ==, 0);
                g_assert_cmpfloat(scroll, ==, 0);
                g_assert_cmpint(dash_grid_hit(&g, -1, 1, 0, 101), ==, -1);
                g_assert_cmpint(dash_grid_hit(&g, g.grid_width, 1, 0, 101), ==, -1);
                g_assert_cmpint(dash_grid_hit(&g, 1, g.grid_height, 0, 101), ==, -1);
                g_assert_cmpint(dash_grid_hit(&g, 1, 1, 0, 0), ==, -1);
                g_assert_cmpint(dash_grid_navigate(&g, 0, 0, 1, &scroll), ==, -1);
                g_assert_cmpfloat(scroll, ==, 0);
            }
}

static void search_performance(void)
{
    GList *apps = NULL;
    for (guint i = 0; i < 2000; i++) {
        char *name = g_strdup_printf("Application %04u", i);
        apps = g_list_prepend(apps, app(name, "/bin/true", "editor;document;", FALSE));
        g_free(name);
    }
    gint64 start = g_get_monotonic_time();
    DashModel *model = dash_model_new(apps);
    g_test_message("Index 2000 applications: %.2f ms", (g_get_monotonic_time() - start) / 1000.0);
    start = g_get_monotonic_time();
    for (guint i = 0; i < 100; i++) {
        dash_model_filter(model, "app 0123");
        g_assert_cmpuint(model->matches->len, ==, 1);
        dash_model_filter(model, "");
        g_assert_cmpuint(model->matches->len, ==, 2000);
    }
    g_test_message("Cached filtering, 2000 apps: %.3f ms/query",
                   (g_get_monotonic_time() - start) / 200000.0);
    dash_model_free(model);
    g_list_free_full(apps, g_object_unref);
}

static void cache_and_cancellation(void)
{
    LauncherDash *dash = g_new0(LauncherDash, 1);
    dash->refs = 1;
    dash->icons = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, icon_free);
    dash->icon_cancel = g_cancellable_new();
    for (guint i = 1; i <= 200; i++) {
        DashApp *key = GUINT_TO_POINTER(i);
        g_hash_table_insert(dash->icons, key, g_new0(DashIcon, 1));
        cache_icon(dash, key);
    }
    g_assert_cmpuint(g_hash_table_size(dash->icons), ==, DASH_ICON_CACHE_LIMIT);
    g_assert_false(g_hash_table_contains(dash->icons, GUINT_TO_POINTER(1)));
    /* Recently visible icons survive eviction of older scroll positions. */
    cache_icon(dash, GUINT_TO_POINTER(105));
    g_hash_table_insert(dash->icons, GUINT_TO_POINTER(201), g_new0(DashIcon, 1));
    cache_icon(dash, GUINT_TO_POINTER(201));
    g_assert_true(g_hash_table_contains(dash->icons, GUINT_TO_POINTER(105)));
    g_assert_false(g_hash_table_contains(dash->icons, GUINT_TO_POINTER(106)));
    DashIcon *pending = g_new0(DashIcon, 1);
    pending->loading = TRUE;
    g_hash_table_insert(dash->icons, GUINT_TO_POINTER(202), pending);
    dash->pending_icons = 1;
    guint generation = dash->generation;
    GCancellable *old = g_object_ref(dash->icon_cancel);
    cancel_icons(dash);
    g_assert_true(g_cancellable_is_cancelled(old));
    g_assert_false(g_cancellable_is_cancelled(dash->icon_cancel));
    g_assert_cmpuint(dash->generation, !=, generation);
    g_assert_cmpuint(dash->pending_icons, ==, 0);
    g_assert_false(g_hash_table_contains(dash->icons, GUINT_TO_POINTER(202)));
    g_assert_cmpuint(g_hash_table_size(dash->icons), ==, DASH_ICON_CACHE_LIMIT);
    g_object_unref(old);
    dash->closing = TRUE;
    cancel_icons(dash);
    g_assert_null(dash->icon_cancel);
    dash_unref(dash);
}

static GdkPixbuf *preview_icon(const char *path)
{
    /* Cairo's PNG reader also works in environments where the platform image
     * loader's helper process cannot be started. Preview fixtures are local PNGs. */
    cairo_surface_t *image = cairo_image_surface_create_from_png(path);
    GdkPixbuf *pixbuf = NULL;
    if (cairo_surface_status(image) == CAIRO_STATUS_SUCCESS)
        pixbuf = gdk_pixbuf_get_from_surface(image, 0, 0, cairo_image_surface_get_width(image),
                                             cairo_image_surface_get_height(image));
    cairo_surface_destroy(image);
    return pixbuf;
}

static GdkPixbuf *busy_desktop(int width, int height, int scale)
{
    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, width * scale, height * scale);
    cairo_surface_set_device_scale(surface, scale, scale);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, .24, .35, .62);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, .95, .94, .92);
    cairo_rectangle(cr, 36, 30, width * .67, height - 65);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, .16, .18, .23);
    cairo_rectangle(cr, 36, 30, width * .67, 38);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    text(cr, "Document window", 52, 40, 300, 14, TRUE, FALSE, 1);
    cairo_set_source_rgb(cr, .14, .14, .16);
    for (int y = 87; y < height - 45; y += 24)
        text(cr, "This window's text must not compete with application names.", 54, y, width * .62,
             13, FALSE, FALSE, 1);
    cairo_set_source_rgb(cr, .16, .24, .20);
    cairo_rectangle(cr, width * .7, height * .3, width * .28, height * .6);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, .75, .92, .68);
    for (int y = height * .3 + 16; y < height * .88; y += 21)
        text(cr, "$ command --option", width * .7 + 12, y, width * .26, 12, FALSE, FALSE, 1);
    cairo_destroy(cr);
    GdkPixbuf *pixels = gdk_pixbuf_get_from_surface(surface, 0, 0, width * scale, height * scale);
    cairo_surface_destroy(surface);
    return pixels;
}

static guint32 surface_pixel(cairo_surface_t *surface, int x, int y)
{
    cairo_surface_flush(surface);
    const guint32 *pixels = (const guint32 *)cairo_image_surface_get_data(surface);
    return pixels[y * (cairo_image_surface_get_stride(surface) / 4) + x];
}

static void blurred_background(void)
{
    g_assert_null(dash_blur_background(NULL, 960, 760));
    /* Constant fields and tiny images stay stable, including clamped edges. */
    GdkPixbuf *constant = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    gdk_pixbuf_fill(constant, 0x336699ff);
    cairo_surface_t *tiny = dash_blur_background(constant, 1, 1);
    g_assert_cmphex(surface_pixel(tiny, 0, 0), ==, 0xff336699);
    cairo_surface_destroy(tiny);
    cairo_surface_t *field = dash_blur_background(constant, 960, 760);
    g_assert_cmphex(surface_pixel(field, 0, 0), ==, 0xff336699);
    g_assert_cmphex(surface_pixel(field, 159, 126), ==, 0xff336699);
    cairo_surface_destroy(field);
    g_object_unref(constant);
    /* High-frequency detail disappears while large color regions survive. */
    GdkPixbuf *stripes = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 960, 760);
    guchar *data = gdk_pixbuf_get_pixels(stripes);
    int stride = gdk_pixbuf_get_rowstride(stripes);
    for (int y = 0; y < 760; y++)
        for (int x = 0; x < 960; x++) {
            guchar *pixel = data + y * stride + x * 3;
            guint high = (y / 6) % 2 ? 255 : 80;
            pixel[0] = x < 480 ? high : 20;
            pixel[1] = 20;
            pixel[2] = x < 480 ? 20 : high;
        }
    gint64 start = g_get_monotonic_time();
    cairo_surface_t *blur = dash_blur_background(stripes, 960, 760);
    g_test_message("960x760 backdrop blur: %.2f ms; cached storage: %d bytes",
                   (g_get_monotonic_time() - start) / 1000.0,
                   cairo_image_surface_get_stride(blur) * cairo_image_surface_get_height(blur));
    guint32 left = surface_pixel(blur, 30, 60), right = surface_pixel(blur, 130, 60);
    g_assert_cmpuint((left >> 16) & 255, >, (left & 255) + 80);
    g_assert_cmpuint(right & 255, >, ((right >> 16) & 255) + 80);
    int minimum = 255, maximum = 0;
    for (int y = 20; y < 100; y++) {
        int red = (surface_pixel(blur, 30, y) >> 16) & 255;
        minimum = MIN(minimum, red);
        maximum = MAX(maximum, red);
    }
    g_assert_cmpint(maximum - minimum, <, 8);
    /* A 2x input uses the same bounded cache and gives equivalent blur. */
    GdkPixbuf *hidpi = gdk_pixbuf_scale_simple(stripes, 1920, 1520, GDK_INTERP_NEAREST);
    cairo_surface_t *blur2 = dash_blur_background(hidpi, 960, 760);
    g_assert_cmpint(cairo_image_surface_get_width(blur2), ==, 160);
    g_assert_cmpint(cairo_image_surface_get_height(blur2), ==, 127);
    g_assert_cmphex(surface_pixel(blur2, 30, 60), ==, left);
    cairo_surface_destroy(blur2);
    g_object_unref(hidpi);
    g_object_unref(stripes);
    cairo_surface_destroy(blur);
}

static void backdrop_composition(void)
{
    const double color[3] = {.3, .2, .4};
    DashGeometry g = dash_geometry(640, 480, FALSE);
    GdkPixbuf *desktop = busy_desktop(g.width, g.height, 1);
    cairo_surface_t *blur = dash_blur_background(desktop, g.width, g.height);
    g_object_unref(desktop);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 480);
    cairo_t *cr = cairo_create(surface);
    dash_paint_frame(cr, 640, 480, &g, color, .15, blur, 10);
    g_assert_cmpuint(surface_pixel(surface, 300, 200) >> 24, ==, 255);
    g_assert_cmpuint(surface_pixel(surface, 0, 0) >> 24, <, 60); /* Rounded clipping. */
    /* Foreground icons remain pixel-sharp over the blurred background. */
    GdkPixbuf *icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 48, 48);
    guchar *pixels = gdk_pixbuf_get_pixels(icon);
    int stride = gdk_pixbuf_get_rowstride(icon);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++)
            memset(pixels + y * stride + x * 3, x < 24 ? 0 : 255, 3);
    dash_paint_tile(cr, 114, "Test", icon, 1, FALSE, FALSE, FALSE);
    g_assert_cmphex(surface_pixel(surface, 56, 30) & 0xffffff, ==, 0);
    g_assert_cmphex(surface_pixel(surface, 57, 30) & 0xffffff, ==, 0xffffff);
    g_object_unref(icon);
    /* Failure to capture uses a readable fallback, never raw transparency. */
    dash_paint_frame(cr, 640, 480, &g, color, .15, NULL, 10);
    g_assert_cmpuint(surface_pixel(surface, 300, 200) >> 24, >=, 244);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    cairo_surface_destroy(blur);
}

static void drawing(void)
{
    GtkCssProvider *css = gtk_css_provider_new();
    GError *error = NULL;
    g_assert_true(gtk_css_provider_load_from_data(css, dash_css, -1, &error));
    g_assert_no_error(error);
    g_object_unref(css);
    g_assert_cmpint(g_mkdir_with_parents("build/host", 0700), ==, 0);
    const double color[3] = {.3, .2, .4};
    const char *names[] = {"Files",    "Terminal",        "Firefox",
                           "Settings", "Image Viewer",    "Text Editor",
                           "Music",    "Document Viewer", "Calculator"};
    const char *paths[] = {"/usr/share/icons/AdwaitaLegacy/48x48/legacy/system-file-manager.png",
                           "/usr/share/icons/AdwaitaLegacy/48x48/legacy/utilities-terminal.png",
                           "/usr/share/icons/hicolor/48x48/apps/firefox.png",
                           "/usr/share/icons/hicolor/48x48/apps/org.xfce.settings.manager.png",
                           "/usr/share/icons/hicolor/48x48/apps/viewnior.png"};
    for (int scale = 1; scale <= 2; scale++)
        for (int right = 0; right < 2; right++) {
            int width = 1100, height = 800;
            DashGeometry g = dash_geometry(width, height, right);
            cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width * scale, height * scale);
            cairo_surface_set_device_scale(surface, scale, scale);
            cairo_t *cr = cairo_create(surface);
            GdkPixbuf *desktop = busy_desktop(g.width, g.height, scale);
            cairo_surface_t *backdrop = dash_blur_background(desktop, g.width, g.height);
            g_object_unref(desktop);
            dash_paint_frame(cr, width, height, &g, color, .35, backdrop, 127);
            cairo_surface_destroy(backdrop);
            /* Represent the native editable entry for the headless preview. */
            rounded(cr, g.grid_x, g.grid_y - 60, g.grid_width, 42, 7);
            cairo_set_source_rgba(cr, 0, 0, 0, .24);
            cairo_fill_preserve(cr);
            cairo_set_source_rgb(cr, .91, .63, .42);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, .65);
            text(cr, "Search applications…", g.grid_x + 16, g.grid_y - 50, g.grid_width - 32, 16,
                 FALSE, FALSE, 1);
            for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
                GdkPixbuf *icon = i < G_N_ELEMENTS(paths) ? preview_icon(paths[i]) : NULL;
                cairo_save(cr);
                cairo_translate(cr, g.grid_x + (i % g.columns) * g.cell_width,
                                g.grid_y + (i / g.columns) * DASH_CELL_HEIGHT);
                dash_paint_tile(cr, g.cell_width, names[i], icon, scale, i == 0, i == 1, i == 0);
                cairo_restore(cr);
                g_clear_object(&icon);
            }
            g_assert_cmpint(cairo_status(cr), ==, CAIRO_STATUS_SUCCESS);
            cairo_surface_flush(surface);
            const guint32 *pixels = (const guint32 *)cairo_image_surface_get_data(surface);
            int stride = cairo_image_surface_get_stride(surface) / 4;
            guint32 inside = pixels[100 * scale * stride + (g.x + 10) * scale];
            guint32 outside = pixels[790 * scale * stride + 100 * scale];
            g_assert_cmpuint(inside >> 24, >, 240);
            g_assert_cmpuint(outside >> 24, <, 60);
            char *path =
                g_strdup_printf("build/host/dash-%s-%dx.png", right ? "right" : "left", scale);
            g_assert_cmpint(cairo_surface_write_to_png(surface, path), ==, CAIRO_STATUS_SUCCESS);
            g_free(path);
            cairo_destroy(cr);
            cairo_surface_destroy(surface);
        }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/dash/search-and-lifetime", matching);
    g_test_add_func("/dash/geometry-hit-testing-and-keyboard-scroll", layout_and_navigation);
    g_test_add_func("/dash/catalog-performance", search_performance);
    g_test_add_func("/dash/icon-cache-and-cancellation", cache_and_cancellation);
    g_test_add_func("/dash/blur-detail-edges-and-hidpi", blurred_background);
    g_test_add_func("/dash/blur-composition-and-sharp-foreground", backdrop_composition);
    g_test_add_func("/dash/custom-drawing", drawing);
    return g_test_run();
}
