/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-registrar.h"
#include "menu-view.h"
#include "gtk-menu-export.h"
#include "session-settings.h"
#include "title-label.h"
#include "app-name.h"
#include "window-control.h"
#include "wm-theme.h"
#include <string.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <gdk/gdkx.h>
#include <libxfce4panel/libxfce4panel.h>

typedef struct {
    gatomicrefcount references;
    gboolean closing;
    XfcePanelPlugin *plugin;
    GtkWidget *box, *label, *menu, *settings_dialog;
    GtkWidget *title_zone, *title_overlay, *action_row, *controls, *window_buttons[3];
    PanelWmTheme *wm_theme;
    char *wm_theme_name, *button_layout;
    guint theme_subscription, theme_idle, theme_generation;
    GCancellable *theme_pending;
    PanelWindowAtoms window_atoms;
    PanelWindowControls window_controls;
    gboolean title_hover, hide_titlebars;
    gboolean alt_down;
    int xkb_event;
    unsigned int alt_mask;
    guint focus_idle;
    gint title_width;
    PanelAppNames *app_names;
    char *program;
    guint open_popups;
    GdkDisplay *gdisplay;
    Display *display;
    Window root, active;
    long active_events;
    GDBusConnection *bus;
    PanelMenuRegistrar *registrar;
    GCancellable *pending, *session_setup, *titleless_setup;
    guint idle, registration_subscription, owner_subscription, generation;
} GlobalMenu;

typedef struct {
    GlobalMenu *state;
    guint generation;
} MenuRequest;

static void schedule_update(GlobalMenu *state);
static void update_window_controls(GlobalMenu *state);
static void apply_titleless(GlobalMenu *state);
static void schedule_theme(GlobalMenu *state);

static void state_unref(GlobalMenu *state)
{
    if (g_atomic_ref_count_dec(&state->references))
        g_free(state);
}

static Atom atom(GlobalMenu *state, const char *name)
{
    return XInternAtom(state->display, name, False);
}

static unsigned char *property(GlobalMenu *state, Window window, const char *name, Atom expected,
                               int expected_format, unsigned long *count)
{
    Atom type;
    int format;
    unsigned long remaining;
    unsigned char *data = NULL;
    *count = 0;
    gdk_x11_display_error_trap_push(state->gdisplay);
    int result = XGetWindowProperty(state->display, window, atom(state, name), 0, 1024, False,
                                    expected, &type, &format, count, &remaining, &data);
    int error = gdk_x11_display_error_trap_pop(state->gdisplay);
    if (error || result != Success || type != expected || format != expected_format || remaining) {
        if (data)
            XFree(data);
        *count = 0;
        return NULL;
    }
    return data;
}

static char *text_property(GlobalMenu *state, Window window, const char *name)
{
    unsigned long count;
    unsigned char *data = property(state, window, name, atom(state, "UTF8_STRING"), 8, &count);
    if (!data)
        return NULL;
    char *text = g_strndup((const char *)data, count);
    XFree(data);
    if (!g_utf8_validate(text, -1, NULL))
        g_clear_pointer(&text, g_free);
    return text;
}

