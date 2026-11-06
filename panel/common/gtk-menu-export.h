/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>

#define PANEL_MENU_BUS_PROPERTY "_LUNATIC_PANEL_MENU_BUS"
#define PANEL_MENU_PATH_PROPERTY "_LUNATIC_PANEL_MENU_PATH"
#define PANEL_MENU_ACTIONS_PROPERTY "_LUNATIC_PANEL_MENU_ACTIONS"

typedef struct PanelGtkMenuExport PanelGtkMenuExport;
PanelGtkMenuExport *panel_gtk_menu_export_new(GtkMenuBar *menubar);
void panel_gtk_menu_export_free(PanelGtkMenuExport *exporter);
GMenuModel *panel_gtk_menu_export_model(PanelGtkMenuExport *exporter);
GActionGroup *panel_gtk_menu_export_actions(PanelGtkMenuExport *exporter);
void panel_gtk_menu_export_rebuild(PanelGtkMenuExport *exporter);
