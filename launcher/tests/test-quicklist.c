/* SPDX-License-Identifier: GPL-3.0-only
 * Integration test: run on an isolated X11 desktop with a compliant WM.
 * DISPLAY=:78 ./tests/test-quicklist
 */
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
/* Xephyr's RandR backend exposes a single output to GTK. Exercise the real
 * all-monitor adapter with two controlled work areas on that output. */
static gboolean split_monitors;
static int test_monitor_count(GdkDisplay *display);
static GdkMonitor *test_monitor(GdkDisplay *display, int index);
static void test_workarea(GdkMonitor *monitor, GdkRectangle *area);
#define gdk_display_get_n_monitors test_monitor_count
#define gdk_display_get_monitor test_monitor
#define gdk_monitor_get_workarea test_workarea
#define main launcher_session_main
#include "../src/main.c"
#undef main
#undef gdk_display_get_n_monitors
#undef gdk_display_get_monitor
#undef gdk_monitor_get_workarea

static int test_monitor_count(GdkDisplay *display)
{
    return split_monitors ? 2 : gdk_display_get_n_monitors(display);
}
static GdkMonitor *test_monitor(GdkDisplay *display, int index)
{
    return split_monitors ? (GdkMonitor *)(guintptr)(index + 1)
                          : gdk_display_get_monitor(display, index);
}
static void test_workarea(GdkMonitor *monitor, GdkRectangle *area)
{
    if ((guintptr)monitor <= 2) {
        GdkRectangle actual;
        gdk_monitor_get_workarea(gdk_display_get_monitor(dock.gdisplay, 0), &actual);
        guint i = (guintptr)monitor - 1;
        *area = (GdkRectangle){actual.x + i * actual.width / 2, actual.y + 32, actual.width / 2,
                               actual.height - 32};
    } else
        gdk_monitor_get_workarea(monitor, area);
}

static void mark_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    *(gboolean *)data = TRUE;
}

static void settle(void)
{
    gint64 until = g_get_monotonic_time() + 180000;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < until);
}

static GtkWidget *new_window(const char *title, int x, int y)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), 260, 180);
    gtk_window_move(GTK_WINDOW(window), x, y);
    gtk_widget_show_all(window);
    return window;
}

static void snapshot_clients(Item *item)
{
    Window active = cardinal(dock.root, "_NET_ACTIVE_WINDOW", None);
    for (guint i = 0; i < item->clients->len; i++) {
        Client *c = item->clients->pdata[i];
        c->desktop = cardinal(c->xid, "_NET_WM_DESKTOP", 0);
        c->active = c->xid == active;
    }
}

