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

#include "config.h"
#include <gnome.h>

#include "balsa-app.h"
#include "cfg-backend.h"
#include "cfg-engine.h"
#include "sm-balsa.h"
#include "cfg-memory-widgets.h"

/* ************************************************************************ */

typedef struct generic_memory_s {
	const gchar *name;
	GSList **list;
} generic_memory_t;

typedef struct plexinfo_priv_s {
	const cfg_memory_plex_t *info;
	gpointer swapdata;
	gboolean swapped;
	const gchar *name;
	gpointer ud;
} plexinfo_priv_t;

/* ************************************************************************ */

static void generic_init( GtkWidget *widget, const gchar *name, GSList **list );
static void generic_destroy( GtkWidget *widget );
static gboolean generic_load( GtkWidget *widget, const cfg_location_t *root, gboolean (*loader)( GtkWidget *, const cfg_location_t * ) );
static void generic_save( GtkWidget *widget, const cfg_location_t *root, void (*saver)( GtkWidget *, const cfg_location_t * ) );

static gboolean window_load( GtkWidget *window, const cfg_location_t *home );
static void window_save( GtkWidget *window, const cfg_location_t *home );

static gboolean clist_load( GtkWidget *clist, const cfg_location_t *home );
static void clist_save( GtkWidget *clist, const cfg_location_t *home );

static gboolean paned_load( GtkWidget *paned, const cfg_location_t *home );
static void paned_save( GtkWidget *paned, const cfg_location_t *home );

static void write_plexes( gpointer key, gpointer value, gpointer user_data );

/* ************************************************************************ */

void cfg_memory_add_to_window( GtkWidget *window, const cfg_location_t *root, const gchar *name, guint32 default_w, guint32 default_h );
void cfg_memory_add_to_clist( GtkWidget *clist, const cfg_location_t *root, const gchar *name, gint numcolumns, ... );
void cfg_memory_add_to_paned( GtkWidget *paned, const cfg_location_t *root, const gchar *name, gint offset );
void cfg_memory_add_multiplexed( const gchar *name, const cfg_memory_plex_t *plexinfo, gpointer user_data );

void cfg_memory_write_all( const cfg_location_t *root );
cfg_location_t *cfg_memory_default_root( void );

void cfg_memory_multiplex_swapout( const gchar *name );
void cfg_memory_multiplex_swapin( const gchar *name );

/* ************************************************************************ */

static const gchar object_key[] = "balsa-config-home";
static const gchar width_key[] = "Width";
static const gchar height_key[] = "Height";
static const gchar col_width_format[] = "Column%dWidth";
static const gchar col_count_key[] = "ColumnCount";
static const gchar offset_key[] = "Offset";
static const gchar clist_obj_key[] = "balsa-config-clist-info";

static GSList *memory_windows = NULL;
static GSList *memory_clists = NULL;
static GSList *memory_paneds = NULL;
static GHashTable *memory_plexes = NULL;

/* ************************************************************************ */

void cfg_memory_add_to_window( GtkWidget *window, const cfg_location_t *root, const gchar *name, guint32 default_w, guint32 default_h )
{
	g_return_if_fail( window );
	g_return_if_fail( root );
	g_return_if_fail( name );
	g_return_if_fail( GTK_IS_WINDOW( window ) );

	generic_init( window, name, &memory_windows );

	if( generic_load( window, root, window_load ) ) {
		cfg_location_t *home;

		home = cfg_location_godown( root, name );	
		cfg_location_put_num( home, width_key, default_w );
		cfg_location_put_num( home, height_key, default_h );
		cfg_location_free( home );

		generic_load( window, root, window_load );
	}
}

