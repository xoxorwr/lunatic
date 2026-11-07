/* SPDX-License-Identifier: GPL-3.0-only */
#include "gtk-menu-export.h"
#include "title-label.h"

static char *find_attribute(GMenuModel *model, const char *label, const char *attribute)
{
    for (int i = 0; i < g_menu_model_get_n_items(model); i++) {
        char *text = NULL, *value = NULL;
        if (g_menu_model_get_item_attribute(model, i, "label", "s", &text) && g_str_equal(text, label))
            g_menu_model_get_item_attribute(model, i, attribute, "s", &value);
        g_free(text);
        if (value) return value;
        const char *links[] = {G_MENU_LINK_SECTION, G_MENU_LINK_SUBMENU};
        for (guint j = 0; j < G_N_ELEMENTS(links); j++) {
            GMenuModel *nested = g_menu_model_get_item_link(model, i, links[j]);
            if (!nested) continue;
            value = find_attribute(nested, label, attribute);
            g_object_unref(nested);
            if (value) return value;
        }
    }
    return NULL;
}
static void clicked(GtkMenuItem *item, gpointer data) { (void)item; (*(guint *)data)++; }
static void populate(GtkWidget *menu, gpointer data)
{
    (void)data;
    if (g_object_get_data(G_OBJECT(menu), "populated")) return;
    g_object_set_data(G_OBJECT(menu), "populated", GINT_TO_POINTER(1));
    GtkWidget *child = gtk_menu_item_new_with_label("Lazy child");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), child);
    gtk_widget_show(child);
}
static void bridge(void)
{
    GtkWidget *bar = g_object_ref_sink(gtk_menu_bar_new());
    GtkWidget *file = gtk_menu_item_new_with_label("File");
    GtkWidget *menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), file);
    GtkWidget *check = gtk_check_menu_item_new_with_label("Check");
    GtkWidget *radio = gtk_radio_menu_item_new_with_label(NULL, "Radio");
    GtkWidget *radio2 = gtk_radio_menu_item_new_with_label_from_widget(GTK_RADIO_MENU_ITEM(radio), "Radio2");
    GtkWidget *run = gtk_menu_item_new_with_label("Run");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), check);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), radio);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), radio2);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), run);
    g_signal_connect(menu, "show", G_CALLBACK(populate), NULL);
    gtk_widget_show_all(bar);
    guint clicks = 0;
    g_signal_connect(run, "activate", G_CALLBACK(clicked), &clicks);
    PanelGtkMenuExport *exporter = panel_gtk_menu_export_new(GTK_MENU_BAR(bar));
    GMenuModel *model = panel_gtk_menu_export_model(exporter);
    GActionGroup *actions = panel_gtk_menu_export_actions(exporter);
    char *name = find_attribute(model, "Run", "action");
    g_assert_nonnull(name);
    g_action_group_activate_action(actions, name + 4, NULL);
    g_assert_cmpuint(clicks, ==, 1);
    GAction *retained = g_object_ref(g_action_map_lookup_action(G_ACTION_MAP(actions), name + 4));
    g_free(name);
    name = find_attribute(model, "Check", "action");
    g_assert_nonnull(name);
    g_action_group_activate_action(actions, name + 4, NULL);
    g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(check)));
    g_action_group_change_action_state(actions, name + 4, g_variant_new_boolean(FALSE));
    g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(check)));
    g_free(name);
    name = find_attribute(model, "Radio2", "action");
    char *target = find_attribute(model, "Radio2", "target");
    g_assert_nonnull(name);
    g_assert_nonnull(target);
    g_action_group_activate_action(actions, name + 4, g_variant_new_string(target));
    g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(radio2)));
    g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(radio)));
    g_free(name);
    g_free(target);
    name = find_attribute(model, "File", "submenu-action");
    g_assert_nonnull(name);
    g_action_group_change_action_state(actions, name + 4, g_variant_new_boolean(TRUE));
    /* Population must schedule its own update via GtkMenuShell::insert. */
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
    g_free(name);
    name = find_attribute(model, "Lazy child", "action");
    g_assert_nonnull(name);
    g_free(name);
    gtk_widget_hide(run);
    panel_gtk_menu_export_rebuild(exporter);
    g_assert_null(find_attribute(model, "Run", "action"));
    gtk_widget_show(run);
    panel_gtk_menu_export_rebuild(exporter);
    gtk_widget_set_sensitive(run, FALSE);
    panel_gtk_menu_export_rebuild(exporter);
    name = find_attribute(model, "Run", "action");
    g_assert_false(g_action_group_get_action_enabled(actions, name + 4));
    g_free(name);
    gtk_widget_set_sensitive(run, TRUE);
    panel_gtk_menu_export_free(exporter);
    g_action_activate(retained, NULL);
    g_assert_cmpuint(clicks, ==, 1);
    g_object_unref(retained);
    gtk_widget_destroy(bar);
    g_object_unref(bar);
}
typedef struct { guint file_shows, nested_shows, clicks; } ColdMenu;

