/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#ifndef __MAILBOX_H__
#define __MAILBOX_H__

#include "c-client.h"

typedef enum
  {
    MAILBOX_MBOX,
    MAILBOX_POP3,
    MAILBOX_IMAP,
    MAILBOX_NNTP
  } 
MailboxType;


typedef struct _Mailbox Mailbox;
struct _Mailbox
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;
  };

typedef struct _MailboxMBox MailboxMBox;
struct _MailboxMBox
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *path;
  };

typedef struct _MailboxPOP3 MailboxPOP3;
struct _MailboxPOP3
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *user;
    gchar *passwd;
    gchar *server;
  };

typedef struct _MailboxIMAP MailboxIMAP;
struct _MailboxIMAP
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *user;
    gchar *passwd;
    gchar *server;
  };

typedef struct _MailboxNNTP MailboxNNTP;
struct _MailboxNNTP
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *user;
    gchar *passwd;
    gchar *server;
  };

typedef union _MailboxUnion MailboxUnion;
union _MailboxUnion
  {
    MailboxType type;
    Mailbox mailbox;
    MailboxMBox mbox;
    MailboxPOP3 pop3;
    MailboxIMAP imap;
    MailboxNNTP nntp;
  };


gchar * mailbox_type_description (MailboxType type);

Mailbox * mailbox_new (MailboxType type);
void mailbox_free (Mailbox * mailbox);

int mailbox_open (Mailbox * mailbox);

void mailbox_close (Mailbox * mailbox);

void current_mailbox_check ();

#endif /* __MAILBOX_H__ */
