/*
 * Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#ifndef _BALSA_CFG_MEMORY_WIDGETS_H
#define _BALSA_CFG_MEMORY_WIDGETS_H

/* ************************************************************************ */

#include "config.h"
#include <gnome.h>

#include "cfg-engine.h"

/* ************************************************************************ */

typedef struct cfg_memory_plex_s {
	guint32 swapsize;
	gboolean (*swap_out)( GtkWidget *, gpointer, gpointer );
	gboolean (*swap_in)( GtkWidget *, gpointer, gpointer );
	gboolean (*do_default)( GtkWidget *, gpointer );
	gboolean (*save)( gpointer, const cfg_location_t *, gpointer );
	gboolean (*load)( gpointer, const cfg_location_t *, gpointer );
	GtkWidget *(*get_active)( gpointer );
} cfg_memory_plex_t;

typedef struct cfg_memory_clist_swapdata_s {
	gint num_cols;
	gint *widths;
} cfg_memory_clist_swapdata_t;

/* ************************************************************************ */

void cfg_memory_add_to_window( GtkWidget *window, const cfg_location_t *root, const gchar *name, guint32 default_w, guint32 default_h );
void cfg_memory_add_to_clist( GtkWidget *clist, const cfg_location_t *root, const gchar *name, gint numcolumns, ... );
void cfg_memory_add_to_paned( GtkWidget *paned, const cfg_location_t *root, const gchar *name, gint offset );
void cfg_memory_add_multiplexed( const gchar *name, const cfg_memory_plex_t *plexinfo, gpointer user_data );

void cfg_memory_write_all( const cfg_location_t *root );
cfg_location_t *cfg_memory_default_root( void );

void cfg_memory_multiplex_swapout( const gchar *name );
void cfg_memory_multiplex_swapin( const gchar *name );

extern cfg_memory_plex_t cfg_memory_clist_plexinfo;

/* ************************************************************************ */

#endif
