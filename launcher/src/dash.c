/* SPDX-License-Identifier: GPL-3.0-only
 * A persistent shell overlay. Applications are data, not a widget per result;
 * only the visible grid is painted; bounded asynchronous jobs decode icons.
 */
#include "dash.h"
#include <X11/extensions/Xrender.h>
#include <cairo-xlib.h>
#include <gdk/gdkx.h>
#include <math.h>
#include <string.h>

static char *search_text(const char *text)
{
    char *folded = g_utf8_casefold(text ? text : "", -1);
    char *result = g_utf8_normalize(folded, -1, G_NORMALIZE_ALL_COMPOSE);
    g_free(folded);
    return result;
}

static void dash_app_free(gpointer data)
{
    DashApp *app = data;
    g_object_unref(app->app);
    g_free(app->name);
    g_free(app->search);
    g_free(app);
}

DashModel *dash_model_new(GList *apps)
{
    DashModel *model = g_new0(DashModel, 1);
    model->apps = g_ptr_array_new_with_free_func(dash_app_free);
    model->matches = g_array_new(FALSE, FALSE, sizeof(guint));
    for (GList *l = apps; l; l = l->next) {
        GAppInfo *info = l->data;
        if (!G_IS_DESKTOP_APP_INFO(info) || !g_app_info_should_show(info) ||
            g_strcmp0(g_app_info_get_id(info), "lunatic-launcher.desktop") == 0)
            continue;
        DashApp *app = g_new0(DashApp, 1);
        app->app = G_DESKTOP_APP_INFO(g_object_ref(info));
        app->name = search_text(g_app_info_get_display_name(info));
        GString *text = g_string_new(app->name);
        const char *fields[] = {g_app_info_get_executable(info), g_app_info_get_description(info),
                                g_app_info_get_id(info),
                                g_desktop_app_info_get_generic_name(app->app)};
        for (guint i = 0; i < G_N_ELEMENTS(fields); i++)
            if (fields[i]) {
                g_string_append_c(text, ' ');
                g_string_append(text, fields[i]);
            }
        const char *const *keywords = g_desktop_app_info_get_keywords(app->app);
        for (guint i = 0; keywords && keywords[i]; i++) {
            g_string_append_c(text, ' ');
            g_string_append(text, keywords[i]);
        }
        app->search = search_text(text->str);
        g_string_free(text, TRUE);
        g_ptr_array_add(model->apps, app);
    }
    dash_model_filter(model, "");
    return model;
}

void dash_model_free(DashModel *model)
{
    if (!model)
        return;
    g_ptr_array_unref(model->apps);
    g_array_unref(model->matches);
    g_free(model);
}

void dash_model_filter(DashModel *model, const char *query)
{
    char *folded = search_text(query);
    char **words = g_strsplit_set(g_strstrip(folded), " \t\n", -1);
    g_array_set_size(model->matches, 0);
    /* Name prefixes precede keyword/executable matches, preserving the desktop
     * list's alphabetical order within each group. */
    for (int pass = 0; pass < 2; pass++)
        for (guint i = 0; i < model->apps->len; i++) {
            DashApp *app = g_ptr_array_index(model->apps, i);
            gboolean prefix = g_str_has_prefix(app->name, folded);
            if (prefix != (pass == 0))
                continue;
            gboolean match = TRUE;
            for (guint j = 0; words[j]; j++)
                if (*words[j] && !strstr(app->search, words[j])) {
                    match = FALSE;
                    break;
                }
            if (match)
                g_array_append_val(model->matches, i);
        }
    g_strfreev(words);
    g_free(folded);
}

DashApp *dash_model_match(DashModel *model, guint index)
{
    if (!model || index >= model->matches->len)
        return NULL;
    return g_ptr_array_index(model->apps, g_array_index(model->matches, guint, index));
}

DashGeometry dash_geometry(int width, int height, gboolean right)
{
    DashGeometry g = {0};
    g.width = MAX(1, MIN(960, width));
    g.height = MAX(1, MIN(760, height));
    g.x = right ? width - g.width : 0;
    int margin = MIN(24, g.width / 12);
    g.grid_x = g.x + margin;
    g.grid_y = MIN(132, MAX(80, g.height / 3));
    g.grid_width = MAX(1, g.width - 2 * margin);
    g.grid_height = MAX(1, g.height - g.grid_y - 42);
    g.columns = MAX(1, g.grid_width / 112);
    g.cell_width = (double)g.grid_width / g.columns;
    return g;
}

double dash_scroll_limit(const DashGeometry *g, guint count)
{
    return MAX(0, ceil((double)count / g->columns) * DASH_CELL_HEIGHT - g->grid_height);
}

int dash_grid_hit(const DashGeometry *g, double x, double y, double scroll, guint count)
{
    if (x < 0 || x >= g->grid_width || y < 0 || y >= g->grid_height)
        return -1;
    int column = (int)(x / g->cell_width);
    int row = (int)((y + scroll) / DASH_CELL_HEIGHT);
    guint index = row * g->columns + column;
    return index < count ? (int)index : -1;
}

