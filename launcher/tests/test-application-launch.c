/* SPDX-License-Identifier: GPL-3.0-only
 * Real subprocess regression: kill the launcher process group, then verify
 * both normal and desktop-action applications still run and update a heartbeat.
 */
#define _POSIX_C_SOURCE 200809L
#include "application-launch.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char *test_executable;

static void pause_ms(long ms)
{
    struct timespec delay = {ms / 1000, (ms % 1000) * 1000000};
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

static int probe(const char *directory, const char *kind)
{
    char *ready = g_build_filename(directory, "ready", NULL);
    char *heartbeat = g_build_filename(directory, "heartbeat", NULL);
    char *input = g_file_read_link("/proc/self/fd/0", NULL);
    char *output = g_file_read_link("/proc/self/fd/1", NULL);
    char *errors = g_file_read_link("/proc/self/fd/2", NULL);
    char *metadata = g_strdup_printf("%ld %ld %ld %ld %s %s %s %s", (long)getpid(), (long)getppid(),
                                     (long)getsid(0), (long)getpgrp(), input ? input : "?",
                                     output ? output : "?", errors ? errors : "?", kind);
    g_file_set_contents(ready, metadata, -1, NULL);
    g_free(metadata);
    g_free(input);
    g_free(output);
    g_free(errors);
    g_free(ready);
    for (guint i = 0; i < 500; i++) {
        char tick[32];
        g_snprintf(tick, sizeof(tick), "%u", i);
        g_file_set_contents(heartbeat, tick, -1, NULL);
        pause_ms(20);
    }
    g_free(heartbeat);
    return 0;
}

static guint heartbeat_value(const char *path)
{
    char *text = NULL;
    if (!g_file_get_contents(path, &text, NULL, NULL))
        return 0;
    guint value = (guint)g_ascii_strtoull(text, NULL, 10);
    g_free(text);
    return value;
}

static void launch_survives_group_shutdown(gboolean action, int shutdown_signal)
{
    GError *error = NULL;
    char *directory = g_dir_make_tmp("lunatic launch-life-XXXXXX", &error);
    g_assert_no_error(error);
    char *desktop = g_build_filename(directory, "probe.desktop", NULL);
    char *ready = g_build_filename(directory, "ready", NULL);
    char *heartbeat = g_build_filename(directory, "heartbeat", NULL);
    char *contents = g_strdup_printf("[Desktop Entry]\nType=Application\nName=Lifetime Probe\n"
                                     "Exec=\"%s\" --lifetime-probe \"%s\" normal\nActions=Probe;\n"
                                     "[Desktop Action Probe]\nName=Probe action\n"
                                     "Exec=\"%s\" --lifetime-probe \"%s\" action\n",
                                     test_executable, directory, test_executable, directory);
    g_assert_true(g_file_set_contents(desktop, contents, -1, &error));
    g_assert_no_error(error);
    g_free(contents);
    pid_t launcher = fork();
    g_assert_cmpint(launcher, >=, 0);
    if (!launcher) {
        if (setsid() < 0)
            _exit(20);
        GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(desktop);
        if (!app)
            _exit(21);
        if (!launcher_launch_application(app, action ? "Probe" : NULL, NULL, 0, NULL))
            _exit(22);
        g_object_unref(app);
        /* Simulate a running launcher until its whole terminal group is killed. */
        for (;;)
            pause();
    }
    char *metadata = NULL;
    for (int i = 0; i < 250 && !metadata; i++) {
        g_file_get_contents(ready, &metadata, NULL, NULL);
        if (!metadata)
            pause_ms(20);
    }
    if (!metadata) {
        kill(launcher, SIGKILL);
        waitpid(launcher, NULL, 0);
        g_error("Launched %s probe never became ready", action ? "action" : "normal");
    }
    long pid, parent, session, group;
    char in[256], out[256], err[256], kind[32];
    g_assert_cmpint(sscanf(metadata, "%ld %ld %ld %ld %255s %255s %255s %31s", &pid, &parent,
                           &session, &group, in, out, err, kind),
                    ==, 8);
    g_free(metadata);
    g_assert_cmpstr(kind, ==, action ? "action" : "normal");
    g_assert_cmpint(parent, !=, launcher);
    g_assert_cmpint(session, !=, launcher);
    g_assert_cmpint(group, !=, launcher);
    g_assert_cmpstr(in, ==, "/dev/null");
    g_assert_cmpstr(out, ==, "/dev/null");
    g_assert_cmpstr(err, ==, "/dev/null");
    guint before = heartbeat_value(heartbeat);
    /* Exercise terminal hangup and explicit launcher-group termination. */
    g_assert_cmpint(kill(-launcher, shutdown_signal), ==, 0);
    int status;
    g_assert_cmpint(waitpid(launcher, &status, 0), ==, launcher);
    g_assert_true(WIFSIGNALED(status));
    g_assert_cmpint(WTERMSIG(status), ==, shutdown_signal);
    g_assert_cmpint(kill((pid_t)pid, 0), ==, 0);
    pause_ms(100);
    g_assert_cmpuint(heartbeat_value(heartbeat), >, before);
    g_assert_cmpint(kill((pid_t)pid, SIGTERM), ==, 0);
    g_assert_cmpint(waitpid((pid_t)pid, &status, 0), ==, pid);
    /* The short-lived desktop-action helper can also be adopted by our test
     * subreaper. Reap it without leaving test children around. */
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    g_unlink(ready);
    g_unlink(heartbeat);
    g_unlink(desktop);
    g_rmdir(directory);
    g_free(ready);
    g_free(heartbeat);
    g_free(desktop);
    g_free(directory);
}

static void normal_lifetime(void)
{
    launch_survives_group_shutdown(FALSE, SIGHUP);
}
static void action_lifetime(void)
{
    launch_survives_group_shutdown(TRUE, SIGHUP);
}

static void normal_termination(void)
{
    launch_survives_group_shutdown(FALSE, SIGTERM);
}
static void action_termination(void)
{
    launch_survives_group_shutdown(TRUE, SIGTERM);
}

int main(int argc, char **argv)
{
    if (argc == 5 && g_str_equal(argv[1], "--launch-desktop-action"))
        return launcher_run_desktop_action(argv[2], argv[3],
                                           (guint32)g_ascii_strtoull(argv[4], NULL, 10));
    if (argc == 4 && g_str_equal(argv[1], "--lifetime-probe"))
        return probe(argv[2], argv[3]);
    g_test_init(&argc, &argv, NULL);
    test_executable = g_file_read_link("/proc/self/exe", NULL);
    g_assert_nonnull(test_executable);
    /* These tests exercise Exec launching independently of a desktop bus or X. */
    g_setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-lunatic-test-bus", TRUE);
    g_unsetenv("DISPLAY");
    g_unsetenv("WAYLAND_DISPLAY");
    g_assert_cmpint(prctl(PR_SET_CHILD_SUBREAPER, 1), ==, 0);
    g_test_add_func("/launch/normal-survives-launcher-exit", normal_lifetime);
    g_test_add_func("/launch/action-survives-launcher-exit", action_lifetime);
    g_test_add_func("/launch/normal-survives-group-termination", normal_termination);
    g_test_add_func("/launch/action-survives-group-termination", action_termination);
    int result = g_test_run();
    g_free(test_executable);
    return result;
}
