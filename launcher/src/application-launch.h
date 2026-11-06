/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gio/gdesktopappinfo.h>

gboolean launcher_launch_uris(GDesktopAppInfo *app, GList *uris, GAppLaunchContext *context,
                              GError **error);
gboolean launcher_launch_application(GDesktopAppInfo *app, const char *action,
                                     GAppLaunchContext *context, guint32 timestamp, GError **error);
/* Internal action helper, invoked before opening the launcher's UI/session. */
int launcher_run_desktop_action(const char *filename, const char *action, guint32 timestamp);
