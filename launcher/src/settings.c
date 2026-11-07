/* SPDX-License-Identifier: GPL-3.0-only */
#include "settings.h"
#include <math.h>

void launcher_settings_defaults(LauncherSettings *s)
{
    *s = (LauncherSettings){.hide_animation = HIDE_FADE_SLIDE,
                            .show_trash = TRUE,
                            .reveal_sensitivity = (2.0 - .2) / 7.8 * 100,
                            .opacity = .6667,
                            .color_mode = COLOR_WALLPAPER,
                            .visibility = VISIBILITY_PRIMARY,
                            .position = DOCK_LEFT,
                            .icon_size = 48,
                            .custom_color = {95 / 255.0, 18 / 255.0, 45 / 255.0, 1}};
}

static int integer(GKeyFile *key, const char *name, int fallback, int minimum, int maximum)
{
    GError *error = NULL;
    int value = g_key_file_get_integer(key, "Launcher", name, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return CLAMP(value, minimum, maximum);
}
static double number(GKeyFile *key, const char *name, double fallback, double minimum,
                     double maximum)
{
    GError *error = NULL;
    double value = g_key_file_get_double(key, "Launcher", name, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return isfinite(value) ? CLAMP(value, minimum, maximum) : fallback;
}
static gboolean boolean(GKeyFile *key, const char *name, gboolean fallback)
{
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(key, "Launcher", name, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}
void launcher_settings_read(LauncherSettings *s, GKeyFile *key)
{
    s->autohide = boolean(key, "Autohide", s->autohide);
    s->minimize_on_click = boolean(key, "MinimizeOnClick", s->minimize_on_click);
    s->all_workspaces = boolean(key, "AllWorkspaces", s->all_workspaces);
    s->show_desktop = boolean(key, "ShowDesktop", s->show_desktop);
    s->show_trash = boolean(key, "ShowTrash", s->show_trash);
    s->hide_animation =
        integer(key, "HideAnimation", s->hide_animation, HIDE_SLIDE, HIDE_FADE_SLIDE);
    s->reveal_sensitivity = number(key, "RevealSensitivity", s->reveal_sensitivity, 0, 100);
    s->opacity = number(key, "Opacity", s->opacity, 0, 1);
    s->color_mode = integer(key, "ColorMode", s->color_mode, COLOR_WALLPAPER, COLOR_CUSTOM);
    s->visibility = integer(key, "Visibility", s->visibility, VISIBILITY_ALL, VISIBILITY_PRIMARY);
    s->position = integer(key, "Position", s->position, DOCK_LEFT, DOCK_RIGHT);
    s->icon_size = integer(key, "IconSize", s->icon_size, 24, 96);
    s->monitor = integer(key, "Monitor", s->monitor, 0, G_MAXINT);
    char *color = g_key_file_get_string(key, "Launcher", "CustomColor", NULL);
    GdkRGBA parsed;
    if (color && gdk_rgba_parse(&parsed, color)) {
        s->custom_color = parsed;
        s->custom_color.alpha = 1;
    }
    g_free(color);
}
void launcher_settings_write(const LauncherSettings *s, GKeyFile *key)
{
    g_key_file_set_boolean(key, "Launcher", "Autohide", s->autohide);
    g_key_file_set_boolean(key, "Launcher", "MinimizeOnClick", s->minimize_on_click);
    g_key_file_set_boolean(key, "Launcher", "AllWorkspaces", s->all_workspaces);
    g_key_file_set_boolean(key, "Launcher", "ShowDesktop", s->show_desktop);
    g_key_file_set_boolean(key, "Launcher", "ShowTrash", s->show_trash);
    g_key_file_set_integer(key, "Launcher", "HideAnimation", s->hide_animation);
    g_key_file_set_double(key, "Launcher", "RevealSensitivity", s->reveal_sensitivity);
    g_key_file_set_double(key, "Launcher", "Opacity", s->opacity);
    g_key_file_set_integer(key, "Launcher", "ColorMode", s->color_mode);
    g_key_file_set_integer(key, "Launcher", "Visibility", s->visibility);
    g_key_file_set_integer(key, "Launcher", "Position", s->position);
    g_key_file_set_integer(key, "Launcher", "IconSize", s->icon_size);
    g_key_file_set_integer(key, "Launcher", "Monitor", s->monitor);
    char *color = gdk_rgba_to_string(&s->custom_color);
    g_key_file_set_string(key, "Launcher", "CustomColor", color);
    g_free(color);
}

gboolean launcher_settings_window_visible(const LauncherSettings *s, unsigned long desktop,
                                          unsigned long current)
{
    return s->all_workspaces || desktop == current || desktop == 0xffffffffUL;
}

typedef struct {
    LauncherSettings *settings;
    LauncherSettingsChanged changed;
    gpointer data;
    GtkWidget *autohide, *hide_animation, *sensitivity, *minimize, *workspaces;
    GtkWidget *transparency, *color_mode, *color, *visibility, *position, *icon_size, *desktop,
        *trash;
    GtkWidget *animation_row, *sensitivity_row;
} Dialog;

static void controls_changed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    Dialog *d = data;
    LauncherSettings previous = *d->settings;
    LauncherSettings *s = d->settings;
    s->autohide = gtk_switch_get_active(GTK_SWITCH(d->autohide));
    s->hide_animation = gtk_combo_box_get_active(GTK_COMBO_BOX(d->hide_animation));
    s->reveal_sensitivity = gtk_range_get_value(GTK_RANGE(d->sensitivity));
    s->minimize_on_click = gtk_switch_get_active(GTK_SWITCH(d->minimize));
    s->all_workspaces = gtk_switch_get_active(GTK_SWITCH(d->workspaces));
    s->opacity = 1 - gtk_range_get_value(GTK_RANGE(d->transparency)) / 100;
    s->color_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(d->color_mode));
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(d->color), &s->custom_color);
    s->custom_color.alpha = 1;
    s->visibility = gtk_combo_box_get_active(GTK_COMBO_BOX(d->visibility));
    s->position = gtk_combo_box_get_active(GTK_COMBO_BOX(d->position));
    s->icon_size = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->icon_size));
    s->show_desktop = gtk_switch_get_active(GTK_SWITCH(d->desktop));
    s->show_trash = gtk_switch_get_active(GTK_SWITCH(d->trash));
    gtk_widget_set_sensitive(d->animation_row, s->autohide);
    gtk_widget_set_sensitive(d->sensitivity_row, s->autohide);
    gtk_widget_set_sensitive(d->color, s->color_mode == COLOR_CUSTOM);
    if (d->changed)
        d->changed(&previous, d->data);
}
static void switch_changed(GObject *object, GParamSpec *spec, gpointer data)
{
    (void)spec;
    controls_changed(GTK_WIDGET(object), data);
}

