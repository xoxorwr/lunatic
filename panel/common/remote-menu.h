/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gio/gio.h>
#define PANEL_TYPE_REMOTE_MENU (panel_remote_menu_get_type())
G_DECLARE_FINAL_TYPE(PanelRemoteMenu, panel_remote_menu, PANEL, REMOTE_MENU, GObject)
typedef void (*PanelRemoteEvent)(gint32 id, const char *event, gpointer data);
PanelRemoteMenu *panel_remote_menu_new(void);
GMenuModel *panel_remote_menu_model(PanelRemoteMenu *self);
GActionGroup *panel_remote_menu_actions(PanelRemoteMenu *self);
/* A complete DBusMenu (ia{sv}av) tree. Invalid trees leave the model intact. */
gboolean panel_remote_menu_update(PanelRemoteMenu *self, GVariant *layout);
void panel_remote_menu_set_event(PanelRemoteMenu *self, PanelRemoteEvent event, gpointer data);
