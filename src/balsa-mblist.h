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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __BALSA_MBLIST_H__
#define __BALSA_MBLIST_H__

#include <gtk/gtk.h>
#include "mailbox-node.h"

#define BALSA_TYPE_MBLIST          (balsa_mblist_get_type ())
#define BALSA_MBLIST(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, BALSA_TYPE_MBLIST, BalsaMBList)
#define BALSA_MBLIST_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, BALSA_TYPE_MBLIST, BalsaMBListClass)
#define BALSA_IS_MBLIST(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, BALSA_TYPE_MBLIST)

typedef struct _BalsaMBList BalsaMBList;
typedef struct _BalsaMBListClass BalsaMBListClass;

struct _BalsaMBList {
    GtkTreeView tree_view;

    /* shall the number of messages be displayed ? */
    gboolean display_info;
    /* signal handler id */
    gulong toggled_handler_id;

    /* to set sort order in an idle callback */
    gint  sort_column_id;
    guint sort_idle_id;
};

struct _BalsaMBListClass {
    GtkTreeViewClass parent_class;

    void (*has_unread_mailbox)(BalsaMBList * mblist,
                               gboolean has_unread_mailbox);
};

GType balsa_mblist_get_type(void);

GtkWidget *balsa_mblist_new(void);

GtkTreeStore *balsa_mblist_get_store(void);
void balsa_mblist_default_signal_bindings(BalsaMBList * tree);

void balsa_mblist_update_mailbox(GtkTreeStore * store,
                                 LibBalsaMailbox * mailbox);
gboolean balsa_mblist_focus_mailbox(BalsaMBList * mblist,
                                    LibBalsaMailbox * mailbox);

GList *balsa_mblist_find_all_unread_mboxes(LibBalsaMailbox * mailbox);
void balsa_mblist_open_mailbox(LibBalsaMailbox * mailbox);
void balsa_mblist_open_mailbox_hidden(LibBalsaMailbox * mailbox);
void balsa_mblist_close_mailbox(LibBalsaMailbox * mailbox);
/* balsa_mblist_close_lru_peer_mbx closes least recently used mailbox
 * on the same server as the one given as the argument */
gboolean balsa_mblist_close_lru_peer_mbx(BalsaMBList * mblist,
                                         LibBalsaMailbox *mailbox);

BalsaMailboxNode *balsa_mblist_get_selected_node(BalsaMBList * mblist);
BalsaMailboxNode *balsa_mblist_get_node_by_mailbox(BalsaMBList * mblist,
                                                   LibBalsaMailbox *
                                                   mailbox);
GtkWidget *balsa_mblist_mru_menu(GtkWindow * window, GList ** url_list,
                                 GCallback user_func, gpointer user_data);
void balsa_mblist_mru_add(GList ** url_list, const gchar * url);
void balsa_mblist_mru_drop(GList ** url_list, const gchar * url);
GtkWidget *balsa_mblist_mru_option_menu(GtkWindow * window, 
                                        GList ** url_list);
void balsa_mblist_mru_option_menu_set(GtkWidget * option_menu,
                                      const gchar * url);
const gchar *balsa_mblist_mru_option_menu_get(GtkWidget * option_menu);

/* BalsaMailboxNode methods */
void balsa_mblist_mailbox_node_append(BalsaMailboxNode * root,
				      BalsaMailboxNode * mbnode);
void balsa_mblist_mailbox_node_redraw(BalsaMailboxNode * mbnode);
gboolean balsa_mblist_mailbox_node_remove(BalsaMailboxNode * mbnode);

#endif
