/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>

/* Returned menubars have GTK's floating widget reference. Caller sinks and owns it. */
GtkWidget *panel_menu_bar_dbusmenu(GDBusConnection *connection, const char *service, const char *path);
GtkWidget *panel_menu_bar_gmenu(GDBusConnection *connection, const char *service,
                                 const char *menubar, const char *appmenu, const char *application,
                                 const char *window);