int dash_grid_navigate(const DashGeometry *g, int selected, guint count, int delta, double *scroll)
{
    if (!count) {
        *scroll = 0;
        return -1;
    }
    selected = CLAMP(selected + delta, 0, (int)count - 1);
    int row = selected / g->columns;
    if (row * DASH_CELL_HEIGHT < *scroll)
        *scroll = row * DASH_CELL_HEIGHT;
    else if ((row + 1) * DASH_CELL_HEIGHT > *scroll + g->grid_height)
        *scroll = (row + 1) * DASH_CELL_HEIGHT - g->grid_height;
    *scroll = CLAMP(*scroll, 0, dash_scroll_limit(g, count));
    return selected;
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double radius)
{
    radius = MIN(radius, MIN(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - radius, y + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, x + w - radius, y + h - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, x + radius, y + h - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static void text(cairo_t *cr, const char *value, double x, double y, double width, int size,
                 gboolean bold, gboolean center, int lines)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_new();
    pango_font_description_set_family(font, "Sans");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, value, -1);
    pango_layout_set_width(layout, MAX(1, (int)width) * PANGO_SCALE);
    pango_layout_set_height(layout, -lines);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_alignment(layout, center ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
}

/* Three separable box passes approximate a Gaussian. Work at one sixth of the
 * logical size, keeping blur cost and storage independent of monitor scale. */
static void blur_pass(const guint32 *source, guint32 *target, int width, int height,
                      gboolean horizontal)
{
    const int radius = 4, divisor = radius * 2 + 1;
    int length = horizontal ? width : height;
    int lines = horizontal ? height : width;
    for (int line = 0; line < lines; line++) {
        int base = horizontal ? line * width : line;
        int step = horizontal ? 1 : width;
        int red = 0, green = 0, blue = 0;
        for (int p = -radius; p <= radius; p++) {
            guint32 pixel = source[base + CLAMP(p, 0, length - 1) * step];
            red += (pixel >> 16) & 255;
            green += (pixel >> 8) & 255;
            blue += pixel & 255;
        }
        for (int p = 0; p < length; p++) {
            target[base + p * step] = 0xff000000u | ((guint32)(red / divisor) << 16) |
                                      ((guint32)(green / divisor) << 8) | (guint32)(blue / divisor);
            guint32 remove = source[base + MAX(0, p - radius) * step];
            guint32 add = source[base + MIN(length - 1, p + radius + 1) * step];
            red += (int)((add >> 16) & 255) - (int)((remove >> 16) & 255);
            green += (int)((add >> 8) & 255) - (int)((remove >> 8) & 255);
            blue += (int)(add & 255) - (int)(remove & 255);
        }
    }
}

cairo_surface_t *dash_blur_background(GdkPixbuf *source, int logical_width, int logical_height)
{
    if (!source || logical_width <= 0 || logical_height <= 0)
        return NULL;
    int width = MAX(1, (logical_width + 5) / 6);
    int height = MAX(1, (logical_height + 5) / 6);
    GdkPixbuf *small = gdk_pixbuf_scale_simple(source, width, height, GDK_INTERP_BILINEAR);
    if (!small)
        return NULL;
    guint32 *pixels = g_new(guint32, (gsize)width * height);
    guint32 *scratch = g_new(guint32, (gsize)width * height);
    const guchar *data = gdk_pixbuf_read_pixels(small);
    int channels = gdk_pixbuf_get_n_channels(small), stride = gdk_pixbuf_get_rowstride(small);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            const guchar *in = data + y * stride + x * channels;
            guint alpha = channels == 4 ? in[3] : 255;
            pixels[y * width + x] = 0xff000000u | (((in[0] * alpha / 255) & 255) << 16) |
                                    (((in[1] * alpha / 255) & 255) << 8) | (in[2] * alpha / 255);
        }
    g_object_unref(small);
    for (int pass = 0; pass < 3; pass++) {
        blur_pass(pixels, scratch, width, height, TRUE);
        blur_pass(scratch, pixels, width, height, FALSE);
    }
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
        guchar *out = cairo_image_surface_get_data(surface);
        int out_stride = cairo_image_surface_get_stride(surface);
        for (int y = 0; y < height; y++)
            memcpy(out + y * out_stride, pixels + y * width, (gsize)width * 4);
        cairo_surface_mark_dirty(surface);
    } else {
        cairo_surface_destroy(surface);
        surface = NULL;
    }
    g_free(pixels);
    g_free(scratch);
    return surface;
}

