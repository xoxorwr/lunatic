/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-model.h"
#include "remote-menu.h"

static void changed(GMenuModel *model, int position, int removed, int added, gpointer data)
{
    (void)model; (void)position; (void)removed; (void)added;
    (*(guint *)data)++;
}
static void fill(PanelMenuModel *model, GMenu *child, const char *label)
{
    PanelMenuBuilder builder;
    panel_menu_builder_init(&builder, model);
    panel_menu_builder_separator(&builder);
    GMenuItem *item = g_menu_item_new_submenu(label, G_MENU_MODEL(child));
    panel_menu_builder_append(&builder, item);
    panel_menu_builder_separator(&builder);
    panel_menu_builder_separator(&builder);
    panel_menu_builder_finish(&builder);
    g_object_unref(item);
}
static void stable_models(void)
{
    PanelMenuModel *model = panel_menu_model_new();
    GMenu *child = g_menu_new();
    fill(model, child, "File");
    g_assert_cmpint(g_menu_model_get_n_items(G_MENU_MODEL(model->model)), ==, 1);
    GMenuModel *section = g_menu_model_get_item_link(G_MENU_MODEL(model->model), 0, G_MENU_LINK_SECTION);
    guint changes = 0;
    g_signal_connect(model->model, "items-changed", G_CALLBACK(changed), &changes);
    g_signal_connect(section, "items-changed", G_CALLBACK(changed), &changes);
    fill(model, child, "File");
    g_menu_append(child, "New", "app.new");
    fill(model, child, "File");
    g_assert_cmpuint(changes, ==, 0);
    GMenuModel *nested = g_menu_model_get_item_link(section, 0, G_MENU_LINK_SUBMENU);
    g_assert_true(nested == G_MENU_MODEL(child));
    fill(model, child, "Renamed");
    g_assert_cmpuint(changes, ==, 2);
    g_object_unref(nested);
    g_object_unref(section);
    panel_menu_model_free(model);
    g_object_unref(child);
}
static GVariant *parse(const char *text)
{
    GError *error = NULL;
    GVariant *value = g_variant_parse(G_VARIANT_TYPE("(ia{sv}av)"), text, NULL, NULL, &error);
    g_assert_no_error(error);
    return value;
}
static GVariant *fixture(gboolean checked)
{
    return parse(checked ?
        "(0, {}, [<@(ia{sv}av) (1, {'label': <'_File'>, 'children-display': <'submenu'>}, [<@(ia{sv}av) (2, {'label': <'Check'>, 'toggle-type': <'checkmark'>, 'toggle-state': <1>}, [])>, <@(ia{sv}av) (3, {'type': <'separator'>}, [])>, <@(ia{sv}av) (4, {'label': <'Radio'>, 'toggle-type': <'radio'>, 'toggle-state': <1>, 'enabled': <false>}, [])>, <@(ia{sv}av) (5, {'visible': <false>}, [])>])>])" :
        "(0, {}, [<@(ia{sv}av) (1, {'label': <'_File'>, 'children-display': <'submenu'>}, [<@(ia{sv}av) (2, {'label': <'Check'>, 'toggle-type': <'checkmark'>, 'toggle-state': <0>}, [])>, <@(ia{sv}av) (3, {'type': <'separator'>}, [])>, <@(ia{sv}av) (4, {'label': <'Radio'>, 'toggle-type': <'radio'>, 'toggle-state': <1>, 'enabled': <false>}, [])>, <@(ia{sv}av) (5, {'visible': <false>}, [])>])>])");
}
static void dispatched(gint32 id, const char *event, gpointer data)
{
    g_assert_cmpint(id, ==, 2);
    g_assert_cmpstr(event, ==, "clicked");
    (*(guint *)data)++;
}
static void remote_updates(void)
{
    PanelRemoteMenu *remote = panel_remote_menu_new();
    GVariant *first = fixture(FALSE), *second = fixture(TRUE);
    g_assert_true(panel_remote_menu_update(remote, first));
    GMenuModel *root = panel_remote_menu_model(remote);
    GMenuModel *section = g_menu_model_get_item_link(root, 0, G_MENU_LINK_SECTION);
    GMenuModel *nested = g_menu_model_get_item_link(section, 0, G_MENU_LINK_SUBMENU);
    g_assert_cmpint(g_menu_model_get_n_items(nested), ==, 2);
    GActionGroup *actions = panel_remote_menu_actions(remote);
    g_assert_false(g_action_group_has_action(actions, "item5"));
    g_assert_false(g_action_group_get_action_enabled(actions, "item4"));
    GVariant *state = g_action_group_get_action_state(actions, "item2");
    g_assert_false(g_variant_get_boolean(state));
    g_variant_unref(state);
    guint changes = 0, clicks = 0;
    g_signal_connect(root, "items-changed", G_CALLBACK(changed), &changes);
    g_signal_connect(nested, "items-changed", G_CALLBACK(changed), &changes);
    g_assert_true(panel_remote_menu_update(remote, second));
    g_assert_cmpuint(changes, ==, 0);
    state = g_action_group_get_action_state(actions, "item2");
    g_assert_true(g_variant_get_boolean(state));
    g_variant_unref(state);
    panel_remote_menu_set_event(remote, dispatched, &clicks);
    g_action_group_activate_action(actions, "item2", NULL);
    g_assert_cmpuint(clicks, ==, 1);
    GAction *retained = g_object_ref(g_action_map_lookup_action(G_ACTION_MAP(actions), "item2"));
    g_object_unref(nested);
    g_object_unref(section);
    g_object_unref(remote);
    /* An action retained by GTK cannot use an owner that has been freed. */
    g_action_activate(retained, NULL);
    g_assert_cmpuint(clicks, ==, 1);
    g_object_unref(retained);
    g_variant_unref(first);
    g_variant_unref(second);
}
static void invalid_and_removed(void)
{
    PanelRemoteMenu *remote = panel_remote_menu_new();
    GVariant *valid = fixture(FALSE);
    g_assert_true(panel_remote_menu_update(remote, valid));
    GVariant *invalid = parse("(0, {}, [<@(ia{sv}av) (0, {}, [])>])");
    g_assert_false(panel_remote_menu_update(remote, invalid));
    g_assert_true(g_action_group_has_action(panel_remote_menu_actions(remote), "item2"));
    g_variant_unref(invalid);
    invalid = g_variant_ref_sink(g_variant_new_string("invalid"));
    g_assert_false(panel_remote_menu_update(remote, invalid));
    g_variant_unref(invalid);
    invalid = parse("(0, {}, [<'invalid child'>])");
    g_assert_false(panel_remote_menu_update(remote, invalid));
    g_variant_unref(invalid);
    guint clicks = 0;
    panel_remote_menu_set_event(remote, dispatched, &clicks);
    GAction *retained = g_object_ref(g_action_map_lookup_action(
        G_ACTION_MAP(panel_remote_menu_actions(remote)), "item2"));
    GVariant *empty = parse("(0, {}, [])");
    g_assert_true(panel_remote_menu_update(remote, empty));
    g_assert_cmpint(g_menu_model_get_n_items(panel_remote_menu_model(remote)), ==, 0);
    g_assert_false(g_action_group_has_action(panel_remote_menu_actions(remote), "item2"));
    g_action_activate(retained, NULL);
    g_assert_cmpuint(clicks, ==, 0);
    g_object_unref(retained);
    g_variant_unref(empty);
    g_variant_unref(valid);
    g_object_unref(remote);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/menu/stable-models", stable_models);
    g_test_add_func("/menu/remote-updates", remote_updates);
    g_test_add_func("/menu/invalid-and-removed", invalid_and_removed);
    return g_test_run();
}
