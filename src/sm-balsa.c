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

#include "config.h"

#include <gnome.h>

#include "main.h" /*balsa_exit*/
#include "balsa-app.h"
#include "cfg-engine.h"
#include "cfg-balsa.h"
#include "sm-balsa.h"
#include "cfg-memory-widgets.h"

/* ************************************************************************ */

typedef struct sinfo_s {
	const cfg_metatype_spec_t *spec;
	gpointer data;
	gconstpointer user_data;
} sinfo;

static gboolean spec_to_writer( const cfg_location_t *loc, gpointer user_data );
static gboolean spec_to_reader( const cfg_location_t *loc, gpointer user_data );

/* ************************************************************************ */

sm_state_t balsa_state;

static sinfo si_balsa_app = { &spec_BalsaApp, &balsa_app, NULL };
static sinfo si_balsa_state = { &spec_sm_state_t, &balsa_state, NULL };
static sinfo si_mailboxes = { &spec_MailboxArray, NULL, NULL };

static gboolean write_memory( const cfg_location_t *loc, gpointer user_data );
static gboolean speak_memory( const cfg_location_t *loc, gpointer user_data );

/* ************************************************************************ */

static gboolean spec_to_writer( const cfg_location_t *loc, gpointer user_data )
{
	sinfo *info = (sinfo *) user_data;

	return (info->spec->write)( info->data, loc, info->user_data );
}

static gboolean spec_to_reader( const cfg_location_t *loc, gpointer user_data )
{
	sinfo *info = (sinfo *) user_data;

	return (info->spec->read)( info->data, loc, info->user_data );
}

static gboolean write_memory( const cfg_location_t *loc, gpointer user_data )
{
	cfg_memory_write_all( loc );
	return FALSE;
}

static gboolean speak_memory( const cfg_location_t *loc, gpointer user_data )
{
	return FALSE;
}

/* ************************************************************************ */

gboolean sm_init( int argc, char **argv )
{
	balsa_sm_init( argc, argv );

	balsa_sm_add_exit_trigger( balsa_maybe_save, NULL );
	balsa_sm_add_exit_trigger( balsa_close_mailboxes, NULL );

	balsa_sm_add_global_pair( spec_to_writer, spec_to_reader, &si_balsa_app );

	balsa_sm_load_global();

	balsa_sm_add_global_pair( spec_to_writer, spec_to_reader, &si_mailboxes );
	balsa_sm_add_local_pair( spec_to_writer, spec_to_reader, &si_balsa_state );
	balsa_sm_add_local_pair( write_memory, speak_memory, NULL );
	return balsa_sm_load();
}

gboolean cfg_load( void )
{
	g_warning( "deprecated cfg_load called" );
	return FALSE;
}

gboolean cfg_save( void )
{
	g_warning( "deprecated cfg_save called" );
	return FALSE;
}