void dash_paint_frame(cairo_t *cr, int width, int height, const DashGeometry *g,
                      const double color[3], double opacity, cairo_surface_t *backdrop, guint count)
{
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, .18);
    cairo_paint(cr);
    rounded(cr, g->x + .5, .5, g->width - 1, g->height - 1, 12);
    if (backdrop) {
        cairo_save(cr);
        cairo_clip_preserve(cr);
        cairo_save(cr);
        cairo_translate(cr, g->x, 0);
        cairo_scale(cr, (double)g->width / cairo_image_surface_get_width(backdrop),
                    (double)g->height / cairo_image_surface_get_height(backdrop));
        cairo_set_source_surface(cr, backdrop, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
        cairo_paint(cr);
        cairo_restore(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        /* Keep white labels readable over bright desktops at low opacity. */
        cairo_set_source_rgba(cr, 0, 0, 0, .55 * (1 - opacity));
        cairo_paint(cr);
        cairo_set_source_rgba(cr, color[0], color[1], color[2], opacity);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        /* A compositor/capture failure must not expose sharp, busy windows. */
        cairo_set_source_rgba(cr, color[0], color[1], color[2], MAX(.96, opacity));
        cairo_fill_preserve(cr);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 1, 1, 1, .19);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, .97, .96, .98);
    text(cr, "Applications", g->grid_x + 4, 22, g->grid_width - 44, 24, TRUE, FALSE, 1);
    /* The close affordance is part of the overlay, never a WM titlebar. */
    double cx = g->x + g->width - 28, cy = 35;
    cairo_set_source_rgba(cr, 1, 1, 1, .65);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, cx - 4, cy - 4);
    cairo_line_to(cr, cx + 4, cy + 4);
    cairo_move_to(cr, cx + 4, cy - 4);
    cairo_line_to(cr, cx - 4, cy + 4);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, .11);
    cairo_rectangle(cr, g->grid_x, g->height - 34, g->grid_width, 1);
    cairo_fill(cr);
    char *status = g_strdup_printf("%u application%s", count, count == 1 ? "" : "s");
    cairo_set_source_rgba(cr, 1, 1, 1, .55);
    text(cr, status, g->grid_x + 4, g->height - 26, g->grid_width / 2, 11, FALSE, FALSE, 1);
    g_free(status);
    text(cr, "Enter to open   ·   Esc to close", g->grid_x + g->grid_width / 2, g->height - 26,
         g->grid_width / 2 - 4, 11, FALSE, TRUE, 1);
    cairo_restore(cr);
    (void)width;
    (void)height;
}

void dash_paint_tile(cairo_t *cr, double width, const char *name, GdkPixbuf *icon, int scale,
                     gboolean selected, gboolean hovered, gboolean pinned)
{
    cairo_save(cr);
    if (selected || hovered) {
        rounded(cr, 3, 3, width - 6, DASH_CELL_HEIGHT - 6, 8);
        cairo_set_source_rgba(cr, 1, 1, 1, hovered ? .11 : .06);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, .59, .30, selected ? .85 : .16);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
    }
    double icon_x = (width - 48) / 2;
    if (icon) {
        cairo_save(cr);
        cairo_translate(cr, icon_x, 12);
        cairo_scale(cr, 48.0 / gdk_pixbuf_get_width(icon), 48.0 / gdk_pixbuf_get_height(icon));
        gdk_cairo_set_source_pixbuf(cr, icon, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        rounded(cr, icon_x, 12, 48, 48, 10);
        cairo_set_source_rgba(cr, 1, 1, 1, .08);
        cairo_fill(cr);
        char *initial = g_utf8_substring(name, 0, 1);
        cairo_set_source_rgba(cr, 1, 1, 1, .6);
        text(cr, initial, icon_x, 23, 48, 21, TRUE, TRUE, 1);
        g_free(initial);
    }
    cairo_set_source_rgb(cr, .96, .95, .98);
    text(cr, name, 7, 69, width - 14, 12, FALSE, TRUE, 2);
    if (hovered || pinned) {
        double x = width - 17, y = 16;
        if (hovered) {
            cairo_arc(cr, x, y, 9, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, 1, 1, 1, .13);
            cairo_fill(cr);
        }
        cairo_set_source_rgba(cr, 1, pinned ? .65 : 1, pinned ? .33 : 1, .9);
        cairo_set_line_width(cr, 1.5);
        if (pinned) {
            cairo_move_to(cr, x - 4, y);
            cairo_line_to(cr, x - 1, y + 3);
            cairo_line_to(cr, x + 4, y - 3);
        } else {
            cairo_move_to(cr, x - 4, y);
            cairo_line_to(cr, x + 4, y);
            cairo_move_to(cr, x, y - 4);
            cairo_line_to(cr, x, y + 4);
        }
        cairo_stroke(cr);
    }
    cairo_restore(cr);
    (void)scale;
}

typedef struct {
    GdkPixbuf *pixbuf;
    gboolean loading;
} DashIcon;

