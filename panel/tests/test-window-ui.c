/* SPDX-License-Identifier: GPL-3.0-only
 * Exercise the plugin's real title-area construction and crossing handlers.
 */
#include "../xfce-plugin/global-menu.c"

static void cross(GtkWidget *widget, GdkEventType type, GdkNotifyType detail, double x, double y)
{
    GdkEvent *event = gdk_event_new(type);
    event->crossing.window = g_object_ref(gtk_widget_get_window(widget));
    event->crossing.send_event = TRUE;
    event->crossing.mode = GDK_CROSSING_NORMAL;
    event->crossing.detail = detail;
    event->crossing.x_root = x;
    event->crossing.y_root = y;
    GdkSeat *seat = gdk_display_get_default_seat(gtk_widget_get_display(widget));
    gdk_event_set_device(event, gdk_seat_get_pointer(seat));
    gtk_widget_event(widget, event);
    gdk_event_free(event);
}
static void clicked_counter(GtkButton *button, gpointer data)
{
    (void)button;
    (*(guint *)data)++;
}
static gboolean has_underline(GtkWidget *label)
{
    PangoAttrList *attributes = pango_layout_get_attributes(gtk_label_get_layout(GTK_LABEL(label)));
    if (!attributes) return FALSE;
    PangoAttrIterator *iter = pango_attr_list_get_iterator(attributes);
    gboolean found = FALSE;
    do {
        PangoAttribute *attribute = pango_attr_iterator_get(iter, PANGO_ATTR_UNDERLINE);
        if (attribute && ((PangoAttrInt *)attribute)->value != PANGO_UNDERLINE_NONE) {
            found = TRUE;
            break;
        }
    } while (pango_attr_iterator_next(iter));
    pango_attr_iterator_destroy(iter);
    return found;
}

static void menu_mnemonics(void)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *bar = gtk_menu_bar_new();
    gtk_container_add(GTK_CONTAINER(window), bar);
    GtkWidget *item = gtk_menu_item_new_with_mnemonic("_File");
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    gtk_widget_show_all(window);
    GtkWidget *label = gtk_bin_get_child(GTK_BIN(item));
    guint mnemonic = gtk_label_get_mnemonic_keyval(GTK_LABEL(label));
    panel_menu_bar_set_mnemonics(bar, FALSE);
    g_assert_false(has_underline(label));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, "File");
    GlobalMenu state = {.menu = bar, .alt_mask = Mod1Mask, .xkb_event = 100};
    XkbEvent keyboard = {0};
    keyboard.state.type = state.xkb_event;
    keyboard.state.xkb_type = XkbStateNotify;
    keyboard.state.mods = ShiftMask;
    window_event((GdkXEvent *)&keyboard, NULL, &state);
    g_assert_false(has_underline(label));
    keyboard.state.mods = Mod1Mask;
    window_event((GdkXEvent *)&keyboard, NULL, &state);
    g_assert_true(has_underline(label));
    g_assert_cmpuint(gtk_label_get_mnemonic_keyval(GTK_LABEL(label)), ==, mnemonic);
    /* Late model items and renamed labels inherit the current Alt state. */
    GtkWidget *late = gtk_menu_item_new_with_mnemonic("_Save __As");
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), late);
    GtkWidget *late_label = gtk_bin_get_child(GTK_BIN(late));
    g_assert_true(has_underline(late_label));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(late_label)), ==, "Save _As");
    keyboard.state.mods = 0;
    window_event((GdkXEvent *)&keyboard, NULL, &state);
    g_assert_false(has_underline(label));
    g_assert_false(has_underline(late_label));
    gtk_menu_item_set_label(GTK_MENU_ITEM(late), "_Édit");
    g_assert_false(has_underline(late_label));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(late_label)), ==, "Édit");
    /* Do not assume Alt always uses Mod1. */
    state.alt_mask = Mod3Mask;
    keyboard.state.mods = Mod3Mask;
    window_event((GdkXEvent *)&keyboard, NULL, &state);
    g_assert_true(has_underline(late_label));
    gtk_menu_item_set_label(GTK_MENU_ITEM(late), "__Literal");
    g_assert_false(has_underline(late_label));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(late_label)), ==, "_Literal");
    gtk_widget_destroy(window);
}

