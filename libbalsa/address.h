/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
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

#ifndef __LIBBALSA_ADDRESS_H__
#define __LIBBALSA_ADDRESS_H__

#include <glib.h>

struct _LibBalsaAddress
{
	gchar *personal;	/* full text name */
	gchar *mailbox;		/* user name and host (mailbox name) on remote system */
};

LibBalsaAddress *libbalsa_address_new(void);
LibBalsaAddress *libbalsa_address_new_from_string (gchar *address);
GList *libbalsa_address_new_list_from_string (gchar *address);

gchar *libbalsa_address_to_gchar (LibBalsaAddress * addr);

void libbalsa_address_free(LibBalsaAddress *address);
void libbalsa_address_list_free(GList *address_list);

#endif /* __LIBBALSA_ADDRESS_H__ */
