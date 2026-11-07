/* SPDX-License-Identifier: GPL-3.0-only */
#include "gtk-menu-export.h"
#include "menu-model.h"
#include <string.h>

typedef struct {
    GtkWidget *widget;
    PanelMenuModel *menu;
    gboolean prepared;
} Shell;
typedef struct {
    GWeakRef item, lifetime;
    gboolean submenu;
} Binding;
struct PanelGtkMenuExport {
    GtkMenuBar *menubar;
    GSimpleActionGroup *actions;
    GObject *lifetime;
    GHashTable *shells, *seen_shells, *seen_actions;
    GPtrArray *watched;
    guint idle;
    gboolean rebuilding, dirty;
};

static void changed(PanelGtkMenuExport *exporter);

static const char *item_id(GtkWidget *item)
{
    static guint64 next_id;
    char *id = g_object_get_data(G_OBJECT(item), "lunatic-panel-menu-id");
    if (!id) {
        id = g_strdup_printf("item%" G_GUINT64_FORMAT, ++next_id);
        g_object_set_data_full(G_OBJECT(item), "lunatic-panel-menu-id", id, g_free);
    }
    return id;
}

static void binding_free(gpointer data, GClosure *closure)
{
    (void)closure;
    Binding *binding = data;
    g_weak_ref_clear(&binding->item);
    g_weak_ref_clear(&binding->lifetime);
    g_free(binding);
}

static void sync_state(GSimpleAction *action, GtkWidget *item)
{
    g_simple_action_set_enabled(action, gtk_widget_is_sensitive(item));
    if (GTK_IS_RADIO_MENU_ITEM(item))
        g_simple_action_set_state(action, g_variant_new_string(
            gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)) ? item_id(item) : ""));
    else if (GTK_IS_CHECK_MENU_ITEM(item))
        g_simple_action_set_state(action, g_variant_new_boolean(
            gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))));
}

static void activate(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)parameter;
    Binding *binding = data;
    GObject *lifetime = g_weak_ref_get(&binding->lifetime);
    GtkWidget *item = g_weak_ref_get(&binding->item);
    if (lifetime && item && gtk_widget_get_parent(item) && gtk_widget_get_visible(item) &&
        gtk_widget_is_sensitive(item)) {
        gtk_menu_item_activate(GTK_MENU_ITEM(item));
        sync_state(action, item);
    }
    g_clear_object(&item);
    g_clear_object(&lifetime);
}

static void populate_menu(GtkWidget *menu)
{
    gboolean was_visible = gtk_widget_get_visible(menu);
    g_signal_emit_by_name(menu, "show");
    /* Emitting show runs GTK's default handler as well as application hooks.
     * Restore hidden source menus so later local opens still emit show. */
    if (!was_visible && !gtk_widget_in_destruction(menu)) gtk_widget_hide(menu);
}

static void change_state(GSimpleAction *action, GVariant *value, gpointer data)
{
    Binding *binding = data;
    GObject *lifetime = g_weak_ref_get(&binding->lifetime);
    GtkWidget *item = g_weak_ref_get(&binding->item);
    if (lifetime && item && gtk_widget_get_parent(item) && gtk_widget_is_sensitive(item)) {
        if (binding->submenu) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
                GVariant *state = g_action_get_state(G_ACTION(action));
                gboolean unchanged = g_variant_equal(state, value);
                g_variant_unref(state);
                if (!unchanged) {
                    GtkWidget *menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
                    /* Populate and publish before acknowledging the open request.
                     * The source menu is not popped up or given a grab. */
                    if (menu && g_variant_get_boolean(value)) {
                        populate_menu(menu);
                        PanelGtkMenuExport *exporter =
                            g_object_get_data(lifetime, "lunatic-panel-exporter");
                        if (exporter) panel_gtk_menu_export_rebuild(exporter);
                    }
                    g_simple_action_set_state(action, value);
                }
            }
        } else if (GTK_IS_RADIO_MENU_ITEM(item)) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) &&
                g_str_equal(g_variant_get_string(value, NULL), item_id(item)) &&
                !gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
                gtk_menu_item_activate(GTK_MENU_ITEM(item));
            sync_state(action, item);
        } else if (GTK_IS_CHECK_MENU_ITEM(item)) {
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN) &&
                g_variant_get_boolean(value) != gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
                gtk_menu_item_activate(GTK_MENU_ITEM(item));
            sync_state(action, item);
        }
    }
    g_clear_object(&item);
    g_clear_object(&lifetime);
}

