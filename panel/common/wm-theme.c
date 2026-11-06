/* Copyright (C) 2026 xoxorwr. SPDX-License-Identifier: GPL-3.0-only */
#include "wm-theme.h"
#include <libxfce4util/libxfce4util.h>
#include <stdio.h>
#include <string.h>

struct PanelWmTheme {
    gatomicrefcount refs;
    cairo_surface_t *frames[3][4];
    gint width[3], height[3], spacing;
};

static GHashTable *read_options(const char *directory)
{
    GHashTable *options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    char *path = g_build_filename(directory, "themerc", NULL), *contents = NULL;
    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        char **lines = g_strsplit(contents, "\n", -1);
        for (guint i = 0; lines[i]; i++) {
            char *line = g_strstrip(lines[i]), *equal = strchr(line, '=');
            if (*line == '#' || !equal) continue;
            *equal = '\0';
            g_hash_table_replace(options, g_ascii_strdown(g_strstrip(line), -1),
                                 g_strdup(g_strstrip(equal + 1)));
        }
        g_strfreev(lines);
    }
    g_free(contents);
    g_free(path);
    return options;
}

static const char *resolve_color(GHashTable *options, GHashTable *colors, const char *symbol)
{
    const char *value = symbol;
    for (guint depth = 0; depth < 24; depth++) {
        char *key = g_ascii_strdown(value, -1);
        const char *next = g_hash_table_lookup(options, key);
        if (!next && colors) next = g_hash_table_lookup(colors, key);
        g_free(key);
        if (!next || g_str_equal(next, value)) break;
        value = next;
    }
    return value;
}

/* Read C XPM string literals, replacing only declared symbolic colors. The
 * ordinary GdkPixbuf XPM loader ignores the symbols used by xfwm4 themes. */
static GdkPixbuf *load_xpm(const char *path, GHashTable *options, GHashTable *colors)
{
    char *contents;
    gsize length;
    if (!g_file_get_contents(path, &contents, &length, NULL)) return NULL;
    if (length > 4 * 1024 * 1024) { g_free(contents); return NULL; }
    GPtrArray *lines = g_ptr_array_new_with_free_func(g_free);
    char *body = strchr(contents, '{');
    for (char *p = body ? body : contents; *p; p++) {
        if (*p != '"') continue;
        char *start = ++p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        if (!*p) break;
        char *literal = g_strndup(start, p - start);
        g_ptr_array_add(lines, g_strcompress(literal));
        g_free(literal);
    }
    g_free(contents);
    gint w, h, count, cpp;
    GdkPixbuf *image = NULL;
    if (!lines->len || sscanf(lines->pdata[0], "%d %d %d %d", &w, &h, &count, &cpp) != 4 ||
        w < 1 || w > 512 || h < 1 || h > 512 || count < 1 || count > 4096 || cpp < 1 || cpp > 4 ||
        lines->len < (guint)(1 + count + h)) goto done;
    for (gint i = 0; i < count; i++) {
        char *line = lines->pdata[i + 1];
        if (strlen(line) < (gsize)cpp) goto done;
        char **tokens = g_strsplit_set(line + cpp, " \t", -1);
        for (guint j = 0; tokens[j]; j++) {
            if (!g_str_equal(tokens[j], "s")) continue;
            guint k = j + 1;
            while (tokens[k] && !*tokens[k]) k++;
            if (!tokens[k]) break;
            const char *value = resolve_color(options, colors, tokens[k]);
            GdkRGBA rgba;
            char *replacement;
            if (!g_ascii_strcasecmp(value, "None")) replacement = g_strdup_printf("%.*s c None", cpp, line);
            else {
                if (!gdk_rgba_parse(&rgba, value)) break;
                replacement = g_strdup_printf("%.*s c #%04x%04x%04x", cpp, line,
                    (guint)(rgba.red * 65535 + .5), (guint)(rgba.green * 65535 + .5), (guint)(rgba.blue * 65535 + .5));
            }
            g_free(line);
            lines->pdata[i + 1] = replacement;
            break;
        }
        g_strfreev(tokens);
    }
    for (gint i = 0; i < h; i++)
        if (strlen(lines->pdata[1 + count + i]) < (gsize)(w * cpp)) goto done;
    g_ptr_array_add(lines, NULL);
    /* GTK 3 theme XPMs need in-process symbolic recoloring. Keep the legacy
     * data decoder (deprecated only in GdkPixbuf 2.44), with bounds checked above. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    image = gdk_pixbuf_new_from_xpm_data((const char **)lines->pdata);
    G_GNUC_END_IGNORE_DEPRECATIONS
done:
    g_ptr_array_unref(lines);
    return image;
}

static cairo_surface_t *image_surface(GdkPixbuf *image, gint scale);

static cairo_surface_t *load_png(const char *path)
{
    /* Cairo decodes PNG in-process even when GTK's optional image loader is
     * absent. Inspect dimensions before allocating the decoded surface. */
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    guchar header[24];
    gsize count = fread(header, 1, sizeof header, file);
    fclose(file);
    if (count != sizeof header || memcmp(header, "\211PNG\r\n\032\n", 8) || memcmp(header + 12, "IHDR", 4)) return NULL;
    guint32 width, height;
    memcpy(&width, header + 16, 4); memcpy(&height, header + 20, 4);
    width = GUINT32_FROM_BE(width); height = GUINT32_FROM_BE(height);
    if (!width || width > 512 || !height || height > 512) return NULL;
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) return surface;
    cairo_surface_destroy(surface);
    return NULL;
}

