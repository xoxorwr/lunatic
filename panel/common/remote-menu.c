/* SPDX-License-Identifier: GPL-3.0-only */
#include "remote-menu.h"
#include "menu-model.h"
#include <string.h>

typedef struct { GWeakRef owner; gint32 id; gboolean submenu; } Binding;
struct _PanelRemoteMenu {
    GObject parent;
    GSimpleActionGroup *actions;
    GHashTable *models, *seen, *seen_actions;
    PanelRemoteEvent event;
    gpointer event_data;
};
G_DEFINE_TYPE(PanelRemoteMenu, panel_remote_menu, G_TYPE_OBJECT)

static void panel_remote_menu_finalize(GObject *object)
{
    PanelRemoteMenu *self = PANEL_REMOTE_MENU(object);
    g_object_unref(self->actions);
    g_hash_table_unref(self->models);
    g_hash_table_unref(self->seen);
    g_hash_table_unref(self->seen_actions);
    G_OBJECT_CLASS(panel_remote_menu_parent_class)->finalize(object);
}
static void panel_remote_menu_class_init(PanelRemoteMenuClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = panel_remote_menu_finalize;
}
static void panel_remote_menu_init(PanelRemoteMenu *self)
{
    self->actions = g_simple_action_group_new();
    self->models = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                        (GDestroyNotify)panel_menu_model_free);
    self->seen = g_hash_table_new(g_direct_hash, g_direct_equal);
    self->seen_actions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_hash_table_insert(self->models, GINT_TO_POINTER(0), panel_menu_model_new());
}
PanelRemoteMenu *panel_remote_menu_new(void) { return g_object_new(PANEL_TYPE_REMOTE_MENU, NULL); }
GMenuModel *panel_remote_menu_model(PanelRemoteMenu *self)
{
    return G_MENU_MODEL(((PanelMenuModel *)g_hash_table_lookup(self->models, GINT_TO_POINTER(0)))->model);
}
GActionGroup *panel_remote_menu_actions(PanelRemoteMenu *self) { return G_ACTION_GROUP(self->actions); }
void panel_remote_menu_set_event(PanelRemoteMenu *self, PanelRemoteEvent event, gpointer data)
{
    self->event = event;
    self->event_data = data;
}
static void binding_free(gpointer data, GClosure *closure)
{
    (void)closure;
    Binding *binding = data;
    g_weak_ref_clear(&binding->owner);
    g_free(binding);
}
static void dispatch(GSimpleAction *action, GVariant *value, gpointer data)
{
    Binding *binding = data;
    PanelRemoteMenu *self = g_weak_ref_get(&binding->owner);
    if (!self) return;
    if (self->event && g_action_map_lookup_action(G_ACTION_MAP(self->actions),
        g_action_get_name(G_ACTION(action))) == G_ACTION(action)) {
        if (binding->submenu) {
            gboolean open = g_variant_get_boolean(value);
            g_simple_action_set_state(action, value);
            self->event(binding->id, open ? "opened" : "closed", self->event_data);
        } else self->event(binding->id, "clicked", self->event_data);
    }
    g_object_unref(self);
}
static char *ensure_action(PanelRemoteMenu *self, gint32 id, gboolean submenu,
                           const char *toggle, gint32 state, gboolean enabled)
{
    char *name = g_strdup_printf("%s%u", submenu ? "open" : "item", (guint32)id);
    gboolean radio = toggle && g_str_equal(toggle, "radio");
    gboolean check = toggle && g_str_equal(toggle, "checkmark");
    const GVariantType *type = radio ? G_VARIANT_TYPE_STRING :
                                    (check || submenu ? G_VARIANT_TYPE_BOOLEAN : NULL);
    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(self->actions), name);
    if (action && ((type == NULL) != (g_action_get_state_type(action) == NULL) ||
        (type && !g_variant_type_equal(type, g_action_get_state_type(action))))) {
        g_action_map_remove_action(G_ACTION_MAP(self->actions), name);
        action = NULL;
    }
    if (!action) {
        GSimpleAction *created = type ? g_simple_action_new_stateful(name,
            radio ? G_VARIANT_TYPE_STRING : NULL,
            radio ? g_variant_new_string(state == 1 ? name : "") : g_variant_new_boolean(state == 1)) :
            g_simple_action_new(name, NULL);
        Binding *binding = g_new0(Binding, 1);
        g_weak_ref_init(&binding->owner, self);
        binding->id = id;
        binding->submenu = submenu;
        g_signal_connect_data(created, submenu ? "change-state" : "activate", G_CALLBACK(dispatch),
                              binding, binding_free, 0);
        g_action_map_add_action(G_ACTION_MAP(self->actions), G_ACTION(created));
        g_object_unref(created);
        action = g_action_map_lookup_action(G_ACTION_MAP(self->actions), name);
    }
    g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
    if (radio) g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string(state == 1 ? name : ""));
    else if (check) g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(state == 1));
    g_hash_table_add(self->seen_actions, g_strdup(name));
    return name;
}
/* Validate the entire tree before changing anything. Bound stack, work and IDs;
 * duplicate IDs would otherwise create model cycles or misroute actions. */
