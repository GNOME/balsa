/* Balsa E-Mail Client
 *
 * This file handles Balsa's configuration information.
 *
 * This file is Copyright (C) 1998-1999 Nat Friedman
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

#ifndef _BALSA_CONFIG_BACKEND_H
#define _BALSA_CONFIG_BACKEND_H

/* ********************************************************************** */

#include "config.h"
#include <gnome.h>

/* ********************************************************************** */

#ifndef _BALSA_CONFIG_TYPEDEF_LOCATION
#define _BALSA_CONFIG_TYPEDEF_LOCATION
typedef struct cfg_location_s cfg_location_t;
#endif

/* ********************************************************************** */

typedef void (*cfg_enum_callback)( const cfg_location_t *where, const gchar *name, const gboolean is_group, gpointer user_data );

cfg_location_t *cfg_get_root( void );
cfg_location_t *cfg_location_godown( const cfg_location_t *where, const gchar *name );

cfg_location_t *cfg_location_dup( const cfg_location_t *where );
gboolean cfg_location_cmp( const cfg_location_t *left, const cfg_location_t *right );

gboolean cfg_location_put_num( const cfg_location_t *where, const gchar *name, const gint value );
gboolean cfg_location_put_str( const cfg_location_t *where, const gchar *name, const gchar *value );
gboolean cfg_location_put_bool( const cfg_location_t *where, const gchar *name, const gboolean value );

gboolean cfg_location_get_num( const cfg_location_t *where, const gchar *name, gint *value, const gint dflt );
gboolean cfg_location_get_str( const cfg_location_t *where, const gchar *name, gchar **value, const gchar *dflt );
gboolean cfg_location_get_bool( const cfg_location_t *where, const gchar *name, gboolean *value, const gboolean dflt );

gboolean cfg_location_del_key( const cfg_location_t *where, const gchar *name );
gboolean cfg_location_del( const cfg_location_t *where );

void cfg_location_enumerate( const cfg_location_t *where, cfg_enum_callback cb, gpointer user_data );
gboolean cfg_location_exists( const cfg_location_t *where );

void cfg_location_free( cfg_location_t *where );

#define cfg_sync gnome_config_sync

/* ********************************************************************** */

#endif

