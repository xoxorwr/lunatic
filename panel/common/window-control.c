/* SPDX-License-Identifier: GPL-3.0-only */
#include "window-control.h"
#include <X11/Xutil.h>
#include <string.h>

void panel_window_atoms_init(Display *display, PanelWindowAtoms *a)
{
    a->state = XInternAtom(display, "_NET_WM_STATE", False);
    a->maximized_horz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    a->maximized_vert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    a->fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    a->allowed_actions = XInternAtom(display, "_NET_WM_ALLOWED_ACTIONS", False);
    a->action_close = XInternAtom(display, "_NET_WM_ACTION_CLOSE", False);
    a->action_minimize = XInternAtom(display, "_NET_WM_ACTION_MINIMIZE", False);
    a->action_max_horz = XInternAtom(display, "_NET_WM_ACTION_MAXIMIZE_HORZ", False);
    a->action_max_vert = XInternAtom(display, "_NET_WM_ACTION_MAXIMIZE_VERT", False);
    a->close_window = XInternAtom(display, "_NET_CLOSE_WINDOW", False);
    a->change_state = XInternAtom(display, "WM_CHANGE_STATE", False);
}
static gboolean contains(const Atom *values, gsize count, Atom atom)
{
    for (gsize i = 0; i < count; i++) if (values[i] == atom) return TRUE;
    return FALSE;
}
PanelWindowControls panel_window_controls_decode(const PanelWindowAtoms *a,
    const Atom *states, gsize ns, const Atom *actions, gsize na, gboolean present)
{
    PanelWindowControls controls = {0};
    controls.maximized = contains(states, ns, a->maximized_horz) && contains(states, ns, a->maximized_vert);
    controls.fullscreen = contains(states, ns, a->fullscreen);
    gboolean eligible = controls.maximized && !controls.fullscreen;
    controls.close = eligible && (!present || contains(actions, na, a->action_close));
    controls.minimize = eligible && (!present || contains(actions, na, a->action_minimize));
    controls.restore = eligible && (!present || (contains(actions, na, a->action_max_horz) &&
                                               contains(actions, na, a->action_max_vert)));
    return controls;
}
gboolean panel_window_control_event(const PanelWindowAtoms *a, Window target,
    PanelWindowCommand command, guint32 timestamp, XEvent *event)
{
    memset(event, 0, sizeof(*event));
    if (!target) return FALSE;
    event->xclient.type = ClientMessage;
    event->xclient.window = target;
    event->xclient.format = 32;
    switch (command) {
    case PANEL_WINDOW_CLOSE:
        event->xclient.message_type = a->close_window;
        event->xclient.data.l[0] = timestamp;
        event->xclient.data.l[1] = 2; /* pager/taskbar source */
        break;
    case PANEL_WINDOW_MINIMIZE:
        event->xclient.message_type = a->change_state;
        event->xclient.data.l[0] = IconicState;
        break;
    case PANEL_WINDOW_RESTORE:
        event->xclient.message_type = a->state;
        event->xclient.data.l[0] = 0; /* remove both maximized states */
        event->xclient.data.l[1] = a->maximized_horz;
        event->xclient.data.l[2] = a->maximized_vert;
        event->xclient.data.l[3] = 2;
        break;
    default: return FALSE;
    }
    return TRUE;
}
