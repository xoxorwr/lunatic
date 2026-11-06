/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gio/gio.h>

#define PANEL_REGISTRAR_NAME "com.canonical.AppMenu.Registrar"
#define PANEL_REGISTRAR_PATH "/com/canonical/AppMenu/Registrar"

typedef struct {
    guint32 window;
    char *service, *path;
} PanelMenuEntry;
typedef struct PanelMenuRegistry PanelMenuRegistry;

PanelMenuRegistry *panel_menu_registry_new(void);
void panel_menu_registry_free(PanelMenuRegistry *registry);
gboolean panel_menu_registry_register(PanelMenuRegistry *registry, guint32 window,
                                      const char *service, const char *path, GError **error);
gboolean panel_menu_registry_unregister(PanelMenuRegistry *registry, guint32 window,
                                        const char *service, GError **error);
const PanelMenuEntry *panel_menu_registry_lookup(PanelMenuRegistry *registry, guint32 window);
GArray *panel_menu_registry_remove_owner(PanelMenuRegistry *registry, const char *service);
GVariant *panel_menu_registry_list(PanelMenuRegistry *registry);
