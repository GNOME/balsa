/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#ifndef __BALSA_MBLIST_H__
#define __BALSA_MBLIST_H__

#include <gtk/gtk.h>

#define BALSA_TYPE_MBLIST          (balsa_mblist_get_type ())
#define BALSA_MBLIST(obj)          GTK_CHECK_CAST (obj, BALSA_TYPE_MBLIST, BalsaMBList)
#define BALSA_MBLIST_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_MBLIST, BalsaMBListClass)
#define BALSA_IS_MBLIST(obj)       GTK_CHECK_TYPE (obj, BALSA_TYPE_MBLIST)

typedef struct _BalsaMBList BalsaMBList;
typedef struct _BalsaMBListClass BalsaMBListClass;

struct _BalsaMBList {
    GtkCTree ctree;
    /* store the style of unread mailboxes */
    GtkStyle *unread_mailbox_style;
    /* shall the number of messages be displayed ? */
    gboolean display_content_info;
};

struct _BalsaMBListClass {
    GtkCTreeClass parent_class;

    void (*select_mailbox) (BalsaMBList * bmblist,
			    LibBalsaMailbox * mailbox, gint row,
			    GdkEventButton *);
};

GtkType balsa_mblist_get_type(void);

GtkWidget *balsa_mblist_new(void);

void balsa_mblist_repopulate(BalsaMBList * bmbl);
void mblist_default_signal_bindings(BalsaMBList * tree);

void balsa_mblist_have_new(BalsaMBList * bmbl);
void balsa_mblist_update_mailbox(BalsaMBList * mblist,
				 LibBalsaMailbox * mailbox);
gboolean balsa_mblist_focus_mailbox(BalsaMBList * bmbl,
				    LibBalsaMailbox * mailbox);

void mblist_menu_add_mbox_cb(GtkWidget * widget, gpointer data);
void mblist_menu_add_maildir_cb(GtkWidget * widget, gpointer data);
void mblist_menu_add_mh_cb(GtkWidget * widget, gpointer data);
void mblist_menu_add_imap_cb(GtkWidget * widget, gpointer data);

void mblist_menu_edit_cb(GtkWidget * widget, gpointer data);
void mblist_menu_delete_cb(GtkWidget * widget, gpointer data);
void mblist_menu_open_cb(GtkWidget * widget, gpointer data);
void mblist_menu_close_cb(GtkWidget * widget, gpointer data);
GList *mblist_find_all_unread_mboxes(void);
void mblist_open_mailbox(LibBalsaMailbox * mailbox);

#endif
