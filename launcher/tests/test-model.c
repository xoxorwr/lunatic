/* SPDX-License-Identifier: GPL-3.0-only
 * Exercise the actual host model and layout without an X server or GTK widgets.
 */
#define main launcher_session_main
#include "../src/main.c"
#undef main

static Item *model_item(const char *id, int kind)
{
    Item *item = add_item(id, NULL, id);
    item->kind = kind;
    item->visible_animation.value = item->visible_animation.target = 1;
    return item;
}

static void desktop_order(void)
{
    DockView view = {0};
    launcher_settings_defaults(&dock.settings);
    dock.view = &view;
    dock.views = g_ptr_array_new();
    g_ptr_array_add(dock.views, &view);
    dock.items = g_ptr_array_new_with_free_func(item_free);
    dock.layout_scratch = g_array_new(FALSE, FALSE, sizeof(LayoutIcon));

    model_item("lunatic://home", 1);
    Item *first = model_item("first.desktop", 0);
    Item *trash = sync_system_item("lunatic://trash", "Trash", TRUE);
    /* Same insertion sequence as enabling Show Desktop in settings. */
    Item *desktop = sync_system_item("lunatic://desktop", "Show Desktop", TRUE);
    layout_items(800);
    g_assert_cmpfloat(first->layout.center, ==, 91);
    g_assert_cmpfloat(desktop->layout.center, ==, 709);
    g_assert_cmpfloat(trash->layout.center, ==, 768);

    /* A panel/work-area change can precede GTK's next size allocation. The
     * system shelf must already fit the new visible area in that interval. */
    view.height = 720;
    layout_items(800);
    g_assert_cmpfloat(desktop->layout.center, ==, 629);
    g_assert_cmpfloat(trash->layout.center, ==, 688);
    g_assert_cmpfloat(trash->layout.hit_end, <=, view.height);
    g_assert_cmpint(canvas_extent(&view, 800), ==, 720);
    g_assert_cmpint(canvas_extent(&view, 640), ==, 640);
    view.height = 0;

    /* Opening/pinning an application must not put it beneath Show Desktop or
     * accidentally include it in the shelf's bottom alignment. */
    Item *second = model_item("second.desktop", 0);
    g_assert_true(g_ptr_array_index(dock.items, 2) == second);
    g_assert_true(g_ptr_array_index(dock.items, 3) == desktop);
    layout_items(800);
    g_assert_cmpfloat(second->layout.center, ==, 150);
    g_assert_cmpfloat(desktop->layout.center, ==, 709);
    g_assert_cmpfloat(trash->layout.center, ==, 768);

    move_item(first, NULL); /* Drag an application below the last application. */
    g_assert_true(g_ptr_array_index(dock.items, 1) == second);
    g_assert_true(g_ptr_array_index(dock.items, 2) == first);
    g_assert_true(g_ptr_array_index(dock.items, 3) == desktop);
    g_assert_true(g_ptr_array_index(dock.items, 4) == trash);

    /* Disable, add another application, then re-enable Show Desktop. */
    sync_system_item("lunatic://desktop", "Show Desktop", FALSE);
    Item *third = model_item("third.desktop", 0);
    desktop = sync_system_item("lunatic://desktop", "Show Desktop", TRUE);
    g_assert_true(g_ptr_array_index(dock.items, 3) == third);
    g_assert_true(g_ptr_array_index(dock.items, 4) == desktop);
    g_assert_true(g_ptr_array_index(dock.items, 5) == trash);
    layout_items(800);
    g_assert_cmpfloat(desktop->layout.center, ==, 709);
    g_assert_cmpfloat(trash->layout.center, ==, 768);

    /* Independent toggles, including re-enabling Trash while Desktop exists.
     * Applications opened and reordered in every state stay in the upper group. */
    const guint states[] = {0, 1, 3, 1, 2, 3, 0};
    for (guint i = 0; i < G_N_ELEMENTS(states); i++) {
        guint state = states[i];
        desktop = sync_system_item("lunatic://desktop", "Show Desktop", state & 1);
        trash = sync_system_item("lunatic://trash", "Trash", state & 2);
        char *id = g_strdup_printf("later-%u.desktop", i);
        Item *later = model_item(id, 0);
        g_free(id);
        move_item(first, NULL);
        layout_items(800);
        g_assert_true((find_item("lunatic://desktop") != NULL) == !!(state & 1));
        g_assert_true((find_item("lunatic://trash") != NULL) == !!(state & 2));
        g_assert_cmpfloat(later->layout.center, <, first->layout.center);
        if (desktop) {
            g_assert_cmpfloat(first->layout.center, <, desktop->layout.center);
            g_assert_cmpfloat(desktop->layout.center, ==, trash ? 709 : 768);
        }
        if (trash) {
            g_assert_true(g_ptr_array_index(dock.items, dock.items->len - 1) == trash);
            g_assert_cmpfloat(first->layout.center, <, trash->layout.center);
            g_assert_cmpfloat(trash->layout.center, ==, 768);
        }
    }

    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    g_array_unref(dock.layout_scratch);
    dock = (Launcher){0};
}

