/* SPDX-License-Identifier: GPL-3.0-only */
#include "session-settings.h"

static void check(const char *before, gboolean enabled, const char *expected)
{
    GVariant *current = before ? g_variant_ref_sink(g_variant_new_string(before)) : NULL;
    GError *error = NULL;
    char *after = panel_menu_module_list(current, enabled, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(after, ==, expected);
    g_free(after);
    g_clear_pointer(&current, g_variant_unref);
}
static void initial(void)
{
    check(NULL, TRUE, PANEL_GTK_MODULE_NAME);
    check("", TRUE, PANEL_GTK_MODULE_NAME);
    check(NULL, FALSE, "");
    check("", FALSE, "");
}
static void preserve(void)
{
    check("canberra-gtk-module:another-module", TRUE,
          "canberra-gtk-module:another-module:" PANEL_GTK_MODULE_NAME);
    check("canberra-gtk-module:another-module", FALSE, "canberra-gtk-module:another-module");
    check("/opt/vendor/libmodule.so:foreign:foreign", TRUE,
          "/opt/vendor/libmodule.so:foreign:foreign:" PANEL_GTK_MODULE_NAME);
    /* Only complete tokens match, never prefixes or similarly named modules. */
    check("lunatic-panel-gtk-module-other", FALSE, "lunatic-panel-gtk-module-other");
}
static void idempotent(void)
{
    check(PANEL_GTK_MODULE_NAME, TRUE, PANEL_GTK_MODULE_NAME);
    check("before:" PANEL_GTK_MODULE_NAME ":after", TRUE, "before:" PANEL_GTK_MODULE_NAME ":after");
    check(PANEL_GTK_MODULE_NAME ":" PANEL_GTK_MODULE_NAME, TRUE, PANEL_GTK_MODULE_NAME);
    check(":" PANEL_GTK_MODULE_NAME "::other:", TRUE, PANEL_GTK_MODULE_NAME ":other");
}
static void disable(void)
{
    check(PANEL_GTK_MODULE_NAME, FALSE, "");
    check("before:" PANEL_GTK_MODULE_NAME ":after", FALSE, "before:after");
    check(PANEL_GTK_MODULE_NAME ":other:" PANEL_GTK_MODULE_NAME, FALSE, "other");
}
static void migrate_legacy(void)
{
    check("unity-panel-gtk-module", TRUE, PANEL_GTK_MODULE_NAME);
    check("before:unity-panel-gtk-module:after", TRUE, "before:" PANEL_GTK_MODULE_NAME ":after");
    check("unity-panel-gtk-module:" PANEL_GTK_MODULE_NAME ":unity-panel-gtk-module:other",
          TRUE, PANEL_GTK_MODULE_NAME ":other");
    check(PANEL_GTK_MODULE_NAME ":other:unity-panel-gtk-module", TRUE, PANEL_GTK_MODULE_NAME ":other");
    check("before:unity-panel-gtk-module:" PANEL_GTK_MODULE_NAME ":after", FALSE, "before:after");
    check("unity-panel-gtk-module-other", TRUE, "unity-panel-gtk-module-other:" PANEL_GTK_MODULE_NAME);
    check("unity-panel-gtk-module-other", FALSE, "unity-panel-gtk-module-other");
}
static void invalid_type(void)
{
    GVariant *current = g_variant_ref_sink(g_variant_new_int32(12));
    GError *error = NULL;
    g_assert_null(panel_menu_module_list(current, TRUE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_variant_unref(current);
}
static void other_instances(void)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&builder, "{sv}", "/plugins/plugin-12", g_variant_new_string("lunatic-global-menu"));
    g_variant_builder_add(&builder, "{sv}", "/plugins/plugin-13", g_variant_new_string("launcher"));
    /* Only exact plugin roots count, not preferences or similarly named plugins. */
    g_variant_builder_add(&builder, "{sv}", "/plugins/plugin-13/name", g_variant_new_string("lunatic-global-menu"));
    g_variant_builder_add(&builder, "{sv}", "/plugins/plugin-14", g_variant_new_string("lunatic-global-menu-other"));
    g_variant_builder_add(&builder, "{sv}", "/plugins/plugin-other", g_variant_new_string("lunatic-global-menu"));
    g_variant_builder_add(&builder, "{sv}", "/plugins/plugin-15", g_variant_new_int32(12));
    GVariant *properties = g_variant_ref_sink(g_variant_builder_end(&builder));
    g_assert_false(panel_menu_has_other_instance(properties, 12));
    g_assert_true(panel_menu_has_other_instance(properties, 13));
    /* Own entry may have already been deleted by the panel. */
    g_assert_true(panel_menu_has_other_instance(properties, 99));
    g_variant_unref(properties);
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    properties = g_variant_ref_sink(g_variant_builder_end(&builder));
    g_assert_false(panel_menu_has_other_instance(properties, 12));
    g_variant_unref(properties);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/session/initial", initial);
    g_test_add_func("/session/preserve-other-modules", preserve);
    g_test_add_func("/session/idempotent", idempotent);
    g_test_add_func("/session/disable", disable);
    g_test_add_func("/session/migrate-legacy-module", migrate_legacy);
    g_test_add_func("/session/invalid-property-type", invalid_type);
    g_test_add_func("/session/other-panel-instances", other_instances);
    return g_test_run();
}
