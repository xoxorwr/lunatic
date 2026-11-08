/* SPDX-License-Identifier: GPL-3.0-only
 * X11 adapter for application-scoped tiling. All geometry is planned and
 * checked before issuing requests; the window manager retains final authority.
 */
typedef struct {
    Window xid;
    guint64 created;
    unsigned long desktop;
    GdkRectangle geometry; /* Client size and frame origin. */
    int left, right, top, bottom;
    gboolean max_h, max_v;
    XSizeHints hints;
} TileWindow;

static gboolean tile_snapshot(const Client *client, TileWindow *out)
{
    unsigned long desktop = cardinal(client->xid, "_NET_WM_DESKTOP", client->desktop);
    unsigned long current = cardinal(dock.root, "_NET_CURRENT_DESKTOP", dock.current_desktop);
    if (desktop != current && desktop != 0xffffffffUL)
        return FALSE;
    if (!has_atom(client->xid, "_NET_WM_ALLOWED_ACTIONS", "_NET_WM_ACTION_MOVE") ||
        !has_atom(client->xid, "_NET_WM_ALLOWED_ACTIONS", "_NET_WM_ACTION_RESIZE") ||
        has_atom(client->xid, "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN") ||
        has_atom(client->xid, "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN") ||
        has_atom(client->xid, "_NET_WM_STATE", "_NET_WM_STATE_SHADED") ||
        has_atom(client->xid, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DIALOG"))
        return FALSE;
    *out = (TileWindow){.xid = client->xid, .created = client->created, .desktop = desktop};
    XWindowAttributes attr;
    Window child, transient = None;
    int x = 0, y = 0;
    long supplied;
    gdk_x11_display_error_trap_push(dock.gdisplay);
    gboolean ok = XGetWindowAttributes(dock.display, client->xid, &attr) &&
                  XTranslateCoordinates(dock.display, client->xid, dock.root, 0, 0, &x, &y, &child);
    XGetTransientForHint(dock.display, client->xid, &transient);
    XGetWMNormalHints(dock.display, client->xid, &out->hints, &supplied);
    int error = gdk_x11_display_error_trap_pop(dock.gdisplay);
    if (!ok || error || transient != None || attr.override_redirect)
        return FALSE;
    if ((out->hints.flags & (PMinSize | PMaxSize)) == (PMinSize | PMaxSize) &&
        out->hints.min_width == out->hints.max_width &&
        out->hints.min_height == out->hints.max_height)
        return FALSE;
    unsigned long n;
    unsigned long *ext =
        (unsigned long *)property(client->xid, "_NET_FRAME_EXTENTS", XA_CARDINAL, 32, &n);
    if (ext && n >= 4) {
        out->left = MIN(ext[0], 4096);
        out->right = MIN(ext[1], 4096);
        out->top = MIN(ext[2], 4096);
        out->bottom = MIN(ext[3], 4096);
    }
    if (ext)
        XFree(ext);
    out->geometry = (GdkRectangle){x - out->left, y - out->top, attr.width, attr.height};
    out->max_h = has_atom(client->xid, "_NET_WM_STATE", "_NET_WM_STATE_MAXIMIZED_HORZ");
    out->max_v = has_atom(client->xid, "_NET_WM_STATE", "_NET_WM_STATE_MAXIMIZED_VERT");
    return TRUE;
}

static gboolean tile_fit(const TileWindow *window, GdkRectangle *cell)
{
    const XSizeHints *h = &window->hints;
    int w = cell->width - window->left - window->right;
    int height = cell->height - window->top - window->bottom;
    int minw = h->flags & PMinSize ? MAX(1, h->min_width) : 1;
    int minh = h->flags & PMinSize ? MAX(1, h->min_height) : 1;
    int basew = h->flags & PBaseSize ? MAX(0, h->base_width) : minw;
    int baseh = h->flags & PBaseSize ? MAX(0, h->base_height) : minh;
    int incw = h->flags & PResizeInc ? MAX(1, h->width_inc) : 1;
    int inch = h->flags & PResizeInc ? MAX(1, h->height_inc) : 1;
    if (h->flags & PMaxSize) {
        w = MIN(w, h->max_width);
        height = MIN(height, h->max_height);
    }
    if (h->flags & PAspect) {
        if (h->min_aspect.x > 0 && h->min_aspect.y > 0 &&
            (double)w / MAX(1, height) < (double)h->min_aspect.x / h->min_aspect.y)
            height = (gint64)w * h->min_aspect.y / h->min_aspect.x;
        if (h->max_aspect.x > 0 && h->max_aspect.y > 0 &&
            (double)w / MAX(1, height) > (double)h->max_aspect.x / h->max_aspect.y)
            w = (gint64)height * h->max_aspect.x / h->max_aspect.y;
    }
    if (w < basew || height < baseh)
        return FALSE;
    w = basew + (w - basew) / incw * incw;
    height = baseh + (height - baseh) / inch * inch;
    if (w < minw || height < minh)
        return FALSE;
    if ((h->flags & PAspect) &&
        ((h->min_aspect.y > 0 && (double)w / height < (double)h->min_aspect.x / h->min_aspect.y) ||
         (h->max_aspect.y > 0 && (double)w / height > (double)h->max_aspect.x / h->max_aspect.y)))
        return FALSE;
    cell->width = w;
    cell->height = height;
    return TRUE;
}

static GdkRectangle tile_workarea(GdkMonitor *monitor)
{
    GdkRectangle area;
    gdk_monitor_get_workarea(monitor, &area);
    unsigned long n;
    unsigned long *values =
        (unsigned long *)property(dock.root, "_NET_WORKAREA", XA_CARDINAL, 32, &n);
    gsize offset = cardinal(dock.root, "_NET_CURRENT_DESKTOP", dock.current_desktop) * 4;
    if (values && offset + 4 <= n) {
        GdkRectangle desktop = {(int)values[offset], (int)values[offset + 1],
                                (int)values[offset + 2], (int)values[offset + 3]},
                     clipped;
        if (gdk_rectangle_intersect(&area, &desktop, &clipped))
            area = clipped;
        else
            area.width = area.height = 0;
    }
    if (values)
        XFree(values);
    return area;
}

static GArray *tile_windows(const Item *item)
{
    GArray *windows = g_array_new(FALSE, FALSE, sizeof(TileWindow));
    /* Focus first, stable creation order for the remaining windows. */
    for (int pass = 0; pass < 2; pass++)
        for (guint i = 0; i < item->clients->len; i++) {
            Client *client = g_ptr_array_index(item->clients, i);
            TileWindow snapshot;
            if (client->active == (pass == 0) && tile_snapshot(client, &snapshot))
                g_array_append_val(windows, snapshot);
        }
    return windows;
}

static gboolean tile_plan_layout(GArray *windows, DockView *view, TileLayout layout,
                                 GdkRectangle *rects)
{
    if (!windows->len || !has_atom(dock.root, "_NET_SUPPORTED", "_NET_MOVERESIZE_WINDOW"))
        return FALSE;
    guint monitors = dock.tile_all_monitors ? gdk_display_get_n_monitors(dock.gdisplay) : 1;
    guint used = MIN(monitors, windows->len), offset = 0;
    for (guint i = 0; i < used; i++) {
        GdkMonitor *monitor =
            dock.tile_all_monitors ? gdk_display_get_monitor(dock.gdisplay, i) : view->screen;
        if (!monitor)
            return FALSE;
        GdkRectangle area = tile_workarea(monitor);
        guint count = windows->len / used + (i < windows->len % used);
        if (!window_layout_plan(layout, &area, count, rects + offset))
            return FALSE;
        for (guint j = 0; j < count; j++)
            if (!tile_fit(&g_array_index(windows, TileWindow, offset + j), &rects[offset + j]))
                return FALSE;
        offset += count;
    }
    return used > 0;
}

static gboolean tile_plan(GArray *windows, DockView *view, TileLayout layout, GdkRectangle *rects)
{
    if (tile_plan_layout(windows, view, layout, rects))
        return TRUE;
    if (layout == TILE_SMART) {
        const TileLayout alternatives[] = {TILE_GRID, TILE_COLUMNS, TILE_ROWS, TILE_FOCUS};
        for (guint i = 0; i < G_N_ELEMENTS(alternatives); i++)
            if (tile_plan_layout(windows, view, alternatives[i], rects))
                return TRUE;
    }
    return FALSE;
}

static gboolean tile_can_restore(const Item *item)
{
    for (guint i = 0; item->previous_layout && i < item->previous_layout->len; i++) {
        TileWindow *old = &g_array_index(item->previous_layout, TileWindow, i);
        for (guint j = 0; j < item->clients->len; j++) {
            Client *client = g_ptr_array_index(item->clients, j);
            TileWindow now;
            if (client->xid == old->xid && client->created == old->created &&
                tile_snapshot(client, &now) && now.desktop == old->desktop)
                return TRUE;
        }
    }
    return FALSE;
}

static void tile_geometry(Window xid, const GdkRectangle *rect)
{
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = xid;
    event.xclient.message_type = atom("_NET_MOVERESIZE_WINDOW");
    event.xclient.format = 32;
    /* NorthWestGravity positions the outer frame; dimensions are client sizes. */
    event.xclient.data.l[0] = NorthWestGravity | (15 << 8) | (2 << 12);
    event.xclient.data.l[1] = rect->x;
    event.xclient.data.l[2] = rect->y;
    event.xclient.data.l[3] = rect->width;
    event.xclient.data.l[4] = rect->height;
    XSendEvent(dock.display, dock.root, False, SubstructureRedirectMask | SubstructureNotifyMask,
               &event);
    XFlush(dock.display);
}

static void menu_tile(GtkMenuItem *entry, gpointer data)
{
    Item *item = data;
    DockView *view = g_object_get_data(G_OBJECT(entry), "tile-view");
    TileLayout layout = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "tile-layout"));
    GArray *windows = tile_windows(item);
    GdkRectangle *rects = g_new0(GdkRectangle, windows->len);
    if (tile_plan(windows, view, layout, rects)) {
        if (!item->previous_layout)
            item->previous_layout = g_array_ref(windows);
        else {
            /* New windows get their own first pre-tiling geometry. */
            for (guint i = 0; i < windows->len; i++) {
                TileWindow w = g_array_index(windows, TileWindow, i);
                gboolean found = FALSE;
                for (guint j = 0; j < item->previous_layout->len; j++) {
                    TileWindow old = g_array_index(item->previous_layout, TileWindow, j);
                    found |= old.xid == w.xid && old.created == w.created;
                }
                if (!found)
                    g_array_append_val(item->previous_layout, w);
            }
        }
        for (guint i = 0; i < windows->len; i++) {
            TileWindow *w = &g_array_index(windows, TileWindow, i);
            send_message_source(w->xid, "_NET_WM_STATE", 0, atom("_NET_WM_STATE_MAXIMIZED_HORZ"),
                                atom("_NET_WM_STATE_MAXIMIZED_VERT"), 2);
            tile_geometry(w->xid, &rects[i]);
        }
    }
    g_free(rects);
    g_array_unref(windows);
}

