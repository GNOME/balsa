/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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

#ifndef __balsa_mailbox_pop3_h__
#define __balsa_mailbox_pop3_h__

#define BALSA_TYPE_MAILBOX_POP3			(balsa_mailbox_pop3_get_type())
#define BALSA_MAILBOX_POP3(obj)			(GTK_CHECK_CAST (obj, BALSA_TYPE_MAILBOX_POP3, MailboxPOP3))
#define BALSA_MAILBOX_POP3_CLASS(klass)		(GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_MAILBOX_POP3, MailboxPOP3Class))
#define BALSA_IS_MAILBOX_POP3(obj)		(GTK_CHECK_TYPE (obj, BALSA_TYPE_MAILBOX_POP3))
#define BALSA_IS_MAILBOX_POP3_CLASS(klass)	(GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_MAILBOX_POP3))

GtkType balsa_mailbox_pop3_get_type (void);

typedef struct _MailboxPOP3 MailboxPOP3;
typedef struct _MailboxPOP3Class MailboxPOP3Class;

struct _MailboxPOP3
{
  Mailbox mailbox;

  Server *server;

  gboolean check;
  gboolean delete_from_server;
  gchar *last_popped_uid;
};

struct _MailboxPOP3Class
{
  MailboxClass klass;
};

#endif /* __balsa_mailbox_pop3_h__ */
