/* Balsa E-Mail Client
 * Copyright (C) 1999 Stuart Parmenter
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

#ifndef __ADDRESS_H__
#define __ADDRESS_H__

#include <glib.h>
#include "libmutt/mutt.h"

struct _Address
{
  gchar *personal;		/* full text name */
  gchar *mailbox;		/* user name and host (mailbox name) on remote system */
};

Address *address_new(void);
void address_free(Address *address);

#endif /* __ADDRESS_H__ */
