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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
    gchar *filter_cmd;
    LibBalsaMailbox *inbox;
    gint msg_size_limit;
    gboolean filter; /* filter through procmail/filter_cmd? */
    gboolean disable_apop; /* Some servers claim to support it but
                              * they do not. */
    gboolean enable_pipe;  /* ditto */
};

LibBalsaMailboxPop3 *libbalsa_mailbox_pop3_new(void);
void libbalsa_mailbox_pop3_set_inbox(LibBalsaMailbox *mailbox,
                                     LibBalsaMailbox *inbox);
void libbalsa_mailbox_pop3_set_msg_size_limit(LibBalsaMailboxPop3 *mailbox,
                                              gint sz_limit);

#endif				/* __LIBBALSA_MAILBOX_POP3_H__ */
