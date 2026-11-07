/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>

/* Control top-level mnemonic underlines without removing keyboard bindings.
 * Tracks model-created items and later label changes. */
void panel_menu_bar_set_mnemonics(GtkWidget *bar, gboolean visible);

/* Give popup rows a shared icon column, including rows without an icon. */
void panel_menu_bar_align_items(GtkWidget *bar);

/* Returned menubars have GTK's floating widget reference. Caller sinks and owns it. */
GtkWidget *panel_menu_bar_dbusmenu(GDBusConnection *connection, const char *service, const char *path);
GtkWidget *panel_menu_bar_gmenu(GDBusConnection *connection, const char *service,
                                 const char *menubar, const char *appmenu, const char *application,
                                 const char *window);
