/* SPDX-License-Identifier: GPL-3.0-only */
#include "desktop.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

char *launcher_normalize(const char *value)
{
    char *result = g_ascii_strdown(value ? value : "", -1);
    if (g_str_has_suffix(result, ".desktop"))
        result[strlen(result) - 8] = '\0';
    return result;
}

static gboolean equal_normalized(const char *a, const char *b)
{
    if (!a || !*a || !b || !*b)
        return FALSE;
    char *left = launcher_normalize(a), *right = launcher_normalize(b);
    gboolean equal = g_str_equal(left, right);
    g_free(left);
    g_free(right);
    return equal;
}

int launcher_match_score(GDesktopAppInfo *app, const char *instance, const char *wmclass,
                         const char *application_id)
{
    const char *id = g_app_info_get_id(G_APP_INFO(app));
    const char *startup = g_desktop_app_info_get_startup_wm_class(app);
    if (equal_normalized(id, application_id))
        return 100;
    if (equal_normalized(startup, wmclass) || equal_normalized(startup, instance))
        return 90;
    if (equal_normalized(id, wmclass) || equal_normalized(id, instance))
        return 80;
    /* Match the final reverse-DNS component, e.g. org.xfce.Thunar. */
    char *normalized = launcher_normalize(id);
    const char *tail = strrchr(normalized, '.');
    gboolean match =
        tail && (equal_normalized(tail + 1, wmclass) || equal_normalized(tail + 1, instance));
    g_free(normalized);
    if (match)
        return 60;
    const char *exec = g_app_info_get_executable(G_APP_INFO(app));
    char *base = exec ? g_path_get_basename(exec) : NULL;
    match = equal_normalized(base, wmclass) || equal_normalized(base, instance);
    g_free(base);
    return match ? 40 : 0;
}

GDesktopAppInfo *launcher_find_app(GList *apps, const char *instance, const char *wmclass,
                                   const char *application_id)
{
    GDesktopAppInfo *best = NULL;
    int best_score = 0;
    for (GList *l = apps; l; l = l->next) {
        if (!G_IS_DESKTOP_APP_INFO(l->data))
            continue;
        int score = launcher_match_score(l->data, instance, wmclass, application_id);
        if (score > best_score) {
            best = l->data;
            best_score = score;
        }
    }
    return best;
}

typedef struct {
    GDesktopAppInfo *app;
    guint rank;
} IndexedApp;
struct LauncherAppIndex {
    GHashTable *ids, *classes, *tails, *executables;
    GPtrArray *entries;
};

static void index_insert(GHashTable *table, const char *key, IndexedApp *entry)
{
    if (!key || !*key)
        return;
    char *normalized = launcher_normalize(key);
    if (g_hash_table_contains(table, normalized))
        g_free(normalized);
    else
        g_hash_table_insert(table, normalized, entry);
}

LauncherAppIndex *launcher_app_index_new(GList *apps)
{
    LauncherAppIndex *index = g_new0(LauncherAppIndex, 1);
    index->ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    index->classes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    index->tails = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    index->executables = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    index->entries = g_ptr_array_new_with_free_func(g_free);
    for (GList *l = apps; l; l = l->next) {
        if (!G_IS_DESKTOP_APP_INFO(l->data))
            continue;
        GDesktopAppInfo *app = l->data;
        IndexedApp *entry = g_new(IndexedApp, 1);
        *entry = (IndexedApp){app, index->entries->len};
        g_ptr_array_add(index->entries, entry);
        const char *id = g_app_info_get_id(G_APP_INFO(app));
        index_insert(index->ids, id, entry);
        index_insert(index->classes, g_desktop_app_info_get_startup_wm_class(app), entry);
        char *normalized = launcher_normalize(id);
        char *tail = strrchr(normalized, '.');
        if (tail)
            index_insert(index->tails, tail + 1, entry);
        g_free(normalized);
        const char *exec = g_app_info_get_executable(G_APP_INFO(app));
        char *base = exec ? g_path_get_basename(exec) : NULL;
        index_insert(index->executables, base, entry);
        g_free(base);
    }
    return index;
}

void launcher_app_index_free(LauncherAppIndex *index)
{
    if (!index)
        return;
    g_hash_table_unref(index->ids);
    g_hash_table_unref(index->classes);
    g_hash_table_unref(index->tails);
    g_hash_table_unref(index->executables);
    g_ptr_array_unref(index->entries);
    g_free(index);
}

static IndexedApp *index_lookup_pair(GHashTable *table, const char *left, const char *right)
{
    IndexedApp *a = *left ? g_hash_table_lookup(table, left) : NULL;
    IndexedApp *b = *right ? g_hash_table_lookup(table, right) : NULL;
    return !a ? b : !b || a->rank < b->rank ? a : b;
}

GDesktopAppInfo *launcher_app_index_find(LauncherAppIndex *index, const char *instance,
                                         const char *wmclass, const char *application_id)
{
    char *id = launcher_normalize(application_id);
    char *name = launcher_normalize(instance), *class = launcher_normalize(wmclass);
    IndexedApp *entry = *id ? g_hash_table_lookup(index->ids, id) : NULL;
    if (!entry)
        entry = index_lookup_pair(index->classes, class, name);
    if (!entry)
        entry = index_lookup_pair(index->ids, class, name);
    if (!entry)
        entry = index_lookup_pair(index->tails, class, name);
    if (!entry)
        entry = index_lookup_pair(index->executables, class, name);
    g_free(id);
    g_free(name);
    g_free(class);
    return entry ? entry->app : NULL;
}

char **launcher_load_config(const char *path, LauncherSettings *settings, GError **error)
{
    GKeyFile *key = g_key_file_new();
    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, error)) {
        g_key_file_unref(key);
        return NULL;
    }
    launcher_settings_read(settings, key);
    char **pins = g_key_file_get_string_list(key, "Launcher", "Favorites", NULL, NULL);
    g_key_file_unref(key);
    return pins ? pins : g_new0(char *, 1);
}

gboolean launcher_save_config(const char *path, char **pins, const LauncherSettings *settings,
                              GError **error)
{
    GKeyFile *key = g_key_file_new();
    launcher_settings_write(settings, key);
    g_key_file_set_string_list(key, "Launcher", "Favorites", (const char *const *)pins,
                               g_strv_length(pins));
    char *parent = g_path_get_dirname(path);
    gboolean ok = g_mkdir_with_parents(parent, 0700) == 0;
    if (!ok)
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Cannot create %s: %s",
                    parent, g_strerror(errno));
    if (ok)
        ok = g_key_file_save_to_file(key, path, error);
    g_free(parent);
    g_key_file_unref(key);
    return ok;
}

char **launcher_load_pins(const char *path, int *icon_size, int *monitor, gboolean *autohide,
                          GError **error)
{
    LauncherSettings settings;
    launcher_settings_defaults(&settings);
    settings.icon_size = *icon_size;
    settings.monitor = *monitor;
    settings.autohide = *autohide;
    char **pins = launcher_load_config(path, &settings, error);
    *icon_size = settings.icon_size;
    *monitor = settings.monitor;
    *autohide = settings.autohide;
    return pins;
}

gboolean launcher_save_pins(const char *path, char **pins, int icon_size, int monitor,
                            gboolean autohide, GError **error)
{
    LauncherSettings settings;
    launcher_settings_defaults(&settings);
    settings.icon_size = icon_size;
    settings.monitor = monitor;
    settings.autohide = autohide;
    return launcher_save_config(path, pins, &settings, error);
}
