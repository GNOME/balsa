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

void cfg_memory_add_to_window( GtkWindow *window, const cfg_location_t *root, const gchar *name, guint32 default_w, guint32 default_h );
void cfg_memory_add_to_clist( GtkCList *clist, const cfg_location_t *root, const gchar *name, gint numcolumns, ... );
void cfg_memory_add_to_paned( GtkPaned *paned, const cfg_location_t *root, const gchar *name, gint offset );

void cfg_memory_write_all( const cfg_location_t *root );
cfg_location_t *cfg_memory_default_root( void );

void cfg_memory_clist_sync_from( GtkCList *clist, const cfg_location_t *root );
void cfg_memory_clist_sync_to( GtkCList *clist, const cfg_location_t *root );

/* ************************************************************************ */

#endif
