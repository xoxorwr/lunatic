/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gio/gio.h>

typedef struct {
    GMenu *model;
    GPtrArray *sections;
} PanelMenuModel;
typedef struct {
    PanelMenuModel *target;
    GPtrArray *sections;
} PanelMenuBuilder;

PanelMenuModel *panel_menu_model_new(void);
void panel_menu_model_free(PanelMenuModel *model);
void panel_menu_builder_init(PanelMenuBuilder *builder, PanelMenuModel *target);
void panel_menu_builder_append(PanelMenuBuilder *builder, GMenuItem *item);
void panel_menu_builder_separator(PanelMenuBuilder *builder);
void panel_menu_builder_finish(PanelMenuBuilder *builder);
