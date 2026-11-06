/* SPDX-License-Identifier: GPL-3.0-only */
#include "app-name.h"
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>

static GAppInfo *fixture(const char *name, const char *wmclass, gboolean hidden)
{
    GKeyFile *file = g_key_file_new();
    g_key_file_set_string(file, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(file, "Desktop Entry", "Name", name);
    g_key_file_set_string(file, "Desktop Entry", "Exec", "/bin/true");
    g_key_file_set_string(file, "Desktop Entry", "StartupWMClass", wmclass);
    g_key_file_set_boolean(file, "Desktop Entry", "NoDisplay", hidden);
    GAppInfo *app = G_APP_INFO(g_desktop_app_info_new_from_keyfile(file));
    g_assert_nonnull(app);
    g_key_file_unref(file);
    return app;
}
static void class_matching(void)
{
    PanelAppNames *names = panel_app_names_new();
    GList *apps = NULL;
    apps = g_list_append(apps, fixture("Code URL Handler", "code-oss", TRUE));
    apps = g_list_append(apps, fixture("Code - OSS", "code-oss", FALSE));
    apps = g_list_append(apps, fixture("Aseprite", "aseprite", FALSE));
    panel_app_names_load(names, apps);
    g_list_free_full(apps, g_object_unref); /* Index owns no desktop objects. */
    g_assert_cmpstr(panel_app_names_find(names, NULL, "editor", "Code-OSS"), ==, "Code - OSS");
    g_assert_cmpstr(panel_app_names_find(names, NULL, "ASEPRITE", "other"), ==, "Aseprite");
    g_assert_null(panel_app_names_find(names, NULL, NULL, "unknown"));
    panel_app_names_free(names);
}
static void application_id(void)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp("lunatic-app-name-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(directory, "org.example.Editor.desktop", NULL);
    g_assert_true(g_file_set_contents(path,
        "[Desktop Entry]\nType=Application\nName=Example Editor\nExec=/bin/true\n", -1, &error));
    g_assert_no_error(error);
    GAppInfo *app = G_APP_INFO(g_desktop_app_info_new_from_filename(path));
    g_assert_nonnull(app);
    GList *apps = g_list_append(NULL, fixture("Other", "different", FALSE));
    apps = g_list_append(apps, app);
    PanelAppNames *names = panel_app_names_new();
    panel_app_names_load(names, apps);
    g_list_free_full(apps, g_object_unref);
    g_assert_cmpstr(panel_app_names_find(names, "org.example.Editor", NULL, "different"), ==, "Example Editor");
    g_assert_cmpstr(panel_app_names_find(names, NULL, NULL, "Editor"), ==, "Example Editor");
    panel_app_names_load(names, NULL);
    g_assert_null(panel_app_names_find(names, "org.example.Editor", NULL, NULL));
    panel_app_names_free(names);
    g_unlink(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}
static void titles(void)
{
    const char *cases[][3] = {
        {"Thunar", "Documents", "Thunar - Documents"},
        {"Thunar", "Documents - Thunar", "Thunar - Documents"},
        {"Thunar", "Documents — Thunar", "Thunar - Documents"},
        {"Thunar", "Thunar - Documents", "Thunar - Documents"},
        {"Code - OSS", "main.c - Project - Code - OSS", "Code - OSS - main.c - Project"},
        {"Aseprite", "Sprite.aseprite", "Aseprite - Sprite.aseprite"},
        {"Steam", "Steam", "Steam"},
        {"Editor", "猫の絵", "Editor - 猫の絵"},
        {"Thunar", NULL, "Thunar"},
        {NULL, "Window", "Window"},
        {NULL, NULL, "Application"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        char *title = panel_app_title(cases[i][0], cases[i][1]);
        g_assert_cmpstr(title, ==, cases[i][2]);
        g_free(title);
    }
}
static void markup(void)
{
    char *label = panel_app_title_markup("Thunar", "Documents - Thunar");
    g_assert_cmpstr(label, ==, "<b>Thunar</b> - Documents");
    g_free(label);
    label = panel_app_title_markup("<Editor&>", "<b>file</b> & notes");
    g_assert_cmpstr(label, ==, "<b>&lt;Editor&amp;&gt;</b> - &lt;b&gt;file&lt;/b&gt; &amp; notes");
    g_free(label);
    label = panel_app_title_markup("Steam", "Steam");
    g_assert_cmpstr(label, ==, "<b>Steam</b>");
    g_free(label);
    label = panel_app_title_markup(NULL, "<unknown>");
    g_assert_cmpstr(label, ==, "&lt;unknown&gt;");
    g_free(label);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/app/class-matching", class_matching);
    g_test_add_func("/app/application-id", application_id);
    g_test_add_func("/app/title-format", titles);
    g_test_add_func("/app/title-markup", markup);
    return g_test_run();
}
