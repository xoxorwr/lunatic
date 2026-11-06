/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "application-launch.h"
#include <gtk/gtk.h>
#include <unistd.h>

static void detached_child(gpointer data)
{
    (void)data;
    /* GSpawn's intermediate child reaps/reparents the application. A new
     * session also removes its controlling terminal and launcher process group.
     * Only async-signal-safe operations are permitted in this child setup. */
    if (setsid() < 0)
        _exit(127);
}

static const GSpawnFlags detached_flags =
    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL;

gboolean launcher_launch_uris(GDesktopAppInfo *app, GList *uris, GAppLaunchContext *context,
                              GError **error)
{
    return g_desktop_app_info_launch_uris_as_manager(app, uris, context, detached_flags,
                                                     detached_child, NULL, NULL, NULL, error);
}

gboolean launcher_launch_application(GDesktopAppInfo *app, const char *action,
                                     GAppLaunchContext *context, guint32 timestamp, GError **error)
{
    if (!action)
        return launcher_launch_uris(app, NULL, context, error);

    /* GIO's desktop-action API has no child-setup hook. Run that API in a fresh,
     * detached executable, preserving desktop field codes, terminal settings,
     * working directories and D-Bus action activation. Never call GIO after a
     * raw fork of the multithreaded GTK process. */
    const char *filename = g_desktop_app_info_get_filename(app);
    if (!filename) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "The application has no desktop file");
        return FALSE;
    }
    char time[32];
    g_snprintf(time, sizeof(time), "%u", timestamp);
    char *argv[] = {
        "/proc/self/exe", "--launch-desktop-action", (char *)filename, (char *)action, time, NULL};
    return g_spawn_async(NULL, argv, NULL, detached_flags, detached_child, NULL, NULL, error);
}

int launcher_run_desktop_action(const char *filename, const char *action, guint32 timestamp)
{
    GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(filename);
    if (!app)
        return 1;
    const char *const *actions = g_desktop_app_info_list_actions(app);
    if (!actions || !g_strv_contains(actions, action)) {
        g_object_unref(app);
        return 1;
    }
    GAppLaunchContext *context;
    /* The same helper can be exercised without a display. In a desktop session
     * keep GDK startup notification and the original input timestamp. */
    if (gtk_init_check(NULL, NULL)) {
        GdkAppLaunchContext *gdk_context =
            gdk_display_get_app_launch_context(gdk_display_get_default());
        gdk_app_launch_context_set_timestamp(gdk_context, timestamp);
        context = G_APP_LAUNCH_CONTEXT(gdk_context);
    } else
        context = g_app_launch_context_new();
    /* Keep the same shared bus alive through action dispatch and flushing. */
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    g_desktop_app_info_launch_action(app, action, context);
    /* launch_action queues its D-Bus call without a completion callback. Flush
     * before this short-lived helper exits so activation is not dropped. */
    if (bus) {
        g_dbus_connection_flush_sync(bus, NULL, NULL);
        g_object_unref(bus);
    }
    g_object_unref(context);
    g_object_unref(app);
    return 0;
}
