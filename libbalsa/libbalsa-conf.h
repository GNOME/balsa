/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __LIBCONFIG_H__
#define __LIBCONFIG_H__

#include "config.h"

#include <glib.h>

void libbalsa_conf_push_group(const char *group);
void libbalsa_conf_remove_group_(const char *group, gboolean priv);
#define libbalsa_conf_remove_group(group) \
        (libbalsa_conf_remove_group_((group),FALSE))
#define libbalsa_conf_private_remove_group(group) \
        (libbalsa_conf_remove_group_((group),TRUE))

gboolean libbalsa_conf_has_group(const char *group);

/* Wrapper for iterating over groups. */
typedef gboolean (*LibBalsaConfForeachFunc)(const gchar * key,
                                            const gchar * value,
                                            gpointer data);
void libbalsa_conf_foreach_group(const gchar * prefix,
                                 LibBalsaConfForeachFunc func,
                                 gpointer data);

#if !defined(HAVE_GNOME) || defined(GNOME_DISABLE_DEPRECATED)
# define BALSA_USE_G_KEY_FILE TRUE
#else
# undef BALSA_USE_G_KEY_FILE
#endif

#if !BALSA_USE_G_KEY_FILE

#define libbalsa_conf_pop_group               gnome_config_pop_prefix
#define libbalsa_conf_set_string              gnome_config_set_string
#define libbalsa_conf_get_string              gnome_config_get_string
#define libbalsa_conf_private_set_string      gnome_config_private_set_string
#define libbalsa_conf_private_get_string      gnome_config_private_get_string
#define libbalsa_conf_get_string_with_default gnome_config_get_string_with_default
#define libbalsa_conf_set_bool                gnome_config_set_bool
#define libbalsa_conf_get_bool                gnome_config_get_bool
#define libbalsa_conf_get_bool_with_default   gnome_config_get_bool_with_default
#define libbalsa_conf_set_int                 gnome_config_set_int
#define libbalsa_conf_get_int                 gnome_config_get_int
#define libbalsa_conf_get_int_with_default    gnome_config_get_int_with_default

#define libbalsa_conf_set_vector              gnome_config_set_vector
#define libbalsa_conf_get_vector_with_default gnome_config_get_vector_with_default

#define libbalsa_conf_push_prefix             gnome_config_push_prefix
#define libbalsa_conf_pop_prefix              gnome_config_pop_prefix
#define libbalsa_conf_clean_key               gnome_config_clean_key
#define libbalsa_conf_drop_all                gnome_config_drop_all
#define libbalsa_conf_sync                    gnome_config_sync
#include <libgnome/gnome-config.h>

#else                           /* BALSA_USE_G_KEY_FILE */

void libbalsa_conf_pop_group                 (void);
void libbalsa_conf_clean_key                 (const char *key);

void libbalsa_conf_set_bool_                 (const char *path,
	                                      gboolean value,
                                              gboolean priv);
#define libbalsa_conf_set_bool(path,new_value) \
        (libbalsa_conf_set_bool_((path),(new_value),FALSE))

gboolean libbalsa_conf_get_bool_with_default_(const char *path,
                                              gboolean * def,
                                              gboolean priv);
#define libbalsa_conf_get_bool_with_default(path,def) \
        (libbalsa_conf_get_bool_with_default_((path),(def),FALSE))
#define libbalsa_conf_get_bool(path) \
        (libbalsa_conf_get_bool_with_default_ ((path), NULL, FALSE))

gint libbalsa_conf_get_int_with_default_     (const char *path,
	                                      gboolean * def,
                                              gboolean priv);
#define libbalsa_conf_get_int(path) \
        (libbalsa_conf_get_int_with_default_ ((path), NULL, FALSE))
#define libbalsa_conf_get_int_with_default(path, def) \
        (libbalsa_conf_get_int_with_default_ ((path), (def), FALSE))

void libbalsa_conf_set_int_                  (const char *path,
	                                      int value,
					      gboolean priv);
#define libbalsa_conf_set_int(path,new_value) \
        (libbalsa_conf_set_int_((path),(new_value),FALSE))

void libbalsa_conf_set_string_               (const char *path,
	                                      const char *value,
                                              gboolean priv);
#define libbalsa_conf_set_string(path,new_value) \
        (libbalsa_conf_set_string_((path),(new_value),FALSE))
#define libbalsa_conf_private_set_string(path,new_value) \
        (libbalsa_conf_set_string_((path),(new_value),TRUE))

char *libbalsa_conf_get_string_with_default_ (const char *path,
                                              gboolean * def,
                                              gboolean priv);
#define libbalsa_conf_get_string(path) \
        (libbalsa_conf_get_string_with_default_((path),NULL, FALSE))
#define libbalsa_conf_get_string_with_default(path, def) \
        (libbalsa_conf_get_string_with_default_((path),(def), FALSE))
#define libbalsa_conf_private_get_string(path) \
        (libbalsa_conf_get_string_with_default_((path),NULL, TRUE))

void libbalsa_conf_set_vector                (const char *path,
	                                      int argc,
                                              const char *const argv[]);

void libbalsa_conf_get_vector_with_default_  (const char *path,
	                                      gint * argcp,
                                              char ***argvp,
                                              gboolean * def,
					      gboolean priv);
#define libbalsa_conf_get_vector_with_default(path, argcp, argvp, def) \
        (libbalsa_conf_get_vector_with_default_((path),(argcp),(argvp), \
                                                (def),FALSE))

void libbalsa_conf_drop_all                  (void);
void libbalsa_conf_sync                      (void);

#endif                          /* BALSA_USE_G_KEY_FILE */

#endif                          /* __LIBCONFIG_H__ */
