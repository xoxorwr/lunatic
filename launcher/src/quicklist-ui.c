/* SPDX-License-Identifier: GPL-3.0-only
 * Quicklist widget layout. Keep every window in one box: title expands while
 * close, visibility and workspace controls keep their own fixed allocations.
 */
#include "quicklist-ui.h"
#include "quicklist.h"

static const char popup_css[] =
    "#lunatic-quicklist-popup { background: transparent; color: #f7f5f8; "
    "border: 0; box-shadow: none; }"
    "#lunatic-quicklist-popup label { color: inherit; text-shadow: none; }"
    "#lunatic-quicklist-popup button { background: transparent; background-image: none; "
    "color: inherit; border: 0; box-shadow: none; border-radius: 4px; padding: 6px 8px; "
    "min-width: 0; min-height: 0; }"
    "#lunatic-quicklist-popup button:hover, #lunatic-quicklist-popup button:focus "
    "{ background: rgba(150,75,205,0.4); }"
    "#lunatic-quicklist-popup .quicklist-window-activate:hover, "
    "#lunatic-quicklist-popup .quicklist-window-activate:focus { background: transparent; }"
    "#lunatic-quicklist-popup .quicklist-domain { font-size: 10px; font-weight: bold; "
    "color: #aaa1b0; margin: 6px 8px 4px; }"
    "#lunatic-quicklist-popup .quicklist-app { padding: 4px 8px 10px; }"
    "#lunatic-quicklist-popup .quicklist-app-name { font-size: 15px; font-weight: bold; }"
    "#lunatic-quicklist-popup .quicklist-app-detail { font-size: 11px; color: #bdb6c2; }"
    "#lunatic-quicklist-popup .quicklist-window-group { padding: 8px; "
    "background: rgba(255,255,255,0.045); border-radius: 6px; }"
    "#lunatic-quicklist-popup .quicklist-window-row { border-radius: 4px; }"
    "#lunatic-quicklist-popup .quicklist-icon-control { padding: 6px; }"
    "#lunatic-quicklist-popup .quicklist-close { opacity: 0; padding: 6px; transition: none; }"
    "#lunatic-quicklist-popup .quicklist-window-icon { transition: none; }"
    "#lunatic-quicklist-popup .quicklist-window-row:hover .quicklist-close, "
    "#lunatic-quicklist-popup .quicklist-window-row:selected .quicklist-close, "
    "#lunatic-quicklist-popup .quicklist-close:focus { opacity: 1; }"
    "#lunatic-quicklist-popup .quicklist-window-row:hover .quicklist-window-icon, "
    "#lunatic-quicklist-popup .quicklist-window-row:selected .quicklist-window-icon "
    "{ opacity: 0; }"
    "#lunatic-quicklist-popup separator { background: rgba(255,255,255,0.13); "
    "min-height: 1px; margin: 4px 0; }";

static void remove_popup_style(GtkWidget *popup, gpointer unused);

static GtkWidget *title_label(const char *title)
{
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 32);
    gtk_widget_set_hexpand(label, TRUE);
    return label;
}

typedef struct {
    gboolean grabbed, pointer_left;
    int arrow_y;
} PopupState;

static void popup_ungrab(GtkWidget *popup, gpointer unused)
{
    (void)unused;
    PopupState *state = g_object_get_data(G_OBJECT(popup), "quicklist-popup");
    if (state->grabbed) {
        state->grabbed = FALSE;
        gdk_seat_ungrab(gdk_display_get_default_seat(gtk_widget_get_display(popup)));
    }
    if (gtk_widget_has_grab(popup))
        gtk_grab_remove(popup);
}

static gboolean popup_grab(GtkWidget *popup)
{
    PopupState *state = g_object_get_data(G_OBJECT(popup), "quicklist-popup");
    if (state->grabbed)
        return TRUE;
    GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(popup));
    if (gdk_seat_grab(seat, gtk_widget_get_window(popup),
                      GDK_SEAT_CAPABILITY_ALL_POINTING | GDK_SEAT_CAPABILITY_KEYBOARD, TRUE, NULL,
                      NULL, NULL, NULL) != GDK_GRAB_SUCCESS)
        return FALSE;
    if (!gtk_widget_has_grab(popup))
        gtk_grab_add(popup);
    state->grabbed = TRUE;
    return TRUE;
}

