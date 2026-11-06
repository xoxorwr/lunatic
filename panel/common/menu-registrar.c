/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-registrar.h"

struct PanelMenuRegistrar {
    gatomicrefcount references;
    gboolean closing;
    GDBusConnection *connection;
    PanelMenuRegistry *registry;
    guint object_id, owner_id, subscription;
};

static void registrar_unref(gpointer data)
{
    PanelMenuRegistrar *registrar = data;
    if (!g_atomic_ref_count_dec(&registrar->references))
        return;
    panel_menu_registry_free(registrar->registry);
    g_object_unref(registrar->connection);
    g_free(registrar);
}

static const char interface_xml[] =
    "<node><interface name='com.canonical.AppMenu.Registrar'>"
    "<method name='RegisterWindow'><arg type='u' direction='in'/><arg type='o' "
    "direction='in'/></method>"
    "<method name='UnregisterWindow'><arg type='u' direction='in'/></method>"
    "<method name='GetMenuForWindow'><arg type='u' direction='in'/>"
    "<arg type='s' direction='out'/><arg type='o' direction='out'/></method>"
    "<method name='GetMenus'><arg type='a(uso)' direction='out'/></method>"
    "<signal name='WindowRegistered'><arg type='u'/><arg type='s'/><arg type='o'/></signal>"
    "<signal name='WindowUnregistered'><arg type='u'/></signal>"
    "</interface></node>";

static void emit_removed(PanelMenuRegistrar *registrar, guint32 window)
{
    g_dbus_connection_emit_signal(registrar->connection, NULL, PANEL_REGISTRAR_PATH,
                                  PANEL_REGISTRAR_NAME, "WindowUnregistered",
                                  g_variant_new("(u)", window), NULL);
}

static void method_call(GDBusConnection *connection, const char *sender, const char *path,
                        const char *interface, const char *method, GVariant *parameters,
                        GDBusMethodInvocation *invocation, gpointer data)
{
    (void)connection;
    (void)path;
    (void)interface;
    PanelMenuRegistrar *registrar = data;
    if (registrar->closing) {
        g_dbus_method_invocation_return_error_literal(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_DISCONNECTED, "Registrar is stopping");
        return;
    }
    GError *error = NULL;
    guint32 window;
    if (g_str_equal(method, "RegisterWindow")) {
        const char *menu_path;
        g_variant_get(parameters, "(u&o)", &window, &menu_path);
        if (!panel_menu_registry_register(registrar->registry, window, sender, menu_path, &error)) {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
            return;
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
        g_dbus_connection_emit_signal(registrar->connection, NULL, PANEL_REGISTRAR_PATH,
                                      PANEL_REGISTRAR_NAME, "WindowRegistered",
                                      g_variant_new("(uso)", window, sender, menu_path), NULL);
    } else if (g_str_equal(method, "UnregisterWindow")) {
        g_variant_get(parameters, "(u)", &window);
        gboolean existed = panel_menu_registry_lookup(registrar->registry, window) != NULL;
        if (!panel_menu_registry_unregister(registrar->registry, window, sender, &error)) {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
            return;
        }
        g_dbus_method_invocation_return_value(invocation, NULL);
        if (existed)
            emit_removed(registrar, window);
    } else if (g_str_equal(method, "GetMenuForWindow")) {
        g_variant_get(parameters, "(u)", &window);
        const PanelMenuEntry *entry = panel_menu_registry_lookup(registrar->registry, window);
        if (entry)
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(so)", entry->service, entry->path));
        else
            g_dbus_method_invocation_return_dbus_error(
                invocation, "com.canonical.AppMenu.Registrar.Error.NotFound",
                "No menu registered for this window");
    } else if (g_str_equal(method, "GetMenus")) {
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@a(uso))", panel_menu_registry_list(registrar->registry)));
    } else {
        g_dbus_method_invocation_return_error_literal(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown registrar method");
    }
}

static void owner_changed(GDBusConnection *connection, const char *sender, const char *path,
                          const char *interface, const char *signal, GVariant *parameters,
                          gpointer data)
{
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface;
    (void)signal;
    PanelMenuRegistrar *registrar = data;
    if (registrar->closing)
        return;
    const char *name, *old_owner, *new_owner;
    g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
    if (name[0] != ':' || !*old_owner || *new_owner)
        return;
    GArray *removed = panel_menu_registry_remove_owner(registrar->registry, name);
    for (guint i = 0; i < removed->len; i++)
        emit_removed(registrar, g_array_index(removed, guint32, i));
    g_array_unref(removed);
}

PanelMenuRegistrar *panel_menu_registrar_new(GDBusConnection *connection, GError **error)
{
    static const GDBusInterfaceVTable vtable = {.method_call = method_call};
    PanelMenuRegistrar *registrar = g_new0(PanelMenuRegistrar, 1);
    g_atomic_ref_count_init(&registrar->references);
    registrar->connection = g_object_ref(connection);
    registrar->registry = panel_menu_registry_new();
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(interface_xml, error);
    if (!info) {
        panel_menu_registrar_free(registrar);
        return NULL;
    }
    registrar->object_id =
        g_dbus_connection_register_object(connection, PANEL_REGISTRAR_PATH, info->interfaces[0],
                                          &vtable, registrar, registrar_unref, error);
    g_dbus_node_info_unref(info);
    if (!registrar->object_id) {
        panel_menu_registrar_free(registrar);
        return NULL;
    }
    g_atomic_ref_count_inc(&registrar->references);
    registrar->subscription = g_dbus_connection_signal_subscribe(
        connection, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
        "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE, owner_changed, registrar,
        registrar_unref);
    g_atomic_ref_count_inc(&registrar->references);
    registrar->owner_id = g_bus_own_name_on_connection(
        connection, PANEL_REGISTRAR_NAME, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
    return registrar;
}

void panel_menu_registrar_free(PanelMenuRegistrar *registrar)
{
    if (!registrar)
        return;
    registrar->closing = TRUE;
    if (registrar->owner_id)
        g_bus_unown_name(registrar->owner_id);
    if (registrar->subscription)
        g_dbus_connection_signal_unsubscribe(registrar->connection, registrar->subscription);
    if (registrar->object_id)
        g_dbus_connection_unregister_object(registrar->connection, registrar->object_id);
    registrar_unref(registrar);
}
