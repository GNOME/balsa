/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1999 Stuart Parmenter
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
typedef struct _LibBalsaMessage LibBalsaMessage;
typedef struct _LibBalsaContact LibBalsaContact;
typedef struct _LibBalsaMessageBody LibBalsaMessageBody;
typedef struct _LibBalsaAddress LibBalsaAddress;
typedef struct _ImapDir ImapDir;
typedef struct _LibBalsaServer LibBalsaServer;

#include "address.h"
#include "message.h"
#include "contact.h"
#include "imapdir.h"
#include "body.h"
#include "files.h"
#include "misc.h"
#include "mime.h"

#include "mailbox.h"
#include "mailbox_local.h"
#include "mailbox_pop3.h"
#include "mailbox_imap.h"

/*
 * Initialize the library
 */
void libbalsa_init (gchar * inbox_path,	void (*error_func) (const char *fmt,...));

#endif /* __LIBBALSA_H__ */
