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

#include "libinit_balsa/init_balsa.h"
#include "cfg-backend.h"
#include "sm.h"

#include "main.h" /* temporary, for exit triggers */

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "Balsa SM Debug"

#define SM_DEBUG 1

#ifdef SM_DEBUG
#define DM( str, args... ) g_message( str, ##args )
#else
#define DM( str, args... ) /**/
#endif

/* ************************************************************************ */

typedef struct pairinfo_t {
	sm_func_saver saver;
	sm_func_loader loader;
	gpointer user_data;
} pairinfo;

typedef struct triginfo_t {
	sm_func_exit_trigger trigger;
	gpointer user_data;
} triginfo;

/* ************************************************************************ */

cfg_location_t *balsa_sm_global_root = NULL;
cfg_location_t *balsa_sm_local_root = NULL;

/* ************************************************************************ */

static void trigger_funcs( GSList *head, cfg_location_t *root, gboolean save );
static gint kill_session( GnomeClient *client, gpointer client_data );
static gint save_session( GnomeClient *client, gint phase, GnomeSaveStyle save_style,
			  gint is_shutdown, GnomeInteractStyle interact_style, 
			  gint is_fast, gpointer client_data );
static void reset_cfg_prefix( GnomeClient *client );
static void connect_to_sm( GnomeClient *client, gint restarted );
static void disconnect_from_sm( GnomeClient *client );

#if 0
static gboolean do_exit_triggers( const cfg_location_t *root, gpointer user_data );
static void do_interactions( GnomeDialogType type, GSList *ilist );
static void interact_cb( GnomeClient *client, gint key, GnomeDialogType type, gpointer data );
#endif

static GSList *global_pairs = NULL;
static GSList *local_pairs = NULL;
static GSList *exit_triggers = NULL;
static GnomeClient *master_client = NULL;
static GnomeClient *active_client = NULL;
static gboolean really_connected = FALSE;
static gboolean exit_ordered = FALSE;
static gboolean save_pending = FALSE;

/* ************************************************************************ */

gboolean balsa_sm_init( int argc, char **argv )
{
	gchar *local_pfx, *global_pfx;
	gchar *clone_args[1];
	
	DM( "Initing master client" );

	master_client = gnome_master_client();
	gtk_signal_connect( GTK_OBJECT(master_client), "save_yourself", GTK_SIGNAL_FUNC(save_session), argv[0] );
	gtk_signal_connect( GTK_OBJECT(master_client), "die", GTK_SIGNAL_FUNC(kill_session), NULL );
	gtk_signal_connect( GTK_OBJECT(master_client), "connect", GTK_SIGNAL_FUNC(connect_to_sm), NULL );
	gtk_signal_connect( GTK_OBJECT(master_client), "disconnect", GTK_SIGNAL_FUNC(disconnect_from_sm), NULL );

	local_pfx = gnome_client_get_config_prefix( master_client );
	global_pfx = gnome_client_get_global_config_prefix( master_client );
	balsa_sm_global_root = cfg_location_from_gnome_prefix( global_pfx );

	DM( "Prefixes: global \"%s\", local \"%s\"", global_pfx, local_pfx );
	reset_cfg_prefix( master_client );

	clone_args[0] = argv[0];
	gnome_client_set_restart_command( master_client, 1, clone_args );
	gnome_client_set_shutdown_command( master_client, 0, NULL );
	gnome_client_set_priority( master_client, 70 );
	gnome_client_set_restart_style( master_client, GNOME_RESTART_ANYWAY );

	#if 0
	balsa_sm_add_global_pair( do_exit_triggers, NULL, NULL );
	#endif

	DM( "Done in balsa_sm_init" );
	return FALSE;
}

gboolean balsa_sm_shutdown( void )
{
	DM( "Shutdown SM services" );

	g_slist_foreach( global_pairs, (GFunc) g_free, NULL );
	g_slist_free( global_pairs );
	global_pairs = NULL;

	g_slist_foreach( local_pairs, (GFunc) g_free, NULL );
	g_slist_free( local_pairs );
	local_pairs = NULL;

	g_slist_foreach( exit_triggers, (GFunc) g_free, NULL );
	g_slist_free( exit_triggers );
	exit_triggers = NULL;

	cfg_location_free( balsa_sm_local_root );
	balsa_sm_local_root = NULL;
	cfg_location_free( balsa_sm_global_root );
	balsa_sm_global_root = NULL;

	return FALSE;
}