static char *legacy_text(const unsigned char *data, gsize length)
{
    if (g_utf8_validate((const char *)data, length, NULL))
        return g_strndup((const char *)data, length);
    return g_convert((const char *)data, length, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
}

static void update_program(GlobalMenu *state)
{
    g_clear_pointer(&state->program, g_free);
    if (!state->active) return;
    char *application_id = text_property(state, state->active, "_GTK_APPLICATION_ID");
    unsigned long count;
    unsigned char *data = property(state, state->active, "WM_CLASS", XA_STRING, 8, &count);
    char *instance = NULL, *wmclass = NULL;
    if (data) {
        const unsigned char *end = memchr(data, '\0', count);
        if (end) {
            instance = legacy_text(data, end - data);
            gsize remaining = count - (end - data) - 1;
            const unsigned char *class_end = memchr(end + 1, '\0', remaining);
            if (class_end) wmclass = legacy_text(end + 1, class_end - end - 1);
        }
        XFree(data);
    }
    const char *name = panel_app_names_find(state->app_names, application_id, instance, wmclass);
    if (!name || !*name) name = wmclass && *wmclass ? wmclass : instance;
    if (!name || !*name) name = application_id;
    state->program = g_strdup(name && *name ? name : "Application");
    g_free(application_id);
    g_free(instance);
    g_free(wmclass);
}

static void update_title(GlobalMenu *state)
{
    char *title = state->active ? text_property(state, state->active, "_NET_WM_NAME") : NULL;
    if (!title && state->active) {
        unsigned long count;
        unsigned char *data = property(state, state->active, "WM_NAME", XA_STRING, 8, &count);
        if (data) {
            title = legacy_text(data, count);
            XFree(data);
        }
    }
    char *label = panel_app_title(state->program, title);
    char *markup = panel_app_title_markup(state->program, title);
    gtk_label_set_markup(GTK_LABEL(state->label), markup);
    g_free(markup);
    gtk_widget_set_tooltip_text(state->label, label);
    g_free(label);
    g_free(title);
}

static void reveal_window_controls(GlobalMenu *state)
{
    if (state->closing || !state->title_zone) return;
    GtkWidget *top = gtk_widget_get_toplevel(state->title_zone);
    GtkWidget *focus = GTK_IS_WINDOW(top) ? gtk_window_get_focus(GTK_WINDOW(top)) : NULL;
    gboolean focused = GTK_IS_WINDOW(top) && gtk_window_has_toplevel_focus(GTK_WINDOW(top)) &&
        focus && (focus == state->title_zone || gtk_widget_is_ancestor(focus, state->title_zone));
    gboolean controls = state->active && state->window_controls.maximized &&
        !state->window_controls.fullscreen && state->wm_theme &&
        (gtk_widget_get_visible(state->window_buttons[0]) ||
         gtk_widget_get_visible(state->window_buttons[1]) ||
         gtk_widget_get_visible(state->window_buttons[2]));
    gtk_widget_set_visible(state->controls, controls);
    gboolean show = state->active && (controls || state->menu) &&
        (state->title_hover || focused || state->alt_down || state->open_popups);
    /* child-visible affects mapping, not size requests. Only the action row
     * measures the overlay; the title never reserves space beside the menu. */
    gtk_widget_set_child_visible(state->action_row, show);
    gtk_widget_set_child_visible(state->label, !show);
}

static void update_window_controls(GlobalMenu *state)
{
    PanelWindowControls controls = {0};
    if (state->active) {
        unsigned long ns, na;
        Atom *states = (Atom *)property(state, state->active, "_NET_WM_STATE", XA_ATOM, 32, &ns);
        Atom *actions = (Atom *)property(state, state->active, "_NET_WM_ALLOWED_ACTIONS", XA_ATOM, 32, &na);
        controls = panel_window_controls_decode(&state->window_atoms, states, ns, actions, na, actions != NULL);
        if (states) XFree(states);
        if (actions) XFree(actions);
    }
    state->window_controls = controls;
    gtk_widget_set_sensitive(state->window_buttons[PANEL_WINDOW_CLOSE], controls.close);
    gtk_widget_set_sensitive(state->window_buttons[PANEL_WINDOW_MINIMIZE], controls.minimize);
    gtk_widget_set_sensitive(state->window_buttons[PANEL_WINDOW_RESTORE], controls.restore);
    reveal_window_controls(state);
}

static gboolean title_pointer_inside(GlobalMenu *state, double root_x, double root_y)
{
    GtkWidget *top = gtk_widget_get_toplevel(state->title_zone);
    GdkWindow *window = gtk_widget_get_window(top);
    gint x, y, origin_x, origin_y;
    if (!window || !gtk_widget_translate_coordinates(state->title_zone, top, 0, 0, &x, &y))
        return FALSE;
    gdk_window_get_origin(window, &origin_x, &origin_y);
    x += origin_x;
    y += origin_y;
    return root_x >= x && root_y >= y &&
        root_x < x + gtk_widget_get_allocated_width(state->title_zone) &&
        root_y < y + gtk_widget_get_allocated_height(state->title_zone);
}

static gboolean title_crossing(GtkWidget *widget, GdkEventCrossing *event, gpointer data)
{
    (void)widget;
    GlobalMenu *state = data;
    if (state->closing) return FALSE;
    if (event->type == GDK_ENTER_NOTIFY)
        state->title_hover = TRUE;
    else if (event->detail != GDK_NOTIFY_INFERIOR)
        /* Switching overlay layers and moving onto a button also produce leave
         * events. Only hide after the pointer leaves the whole title area. */
        state->title_hover = title_pointer_inside(state, event->x_root, event->y_root);
    reveal_window_controls(state);
    return FALSE;
}

static void watch_title_hover(GlobalMenu *state, GtkWidget *widget)
{
    /* Descendant input windows must also request crossing events. */
    gtk_widget_add_events(widget, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(widget, "enter-notify-event", G_CALLBACK(title_crossing), state);
    g_signal_connect(widget, "leave-notify-event", G_CALLBACK(title_crossing), state);
}

static gboolean focus_idle(gpointer data)
{
    GlobalMenu *state = data;
    state->focus_idle = 0;
    reveal_window_controls(state);
    return G_SOURCE_REMOVE;
}
static gboolean title_focus(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    (void)widget; (void)event;
    GlobalMenu *state = data;
    if (!state->closing && !state->focus_idle) state->focus_idle = g_idle_add(focus_idle, state);
    return FALSE;
}
static void control_pressed(GtkButton *button, gpointer data)
{
    GlobalMenu *state = data;
    g_object_set_data(G_OBJECT(button), "lunatic-window-target", GSIZE_TO_POINTER(state->active));
}
static void control_clicked(GtkButton *button, gpointer data)
{
    GlobalMenu *state = data;
    Window target = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(button), "lunatic-window-target"));
    if (!target || state->closing || target != state->active) return;
    unsigned long count;
    Window *active = (Window *)property(state, state->root, "_NET_ACTIVE_WINDOW", XA_WINDOW, 32, &count);
    gboolean current = active && count && active[0] == target;
    if (active) XFree(active);
    if (!current) return; /* Focus changed between press and release. */
    update_window_controls(state);
    PanelWindowCommand command = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "lunatic-window-command"));
    gboolean allowed = command == PANEL_WINDOW_CLOSE ? state->window_controls.close :
        command == PANEL_WINDOW_MINIMIZE ? state->window_controls.minimize : state->window_controls.restore;
    XEvent event;
    if (!allowed || !panel_window_control_event(&state->window_atoms, target, command,
                                               gtk_get_current_event_time(), &event)) return;
    event.xclient.display = state->display;
    gdk_x11_display_error_trap_push(state->gdisplay);
    XSendEvent(state->display, state->root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(state->display);
    gdk_x11_display_error_trap_pop_ignored(state->gdisplay);
}

