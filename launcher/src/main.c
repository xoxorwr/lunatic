/* SPDX-License-Identifier: GPL-3.0-only
 * Xfce host for the Unity launcher C port. Original layout, hide machinery and
 * renderer live in launcher-core.c and renderer.c; this file adapts desktop
 * entries, window management, menus and input to GTK/GIO and X11/EWMH.
 */
#define _POSIX_C_SOURCE 200809L
#include "application-launch.h"
#include "dash.h"
#include "desktop.h"
#include "launcher-core.h"
#include "quicklist.h"
#include "renderer.h"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>
#include <fcntl.h>
#include <gdk/gdkx.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

typedef struct {
    Window xid;
    char *title;
    gboolean active, urgent;
    unsigned long desktop;
} Client;

typedef struct {
    char *id, *name;
    GDesktopAppInfo *app;
    GPtrArray *clients;
    GdkPixbuf *icon;
    GHashTable *render_caches;
    gboolean pinned, active, urgent, window_urgent, seen;
    gboolean has_windows, had_windows;
    GHashTable *urgent_senders;
    GHashTable *window_attention; /* XID -> 1: pending, 2: locally acknowledged. */
    gint64 count;
    double progress;
    gboolean count_visible, progress_visible;
    int kind; /* 0: application, 1: BFB, 2: Trash, 3: Show Desktop */
    LayoutIcon layout;
    Animation visible_animation, active_animation;
    double background[3], glow_color[3];
    gint64 launch_started, urgent_started;
    gboolean urgency_acknowledged;
    double attention_level;
} Item;

typedef struct {
    gboolean down, cancelled;
} SuperTap;

/* Modifier-only activation is decided on release. Observing a chord must never
 * steal its other key or also open Dash when the chord has already run. */
static gboolean super_tap_update(SuperTap *tap, gboolean super_left, gboolean press,
                                 gboolean other_keys_down)
{
    if (super_left && press) {
        if (!tap->down) {
            tap->down = TRUE;
            tap->cancelled = other_keys_down;
        }
    } else if (super_left) {
        gboolean activate = tap->down && !tap->cancelled;
        *tap = (SuperTap){0};
        return activate;
    } else if (tap->down && press)
        tap->cancelled = TRUE;
    return FALSE;
}

/* Each monitor owns a window, raster surface and hide animation. The task
 * model and configuration are shared by all views in this one process. */
typedef struct {
    GtkWidget *window, *items_box, *menu;
    GtkWidget *tooltip_window, *tooltip_label;
    guint tooltip_timer;
    double menu_icon_y;
    Item *menu_item;
    gboolean menu_pointer_left;
    GdkRectangle tooltip_rect;
    int monitor, width, scale, top, height, input_width, rendered_icon_size;
    GdkMonitor *screen;
    gboolean disposing;
    GdkRectangle geometry;
    HideMachine hide;
    Animation hide_animation, hover_animation;
    IconRenderer renderer;
    gboolean hidden, keyboard_mode, shortcuts, pointer_inside;
    Item *hovered, *pressed, *dragged;
    double drag_delta, press_x, press_y;
    int selected;
    unsigned long last_struts[12];
    gboolean struts_set;
    gint64 last_urgency_trace;
} DockView;

typedef struct {
    DockView *view, *keyboard_view;
    GPtrArray *views;
    GtkWidget *search, *settings_window;
    LauncherDash *dash;
    GdkDisplay *gdisplay;
    Display *display;
    Window root, last_active;
    GList *apps;
    LauncherAppIndex *apps_index;
    GPtrArray *items;
    GArray *layout_scratch;
    GHashTable *watched_windows;
    char *config;
    const char *urgency_trace_dir;
    LauncherSettings settings, applied_settings, pending_settings;
    double wallpaper_color[3];
    unsigned long current_desktop;
    gboolean refreshing, menu_open, closing;
    GDBusConnection *bus;
    guint bus_subscription, bus_owner_subscription;
    guint refresh_idle, reload_idle, settings_idle;
    gulong app_monitor_handler;
    GAppInfoMonitor *app_monitor;
    guint animation_timer;
    int mouse_x, mouse_y, image_scale;
    gint64 last_pointer_query, last_frame_time;
    gboolean animation_fast;
    unsigned numlock_mask;
    int xi_opcode;
    KeyCode super_left, lock_keys[3];
    unsigned char raw_keys[32], raw_previous[XIMaskLen(XI_LASTEVENT)];
    gboolean raw_shortcuts;
    SuperTap super_tap;
} Launcher;

static Launcher dock;

/* Transparent animation space never participates in struts or pointer input. */
#define BOUNCE_GUTTER 32

static void queue_render(GtkWidget *widget)
{
    g_object_set_data(G_OBJECT(widget), "frame-dirty", GINT_TO_POINTER(1));
    gtk_widget_queue_draw(widget);
}
static const GtkTargetEntry drop_targets[] = {
    {"application/x-lunatic-launcher-id", GTK_TARGET_SAME_APP, 1}, {"text/uri-list", 0, 2}};

static void refresh(void);
static void queue_all_views(void);
static void position_all(void);
static void reconcile_views(void);
static void show_settings(GtkMenuItem *menu, gpointer data);
static void destroy_view(gpointer data);
static DockView *create_view(int monitor);
static void view_window_destroyed(GtkWidget *widget, gpointer data);
static void settings_changed(const LauncherSettings *previous, gpointer data);
static void refresh_wallpaper_color(void);
static void reload_item_images(void);
static int preferred_image_scale(void);
static void position(void);
static void show_search(void);
static void show_menu(Item *item, GdkEvent *event);
static void trash_clicked(GtkButton *button, gpointer data);
static gboolean canvas_render(GtkWidget *area, cairo_t *cr, gpointer data);
static gboolean canvas_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean canvas_release(GtkWidget *widget, GdkEventButton *event, gpointer data);
static gboolean canvas_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data);
static gboolean canvas_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data);
static gboolean canvas_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data);
static gboolean animation_frame(gpointer data);
static void schedule_refresh(void);
static void wake_animation(void);
static void hide_icon_tooltip(DockView *view);
static gboolean handle_key(XKeyEvent *event);
static void handle_raw_shortcut(XEvent *event);

static void dock_background_color(double color[3])
{
    if (dock.settings.color_mode == COLOR_CUSTOM) {
        color[0] = dock.settings.custom_color.red;
        color[1] = dock.settings.custom_color.green;
        color[2] = dock.settings.custom_color.blue;
    } else
        memcpy(color, dock.wallpaper_color, sizeof(dock.wallpaper_color));
}

static void queue_all_views(void)
{
    if (dock.dash) {
        double color[3];
        dock_background_color(color);
        launcher_dash_set_background(dock.dash, color, dock.settings.opacity);
    }
    if (!dock.views)
        return;
    for (guint i = 0; i < dock.views->len; i++) {
        DockView *view = g_ptr_array_index(dock.views, i);
        if (view->items_box)
            queue_render(view->items_box);
    }
}

typedef struct {
    RenderCache render;
    Animation unfold;
} ItemView;

static ItemView *item_view_state(Item *item)
{
    ItemView *cache = g_hash_table_lookup(item->render_caches, dock.view);
    if (!cache) {
        cache = g_new0(ItemView, 1);
        g_hash_table_insert(item->render_caches, dock.view, cache);
    }
    return cache;
}

static Atom atom(const char *name)
{
    return gdk_x11_get_xatom_by_name_for_display(dock.gdisplay, name);
}

/* Return Xlib-owned data. Trap BadWindow: clients can disappear at any time. */
static unsigned char *property(Window window, const char *name, Atom type, int expected_format,
                               unsigned long *count)
{
    Atom actual;
    int format;
    unsigned long remaining, n = 0;
    unsigned char *data = NULL;
    gdk_x11_display_error_trap_push(dock.gdisplay);
    int status = XGetWindowProperty(dock.display, window, atom(name), 0, 262144, False, type,
                                    &actual, &format, &n, &remaining, &data);
    int error = gdk_x11_display_error_trap_pop(dock.gdisplay);
    if (error || status != Success || actual != type || format != expected_format || remaining) {
        if (data)
            XFree(data);
        data = NULL;
        n = 0;
    }
    if (count)
        *count = n;
    return data;
}

static unsigned long cardinal(Window window, const char *name, unsigned long fallback)
{
    unsigned long n;
    unsigned long *values = (unsigned long *)property(window, name, XA_CARDINAL, 32, &n);
    unsigned long value = values && n ? values[0] : fallback;
    if (values)
        XFree(values);
    return value;
}

static gboolean has_atom(Window window, const char *name, const char *value)
{
    unsigned long n;
    Atom *values = (Atom *)property(window, name, XA_ATOM, 32, &n);
    Atom wanted = atom(value);
    gboolean found = FALSE;
    for (unsigned long i = 0; i < n; i++)
        if (values[i] == wanted)
            found = TRUE;
    if (values)
        XFree(values);
    return found;
}

static char *window_text(Window window, const char *name)
{
    unsigned long n;
    unsigned char *data = property(window, name, atom("UTF8_STRING"), 8, &n);
    char *value = data ? g_utf8_make_valid((char *)data, (gssize)n) : NULL;
    if (data)
        XFree(data);
    return value;
}

static void send_message(Window window, const char *name, long a, long b, long c)
{
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = atom(name);
    event.xclient.format = 32;
    event.xclient.data.l[0] = a;
    event.xclient.data.l[1] = b;
    event.xclient.data.l[2] = c;
    XSendEvent(dock.display, dock.root, False, SubstructureRedirectMask | SubstructureNotifyMask,
               &event);
    XFlush(dock.display);
}

static gboolean window_attention_pending(const Item *item, const Client *client)
{
    return client->urgent &&
           (!item->window_attention ||
            g_hash_table_lookup(item->window_attention, (gpointer)(guintptr)client->xid) !=
                GINT_TO_POINTER(2));
}

static gboolean item_attention_pending(const Item *item)
{
    if (!item->urgent)
        return FALSE;
    gboolean window_urgent = FALSE;
    for (guint i = 0; item->clients && i < item->clients->len; i++) {
        const Client *client = g_ptr_array_index(item->clients, i);
        window_urgent |= client->urgent;
        if (window_attention_pending(item, client))
            return TRUE;
    }
    if (item->urgent_senders && g_hash_table_size(item->urgent_senders))
        return !item->urgency_acknowledged;
    return !window_urgent && !item->urgency_acknowledged;
}

static void track_window_attention(Item *item, const Client *client)
{
    gpointer key = (gpointer)(guintptr)client->xid;
    if (client->urgent) {
        if (!item->window_attention)
            item->window_attention = g_hash_table_new(g_direct_hash, g_direct_equal);
        if (!g_hash_table_contains(item->window_attention, key)) {
            g_hash_table_insert(item->window_attention, key, GINT_TO_POINTER(1));
            item->urgent_started = g_get_monotonic_time() / 1000;
            wake_animation();
        }
    } else if (item->window_attention) {
        g_hash_table_remove(item->window_attention, key);
    }
}

static void acknowledge_window_urgency(Item *item, const Client *client)
{
    if (!item->urgent)
        return;
    if (client->urgent) {
        if (!item->window_attention)
            item->window_attention = g_hash_table_new(g_direct_hash, g_direct_equal);
        g_hash_table_insert(item->window_attention, (gpointer)(guintptr)client->xid,
                            GINT_TO_POINTER(2));
    }
    if (client->urgent || (item->urgent_senders && g_hash_table_size(item->urgent_senders)))
        item->urgency_acknowledged = TRUE;
    if (!item_attention_pending(item))
        item->attention_level = 0;
}

/* Acknowledgements outlive Client snapshots, but never a closed/non-urgent window. */
static void prune_window_attention(Item *item)
{
    if (!item->window_attention)
        return;
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, item->window_attention);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        gboolean found = FALSE;
        for (guint i = 0; i < item->clients->len; i++) {
            const Client *client = g_ptr_array_index(item->clients, i);
            if (client->urgent && client->xid == (Window)(guintptr)key) {
                found = TRUE;
                break;
            }
        }
        if (!found)
            g_hash_table_iter_remove(&iter);
    }
}

static void activate(Window window)
{
    guint32 time = gtk_get_current_event_time();
    unsigned long desktop = cardinal(window, "_NET_WM_DESKTOP", 0xffffffffUL);
    if (desktop != 0xffffffffUL)
        send_message(dock.root, "_NET_CURRENT_DESKTOP", (long)desktop, time, 0);
    send_message(window, "_NET_ACTIVE_WINDOW", 2, time, 0);
    /* Explicit activation acknowledges only the selected requesting window,
     * or an application-wide request from its owning application. */
    for (guint i = 0; dock.items && i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        for (guint j = 0; item->urgent && j < item->clients->len; j++) {
            Client *client = g_ptr_array_index(item->clients, j);
            if (client->xid == window) {
                acknowledge_window_urgency(item, client);
                queue_all_views();
                wake_animation();
            }
        }
    }
}

static void client_free(gpointer data)
{
    Client *client = data;
    g_free(client->title);
    g_free(client);
}

static void acknowledge_active_urgency(Item *item)
{
    if (!item->urgent)
        return;
    for (guint i = 0; i < item->clients->len; i++) {
        const Client *client = g_ptr_array_index(item->clients, i);
        if (client->active)
            acknowledge_window_urgency(item, client);
    }
}

static gboolean update_item_urgency(Item *item)
{
    gboolean urgent = item->window_urgent ||
                      (item->urgent_senders && g_hash_table_size(item->urgent_senders) > 0);
    if (urgent == item->urgent)
        return FALSE;
    if (dock.urgency_trace_dir)
        g_printerr("urgency: %s state=%d active=%d window-urgent=%d publishers=%u\n", item->id,
                   urgent, item->active, item->window_urgent,
                   item->urgent_senders ? g_hash_table_size(item->urgent_senders) : 0);
    item->urgent = urgent;
    item->urgent_started = urgent ? g_get_monotonic_time() / 1000 : 0;
    item->urgency_acknowledged = FALSE;
    item->attention_level = 0;
    queue_all_views();
    wake_animation();
    return TRUE;
}

static void item_free(gpointer data)
{
    Item *item = data;
    if (dock.views)
        for (guint i = 0; i < dock.views->len; i++) {
            DockView *view = g_ptr_array_index(dock.views, i);
            if (view->menu_item == item)
                view->menu_item = NULL;
            if (view->hovered == item) {
                hide_icon_tooltip(view);
                view->hovered = NULL;
            }
            if (view->pressed == item)
                view->pressed = NULL;
            if (view->dragged == item)
                view->dragged = NULL;
            icon_renderer_forget(&view->renderer, item->icon);
            ItemView *cache = g_hash_table_lookup(item->render_caches, view);
            if (cache)
                icon_renderer_cache_clear(&view->renderer, &cache->render);
        }
    g_hash_table_unref(item->render_caches);
    g_clear_pointer(&item->urgent_senders, g_hash_table_unref);
    g_clear_pointer(&item->window_attention, g_hash_table_unref);
    g_clear_object(&item->icon);
    g_clear_object(&item->app);
    g_ptr_array_unref(item->clients);
    g_free(item->id);
    g_free(item->name);
    g_free(item);
}

static Item *find_item(const char *id)
{
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        if (g_str_equal(item->id, id))
            return item;
    }
    return NULL;
}

static void report_error(const char *action, GError *error)
{
    GtkWidget *dialog =
        gtk_message_dialog_new(GTK_WINDOW(dock.view->window), GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", action);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
                                             error ? error->message : "Unknown error");
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show(dialog);
    g_clear_error(&error);
}

static void save_config(void)
{
    GPtrArray *pins = g_ptr_array_new();
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        if (item->pinned && item->app)
            g_ptr_array_add(pins, item->id);
    }
    g_ptr_array_add(pins, NULL);
    GError *error = NULL;
    if (!launcher_save_config(dock.config, (char **)pins->pdata, &dock.settings, &error))
        report_error("Could not save launcher settings", error);
    g_ptr_array_unref(pins);
}

