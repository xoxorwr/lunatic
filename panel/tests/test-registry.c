/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-registry.h"

static void registry_ownership(void)
{
    PanelMenuRegistry *registry = panel_menu_registry_new();
    GError *error = NULL;
    g_assert_true(panel_menu_registry_register(registry, 42, ":1.10", "/menu", &error));
    g_assert_no_error(error);
    g_assert_true(panel_menu_registry_register(registry, 42, ":1.10", "/updated", &error));
    g_assert_cmpstr(panel_menu_registry_lookup(registry, 42)->path, ==, "/updated");
    g_assert_false(panel_menu_registry_register(registry, 42, ":1.11", "/other", &error));
    g_assert_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);
    g_clear_error(&error);
    g_assert_false(panel_menu_registry_unregister(registry, 42, ":1.11", &error));
    g_assert_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);
    g_clear_error(&error);
    g_assert_nonnull(panel_menu_registry_lookup(registry, 42));
    g_assert_true(panel_menu_registry_unregister(registry, 42, ":1.10", &error));
    g_assert_null(panel_menu_registry_lookup(registry, 42));
    g_assert_true(panel_menu_registry_unregister(registry, 42, ":1.10", &error));
    g_assert_no_error(error);
    panel_menu_registry_free(registry);
}

static void registry_disconnect(void)
{
    PanelMenuRegistry *registry = panel_menu_registry_new();
    g_assert_true(panel_menu_registry_register(registry, 42, ":1.10", "/one", NULL));
    g_assert_true(panel_menu_registry_register(registry, 43, ":1.10", "/two", NULL));
    g_assert_true(panel_menu_registry_register(registry, 0xf0000001u, ":1.11", "/other", NULL));
    GVariant *menus = g_variant_ref_sink(panel_menu_registry_list(registry));
    g_assert_true(g_variant_is_of_type(menus, G_VARIANT_TYPE("a(uso)")));
    g_assert_cmpuint(g_variant_n_children(menus), ==, 3);
    g_variant_unref(menus);
    GArray *removed = panel_menu_registry_remove_owner(registry, ":1.10");
    g_assert_cmpuint(removed->len, ==, 2);
    g_assert_null(panel_menu_registry_lookup(registry, 42));
    g_assert_null(panel_menu_registry_lookup(registry, 43));
    g_array_unref(removed);
    menus = g_variant_ref_sink(panel_menu_registry_list(registry));
    guint32 window;
    const char *service, *path;
    g_variant_get_child(menus, 0, "(u&s&o)", &window, &service, &path);
    g_assert_cmpuint(window, ==, 0xf0000001u);
    g_assert_cmpstr(service, ==, ":1.11");
    g_assert_cmpstr(path, ==, "/other");
    g_variant_unref(menus);
    /* A recycled XID can be registered once its previous owner disconnects. */
    g_assert_true(panel_menu_registry_register(registry, 42, ":1.12", "/replacement", NULL));
    panel_menu_registry_free(registry);
}

static void registry_invalid(void)
{
    PanelMenuRegistry *registry = panel_menu_registry_new();
    g_assert_false(panel_menu_registry_register(registry, 0, ":1.10", "/menu", NULL));
    g_assert_false(panel_menu_registry_register(registry, 42, "not a bus name", "/menu", NULL));
    g_assert_false(panel_menu_registry_register(registry, 42, "org.example.App", "/menu", NULL));
    g_assert_false(panel_menu_registry_register(registry, 42, ":1.10", "not/a/path", NULL));
    g_assert_false(panel_menu_registry_register(registry, 42, NULL, "/menu", NULL));
    g_assert_null(panel_menu_registry_lookup(registry, 42));
    panel_menu_registry_free(registry);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/panel/registry-ownership", registry_ownership);
    g_test_add_func("/panel/registry-disconnect", registry_disconnect);
    g_test_add_func("/panel/registry-invalid", registry_invalid);
    return g_test_run();
}
