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

#ifdef HAVE_GNOME
#define libbalsa_conf_set_string  gnome_config_set_string
#define libbalsa_conf_get_string  gnome_config_get_string
#define libbalsa_conf_private_set_string  gnome_config_private_set_string
#define libbalsa_conf_private_get_string  gnome_config_private_get_string
#define libbalsa_conf_get_string_with_default gnome_config_get_string_with_default
#define libbalsa_conf_set_bool    gnome_config_set_bool
#define libbalsa_conf_get_bool    gnome_config_get_bool
#define libbalsa_conf_get_bool_with_default gnome_config_get_bool_with_default
#define libbalsa_conf_set_int     gnome_config_set_int
#define libbalsa_conf_get_int     gnome_config_get_int
#define libbalsa_conf_get_int_with_default gnome_config_get_int_with_default

#define libbalsa_conf_set_vector  gnome_config_set_vector
#define libbalsa_conf_get_vector_with_default gnome_config_get_vector_with_default 

#define libbalsa_conf_push_prefix gnome_config_push_prefix
#define libbalsa_conf_pop_prefix  gnome_config_pop_prefix
#define libbalsa_conf_private_clean_section gnome_config_private_clean_section
#define libbalsa_conf_clean_section gnome_config_clean_section
#define libbalsa_conf_init_iterator_sections gnome_config_init_iterator_sections
#define libbalsa_conf_iterator_next gnome_config_iterator_next
#include <libgnome/gnome-config.h>

#else
void libbalsa_conf_push_prefix (const char *path);
void libbalsa_conf_pop_prefix (void);

void libbalsa_conf_clean_section_  (const char *path, gboolean priv);
#define libbalsa_conf_clean_section(path) \
        (libbalsa_conf_clean_section_((path),FALSE))
#define libbalsa_conf_private_clean_section(path) \
        (libbalsa_conf_clean_section_((path),TRUE))

void *libbalsa_conf_init_iterator_sections(const char *path);
void *libbalsa_conf_iterator_next(void *iterator_handle,
				    char **key, char **value);

void libbalsa_conf_set_bool_       (const char *path, gboolean value,
                                   gboolean priv);
gboolean libbalsa_conf_get_bool_with_default_   (const char *path,
                                                gboolean *def,
                                                gboolean priv);
#define libbalsa_conf_set_bool(path,new_value) \
        (libbalsa_conf_set_bool_((path),(new_value),FALSE))

#define libbalsa_conf_get_bool_with_default(path,def) \
        (libbalsa_conf_get_bool_with_default_((path),(def),FALSE))
#define libbalsa_conf_get_bool(path) \
        (libbalsa_conf_get_bool_with_default_ ((path), NULL, FALSE))

gint  libbalsa_conf_get_int_with_default_  (const char *path,
                                              gboolean *def,
                                              gboolean priv);
#define libbalsa_conf_set_int(path,new_value) \
        (libbalsa_conf_set_int_((path),(new_value),FALSE))
void libbalsa_conf_set_int_        (const char *path, int value,
				   gboolean priv);

#define libbalsa_conf_get_int(path) \
        (libbalsa_conf_get_int_with_default_ ((path), NULL, FALSE))
#define libbalsa_conf_get_int_with_default(path, def) \
        (libbalsa_conf_get_int_with_default_ ((path), (def), FALSE))

void libbalsa_conf_set_string_  (const char *path, const char *value,
                                   gboolean priv);
char* libbalsa_conf_get_string_with_default_(const char *path,
					       gboolean   *def,
					       gboolean priv);

#define libbalsa_conf_set_string(path,new_value) \
        (libbalsa_conf_set_string_((path),(new_value),FALSE))
#define libbalsa_conf_private_set_string(path,new_value) \
        (libbalsa_conf_set_string_((path),(new_value),TRUE))
#define libbalsa_conf_get_string(path) \
        (libbalsa_conf_get_string_with_default_((path),NULL, FALSE))
#define libbalsa_conf_get_string_with_default(path, def) \
        (libbalsa_conf_get_string_with_default_((path),(def), FALSE))
#define libbalsa_conf_private_get_string(path) \
        (libbalsa_conf_get_string_with_default_((path),NULL, TRUE))

void libbalsa_conf_set_vector(const char *path, int argc,
				const char * const argv[]);

void libbalsa_conf_get_vector_with_default_(const char *path, gint *argcp,
                                                char ***argvp,
                                                gboolean *def,
                                                gboolean priv);

#define libbalsa_conf_get_vector_with_default(path, argcp, argvp, def) \
        (libbalsa_conf_get_vector_with_default_ ((path),(argcp),(argvp), \
                                                (def),FALSE))


#endif /* HAVE_GNOME */

#endif                          /* __LIBCONFIG_H__ */
