/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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
#ifndef __BALSA_MBLIST_H__
#define __BALSA_MBLIST_H__

#define BALSA_TYPE_MBLIST          (balsa_mblist_get_type ())
#define BALSA_MBLIST(obj)          GTK_CHECK_CAST (obj, BALSA_TYPE_MBLIST, BalsaMBList)
#define BALSA_MBLIST_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_MBLIST, BalsaMBListClass)
#define BALSA_IS_MBLIST(obj)       GTK_CHECK_TYPE (obj, BALSA_TYPE_MBLIST)

typedef struct _BalsaMBList BalsaMBList;
typedef struct _BalsaMBListClass BalsaMBListClass;

struct _BalsaMBList
  {
    GtkCTree ctree;

    GList *watched_mailbox; /* list of mailbox watched */
#ifdef BALSA_SHOW_INFO
    gboolean display_content_info; /* shall the number of messages be displayed ? */
#endif
  };

struct _BalsaMBListClass
  {
    GtkCTreeClass parent_class;

    void (*select_mailbox) (BalsaMBList * bmblist,
			    Mailbox * mailbox,
			    gint row,
			    GdkEventButton *);
  };

GtkWidget *balsa_mblist_new (void);
void balsa_mblist_redraw (BalsaMBList * bmbl);
guint balsa_mblist_get_type (void);
void balsa_mblist_update_mailbox (BalsaMBList * mblist, Mailbox * mailbox);
#endif
