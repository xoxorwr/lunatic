/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-registry.h"

struct PanelMenuRegistry {
    GHashTable *entries;
};

static void entry_free(gpointer data)
{
    PanelMenuEntry *entry = data;
    g_free(entry->service);
    g_free(entry->path);
    g_free(entry);
}

PanelMenuRegistry *panel_menu_registry_new(void)
{
    PanelMenuRegistry *registry = g_new0(PanelMenuRegistry, 1);
    registry->entries = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, entry_free);
    return registry;
}

void panel_menu_registry_free(PanelMenuRegistry *registry)
{
    if (!registry)
        return;
    g_hash_table_unref(registry->entries);
    g_free(registry);
}

const PanelMenuEntry *panel_menu_registry_lookup(PanelMenuRegistry *registry, guint32 window)
{
    return g_hash_table_lookup(registry->entries, GUINT_TO_POINTER(window));
}

gboolean panel_menu_registry_register(PanelMenuRegistry *registry, guint32 window,
                                      const char *service, const char *path, GError **error)
{
    if (!window || !service || !g_dbus_is_unique_name(service) || !path ||
        !g_variant_is_object_path(path)) {
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                            "Invalid window, sender or menu path");
        return FALSE;
    }
    const PanelMenuEntry *existing = panel_menu_registry_lookup(registry, window);
    if (existing && !g_str_equal(existing->service, service)) {
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                            "Window is registered by another connection");
        return FALSE;
    }
    PanelMenuEntry *entry = g_new0(PanelMenuEntry, 1);
    *entry = (PanelMenuEntry){window, g_strdup(service), g_strdup(path)};
    g_hash_table_replace(registry->entries, GUINT_TO_POINTER(window), entry);
    return TRUE;
}

gboolean panel_menu_registry_unregister(PanelMenuRegistry *registry, guint32 window,
                                        const char *service, GError **error)
{
    const PanelMenuEntry *entry = panel_menu_registry_lookup(registry, window);
    if (entry && g_strcmp0(entry->service, service) != 0) {
        g_set_error_literal(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                            "Window belongs to another connection");
        return FALSE;
    }
    g_hash_table_remove(registry->entries, GUINT_TO_POINTER(window));
    return TRUE;
}

GArray *panel_menu_registry_remove_owner(PanelMenuRegistry *registry, const char *service)
{
    GArray *removed = g_array_new(FALSE, FALSE, sizeof(guint32));
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, registry->entries);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        PanelMenuEntry *entry = value;
        if (g_strcmp0(entry->service, service) == 0) {
            guint32 window = entry->window;
            g_array_append_val(removed, window);
            g_hash_table_iter_remove(&iter);
        }
    }
    return removed;
}

GVariant *panel_menu_registry_list(PanelMenuRegistry *registry)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(uso)"));
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, registry->entries);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        PanelMenuEntry *entry = value;
        g_variant_builder_add(&builder, "(uso)", entry->window, entry->service, entry->path);
    }
    return g_variant_builder_end(&builder);
}
