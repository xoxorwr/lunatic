/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gio/gio.h>
typedef struct PanelAppNames PanelAppNames;
PanelAppNames *panel_app_names_new(void);
void panel_app_names_free(PanelAppNames *names);
/* Rebuild from desktop applications; retains only names and lookup keys. */
void panel_app_names_load(PanelAppNames *names, GList *apps);
/* Borrowed name, valid until the next lookup/rebuild. */
const char *panel_app_names_find(PanelAppNames *names, const char *application_id,
                                 const char *instance, const char *wmclass);
char *panel_app_title(const char *program, const char *title);

/* Escaped Pango markup: program bold, document title normal. */
char *panel_app_title_markup(const char *program, const char *title);
