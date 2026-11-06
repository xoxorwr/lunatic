/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "settings.h"
#include <gio/gdesktopappinfo.h>

char *launcher_normalize(const char *value);
int launcher_match_score(GDesktopAppInfo *app, const char *instance, const char *wmclass,
                         const char *application_id);
GDesktopAppInfo *launcher_find_app(GList *apps, const char *instance, const char *wmclass,
                                   const char *application_id);
typedef struct LauncherAppIndex LauncherAppIndex;
LauncherAppIndex *launcher_app_index_new(GList *apps);
void launcher_app_index_free(LauncherAppIndex *index);
GDesktopAppInfo *launcher_app_index_find(LauncherAppIndex *index, const char *instance,
                                         const char *wmclass, const char *application_id);
char **launcher_load_pins(const char *path, int *icon_size, int *monitor, gboolean *autohide,
                          GError **error);
gboolean launcher_save_pins(const char *path, char **pins, int icon_size, int monitor,
                            gboolean autohide, GError **error);

char **launcher_load_config(const char *path, LauncherSettings *settings, GError **error);
gboolean launcher_save_config(const char *path, char **pins, const LauncherSettings *settings,
                              GError **error);
