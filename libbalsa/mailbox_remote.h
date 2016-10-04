/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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


#ifndef __LIBBALSA_MAILBOX_REMOTE_H__
#define __LIBBALSA_MAILBOX_REMOTE_H__

#include "libbalsa.h"

/* Imap and Pop3 are remote mailboxes 
   
   this object doesn't do any real work.  it is abstract

     this mini-struct greatly helps in getting the server
     from the mailbox without having a Server pointer off of
     all mailboxes....  which arguably we might want eventually,
     and claim that a directory is a "server", but until then...
 */
#define LIBBALSA_TYPE_MAILBOX_REMOTE \
    (libbalsa_mailbox_remote_get_type())
#define LIBBALSA_MAILBOX_REMOTE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIBBALSA_TYPE_MAILBOX_REMOTE, \
                                 LibBalsaMailboxRemote))
#define LIBBALSA_MAILBOX_REMOTE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX, \
                              LibBalsaMailboxRemoteClass))
#define LIBBALSA_IS_MAILBOX_REMOTE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIBBALSA_TYPE_MAILBOX_REMOTE))
#define LIBBALSA_IS_MAILBOX_REMOTE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX_REMOTE))

#define LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox) \
    (LIBBALSA_SERVER(LIBBALSA_MAILBOX_REMOTE(mailbox)->server))

typedef struct _LibBalsaMailboxRemoteClass LibBalsaMailboxRemoteClass;

struct _LibBalsaMailboxRemote {
    LibBalsaMailbox mailbox;

    LibBalsaServer *server;
};

struct _LibBalsaMailboxRemoteClass {
    LibBalsaMailboxClass parent_class;
};

GType libbalsa_mailbox_remote_get_type(void);

void libbalsa_mailbox_remote_set_server(LibBalsaMailboxRemote* m, 
					LibBalsaServer* s);

#endif				/* __LIBBALSA_MAILBOX_REMOTE_H__ */
