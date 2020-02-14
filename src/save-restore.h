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

#ifndef __SAVE_RESTORE_H__
#define __SAVE_RESTORE_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "libbalsa.h"
#include "mailbox-node.h"

#define VIEW_BY_URL_SECTION_PREFIX "viewByUrl-"

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
gint config_mailbox_delete(LibBalsaMailbox * mailbox);
gint config_mailbox_update(LibBalsaMailbox * mailbox);

gint config_folder_add(BalsaMailboxNode * mbnode, const char *key_arg);
gint config_folder_delete(BalsaMailboxNode * mbnode);
gint config_folder_update(BalsaMailboxNode * mbnode);

void config_address_book_save(LibBalsaAddressBook * ab);
void config_address_book_delete(LibBalsaAddressBook * ab);
void config_address_books_load(void);

void config_identities_save(void);
void config_view_remove(const gchar * url);
LibBalsaMailboxView *config_load_mailbox_view(const gchar * url);
void config_save_mailbox_view(const gchar * url, LibBalsaMailboxView * view);

gboolean config_mailbox_was_open(const gchar * url);
gboolean config_mailbox_was_exposed(const gchar * url);
gint config_mailbox_get_position(const gchar * url);

void config_filters_save(void);
void config_mailbox_filters_save(LibBalsaMailbox * mbox);
void clean_filter_config_section(const gchar * name);

#endif				/* __SAVE_RESTORE_H__ */
