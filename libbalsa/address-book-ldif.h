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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The LDIF address book ...
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_LDIF_H__
#define __LIBBALSA_ADDRESS_BOOK_LDIF_H__

#include "address-book-text.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_LDIF			(libbalsa_address_book_ldif_get_type())
#define LIBBALSA_ADDRESS_BOOK_LDIF(obj)			(G_TYPE_CHECK_INSTANCE_CAST (obj, LIBBALSA_TYPE_ADDRESS_BOOK_LDIF, LibBalsaAddressBookLdif))
#define LIBBALSA_ADDRESS_BOOK_LDIF_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS_BOOK_LDIF, LibBalsaAddressBookLdifClass))
#define LIBBALSA_IS_ADDRESS_BOOK_LDIF(obj)		(G_TYPE_CHECK_INSTANCE_TYPE (obj, LIBBALSA_TYPE_ADDRESS_BOOK_LDIF))
#define LIBBALSA_IS_ADDRESS_BOOK_LDIF_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS_BOOK_LDIF))

typedef struct _LibBalsaAddressBookLdif LibBalsaAddressBookLdif;
typedef struct _LibBalsaAddressBookLdifClass
    LibBalsaAddressBookLdifClass;

struct _LibBalsaAddressBookLdif {
    LibBalsaAddressBookText parent;
};

struct _LibBalsaAddressBookLdifClass {
    LibBalsaAddressBookTextClass parent_class;
};

GType libbalsa_address_book_ldif_get_type(void);

LibBalsaAddressBook *libbalsa_address_book_ldif_new(const gchar * name,
                                                    const gchar * path);

#endif /* __LIBBALSA_ADDRESS_BOOK_LDIF_H__ */