struct LauncherDash {
    GtkWidget *window, *fixed, *frame, *entry, *grid;
    DashModel *model;
    DashGeometry geometry;
    GHashTable *icons;
    GQueue icon_lru;
    guint icon_idle, generation, pending_icons, refs;
    gboolean closing;
    GCancellable *icon_cancel;
    int scale, selected, hovered, pressed;
    double scroll, color[3], opacity;
    cairo_surface_t *backdrop;
    gboolean pin_pressed;
    DashAppAction launch, pin;
    DashAppPinned pinned;
    gpointer data;
};

static void icon_free(gpointer data)
{
    DashIcon *icon = data;
    g_clear_object(&icon->pixbuf);
    g_free(icon);
}

static void redraw(LauncherDash *dash)
{
    gtk_widget_queue_draw(dash->frame);
    gtk_widget_queue_draw(dash->grid);
}

static void visible_range(LauncherDash *dash, guint *start, guint *end)
{
    *start = (guint)(dash->scroll / DASH_CELL_HEIGHT) * dash->geometry.columns;
    *end = MIN(dash->model->matches->len,
               (guint)ceil((dash->scroll + dash->geometry.grid_height) / DASH_CELL_HEIGHT) *
                   dash->geometry.columns);
}

static void queue_icons(LauncherDash *dash);

static void dash_unref(LauncherDash *dash)
{
    if (--dash->refs)
        return;
    dash_model_free(dash->model);
    g_clear_pointer(&dash->backdrop, cairo_surface_destroy);
    g_hash_table_unref(dash->icons);
    g_queue_clear(&dash->icon_lru);
    g_clear_object(&dash->icon_cancel);
    g_free(dash);
}

static void cache_icon(LauncherDash *dash, DashApp *app)
{
    g_queue_remove(&dash->icon_lru, app);
    g_queue_push_tail(&dash->icon_lru, app);
    while (g_queue_get_length(&dash->icon_lru) > DASH_ICON_CACHE_LIMIT)
        g_hash_table_remove(dash->icons, g_queue_pop_head(&dash->icon_lru));
}

static void cancel_icons(LauncherDash *dash)
{
    if (dash->icon_idle) {
        g_source_remove(dash->icon_idle);
        dash->icon_idle = 0;
    }
    if (dash->icon_cancel)
        g_cancellable_cancel(dash->icon_cancel);
    g_clear_object(&dash->icon_cancel);
    dash->generation++;
    dash->pending_icons = 0;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, dash->icons);
    while (g_hash_table_iter_next(&iter, NULL, &value))
        if (((DashIcon *)value)->loading)
            g_hash_table_iter_remove(&iter);
    if (!dash->closing)
        dash->icon_cancel = g_cancellable_new();
}

typedef struct {
    LauncherDash *dash;
    DashApp *app;
    guint generation;
} IconRequest;

static void icon_loaded(GObject *source, GAsyncResult *result, gpointer data)
{
    IconRequest *request = data;
    LauncherDash *dash = request->dash;
    GdkPixbuf *pixbuf = gtk_icon_info_load_icon_finish(GTK_ICON_INFO(source), result, NULL);
    /* Hiding/reloading cancels jobs; an old completion must not touch a new
     * catalog, a replacement icon request or a destroyed GTK window. */
    if (!dash->closing && request->generation == dash->generation) {
        DashIcon *icon = g_hash_table_lookup(dash->icons, request->app);
        dash->pending_icons--;
        if (icon) {
            icon->loading = FALSE;
            icon->pixbuf = pixbuf;
            pixbuf = NULL;
            cache_icon(dash, request->app);
        }
        gtk_widget_queue_draw(dash->grid);
        queue_icons(dash);
    }
    g_clear_object(&pixbuf);
    g_free(request);
    dash_unref(dash);
}

static gboolean load_visible_icon(gpointer data)
{
    LauncherDash *dash = data;
    if (!gtk_widget_get_visible(dash->window) || !dash->model || dash->pending_icons >= 4) {
        dash->icon_idle = 0;
        return G_SOURCE_REMOVE;
    }
    guint start, end;
    visible_range(dash, &start, &end);
    for (guint i = start; i < end; i++) {
        DashApp *app = dash_model_match(dash->model, i);
        if (g_hash_table_contains(dash->icons, app))
            continue;
        DashIcon *icon = g_new0(DashIcon, 1);
        GIcon *gicon = g_app_info_get_icon(G_APP_INFO(app->app));
        GtkIconTheme *theme = gtk_icon_theme_get_for_screen(gtk_widget_get_screen(dash->window));
        GtkIconInfo *info = gicon ? gtk_icon_theme_lookup_by_gicon_for_scale(
                                        theme, gicon, 48, dash->scale, GTK_ICON_LOOKUP_FORCE_SIZE)
                                  : NULL;
        if (!info)
            info = gtk_icon_theme_lookup_icon_for_scale(theme, "application-x-executable", 48,
                                                        dash->scale, GTK_ICON_LOOKUP_FORCE_SIZE);
        g_hash_table_insert(dash->icons, app, icon);
        if (info) {
            icon->loading = TRUE;
            IconRequest *request = g_new0(IconRequest, 1);
            *request = (IconRequest){dash, app, dash->generation};
            dash->refs++;
            dash->pending_icons++;
            gtk_icon_info_load_icon_async(info, dash->icon_cancel, icon_loaded, request);
            g_object_unref(info);
        } else
            cache_icon(dash, app);
        return G_SOURCE_CONTINUE;
    }
    dash->icon_idle = 0;
    return G_SOURCE_REMOVE;
}

