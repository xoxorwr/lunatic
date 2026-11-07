/* SPDX-License-Identifier: GPL-3.0-only
 * Run the production X11 refresh/event path with controlled server replies.
 * This does not require or alter the user's desktop session.
 */
#define XGetWindowProperty fixture_property
#define XGetClassHint fixture_class
#define XGetWindowAttributes fixture_attributes
#define XSelectInput fixture_select
#define XGetWMHints fixture_hints
#define XSendEvent fixture_send
#define XFlush fixture_flush
#define XFree fixture_free
#define gdk_x11_display_error_trap_push fixture_trap_push
#define gdk_x11_display_error_trap_pop fixture_trap_pop
#define gdk_x11_display_error_trap_pop_ignored fixture_trap_ignore
#define gdk_x11_get_xatom_by_name_for_display fixture_atom
#define gdk_x11_window_lookup_for_display fixture_lookup
#define gdk_window_get_window_type fixture_window_type
#define main launcher_session_main
#include "../src/main.c"
#undef main

static GHashTable *atoms;
static unsigned long workspace;
static gboolean cached_window;
static GdkWindowType cached_type;
static guint activation_requests;
static const Window chrome_window = 200;

Atom fixture_atom(GdkDisplay *display, const gchar *name)
{
    (void)display;
    gpointer value = g_hash_table_lookup(atoms, name);
    if (!value) {
        value = GUINT_TO_POINTER(1000 + g_hash_table_size(atoms));
        g_hash_table_insert(atoms, g_strdup(name), value);
    }
    return (Atom)(guintptr)value;
}

int fixture_property(Display *display, Window window, Atom property_atom, long offset,
                     long length, Bool delete_property, Atom requested_type, Atom *actual,
                     int *format, unsigned long *count, unsigned long *remaining,
                     unsigned char **data)
{
    (void)display; (void)offset; (void)length; (void)delete_property;
    *actual = requested_type;
    *format = requested_type == atom("UTF8_STRING") ? 8 : 32;
    *remaining = *count = 0;
    *data = NULL;
    unsigned long value;
    if (window == dock.root && property_atom == atom("_NET_CURRENT_DESKTOP"))
        value = workspace;
    else if (window == dock.root && property_atom == atom("_NET_CLIENT_LIST"))
        value = chrome_window;
    else if (window == dock.root && property_atom == atom("_NET_ACTIVE_WINDOW"))
        value = None;
    else if (window == chrome_window && property_atom == atom("_NET_WM_DESKTOP"))
        value = 0;
    else
        return Success;
    *count = 1;
    *data = g_memdup2(&value, sizeof(value));
    return Success;
}

int fixture_class(Display *display, Window window, XClassHint *hint)
{
    (void)display;
    g_assert_cmpuint(window, ==, chrome_window);
    hint->res_name = g_strdup("google-chrome");
    hint->res_class = g_strdup("Google-chrome");
    return 1;
}

Status fixture_attributes(Display *display, Window window, XWindowAttributes *attributes)
{
    (void)display; (void)window;
    memset(attributes, 0, sizeof(*attributes));
    return 1;
}

int fixture_select(Display *display, Window window, long mask)
{
    (void)display; (void)window;
    g_assert_true((mask & PropertyChangeMask) != 0);
    return 1;
}

XWMHints *fixture_hints(Display *display, Window window)
{
    (void)display; (void)window;
    return NULL;
}

int fixture_free(void *data) { g_free(data); return 0; }
int fixture_flush(Display *display) { (void)display; return 0; }

Status fixture_send(Display *display, Window window, Bool propagate, long mask, XEvent *event)
{
    (void)display; (void)propagate; (void)mask;
    g_assert_cmpuint(window, ==, dock.root);
    if (event->xclient.message_type == atom("_NET_ACTIVE_WINDOW")) {
        g_assert_cmpuint(event->xclient.window, ==, chrome_window);
        activation_requests++;
    } else {
        g_assert_cmpuint(event->xclient.message_type, ==, atom("_NET_CURRENT_DESKTOP"));
        g_assert_cmpint(event->xclient.data.l[0], ==, 0);
    }
    return 1;
}

