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

/* ********************************************************************** */

#include "config.h"
#include <gnome.h>
#include <string.h> /*strncmp*/
#include "cfg-backend.h"

/* Our config system: we view the configuration system essentially as a
 * filesystem, with a root section, and nestible children below it. Each
 * section can contain subsections and keys.
 *
 * All operations are relative to a 'cfg_location_t', which represents the
 * equivalent of a directory handle -- we can create child sections of a
 * location, and put keys in a location, but only relative to that location.
 *
 * Locations support some basic operations: dup, cmp, free, put_type, get_type, 
 * exists, iterate, and godown. The only ambiguous one, godown, creates or 
 * opens a subsection of the current location. There is no corresponding goup,
 * because there shouldn't ever be a need to know what location you're below. 
 * If you think you need this, you're probably doing something wrong.
 *
 * Other ops include the 'get_root', which gives us the toplevel section
 * from which all other sections spring, and 'sync', which syncronizes the
 * config information.
 */

/* gnome-config, our current backend, cannot handle nesting sections. This
 * is really important to the config system, so we fake it.
 *
 * The trick is that to go into a subsection, we just splice the two section
 * names together. This is just like file paths. If we have one location
 * that is section 'Bar' under section 'Foo', it will currently be 
 * represented as 'Foo---Bar'. Then, if we want section 'Baz' under that,
 * the splicing yields 'Foo---Bar---Baz'.
 *
 * Things to remember:
 *    -- The root section must be handled specially. We give it a fake
 *       section name, but remember not to put subsections of root under
 *       that fake section name, because that would be dumb.
 *    -- When iterating through a section, we must match subsections
 *       that are immediate children of it, but not children of children.
 *    -- When deleting a section, we nuke all of its keys and subsections,
 *       even non-immediate children.
 */

/* ********************************************************************** */

/* SUBSECTSEP - subsection separator. This spaces out location names in our
 *     pseudo-depth system.
 * SSSLEN -- length of the subsection separator string
 * SECTROOT -- the fake section name we use for variables in the root section
 */

#define SUBSECTSEP "---"
#define SSSLEN 3
#define SECTROOT "__balsa_root__"

/* prefix -- the filename or appname for gnome-config, something like "/balsa"
 *     or "=/path/to/file=".
 *  section -- the section in the config file that we represent.
 */
struct cfg_location_s {
	gchar *prefix;
	gchar *section;
};

/* ********************************************************************** */

/* makepath uilds a gnome-config path out of a location and a keyname,
 * while makedpath makes one with a default specified. Return results
 * must be g_freed.
 */

static gchar *makepath( const cfg_location_t *where, const gchar *name );
static gchar *makedpath( const cfg_location_t *where, const gchar *name, const gchar *dflt );

/* ********************************************************************** */

cfg_location_t *cfg_get_root( void );
cfg_location_t *cfg_location_godown( const cfg_location_t *where, const gchar *name );

cfg_location_t *cfg_location_dup( const cfg_location_t *where );
gboolean cfg_location_cmp( const cfg_location_t *left, const cfg_location_t *right );

gboolean cfg_location_put_num( const cfg_location_t *where, const gchar *name, const gint value );
gboolean cfg_location_put_str( const cfg_location_t *where, const gchar *name, const gchar *value );
gboolean cfg_location_put_bool( const cfg_location_t *where, const gchar *name, const gboolean value );

gboolean cfg_location_get_num( const cfg_location_t *where, const gchar *name, gint *value, const gint dflt );
gboolean cfg_location_get_str( const cfg_location_t *where, const gchar *name, gchar **value, const gchar *dflt );
gboolean cfg_location_get_bool( const cfg_location_t *where, const gchar *name, gboolean *value, const gboolean dflt );

gboolean cfg_location_del_key( const cfg_location_t *where, const gchar *name );
gboolean cfg_location_del( const cfg_location_t *where );

void cfg_location_enumerate( const cfg_location_t *where, cfg_enum_callback cb, gpointer user_data );
gboolean cfg_location_exists( const cfg_location_t *where );

void cfg_location_free( cfg_location_t *where );

/* ********************************************************************** */

static gchar *makepath( const cfg_location_t *where, const gchar *name )
{
	if( where->section == NULL )
		return g_strconcat( where->prefix, "/" SECTROOT "/", name, NULL );
	return g_strconcat( where->prefix, "/", where->section, "/", name, NULL );
}