static void queue_icons(LauncherDash *dash)
{
    if (!dash->closing && !dash->icon_idle && dash->pending_icons < 4 &&
        gtk_widget_get_visible(dash->window))
        dash->icon_idle = g_idle_add_full(G_PRIORITY_LOW, load_visible_icon, dash, NULL);
}

static gboolean frame_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    LauncherDash *dash = data;
    dash_paint_frame(cr, gtk_widget_get_allocated_width(widget),
                     gtk_widget_get_allocated_height(widget), &dash->geometry, dash->color,
                     dash->opacity, dash->backdrop, dash->model ? dash->model->matches->len : 0);
    return FALSE;
}

static gboolean grid_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    LauncherDash *dash = data;
    if (!dash->model)
        return FALSE;
    int scale = gtk_widget_get_scale_factor(widget);
    if (scale != dash->scale) {
        dash->scale = scale;
        launcher_dash_clear_icons(dash);
    }
    cairo_rectangle(cr, 0, 0, dash->geometry.grid_width, dash->geometry.grid_height);
    cairo_clip(cr);
    guint start, end;
    visible_range(dash, &start, &end);
    for (guint i = start; i < end; i++) {
        DashApp *app = dash_model_match(dash->model, i);
        DashIcon *icon = g_hash_table_lookup(dash->icons, app);
        if (icon && !icon->loading)
            cache_icon(dash, app);
        cairo_save(cr);
        cairo_translate(cr, (i % dash->geometry.columns) * dash->geometry.cell_width,
                        (i / dash->geometry.columns) * DASH_CELL_HEIGHT - dash->scroll);
        dash_paint_tile(
            cr, dash->geometry.cell_width, g_app_info_get_display_name(G_APP_INFO(app->app)),
            icon ? icon->pixbuf : NULL, scale, (int)i == dash->selected, (int)i == dash->hovered,
            dash->pinned && dash->pinned(app->app, dash->data));
        cairo_restore(cr);
    }
    if (!dash->model->matches->len) {
        cairo_set_source_rgba(cr, 1, 1, 1, .6);
        text(cr, "No applications found", 0, 42, dash->geometry.grid_width, 16, FALSE, TRUE, 1);
    }
    double limit = dash_scroll_limit(&dash->geometry, dash->model->matches->len);
    if (limit > 0) {
        double h = MAX(24, (double)dash->geometry.grid_height * dash->geometry.grid_height /
                               (limit + dash->geometry.grid_height));
        rounded(cr, dash->geometry.grid_width - 3,
                (dash->geometry.grid_height - h) * dash->scroll / limit, 3, h, 1.5);
        cairo_set_source_rgba(cr, 1, 1, 1, .28);
        cairo_fill(cr);
    }
    queue_icons(dash);
    return FALSE;
}

static void search_changed(GtkEditable *entry, gpointer data)
{
    LauncherDash *dash = data;
    if (!dash->model)
        return;
    dash_model_filter(dash->model, gtk_entry_get_text(GTK_ENTRY(entry)));
    dash->selected = dash->model->matches->len ? 0 : -1;
    dash->hovered = dash->pressed = -1;
    dash->scroll = 0;
    redraw(dash);
}

static void activate(LauncherDash *dash, int index, gboolean pin)
{
    DashApp *app = dash_model_match(dash->model, index);
    if (!app)
        return;
    if (pin) {
        if (dash->pin)
            dash->pin(app->app, dash->data);
        gtk_widget_queue_draw(dash->grid);
    } else if (dash->launch) {
        /* Keep the desktop entry alive across callbacks that may refresh apps. */
        GDesktopAppInfo *info = g_object_ref(app->app);
        launcher_dash_hide(dash);
        dash->launch(info, dash->data);
        g_object_unref(info);
    }
}