void cfg_memory_add_to_clist( GtkWidget *clist, const cfg_location_t *root, const gchar *name, gint numcolumns, ... )
{
	g_return_if_fail( clist );
	g_return_if_fail( root );
	g_return_if_fail( name );
	g_return_if_fail( GTK_IS_CLIST( clist ) );

	generic_init( clist, name, &memory_clists );

	if( generic_load( clist, root, clist_load ) ) {
		cfg_location_t *home;
		gchar *subname;
		gint i;
		va_list val;

		va_start( val, numcolumns );
		home = cfg_location_godown( root, name );

		cfg_location_put_num( home, col_count_key, numcolumns );

		for( i = 0; i < numcolumns; i++ ) {
			subname = g_strdup_printf( col_width_format, i );
			cfg_location_put_num( home, subname, va_arg( val, gint ) );
			g_free( subname );
		}

		va_end( val );
		cfg_location_free( home );

		generic_load( clist, root, clist_load );
	}
}

void cfg_memory_add_multiplexed( const gchar *name, const cfg_memory_plex_t *plexinfo, gpointer user_data )
{
	plexinfo_priv_t *priv;

	g_return_if_fail( name );
	g_return_if_fail( plexinfo );

	if( memory_plexes == NULL )
		memory_plexes = g_hash_table_new( g_str_hash, g_str_equal );

	priv = g_new( plexinfo_priv_t, 1 );
	priv->info = plexinfo;
	priv->swapdata = g_new0( guint8, plexinfo->swapsize );
	priv->name = name;
	priv->ud = user_data;
	priv->swapped = FALSE;

	g_hash_table_insert( memory_plexes, (gpointer) name, priv );
}

void cfg_memory_add_to_paned( GtkWidget *paned, const cfg_location_t *root, const gchar *name, gint offset )
{
	g_return_if_fail( paned );
	g_return_if_fail( root );
	g_return_if_fail( name );
	g_return_if_fail( GTK_IS_PANED( paned ) );

	generic_init( GTK_WIDGET( paned ), name, &memory_paneds );

	if( generic_load( paned, root, paned_load ) ) {
		cfg_location_t *home;

		home = cfg_location_godown( root, name );	
		cfg_location_put_num( home, offset_key, offset );
		cfg_location_free( home );

		generic_load( paned, root, paned_load );
	}
}


void cfg_memory_write_all( const cfg_location_t *root )
{
	GSList *iter;
	cfg_location_t *down;

	down = cfg_location_godown( root, "UISettings" );

	for( iter = memory_windows; iter; iter = iter->next )
		generic_save( GTK_WIDGET( iter->data ), down, window_save );

	for( iter = memory_clists; iter; iter = iter->next )
		generic_save( GTK_WIDGET( iter->data ), down, clist_save );

	for( iter = memory_paneds; iter; iter = iter->next )
		generic_save( GTK_WIDGET( iter->data ), down, paned_save );

	if( memory_plexes )
		g_hash_table_foreach( memory_plexes, write_plexes, down );

	cfg_location_free( down );
}

cfg_location_t *cfg_memory_default_root( void )
{
	return cfg_location_godown( balsa_sm_local_root, "UISettings" );
}

/* **************************************** */

/* Aah, that notebook is a real pain in the ass. "Multiplexing" in this context
 * is mapping one set of config values to more than one widget -- specifically,
 * every mailbox has one GtkCList for its messageindex, each of which should
 * sync up and look the same.
 *
 * We can't just memory_add_to_clist for each one, because then we get bunches
 * of widgets trying to write to the same prefix, and loading from.... I tried
 * it and it really didn't work.
 *
 * So instead we multiplex. The swap_{in,out} functions save the widgets' settings
 * to an in-memory structure, while the save and load functions go from cfg_locations
 * to the in-memory structure. 'default' will apply defaults to the widget... maybe
 * this mini-API is a bit weird but whatever, we only use it once.
 */