static gchar *makedpath( const cfg_location_t *where, const gchar *name, const gchar *dflt )
{
	if( where->section == NULL )
		return g_strconcat( where->prefix, "/" SECTROOT "/", name, "=", dflt, NULL );
	return g_strconcat( where->prefix, "/", where->section, "/", name, "=", dflt, NULL );
}

/* ********************************************************************** */

/* Get the toplevel location */
cfg_location_t *cfg_get_root( void )
{
	cfg_location_t *loc = g_new( cfg_location_t, 1 );

	loc->prefix = g_strdup( "/balsa" );
	loc->section = NULL;
	return loc;
}

/* Go down a level to the specified child. Don't create it if it doesn't exist. */
cfg_location_t *cfg_location_godown( const cfg_location_t *where, const gchar *name )
{
	cfg_location_t *loc = g_new( cfg_location_t, 1 );

	loc->prefix = g_strdup( where->prefix );

	if( where->section )
		loc->section = g_strconcat( where->section, SUBSECTSEP, name, NULL );
	else
		loc->section = g_strdup( name );

	return loc;
}

/* Duplicate the input */
cfg_location_t *cfg_location_dup( const cfg_location_t *where )
{
	cfg_location_t *loc = g_new( cfg_location_t, 1 );

	loc->prefix = g_strdup( where->prefix );

	if( where->section )
		loc->section = g_strdup( where->section );
	else
		loc->section = NULL;

	return loc;
}

/* Return FALSE if left and right are the same, TRUE if not. */
gboolean cfg_location_cmp( const cfg_location_t *left, const cfg_location_t *right )
{
	if( strcmp( left->prefix, right->prefix ) )
		return TRUE;

	if( left->section == NULL ) {
		if( right->section )
			return TRUE;
		return FALSE;
	} else {
		if( right->section == NULL )
			return TRUE;
	}

	if( strcmp( left->section, right->section ) )
		return TRUE;

	return FALSE;
}

/* Write a gint, valued value, keyname name, in section where. Create sections
 * as needed to create the key as needed. Return TRUE on error, FALSE on success.
 */
gboolean cfg_location_put_num( const cfg_location_t *where, const gchar *name, const gint value )
{
	gchar *prefix = makepath( where, name );
	gnome_config_set_int( prefix, value );
	g_free( prefix );
	return FALSE;
}

/* Like above with strings. */
gboolean cfg_location_put_str( const cfg_location_t *where, const gchar *name, const gchar *value )
{
	gchar *prefix = makepath( where, name );
	gnome_config_set_string( prefix, value );
	g_free( prefix );
	return FALSE;
}

/* Like above with gboolean. */
gboolean cfg_location_put_bool( const cfg_location_t *where, const gchar *name, const gboolean value )
{
	gchar *prefix = makepath( where, name );
	gnome_config_set_bool( prefix, value );
	g_free( prefix );
	return FALSE;
}

/* Read a gint, placed into value, with default dflt, keyname name, in config section where.
 * Return FALSE if successfully read or defaulted, return TRUE on a condition where they
 * key could not be read, created, and defaulted.
 */
gboolean cfg_location_get_num( const cfg_location_t *where, const gchar *name, gint *value, const gint dflt )
{
	gchar *sd = g_strdup_printf( "%d", dflt );
	gchar *prefix = makedpath( where, name, sd );
	(*value) = gnome_config_get_int( prefix );
	g_free( prefix );
	g_free( sd );
	return FALSE;
}

/* Like about with strings. */
gboolean cfg_location_get_str( const cfg_location_t *where, const gchar *name, gchar **value, const gchar *dflt )
{
	gchar *sd = g_strdup_printf( "%s", dflt );
	gchar *prefix = makedpath( where, name, sd );
	(*value) = g_strdup( gnome_config_get_string( prefix ) );
	g_free( prefix );
	g_free( sd );
	return FALSE;
}

/* Like above with gbooleans. */
gboolean cfg_location_get_bool( const cfg_location_t *where, const gchar *name, gboolean *value, const gboolean dflt )
{
	gchar *sd;
	gchar *prefix;

	if( dflt )
		sd = "true";
	else
		sd = "false";

	prefix = makedpath( where, name, sd );
	(*value) = gnome_config_get_bool( prefix );
	g_free( prefix );

	return FALSE;
}