static void pinned_workspace_tracking(void)
{
    DockView view = {0};
    launcher_settings_defaults(&dock.settings);
    dock.view = &view;
    dock.views = g_ptr_array_new();
    dock.items = g_ptr_array_new_with_free_func(item_free);
    Item *item = model_item("google-chrome.desktop", 0);
    item->pinned = TRUE;
    g_assert_false(dock.settings.all_workspaces);

    /* Pinned icons obey the same window filter as unpinned ones. Switching
     * back restores the existing window; switching away hides its marker
     * and excludes it from the quicklist and activation selection. */
    const unsigned long workspaces[] = {0, 1, 2, 0};
    for (guint i = 0; i < G_N_ELEMENTS(workspaces); i++) {
        dock.current_desktop = workspaces[i];
        begin_window_refresh(item);
        gboolean visible = launcher_settings_window_visible(
            &dock.settings, 0, dock.current_desktop);
        g_assert_cmpint(record_window_presence(item, visible), ==, visible);
        Client *client = NULL;
        if (visible) {
            client = g_new0(Client, 1);
            client->xid = 123;
            client->desktop = 0;
            g_ptr_array_add(item->clients, client);
        }
        g_assert_true(item->has_windows);
        g_assert_cmpint(item->seen, ==, visible);
        g_assert_true(item_activation_client(item, dock.current_desktop) == client);
        RenderArg arg = item_render_arg(item, 1, 1000);
        g_assert_cmpuint(arg.running, ==, visible ? 1 : 0);
        g_assert_cmpint(arg.running_here, ==, visible);
    }

    /* Enabling all workspaces permits remote windows, while activation
     * still prefers a window on the current workspace. */
    dock.settings.all_workspaces = TRUE;
    dock.current_desktop = 0;
    g_assert_true(record_window_presence(item, launcher_settings_window_visible(
        &dock.settings, 1, dock.current_desktop)));
    Client *remote = g_new0(Client, 1);
    remote->xid = 456;
    remote->desktop = 1;
    g_ptr_array_add(item->clients, remote);
    g_assert_cmpuint(item_activation_client(item, 0)->xid, ==, 123);
    g_assert_true(item_activation_client(item, 1) == remote);

    begin_window_refresh(item); /* Last window closes. */
    g_assert_null(item_activation_client(item, 0));
    g_assert_cmpuint(item_render_arg(item, 1, 1000).running, ==, 0);

    /* Unpinned icons retain the configured workspace filtering. */
    item->pinned = FALSE;
    g_assert_false(record_window_presence(item, FALSE));
    g_assert_false(item->seen);
    g_assert_true(item->has_windows);
    g_assert_true(record_window_presence(item, TRUE));
    g_assert_true(item->seen);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    dock = (Launcher){0};
}

static void invalid_drag_sources(void)
{
    DockView view = {0};
    GdkEventMotion motion = {.state = GDK_BUTTON1_MASK, .x = 40, .y = 200};
    /* No widget/display is needed: these gestures must never enter GTK's
     * drag initialization (or its selection ownership machinery). */
    g_assert_false(begin_item_drag(NULL, &motion, &view));
    Item system = {.kind = 2};
    view.pressed = &system;
    g_assert_false(begin_item_drag(NULL, &motion, &view));
    view.dragged = &system;
    g_assert_false(begin_item_drag(NULL, &motion, &view));

    Item app = {0};
    view.dragged = &app;
    view.pressed = NULL; /* Source removed or gesture already consumed. */
    g_assert_false(begin_item_drag(NULL, &motion, &view));
    view.pressed = &app;
    motion.state = 0;
    g_assert_false(begin_item_drag(NULL, &motion, &view));
    motion.state = GDK_BUTTON1_MASK;
    view.hide.quirks = INTERNAL_DND_ACTIVE;
    g_assert_false(begin_item_drag(NULL, &motion, &view));
}

static void launcher_update(const char *sender, const char *uri, const char *dictionary)
{
    GError *error = NULL;
    GVariant *properties = g_variant_parse(G_VARIANT_TYPE_VARDICT, dictionary, NULL, NULL, &error);
    g_assert_no_error(error);
    GVariant *parameters = g_variant_ref_sink(g_variant_new("(s@a{sv})", uri, properties));
    launcher_entry_update(NULL, sender, NULL, NULL, NULL, parameters, NULL);
    g_variant_unref(parameters);
}

static void publisher_exit(const char *sender)
{
    GVariant *parameters = g_variant_ref_sink(g_variant_new("(sss)", sender, sender, ""));
    launcher_entry_owner_changed(NULL, "org.freedesktop.DBus", NULL, NULL, NULL, parameters, NULL);
    g_variant_unref(parameters);
}

static void launcherentry_urgency(void)
{
    dock.views = g_ptr_array_new();
    dock.items = g_ptr_array_new_with_free_func(item_free);
    Item *item = model_item("terminal.desktop", 0);
    const char *uri = "application://terminal.desktop";

    launcher_update(":1.10", uri, "{'urgent': <true>}");
    g_assert_true(item->urgent);
    g_assert_cmpint(item->urgent_started, >, 0);
    g_assert_true(dock.animation_fast);
    g_assert_cmpuint(dock.animation_timer, >, 0);
    gint64 started = item->urgent_started;
    launcher_update(":1.10", uri, "{'urgent': <true>}");
    g_assert_cmpint(item->urgent_started, ==, started);
    g_assert_cmpuint(g_hash_table_size(item->urgent_senders), ==, 1);

    /* Partial badge/progress updates must preserve the urgent state. */
    launcher_update(":1.10", uri,
                    "{'count': <int64 3>, 'count-visible': <true>, "
                    "'progress': <0.5>, 'progress-visible': <true>}");
    g_assert_true(item->urgent);
    g_assert_cmpint(item->count, ==, 3);
    g_assert_true(item->count_visible && item->progress_visible);
    g_assert_cmpfloat(item->progress, ==, .5);
    launcher_update(":1.10", uri, "{'urgent': <'invalid-type'>}");
    g_assert_true(item->urgent);
    launcher_update(":1.10", "application://unknown.desktop", "{'urgent': <true>}");
    g_assert_cmpuint(dock.items->len, ==, 1);

    /* One publisher cannot clear another publisher's attention request. */
    launcher_update(":1.11", uri, "{'urgent': <true>}");
    launcher_update(":1.10", uri, "{'urgent': <false>}");
    g_assert_true(item->urgent);
    launcher_update(":1.11", uri, "{'urgent': <false>}");
    g_assert_false(item->urgent);
    g_assert_cmpint(item->urgent_started, ==, 0);
    g_assert_null(item->urgent_senders);

    /* A normal X11 refresh can remove window urgency without losing D-Bus
     * urgency, and clearing D-Bus urgency must preserve an urgent window. */
    item->window_urgent = TRUE;
    update_item_urgency(item);
    launcher_update(":1.10", uri, "{'urgent': <true>}");
    item->window_urgent = FALSE;
    update_item_urgency(item);
    g_assert_true(item->urgent);
    item->window_urgent = TRUE;
    update_item_urgency(item);
    launcher_update(":1.10", uri, "{'urgent': <false>}");
    g_assert_true(item->urgent);
    item->window_urgent = FALSE;
    update_item_urgency(item);
    g_assert_false(item->urgent);

    /* Exiting publishers release their state and the sender table. */
    launcher_update(":1.10", uri, "{'urgent': <true>}");
    launcher_update(":1.11", uri, "{'urgent': <true>}");
    publisher_exit(":1.10");
    g_assert_true(item->urgent);
    publisher_exit(":1.11");
    g_assert_false(item->urgent);
    g_assert_cmpint(item->urgent_started, ==, 0);
    g_assert_null(item->urgent_senders);

    g_source_remove(dock.animation_timer);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    dock = (Launcher){0};
}