static void menu_restore_layout(GtkMenuItem *entry, gpointer data)
{
    (void)entry;
    Item *item = data;
    for (guint i = 0; item->previous_layout && i < item->previous_layout->len; i++) {
        TileWindow *old = &g_array_index(item->previous_layout, TileWindow, i);
        for (guint j = 0; j < item->clients->len; j++) {
            Client *client = g_ptr_array_index(item->clients, j);
            TileWindow now;
            if (client->xid != old->xid || client->created != old->created ||
                !tile_snapshot(client, &now) || now.desktop != old->desktop)
                continue;
            GdkRectangle rect = old->geometry;
            /* Keep restored windows reachable after monitor removal or panel changes. */
            GdkMonitor *monitor = gdk_display_get_monitor_at_point(dock.gdisplay, rect.x, rect.y);
            GdkRectangle area = tile_workarea(monitor);
            GdkRectangle frame = {rect.x, rect.y, rect.width + now.left + now.right,
                                  rect.height + now.top + now.bottom};
            if (area.width < 1 || area.height < 1)
                continue;
            if (frame.width > area.width || frame.height > area.height) {
                GdkRectangle fitted = area;
                if (!tile_fit(&now, &fitted))
                    continue;
                rect.width = fitted.width;
                rect.height = fitted.height;
            }
            rect.x = CLAMP(rect.x, area.x,
                           MAX(area.x, area.x + area.width - rect.width - now.left - now.right));
            rect.y = CLAMP(rect.y, area.y,
                           MAX(area.y, area.y + area.height - rect.height - now.top - now.bottom));
            send_message_source(old->xid, "_NET_WM_STATE", 0, atom("_NET_WM_STATE_MAXIMIZED_HORZ"),
                                atom("_NET_WM_STATE_MAXIMIZED_VERT"), 2);
            tile_geometry(old->xid, &rect);
            if (old->max_h || old->max_v)
                send_message_source(
                    old->xid, "_NET_WM_STATE", 1,
                    atom(old->max_h ? "_NET_WM_STATE_MAXIMIZED_HORZ"
                                    : "_NET_WM_STATE_MAXIMIZED_VERT"),
                    old->max_h && old->max_v ? atom("_NET_WM_STATE_MAXIMIZED_VERT") : 0, 2);
        }
    }
    g_clear_pointer(&item->previous_layout, g_array_unref);
}