static void popup_mapped(GtkWidget *popup, gpointer data)
{
    (void)popup;
    GlobalMenu *state = data;
    if (!state->closing && state->open_popups++ == 0)
        xfce_panel_plugin_block_autohide(state->plugin, TRUE);
    reveal_window_controls(state);
}

static void popup_unmapped(GtkWidget *popup, gpointer data)
{
    (void)popup;
    GlobalMenu *state = data;
    if (state->open_popups && --state->open_popups == 0)
        xfce_panel_plugin_block_autohide(state->plugin, FALSE);
    reveal_window_controls(state);
}

static void watch_menu_item(GtkMenuShell *shell, GtkWidget *item, gint position, gpointer data)
{
    (void)shell; (void)position;
    watch_title_hover(data, item);
    GtkWidget *popup = GTK_IS_MENU_ITEM(item) ? gtk_menu_item_get_submenu(GTK_MENU_ITEM(item)) : NULL;
    if (!popup) return;
    /* Only top-level popups need tracking; they remain mapped while a nested
     * submenu is open. Model-created items arrive with submenus attached. */
    g_signal_connect(popup, "map", G_CALLBACK(popup_mapped), data);
    g_signal_connect(popup, "unmap", G_CALLBACK(popup_unmapped), data);
}

static void set_menu(GlobalMenu *state, GtkWidget *menu)
{
    if (state->menu) {
        GtkWidget *old = state->menu;
        state->menu = NULL;
        gtk_widget_destroy(old);
        g_object_unref(old);
    }
    if (state->open_popups) {
        state->open_popups = 0;
        xfce_panel_plugin_block_autohide(state->plugin, FALSE);
    }
    if (menu) {
        state->menu = g_object_ref_sink(menu);
        gtk_widget_set_hexpand(menu, TRUE);
        gtk_widget_set_halign(menu, GTK_ALIGN_FILL);
        gtk_box_pack_start(GTK_BOX(state->action_row), menu, TRUE, TRUE, 0);
        watch_title_hover(state, menu);
        panel_menu_bar_set_mnemonics(menu, state->alt_down);
        if (state->plugin) xfce_panel_plugin_add_action_widget(state->plugin, menu);
        g_signal_connect_after(menu, "insert", G_CALLBACK(watch_menu_item), state);
        GList *items = gtk_container_get_children(GTK_CONTAINER(menu));
        for (GList *it = items; it; it = it->next)
            watch_menu_item(GTK_MENU_SHELL(menu), it->data, 0, state);
        g_list_free(items);
        gtk_widget_show(menu);
    }
    reveal_window_controls(state);
}

static void lookup_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    MenuRequest *request = data;
    GlobalMenu *state = request->state;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    if (!state->closing && request->generation == state->generation && reply) {
        const char *service, *path;
        g_variant_get(reply, "(&s&o)", &service, &path);
        set_menu(state, panel_menu_bar_dbusmenu(state->bus, service, path));
    }
    g_clear_pointer(&reply, g_variant_unref);
    g_clear_error(&error); /* No registrar/menu is a normal unsupported-app case. */
    state_unref(state);
    g_free(request);
}

static void watch_window(GlobalMenu *state, Window window)
{
    if (state->active == window)
        return;
    gdk_x11_display_error_trap_push(state->gdisplay);
    if (state->active)
        XSelectInput(state->display, state->active, state->active_events);
    state->active = window;
    if (window) {
        XWindowAttributes attributes;
        if (XGetWindowAttributes(state->display, window, &attributes)) {
            state->active_events = attributes.your_event_mask;
            XSelectInput(state->display, window,
                         attributes.your_event_mask | PropertyChangeMask | StructureNotifyMask);
        } else
            state->active = None;
    }
    gdk_x11_display_error_trap_pop_ignored(state->gdisplay);
}

