/* SPDX-License-Identifier: GPL-3.0-only */
#include "title-label.h"

struct _PanelTitleLabel { GtkLabel parent; gint max_width; };
G_DEFINE_TYPE(PanelTitleLabel, panel_title_label, GTK_TYPE_LABEL)

static void preferred_width(GtkWidget *widget, gint *minimum, gint *natural)
{
    gint low, high;
    GTK_WIDGET_CLASS(panel_title_label_parent_class)->get_preferred_width(widget, &low, &high);
    gint cap = PANEL_TITLE_LABEL(widget)->max_width;
    *minimum = MIN(low, cap);
    *natural = MIN(high, cap);
}
static void preferred_width_for_height(GtkWidget *widget, gint height, gint *minimum, gint *natural)
{
    gint low, high;
    GTK_WIDGET_CLASS(panel_title_label_parent_class)->get_preferred_width_for_height(widget, height, &low, &high);
    gint cap = PANEL_TITLE_LABEL(widget)->max_width;
    *minimum = MIN(low, cap);
    *natural = MIN(high, cap);
}
static void size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    GtkAllocation capped = *allocation;
    capped.width = MIN(capped.width, PANEL_TITLE_LABEL(widget)->max_width);
    GTK_WIDGET_CLASS(panel_title_label_parent_class)->size_allocate(widget, &capped);
}
static void panel_title_label_class_init(PanelTitleLabelClass *klass)
{
    GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
    widget->get_preferred_width = preferred_width;
    widget->get_preferred_width_for_height = preferred_width_for_height;
    widget->size_allocate = size_allocate;
}
static void panel_title_label_init(PanelTitleLabel *self)
{
    self->max_width = PANEL_TITLE_WIDTH_DEFAULT;
    gtk_label_set_ellipsize(GTK_LABEL(self), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(self), TRUE);
    gtk_label_set_xalign(GTK_LABEL(self), 0.0f);
    gtk_widget_set_hexpand(GTK_WIDGET(self), FALSE);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_START);
}
void panel_title_label_set_max_width(PanelTitleLabel *label, gint max_width)
{
    g_return_if_fail(PANEL_IS_TITLE_LABEL(label));
    max_width = CLAMP(max_width, PANEL_TITLE_WIDTH_MIN, PANEL_TITLE_WIDTH_MAX);
    if (label->max_width == max_width) return;
    label->max_width = max_width;
    gtk_widget_queue_resize(GTK_WIDGET(label));
}
GtkWidget *panel_title_label_new(gint max_width)
{
    GtkWidget *widget = g_object_new(PANEL_TYPE_TITLE_LABEL, NULL);
    panel_title_label_set_max_width(PANEL_TITLE_LABEL(widget), max_width);
    return widget;
}
