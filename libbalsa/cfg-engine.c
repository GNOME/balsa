/* Balsa E-Mail Client
 *
 * This file handles Balsa's configuration information.
 *
 * This file is Copyright (C) 1998-1999 Nat Friedman
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

/* The config group helps us write a structure automagically. There is an array of
 * cfg_parm_t's, each one of which represents an element in the structure that we
 * want to record or read. We calculate the address of the element with the data
 * parameter and the offset member of the cfg_parm_t, then call the appropriate
 * function based on the type of the parm.
 *
 * There are certain cases that we want to specially handle: structures within
 * structures, and datatypes that aren't gboolean, gint, or gchar *. In 
 * enumerations, for example, we don't know their size, so the code to write an
 * integer might trash data. For this we have meta-types. 
 *
 * When a meta-type is encountered, the offset is calculated as usual, but a
 * function pointed to by the type's parmdata is called, so that the datatype
 * can be specially handled. An enumeration's metafunctions, for instance,
 * will probably just cast it to an int and write that.
 * 
 * When a metatype is written, a new cfg_location is created for that type
 * so that names do not clash. This can cause depth that gets a bit excessive,
 * but that's okay.
 *
 * There's an important parmdata for metatypes, meta.pointer. If the element
 * of the structure that you're looking at is a pointer, it should be true.
 * If the structure is included directly into the superstructure, it should
 * be false.
 *
 *    struct this_s { struct that *s; };   ---> meta.pointer = TRUE
 *    struct this_s { struct that s; };    ---> meta.pointer = FALSE
 *
 * These functions also check values for incoming data; numbers have ranges,
 * and strings have check_valid functions, although they're not used.
 */

/* ************************************************************************ */

gboolean cfg_group_write( gconstpointer data, const cfg_parm_t *elements, const cfg_location_t *where );
gboolean cfg_group_read( gpointer data, const cfg_parm_t *elements, const cfg_location_t *where );

/* ************************************************************************ */

/* O gets the value of (ptr + offset) cast to "type"; 
 * P gets the address of (ptr + offset) cast to "type *".
 */

#define O( type, ptr, offset ) (*( ((type *) (((guint8 *) (ptr)) + (offset))) ))
#define P( type, ptr, offset ) ( (type *) ( ((guint8 *) (ptr)) + (offset) ) )

/* Loop through all the parameters in elements. Write each one in location where, stopping if
 * true is returned. If the parameter is a metatype, create a sub-location, and call the
 * metawriter. FALSE on success, TRUE on failure.
 */

gboolean cfg_group_write( gconstpointer data, const cfg_parm_t *elements, const cfg_location_t *where )
{
	const cfg_parm_t *iter;
	cfg_location_t *down;

	gconstpointer elem;
	gchar *str;
	gboolean bool;
	gint num;

	gboolean res = TRUE;
	
	for( iter = elements; iter && iter->name; iter = &(iter[1]) ) {
		switch( iter->type ) {
		case CPT_STR:
			str = O( gchar *, data, iter->offset );
			res = cfg_location_put_str( where, iter->name, str );
			break;

		case CPT_BOOL:
			bool = O( gboolean, data, iter->offset );
			res = cfg_location_put_bool( where, iter->name, bool );
			break;

		case CPT_NUM:
			num = O( gint, data, iter->offset );
			res = cfg_location_put_num( where, iter->name, num );
			break;

		case CPT_META:
			if( iter->data.meta.pointer )
				elem = O( gconstpointer, data, iter->offset );
			else
				elem = P( const void, data, iter->offset );


			down = cfg_location_godown( where, iter->name );
			res = (iter->data.meta.spec->write)( elem, down, iter->data.meta.typedata );

			cfg_location_free( down );
			break;

		case CPT_NUM_TYPES:
		default:
			g_warning( "Bad type number in cfg_group_write: \"%s\", %d", iter->name, (int) iter->type );
			break;
		}

		if( res ) {
			g_warning( "Error writing config item \"%s\"!", iter->name );
			return TRUE;
		}
	}

	return FALSE;
}

/* Loop through all the parameters in elements. Fetch each member from location 'where', check its
 * value, and stop if the reader fails. For metatypes, go into the sub-location and call the
 * metareader. FALSE on success, TRUE on failure.
 */

gboolean cfg_group_read( gpointer data, const cfg_parm_t *elements, const cfg_location_t *where )
{
	const cfg_parm_t *iter;
	cfg_location_t *down;
	gchar *dflt;
	gint get;
	gpointer elem;
	gboolean res = TRUE;

	for( iter = elements; iter && iter->name; iter = &(iter[1]) ) {
		switch( iter->type ) {
		case CPT_STR:
			if( iter->data.str.fetch_default ) 
				dflt = (iter->data.str.fetch_default)( iter->name );
			else if( iter->data.str.dflt )
				dflt = g_strdup( iter->data.str.dflt );
			else
				dflt = g_strdup( "" );

			res = cfg_location_get_str( where, iter->name, P( gchar *, data, iter->offset ), dflt );
			g_free( dflt );
			break;

		case CPT_BOOL:
			res = cfg_location_get_bool( where, iter->name, P( gboolean, data, iter->offset ), iter->data.bool.dflt );
			break;

		case CPT_NUM:
			res = cfg_location_get_num( where, iter->name, &get, iter->data.num.dflt );

			if( iter->data.num.flags & CPNF_USEMIN && get < iter->data.num.min )
				get = iter->data.num.min;
			if( iter->data.num.flags & CPNF_USEMAX && get > iter->data.num.max )
				get = iter->data.num.max;

			O( gint, data, iter->offset ) = get;
			break;

		case CPT_META:
			down = cfg_location_godown( where, iter->name );

			if( iter->data.meta.pointer )
				elem = O( gpointer, data, iter->offset );
			else
				elem = P( void, data, iter->offset );

			res = (iter->data.meta.spec->read)( elem, down, iter->data.meta.typedata );

			cfg_location_free( down );
			break;

		case CPT_NUM_TYPES:
		default:
			g_warning( "Bad type number in cfg_group_read: \"%s\", %d", iter->name, (int) iter->type );
			break;
		}

		if( res ) {
			g_warning( "Error writing config item \"%s\"!", iter->name );
			return TRUE;
		}
	}

	return FALSE;
}
