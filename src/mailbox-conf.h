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

#ifndef __MAILBOX_CONF_H__
#define __MAILBOX_CONF_H__

#include "mailbox-node.h"

void mailbox_conf_new(GtkType mailbox_type);
void mailbox_conf_edit(BalsaMailboxNode * mbnode);
void mailbox_conf_delete(BalsaMailboxNode * mbnode);

/* callbacks used also by the main window menu */
void mailbox_conf_add_mbox_cb(GtkWidget * widget, gpointer data);
void mailbox_conf_add_maildir_cb(GtkWidget * widget, gpointer data);
void mailbox_conf_add_mh_cb(GtkWidget * widget, gpointer data);
void mailbox_conf_add_imap_cb(GtkWidget * widget, gpointer data);
void mailbox_conf_delete_cb(GtkWidget * widget, gpointer data);
#endif				/* __MAILBOX_CONF_H__ */