static cairo_surface_t *load_image(const char *directory, const char *name, GHashTable *options, GHashTable *colors)
{
    char *path = g_strdup_printf("%s/%s.xpm", directory, name);
    GdkPixbuf *pixels = load_xpm(path, options, colors);
    cairo_surface_t *image = pixels ? image_surface(pixels, 1) : NULL;
    g_clear_object(&pixels);
    g_free(path);
    const char *extensions[] = {"svg", "png", "gif", "jpg", "bmp"};
    for (guint i = 0; i < G_N_ELEMENTS(extensions); i++) {
        path = g_strdup_printf("%s/%s.%s", directory, name, extensions[i]);
        cairo_surface_t *overlay = NULL;
        if (g_str_equal(extensions[i], "png")) overlay = load_png(path);
        else {
            gint w, h;
            if (gdk_pixbuf_get_file_info(path, &w, &h) && w > 0 && w <= 512 && h > 0 && h <= 512)
                pixels = gdk_pixbuf_new_from_file(path, NULL);
            if (pixels) overlay = image_surface(pixels, 1);
            g_clear_object(&pixels);
        }
        g_free(path);
        if (!overlay) continue;
        if (!image) return overlay;
        cairo_t *cr = cairo_create(image);
        cairo_set_source_surface(cr, overlay, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(overlay);
        break;
    }
    return image;
}

static cairo_surface_t *image_surface(GdkPixbuf *image, gint scale)
{
    gint w = gdk_pixbuf_get_width(image), h = gdk_pixbuf_get_height(image);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) return surface;
    guchar *data = cairo_image_surface_get_data(surface);
    gint stride = cairo_image_surface_get_stride(surface), channels = gdk_pixbuf_get_n_channels(image);
    const guchar *pixels = gdk_pixbuf_read_pixels(image);
    for (gint y = 0; y < h; y++) {
        guint32 *row = (guint32 *)(data + y * stride);
        const guchar *src = pixels + y * gdk_pixbuf_get_rowstride(image);
        for (gint x = 0; x < w; x++, src += channels) {
            guint a = channels == 4 ? src[3] : 255;
            row[x] = (a << 24) | (((src[0] * a + 127) / 255) << 16) |
                     (((src[1] * a + 127) / 255) << 8) | ((src[2] * a + 127) / 255);
        }
    }
    cairo_surface_mark_dirty(surface);
    cairo_surface_set_device_scale(surface, scale, scale);
    return surface;
}