static void populate_nested(GtkWidget *menu, gpointer data)
{
    ColdMenu *cold = data;
    cold->nested_shows++;
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *it = children; it; it = it->next) gtk_widget_destroy(it->data);
    g_list_free(children);
    GtkWidget *item = gtk_menu_item_new_with_label("Generated action");
    g_signal_connect(item, "activate", G_CALLBACK(clicked), &cold->clicks);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
}
static void populate_file(GtkWidget *menu, gpointer data)
{
    ColdMenu *cold = data;
    cold->file_shows++;
    /* Thunar replaces children on each opening, rather than filling once. */
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *it = children; it; it = it->next) gtk_widget_destroy(it->data);
    g_list_free(children);
    GtkWidget *item = gtk_menu_item_new_with_label("Nested");
    GtkWidget *nested = gtk_menu_new();
    g_signal_connect(nested, "show", G_CALLBACK(populate_nested), cold);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), nested);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
}
static GtkWidget *first_child(GtkWidget *menu)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    g_assert_nonnull(children);
    GtkWidget *child = children->data;
    g_list_free(children);
    return child;
}

static GIcon *find_icon(GMenuModel *model)
{
    for (int i = 0; i < g_menu_model_get_n_items(model); i++) {
        GVariant *value = g_menu_model_get_item_attribute_value(model, i, G_MENU_ATTRIBUTE_ICON, NULL);
        if (value) {
            GIcon *icon = g_icon_deserialize(value);
            g_variant_unref(value);
            return icon;
        }
        GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
        if (section) {
            GIcon *icon = find_icon(section);
            g_object_unref(section);
            if (icon) return icon;
        }
    }
    return NULL;
}

static void find_display_image(GtkWidget *widget, gpointer data)
{
    GtkWidget **image = data;
    if (GTK_IS_IMAGE(widget)) *image = widget;
    else if (GTK_IS_CONTAINER(widget))
        gtk_container_forall(GTK_CONTAINER(widget), find_display_image, data);
}