void quicklist_popup_close(GtkWidget *popup)
{
    gtk_widget_hide(popup);
}

void quicklist_popup_close_owner(GtkWidget *widget)
{
    while (widget) {
        GtkWidget *popup = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);
        if (popup && g_object_get_data(G_OBJECT(popup), "quicklist-popup")) {
            quicklist_popup_close(popup);
            return;
        }
        GtkWidget *menu = gtk_widget_get_ancestor(widget, GTK_TYPE_MENU);
        widget = menu ? gtk_menu_get_attach_widget(GTK_MENU(menu)) : NULL;
    }
}

static gboolean popup_key(GtkWidget *popup, GdkEventKey *event, gpointer unused)
{
    (void)unused;
    if (event->keyval != GDK_KEY_Escape)
        return FALSE;
    quicklist_popup_close(popup);
    return TRUE;
}

static gboolean popup_press(GtkWidget *popup, GdkEventButton *event, gpointer unused)
{
    (void)unused;
    int x, y;
    gdk_window_get_origin(gtk_widget_get_window(popup), &x, &y);
    if (event->x_root >= x && event->x_root < x + gtk_widget_get_allocated_width(popup) &&
        event->y_root >= y && event->y_root < y + gtk_widget_get_allocated_height(popup))
        return FALSE;
    quicklist_popup_close(popup);
    return TRUE;
}

static gboolean popup_draw(GtkWidget *popup, cairo_t *cr, gpointer unused)
{
    (void)unused;
    PopupState *state = g_object_get_data(G_OBJECT(popup), "quicklist-popup");
    quicklist_paint_frame(cr, gtk_widget_get_allocated_width(popup),
                          gtk_widget_get_allocated_height(popup), state->pointer_left,
                          state->arrow_y);
    return FALSE;
}

GtkWidget *quicklist_popup_new(GtkWidget *anchor)
{
    GtkWidget *popup = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_set_name(popup, "lunatic-quicklist-popup");
    gtk_window_set_accept_focus(GTK_WINDOW(popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_transient_for(GTK_WINDOW(popup), GTK_WINDOW(gtk_widget_get_toplevel(anchor)));
    GdkVisual *visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(popup));
    if (visual)
        gtk_widget_set_visual(popup, visual);
    g_object_set_data_full(G_OBJECT(popup), "quicklist-popup", g_new0(PopupState, 1), g_free);
    g_signal_connect(popup, "hide", G_CALLBACK(popup_ungrab), NULL);
    g_signal_connect(popup, "destroy", G_CALLBACK(popup_ungrab), NULL);
    g_signal_connect(popup, "key-press-event", G_CALLBACK(popup_key), NULL);
    g_signal_connect(popup, "button-press-event", G_CALLBACK(popup_press), NULL);
    g_signal_connect(popup, "draw", G_CALLBACK(popup_draw), NULL);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, popup_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(popup), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set_data_full(G_OBJECT(popup), "quicklist-ui-css", css, g_object_unref);
    g_signal_connect(popup, "destroy", G_CALLBACK(remove_popup_style), NULL);
    return popup;
}

void quicklist_popup_present(GtkWidget *popup, const GdkRectangle *anchor,
                             const GdkRectangle *bounds, gboolean left)
{
    PopupState *state = g_object_get_data(G_OBJECT(popup), "quicklist-popup");
    GtkWidget *content = gtk_bin_get_child(GTK_BIN(popup));
    state->pointer_left = !left;
    gtk_widget_set_margin_start(content, left ? 12 : 24);
    gtk_widget_set_margin_end(content, left ? 24 : 12);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 12);
    gtk_widget_show_all(content);
    GtkRequisition size;
    gtk_widget_get_preferred_size(popup, NULL, &size);
    int x = left ? anchor->x - size.width : anchor->x + anchor->width;
    int y = anchor->y + anchor->height / 2 - size.height / 2;
    x = CLAMP(x, bounds->x, MAX(bounds->x, bounds->x + bounds->width - size.width));
    y = CLAMP(y, bounds->y, MAX(bounds->y, bounds->y + bounds->height - size.height));
    state->arrow_y = anchor->y + anchor->height / 2 - y;
    gtk_window_move(GTK_WINDOW(popup), x, y);
    gtk_widget_show(popup);
    gtk_widget_queue_draw(popup);
    /* A dock does not take X11 focus. Grab input for this popup without
     * activating the dock or changing the focused application. */
    if (!popup_grab(popup))
        quicklist_popup_close(popup);
}