static void badge_app_lifetime(void)
{
    dock.views = g_ptr_array_new();
    dock.items = g_ptr_array_new_with_free_func(item_free);
    Item *item = model_item("thunar.desktop", 0);
    item->pinned = TRUE;
    const char *uri = "application://thunar.desktop";
    /* Same payload and immediate sender exit as the user's gdbus emit command. */
    launcher_update(":1.50", uri, "{'count': <int64 5>, 'count-visible': <true>}");
    publisher_exit(":1.50");
    g_assert_cmpint(item->count, ==, 5);
    g_assert_true(item->count_visible);
    /* A one-shot update also works before an app has opened any windows. */
    begin_window_refresh(item);
    clear_closed_app_overlays(item);
    g_assert_true(item->count_visible);
    item->has_windows = TRUE;
    begin_window_refresh(item);
    /* Its window still exists on another workspace. Visible clients are empty,
     * but the global window scan observes the app and preserves the badge. */
    item->has_windows = TRUE;
    clear_closed_app_overlays(item);
    g_assert_cmpint(item->clients->len, ==, 0);
    g_assert_cmpint(item->count, ==, 5);
    g_assert_true(item->count_visible);
    /* A newer update replaces the count; unrelated exits do not clear it. */
    launcher_update(":1.51", uri, "{'count': <int64 12>, 'count-visible': <true>}");
    launcher_update(":1.52", uri, "{'progress': <0.6>, 'progress-visible': <true>}");
    publisher_exit(":1.51");
    publisher_exit(":1.52");
    g_assert_cmpint(item->count, ==, 12);
    g_assert_true(item->progress_visible);
    /* Closing one of several windows still leaves the app running. */
    begin_window_refresh(item);
    item->has_windows = TRUE;
    clear_closed_app_overlays(item);
    g_assert_true(item->count_visible);
    /* Last window closes: keep the pinned entry, clear both overlays. */
    begin_window_refresh(item);
    clear_closed_app_overlays(item);
    g_assert_cmpuint(dock.items->len, ==, 1);
    g_assert_true(item->pinned);
    g_assert_cmpint(item->count, ==, 0);
    g_assert_false(item->count_visible);
    g_assert_false(item->progress_visible);
    /* A reopened program can publish a fresh badge, or explicitly hide it. */
    item->has_windows = TRUE;
    launcher_update(":1.53", uri, "{'count': <int64 8>, 'count-visible': <true>}");
    publisher_exit(":1.53");
    g_assert_true(item->count_visible);
    launcher_update(":1.54", uri, "{'count-visible': <false>}");
    publisher_exit(":1.54");
    g_assert_false(item->count_visible);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    dock = (Launcher){0};
}

