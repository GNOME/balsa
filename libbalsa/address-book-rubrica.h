/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * Rubrica2 address book support was written by Copyright (C)
 * Albrecht Dreﬂ <albrecht.dress@arcor.de> 2007.
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
 * Rubrica address book ...
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_RUBRICA_H__
#define __LIBBALSA_ADDRESS_BOOK_RUBRICA_H__

#include "address-book-text.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA		(libbalsa_address_book_rubrica_get_type())
#define LIBBALSA_ADDRESS_BOOK_RUBRICA(obj)		(G_TYPE_CHECK_INSTANCE_CAST (obj, LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA, LibBalsaAddressBookRubrica))
#define LIBBALSA_ADDRESS_BOOK_RUBRICA_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA, LibBalsaAddressBookRubricaClass))
#define LIBBALSA_IS_ADDRESS_BOOK_RUBRICA(obj)	        (G_TYPE_CHECK_INSTANCE_TYPE (obj, LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA))
#define LIBBALSA_IS_ADDRESS_BOOK_RUBRICA_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA))

struct _LibBalsaAddressBookRubrica {
    LibBalsaAddressBookText parent;
};

struct _LibBalsaAddressBookRubricaClass {
    LibBalsaAddressBookTextClass parent_class;
};

typedef struct _LibBalsaAddressBookRubrica LibBalsaAddressBookRubrica;
typedef struct _LibBalsaAddressBookRubricaClass
    LibBalsaAddressBookRubricaClass;

GType libbalsa_address_book_rubrica_get_type(void);

LibBalsaAddressBook *libbalsa_address_book_rubrica_new(const gchar * name,
						       const gchar * path);

#endif				/* __LIBBALSA_ADDRESS_BOOK_RUBRICA_H__ */