void cfg_memory_multiplex_swapout( const gchar *name )
{
	plexinfo_priv_t *priv;
	GtkWidget *current;

	g_return_if_fail( name );

	priv = g_hash_table_lookup( memory_plexes, name );
	g_return_if_fail( priv );

	current = (priv->info->get_active)( priv->ud );
	g_return_if_fail( current );

	if( priv->swapped ) {
		g_warning( "Multiplex group \"%s\" already swapped out, ignoring.", name );
		return;
	}

	if( (priv->info->swap_out)( current, priv->swapdata, priv->ud ) )
		g_warning( "Error while swapping out multiplex group \"%s\", widget %p", name, current );

	priv->swapped = TRUE;
}

void cfg_memory_multiplex_swapin( const gchar *name )
{
	plexinfo_priv_t *priv;
	GtkWidget *current;

	g_return_if_fail( name );

	priv = g_hash_table_lookup( memory_plexes, name );
	g_return_if_fail( priv );

	current = (priv->info->get_active)( priv->ud );
	g_return_if_fail( current );

	if( GTK_WIDGET_VISIBLE( current ) == FALSE )
		return;

	if( priv->swapped == FALSE ) {
		cfg_location_t *root, *home;

		root = cfg_memory_default_root();
		home = cfg_location_godown( root, name );
		cfg_location_free( root );

		if( cfg_location_exists( home ) == FALSE ) {
			if( (priv->info->do_default)( current, priv->ud ) )
				g_warning( "Error setting defaults for multiplex group \"%s\"", name );
		} else if( (priv->info->load)( priv->swapdata, home, priv->ud ) ) {
			g_warning( "Error loading config data for multiplex group \"%s\"", name );
		} else {
			priv->swapped = TRUE;
			cfg_memory_multiplex_swapin( name );
		}

		cfg_location_free( home );
		return;
	}

	if( (priv->info->swap_in)( current, priv->swapdata, priv->ud ) )
		g_warning( "Error while swapping out multiplex group \"%s\", widget %p", name, current );

	priv->swapped = FALSE;
}

static void write_plexes( gpointer key, gpointer value, gpointer user_data )
{
	gchar *realkey = (gchar *) key;
	plexinfo_priv_t *priv = (plexinfo_priv_t *) value;
	const cfg_location_t *root = (const cfg_location_t *) user_data;
	cfg_location_t *home;

	cfg_memory_multiplex_swapout( realkey );

	home = cfg_location_godown( root, realkey );

	if( (priv->info->save)( priv->swapdata, home, priv->ud ) )
		g_warning( "Problem saving multiplex group \"%s\"", realkey );

	cfg_memory_multiplex_swapin( realkey );

	cfg_location_free( home );
}

/* ************************************************************************ */

static void generic_init( GtkWidget *widget, const gchar *name, GSList **list )
{
	generic_memory_t *gm;

	g_return_if_fail( GTK_IS_WIDGET( widget ) );
	g_return_if_fail( name );
	g_return_if_fail( list );

	gm = g_new( generic_memory_t, 1 );
	gm->name = name;
	gm->list = list;

	gtk_object_set_data( GTK_OBJECT( widget ), object_key, gm );
	gtk_signal_connect( GTK_OBJECT( widget ), "destroy", generic_destroy, NULL );

	(*list) = g_slist_prepend( (*list), widget );
}

static void generic_destroy( GtkWidget *widget )
{
	generic_memory_t *gm;

	gm = gtk_object_get_data( GTK_OBJECT( widget ), object_key );
	g_return_if_fail( gm );

	gm->name = NULL;
	(*(gm->list)) = g_slist_remove( (*(gm->list)), widget );
	gm->list = NULL;
	g_free( gm );
}

static gboolean generic_load( GtkWidget *widget, const cfg_location_t *root, gboolean (*loader)( GtkWidget *, const cfg_location_t * ) )
{
	generic_memory_t *gm;
	cfg_location_t *home;
	gboolean ret;

	g_return_val_if_fail( GTK_IS_WIDGET( widget ), TRUE );

	gm = gtk_object_get_data( GTK_OBJECT( widget ), object_key );
	g_return_val_if_fail( gm, TRUE );

	home = cfg_location_godown( root, gm->name );
	ret = loader( widget, home );
	cfg_location_free( home );
	return ret;
}