static char *ensure_action(PanelGtkMenuExport *exporter, GtkWidget *item, gboolean submenu)
{
    char *name = g_strconcat(submenu ? "open-" : "", item_id(item), NULL);
    GAction *existing = g_action_map_lookup_action(G_ACTION_MAP(exporter->actions), name);
    if (!existing) {
        GSimpleAction *action;
        if (submenu || GTK_IS_CHECK_MENU_ITEM(item)) {
            gboolean radio = !submenu && GTK_IS_RADIO_MENU_ITEM(item);
            action = g_simple_action_new_stateful(name, radio ? G_VARIANT_TYPE_STRING : NULL,
                radio ? g_variant_new_string("") : g_variant_new_boolean(FALSE));
        } else action = g_simple_action_new(name, NULL);
        Binding *binding = g_new0(Binding, 1);
        g_weak_ref_init(&binding->item, item);
        g_weak_ref_init(&binding->lifetime, exporter->lifetime);
        binding->submenu = submenu;
        g_signal_connect_data(action, "change-state", G_CALLBACK(change_state), binding, binding_free, 0);
        if (!submenu) g_signal_connect(action, "activate", G_CALLBACK(activate), binding);
        g_action_map_add_action(G_ACTION_MAP(exporter->actions), G_ACTION(action));
        g_object_unref(action);
        existing = g_action_map_lookup_action(G_ACTION_MAP(exporter->actions), name);
    }
    if (submenu) g_simple_action_set_enabled(G_SIMPLE_ACTION(existing), gtk_widget_is_sensitive(item));
    else sync_state(G_SIMPLE_ACTION(existing), item);
    g_hash_table_add(exporter->seen_actions, g_strdup(name));
    char *detailed = g_strconcat("app.", name, NULL);
    g_free(name);
    return detailed;
}

static void shell_free(gpointer data)
{
    Shell *shell = data;
    panel_menu_model_free(shell->menu);
    g_object_unref(shell->widget);
    g_free(shell);
}

static void item_notify(GObject *object, GParamSpec *spec, gpointer data)
{
    if (GTK_IS_IMAGE(object)) { changed(data); return; }
    static const char *const properties[] = {"label", "use-underline", "sensitive", "visible",
                                            "active", "inconsistent", "submenu", "image"};
    for (guint i = 0; i < G_N_ELEMENTS(properties); i++)
        if (g_str_equal(spec->name, properties[i])) { changed(data); break; }
}

static void child_changed(GtkContainer *container, GtkWidget *child, gpointer data)
{
    (void)container; (void)child;
    changed(data);
}

static void child_inserted(GtkMenuShell *shell, GtkWidget *child, gint position, gpointer data)
{
    (void)shell; (void)child; (void)position;
    changed(data);
}

static void widget_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    changed(data);
}

static void watch(PanelGtkMenuExport *exporter, GtkWidget *widget)
{
    g_ptr_array_add(exporter->watched, g_object_ref(widget));
    g_signal_connect(widget, "destroy", G_CALLBACK(widget_destroyed), exporter);
    if (GTK_IS_MENU_SHELL(widget)) {
        g_signal_connect(widget, "insert", G_CALLBACK(child_inserted), exporter);
        g_signal_connect(widget, "remove", G_CALLBACK(child_changed), exporter);
    }
    if (GTK_IS_MENU_ITEM(widget) || GTK_IS_IMAGE(widget))
        g_signal_connect(widget, "notify", G_CALLBACK(item_notify), exporter);
}

