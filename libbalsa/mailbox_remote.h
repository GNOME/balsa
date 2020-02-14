/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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


#ifndef __LIBBALSA_MAILBOX_REMOTE_H__
#define __LIBBALSA_MAILBOX_REMOTE_H__

#include "libbalsa.h"

/* Imap and Pop3 are remote mailboxes 
   
   this object doesn't do any real work.  it is abstract
 */

#define LIBBALSA_TYPE_MAILBOX_REMOTE libbalsa_mailbox_remote_get_type()

G_DECLARE_DERIVABLE_TYPE(LibBalsaMailboxRemote,
                         libbalsa_mailbox_remote,
                         LIBBALSA,
                         MAILBOX_REMOTE,
                         LibBalsaMailbox)

struct _LibBalsaMailboxRemoteClass {
    LibBalsaMailboxClass parent_class;
};

LibBalsaServer *libbalsa_mailbox_remote_get_server(LibBalsaMailboxRemote *remote);
/* Macro to avoid casts: */
#define LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox) \
    libbalsa_mailbox_remote_get_server(LIBBALSA_MAILBOX_REMOTE(mailbox))

void libbalsa_mailbox_remote_set_server(LibBalsaMailboxRemote *remote,
                                        LibBalsaServer *server);

#endif				/* __LIBBALSA_MAILBOX_REMOTE_H__ */