static gboolean update_active(gpointer data)
{
    GlobalMenu *state = data;
    state->idle = 0;
    unsigned long count;
    Window *value =
        (Window *)property(state, state->root, "_NET_ACTIVE_WINDOW", XA_WINDOW, 32, &count);
    Window window = value && count ? value[0] : None;
    if (value)
        XFree(value);
    /* A popup owned by this external plugin must not replace its application's menu. */
    if (window && gdk_x11_window_lookup_for_display(state->gdisplay, window))
        return G_SOURCE_REMOVE;
    watch_window(state, window);
    update_program(state);
    update_window_controls(state);
    state->generation++;
    if (state->pending)
        g_cancellable_cancel(state->pending);
    g_clear_object(&state->pending);
    set_menu(state, NULL);
    update_title(state);
    if (!state->active || !state->bus)
        return G_SOURCE_REMOVE;
    char *export_bus = text_property(state, state->active, PANEL_MENU_BUS_PROPERTY);
    char *export_menu = text_property(state, state->active, PANEL_MENU_PATH_PROPERTY);
    char *export_actions = text_property(state, state->active, PANEL_MENU_ACTIONS_PROPERTY);
    GtkWidget *exported = panel_menu_bar_gmenu(state->bus, export_bus, export_menu, NULL,
                                               export_actions, NULL);
    g_free(export_bus);
    g_free(export_menu);
    g_free(export_actions);
    if (exported) {
        set_menu(state, exported);
        return G_SOURCE_REMOVE;
    }
    char *service = text_property(state, state->active, "_GTK_UNIQUE_BUS_NAME");
    char *menubar = text_property(state, state->active, "_GTK_MENUBAR_OBJECT_PATH");
    char *appmenu = text_property(state, state->active, "_GTK_APP_MENU_OBJECT_PATH");
    char *application = text_property(state, state->active, "_GTK_APPLICATION_OBJECT_PATH");
    char *window_path = text_property(state, state->active, "_GTK_WINDOW_OBJECT_PATH");
    GtkWidget *menu =
        panel_menu_bar_gmenu(state->bus, service, menubar, appmenu, application, window_path);
    g_free(service);
    g_free(menubar);
    g_free(appmenu);
    g_free(application);
    g_free(window_path);
    if (menu) {
        set_menu(state, menu);
    } else {
        state->pending = g_cancellable_new();
        MenuRequest *request = g_new0(MenuRequest, 1);
        g_atomic_ref_count_inc(&state->references);
        *request = (MenuRequest){state, state->generation};
        g_dbus_connection_call(state->bus, PANEL_REGISTRAR_NAME, PANEL_REGISTRAR_PATH,
                               PANEL_REGISTRAR_NAME, "GetMenuForWindow",
                               g_variant_new("(u)", (guint32)state->active), G_VARIANT_TYPE("(so)"),
                               G_DBUS_CALL_FLAGS_NO_AUTO_START, 1500, state->pending, lookup_ready,
                               request);
    }
    return G_SOURCE_REMOVE;
}

static void schedule_update(GlobalMenu *state)
{
    if (!state->closing && !state->idle)
        state->idle = g_idle_add(update_active, state);
}

static void update_alt(GlobalMenu *state, unsigned int modifiers)
{
    gboolean down = (modifiers & state->alt_mask) != 0;
    if (state->alt_down == down) return;
    state->alt_down = down;
    if (state->menu) panel_menu_bar_set_mnemonics(state->menu, down);
    reveal_window_controls(state);
}

static void refresh_alt(GlobalMenu *state)
{
    state->alt_mask = XkbKeysymToModifiers(state->display, XK_Alt_L) |
                      XkbKeysymToModifiers(state->display, XK_Alt_R);
    XkbStateRec keyboard;
    if (XkbGetState(state->display, XkbUseCoreKbd, &keyboard) == Success)
        update_alt(state, keyboard.mods);
}

static GdkFilterReturn window_event(GdkXEvent *raw, GdkEvent *event, gpointer data)
{
    (void)event;
    GlobalMenu *state = data;
    XEvent *xevent = raw;
    if (xevent->type == state->xkb_event) {
        XkbEvent *keyboard = (XkbEvent *)raw;
        if (keyboard->any.xkb_type == XkbStateNotify)
            update_alt(state, keyboard->state.mods);
        else if (keyboard->any.xkb_type == XkbMapNotify) {
            XkbRefreshKeyboardMapping(&keyboard->map);
            refresh_alt(state);
        } else if (keyboard->any.xkb_type == XkbNewKeyboardNotify)
            refresh_alt(state);
        return GDK_FILTER_CONTINUE;
    }
    if (xevent->type == DestroyNotify && xevent->xdestroywindow.window == state->active) {
        state->active = None;
        schedule_update(state);
    } else if (xevent->type == PropertyNotify) {
        XPropertyEvent *property_event = &xevent->xproperty;
        if (property_event->window == state->root &&
            property_event->atom == atom(state, "_NET_ACTIVE_WINDOW"))
            schedule_update(state);
        else if (property_event->window == state->active) {
            if (property_event->atom == state->window_atoms.state ||
                property_event->atom == state->window_atoms.allowed_actions)
                update_window_controls(state);
            else if (property_event->atom == atom(state, "WM_CLASS") ||
                property_event->atom == atom(state, "_GTK_APPLICATION_ID")) {
                update_program(state);
                update_title(state);
            } else if (property_event->atom == atom(state, "_NET_WM_NAME") ||
                       property_event->atom == atom(state, "WM_NAME"))
                update_title(state);
            else {
                static const char *const names[] = {
                    "_GTK_UNIQUE_BUS_NAME", "_GTK_MENUBAR_OBJECT_PATH", "_GTK_APP_MENU_OBJECT_PATH",
                    "_GTK_APPLICATION_OBJECT_PATH", "_GTK_WINDOW_OBJECT_PATH",
                    PANEL_MENU_BUS_PROPERTY, PANEL_MENU_PATH_PROPERTY, PANEL_MENU_ACTIONS_PROPERTY};
                for (guint i = 0; i < G_N_ELEMENTS(names); i++)
                    if (property_event->atom == atom(state, names[i])) {
                        schedule_update(state);
                        break;
                    }
            }
        }
    }
    return GDK_FILTER_CONTINUE;
}