gboolean balsa_sm_add_global_pair( sm_func_saver saver, sm_func_loader loader, gpointer user_data )
{
	pairinfo *pi;

	/* g_assert( saver );
	 * g_assert( loader );
	 */

	DM( "Registering SM global pair: %p %p %p", saver, loader, user_data );

	pi = g_new( pairinfo, 1 );
	pi->saver = saver;
	pi->loader = loader;
	pi->user_data = user_data;

	global_pairs = g_slist_prepend( global_pairs, pi );

	return FALSE;
}

gboolean balsa_sm_add_local_pair( sm_func_saver saver, sm_func_loader loader, gpointer user_data )
{
	pairinfo *pi;

	/* g_assert( saver );
	 * g_assert( loader );
	 */

	DM( "Registering SM local pair: %p %p %p", saver, loader, user_data );

	pi = g_new( pairinfo, 1 );
	pi->saver = saver;
	pi->loader = loader;
	pi->user_data = user_data;

	local_pairs = g_slist_prepend( local_pairs, pi );

	return FALSE;
}

gboolean balsa_sm_add_exit_trigger( sm_func_exit_trigger trigger, gpointer user_data )
{
	triginfo *ti;

	g_return_val_if_fail( trigger, TRUE );

	DM( "add_exit_trigger: %p %p", trigger, user_data );

	ti = g_new( triginfo, 1 );
	ti->trigger = trigger;
	ti->user_data = user_data;
	exit_triggers = g_slist_prepend( exit_triggers, ti );

	return FALSE;
}

gboolean balsa_sm_save( void )
{
	DM( "balsa_sm_save" );

	save_pending = TRUE;
	gnome_client_request_save( master_client, GNOME_SAVE_BOTH, FALSE,
				   GNOME_INTERACT_ANY, FALSE, FALSE );

	/* FIXME: provide some feedback? */
	while( save_pending )
		gtk_main_iteration();

	return FALSE;
}

gboolean balsa_sm_save_local( void )
{
	DM( "save: only local" );

	save_pending = TRUE;
	gnome_client_request_save( master_client, GNOME_SAVE_LOCAL, FALSE,
				   GNOME_INTERACT_ANY, FALSE, FALSE );

	/* FIXME: provide some feedback? */
	while( save_pending )
		gtk_main_iteration();

	return FALSE;
}

gboolean balsa_sm_save_global( void )
{
	DM( "save: only global" );

	save_pending = TRUE;
	gnome_client_request_save( master_client, GNOME_SAVE_GLOBAL, FALSE,
				   GNOME_INTERACT_ANY, FALSE, FALSE );

	/* FIXME: provide some feedback? */
	while( save_pending )
		gtk_main_iteration();

	return FALSE;
}

gboolean balsa_sm_save_local_event( sm_func_saver saver, gpointer user_data )
{
	DM( "local save event: %p %p", saver, user_data );

	if( saver( balsa_sm_local_root, user_data ) )
		return TRUE;

	/* Don't sync unless we really should. this is used for mailbox
	 * changes et al, which need the config prefix, but they don't
	 * deserve a whole save-session and sync.
	 * gnome_client_request_save( master_client, GNOME_SAVE_LOCAL, FALSE,
	 * GNOME_INTERACT_ANY, FALSE, FALSE );
	 */

	return FALSE;
}

gboolean balsa_sm_save_global_event( sm_func_saver saver, gpointer user_data )
{
	DM( "global save event: %p %p", saver, user_data );

	if( saver( balsa_sm_global_root, user_data ) )
		return TRUE;

	/* Don't sync unless we really should. We shouldn't. 
	 * gnome_client_request_save( master_client, GNOME_SAVE_GLOBAL, FALSE,
	 * GNOME_INTERACT_ANY, FALSE, FALSE );
	 */

	return FALSE;
}

gboolean balsa_sm_load( void )
{

	cfg_location_t *down;

	DM( "balsa_sm_load" );
#if 0
	if( GNOME_CLIENT_CONNECTED( master_client ) ) {
		if( (gnome_client_get_flags( master_client ) & GNOME_CLIENT_RESTORED) == 0 ) {
			DM( "Not restored, check for local root key" );
			if( cfg_location_exists( balsa_sm_local_root ) == 0 )
				balsa_init_begin();
		}

		DM( "connected, load prefs" );
		balsa_sm_load_global();

		/* if( gnome_client_get_flags( master_client ) & GNOME_CLIENT_RESTARTED ) */
		balsa_sm_load_local();
	} else {
		DM( "Unconnected, do a basic load" );
		balsa_sm_load_global();
		balsa_sm_load_local();
	}
#else
	down = cfg_location_godown( balsa_sm_global_root, "Mailboxes" );
	if( cfg_location_exists( down ) == 0 )
		balsa_init_begin();
	cfg_location_free( down );

	balsa_sm_load_global();

	down = cfg_location_godown( balsa_sm_local_root, "UISettings" );
	if( cfg_location_exists( down ) == 0 )
		/* default ui prefs */ { ; }
	cfg_location_free( down );

	balsa_sm_load_local();
#endif
	return FALSE;
}

