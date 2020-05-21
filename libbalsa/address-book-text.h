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
 * LibBalsaAddressBookText
 * 
 * The code that is common to vCard (== GnomeCard) and LDIF address
 * books ...
 */

#ifndef __LIBBALSA_ADDRESS_BOOK_TEXT_H__
#define __LIBBALSA_ADDRESS_BOOK_TEXT_H__

#include "address-book.h"
#include "completion.h"

#define LIBBALSA_TYPE_ADDRESS_BOOK_TEXT	(libbalsa_address_book_text_get_type())
G_DECLARE_DERIVABLE_TYPE(LibBalsaAddressBookText, libbalsa_address_book_text,
        LIBBALSA, ADDRESS_BOOK_TEXT, LibBalsaAddressBook)

struct _LibBalsaAddressBookTextClass {
    LibBalsaAddressBookClass parent_class;

    LibBalsaABErr (*parse_address)(FILE * stream_in,
                                   LibBalsaAddress * address_in,
                                   FILE * stream_out,
                                   LibBalsaAddress * address_out);
    LibBalsaABErr (*save_address) (FILE * stream,
                                   LibBalsaAddress * address);

    GDestroyNotify text_item_free_func;
};

typedef struct _LibBalsaAddressBookText LibBalsaAddressBookText;
typedef struct _LibBalsaAddressBookTextClass LibBalsaAddressBookTextClass;

/*
 * Getters
 */

const gchar *        libbalsa_address_book_text_get_path(LibBalsaAddressBookText * ab_text);
GSList *             libbalsa_address_book_text_get_item_list(LibBalsaAddressBookText * ab_text);
time_t               libbalsa_address_book_text_get_mtime(LibBalsaAddressBookText * ab_text);
LibBalsaCompletion * libbalsa_address_book_text_get_name_complete(LibBalsaAddressBookText * ab_text);

/*
 * Setters
 */

void libbalsa_address_book_text_set_path(LibBalsaAddressBookText * ab_text,
                                         const gchar             * path);
void libbalsa_address_book_text_set_item_list(LibBalsaAddressBookText * ab_text,
                                              GSList                  * item_list);
void libbalsa_address_book_text_set_mtime(LibBalsaAddressBookText * ab_text,
                                          time_t                    mtime);
void libbalsa_address_book_text_set_name_complete(LibBalsaAddressBookText * ab_text,
                                                  LibBalsaCompletion      * name_complete);

#endif                          /* __LIBBALSA_ADDRESS_BOOK_TEXT_H__ */
