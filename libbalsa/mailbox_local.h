/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#ifndef __LIBBALSA_MAILBOX_LOCAL_H__
#define __LIBBALSA_MAILBOX_LOCAL_H__

#include "libbalsa.h"

#define LIBBALSA_TYPE_MAILBOX_LOCAL libbalsa_mailbox_local_get_type()

G_DECLARE_DERIVABLE_TYPE(LibBalsaMailboxLocal,
                         libbalsa_mailbox_local,
                         LIBBALSA,
                         MAILBOX_LOCAL,
                         LibBalsaMailbox)

struct _LibBalsaMailboxLocalPool {
    LibBalsaMessage * message;
    guint pool_seqno;
};
typedef struct _LibBalsaMailboxLocalPool LibBalsaMailboxLocalPool;
#define LBML_POOL_SIZE 32

struct _LibBalsaMailboxLocalMessageInfo {
    LibBalsaMessageFlag flags;          /* May have pseudo-flags */
    LibBalsaMessage *message;
    gboolean loaded;
};
typedef struct _LibBalsaMailboxLocalMessageInfo LibBalsaMailboxLocalMessageInfo;

typedef gboolean LibBalsaMailboxLocalAddMessageFunc(LibBalsaMailboxLocal *
                                                    local,
                                                    GMimeStream * stream,
                                                    LibBalsaMessageFlag
                                                    flags, GError ** err);

struct _LibBalsaMailboxLocalClass {
    LibBalsaMailboxClass klass;

    gint (*check_files)(const gchar * path, gboolean create);
    void (*set_path)(LibBalsaMailboxLocal * local, const gchar * path);
    void (*remove_files)(LibBalsaMailboxLocal * local);
    guint (*fileno)(LibBalsaMailboxLocal * local, guint msgno);
    LibBalsaMailboxLocalMessageInfo *(*get_info)(LibBalsaMailboxLocal * local,
                                                 guint msgno);
    LibBalsaMailboxLocalAddMessageFunc *add_message;
};

LibBalsaMailbox *libbalsa_mailbox_local_new(const gchar * path,
                                            gboolean      create);
gint libbalsa_mailbox_local_set_path(LibBalsaMailboxLocal * mailbox,
				     const gchar * path, gboolean create);

#define libbalsa_mailbox_local_get_path(local) \
	(libbalsa_mailbox_get_url((LibBalsaMailbox*)local)+7)

void libbalsa_mailbox_local_msgno_removed(LibBalsaMailbox * mailbox,
					  guint msgno);
void libbalsa_mailbox_local_remove_files(LibBalsaMailboxLocal *mailbox);

/* Helpers for maildir and mh. */
GMimeMessage *libbalsa_mailbox_local_get_mime_message(LibBalsaMailbox *
						      mailbox,
						      const gchar * name1,
						      const gchar * name2);
GMimeStream *libbalsa_mailbox_local_get_message_stream(LibBalsaMailbox *
						       mailbox,
						       const gchar * name1,
						       const gchar * name2);

#endif				/* __LIBBALSA_MAILBOX_LOCAL_H__ */
