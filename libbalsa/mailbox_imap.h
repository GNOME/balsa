/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 *
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

#ifndef __LIBBALSA_MAILBOX_IMAP_H__
#define __LIBBALSA_MAILBOX_IMAP_H__

#define LIBBALSA_TYPE_MAILBOX_IMAP			(libbalsa_mailbox_imap_get_type())
#define LIBBALSA_MAILBOX_IMAP(obj)			(GTK_CHECK_CAST (obj, LIBBALSA_TYPE_MAILBOX_IMAP, LibBalsaMailboxImap))
#define LIBBALSA_MAILBOX_IMAP_CLASS(klass)		(GTK_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_MAILBOX_IMAP, LibBalsaMailboxImapClass))
#define LIBBALSA_IS_MAILBOX_IMAP(obj)		(GTK_CHECK_TYPE (obj, LIBBALSA_TYPE_MAILBOX_IMAP))
#define LIBBALSA_IS_MAILBOX_IMAP_CLASS(klass)	(GTK_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_MAILBOX_IMAP))

GtkType libbalsa_mailbox_imap_get_type (void);

typedef struct _LibBalsaMailboxImap LibBalsaMailboxImap;
typedef struct _LibBalsaMailboxImapClass LibBalsaMailboxImapClass;

enum _ImapAuthType {
	AuthLogin,
	AuthCram,
	AuthGSS
};
typedef enum _ImapAuthType ImapAuthType;

struct _LibBalsaMailboxImap
{
	LibBalsaMailboxRemote mailbox;

	gchar *path;                  /* Imap path {host:port}mailbox */
	ImapAuthType auth_type;       /* accepted authentication type */
};

struct _LibBalsaMailboxImapClass
{
	LibBalsaMailboxRemoteClass klass;
};

GtkObject *libbalsa_mailbox_imap_new(void);

void libbalsa_mailbox_imap_set_path(LibBalsaMailboxImap *mailbox, gchar *path);

#endif /* __LIBBALSA_MAILBOX_IMAP_H__ */