static GdkPixbuf *load_icon(GIcon *icon, const char *fallback)
{
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    GtkIconInfo *info = icon ? gtk_icon_theme_lookup_by_gicon(
                                   theme, icon, dock.settings.icon_size * dock.image_scale,
                                   GTK_ICON_LOOKUP_FORCE_SIZE)
                             : NULL;
    GdkPixbuf *image = info ? gtk_icon_info_load_icon(info, NULL) : NULL;
    if (info)
        g_object_unref(info);
    if (!image)
        image =
            gtk_icon_theme_load_icon(theme, fallback, dock.settings.icon_size * dock.image_scale,
                                     GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    return image;
}

static GdkPixbuf *window_icon(Window window)
{
    unsigned long n;
    unsigned long *pixels = (unsigned long *)property(window, "_NET_WM_ICON", XA_CARDINAL, 32, &n);
    GdkPixbuf *result = NULL;
    unsigned long best = 0, best_size = 0;
    for (unsigned long i = 0; pixels && i + 2 < n;) {
        unsigned long w = pixels[i], h = pixels[i + 1];
        if (!w || !h || w > 2048 || h > 2048 || w * h > n - i - 2)
            break;
        if (!best_size || labs((long)w - dock.settings.icon_size) <
                              labs((long)best_size - dock.settings.icon_size)) {
            best = i;
            best_size = w;
        }
        i += 2 + w * h;
    }
    if (best_size) {
        int w = (int)pixels[best], h = (int)pixels[best + 1];
        GdkPixbuf *raw = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, w, h);
        if (raw) {
            guchar *out = gdk_pixbuf_get_pixels(raw);
            int stride = gdk_pixbuf_get_rowstride(raw);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    unsigned long argb = pixels[best + 2 + (unsigned long)y * w + x];
                    guchar *p = out + y * stride + x * 4;
                    p[0] = (argb >> 16) & 255;
                    p[1] = (argb >> 8) & 255;
                    p[2] = argb & 255;
                    p[3] = (argb >> 24) & 255;
                }
            result = gdk_pixbuf_scale_simple(raw, dock.settings.icon_size * dock.image_scale,
                                             dock.settings.icon_size * dock.image_scale,
                                             GDK_INTERP_BILINEAR);
            g_object_unref(raw);
        }
    }
    if (pixels)
        XFree(pixels);
    return result;
}

static void launch(Item *item, const char *action)
{
    if (!item->app)
        return;
    item->launch_started = g_get_monotonic_time() / 1000;
    GdkAppLaunchContext *context = gdk_display_get_app_launch_context(dock.gdisplay);
    gdk_app_launch_context_set_timestamp(context, gtk_get_current_event_time());
    GError *error = NULL;
    if (!launcher_launch_application(item->app, action, G_APP_LAUNCH_CONTEXT(context),
                                     gtk_get_current_event_time(), &error))
        report_error("Could not launch application", error);
    g_object_unref(context);
}

static void item_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Item *item = data;
    if (item->kind == 1) {
        show_search();
        return;
    }
    if (item->kind == 3) {
        send_message(dock.root, "_NET_SHOWING_DESKTOP",
                     !cardinal(dock.root, "_NET_SHOWING_DESKTOP", 0), 0, 0);
        return;
    }
    if (item->kind == 2) {
        trash_clicked(NULL, NULL);
        return;
    }
    GdkModifierType modifiers = 0;
    gtk_get_current_event_state(&modifiers);
    if ((modifiers & GDK_SHIFT_MASK) || !item->clients->len) {
        if (!item->launch_started)
            launch(item, NULL);
        return;
    }
    /* WindowedLauncherIcon::ActivateLauncherIcon: focus an inactive group,
     * spread the active group; minimizing is disabled by default in Unity.
     */
    if (item->active && item->clients->len > 1) {
        show_menu(item, NULL);
        return;
    }
    Client *client = g_ptr_array_index(item->clients, item->clients->len - 1);
    if (client->active && dock.settings.minimize_on_click) {
        gdk_x11_display_error_trap_push(dock.gdisplay);
        XIconifyWindow(dock.display, client->xid, DefaultScreen(dock.display));
        gdk_x11_display_error_trap_pop_ignored(dock.gdisplay);
    } else {
        for (guint i = 0; i < item->clients->len; i++) {
            Client *candidate = g_ptr_array_index(item->clients, i);
            if (candidate->desktop == cardinal(dock.root, "_NET_CURRENT_DESKTOP", 0))
                client = candidate;
        }
        activate(client->xid);
    }
}

static gboolean item_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    (void)widget;
    Item *item = data;
    if (!item->clients->len)
        return FALSE;
    int current = -1;
    for (guint i = 0; i < item->clients->len; i++)
        if (((Client *)g_ptr_array_index(item->clients, i))->active)
            current = (int)i;
    int step = event->direction == GDK_SCROLL_UP || event->delta_y < 0 ? -1 : 1;
    int next = (current + step + (int)item->clients->len) % (int)item->clients->len;
    activate(((Client *)g_ptr_array_index(item->clients, next))->xid);
    return TRUE;
}

static void drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection,
                          guint info, guint time, gpointer data)
{
    if (data)
        dock.view = data;
    (void)widget;
    (void)context;
    (void)info;
    (void)time;
    Item *item = dock.view->dragged;
    (void)data;
    if (!item)
        return;
    gtk_selection_data_set(selection, gtk_selection_data_get_target(selection), 8,
                           (const guchar *)item->id, (gint)strlen(item->id));
}

static void arrange_items(void)
{
    queue_all_views();
}

static guint application_end(void)
{
    /* Keep application insertion and drag reordering ahead of the bottom group. */
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        if (item->kind == 3 || item->kind == 2)
            return i;
    }
    return dock.items->len;
}

static void move_item(Item *item, Item *before)
{
    if (item == before || item->kind || (before && before->kind))
        return;
    guint from = 0;
    if (!g_ptr_array_find(dock.items, item, &from))
        return;
    /* steal so the free function does not destroy a moving item */
    g_ptr_array_steal_index(dock.items, from);
    guint to = application_end();
    if (before)
        g_ptr_array_find(dock.items, before, &to);
    g_ptr_array_insert(dock.items, (gint)to, item);
    arrange_items();
}

static Item *add_item(const char *id, GDesktopAppInfo *app, const char *name);

static void drop_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
                          GtkSelectionData *selection, guint info, guint time, gpointer data)
{
    if (data)
        dock.view = data;
    (void)widget;
    (void)x;
    (void)y;
    Item *before = NULL;
    (void)data;
    for (guint j = 0; j < dock.items->len; j++) {
        Item *candidate = g_ptr_array_index(dock.items, j);
        if (!candidate->kind && y < candidate->layout.center) {
            before = candidate;
            break;
        }
    }
    gboolean success = FALSE;
    if (info == 1 && gtk_selection_data_get_length(selection) > 0) {
        char *id = g_strndup((const char *)gtk_selection_data_get_data(selection),
                             (gsize)gtk_selection_data_get_length(selection));
        Item *item = find_item(id);
        if (item && item->app) {
            item->pinned = TRUE;
            move_item(item, before);
            success = TRUE;
        }
        g_free(id);
    } else if (info == 2) {
        char **uris = gtk_selection_data_get_uris(selection);
        for (guint i = 0; uris && uris[i]; i++) {
            char *path = g_filename_from_uri(uris[i], NULL, NULL);
            GDesktopAppInfo *app = path && g_str_has_suffix(path, ".desktop")
                                       ? g_desktop_app_info_new_from_filename(path)
                                       : NULL;
            if (app) {
                /* Keep absolute paths for desktop files outside XDG app directories. */
                const char *id = g_app_info_get_id(G_APP_INFO(app));
                GDesktopAppInfo *installed = id ? g_desktop_app_info_new(id) : NULL;
                const char *key =
                    installed && g_strcmp0(g_desktop_app_info_get_filename(installed), path) == 0
                        ? id
                        : path;
                Item *item = add_item(key, app, NULL);
                item->pinned = TRUE;
                move_item(item, before);
                success = TRUE;
                g_clear_object(&installed);
                g_object_unref(app);
            }
            g_free(path);
        }
        g_strfreev(uris);
    }
    if (success) {
        save_config();
        refresh();
    }
    gtk_drag_finish(context, success, FALSE, time);
}

static Item *add_item(const char *id, GDesktopAppInfo *app, const char *name)
{
    if (!id)
        id = app ? g_desktop_app_info_get_filename(app) : "unknown";
    Item *existing = find_item(id);
    if (existing)
        return existing;
    Item *item = g_new0(Item, 1);
    item->id = g_strdup(id);
    item->kind = g_str_equal(id, "lunatic://home")      ? 1
                 : g_str_equal(id, "lunatic://trash")   ? 2
                 : g_str_equal(id, "lunatic://desktop") ? 3
                                                      : 0;
    item->app = app ? g_object_ref(app) : NULL;
    item->name = g_strdup(app ? g_app_info_get_display_name(G_APP_INFO(app)) : name ? name : id);
    item->clients = g_ptr_array_new_with_free_func(client_free);
    if (app)
        item->icon = load_icon(g_app_info_get_icon(G_APP_INFO(app)), "application-x-executable");
    item->render_caches = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    item->visible_animation.target = 1;
    item->visible_animation.start = g_get_monotonic_time() / 1000;
    item->visible_animation.duration = 200;
    for (int k = 0; k < 3; k++)
        item->background[k] = item->glow_color[k] = 1;
    if (item->icon)
        launcher_icon_colors(item->icon, item->background, item->glow_color);
    guint position = item->kind == 2 ? dock.items->len : application_end();
    g_ptr_array_insert(dock.items, (gint)position, item);
    return item;
}

static void menu_activate(GtkMenuItem *menu, gpointer data)
{
    (void)menu;
    activate((Window)(guintptr)data);
}

static void menu_launch(GtkMenuItem *menu, gpointer data)
{
    launch(data, g_object_get_data(G_OBJECT(menu), "action"));
}

static void menu_pin(GtkMenuItem *menu, gpointer data)
{
    (void)menu;
    Item *item = data;
    item->pinned = !item->pinned;
    save_config();
}

static void menu_close(GtkMenuItem *menu, gpointer data)
{
    (void)menu;
    Item *item = data;
    for (guint i = 0; i < item->clients->len; i++)
        send_message(((Client *)g_ptr_array_index(item->clients, i))->xid, "_NET_CLOSE_WINDOW",
                     gtk_get_current_event_time(), 2, 0);
}

static void quit_launcher(void)
{
    if (gtk_main_level())
        gtk_main_quit();
}

static gboolean quicklist_row_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    (void)data;
    gboolean active = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "active-window"));
    gboolean hovered = gtk_widget_get_state_flags(widget) & GTK_STATE_FLAG_PRELIGHT;
    quicklist_paint_row(cr, gtk_widget_get_allocated_width(widget),
                        gtk_widget_get_allocated_height(widget), active, hovered);
    return FALSE;
}

static gboolean quicklist_frame_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    DockView *view = data;
    GdkWindow *menu_window = gtk_widget_get_window(widget);
    /* GtkMenu also draws its scrolling bin/view windows. Painting the frame
     * into those windows repeats it with their padding/scroll translation. */
    if (!gtk_cairo_should_draw_window(cr, menu_window))
        return FALSE;
    GdkWindow *canvas_window = gtk_widget_get_window(view->items_box);
    if (!canvas_window)
        return FALSE;
    int ignored, icon_root_y, menu_root_y;
    double icon_y = view->menu_item ? view->menu_item->layout.center : view->menu_icon_y;
    gdk_window_get_root_coords(canvas_window, 0, (int)round(icon_y), &ignored, &icon_root_y);
    gdk_window_get_root_coords(menu_window, 0, 0, &ignored, &menu_root_y);
    double arrow_y =
        quicklist_arrow_y(icon_root_y, menu_root_y, gtk_widget_get_allocated_height(widget));
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
    quicklist_paint_frame(cr, gtk_widget_get_allocated_width(widget),
                          gtk_widget_get_allocated_height(widget), view->menu_pointer_left,
                          arrow_y);
    return FALSE;
}

static void quicklist_remove_style(GtkWidget *menu, gpointer data)
{
    (void)data;
    GtkCssProvider *css = g_object_get_data(G_OBJECT(menu), "quicklist-css");
    if (css)
        gtk_style_context_remove_provider_for_screen(gtk_widget_get_screen(menu),
                                                     GTK_STYLE_PROVIDER(css));
}

static const char quicklist_css[] =
    "#lunatic-quicklist-window { background: transparent; border: 0; box-shadow: none; }"
    "#lunatic-quicklist { background: transparent; border: 0; box-shadow: none; "
    "padding: 6px 14px; color: #f7f5f8; }"
    "#lunatic-quicklist.pointer-left { padding-left: 18px; padding-right: 6px; }"
    "#lunatic-quicklist.pointer-right { padding-left: 6px; padding-right: 18px; }"
    "#lunatic-quicklist menuitem { background: transparent; background-image: none; "
    "border: 0; box-shadow: none; padding: 3px 8px; min-height: 0; color: #f7f5f8; }"
    "#lunatic-quicklist menuitem label { color: inherit; text-shadow: none; }"
    "#lunatic-quicklist menuitem:disabled { color: #bdb6c2; opacity: 1; }"
    "#lunatic-quicklist .quicklist-domain { font-size: 10px; font-weight: bold; "
    "color: #aaa1b0; padding-top: 1px; padding-bottom: 1px; min-height: 0; }"
    "#lunatic-quicklist .quicklist-app { padding: 4px 8px; }"
    "#lunatic-quicklist .quicklist-app-name { font-weight: bold; color: "
    "#ffffff; }"
    "#lunatic-quicklist .quicklist-app-detail { font-size: 11px; color: #bdb6c2; }"
    "#lunatic-quicklist .quicklist-active label { font-weight: bold; color: #ffffff; }"
    "#lunatic-quicklist menuitem.quicklist-active label.quicklist-active-tag { font-size: 10px; "
    "color: #ffd0a0; }"
    "#lunatic-quicklist button.quicklist-close { padding: 0; margin: 0; border: 0; "
    "min-width: 18px; min-height: 18px; border-radius: 3px; box-shadow: none; "
    "background: transparent; background-image: none; opacity: 0; color: inherit; }"
    "#lunatic-quicklist menuitem.quicklist-window:hover button.quicklist-close { opacity: 1; }"
    "#lunatic-quicklist button.quicklist-close:hover { background: rgba(255,255,255,0.20); }"
    "#lunatic-quicklist button.quicklist-close:active { background: rgba(255,255,255,0.32); }"
    "#lunatic-quicklist separator { background: rgba(255,255,255,0.13); min-height: 1px; "
    "margin: 3px 8px; border: 0; }";

static void quicklist_style(GtkWidget *menu, DockView *view)
{
    gtk_widget_set_name(menu, "lunatic-quicklist");
    GtkWidget *popup = gtk_widget_get_toplevel(menu);
    if (GTK_IS_WINDOW(popup))
        gtk_widget_set_name(popup, "lunatic-quicklist-window");
    gtk_style_context_add_class(gtk_widget_get_style_context(menu),
                                view->menu_pointer_left ? "pointer-left" : "pointer-right");
    gtk_menu_set_reserve_toggle_size(GTK_MENU(menu), FALSE);
    g_object_set(menu, "anchor-hints", GDK_ANCHOR_SLIDE_Y | GDK_ANCHOR_RESIZE_Y, NULL);
    GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(menu));
    if (visual) {
        gtk_widget_set_visual(menu, visual);
        GtkWidget *toplevel = gtk_widget_get_toplevel(menu);
        if (GTK_IS_WINDOW(toplevel) && !gtk_widget_get_realized(toplevel))
            gtk_widget_set_visual(toplevel, visual);
    }
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, quicklist_css, -1, NULL);
    /* A screen provider is scoped by the unique menu name, so descendants get
     * the same styling without replacing keyboard, focus or grab behavior. */
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(menu), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set_data_full(G_OBJECT(menu), "quicklist-css", css, g_object_unref);
    g_signal_connect(menu, "destroy", G_CALLBACK(quicklist_remove_style), NULL);
    g_signal_connect(menu, "draw", G_CALLBACK(quicklist_frame_draw), view);
}

