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

/*
 * The LDIF address book ...
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_LDIF_H__
#define __LIBBALSA_ADDRESS_BOOK_LDIF_H__

#include "address-book-text.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_LDIF (libbalsa_address_book_ldif_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaAddressBookLdif, libbalsa_address_book_ldif,
                     LIBBALSA, ADDRESS_BOOK_LDIF, LibBalsaAddressBookText)

LibBalsaAddressBook *libbalsa_address_book_ldif_new(const gchar * name,
                                                    const gchar * path);

#endif /* __LIBBALSA_ADDRESS_BOOK_LDIF_H__ */
