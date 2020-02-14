/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
#	include "config.h"
#endif                          /* HAVE_CONFIG_H */


#include <stdlib.h>
#include "libbalsa-conf.h"
#include "geometry-manager.h"


static GHashTable *geometry_hash = NULL;
G_LOCK_DEFINE_STATIC(geometry_hash);


static void geometry_manager_destroy(void);
static void geometry_manager_save_item(const gchar            *key,
							  	  	   const geometry_t       *size_item,
									   gpointer G_GNUC_UNUSED  user_data);
static void size_allocate_cb(GtkWindow                  *window,
                 	 	 	 GdkRectangle G_GNUC_UNUSED *allocation,
							 geometry_t                 *size_item);
static void notify_is_maximized_cb(GtkWindow                *window,
                       	   	   	   GParamSpec G_GNUC_UNUSED *pspec,
								   geometry_t               *size_item);


void
geometry_manager_init(const gchar *key, gint width, gint height, gboolean maximized)
{
	gchar *config_key;

	g_return_if_fail((key != NULL) && (key[0] != '\0') && (width > 0) && (height > 0));

	G_LOCK(geometry_hash);

	if (geometry_hash == NULL) {
		geometry_hash = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify) g_free, (GDestroyNotify) g_free);
		atexit(geometry_manager_destroy);
	}

	if (!g_hash_table_contains(geometry_hash, key)) {
		geometry_t *size_item;

		size_item = g_new0(geometry_t, 1);
		g_hash_table_insert(geometry_hash, g_strdup(key), size_item);

		config_key = g_strdup_printf("%sWidth=%d", key, width);
		size_item->width = libbalsa_conf_get_int(config_key);
		g_free(config_key);

		config_key = g_strdup_printf("%sHeight=%d", key, height);
		size_item->height = libbalsa_conf_get_int(config_key);
		g_free(config_key);

		config_key = g_strdup_printf("%sMaximized=%s", key, maximized ? "true" : "false");
		size_item->maximized = libbalsa_conf_get_bool(config_key);
		g_free(config_key);
	}

	G_UNLOCK(geometry_hash);
}


const geometry_t *
geometry_manager_get(const gchar *key)
{
	return (const geometry_t *) g_hash_table_lookup(geometry_hash, key);
}


void
geometry_manager_attach(GtkWindow *window, const gchar *key)
{
	geometry_t *size_item;

	G_LOCK(geometry_hash);
	size_item = g_hash_table_lookup(geometry_hash, key);
	if (size_item != NULL) {
		gtk_window_set_resizable(window, TRUE);
		gtk_window_set_default_size(window, size_item->width, size_item->height);
		if (size_item->maximized) {
			gtk_window_maximize(window);
		}
		g_signal_connect(window, "size_allocate", G_CALLBACK(size_allocate_cb), size_item);
	    g_signal_connect(window, "notify::is-maximized", G_CALLBACK(notify_is_maximized_cb), size_item);
	}
	G_UNLOCK(geometry_hash);
}


void
geometry_manager_save(void)
{
	G_LOCK(geometry_hash);
	g_assert(geometry_hash != NULL);
	g_hash_table_foreach(geometry_hash, (GHFunc) geometry_manager_save_item, NULL);
	G_UNLOCK(geometry_hash);
}


static void
geometry_manager_destroy(void)
{
	G_LOCK(geometry_hash);
	g_hash_table_unref(geometry_hash);
	geometry_hash = NULL;
	G_UNLOCK(geometry_hash);
}


static void
geometry_manager_save_item(const gchar            *key,
						   const geometry_t       *size_item,
						   gpointer G_GNUC_UNUSED  user_data)
{
	gchar *config_key;

	config_key = g_strdup_printf("%sWidth", key);
	libbalsa_conf_set_int(config_key, size_item->width);
	g_free(config_key);

	config_key = g_strdup_printf("%sHeight", key);
	libbalsa_conf_set_int(config_key, size_item->height);
	g_free(config_key);

	config_key = g_strdup_printf("%sMaximized", key);
	libbalsa_conf_set_bool(config_key, size_item->maximized);
	g_free(config_key);
}


static void
size_allocate_cb(GtkWindow                  *window,
                 GdkRectangle G_GNUC_UNUSED *allocation,
	         geometry_t                 *size_item)
{
	G_LOCK(geometry_hash);
        if (!size_item->maximized) {
                gtk_window_get_size(window, &size_item->width, &size_item->height);
        }
	G_UNLOCK(geometry_hash);
}


static void
notify_is_maximized_cb(GtkWindow                *window,
                       GParamSpec G_GNUC_UNUSED *pspec,
					   geometry_t               *size_item)
{
	G_LOCK(geometry_hash);
	size_item->maximized = gtk_window_is_maximized(window);
	G_UNLOCK(geometry_hash);
}