static void hover_controls(void)
{
    GlobalMenu state = {0};
    state.active = 123;
    state.window_controls = (PanelWindowControls){.maximized = TRUE, .close = TRUE,
                                                   .minimize = TRUE, .restore = TRUE};
    state.box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    state.label = panel_title_label_new(200);
    gtk_label_set_markup(GTK_LABEL(state.label), "<b>Editor</b> - Document");
    create_title_area(&state);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
    gtk_container_add(GTK_CONTAINER(window), state.box);
    gtk_widget_show_all(window);
    gtk_window_set_focus(GTK_WINDOW(window), NULL);
    if (state.focus_idle) { g_source_remove(state.focus_idle); state.focus_idle = 0; }
    gint root_x, root_y, x, y;
    gdk_window_get_origin(gtk_widget_get_window(window), &root_x, &root_y);
    g_assert_true(gtk_widget_translate_coordinates(state.title_zone, window, 0, 0, &x, &y));
    double inside_x = root_x + x + 2, inside_y = root_y + y + 2;
    /* GtkStack owns a GdkWindow: its event mask must request these events. */
    g_assert_true((gtk_widget_get_events(state.title_stack) & GDK_ENTER_NOTIFY_MASK) != 0);
    g_assert_true((gtk_widget_get_events(state.title_stack) & GDK_LEAVE_NOTIFY_MASK) != 0);
    reveal_window_controls(&state);
    g_assert_true(gtk_stack_get_visible_child(GTK_STACK(state.title_stack)) == state.label);
    gint before_min, before_nat, after_min, after_nat;
    gtk_widget_get_preferred_width(state.title_stack, &before_min, &before_nat);
    cross(state.title_stack, GDK_ENTER_NOTIFY, GDK_NOTIFY_NONLINEAR, inside_x, inside_y);
    g_assert_true(gtk_stack_get_visible_child(GTK_STACK(state.title_stack)) == state.controls);
    gtk_widget_get_preferred_width(state.title_stack, &after_min, &after_nat);
    g_assert_cmpint(before_min, ==, after_min);
    g_assert_cmpint(before_nat, ==, after_nat);
    /* Crossing onto children/stack replacement must not hide the buttons. */
    cross(state.title_zone, GDK_LEAVE_NOTIFY, GDK_NOTIFY_INFERIOR, inside_x, inside_y);
    cross(state.window_buttons[0], GDK_ENTER_NOTIFY, GDK_NOTIFY_ANCESTOR, inside_x, inside_y);
    g_assert_true(gtk_stack_get_visible_child(GTK_STACK(state.title_stack)) == state.controls);
    guint clicks = 0;
    g_signal_handlers_disconnect_by_func(state.window_buttons[0], G_CALLBACK(control_clicked), &state);
    g_signal_connect(state.window_buttons[0], "clicked", G_CALLBACK(clicked_counter), &clicks);
    gtk_widget_set_sensitive(state.window_buttons[0], TRUE);
    gtk_button_clicked(GTK_BUTTON(state.window_buttons[0]));
    g_assert_cmpuint(clicks, ==, 1);
    cross(state.window_buttons[0], GDK_LEAVE_NOTIFY, GDK_NOTIFY_NONLINEAR, root_x + x - 1, root_y + y - 1);
    g_assert_true(gtk_stack_get_visible_child(GTK_STACK(state.title_stack)) == state.label);
    state.window_controls.maximized = FALSE;
    cross(state.title_stack, GDK_ENTER_NOTIFY, GDK_NOTIFY_NONLINEAR, inside_x, inside_y);
    g_assert_true(gtk_stack_get_visible_child(GTK_STACK(state.title_stack)) == state.label);
    state.window_controls.maximized = TRUE;
    state.window_controls.fullscreen = TRUE;
    reveal_window_controls(&state);
    g_assert_true(gtk_stack_get_visible_child(GTK_STACK(state.title_stack)) == state.label);
    state.closing = TRUE;
    g_signal_handlers_disconnect_by_data(state.title_zone, &state);
    g_signal_handlers_disconnect_by_data(state.title_stack, &state);
    for (guint i = 0; i < 3; i++) g_signal_handlers_disconnect_by_data(state.window_buttons[i], &state);
    if (state.focus_idle) g_source_remove(state.focus_idle);
    gtk_widget_destroy(window);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!gtk_init_check(&argc, &argv)) { g_print("SKIP: window UI test needs a display\n"); return 77; }
    g_test_add_func("/panel/window-controls-hover", hover_controls);
    g_test_add_func("/panel/menu-alt-mnemonics", menu_mnemonics);
    return g_test_run();
}
