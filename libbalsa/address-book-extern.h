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
 * Just a quick hack to support reading address books from external commands (like lbdb)
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_EXTERN_H__
#define __LIBBALSA_ADDRESS_BOOK_EXTERN_H__

#include "address-book.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_EXTERNQ (libbalsa_address_book_externq_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaAddressBookExternq, libbalsa_address_book_externq,
        LIBBALSA, ADDRESS_BOOK_EXTERNQ, LibBalsaAddressBook)

LibBalsaAddressBook *libbalsa_address_book_externq_new(const gchar * name,
                                                      const gchar * load,
                                                      const char * save);

/*
 * Getters
 */
const gchar *libbalsa_address_book_externq_get_load(LibBalsaAddressBookExternq * ab_externq);
const gchar *libbalsa_address_book_externq_get_save(LibBalsaAddressBookExternq * ab_externq);

/*
 * Setters
 */
void libbalsa_address_book_externq_set_load(LibBalsaAddressBookExternq * ab_externq,
                                           const gchar               * load);
void libbalsa_address_book_externq_set_save(LibBalsaAddressBookExternq * ab_externq,
                                           const gchar               * save);

#endif