static gboolean validate(GVariant *node, guint depth, guint *count, GHashTable *ids)
{
    if (depth > 32 || ++*count > 8192 || !g_variant_is_of_type(node, G_VARIANT_TYPE("(ia{sv}av)"))) return FALSE;
    gint32 id;
    GVariant *props, *children;
    g_variant_get(node, "(i@a{sv}@av)", &id, &props, &children);
    gboolean valid = !g_hash_table_contains(ids, GINT_TO_POINTER(id));
    g_hash_table_add(ids, GINT_TO_POINTER(id));
    for (gsize i = 0; valid && i < g_variant_n_children(children); i++) {
        GVariant *box = g_variant_get_child_value(children, i);
        GVariant *child = g_variant_get_variant(box);
        valid = validate(child, depth + 1, count, ids);
        g_variant_unref(child);
        g_variant_unref(box);
    }
    g_variant_unref(props);
    g_variant_unref(children);
    return valid;
}
static void presentation(GMenuItem *item, GVariant *properties)
{
    const char *icon_name = NULL;
    g_variant_lookup(properties, "icon-name", "&s", &icon_name);
    GIcon *icon = NULL;
    if (icon_name && *icon_name) icon = g_themed_icon_new(icon_name);
    else {
        GVariant *data = g_variant_lookup_value(properties, "icon-data", G_VARIANT_TYPE("ay"));
        if (data) {
            gsize size;
            gconstpointer pixels = g_variant_get_fixed_array(data, &size, 1);
            if (size && size <= 1024 * 1024) {
                GBytes *bytes = g_bytes_new(pixels, size);
                icon = g_bytes_icon_new(bytes);
                g_bytes_unref(bytes);
            }
            g_variant_unref(data);
        }
    }
    if (icon) {
        g_menu_item_set_icon(item, icon);
        g_object_unref(icon);
    }
    GVariant *shortcuts = g_variant_lookup_value(properties, "shortcut", G_VARIANT_TYPE("aas"));
    if (!shortcuts) return;
    if (g_variant_n_children(shortcuts)) {
        GVariant *keys = g_variant_get_child_value(shortcuts, 0);
        gsize count;
        const char **parts = g_variant_get_strv(keys, &count);
        if (count && count <= 16) {
            GString *accel = g_string_new(NULL);
            for (gsize k = 0; k + 1 < count; k++)
                g_string_append_printf(accel, "<%s>", parts[k]);
            g_string_append(accel, parts[count - 1]);
            g_menu_item_set_attribute(item, "accel", "s", accel->str);
            g_string_free(accel, TRUE);
        }
        g_free(parts);
        g_variant_unref(keys);
    }
    g_variant_unref(shortcuts);
}

