/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

/*
 * The vCard (== GnomeCard) address book ...
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_VCARD_H__
#define __LIBBALSA_ADDRESS_BOOK_VCARD_H__

#include "address-book-text.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_VCARD		(libbalsa_address_book_vcard_get_type())
#define LIBBALSA_ADDRESS_BOOK_VCARD(obj)		(G_TYPE_CHECK_INSTANCE_CAST (obj, LIBBALSA_TYPE_ADDRESS_BOOK_VCARD, LibBalsaAddressBookVcard))
#define LIBBALSA_ADDRESS_BOOK_VCARD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS_BOOK_VCARD, LibBalsaAddressBookVcardClass))
#define LIBBALSA_IS_ADDRESS_BOOK_VCARD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE (obj, LIBBALSA_TYPE_ADDRESS_BOOK_VCARD))
#define LIBBALSA_IS_ADDRESS_BOOK_VCARD_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS_BOOK_VCARD))

struct _LibBalsaAddressBookVcard {
    LibBalsaAddressBookText parent;
};

struct _LibBalsaAddressBookVcardClass {
    LibBalsaAddressBookTextClass parent_class;
};

typedef struct _LibBalsaAddressBookVcard LibBalsaAddressBookVcard;
typedef struct _LibBalsaAddressBookVcardClass
    LibBalsaAddressBookVcardClass;

GType libbalsa_address_book_vcard_get_type(void);

LibBalsaAddressBook *libbalsa_address_book_vcard_new(const gchar * name,
						     const gchar * path);

#endif /* __LIBBALSA_ADDRESS_BOOK_VCARD_H__ */