static void registrar_event(GDBusConnection *connection, const char *sender, const char *path,
                            const char *interface, const char *signal, GVariant *parameters,
                            gpointer data)
{
    (void)connection;
    (void)sender;
    (void)path;
    (void)signal;
    GlobalMenu *state = data;
    if (state->closing)
        return;
    if (g_str_equal(interface, PANEL_REGISTRAR_NAME)) {
        GVariant *window = g_variant_get_child_value(parameters, 0);
        gboolean relevant = g_variant_is_of_type(window, G_VARIANT_TYPE_UINT32) &&
                            g_variant_get_uint32(window) == state->active;
        g_variant_unref(window);
        if (!relevant)
            return;
    }
    schedule_update(state);
}

static void load_settings(GlobalMenu *state)
{
    state->title_width = PANEL_TITLE_WIDTH_DEFAULT;
    state->hide_titlebars = TRUE;
    char *path = xfce_panel_plugin_lookup_rc_file(state->plugin);
    GKeyFile *file = g_key_file_new();
    if (path && g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
        GError *error = NULL;
        gint width = g_key_file_get_integer(file, "Global Menu", "title-max-width", &error);
        if (!error) state->title_width = CLAMP(width, PANEL_TITLE_WIDTH_MIN, PANEL_TITLE_WIDTH_MAX);
        g_clear_error(&error);
        if (g_key_file_has_key(file, "Global Menu", "hide-maximized-titlebars", NULL)) {
            gboolean hide = g_key_file_get_boolean(file, "Global Menu", "hide-maximized-titlebars", &error);
            if (!error) state->hide_titlebars = hide;
            g_clear_error(&error);
        }
    }
    g_key_file_unref(file);
    g_free(path);
}

static void save_settings(XfcePanelPlugin *plugin, gpointer data)
{
    GlobalMenu *state = data;
    char *path = xfce_panel_plugin_save_location(plugin, TRUE);
    if (!path) return;
    GKeyFile *file = g_key_file_new();
    g_key_file_load_from_file(file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_key_file_set_integer(file, "Global Menu", "title-max-width", state->title_width);
    g_key_file_set_boolean(file, "Global Menu", "hide-maximized-titlebars", state->hide_titlebars);
    char *contents = g_key_file_to_data(file, NULL, NULL);
    GError *error = NULL;
    if (!g_file_set_contents(path, contents, -1, &error)) {
        g_warning("Cannot save global menu settings: %s", error->message);
        g_clear_error(&error);
    }
    g_free(contents);
    g_key_file_unref(file);
    g_free(path);
}

static void titleless_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)data;
    GError *error = NULL;
    if (!panel_titleless_set_finish(G_DBUS_CONNECTION(object), result, &error)) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("Cannot configure xfwm4 maximized title bars: %s", error->message);
        g_clear_error(&error);
    }
}
static void apply_titleless(GlobalMenu *state)
{
    if (!state->bus) return;
    if (state->titleless_setup) g_cancellable_cancel(state->titleless_setup);
    g_clear_object(&state->titleless_setup);
    state->titleless_setup = g_cancellable_new();
    panel_titleless_set_async(state->bus, state->hide_titlebars, state->titleless_setup, titleless_ready, NULL);
}
static void hide_titlebars_changed(GtkToggleButton *button, gpointer data)
{
    GlobalMenu *state = data;
    state->hide_titlebars = gtk_toggle_button_get_active(button);
    apply_titleless(state);
    save_settings(state->plugin, state);
}

static void title_width_changed(GtkSpinButton *spin, gpointer data)
{
    GlobalMenu *state = data;
    state->title_width = gtk_spin_button_get_value_as_int(spin);
    panel_title_label_set_max_width(PANEL_TITLE_LABEL(state->label), state->title_width);
    save_settings(state->plugin, state);
}

static void settings_destroyed(GtkWidget *dialog, gpointer data)
{
    (void)dialog;
    GlobalMenu *state = data;
    state->settings_dialog = NULL;
    xfce_panel_plugin_unblock_menu(state->plugin);
}