gboolean balsa_sm_load_local( void )
{
	DM( "load: only local" );
	trigger_funcs( local_pairs, balsa_sm_local_root, FALSE );
	return FALSE;
}

gboolean balsa_sm_load_global( void )
{
	DM( "load: only global" );
	trigger_funcs( global_pairs, balsa_sm_global_root, FALSE );
	return FALSE;
}

gboolean balsa_sm_load_local_event( sm_func_loader loader, gpointer user_data )
{
	DM( "local load event: %p %p", loader, user_data );
	return loader( balsa_sm_local_root, user_data );
}

gboolean balsa_sm_load_global_event( sm_func_loader loader, gpointer user_data )
{
	DM( "global load event: %p %p", loader, user_data );
	return loader( balsa_sm_global_root, user_data );
}

gboolean balsa_sm_exit( void )
{
	DM( "exit ordered" );

	exit_ordered = TRUE;
#if 0
	{

		gnome_client_request_save( master_client, GNOME_SAVE_GLOBAL, TRUE,
					   GNOME_INTERACT_ANY, FALSE, FALSE );
		return FALSE;
	}
#else
	{
		sm_exit_trigger_results_t res;

		balsa_maybe_save( &res, NULL );
		balsa_close_mailboxes( &res, NULL );

		if( really_connected == FALSE ) {
			balsa_sm_save();
		} else {
			kill_session( NULL, NULL );
		}

		return FALSE;
	}
#endif
}

#if 0
static gboolean do_exit_triggers( const cfg_location_t *root, gpointer user_data )
{
	GSList *iter;
	triginfo *ti;
	GSList *normal_interactions = NULL;
	GSList *error_interactions = NULL;
	sm_exit_trigger_results_t *res;

	if( exit_ordered == FALSE )
		return FALSE;

	for( iter = exit_triggers; iter; iter = iter->next ) {
		ti = (triginfo *) (iter->data);
		res = g_new0( sm_exit_trigger_results_t, 1 );
		g_assert( ti->trigger );

		(ti->trigger)( res, ti->user_data );

		if( res->internal_error ) {
			g_warning( "Error running exit trigger function %p", ti->trigger );
			continue;
		}

		if( res->need_interaction == FALSE ) {
			g_free( res );
			continue;
		}

		if( res->external_error )
			error_interactions = g_slist_prepend( error_interactions, res );
		else
			normal_interactions = g_slist_prepend( normal_interactions, res );
	}
			
	if( error_interactions )
		do_interactions( GNOME_DIALOG_ERROR, error_interactions );
	if( normal_interactions )
		do_interactions( GNOME_DIALOG_NORMAL, normal_interactions );

	return FALSE;
}
#endif

/* ************************************************************************ */

#if 0
static void do_interactions( GnomeDialogType type, GSList *ilist )
{
	GSList *iter;

	for( iter = ilist; iter; iter = iter->next ) {
		gnome_client_request_interaction( master_client, type, interact_cb, iter->data );
		g_free( iter->data );
	}

	g_slist_free( ilist );
}

static void interact_cb( GnomeClient *client, gint key, GnomeDialogType type, gpointer data )
{
	sm_exit_trigger_results_t *res = (sm_exit_trigger_results_t *) data;
	gboolean exit_cancelled;

	g_assert( res->interactor );

	if( (res->interactor)( &exit_cancelled, res->interactor_user_data ) )
		g_warning( "Problem running exit trigger interaction routine" );

	gnome_interaction_key_return( key, exit_cancelled );
}
#endif

