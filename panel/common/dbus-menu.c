/* SPDX-License-Identifier: GPL-3.0-only */
#include "dbus-menu.h"
#include "remote-menu.h"
#include "menu-view.h"
#define INTERFACE "com.canonical.dbusmenu"

typedef struct {
    gatomicrefcount refs;
    GDBusConnection *bus;
    GCancellable *cancel;
    PanelRemoteMenu *model;
    char *service, *path;
    guint subscription, idle;
    gboolean closing, fetching, dirty;
} Client;
static void refresh(Client *client);
static Client *client_ref(Client *client) { g_atomic_ref_count_inc(&client->refs); return client; }
static void client_unref(gpointer data)
{
    Client *client = data;
    if (!g_atomic_ref_count_dec(&client->refs)) return;
    g_object_unref(client->bus);
    g_object_unref(client->cancel);
    g_object_unref(client->model);
    g_free(client->service);
    g_free(client->path);
    g_free(client);
}
static void layout_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Client *client = data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    client->fetching = FALSE;
    if (!client->closing && reply) {
        GVariant *layout = g_variant_get_child_value(reply, 1);
        panel_remote_menu_update(client->model, layout);
        g_variant_unref(layout);
    }
    g_clear_pointer(&reply, g_variant_unref);
    g_clear_error(&error);
    if (!client->closing && client->dirty) refresh(client);
    client_unref(client);
}
static gboolean fetch(gpointer data)
{
    Client *client = data;
    client->idle = 0;
    client->fetching = TRUE;
    client->dirty = FALSE;
    g_dbus_connection_call(client->bus, client->service, client->path, INTERFACE, "GetLayout",
        g_variant_new("(ii@as)", 0, -1, g_variant_new_strv(NULL, 0)),
        G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 1500,
        client->cancel, layout_ready, client_ref(client));
    return G_SOURCE_REMOVE;
}
static void refresh(Client *client)
{
    if (client->closing) return;
    if (client->fetching) { client->dirty = TRUE; return; }
    if (!client->idle) client->idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, fetch,
                                                    client_ref(client), client_unref);
}
static void updated(GDBusConnection *bus, const char *sender, const char *path,
                    const char *interface, const char *signal, GVariant *parameters, gpointer data)
{
    (void)bus; (void)sender; (void)path; (void)interface; (void)parameters;
    if (g_str_equal(signal, "LayoutUpdated") || g_str_equal(signal, "ItemsPropertiesUpdated")) refresh(data);
}
static void about_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    Client *client = data;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, NULL);
    if (reply) {
        gboolean changed;
        g_variant_get(reply, "(b)", &changed);
        if (changed) refresh(client);
        g_variant_unref(reply);
    }
    client_unref(client);
}
static void about_to_show(Client *client, gint32 id)
{
    g_dbus_connection_call(client->bus, client->service, client->path, INTERFACE, "AboutToShow",
        g_variant_new("(i)", id), G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
        1500, client->cancel, about_ready, client_ref(client));
}
static void event(gint32 id, const char *event_name, gpointer data)
{
    Client *client = data;
    if (client->closing) return;
    if (g_str_equal(event_name, "opened")) about_to_show(client, id);
    g_dbus_connection_call(client->bus, client->service, client->path, INTERFACE, "Event",
        g_variant_new("(isvu)", id, event_name, g_variant_new_int32(0), gtk_get_current_event_time()),
        NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1500, client->cancel, NULL, NULL);
}
static void show(GtkWidget *menu, gpointer data)
{
    (void)menu;
    Client *client = data;
    about_to_show(client, 0);
    refresh(client);
}
static void destroy(GtkWidget *menu, gpointer data)
{
    Client *client = data;
    client->closing = TRUE;
    g_signal_handlers_disconnect_by_data(menu, client);
    panel_remote_menu_set_event(client->model, NULL, NULL);
    g_cancellable_cancel(client->cancel);
    if (client->idle) { g_source_remove(client->idle); client->idle = 0; }
    g_dbus_connection_signal_unsubscribe(client->bus, client->subscription);
    client_unref(client);
}
GtkWidget *panel_dbus_menu_bar_new(GDBusConnection *bus, const char *service, const char *path)
{
    Client *client = g_new0(Client, 1);
    g_atomic_ref_count_init(&client->refs);
    client->bus = g_object_ref(bus);
    client->cancel = g_cancellable_new();
    client->model = panel_remote_menu_new();
    client->service = g_strdup(service);
    client->path = g_strdup(path);
    panel_remote_menu_set_event(client->model, event, client);
    GtkWidget *menu = gtk_menu_bar_new();
    gtk_widget_insert_action_group(menu, "remote", panel_remote_menu_actions(client->model));
    gtk_menu_shell_bind_model(GTK_MENU_SHELL(menu), panel_remote_menu_model(client->model), NULL, FALSE);
    panel_menu_bar_align_items(menu);
    g_signal_connect(menu, "destroy", G_CALLBACK(destroy), client);
    g_signal_connect(menu, "show", G_CALLBACK(show), client);
    client->subscription = g_dbus_connection_signal_subscribe(bus, service, INTERFACE, NULL,
        path, NULL, G_DBUS_SIGNAL_FLAGS_NONE, updated, client_ref(client), client_unref);
    refresh(client);
    return menu;
}
