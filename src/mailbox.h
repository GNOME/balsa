/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __mailbox_h__
#define __mailbox_h__

#include "balsa-app.h"


enum
  {
    MAILBOX_MBOX,
    MAILBOX_POP3,
    MAILBOX_IMAP,
    MAILBOX_NNTP
  } 
MailboxType;


typedef union _Mailbox Mailbox;
typedef struct _MailboxCommon MailboxCommon;
typedef struct _MailboxMBox MailboxMBox;
typedef struct _MailboxPOP3 MailboxPOP3;
typedef struct _MailboxIMAP MailboxIMAP;


struct _MailboxCommon
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;
  };

struct _MailboxMBox
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *path;
  };

struct _MailboxPOP3
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *user;
    gchar *passwd;
    gchar *server;
  };

struct _MailboxIMAP
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *user;
    gchar *passwd;
    gchar *server;
  };

struct _MailboxNNTP
  {
    MailboxType type;
    gchar *name;
    MAILSTREAM *stream;

    gchar *user;
    gchar *passwd;
    gchar *server;
  };

union _Mailbox
  {
    MailboxType type;
    MailboxCommon common;
    MailboxMBox mbox;
    MailboxPOP3 pop3;
    MailboxIMAP imap;
    MailboxNNTP nntp;
  };


Mailbox * mailbox_new (MailboxType * type);

int mailbox_open (Mailbox * mailbox);

void mailbox_close (Mailbox * mailbox);

void current_mailbox_check ();

#endif /* __mailbox_h__ */