static gboolean resume_popup(gpointer data)
{
    GtkWidget *popup = data;
    if (gtk_widget_get_visible(popup) && !gtk_widget_in_destruction(popup) && !popup_grab(popup))
        quicklist_popup_close(popup);
    return G_SOURCE_REMOVE;
}

static void submenu_done(GtkWidget *popup)
{
    /* GtkMenu owns a temporary seat grab. Restore the context grab after its
     * own deactivation has completed, unless its command closed the context. */
    PopupState *state = g_object_get_data(G_OBJECT(popup), "quicklist-popup");
    state->grabbed = FALSE;
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, resume_popup, g_object_ref(popup), g_object_unref);
}

void quicklist_popup_bind_menu(GtkWidget *control, GtkWidget *menu)
{
    GtkWidget *popup = gtk_widget_get_toplevel(control);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(control), menu);
    g_signal_connect_object(menu, "deactivate", G_CALLBACK(submenu_done), popup, G_CONNECT_SWAPPED);
}

static void remove_popup_style(GtkWidget *popup, gpointer unused)
{
    (void)unused;
    GtkCssProvider *css = g_object_get_data(G_OBJECT(popup), "quicklist-ui-css");
    gtk_style_context_remove_provider_for_screen(gtk_widget_get_screen(popup),
                                                 GTK_STYLE_PROVIDER(css));
}

GtkWidget *quicklist_action_new(const char *title, const char *icon)
{
    GtkWidget *button = gtk_button_new();
    atk_object_set_name(gtk_widget_get_accessible(button), title);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    if (icon)
        gtk_box_pack_start(GTK_BOX(box), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU),
                           FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), title_label(title), TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(button), box);
    return button;
}

GtkWidget *quicklist_window_group_new(void)
{
    GtkWidget *group = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_style_context_add_class(gtk_widget_get_style_context(group), "quicklist-window-group");
    return group;
}

static gboolean row_crossing(GtkWidget *row, GdkEventCrossing *event, gpointer unused)
{
    (void)unused;
    if (event->detail == GDK_NOTIFY_INFERIOR)
        return FALSE;
    if (event->type == GDK_ENTER_NOTIFY)
        gtk_widget_set_state_flags(row, GTK_STATE_FLAG_PRELIGHT, FALSE);
    else
        gtk_widget_unset_state_flags(row, GTK_STATE_FLAG_PRELIGHT);
    return FALSE;
}

static void row_focus_changed(GtkWidget *button, GParamSpec *spec, gpointer data)
{
    (void)spec;
    GtkWidget *row = data;
    GtkWidget *window = gtk_widget_get_toplevel(button);
    GtkWidget *focus = GTK_IS_WINDOW(window) ? gtk_window_get_focus(GTK_WINDOW(window)) : NULL;
    if (focus && gtk_widget_is_ancestor(focus, row))
        gtk_widget_set_state_flags(row, GTK_STATE_FLAG_SELECTED, FALSE);
    else
        gtk_widget_unset_state_flags(row, GTK_STATE_FLAG_SELECTED);
    gtk_widget_queue_draw(row);
}

