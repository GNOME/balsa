/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Jay Painter and Stuart Parmenter
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


#ifndef __MAILBOX_REMOTE_H__
#define __MAILBOX_REMOTE_H__

#include "libbalsa.h"

/* Imap and Pop3 are remote mailboxes 
   
   this object doesn't do any real work.  it is abstract

     this mini-struct greatly helps in getting the server
     from the mailbox without having a Server pointer off of
     all mailboxes....  which arguably we might want eventually,
     and claim that a directory is a "server", but until then...
 */
#define LIBBALSA_TYPE_MAILBOX_REMOTE	        (libbalsa_mailbox_remote_get_type())
#define LIBBALSA_MAILBOX_REMOTE(obj)		(GTK_CHECK_CAST ((obj), LIBBALSA_TYPE_MAILBOX_REMOTE, LibBalsaMailboxRemote))
#define LIBBALSA_MAILBOX_REMOTE_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX, LibBalsaMailboxRemoteClass))
#define LIBBALSA_IS_MAILBOX_REMOTE(obj)		(GTK_CHECK_TYPE ((obj), LIBBALSA_TYPE_MAILBOX_REMOTE))
#define LIBBALSA_IS_MAILBOX_REMOTE_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX_REMOTE))

#define LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox) (LIBBALSA_SERVER(LIBBALSA_MAILBOX_REMOTE(mailbox)->server))

typedef struct _LibBalsaMailboxRemoteClass LibBalsaMailboxRemoteClass;
struct _LibBalsaMailboxRemote
{
  LibBalsaMailbox mailbox;
  
  LibBalsaServer *server;
};

struct _LibBalsaMailboxRemoteClass
{
  LibBalsaMailboxClass parent_class;
};

GtkType libbalsa_mailbox_remote_get_type (void);

#endif

