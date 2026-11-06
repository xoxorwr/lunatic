/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <gtk/gtk.h>
#define PANEL_TYPE_TITLE_LABEL (panel_title_label_get_type())
G_DECLARE_FINAL_TYPE(PanelTitleLabel, panel_title_label, PANEL, TITLE_LABEL, GtkLabel)
#define PANEL_TITLE_WIDTH_DEFAULT 200
#define PANEL_TITLE_WIDTH_MIN 40
#define PANEL_TITLE_WIDTH_MAX 1200
GtkWidget *panel_title_label_new(gint max_width);
void panel_title_label_set_max_width(PanelTitleLabel *label, gint max_width);
