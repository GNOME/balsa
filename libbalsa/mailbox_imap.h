/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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

#ifndef __LIBBALSA_MAILBOX_IMAP_H__
#define __LIBBALSA_MAILBOX_IMAP_H__

#define LIBBALSA_TYPE_MAILBOX_IMAP libbalsa_mailbox_imap_get_type()

G_DECLARE_FINAL_TYPE(LibBalsaMailboxImap,
                     libbalsa_mailbox_imap,
                     LIBBALSA,
                     MAILBOX_IMAP,
                     LibBalsaMailboxRemote)

#define POINTER_TO_UID(p) GPOINTER_TO_UINT(p)
#define UID_TO_POINTER(p) GUINT_TO_POINTER(p)

LibBalsaMailbox *libbalsa_mailbox_imap_new(void);

void libbalsa_mailbox_imap_update_url(LibBalsaMailboxImap* mailbox);
void libbalsa_mailbox_imap_set_path(LibBalsaMailboxImap * mailbox,
				    const gchar * path);
const gchar* libbalsa_mailbox_imap_get_path(LibBalsaMailboxImap * mailbox);

GHashTable * libbalsa_mailbox_imap_get_matchings(LibBalsaMailboxImap* mbox,
						 LibBalsaCondition *condition,
						 gboolean only_recent,
						 gboolean * err);

void libbalsa_mailbox_imap_noop(LibBalsaMailboxImap* mbox);

void libbalsa_mailbox_imap_force_disconnect(LibBalsaMailboxImap* mimap);
gboolean libbalsa_mailbox_imap_is_connected(LibBalsaMailboxImap* mimap);
void libbalsa_mailbox_imap_reconnect(LibBalsaMailboxImap* mimap);
void libbalsa_imap_close_all_connections(void);

gboolean libbalsa_imap_new_subfolder(const gchar *parent, const gchar *folder,
                                     gboolean subscribe,
                                     LibBalsaServer *server, GError **err);

gboolean libbalsa_imap_rename_subfolder(LibBalsaMailboxImap* mbox,
                                        const gchar *new_parent,
                                        const gchar *folder, gboolean subscr,
                                        GError **err);

gboolean libbalsa_imap_delete_folder(LibBalsaMailboxImap * mailbox,
                                     GError **err);

gboolean libbalsa_imap_get_quota(LibBalsaMailboxImap * mailbox,
                                 gulong *max_kbyte, gulong *used_kbyte);

gchar *libbalsa_imap_get_rights(LibBalsaMailboxImap * mailbox);
gchar **libbalsa_imap_get_acls(LibBalsaMailboxImap * mailbox);

gchar *libbalsa_imap_url(LibBalsaServer * server, const gchar * path);

void libbalsa_imap_set_cache_size(off_t cache_size);
void libbalsa_imap_purge_temp_dir(off_t cache_size);
#endif				/* __LIBBALSA_MAILBOX_IMAP_H__ */