static void hover_targets_and_tooltip_position(void)
{
    launcher_settings_defaults(&dock.settings);
    DockView view = {.geometry = {1920, 100, 1200, 900},
                     .top = 128,
                     .width = 61,
                     .height = 872,
                     .pointer_inside = TRUE};
    dock.view = &view;
    dock.views = g_ptr_array_new();
    g_ptr_array_add(dock.views, &view);
    dock.items = g_ptr_array_new_with_free_func(item_free);
    dock.layout_scratch = g_array_new(FALSE, FALSE, sizeof(LayoutIcon));
    Item *first = model_item("first.desktop", 0);
    Item *second = model_item("second.desktop", 0);
    model_item("third.desktop", 0);
    layout_items(view.height);
    int root_x = view.geometry.x + 10;
    int first_y = view.top + (int)first->layout.center;
    int second_y = view.top + (int)second->layout.center;
    g_assert_true(pointer_item(root_x, first_y) == first);
    g_assert_true(set_hovered_item(pointer_item(root_x, first_y), 1000));
    g_assert_true(view.hovered == first);
    GdkRectangle tip = icon_tooltip_rect(&view, first, 160, 24);
    g_assert_cmpint(tip.y + tip.height / 2, ==, first_y);
    g_assert_cmpint(tip.x, ==, view.geometry.x + view.width + 8);
    g_assert_true(set_hovered_item(pointer_item(root_x, second_y), 1016));
    g_assert_true(view.hovered == second);
    tip = icon_tooltip_rect(&view, second, 160, 24);
    g_assert_cmpint(tip.y + tip.height / 2, ==, second_y);
    g_assert_null(pointer_item(root_x, view.top + 2)); /* Empty space above first row. */
    /* Reordering beneath a stationary pointer must change the hover target. */
    move_item(second, first);
    layout_items(view.height);
    g_assert_true(pointer_item(root_x, second_y) == first);
    set_hovered_item(pointer_item(root_x, second_y), 1032);
    g_assert_true(view.hovered == first);
    /* Right-edge geometry includes a transparent gutter outside the dock. */
    dock.settings.position = DOCK_RIGHT;
    root_x = view.geometry.x + view.geometry.width - 10;
    g_assert_true(pointer_item(root_x, second_y) == first);
    tip = icon_tooltip_rect(&view, first, 160, 24);
    g_assert_cmpint(tip.x + tip.width + 8, ==, view.geometry.x + view.geometry.width - view.width);
    g_assert_cmpint(tip.y + tip.height / 2, ==, second_y);
    g_assert_null(pointer_item(view.geometry.x + view.geometry.width - view.width - 1, second_y));
    /* GTK coordinates stay logical at 2x scale; monitor top offset is applied once. */
    view.scale = 2;
    g_assert_true(pointer_item(root_x, second_y) == first);
    view.hidden = TRUE;
    g_assert_null(pointer_item(root_x, second_y));
    view.hidden = FALSE;
    view.pointer_inside = FALSE;
    g_assert_null(pointer_item(root_x, second_y));
    first->layout.center = -100;
    tip = icon_tooltip_rect(&view, first, 160, 24);
    g_assert_cmpint(tip.y, ==, view.geometry.y);
    first->layout.center = 2000;
    tip = icon_tooltip_rect(&view, first, 160, 24);
    g_assert_cmpint(tip.y + tip.height, ==, view.geometry.y + view.geometry.height);
    set_hovered_item(NULL, 1048);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    g_array_unref(dock.layout_scratch);
    dock = (Launcher){0};
}

static void urgency_multiple_windows(void)
{
    Client focused = {.active = TRUE};
    Client requesting = {.urgent = TRUE};
    Item item = {.urgent = TRUE, .active = TRUE, .clients = g_ptr_array_new()};
    g_ptr_array_add(item.clients, &focused);
    g_ptr_array_add(item.clients, &requesting);
    acknowledge_active_urgency(&item);
    g_assert_false(item.urgency_acknowledged);
    requesting.active = TRUE;
    focused.active = FALSE;
    acknowledge_active_urgency(&item);
    g_assert_true(item.urgency_acknowledged);
    /* An application-wide request can be acknowledged by any app window. */
    item.urgency_acknowledged = FALSE;
    requesting.active = FALSE;
    focused.active = TRUE;
    item.urgent_senders = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_add(item.urgent_senders, "publisher");
    acknowledge_active_urgency(&item);
    g_assert_true(item.urgency_acknowledged);
    g_hash_table_unref(item.urgent_senders);
    g_clear_pointer(&item.window_attention, g_hash_table_unref);
    g_ptr_array_unref(item.clients);
}

static void attention_acknowledgement(void)
{
    launcher_settings_defaults(&dock.settings);
    DockView view = {.width = 61};
    dock.view = &view;
    dock.views = g_ptr_array_new();
    g_ptr_array_add(dock.views, &view);
    dock.items = g_ptr_array_new_with_free_func(item_free);
    Item *item = model_item("terminal.desktop", 0);
    Client *first = g_new0(Client, 1);
    first->xid = 10;
    first->urgent = TRUE;
    g_ptr_array_add(item->clients, first);
    track_window_attention(item, first);
    item->window_urgent = TRUE;
    update_item_urgency(item);
    launcher_update(":1.40", "application://terminal.desktop",
                    "{'urgent': <true>, 'count': <int64 7>, 'count-visible': <true>}");
    item->attention_level = 1;
    g_assert_true(item_render_arg(item, 0, 0).urgent);
    acknowledge_window_urgency(item, first);
    /* Source urgency survives; every launcher attention cue is dismissed. */
    g_assert_true(item->urgent);
    g_assert_true(first->urgent);
    g_assert_false(item_attention_pending(item));
    g_assert_false(window_attention_pending(item, first));
    RenderArg arg = item_render_arg(item, 0, 0);
    g_assert_false(arg.urgent);
    g_assert_cmpfloat(arg.bounce, ==, 0);
    g_assert_true(arg.count_visible);
    g_assert_cmpint(arg.count, ==, 7);
    /* Refreshes reconstruct Client objects; acknowledgements survive by XID. */
    Client *replacement = g_new0(Client, 1);
    *replacement = *first;
    g_ptr_array_remove_index(item->clients, 0);
    g_ptr_array_add(item->clients, replacement);
    track_window_attention(item, replacement);
    prune_window_attention(item);
    update_item_urgency(item);
    g_assert_false(item_attention_pending(item));
    launcher_update(":1.40", "application://terminal.desktop", "{'urgent': <true>}");
    g_assert_false(item_attention_pending(item));
    /* A second window requests attention without the aggregate flag changing. */
    Client *second = g_new0(Client, 1);
    second->xid = 20;
    second->urgent = TRUE;
    g_ptr_array_add(item->clients, second);
    track_window_attention(item, second);
    g_assert_true(item_attention_pending(item));
    g_assert_false(window_attention_pending(item, replacement));
    g_assert_true(window_attention_pending(item, second));
    acknowledge_window_urgency(item, replacement);
    g_assert_true(item_attention_pending(item));
    acknowledge_window_urgency(item, second);
    g_assert_false(item_attention_pending(item));
    /* Clear/reassert of an individual window re-arms it, even while siblings
     * and the application continue to publish their acknowledged urgency. */
    second->urgent = FALSE;
    track_window_attention(item, second);
    second->urgent = TRUE;
    track_window_attention(item, second);
    g_assert_true(window_attention_pending(item, second));
    g_assert_true(item_attention_pending(item));
    acknowledge_window_urgency(item, second);
    /* Application-level requests also re-arm after clear/reassert. */
    launcher_update(":1.40", "application://terminal.desktop", "{'urgent': <false>}");
    g_assert_false(item_attention_pending(item));
    launcher_update(":1.40", "application://terminal.desktop", "{'urgent': <true>}");
    g_assert_true(item_attention_pending(item));
    acknowledge_window_urgency(item, replacement);
    g_assert_false(item_attention_pending(item));
    /* Closed windows leave no stale acknowledgement behind. */
    g_ptr_array_remove_index(item->clients, 1);
    prune_window_attention(item);
    g_assert_false(g_hash_table_contains(item->window_attention, (gpointer)(guintptr)20));
    g_assert_cmpint(item->count, ==, 7);
    g_assert_true(item->count_visible);
    g_source_remove(dock.animation_timer);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    dock = (Launcher){0};
}

