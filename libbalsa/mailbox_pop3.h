/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
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

#ifndef __LIBBALSA_MAILBOX_POP3_H__
#define __LIBBALSA_MAILBOX_POP3_H__

#define LIBBALSA_TYPE_MAILBOX_POP3 \
    (libbalsa_mailbox_pop3_get_type())
#define LIBBALSA_MAILBOX_POP3(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIBBALSA_TYPE_MAILBOX_POP3, \
                                 LibBalsaMailboxPop3))
#define LIBBALSA_MAILBOX_POP3_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX_POP3, \
                              LibBalsaMailboxPop3Class))
#define LIBBALSA_IS_MAILBOX_POP3(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIBBALSA_TYPE_MAILBOX_POP3))
#define LIBBALSA_IS_MAILBOX_POP3_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX_POP3))

GType libbalsa_mailbox_pop3_get_type(void);

typedef struct _LibBalsaMailboxPop3 LibBalsaMailboxPop3;
typedef struct _LibBalsaMailboxPop3Class LibBalsaMailboxPop3Class;

struct _LibBalsaMailboxPop3 {
    LibBalsaMailboxRemote mailbox;

    gboolean check;
    gboolean delete_from_server;
    gchar *last_popped_uid;
    gchar *filter_cmd;
    LibBalsaMailbox *inbox;
    unsigned filter:1; /* filter through procmail/filter_cmd? */
    unsigned disable_apop:1; /* Some servers claim to support it but
                              * they do not. */
};

struct _LibBalsaMailboxPop3Class {
    LibBalsaMailboxRemoteClass klass;

    void (*config_changed) (LibBalsaMailboxPop3* mailbox);
};

GObject *libbalsa_mailbox_pop3_new(void);
void libbalsa_mailbox_pop3_set_inbox(LibBalsaMailbox *mailbox,
                                     LibBalsaMailbox *inbox);

extern int PopDebug;

#endif				/* __LIBBALSA_MAILBOX_POP3_H__ */
