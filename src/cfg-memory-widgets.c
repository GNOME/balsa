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

#include "cfg-backend.h"
#include "cfg-engine.h"

/* ************************************************************************ */

static gboolean window_load( GtkWindow *window, const cfg_location_t *root );
static void window_destroy( GtkWidget *window );
static void window_save( GtkWidget *window, const cfg_location_t *root );

static gboolean clist_load( GtkCList *clist, const cfg_location_t *root );
static void clist_destroy( GtkWidget *clist );
static void clist_save( GtkWidget *clist, const cfg_location_t *root );

/* ************************************************************************ */

void cfg_memory_add_to_window( GtkWindow *window, const cfg_location_t *root, const gchar *name, guint32 default_w, guint32 default_h );
void cfg_memory_add_to_clist( GtkCList *clist, const cfg_location_t *root, const gchar *name, gint numcolumns, ... );
void cfg_memory_write_all( const cfg_location_t *root );
cfg_location_t *cfg_memory_default_root( void );
void cfg_memory_clist_sync_from( GtkCList *clist, const cfg_location_t *root );
void cfg_memory_clist_sync_to( GtkCList *clist, const cfg_location_t *root );

/* ************************************************************************ */

static const gchar object_key[] = "balsa-config-home";
static const gchar width_key[] = "Width";
static const gchar height_key[] = "Height";
static const gchar col_width_format[] = "Column%dWidth";
static const gchar col_count_key[] = "ColumnCount";

static GSList *memory_windows = NULL;
static GSList *memory_clists = NULL;

/* ************************************************************************ */

void cfg_memory_add_to_window( GtkWindow *window, const cfg_location_t *root, const gchar *name, guint32 default_w, guint32 default_h )
{
	g_return_if_fail( window );
	g_return_if_fail( root );
	g_return_if_fail( name );
	g_return_if_fail( GTK_IS_WINDOW( window ) );


	gtk_object_set_data( GTK_OBJECT( window ), object_key, g_strdup( name ) );
	gtk_signal_connect( GTK_OBJECT( window ), "destroy", GTK_SIGNAL_FUNC( window_destroy ), NULL );

	if( window_load( window, root ) ) {
		cfg_location_t *home;

		home = cfg_location_godown( root, name );	
		cfg_location_put_num( home, width_key, default_w );
		cfg_location_put_num( home, height_key, default_h );
		cfg_location_free( home );
		window_load( window, root );
	}

	memory_windows = g_slist_prepend( memory_windows, (gpointer) window );
}

void cfg_memory_add_to_clist( GtkCList *clist, const cfg_location_t *root, const gchar *name, gint numcolumns, ... )
{
	g_return_if_fail( clist );
	g_return_if_fail( root );
	g_return_if_fail( name );
	g_return_if_fail( GTK_IS_CLIST( clist ) );

	gtk_object_set_data( GTK_OBJECT( clist ), object_key, g_strdup( name ) );
	gtk_signal_connect( GTK_OBJECT( clist ), "destroy", GTK_SIGNAL_FUNC( clist_destroy ), NULL );

	if( clist_load( clist, root ) ) {
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

		clist_load( clist, root );
	}

	memory_clists = g_slist_prepend( memory_clists, (gpointer) clist );
}

void cfg_memory_write_all( const cfg_location_t *root )
{
	GSList *iter;

	for( iter = memory_windows; iter; iter = iter->next )
		window_save( GTK_WIDGET( iter->data ), root );

	for( iter = memory_clists; iter; iter = iter->next )
		clist_save( GTK_WIDGET( iter->data ), root );
}

cfg_location_t *cfg_memory_default_root( void )
{
	cfg_location_t *realroot, *myroot;

	realroot = cfg_get_root();
	myroot = cfg_location_godown( realroot, "UISettings" );
	cfg_location_free( realroot );
	return myroot;
}