static void attention_request_while_active(void)
{
    dock.views = g_ptr_array_new();
    dock.items = g_ptr_array_new_with_free_func(item_free);
    Item *item = model_item("terminal.desktop", 0);
    item->active = TRUE;
    Client *client = g_new0(Client, 1);
    client->active = TRUE;
    g_ptr_array_add(item->clients, client);
    launcher_update(":1.20", "application://terminal.desktop", "{'urgent': <true>}");
    gint64 start = item->urgent_started;
    gboolean pulsing;
    /* Exercise the same update function as the production timer. An app that
     * was already active when it requested attention still gets its burst. */
    g_assert_true(update_attention(item, start + 220, TRUE, &pulsing));
    g_assert_true(pulsing);
    g_assert_cmpfloat(item->attention_level, >, .99);
    g_assert_false(item->urgency_acknowledged);
    launcher_update(":1.20", "application://terminal.desktop", "{'urgent': <true>}");
    g_assert_cmpint(item->urgent_started, ==, start);
    g_assert_true(update_attention(item, start + 221, TRUE, &pulsing));
    g_assert_true(update_attention(item, start + 900, TRUE, &pulsing));
    g_assert_false(pulsing);
    g_assert_true(update_attention(item, start + 3220, TRUE, &pulsing));
    g_assert_true(pulsing);
    /* A subsequent focus/activation action acknowledges it, not mere polling. */
    acknowledge_active_urgency(item);
    g_assert_false(update_attention(item, start + 3220, TRUE, &pulsing));
    g_assert_false(pulsing);
    g_assert_cmpfloat(item->attention_level, ==, 0);
    item->urgency_acknowledged = FALSE;
    g_assert_false(update_attention(item, start + 220, FALSE, &pulsing));
    g_assert_false(pulsing);
    g_source_remove(dock.animation_timer);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    dock = (Launcher){0};
}

static void attention_rendered_motion(void)
{
    launcher_settings_defaults(&dock.settings);
    DockView view = {.width = 61, .height = 480, .scale = 1};
    dock.view = &view;
    dock.views = g_ptr_array_new();
    g_ptr_array_add(dock.views, &view);
    dock.items = g_ptr_array_new_with_free_func(item_free);
    Item *item = model_item("terminal.desktop", 0);
    item->active = TRUE;
    item->icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 48, 48);
    gdk_pixbuf_fill(item->icon, 0x20c040ff);
    item->layout = (LayoutIcon){.visible = 1, .center = 240};
    Client *client = g_new0(Client, 1);
    client->active = TRUE;
    g_ptr_array_add(item->clients, client);
    view.hovered = item;
    GError *error = NULL;
    g_assert_true(icon_renderer_init(&view.renderer, 54, 48, 1, &error));
    g_assert_no_error(error);
    launcher_update(":1.30", "application://terminal.desktop", "{'urgent': <true>}");
    gint64 start = item->urgent_started;
    const int offsets[] = {0, 220, 900};
    g_assert_cmpint(g_mkdir_with_parents("build/host", 0700), ==, 0);
    int frame_width = view.width + BOUNCE_GUTTER;
    cairo_surface_t *strip =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame_width * 3, 100 * 2);
    cairo_t *preview = cairo_create(strip);
    for (int right = 0; right < 2; right++) {
        RenderStyle style = {.color = {.3, .1, .2},
                             .opacity = .7,
                             .right = right,
                             .dock_width = 61,
                             .hide_animation = HIDE_SLIDE};
        icon_renderer_set_style(&view.renderer, &style);
        double centers[3];
        for (guint t = 0; t < G_N_ELEMENTS(offsets); t++) {
            gboolean dirty = FALSE, active = FALSE, attention = FALSE;
            /* The actual production timer update, argument construction and
             * rasterizer: no manually supplied bounce displacement. */
            advance_shared_animations(start + offsets[t], TRUE, &dirty, &active, &attention);
            RenderArg arg = item_render_arg(item, 0, start + offsets[t]);
            icon_renderer_begin(&view.renderer, frame_width, 480, 0);
            icon_renderer_draw(&view.renderer, &arg, &item_view_state(item)->render, &item->layout,
                               30, 480, 61);
            icon_renderer_end(&view.renderer);
            cairo_surface_t *frame = icon_renderer_surface(&view.renderer);
            const guint32 *pixels = (const guint32 *)cairo_image_surface_get_data(frame);
            int stride = cairo_image_surface_get_stride(frame) / 4;
            double sum = 0;
            int count = 0;
            for (int y = 210; y < 270; y++)
                for (int x = 0; x < frame_width; x++) {
                    guint32 p = pixels[y * stride + x];
                    int green = (p >> 8) & 255, red = (p >> 16) & 255;
                    if (green > 120 && green > red * 2) {
                        sum += x;
                        count++;
                    }
                }
            g_assert_cmpint(count, >, 1000);
            centers[t] = sum / count;
            cairo_save(preview);
            cairo_rectangle(preview, t * frame_width, right * 100, frame_width, 100);
            cairo_clip(preview);
            cairo_set_source_surface(preview, frame, t * frame_width, right * 100 - 190);
            cairo_paint(preview);
            cairo_restore(preview);
        }
        double displacement = (centers[1] - centers[0]) * (right ? -1 : 1);
        g_assert_cmpfloat(displacement, >, 20);
        g_assert_cmpfloat(fabs(centers[2] - centers[0]), <, .01);
        g_test_message("%s dock: measured icon displacement %.2f px, then returned to origin",
                       right ? "right" : "left", displacement);
        if (!right) {
            g_assert_cmpint(g_mkdir_with_parents("build/host/motion", 0700), ==, 0);
            cairo_surface_t *crop =
                cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame_width, 100);
            cairo_t *capture = cairo_create(crop);
            /* Export an actual 60 fps production-rendered sequence, including
             * the quiet interval, for visual review of timing and easing. */
            for (int frame_index = 0; frame_index < 180; frame_index++) {
                gint64 now = start + frame_index * 1000 / 60;
                gboolean dirty = FALSE, active = FALSE, attention = FALSE;
                advance_shared_animations(now, TRUE, &dirty, &active, &attention);
                RenderArg arg = item_render_arg(item, 0, now);
                icon_renderer_begin(&view.renderer, frame_width, 480, 0);
                icon_renderer_draw(&view.renderer, &arg, &item_view_state(item)->render,
                                   &item->layout, 30, 480, 61);
                icon_renderer_end(&view.renderer);
                cairo_set_operator(capture, CAIRO_OPERATOR_SOURCE);
                cairo_set_source_surface(capture, icon_renderer_surface(&view.renderer), 0, -190);
                cairo_paint(capture);
                char *path = g_strdup_printf("build/host/motion/frame-%03d.png", frame_index);
                g_assert_cmpint(cairo_surface_write_to_png(crop, path), ==, CAIRO_STATUS_SUCCESS);
                g_free(path);
            }
            cairo_destroy(capture);
            cairo_surface_destroy(crop);
        }
    }
    g_assert_cmpint(cairo_surface_write_to_png(strip, "build/host/bounce-timeline.png"), ==,
                    CAIRO_STATUS_SUCCESS);
    cairo_destroy(preview);
    cairo_surface_destroy(strip);
    g_source_remove(dock.animation_timer);
    g_ptr_array_unref(dock.items);
    g_ptr_array_unref(dock.views);
    icon_renderer_clear(&view.renderer);
    dock = (Launcher){0};
}

