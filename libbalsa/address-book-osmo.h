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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Access osmo (http://clayo.org/osmo/) address book through DBus
 */

#ifndef LIBBALSA_ADDRESS_BOOK_OSMO_H__
#define LIBBALSA_ADDRESS_BOOK_OSMO_H__

#include "address-book.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_OSMO (libbalsa_address_book_osmo_get_type())

G_DECLARE_FINAL_TYPE(LibBalsaAddressBookOsmo,
                     libbalsa_address_book_osmo,
                     LIBBALSA,
                     ADDRESS_BOOK_OSMO,
                     LibBalsaAddressBook);

LibBalsaAddressBook *libbalsa_address_book_osmo_new(const gchar *name);


#endif              /* __LIBBALSA_ADDRESS_BOOK_OSMO_H__ */
