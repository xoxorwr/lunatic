/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <glib.h>
#include <X11/Xlib.h>

typedef enum { PANEL_WINDOW_CLOSE, PANEL_WINDOW_MINIMIZE, PANEL_WINDOW_RESTORE } PanelWindowCommand;
typedef struct {
    Atom state, maximized_horz, maximized_vert, fullscreen, allowed_actions;
    Atom action_close, action_minimize, action_max_horz, action_max_vert;
    Atom close_window, change_state;
} PanelWindowAtoms;
typedef struct { gboolean maximized, fullscreen, close, minimize, restore; } PanelWindowControls;
void panel_window_atoms_init(Display *display, PanelWindowAtoms *atoms);
PanelWindowControls panel_window_controls_decode(const PanelWindowAtoms *atoms,
    const Atom *states, gsize state_count, const Atom *actions, gsize action_count, gboolean actions_present);
/* Build requests separately from sending so their target and payload are testable. */
gboolean panel_window_control_event(const PanelWindowAtoms *atoms, Window target,
    PanelWindowCommand command, guint32 timestamp, XEvent *event);
