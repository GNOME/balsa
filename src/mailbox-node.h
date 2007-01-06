/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#ifndef __BALSA_MAILBOX_NODE_H__
#define __BALSA_MAILBOX_NODE_H__

#include <gtk/gtk.h>
#include "libbalsa.h"

#define BALSA_TYPE_MAILBOX_NODE          (balsa_mailbox_node_get_type ())
#define BALSA_MAILBOX_NODE(obj) \
    G_TYPE_CHECK_INSTANCE_CAST(obj, BALSA_TYPE_MAILBOX_NODE, \
                                BalsaMailboxNode)
#define BALSA_MAILBOX_NODE_CLASS(klass) \
    G_TYPE_CHECK_CLASS_CAST(klass, BALSA_TYPE_MAILBOX_NODE, \
                             BalsaMailboxNodeClass)
#define BALSA_IS_MAILBOX_NODE(obj) \
    G_TYPE_CHECK_INSTANCE_TYPE(obj, BALSA_TYPE_MAILBOX_NODE)
#define BALSA_IS_MAILBOX_NODE_CLASS(klass) \
    G_TYPE_CHECK_CLASS_TYPE(klass, BALSA_TYPE_MAILBOX_NODE)
    
typedef struct _BalsaMailboxNode BalsaMailboxNode;
typedef struct _BalsaMailboxNodeClass BalsaMailboxNodeClass;

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

struct _BalsaMailboxNode {
    GObject object;
    BalsaMailboxNode *parent; /* NULL for root-level folders & mailboxes */
    LibBalsaMailbox *mailbox; /* != NULL for leaves only */
    gchar *name;       /* used for folders, i.e. when mailbox == NULL */
    time_t last_use;   /* for closing least recently used mailboxes */
    BalsaMailboxNodeStyle style;
    /* folder data */
    gchar* config_prefix;
    gchar* dir;      
    LibBalsaServer * server; /* Used only by remote; is referenced */
    char delim; /* IMAP delimiter so that we do not need to check it
		 * too often. */


    unsigned remote:1;/* is dirname or server field used in data union.
		       * If there is a need for more types, make a subclass. */

    unsigned subscribed:1;     /* Used only by remote */
    unsigned list_inbox:1;     /* Used only by remote */
    unsigned scanned:1;        /* IMAP flag */
};

struct _BalsaMailboxNodeClass {
    GObjectClass parent_class;
    void (*save_config) (BalsaMailboxNode * mn, const gchar * prefix);
    void (*load_config) (BalsaMailboxNode * mn, const gchar * prefix);
    GtkWidget* (*show_prop_dialog) (BalsaMailboxNode * mn);
    void (*append_subtree) (BalsaMailboxNode * mn);
};

GType balsa_mailbox_node_get_type(void);

BalsaMailboxNode *balsa_mailbox_node_new(void);
BalsaMailboxNode *balsa_mailbox_node_new_from_mailbox(LibBalsaMailbox *m);
BalsaMailboxNode *balsa_mailbox_node_new_from_dir(const gchar* dir);
BalsaMailboxNode *balsa_mailbox_node_new_imap(LibBalsaServer* s, const char*p);
BalsaMailboxNode *balsa_mailbox_node_new_imap_folder(LibBalsaServer* s, 
						     const char*p);
BalsaMailboxNode *balsa_mailbox_node_new_from_config(const gchar* prefix);

GtkWidget *balsa_mailbox_node_get_context_menu(BalsaMailboxNode * mbnode);
void balsa_mailbox_node_show_prop_dialog(BalsaMailboxNode * mbnode);
void balsa_mailbox_node_append_subtree(BalsaMailboxNode * mbnode);
void balsa_mailbox_node_load_config(BalsaMailboxNode* mn, const gchar* prefix);
void balsa_mailbox_node_save_config(BalsaMailboxNode* mn, const gchar* prefix);
void balsa_mailbox_node_show_prop_dialog_cb(GtkWidget * widget, gpointer data);

/* applicable only to local mailboxes (mailbox collections) */
void balsa_mailbox_local_append(LibBalsaMailbox* mbx);
/* applicable only to folders (mailbox collections) */
void balsa_mailbox_node_rescan(BalsaMailboxNode* mn);
void balsa_mailbox_node_clear_children_cache(BalsaMailboxNode * mbnode);

/* applicable to any mailbox node */
void balsa_mailbox_node_scan_children(BalsaMailboxNode * mbnode);

#endif