static void search_enter(GtkEntry *entry, gpointer data)
{
    (void)entry;
    LauncherDash *dash = data;
    activate(dash, dash->selected, FALSE);
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    (void)widget;
    LauncherDash *dash = data;
    if (event->keyval == GDK_KEY_Escape) {
        launcher_dash_hide(dash);
        return TRUE;
    }
    if (!dash->model || !dash->model->matches->len)
        return FALSE;
    int delta = 0;
    switch (event->keyval) {
    case GDK_KEY_Down:
        delta = dash->geometry.columns;
        break;
    case GDK_KEY_Up:
        delta = -dash->geometry.columns;
        break;
    case GDK_KEY_Right:
        if (event->state & GDK_CONTROL_MASK)
            return FALSE;
        delta = 1;
        break;
    case GDK_KEY_Left:
        if (event->state & GDK_CONTROL_MASK)
            return FALSE;
        delta = -1;
        break;
    case GDK_KEY_Tab:
        delta = event->state & GDK_SHIFT_MASK ? -1 : 1;
        break;
    case GDK_KEY_ISO_Left_Tab:
        delta = -1;
        break;
    default:
        return FALSE;
    }
    dash->selected = dash_grid_navigate(&dash->geometry, dash->selected, dash->model->matches->len,
                                        delta, &dash->scroll);
    dash->hovered = -1;
    gtk_widget_queue_draw(dash->grid);
    return TRUE;
}

static int hit(LauncherDash *dash, double x, double y)
{
    return dash_grid_hit(&dash->geometry, x, y, dash->scroll,
                         dash->model ? dash->model->matches->len : 0);
}

static gboolean pin_hit(LauncherDash *dash, double x, double y)
{
    double cx = fmod(x, dash->geometry.cell_width);
    double cy = fmod(y + dash->scroll, DASH_CELL_HEIGHT);
    return cx >= dash->geometry.cell_width - 28 && cx < dash->geometry.cell_width - 6 && cy >= 5 &&
           cy < 27;
}

static gboolean grid_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    LauncherDash *dash = data;
    int hovered = hit(dash, event->x, event->y);
    if (hovered != dash->hovered) {
        dash->hovered = hovered;
        gtk_widget_queue_draw(widget);
        DashApp *app = dash_model_match(dash->model, hovered);
        gtk_widget_set_tooltip_text(widget,
                                    app ? g_app_info_get_display_name(G_APP_INFO(app->app)) : NULL);
    }
    return TRUE;
}

static gboolean grid_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
    (void)event;
    LauncherDash *dash = data;
    dash->hovered = -1;
    gtk_widget_queue_draw(widget);
    return FALSE;
}

static gboolean grid_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    LauncherDash *dash = data;
    if (event->button != 1)
        return FALSE;
    dash->pressed = hit(dash, event->x, event->y);
    dash->pin_pressed = pin_hit(dash, event->x, event->y);
    return TRUE;
}

static gboolean grid_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    LauncherDash *dash = data;
    if (event->button != 1)
        return FALSE;
    int target = hit(dash, event->x, event->y), pressed = dash->pressed;
    dash->pressed = -1;
    if (target >= 0 && target == pressed && dash->pin_pressed == pin_hit(dash, event->x, event->y))
        activate(dash, target, dash->pin_pressed);
    return TRUE;
}

static gboolean scroll_grid(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    LauncherDash *dash = data;
    if (!dash->model)
        return TRUE;
    double delta = event->direction == GDK_SCROLL_SMOOTH ? event->delta_y * 36
                   : event->direction == GDK_SCROLL_UP   ? -60
                                                         : 60;
    dash->scroll = CLAMP(dash->scroll + delta, 0,
                         dash_scroll_limit(&dash->geometry, dash->model->matches->len));
    dash->hovered = dash->pressed = -1;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean backdrop_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    (void)widget;
    LauncherDash *dash = data;
    double x = event->x - dash->geometry.x;
    if (event->button == 1 &&
        (x < 0 || x >= dash->geometry.width || event->y >= dash->geometry.height ||
         (x >= dash->geometry.width - 48 && event->y < 56))) {
        launcher_dash_hide(dash);
        return TRUE;
    }
    return FALSE;
}

static gboolean focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    (void)widget;
    (void)event;
    launcher_dash_hide(data);
    return FALSE;
}

static void hidden(GtkWidget *widget, gpointer data)
{
    (void)widget;
    LauncherDash *dash = data;
    cancel_icons(dash);
    g_clear_pointer(&dash->backdrop, cairo_surface_destroy);
    dash->pressed = dash->hovered = -1;
}

static const char dash_css[] =
    "#lunatic-dash { background: transparent; box-shadow: none; }"
    "#lunatic-dash-search { background: rgba(0,0,0,0.24); background-image: none; "
    "color: #faf8fc; border: 1px solid rgba(255,255,255,0.23); border-radius: 7px; "
    "padding: 8px 12px; min-height: 22px; font-size: 16px; box-shadow: none; }"
    "#lunatic-dash-search:focus { border-color: #e9a06b; }";

