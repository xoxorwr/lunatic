/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtk/gtk.h>

static void greet(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    GtkWindow *window = gtk_application_get_active_window(data);
    if (window)
        gtk_label_set_text(g_object_get_data(G_OBJECT(window), "message"),
                           "The panel invoked this application's exported action.");
}

static void quit(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    g_application_quit(data);
}

static void activate(GtkApplication *application, gpointer data)
{
    (void)data;
    GtkWidget *window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Global Menu Demo");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 160);
    GtkWidget *label = gtk_label_new("Choose File → Greet from the panel's application menu.");
    gtk_container_add(GTK_CONTAINER(window), label);
    g_object_set_data(G_OBJECT(window), "message", label);
    gtk_widget_show_all(window);
}

static void startup(GtkApplication *application, gpointer data)
{
    (void)data;
    GSimpleAction *action = g_simple_action_new("greet", NULL);
    g_signal_connect(action, "activate", G_CALLBACK(greet), application);
    g_action_map_add_action(G_ACTION_MAP(application), G_ACTION(action));
    g_object_unref(action);
    action = g_simple_action_new("quit", NULL);
    g_signal_connect(action, "activate", G_CALLBACK(quit), application);
    g_action_map_add_action(G_ACTION_MAP(application), G_ACTION(action));
    g_object_unref(action);
    GMenu *bar = g_menu_new(), *file = g_menu_new();
    g_menu_append(file, "_Greet", "app.greet");
    g_menu_append(file, "_Quit", "app.quit");
    g_menu_append_submenu(bar, "_File", G_MENU_MODEL(file));
    gtk_application_set_menubar(application, G_MENU_MODEL(bar));
    g_object_unref(file);
    g_object_unref(bar);
}

int main(int argc, char **argv)
{
    GtkApplication *application =
        gtk_application_new("org.xoxorwr.Lunatic.MenuDemo", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