static void menu_tile_scope(GtkMenuItem *entry, gpointer data)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(entry)))
        dock.tile_all_monitors = GPOINTER_TO_INT(data);
}

static void quicklist_tile_menu(GtkWidget *menu, Item *item, DockView *view)
{
    GtkWidget *entry = gtk_menu_item_new();
    gtk_container_add(GTK_CONTAINER(entry),
                      quicklist_content("Tile windows", "view-grid-symbolic"));
    quicklist_append(menu, entry);
    g_object_set_data(G_OBJECT(menu), "tile-control", entry);
    g_signal_connect_after(entry, "draw", G_CALLBACK(quicklist_row_draw), NULL);
    GtkWidget *submenu = gtk_menu_new();
    quicklist_style(submenu, NULL);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(entry), submenu);
    quicklist_section(submenu, "TILE WINDOWS");
    quicklist_section(submenu, NULL);
    const char *labels[] = {"Side by side", "Stack vertically", "Grid", "Focus + stack",
                            "Smart layout"};
    const char *icons[] = {"view-dual-symbolic", "view-list-symbolic", "view-grid-symbolic",
                           "view-sidebar-symbolic", "view-restore-symbolic"};
    GArray *windows = tile_windows(item);
    GdkRectangle *rects = g_new0(GdkRectangle, windows->len);
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++) {
        GtkWidget *choice =
            menu_icon_entry(submenu, labels[i], icons[i], G_CALLBACK(menu_tile), item);
        g_object_set_data(G_OBJECT(choice), "tile-layout", GINT_TO_POINTER(i));
        g_object_set_data(G_OBJECT(choice), "tile-view", view);
        gboolean available = tile_plan(windows, view, i, rects);
        gtk_widget_set_sensitive(choice, available);
        if (!available)
            gtk_widget_set_tooltip_text(
                choice,
                windows->len
                    ? "This layout cannot fit the available windows in the monitor work area"
                    : "No windows available to tile on this workspace");
    }
    g_free(rects);
    g_array_unref(windows);
    quicklist_section(submenu, NULL);
    GtkWidget *scope = gtk_menu_item_new();
    gtk_container_add(GTK_CONTAINER(scope), quicklist_content("Tile on", "video-display-symbolic"));
    quicklist_append(submenu, scope);
    g_signal_connect_after(scope, "draw", G_CALLBACK(quicklist_row_draw), NULL);
    GtkWidget *scopes = gtk_menu_new();
    quicklist_style(scopes, NULL);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(scope), scopes);
    GSList *group = NULL;
    const char *scope_labels[] = {"Current monitor", "All monitors"};
    for (guint i = 0; i < G_N_ELEMENTS(scope_labels); i++) {
        GtkWidget *choice = gtk_radio_menu_item_new_with_label(group, scope_labels[i]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(choice));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(choice),
                                       dock.tile_all_monitors == (gboolean)i);
        quicklist_append(scopes, choice);
        g_signal_connect(choice, "activate", G_CALLBACK(menu_tile_scope), GUINT_TO_POINTER(i));
    }
    quicklist_section(submenu, NULL);
    GtkWidget *restore = menu_icon_entry(submenu, "Restore previous layout", "edit-undo-symbolic",
                                         G_CALLBACK(menu_restore_layout), item);
    gtk_widget_set_sensitive(restore, tile_can_restore(item));
}