void cfg_memory_clist_sync_from( GtkCList *clist, const cfg_location_t *root )
{
	clist_load( clist, root );
}

void cfg_memory_clist_sync_to( GtkCList *clist, const cfg_location_t *root )
{
	clist_save( GTK_WIDGET( clist ), root );
}

/* ************************************************************************ */

static gboolean window_load( GtkWindow *window, const cfg_location_t *root )
{
	gchar *name;
	cfg_location_t *home;
	guint32 w = 0, h = 0;

	name = gtk_object_get_data( GTK_OBJECT( window ), object_key );
	g_return_val_if_fail( name, TRUE );
	home = cfg_location_godown( root, name );

	if( cfg_location_get_num( home, width_key, &w, 0 ) || w == 0 ) {
		cfg_location_free( home );
		return TRUE;
	}

	if( cfg_location_get_num( home, height_key, &h, 0 ) || h == 0 ) {
		cfg_location_free( home );
		return TRUE;
	}

	gtk_window_set_default_size( window, w, h );
	cfg_location_free( home );
	return FALSE;
}

static void window_destroy( GtkWidget *window )
{
	cfg_location_t *home;

	g_return_if_fail( GTK_IS_WINDOW( window ) );

	home = gtk_object_get_data( GTK_OBJECT( window ), object_key );
	if( home )
		g_free( home );

	memory_windows = g_slist_remove( memory_windows, window );
}

static void window_save( GtkWidget *window, const cfg_location_t *root )
{
	gchar *name;
	cfg_location_t *home;

	g_return_if_fail( GTK_IS_WINDOW( window ) );

	name = gtk_object_get_data( GTK_OBJECT( window ), object_key );
	g_return_if_fail( name );
	home = cfg_location_godown( root, name );

	cfg_location_put_num( home, width_key, (GTK_WIDGET( window ))->allocation.width );
	cfg_location_put_num( home, height_key, (GTK_WIDGET( window ))->allocation.height );

	cfg_location_free( home );
}

static gboolean clist_load( GtkCList *clist, const cfg_location_t *root )
{
	cfg_location_t *home;
	guint32 num = 0, width = 0, max = 0;
	gchar *name;

	name = gtk_object_get_data( GTK_OBJECT( clist ), object_key );
	g_return_val_if_fail( name, TRUE );
	home = cfg_location_godown( root, name );

	if( cfg_location_get_num( home, col_count_key, &max, 0 ) || max == 0 ) {
		cfg_location_free( home );
		return TRUE;
	}

	for( num = 0; num < max; num++ ) {
		name = g_strdup_printf( col_width_format, num );
		width = 0;

		if( cfg_location_get_num( home, name, &width, 0 ) || width == 0 ) {
			g_free( name );
			cfg_location_free( home );
			return TRUE;
		}

		g_free( name );
		gtk_clist_set_column_width( clist, num, width );
	}

	cfg_location_free( home );
	return FALSE;
}

static void clist_destroy( GtkWidget *clist )
{
	cfg_location_t *home;

	g_return_if_fail( GTK_IS_CLIST( clist ) );

	home = gtk_object_get_data( GTK_OBJECT( clist ), object_key );
	if( home )
		cfg_location_free( home );

	memory_clists = g_slist_remove( memory_clists, clist );
}

static void clist_save( GtkWidget *clist, const cfg_location_t *root )
{
	cfg_location_t *home;
	gint i;
	gchar *name;

	g_return_if_fail( GTK_IS_CLIST( clist ) );

	name = gtk_object_get_data( GTK_OBJECT( clist ), object_key );
	g_return_if_fail( name );
	home = cfg_location_godown( root, name );

	for( i = 0; i < (GTK_CLIST( clist ))->columns; i++ ) {
		name = g_strdup_printf( col_width_format, i );
		cfg_location_put_num( home, name, (GTK_CLIST( clist ))->column[i].width );
		g_free( name );
	}

	cfg_location_free( home );
}
