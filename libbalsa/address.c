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
    addr->middle_name = NULL;
    addr->last_name = NULL;
    addr->organization = NULL;
    addr->address_list = NULL;
    addr->member_list = NULL;
}

static void
libbalsa_address_destroy(GtkObject * object)
{
    LibBalsaAddress *addr;

    g_return_if_fail(object != NULL);

    addr = LIBBALSA_ADDRESS(object);

    g_free(addr->id);           addr->id = NULL;
    g_free(addr->full_name);    addr->full_name = NULL;
    g_free(addr->first_name);   addr->first_name = NULL;
    g_free(addr->middle_name);  addr->middle_name = NULL;
    g_free(addr->last_name);    addr->last_name = NULL;
    g_free(addr->organization); addr->organization = NULL;

    g_list_foreach(addr->address_list, (GFunc) g_free, NULL);
    g_list_free(addr->address_list);
    addr->address_list = NULL;

    g_list_foreach(addr->member_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(addr->member_list);
    addr->member_list = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

LibBalsaAddress *
libbalsa_address_new(void)
{
    return gtk_type_new(LIBBALSA_TYPE_ADDRESS);
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
        if (address->mailbox && !address->group) { /* *** For now */
            addr = libbalsa_address_new_from_libmutt(address);
            list = g_list_append(list, addr);
        }
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


static gboolean needs_quotes(const gchar *str)
{
    gboolean quoted = FALSE;

    while (*str) {
        if (*str == '\\') {
            if (*++str)
                ++str;
        } else {
            if (*str == '"')
                quoted = !quoted;
            else if (!quoted
                /* RFC 2822 specials, less '"': */
                 && strchr("()<>[]:;@\\,.", *str))
                return TRUE;
            ++str;
        }
    }
    return FALSE;
}

static gchar *rfc2822_mailbox(const gchar *full_name, gchar *address)
{
    gchar *new_str;

    if(full_name) {
        gchar *dequote = g_new(char, strlen(full_name) + 1);
        const gchar *p = full_name;
        gchar *q = dequote;
    
        do {
            if (*p == '\\') {
                *q++ = *p++;
                if (*p)
                    *q++ = *p++;
            } else if (*p == '"')
                ++p;
            else
                *q++ = *p++;
        } while (*p);
        *q = '\0';

        if (needs_quotes(dequote))
	    new_str = g_strdup_printf("\042%s\042 <%s>", dequote, address);
        else
            new_str = g_strdup_printf("%s <%s>", dequote, address);
        g_free(dequote);
    } else
	new_str = g_strdup(address);
    return new_str;
}


static gchar *rfc2822_group(const gchar *full_name, GList *addr_list)
{
    gchar *tmp_str;
    GString *str = g_string_new("");
    GList *addr_entry;

    if(full_name) { 
	if(needs_quotes(full_name))
	    g_string_sprintf(str, "\042%s\042: ", full_name);
	else
	    g_string_sprintf(str, "%s: ", full_name);
    }

    if(addr_list) {
	tmp_str = libbalsa_address_to_gchar(LIBBALSA_ADDRESS(addr_list->data), 0);
	g_string_append(str, tmp_str);
	g_free(tmp_str);

	for(addr_entry=g_list_next(addr_list); addr_entry; 
	    addr_entry=g_list_next(addr_entry)) {
	    tmp_str = libbalsa_address_to_gchar(LIBBALSA_ADDRESS(addr_entry->data), 0);
	    g_string_sprintfa(str, ", %s", tmp_str);
	    g_free(tmp_str);
	}
    }
    if(full_name)
	g_string_append(str, ";");
    
    tmp_str=str->str;
    g_string_free(str, FALSE);
    
    return tmp_str;
}


static gchar *rfc2822_list(GList *list)
{
    gchar *retc = NULL; 
    GString *str;
    GList *addr_entry;
    
    g_return_val_if_fail(list!=NULL, NULL);

    str=g_string_new((gchar *)list->data);

    for(addr_entry=g_list_next(list); addr_entry; 
	addr_entry=g_list_next(list)) {
	g_string_sprintfa(str, ", %s", (gchar *)addr_entry->data);
    }
    retc=str->str;
    g_string_free(str, FALSE);

    return retc;
}




static LibBalsaAddress *find_address(const gchar *addr_str, 
				     LibBalsaAddress *match_addr)
{
    GList *ab_list;             /* To iterate address books   */
    LibBalsaAddressBook *ab;
    
    /* *** TODO: Search all address books or just the one match_addr 
                 belongs for entries containing addr_str in address list.
                 Return first one that's not a distribution list. */
    return NULL;
}


/* 
   Get a string version of this address.

   If n == -1 then return all addresses, else return the n'th one.
   If n > the number of addresses, will cause an error.
*/
gchar *
libbalsa_address_to_gchar(LibBalsaAddress * address, gint n)
{
    return libbalsa_address_to_gchar_p(address, n);
}
/* private version */
gchar *
libbalsa_address_to_gchar_p(LibBalsaAddress * address, gint n)
{
    gchar *retc = NULL;
    gboolean dist_list = TRUE;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    if(address->member_list || (n==-1 && !address->address_list))
	retc = rfc2822_group(address->full_name, address->member_list);
    else if(n==-1) {
	retc = rfc2822_list(address->address_list);
    } else {
	GList *nth_address;

	nth_address = g_list_nth(address->address_list, n);

	g_return_val_if_fail(nth_address != NULL, NULL);

	retc = rfc2822_mailbox(address->full_name, nth_address->data);
    }
    
    return retc;
}

const gchar *
libbalsa_address_get_name(const LibBalsaAddress * addr)
{
    return addr->full_name ? addr->full_name :
	(addr->address_list ? addr->address_list->data : NULL);
}

#if ENABLE_ESMTP

/* XXX - added by Brian Stafford <brian@stafford.uklinux.net> */

/* libESMTP works with the RFC 821 mailbox and the RFC 822 phrase and 
   mailbox as seperate entities.  Because of this it is useful to add
   these extra methods. */

/* Extract the RFC 822 phrase from the address.  Almost the same
   as libbalsa_address_get_name() except returns NULL if no phrase. */
const gchar *
libbalsa_address_get_phrase(LibBalsaAddress * address)
{
    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    return address->full_name;
}

#endif
/* Extract the nth RFC 821/RFC 822 mailbox from the address. */
const gchar *
libbalsa_address_get_mailbox(LibBalsaAddress * address, gint n)
{
    GList *nth_address;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS(address), NULL);

    nth_address = g_list_nth(address->address_list, n);
    g_return_val_if_fail(nth_address != NULL, NULL);
    return (gchar*)nth_address->data;
}

