/* SPDX-License-Identifier: GPL-3.0-only */
#include "desktop.h"
#include "launcher-core.h"
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

static void hide_states(void)
{
    HideMachine m;
    hide_machine_init(&m);
    hide_machine_quirk(&m, LOCK_HIDE, TRUE, 0);
    g_assert_false(m.should_hide); /* HIDE_NEVER overrides LOCK_HIDE */
    hide_machine_mode(&m, AUTOHIDE, 0);
    g_assert_true(m.should_hide);
    hide_machine_quirk(&m, LOCK_HIDE, FALSE, 0);
    hide_machine_quirk(&m, LAUNCHER_HIDDEN, TRUE, 0);
    hide_machine_quirk(&m, MOUSE_OVER_LAUNCHER, TRUE, 0);
    g_assert_true(m.should_hide); /* A hidden launcher requires reveal pressure. */
    hide_machine_pressure(&m, 2000, 0);
    g_assert_true(m.should_hide);
    hide_machine_pressure(&m, 1, 0);
    g_assert_false(m.should_hide);
    hide_machine_quirk(&m, LAUNCHER_HIDDEN, FALSE, 0);
    hide_machine_quirk(&m, REVEAL_PRESSURE_PASS, FALSE, 0);
    hide_machine_quirk(&m, MOUSE_OVER_LAUNCHER, FALSE, 10);
    hide_machine_tick(&m, 409);
    g_assert_false(m.should_hide);
    hide_machine_tick(&m, 410);
    g_assert_true(m.should_hide);
    hide_machine_quirk(&m, QUICKLIST_OPEN, TRUE, 420);
    g_assert_false(m.should_hide);
    hide_machine_quirk(&m, QUICKLIST_OPEN, FALSE, 430);
    hide_machine_quirk(&m, PLACES_VISIBLE, TRUE, 500);
    hide_machine_tick(&m, 2000);
    g_assert_false(m.should_hide); /* delayed hide cancelled */
    hide_machine_quirk(&m, EXTERNAL_DND_ACTIVE, TRUE, 2100);
    hide_machine_quirk(&m, DND_PUSHED_OFF, TRUE, 2101);
    g_assert_true(m.should_hide);
    hide_machine_mode(&m, HIDE_NEVER, 2200);
    g_assert_false(m.should_hide);
}

static void pressure_decay(void)
{
    HideMachine m;
    hide_machine_init(&m);
    hide_machine_pressure(&m, 1000, 100);
    hide_machine_tick(&m, 199);
    g_assert_cmpint(m.pressure, ==, 865);
    hide_machine_tick(&m, 200);
    g_assert_cmpint(m.pressure, ==, 850);
    hide_machine_tick(&m, 5000);
    g_assert_cmpint(m.pressure, ==, 0);
    g_assert_cmpfloat(m.reveal_progress, ==, 0);
}

static void animations(void)
{
    Animation a = {0};
    animation_to(&a, 1, 200, 0);
    animation_tick(&a, 50);
    g_assert_cmpfloat_with_epsilon(a.value, .25, .00001);
    animation_to(&a, 0, 200, 50);
    animation_tick(&a, 75);
    g_assert_cmpfloat_with_epsilon(a.value, .125, .00001);
    animation_tick(&a, 100);
    g_assert_cmpfloat(a.value, ==, 0);
    g_assert_false(animation_tick(&a, 101));
    g_assert_cmpfloat_with_epsilon(launcher_urgent_pulse(0), 1, .00001);
    g_assert_cmpfloat_with_epsilon(launcher_starting_pulse(0), 0, .00001);
    g_assert_cmpfloat_with_epsilon(launcher_urgent_wiggle(1), 0, .00001);
    g_assert_cmpfloat_with_epsilon(launcher_drag_limiter(100), -launcher_drag_limiter(-100),
                                   .00001);
    g_assert_cmpfloat(launcher_drag_limiter(100000), <=, 160);
}

static void layout(void)
{
    LayoutIcon icons[14] = {{0}};
    for (guint i = 0; i < 14; i++)
        icons[i].visible = 1;
    icons[13].shelf = TRUE;
    launcher_layout(icons, 4, 54, 800, 0, 0);
    g_assert_cmpfloat(icons[0].center, ==, 32);
    g_assert_cmpfloat(icons[1].center, ==, 91);
    icons[3].shelf = TRUE;
    launcher_layout(icons, 4, 54, 800, 0, 0);
    g_assert_cmpfloat(icons[3].center, ==, 768);
    icons[3].shelf = FALSE;
    launcher_layout(icons, 14, 54, 480, 0, 0);
    gboolean folded = FALSE;
    for (guint i = 0; i < 14; i++) {
        g_assert_true(isfinite(icons[i].center));
        if (i)
            g_assert_cmpfloat(icons[i].center, >, icons[i - 1].center);
        g_assert_cmpfloat(icons[i].rotation, >=, -1);
        folded |= icons[i].rotation < 0;
    }
    g_assert_true(folded);
    icons[9].active = 1;
    launcher_layout(icons, 14, 54, 480, 0, 0);
    g_assert_cmpfloat(icons[9].rotation, ==, 0);
    launcher_layout(icons, 14, 54, 480, 1, -200);
    for (guint i = 0; i < 14; i++)
        g_assert_cmpfloat(icons[i].rotation, ==, 0);
}

