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

#ifndef __SAVE_RESTORE_H__
#define __SAVE_RESTORE_H__

#include "libbalsa.h"
#include "mailbox-node.h"

typedef enum {
    SPECIAL_INBOX = 0,
    SPECIAL_SENT,
    SPECIAL_TRASH,
    SPECIAL_DRAFT,
    SPECIAL_OUTBOX
} specialType;

void config_mailbox_set_as_special(LibBalsaMailbox * mailbox,
				   specialType which);

gint config_load(void);
void config_load_sections(void);
gint config_save(void);
void config_defclient_save(void);

gchar *mailbox_get_pkey(const LibBalsaMailbox * mbox);
gint config_mailbox_add(LibBalsaMailbox * mailbox, const char *key_arg);
gint config_mailbox_delete(const LibBalsaMailbox * mailbox);
gint config_mailbox_update(LibBalsaMailbox * mailbox);

gint config_folder_add(BalsaMailboxNode * mbnode, const char *key_arg);
gint config_folder_delete(const BalsaMailboxNode * mbnode);
gint config_folder_update(BalsaMailboxNode * mbnode);

void config_address_book_save(LibBalsaAddressBook * ab);
void config_address_book_delete(LibBalsaAddressBook * ab);

void config_identities_save(void);
void config_views_load(void);
void config_views_save(void);

void config_filters_save(void);
void config_mailbox_filters_save(LibBalsaMailbox * mbox);
void clean_filter_config_section(const gchar * name);

#endif				/* __SAVE_RESTORE_H__ */
