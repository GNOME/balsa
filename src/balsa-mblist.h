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
#include "mailbox-node.h"

#define BALSA_TYPE_MBLIST          (balsa_mblist_get_type ())
#define BALSA_MBLIST(obj)          GTK_CHECK_CAST (obj, BALSA_TYPE_MBLIST, BalsaMBList)
#define BALSA_MBLIST_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_MBLIST, BalsaMBListClass)
#define BALSA_IS_MBLIST(obj)       GTK_CHECK_TYPE (obj, BALSA_TYPE_MBLIST)

typedef struct _BalsaMBList BalsaMBList;
typedef struct _BalsaMBListClass BalsaMBListClass;

struct _BalsaMBList {
    GtkTreeView tree_view;

    /* shall the number of messages be displayed ? */
    gboolean display_info;
    /* signal handler id */
    gulong toggled_handler_id;
};

struct _BalsaMBListClass {
    GtkTreeViewClass parent_class;
};

GtkType balsa_mblist_get_type(void);

GtkWidget *balsa_mblist_new(void);

void balsa_mblist_repopulate(GtkTreeStore * store);
void balsa_mblist_default_signal_bindings(BalsaMBList * tree);

void balsa_mblist_have_new(GtkTreeStore * store);
void balsa_mblist_update_mailbox(GtkTreeStore * store,
                                 LibBalsaMailbox * mailbox);
gboolean balsa_mblist_focus_mailbox(BalsaMBList * mblist,
                                    LibBalsaMailbox * mailbox);

GList *balsa_mblist_find_all_unread_mboxes(void);
void balsa_mblist_open_mailbox(LibBalsaMailbox * mailbox);
void balsa_mblist_close_mailbox(LibBalsaMailbox * mailbox);
BalsaMailboxNode *balsa_mblist_get_selected_node(BalsaMBList * mblist);
BalsaMailboxNode *balsa_mblist_get_node_by_mailbox(BalsaMBList * mblist,
                                                   LibBalsaMailbox *
                                                   mailbox);
gboolean balsa_mblist_remove_mailbox_node(GtkTreeStore * store,
                                          BalsaMailboxNode * mbnode);
void balsa_mblist_scan_mailbox_node(BalsaMailboxNode * mbnode);
GtkWidget *balsa_mblist_mru_menu(GtkWindow * window, GList ** url_list,
                                 GCallback user_func, gpointer user_data);
void balsa_mblist_mru_add(GList ** url_list, const gchar * url);
void balsa_mblist_mru_drop(GList ** url_list, const gchar * url);
GtkWidget *balsa_mblist_mru_option_menu(GtkWindow * window, 
                                        GList ** url_list);
void balsa_mblist_mru_option_menu_set(GtkWidget * option_menu,
                                      const gchar * url);
const gchar *balsa_mblist_mru_option_menu_get(GtkWidget * option_menu);
void balsa_mblist_set_status_bar(LibBalsaMailbox * mailbox);
#endif
