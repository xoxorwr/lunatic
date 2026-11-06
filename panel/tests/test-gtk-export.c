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
    g_test_add_func("/panel/title-width", title_width);
    return g_test_run();
}