static void menu_icons(void)
{
    GtkWidget *bar = g_object_ref_sink(gtk_menu_bar_new());
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *item = gtk_image_menu_item_new_with_label("Open");
    GtkWidget *image = gtk_image_new_from_icon_name("document-open", GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    gtk_widget_show(item);
    /* Source images can be hidden by GTK's legacy menu-image preference. */
    gtk_widget_hide(image);
    PanelGtkMenuExport *exporter = panel_gtk_menu_export_new(GTK_MENU_BAR(bar));
    GMenuModel *model = panel_gtk_menu_export_model(exporter);
    GIcon *expected = g_themed_icon_new("document-open");
    GIcon *icon = find_icon(model);
    g_assert_nonnull(icon);
    g_assert_true(g_icon_equal(icon, expected));
    g_object_unref(icon);
    g_object_unref(expected);

    GtkWidget *view = g_object_ref_sink(gtk_menu_new());
    gtk_widget_insert_action_group(view, "app", panel_gtk_menu_export_actions(exporter));
    gtk_menu_shell_bind_model(GTK_MENU_SHELL(view), model, NULL, FALSE);
    GtkWidget *display_image = NULL;
    find_display_image(view, &display_image);
    g_assert_nonnull(display_image);
    g_assert_true(gtk_widget_get_visible(display_image));

    /* Changes must propagate without explicitly rebuilding the export. */
    expected = g_themed_icon_new("document-save");
    gtk_image_set_from_gicon(GTK_IMAGE(image), expected, GTK_ICON_SIZE_MENU);
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
    icon = find_icon(model);
    g_assert_nonnull(icon);
    g_assert_true(g_icon_equal(icon, expected));
    g_object_unref(icon);
    g_object_unref(expected);

    GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 16, 16);
    gdk_pixbuf_fill(pixbuf, 0x123456ff);
    gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
    g_object_unref(pixbuf);
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
    icon = find_icon(model);
    g_assert_true(G_IS_BYTES_ICON(icon));
    GBytes *bytes = g_bytes_icon_get_bytes(G_BYTES_ICON(icon));
    GInputStream *stream = g_memory_input_stream_new_from_bytes(bytes);
    pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, NULL);
    g_assert_nonnull(pixbuf);
    g_assert_cmpuint(gdk_pixbuf_get_pixels(pixbuf)[0], ==, 0x12);
    g_object_unref(pixbuf);
    g_object_unref(stream);
    g_object_unref(icon);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 0.2, 0.4, 0.6, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
    gtk_image_set_from_surface(GTK_IMAGE(image), surface);
    cairo_surface_destroy(surface);
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
    icon = find_icon(model);
    g_assert_true(G_IS_BYTES_ICON(icon));
    g_object_unref(icon);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_image_set_from_stock(GTK_IMAGE(image), GTK_STOCK_OPEN, GTK_ICON_SIZE_MENU);
    G_GNUC_END_IGNORE_DEPRECATIONS
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
    icon = find_icon(model);
    g_assert_true(G_IS_BYTES_ICON(icon));
    g_object_unref(icon);

    gtk_image_clear(GTK_IMAGE(image));
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
    g_assert_null(find_icon(model));
    gtk_widget_destroy(view);
    g_object_unref(view);
    panel_gtk_menu_export_free(exporter);
    gtk_widget_destroy(bar);
    g_object_unref(bar);
}

