/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __BALSA_MBLIST_H__
#define __BALSA_MBLIST_H__

#include <gtk/gtk.h>
#include "mailbox-node.h"

#define BALSA_TYPE_MBLIST (balsa_mblist_get_type ())

G_DECLARE_FINAL_TYPE(BalsaMBList, balsa_mblist, BALSA, MBLIST, GtkTreeView)

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
GtkWidget *balsa_mblist_mru_menu(GtkWindow            *window,
                                 GList               **url_list,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data);
LibBalsaMailbox *balsa_mblist_mru_menu_finish(GAsyncResult *result);
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
