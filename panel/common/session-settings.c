/* SPDX-License-Identifier: GPL-3.0-only */
#include "session-settings.h"
#include <string.h>

#define XFCONF_NAME "org.xfce.Xfconf"
#define XFCONF_PATH "/org/xfce/Xfconf"
#define MODULE_PROPERTY "/Gtk/Modules"

char *panel_menu_module_list(GVariant *current, gboolean enabled, GError **error)
{
    if (current && !g_variant_is_of_type(current, G_VARIANT_TYPE_STRING)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Xfce /Gtk/Modules is not a string; leaving it unchanged");
        return NULL;
    }
    const char *text = current ? g_variant_get_string(current, NULL) : "";
    char **modules = g_strsplit(text, G_SEARCHPATH_SEPARATOR_S, -1);
    GString *merged = g_string_new(NULL);
    gboolean found = FALSE;
    for (guint i = 0; modules[i]; i++) {
        if (!*modules[i]) continue;
        const char *module = modules[i];
        /* Migrate the persisted token from before the Lunatic rename. */
        if (g_str_equal(module, PANEL_GTK_MODULE_NAME) ||
            g_str_equal(module, "unity-panel-gtk-module")) {
            if (!enabled || found) continue;
            found = TRUE;
            module = PANEL_GTK_MODULE_NAME;
        }
        if (merged->len) g_string_append_c(merged, G_SEARCHPATH_SEPARATOR);
        g_string_append(merged, module);
    }
    if (enabled && !found) {
        if (merged->len) g_string_append_c(merged, G_SEARCHPATH_SEPARATOR);
        g_string_append(merged, PANEL_GTK_MODULE_NAME);
    }
    g_strfreev(modules);
    return g_string_free(merged, FALSE);
}

static void written(GObject *object, GAsyncResult *result, gpointer data)
{
    GTask *task = data;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    if (reply) {
        g_variant_unref(reply);
        g_task_return_boolean(task, TRUE);
    } else g_task_return_error(task, error);
    g_object_unref(task);
}

static void read_modules(GObject *object, GAsyncResult *result, gpointer data)
{
    GTask *task = data;
    GDBusConnection *bus = G_DBUS_CONNECTION(object);
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(bus, result, &error);
    GVariant *current = NULL;
    if (reply) {
        g_variant_get(reply, "(v)", &current);
        g_variant_unref(reply);
    } else {
        char *name = g_dbus_error_get_remote_error(error);
        gboolean absent = g_strcmp0(name, XFCONF_NAME ".Error.PropertyNotFound") == 0 ||
                          g_strcmp0(name, XFCONF_NAME ".Error.ChannelNotFound") == 0;
        g_free(name);
        if (!absent) {
            g_task_return_error(task, error);
            g_object_unref(task);
            return;
        }
        g_clear_error(&error);
    }
    gboolean enabled = GPOINTER_TO_INT(g_task_get_task_data(task));
    char *merged = panel_menu_module_list(current, enabled, &error);
    if (!merged) g_task_return_error(task, error);
    else if ((!current && !enabled) || (current && g_str_equal(merged, g_variant_get_string(current, NULL))))
        g_task_return_boolean(task, TRUE);
    else if (!g_task_return_error_if_cancelled(task)) {
        /* Xfconf is already part of Xfce. Use its D-Bus API directly: no new
         * library, subprocess, shell quoting or session environment mutation. */
        g_dbus_connection_call(bus, XFCONF_NAME, XFCONF_PATH, XFCONF_NAME, "SetProperty",
            g_variant_new("(ssv)", "xsettings", MODULE_PROPERTY, g_variant_new_string(merged)),
            G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE, 2000, g_task_get_cancellable(task),
            written, g_object_ref(task));
    }
    g_free(merged);
    g_clear_pointer(&current, g_variant_unref);
    g_object_unref(task);
}

void panel_menu_session_setup_async(GDBusConnection *bus, gboolean enabled,
                                    GCancellable *cancellable, GAsyncReadyCallback callback,
                                    gpointer data)
{
    GTask *task = g_task_new(bus, cancellable, callback, data);
    g_task_set_source_tag(task, panel_menu_session_setup_async);
    g_task_set_task_data(task, GINT_TO_POINTER(enabled), NULL);
    g_dbus_connection_call(bus, XFCONF_NAME, XFCONF_PATH, XFCONF_NAME, "GetProperty",
        g_variant_new("(ss)", "xsettings", MODULE_PROPERTY), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 2000, cancellable, read_modules, task);
}

