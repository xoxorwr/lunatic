/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>
GtkWidget *panel_dbus_menu_bar_new(GDBusConnection *bus, const char *service, const char *path);
