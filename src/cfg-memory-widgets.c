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

typedef struct generic_memory_s {
	const gchar *name;
	GSList **list;
} generic_memory_t;

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

/* ************************************************************************ */

void cfg_memory_add_to_window( GtkWidget *window, const cfg_location_t *root, const gchar *name, guint32 default_w, guint32 default_h );
void cfg_memory_add_to_clist( GtkWidget *clist, const cfg_location_t *root, const gchar *name, gint numcolumns, ... );
void cfg_memory_add_to_paned( GtkWidget *paned, const cfg_location_t *root, const gchar *name, gint offset );

void cfg_memory_write_all( const cfg_location_t *root );
cfg_location_t *cfg_memory_default_root( void );

void cfg_memory_clist_sync_from( GtkWidget *clist, const cfg_location_t *root );
void cfg_memory_clist_sync_to( GtkWidget *clist, const cfg_location_t *root );

/* ************************************************************************ */

static const gchar object_key[] = "balsa-config-home";
static const gchar width_key[] = "Width";
static const gchar height_key[] = "Height";
static const gchar col_width_format[] = "Column%dWidth";
static const gchar col_count_key[] = "ColumnCount";
static const gchar offset_key[] = "Offset";

static GSList *memory_windows = NULL;
static GSList *memory_clists = NULL;
static GSList *memory_paneds = NULL;

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

	for( iter = memory_windows; iter; iter = iter->next )
		generic_save( GTK_WIDGET( iter->data ), root, window_save );

	for( iter = memory_clists; iter; iter = iter->next )
		generic_save( GTK_WIDGET( iter->data ), root, clist_save );

	for( iter = memory_paneds; iter; iter = iter->next )
		generic_save( GTK_WIDGET( iter->data ), root, paned_save );
}

cfg_location_t *cfg_memory_default_root( void )
{
	cfg_location_t *realroot, *myroot;

	realroot = cfg_get_root();
	myroot = cfg_location_godown( realroot, "UISettings" );
	cfg_location_free( realroot );
	return myroot;
}

void cfg_memory_clist_sync_from( GtkWidget *clist, const cfg_location_t *root )
{
	generic_load( clist, root, clist_load );
	cfg_sync();
}

void cfg_memory_clist_sync_to( GtkWidget *clist, const cfg_location_t *root )
{
	generic_save( clist, root, clist_save );
	cfg_sync();
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

