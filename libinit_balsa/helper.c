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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "helper.h"
#include "balsa-druid-page.h"

#ifdef BALSA_LOCAL_INSTALL
#define gnome_pixmap_file( s ) g_strdup( g_strconcat( BALSA_RESOURCE_PREFIX, "/pixmaps/", s, NULL ) )
#define gnome_unconditional_pixmap_file( s ) g_strdup( g_strconcat( BALSA_RESOURCE_PREFIX, "/pixmaps", s, NULL ) )
#endif

/* ************************************************************************** */

GdkImlibImage *balsa_init_get_png( const gchar *fname );
void           balsa_init_add_table_entry( GtkTable *table, guint num, gchar *ltext, gchar *etext, EntryData *ed, GtkWidget *page, GtkWidget **dest );
gboolean       balsa_init_create_to_directory( const gchar *dir, gchar **complaint );

static void entry_changed_cb( GtkEntry *entry );

/* ************************************************************************** */

GdkImlibImage *balsa_init_get_png( const gchar *fname )
{
    gchar *fullname, *fullpath;
    GdkImlibImage *img;

    g_return_val_if_fail( fname != NULL, NULL );

    fullname = g_strconcat( "balsa/", fname, NULL );
    fullpath = gnome_pixmap_file( fullname );
    g_free( fullname );
    
    if( !fullpath )
	return NULL;

    img = gdk_imlib_load_image( fullpath );
    g_free( fullpath );
    
    return img;
}

void balsa_init_add_table_entry( GtkTable *table, guint num, gchar *ltext, gchar *etext, EntryData *ed, GtkWidget *page, GtkWidget **dest )
{
	GtkWidget *w;

	ed->num = num;
	ed->page = page;

	w = gtk_label_new( ltext );
	gtk_label_set_justify( GTK_LABEL( w ), GTK_JUSTIFY_RIGHT );
	gtk_misc_set_alignment( GTK_MISC( w ), 1.0, 0.5 );
	gtk_table_attach( table, GTK_WIDGET( w ), 0, 1, num + 1, num + 2,
		GTK_FILL, GTK_FILL,
		8, 4 );

	w = gtk_entry_new();
	gtk_object_set_user_data( GTK_OBJECT( w ), ed );
	gtk_entry_set_text( GTK_ENTRY( w ), etext );
	gtk_signal_connect( GTK_OBJECT( w ), "changed", entry_changed_cb, NULL );
	gtk_table_attach( table, GTK_WIDGET( w ), 1, 2, num + 1, num + 2,
		GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL,
		8, 4 );

	if( etext && etext[0] != '\0' )
		ed->master->setbits |= (1 << num);

	(*dest) = w;
	ed->master->numentries++;
	ed->master->donemask = (ed->master->donemask << 1) | 1;
}

static void entry_changed_cb( GtkEntry *entry )
{
	EntryData *ed = gtk_object_get_user_data( GTK_OBJECT( entry ) );

	g_assert( ed != NULL );
	
	if( entry->text_length ) {
		ed->master->setbits |= (1 << ed->num);
	} else {
		ed->master->setbits &= ~(1 << ed->num);
	}

	/* The stuff below is only when we are displayed... which is not
	 * always the case.
	 */
	if( !GTK_WIDGET_VISIBLE( GTK_WIDGET( entry ) ) )
		return;

	if( BALSA_IS_DRUID_PAGE( ed->page ) ) {
		/* Don't let them continue unless all entries have something. */
		if( ENTRY_MASTER_P_DONE( ed->master ) ) {
			gnome_druid_set_buttons_sensitive( GNOME_DRUID( (BALSA_DRUID_PAGE( ed->page ))->druid ), TRUE, TRUE, TRUE );
		} else {
			gnome_druid_set_buttons_sensitive( GNOME_DRUID( (BALSA_DRUID_PAGE( ed->page ))->druid ), TRUE, FALSE, TRUE );
		}
	}
}

gboolean balsa_init_create_to_directory( const gchar *dir, gchar **complaint )
{
    /* Security. Well, we could create some weird directories, but
       a) that's not very destructive and b) unless we have root
       privileges (which would be so, so, wrong) we can't do any
       damage. */
    struct stat sb;
    gchar *sofar;
    guint32 i;

    if( dir[0] != '/' ) {
	(*complaint) = g_strdup_printf( _("The path %s must be relative to the filesystem root (start with /)."), dir );
	return TRUE;
    }

    for( i = 1; dir[i] != '\0'; i++ ) {
	if( dir[i] == '/' ) {
	    sofar = g_strndup( dir, i );

	    if( stat( sofar, &sb ) < 0 ) {
		if( mkdir( sofar, S_IRUSR | S_IWUSR | S_IXUSR ) < 0 ) {
		    (*complaint) = g_strdup_printf( _("Couldn't create a directory: mkdir() failed on pathname \"%s\"."), sofar );
		    g_free( sofar );
		    return TRUE;
		}
	    }

	    if( !S_ISDIR( sb.st_mode ) ) {
		(*complaint) = g_strdup_printf( _("The file with pathname \"%s\" is not a directory."), sofar );
		g_free( sofar );
		return TRUE;
	    }

	    g_free( sofar );
	}
    }
	
    if( stat( dir, &sb ) < 0 ) {
	if( mkdir( dir, S_IRUSR | S_IWUSR | S_IXUSR ) < 0 ) {
	    (*complaint) = g_strdup_printf( _("Couldn't create a directory: mkdir() failed on pathname \"%s\"."), dir );
	    return TRUE;
	}
    }

    if( !S_ISDIR( sb.st_mode ) ) {
	(*complaint) = g_strdup_printf( _("The file with pathname \"%s\" is not a directory."), dir );
	return TRUE;
    }

    return FALSE;
}
