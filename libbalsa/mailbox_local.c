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

#include "libbalsa.h"

static MailboxClass *parent_class = NULL;
//static guint mailbox_signals[LAST_SIGNAL] = { 0 };

static void balsa_mailbox_local_class_init (MailboxLocalClass *klass);
static void balsa_mailbox_local_init(MailboxLocal *mailbox);
static void balsa_mailbox_local_destroy (GtkObject *object);

GtkType
balsa_mailbox_local_get_type (void)
{
  static GtkType mailbox_type = 0;

  if (!mailbox_type)
    {
      static const GtkTypeInfo mailbox_info =
      {
	"MailboxLocal",
	sizeof (MailboxLocal),
	sizeof (MailboxLocalClass),
	(GtkClassInitFunc) balsa_mailbox_local_class_init,
	(GtkObjectInitFunc) balsa_mailbox_local_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      mailbox_type = gtk_type_unique(balsa_mailbox_get_type(), &mailbox_info);
    }

  return mailbox_type;
}

static void
balsa_mailbox_local_class_init (MailboxLocalClass *klass)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) klass;

  parent_class = gtk_type_class(balsa_mailbox_get_type());

  object_class->destroy = balsa_mailbox_local_destroy;
}

static void
balsa_mailbox_local_init(MailboxLocal *mailbox)
{
  mailbox->path = NULL;
}

static void
balsa_mailbox_local_destroy (GtkObject *object)
{
  MailboxLocal *mailbox = BALSA_MAILBOX_LOCAL(object);

  if (!mailbox)
    return;

  g_free(mailbox->path);

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}