static void fixed_shelf(void)
{
    LayoutIcon icons[24] = {{0}};
    for (guint i = 0; i < G_N_ELEMENTS(icons); i++)
        icons[i].visible = 1;
    icons[22].shelf = icons[23].shelf = TRUE;
    for (int n = 0; n <= 10; n++) {
        double hover = n / 10.0;
        for (int scroll = 0; scroll >= -1600; scroll -= 100) {
            /* Exercise a hovered app unfolding while the list changes size. */
            icons[20].unfolded = hover;
            icons[21].visible = hover;
            launcher_layout(icons, 24, 54, 480, hover, scroll);
            g_assert_cmpfloat(icons[22].center, ==, 389);
            g_assert_cmpfloat(icons[23].center, ==, 448);
            g_assert_cmpfloat(icons[23].center + 27, <=, 480);
            g_assert_cmpfloat(icons[22].rotation, ==, 0);
            for (guint i = 0; i < 22; i++)
                g_assert_cmpfloat(icons[i].hit_end, <=, icons[22].hit_start - 5);
        }
    }
}

static void colors(void)
{
    for (int alpha = 0; alpha <= 1; alpha++) {
        GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alpha, 8, 8, 8);
        gdk_pixbuf_fill(p, 0xff0000ff);
        double background[3], glow[3];
        launcher_icon_colors(p, background, glow);
        g_assert_cmpfloat_with_epsilon(background[0], .9, .0001);
        g_assert_cmpfloat_with_epsilon(background[1], .315, .0001);
        g_assert_cmpfloat_with_epsilon(background[2], .315, .0001);
        g_assert_cmpfloat_with_epsilon(glow[0], 1, .0001);
        g_object_unref(p);
    }
}

static GDesktopAppInfo *desktop(const char *id, const char *wmclass)
{
    GKeyFile *key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(key, "Desktop Entry", "Name", id);
    g_key_file_set_string(key, "Desktop Entry", "Exec", "/bin/true");
    g_key_file_set_string(key, "Desktop Entry", "StartupWMClass", wmclass);
    GDesktopAppInfo *app = g_desktop_app_info_new_from_keyfile(key);
    g_key_file_unref(key);
    g_assert_nonnull(app);
    return app;
}

static void matching(void)
{
    GDesktopAppInfo *a = desktop("a", "Code"), *b = desktop("b", "Code-OSS");
    GList *list = NULL;
    list = g_list_append(list, a);
    list = g_list_append(list, b);
    g_assert_true(launcher_find_app(list, "code-oss", "Code-OSS", NULL) == b);
    g_assert_true(launcher_find_app(list, NULL, "code", NULL) == a);
    g_assert_null(launcher_find_app(list, NULL, NULL, NULL));
    g_assert_null(launcher_find_app(list, "some-code-helper", NULL, NULL));
    LauncherAppIndex *index = launcher_app_index_new(list);
    const char *queries[][3] = {{"code-oss", "Code-OSS", NULL},
                                {NULL, "code", NULL},
                                {NULL, NULL, NULL},
                                {"some-code-helper", NULL, NULL},
                                {"Code-OSS", "Code", NULL}};
    for (guint i = 0; i < G_N_ELEMENTS(queries); i++)
        g_assert_true(launcher_app_index_find(index, queries[i][0], queries[i][1], queries[i][2]) ==
                      launcher_find_app(list, queries[i][0], queries[i][1], queries[i][2]));
    launcher_app_index_free(index);
    g_list_free_full(list, g_object_unref);
}