static gboolean row_draw(GtkWidget *row, cairo_t *cr, gpointer unused)
{
    (void)unused;
    GtkStateFlags state = gtk_widget_get_state_flags(row);
    quicklist_paint_row(
        cr, gtk_widget_get_allocated_width(row), gtk_widget_get_allocated_height(row),
        gtk_style_context_has_class(gtk_widget_get_style_context(row), "quicklist-active"),
        state & (GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_SELECTED));
    return FALSE;
}

static GtkWidget *icon_control(const char *icon, const char *title, gboolean submenu)
{
    GtkWidget *button = submenu ? gtk_menu_button_new() : gtk_button_new();
    GtkWidget *old = gtk_bin_get_child(GTK_BIN(button));
    if (old)
        gtk_container_remove(GTK_CONTAINER(button), old);
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_MENU));
    gtk_widget_set_size_request(button, 30, 30);
    gtk_widget_set_tooltip_text(button, title);
    atk_object_set_name(gtk_widget_get_accessible(button), title);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "quicklist-icon-control");
    return button;
}

QuicklistWindowRow quicklist_window_row_new(const char *title, GIcon *icon, gboolean active,
                                            gboolean urgent)
{
    QuicklistWindowRow row = {0};
    row.widget = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(row.widget), FALSE);
    gtk_widget_add_events(row.widget, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(row.widget, "enter-notify-event", G_CALLBACK(row_crossing), NULL);
    g_signal_connect(row.widget, "leave-notify-event", G_CALLBACK(row_crossing), NULL);
    g_signal_connect(row.widget, "draw", G_CALLBACK(row_draw), NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(row.widget), "quicklist-window-row");
    if (active)
        gtk_style_context_add_class(gtk_widget_get_style_context(row.widget), "quicklist-active");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_container_add(GTK_CONTAINER(row.widget), box);
    GtkWidget *slot = gtk_overlay_new();
    gtk_widget_set_size_request(slot, 30, 30);
    GtkWidget *image = icon
                           ? gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_MENU)
                           : gtk_image_new_from_icon_name("view-grid-symbolic", GTK_ICON_SIZE_MENU);
    gtk_style_context_add_class(gtk_widget_get_style_context(image), "quicklist-window-icon");
    gtk_container_add(GTK_CONTAINER(slot), image);
    char *close_title = g_strdup_printf("Close %s", title);
    row.close = icon_control("window-close-symbolic", close_title, FALSE);
    g_free(close_title);
    gtk_style_context_add_class(gtk_widget_get_style_context(row.close), "quicklist-close");
    gtk_overlay_add_overlay(GTK_OVERLAY(slot), row.close);
    gtk_box_pack_start(GTK_BOX(box), slot, FALSE, FALSE, 0);
    row.activate = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(row.activate),
                                "quicklist-window-activate");
    row.label = title_label(title);
    gtk_container_add(GTK_CONTAINER(row.activate), row.label);
    atk_object_set_name(gtk_widget_get_accessible(row.activate), title);
    gtk_box_pack_start(GTK_BOX(box), row.activate, TRUE, TRUE, 0);
    if (urgent) {
        GtkWidget *attention =
            gtk_image_new_from_icon_name("dialog-warning-symbolic", GTK_ICON_SIZE_MENU);
        gtk_box_pack_start(GTK_BOX(box), attention, FALSE, FALSE, 0);
        gtk_widget_set_tooltip_text(row.activate, "This window requests attention");
    }
    row.visibility = icon_control("view-reveal-symbolic", "Visibility", TRUE);
    row.workspace = icon_control("view-grid-symbolic", "Move to Workspace", TRUE);
    gtk_box_pack_start(GTK_BOX(box), row.visibility, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row.workspace, FALSE, FALSE, 0);
    GtkWidget *controls[] = {row.activate, row.close, row.visibility, row.workspace};
    for (guint i = 0; i < G_N_ELEMENTS(controls); i++)
        g_signal_connect(controls[i], "notify::is-focus", G_CALLBACK(row_focus_changed),
                         row.widget);
    return row;
}