static void find_image(GtkWidget *widget, gpointer data)
{
    GtkWidget **image = data;
    if (*image) return;
    if (GTK_IS_IMAGE(widget)) *image = widget;
    else if (GTK_IS_CONTAINER(widget) && !GTK_IS_MENU_SHELL(widget))
        /* Include GtkImageMenuItem's internal image, even when GTK hides it. */
        gtk_container_forall(GTK_CONTAINER(widget), find_image, data);
}

static void copy_icon(PanelGtkMenuExport *exporter, GtkWidget *widget, GMenuItem *item)
{
    GtkWidget *image = NULL;
    find_image(widget, &image);
    if (!image) return;
    watch(exporter, image);
    GIcon *icon = NULL;
    GdkPixbuf *pixbuf = NULL;
    switch (gtk_image_get_storage_type(GTK_IMAGE(image))) {
    case GTK_IMAGE_ICON_NAME: {
        const char *name = NULL;
        gtk_image_get_icon_name(GTK_IMAGE(image), &name, NULL);
        if (name && *name) icon = g_themed_icon_new(name);
        break;
    }
    case GTK_IMAGE_GICON:
        gtk_image_get_gicon(GTK_IMAGE(image), &icon, NULL);
        if (icon) g_object_ref(icon);
        break;
    case GTK_IMAGE_PIXBUF:
        pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(image));
        if (pixbuf) g_object_ref(pixbuf);
        break;
    case GTK_IMAGE_SURFACE: {
        cairo_surface_t *surface = NULL;
        g_object_get(image, "surface", &surface, NULL);
        if (surface && cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE)
            pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0,
                cairo_image_surface_get_width(surface), cairo_image_surface_get_height(surface));
        if (surface) cairo_surface_destroy(surface);
        break;
    }
    case GTK_IMAGE_STOCK: {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        char *stock = NULL;
        GtkIconSize size;
        gtk_image_get_stock(GTK_IMAGE(image), &stock, &size);
        if (stock) pixbuf = gtk_widget_render_icon_pixbuf(image, stock, size);
        G_GNUC_END_IGNORE_DEPRECATIONS
        break;
    }
    default:
        break;
    }
    if (pixbuf) {
        char *buffer = NULL;
        gsize length = 0;
        if (gdk_pixbuf_save_to_buffer(pixbuf, &buffer, &length, "png", NULL, NULL)) {
            GBytes *bytes = g_bytes_new_take(buffer, length);
            icon = g_bytes_icon_new(bytes);
            g_bytes_unref(bytes);
        }
        g_object_unref(pixbuf);
    }
    if (icon) {
        g_menu_item_set_icon(item, icon);
        g_object_unref(icon);
    }
}

static void find_accel_label(GtkWidget *widget, gpointer data)
{
    GtkWidget **label = data;
    if (*label) return;
    if (GTK_IS_ACCEL_LABEL(widget)) *label = widget;
    else if (GTK_IS_CONTAINER(widget) && !GTK_IS_MENU_SHELL(widget))
        gtk_container_forall(GTK_CONTAINER(widget), find_accel_label, data);
}

static gboolean same_closure(GtkAccelKey *key, GClosure *closure, gpointer data)
{
    return closure == data && key->accel_key && (key->accel_flags & GTK_ACCEL_VISIBLE);
}

static void copy_accel(GtkWidget *widget, GMenuItem *item)
{
    GtkAccelKey key = {0};
    GtkWidget *label = NULL;
    find_accel_label(widget, &label);
    /* Xfce explicitly sets the label's accelerator without an item accel path. */
    if (label) gtk_accel_label_get_accel(GTK_ACCEL_LABEL(label), &key.accel_key, &key.accel_mods);
    const char *path = gtk_menu_item_get_accel_path(GTK_MENU_ITEM(widget));
    if (!key.accel_key && path) gtk_accel_map_lookup_entry(path, &key);
    if (!key.accel_key) {
        GList *closures = gtk_widget_list_accel_closures(widget);
        for (GList *it = closures; it && !key.accel_key; it = it->next) {
            GtkAccelGroup *group = gtk_accel_group_from_accel_closure(it->data);
            GtkAccelKey *found = group ? gtk_accel_group_find(group, same_closure, it->data) : NULL;
            if (found) key = *found;
        }
        g_list_free(closures);
    }
    if (key.accel_key) {
        char *accel = gtk_accelerator_name(key.accel_key, key.accel_mods);
        g_menu_item_set_attribute(item, "accel", "s", accel);
        g_free(accel);
    }
}

