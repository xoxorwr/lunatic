/* SPDX-License-Identifier: GPL-3.0-only
 * GTK hover-list integration on a disposable nested X display.
 */
#define XSendEvent capture_send_event
#define main launcher_session_main
#include "../src/main.c"
#undef main

static Window activated;

Status capture_send_event(Display *display, Window window, Bool propagate, long mask, XEvent *event)
{
    (void)display;
    (void)window;
    (void)propagate;
    (void)mask;
    if (event->xclient.message_type == atom("_NET_ACTIVE_WINDOW"))
        activated = event->xclient.window;
    return 1;
}

static void pump(unsigned millis)
{
    gint64 until = g_get_monotonic_time() + millis * 1000;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < until);
}

static GtkWidget *find_window_button(GtkWidget *widget, Window xid)
{
    if (GTK_IS_BUTTON(widget) &&
        (Window)(guintptr)g_object_get_data(G_OBJECT(widget), "window-xid") == xid)
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *child = children; child && !found; child = child->next)
        found = find_window_button(child->data, xid);
    g_list_free(children);
    return found;
}

static void hover_window_list(void)
{
    launcher_settings_defaults(&dock.settings);
    dock.animation_fast = TRUE;
    dock.items = g_ptr_array_new_with_free_func(item_free);
    dock.views = g_ptr_array_new();
    dock.layout_scratch = g_array_new(FALSE, FALSE, sizeof(LayoutIcon));
    Item *item = add_item("hover-test.desktop", NULL, "Hover test");
    for (int i = 0; i < 3; i++) {
        Client *client = g_new0(Client, 1);
        client->xid = XCreateSimpleWindow(dock.display, dock.root, 400, 100, 50, 50, 0, 0, 0);
        client->title = g_strdup_printf("Window %d", i + 1);
        client->active = i == 1;
        g_ptr_array_add(item->clients, client);
    }
    DockView view = {.geometry = {0, 0, 800, 600}, .top = 24, .width = 61, .height = 576};
    hide_machine_init(&view.hide);
    dock.view = &view;
    g_ptr_array_add(dock.views, &view);
    view.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    create_icon_tooltip(&view);
    gtk_widget_show_all(view.window);
    item->layout.center = 300;
    const int sides[] = {DOCK_LEFT, DOCK_LEFT, DOCK_RIGHT, DOCK_RIGHT};
    for (guint repeat = 0; repeat < G_N_ELEMENTS(sides); repeat++) {
        int side = sides[repeat];
        dock.settings.position = side;
        view.hovered = NULL;
        set_hovered_item(item, g_get_monotonic_time() / 1000);
        g_assert_cmpuint(view.tooltip_timer, !=, 0);
        g_assert_null(view.window_list);
        pump(550);
        g_assert_nonnull(view.window_list);
        g_assert_true(gtk_widget_get_visible(view.window_list));
        g_assert_false(gtk_widget_get_visible(view.tooltip_window));
        g_assert_true(view.hide.quirks & QUICKLIST_OPEN);
        g_assert_true(view.window_list_item == item);
        int popup_x, popup_y;
        gdk_window_get_origin(gtk_widget_get_window(view.window_list), &popup_x, &popup_y);
        g_assert_cmpint(popup_x, ==, view.window_list_rect.x);
        g_assert_cmpint(popup_y, ==, view.window_list_rect.y);
        g_assert_cmpint(g_mkdir_with_parents("build/host", 0700), ==, 0);
        cairo_surface_t *surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, gtk_widget_get_allocated_width(view.window_list),
            gtk_widget_get_allocated_height(view.window_list));
        cairo_t *cr = cairo_create(surface);
        gtk_widget_draw(view.window_list, cr);
        cairo_surface_write_to_png(surface, side == DOCK_RIGHT ? "build/host/hover-right.png"
                                                               : "build/host/hover-left.png");
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_assert_true(side == DOCK_RIGHT
                          ? view.window_list_rect.x + view.window_list_rect.width < 739
                          : view.window_list_rect.x > 61);
        Window second = ((Client *)g_ptr_array_index(item->clients, 1))->xid;
        GtkWidget *button = find_window_button(view.window_list, second);
        g_assert_nonnull(button);
        g_assert_cmpstr(gtk_widget_get_tooltip_text(button), ==, "Window 2");
        g_assert_cmpint(gtk_widget_get_allocated_height(button), <=, 32);
        g_assert_true(
            gtk_style_context_has_class(gtk_widget_get_style_context(button), "active-window"));
        gtk_button_clicked(GTK_BUTTON(button));
        g_assert_cmpuint(activated, ==, second);
        g_assert_null(view.window_list);
        g_assert_false(view.hide.quirks & QUICKLIST_OPEN);
    }
    show_window_list(&view, item);
    Client *first = g_ptr_array_index(item->clients, 0);
    g_free(first->title);
    first->title = g_strdup("Renamed window");
    refresh_window_lists();
    GtkWidget *button = find_window_button(view.window_list, first->xid);
    g_assert_cmpstr(atk_object_get_name(gtk_widget_get_accessible(button)), ==, "Renamed window");
    g_ptr_array_set_size(item->clients, 1);
    refresh_window_lists();
    g_assert_null(view.window_list);
    /* A single window keeps the compact tooltip with the shared frame. */
    view.hovered = item;
    show_icon_tooltip(&view);
    g_assert_true(gtk_widget_get_visible(view.tooltip_window));
    g_assert_null(view.window_list);
    pump(30);
    cairo_surface_t *tooltip = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, gtk_widget_get_allocated_width(view.tooltip_window),
        gtk_widget_get_allocated_height(view.tooltip_window));
    cairo_t *tooltip_cr = cairo_create(tooltip);
    gtk_widget_draw(view.tooltip_window, tooltip_cr);
    cairo_surface_write_to_png(tooltip, "build/host/hover-single.png");
    cairo_destroy(tooltip_cr);
    cairo_surface_destroy(tooltip);
    hide_icon_tooltip(&view);
    gtk_widget_destroy(view.tooltip_window);
    g_object_unref(view.tooltip_window);
    view.tooltip_window = NULL;
    gtk_widget_destroy(view.window);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    g_array_unref(dock.layout_scratch);
    dock.views = NULL;
    dock.view = NULL;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!g_getenv("LUNATIC_TEST_NESTED_DISPLAY"))
        g_error("Set LUNATIC_TEST_NESTED_DISPLAY to a disposable nested X display");
    g_setenv("DISPLAY", g_getenv("LUNATIC_TEST_NESTED_DISPLAY"), TRUE);
    gtk_init(&argc, &argv);
    dock.gdisplay = gdk_display_get_default();
    dock.display = gdk_x11_display_get_xdisplay(dock.gdisplay);
    dock.root = DefaultRootWindow(dock.display);
    g_test_add_func("/gtk/hover-window-list", hover_window_list);
    return g_test_run();
}
