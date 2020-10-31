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

static void
extract_accel_from_menu_item(GMenuModel            *model,
                             int                    item,
                             GtkShortcutController *controller)
{
    GMenuAttributeIter *iter;
    const char *key;
    GVariant *value;
    const char *accel = NULL;
    const char *action_name = NULL;
    GVariant *target = NULL;

    iter = g_menu_model_iterate_item_attributes(model, item);
    while (g_menu_attribute_iter_get_next(iter, &key, &value)) {
        if (g_str_equal(key, "action") &&
            g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            action_name = g_variant_get_string(value, NULL);
        } else if (g_str_equal(key, "accel") &&
                   g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            accel = g_variant_get_string(value, NULL);
        } else if (g_str_equal(key, "target")) {
            target = g_variant_ref(value);
        }
        g_variant_unref(value);
    }
    g_object_unref(iter);

    if (accel != NULL && action_name != NULL) {
        const char *basename;
        GtkShortcutTrigger *trigger;
        GtkShortcutAction *action;
        GtkShortcut *shortcut;

        trigger = gtk_shortcut_trigger_parse_string(accel);

        basename = strchr(action_name, '.');
        basename = basename != NULL ? basename + 1 : action_name;
        action = gtk_named_action_new(basename);

        shortcut = gtk_shortcut_new(trigger, action);
        gtk_shortcut_set_arguments(shortcut, target);

        gtk_shortcut_controller_add_shortcut(controller, shortcut);
    } else if (target != NULL) {
        g_variant_unref(target);
    }
}

static void
extract_accels_from_menu(GMenuModel            *model,
                         GtkShortcutController *controller)
{
    int n = g_menu_model_get_n_items(model);
    int i;

    for (i = 0; i < n; i++) {
        GMenuLinkIter *iter;
        GMenuModel *linked_model;

        extract_accel_from_menu_item(model, i, controller);

        iter = g_menu_model_iterate_item_links(model, i);
        while (g_menu_link_iter_get_next(iter, NULL, &linked_model)) {
            extract_accels_from_menu(linked_model, controller);
            g_object_unref(linked_model);
        }
        g_object_unref(iter);
    }
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
                             int                    n_entries,
                             const char           * resource_path,
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

        menu_bar = gtk_popover_menu_bar_new_from_model(menu_model);

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
    GtkEventController *controller;

    controller = gtk_shortcut_controller_new();
    extract_accels_from_menu(menu_model, GTK_SHORTCUT_CONTROLLER(controller));
    gtk_widget_add_controller(GTK_WIDGET(window), controller);
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
                                const char           * accel,
                                const char           * action_name)
{
    const char *basename;
    GtkShortcutTrigger *trigger;
    GtkShortcutAction *action;
    GtkShortcut *shortcut;
    GtkEventController *controller;

    trigger = gtk_shortcut_trigger_parse_string(accel);

    basename = strchr(action_name, '.');
    basename = basename != NULL ? basename + 1 : action_name;
    action = gtk_named_action_new(basename);

    shortcut = gtk_shortcut_new(trigger, action);

    controller = gtk_shortcut_controller_new();
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller), shortcut);
    gtk_widget_add_controller(GTK_WIDGET(window), controller);
}
