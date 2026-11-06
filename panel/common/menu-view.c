/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-view.h"
#include "dbus-menu.h"

static gboolean valid_path(const char *path)
{
    return path && g_variant_is_object_path(path);
}

GtkWidget *panel_menu_bar_dbusmenu(GDBusConnection *connection, const char *service, const char *path)
{
    if (!service || !g_dbus_is_name(service) || !valid_path(path))
        return NULL;
    return panel_dbus_menu_bar_new(connection, service, path);
}

GtkWidget *panel_menu_bar_gmenu(GDBusConnection *connection, const char *service,
                                 const char *menubar, const char *appmenu, const char *application,
                                 const char *window)
{
    if (!service || !g_dbus_is_name(service) || (!valid_path(menubar) && !valid_path(appmenu)))
        return NULL;
    GMenu *combined = g_menu_new();
    if (valid_path(appmenu)) {
        GDBusMenuModel *model = g_dbus_menu_model_get(connection, service, appmenu);
        g_menu_append_submenu(combined, "Application", G_MENU_MODEL(model));
        g_object_unref(model);
    }
    if (valid_path(menubar)) {
        GDBusMenuModel *model = g_dbus_menu_model_get(connection, service, menubar);
        g_menu_append_section(combined, NULL, G_MENU_MODEL(model));
        g_object_unref(model);
    }
    GtkWidget *menu = gtk_menu_bar_new();
    if (valid_path(application)) {
        GDBusActionGroup *actions = g_dbus_action_group_get(connection, service, application);
        gtk_widget_insert_action_group(menu, "app", G_ACTION_GROUP(actions));
        g_object_unref(actions);
    }
    if (valid_path(window)) {
        GDBusActionGroup *actions = g_dbus_action_group_get(connection, service, window);
        gtk_widget_insert_action_group(menu, "win", G_ACTION_GROUP(actions));
        g_object_unref(actions);
    }
    /* Submenu trackers must see their action namespaces from creation. */
    gtk_menu_shell_bind_model(GTK_MENU_SHELL(menu), G_MENU_MODEL(combined), NULL, FALSE);
    g_object_unref(combined);
    return menu;
}
