/* Balsa E-Mail Client
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

#ifndef _BALSA_SM_H
#define _BALSA_SM_H

/* ************************************************************************ */

#include "cfg-engine.h"

typedef gboolean (*sm_func_exit_interactor)( gboolean *exit_cancelled, gpointer user_data );

typedef struct sm_exit_trigger_results_s {
	gboolean internal_error;
	gboolean external_error;
	gboolean need_interaction;
	sm_func_exit_interactor interactor;
	gpointer interactor_user_data;
} sm_exit_trigger_results_t;

typedef gboolean (*sm_func_saver)( const cfg_location_t *root, gpointer user_data );
typedef gboolean (*sm_func_loader)( const cfg_location_t *root, gpointer user_data );
typedef void (*sm_func_exit_trigger)( sm_exit_trigger_results_t *results, gpointer user_data );

/* ************************************************************************ */

gboolean balsa_sm_init( int argc, char **argv );
gboolean balsa_sm_shutdown( void );

gboolean balsa_sm_add_global_pair( sm_func_saver saver, sm_func_loader loader, gpointer user_data );
gboolean balsa_sm_add_local_pair( sm_func_saver saver, sm_func_loader loader, gpointer user_data );
gboolean balsa_sm_add_exit_trigger( sm_func_exit_trigger trigger, gpointer user_data );

gboolean balsa_sm_save_local( void );
gboolean balsa_sm_save_global( void );
gboolean balsa_sm_save_local_event( sm_func_saver saver, gpointer user_data );
gboolean balsa_sm_save_global_event( sm_func_saver saver, gpointer user_data );
gboolean balsa_sm_save( void );

gboolean balsa_sm_load_local( void );
gboolean balsa_sm_load_global( void );
gboolean balsa_sm_load_local_event( sm_func_loader loader, gpointer user_data );
gboolean balsa_sm_load_global_event( sm_func_loader loader, gpointer user_data );
gboolean balsa_sm_load( void );

gboolean balsa_sm_exit( void );

/* ************************************************************************ */

cfg_location_t *balsa_sm_global_root;
cfg_location_t *balsa_sm_local_root;

#endif