static void quicklist_section(GtkWidget *menu, const char *title)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    if (children)
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    g_list_free(children);
    if (title) {
        GtkWidget *entry = gtk_menu_item_new_with_label(title);
        gtk_style_context_add_class(gtk_widget_get_style_context(entry), "quicklist-domain");
        gtk_widget_set_sensitive(entry, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
    }
}

static void quicklist_app_header(GtkWidget *menu, const Item *item)
{
    quicklist_section(menu, NULL);
    GtkWidget *entry = gtk_menu_item_new();
    gtk_widget_set_sensitive(entry, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(entry), "quicklist-app");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *image = NULL;
    GIcon *icon = item->app ? g_app_info_get_icon(G_APP_INFO(item->app)) : NULL;
    if (icon)
        image = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_MENU);
    else if (item->icon) {
        GdkPixbuf *small = gdk_pixbuf_scale_simple(item->icon, 16, 16, GDK_INTERP_BILINEAR);
        image = gtk_image_new_from_pixbuf(small);
        g_object_unref(small);
    }
    if (image)
        gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *name = gtk_label_new(item->name);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(name), 26);
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "quicklist-app-name");
    gtk_box_pack_start(GTK_BOX(text), name, TRUE, TRUE, 0);
    char *detail =
        g_strdup_printf("%u open window%s", item->clients->len, item->clients->len == 1 ? "" : "s");
    GtkWidget *subtitle = gtk_label_new(detail);
    g_free(detail);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "quicklist-app-detail");
    gtk_box_pack_end(GTK_BOX(text), subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(entry), box);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
}

static GtkWidget *menu_entry(GtkWidget *menu, const char *label, GCallback callback, gpointer data)
{
    GtkWidget *entry = gtk_menu_item_new_with_label(label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
    g_signal_connect(entry, "activate", callback, data);
    g_signal_connect(entry, "draw", G_CALLBACK(quicklist_row_draw), NULL);
    return entry;
}

static void quicklist_close_window(GtkButton *button, gpointer data)
{
    Window window = (Window)(guintptr)data;
    GtkWidget *menu = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_MENU);
    send_message(window, "_NET_CLOSE_WINDOW", gtk_get_current_event_time(), 2, 0);
    if (menu)
        gtk_menu_shell_cancel(GTK_MENU_SHELL(menu));
}

/* Menus grab pointer events themselves. Route the close hotspot before the
 * menu shell's release handler can activate (and focus) the window row. */
static GtkWidget *quicklist_close_at(GtkWidget *menu, GdkEventButton *event)
{
    int root_x, root_y;
    gdk_window_get_origin(gtk_widget_get_window(menu), &root_x, &root_y);
    double x = event->x_root - root_x, y = event->y_root - root_y;
    GList *rows = gtk_container_get_children(GTK_CONTAINER(menu));
    GtkWidget *hit = NULL;
    for (GList *row = rows; row; row = row->next) {
        GtkWidget *button = g_object_get_data(G_OBJECT(row->data), "window-close");
        int bx, by;
        if (!button || !gtk_widget_translate_coordinates(button, menu, 0, 0, &bx, &by))
            continue;
        if (x >= bx && x < bx + gtk_widget_get_allocated_width(button) && y >= by &&
            y < by + gtk_widget_get_allocated_height(button)) {
            hit = button;
            break;
        }
    }
    g_list_free(rows);
    return hit;
}

static gboolean quicklist_close_gesture(gpointer *pressed, gpointer target, GdkEventType type,
                                        guint button, gboolean *close)
{
    *close = FALSE;
    if (button != 1)
        return FALSE;
    if (type == GDK_BUTTON_PRESS) {
        *pressed = target;
        return target != NULL;
    }
    if (type == GDK_BUTTON_RELEASE && *pressed) {
        *close = *pressed == target;
        *pressed = NULL;
        return TRUE;
    }
    return FALSE;
}

static gboolean quicklist_close_event(GtkWidget *menu, GdkEventButton *event, gpointer data)
{
    (void)data;
    gpointer pressed = g_object_get_data(G_OBJECT(menu), "close-pressed");
    GtkWidget *target = quicklist_close_at(menu, event);
    gboolean close;
    gboolean handled =
        quicklist_close_gesture(&pressed, target, event->type, event->button, &close);
    g_object_set_data(G_OBJECT(menu), "close-pressed", pressed);
    if (close)
        gtk_button_clicked(GTK_BUTTON(target));
    return handled;
}

static GtkWidget *window_menu_entry(GtkWidget *menu, const Item *item, const Client *client,
                                    const char *fallback)
{
    GtkWidget *entry =
        menu_entry(menu, "", G_CALLBACK(menu_activate), (gpointer)(guintptr)client->xid);
    gtk_container_remove(GTK_CONTAINER(entry), gtk_bin_get_child(GTK_BIN(entry)));
    if (client->active) {
        g_object_set_data(G_OBJECT(entry), "active-window", GINT_TO_POINTER(1));
        gtk_style_context_add_class(gtk_widget_get_style_context(entry), "quicklist-active");
    }
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(entry), "quicklist-window");
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_style_context_add_class(gtk_widget_get_style_context(close), "quicklist-close");
    gtk_widget_set_can_focus(close, FALSE);
    gtk_widget_set_focus_on_click(close, FALSE);
    const char *title = client->title ? client->title : fallback;
    char *close_label = g_strdup_printf("Close %s", title);
    atk_object_set_name(gtk_widget_get_accessible(close), close_label);
    g_free(close_label);
    gtk_widget_set_tooltip_text(close, "Close window");
    g_object_set_data(G_OBJECT(entry), "window-close", close);
    g_signal_connect(close, "clicked", G_CALLBACK(quicklist_close_window),
                     (gpointer)(guintptr)client->xid);
    gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
    GtkWidget *image = gtk_image_new();
    gtk_widget_set_size_request(image, 16, -1);
    if (window_attention_pending(item, client)) {
        gtk_image_set_from_icon_name(GTK_IMAGE(image), "dialog-warning-symbolic",
                                     GTK_ICON_SIZE_MENU);
        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css, "image { color: #ffb326; }", -1, NULL);
        gtk_style_context_add_provider(gtk_widget_get_style_context(image), GTK_STYLE_PROVIDER(css),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        gtk_widget_set_tooltip_text(entry, "This window requests attention");
        atk_object_set_description(gtk_widget_get_accessible(entry),
                                   "This window requests attention");
    }
    atk_object_set_name(gtk_widget_get_accessible(entry), title);
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 32);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
    if (client->active) {
        GtkWidget *tag = gtk_label_new("Active");
        gtk_style_context_add_class(gtk_widget_get_style_context(tag), "quicklist-active-tag");
        gtk_box_pack_end(GTK_BOX(box), tag, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(entry), box);
    return entry;
}

static void menu_done(GtkWidget *menu, gpointer data)
{
    if (data)
        dock.view = data;
    (void)data;
    dock.menu_open = FALSE;
    dock.view->menu = NULL;
    dock.view->menu_item = NULL;
    /* Destroy after the activate signal has run; its handlers use Item pointers. */
    gtk_widget_destroy(menu);
    schedule_refresh();
}

static GtkWidget *build_quicklist(Item *item, DockView *view)
{
    GtkWidget *menu = gtk_menu_new();
    quicklist_style(menu, view);
    g_signal_connect(menu, "button-press-event", G_CALLBACK(quicklist_close_event), NULL);
    g_signal_connect(menu, "button-release-event", G_CALLBACK(quicklist_close_event), NULL);
    if (item) {
        if (item->app || item->clients->len) {
            quicklist_section(menu, "ACTIONS");
            if (item->app) {
                menu_entry(menu, "New window", G_CALLBACK(menu_launch), item);
                const char *const *actions = g_desktop_app_info_list_actions(item->app);
                for (guint i = 0; actions && actions[i]; i++) {
                    char *name = g_desktop_app_info_get_action_name(item->app, actions[i]);
                    GtkWidget *entry = menu_entry(menu, name, G_CALLBACK(menu_launch), item);
                    g_object_set_data_full(G_OBJECT(entry), "action", g_strdup(actions[i]), g_free);
                    g_free(name);
                }
            }
            if (item->clients->len)
                menu_entry(menu, "Close all windows", G_CALLBACK(menu_close), item);
        }
        quicklist_app_header(menu, item);
        if (!item->urgency_acknowledged && item->urgent_senders &&
            g_hash_table_size(item->urgent_senders)) {
            GtkWidget *notice = gtk_menu_item_new_with_label("Application requests attention");
            gtk_widget_set_sensitive(notice, FALSE);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), notice);
        }
        if (item->clients->len) {
            quicklist_section(menu, "WINDOWS");
            for (guint i = 0; i < item->clients->len; i++)
                window_menu_entry(menu, item, g_ptr_array_index(item->clients, i), item->name);
        }
    }
    quicklist_section(menu, "LAUNCHER");
    if (item && item->app)
        menu_entry(menu, item->pinned ? "Unpin from launcher" : "Pin to launcher",
                   G_CALLBACK(menu_pin), item);
    menu_entry(menu, "Settings…", G_CALLBACK(show_settings), view);
    menu_entry(menu, "Quit launcher", G_CALLBACK(quit_launcher), NULL);
    return menu;
}

static double quicklist_anchor_y(Item *item, GdkEvent *event, int canvas_root_y, double fallback)
{
    double root_x, root_y;
    if (item)
        return item->layout.center;
    if (event && gdk_event_get_root_coords(event, &root_x, &root_y))
        return root_y - canvas_root_y;
    return fallback;
}

static void show_menu(Item *item, GdkEvent *event)
{
    hide_icon_tooltip(dock.view);
    if (dock.view->menu)
        gtk_menu_popdown(GTK_MENU(dock.view->menu));
    dock.view->menu_pointer_left = dock.settings.position != DOCK_RIGHT;
    dock.view->menu_item = item;
    GdkWindow *canvas_window = gtk_widget_get_window(dock.view->items_box);
    int canvas_root_x, canvas_root_y;
    gdk_window_get_origin(canvas_window, &canvas_root_x, &canvas_root_y);
    dock.view->menu_icon_y = quicklist_anchor_y(
        item, event, canvas_root_y, gtk_widget_get_allocated_height(dock.view->items_box) / 2.0);
    GtkWidget *menu = build_quicklist(item, dock.view);
    dock.view->menu = menu;
    dock.menu_open = TRUE;
    g_signal_connect(menu, "selection-done", G_CALLBACK(menu_done), dock.view);
    gtk_widget_show_all(menu);
    GdkRectangle anchor = {dock.settings.position == DOCK_RIGHT ? BOUNCE_GUTTER
                                                                : dock.view->width - 1,
                           (int)round(dock.view->menu_icon_y), 1, 1};
    gtk_menu_popup_at_rect(
        GTK_MENU(menu), canvas_window, &anchor,
        dock.settings.position == DOCK_RIGHT ? GDK_GRAVITY_WEST : GDK_GRAVITY_EAST,
        dock.settings.position == DOCK_RIGHT ? GDK_GRAVITY_EAST : GDK_GRAVITY_WEST, event);
}

static void begin_window_refresh(Item *item)
{
    item->had_windows = item->has_windows;
    item->has_windows = FALSE;
    g_ptr_array_set_size(item->clients, 0);
    item->active = item->window_urgent = item->seen = FALSE;
}

static void clear_closed_app_overlays(Item *item)
{
    if (item->had_windows && !item->has_windows) {
        item->count = 0;
        item->count_visible = FALSE;
        item->progress = 0;
        item->progress_visible = FALSE;
    }
}