static void settings_response(GtkDialog *dialog, gint response, gpointer data)
{
    (void)response; (void)data;
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void configure(XfcePanelPlugin *plugin, gpointer data)
{
    GlobalMenu *state = data;
    if (state->settings_dialog) {
        gtk_window_present(GTK_WINDOW(state->settings_dialog));
        return;
    }
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Lunatic Global Menu", NULL, 0,
                                                   "_Close", GTK_RESPONSE_CLOSE, NULL);
    state->settings_dialog = dialog;
    gtk_window_set_screen(GTK_WINDOW(dialog), gtk_widget_get_screen(GTK_WIDGET(plugin)));
    gtk_window_set_icon_name(GTK_WINDOW(dialog), "applications-system");
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
    GtkWidget *grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    GtkWidget *label = gtk_label_new_with_mnemonic("Maximum _title width (pixels):");
    GtkWidget *spin = gtk_spin_button_new_with_range(PANEL_TITLE_WIDTH_MIN, PANEL_TITLE_WIDTH_MAX, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), state->title_width);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spin, 1, 0, 1, 1);
    GtkWidget *hide = gtk_check_button_new_with_mnemonic("_Hide title bars of maximized windows (xfwm4)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(hide), state->hide_titlebars);
    gtk_widget_set_tooltip_text(hide, "Changes xfwm4's session-wide setting for window-manager title bars.");
    gtk_grid_attach(GTK_GRID(grid), hide, 0, 1, 2, 1);
    g_signal_connect(hide, "toggled", G_CALLBACK(hide_titlebars_changed), state);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
    g_signal_connect(spin, "value-changed", G_CALLBACK(title_width_changed), state);
    g_signal_connect(dialog, "response", G_CALLBACK(settings_response), NULL);
    g_signal_connect(dialog, "destroy", G_CALLBACK(settings_destroyed), state);
    xfce_panel_plugin_block_menu(plugin);
    gtk_widget_show_all(dialog);
}

static void removed(XfcePanelPlugin *plugin, gpointer data)
{
    GlobalMenu *state = data;
    if (state->session_setup) g_cancellable_cancel(state->session_setup);
    if (!state->bus) return;
    GError *error = NULL;
    if (!panel_menu_session_remove(state->bus, xfce_panel_plugin_get_unique_id(plugin), &error)) {
        g_warning("Cannot remove GTK menu export from Xfce settings: %s", error->message);
        g_clear_error(&error);
    }
}

static void free_data(XfcePanelPlugin *plugin, gpointer data)
{
    (void)plugin;
    GlobalMenu *state = data;
    state->closing = TRUE;
    g_signal_handlers_disconnect_by_data(gtk_widget_get_settings(state->box), state);
    if (state->focus_idle) g_source_remove(state->focus_idle);
    if (state->theme_idle) g_source_remove(state->theme_idle);
    if (state->theme_pending) g_cancellable_cancel(state->theme_pending);
    g_clear_object(&state->theme_pending);
    if (state->titleless_setup) g_cancellable_cancel(state->titleless_setup);
    g_clear_object(&state->titleless_setup);
    g_signal_handlers_disconnect_by_data(state->title_zone, state);
    g_signal_handlers_disconnect_by_data(state->title_overlay, state);
    g_signal_handlers_disconnect_by_data(state->action_row, state);
    for (guint i = 0; i < G_N_ELEMENTS(state->window_buttons); i++)
        g_signal_handlers_disconnect_by_data(state->window_buttons[i], state);
    if (state->session_setup) g_cancellable_cancel(state->session_setup);
    g_clear_object(&state->session_setup);
    if (state->settings_dialog) gtk_widget_destroy(state->settings_dialog);
    state->generation++;
    if (state->idle)
        g_source_remove(state->idle);
    gdk_window_remove_filter(NULL, window_event, state);
    watch_window(state, None);
    if (state->pending)
        g_cancellable_cancel(state->pending);
    g_clear_object(&state->pending);
    set_menu(state, NULL);
    if (state->bus) {
        if (state->theme_subscription) g_dbus_connection_signal_unsubscribe(state->bus, state->theme_subscription);
        g_dbus_connection_signal_unsubscribe(state->bus, state->registration_subscription);
        g_dbus_connection_signal_unsubscribe(state->bus, state->owner_subscription);
    }
    panel_menu_registrar_free(state->registrar);
    g_clear_object(&state->bus);
    panel_app_names_free(state->app_names);
    panel_wm_theme_unref(state->wm_theme);
    g_free(state->wm_theme_name);
    g_free(state->button_layout);
    g_clear_pointer(&state->program, g_free);
    state_unref(state);
}

static void session_setup_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    (void)data;
    GError *error = NULL;
    if (!panel_menu_session_setup_finish(G_DBUS_CONNECTION(object), result, &error)) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("Cannot enable GTK menu export in Xfce settings: %s", error->message);
        g_clear_error(&error);
    }
}

static void apply_wm_theme(GlobalMenu *state)
{
    PanelWmTheme *theme = panel_wm_theme_new(state->wm_theme_name, state->title_zone);
    PanelWindowCommand order[3];
    guint count = panel_wm_button_order(state->button_layout, order);
    for (guint i = 0; i < 3; i++) {
        panel_wm_button_set_theme(state->window_buttons[i], theme);
        gtk_widget_hide(state->window_buttons[i]);
    }
    for (guint i = 0; i < count; i++) {
        GtkWidget *button = state->window_buttons[order[i]];
        gtk_box_reorder_child(GTK_BOX(state->controls), button, i);
        gtk_widget_set_visible(button, panel_wm_theme_frame(theme, order[i], PANEL_WM_NORMAL) != NULL);
    }
    gtk_box_set_spacing(GTK_BOX(state->controls), panel_wm_theme_spacing(theme));
    panel_wm_theme_unref(state->wm_theme);
    state->wm_theme = theme;
    reveal_window_controls(state);
}

