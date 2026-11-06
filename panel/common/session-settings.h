/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gio/gio.h>

#define PANEL_GTK_MODULE_NAME "lunatic-panel-gtk-module"

/* NULL means the property is absent. Migrates our legacy token; unrelated module
 * tokens retain order and spelling.
 * Returns NULL on a non-string property, rather than replacing unknown settings. */
char *panel_menu_module_list(GVariant *current, gboolean enabled, GError **error);
void panel_menu_session_setup_async(GDBusConnection *bus, gboolean enabled,
                                    GCancellable *cancellable, GAsyncReadyCallback callback,
                                    gpointer data);
gboolean panel_menu_session_setup_finish(GDBusConnection *bus, GAsyncResult *result, GError **error);
/* Permanently removing a panel item is distinct from restarting its process.
 * Leave the exporter enabled if another configured instance still needs it. */
gboolean panel_menu_has_other_instance(GVariant *properties, gint removed_id);
gboolean panel_menu_session_remove(GDBusConnection *bus, gint removed_id, GError **error);
/* xfwm4's session-wide server-decoration preference. */
void panel_titleless_set_async(GDBusConnection *bus, gboolean enabled, GCancellable *cancellable,
                               GAsyncReadyCallback callback, gpointer data);
gboolean panel_titleless_set_finish(GDBusConnection *bus, GAsyncResult *result, GError **error);
