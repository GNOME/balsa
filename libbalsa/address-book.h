/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_H__
#define __LIBBALSA_ADDRESS_BOOK_H__

#include "address.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK			(libbalsa_address_book_get_type())
G_DECLARE_DERIVABLE_TYPE(LibBalsaAddressBook, libbalsa_address_book, LIBBALSA, ADDRESS_BOOK, GObject)

typedef enum {
    LBABERR_OK = 0,
    LBABERR_CANNOT_READ,
    LBABERR_CANNOT_WRITE,
    LBABERR_CANNOT_CONNECT,
    LBABERR_CANNOT_SEARCH,
    LBABERR_DUPLICATE,
    LBABERR_ADDRESS_NOT_FOUND
} LibBalsaABErr;

typedef LibBalsaABErr (*LibBalsaAddressBookLoadFunc)(LibBalsaAddressBook *ab,
                                                     LibBalsaAddress *address,
                                                     gpointer closure);

struct _LibBalsaAddressBookClass {
    GObjectClass parent;

    LibBalsaABErr (*load) (LibBalsaAddressBook * ab,
                           const gchar *filter,
                           LibBalsaAddressBookLoadFunc callback,
                           gpointer closure);

    /* adds given address to the address book, updating the permanent
     * storage. */
    LibBalsaABErr (*add_address) (LibBalsaAddressBook * ab,
                                  LibBalsaAddress * address);

    /* remove given address to the address book, updating the permanent
     * storage. */
    LibBalsaABErr (*remove_address) (LibBalsaAddressBook * ab,
                                     LibBalsaAddress * address);

    /* Sets new entries for given address, copying fields from newval.
     * Updates the permanent storage. */
    LibBalsaABErr (*modify_address) (LibBalsaAddressBook * ab,
                                     LibBalsaAddress * address,
                                     LibBalsaAddress * newval);

    void (*save_config) (LibBalsaAddressBook * ab, const gchar * prefix);
    void (*load_config) (LibBalsaAddressBook * ab, const gchar * prefix);

    GList* (*alias_complete) (LibBalsaAddressBook * ab, const gchar *prefix);
};

LibBalsaAddressBook *libbalsa_address_book_new_from_config(const gchar *
							   prefix);

/*
  This will call the callback function once for each address in the
  address book.  The recipient should make sure to ref the address if
  they will be keeping a reference to it around. The callback may
  occur asynchronously.
  
  After all addresses are loaded the callback will be called with
  address==NULL.  
*/
LibBalsaABErr libbalsa_address_book_load(LibBalsaAddressBook * ab,
                                         const char *filter,
                                         LibBalsaAddressBookLoadFunc callback,
                                         gpointer closure);

LibBalsaABErr libbalsa_address_book_add_address(LibBalsaAddressBook *ab,
                                                LibBalsaAddress *address);
LibBalsaABErr libbalsa_address_book_remove_address(LibBalsaAddressBook *ab,
                                                   LibBalsaAddress *address);
LibBalsaABErr libbalsa_address_book_modify_address(LibBalsaAddressBook *ab,
                                                   LibBalsaAddress *address,
                                                   LibBalsaAddress *newval);

void libbalsa_address_book_set_status(LibBalsaAddressBook * ab, const gchar *str);
void libbalsa_address_book_save_config(LibBalsaAddressBook * ab,
				       const gchar * prefix);
void libbalsa_address_book_load_config(LibBalsaAddressBook * ab,
				       const gchar * prefix);

const gchar* libbalsa_address_book_strerror(LibBalsaAddressBook * ab,
					    LibBalsaABErr err);

/*

 Returns a list of InternetAddress objects. The caller is responsible
 for unref()ing these address objects when it is finished with them
 and for freeing the list.

*/
GList *libbalsa_address_book_alias_complete(LibBalsaAddressBook * ab,
					    const gchar *prefix);

/*
 * Getters
 */
gboolean libbalsa_address_book_get_dist_list_mode(LibBalsaAddressBook *ab);
gboolean libbalsa_address_book_get_expand_aliases(LibBalsaAddressBook *ab);
gboolean libbalsa_address_book_get_is_expensive(LibBalsaAddressBook *ab);
const gchar *libbalsa_address_book_get_name(LibBalsaAddressBook *ab);
const gchar *libbalsa_address_book_get_config_prefix(LibBalsaAddressBook *ab);

/*
 * Setters
 */
void libbalsa_address_book_set_dist_list_mode(LibBalsaAddressBook *ab, gboolean dist_list_mode);
void libbalsa_address_book_set_expand_aliases(LibBalsaAddressBook *ab, gboolean expand_aliases);
void libbalsa_address_book_set_is_expensive(LibBalsaAddressBook *ab, gboolean is_expensive);
void libbalsa_address_book_set_name(LibBalsaAddressBook *ab, const gchar *name);


#endif