static void reset_cfg_prefix( GnomeClient *client )
{
	gchar *prefix;

	if( balsa_sm_local_root )
		cfg_location_free( balsa_sm_local_root );

	prefix = gnome_client_get_config_prefix( client );
	DM( "reset_cfg_prefix: new prefix %s", prefix );
	balsa_sm_local_root = cfg_location_from_gnome_prefix( prefix );

	if( cfg_location_cmp( balsa_sm_local_root, balsa_sm_global_root ) == 0 ) {
		cfg_location_t *tmp;

		tmp = cfg_location_godown( balsa_sm_local_root, "DefaultSession" );
		cfg_location_free( balsa_sm_local_root );
		balsa_sm_local_root = tmp;

		DM( "reset_cfg_prefix: Treating as new session" );
		really_connected = FALSE;

		/* Don't toast our real configuration! */
		gnome_client_set_discard_command( client, 0, NULL );
	} else {
		gchar *discard_args[] = { "rm", "-r", NULL };

		DM( "reset_cfg_prefix: Treating as continued session" );
		really_connected = TRUE;
		
		/* get_cfg_prefix gives us an evil leading slash that concat_dir_and_file (implicitly called in
		 * get_real_path) can't handle. 
		 */
		
		discard_args[2] = gnome_config_get_real_path( &(prefix[1]) );
		gnome_client_set_discard_command( client, 3, discard_args );
	}
}
	
static void connect_to_sm( GnomeClient *client, gint restarted )
{
	DM( "connect_to_sm: set new local prefix" );

	reset_cfg_prefix( client );

	if( cfg_location_cmp( balsa_sm_local_root, balsa_sm_global_root ) ) {
		DM( "connect_to_sm: real alternate session" );
		really_connected = TRUE;

		if( restarted )
			balsa_sm_load();
	} else {
		DM( "connect_to_sm: not really alternate session" );
		really_connected = FALSE;
	}
}

static void disconnect_from_sm( GnomeClient *client )
{
	if( client->state != GNOME_CLIENT_DISCONNECTED ) {
		DM( "disconnect_from_sm: new cfg prefix" );
		reset_cfg_prefix( client );
	} else {
		DM( "disconnect_from_sm: don't set new prefix" );
	}
}

/* ************************************************************************ */

static void trigger_funcs( GSList *head, cfg_location_t *root, gboolean save )
{
	GSList *iter;
	pairinfo *pi;

	if( save ) {
		for( iter = head; iter; iter = iter->next ) {
			pi = (pairinfo *) iter->data;
			
			DM( "trigger_funcs: save %p %p", pi->saver, pi->user_data );

			if( pi->saver && pi->saver( root, pi->user_data ) )
				g_warning( "Error while running saver callback at %p (userdata: %p).", pi->saver, pi->user_data );
		}
	} else {
		for( iter = head; iter; iter = iter->next ) {
			pi = (pairinfo *) iter->data;

			DM( "trigger_funcs: load %p %p", pi->loader, pi->user_data );
			
			if( pi->loader && pi->loader( root, pi->user_data ) )
				g_warning( "Error while running loader callback at %p (userdata: %p).", pi->saver, pi->user_data );
		}
	}
}

static gint kill_session( GnomeClient *client, gpointer client_data )
{
	DM( "kill_session signaled" );

	/*cfg_revert();*/
	/*balsa_sm_exit();*/
	balsa_sm_shutdown();
	gnome_sound_shutdown ();
	gtk_exit( 0 );
        return TRUE;
}

static gint save_session( GnomeClient *client, gint phase, GnomeSaveStyle save_style,
			  gint is_shutdown, GnomeInteractStyle interact_style, 
			  gint is_fast, gpointer client_data )
{
	DM( "save_session signaled" );

	active_client = client;

	/* "Huh," you say? Well, as explained below we sometime need to
	 * trigger a save_session upon exit, if we're not running 
	 * session-managed (=with config_prefix != global_config_prefix).
	 * But then we would get a new config prefix, set here, instead
	 * of /balsa/DefaultSession/. So if we're doing the save-on-exit
	 * thing, screw it. 
	 */

	if( exit_ordered == FALSE )
		reset_cfg_prefix( client );

	switch( save_style ) {
	case GNOME_SAVE_BOTH:
		DM( "save_session: both" );
		trigger_funcs( global_pairs, balsa_sm_global_root, TRUE );
		trigger_funcs( local_pairs, balsa_sm_local_root, TRUE );
		break;
	case GNOME_SAVE_GLOBAL:
		DM( "save_session: global" );
		trigger_funcs( global_pairs, balsa_sm_global_root, TRUE );
		break;
	case GNOME_SAVE_LOCAL:
		DM( "save_session: local" );
		trigger_funcs( local_pairs, balsa_sm_local_root, TRUE );
		break;
	}

	DM( "save_session: Help me, I'm syncing!" );
	cfg_sync();

	/* If we're not really using SM, balsa_sm_exit() must ask for the 
	 * save session signal, and we can't exit until it actually happens,
	 * so we must wait until we get here to exit. exit_ordered lets
	 * us know when that happens.
	 */

	if( exit_ordered )
		kill_session( NULL, NULL );

	active_client = NULL;
	save_pending = FALSE;
        return TRUE;
}