static gboolean rect_equal(const GdkRectangle *a, const GdkRectangle *b)
{
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

static void check_planner(void)
{
    GdkRectangle areas[] = {{-1200, 30, 1200, 870}, {0, 24, 800, 1100}, {800, 0, 1000, 700}};
    for (guint a = 0; a < G_N_ELEMENTS(areas); a++)
        for (guint n = 1; n < 24; n++)
            for (int mode = TILE_COLUMNS; mode <= TILE_SMART; mode++) {
                GdkRectangle rects[24];
                g_assert_true(window_layout_plan(mode, &areas[a], n, rects));
                for (guint i = 0; i < n; i++) {
                    g_assert_cmpint(rects[i].x, >=, areas[a].x);
                    g_assert_cmpint(rects[i].y, >=, areas[a].y);
                    g_assert_cmpint(rects[i].x + rects[i].width, <=, areas[a].x + areas[a].width);
                    g_assert_cmpint(rects[i].y + rects[i].height, <=, areas[a].y + areas[a].height);
                    for (guint j = 0; j < i; j++)
                        g_assert_false(gdk_rectangle_intersect(&rects[i], &rects[j], NULL));
                }
            }
    TileWindow fixed = {.hints = {.flags = PMinSize | PResizeInc | PBaseSize,
                                  .min_width = 300,
                                  .min_height = 200,
                                  .base_width = 10,
                                  .base_height = 10,
                                  .width_inc = 8,
                                  .height_inc = 16}};
    GdkRectangle tiny = {0, 0, 200, 100}, cell = {0, 0, 650, 470};
    g_assert_false(tile_fit(&fixed, &tiny));
    g_assert_true(tile_fit(&fixed, &cell));
    g_assert_cmpint((cell.width - 10) % 8, ==, 0);
    g_assert_cmpint((cell.height - 10) % 16, ==, 0);
    Client first = {.title = "Alpha", .created = 1, .focused = 2};
    Client second = {.title = "Beta", .created = 2, .focused = 5};
    Client *a = &first, *b = &second;
    g_assert_cmpint(compare_window_rows(&a, &b, GINT_TO_POINTER(0)), >, 0);
    g_assert_cmpint(compare_window_rows(&a, &b, GINT_TO_POINTER(1)), <, 0);
    g_assert_cmpint(compare_window_rows(&a, &b, GINT_TO_POINTER(2)), <, 0);
    GKeyFile *key = g_key_file_new();
    LauncherSettings settings, loaded;
    launcher_settings_defaults(&settings);
    g_assert_cmpint(settings.window_sort, ==, 0);
    settings.window_sort = 2;
    launcher_settings_write(&settings, key);
    launcher_settings_defaults(&loaded);
    launcher_settings_read(&loaded, key);
    g_assert_cmpint(loaded.window_sort, ==, 2);
    g_key_file_unref(key);
    g_print("Planner: 345 geometry cases, constraints, sorting and persistence passed\n");
}

int main(int argc, char **argv)
{
    check_planner();
    if (!gtk_init_check(&argc, &argv))
        return 77;
    dock.gdisplay = gdk_display_get_default();
    dock.display = gdk_x11_display_get_xdisplay(dock.gdisplay);
    dock.root = DefaultRootWindow(dock.display);
    dock.current_desktop = cardinal(dock.root, "_NET_CURRENT_DESKTOP", 0);
    launcher_settings_defaults(&dock.settings);
    g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, NULL);
    DockView view = {.menu_pointer_left = TRUE,
                     .screen = gdk_display_get_monitor(dock.gdisplay, 0)};
    dock.view = &view;
    view.items_box = gtk_offscreen_window_new();
    gtk_widget_realize(view.items_box);
    GKeyFile *app_key = g_key_file_new();
    g_key_file_load_from_data(app_key,
                              "[Desktop Entry]\nType=Application\nName=Test "
                              "application\nExec=true\nIcon=system-file-manager\nActions=Home;\n["
                              "Desktop Action Home]\nName=Home\nExec=true\n",
                              -1, 0, NULL);
    Item item = {.name = "Test application",
                 .pinned = TRUE,
                 .app = g_desktop_app_info_new_from_keyfile(app_key)};
    item.clients = g_ptr_array_new_with_free_func(client_free);
    GtkWidget *widgets[4];
    gboolean destroyed[4] = {0};
    for (guint i = 0; i < 4; i++) {
        widgets[i] = new_window(
            i ? "Alpha"
              : "A very long window title that must be truncated without widening the menu",
            80 + i * 50, 100 + i * 45);
        g_signal_connect(widgets[i], "destroy", G_CALLBACK(mark_destroyed), &destroyed[i]);
        Client *c = g_new0(Client, 1);
        c->xid = GDK_WINDOW_XID(gtk_widget_get_window(widgets[i]));
        c->created = i + 1;
        c->focused = 4 - i;
        c->title = g_strdup(gtk_window_get_title(GTK_WINDOW(widgets[i])));
        g_ptr_array_add(item.clients, c);
    }
    GtkWidget *unrelated = new_window("Unrelated application", 400, 400);
    settle();
    snapshot_clients(&item);
    Window outside_xid = GDK_WINDOW_XID(gtk_widget_get_window(unrelated));
    Client outside = {.xid = outside_xid, .desktop = dock.current_desktop};
    TileWindow untouched;
    g_assert_true(tile_snapshot(&outside, &untouched));
    guint monitors = 2;
    g_print("X11 real window manager, two simulated monitor work areas with top panels\n");
    GtkWidget *action = gtk_menu_item_new();
    g_object_ref_sink(action);
    g_object_set_data(G_OBJECT(action), "tile-view", &view);
    for (guint scope = 0; scope < (monitors > 1 ? 2u : 1u); scope++) {
        dock.tile_all_monitors = scope;
        split_monitors = scope > 0;
        for (guint mode = TILE_COLUMNS; mode <= TILE_SMART; mode++) {
            GArray *before = tile_windows(&item);
            g_assert_cmpuint(before->len, ==, 4);
            GdkRectangle planned[4];
            g_assert_true(tile_plan(before, &view, mode, planned));
            g_object_set_data(G_OBJECT(action), "tile-layout", GUINT_TO_POINTER(mode));
            menu_tile(GTK_MENU_ITEM(action), &item);
            settle();
            for (guint i = 0; i < 4; i++) {
                TileWindow after;
                g_assert_true(tile_snapshot(item.clients->pdata[i], &after));
                if (!rect_equal(&after.geometry, &planned[i]))
                    g_error("scope=%u mode=%u window=%u got=%d,%d %dx%d expected=%d,%d %dx%d",
                            scope, mode, i, after.geometry.x, after.geometry.y,
                            after.geometry.width, after.geometry.height, planned[i].x, planned[i].y,
                            planned[i].width, planned[i].height);
            }
            menu_restore_layout(GTK_MENU_ITEM(action), &item);
            settle();
            for (guint i = 0; i < 4; i++) {
                TileWindow after;
                g_assert_true(tile_snapshot(item.clients->pdata[i], &after));
                g_assert_true(
                    rect_equal(&after.geometry, &g_array_index(before, TileWindow, i).geometry));
            }
            TileWindow outside_after;
            g_assert_true(tile_snapshot(&outside, &outside_after));
            g_assert_true(rect_equal(&untouched.geometry, &outside_after.geometry));
            g_array_unref(before);
        }
    }
    split_monitors = FALSE;
    dock.tile_all_monitors = FALSE;
    g_print(
        "All five layouts and restore passed for each monitor scope; unrelated window unchanged\n");
    GPtrArray *many = item.clients;
    item.clients = g_ptr_array_new();
    g_ptr_array_add(item.clients, many->pdata[0]);
    for (guint mode = TILE_COLUMNS; mode <= TILE_SMART; mode++) {
        g_object_set_data(G_OBJECT(action), "tile-layout", GUINT_TO_POINTER(mode));
        menu_tile(GTK_MENU_ITEM(action), &item);
        settle();
        menu_restore_layout(GTK_MENU_ITEM(action), &item);
        settle();
    }
    g_ptr_array_unref(item.clients);
    item.clients = many;
    g_print("One-window layouts and restore passed\n");
    gtk_window_iconify(GTK_WINDOW(widgets[0]));
    settle();
    TileWindow minimized;
    g_assert_false(tile_snapshot(item.clients->pdata[0], &minimized));
    menu_tile(GTK_MENU_ITEM(action), &item);
    settle();
    g_assert_true(
        has_atom(((Client *)item.clients->pdata[0])->xid, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN"));
    menu_restore_layout(GTK_MENU_ITEM(action), &item);
    settle();
    g_assert_true(
        has_atom(((Client *)item.clients->pdata[0])->xid, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN"));
    gtk_window_deiconify(GTK_WINDOW(widgets[0]));
    settle();
    gtk_window_maximize(GTK_WINDOW(widgets[0]));
    settle();
    g_assert_true(has_atom(((Client *)item.clients->pdata[0])->xid, "_NET_WM_STATE",
                           "_NET_WM_STATE_MAXIMIZED_HORZ"));
    menu_tile(GTK_MENU_ITEM(action), &item);
    settle();
    menu_restore_layout(GTK_MENU_ITEM(action), &item);
    settle();
    g_assert_true(has_atom(((Client *)item.clients->pdata[0])->xid, "_NET_WM_STATE",
                           "_NET_WM_STATE_MAXIMIZED_HORZ"));
    g_print("Maximized state restored\n");
    /* Window restrictions and off-workspace windows are excluded at action time. */
    gtk_window_set_resizable(GTK_WINDOW(widgets[1]), FALSE);
    settle();
    TileWindow unavailable;
    g_assert_false(tile_snapshot(item.clients->pdata[1], &unavailable));
    gtk_window_set_resizable(GTK_WINDOW(widgets[1]), TRUE);
    settle();
    if (cardinal(dock.root, "_NET_NUMBER_OF_DESKTOPS", 1) > 1) {
        Client *other = item.clients->pdata[1];
        move_window_to_workspace(other->xid, (dock.current_desktop + 1) %
                                                 cardinal(dock.root, "_NET_NUMBER_OF_DESKTOPS", 1));
        settle();
        g_assert_false(tile_snapshot(other, &unavailable));
        move_window_to_workspace(other->xid, dock.current_desktop);
        settle();
    }
    GtkWidget *menu = build_quicklist(&item, &view);
    gtk_widget_show_all(menu);
    gtk_widget_show(menu);
    gtk_widget_realize(menu);
    GtkRequisition size;
    gtk_widget_get_preferred_size(menu, NULL, &size);
    GtkAllocation allocation = {0, 0, size.width, size.height};
    gtk_widget_size_allocate(menu, &allocation);
    gtk_widget_map(menu);
    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size.width, size.height);
    cairo_t *cr = cairo_create(surface);
    gtk_widget_draw(menu, cr);
    cairo_surface_write_to_png(surface, "/tmp/quicklist-refined.png");
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_print("Menu rendered: %dx%d\n", size.width, size.height);
    GList *rows = gtk_container_get_children(GTK_CONTAINER(menu));
    /* Sorting changes only row attachment order; every title and XID survives. */
    for (guint mode = 0; mode < 3; mode++) {
        dock.settings.window_sort = mode;
        quicklist_sort_rows(menu);
        for (GList *p = rows; p; p = p->next) {
            Client *a = g_object_get_data(G_OBJECT(p->data), "sort-client");
            if (!a)
                continue;
            guint ay;
            gtk_container_child_get(GTK_CONTAINER(menu), p->data, "top-attach", &ay, NULL);
            for (GList *q = rows; q; q = q->next) {
                Client *b = g_object_get_data(G_OBJECT(q->data), "sort-client");
                if (!b || compare_window_rows(&a, &b, GUINT_TO_POINTER(mode)) >= 0)
                    continue;
                guint by;
                gtk_container_child_get(GTK_CONTAINER(menu), q->data, "top-attach", &by, NULL);
                g_assert_cmpuint(ay, <, by);
            }
        }
    }
    guint window_rows = 0;
    for (GList *p = rows; p; p = p->next) {
        GtkWidget *row = p->data;
        GtkWidget *close = g_object_get_data(G_OBJECT(row), "window-close");
        if (!close)
            continue;
        window_rows++;
        g_assert_nonnull(gtk_widget_get_tooltip_text(row));
        int x, y, rx, ry;
        g_assert_true(gtk_widget_translate_coordinates(close, menu, 0, 0, &x, &y));
        gdk_window_get_origin(gtk_widget_get_window(menu), &rx, &ry);
        GdkEventButton hit = {.x_root = rx + x + 8, .y_root = ry + y + 8};
        g_assert_true(quicklist_close_at(menu, &hit) == close);
        gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), row);
    }
    g_assert_cmpuint(window_rows, ==, 4);
    g_list_free(rows);
    gtk_widget_destroy(menu);
    for (guint edge = 0; edge < 2; edge++) {
        menu = build_quicklist(&item, &view);
        gtk_widget_show_all(menu);
        GdkRectangle area;
        gdk_monitor_get_geometry(view.screen, &area);
        GdkRectangle anchor = {area.x + (edge ? area.width - 390 : 20), area.y + 120, 1, 1};
        GdkEvent *trigger = gdk_event_new(GDK_BUTTON_PRESS);
        trigger->button.window =
            g_object_ref(gdk_screen_get_root_window(gdk_display_get_default_screen(dock.gdisplay)));
        trigger->button.button = 3;
        trigger->button.time = GDK_CURRENT_TIME;
        gdk_event_set_device(trigger,
                             gdk_seat_get_pointer(gdk_display_get_default_seat(dock.gdisplay)));
        gtk_menu_popup_at_rect(GTK_MENU(menu), trigger->button.window, &anchor,
                               GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, trigger);
        settle();
        GtkWidget *tile = g_object_get_data(G_OBJECT(menu), "tile-control");
        gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), tile);
        GdkEvent *right = gdk_event_new(GDK_KEY_PRESS);
        right->key.window = g_object_ref(gtk_widget_get_window(menu));
        right->key.keyval = GDK_KEY_Right;
        right->key.hardware_keycode = XKeysymToKeycode(dock.display, XK_Right);
        right->key.time = GDK_CURRENT_TIME;
        gdk_event_set_device(right,
                             gdk_seat_get_keyboard(gdk_display_get_default_seat(dock.gdisplay)));
        gtk_main_do_event(right);
        gdk_event_free(right);
        settle();
        GtkWidget *sub = gtk_menu_item_get_submenu(GTK_MENU_ITEM(tile));
        g_assert_true(gtk_widget_get_mapped(sub));
        int sx, sy, mx, my;
        gdk_window_get_origin(gtk_widget_get_window(sub), &sx, &sy);
        gdk_window_get_origin(gtk_widget_get_window(menu), &mx, &my);
        g_assert_cmpint(sx, >=, area.x);
        g_assert_cmpint(sx + gtk_widget_get_allocated_width(sub), <=, area.x + area.width);
        if (edge)
            g_assert_cmpint(sx, <, mx);
        else
            g_assert_cmpint(sx, >, mx);
        /* Left returns to the parent, Escape dismisses its menu shell. */
        g_signal_emit_by_name(sub, "move-current", GTK_MENU_DIR_PARENT);
        settle();
        GList *entries = gtk_container_get_children(GTK_CONTAINER(menu));
        for (GList *p = entries; p; p = p->next) {
            GtkWidget *context = g_object_get_data(G_OBJECT(p->data), "window-context");
            if (!context)
                continue;
            gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), p->data);
            GdkEvent *key = gdk_event_new(GDK_KEY_PRESS);
            key->key.window = g_object_ref(gtk_widget_get_window(menu));
            key->key.keyval = GDK_KEY_F10;
            key->key.state = GDK_SHIFT_MASK;
            gdk_event_set_device(
                key, gdk_seat_get_keyboard(gdk_display_get_default_seat(dock.gdisplay)));
            gtk_main_do_event(key);
            settle();
            g_assert_true(gtk_widget_get_mapped(context));
            g_assert_true(gtk_widget_get_mapped(menu));
            GdkEventKey back = {.keyval = GDK_KEY_Left};
            g_assert_true(window_context_key(context, &back, p->data));
            settle();
            g_assert_false(gtk_widget_get_mapped(context));
            g_assert_true(gtk_widget_get_mapped(menu));
            g_assert_null(gtk_menu_item_get_submenu(GTK_MENU_ITEM(p->data)));
            gdk_event_free(key);
            break;
        }
        g_list_free(entries);
        gtk_menu_shell_cancel(GTK_MENU_SHELL(menu));
        g_assert_false(gtk_widget_get_mapped(menu));
        gtk_widget_destroy(menu);
        gdk_event_free(trigger);
    }
    g_print("Native submenu opening, left/right placement and keyboard dismissal passed\n");
    Client *first = item.clients->pdata[0];
    menu_activate(NULL, (gpointer)(guintptr)first->xid);
    settle();
    unsigned long n;
    Window *active = (Window *)property(dock.root, "_NET_ACTIVE_WINDOW", XA_WINDOW, 32, &n);
    g_assert_nonnull(active);
    g_assert_cmpuint(active[0], ==, first->xid);
    XFree(active);
    gtk_window_iconify(GTK_WINDOW(widgets[0]));
    settle();
    menu_activate(NULL, (gpointer)(guintptr)first->xid);
    settle();
    g_assert_false(has_atom(first->xid, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN"));
    /* Delete closes the selected window without first activating it. */
    menu = build_quicklist(&item, &view);
    rows = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *p = rows; p; p = p->next) {
        if (g_object_get_data(G_OBJECT(p->data), "sort-client") != first)
            continue;
        gtk_widget_show_all(menu);
        gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), p->data);
        GdkEventKey key = {.keyval = GDK_KEY_Delete};
        g_assert_true(quicklist_key_event(menu, &key, NULL));
        break;
    }
    g_list_free(rows);
    settle();
    g_assert_true(destroyed[0]);
    for (guint i = 1; i < 4; i++)
        g_assert_false(destroyed[i]);
    gtk_widget_destroy(menu);
    g_ptr_array_remove_index(item.clients, 0);
    menu_close(NULL, &item);
    settle();
    for (guint i = 0; i < 4; i++)
        g_assert_true(destroyed[i]);
    g_print("Window activation, minimized restore, close-all requests passed\n");
    /* No windows: menu remains valid, tiling and close actions are disabled. */
    g_ptr_array_set_size(item.clients, 0);
    menu = build_quicklist(&item, &view);
    gtk_widget_destroy(menu);
    g_object_unref(action);
    gtk_widget_destroy(unrelated);
    gtk_widget_destroy(view.items_box);
    g_ptr_array_unref(item.clients);
    g_object_unref(item.app);
    g_key_file_unref(app_key);
    return 0;
}
