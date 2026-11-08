/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>

typedef enum { HIDE_SLIDE, HIDE_FADE, HIDE_FADE_SLIDE } LauncherHideAnimation;
typedef enum { COLOR_WALLPAPER, COLOR_CUSTOM } LauncherColorMode;
typedef enum { DOCK_LEFT, DOCK_RIGHT } LauncherPosition;
typedef enum { VISIBILITY_ALL, VISIBILITY_PRIMARY } LauncherVisibility;

typedef struct {
    gboolean autohide, minimize_on_click, all_workspaces, show_desktop, show_trash;
    LauncherHideAnimation hide_animation;
    double reveal_sensitivity, opacity;
    LauncherColorMode color_mode;
    GdkRGBA custom_color;
    LauncherVisibility visibility;
    LauncherPosition position;
    int icon_size, monitor;
    int window_sort; /* 0: recent focus, 1: creation, 2: title */
} LauncherSettings;

typedef void (*LauncherSettingsChanged)(const LauncherSettings *previous, gpointer data);
void launcher_settings_defaults(LauncherSettings *settings);
void launcher_settings_read(LauncherSettings *settings, GKeyFile *key);
void launcher_settings_write(const LauncherSettings *settings, GKeyFile *key);
GtkWidget *launcher_settings_dialog_new(GtkWindow *parent, LauncherSettings *settings,
                                        LauncherSettingsChanged changed, gpointer data);
gboolean launcher_settings_window_visible(const LauncherSettings *settings, unsigned long desktop,
                                          unsigned long current);
