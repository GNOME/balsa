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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __FOLDER_CONF_H__
#define __FOLDER_CONF_H__

#include "mailbox-node.h"

void folder_conf_imap_node(BalsaMailboxNode *mn);
void folder_conf_imap_sub_node(BalsaMailboxNode *mn);
/* callbacks used also by the main window menu */
void folder_conf_add_imap_cb(GtkWidget * widget, gpointer data);
void folder_conf_add_imap_sub_cb(GtkWidget * widget, gpointer data);
void folder_conf_edit_imap_cb(GtkWidget * widget, gpointer data);
void folder_conf_delete(BalsaMailboxNode* mbnode);
void folder_conf_delete_cb(GtkWidget * widget, gpointer data);
#endif				/* __FOLDER_CONF_H__ */