static GtkWidget *row(GtkWidget *box, const char *name, GtkWidget *control)
{
    GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_halign(control, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(line), label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(line), control, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), control);
    atk_object_set_name(gtk_widget_get_accessible(control), name);
    return line;
}
static GtkWidget *page(GtkWidget *book, const char *name)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_container_set_border_width(GTK_CONTAINER(box), 20);
    gtk_notebook_append_page(GTK_NOTEBOOK(book), box, gtk_label_new(name));
    return box;
}
static GtkWidget *toggle(gboolean active)
{
    GtkWidget *w = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(w), active);
    return w;
}
static GtkWidget *combo(const char *const *labels, int selected)
{
    GtkWidget *w = gtk_combo_box_text_new();
    for (guint i = 0; labels[i]; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), labels[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), selected);
    return w;
}
static GtkWidget *slider(double value)
{
    GtkWidget *w = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_widget_set_size_request(w, 200, -1);
    gtk_scale_set_digits(GTK_SCALE(w), 0);
    gtk_scale_set_value_pos(GTK_SCALE(w), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(w), value);
    return w;
}

GtkWidget *launcher_settings_dialog_new(GtkWindow *parent, LauncherSettings *s,
                                        LauncherSettingsChanged changed, gpointer data)
{
    Dialog *d = g_new0(Dialog, 1);
    d->settings = s;
    d->changed = changed;
    d->data = data;
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons("Lunatic Launcher Settings", parent, GTK_DIALOG_DESTROY_WITH_PARENT,
                                    "_Close", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 580, 380);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    GtkWidget *book = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), book, TRUE, TRUE,
                       8);
    GtkWidget *behavior = page(book, "Behavior");
    d->autohide = toggle(s->autohide);
    row(behavior, "Auto hide", d->autohide);
    const char *animations[] = {"Slide", "Fade", "Fade and slide", NULL};
    d->hide_animation = combo(animations, s->hide_animation);
    d->animation_row = row(behavior, "Auto hide animation", d->hide_animation);
    d->sensitivity = slider(s->reveal_sensitivity);
    gtk_widget_set_tooltip_text(
        d->sensitivity,
        "Higher sensitivity reveals the launcher with less pressure at the screen edge.");
    d->sensitivity_row = row(behavior, "Reveal sensitivity", d->sensitivity);
    d->minimize = toggle(s->minimize_on_click);
    row(behavior, "Minimize single-window programs on click", d->minimize);
    d->workspaces = toggle(s->all_workspaces);
    row(behavior, "Show programs from all workspaces", d->workspaces);
    GtkWidget *appearance = page(book, "Appearance");
    d->transparency = slider((1 - s->opacity) * 100);
    row(appearance, "Transparency (%)", d->transparency);
    const char *colors[] = {"Based on wallpaper", "Custom", NULL};
    d->color_mode = combo(colors, s->color_mode);
    row(appearance, "Color", d->color_mode);
    d->color = gtk_color_button_new_with_rgba(&s->custom_color);
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(d->color), FALSE);
    row(appearance, "Custom color", d->color);
    const char *visibility[] = {"All monitors", "Primary monitor only", NULL};
    d->visibility = combo(visibility, s->visibility);
    row(appearance, "Visibility", d->visibility);
    const char *positions[] = {"Left", "Right", NULL};
    d->position = combo(positions, s->position);
    row(appearance, "Position", d->position);
    GtkWidget *icons = page(book, "Icons");
    d->icon_size = gtk_spin_button_new_with_range(24, 96, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->icon_size), s->icon_size);
    row(icons, "Icon size (pixels)", d->icon_size);
    d->desktop = toggle(s->show_desktop);
    row(icons, "Show Desktop", d->desktop);
    d->trash = toggle(s->show_trash);
    row(icons, "Show Trash", d->trash);
    gtk_widget_set_sensitive(d->animation_row, s->autohide);
    gtk_widget_set_sensitive(d->sensitivity_row, s->autohide);
    gtk_widget_set_sensitive(d->color, s->color_mode == COLOR_CUSTOM);
    GtkWidget *switches[] = {d->autohide, d->minimize, d->workspaces, d->desktop, d->trash};
    for (guint i = 0; i < G_N_ELEMENTS(switches); i++)
        g_signal_connect(switches[i], "notify::active", G_CALLBACK(switch_changed), d);
    GtkWidget *combos[] = {d->hide_animation, d->color_mode, d->visibility, d->position};
    for (guint i = 0; i < G_N_ELEMENTS(combos); i++)
        g_signal_connect(combos[i], "changed", G_CALLBACK(controls_changed), d);
    g_signal_connect(d->sensitivity, "value-changed", G_CALLBACK(controls_changed), d);
    g_signal_connect(d->transparency, "value-changed", G_CALLBACK(controls_changed), d);
    g_signal_connect(d->icon_size, "value-changed", G_CALLBACK(controls_changed), d);
    g_signal_connect(d->color, "color-set", G_CALLBACK(controls_changed), d);
    g_object_set_data_full(G_OBJECT(dialog), "settings-dialog", d, g_free);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
    gtk_widget_show_all(dialog);
    return dialog;
}
