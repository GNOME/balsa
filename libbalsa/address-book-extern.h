/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
 * Just a quick hack to support reading address books from external commands (like lbdb)
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_EXTERN_H__
#define __LIBBALSA_ADDRESS_BOOK_EXTERN_H__

#include "address-book.h"
#include <time.h>

#define LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN		(libbalsa_address_book_externq_get_type())
#define LIBBALSA_ADDRESS_BOOK_EXTERN(obj)		(G_TYPE_CHECK_INSTANCE_CAST (obj, LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN, LibBalsaAddressBookExtern))
#define LIBBALSA_ADDRESS_BOOK_EXTERN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN, LibBalsaAddressBookExternClass))
#define LIBBALSA_IS_ADDRESS_BOOK_EXTERN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE (obj, LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN))
#define LIBBALSA_IS_ADDRESS_BOOK_EXTERN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN))

typedef struct _LibBalsaAddressBookExtern LibBalsaAddressBookExtern;
typedef struct _LibBalsaAddressBookExternClass
    LibBalsaAddressBookExternClass;

struct _LibBalsaAddressBookExtern {
    LibBalsaAddressBook parent;

    gchar *load;
    gchar *save;

    GList *address_list;

    time_t mtime;

    GCompletion *name_complete;
    GCompletion *alias_complete;
};

struct _LibBalsaAddressBookExternClass {
    LibBalsaAddressBookClass parent_class;
};

GType libbalsa_address_book_externq_get_type(void);

LibBalsaAddressBook *libbalsa_address_book_externq_new(const gchar * name,
                                                       const gchar * load,
                                                       const char * save);


#endif