PanelWmTheme *panel_wm_theme_load(const char *directory, GHashTable *colors, gint scale)
{
    g_return_val_if_fail(directory != NULL && scale >= 1, NULL);
    PanelWmTheme *theme = g_new0(PanelWmTheme, 1);
    g_atomic_ref_count_init(&theme->refs);
    GHashTable *options = read_options(directory);
    const char *spacing = g_hash_table_lookup(options, "button_spacing");
    gint pixels = spacing ? CLAMP(g_ascii_strtoll(spacing, NULL, 10), 0, 32) : 0;
    theme->spacing = (pixels + scale - 1) / scale;
    const char *names[] = {"close", "hide", "maximize"};
    const char *states[] = {"active", "prelight", "pressed", "inactive"};
    gboolean found = FALSE;
    for (guint c = 0; c < 3; c++) for (guint s = 0; s < 4; s++) {
        char *name = g_strdup_printf("%s-%s%s", names[c], c == PANEL_WINDOW_RESTORE ? "toggled-" : "", states[s]);
        cairo_surface_t *image = load_image(directory, name, options, colors);
        g_free(name);
        if (!image && c == PANEL_WINDOW_RESTORE) {
            name = g_strdup_printf("%s-%s", names[c], states[s]);
            image = load_image(directory, name, options, colors);
            g_free(name);
        }
        if (!image) continue;
        gint w = cairo_image_surface_get_width(image), h = cairo_image_surface_get_height(image);
        cairo_surface_set_device_scale(image, scale, scale);
        theme->frames[c][s] = image;
        theme->width[c] = MAX(theme->width[c], (w + scale - 1) / scale);
        theme->height[c] = MAX(theme->height[c], (h + scale - 1) / scale);
        found = TRUE;
    }
    g_hash_table_unref(options);
    if (!found) { panel_wm_theme_unref(theme); return NULL; }
    return theme;
}

PanelWmTheme *panel_wm_theme_ref(PanelWmTheme *theme)
{
    if (theme) g_atomic_ref_count_inc(&theme->refs);
    return theme;
}
void panel_wm_theme_unref(PanelWmTheme *theme)
{
    if (!theme || !g_atomic_ref_count_dec(&theme->refs)) return;
    for (guint c = 0; c < 3; c++) for (guint s = 0; s < 4; s++)
        if (theme->frames[c][s]) cairo_surface_destroy(theme->frames[c][s]);
    g_free(theme);
}
cairo_surface_t *panel_wm_theme_frame(PanelWmTheme *theme, PanelWindowCommand c, PanelWmState s)
{
    if (!theme || (guint)c >= 3 || (guint)s >= 4) return NULL;
    return theme->frames[c][s] ? theme->frames[c][s] : theme->frames[c][PANEL_WM_NORMAL];
}
gint panel_wm_theme_spacing(PanelWmTheme *theme) { return theme ? theme->spacing : 0; }

guint panel_wm_button_order(const char *layout, PanelWindowCommand order[3])
{
    gboolean seen[3] = {0};
    guint count = 0;
    for (const char *p = layout ? layout : "HMC"; *p; p++) {
        gint c = *p == 'C' ? PANEL_WINDOW_CLOSE : *p == 'H' ? PANEL_WINDOW_MINIMIZE :
                 *p == 'M' ? PANEL_WINDOW_RESTORE : -1;
        if (c >= 0 && !seen[c]) { order[count++] = c; seen[c] = TRUE; }
    }
    return count;
}

static double hue_channel(double p, double q, double t)
{
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1. / 6) return p + (q - p) * 6 * t;
    if (t < .5) return q;
    if (t < 2. / 3) return p + (q - p) * (2. / 3 - t) * 6;
    return p;
}
static GdkRGBA shade(GdkRGBA c, double factor)
{
    double high = MAX(c.red, MAX(c.green, c.blue)), low = MIN(c.red, MIN(c.green, c.blue));
    double l = (high + low) / 2, delta = high - low, s = 0, h = 0;
    if (delta > 0) {
        s = l <= .5 ? delta / (high + low) : delta / (2 - high - low);
        h = high == c.red ? (c.green - c.blue) / delta + (c.green < c.blue ? 6 : 0) :
            high == c.green ? (c.blue - c.red) / delta + 2 : (c.red - c.green) / delta + 4;
        h /= 6;
    }
    l = CLAMP(l * factor, 0, 1); s = CLAMP(s * factor, 0, 1);
    double q = l < .5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    c.red = hue_channel(p, q, h + 1. / 3); c.green = hue_channel(p, q, h); c.blue = hue_channel(p, q, h - 1. / 3);
    return c;
}

