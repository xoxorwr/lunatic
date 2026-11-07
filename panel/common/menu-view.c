/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-view.h"
#include "dbus-menu.h"

static void align_shell(GtkWidget *shell);

static gboolean item_has_toggle(GtkWidget *item)
{
    if (!GTK_IS_MENU_ITEM(item)) return FALSE;
    gint width = 0;
    gtk_menu_item_toggle_size_request(GTK_MENU_ITEM(item), &width);
    return width > 0;
}

static void update_menu_gutter(GtkWidget *menu, GtkWidget *inserting)
{
    if (!GTK_IS_MENU(menu)) return;
    /* GtkMenuShell::insert fires before the new row joins the child list. */
    gboolean has_toggle = inserting && item_has_toggle(inserting);
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *it = children; it && !has_toggle; it = it->next)
        has_toggle = item_has_toggle(it->data);
    if (gtk_menu_get_reserve_toggle_size(GTK_MENU(menu)) != has_toggle) {
        gtk_menu_set_reserve_toggle_size(GTK_MENU(menu), has_toggle);
        for (GList *it = children; it; it = it->next)
            gtk_widget_queue_resize(it->data);
        gtk_widget_queue_resize(menu);
    }
    g_list_free(children);
}

static void align_item(GtkWidget *item)
{
    if (!GTK_IS_MENU_ITEM(item) || GTK_IS_SEPARATOR_MENU_ITEM(item)) return;
    GtkWidget *submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
    if (submenu) align_shell(submenu);
    if (!GTK_IS_MENU(gtk_widget_get_parent(item))) return;
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
    if (!child) return;
    if (GTK_IS_LABEL(child)) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        g_object_ref(child);
        gtk_container_remove(GTK_CONTAINER(item), child);
        gtk_box_pack_end(GTK_BOX(box), child, TRUE, TRUE, 0);
        g_object_unref(child);
        gtk_container_add(GTK_CONTAINER(item), box);
        gtk_widget_show(box);
        child = box;
    }
    if (!GTK_IS_BOX(child)) return;
    GtkWidget *image = NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(child));
    for (GList *it = children; it; it = it->next)
        if (GTK_IS_IMAGE(it->data)) { image = it->data; break; }
    g_list_free(children);
    if (!image) {
        /* GtkModelMenuItem keeps iconless labels directly in the item. An empty
         * image gives them the same geometry without inventing an icon. Its
         * normal icon setter can still replace this image on model updates. */
        image = gtk_image_new();
        gtk_box_pack_start(GTK_BOX(child), image, FALSE, FALSE, 0);
        gtk_widget_show(image);
    }
    gtk_widget_set_size_request(image, 16, -1);
    gtk_widget_set_halign(image, GTK_ALIGN_START);
    gtk_box_set_spacing(GTK_BOX(child), 6);
}

static void item_presentation_changed(GObject *object, GParamSpec *spec, gpointer data)
{
    (void)spec; (void)data;
    align_item(GTK_WIDGET(object));
    update_menu_gutter(gtk_widget_get_parent(GTK_WIDGET(object)), GTK_WIDGET(object));
}

static void watch_item(GtkWidget *item)
{
    if (!GTK_IS_MENU_ITEM(item)) return;
    if (!g_object_get_data(G_OBJECT(item), "lunatic-layout-watched")) {
        g_object_set_data(G_OBJECT(item), "lunatic-layout-watched", GINT_TO_POINTER(1));
        g_signal_connect(item, "notify::submenu", G_CALLBACK(item_presentation_changed), NULL);
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(item), "icon"))
            g_signal_connect(item, "notify::icon", G_CALLBACK(item_presentation_changed), NULL);
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(item), "action-role"))
            g_signal_connect(item, "notify::action-role", G_CALLBACK(item_presentation_changed), NULL);
    }
    align_item(item);
}

static void layout_item_inserted(GtkMenuShell *shell, GtkWidget *item, gint position, gpointer data)
{
    (void)position; (void)data;
    watch_item(item);
    update_menu_gutter(GTK_WIDGET(shell), item);
}

static void layout_item_removed(GtkContainer *shell, GtkWidget *item, gpointer data)
{
    (void)item; (void)data;
    update_menu_gutter(GTK_WIDGET(shell), NULL);
}

static void layout_menu_shown(GtkWidget *menu, gpointer data)
{
    (void)data;
    update_menu_gutter(menu, NULL);
}

static void align_shell(GtkWidget *shell)
{
    if (!GTK_IS_MENU_SHELL(shell)) return;
    if (!g_object_get_data(G_OBJECT(shell), "lunatic-layout-watched")) {
        g_object_set_data(G_OBJECT(shell), "lunatic-layout-watched", GINT_TO_POINTER(1));
        g_signal_connect_after(shell, "insert", G_CALLBACK(layout_item_inserted), NULL);
        g_signal_connect_after(shell, "remove", G_CALLBACK(layout_item_removed), NULL);
        g_signal_connect(shell, "show", G_CALLBACK(layout_menu_shown), NULL);
    }
    GList *children = gtk_container_get_children(GTK_CONTAINER(shell));
    for (GList *it = children; it; it = it->next) watch_item(it->data);
    g_list_free(children);
    /* Only menus containing checks/radios need GTK's separate toggle gutter. */
    update_menu_gutter(shell, NULL);
}

void panel_menu_bar_align_items(GtkWidget *bar)
{
    g_return_if_fail(GTK_IS_MENU_BAR(bar));
    align_shell(bar);
}

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
    panel_menu_bar_align_items(menu);
    g_object_unref(combined);
    return menu;
}
