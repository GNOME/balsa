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

gchar *address_to_gchar (const Address * addr);
gchar *make_string_from_list (GList *);
gchar *ADDRESS_to_gchar (const ADDRESS * addr);

size_t readfile (FILE * fp, char **buf);

/* MailboxNodeStyle [MBG] 
 * 
 * MBNODE_STYLE_ICONFULL: Whether the full mailbox icon is displayed
 *      (also when font is bolded)
 * MBNODE_STYLE_UNREAD_MESSAGES: Whether the number of unread messages 
 *      is being displayed in the maibox list
 * MBNODE_STYLE_TOTAL_MESSAGES: Whether the number of total messages 
 *      is being displayed in the mailbox list
 * 
 * I added these style flags so we can easily keep track of what the
 * node looks like without having to resort to ugly gtk_get_style...
 * stuff.  Currently only MBNODE_STYLE_ICONFULL is really used, but
 * the others may be used later for more efficient style handling.
 * */
typedef enum
{
  MBNODE_STYLE_ICONFULL = 1 << 1,
  MBNODE_STYLE_UNREAD_MESSAGES = 1 << 2,
  MBNODE_STYLE_TOTAL_MESSAGES = 1 << 3,
} MailboxNodeStyle;


typedef struct _MailboxNode MailboxNode;
struct _MailboxNode
{
  GtkObject object;
  gchar *name;
  Mailbox *mailbox;
  gint IsDir;
  gint expanded;
  MailboxNodeStyle style;
};

MailboxNode *mailbox_node_new (const gchar * name, Mailbox * mb, gint i);
void mailbox_node_destroy(MailboxNode *mbn);

gchar *g_get_host_name (void);

gboolean find_word(const gchar * word, const gchar* str);
void wrap_string(gchar* str, int width);
#endif /* __MISC_H__ */