static GHashTable *style_colors(GtkWidget *widget)
{
    /* Match xfwm4's GTK fallback palette, before themerc overrides/aliases. */
    static const char *keys[] = {"active_text_color", "inactive_text_color", "active_text_shadow_color",
        "inactive_text_shadow_color", "active_border_color", "inactive_border_color",
        "active_color_1", "active_hilight_1", "active_shadow_1", "active_mid_1", "active_text_color_2",
        "active_color_2", "active_hilight_2", "active_shadow_2", "active_mid_2",
        "inactive_color_1", "inactive_hilight_1", "inactive_shadow_1", "inactive_mid_1", "inactive_text_color_2",
        "inactive_color_2", "inactive_hilight_2", "inactive_shadow_2", "inactive_mid_2"};
    static const guint parts[] = {0,0,3,3,0,0,1,2,3,4,0,1,2,3,4,1,2,3,4,0,1,2,3,4};
    GtkStyleContext *context = gtk_style_context_new();
    GtkWidgetPath *path = gtk_widget_path_new();
    gtk_widget_path_append_type(path, GTK_TYPE_WINDOW);
    gtk_widget_path_iter_set_object_name(path, -1, "window");
    gtk_widget_path_iter_set_name(path, -1, "xfwm");
    gtk_style_context_set_path(context, path);
    gtk_style_context_set_screen(context, gtk_widget_get_screen(widget));
    gtk_style_context_set_scale(context, gtk_widget_get_scale_factor(widget));
    gtk_style_context_add_class(context, "background");
    gtk_style_context_add_class(context, "gtkstyle-fallback");
    GHashTable *colors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
        GtkStateFlags state = (i == 0 || i == 2 || (i >= 6 && i <= 9)) ? GTK_STATE_FLAG_SELECTED :
            (i == 1 || i == 3 || (i >= 15 && i <= 18)) ? GTK_STATE_FLAG_INSENSITIVE : GTK_STATE_FLAG_NORMAL;
        GdkRGBA *value = NULL;
        gtk_style_context_get(context, state, parts[i] == 0 ? "color" : "background-color", &value, NULL);
        GdkRGBA c = value ? *value : (GdkRGBA){0,0,0,1};
        if (value) gdk_rgba_free(value);
        if (parts[i] == 2) c = shade(c, 1.3);
        else if (parts[i] == 3) c = shade(c, .7);
        else if (parts[i] == 4) {
            GdkRGBA light = shade(c, 1.3), dark = shade(c, .7);
            c.red = (light.red + dark.red) / 2; c.green = (light.green + dark.green) / 2; c.blue = (light.blue + dark.blue) / 2;
        }
        char *text = g_strdup_printf("#%04x%04x%04x", (guint)(c.red * 65535 + .5),
                                    (guint)(c.green * 65535 + .5), (guint)(c.blue * 65535 + .5));
        g_hash_table_insert(colors, g_strdup(keys[i]), text);
    }
    gtk_widget_path_unref(path);
    g_object_unref(context);
    return colors;
}

PanelWmTheme *panel_wm_theme_new(const char *name, GtkWidget *style_widget)
{
    gint scale = MAX(1, gtk_widget_get_scale_factor(style_widget));
    const char *selected = name && *name ? name : "Default";
    if (scale > 1 && g_str_equal(selected, "Default")) selected = "Default-hdpi";
    char *directory = NULL;
    if (g_path_is_absolute(selected) && g_file_test(selected, G_FILE_TEST_IS_DIR)) directory = g_strdup(selected);
    else {
        char *relative = g_build_filename(selected, "xfwm4", "themerc", NULL);
        char *path = xfce_resource_lookup(XFCE_RESOURCE_THEMES, relative);
        if (path) directory = g_path_get_dirname(path);
        g_free(path); g_free(relative);
    }
    if (!directory) {
        char *path = xfce_resource_lookup(XFCE_RESOURCE_THEMES, "Default/xfwm4/themerc");
        if (path) directory = g_path_get_dirname(path);
        g_free(path);
    }
    if (!directory) return NULL;
    GHashTable *colors = style_colors(style_widget);
    PanelWmTheme *theme = panel_wm_theme_load(directory, colors, scale);
    g_hash_table_unref(colors);
    g_free(directory);
    return theme;
}

/* GtkButton supplies accessible activation and press/release handling; all
 * visuals and dimensions below come from xfwm4, never GTK button/icon styling. */
typedef struct { GtkButton parent; PanelWmTheme *theme; PanelWindowCommand command; } PanelWmButton;
typedef GtkButtonClass PanelWmButtonClass;
G_DEFINE_TYPE(PanelWmButton, panel_wm_button, GTK_TYPE_BUTTON)

