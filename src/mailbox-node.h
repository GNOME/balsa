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

#ifndef __BALSA_MAILBOX_NODE_H__
#define __BALSA_MAILBOX_NODE_H__

#include <gtk/gtk.h>
#include "libbalsa.h"

#define BALSA_TYPE_MAILBOX_NODE (balsa_mailbox_node_get_type ())

G_DECLARE_FINAL_TYPE(BalsaMailboxNode,
                     balsa_mailbox_node,
                     BALSA,
                     MAILBOX_NODE,
                     GObject)
    
/* BalsaMailboxNodeStyle 
 * used to store the style of mailbox entry in the mailbox tree.
 * Currently only MBNODE_STYLE_NEW_MAIL is really used, but
 * the others may be used later for more efficient style handling.
 * 
 * MBNODE_STYLE_NEW_MAIL: Whether the full mailbox icon is displayed
 *      (also when font is bolded)
 * MBNODE_STYLE_UNREAD_MESSAGES: Whether the number of unread messages 
 *      is being displayed in the maibox list
 * MBNODE_STYLE_TOTAL_MESSAGES: Whether the number of total messages 
 *      is being displayed in the mailbox list
 * 
 * */
typedef enum {
    MBNODE_STYLE_NEW_MAIL = 1 << 1,
    MBNODE_STYLE_UNREAD_MESSAGES = 1 << 2,
    MBNODE_STYLE_TOTAL_MESSAGES = 1 << 3,
    MBNODE_STYLE_UNREAD_CHILD = 1 << 4
} BalsaMailboxNodeStyle;

BalsaMailboxNode *balsa_mailbox_node_new(void);
BalsaMailboxNode *balsa_mailbox_node_new_from_mailbox(LibBalsaMailbox *m);
BalsaMailboxNode *balsa_mailbox_node_new_from_dir(const gchar* dir);
BalsaMailboxNode *balsa_mailbox_node_new_imap_folder(LibBalsaServer* s, 
						     const char*p);
BalsaMailboxNode *balsa_mailbox_node_new_from_config(const gchar* prefix);

GtkWidget *balsa_mailbox_node_get_context_menu(BalsaMailboxNode * mbnode);
void balsa_mailbox_node_show_prop_dialog(BalsaMailboxNode * mbnode);
void balsa_mailbox_node_append_subtree(BalsaMailboxNode * mbnode);
void balsa_mailbox_node_load_config(BalsaMailboxNode* mn, const gchar* prefix);
void balsa_mailbox_node_save_config(BalsaMailboxNode* mn, const gchar* prefix);

/* applicable only to local mailboxes (mailbox collections) */
void balsa_mailbox_local_append(LibBalsaMailbox* mbx);
/* applicable only to folders (mailbox collections) */
void balsa_mailbox_node_rescan(BalsaMailboxNode* mn);
void balsa_mailbox_node_clear_children_cache(BalsaMailboxNode * mbnode);

/* applicable to any mailbox node */
void balsa_mailbox_node_scan_children(BalsaMailboxNode * mbnode);

/* return if the passed node resides on a remote IMAP server */
gboolean balsa_mailbox_node_is_imap(const BalsaMailboxNode *mbnode);

/*
 * Setters
 */
void balsa_mailbox_node_set_dir(BalsaMailboxNode * mbnode, const gchar * dir);
void balsa_mailbox_node_set_name(BalsaMailboxNode * mbnode, const gchar * name);
void balsa_mailbox_node_set_config_prefix(BalsaMailboxNode * mbnode, const gchar * config_prefix);
void balsa_mailbox_node_set_last_use_time(BalsaMailboxNode * mbnode);
void balsa_mailbox_node_change_style(BalsaMailboxNode * mbnode,
                                     BalsaMailboxNodeStyle set,
                                     BalsaMailboxNodeStyle clear);
void balsa_mailbox_node_set_subscribed(BalsaMailboxNode * mbnode, guint subscribed);
void balsa_mailbox_node_set_list_inbox(BalsaMailboxNode * mbnode, guint list_inbox);

/*
 * Getters
 */
BalsaMailboxNode * balsa_mailbox_node_get_parent(BalsaMailboxNode * mbnode);
LibBalsaMailbox * balsa_mailbox_node_get_mailbox(BalsaMailboxNode * mbnode);
LibBalsaServer * balsa_mailbox_node_get_server(BalsaMailboxNode * mbnode);
const gchar * balsa_mailbox_node_get_dir(BalsaMailboxNode * mbnode);
const gchar * balsa_mailbox_node_get_name(BalsaMailboxNode * mbnode);
const gchar * balsa_mailbox_node_get_config_prefix(BalsaMailboxNode * mbnode);
time_t balsa_mailbox_node_get_last_use_time(BalsaMailboxNode * mbnode);
BalsaMailboxNodeStyle balsa_mailbox_node_get_style(BalsaMailboxNode * mbnode);
guint balsa_mailbox_node_get_subscribed(BalsaMailboxNode * mbnode);
guint balsa_mailbox_node_get_list_inbox(BalsaMailboxNode * mbnode);
gint balsa_mailbox_node_get_delim(BalsaMailboxNode * mbnode);


#endif
