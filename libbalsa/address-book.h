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

#ifndef __LIBBALSA_ADDRESS_BOOK_H__
#define __LIBBALSA_ADDRESS_BOOK_H__

#include <gtk/gtkobject.h>

#include "address.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK			(libbalsa_address_book_get_type())
#define LIBBALSA_ADDRESS_BOOK(obj)			(GTK_CHECK_CAST (obj, LIBBALSA_TYPE_ADDRESS_BOOK, LibBalsaAddressBook))
#define LIBBALSA_ADDRESS_BOOK_CLASS(klass)		(GTK_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS_BOOK, LibBalsaAddressBookClass))
#define LIBBALSA_IS_ADDRESS_BOOK(obj)			(GTK_CHECK_TYPE (obj, LIBBALSA_TYPE_ADDRESS_BOOK))
#define LIBBALSA_IS_ADDRESS_BOOK_CLASS(klass)		(GTK_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS_BOOK))

typedef struct _LibBalsaAddressBook LibBalsaAddressBook;
typedef struct _LibBalsaAddressBookClass LibBalsaAddressBookClass;

struct _LibBalsaAddressBook {
    GtkObject parent;

    /* The gnome_config prefix where we save this address book */
    gchar *config_prefix;

    gchar *name;
    gboolean expand_aliases;

    GList *address_list;
};

struct _LibBalsaAddressBookClass {
    GtkObjectClass parent;

    void (*load) (LibBalsaAddressBook * ab);
    void (*store_address) (LibBalsaAddressBook * ab,
			   LibBalsaAddress * address);

    void (*save_config) (LibBalsaAddressBook * ab, const gchar * prefix);
    void (*load_config) (LibBalsaAddressBook * ab, const gchar * prefix);
};

GtkType libbalsa_address_book_get_type(void);

LibBalsaAddressBook *libbalsa_address_book_new_from_config(const gchar *
							   prefix);

void libbalsa_address_book_load(LibBalsaAddressBook * ab);
void libbalsa_address_book_store_address(LibBalsaAddressBook * ab,
					 LibBalsaAddress * address);

void libbalsa_address_book_save_config(LibBalsaAddressBook * ab,
				       const gchar * prefix);
void libbalsa_address_book_load_config(LibBalsaAddressBook * ab,
				       const gchar * prefix);

#endif