static void menu_shortcuts(void)
{
    GtkWidget *bar = g_object_ref_sink(gtk_menu_bar_new());
    GtkWidget *item = gtk_menu_item_new_with_label("New Tab");
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    gtk_widget_show(item);
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(item));
    /* libxfce4ui sets this directly; it deliberately leaves accel-path unset. */
    gtk_accel_label_set_accel(GTK_ACCEL_LABEL(label), GDK_KEY_t, GDK_CONTROL_MASK);
    g_assert_null(gtk_menu_item_get_accel_path(GTK_MENU_ITEM(item)));
    PanelGtkMenuExport *exporter = panel_gtk_menu_export_new(GTK_MENU_BAR(bar));
    GMenuModel *model = panel_gtk_menu_export_model(exporter);
    char *accel = find_attribute(model, "New Tab", "accel");
    g_assert_nonnull(accel);
    guint key;
    GdkModifierType mods;
    gtk_accelerator_parse(accel, &key, &mods);
    g_assert_cmpuint(key, ==, GDK_KEY_t);
    g_assert_cmpuint(mods, ==, GDK_CONTROL_MASK);
    g_free(accel);

    GtkWidget *view = g_object_ref_sink(gtk_menu_new());
    gtk_widget_insert_action_group(view, "app", panel_gtk_menu_export_actions(exporter));
    gtk_menu_shell_bind_model(GTK_MENU_SHELL(view), model, NULL, FALSE);
    GtkWidget *display_label = gtk_bin_get_child(GTK_BIN(first_child(view)));
    g_assert_true(GTK_IS_ACCEL_LABEL(display_label));
    gtk_accel_label_get_accel(GTK_ACCEL_LABEL(display_label), &key, &mods);
    g_assert_cmpuint(key, ==, GDK_KEY_t);
    g_assert_cmpuint(mods, ==, GDK_CONTROL_MASK);
    GtkRequisition minimum, natural;
    gtk_accel_label_refetch(GTK_ACCEL_LABEL(display_label));
    gtk_widget_get_preferred_size(display_label, &minimum, &natural);
    g_assert_cmpuint(gtk_accel_label_get_accel_width(GTK_ACCEL_LABEL(display_label)), >, 0);

    /* Traditional item paths and signal-connected accelerators still work. */
    gtk_accel_label_set_accel(GTK_ACCEL_LABEL(label), 0, 0);
    GtkAccelGroup *group = gtk_accel_group_new();
    gtk_menu_set_accel_group(GTK_MENU(view), group);
    gtk_accel_map_add_entry("<LunaticTest>/new-tab", GDK_KEY_n, GDK_CONTROL_MASK);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), "<LunaticTest>/new-tab");
    panel_gtk_menu_export_rebuild(exporter);
    accel = find_attribute(model, "New Tab", "accel");
    g_assert_nonnull(accel);
    gtk_accelerator_parse(accel, &key, &mods);
    g_assert_cmpuint(key, ==, GDK_KEY_n);
    g_free(accel);
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(item), NULL);
    gtk_widget_add_accelerator(item, "activate", group, GDK_KEY_w, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    panel_gtk_menu_export_rebuild(exporter);
    accel = find_attribute(model, "New Tab", "accel");
    g_assert_nonnull(accel);
    gtk_accelerator_parse(accel, &key, &mods);
    g_assert_cmpuint(key, ==, GDK_KEY_w);
    g_free(accel);

    gtk_widget_destroy(view);
    g_object_unref(view);
    panel_gtk_menu_export_free(exporter);
    gtk_widget_destroy(bar);
    g_object_unref(bar);
    g_object_unref(group);
}
static void cold_submenus(void)
{
    ColdMenu cold = {0};
    GtkWidget *bar = g_object_ref_sink(gtk_menu_bar_new());
    GtkWidget *file = gtk_menu_item_new_with_label("File");
    GtkWidget *source = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), source);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), file);
    /* Never show_all(): that would prepare source submenus before the test. */
    gtk_widget_show(file);
    gtk_widget_show(bar);
    g_signal_connect(source, "show", G_CALLBACK(populate_file), &cold);
    g_assert_cmpuint(cold.file_shows, ==, 0);
    PanelGtkMenuExport *exporter = panel_gtk_menu_export_new(GTK_MENU_BAR(bar));
    GMenuModel *model = panel_gtk_menu_export_model(exporter);
    char *action = find_attribute(model, "Generated action", "action");
    g_assert_nonnull(action);
    g_free(action);
    g_assert_cmpuint(cold.file_shows, ==, 1);
    g_assert_cmpuint(cold.nested_shows, ==, 1);
    /* Ordinary model rebuilds must not rerun application menu callbacks. */
    panel_gtk_menu_export_rebuild(exporter);
    panel_gtk_menu_export_rebuild(exporter);
    g_assert_cmpuint(cold.file_shows, ==, 1);
    g_assert_cmpuint(cold.nested_shows, ==, 1);
    GtkWidget *view = g_object_ref_sink(gtk_menu_bar_new());
    gtk_widget_insert_action_group(view, "app", panel_gtk_menu_export_actions(exporter));
    gtk_menu_shell_bind_model(GTK_MENU_SHELL(view), model, NULL, FALSE);
    GtkWidget *display_file = first_child(view);
    GtkWidget *display_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(display_file));
    g_assert_nonnull(display_menu);
    /* Exercise GTK's real submenu tracker, rather than calling our action. */
    gtk_widget_show(display_menu);
    g_assert_cmpuint(cold.file_shows, ==, 2);
    g_assert_false(gtk_widget_get_mapped(source));
    g_assert_false(gtk_widget_get_visible(source));
    GtkWidget *display_nested = first_child(display_menu);
    GtkWidget *nested_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(display_nested));
    g_assert_nonnull(nested_menu);
    guint nested_shows = cold.nested_shows;
    gtk_widget_show(nested_menu);
    g_assert_cmpuint(cold.nested_shows, ==, nested_shows + 1);
    gtk_menu_item_activate(GTK_MENU_ITEM(first_child(nested_menu)));
    g_assert_cmpuint(cold.clicks, ==, 1);
    gtk_widget_hide(nested_menu);
    gtk_widget_hide(display_menu);
    gtk_widget_show(display_menu);
    g_assert_cmpuint(cold.file_shows, ==, 3);
    gtk_widget_destroy(view);
    g_object_unref(view);
    panel_gtk_menu_export_free(exporter);
    gtk_widget_destroy(bar);
    g_object_unref(bar);
}

