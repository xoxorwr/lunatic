/* Copyright (C) 2026 xoxorwr. SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>
#include "window-control.h"

typedef struct PanelWmTheme PanelWmTheme;
typedef enum { PANEL_WM_NORMAL, PANEL_WM_HOVER, PANEL_WM_PRESSED, PANEL_WM_DISABLED } PanelWmState;
/* Load xfwm4 artwork, including XPM symbols and optional image overlays. */
PanelWmTheme *panel_wm_theme_load(const char *directory, GHashTable *colors, gint scale);
PanelWmTheme *panel_wm_theme_new(const char *name, GtkWidget *style_widget);
PanelWmTheme *panel_wm_theme_ref(PanelWmTheme *theme);
void panel_wm_theme_unref(PanelWmTheme *theme);
cairo_surface_t *panel_wm_theme_frame(PanelWmTheme *theme, PanelWindowCommand command, PanelWmState state);
gint panel_wm_theme_spacing(PanelWmTheme *theme);
/* C/H/M from xfwm4's button_layout, in left-to-right order; omit other actions. */
guint panel_wm_button_order(const char *layout, PanelWindowCommand order[3]);
GtkWidget *panel_wm_button_new(PanelWindowCommand command);
void panel_wm_button_set_theme(GtkWidget *button, PanelWmTheme *theme);
