/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#ifndef __MISC_H__
#define __MISC_H__

#include "libbalsa.h"

gchar *address_to_gchar (Address * addr);
gchar *make_string_from_list (GList *);

size_t readfile (FILE * fp, char **buf);

typedef struct _MailboxNode MailboxNode;
struct _MailboxNode
  {
    gboolean ismbnode;
    gchar *name;
    Mailbox *mailbox;
    gint IsDir;
    gint expanded;
  };

MailboxNode *mailbox_node_new (const gchar * name, Mailbox * mb, gint i);
gchar *g_get_host_name (void);
#endif /* __MISC_H__ */
