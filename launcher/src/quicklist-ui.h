/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>

typedef struct {
    GtkWidget *widget;
    GtkWidget *label;
    GtkWidget *activate;
    GtkWidget *close;
    GtkWidget *visibility;
    GtkWidget *workspace;
} QuicklistWindowRow;

/* Widgets own their children. Callers connect actions to the returned buttons. */
GtkWidget *quicklist_popup_new(GtkWidget *anchor);
void quicklist_popup_present(GtkWidget *popup, const GdkRectangle *anchor,
                             const GdkRectangle *bounds, gboolean left);
void quicklist_popup_close(GtkWidget *popup);
void quicklist_popup_close_owner(GtkWidget *widget);
void quicklist_popup_bind_menu(GtkWidget *control, GtkWidget *menu);
GtkWidget *quicklist_action_new(const char *title, const char *icon);
GtkWidget *quicklist_window_group_new(void);
QuicklistWindowRow quicklist_window_row_new(const char *title, GIcon *icon, gboolean active,
                                            gboolean urgent);
