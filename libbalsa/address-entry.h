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


#ifndef __LIBBALSA_ADDRESS_ENTRY_H__
#define __LIBBALSA_ADDRESS_ENTRY_H__

#include "address.h"

typedef GtkEntry LibBalsaAddressEntry;
#define LIBBALSA_ADDRESS_ENTRY(obj) GTK_ENTRY(obj)
#define LIBBALSA_IS_ADDRESS_ENTRY(obj) GTK_IS_ENTRY(obj)
void libbalsa_address_entry_set_address_book_list(GList *
                                                  address_book_list);
gboolean libbalsa_address_entry_show_matches(GtkEntry * address_entry);
gint libbalsa_address_entry_addresses(GtkEntry * entry);
GtkWidget *libbalsa_address_entry_new(void);
void libbalsa_address_entry_set_domain(LibBalsaAddressEntry *, void *);
GList *libbalsa_address_entry_get_list(LibBalsaAddressEntry *address_entry);
#endif