static void preview_text(cairo_t *cr, const char *text, double x, double y, int size, gboolean bold,
                         double r, double g, double b)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    cairo_move_to(cr, x, y);
    cairo_set_source_rgb(cr, r, g, b);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
}

static void quicklist_decoration(void)
{
    GError *error = NULL;
    GtkCssProvider *css = gtk_css_provider_new();
    g_assert_true(gtk_css_provider_load_from_data(css, quicklist_css, -1, &error));
    g_assert_no_error(error);
    g_object_unref(css);
    GdkEvent *click = gdk_event_new(GDK_BUTTON_PRESS);
    click->button.y_root = 832;
    g_assert_cmpfloat(quicklist_anchor_y(NULL, click, 200, 24), ==, 632);
    click->button.y_root = -140;
    g_assert_cmpfloat(quicklist_anchor_y(NULL, click, -600, 24), ==, 460);
    Item target = {.layout.center = 91};
    g_assert_cmpfloat(quicklist_anchor_y(&target, click, -600, 24), ==, 91);
    g_assert_cmpfloat(quicklist_anchor_y(NULL, NULL, -600, 240), ==, 240);
    gdk_event_free(click);
    g_assert_cmpfloat(quicklist_arrow_y(160, 100, 300), ==, 60);
    g_assert_cmpfloat(quicklist_arrow_y(110, 100, 300), ==, 24);
    g_assert_cmpfloat(quicklist_arrow_y(500, 100, 300), ==, 276);
    g_assert_cmpint(g_mkdir_with_parents("build/host", 0700), ==, 0);
    for (int left = 0; left < 2; left++) {
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 300, 364);
        cairo_t *cr = cairo_create(surface);
        quicklist_paint_frame(cr, 300, 364, left, 190);
        cairo_surface_flush(surface);
        guint32 *pixels = (guint32 *)cairo_image_surface_get_data(surface);
        int stride = cairo_image_surface_get_stride(surface) / 4;
        /* Point remains present on its chosen side; distant gutter is clear. */
        g_assert_cmpuint(pixels[190 * stride + (left ? 6 : 293)] >> 24, >, 200);
        g_assert_cmpuint(pixels[50 * stride + (left ? 6 : 293)] >> 24, <, 100);
        int content_x = left ? 18 : 6;
        preview_text(cr, "APP ACTIONS", content_x + 8, 9, 10, TRUE, .67, .63, .70);
        const char *actions[] = {"New window", "New tab", "Close all windows"};
        for (guint i = 0; i < G_N_ELEMENTS(actions); i++)
            preview_text(cr, actions[i], content_x + 8, 29 + i * 24, 13, FALSE, .97, .96, .98);
        const int dividers[] = {104, 142, 245};
        cairo_set_source_rgba(cr, 1, 1, 1, .13);
        for (guint i = 0; i < G_N_ELEMENTS(dividers); i++) {
            cairo_rectangle(cr, content_x + 8, dividers[i], 256, 1);
            cairo_fill(cr);
        }
        cairo_set_source_rgb(cr, .95, .65, .22);
        cairo_rectangle(cr, content_x + 8, 117, 16, 14);
        cairo_fill(cr);
        preview_text(cr, "Files", content_x + 30, 115, 13, TRUE, 1, 1, 1);
        preview_text(cr, "3 open windows", content_x + 165, 116, 11, FALSE, .74, .71, .76);
        preview_text(cr, "WINDOWS", content_x + 8, 151, 10, TRUE, .67, .63, .70);
        const char *windows[] = {"Home", "Downloads", "Projects"};
        for (guint i = 0; i < G_N_ELEMENTS(windows); i++) {
            cairo_save(cr);
            cairo_translate(cr, content_x, 173 + i * 24);
            quicklist_paint_row(cr, 272, 24, i == 0, i == 1);
            cairo_restore(cr);
            preview_text(cr, windows[i], content_x + 54, 177 + i * 24, 13, i == 0, .97, .96, .98);
        }
        preview_text(cr, "×", content_x + 9, 200, 18, FALSE, .97, .96, .98);
        preview_text(cr, "Active", content_x + 227, 178, 10, TRUE, 1, .82, .63);
        preview_text(cr, "!", content_x + 38, 224, 13, TRUE, 1, .70, .2);
        preview_text(cr, "LAUNCHER", content_x + 8, 254, 10, TRUE, .67, .63, .70);
        const char *commands[] = {"Unpin from launcher", "Settings…", "Quit launcher"};
        for (guint i = 0; i < G_N_ELEMENTS(commands); i++)
            preview_text(cr, commands[i], content_x + 8, 274 + i * 24, 13, FALSE, .97, .96, .98);
        /* Persistent selection and hover have distinct custom backgrounds. */
        cairo_surface_flush(surface);
        guint32 active = pixels[185 * stride + content_x + 7];
        guint32 hover = pixels[209 * stride + content_x + 7];
        g_assert_cmpuint((active >> 16) & 255, >, (hover >> 16) & 255);
        char *path = g_strdup_printf("build/host/quicklist-%s.png", left ? "left" : "right");
        g_assert_cmpint(cairo_surface_write_to_png(surface, path), ==, CAIRO_STATUS_SUCCESS);
        g_free(path);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
}

