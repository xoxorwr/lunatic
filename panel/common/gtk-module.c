/* SPDX-License-Identifier: GPL-3.0-only
 * In-process GTK 3 exporter. Loaded by GTK through XSettings; never inject into or
 * alter another process from the panel. Existing native GTK exports stay intact.
 */
#include "gtk-menu-export.h"
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gmodule.h>

typedef struct {
    GtkWidget *window, *menubar;
    GDBusConnection *bus;
    PanelGtkMenuExport *exporter;
    guint menu_id, actions_id;
    char *menu_path, *actions_path;
} WindowExport;

static GDBusConnection *session_bus;
static guint discovery_idle;
static guint64 next_window;
static const char export_key[] = "lunatic-panel-window-export";

static void window_export_free(gpointer data)
{
    WindowExport *record = data;
    GdkWindow *window = gtk_widget_get_window(record->window);
    if (window && GDK_IS_X11_WINDOW(window) && !gdk_window_is_destroyed(window)) {
        GdkDisplay *display = gdk_window_get_display(window);
        gdk_x11_display_error_trap_push(display);
        const char *properties[] = {PANEL_MENU_BUS_PROPERTY, PANEL_MENU_PATH_PROPERTY,
                                     PANEL_MENU_ACTIONS_PROPERTY};
        for (guint i = 0; i < G_N_ELEMENTS(properties); i++)
            XDeleteProperty(gdk_x11_display_get_xdisplay(display), gdk_x11_window_get_xid(window),
                            gdk_x11_get_xatom_by_name_for_display(display, properties[i]));
        gdk_x11_display_error_trap_pop_ignored(display);
    }
    if (record->menu_id) g_dbus_connection_unexport_menu_model(record->bus, record->menu_id);
    if (record->actions_id) g_dbus_connection_unexport_action_group(record->bus, record->actions_id);
    panel_gtk_menu_export_free(record->exporter);
    g_object_unref(record->bus);
    g_free(record->menu_path);
    g_free(record->actions_path);
    g_free(record);
}

static void window_destroy(GtkWidget *window, gpointer data)
{
    (void)data;
    WindowExport *record = g_object_steal_data(G_OBJECT(window), export_key);
    if (record) window_export_free(record);
}

static GtkWidget *find_menubar(GtkWidget *widget, guint depth)
{
    if (GTK_IS_MENU_BAR(widget)) return widget;
    if (depth > 32 || !GTK_IS_CONTAINER(widget)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *it = children; it && !found; it = it->next)
        found = find_menubar(it->data, depth + 1);
    g_list_free(children);
    return found;
}

static void publish_window(GtkWidget *window, GtkWidget *menubar)
{
    WindowExport *existing = g_object_get_data(G_OBJECT(window), export_key);
    if (existing && existing->menubar == menubar) return;
    if (existing) {
        g_object_steal_data(G_OBJECT(window), export_key);
        window_export_free(existing);
    }
    WindowExport *record = g_new0(WindowExport, 1);
    record->window = window;
    record->menubar = menubar;
    record->bus = g_object_ref(session_bus);
    record->exporter = panel_gtk_menu_export_new(GTK_MENU_BAR(menubar));
    guint64 serial = ++next_window;
    record->menu_path = g_strdup_printf("/org/xoxorwr/Lunatic/Panel/Menus/w%" G_GUINT64_FORMAT, serial);
    record->actions_path = g_strdup_printf("/org/xoxorwr/Lunatic/Panel/Actions/w%" G_GUINT64_FORMAT, serial);
    GError *error = NULL;
    record->actions_id = g_dbus_connection_export_action_group(record->bus, record->actions_path,
        panel_gtk_menu_export_actions(record->exporter), &error);
    if (record->actions_id)
        record->menu_id = g_dbus_connection_export_menu_model(record->bus, record->menu_path,
            panel_gtk_menu_export_model(record->exporter), &error);
    if (!record->menu_id) {
        g_debug("Panel GTK menu export failed: %s", error ? error->message : "no connection");
        g_clear_error(&error);
        window_export_free(record);
        return;
    }
    /* Publish only after both D-Bus objects are ready. The names are separate
     * from _GTK_* so GtkApplication's original action namespaces are preserved. */
    GdkWindow *gwindow = gtk_widget_get_window(window);
    gdk_x11_window_set_utf8_property(gwindow, PANEL_MENU_BUS_PROPERTY,
                                    g_dbus_connection_get_unique_name(record->bus));
    gdk_x11_window_set_utf8_property(gwindow, PANEL_MENU_PATH_PROPERTY, record->menu_path);
    gdk_x11_window_set_utf8_property(gwindow, PANEL_MENU_ACTIONS_PROPERTY, record->actions_path);
    g_object_set_data_full(G_OBJECT(window), export_key, record, window_export_free);
    if (!g_object_get_data(G_OBJECT(window), "lunatic-panel-window-destroy-hook")) {
        g_signal_connect(window, "destroy", G_CALLBACK(window_destroy), NULL);
        g_signal_connect(window, "unrealize", G_CALLBACK(window_destroy), NULL);
        g_object_set_data(G_OBJECT(window), "lunatic-panel-window-destroy-hook", GINT_TO_POINTER(1));
    }
}

static gboolean discover(gpointer data)
{
    (void)data;
    discovery_idle = 0;
    if (!session_bus || g_dbus_connection_is_closed(session_bus)) return G_SOURCE_REMOVE;
    GList *windows = gtk_window_list_toplevels();
    for (GList *it = windows; it; it = it->next) {
        GtkWidget *window = it->data;
        if (!gtk_widget_get_realized(window) || gtk_widget_in_destruction(window) ||
            gtk_window_get_window_type(GTK_WINDOW(window)) != GTK_WINDOW_TOPLEVEL ||
            !GDK_IS_X11_WINDOW(gtk_widget_get_window(window))) continue;
        GtkWidget *bar = find_menubar(window, 0);
        if (bar) publish_window(window, bar);
        else window_destroy(window, NULL);
    }
    g_list_free(windows);
    return G_SOURCE_REMOVE;
}

static void schedule_discovery(void)
{
    if (!discovery_idle) discovery_idle = g_idle_add(discover, NULL);
}

static gboolean realized(GSignalInvocationHint *hint, guint count, const GValue *values, gpointer data)
{
    (void)hint; (void)data;
    if (count) {
        GtkWidget *widget = g_value_get_object(values);
        if (GTK_IS_MENU_BAR(widget) ||
            (GTK_IS_WINDOW(widget) && gtk_window_get_window_type(GTK_WINDOW(widget)) == GTK_WINDOW_TOPLEVEL))
            schedule_discovery();
    }
    return TRUE;
}

static void bus_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)object; (void)data;
    GError *error = NULL;
    session_bus = g_bus_get_finish(result, &error);
    if (session_bus) schedule_discovery();
    else { g_debug("Panel GTK exporter: %s", error->message); g_error_free(error); }
}

G_MODULE_EXPORT const char *g_module_check_init(GModule *module)
{
    g_module_make_resident(module);
    return NULL;
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
    (void)argc; (void)argv;
    static gboolean initialized;
    if (initialized) return;
    initialized = TRUE;
    gpointer klass = g_type_class_ref(GTK_TYPE_WIDGET);
    g_signal_add_emission_hook(g_signal_lookup("realize", GTK_TYPE_WIDGET), 0, realized, NULL, NULL);
    g_type_class_unref(klass);
    g_bus_get(G_BUS_TYPE_SESSION, NULL, bus_ready, NULL);
}
