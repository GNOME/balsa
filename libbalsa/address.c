/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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

#include <glib.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

static GtkObjectClass *parent_class;

static void libbalsa_address_class_init(LibBalsaAddressClass * klass);
static void libbalsa_address_init(LibBalsaAddress * ab);
static void libbalsa_address_destroy(GtkObject * object);

GtkType libbalsa_address_get_type(void)
{
    static GtkType address_type = 0;

    if (!address_type) {
	static const GtkTypeInfo address_info = {
	    "LibBalsaAddress",
	    sizeof(LibBalsaAddress),
	    sizeof(LibBalsaAddressClass),
	    (GtkClassInitFunc) libbalsa_address_class_init,
	    (GtkObjectInitFunc) libbalsa_address_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	address_type =
	    gtk_type_unique(gtk_object_get_type(), &address_info);
    }

    return address_type;
}

static void
libbalsa_address_class_init(LibBalsaAddressClass * klass)
{
    GtkObjectClass *object_class;

    parent_class = gtk_type_class(gtk_object_get_type());

    object_class = GTK_OBJECT_CLASS(klass);
    object_class->destroy = libbalsa_address_destroy;
}

static void
libbalsa_address_init(LibBalsaAddress * addr)
{
    addr->id = NULL;
    addr->full_name = NULL;
    addr->first_name = NULL;
    addr->last_name = NULL;
    addr->organization = NULL;
    addr->address_list = NULL;
}

static void
libbalsa_address_destroy(GtkObject * object)
{
    LibBalsaAddress *addr;

    g_return_if_fail(object != NULL);

    addr = LIBBALSA_ADDRESS(object);

    g_free(addr->id);
    addr->id = NULL;

    g_free(addr->full_name);
    addr->full_name = NULL;

    g_free(addr->first_name);
    addr->first_name = NULL;
    g_free(addr->last_name);
    addr->last_name = NULL;

    g_free(addr->organization);
    addr->organization = NULL;

    g_list_foreach(addr->address_list, (GFunc) g_free, NULL);
    g_list_free(addr->address_list);
    addr->address_list = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

LibBalsaAddress *
libbalsa_address_new(void)
{
    LibBalsaAddress *address;

    address = gtk_type_new(LIBBALSA_TYPE_ADDRESS);

    return address;
}

/* returns only first address on the list; ignores remaining ones */
LibBalsaAddress *
libbalsa_address_new_from_string(gchar * str)
{
    ADDRESS *address = NULL;
    LibBalsaAddress *addr = NULL;

    libbalsa_lock_mutt();
    address = rfc822_parse_adrlist(address, str);
    addr = libbalsa_address_new_from_libmutt(address);
    rfc822_free_address(&address);
    libbalsa_unlock_mutt();

    return addr;
}

GList *
libbalsa_address_new_list_from_string(gchar * the_str)
{
    ADDRESS *address = NULL;
    LibBalsaAddress *addr = NULL;
    GList *list = NULL;

    libbalsa_lock_mutt();
    address = rfc822_parse_adrlist(address, the_str);

    while (address) {
	addr = libbalsa_address_new_from_libmutt(address);
	list = g_list_append(list, addr);
	address = address->next;
    }
    rfc822_free_address(&address);
    libbalsa_unlock_mutt();

    return list;
}

LibBalsaAddress *
libbalsa_address_new_from_libmutt(ADDRESS * caddr)
{
    LibBalsaAddress *address;

    if (!caddr || (caddr->personal==NULL && caddr->mailbox==NULL))
	return NULL;

    address = libbalsa_address_new();

    address->full_name = g_strdup(caddr->personal);
    if (caddr->mailbox)
	address->address_list = g_list_append(address->address_list,
					      g_strdup(caddr->mailbox));

    return address;
}

/* 
   Get a string version of this address.

   If n == -1 then return all addresses, else return the n'th one.
   If n > the number of addresses, will cause an error.
*/
gchar *
libbalsa_address_to_gchar(LibBalsaAddress * address, gint n)
{
    gchar *retc = NULL;
    gchar *address_string;
    GList *nth_address;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    if ( n == -1 ) {
	GString *str = NULL;

	nth_address = address->address_list;
	while ( nth_address ) {
	    address_string = (gchar *)nth_address->data;
	    if ( str )
		g_string_sprintfa(str, ", %s", address_string);
	    else
		str = g_string_new(address_string);
	    nth_address = g_list_next(nth_address);
	}

	if ( str ) {
	    retc = str->str;
	    g_string_free(str, FALSE);
	} else { 
	    retc = NULL;
	}
    } else {
	nth_address = g_list_nth(address->address_list, n);
	g_return_val_if_fail(nth_address != NULL, NULL);

	address_string = (gchar*)nth_address->data;

	if (address->full_name) {
	    retc = g_strdup_printf("%s <%s>", address->full_name, address_string);
	} else {
	    retc = g_strdup(address_string);
	}
    }
    
    return retc;
}

const gchar *
libbalsa_address_get_name(const LibBalsaAddress * addr)
{
    return addr->full_name ? addr->full_name :
	(addr->address_list ? addr->address_list->data : NULL);
}
