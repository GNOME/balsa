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

#ifndef __LIBBALSA_MAILBOX_IMAP_H__
#define __LIBBALSA_MAILBOX_IMAP_H__

#define LIBBALSA_TYPE_MAILBOX_IMAP \
    (libbalsa_mailbox_imap_get_type())
#define LIBBALSA_MAILBOX_IMAP(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIBBALSA_TYPE_MAILBOX_IMAP, \
                                 LibBalsaMailboxImap))
#define LIBBALSA_MAILBOX_IMAP_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX_IMAP, \
                              LibBalsaMailboxImapClass))
#define LIBBALSA_IS_MAILBOX_IMAP(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIBBALSA_TYPE_MAILBOX_IMAP))
#define LIBBALSA_IS_MAILBOX_IMAP_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX_IMAP))

GType libbalsa_mailbox_imap_get_type(void);

typedef struct _LibBalsaMailboxImap LibBalsaMailboxImap;
typedef struct _LibBalsaMailboxImapClass LibBalsaMailboxImapClass;

#define POINTER_TO_UID(p) GPOINTER_TO_UINT(p)
#define UID_TO_POINTER(p) GUINT_TO_POINTER(p)

GObject *libbalsa_mailbox_imap_new(void);

void libbalsa_mailbox_imap_update_url(LibBalsaMailboxImap* mailbox);
void libbalsa_mailbox_imap_set_path(LibBalsaMailboxImap * mailbox,
				    const gchar * path);
const gchar* libbalsa_mailbox_imap_get_path(LibBalsaMailboxImap * mailbox);

gboolean libbalsa_mailbox_imap_subscribe(LibBalsaMailboxImap * mailbox, 
                                         gboolean subscribe);

GHashTable * libbalsa_mailbox_imap_get_matchings(LibBalsaMailboxImap* mbox,
						 LibBalsaCondition *condition,
						 gboolean only_recent,
						 gboolean * err);

void libbalsa_mailbox_imap_noop(LibBalsaMailboxImap* mbox);

void libbalsa_imap_close_all_connections(void);

void libbalsa_imap_new_subfolder(const gchar * parent, const gchar * folder,
				 gboolean subscribe, LibBalsaServer * server);

gboolean libbalsa_imap_rename_subfolder(LibBalsaMailboxImap* mbox,
                                        const gchar *new_parent,
                                        const gchar *folder, gboolean subscr);

gboolean libbalsa_imap_delete_folder(LibBalsaMailboxImap * mailbox);

gchar *libbalsa_imap_path(LibBalsaServer * server, const gchar * path);
gchar *libbalsa_imap_url(LibBalsaServer * server, const gchar * path);

void libbalsa_imap_remove_temp_dir(void);
#endif				/* __LIBBALSA_MAILBOX_IMAP_H__ */