static void refresh(void)
{
    if (dock.refreshing || dock.menu_open)
        return;
    dock.refreshing = TRUE;
    dock.current_desktop = cardinal(dock.root, "_NET_CURRENT_DESKTOP", 0);
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        begin_window_refresh(item);
    }
    unsigned long active_count, count;
    Window *active_data =
        (Window *)property(dock.root, "_NET_ACTIVE_WINDOW", XA_WINDOW, 32, &active_count);
    Window active = active_data && active_count ? active_data[0] : None;
    gboolean focus_changed = active != dock.last_active;
    dock.last_active = active;
    if (active_data)
        XFree(active_data);
    Window *windows = (Window *)property(dock.root, "_NET_CLIENT_LIST", XA_WINDOW, 32, &count);
    for (unsigned long i = 0; i < count; i++) {
        Window window = windows[i];
        if (gdk_x11_window_lookup_for_display(dock.gdisplay, window))
            continue;
        if (has_atom(window, "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR") ||
            has_atom(window, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK") ||
            has_atom(window, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP"))
            continue;
        if (!g_hash_table_contains(dock.watched_windows, (gpointer)(guintptr)window)) {
            gdk_x11_display_error_trap_push(dock.gdisplay);
            XWindowAttributes attributes;
            if (XGetWindowAttributes(dock.display, window, &attributes)) {
                XSelectInput(dock.display, window,
                             attributes.your_event_mask | PropertyChangeMask | StructureNotifyMask);
                g_hash_table_add(dock.watched_windows, (gpointer)(guintptr)window);
            }
            gdk_x11_display_error_trap_pop_ignored(dock.gdisplay);
        }
        unsigned long desktop = cardinal(window, "_NET_WM_DESKTOP", 0xffffffffUL);
        gboolean visible =
            launcher_settings_window_visible(&dock.settings, desktop, dock.current_desktop);
        XClassHint hint = {0};
        gdk_x11_display_error_trap_push(dock.gdisplay);
        XGetClassHint(dock.display, window, &hint);
        gdk_x11_display_error_trap_pop_ignored(dock.gdisplay);
        char *app_id = window_text(window, "_GTK_APPLICATION_ID");
        GDesktopAppInfo *app =
            launcher_app_index_find(dock.apps_index, hint.res_name, hint.res_class, app_id);
        /* A pinned custom desktop file takes precedence over installed entries. */
        int best = app ? launcher_match_score(app, hint.res_name, hint.res_class, app_id) : 0;
        Item *matched = NULL;
        for (guint j = 0; j < dock.items->len; j++) {
            Item *candidate = g_ptr_array_index(dock.items, j);
            int score = candidate->app ? launcher_match_score(candidate->app, hint.res_name,
                                                              hint.res_class, app_id)
                                       : 0;
            if (candidate->pinned && score > 0 && score >= best) {
                matched = candidate;
                best = score;
            }
        }
        char *id = app ? g_strdup(g_app_info_get_id(G_APP_INFO(app))) : NULL;
        if (!id) {
            char *normalized = launcher_normalize(hint.res_class ? hint.res_class : hint.res_name);
            id = *normalized ? g_strconcat("window:", normalized, NULL)
                             : g_strdup_printf("window:%lu", window);
            g_free(normalized);
        }
        Item *item = matched ? matched : find_item(id);
        if (!item && visible)
            item = add_item(id, app, hint.res_class);
        if (item)
            item->has_windows = TRUE;
        /* Track application lifetime across all workspaces, while retaining
         * the configured workspace filter for the visible task list. */
        if (!visible || !item)
            goto window_done;
        item->seen = TRUE;
        Client *client = g_new0(Client, 1);
        client->xid = window;
        client->active = window == active;
        client->desktop = desktop;
        client->title = window_text(window, "_NET_WM_NAME");
        client->urgent = has_atom(window, "_NET_WM_STATE", "_NET_WM_STATE_DEMANDS_ATTENTION");
        gdk_x11_display_error_trap_push(dock.gdisplay);
        XWMHints *wmhints = XGetWMHints(dock.display, window);
        if (wmhints) {
            client->urgent |= !!(wmhints->flags & XUrgencyHint);
            XFree(wmhints);
        }
        gdk_x11_display_error_trap_pop_ignored(dock.gdisplay);
        track_window_attention(item, client);
        item->active |= client->active;
        item->window_urgent |= client->urgent;
        g_ptr_array_add(item->clients, client);
        if (!item->icon) {
            item->icon = window_icon(window);
            if (!item->icon)
                item->icon = load_icon(NULL, "application-x-executable");
            if (item->icon)
                launcher_icon_colors(item->icon, item->background, item->glow_color);
        }
    window_done:
        g_free(id);
        g_free(app_id);
        if (hint.res_class)
            XFree(hint.res_class);
        if (hint.res_name)
            XFree(hint.res_name);
    }
    if (windows)
        XFree(windows);
    for (guint i = dock.items->len; i > 0; i--) {
        Item *item = g_ptr_array_index(dock.items, i - 1);
        if (!item->kind && !item->pinned && !item->seen)
            g_ptr_array_remove_index(dock.items, i - 1);
        else {
            gint64 now = g_get_monotonic_time() / 1000;
            animation_to(&item->active_animation, item->active ? 1 : 0, 200, now);
            clear_closed_app_overlays(item);
            prune_window_attention(item);
            update_item_urgency(item);
            if (focus_changed)
                acknowledge_active_urgency(item);
            if (item->clients->len)
                item->launch_started = 0;
            queue_all_views();
        }
    }
    dock.refreshing = FALSE;
}

static gboolean refresh_idle(gpointer data)
{
    (void)data;
    dock.refresh_idle = 0;
    refresh();
    return G_SOURCE_REMOVE;
}

static void schedule_refresh(void)
{
    if (!dock.refresh_idle)
        dock.refresh_idle = g_idle_add(refresh_idle, NULL);
}

static GdkFilterReturn window_event_filter(GdkXEvent *raw, GdkEvent *event, gpointer data)
{
    (void)event;
    (void)data;
    XEvent *xevent = raw;
    handle_raw_shortcut(xevent);
    if (xevent->type == KeyPress && handle_key(&xevent->xkey))
        return GDK_FILTER_REMOVE;
    if (xevent->type == DestroyNotify) {
        if (g_hash_table_remove(dock.watched_windows,
                                (gpointer)(guintptr)xevent->xdestroywindow.window))
            schedule_refresh();
    } else if (xevent->type == PropertyNotify) {
        Window window = xevent->xproperty.window;
        if (window == dock.root) {
            Atom changed = xevent->xproperty.atom;
            if (changed == atom("_NET_CLIENT_LIST") || changed == atom("_NET_ACTIVE_WINDOW") ||
                changed == atom("_NET_CURRENT_DESKTOP"))
                schedule_refresh();
            else if (changed == atom("_XROOTPMAP_ID") || changed == atom("ESETROOT_PMAP_ID") ||
                     changed == atom("_XSETROOT_ID"))
                refresh_wallpaper_color();
            else if (changed == atom("_NET_WORKAREA"))
                position_all();
        } else if (g_hash_table_contains(dock.watched_windows, (gpointer)(guintptr)window)) {
            /* Ignore activity counters, icon geometry and other unrelated changes;
             * otherwise typing in a client could rebuild the complete task list. */
            static const char *const watched[] = {
                "_NET_WM_STATE",      "_NET_WM_DESKTOP", "_NET_WM_NAME",        "WM_NAME",
                "WM_CLASS",           "WM_HINTS",        "_GTK_APPLICATION_ID", "_NET_WM_ICON",
                "_NET_WM_WINDOW_TYPE"};
            for (guint i = 0; i < G_N_ELEMENTS(watched); i++)
                if (xevent->xproperty.atom == atom(watched[i])) {
                    schedule_refresh();
                    break;
                }
        }
    }
    return GDK_FILTER_CONTINUE;
}

static void position(void)
{
    GdkMonitor *monitor = dock.view->screen;
    if (!monitor)
        return;
    gdk_monitor_get_geometry(monitor, &dock.view->geometry);
    GdkRectangle work;
    gdk_monitor_get_workarea(monitor, &work);
    int top = MAX(dock.view->geometry.y, work.y);
    dock.view->top = top;
    int height =
        MAX(1, MIN(dock.view->geometry.y + dock.view->geometry.height, work.y + work.height) - top);
    dock.view->height = height;
    gboolean right = dock.settings.position == DOCK_RIGHT;
    int x = dock.view->geometry.x +
            (right ? dock.view->geometry.width - dock.view->width - BOUNCE_GUTTER : 0);
    /* A non-resizable GtkWindow derives its fixed geometry hints from its
     * child's requisition. Keep that requisition equal to the full work area;
     * gtk_window_resize alone cannot override a 100-pixel fixed-size hint. */
    gtk_widget_set_size_request(dock.view->items_box, dock.view->width + BOUNCE_GUTTER, height);
    gtk_window_set_default_size(GTK_WINDOW(dock.view->window), dock.view->width + BOUNCE_GUTTER,
                                height);
    gtk_window_move(GTK_WINDOW(dock.view->window), x, top);
    gtk_window_resize(GTK_WINDOW(dock.view->window), dock.view->width + BOUNCE_GUTTER, height);
    GdkWindow *gwindow = gtk_widget_get_window(dock.view->window);
    if (!gwindow)
        return;
    int visible = dock.settings.hide_animation == HIDE_FADE && dock.view->hide_animation.value < 1
                      ? dock.view->width
                      : MAX(1, (int)ceil(dock.view->width * (1 - dock.view->hide_animation.value)));
    GdkRectangle input = {right ? BOUNCE_GUTTER + dock.view->width - visible : 0, 0, visible,
                          height};
    cairo_region_t *region = cairo_region_create_rectangle(&input);
    gdk_window_input_shape_combine_region(gwindow, region, 0, 0);
    cairo_region_destroy(region);
    dock.view->input_width = input.width;
    /* EWMH struts are physical root coordinates, not GTK logical pixels. */
    int scale = gdk_window_get_scale_factor(gwindow);
    unsigned long struts[12] = {0};
    /* EWMH cannot express an interior monitor edge. */
    int root_width = 0;
    for (int i = 0; i < gdk_display_get_n_monitors(dock.gdisplay); i++) {
        GdkRectangle rect;
        gdk_monitor_get_geometry(gdk_display_get_monitor(dock.gdisplay, i), &rect);
        root_width = MAX(root_width, rect.x + rect.width);
    }
    if (!dock.settings.autohide && (!right && dock.view->geometry.x == 0)) {
        struts[0] = (unsigned long)dock.view->width * scale;
        struts[4] = (unsigned long)MAX(0, top) * scale;
        struts[5] = (unsigned long)MAX(0, top + height) * scale - 1;
    } else if (!dock.settings.autohide && right &&
               dock.view->geometry.x + dock.view->geometry.width == root_width) {
        struts[1] = (unsigned long)dock.view->width * scale;
        struts[6] = (unsigned long)MAX(0, top) * scale;
        struts[7] = (unsigned long)MAX(0, top + height) * scale - 1;
    }
    Window window = gdk_x11_window_get_xid(gwindow);
    if (!dock.view->struts_set || memcmp(struts, dock.view->last_struts, sizeof(struts))) {
        XChangeProperty(dock.display, window, atom("_NET_WM_STRUT_PARTIAL"), XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)struts, 12);
        XChangeProperty(dock.display, window, atom("_NET_WM_STRUT"), XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)struts, 4);
        memcpy(dock.view->last_struts, struts, sizeof(struts));
        dock.view->struts_set = TRUE;
        XFlush(dock.display);
    }
}

static void dash_launch(GDesktopAppInfo *app, gpointer data)
{
    (void)data;
    Item temporary = {.app = app};
    launch(&temporary, NULL);
}

static void dash_pin(GDesktopAppInfo *app, gpointer data)
{
    (void)data;
    Item *item = add_item(g_app_info_get_id(G_APP_INFO(app)), app, NULL);
    item->pinned = TRUE;
    save_config();
    queue_all_views();
}

static gboolean dash_pinned(GDesktopAppInfo *app, gpointer data)
{
    (void)data;
    const char *id = g_app_info_get_id(G_APP_INFO(app));
    Item *item = id ? find_item(id) : NULL;
    return item && item->pinned;
}

static void show_search(void)
{
    hide_icon_tooltip(dock.view);
    if (!dock.dash) {
        dock.dash = launcher_dash_new(dash_launch, dash_pin, dash_pinned, NULL);
        launcher_dash_set_apps(dock.dash, dock.apps);
        dock.search = launcher_dash_window(dock.dash);
    }
    gboolean right = dock.settings.position == DOCK_RIGHT;
    GdkRectangle bounds = {dock.view->geometry.x + (right ? 0 : dock.view->width), dock.view->top,
                           MAX(1, dock.view->geometry.width - dock.view->width), dock.view->height};
    double color[3];
    dock_background_color(color);
    launcher_dash_set_background(dock.dash, color, dock.settings.opacity);
    launcher_dash_toggle(dock.dash, GTK_WINDOW(dock.view->window), &bounds, right);
    wake_animation();
}

static void trash_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    GError *error = NULL;
    GAppInfo *app = g_app_info_get_default_for_uri_scheme("trash");
    if (!app)
        app = g_app_info_get_default_for_type("inode/directory", FALSE);
    if (!app || !G_IS_DESKTOP_APP_INFO(app)) {
        g_clear_object(&app);
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                            "No desktop application handles Trash");
        report_error("Could not open Trash", error);
        return;
    }
    GdkAppLaunchContext *context = gdk_display_get_app_launch_context(dock.gdisplay);
    gdk_app_launch_context_set_timestamp(context, gtk_get_current_event_time());
    GList uri = {.data = "trash:///"};
    if (!launcher_launch_uris(G_DESKTOP_APP_INFO(app), &uri, G_APP_LAUNCH_CONTEXT(context), &error))
        report_error("Could not open Trash", error);
    g_object_unref(context);
    g_object_unref(app);
}

static void launcher_entry_update(GDBusConnection *connection, const char *sender, const char *path,
                         const char *interface, const char *signal, GVariant *parameters,
                         gpointer data)
{
    (void)connection;
    (void)path;
    (void)interface;
    (void)signal;
    (void)data;
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sa{sv})")))
        return;
    const char *uri;
    GVariant *properties;
    g_variant_get(parameters, "(&s@a{sv})", &uri, &properties);
    const char *id = g_str_has_prefix(uri, "application://") ? uri + 14 : uri;
    Item *item = find_item(id);
    if (!item) {
        for (guint i = 0; i < dock.items->len; i++) {
            Item *candidate = g_ptr_array_index(dock.items, i);
            if (candidate->app &&
                g_strcmp0(g_app_info_get_id(G_APP_INFO(candidate->app)), id) == 0) {
                item = candidate;
                break;
            }
        }
    }
    if (dock.urgency_trace_dir)
        g_printerr("urgency: Update uri=%s matched=%s sender=%s\n", uri, item ? item->id : "NONE",
                   sender ? sender : "NONE");
    if (item) {
        g_variant_lookup(properties, "count", "x", &item->count);
        g_variant_lookup(properties, "count-visible", "b", &item->count_visible);
        g_variant_lookup(properties, "progress", "d", &item->progress);
        g_variant_lookup(properties, "progress-visible", "b", &item->progress_visible);
        gboolean urgent;
        if (sender && *sender && g_variant_lookup(properties, "urgent", "b", &urgent)) {
            if (urgent) {
                if (!item->urgent_senders)
                    item->urgent_senders =
                        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
                if (!g_hash_table_contains(item->urgent_senders, sender)) {
                    g_hash_table_add(item->urgent_senders, g_strdup(sender));
                    item->urgency_acknowledged = FALSE;
                    item->urgent_started = g_get_monotonic_time() / 1000;
                    wake_animation();
                }
            } else if (item->urgent_senders) {
                g_hash_table_remove(item->urgent_senders, sender);
                if (!g_hash_table_size(item->urgent_senders))
                    g_clear_pointer(&item->urgent_senders, g_hash_table_unref);
            }
            update_item_urgency(item);
        }
        queue_all_views();
    }
    g_variant_unref(properties);
}

static void launcher_entry_owner_changed(GDBusConnection *connection, const char *sender, const char *path,
                                const char *interface, const char *signal, GVariant *parameters,
                                gpointer data)
{
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface;
    (void)signal;
    (void)data;
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sss)")))
        return;
    const char *name, *old_owner, *new_owner;
    g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
    /* Track unique connections, not optional well-known names. A publisher's
     * exit must not leave a pinned icon permanently asking for attention. */
    if (name[0] != ':' || !*old_owner || *new_owner)
        return;
    gboolean changed = FALSE;
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        /* Badge/progress updates may come from a one-shot gdbus sender.
         * Their lifetime follows the app's windows, not the emitting process. */
        if (item->urgent_senders && g_hash_table_remove(item->urgent_senders, name)) {
            if (!g_hash_table_size(item->urgent_senders))
                g_clear_pointer(&item->urgent_senders, g_hash_table_unref);
            changed |= update_item_urgency(item);
        }
    }
    if (changed)
        queue_all_views();
}

static gint app_compare(gconstpointer a, gconstpointer b)
{
    return g_utf8_collate(g_app_info_get_display_name((GAppInfo *)a),
                          g_app_info_get_display_name((GAppInfo *)b));
}

static gboolean reload_apps(gpointer data)
{
    (void)data;
    dock.reload_idle = 0;
    launcher_app_index_free(dock.apps_index);
    g_list_free_full(dock.apps, g_object_unref);
    dock.apps = g_list_sort(g_app_info_get_all(), app_compare);
    dock.apps_index = launcher_app_index_new(dock.apps);
    if (dock.dash)
        launcher_dash_set_apps(dock.dash, dock.apps);
    schedule_refresh();
    return G_SOURCE_REMOVE;
}

static void apps_changed(GAppInfoMonitor *monitor, gpointer data)
{
    (void)monitor;
    (void)data;
    if (!dock.reload_idle)
        dock.reload_idle = g_idle_add(reload_apps, NULL);
}

static void monitors_changed(GdkDisplay *display, GdkMonitor *monitor, gpointer data)
{
    launcher_dash_hide(dock.dash);
    (void)display;
    (void)monitor;
    (void)data;
    reconcile_views();
}

static void screen_monitors_changed(GdkScreen *screen, gpointer data)
{
    (void)screen;
    (void)data;
    reconcile_views();
    refresh_wallpaper_color();
}

static gboolean terminate_signal(gpointer data)
{
    (void)data;
    gtk_main_quit();
    return G_SOURCE_CONTINUE;
}

static void realized(GtkWidget *widget, gpointer data)
{
    if (data)
        dock.view = data;
    (void)data;
    GdkWindow *window = gtk_widget_get_window(widget);
    gdk_x11_window_set_user_time(window, 0);
    position();
}

static void layout_items(int height)
{
    guint count = dock.items->len;
    g_array_set_size(dock.layout_scratch, count);
    LayoutIcon *icons = (LayoutIcon *)dock.layout_scratch->data;
    memset(icons, 0, count * sizeof(*icons));
    for (guint i = 0; i < count; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        icons[i].visible = item->visible_animation.value;
        icons[i].active = item->active_animation.value;
        icons[i].unfolded = item_view_state(item)->unfold.value +
                            (dock.view->keyboard_mode && (int)i == dock.view->selected ? 1 : 0);
        icons[i].shelf = item->kind == 2 || item->kind == 3;
    }
    launcher_layout(icons, count, dock.settings.icon_size + 6, height,
                    dock.view->hover_animation.value, dock.view->drag_delta);
    for (guint i = 0; i < count; i++)
        ((Item *)g_ptr_array_index(dock.items, i))->layout = icons[i];
}

static void update_layout(void)
{
    layout_items(gtk_widget_get_allocated_height(dock.view->items_box));
}