gboolean panel_menu_session_setup_finish(GDBusConnection *bus, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, bus), FALSE);
    g_return_val_if_fail(g_async_result_is_tagged(result, panel_menu_session_setup_async), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

gboolean panel_menu_has_other_instance(GVariant *properties, gint removed_id)
{
    GVariantIter iter;
    const char *key;
    GVariant *value;
    char *removed = g_strdup_printf("/plugins/plugin-%d", removed_id);
    gboolean found = FALSE;
    g_variant_iter_init(&iter, properties);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
        const char *id = g_str_has_prefix(key, "/plugins/plugin-") ? key + strlen("/plugins/plugin-") : "";
        const char *end = id;
        while (g_ascii_isdigit(*end)) end++;
        found = end != id && !*end && !g_str_equal(key, removed) &&
                g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) &&
                g_str_equal(g_variant_get_string(value, NULL), "lunatic-global-menu");
        g_variant_unref(value);
        if (found) break;
    }
    g_free(removed);
    return found;
}

gboolean panel_menu_session_remove(GDBusConnection *bus, gint removed_id, GError **error)
{
    /* The external plugin process can exit immediately after ::removed.
     * Complete this bounded transaction here; scheduling async writes during
     * teardown can lose them. Ordinary startup remains fully asynchronous. */
    GVariant *reply = g_dbus_connection_call_sync(bus, XFCONF_NAME, XFCONF_PATH, XFCONF_NAME,
        "GetAllProperties", g_variant_new("(ss)", "xfce4-panel", "/plugins"),
        G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, error);
    if (!reply) return FALSE;
    GVariant *properties = g_variant_get_child_value(reply, 0);
    gboolean other = panel_menu_has_other_instance(properties, removed_id);
    g_variant_unref(properties);
    g_variant_unref(reply);
    if (other) return TRUE;

    reply = g_dbus_connection_call_sync(bus, XFCONF_NAME, XFCONF_PATH, XFCONF_NAME,
        "GetProperty", g_variant_new("(ss)", "xsettings", MODULE_PROPERTY),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, error);
    if (!reply) {
        char *name = error && *error ? g_dbus_error_get_remote_error(*error) : NULL;
        gboolean absent = g_strcmp0(name, XFCONF_NAME ".Error.PropertyNotFound") == 0 ||
                          g_strcmp0(name, XFCONF_NAME ".Error.ChannelNotFound") == 0;
        g_free(name);
        if (absent) g_clear_error(error);
        return absent;
    }
    GVariant *current;
    g_variant_get(reply, "(v)", &current);
    g_variant_unref(reply);
    char *merged = panel_menu_module_list(current, FALSE, error);
    if (!merged) {
        g_variant_unref(current);
        return FALSE;
    }
    gboolean unchanged = g_str_equal(merged, g_variant_get_string(current, NULL));
    g_variant_unref(current);
    if (unchanged) {
        g_free(merged);
        return TRUE;
    }
    reply = g_dbus_connection_call_sync(bus, XFCONF_NAME, XFCONF_PATH, XFCONF_NAME,
        "SetProperty", g_variant_new("(ssv)", "xsettings", MODULE_PROPERTY, g_variant_new_string(merged)),
        G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, error);
    g_free(merged);
    if (!reply) return FALSE;
    g_variant_unref(reply);
    return TRUE;
}

void panel_titleless_set_async(GDBusConnection *bus, gboolean enabled, GCancellable *cancellable,
                               GAsyncReadyCallback callback, gpointer data)
{
    GTask *task = g_task_new(bus, cancellable, callback, data);
    g_task_set_source_tag(task, panel_titleless_set_async);
    g_dbus_connection_call(bus, XFCONF_NAME, XFCONF_PATH, XFCONF_NAME, "SetProperty",
        g_variant_new("(ssv)", "xfwm4", "/general/titleless_maximize", g_variant_new_boolean(enabled)),
        G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE, 2000, cancellable, written, task);
}

gboolean panel_titleless_set_finish(GDBusConnection *bus, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(g_task_is_valid(result, bus), FALSE);
    g_return_val_if_fail(g_async_result_is_tagged(result, panel_titleless_set_async), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}
