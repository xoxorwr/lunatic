/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "menu-registry.h"

typedef struct PanelMenuRegistrar PanelMenuRegistrar;
/* Own the standard service only when available; never replace another registrar. */
PanelMenuRegistrar *panel_menu_registrar_new(GDBusConnection *connection, GError **error);
void panel_menu_registrar_free(PanelMenuRegistrar *registrar);
