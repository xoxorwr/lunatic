/* SPDX-License-Identifier: GPL-3.0-only */
#include "app-name.h"
#include <gio/gdesktopappinfo.h>
#include <string.h>

struct PanelAppNames {
    GHashTable *ids, *classes, *tails, *executables;
    GAppInfoMonitor *monitor;
    gboolean dirty;
};
static char *normalize(const char *text)
{
    char *key = g_utf8_casefold(text ? text : "", -1);
    if (g_str_has_suffix(key, ".desktop")) key[strlen(key) - 8] = '\0';
    return key;
}
static void insert(GHashTable *table, const char *key, const char *name)
{
    if (!key || !*key || !name || !*name) return;
    char *normalized = normalize(key);
    if (!g_hash_table_contains(table, normalized))
        g_hash_table_insert(table, normalized, g_strdup(name));
    else g_free(normalized);
}
static void changed(GAppInfoMonitor *monitor, gpointer data)
{
    (void)monitor;
    ((PanelAppNames *)data)->dirty = TRUE;
}
PanelAppNames *panel_app_names_new(void)
{
    PanelAppNames *names = g_new0(PanelAppNames, 1);
    names->ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    names->classes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    names->tails = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    names->executables = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    names->monitor = g_app_info_monitor_get();
    g_signal_connect(names->monitor, "changed", G_CALLBACK(changed), names);
    names->dirty = TRUE;
    return names;
}
void panel_app_names_free(PanelAppNames *names)
{
    g_signal_handlers_disconnect_by_data(names->monitor, names);
    g_object_unref(names->monitor);
    g_hash_table_unref(names->ids);
    g_hash_table_unref(names->classes);
    g_hash_table_unref(names->tails);
    g_hash_table_unref(names->executables);
    g_free(names);
}
void panel_app_names_load(PanelAppNames *names, GList *apps)
{
    g_hash_table_remove_all(names->ids);
    g_hash_table_remove_all(names->classes);
    g_hash_table_remove_all(names->tails);
    g_hash_table_remove_all(names->executables);
    /* Prefer visible launchers over URL handlers sharing the same WM_CLASS. */
    for (guint hidden = 0; hidden < 2; hidden++) {
        for (GList *it = apps; it; it = it->next) {
            GAppInfo *app = it->data;
            gboolean want_visible = hidden == 0;
            if (!G_IS_DESKTOP_APP_INFO(app) || g_app_info_should_show(app) != want_visible) continue;
            const char *name = g_app_info_get_display_name(app);
            const char *id = g_app_info_get_id(app);
            insert(names->ids, id, name);
            insert(names->classes, g_desktop_app_info_get_startup_wm_class(G_DESKTOP_APP_INFO(app)), name);
            char *normalized = normalize(id);
            const char *tail = strrchr(normalized, '.');
            if (tail) insert(names->tails, tail + 1, name);
            g_free(normalized);
            const char *exe = g_app_info_get_executable(app);
            char *base = exe ? g_path_get_basename(exe) : NULL;
            insert(names->executables, base, name);
            g_free(base);
        }
    }
    names->dirty = FALSE;
}
static const char *lookup(GHashTable *table, const char *key)
{
    if (!key || !*key) return NULL;
    char *normalized = normalize(key);
    const char *name = g_hash_table_lookup(table, normalized);
    g_free(normalized);
    return name;
}
const char *panel_app_names_find(PanelAppNames *names, const char *application_id,
                                 const char *instance, const char *wmclass)
{
    if (names->dirty) {
        GList *apps = g_app_info_get_all();
        panel_app_names_load(names, apps);
        g_list_free_full(apps, g_object_unref);
    }
    const char *name = lookup(names->ids, application_id);
    GHashTable *tables[] = {names->classes, names->ids, names->tails, names->executables};
    for (guint i = 0; !name && i < G_N_ELEMENTS(tables); i++) {
        name = lookup(tables[i], wmclass);
        if (!name) name = lookup(tables[i], instance);
    }
    return name;
}
char *panel_app_title(const char *program, const char *title)
{
    if (!program || !*program) return g_strdup(title && *title ? title : "Application");
    if (!title || !*title || g_str_equal(program, title)) return g_strdup(program);
    char *document = g_strdup(title);
    const char *separators[] = {" - ", " — ", " – ", " | "};
    for (guint i = 0; i < G_N_ELEMENTS(separators); i++) {
        char *suffix = g_strconcat(separators[i], program, NULL);
        char *prefix = g_strconcat(program, separators[i], NULL);
        if (g_str_has_suffix(document, suffix))
            document[strlen(document) - strlen(suffix)] = '\0';
        else if (g_str_has_prefix(document, prefix))
            memmove(document, document + strlen(prefix), strlen(document + strlen(prefix)) + 1);
        g_free(suffix);
        g_free(prefix);
    }
    char *label = *document ? g_strdup_printf("%s - %s", program, document) : g_strdup(program);
    g_free(document);
    return label;
}

char *panel_app_title_markup(const char *program, const char *title)
{
    char *label = panel_app_title(program, title);
    char *markup;
    if (program && *program && g_str_has_prefix(label, program))
        markup = g_markup_printf_escaped("<b>%s</b>%s", program, label + strlen(program));
    else markup = g_markup_printf_escaped("%s", label);
    g_free(label);
    return markup;
}
