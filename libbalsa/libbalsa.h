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

#ifndef __LIBBALSA_H__
#define __LIBBALSA_H__

typedef struct _LibBalsaMailbox LibBalsaMailbox;
typedef struct _LibBalsaMailboxRemote LibBalsaMailboxRemote;
typedef struct _LibBalsaMessage LibBalsaMessage;
typedef struct _LibBalsaMessageBody LibBalsaMessageBody;
typedef struct _LibBalsaServer LibBalsaServer;

/* Opaque structures */
typedef struct body MuttBody;

#include "address.h"
#include "message.h"
#include "body.h"
#include "files.h"
#include "misc.h"
#include "mime.h"
#include "notify.h"

#include "information.h"

#include "server.h"

#include "address-book.h"
#include "address-book-vcard.h"
#if ENABLE_LDAP
#include "address-book-ldap.h"
#endif

#include "mailbox.h"
#include "mailbox_local.h"
#include "mailbox_remote.h"
#include "mailbox_pop3.h"
#include "mailbox_imap.h"
#include "mailbox_mbox.h"
#include "mailbox_mh.h"
#include "mailbox_maildir.h"

#ifdef BALSA_SHOW_ALL
#include "filter.h"
#endif

/*
 * Initialize the library
 */
void libbalsa_init(LibBalsaInformationFunc information_callback);
void libbalsa_set_spool(gchar * spool);

void libbalsa_show_message_source(LibBalsaMessage* msg);

gchar *libbalsa_guess_mail_spool(void);

void libbalsa_lock_mutt(void);
void libbalsa_unlock_mutt(void);

#endif				/* __LIBBALSA_H__ */
