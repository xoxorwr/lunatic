/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu-model.h"

/* Preserve models and unchanged links: opening a lazy submenu must not replace
 * its parent's model or close the popup while its children arrive. */
static gboolean item_equal(GMenuModel *a, int ai, GMenuModel *b, int bi)
{
    int attributes = 0, other_attributes = 0, links = 0, other_links = 0;
    GMenuAttributeIter *iter = g_menu_model_iterate_item_attributes(a, ai);
    const char *name;
    GVariant *value;
    gboolean equal = TRUE;
    while (g_menu_attribute_iter_get_next(iter, &name, &value)) {
        GVariant *other = g_menu_model_get_item_attribute_value(b, bi, name, NULL);
        equal &= other && g_variant_equal(value, other);
        g_clear_pointer(&other, g_variant_unref);
        g_variant_unref(value);
        attributes++;
    }
    g_object_unref(iter);
    iter = g_menu_model_iterate_item_attributes(b, bi);
    while (g_menu_attribute_iter_get_next(iter, &name, &value)) {
        other_attributes++;
        g_variant_unref(value);
    }
    g_object_unref(iter);
    GMenuLinkIter *link_iter = g_menu_model_iterate_item_links(a, ai);
    GMenuModel *link;
    while (g_menu_link_iter_get_next(link_iter, &name, &link)) {
        GMenuModel *other = g_menu_model_get_item_link(b, bi, name);
        equal &= link == other;
        g_clear_object(&other);
        g_object_unref(link);
        links++;
    }
    g_object_unref(link_iter);
    link_iter = g_menu_model_iterate_item_links(b, bi);
    while (g_menu_link_iter_get_next(link_iter, &name, &link)) {
        other_links++;
        g_object_unref(link);
    }
    g_object_unref(link_iter);
    return equal && attributes == other_attributes && links == other_links;
}

static void apply(GMenu *target, GMenuModel *desired)
{
    int count = g_menu_model_get_n_items(desired);
    for (int i = 0; i < count; i++) {
        int current = g_menu_model_get_n_items(G_MENU_MODEL(target));
        if (i < current && item_equal(G_MENU_MODEL(target), i, desired, i)) continue;
        GMenuItem *item = g_menu_item_new_from_model(desired, i);
        if (i < current) g_menu_remove(target, i);
        g_menu_insert_item(target, i, item);
        g_object_unref(item);
    }
    while (g_menu_model_get_n_items(G_MENU_MODEL(target)) > count)
        g_menu_remove(target, count);
}

PanelMenuModel *panel_menu_model_new(void)
{
    PanelMenuModel *model = g_new0(PanelMenuModel, 1);
    model->model = g_menu_new();
    model->sections = g_ptr_array_new_with_free_func(g_object_unref);
    return model;
}

void panel_menu_model_free(PanelMenuModel *model)
{
    g_object_unref(model->model);
    g_ptr_array_unref(model->sections);
    g_free(model);
}

void panel_menu_builder_init(PanelMenuBuilder *builder, PanelMenuModel *target)
{
    builder->target = target;
    builder->sections = g_ptr_array_new_with_free_func(g_object_unref);
    g_ptr_array_add(builder->sections, g_menu_new());
}

void panel_menu_builder_append(PanelMenuBuilder *builder, GMenuItem *item)
{
    g_menu_append_item(g_ptr_array_index(builder->sections, builder->sections->len - 1), item);
}

void panel_menu_builder_separator(PanelMenuBuilder *builder)
{
    GMenu *last = g_ptr_array_index(builder->sections, builder->sections->len - 1);
    if (g_menu_model_get_n_items(G_MENU_MODEL(last))) g_ptr_array_add(builder->sections, g_menu_new());
}

void panel_menu_builder_finish(PanelMenuBuilder *builder)
{
    PanelMenuModel *target = builder->target;
    GMenu *root = g_menu_new();
    guint used = 0;
    for (guint i = 0; i < builder->sections->len; i++) {
        GMenu *section = g_ptr_array_index(builder->sections, i);
        if (!g_menu_model_get_n_items(G_MENU_MODEL(section))) continue;
        if (used == target->sections->len) g_ptr_array_add(target->sections, g_menu_new());
        GMenu *existing = g_ptr_array_index(target->sections, used++);
        apply(existing, G_MENU_MODEL(section));
        g_menu_append_section(root, NULL, G_MENU_MODEL(existing));
    }
    apply(target->model, G_MENU_MODEL(root));
    g_ptr_array_set_size(target->sections, used);
    g_object_unref(root);
    g_ptr_array_unref(builder->sections);
}