static gboolean update_renderer_scale(GtkWidget *area, GError **error)
{
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    if (scale == dock.view->scale && dock.view->rendered_icon_size == dock.settings.icon_size)
        return TRUE;
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        ItemView *cache = g_hash_table_lookup(item->render_caches, dock.view);
        if (cache)
            icon_renderer_cache_clear(&dock.view->renderer, &cache->render);
    }
    icon_renderer_unrealize(&dock.view->renderer);
    icon_renderer_clear(&dock.view->renderer);
    dock.view->scale = scale;
    dock.view->rendered_icon_size = dock.settings.icon_size;
    return icon_renderer_init(&dock.view->renderer, dock.settings.icon_size + 6,
                              dock.settings.icon_size, scale, error);
}

static void canvas_realize(GtkWidget *area, gpointer data)
{
    dock.view = data;
    GError *error = NULL;
    if (!update_renderer_scale(area, &error)) {
        g_warning("Cannot initialize renderer: %s", error->message);
        g_clear_error(&error);
    }
}

static void canvas_scale_changed(GtkWidget *area, GParamSpec *spec, gpointer data)
{
    dock.view = data;
    (void)spec;
    if (!gtk_widget_get_realized(area))
        return;
    GError *error = NULL;
    if (!update_renderer_scale(area, &error)) {
        g_warning("Cannot resize renderer: %s", error->message);
        g_clear_error(&error);
    }
    queue_render(area);
}

static void canvas_unrealize(GtkWidget *area, gpointer data)
{
    dock.view = data;
    (void)area;
    icon_renderer_unrealize(&dock.view->renderer);
}

static Item *hit_item_from_layout(double y)
{
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        if (item->layout.visible > 0 && y >= item->layout.hit_start && y < item->layout.hit_end)
            return item;
    }
    return NULL;
}

static Item *hit_item(double y)
{
    update_layout();
    return hit_item_from_layout(y);
}

static Item *pointer_item(int root_x, int root_y)
{
    int left =
        dock.view->geometry.x +
        (dock.settings.position == DOCK_RIGHT ? dock.view->geometry.width - dock.view->width : 0);
    if (!dock.view->pointer_inside || dock.view->hidden || dock.view->hide_animation.value > .01 ||
        dock.view->menu || (dock.view->hide.quirks & INTERNAL_DND_ACTIVE) || root_x < left ||
        root_x >= left + dock.view->width)
        return NULL;
    return hit_item_from_layout(root_y - dock.view->top);
}

/* Tooltip coordinates are monitor/root coordinates; icon rows are canvas-local.
 * Anchor beside the icon center, never below the pointer or the whole canvas. */
static GdkRectangle icon_tooltip_rect(const DockView *view, const Item *item, int width, int height)
{
    int edge =
        view->geometry.x +
        (dock.settings.position == DOCK_RIGHT ? view->geometry.width - view->width : view->width);
    int bounce = item_attention_pending(item) && !view->menu
                     ? (int)ceil(item->attention_level * MIN(24, dock.settings.icon_size * .48) *
                                 (1 - view->hide_animation.value))
                     : 0;
    int x = dock.settings.position == DOCK_RIGHT ? edge - width - 8 - bounce : edge + 8 + bounce;
    int y = view->top + (int)round(item->layout.center) - height / 2;
    return (GdkRectangle){
        CLAMP(x, view->geometry.x,
              MAX(view->geometry.x, view->geometry.x + view->geometry.width - width)),
        CLAMP(y, view->geometry.y,
              MAX(view->geometry.y, view->geometry.y + view->geometry.height - height)),
        width, height};
}

static void hide_icon_tooltip(DockView *view)
{
    if (view->tooltip_timer) {
        g_source_remove(view->tooltip_timer);
        view->tooltip_timer = 0;
    }
    if (view->tooltip_window)
        gtk_widget_hide(view->tooltip_window);
}

static void position_icon_tooltip(DockView *view)
{
    if (!view->tooltip_window || !view->hovered)
        return;
    GtkRequisition size;
    gtk_widget_get_preferred_size(view->tooltip_window, NULL, &size);
    GdkRectangle rect = icon_tooltip_rect(view, view->hovered, size.width, size.height);
    if (gdk_rectangle_equal(&rect, &view->tooltip_rect))
        return;
    view->tooltip_rect = rect;
    gtk_window_resize(GTK_WINDOW(view->tooltip_window), rect.width, rect.height);
    gtk_window_move(GTK_WINDOW(view->tooltip_window), rect.x, rect.y);
}

static gboolean show_icon_tooltip(gpointer data)
{
    DockView *view = data;
    view->tooltip_timer = 0;
    if (view->disposing || !view->hovered || view->hidden || view->menu ||
        (view->hide.quirks & INTERNAL_DND_ACTIVE) || !view->tooltip_window)
        return G_SOURCE_REMOVE;
    gtk_label_set_text(GTK_LABEL(view->tooltip_label), view->hovered->name);
    position_icon_tooltip(view);
    gtk_widget_show(view->tooltip_window);
    return G_SOURCE_REMOVE;
}

static gboolean set_hovered_item(Item *item, gint64 now)
{
    DockView *view = dock.view;
    if (item == view->hovered)
        return FALSE;
    gboolean visible = view->tooltip_window && gtk_widget_get_visible(view->tooltip_window);
    hide_icon_tooltip(view);
    view->hovered = item;
    for (guint i = 0; i < dock.items->len; i++) {
        Item *candidate = g_ptr_array_index(dock.items, i);
        animation_to(&item_view_state(candidate)->unfold, candidate == item ? 1 : 0, 200, now);
    }
    if (item && view->tooltip_window) {
        if (visible)
            show_icon_tooltip(view);
        else
            view->tooltip_timer = g_timeout_add(500, show_icon_tooltip, view);
    }
    return TRUE;
}

static void tooltip_realized(GtkWidget *widget, gpointer data)
{
    (void)data;
    /* A tooltip must never intercept motion or clicks intended for the dock
     * or for the application beside it. */
    cairo_region_t *empty = cairo_region_create();
    gdk_window_input_shape_combine_region(gtk_widget_get_window(widget), empty, 0, 0);
    cairo_region_destroy(empty);
}

static RenderArg item_render_arg(Item *item, guint i, gint64 now)
{
    RenderArg arg = {0};
    arg.image = item->icon;
    memcpy(arg.background, item->background, sizeof(arg.background));
    memcpy(arg.glow, item->glow_color, sizeof(arg.glow));
    arg.alpha = item->visible_animation.value;
    arg.active = item->active;
    arg.urgent = item_attention_pending(item);
    arg.running = item->clients->len;
    for (guint j = 0; j < item->clients->len; j++) {
        Client *client = g_ptr_array_index(item->clients, j);
        if (client->desktop == dock.current_desktop || client->desktop == 0xffffffffUL)
            arg.running_here = TRUE;
    }
    arg.system = item->kind == 2 || item->kind == 3;
    arg.backlight = item->clients->len || item->kind ? .9 : 0;
    if (item->kind == 1)
        arg.backlight = 0; /* BFB provides its own background */
    if (item->launch_started) {
        double progress = CLAMP((now - item->launch_started) / 10000.0, 0, 1);
        arg.backlight += (.9 - arg.backlight) * (1 - launcher_starting_pulse(progress));
    }
    if (arg.urgent && !dock.view->menu)
        arg.bounce = item->attention_level * MIN(24, dock.settings.icon_size * .48) *
                     (1 - dock.view->hide_animation.value);
    arg.selected = dock.view->keyboard_mode && (gint)i == dock.view->selected;
    arg.count = item->count;
    arg.count_visible = item->count_visible;
    arg.progress_visible = item->progress_visible;
    arg.progress = item->progress;
    if (dock.view->shortcuts && i > 0 && i <= 10)
        arg.shortcut = i == 10 ? '0' : (char)('0' + i);
    return arg;
}

/* Opt-in diagnostics capture the actual host frame and GTK clip, rather than
 * a synthetic renderer scene. Disabled in ordinary sessions. */
static void trace_urgency_frame(GtkWidget *area, cairo_t *cr)
{
    if (!dock.urgency_trace_dir)
        return;
    gint64 now = g_get_monotonic_time() / 1000;
    if (now - dock.view->last_urgency_trace < 1000)
        return;
    dock.view->last_urgency_trace = now;
    double x1, y1, x2, y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    g_printerr("urgency: monitor=%d allocation=%dx%d renderer=%dx%d scale=%d "
               "mapped=%d hidden=%d hide=%.3f clip=%.0f,%.0f..%.0f,%.0f\n",
               dock.view->monitor, gtk_widget_get_allocated_width(area),
               gtk_widget_get_allocated_height(area), dock.view->width, dock.view->height,
               dock.view->scale, gtk_widget_get_mapped(area), dock.view->hidden,
               dock.view->hide_animation.value, x1, y1, x2, y2);
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        if (item->urgent)
            g_printerr("urgency: drawn=%s y=%.1f alpha=%.3f level=%.3f "
                       "acknowledged=%d hovered=%d\n",
                       item->id, item->layout.center, item->visible_animation.value,
                       item->attention_level, item->urgency_acknowledged,
                       dock.view->hovered == item);
    }
    cairo_surface_t *frame = icon_renderer_surface(&dock.view->renderer);
    if (frame) {
        char *name = g_strdup_printf("urgency-monitor-%d.png", dock.view->monitor);
        char *path = g_build_filename(dock.urgency_trace_dir, name, NULL);
        cairo_status_t status = cairo_surface_write_to_png(frame, path);
        if (status != CAIRO_STATUS_SUCCESS)
            g_printerr("urgency: cannot save %s: %s\n", path, cairo_status_to_string(status));
        g_free(path);
        g_free(name);
    }
}

static gboolean canvas_render(GtkWidget *area, cairo_t *cr, gpointer data)
{
    if (data)
        dock.view = data;
    (void)data;
    GtkWidget *widget = area;
    int height = gtk_widget_get_allocated_height(widget);
    cairo_surface_t *frame = icon_renderer_surface(&dock.view->renderer);
    if (frame && !g_object_get_data(G_OBJECT(widget), "frame-dirty") &&
        cairo_image_surface_get_width(frame) ==
            (dock.view->width + BOUNCE_GUTTER) * dock.view->scale &&
        cairo_image_surface_get_height(frame) == height * dock.view->scale) {
        trace_urgency_frame(area, cr);
        icon_renderer_paint(&dock.view->renderer, cr);
        return TRUE;
    }
    update_layout();
    RenderStyle style = {.dock_width = dock.view->width,
                         .opacity = dock.settings.opacity,
                         .right = dock.settings.position == DOCK_RIGHT,
                         .hide_animation = dock.settings.hide_animation};
    dock_background_color(style.color);
    icon_renderer_set_style(&dock.view->renderer, &style);
    icon_renderer_begin(&dock.view->renderer, dock.view->width + BOUNCE_GUTTER, height,
                        dock.view->hide_animation.value);
    gint64 now = g_get_monotonic_time() / 1000;
    gboolean shelf_started = FALSE;
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        if (!shelf_started && (item->kind == 2 || item->kind == 3)) {
            icon_renderer_shelf(&dock.view->renderer, MAX(0, item->layout.hit_start - 5));
            shelf_started = TRUE;
        }
        RenderArg arg = item_render_arg(item, i, now);
        icon_renderer_draw(&dock.view->renderer, &arg, &item_view_state(item)->render,
                           &item->layout, (dock.view->width - 1) / 2.0, height, dock.view->width);
    }
    /* These quads stay at the screen edge while the dock slides/fades away.
     * They share the dock window and its input shape, so they cannot rise above
     * fullscreen clients or capture clicks outside the normal reveal edge. */
    if (dock.view->hide_animation.value > 0) {
        for (guint i = 0; i < dock.items->len; i++) {
            Item *item = g_ptr_array_index(dock.items, i);
            if (item_attention_pending(item)) {
                const double color[3] = {1, .65, .15};
                icon_renderer_beacon(&dock.view->renderer, item->layout.center, color,
                                     (.25 + .65 * item->attention_level) *
                                         dock.view->hide_animation.value);
            }
        }
    }
    icon_renderer_end(&dock.view->renderer);
    g_object_set_data(G_OBJECT(widget), "frame-dirty", NULL);
    trace_urgency_frame(area, cr);
    icon_renderer_paint(&dock.view->renderer, cr);
    return TRUE;
}

static gboolean canvas_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (data)
        dock.view = data;
    (void)widget;
    (void)data;
    Item *item = hit_item(event->y);
    hide_icon_tooltip(dock.view);
    dock.view->press_x = event->x;
    dock.view->press_y = event->y;
    dock.view->pressed = item;
    dock.view->dragged = event->button == 1 && item && !item->kind ? item : NULL;
    if (event->button == 3) {
        show_menu(item, (GdkEvent *)event);
        dock.view->pressed = NULL;
        return TRUE;
    }
    if (event->button == 2 && item) {
        launch(item, NULL);
        dock.view->pressed = NULL;
        return TRUE;
    }
    return FALSE; /* GTK's drag threshold handling must also see the press. */
}

static gboolean canvas_release(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (data)
        dock.view = data;
    (void)widget;
    (void)data;
    Item *item = hit_item(event->y);
    if (event->button == 1 && item && item == dock.view->pressed &&
        fabs(event->x - dock.view->press_x) < 8 && fabs(event->y - dock.view->press_y) < 8)
        item_clicked(NULL, item);
    dock.view->pressed = NULL;
    if (!(dock.view->hide.quirks & INTERNAL_DND_ACTIVE))
        dock.view->dragged = NULL;
    return FALSE;
}

static gboolean canvas_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
    if (data)
        dock.view = data;
    (void)data;
    (void)widget;
    dock.view->pointer_inside = TRUE;
    update_layout();
    dock.mouse_x = dock.view->geometry.x +
                   (dock.settings.position == DOCK_RIGHT
                        ? dock.view->geometry.width - dock.view->width - BOUNCE_GUTTER
                        : 0) +
                   (int)event->x;
    dock.mouse_y = dock.view->top + (int)event->y;
    dock.last_pointer_query = g_get_monotonic_time() / 1000;
    set_hovered_item(pointer_item(dock.mouse_x, dock.mouse_y), g_get_monotonic_time() / 1000);
    wake_animation();
    return FALSE;
}

static gboolean canvas_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
    if (data)
        dock.view = data;
    (void)widget;
    (void)event;
    (void)data;
    dock.view->pointer_inside = FALSE;
    set_hovered_item(NULL, g_get_monotonic_time() / 1000);
    hide_icon_tooltip(dock.view);
    dock.last_pointer_query = 0;
    wake_animation();
    return FALSE;
}

static gboolean canvas_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data)
{
    if (data)
        dock.view = data;
    (void)data;
    Item *item = hit_item(event->y);
    if (item && !item->kind && item->clients->len && item->active)
        return item_scroll(widget, event, item);
    double amount = event->direction == GDK_SCROLL_UP || event->delta_y < 0 ? 25 : -25;
    dock.view->drag_delta += amount;
    double sum = 5;
    for (guint i = 0; i < dock.items->len; i++) {
        Item *candidate = g_ptr_array_index(dock.items, i);
        sum += (dock.settings.icon_size + 11) * candidate->visible_animation.value;
    }
    if (application_end() < dock.items->len)
        sum += 5; /* Gap reserved between scrolling apps and the system shelf. */
    dock.view->drag_delta =
        CLAMP(dock.view->drag_delta, MIN(0, gtk_widget_get_allocated_height(widget) - sum), 0);
    queue_render(widget);
    return TRUE;
}

static void drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
    if (data)
        dock.view = data;
    (void)data;
    dock.view->pressed = NULL;
    hide_icon_tooltip(dock.view);
    hide_machine_quirk(&dock.view->hide, INTERNAL_DND_ACTIVE, TRUE, g_get_monotonic_time() / 1000);
    if (dock.view->dragged && dock.view->dragged->icon)
        gtk_drag_set_icon_pixbuf(context, dock.view->dragged->icon, 0, 0);
    else
        gtk_drag_cancel(context);
    queue_render(widget);
}