static void generic_save( GtkWidget *widget, const cfg_location_t *root, void (*saver)( GtkWidget *, const cfg_location_t * ) )
{
	generic_memory_t *gm;
	cfg_location_t *home;

	g_return_if_fail( GTK_IS_WIDGET( widget ) );

	gm = gtk_object_get_data( GTK_OBJECT( widget ), object_key );
	g_return_if_fail( gm );

	home = cfg_location_godown( root, gm->name );
	saver( widget, home );
	cfg_location_free( home );
}

static gboolean window_load( GtkWidget *window, const cfg_location_t *home )
{
	guint32 w = 0, h = 0;

	g_return_val_if_fail( GTK_IS_WINDOW( window ), TRUE );

	if( cfg_location_get_num( home, width_key, &w, 0 ) || w == 0 )
		return TRUE;

	if( cfg_location_get_num( home, height_key, &h, 0 ) || h == 0 )
		return TRUE;

	gtk_window_set_default_size( GTK_WINDOW( window ), w, h );
	return FALSE;
}

static void window_save( GtkWidget *window, const cfg_location_t *home )
{
	g_return_if_fail( GTK_IS_WINDOW( window ) );

	cfg_location_put_num( home, width_key, (GTK_WIDGET( window ))->allocation.width );
	cfg_location_put_num( home, height_key, (GTK_WIDGET( window ))->allocation.height );
}

static gboolean clist_load( GtkWidget *clist, const cfg_location_t *home )
{
	guint32 num = 0, width = 0, max = 0;
	gchar *name;

	g_return_val_if_fail( GTK_IS_CLIST( clist ), TRUE );

	if( cfg_location_get_num( home, col_count_key, &max, 0 ) || max == 0 )
		return TRUE;

	for( num = 0; num < max; num++ ) {
		name = g_strdup_printf( col_width_format, num );
		width = 0;

		if( cfg_location_get_num( home, name, &width, 0 ) || width == 0 ) {
			g_free( name );
			return TRUE;
		}

		g_free( name );
		gtk_clist_set_column_width( GTK_CLIST( clist ), num, width );
	}

	return FALSE;
}

static void clist_save( GtkWidget *clist, const cfg_location_t *home )
{
	gint i;
	gchar *name;

	g_return_if_fail( GTK_IS_CLIST( clist ) );

	for( i = 0; i < (GTK_CLIST( clist ))->columns; i++ ) {
		name = g_strdup_printf( col_width_format, i );
		cfg_location_put_num( home, name, (GTK_CLIST( clist ))->column[i].width );
		g_free( name );
	}

	cfg_location_put_num( home, col_count_key, i );
}

static gboolean paned_load( GtkWidget *paned, const cfg_location_t *home )
{
	guint32 offset = 0;

	g_return_val_if_fail( GTK_IS_PANED( paned ), TRUE );

	if( cfg_location_get_num( home, offset_key, &offset, 0 ) || offset == 0 )
		return TRUE;

	gtk_paned_set_position( GTK_PANED( paned ), offset );
	return FALSE;
}

static void paned_save( GtkWidget *paned, const cfg_location_t *home )
{
	GtkWidget *child;

	g_return_if_fail( GTK_IS_PANED( paned ) );

	child = (GTK_PANED( paned ))->child1;

	if( child == NULL )
		cfg_location_put_num( home, offset_key, 0 );
	else if( GTK_IS_HPANED( paned ) )
		cfg_location_put_num( home, offset_key, (GTK_WIDGET( child ))->allocation.width );
	else
		cfg_location_put_num( home, offset_key, (GTK_WIDGET( child ))->allocation.height );
}

/* ************************************************************************ */