static void configuration(void)
{
    GError *error = NULL;
    char *dir = g_dir_make_tmp("lunatic-test-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(dir, "nested", "launcher.ini", NULL);
    char *pins[] = {"foo.desktop", "/tmp/custom launcher.desktop", NULL};
    g_assert_true(launcher_save_pins(path, pins, 64, 2, TRUE, &error));
    g_assert_no_error(error);
    int size = 48, monitor = 0;
    gboolean autohide = FALSE;
    char **loaded = launcher_load_pins(path, &size, &monitor, &autohide, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(loaded[0], ==, pins[0]);
    g_assert_cmpstr(loaded[1], ==, pins[1]);
    g_assert_null(loaded[2]);
    g_assert_cmpint(size, ==, 64);
    g_assert_cmpint(monitor, ==, 2);
    g_assert_true(autohide);
    g_strfreev(loaded);
    char *empty[] = {NULL};
    g_assert_true(launcher_save_pins(path, empty, 48, 0, FALSE, &error));
    g_assert_no_error(error);
    loaded = launcher_load_pins(path, &size, &monitor, &autohide, &error);
    g_assert_no_error(error);
    g_assert_nonnull(loaded);
    g_assert_null(loaded[0]);
    g_strfreev(loaded);
    g_remove(path);
    char *parent = g_path_get_dirname(path);
    g_rmdir(parent);
    g_rmdir(dir);
    g_free(parent);
    g_free(path);
    g_free(dir);
}

static void settings_persistence(void)
{
    LauncherSettings source, loaded;
    launcher_settings_defaults(&source);
    g_assert_false(source.all_workspaces);
    g_assert_false(source.show_desktop);
    g_assert_true(source.show_trash);
    g_assert_cmpint(source.visibility, ==, VISIBILITY_PRIMARY);
    source.autohide = source.minimize_on_click = source.all_workspaces = source.show_desktop = TRUE;
    source.show_trash = FALSE;
    source.hide_animation = HIDE_FADE;
    source.reveal_sensitivity = 87;
    source.opacity = .28;
    source.color_mode = COLOR_CUSTOM;
    source.custom_color = (GdkRGBA){.2, .4, .8, 1};
    source.visibility = VISIBILITY_ALL;
    source.position = DOCK_RIGHT;
    source.icon_size = 72;
    source.monitor = 2;
    GKeyFile *key = g_key_file_new();
    launcher_settings_write(&source, key);
    launcher_settings_defaults(&loaded);
    launcher_settings_read(&loaded, key);
    g_assert_true(loaded.autohide && loaded.minimize_on_click && loaded.all_workspaces &&
                  loaded.show_desktop);
    g_assert_false(loaded.show_trash);
    g_assert_cmpint(loaded.hide_animation, ==, HIDE_FADE);
    g_assert_cmpfloat(loaded.reveal_sensitivity, ==, 87);
    g_assert_cmpfloat_with_epsilon(loaded.opacity, .28, .0001);
    g_assert_cmpint(loaded.color_mode, ==, COLOR_CUSTOM);
    g_assert_cmpfloat_with_epsilon(loaded.custom_color.blue, .8, 1.0 / 255);
    g_assert_cmpint(loaded.visibility, ==, VISIBILITY_ALL);
    g_assert_cmpint(loaded.position, ==, DOCK_RIGHT);
    g_assert_cmpint(loaded.icon_size, ==, 72);
    g_assert_cmpint(loaded.monitor, ==, 2);
    source.show_desktop = FALSE;
    source.show_trash = TRUE;
    launcher_settings_write(&source, key);
    launcher_settings_read(&loaded, key);
    g_assert_false(loaded.show_desktop);
    g_assert_true(loaded.show_trash);
    /* Invalid hand-edited settings retain safe defaults or clamp to valid ranges. */
    g_key_file_set_string(key, "Launcher", "Opacity", "nan");
    g_key_file_set_integer(key, "Launcher", "HideAnimation", 99);
    g_key_file_set_integer(key, "Launcher", "IconSize", 900);
    g_key_file_set_double(key, "Launcher", "RevealSensitivity", -10);
    g_key_file_set_string(key, "Launcher", "CustomColor", "not-a-color");
    launcher_settings_defaults(&loaded);
    launcher_settings_read(&loaded, key);
    g_assert_cmpfloat(loaded.opacity, ==, .6667);
    g_assert_cmpint(loaded.hide_animation, ==, HIDE_FADE_SLIDE);
    g_assert_cmpint(loaded.icon_size, ==, 96);
    g_assert_cmpfloat(loaded.reveal_sensitivity, ==, 0);
    g_key_file_unref(key);
    /* Config files from builds before the settings dialog remain valid. */
    key = g_key_file_new();
    g_key_file_set_integer(key, "Launcher", "IconSize", 32);
    g_key_file_set_boolean(key, "Launcher", "Autohide", TRUE);
    launcher_settings_defaults(&loaded);
    launcher_settings_read(&loaded, key);
    g_assert_cmpint(loaded.icon_size, ==, 32);
    g_assert_true(loaded.autohide);
    g_assert_false(loaded.all_workspaces);
    g_assert_true(loaded.show_trash);
    g_assert_cmpfloat(loaded.opacity, ==, .6667);
    g_key_file_unref(key);
}

static void workspace_filter(void)
{
    LauncherSettings s;
    launcher_settings_defaults(&s);
    g_assert_true(launcher_settings_window_visible(&s, 3, 3));
    g_assert_true(launcher_settings_window_visible(&s, 0xffffffffUL, 3));
    g_assert_false(launcher_settings_window_visible(&s, 2, 3));
    s.all_workspaces = TRUE;
    g_assert_true(launcher_settings_window_visible(&s, 2, 3));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/lunatic/hide-state", hide_states);
    g_test_add_func("/lunatic/pressure-decay", pressure_decay);
    g_test_add_func("/lunatic/animation-reversal", animations);
    g_test_add_func("/lunatic/fold-and-shelf", layout);
    g_test_add_func("/lunatic/fixed-shelf-during-unfold-and-scroll", fixed_shelf);
    g_test_add_func("/lunatic/icon-color", colors);
    g_test_add_func("/xfce/desktop-matching", matching);
    g_test_add_func("/xfce/pin-persistence", configuration);
    g_test_add_func("/settings/persistence-and-migration", settings_persistence);
    g_test_add_func("/settings/workspace-filter", workspace_filter);
    return g_test_run();
}