static void drag_end(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
    if (data)
        dock.view = data;
    (void)widget;
    (void)context;
    (void)data;
    dock.view->dragged = NULL;
    hide_machine_quirk(&dock.view->hide, INTERNAL_DND_ACTIVE, FALSE, g_get_monotonic_time() / 1000);
}

static void wake_animation(void)
{
    if (dock.animation_fast)
        return;
    if (dock.animation_timer)
        g_source_remove(dock.animation_timer);
    dock.animation_fast = TRUE;
    dock.animation_timer = g_timeout_add(16, animation_frame, NULL);
}

static void end_key_navigation(void)
{
    if (dock.keyboard_view)
        dock.view = dock.keyboard_view;
    dock.keyboard_view = NULL;
    if (!dock.view)
        return;
    if (dock.view->keyboard_mode)
        XUngrabKeyboard(dock.display, CurrentTime);
    dock.view->keyboard_mode = FALSE;
    dock.view->shortcuts = FALSE;
    hide_machine_quirk(&dock.view->hide, KEY_NAV_ACTIVE, FALSE, g_get_monotonic_time() / 1000);
    queue_all_views();
    wake_animation();
}

static void shortcut_view_at_pointer(void)
{
    GdkSeat *seat = gdk_display_get_default_seat(dock.gdisplay);
    GdkDevice *pointer = gdk_seat_get_pointer(seat);
    int x = dock.mouse_x, y = dock.mouse_y;
    gdk_device_get_position(pointer, NULL, &x, &y);
    GdkMonitor *monitor = gdk_display_get_monitor_at_point(dock.gdisplay, x, y);
    for (guint i = 0; i < dock.views->len; i++) {
        DockView *view = g_ptr_array_index(dock.views, i);
        if (view->screen == monitor) {
            dock.view = view;
            break;
        }
    }
}

static void handle_raw_shortcut(XEvent *event)
{
    if (!dock.raw_shortcuts || event->type != GenericEvent ||
        event->xcookie.extension != dock.xi_opcode)
        return;
    XGenericEventCookie *cookie = &event->xcookie;
    if (cookie->evtype != XI_RawKeyPress && cookie->evtype != XI_RawKeyRelease &&
        cookie->evtype != XI_RawButtonPress)
        return;
    /* GDK may already have obtained the cookie. Only free data we acquired. */
    gboolean acquired = FALSE;
    if (!cookie->data) {
        if (!XGetEventData(dock.display, cookie))
            return;
        acquired = TRUE;
    }
    if (cookie->evtype == XI_RawButtonPress) {
        super_tap_update(&dock.super_tap, FALSE, TRUE, FALSE);
    } else if (cookie->evtype == XI_RawKeyPress || cookie->evtype == XI_RawKeyRelease) {
        XIRawEvent *key = cookie->data;
        gboolean super = key->detail == dock.super_left;
        gboolean press = cookie->evtype == XI_RawKeyPress;
        gboolean held = FALSE;
        if (super && press && !dock.super_tap.down) {
            for (int code = 8; code < 256; code++) {
                if (code == dock.super_left || code == dock.lock_keys[0] ||
                    code == dock.lock_keys[1] || code == dock.lock_keys[2])
                    continue;
                if (dock.raw_keys[code / 8] & (1u << (code % 8))) {
                    held = TRUE;
                    break;
                }
            }
        }
        if (key->detail >= 0 && key->detail < 256) {
            unsigned char bit = (unsigned char)(1u << (key->detail % 8));
            if (press)
                dock.raw_keys[key->detail / 8] |= bit;
            else
                dock.raw_keys[key->detail / 8] &= (unsigned char)~bit;
        }
        if (super_tap_update(&dock.super_tap, super, press, held) && dock.views->len) {
            shortcut_view_at_pointer();
            show_search();
        }
    }
    if (acquired)
        XFreeEventData(dock.display, cookie);
}

static void configure_super_shortcut(gboolean enable)
{
    if (!enable) {
        if (dock.raw_shortcuts) {
            XIEventMask mask = {XIAllMasterDevices, sizeof(dock.raw_previous), dock.raw_previous};
            XISelectEvents(dock.display, dock.root, &mask, 1);
        }
        dock.raw_shortcuts = FALSE;
        dock.super_tap = (SuperTap){0};
        return;
    }
    int event, error;
    GdkDevice *keyboard = gdk_seat_get_keyboard(gdk_display_get_default_seat(dock.gdisplay));
    /* GDK already negotiated XI2 on this connection. Renegotiating a lower
     * version here could change GDK's event semantics, especially during grabs. */
    if (!keyboard || !GDK_IS_X11_DEVICE_XI2(keyboard) ||
        !XQueryExtension(dock.display, "XInputExtension", &dock.xi_opcode, &event, &error)) {
        g_printerr("Super_L shortcut needs XInput2.\n");
        return;
    }
    dock.super_left = XKeysymToKeycode(dock.display, XK_Super_L);
    dock.lock_keys[0] = XKeysymToKeycode(dock.display, XK_Caps_Lock);
    dock.lock_keys[1] = XKeysymToKeycode(dock.display, XK_Num_Lock);
    dock.lock_keys[2] = XKeysymToKeycode(dock.display, XK_Scroll_Lock);
    if (!dock.super_left)
        return;
    XQueryKeymap(dock.display, (char *)dock.raw_keys);
    memset(dock.raw_previous, 0, sizeof(dock.raw_previous));
    int selected_count = 0;
    XIEventMask *selected = XIGetSelectedEvents(dock.display, dock.root, &selected_count);
    for (int i = 0; i < selected_count; i++)
        if (selected[i].deviceid == XIAllMasterDevices)
            memcpy(dock.raw_previous, selected[i].mask,
                   MIN((gsize)selected[i].mask_len, sizeof(dock.raw_previous)));
    if (selected)
        XFree(selected);
    unsigned char events[XIMaskLen(XI_LASTEVENT)];
    memcpy(events, dock.raw_previous, sizeof(events));
    XISetMask(events, XI_RawKeyPress);
    XISetMask(events, XI_RawKeyRelease);
    XISetMask(events, XI_RawButtonPress);
    XIEventMask mask = {XIAllMasterDevices, sizeof(events), events};
    /* Raw input observes the modifier globally without grabbing the keyboard:
     * WM shortcuts, Super+digits and other clients' chords stay deliverable. */
    gdk_x11_display_error_trap_push(dock.gdisplay);
    XISelectEvents(dock.display, dock.root, &mask, 1);
    dock.raw_shortcuts = gdk_x11_display_error_trap_pop(dock.gdisplay) == 0;
    if (!dock.raw_shortcuts)
        g_printerr("Cannot register the Super_L shortcut.\n");
}

static gboolean handle_key(XKeyEvent *event)
{
    if (dock.keyboard_view)
        dock.view = dock.keyboard_view;
    else if (event->state & Mod4Mask)
        shortcut_view_at_pointer();
    if (!dock.view)
        return FALSE;
    KeySym key = XLookupKeysym(event, 0);
    if (dock.view->keyboard_mode) {
        if (key == XK_Escape)
            end_key_navigation();
        else if (key == XK_Up || key == XK_Down) {
            int count = (int)dock.items->len;
            dock.view->selected = (dock.view->selected + (key == XK_Down ? 1 : count - 1)) % count;
            /* Ensure an unfolded selected item can be reached on a short screen. */
            double extent = gtk_widget_get_allocated_height(dock.view->items_box);
            double center = 5 + (dock.view->selected + .5) * (dock.settings.icon_size + 11);
            dock.view->drag_delta = MIN(0, extent / 2 - center);
            queue_all_views();
        } else if (key == XK_Return || key == XK_KP_Enter || key == XK_space) {
            Item *item =
                g_ptr_array_index(dock.items, MIN(dock.view->selected, (int)dock.items->len - 1));
            end_key_navigation();
            item_clicked(NULL, item);
        } else if (key == XK_Menu) {
            Item *item =
                g_ptr_array_index(dock.items, MIN(dock.view->selected, (int)dock.items->len - 1));
            end_key_navigation();
            show_menu(item, NULL);
        }
        return TRUE;
    }
    if (!(event->state & Mod4Mask))
        return FALSE;
    if (key == XK_F1) {
        if (XGrabKeyboard(dock.display, dock.root, False, GrabModeAsync, GrabModeAsync,
                          event->time) == GrabSuccess) {
            dock.view->keyboard_mode = dock.view->shortcuts = TRUE;
            dock.keyboard_view = dock.view;
            dock.view->selected = 0;
            wake_animation();
        }
        return TRUE;
    }
    int index = key >= XK_1 && key <= XK_9 ? (int)(key - XK_0) : key == XK_0 ? 10 : -1;
    if (index > 0 && index < (int)dock.items->len) {
        Item *item = g_ptr_array_index(dock.items, index);
        if (!item->kind) {
            if (event->state & ShiftMask)
                launch(item, NULL);
            else
                item_clicked(NULL, item);
        }
        return TRUE;
    }
    return FALSE;
}

static void configure_shortcuts(gboolean enable)
{
    configure_super_shortcut(enable);
    if (enable) {
        XModifierKeymap *map = XGetModifierMapping(dock.display);
        KeyCode numlock = XKeysymToKeycode(dock.display, XK_Num_Lock);
        for (int modifier = 0; modifier < 8; modifier++)
            for (int key = 0; key < map->max_keypermod; key++)
                if (map->modifiermap[modifier * map->max_keypermod + key] == numlock)
                    dock.numlock_mask |= 1u << modifier;
        XFreeModifiermap(map);
    }
    unsigned locks[] = {0, LockMask, dock.numlock_mask, LockMask | dock.numlock_mask};
    for (int key = 0; key < 11; key++) {
        KeySym symbol = key < 10 ? XK_0 + key : XK_F1;
        KeyCode code = XKeysymToKeycode(dock.display, symbol);
        if (!code)
            continue;
        for (unsigned shift = 0; shift <= (key < 10 ? 1u : 0u); shift++) {
            gdk_x11_display_error_trap_push(dock.gdisplay);
            for (guint lock = 0; lock < G_N_ELEMENTS(locks); lock++) {
                unsigned modifiers = Mod4Mask | (shift ? ShiftMask : 0) | locks[lock];
                if (enable)
                    XGrabKey(dock.display, code, modifiers, dock.root, False, GrabModeAsync,
                             GrabModeAsync);
                else
                    XUngrabKey(dock.display, code, modifiers, dock.root);
            }
            if (gdk_x11_display_error_trap_pop(dock.gdisplay) && enable)
                g_printerr("Shortcut Super+%s%s is already in use.\n", shift ? "Shift+" : "",
                           XKeysymToString(symbol));
        }
    }
}

static gboolean animation_step(gint64 now, gboolean shared_redraw)
{
    GdkSeat *seat = gdk_display_get_default_seat(dock.gdisplay);
    GdkDevice *pointer = gdk_seat_get_pointer(seat);
    int x, y;
    x = dock.mouse_x;
    y = dock.mouse_y;
    if (now - dock.last_pointer_query >= 100) {
        gdk_device_get_position(pointer, NULL, &x, &y);
        dock.last_pointer_query = now;
    }
    gboolean on_monitor = y >= dock.view->top && y < dock.view->top + dock.view->height;
    gboolean right = dock.settings.position == DOCK_RIGHT;
    int edge = dock.view->geometry.x + (right ? dock.view->geometry.width : 0);
    gboolean mouse_over = on_monitor && x >= (right ? edge - dock.view->width : edge) &&
                          x < (right ? edge : edge + dock.view->width);
    dock.view->hide.reveal_pressure =
        MAX(100, (int)round(4000 - 40 * dock.settings.reveal_sensitivity));
    hide_machine_mode(&dock.view->hide, dock.settings.autohide ? AUTOHIDE : HIDE_NEVER, now);
    hide_machine_quirk(&dock.view->hide, MOUSE_OVER_LAUNCHER, mouse_over && !dock.view->hidden,
                       now);
    hide_machine_quirk(&dock.view->hide, QUICKLIST_OPEN, dock.menu_open, now);
    hide_machine_quirk(&dock.view->hide, PLACES_VISIBLE,
                       (dock.search && gtk_widget_get_visible(dock.search)) ||
                           (dock.settings_window && gtk_widget_get_visible(dock.settings_window)),
                       now);
    hide_machine_quirk(&dock.view->hide, KEY_NAV_ACTIVE, dock.view->keyboard_mode, now);
    if (dock.view->hidden && on_monitor &&
        (right ? (x >= edge - 2 && x < edge) : (x >= edge && x <= edge + 1)))
        hide_machine_pressure(&dock.view->hide,
                              (int)MIN(100, MAX(16, now - dock.last_frame_time)) * 6, now);
    hide_machine_tick(&dock.view->hide, now);
    gboolean redraw = shared_redraw;
    if (dock.view->hidden != dock.view->hide.should_hide) {
        dock.view->hidden = dock.view->hide.should_hide;
        hide_machine_quirk(&dock.view->hide, LAUNCHER_HIDDEN, dock.view->hidden, now);
        if (dock.view->hidden) {
            hide_machine_quirk(&dock.view->hide, MOUSE_MOVE_POST_REVEAL, FALSE, now);
            hide_machine_quirk(&dock.view->hide, MOUSE_OVER_LAUNCHER, FALSE, now);
        } else
            hide_machine_quirk(&dock.view->hide, REVEAL_PRESSURE_PASS, FALSE, now);
        animation_to(&dock.view->hide_animation, dock.view->hidden ? 1 : 0, 125, now);
        redraw = TRUE;
    }
    gboolean hover =
        launcher_should_hover(dock.view->hidden, mouse_over, FALSE, dock.view->keyboard_mode,
                              dock.menu_open, dock.view->dragged != NULL);
    animation_to(&dock.view->hover_animation, hover ? 1 : 0, 200, now);
    if (animation_tick(&dock.view->hide_animation, now)) {
        int input_width =
            dock.settings.hide_animation == HIDE_FADE && dock.view->hide_animation.value < 1
                ? dock.view->width
                : MAX(1, (int)ceil(dock.view->width * (1 - dock.view->hide_animation.value)));
        if (input_width != dock.view->input_width) {
            GdkRectangle rectangle = {right ? BOUNCE_GUTTER + dock.view->width - input_width : 0, 0,
                                      input_width, dock.view->height};
            cairo_region_t *region = cairo_region_create_rectangle(&rectangle);
            gdk_window_input_shape_combine_region(gtk_widget_get_window(dock.view->window), region,
                                                  0, 0);
            cairo_region_destroy(region);
            dock.view->input_width = input_width;
        }
        redraw = TRUE;
    }
    redraw |= animation_tick(&dock.view->hover_animation, now);
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        redraw |= animation_tick(&item_view_state(item)->unfold, now);
    }
    if (!hover && dock.view->hover_animation.value == 0 && dock.view->drag_delta) {
        dock.view->drag_delta = 0;
        redraw = TRUE;
    }
    /* Layout can move under a stationary pointer while unfolding, scrolling
     * or changing the app list. Re-evaluate hover using that same current layout. */
    double previous_anchor = dock.view->menu_item ? dock.view->menu_item->layout.center : 0;
    update_layout();
    if (dock.view->menu && dock.view->menu_item &&
        previous_anchor != dock.view->menu_item->layout.center)
        gtk_widget_queue_draw(dock.view->menu);
    redraw |= set_hovered_item(pointer_item(x, y), now);
    if (dock.view->tooltip_window && gtk_widget_get_visible(dock.view->tooltip_window))
        position_icon_tooltip(dock.view);
    if (redraw)
        queue_render(dock.view->items_box);
    dock.mouse_x = x;
    dock.mouse_y = y;
    gboolean active = dock.view->hide_animation.value != dock.view->hide_animation.target ||
                      dock.view->hover_animation.value != dock.view->hover_animation.target;
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        Animation *unfold = &item_view_state(item)->unfold;
        active |= unfold->value != unfold->target;
    }
    return active;
}