LauncherDash *launcher_dash_new(DashAppAction launch, DashAppAction pin, DashAppPinned pinned,
                                gpointer data)
{
    LauncherDash *dash = g_new0(LauncherDash, 1);
    dash->launch = launch;
    dash->pin = pin;
    dash->pinned = pinned;
    dash->data = data;
    dash->scale = 1;
    dash->refs = 1;
    dash->icon_cancel = g_cancellable_new();
    dash->selected = dash->hovered = dash->pressed = -1;
    dash->icons = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, icon_free);
    dash->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dash->window), "Lunatic Applications");
    gtk_widget_set_name(dash->window, "lunatic-dash");
    gtk_window_set_decorated(GTK_WINDOW(dash->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(dash->window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dash->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(dash->window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(dash->window), GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU);
    gtk_window_stick(GTK_WINDOW(dash->window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(dash->window));
    if (visual)
        gtk_widget_set_visual(dash->window, visual);
    gtk_widget_set_app_paintable(dash->window, TRUE);
    dash->fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(dash->window), dash->fixed);
    dash->frame = gtk_drawing_area_new();
    gtk_widget_add_events(dash->frame, GDK_BUTTON_PRESS_MASK);
    gtk_fixed_put(GTK_FIXED(dash->fixed), dash->frame, 0, 0);
    dash->entry = gtk_search_entry_new();
    gtk_widget_set_name(dash->entry, "lunatic-dash-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(dash->entry), "Search applications…");
    gtk_entry_set_max_length(GTK_ENTRY(dash->entry), 256);
    gtk_entry_set_width_chars(GTK_ENTRY(dash->entry), 1);
    gtk_fixed_put(GTK_FIXED(dash->fixed), dash->entry, 24, 72);
    dash->grid = gtk_drawing_area_new();
    gtk_widget_add_events(dash->grid, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK |
                                          GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                          GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_widget_set_has_tooltip(dash->grid, TRUE);
    gtk_fixed_put(GTK_FIXED(dash->fixed), dash->grid, 24, 132);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, dash_css, -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(dash->window),
                                   GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_style_context_add_provider(gtk_widget_get_style_context(dash->entry),
                                   GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    g_signal_connect(dash->frame, "draw", G_CALLBACK(frame_draw), dash);
    g_signal_connect(dash->frame, "button-press-event", G_CALLBACK(backdrop_press), dash);
    g_signal_connect(dash->grid, "draw", G_CALLBACK(grid_draw), dash);
    g_signal_connect(dash->grid, "motion-notify-event", G_CALLBACK(grid_motion), dash);
    g_signal_connect(dash->grid, "leave-notify-event", G_CALLBACK(grid_leave), dash);
    g_signal_connect(dash->grid, "button-press-event", G_CALLBACK(grid_press), dash);
    g_signal_connect(dash->grid, "button-release-event", G_CALLBACK(grid_release), dash);
    g_signal_connect(dash->grid, "scroll-event", G_CALLBACK(scroll_grid), dash);
    g_signal_connect(dash->entry, "changed", G_CALLBACK(search_changed), dash);
    g_signal_connect(dash->entry, "activate", G_CALLBACK(search_enter), dash);
    /* A window key handler runs after entry editing. Arrow navigation also
     * belongs on the entry so its default caret handling cannot swallow it. */
    g_signal_connect(dash->entry, "key-press-event", G_CALLBACK(key_press), dash);
    g_signal_connect(dash->window, "key-press-event", G_CALLBACK(key_press), dash);
    g_signal_connect(dash->window, "focus-out-event", G_CALLBACK(focus_out), dash);
    g_signal_connect(dash->window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    g_signal_connect(dash->window, "hide", G_CALLBACK(hidden), dash);
    return dash;
}

GtkWidget *launcher_dash_window(LauncherDash *dash)
{
    return dash->window;
}

void launcher_dash_clear_icons(LauncherDash *dash)
{
    if (!dash)
        return;
    cancel_icons(dash);
    g_hash_table_remove_all(dash->icons);
    g_queue_clear(&dash->icon_lru);
    if (dash->grid)
        gtk_widget_queue_draw(dash->grid);
}

void launcher_dash_set_apps(LauncherDash *dash, GList *apps)
{
    launcher_dash_clear_icons(dash);
    dash_model_free(dash->model);
    dash->model = dash_model_new(apps);
    search_changed(GTK_EDITABLE(dash->entry), dash);
}

void launcher_dash_hide(LauncherDash *dash)
{
    if (dash)
        gtk_widget_hide(dash->window);
}

void launcher_dash_set_background(LauncherDash *dash, const double color[3], double opacity)
{
    if (!dash || (memcmp(dash->color, color, sizeof(dash->color)) == 0 && dash->opacity == opacity))
        return;
    memcpy(dash->color, color, sizeof(dash->color));
    dash->opacity = opacity;
    gtk_widget_queue_draw(dash->frame);
}

static cairo_surface_t *capture_backdrop(LauncherDash *dash, const GdkRectangle *bounds)
{
    if (dash->opacity >= 1)
        return NULL;
    GdkScreen *screen = gtk_widget_get_screen(dash->window);
    GdkWindow *root = gdk_screen_get_root_window(screen);
    GdkDisplay *display = gtk_widget_get_display(dash->window);
    DashGeometry *g = &dash->geometry;
    int x = bounds->x + g->x, y = bounds->y;
    if (!root || x < 0 || y < 0 || x + g->width > gdk_window_get_width(root) ||
        y + g->height > gdk_window_get_height(root))
        return NULL;
    int width = MAX(1, (g->width + 5) / 6), height = MAX(1, (g->height + 5) / 6);
    Display *xdisplay = gdk_x11_display_get_xdisplay(display);
    Visual *visual = gdk_x11_visual_get_xvisual(gdk_window_get_visual(root));
    int event, error;
    if (!XRenderQueryExtension(xdisplay, &event, &error))
        return NULL;
    XRenderPictFormat *format = XRenderFindVisualFormat(xdisplay, visual);
    if (!format)
        return NULL;
    int scale = gdk_window_get_scale_factor(root);
    /* IncludeInferiors is essential: the screenshot must contain application
     * windows and the compositor overlay, not just the root wallpaper. Reduce
     * it on the X server so only the small sampled image crosses to the CPU. */
    gdk_x11_display_error_trap_push(display);
    Window root_xid = gdk_x11_window_get_xid(root);
    Pixmap pixmap = XCreatePixmap(xdisplay, root_xid, width, height, format->depth);
    XRenderPictureAttributes attributes = {.subwindow_mode = IncludeInferiors};
    Picture source = XRenderCreatePicture(xdisplay, root_xid, format, CPSubwindowMode, &attributes);
    Picture target = XRenderCreatePicture(xdisplay, pixmap, format, 0, NULL);
    XTransform transform = {
        {{XDoubleToFixed((double)g->width * scale / width), 0, XDoubleToFixed((double)x * scale)},
         {0, XDoubleToFixed((double)g->height * scale / height), XDoubleToFixed((double)y * scale)},
         {0, 0, XDoubleToFixed(1)}}};
    XRenderSetPictureTransform(xdisplay, source, &transform);
    XRenderSetPictureFilter(xdisplay, source, FilterBilinear, NULL, 0);
    XRenderComposite(xdisplay, PictOpSrc, source, None, target, 0, 0, 0, 0, 0, 0, width, height);
    cairo_surface_t *sample = cairo_xlib_surface_create(xdisplay, pixmap, visual, width, height);
    GdkPixbuf *pixels = gdk_pixbuf_get_from_surface(sample, 0, 0, width, height);
    cairo_surface_destroy(sample);
    XRenderFreePicture(xdisplay, source);
    XRenderFreePicture(xdisplay, target);
    XFreePixmap(xdisplay, pixmap);
    if (gdk_x11_display_error_trap_pop(display))
        g_clear_object(&pixels);
    cairo_surface_t *blurred = dash_blur_background(pixels, g->width, g->height);
    g_clear_object(&pixels);
    return blurred;
}

void launcher_dash_toggle(LauncherDash *dash, GtkWindow *parent, const GdkRectangle *bounds,
                          gboolean right)
{
    if (gtk_widget_get_visible(dash->window)) {
        launcher_dash_hide(dash);
        return;
    }
    dash->geometry = dash_geometry(bounds->width, bounds->height, right);
    g_clear_pointer(&dash->backdrop, cairo_surface_destroy);
    /* Capture while hidden, so Dash never samples its own frame or labels. */
    dash->backdrop = capture_backdrop(dash, bounds);
    DashGeometry *g = &dash->geometry;
    gtk_window_set_transient_for(GTK_WINDOW(dash->window), parent);
    gtk_widget_set_size_request(dash->frame, bounds->width, bounds->height);
    gtk_widget_set_size_request(dash->entry, g->grid_width, 42);
    gtk_fixed_move(GTK_FIXED(dash->fixed), dash->entry, g->grid_x, g->grid_y - 60);
    gtk_widget_set_size_request(dash->grid, g->grid_width, g->grid_height);
    gtk_fixed_move(GTK_FIXED(dash->fixed), dash->grid, g->grid_x, g->grid_y);
    gtk_window_move(GTK_WINDOW(dash->window), bounds->x, bounds->y);
    gtk_window_resize(GTK_WINDOW(dash->window), bounds->width, bounds->height);
    gtk_entry_set_text(GTK_ENTRY(dash->entry), "");
    search_changed(GTK_EDITABLE(dash->entry), dash);
    gtk_widget_show_all(dash->window);
    gtk_window_present(GTK_WINDOW(dash->window));
    gtk_widget_grab_focus(dash->entry);
    queue_icons(dash);
}

void launcher_dash_free(LauncherDash *dash)
{
    if (!dash)
        return;
    dash->closing = TRUE;
    cancel_icons(dash);
    gtk_widget_destroy(dash->window);
    /* Asynchronous completions retain only this small state until they finish. */
    dash_unref(dash);
}
