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

#ifndef __balsa_mailbox_local_h__
#define __balsa_mailbox_local_h__

#define BALSA_TYPE_MAILBOX_LOCAL	       (balsa_mailbox_local_get_type())
#define BALSA_MAILBOX_LOCAL(obj)	       (GTK_CHECK_CAST (obj, BALSA_TYPE_MAILBOX_LOCAL, MailboxLocal))
#define BALSA_MAILBOX_LOCAL_CLASS(klass)       (GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_MAILBOX_LOCAL, MailboxLocalClass))
#define BALSA_IS_MAILBOX_LOCAL(obj)	       (GTK_CHECK_TYPE (obj, BALSA_TYPE_MAILBOX_LOCAL))
#define BALSA_IS_MAILBOX_LOCAL_CLASS(klass)    (GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_MAILBOX_LOCAL))

GtkType balsa_mailbox_local_get_type (void);

typedef struct _MailboxLocal MailboxLocal;
typedef struct _MailboxLocalClass MailboxLocalClass;

struct _MailboxLocal
{
  Mailbox mailbox;
  gchar *path;
};

struct _MailboxLocalClass
{
  MailboxClass klass;
};

#endif /* __balsa_mailbox_local_h__ */
