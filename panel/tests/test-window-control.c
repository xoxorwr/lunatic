/* SPDX-License-Identifier: GPL-3.0-only */
#include "window-control.h"
#include <X11/Xutil.h>
static const PanelWindowAtoms atoms = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static void eligibility(void)
{
    const Atom full[] = {2, 3}, half[] = {2}, fullscreen[] = {2, 3, 4};
    const Atom allowed[] = {6, 7, 8, 9};
    PanelWindowControls c = panel_window_controls_decode(&atoms, NULL, 0, allowed, 4, TRUE);
    g_assert_false(c.maximized);
    g_assert_false(c.close);
    c = panel_window_controls_decode(&atoms, half, 1, allowed, 4, TRUE);
    g_assert_false(c.maximized);
    c = panel_window_controls_decode(&atoms, full, 2, allowed, 4, TRUE);
    g_assert_true(c.maximized);
    g_assert_true(c.close);
    g_assert_true(c.minimize);
    g_assert_true(c.restore);
    c = panel_window_controls_decode(&atoms, fullscreen, 3, allowed, 4, TRUE);
    g_assert_true(c.fullscreen);
    g_assert_false(c.close);
    g_assert_false(c.minimize);
    g_assert_false(c.restore);
    c = panel_window_controls_decode(&atoms, NULL, 0, NULL, 0, FALSE);
    g_assert_false(c.restore); /* Restoring removes eligibility immediately. */
}
static void restrictions(void)
{
    const Atom full[] = {2, 3}, close_only[] = {6}, half_restore[] = {6, 7, 8};
    PanelWindowControls c = panel_window_controls_decode(&atoms, full, 2, close_only, 1, TRUE);
    g_assert_true(c.close);
    g_assert_false(c.minimize);
    g_assert_false(c.restore);
    c = panel_window_controls_decode(&atoms, full, 2, half_restore, 3, TRUE);
    g_assert_true(c.minimize);
    g_assert_false(c.restore);
    c = panel_window_controls_decode(&atoms, full, 2, NULL, 0, TRUE);
    g_assert_false(c.close);
    c = panel_window_controls_decode(&atoms, full, 2, NULL, 0, FALSE);
    g_assert_true(c.close); /* Missing hints: let the WM decide. */
    g_assert_true(c.minimize);
    g_assert_true(c.restore);
}
static void requests(void)
{
    XEvent event;
    g_assert_true(panel_window_control_event(&atoms, 0x123456, PANEL_WINDOW_CLOSE, 42, &event));
    g_assert_cmpint(event.type, ==, ClientMessage);
    g_assert_cmpuint(event.xclient.window, ==, 0x123456);
    g_assert_cmpint(event.xclient.format, ==, 32);
    g_assert_cmpuint(event.xclient.message_type, ==, atoms.close_window);
    g_assert_cmpint(event.xclient.data.l[0], ==, 42);
    g_assert_cmpint(event.xclient.data.l[1], ==, 2);
    g_assert_true(panel_window_control_event(&atoms, 15, PANEL_WINDOW_RESTORE, 123, &event));
    g_assert_cmpuint(event.xclient.window, ==, 15);
    g_assert_cmpuint(event.xclient.message_type, ==, atoms.state);
    g_assert_cmpint(event.xclient.data.l[0], ==, 0);
    g_assert_cmpuint(event.xclient.data.l[1], ==, atoms.maximized_horz);
    g_assert_cmpuint(event.xclient.data.l[2], ==, atoms.maximized_vert);
    g_assert_cmpint(event.xclient.data.l[3], ==, 2);
    g_assert_true(panel_window_control_event(&atoms, 17, PANEL_WINDOW_MINIMIZE, 9, &event));
    g_assert_cmpuint(event.xclient.window, ==, 17);
    g_assert_cmpuint(event.xclient.message_type, ==, atoms.change_state);
    g_assert_cmpint(event.xclient.data.l[0], ==, IconicState);
    for (guint i = 1; i < 5; i++) g_assert_cmpint(event.xclient.data.l[i], ==, 0);
    g_assert_false(panel_window_control_event(&atoms, None, PANEL_WINDOW_CLOSE, 0, &event));
    g_assert_false(panel_window_control_event(&atoms, 1, 99, 0, &event));
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/window/eligibility", eligibility);
    g_test_add_func("/window/restrictions", restrictions);
    g_test_add_func("/window/requests", requests);
    return g_test_run();
}