static void quicklist_close_clicks(void)
{
    gpointer first = GINT_TO_POINTER(1), second = GINT_TO_POINTER(2), pressed = NULL;
    gboolean close;
    /* A complete click closes only its original window. These handled events
     * never reach GtkMenuShell's window-activation handler. */
    g_assert_true(quicklist_close_gesture(&pressed, first, GDK_BUTTON_PRESS, 1, &close));
    g_assert_false(close);
    g_assert_true(quicklist_close_gesture(&pressed, first, GDK_BUTTON_RELEASE, 1, &close));
    g_assert_true(close);
    g_assert_null(pressed);
    /* Dragging off, or onto another row's close button, cancels the close and
     * still consumes the release so neither window gets activated. */
    for (int outside = 0; outside < 2; outside++) {
        quicklist_close_gesture(&pressed, first, GDK_BUTTON_PRESS, 1, &close);
        g_assert_true(quicklist_close_gesture(&pressed, outside ? NULL : second, GDK_BUTTON_RELEASE,
                                              1, &close));
        g_assert_false(close);
        g_assert_null(pressed);
    }
    /* Ordinary row clicks and the right-click opening the menu stay native. */
    g_assert_false(quicklist_close_gesture(&pressed, NULL, GDK_BUTTON_PRESS, 1, &close));
    g_assert_false(quicklist_close_gesture(&pressed, first, GDK_BUTTON_RELEASE, 1, &close));
    g_assert_false(close);
    g_assert_false(quicklist_close_gesture(&pressed, first, GDK_BUTTON_PRESS, 3, &close));
    g_assert_false(quicklist_close_gesture(&pressed, first, GDK_BUTTON_RELEASE, 3, &close));
    g_assert_false(close);
}

static void super_shortcut_taps(void)
{
    SuperTap tap = {0};
    g_assert_false(super_tap_update(&tap, TRUE, TRUE, FALSE));
    g_assert_true(super_tap_update(&tap, TRUE, FALSE, FALSE));
    g_assert_false(super_tap_update(&tap, TRUE, FALSE, FALSE)); /* no duplicate */
    /* Auto-repeat does not activate Dash before the physical release. */
    g_assert_false(super_tap_update(&tap, TRUE, TRUE, FALSE));
    g_assert_false(super_tap_update(&tap, TRUE, TRUE, FALSE));
    g_assert_true(super_tap_update(&tap, TRUE, FALSE, FALSE));
    /* Existing Super chords (digits, Space, WM shortcuts) and mouse chords
     * cancel modifier-only activation; releasing their key first is harmless. */
    super_tap_update(&tap, TRUE, TRUE, FALSE);
    g_assert_false(super_tap_update(&tap, FALSE, TRUE, FALSE));
    g_assert_false(super_tap_update(&tap, FALSE, FALSE, FALSE));
    g_assert_false(super_tap_update(&tap, TRUE, FALSE, FALSE));
    /* Shift/Ctrl/another key held before Super is a chord too. */
    super_tap_update(&tap, TRUE, TRUE, TRUE);
    g_assert_false(super_tap_update(&tap, TRUE, FALSE, FALSE));
    /* Cancellation must not suppress the following independent tap. */
    super_tap_update(&tap, TRUE, TRUE, FALSE);
    g_assert_true(super_tap_update(&tap, TRUE, FALSE, FALSE));
}