/* Delete key named name in section where. TRUE on failure, FALSE on success. */
gboolean cfg_location_del_key( const cfg_location_t *where, const gchar *name )
{
	gchar *prefix = makepath( where, name );
	gnome_config_clean_key( prefix );
	g_free( prefix );
	return FALSE;
}

/* Delete the section that location represents, and all its children. 
 * FALSE on success, TRUE on failure.
 */
gboolean cfg_location_del( const cfg_location_t *where )
{
	gchar *prefix;
	char *key, *val;
	gpointer iter;
	int len;

	if( ! where->section ) {
		g_warning( "Cannot delete root section in cfg_location_del!" );
		return TRUE;
	}

	iter = gnome_config_init_iterator_sections( where->prefix );
	len = strlen( where->section );

	while( (iter = gnome_config_iterator_next( iter, &key, &val )) != NULL ) {
		if( strncmp( where->section, key, len ) )
			continue;

		prefix = g_strconcat( where->prefix, "/", key, NULL );
		gnome_config_clean_section( prefix );
		g_free( prefix );
	}

	return FALSE;
}

/* List the keys and subsections in section 'where'. Call callback with arguments: the
 * parent location, the name of the key or section, a gboolean TRUE if the child is
 * a section, and the user_data parameter. 
 */
void cfg_location_enumerate( const cfg_location_t *where, cfg_enum_callback cb, gpointer user_data )
{
	int len;
	gchar *section;
	gchar *prefix;
	char *key, *val;
	gpointer iter;
	
	if( where->section )
		section = where->section;
	else
		section = SECTROOT;

	prefix = g_strconcat( where->prefix, "/", section, NULL );
	iter = gnome_config_init_iterator( prefix );
	g_free( prefix );

	while( (iter = gnome_config_iterator_next( iter, &key, &val )) != NULL )
		cb( where, key, FALSE, user_data );

	iter = gnome_config_init_iterator_sections( where->prefix );

	if( where->section ) {
		len = strlen( where->section );
		while( (iter = gnome_config_iterator_next( iter, &key, &val )) != NULL ) {
			if( strcmp( key, where->section ) == 0 )
				continue;
			if( strncmp( key, where->section, len ) )
				continue;
			if( strncmp( &( key[len] ), SUBSECTSEP, SSSLEN ) )
				continue;
			if( strstr( &( key[len+SSSLEN] ), SUBSECTSEP ) )
				continue;
			cb( where, &( key[len+SSSLEN] ), TRUE, user_data );
		}
	} else {
		while( (iter = gnome_config_iterator_next( iter, &key, &val )) != NULL ) {
			if( strcmp( key, SECTROOT ) == 0 )
				continue;
			if( strstr( key, SUBSECTSEP ) )
				continue;
			cb( where, key, TRUE, user_data );
		}
	}
}

/* Return TRUE if the location represented by where actually
 * exists. FALSE otherwise.
 */
gboolean cfg_location_exists( const cfg_location_t *where )
{
	gchar *prefix;
	gboolean val;

	/* The root always exists. */

	if( where->section == NULL )
		return TRUE;

	prefix = g_strconcat( where->prefix, "/", where->section, NULL );

	val = gnome_config_has_section( prefix );
	g_free( prefix );
	return val;
}

/* Free the memory for 'where'. Doesn't affect the
 * config system! 
 */
void cfg_location_free( cfg_location_t *where )
{
	if( where->prefix )
		g_free( where->prefix );
	if( where->section )
		g_free( where->section );
	g_free( where );
}

/* Only used internally for the gnome-config subsystem 
*/
cfg_location_t *cfg_location_from_gnome_prefix( const gchar *prefix )
{
	cfg_location_t *loc = g_new( cfg_location_t, 1 );
	guint32 len;

	loc->prefix = g_strdup( prefix );
	loc->section = NULL;

	len = strlen( prefix );
	if( loc->prefix[len-1] == '/' )
		loc->prefix[len-1] = '\0';

	return loc;
}

/* Discard changes made below a location 
 */
gboolean cfg_location_discard( const cfg_location_t *loc )
{
	if( loc->section ) {
		g_warning( "Discarding of section information not supported by gnome-config!" );
		return TRUE;
	}

	gnome_config_drop_file( loc->prefix );
	return FALSE;
}
