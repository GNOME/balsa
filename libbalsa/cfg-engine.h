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

#ifndef _BALSA_CONFIG_ENGINE_H
#define _BALSA_CONFIG_ENGINE_H

/* ********************************************************************** */

#include "config.h"
#include <gnome.h>

/* ********************************************************************** */

#ifndef _BALSA_CONFIG_TYPEDEF_LOCATION
#define _BALSA_CONFIG_TYPEDEF_LOCATION
typedef struct cfg_location_s cfg_location_t;
#endif

/* ********************************************************************** */

typedef struct cfg_parm_str_s {
	gchar *dflt;
	gchar *(*fetch_default)( const gchar *parmname );
	gboolean (*verify_val)( const gchar *parmname, const gchar *value );
} cfg_parm_str_t;

#define cfg_type_null_init_str { NULL, NULL NULL }

/* ********************************************************************** */

typedef struct cfg_parm_bool_s {
	gboolean dflt;
} cfg_parm_bool_t;

#define cfg_type_null_init_bool { FALSE }

/* ********************************************************************** */

typedef enum cfg_parm_num_flags_e {
	CPNF_USEMIN = (1 << 0),
	CPNF_USEMAX = (1 << 1)
} cfg_parm_num_flags_t;

typedef struct cfg_parm_num_s {
	gint32 dflt;
	gint32 min;
	gint32 max;
	cfg_parm_num_flags_t flags;
} cfg_parm_num_t;

#define cfg_type_null_init_num { 0, 0, 0, 0 }

/* ********************************************************************** */

typedef gboolean (*meta_writer)( gconstpointer, const cfg_location_t *, gconstpointer );
typedef gboolean (*meta_reader)( gpointer, const cfg_location_t *, gconstpointer );

typedef struct cfg_metatype_spec_s {
	meta_writer write;
	meta_reader read;
} cfg_metatype_spec_t;

typedef struct cfg_parm_meta_s {
	const cfg_metatype_spec_t *spec;
	gpointer typedata;
	gboolean pointer;
} cfg_parm_meta_t;

#define cfg_type_null_init_meta { NULL, NULL, FALSE }

/* ********************************************************************** */

typedef enum cfg_parm_types_e {
	CPT_STR, CPT_BOOL, CPT_NUM, CPT_META, CPT_NUM_TYPES
} cfg_parm_types_t;

/* ********************************************************************** */

/* I don't think other compilers support the struct { int a, b } this = { .a = 0 }; syntax
 */

#ifdef __GNUC__
#        define PDTYPE union
#        define cfg_type_const_init_str( dflt, fetch, verify_val ) { .str = { dflt, fetch, verify_val } }
#        define cfg_type_const_init_bool( dflt ) { .bool = { dflt } }
#        define cfg_type_const_init_num( dflt, min, max, flags ) { .num = { dflt, min, max, flags } }
#        define cfg_type_const_init_meta( spec, data, pointer ) { .meta = { spec, data, pointer } }
#        define cfg_type_null_init { .bool = { FALSE } }
#else
#        define PDTYPE struct
#        define cfg_type_const_init_str( dflt, fetch, verify_val ) \
                { { dflt, fetch, verify_val } \
                cfg_type_null_init_bool, \
                cfg_type_null_init_num, \
                cfg_type_null_init_meta }
#        define cfg_type_const_init_bool( dflt ) \
                { cfg_type_null_init_str, \
                { dflt }, \
                cfg_type_null_init_num, \
                cfg_type_null_init_meta }
#        define cfg_type_const_init_num( dflt, min, max, flags ) \
                { cfg_type_null_init_str, \
                cfg_type_null_init_bool, \
		{ dflt, min, max, flags }, \
                cfg_type_null_init_meta }
#        define cfg_type_const_init_meta( spec, data, pointer ) \
                { cfg_type_null_init_str, \
                cfg_type_null_init_bool, \
		cfg_type_null_init_num, \
                { spec, data, pointer } }
#        define cfg_type_null_init \
                { cfg_type_null_init_str, \
                cfg_type_null_init_bool, \
                cfg_type_null_init_num, \
                cfg_type_null_init_meta }

#endif

typedef PDTYPE cfg_parm_data_su {
	cfg_parm_str_t str;
	cfg_parm_bool_t bool;
	cfg_parm_num_t num;
	cfg_parm_meta_t meta;
} cfg_parm_data_t;

#undef PDTYPE


/* ********************************************************************** */

typedef struct cfg_parm_s {
	const gchar *name;
	const gint16 offset;
	const cfg_parm_types_t type;
	void (*changed)( const gchar *parmname, const gpointer val );
	cfg_parm_data_t data;
} cfg_parm_t;

#define cfg_parm_null { NULL, 0, CPT_NUM_TYPES, NULL, cfg_type_null_init }

/* ************************************************************************ */

gboolean cfg_group_write( gconstpointer data, const cfg_parm_t *elements, const cfg_location_t *where );
gboolean cfg_group_read( gpointer data, const cfg_parm_t *elements, const cfg_location_t *where );

/* ********************************************************************** */

#endif
