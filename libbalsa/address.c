/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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

#include "config.h"

#include <glib.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

/*
 * addresses
 */
LibBalsaAddress *
libbalsa_address_new (void)
{
  LibBalsaAddress *address;

  address = g_new(LibBalsaAddress, 1);

  address->personal = NULL;
  address->mailbox = NULL;

  return address;
}

/* returns only first address on the list; ignores remaining ones */
LibBalsaAddress*
libbalsa_address_new_from_string(gchar* str) {
  ADDRESS *address = NULL;
  LibBalsaAddress *addr = NULL;

  address = rfc822_parse_adrlist (address, str);
  addr = libbalsa_address_new_from_libmutt (address);
  rfc822_free_address (&address);
  return addr;
}

GList *
libbalsa_address_new_list_from_string (gchar * the_str)
{
  ADDRESS *address = NULL;
  LibBalsaAddress *addr = NULL;
  GList *list = NULL;
  address = rfc822_parse_adrlist (address, the_str);

  while (address)
    {
      addr = libbalsa_address_new_from_libmutt (address);
      list = g_list_append (list, addr);
      address = address->next;
    }
  rfc822_free_address( &address );
  return list;
}

LibBalsaAddress *
libbalsa_address_new_from_libmutt (ADDRESS * caddr)
{
  LibBalsaAddress *address;

  if (!caddr)
    return NULL;
  address = libbalsa_address_new ();
  address->personal = g_strdup (caddr->personal);
  address->mailbox = g_strdup (caddr->mailbox);

  return address;
}

gchar *
libbalsa_address_to_gchar (LibBalsaAddress * addr)
{
  gchar *retc = NULL;

  if (addr->personal) {
     if(addr->mailbox)
	retc= g_strdup_printf("%s <%s>", addr->personal, addr->mailbox);
     else retc = g_strdup(addr->personal);
  } else
     if(addr->mailbox)
	retc = g_strdup(addr->mailbox);
  
  return retc;
}

void
libbalsa_address_free (LibBalsaAddress * address)
{

  if (!address)
    return;

  g_free (address->personal);
  g_free (address->mailbox);

  g_free (address);
}

void libbalsa_address_list_free(GList * address_list)
{
  GList *list;
  for (list = g_list_first (address_list); list; list = g_list_next (list))
    if(list->data) libbalsa_address_free (list->data);
  g_list_free (address_list);
}