static Shell *build_shell(PanelGtkMenuExport *exporter, GtkWidget *widget, guint depth)
{
    if (depth > 32 || g_hash_table_contains(exporter->seen_shells, widget)) return NULL;
    Shell *shell = g_hash_table_lookup(exporter->shells, widget);
    if (!shell) {
        shell = g_new0(Shell, 1);
        shell->widget = g_object_ref(widget);
        shell->menu = panel_menu_model_new();
        g_hash_table_insert(exporter->shells, widget, shell);
    }
    g_hash_table_add(exporter->seen_shells, widget);
    /* Some GtkMenu consumers cannot open an initially empty submenu, so the
     * remote show action alone cannot bootstrap Thunar's lazy menus. Prepare
     * each empty source menu once, before publishing its first model. Never do
     * this on every rebuild: show callbacks often destroy/recreate children. */
    if (!shell->prepared) {
        shell->prepared = TRUE;
        if (GTK_IS_MENU(widget)) {
            GList *initial = gtk_container_get_children(GTK_CONTAINER(widget));
            gboolean empty = initial == NULL;
            g_list_free(initial);
            guint show = g_signal_lookup("show", GTK_TYPE_WIDGET);
            if (empty && g_signal_has_handler_pending(widget, show, 0, TRUE))
                populate_menu(widget);
        }
    }
    watch(exporter, widget);
    PanelMenuBuilder builder;
    panel_menu_builder_init(&builder, shell->menu);
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (GList *it = children; it; it = it->next) {
        GtkWidget *child = it->data;
        if (!GTK_IS_MENU_ITEM(child)) continue;
        watch(exporter, child);
        if (!gtk_widget_get_visible(child)) continue;
        if (GTK_IS_SEPARATOR_MENU_ITEM(child)) { panel_menu_builder_separator(&builder); continue; }
        const char *label = gtk_menu_item_get_label(GTK_MENU_ITEM(child));
        /* Thunar also puts a spinner inside a blank, disabled menu item. */
        if (!label || !*label) continue;
        char *escaped = NULL;
        if (!gtk_menu_item_get_use_underline(GTK_MENU_ITEM(child))) {
            GString *text = g_string_new(NULL);
            for (const char *c = label ? label : ""; *c; c++) {
                g_string_append_c(text, *c);
                if (*c == '_') g_string_append_c(text, '_');
            }
            escaped = g_string_free(text, FALSE);
            label = escaped;
        }
        GMenuItem *item = g_menu_item_new(label ? label : "", NULL);
        copy_icon(exporter, child, item);
        GtkWidget *submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(child));
        char *action = ensure_action(exporter, child, submenu != NULL);
        if (submenu && GTK_IS_MENU_SHELL(submenu)) {
            Shell *nested = build_shell(exporter, submenu, depth + 1);
            if (!nested) {
                g_object_unref(item);
                g_free(action);
                g_free(escaped);
                continue;
            }
            g_menu_item_set_submenu(item, G_MENU_MODEL(nested->menu->model));
            g_menu_item_set_attribute(item, "submenu-action", "s", action);
        } else {
            g_menu_item_set_action_and_target_value(item, action,
                GTK_IS_RADIO_MENU_ITEM(child) ? g_variant_new_string(item_id(child)) : NULL);
        }
        copy_accel(child, item);
        panel_menu_builder_append(&builder, item);
        g_object_unref(item);
        g_free(action);
        g_free(escaped);
    }
    g_list_free(children);
    panel_menu_builder_finish(&builder);
    return shell;
}

