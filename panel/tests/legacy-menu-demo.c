/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtk/gtk.h>
static void greet(GtkMenuItem *item, gpointer label)
{
    (void)item;
    gtk_label_set_text(GTK_LABEL(label), "The in-tree GTK exporter invoked this menu item.");
}
static void populate(GtkWidget *menu, gpointer label)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList *it = children; it; it = it->next) gtk_widget_destroy(it->data);
    g_list_free(children);
    GtkWidget *item = gtk_menu_item_new_with_mnemonic("_Generated Greet");
    g_signal_connect(item, "activate", G_CALLBACK(greet), label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show(item);
}
int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Legacy GTK Menu Demo");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 180);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *bar = gtk_menu_bar_new();
    GtkWidget *file = gtk_menu_item_new_with_mnemonic("_File");
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *run = gtk_menu_item_new_with_mnemonic("_Greet");
    GtkWidget *check = gtk_check_menu_item_new_with_mnemonic("_Check");
    GtkWidget *label = gtk_label_new("Choose File → Greet in the panel menubar.");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), run);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), check);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), file);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), box);
    /* Attach after show_all so this starts empty, like Thunar's lazy menus. */
    g_signal_connect(run, "activate", G_CALLBACK(greet), label);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(window);
    GtkWidget *lazy = gtk_menu_item_new_with_mnemonic("_Lazy");
    GtkWidget *lazy_menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(lazy), lazy_menu);
    g_signal_connect(lazy_menu, "show", G_CALLBACK(populate), label);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), lazy);
    gtk_widget_show(lazy);
    gtk_main();
    return 0;
}