static void attention_timing(void)
{
    Item item = {.urgent = TRUE, .urgent_started = 1000};
    gboolean pulsing;
    g_assert_cmpfloat(attention_level(&item, 1000, &pulsing), ==, 0);
    g_assert_true(pulsing);
    g_assert_cmpfloat(attention_level(&item, 1016, &pulsing), >, .13);
    g_assert_cmpfloat(attention_level(&item, 1220, &pulsing), ==, 1);
    g_assert_cmpfloat(attention_level(&item, 1480, &pulsing), ==, 0);
    g_assert_cmpfloat(attention_level(&item, 1600, &pulsing), ==, .28);
    g_assert_cmpfloat(attention_level(&item, 1830, &pulsing), ==, .07);
    g_assert_cmpfloat(attention_level(&item, 1900, &pulsing), ==, 0);
    g_assert_false(pulsing);
    g_assert_cmpfloat(attention_level(&item, 4220, &pulsing), ==, 1);
    g_assert_true(pulsing);
    /* Bound the trajectory and catch discontinuities between flight segments. */
    double previous = 0;
    for (int ms = 0; ms <= 3000; ms++) {
        double current = attention_level(&item, 1000 + ms, &pulsing);
        g_assert_cmpfloat(current, >=, 0);
        g_assert_cmpfloat(current, <=, 1);
        g_assert_cmpfloat(fabs(current - previous), <, .01);
        previous = current;
    }
    item.urgency_acknowledged = TRUE;
    g_assert_cmpfloat(attention_level(&item, 4220, &pulsing), ==, 0);
    g_assert_false(pulsing);
    item.urgent = FALSE;
    g_assert_cmpfloat(attention_level(&item, 4220, &pulsing), ==, 0);
    g_assert_false(pulsing);
}

static void barrier_reveal_rules(void)
{
    LauncherSettings previous = dock.settings;
    launcher_settings_defaults(&dock.settings);
    dock.settings.autohide = TRUE;
    for (int side = DOCK_LEFT; side <= DOCK_RIGHT; side++) {
        dock.settings.position = side;
        DockView view = {.geometry = {100, 0, 800, 600}, .top = 24, .height = 576,
                         .width = 61, .scale = 1, .hidden = TRUE};
        hide_machine_init(&view.hide);
        hide_machine_mode(&view.hide, AUTOHIDE, 0);
        hide_machine_quirk(&view.hide, LAUNCHER_HIDDEN, TRUE, 0);
        int x = side == DOCK_RIGHT ? 899 : 100;
        g_assert_cmpfloat_with_epsilon(reveal_responsiveness(), 2, .00001);
        /* First contact is observed without adding reveal pressure. */
        g_assert_cmpint(barrier_reveal_pressure(&view, x, 300, 1200, TRUE, 0, 0), ==,
                        BARRIER_HANDLED);
        g_assert_cmpint(view.hide.pressure, ==, 0);
        g_assert_true(view.hide.should_hide);
        barrier_reveal_pressure(&view, x, 300, 1000, FALSE, 0, 75);
        g_assert_cmpint(view.hide.pressure, ==, 1000);
        /* Leaving does not reset pressure. Only the 10 ms decay reduces it. */
        hide_machine_tick(&view.hide, 175);
        g_assert_cmpint(view.hide.pressure, ==, 810);
        g_assert_cmpint(barrier_reveal_pressure(&view, x, 10, 1200, FALSE, 0, 175), ==,
                        BARRIER_RELEASE);
        g_assert_cmpint(view.hide.pressure, ==, 810);
        g_assert_cmpint(barrier_reveal_pressure(&view, x, 300, 1200, FALSE, Button1Mask,
                                               175), ==, BARRIER_RELEASE);
        g_assert_cmpint(view.hide.pressure, ==, 810);
        barrier_reveal_pressure(&view, x, 300, 1240, FALSE, 0, 175);
        g_assert_cmpint(view.hide.pressure, ==, 2050);
        g_assert_true(view.hide.should_hide); /* Strictly greater, not >=. */
        barrier_reveal_pressure(&view, x, 300, 1, FALSE, 0, 175);
        g_assert_false(view.hide.should_hide);
        g_assert_cmpint(view.hide.pressure, ==, 0);
        g_assert_true(view.hide.quirks & MOUSE_MOVE_POST_REVEAL);
        /* The physical coordinates from XI events respect display scale. */
        view.scale = 2;
        view.hidden = TRUE;
        g_assert_cmpint(barrier_reveal_pressure(&view, x * 2, 300 * 2, 100, TRUE, 0,
                                               200), ==, BARRIER_HANDLED);
        view.barrier_released_until = 1200;
        g_assert_cmpint(barrier_reveal_pressure(&view, x * 2, 300 * 2, 100, FALSE, 0,
                                               300), ==, BARRIER_RELEASE);
    }
    dock.settings = previous;
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/host/barrier-reveal-rules", barrier_reveal_rules);
    g_test_add_func("/host/desktop-order-and-layout", desktop_order);
    g_test_add_func("/host/invalid-drag-sources", invalid_drag_sources);
    g_test_add_func("/host/pinned-workspace-tracking", pinned_workspace_tracking);
    g_test_add_func("/host/launcherentry-urgency", launcherentry_urgency);
    g_test_add_func("/host/badge-app-lifetime", badge_app_lifetime);
    g_test_add_func("/host/hover-targets-and-tooltip-position", hover_targets_and_tooltip_position);
    g_test_add_func("/host/attention-timing", attention_timing);
    g_test_add_func("/host/quicklist-decoration", quicklist_decoration);
    g_test_add_func("/host/quicklist-close-clicks", quicklist_close_clicks);
    g_test_add_func("/host/super-shortcut-tap-and-chords", super_shortcut_taps);
    g_test_add_func("/host/attention-rendered-motion", attention_rendered_motion);
    g_test_add_func("/host/attention-request-while-active", attention_request_while_active);
    g_test_add_func("/host/urgency-multiple-windows", urgency_multiple_windows);
    g_test_add_func("/host/attention-acknowledgement", attention_acknowledgement);
    return g_test_run();
}