/* A brisk throw, a weighted landing and two diminishing rebounds. The rise
 * decelerates into the apex and the fall accelerates into contact; unlike a
 * sine-squared pulse, launch has immediate velocity instead of a slow drift.
 * This is a perceptual approximation, not Apple's private Dock implementation.
 * Reminders have a quiet interval, and animation frames stop during that rest. */
static double attention_jump(double time, double rise, double fall)
{
    if (time < rise) {
        double t = time / rise;
        return 1 - (1 - t) * (1 - t);
    }
    double t = (time - rise) / fall;
    return MAX(0, 1 - t * t);
}

static double attention_level(const Item *item, gint64 now, gboolean *pulsing)
{
    *pulsing = FALSE;
    if (!item_attention_pending(item))
        return 0;
    gint64 elapsed = MAX((gint64)0, now - item->urgent_started);
    double phase = elapsed % 3000;
    if (phase >= 900)
        return 0;
    *pulsing = TRUE;
    if (phase < 480)
        return attention_jump(phase, 220, 260);
    if (phase < 780)
        return .28 * attention_jump(phase - 480, 120, 180);
    return .07 * attention_jump(phase - 780, 50, 70);
}

static gboolean update_attention(Item *item, gint64 now, gboolean animations_enabled,
                                 gboolean *pulsing)
{
    double level = attention_level(item, now, pulsing);
    if (!animations_enabled) {
        level = 0;
        *pulsing = FALSE;
    }
    if (level == item->attention_level)
        return FALSE;
    item->attention_level = level;
    return TRUE;
}

static void advance_shared_animations(gint64 now, gboolean animations_enabled,
                                      gboolean *shared_redraw, gboolean *active,
                                      gboolean *attention_animating)
{
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        *shared_redraw |= animation_tick(&item->visible_animation, now);
        *shared_redraw |= animation_tick(&item->active_animation, now);
        if (item->launch_started && now - item->launch_started >= 10000)
            item->launch_started = 0;
        gboolean pulsing = FALSE;
        *shared_redraw |= update_attention(item, now, animations_enabled, &pulsing);
        *attention_animating |= pulsing;
        gboolean effect = item->launch_started != 0;
        *shared_redraw |= effect;
        *active |= effect || item->visible_animation.value != item->visible_animation.target ||
                   item->active_animation.value != item->active_animation.target;
    }
}

static gboolean animation_frame(gpointer data)
{
    (void)data;
    dock.animation_timer = 0;
    DockView *previous = dock.view;
    gint64 now = g_get_monotonic_time() / 1000;
    gboolean shared_redraw = FALSE, active = FALSE, attention_animating = FALSE;
    gboolean animations_enabled = TRUE;
    GtkSettings *gtk_settings = gtk_settings_get_default();
    if (gtk_settings)
        g_object_get(gtk_settings, "gtk-enable-animations", &animations_enabled, NULL);
    advance_shared_animations(now, animations_enabled, &shared_redraw, &active,
                              &attention_animating);
    for (guint i = 0; i < dock.views->len; i++) {
        dock.view = g_ptr_array_index(dock.views, i);
        active |= animation_step(now, shared_redraw);
    }
    dock.view = previous;
    dock.last_frame_time = g_get_monotonic_time() / 1000;
    dock.animation_fast = active;
    dock.animation_timer =
        g_timeout_add(active || attention_animating ? 16 : 100, animation_frame, NULL);
    return G_SOURCE_REMOVE;
}