static void wm_theme_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    MenuRequest *request = data;
    GlobalMenu *state = request->state;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    if (!state->closing && request->generation == state->theme_generation &&
        !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        if (reply) {
            GVariant *properties = g_variant_get_child_value(reply, 0);
            const char *name = NULL, *layout = NULL;
            g_variant_lookup(properties, "/general/theme", "&s", &name);
            g_variant_lookup(properties, "/general/button_layout", "&s", &layout);
            g_free(state->wm_theme_name); g_free(state->button_layout);
            state->wm_theme_name = g_strdup(name); state->button_layout = g_strdup(layout);
            g_variant_unref(properties);
        }
        apply_wm_theme(state);
    }
    g_clear_pointer(&reply, g_variant_unref);
    g_clear_error(&error);
    state_unref(state);
    g_free(request);
}

static gboolean read_wm_theme(gpointer data)
{
    GlobalMenu *state = data;
    state->theme_idle = 0;
    if (!state->bus) { apply_wm_theme(state); return G_SOURCE_REMOVE; }
    if (state->theme_pending) g_cancellable_cancel(state->theme_pending);
    g_clear_object(&state->theme_pending);
    state->theme_pending = g_cancellable_new();
    MenuRequest *request = g_new0(MenuRequest, 1);
    request->state = state; request->generation = ++state->theme_generation;
    g_atomic_ref_count_inc(&state->references);
    g_dbus_connection_call(state->bus, "org.xfce.Xfconf", "/org/xfce/Xfconf", "org.xfce.Xfconf",
        "GetAllProperties", g_variant_new("(ss)", "xfwm4", "/general"), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 2000, state->theme_pending, wm_theme_ready, request);
    return G_SOURCE_REMOVE;
}

static void schedule_theme(GlobalMenu *state)
{
    if (!state->closing && !state->theme_idle) state->theme_idle = g_idle_add(read_wm_theme, state);
}

static void wm_theme_changed(GDBusConnection *bus, const char *sender, const char *path,
                             const char *interface, const char *signal, GVariant *parameters, gpointer data)
{
    (void)bus; (void)sender; (void)path; (void)interface; (void)signal;
    const char *channel, *property;
    GVariant *value;
    g_variant_get(parameters, "(&s&sv)", &channel, &property, &value);
    if (g_str_equal(channel, "xfwm4") &&
        (g_str_equal(property, "/general/theme") || g_str_equal(property, "/general/button_layout")))
        schedule_theme(data);
    g_variant_unref(value);
}

static void wm_style_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void)object; (void)pspec;
    schedule_theme(data);
}

static void create_title_area(GlobalMenu *state)
{
    state->title_zone = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(state->title_zone), FALSE);
    gtk_widget_set_can_focus(state->title_zone, TRUE);
    state->title_overlay = gtk_overlay_new();
    state->action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    state->controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_no_show_all(state->controls, TRUE);
    const char *tips[] = {"Close window", "Minimize window", "Restore window"};
    for (guint i = 0; i < G_N_ELEMENTS(state->window_buttons); i++) {
        GtkWidget *button = panel_wm_button_new(i);
        state->window_buttons[i] = button;
        gtk_widget_set_no_show_all(button, TRUE);
        watch_title_hover(state, button);
        gtk_widget_set_tooltip_text(button, tips[i]);
        atk_object_set_name(gtk_widget_get_accessible(button), tips[i]);
        gtk_widget_set_sensitive(button, FALSE);
        g_object_set_data(G_OBJECT(button), "lunatic-window-command", GINT_TO_POINTER(i));
        g_signal_connect(button, "pressed", G_CALLBACK(control_pressed), state);
        g_signal_connect(button, "activate", G_CALLBACK(control_pressed), state);
        g_signal_connect(button, "clicked", G_CALLBACK(control_clicked), state);
        g_signal_connect(button, "focus-in-event", G_CALLBACK(title_focus), state);
        g_signal_connect(button, "focus-out-event", G_CALLBACK(title_focus), state);
        gtk_box_pack_start(GTK_BOX(state->controls), button, FALSE, FALSE, 0);
        gtk_widget_show(button);
    }
    gtk_box_pack_start(GTK_BOX(state->action_row), state->controls, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(state->title_overlay), state->action_row);
    gtk_overlay_add_overlay(GTK_OVERLAY(state->title_overlay), state->label);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(state->title_overlay), state->label, TRUE);
    gtk_container_add(GTK_CONTAINER(state->title_zone), state->title_overlay);
    gtk_box_pack_start(GTK_BOX(state->box), state->title_zone, TRUE, TRUE, 0);
    watch_title_hover(state, state->title_zone);
    watch_title_hover(state, state->title_overlay);
    watch_title_hover(state, state->action_row);
    g_signal_connect(state->title_zone, "focus-in-event", G_CALLBACK(title_focus), state);
    g_signal_connect(state->title_zone, "focus-out-event", G_CALLBACK(title_focus), state);
}

