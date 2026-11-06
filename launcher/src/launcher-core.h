/* Copyright (C) 2010-2012 Canonical Ltd.
 * C port of launcher/{Launcher,LauncherHideMachine,LauncherHoverMachine,
 * Decaymulator,LauncherIcon}.cpp. SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

typedef enum { HIDE_NEVER, AUTOHIDE } HideMode;

/* Values intentionally match the original LauncherHideMachine::HideQuirk. */
typedef enum {
    LAUNCHER_HIDDEN = 1 << 0,
    MOUSE_OVER_LAUNCHER = 1 << 1,
    QUICKLIST_OPEN = 1 << 2,
    EXTERNAL_DND_ACTIVE = 1 << 3,
    INTERNAL_DND_ACTIVE = 1 << 4,
    TRIGGER_BUTTON_SHOW = 1 << 5,
    DND_PUSHED_OFF = 1 << 6,
    MOUSE_MOVE_POST_REVEAL = 1 << 7,
    VERTICAL_SLIDE_ACTIVE = 1 << 8,
    KEY_NAV_ACTIVE = 1 << 9,
    PLACES_VISIBLE = 1 << 10,
    SCALE_ACTIVE = 1 << 11,
    EXPO_ACTIVE = 1 << 12,
    MT_DRAG_OUT = 1 << 13,
    REVEAL_PRESSURE_PASS = 1 << 14,
    LAUNCHER_PULSE = 1 << 15,
    LOCK_HIDE = 1 << 16,
    SHORTCUT_KEYS_VISIBLE = 1 << 17
} HideQuirk;

typedef struct {
    HideMode mode;
    guint quirks;
    gboolean should_hide;
    int pressure, reveal_pressure, decay_rate;
    double reveal_progress;
    gint64 hide_deadline, decay_time;
} HideMachine;

void hide_machine_init(HideMachine *machine);
void hide_machine_mode(HideMachine *machine, HideMode mode, gint64 now);
void hide_machine_quirk(HideMachine *machine, HideQuirk quirk, gboolean value, gint64 now);
void hide_machine_pressure(HideMachine *machine, int pressure, gint64 now);
void hide_machine_tick(HideMachine *machine, gint64 now);
gboolean launcher_should_hover(gboolean hidden, gboolean mouse_over, gboolean bfb_over,
                               gboolean key_nav, gboolean quicklist, gboolean in_action);

typedef struct {
    double value, from, target;
    gint64 start;
    unsigned duration;
} Animation;
void animation_to(Animation *animation, double target, unsigned duration, gint64 now);
gboolean animation_tick(Animation *animation, gint64 now);

typedef struct {
    double visible, unfolded, active;
    gboolean shelf;
    double center, z, rotation, hit_start, hit_end;
} LayoutIcon;

/* Launcher::RenderArgs and FillRenderArg, left launcher orientation.
 * Caller supplies animated quirk progress; results are logical pixels.
 */
void launcher_layout(LayoutIcon *icons, guint count, double tile_size, double extent, double hover,
                     double drag_delta);
double launcher_drag_limiter(double delta);
double launcher_urgent_wiggle(double progress);
double launcher_urgent_pulse(double progress);
double launcher_starting_pulse(double progress);
void launcher_icon_colors(GdkPixbuf *pixbuf, double background[3], double glow[3]);