static void disconnect_widgets(PanelGtkMenuExport *exporter)
{
    for (guint i = 0; i < exporter->watched->len; i++)
        g_signal_handlers_disconnect_by_data(g_ptr_array_index(exporter->watched, i), exporter);
    g_ptr_array_set_size(exporter->watched, 0);
}

static gboolean rebuild_idle(gpointer data)
{
    PanelGtkMenuExport *exporter = data;
    exporter->idle = 0;
    panel_gtk_menu_export_rebuild(exporter);
    return G_SOURCE_REMOVE;
}

static void changed(PanelGtkMenuExport *exporter)
{
    if (exporter->rebuilding) { exporter->dirty = TRUE; return; }
    if (!exporter->idle) exporter->idle = g_idle_add(rebuild_idle, exporter);
}

void panel_gtk_menu_export_rebuild(PanelGtkMenuExport *exporter)
{
    if (exporter->rebuilding) return;
    if (exporter->idle) { g_source_remove(exporter->idle); exporter->idle = 0; }
    exporter->rebuilding = TRUE;
    exporter->dirty = FALSE;
    disconnect_widgets(exporter);
    g_hash_table_remove_all(exporter->seen_shells);
    g_hash_table_remove_all(exporter->seen_actions);
    build_shell(exporter, GTK_WIDGET(exporter->menubar), 0);
    char **actions = g_action_group_list_actions(G_ACTION_GROUP(exporter->actions));
    for (guint i = 0; actions[i]; i++)
        if (!g_hash_table_contains(exporter->seen_actions, actions[i]))
            g_action_map_remove_action(G_ACTION_MAP(exporter->actions), actions[i]);
    g_strfreev(actions);
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, exporter->shells);
    while (g_hash_table_iter_next(&iter, &key, NULL))
        if (!g_hash_table_contains(exporter->seen_shells, key)) g_hash_table_iter_remove(&iter);
    exporter->rebuilding = FALSE;
    if (exporter->dirty) changed(exporter);
}

PanelGtkMenuExport *panel_gtk_menu_export_new(GtkMenuBar *menubar)
{
    PanelGtkMenuExport *exporter = g_new0(PanelGtkMenuExport, 1);
    exporter->menubar = g_object_ref(menubar);
    exporter->actions = g_simple_action_group_new();
    exporter->lifetime = g_object_new(G_TYPE_OBJECT, NULL);
    g_object_set_data(exporter->lifetime, "lunatic-panel-exporter", exporter);
    exporter->shells = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, shell_free);
    exporter->seen_shells = g_hash_table_new(g_direct_hash, g_direct_equal);
    exporter->seen_actions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    exporter->watched = g_ptr_array_new_with_free_func(g_object_unref);
    panel_gtk_menu_export_rebuild(exporter);
    return exporter;
}

GMenuModel *panel_gtk_menu_export_model(PanelGtkMenuExport *exporter)
{
    Shell *shell = g_hash_table_lookup(exporter->shells, exporter->menubar);
    return G_MENU_MODEL(shell->menu->model);
}

GActionGroup *panel_gtk_menu_export_actions(PanelGtkMenuExport *exporter)
{
    return G_ACTION_GROUP(exporter->actions);
}

void panel_gtk_menu_export_free(PanelGtkMenuExport *exporter)
{
    if (exporter->idle) g_source_remove(exporter->idle);
    g_object_set_data(exporter->lifetime, "lunatic-panel-exporter", NULL);
    g_clear_object(&exporter->lifetime);
    disconnect_widgets(exporter);
    g_ptr_array_unref(exporter->watched);
    g_hash_table_unref(exporter->shells);
    g_hash_table_unref(exporter->seen_shells);
    g_hash_table_unref(exporter->seen_actions);
    g_object_unref(exporter->actions);
    g_object_unref(exporter->menubar);
    g_free(exporter);
}