static PanelMenuModel *build(PanelRemoteMenu *self, GVariant *node)
{
    gint32 id;
    GVariant *props, *children;
    g_variant_get(node, "(i@a{sv}@av)", &id, &props, &children);
    PanelMenuModel *model = g_hash_table_lookup(self->models, GINT_TO_POINTER(id));
    if (!model) {
        model = panel_menu_model_new();
        g_hash_table_insert(self->models, GINT_TO_POINTER(id), model);
    }
    g_hash_table_add(self->seen, GINT_TO_POINTER(id));
    PanelMenuBuilder builder;
    panel_menu_builder_init(&builder, model);
    for (gsize i = 0; i < g_variant_n_children(children); i++) {
        GVariant *box = g_variant_get_child_value(children, i);
        GVariant *child = g_variant_get_variant(box);
        gint32 cid, state = 0;
        GVariant *cp, *cc;
        g_variant_get(child, "(i@a{sv}@av)", &cid, &cp, &cc);
        gboolean visible = TRUE, enabled = TRUE;
        const char *label = "", *type = "", *display = "", *toggle = "";
        g_variant_lookup(cp, "visible", "b", &visible);
        g_variant_lookup(cp, "enabled", "b", &enabled);
        g_variant_lookup(cp, "label", "&s", &label);
        g_variant_lookup(cp, "type", "&s", &type);
        g_variant_lookup(cp, "children-display", "&s", &display);
        g_variant_lookup(cp, "toggle-type", "&s", &toggle);
        g_variant_lookup(cp, "toggle-state", "i", &state);
        if (visible && g_str_equal(type, "separator")) panel_menu_builder_separator(&builder);
        else if (visible) {
            gboolean submenu = g_str_equal(display, "submenu") || g_variant_n_children(cc) > 0;
            char *name = ensure_action(self, cid, submenu, submenu ? "" : toggle, state, enabled);
            char *detailed = g_strconcat("remote.", name, NULL);
            GMenuItem *item = g_menu_item_new(label, NULL);
            if (submenu) {
                PanelMenuModel *nested = build(self, child);
                g_menu_item_set_submenu(item, G_MENU_MODEL(nested->model));
                g_menu_item_set_attribute(item, "submenu-action", "s", detailed);
            } else {
                g_menu_item_set_action_and_target_value(item, detailed,
                    g_str_equal(toggle, "radio") ? g_variant_new_string(name) : NULL);
                if (*toggle) g_menu_item_set_attribute(item, "hidden-when", "s", "action-missing");
            }
            presentation(item, cp);
            panel_menu_builder_append(&builder, item);
            g_object_unref(item);
            g_free(name);
            g_free(detailed);
        }
        g_variant_unref(cp);
        g_variant_unref(cc);
        g_variant_unref(child);
        g_variant_unref(box);
    }
    panel_menu_builder_finish(&builder);
    g_variant_unref(props);
    g_variant_unref(children);
    return model;
}
gboolean panel_remote_menu_update(PanelRemoteMenu *self, GVariant *layout)
{
    guint count = 0;
    g_hash_table_remove_all(self->seen);
    gboolean valid = validate(layout, 0, &count, self->seen);
    GVariant *id = valid ? g_variant_get_child_value(layout, 0) : NULL;
    valid = valid && g_variant_get_int32(id) == 0;
    g_clear_pointer(&id, g_variant_unref);
    g_hash_table_remove_all(self->seen);
    if (!valid) return FALSE;
    g_hash_table_remove_all(self->seen_actions);
    build(self, layout);
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, self->models);
    while (g_hash_table_iter_next(&iter, &key, NULL))
        if (!g_hash_table_contains(self->seen, key)) g_hash_table_iter_remove(&iter);
    char **actions = g_action_group_list_actions(G_ACTION_GROUP(self->actions));
    for (guint i = 0; actions[i]; i++)
        if (!g_hash_table_contains(self->seen_actions, actions[i]))
            g_action_map_remove_action(G_ACTION_MAP(self->actions), actions[i]);
    g_strfreev(actions);
    return TRUE;
}