static DockView *create_view(int monitor)
{
    DockView *view = g_new0(DockView, 1);
    dock.view = view;
    view->monitor = monitor;
    view->width = dock.settings.icon_size + 13;
    GdkMonitor *target = gdk_display_get_monitor(dock.gdisplay, monitor);
    view->screen = g_object_ref(target);
    view->scale = target ? gdk_monitor_get_scale_factor(target) : 1;
    view->rendered_icon_size = dock.settings.icon_size;
    hide_machine_init(&view->hide);
    GError *error = NULL;
    if (!icon_renderer_init(&view->renderer, dock.settings.icon_size + 6, dock.settings.icon_size,
                            view->scale, &error)) {
        g_warning("Cannot load launcher artwork: %s", error->message);
        g_clear_error(&error);
        g_object_unref(view->screen);
        g_free(view);
        dock.view = NULL;
        return NULL;
    }
    g_ptr_array_add(dock.views, view);
    dock.view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_object_ref_sink(dock.view->window);
    gtk_window_set_title(GTK_WINDOW(dock.view->window), "Lunatic Launcher");
    gtk_widget_set_name(dock.view->window, "lunatic-launcher");
    gtk_window_set_decorated(GTK_WINDOW(dock.view->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(dock.view->window), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(dock.view->window), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dock.view->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(dock.view->window), TRUE);
    /* Let the window manager stack this dock above ordinary windows and below
     * fullscreen clients. An explicit ABOVE state overrides fullscreen stacking
     * in window managers such as xfwm4. */
    gtk_window_set_accept_focus(GTK_WINDOW(dock.view->window), FALSE);
    gtk_window_stick(GTK_WINDOW(dock.view->window));
    GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(dock.view->window));
    if (visual)
        gtk_widget_set_visual(dock.view->window, visual);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "#lunatic-launcher { background-color: transparent; }", -1,
                                    NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(dock.view->window),
                                   GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    dock.view->items_box = gtk_drawing_area_new();
    gtk_widget_set_size_request(dock.view->items_box, dock.view->width, 1);
    gtk_widget_set_can_focus(dock.view->items_box, TRUE);
    view->tooltip_window = gtk_window_new(GTK_WINDOW_POPUP);
    g_object_ref_sink(view->tooltip_window);
    gtk_window_set_type_hint(GTK_WINDOW(view->tooltip_window), GDK_WINDOW_TYPE_HINT_TOOLTIP);
    gtk_window_set_accept_focus(GTK_WINDOW(view->tooltip_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(view->tooltip_window), FALSE);
    gtk_window_set_transient_for(GTK_WINDOW(view->tooltip_window), GTK_WINDOW(view->window));
    gtk_style_context_add_class(gtk_widget_get_style_context(view->tooltip_window),
                                GTK_STYLE_CLASS_TOOLTIP);
    view->tooltip_label = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(view->tooltip_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(view->tooltip_label), 40);
    gtk_widget_set_margin_start(view->tooltip_label, 10);
    gtk_widget_set_margin_end(view->tooltip_label, 10);
    gtk_widget_set_margin_top(view->tooltip_label, 6);
    gtk_widget_set_margin_bottom(view->tooltip_label, 6);
    gtk_container_add(GTK_CONTAINER(view->tooltip_window), view->tooltip_label);
    gtk_widget_show(view->tooltip_label);
    g_signal_connect(view->tooltip_window, "realize", G_CALLBACK(tooltip_realized), view);
    gtk_container_add(GTK_CONTAINER(dock.view->window), dock.view->items_box);
    gtk_widget_add_events(dock.view->items_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                                    GDK_POINTER_MOTION_MASK |
                                                    GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK |
                                                    GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(dock.view->items_box, "draw", G_CALLBACK(canvas_render), view);
    g_signal_connect(dock.view->items_box, "realize", G_CALLBACK(canvas_realize), view);
    g_signal_connect(dock.view->items_box, "unrealize", G_CALLBACK(canvas_unrealize), view);
    g_signal_connect(dock.view->items_box, "notify::scale-factor", G_CALLBACK(canvas_scale_changed),
                     view);
    g_signal_connect(dock.view->items_box, "button-press-event", G_CALLBACK(canvas_press), view);
    g_signal_connect(dock.view->items_box, "button-release-event", G_CALLBACK(canvas_release),
                     view);
    g_signal_connect(dock.view->items_box, "motion-notify-event", G_CALLBACK(canvas_motion), view);
    g_signal_connect(dock.view->items_box, "leave-notify-event", G_CALLBACK(canvas_leave), view);
    g_signal_connect(dock.view->items_box, "scroll-event", G_CALLBACK(canvas_scroll), view);
    gtk_drag_source_set(dock.view->items_box, GDK_BUTTON1_MASK, drop_targets, 1, GDK_ACTION_MOVE);
    g_signal_connect(dock.view->items_box, "drag-begin", G_CALLBACK(drag_begin), view);
    g_signal_connect(dock.view->items_box, "drag-end", G_CALLBACK(drag_end), view);
    g_signal_connect(dock.view->items_box, "drag-data-get", G_CALLBACK(drag_data_get), view);
    gtk_drag_dest_set(dock.view->items_box, GTK_DEST_DEFAULT_ALL, drop_targets, 2,
                      GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(dock.view->items_box, "drag-data-received", G_CALLBACK(drop_received), view);
    g_signal_connect(dock.view->window, "realize", G_CALLBACK(realized), view);
    g_signal_connect(dock.view->window, "destroy", G_CALLBACK(view_window_destroyed), view);
    position();
    gtk_widget_show_all(view->window);
    position();
    return view;
}

static void view_window_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    DockView *view = data;
    if (!view->disposing && !dock.closing)
        quit_launcher();
}

static void destroy_view(gpointer data)
{
    DockView *view = data;
    DockView *previous = dock.view;
    dock.view = view;
    view->disposing = TRUE;
    hide_icon_tooltip(view);
    if (view->tooltip_window) {
        gtk_widget_destroy(view->tooltip_window);
        g_object_unref(view->tooltip_window);
        view->tooltip_window = view->tooltip_label = NULL;
    }
    if (view->menu) {
        g_signal_handlers_disconnect_by_data(view->menu, view);
        gtk_widget_destroy(view->menu);
        view->menu = NULL;
        dock.menu_open = FALSE;
    }
    /* A settings change can remove the monitor hosting the dialog. Keep the
     * dialog alive and reattach it to a surviving view after reconciliation. */
    if (dock.settings_window &&
        gtk_window_get_transient_for(GTK_WINDOW(dock.settings_window)) == GTK_WINDOW(view->window))
        gtk_window_set_transient_for(GTK_WINDOW(dock.settings_window), NULL);
    if (dock.search &&
        gtk_window_get_transient_for(GTK_WINDOW(dock.search)) == GTK_WINDOW(view->window))
        gtk_window_set_transient_for(GTK_WINDOW(dock.search), NULL);
    if (dock.keyboard_view == view)
        dock.keyboard_view = NULL;
    if (view->keyboard_mode)
        XUngrabKeyboard(dock.display, CurrentTime);
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        ItemView *cache = g_hash_table_lookup(item->render_caches, view);
        if (cache) {
            icon_renderer_cache_clear(&view->renderer, &cache->render);
            g_hash_table_remove(item->render_caches, view);
        }
    }
    gtk_widget_destroy(view->window);
    g_object_unref(view->window);
    icon_renderer_clear(&view->renderer);
    g_object_unref(view->screen);
    dock.view = previous == view ? NULL : previous;
    g_free(view);
}

static void position_all(void)
{
    launcher_dash_hide(dock.dash);
    if (!dock.views)
        return;
    DockView *previous = dock.view;
    for (guint i = 0; i < dock.views->len; i++) {
        dock.view = g_ptr_array_index(dock.views, i);
        position();
    }
    dock.view = previous;
}

static int preferred_image_scale(void)
{
    int count = gdk_display_get_n_monitors(dock.gdisplay), scale = 1;
    GdkMonitor *primary = gdk_display_get_primary_monitor(dock.gdisplay);
    if (!primary && count)
        primary = gdk_display_get_monitor(dock.gdisplay, MIN(dock.settings.monitor, count - 1));
    for (int i = 0; i < count; i++) {
        GdkMonitor *monitor = gdk_display_get_monitor(dock.gdisplay, i);
        if (dock.settings.visibility == VISIBILITY_ALL || monitor == primary)
            scale = MAX(scale, gdk_monitor_get_scale_factor(monitor));
    }
    return scale;
}

static void reconcile_views(void)
{
    launcher_dash_hide(dock.dash);
    if (!dock.views || dock.closing)
        return;
    int image_scale = preferred_image_scale();
    if (image_scale != dock.image_scale) {
        dock.image_scale = image_scale;
        reload_item_images();
    }
    DockView *previous = dock.view;
    GdkMonitor *primary = gdk_display_get_primary_monitor(dock.gdisplay);
    int count = gdk_display_get_n_monitors(dock.gdisplay);
    if (!primary && count)
        primary = gdk_display_get_monitor(dock.gdisplay, MIN(dock.settings.monitor, count - 1));
    for (guint i = dock.views->len; i > 0; i--) {
        DockView *view = g_ptr_array_index(dock.views, i - 1);
        gboolean keep = FALSE;
        for (int j = 0; j < count; j++) {
            GdkMonitor *monitor = gdk_display_get_monitor(dock.gdisplay, j);
            if (monitor == view->screen &&
                (dock.settings.visibility == VISIBILITY_ALL || monitor == primary)) {
                keep = TRUE;
                view->monitor = j;
                break;
            }
        }
        if (!keep) {
            if (previous == view)
                previous = NULL;
            g_ptr_array_remove_index(dock.views, i - 1);
        }
    }
    for (int j = 0; j < count; j++) {
        GdkMonitor *monitor = gdk_display_get_monitor(dock.gdisplay, j);
        if (dock.settings.visibility == VISIBILITY_PRIMARY && monitor != primary)
            continue;
        gboolean present = FALSE;
        for (guint i = 0; i < dock.views->len; i++)
            if (((DockView *)g_ptr_array_index(dock.views, i))->screen == monitor) {
                present = TRUE;
                break;
            }
        if (!present)
            create_view(j);
    }
    dock.view = previous ? previous : dock.views->len ? g_ptr_array_index(dock.views, 0) : NULL;
    if (dock.view && dock.settings_window &&
        !gtk_window_get_transient_for(GTK_WINDOW(dock.settings_window)))
        gtk_window_set_transient_for(GTK_WINDOW(dock.settings_window),
                                     GTK_WINDOW(dock.view->window));
    position_all();
    queue_all_views();
}

static double pixel_channel(unsigned long pixel, unsigned long mask)
{
    if (!mask)
        return 0;
    while (!(mask & 1)) {
        mask >>= 1;
        pixel >>= 1;
    }
    return (double)(pixel & mask) / mask;
}

static void refresh_wallpaper_color(void)
{
    if (dock.settings.color_mode != COLOR_WALLPAPER)
        return;
    const char *properties[] = {"_XROOTPMAP_ID", "ESETROOT_PMAP_ID", "_XSETROOT_ID"};
    Pixmap pixmap = None;
    for (guint i = 0; i < G_N_ELEMENTS(properties) && !pixmap; i++) {
        unsigned long count = 0;
        unsigned long *value =
            (unsigned long *)property(dock.root, properties[i], XA_PIXMAP, 32, &count);
        if (value && count)
            pixmap = value[0];
        if (value)
            XFree(value);
    }
    if (!pixmap) {
        queue_all_views();
        return;
    }
    Window root;
    int x, y;
    unsigned w, h, border, depth;
    XImage *image = NULL;
    gdk_x11_display_error_trap_push(dock.gdisplay);
    if (XGetGeometry(dock.display, pixmap, &root, &x, &y, &w, &h, &border, &depth) && w && h) {
        /* Sample a bounded central region once per wallpaper notification. */
        unsigned sw = MIN(w, 1024), sh = MIN(h, 1024);
        image = XGetImage(dock.display, pixmap, (int)(w - sw) / 2, (int)(h - sh) / 2, sw, sh,
                          AllPlanes, ZPixmap);
    }
    int error = gdk_x11_display_error_trap_pop(dock.gdisplay);
    if (image) {
        if (!error) {
            Visual *visual = DefaultVisual(dock.display, DefaultScreen(dock.display));
            if (!image->red_mask)
                image->red_mask = visual->red_mask;
            if (!image->green_mask)
                image->green_mask = visual->green_mask;
            if (!image->blue_mask)
                image->blue_mask = visual->blue_mask;
            double color[3] = {0};
            guint samples = 0;
            for (int py = 0; py < image->height; py += MAX(1, image->height / 32))
                for (int px = 0; px < image->width; px += MAX(1, image->width / 32)) {
                    unsigned long pixel = XGetPixel(image, px, py);
                    color[0] += pixel_channel(pixel, image->red_mask);
                    color[1] += pixel_channel(pixel, image->green_mask);
                    color[2] += pixel_channel(pixel, image->blue_mask);
                    samples++;
                }
            for (int i = 0; i < 3; i++)
                dock.wallpaper_color[i] = color[i] / samples;
        }
        XDestroyImage(image);
    }
    queue_all_views();
}

static void reload_item_images(void)
{
    launcher_dash_clear_icons(dock.dash);
    for (guint i = 0; i < dock.items->len; i++) {
        Item *item = g_ptr_array_index(dock.items, i);
        for (guint j = 0; j < dock.views->len; j++) {
            DockView *view = g_ptr_array_index(dock.views, j);
            icon_renderer_forget(&view->renderer, item->icon);
        }
        g_clear_object(&item->icon);
        if (item->kind == 1)
            item->icon = launcher_resource_icon("launcher_mark.svg",
                                                dock.settings.icon_size * dock.image_scale, NULL);
        else if (item->kind == 2)
            item->icon = load_icon(NULL, "user-trash");
        else if (item->kind == 3)
            item->icon = load_icon(NULL, "user-desktop");
        else if (item->app)
            item->icon =
                load_icon(g_app_info_get_icon(G_APP_INFO(item->app)), "application-x-executable");
        else if (item->clients->len)
            item->icon = window_icon(((Client *)g_ptr_array_index(item->clients, 0))->xid);
        if (item->icon && !item->kind)
            launcher_icon_colors(item->icon, item->background, item->glow_color);
    }
}

static Item *sync_system_item(const char *id, const char *name, gboolean enabled)
{
    Item *item = find_item(id);
    if (!enabled) {
        if (item)
            g_ptr_array_remove(dock.items, item);
        return NULL;
    }
    if (!item) {
        item = add_item(id, NULL, name);
        item->visible_animation.value = item->visible_animation.target = 1;
    }
    return item;
}

static void sync_system_icons(void)
{
    Item *desktop = sync_system_item("lunatic://desktop", "Show Desktop", dock.settings.show_desktop);
    Item *trash = sync_system_item("lunatic://trash", "Trash", dock.settings.show_trash);
    if (desktop && !desktop->icon)
        desktop->icon = load_icon(NULL, "user-desktop");
    if (trash && !trash->icon)
        trash->icon = load_icon(NULL, "user-trash");
}

static gboolean apply_settings(gpointer data)
{
    (void)data;
    dock.settings_idle = 0;
    dock.settings = dock.pending_settings;
    int image_scale = preferred_image_scale();
    gboolean images_changed = dock.settings.icon_size != dock.applied_settings.icon_size ||
                              image_scale != dock.image_scale;
    dock.image_scale = image_scale;
    sync_system_icons();
    if (images_changed)
        reload_item_images();
    reconcile_views();
    DockView *previous = dock.view;
    for (guint i = 0; i < dock.views->len; i++) {
        dock.view = g_ptr_array_index(dock.views, i);
        dock.view->width = dock.settings.icon_size + 13;
        if (images_changed)
            canvas_scale_changed(dock.view->items_box, NULL, dock.view);
        position();
    }
    dock.view = previous;
    if (dock.settings.color_mode == COLOR_WALLPAPER)
        refresh_wallpaper_color();
    dock.applied_settings = dock.settings;
    save_config();
    schedule_refresh();
    wake_animation();
    queue_all_views();
    return G_SOURCE_REMOVE;
}

static void settings_changed(const LauncherSettings *previous, gpointer data)
{
    (void)previous;
    (void)data;
    /* Coalesce slider/spin changes; rebuilding image caches for every mouse-motion
     * event would defeat the renderer's cache. */
    if (dock.settings_idle)
        g_source_remove(dock.settings_idle);
    dock.settings_idle = g_timeout_add(150, apply_settings, NULL);
}

static void show_settings(GtkMenuItem *menu, gpointer data)
{
    (void)menu;
    if (data)
        dock.view = data;
    if (dock.settings_window) {
        gtk_window_present(GTK_WINDOW(dock.settings_window));
        return;
    }
    if (dock.settings_idle) {
        g_source_remove(dock.settings_idle);
        apply_settings(NULL);
    }
    dock.pending_settings = dock.settings;
    dock.settings_window = launcher_settings_dialog_new(
        GTK_WINDOW(dock.view->window), &dock.pending_settings, settings_changed, NULL);
    g_object_add_weak_pointer(G_OBJECT(dock.settings_window), (gpointer *)&dock.settings_window);
    wake_animation();
}

int main(int argc, char **argv)
{
    g_set_prgname("lunatic-launcher");
    g_set_application_name("Lunatic Launcher");
    if (argc == 5 && g_str_equal(argv[1], "--launch-desktop-action"))
        return launcher_run_desktop_action(argv[2], argv[3],
                                           (guint32)g_ascii_strtoull(argv[4], NULL, 10));
    launcher_settings_defaults(&dock.settings);
    int requested_size = 0, requested_monitor = -1;
    gboolean requested_autohide = FALSE, no_autohide = FALSE, version = FALSE,
             requested_minimize = FALSE;
    char **requested_pins = NULL;
    GOptionEntry options[] = {
        {"icon-size", 0, 0, G_OPTION_ARG_INT, &requested_size, "Icon size (24–96)", "PIXELS"},
        {"monitor", 0, 0, G_OPTION_ARG_INT, &requested_monitor, "Monitor index, starting at 0",
         "INDEX"},
        {"autohide", 0, 0, G_OPTION_ARG_NONE, &requested_autohide,
         "Reveal on the launcher's screen edge", NULL},
        {"no-autohide", 0, 0, G_OPTION_ARG_NONE, &no_autohide, "Keep launcher visible", NULL},
        {"config", 0, 0, G_OPTION_ARG_FILENAME, &dock.config, "Use a different configuration file",
         "FILE"},
        {"pin", 0, 0, G_OPTION_ARG_STRING_ARRAY, &requested_pins,
         "Add a desktop entry to favorites (repeatable)", "DESKTOP-ID"},
        {"minimize-on-click", 0, 0, G_OPTION_ARG_NONE, &requested_minimize,
         "Minimize a focused single window when clicked", NULL},
        {"version", 0, 0, G_OPTION_ARG_NONE, &version, "Print version", NULL},
        {NULL, 0, 0, 0, NULL, NULL, NULL}};
    GOptionContext *options_context = g_option_context_new("— Lunatic application launcher for X11");
    g_option_context_add_main_entries(options_context, options, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(options_context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        return 2;
    }
    g_option_context_free(options_context);
    if (version) {
        g_print("lunatic-launcher 0.1.0\n");
        return 0;
    }
    if (argc > 1 || (requested_size && (requested_size < 24 || requested_size > 96)) ||
        requested_monitor < -1 || (requested_autohide && no_autohide)) {
        g_printerr("Invalid options; see lunatic-launcher --help\n");
        return 2;
    }
    dock.urgency_trace_dir = g_getenv("LUNATIC_LAUNCHER_TRACE_URGENCY");
    if (dock.urgency_trace_dir && !*dock.urgency_trace_dir)
        dock.urgency_trace_dir = NULL;
    if (dock.urgency_trace_dir) {
        if (g_mkdir_with_parents(dock.urgency_trace_dir, 0700) < 0) {
            g_printerr("Cannot create urgency trace directory: %s\n", dock.urgency_trace_dir);
            return 1;
        }
        g_printerr("urgency: diagnostics enabled; frames saved to %s\n", dock.urgency_trace_dir);
    }
    /* Choose X11 explicitly, even when GTK's default backend is Wayland. */
    gdk_set_allowed_backends("x11");
    if (!gtk_init_check(&argc, &argv)) {
        g_printerr("Cannot open an X11 display. Run inside an X11 desktop session.\n");
        return 1;
    }
    dock.gdisplay = gdk_display_get_default();
    dock.display = gdk_x11_display_get_xdisplay(dock.gdisplay);
    dock.root = DefaultRootWindow(dock.display);
    /* X selection gives a session-wide singleton without a daemon or lock file. */
    Atom selection = atom("_LUNATIC_LAUNCHER_STANDALONE_S0");
    gdk_x11_display_grab(dock.gdisplay);
    if (XGetSelectionOwner(dock.display, selection) != None) {
        gdk_x11_display_ungrab(dock.gdisplay);
        g_printerr("Lunatic Launcher is already running on this display.\n");
        return 1;
    }
    Window owner = XCreateSimpleWindow(dock.display, dock.root, -1, -1, 1, 1, 0, 0, 0);
    XSetSelectionOwner(dock.display, selection, owner, CurrentTime);
    gdk_x11_display_ungrab(dock.gdisplay);
    gboolean default_config = dock.config == NULL, migrated_config = FALSE;
    if (default_config)
        dock.config =
            g_build_filename(g_get_user_config_dir(), "lunatic", "launcher.ini", NULL);
    char **pins = launcher_load_config(dock.config, &dock.settings, &error);
    if (default_config && g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        g_clear_error(&error);
        char *legacy = g_build_filename(g_get_user_config_dir(), "unity-launcher", "launcher.ini", NULL);
        pins = launcher_load_config(legacy, &dock.settings, &error);
        g_free(legacy);
        migrated_config = error == NULL;
    }
    gboolean first_run = FALSE;
    if (error) {
        if (g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            first_run = TRUE;
        else {
            g_printerr("Cannot read %s: %s\n", dock.config, error->message);
            return 1;
        }
        g_clear_error(&error);
    }
    if (requested_minimize)
        dock.settings.minimize_on_click = TRUE;
    if (requested_size)
        dock.settings.icon_size = requested_size;
    if (requested_monitor >= 0)
        dock.settings.monitor = requested_monitor;
    if (requested_autohide || no_autohide)
        dock.settings.autohide = requested_autohide;
    dock.wallpaper_color[0] = 95 / 255.0;
    dock.wallpaper_color[1] = 18 / 255.0;
    dock.wallpaper_color[2] = 45 / 255.0;
    dock.applied_settings = dock.settings;
    dock.image_scale = preferred_image_scale();
    dock.views = g_ptr_array_new_with_free_func(destroy_view);
    dock.items = g_ptr_array_new_with_free_func(item_free);
    dock.layout_scratch = g_array_sized_new(FALSE, FALSE, sizeof(LayoutIcon), 64);
    dock.watched_windows = g_hash_table_new(g_direct_hash, g_direct_equal);
    dock.apps = g_list_sort(g_app_info_get_all(), app_compare);
    dock.apps_index = launcher_app_index_new(dock.apps);
    Item *home = add_item("lunatic://home", NULL, "Search applications");
    home->kind = 1;
    home->icon = launcher_resource_icon("launcher_mark.svg",
                                        dock.settings.icon_size * dock.image_scale, NULL);
    home->visible_animation.value = home->visible_animation.target = 1;
    if (first_run) {
        GAppInfo *defaults[] = {g_app_info_get_default_for_type("inode/directory", FALSE),
                                g_app_info_get_default_for_uri_scheme("https")};
        for (guint i = 0; i < G_N_ELEMENTS(defaults); i++) {
            if (defaults[i] && G_IS_DESKTOP_APP_INFO(defaults[i]))
                add_item(g_app_info_get_id(defaults[i]), G_DESKTOP_APP_INFO(defaults[i]), NULL)
                    ->pinned = TRUE;
            g_clear_object(&defaults[i]);
        }
    }
    for (guint pass = 0; pass < 2; pass++) {
        char **list = pass ? requested_pins : pins;
        for (guint i = 0; list && list[i]; i++) {
            GDesktopAppInfo *app = g_path_is_absolute(list[i])
                                       ? g_desktop_app_info_new_from_filename(list[i])
                                       : g_desktop_app_info_new(list[i]);
            if (app) {
                add_item(list[i], app, NULL)->pinned = TRUE;
                g_object_unref(app);
            } else
                g_printerr("Desktop entry unavailable: %s\n", list[i]);
        }
    }
    g_strfreev(pins);
    gboolean pins_requested = requested_pins && requested_pins[0];
    g_strfreev(requested_pins);
    sync_system_icons();
    reconcile_views();
    if (!dock.views->len)
        return 1;
    refresh_wallpaper_color();
    refresh();
    GdkWindow *root_window = gdk_get_default_root_window();
    gdk_window_set_events(root_window,
                          gdk_window_get_events(root_window) | GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(NULL, window_event_filter, NULL);
    configure_shortcuts(TRUE);
    dock.animation_timer = g_timeout_add(16, animation_frame, NULL);
    dock.app_monitor = g_app_info_monitor_get();
    dock.app_monitor_handler =
        g_signal_connect(dock.app_monitor, "changed", G_CALLBACK(apps_changed), NULL);
    g_signal_connect(dock.gdisplay, "monitor-added", G_CALLBACK(monitors_changed), NULL);
    g_signal_connect(dock.gdisplay, "monitor-removed", G_CALLBACK(monitors_changed), NULL);
    g_signal_connect(gdk_display_get_default_screen(dock.gdisplay), "monitors-changed",
                     G_CALLBACK(screen_monitors_changed), NULL);
    /* Optional: a missing session bus never prevents the launcher from running. */
    dock.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (dock.bus) {
        dock.bus_owner_subscription = g_dbus_connection_signal_subscribe(
            dock.bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
            "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE, launcher_entry_owner_changed, NULL,
            NULL);
        dock.bus_subscription = g_dbus_connection_signal_subscribe(
            dock.bus, NULL, "com.canonical.Unity.LauncherEntry", "Update", NULL, NULL,
            G_DBUS_SIGNAL_FLAGS_NONE, launcher_entry_update, NULL, NULL);
    }
    guint sigterm = g_unix_signal_add(SIGTERM, terminate_signal, NULL);
    guint sigint = g_unix_signal_add(SIGINT, terminate_signal, NULL);
    if (first_run || migrated_config || pins_requested || requested_size || requested_monitor >= 0 ||
        requested_autohide || no_autohide)
        save_config();
    gtk_main();
    end_key_navigation();
    configure_shortcuts(FALSE);
    g_source_remove(sigterm);
    g_source_remove(sigint);
    gdk_window_remove_filter(NULL, window_event_filter, NULL);
    if (dock.refresh_idle)
        g_source_remove(dock.refresh_idle);
    g_source_remove(dock.animation_timer);
    if (dock.reload_idle)
        g_source_remove(dock.reload_idle);
    g_signal_handler_disconnect(dock.app_monitor, dock.app_monitor_handler);
    g_object_unref(dock.app_monitor);
    if (dock.bus) {
        g_dbus_connection_signal_unsubscribe(dock.bus, dock.bus_subscription);
        g_dbus_connection_signal_unsubscribe(dock.bus, dock.bus_owner_subscription);
        g_object_unref(dock.bus);
    }
    dock.closing = TRUE;
    launcher_dash_free(dock.dash);
    dock.dash = NULL;
    dock.search = NULL;
    if (dock.settings_window)
        gtk_widget_destroy(dock.settings_window);
    if (dock.settings_idle) {
        g_source_remove(dock.settings_idle);
        dock.settings_idle = 0;
        dock.settings = dock.pending_settings;
        save_config();
    }
    g_ptr_array_unref(dock.views);
    dock.views = NULL;
    dock.view = NULL;
    g_ptr_array_unref(dock.items);
    g_array_unref(dock.layout_scratch);
    g_hash_table_unref(dock.watched_windows);
    launcher_app_index_free(dock.apps_index);
    g_list_free_full(dock.apps, g_object_unref);
    XDestroyWindow(dock.display, owner);
    g_free(dock.config);
    return 0;
}
