/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 2013-2020 Peter Bloomfield
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Helpers for GtkApplicationWindow
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <gtk/gtk.h>
#include <string.h>
#include "application-helpers.h"

typedef struct {
    GAction  *action;
    GVariant *parameter;
} AccelInfo;

static void
accel_info_free(AccelInfo * info)
{
    if (info->parameter)
        g_variant_unref(info->parameter);
    g_free(info);
}

static gboolean
accel_activate(GtkAccelGroup * accel_group,
               GObject       * acceleratable,
               guint           keyval,
               GdkModifierType modifier,
               gpointer        user_data)
{
    AccelInfo *info = user_data;
    gboolean block_accels;

    block_accels = GPOINTER_TO_INT(g_object_get_data(acceleratable, "block-accels"));
    if (block_accels)
        return FALSE;

    g_action_activate(info->action, info->parameter);

    return TRUE;
}

static void
extract_accel_from_menu_item(GMenuModel    * model,
                             gint            item,
                             GActionMap    * action_map,
                             GtkAccelGroup * accel_group)
{
    GMenuAttributeIter *iter;
    const gchar *key;
    GVariant *value;
    const gchar *accel = NULL;
    const gchar *action = NULL;
    GVariant *target = NULL;

    iter = g_menu_model_iterate_item_attributes(model, item);
    while (g_menu_attribute_iter_get_next(iter, &key, &value)) {
        if (g_str_equal(key, "action")
            && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            action = g_variant_get_string(value, NULL);
        else if (g_str_equal(key, "accel")
                 && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            accel = g_variant_get_string(value, NULL);
        else if (g_str_equal(key, "target"))
            target = g_variant_ref(value);
        g_variant_unref(value);
    }
    g_object_unref(iter);

    if (accel && action) {
        guint accel_key;
        GdkModifierType accel_mods;
        AccelInfo *info;
        const gchar *basename;
        GClosure *closure;

        gtk_accelerator_parse(accel, &accel_key, &accel_mods);
        basename = strchr(action, '.');
        basename = basename ? basename + 1 : action;
        info = g_new(AccelInfo, 1);
        info->action = g_action_map_lookup_action(action_map, basename);
        info->parameter = target ? g_variant_ref(target) : NULL;
        closure = g_cclosure_new(G_CALLBACK(accel_activate), info,
                                 (GClosureNotify) accel_info_free);
        gtk_accel_group_connect(accel_group, accel_key, accel_mods, 0,
                                closure);
    }

    if (target)
        g_variant_unref(target);
}

static void
extract_accels_from_menu(GMenuModel    * model,
                         GActionMap    * action_map,
                         GtkAccelGroup * accel_group)
{
    gint i, n = g_menu_model_get_n_items(model);
    GMenuLinkIter *iter;
    const gchar *key;
    GMenuModel *m;

    for (i = 0; i < n; i++) {
        extract_accel_from_menu_item(model, i, action_map, accel_group);

        iter = g_menu_model_iterate_item_links(model, i);
        while (g_menu_link_iter_get_next(iter, &key, &m)) {
            extract_accels_from_menu(m, action_map, accel_group);
            g_object_unref(m);
        }
        g_object_unref(iter);
    }
}

static GtkAccelGroup *
get_accel_group(GMenuModel * model,
                GActionMap * action_map)
{
    GtkAccelGroup *accel_group;

    accel_group = gtk_accel_group_new();
    extract_accels_from_menu(model, action_map, accel_group);

    return accel_group;
}

/*
 * libbalsa_window_get_menu_bar
 *
 * Construct a menu-bar for a GtkApplicationWindow that does not use the
 * GApplication's menubar
 *
 * window        the GtkApplicationWindow
 * entries       array of GActionEntry structures
 * n_entries     length of the array
 * resource_path resource path for GtkBuilder input defining a menu named
 *               "menubar"
 * error         GError for returning error information
 * cb_data       user data for GAction callbacks
 *
 * returns:      the GtkMenuBar
 */

GtkWidget *
libbalsa_window_get_menu_bar(GtkApplicationWindow * window,
                             const GActionEntry   * entries,
                             gint                   n_entries,
                             const gchar          * resource_path,
                             GError              ** error,
                             gpointer               cb_data)
{
    GActionMap *map = G_ACTION_MAP(window);
    GtkBuilder *builder;
    GtkWidget *menu_bar = NULL;

    g_action_map_add_action_entries(map, entries, n_entries, cb_data);

    builder = gtk_builder_new();
    if (gtk_builder_add_from_resource(builder, resource_path, error)) {
        GMenuModel *menu_model;

        menu_model =
            G_MENU_MODEL(gtk_builder_get_object(builder, "menubar"));

        menu_bar = gtk_menu_bar_new_from_model(menu_model);

        libbalsa_window_set_accels(window, menu_model);
        gtk_application_window_set_show_menubar(window, FALSE);
    }
    g_object_unref(builder);

    return menu_bar;
}

/*
 * libbalsa_window_set_accels
 *
 * Get the accelerators from a GMenuModel and add them to a
 * GtkApplicationWindow
 *
 * window       the GtkApplicationWindow
 * menumodel    the GMenuModel
 */

void
libbalsa_window_set_accels(GtkApplicationWindow * window,
                           GMenuModel           * menu_model)
{
    GSList *accel_groups;
    GtkAccelGroup *accel_group;

    /* Remove current accelerators: */
    accel_groups = gtk_accel_groups_from_object(G_OBJECT(window));
    if (accel_groups)
        /* Last is first... */
        gtk_window_remove_accel_group(GTK_WINDOW(window),
                                      accel_groups->data);

    accel_group = get_accel_group(menu_model, G_ACTION_MAP(window));
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
    g_object_unref(accel_group);
}

void
libbalsa_window_block_accels(GtkApplicationWindow * window,
                             gboolean               block)
{
    g_object_set_data(G_OBJECT(window), "block-accels", GINT_TO_POINTER(!!block));
}

/*
 * libbalsa_window_add_accelerator
 *
 * Add an accelerator key combination for an action
 *
 * window       the GtkApplicationWindow
 * accel        the accelerator string
 * action_name  name of the GAction
 */

void
libbalsa_window_add_accelerator(GtkApplicationWindow * window,
                                const gchar          * accel,
                                const gchar          * action_name)
{
    GActionMap *action_map = G_ACTION_MAP(window);
    guint accel_key;
    GdkModifierType accel_mods;
    const gchar *basename;
    GAction *action;
    AccelInfo *info;
    GClosure *closure;
    GtkAccelGroup *accel_group;

    gtk_accelerator_parse(accel, &accel_key, &accel_mods);
    if (!accel_key) {
        g_warning("%s: could not parse accelerator “%s”", __func__,
                accel);
        return;
    }

    basename = strchr(action_name, '.');
    basename = basename ? basename + 1 : action_name;
    action = g_action_map_lookup_action(action_map, basename);
    if (!action) {
    	g_warning("%s: could not lookup action “%s”", __func__,
                action_name);
        return;
    }

    info = g_new(AccelInfo, 1);
    info->action = action;
    info->parameter = NULL;
    closure = g_cclosure_new(G_CALLBACK(accel_activate), info,
                             (GClosureNotify) accel_info_free);

    accel_group = gtk_accel_group_new();
    gtk_accel_group_connect(accel_group, accel_key, accel_mods, 0,
                            closure);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
}
