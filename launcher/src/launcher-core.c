/* Copyright (C) 2010-2012 Canonical Ltd.
 * Authored by Jason Smith, Jay Taoko, Didier Roche and Unity contributors.
 * C adaptation: see PORTING.md. SPDX-License-Identifier: GPL-3.0-only
 */
#include "launcher-core.h"
#include <math.h>
#include <string.h>

#define VISIBLE_REQUIRED                                                                           \
    (QUICKLIST_OPEN | EXTERNAL_DND_ACTIVE | INTERNAL_DND_ACTIVE | TRIGGER_BUTTON_SHOW |            \
     VERTICAL_SLIDE_ACTIVE | KEY_NAV_ACTIVE | PLACES_VISIBLE | SCALE_ACTIVE | EXPO_ACTIVE |        \
     MT_DRAG_OUT | LAUNCHER_PULSE | SHORTCUT_KEYS_VISIBLE)
#define SKIP_DELAY_QUIRK                                                                           \
    (EXTERNAL_DND_ACTIVE | DND_PUSHED_OFF | EXPO_ACTIVE | SCALE_ACTIVE | MT_DRAG_OUT |             \
     TRIGGER_BUTTON_SHOW)

static void ensure_hide_state(HideMachine *m, gboolean skip_delay, gint64 now)
{
    gboolean hide = m->mode != HIDE_NEVER;
    if (hide && (m->quirks & LOCK_HIDE))
        skip_delay = TRUE;
    else if (hide && (m->quirks & (EXTERNAL_DND_ACTIVE | DND_PUSHED_OFF)) ==
                         (EXTERNAL_DND_ACTIVE | DND_PUSHED_OFF))
        hide = TRUE;
    else if (hide) {
        guint show = VISIBLE_REQUIRED;
        if (m->quirks & LAUNCHER_HIDDEN)
            show |= REVEAL_PRESSURE_PASS;
        else if (m->quirks & MOUSE_MOVE_POST_REVEAL)
            show |= MOUSE_OVER_LAUNCHER;
        if (m->quirks & show)
            hide = FALSE;
    }
    if (!hide)
        m->hide_deadline = 0;
    if (m->should_hide == hide)
        return;
    if (hide && !skip_delay) {
        if (!m->hide_deadline)
            m->hide_deadline = now + 400;
    } else {
        m->should_hide = hide;
        m->hide_deadline = 0;
    }
}

void hide_machine_init(HideMachine *m)
{
    memset(m, 0, sizeof(*m));
    /* LauncherOptions.cpp */
    m->reveal_pressure = 2000;
    m->decay_rate = 1500;
}

void hide_machine_mode(HideMachine *m, HideMode mode, gint64 now)
{
    if (m->mode == mode)
        return;
    m->mode = mode;
    ensure_hide_state(m, TRUE, now);
}

void hide_machine_quirk(HideMachine *m, HideQuirk quirk, gboolean value, gint64 now)
{
    if (!!(m->quirks & quirk) == !!value)
        return;
    if (value)
        m->quirks |= quirk;
    else
        m->quirks &= ~quirk;
    ensure_hide_state(m, (quirk & SKIP_DELAY_QUIRK) != 0, now);
}

void hide_machine_pressure(HideMachine *m, int pressure, gint64 now)
{
    if (!m->pressure)
        m->decay_time = now;
    m->pressure += MAX(0, pressure);
    if (m->pressure > m->reveal_pressure) {
        hide_machine_quirk(m, REVEAL_PRESSURE_PASS, TRUE, now);
        hide_machine_quirk(m, MOUSE_MOVE_POST_REVEAL, TRUE, now);
        m->pressure = 0;
    }
    m->reveal_progress = (double)m->pressure / MAX(1, m->reveal_pressure);
}

void hide_machine_tick(HideMachine *m, gint64 now)
{
    if (m->pressure && now - m->decay_time >= 10) {
        /* Decaymulator::OnDecayTimeout runs every 10 ms. */
        gint64 ticks = (now - m->decay_time) / 10;
        m->pressure = (int)MAX(0, m->pressure - ticks * (m->decay_rate / 100));
        m->decay_time += ticks * 10;
        m->reveal_progress = (double)m->pressure / MAX(1, m->reveal_pressure);
    }
    if (m->hide_deadline && now >= m->hide_deadline) {
        m->hide_deadline = 0;
        ensure_hide_state(m, TRUE, now);
    }
}

gboolean launcher_should_hover(gboolean hidden, gboolean mouse_over, gboolean bfb_over,
                               gboolean key_nav, gboolean quicklist, gboolean in_action)
{
    return !hidden && (mouse_over || bfb_over || key_nav || quicklist || in_action);
}

void animation_to(Animation *a, double target, unsigned duration, gint64 now)
{
    if (a->target == target)
        return;
    animation_tick(a, now);
    a->from = a->value;
    a->target = target;
    a->start = now;
    /* StartOrReverse preserves the speed when the direction changes. */
    a->duration = (unsigned)round(duration * fabs(target - a->from));
    if (!a->duration)
        a->value = target;
}

gboolean animation_tick(Animation *a, gint64 now)
{
    if (a->value == a->target)
        return FALSE;
    double progress = a->duration ? CLAMP((double)(now - a->start) / a->duration, 0, 1) : 1;
    a->value = a->from + (a->target - a->from) * progress;
    return TRUE;
}

double launcher_drag_limiter(double delta)
{
    double result = (1 - pow(159.0 / 160, fabs(delta))) * 160;
    return delta >= 0 ? result : -result;
}

