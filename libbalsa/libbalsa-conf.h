/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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

#ifndef __LIBCONFIG_H__
#define __LIBCONFIG_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <gtk/gtk.h>

void libbalsa_conf_push_group(const char *group);
void libbalsa_conf_remove_group_(const char *group, gboolean priv);
#define libbalsa_conf_remove_group(group) \
        (libbalsa_conf_remove_group_((group),FALSE))
#define libbalsa_conf_private_remove_group(group) \
        (libbalsa_conf_remove_group_((group),TRUE))

gboolean libbalsa_conf_has_group(const char *group);
gboolean libbalsa_conf_has_key(const gchar *key);

/* Wrapper for iterating over groups. */
typedef gboolean (*LibBalsaConfForeachFunc)(const gchar * key,
                                            const gchar * value,
                                            gpointer data);
void libbalsa_conf_foreach_group(const gchar * prefix,
                                 LibBalsaConfForeachFunc func,
                                 gpointer data);

void libbalsa_conf_foreach_keys              (const gchar * group,
					      LibBalsaConfForeachFunc func,
					      gpointer data);

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

gdouble libbalsa_conf_get_double_with_default_ (const char *path,
	                                      gboolean * def,
                                              gboolean priv);
#define libbalsa_conf_get_double(path) \
        (libbalsa_conf_get_double_with_default_ ((path), NULL, FALSE))
#define libbalsa_conf_get_double_with_default(path, def) \
        (libbalsa_conf_get_double_with_default_ ((path), (def), FALSE))

void libbalsa_conf_set_int_                  (const char *path,
	                                      int value,
					      gboolean priv);
#define libbalsa_conf_set_int(path,new_value) \
        (libbalsa_conf_set_int_((path),(new_value),FALSE))

void libbalsa_conf_set_double_               (const char *path,
	                                      double value,
					      gboolean priv);
#define libbalsa_conf_set_double(path,new_value) \
        (libbalsa_conf_set_double_((path),(new_value),FALSE))

void libbalsa_conf_set_string_               (const char *path,
	                                      const char *value,
                                              gboolean priv);
#define libbalsa_conf_set_string(path,new_value) \
        (libbalsa_conf_set_string_((path),(new_value),FALSE))
void libbalsa_conf_private_set_string(const gchar *path,
    								  const gchar *value,
									  gboolean     obfuscated);

char *libbalsa_conf_get_string_with_default_ (const char *path,
                                              gboolean * def,
                                              gboolean priv);
#define libbalsa_conf_get_string(path) \
        (libbalsa_conf_get_string_with_default_((path),NULL, FALSE))
#define libbalsa_conf_get_string_with_default(path, def) \
        (libbalsa_conf_get_string_with_default_((path),(def), FALSE))
gchar *libbalsa_conf_private_get_string(const gchar *path,
										gboolean	 obfuscated)
	G_GNUC_WARN_UNUSED_RESULT;

void libbalsa_conf_set_vector                (const char *path,
	                                      int argc,
                                              const char *const argv[]);

void libbalsa_conf_get_vector_with_default   (const char *path,
                                              gint * argcp,
                                              char ***argvp,
                                              gboolean * def);

void libbalsa_conf_drop_all                  (void);
void libbalsa_conf_sync                      (void);
void libbalsa_conf_queue_sync                (void);

#if defined(HAVE_LIBSECRET)
gboolean libbalsa_conf_use_libsecret(void);
#endif

#endif                          /* __LIBCONFIG_H__ */