static gboolean mpclist_swap_out( GtkWidget *current, gpointer dest, gpointer userdata );
static gboolean mpclist_swap_in( GtkWidget *current, gpointer src, gpointer userdata );
static gboolean mpclist_do_default( GtkWidget *current, gpointer userdata );
static gboolean mpclist_save( gpointer swap, const cfg_location_t *home, gpointer userdata );
static gboolean mpclist_load( gpointer swap, const cfg_location_t *home, gpointer userdata );
static GtkWidget *mpclist_get_active( gpointer userdata );

static gboolean mpclist_swap_out( GtkWidget *current, gpointer dest, gpointer userdata )
{
	int i;
	cfg_memory_clist_swapdata_t *sd = (cfg_memory_clist_swapdata_t *) dest;

	sd->num_cols = (GTK_CLIST( current ))->columns;

	if( sd->widths )
		g_free( sd->widths );
	sd->widths = g_new( gint, sd->num_cols );

	for( i = 0; i < sd->num_cols; i++ )
		sd->widths[i] = (GTK_CLIST( current ))->column[i].area.width;

	return FALSE;
}

static gboolean mpclist_swap_in( GtkWidget *current, gpointer src, gpointer userdata )
{
	int i;
	cfg_memory_clist_swapdata_t *sd = (cfg_memory_clist_swapdata_t *) src;

	for( i = 0; i < sd->num_cols; i++ )
		gtk_clist_set_column_width( GTK_CLIST( current ), i, sd->widths[i] );

	return FALSE;
}

static gboolean mpclist_do_default( GtkWidget *current, gpointer userdata )
{
	return mpclist_swap_in( current, userdata, NULL );
}

static gboolean mpclist_save( gpointer swap, const cfg_location_t *home, gpointer userdata )
{
	cfg_memory_clist_swapdata_t *sd = (cfg_memory_clist_swapdata_t *) swap;
	gint i;
	gchar *name;

	cfg_location_put_num( home, col_count_key, sd->num_cols );

	for( i = 0; i < sd->num_cols; i++ ) {
		name = g_strdup_printf( col_width_format, i );
		cfg_location_put_num( home, name, sd->widths[i] );
		g_free( name );
	}

	return FALSE;
}

static gboolean mpclist_load( gpointer swap, const cfg_location_t *home, gpointer userdata )
{
	int i;
	gchar *name;
	cfg_memory_clist_swapdata_t *sd = (cfg_memory_clist_swapdata_t *) swap;

	if( cfg_location_get_num( home, col_count_key, &( sd->num_cols ), 0 ) || sd->num_cols == 0 )
		return TRUE;

	if( sd->widths )
		g_free( sd->widths );
	sd->widths = g_new0( gint, sd->num_cols );

	for( i = 0; i < sd->num_cols; i++ ) {
		gint dest;

		name = g_strdup_printf( col_width_format, i );
		cfg_location_get_num( home, name, &dest, 12 );

		g_assert( sd->widths );
		g_message( "Sanity: %p %p %d", sd, sd->widths, sd->num_cols );
		(sd->widths)[i] = dest;
		g_free( name );
	}

	return FALSE;
}

static GtkWidget *mpclist_get_active( gpointer userdata )
{
	gint page;
	GtkObject *w;

	page = gtk_notebook_get_current_page( GTK_NOTEBOOK( balsa_app.notebook ) );
	if( page == -1 )
		return NULL;

	w = GTK_OBJECT( gtk_notebook_get_nth_page( GTK_NOTEBOOK( balsa_app.notebook ), page ) );
	w = GTK_OBJECT( gtk_object_get_data( GTK_OBJECT( w ), "indexpage" ) );
	return GTK_WIDGET( &( (BALSA_INDEX( (BALSA_INDEX_PAGE( w ))->index ))->clist ) );
}

cfg_memory_plex_t cfg_memory_clist_plexinfo =
{ sizeof( cfg_memory_clist_swapdata_t ), mpclist_swap_out, mpclist_swap_in, mpclist_do_default, mpclist_save, mpclist_load, mpclist_get_active };