static void layout_group(LayoutIcon *icons, guint count, double tile, double extent, double hover,
                         double drag_delta)
{
    const double spacing = 5, folding_constant = .25;
    double sum = spacing, threshold = extent - tile / 2.5;
    for (guint i = 0; i < count; i++) {
        double size = (tile + spacing) * icons[i].visible;
        sum += size;
        double unfold = CLAMP(icons[i].unfolded + icons[i].active, 0, 1);
        threshold -= CLAMP(sum - extent, 0, size * 1.3) *
                     (folding_constant + (1 - folding_constant) * unfold);
    }
    if (sum - spacing <= extent) {
        threshold = extent;
        hover = 0;
    }
    double folded_size = tile * (folding_constant + (1 - folding_constant) * hover);
    double folded_spacing = spacing * (folding_constant + (1 - folding_constant) * hover);
    double center = spacing;
    double minimum = MIN(0, extent - sum);
    double delta = drag_delta;
    if (delta > 0)
        delta = launcher_drag_limiter(delta);
    else if (delta < minimum)
        delta = minimum + launcher_drag_limiter(delta - minimum);
    center += delta * hover;
    threshold += delta * hover;
    for (guint i = 0; i < count; i++) {
        LayoutIcon *icon = &icons[i];
        double folding = CLAMP((center + tile - threshold) / tile, 0, 1);
        folding *= 1 - CLAMP(icon->unfolded + icon->active, 0, 1);
        double half = folded_size / 2 + (tile / 2 - folded_size / 2) * (1 - folding);
        double overlap = CLAMP(
            (center + 2 * half * icon->visible + spacing * icon->visible - threshold) / tile, 0, 1);
        double gap = (spacing * (1 - overlap) + folded_spacing * overlap) * icon->visible;
        icon->hit_start = center;
        center += half * icon->visible;
        icon->center = round(center);
        icon->z = 300 * (1 - icon->visible) + 10 * (1 - hover) * folding;
        icon->rotation = -(1 - hover) * folding;
        center += half * icon->visible + gap;
        icon->hit_end = center;
    }
}

void launcher_layout(LayoutIcon *icons, guint count, double tile, double extent, double hover,
                     double drag_delta)
{
    guint shelf = 0;
    while (shelf < count && !icons[shelf].shelf)
        shelf++;
    double shelf_height = 0;
    for (guint i = shelf; i < count; i++)
        shelf_height += (tile + 5) * icons[i].visible;
    double app_extent = MAX(0, extent - shelf_height - (shelf < count ? 5 : 0));
    layout_group(icons, shelf, tile, MAX(1, app_extent), hover, drag_delta);
    if (shelf < count) {
        /* The system shelf is fixed; unfolding and scrolling only move apps. */
        layout_group(icons + shelf, count - shelf, tile, shelf_height + 5, 0, 0);
        for (guint i = shelf; i < count; i++) {
            icons[i].center += app_extent;
            icons[i].hit_start += app_extent;
            icons[i].hit_end += app_extent;
        }
        for (guint i = 0; i < shelf; i++)
            icons[i].hit_end = MIN(icons[i].hit_end, app_extent);
    }
}

double launcher_urgent_wiggle(double progress)
{
    return .15 * sin(G_PI * 12 * progress);
}
double launcher_urgent_pulse(double progress)
{
    return .5 + .5 * cos(G_PI * 6 * progress);
}
double launcher_starting_pulse(double progress)
{
    return .5 - .5 * cos(G_PI * 30 * progress);
}

void launcher_icon_colors(GdkPixbuf *pixbuf, double background[3], double glow[3])
{
    /* LauncherIcon::ColorForIcon, including the original byte truncation. */
    long totals[3] = {0};
    double total = 0;
    int channels = gdk_pixbuf_get_n_channels(pixbuf), stride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar *pixels = gdk_pixbuf_read_pixels(pixbuf);
    for (int x = 0; x < gdk_pixbuf_get_width(pixbuf); x++)
        for (int y = 0; y < gdk_pixbuf_get_height(pixbuf); y++) {
            const guchar *p = pixels + y * stride + x * channels;
            double saturation = (MAX(p[0], MAX(p[1], p[2])) - MIN(p[0], MIN(p[1], p[2]))) / 255.0;
            double relevance = .1 + .9 * (channels == 4 ? p[3] / 255.0 : 1) * saturation;
            for (int k = 0; k < 3; k++)
                totals[k] += (guchar)(p[k] * relevance);
            total += relevance * 255;
        }
    double r = totals[0] / total, g = totals[1] / total, b = totals[2] / total;
    double maximum = MAX(r, MAX(g, b)), minimum = MIN(r, MIN(g, b));
    double d = maximum - minimum, hue = 0, sat = maximum ? d / maximum : 0;
    if (d > 0) {
        if (maximum == r)
            hue = fmod((g - b) / d, 6);
        else if (maximum == g)
            hue = (b - r) / d + 2;
        else
            hue = (r - g) / d + 4;
        hue /= 6;
        if (hue < 0)
            hue += 1;
    }
    if (sat > .15)
        sat = .65;
    double h = hue * 6, c = sat, x = c * (1 - fabs(fmod(h, 2) - 1));
    double rgb[3] = {0};
    if (h < 1) {
        rgb[0] = c;
        rgb[1] = x;
    } else if (h < 2) {
        rgb[0] = x;
        rgb[1] = c;
    } else if (h < 3) {
        rgb[1] = c;
        rgb[2] = x;
    } else if (h < 4) {
        rgb[1] = x;
        rgb[2] = c;
    } else if (h < 5) {
        rgb[0] = x;
        rgb[2] = c;
    } else {
        rgb[0] = c;
        rgb[2] = x;
    }
    for (int k = 0; k < 3; k++) {
        glow[k] = rgb[k] + 1 - c;
        background[k] = glow[k] * .9;
    }
}
