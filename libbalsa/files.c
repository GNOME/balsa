/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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

static const gchar *permanent_prefixes[] =
{
/*	BALSA_DATA_PREFIX,
	BALSA_STD_PREFIX,
	GNOME_DATA_PREFIX
	GNOME_STD_PREFIX,
	GNOME_LIB_PREFIX,*/
	BALSA_COMMON_PREFIXES
	NULL
};

gchar *balsa_file_finder( const gchar *filename, const gchar *splice, const gchar **prefixes );

/* filename is the filename (naw!)
 * splice is what to put in between the prefix and the filename, if desired
 * prefixes is a null-termed array of strings of prefixes to try. There are defaults that are always
 *   tried.
 * We ignore proper slashing of names. Ie, /prefix//splice//file won't be caught.
 */

gchar *balsa_file_finder( const gchar *filename, const gchar *splice, const gchar **prefixes )
{
	gchar *cat;
	int i;
	
	g_return_val_if_fail( filename, NULL );

	if( splice == NULL )
		splice = "";

	for( i = 0; permanent_prefixes[i]; i++ ) {
		cat = g_strconcat( permanent_prefixes[i], PATH_SEP_STR, splice, PATH_SEP_STR, filename, NULL );
		
		if( g_file_exists( cat ) )
			return cat;

		g_free( cat );
	}

	if( prefixes == NULL ) {
		g_warning( "Cannot find expected file \"%s\" (spliced with \"%s\") with no extra prefixes", filename, splice );
		return NULL;
	}

	for( i = 0; prefixes[i]; i++ ) {
		cat = g_strconcat( prefixes[i], PATH_SEP_STR, splice, PATH_SEP_STR, filename, NULL );
		
		if( g_file_exists( cat ) )
			return cat;

		g_free( cat );
	}

	g_warning( "Cannot find expected file \"%s\" (spliced with \"%s\") even with extra prefixes", filename, splice );
	return NULL;
}