static void title_width(void)
{
    GtkWidget *title = g_object_ref_sink(panel_title_label_new(200));
    gtk_label_set_text(GTK_LABEL(title), "App");
    gtk_widget_show(title);
    gint minimum, natural;
    gtk_widget_get_preferred_width(title, &minimum, &natural);
    g_assert_cmpint(natural, <, 200);
    /* The cap is in pixels regardless of title length or character widths. */
    GString *long_title = g_string_new(NULL);
    for (guint i = 0; i < 128; i++) g_string_append(long_title, "Long title — ファイル ");
    gtk_label_set_text(GTK_LABEL(title), long_title->str);
    g_string_free(long_title, TRUE);
    gtk_widget_get_preferred_width(title, &minimum, &natural);
    g_assert_cmpint(minimum, <=, 200);
    g_assert_cmpint(natural, <=, 200);
    gtk_widget_get_preferred_width_for_height(title, 32, &minimum, &natural);
    g_assert_cmpint(natural, <=, 200);
    GtkAllocation allocation = {0, 0, 900, 32};
    gtk_widget_size_allocate(title, &allocation);
    g_assert_cmpint(gtk_widget_get_allocated_width(title), <=, 200);
    panel_title_label_set_max_width(PANEL_TITLE_LABEL(title), 80);
    gtk_widget_get_preferred_width(title, &minimum, &natural);
    g_assert_cmpint(natural, <=, 80);
    gtk_widget_size_allocate(title, &allocation);
    g_assert_cmpint(gtk_widget_get_allocated_width(title), <=, 80);
    /* A larger preference must also release the previous width constraint. */
    panel_title_label_set_max_width(PANEL_TITLE_LABEL(title), 320);
    gtk_widget_get_preferred_width(title, &minimum, &natural);
    g_assert_cmpint(natural, >, 80);
    g_assert_cmpint(natural, <=, 320);
    g_assert_cmpint(gtk_label_get_ellipsize(GTK_LABEL(title)), ==, PANGO_ELLIPSIZE_END);
    gtk_widget_destroy(title);
    g_object_unref(title);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!gtk_init_check(&argc, &argv)) { g_print("SKIP: GTK exporter needs a display\n"); return 77; }
    g_test_add_func("/menu/gtk-export", bridge);
    g_test_add_func("/menu/cold-submenus", cold_submenus);
    g_test_add_func("/menu/icons", menu_icons);
    g_test_add_func("/menu/shortcuts", menu_shortcuts);
    g_test_add_func("/panel/title-width", title_width);
    return g_test_run();
}
