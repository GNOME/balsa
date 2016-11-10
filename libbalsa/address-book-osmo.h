/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * Osmo address book support has been written by Copyright (C) 2016
 * Albrecht Dreß <albrecht.dress@arcor.de>.
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
 * Access osmo (http://clayo.org/osmo/) address book through DBus
 */

#ifndef LIBBALSA_ADDRESS_BOOK_OSMO_H__
#define LIBBALSA_ADDRESS_BOOK_OSMO_H__

#include <gio/gio.h>
#include "address-book.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_OSMO     		(libbalsa_address_book_osmo_get_type())
#define LIBBALSA_ADDRESS_BOOK_OSMO(obj)     		(G_TYPE_CHECK_INSTANCE_CAST(obj, LIBBALSA_TYPE_ADDRESS_BOOK_OSMO, LibBalsaAddressBookOsmo))
#define LIBBALSA_ADDRESS_BOOK_OSMO_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST(klass, LIBBALSA_TYPE_ADDRESS_BOOK_OSMO, LibBalsaAddressBookOsmoClass))
#define LIBBALSA_IS_ADDRESS_BOOK_OSMO(obj)      	(G_TYPE_CHECK_INSTANCE_TYPE(obj, LIBBALSA_TYPE_ADDRESS_BOOK_OSMO))
#define LIBBALSA_IS_ADDRESS_BOOK_OSMO_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE(klass, LIBBALSA_TYPE_ADDRESS_BOOK_OSMO))

typedef struct _LibBalsaAddressBookOsmo LibBalsaAddressBookOsmo;
typedef struct _LibBalsaAddressBookOsmoClass LibBalsaAddressBookOsmoClass;

struct _LibBalsaAddressBookOsmo {
	LibBalsaAddressBook parent;

	GDBusProxy *proxy;
};

struct _LibBalsaAddressBookOsmoClass {
	LibBalsaAddressBookClass parent_class;
};

GType libbalsa_address_book_osmo_get_type(void);

LibBalsaAddressBook *libbalsa_address_book_osmo_new(const gchar *name);


#endif              /* __LIBBALSA_ADDRESS_BOOK_LDAP_H__ */