void fixture_trap_push(GdkDisplay *display) { (void)display; }
gint fixture_trap_pop(GdkDisplay *display) { (void)display; return 0; }
void fixture_trap_ignore(GdkDisplay *display) { (void)display; }

GdkWindow *fixture_lookup(GdkDisplay *display, Window window)
{
    (void)display;
    return cached_window && window == chrome_window ? (GdkWindow *)&cached_type : NULL;
}

GdkWindowType fixture_window_type(GdkWindow *window)
{
    g_assert_true(window == (GdkWindow *)&cached_type);
    return cached_type;
}

static void switch_workspace(unsigned long target)
{
    workspace = target;
    XEvent event = {0};
    event.xproperty.type = PropertyNotify;
    event.xproperty.window = dock.root;
    event.xproperty.atom = atom("_NET_CURRENT_DESKTOP");
    window_event_filter(&event, NULL, NULL);
    while (g_main_context_iteration(NULL, FALSE)) {}
}

static void tracking(gconstpointer foreign)
{
    atoms = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    DockView view = {0};
    dock.view = &view;
    dock.root = 100;
    launcher_settings_defaults(&dock.settings);
    dock.items = g_ptr_array_new_with_free_func(item_free);
    dock.views = g_ptr_array_new();
    dock.watched_windows = g_hash_table_new(g_direct_hash, g_direct_equal);
    dock.apps_index = launcher_app_index_new(NULL);
    Item *item = add_item("google-chrome.desktop", NULL, "Chrome");
    item->pinned = TRUE;
    item->icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
    GKeyFile *key = g_key_file_new();
    g_key_file_load_from_data(key, "[Desktop Entry]\nType=Application\nName=Chrome\n"
                                  "Exec=true\nStartupWMClass=Google-chrome\n", -1, 0, NULL);
    item->app = g_desktop_app_info_new_from_keyfile(key);
    g_key_file_unref(key);
    g_assert_nonnull(item->app);
    cached_window = FALSE;
    cached_type = GDK_WINDOW_FOREIGN;
    workspace = 0;
    activation_requests = 0;
    refresh();
    g_assert_cmpuint(item->clients->len, ==, 1);

    for (guint round = 0; round < 5; round++) {
        switch_workspace(1);
        g_assert_cmpuint(item->clients->len, ==, 0);
        g_assert_cmpuint(item_render_arg(item, 1, 1000).running, ==, 0);
        g_assert_null(item_activation_client(item, 1));
        cached_window = GPOINTER_TO_INT(foreign);
        switch_workspace(0);
        g_assert_cmpuint(item->clients->len, ==, 1);
        g_assert_cmpuint(item_render_arg(item, 1, 1000).running, ==, 1);
        g_assert_cmpuint(item_activation_client(item, 0)->xid, ==, chrome_window);
        item_clicked(NULL, item);
        g_assert_cmpuint(activation_requests, ==, round + 1);
    }

    dock.settings.all_workspaces = TRUE;
    switch_workspace(1);
    g_assert_cmpuint(item->clients->len, ==, 1);
    g_assert_cmpuint(item_activation_client(item, 1)->xid, ==, chrome_window);
    item_clicked(NULL, item);
    g_assert_cmpuint(activation_requests, ==, 6);

    /* A window genuinely owned by this process must still be excluded. */
    cached_window = TRUE;
    cached_type = GDK_WINDOW_TOPLEVEL;
    refresh();
    g_assert_cmpuint(item->clients->len, ==, 0);
    launcher_app_index_free(dock.apps_index);
    g_hash_table_unref(dock.watched_windows);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    g_hash_table_unref(atoms);
    dock = (Launcher){0};
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/tracking/workspace-round-trip", GINT_TO_POINTER(FALSE), tracking);
    g_test_add_data_func("/tracking/foreign-window-round-trip", GINT_TO_POINTER(TRUE), tracking);
    return g_test_run();
}
