/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-view.h"
#include "dbus-menu.h"

static void mnemonic_label(GtkWidget *widget, gpointer data)
{
    GtkWidget *bar = data;
    if (GTK_IS_LABEL(widget)) {
        GtkLabel *label = GTK_LABEL(widget);
        if (!gtk_label_get_use_underline(label) || gtk_label_get_use_markup(label)) {
            gtk_label_set_pattern(label, NULL);
            return;
        }
        gboolean visible = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(bar), "lunatic-mnemonics-visible"));
        GString *pattern = g_string_new(NULL);
        if (visible) {
            const char *text = gtk_label_get_label(label);
            for (const char *p = text; *p; p = g_utf8_next_char(p)) {
                gboolean underline = *p == '_' && p[1];
                if (underline) p++;
                g_string_append_c(pattern, underline && *p != '_' ? '_' : ' ');
            }
        }
        gtk_label_set_pattern(label, pattern->str);
        g_string_free(pattern, TRUE);
    } else if (GTK_IS_CONTAINER(widget))
        gtk_container_forall(GTK_CONTAINER(widget), mnemonic_label, bar);
}

static void mnemonic_label_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)pspec; (void)data;
    GtkWidget *bar = gtk_widget_get_ancestor(GTK_WIDGET(object), GTK_TYPE_MENU_BAR);
    if (bar) mnemonic_label(GTK_WIDGET(object), bar);
}

static void watch_mnemonic_label(GtkWidget *widget, gpointer data)
{
    if (GTK_IS_LABEL(widget)) {
        if (!g_object_get_data(G_OBJECT(widget), "lunatic-mnemonic-watched")) {
            g_object_set_data(G_OBJECT(widget), "lunatic-mnemonic-watched", GINT_TO_POINTER(1));
            g_signal_connect(widget, "notify::label", G_CALLBACK(mnemonic_label_changed), NULL);
            g_signal_connect(widget, "notify::use-underline", G_CALLBACK(mnemonic_label_changed), NULL);
        }
        mnemonic_label(widget, data);
    } else if (GTK_IS_CONTAINER(widget))
        gtk_container_forall(GTK_CONTAINER(widget), watch_mnemonic_label, data);
}

static void mnemonic_item_inserted(GtkMenuShell *shell, GtkWidget *item, gint position, gpointer data)
{
    (void)position; (void)data;
    watch_mnemonic_label(item, shell);
}

void panel_menu_bar_set_mnemonics(GtkWidget *bar, gboolean visible)
{
    g_return_if_fail(GTK_IS_MENU_BAR(bar));
    g_object_set_data(G_OBJECT(bar), "lunatic-mnemonics-visible", GINT_TO_POINTER(visible != FALSE));
    if (!g_object_get_data(G_OBJECT(bar), "lunatic-mnemonics-watched")) {
        g_object_set_data(G_OBJECT(bar), "lunatic-mnemonics-watched", GINT_TO_POINTER(1));
        g_signal_connect_after(bar, "insert", G_CALLBACK(mnemonic_item_inserted), NULL);
        gtk_container_forall(GTK_CONTAINER(bar), watch_mnemonic_label, bar);
    } else
        gtk_container_forall(GTK_CONTAINER(bar), mnemonic_label, bar);
}

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
