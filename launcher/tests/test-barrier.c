/* SPDX-License-Identifier: GPL-3.0-only
 * Run only on a disposable nested X server: this test moves its pointer.
 */
#define main launcher_session_main
#include "../src/main.c"
#undef main
#include <X11/extensions/XTest.h>

static void pump(unsigned millis)
{
    gint64 until = g_get_monotonic_time() + millis * 1000;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < until);
}

static void move_pointer(int x, int y)
{
    XWarpPointer(dock.display, None, dock.root, 0, 0, 0, 0, x, y);
    XSync(dock.display, False);
    pump(20);
}

static void push_pointer(int dx)
{
    XTestFakeRelativeMotionEvent(dock.display, dx, 0, CurrentTime);
    XFlush(dock.display);
    pump(20);
}

static void reset_hidden(DockView *view)
{
    hide_machine_init(&view->hide);
    hide_machine_mode(&view->hide, AUTOHIDE, g_get_monotonic_time() / 1000);
    hide_machine_quirk(&view->hide, LAUNCHER_HIDDEN, TRUE, g_get_monotonic_time() / 1000);
    view->hidden = TRUE;
    configure_barrier(view);
}

static void barrier_input(void)
{
    DockView view = {
        .geometry = {200, 0, 400, 600}, .top = 24, .height = 576, .width = 61, .scale = 1};
    dock.views = g_ptr_array_new();
    g_ptr_array_add(dock.views, &view);
    /* These tests drive pressure independently of GTK's drawing loop. */
    dock.animation_fast = TRUE;
    launcher_settings_defaults(&dock.settings);
    dock.settings.autohide = TRUE;
    init_barriers();
    g_assert_nonnull(dock.barrier_display);
    for (int side = DOCK_LEFT; side <= DOCK_RIGHT; side++) {
        dock.settings.position = side;
        int edge = side == DOCK_RIGHT ? 600 : 200;
        int direction = side == DOCK_RIGHT ? 1 : -1;
        reset_hidden(&view);
        g_assert_cmpuint(view.barrier, !=, 0);
        move_pointer(edge - direction * 30, 300);
        /* Approach slowly enough to avoid Unity's fast-crossing bypass. */
        for (int i = 0; i < 8; i++)
            push_pointer(direction * 5);
        g_assert_cmpint(view.barrier_last_event, !=, 0);
        pump(150);
        g_assert_true(view.hide.should_hide);
        int pressure = view.hide.pressure;
        pump(200);
        g_assert_true(view.hide.should_hide);
        g_assert_cmpint(view.hide.pressure, <=, pressure);
        /* Continued physical pushes, not time at the edge, must reveal. */
        for (int i = 0; i < 100 && view.hide.should_hide; i++)
            push_pointer(direction * 40);
        g_assert_false(view.hide.should_hide);
        g_test_message("%s edge: stationary contact stayed hidden; continued pushes revealed",
                       side == DOCK_RIGHT ? "right" : "left");
        destroy_barrier(&view);
        /* Feed a controlled fast XI event for the current server contact.
         * XTest clips large relative moves and cannot reliably generate a
         * specified barrier velocity. Verify release with a real move. */
        reset_hidden(&view);
        move_pointer(edge - direction * 20, 300);
        push_pointer(direction * 40);
        g_assert_cmpuint(view.barrier_last_event, !=, 0);
        XIBarrierEvent fast = {.deviceid = view.barrier_device,
                               .barrier = view.barrier,
                               .eventid = view.barrier_last_event,
                               .dx = direction * 100,
                               .dtime = 1};
        handle_barrier_hit(&view, &fast);
        XSync(dock.barrier_display, False);
        push_pointer(direction * 40);
        pump(100);
        Window root, child;
        int x, y, win_x, win_y;
        unsigned buttons;
        XQueryPointer(dock.display, dock.root, &root, &child, &x, &y, &win_x, &win_y, &buttons);
        g_test_message("released contact %u on device %d: pointer x=%d, edge=%d",
                       view.barrier_last_event, view.barrier_device, x, edge);
        g_assert_true(direction > 0 ? x >= edge : x < edge);
        g_assert_true(view.hide.should_hide);
        g_assert_cmpint(view.hide.pressure, ==, 0);
        destroy_barrier(&view);
        g_assert_cmpuint(view.barrier, ==, 0);
        g_assert_cmpuint(view.pressure_timer, ==, 0);
        g_assert_cmpuint(view.smoothing_timer, ==, 0);
    }
    dock.settings.autohide = FALSE;
    configure_barrier(&view);
    g_assert_cmpuint(view.barrier, ==, 0);
    g_source_remove(dock.barrier_source);
    XCloseDisplay(dock.barrier_display);
    dock.barrier_display = NULL;
    dock.barrier_source = 0;
    g_ptr_array_unref(dock.views);
    dock.views = NULL;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!g_getenv("LUNATIC_TEST_NESTED_DISPLAY"))
        g_error("Set LUNATIC_TEST_NESTED_DISPLAY to a disposable nested X display");
    g_setenv("DISPLAY", g_getenv("LUNATIC_TEST_NESTED_DISPLAY"), TRUE);
    gtk_init(&argc, &argv);
    dock.gdisplay = gdk_display_get_default();
    dock.display = gdk_x11_display_get_xdisplay(dock.gdisplay);
    dock.root = DefaultRootWindow(dock.display);
    g_test_add_func("/x11/barrier-pressure", barrier_input);
    return g_test_run();
}