static gboolean button_draw(GtkWidget *widget, cairo_t *cr)
{
    PanelWmButton *button = (PanelWmButton *)widget;
    GtkStateFlags flags = gtk_widget_get_state_flags(widget);
    PanelWmState state = !gtk_widget_is_sensitive(widget) ? PANEL_WM_DISABLED :
        flags & GTK_STATE_FLAG_ACTIVE ? PANEL_WM_PRESSED : flags & GTK_STATE_FLAG_PRELIGHT ? PANEL_WM_HOVER : PANEL_WM_NORMAL;
    cairo_surface_t *frame = panel_wm_theme_frame(button->theme, button->command, state);
    if (!frame) return TRUE;
    double sx, sy;
    cairo_surface_get_device_scale(frame, &sx, &sy);
    double width = cairo_image_surface_get_width(frame) / sx, height = cairo_image_surface_get_height(frame) / sy;
    double factor = MIN(1., MIN(gtk_widget_get_allocated_width(widget) / width, gtk_widget_get_allocated_height(widget) / height));
    double x = (gtk_widget_get_allocated_width(widget) - width * factor) / 2;
    double y = (gtk_widget_get_allocated_height(widget) - height * factor) / 2;
    cairo_save(cr);
    /* Center on physical pixels so odd-sized theme artwork stays sharp. */
    cairo_translate(cr, (gint)(x * sx + .5) / sx, (gint)(y * sy + .5) / sy);
    cairo_scale(cr, factor, factor);
    cairo_set_source_surface(cr, frame, 0, 0);
    cairo_paint(cr);
    if (gtk_widget_has_visible_focus(widget)) {
        cairo_set_source_rgba(cr, 1, 1, 1, .8);
        double dashes[] = {1, 1};
        cairo_set_dash(cr, dashes, 2, 0); cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, .5, .5, MAX(0, width - 1), MAX(0, height - 1)); cairo_stroke(cr);
    }
    cairo_restore(cr);
    return TRUE;
}
static void button_width(GtkWidget *widget, gint *minimum, gint *natural)
{
    PanelWmButton *button = (PanelWmButton *)widget;
    *minimum = *natural = button->theme ? button->theme->width[button->command] : 0;
}
static void button_height(GtkWidget *widget, gint *minimum, gint *natural)
{
    PanelWmButton *button = (PanelWmButton *)widget;
    *minimum = *natural = button->theme ? button->theme->height[button->command] : 0;
}
static void button_width_for_height(GtkWidget *widget, gint height, gint *minimum, gint *natural)
{
    (void)height;
    button_width(widget, minimum, natural);
}
static void button_height_for_width(GtkWidget *widget, gint width, gint *minimum, gint *natural)
{
    (void)width;
    button_height(widget, minimum, natural);
}
static void button_finalize(GObject *object)
{
    panel_wm_theme_unref(((PanelWmButton *)object)->theme);
    G_OBJECT_CLASS(panel_wm_button_parent_class)->finalize(object);
}
static void panel_wm_button_class_init(PanelWmButtonClass *klass)
{
    GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
    widget->draw = button_draw; widget->get_preferred_width = button_width; widget->get_preferred_height = button_height;
    widget->get_preferred_width_for_height = button_width_for_height;
    widget->get_preferred_height_for_width = button_height_for_width;
    G_OBJECT_CLASS(klass)->finalize = button_finalize;
}
static void panel_wm_button_init(PanelWmButton *button)
{
    gtk_widget_set_can_focus(GTK_WIDGET(button), TRUE);
}
GtkWidget *panel_wm_button_new(PanelWindowCommand command)
{
    g_return_val_if_fail((guint)command < 3, NULL);
    PanelWmButton *button = g_object_new(panel_wm_button_get_type(), NULL);
    button->command = command;
    return GTK_WIDGET(button);
}
void panel_wm_button_set_theme(GtkWidget *widget, PanelWmTheme *theme)
{
    PanelWmButton *button = (PanelWmButton *)widget;
    panel_wm_theme_ref(theme);
    panel_wm_theme_unref(button->theme);
    button->theme = theme;
    gtk_widget_queue_resize(widget);
    gtk_widget_queue_draw(widget);
}
