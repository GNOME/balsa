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

GdkImlibImage *balsa_init_get_png( gchar *fname );

GdkImlibImage *balsa_init_get_png( gchar *fname )
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