static void construct(XfcePanelPlugin *plugin)
{
    GlobalMenu *state = g_new0(GlobalMenu, 1);
    g_atomic_ref_count_init(&state->references);
    state->plugin = plugin;
    state->app_names = panel_app_names_new();
    load_settings(state);
    state->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    state->label = panel_title_label_new(state->title_width);
    gtk_label_set_text(GTK_LABEL(state->label), "Application");
    gtk_widget_set_margin_start(state->label, 4);
    gtk_widget_set_margin_end(state->label, 8);
    gtk_widget_set_hexpand(GTK_WIDGET(plugin), TRUE);
    gtk_widget_set_hexpand(state->box, TRUE);
    gtk_widget_set_halign(state->box, GTK_ALIGN_FILL);
    create_title_area(state);
    gtk_container_add(GTK_CONTAINER(plugin), state->box);
    xfce_panel_plugin_add_action_widget(plugin, state->box);
    xfce_panel_plugin_set_expand(plugin, TRUE);
    xfce_panel_plugin_set_small(plugin, FALSE);
    state->gdisplay = gtk_widget_get_display(GTK_WIDGET(plugin));
    if (!GDK_IS_X11_DISPLAY(state->gdisplay)) {
        gtk_widget_set_sensitive(state->box, FALSE);
        gtk_widget_set_tooltip_text(state->box, "Global menu requires an X11 session");
        gtk_widget_show_all(GTK_WIDGET(plugin));
        g_signal_handlers_disconnect_by_data(state->title_zone, state);
        g_signal_handlers_disconnect_by_data(state->title_overlay, state);
        g_signal_handlers_disconnect_by_data(state->action_row, state);
        for (guint i = 0; i < G_N_ELEMENTS(state->window_buttons); i++)
            g_signal_handlers_disconnect_by_data(state->window_buttons[i], state);
        panel_app_names_free(state->app_names);
        g_free(state);
        return;
    }
    state->display = gdk_x11_display_get_xdisplay(state->gdisplay);
    state->xkb_event = -1;
    int opcode, error_base, major = XkbMajorVersion, minor = XkbMinorVersion;
    if (XkbQueryExtension(state->display, &opcode, &state->xkb_event, &error_base, &major, &minor)) {
        XkbSelectEventDetails(state->display, XkbUseCoreKbd, XkbStateNotify,
                             XkbModifierStateMask, XkbModifierStateMask);
        XkbSelectEvents(state->display, XkbUseCoreKbd,
                        XkbMapNotifyMask | XkbNewKeyboardNotifyMask,
                        XkbMapNotifyMask | XkbNewKeyboardNotifyMask);
        refresh_alt(state);
    }
    panel_window_atoms_init(state->display, &state->window_atoms);
    state->root = DefaultRootWindow(state->display);
    state->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (state->bus) {
        state->theme_subscription = g_dbus_connection_signal_subscribe(state->bus,
            "org.xfce.Xfconf", "org.xfce.Xfconf", "PropertyChanged", "/org/xfce/Xfconf", "xfwm4",
            G_DBUS_SIGNAL_FLAGS_NONE, wm_theme_changed, state, (GDestroyNotify)state_unref);
        g_atomic_ref_count_inc(&state->references);
        apply_titleless(state);
        state->session_setup = g_cancellable_new();
        panel_menu_session_setup_async(state->bus, TRUE, state->session_setup, session_setup_ready, NULL);
        GError *error = NULL;
        state->registrar = panel_menu_registrar_new(state->bus, &error);
        if (error) {
            g_warning("Cannot host global-menu registrar: %s", error->message);
            g_clear_error(&error);
        }
        state->registration_subscription = g_dbus_connection_signal_subscribe(
            state->bus, PANEL_REGISTRAR_NAME, PANEL_REGISTRAR_NAME, NULL, PANEL_REGISTRAR_PATH,
            NULL, G_DBUS_SIGNAL_FLAGS_NONE, registrar_event, state, (GDestroyNotify)state_unref);
        g_atomic_ref_count_inc(&state->references);
        state->owner_subscription = g_dbus_connection_signal_subscribe(
            state->bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
            "/org/freedesktop/DBus", PANEL_REGISTRAR_NAME, G_DBUS_SIGNAL_FLAGS_NONE,
            registrar_event, state, (GDestroyNotify)state_unref);
        g_atomic_ref_count_inc(&state->references);
    }
    GdkWindow *root = gdk_get_default_root_window();
    gdk_window_set_events(root, gdk_window_get_events(root) | GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(NULL, window_event, state);
    xfce_panel_plugin_menu_show_configure(plugin);
    g_signal_connect(plugin, "configure-plugin", G_CALLBACK(configure), state);
    g_signal_connect(plugin, "save", G_CALLBACK(save_settings), state);
    g_signal_connect(plugin, "removed", G_CALLBACK(removed), state);
    g_signal_connect(plugin, "free-data", G_CALLBACK(free_data), state);
    GtkSettings *settings = gtk_widget_get_settings(state->box);
    g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(wm_style_changed), state);
    g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme", G_CALLBACK(wm_style_changed), state);
    g_signal_connect(state->title_zone, "notify::scale-factor", G_CALLBACK(wm_style_changed), state);
    gtk_widget_show_all(GTK_WIDGET(plugin));
    reveal_window_controls(state);
    schedule_theme(state);
    schedule_update(state);
}

XFCE_PANEL_PLUGIN_REGISTER(construct)
