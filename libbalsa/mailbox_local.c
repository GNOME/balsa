/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
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

#include "config.h"

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_local_class_init (LibBalsaMailboxLocalClass *klass);
static void libbalsa_mailbox_local_init(LibBalsaMailboxLocal *mailbox);
static void libbalsa_mailbox_local_destroy (GtkObject *object);

/*static guint mailbox_signals[LAST_SIGNAL] = { 0 };*/

static void libbalsa_mailbox_local_open(LibBalsaMailbox *mailbox, gboolean append);

GtkType
libbalsa_mailbox_local_get_type (void)
{
  static GtkType mailbox_type = 0;

  if (!mailbox_type)
    {
      static const GtkTypeInfo mailbox_info =
      {
	"LibBalsaMailboxLocal",
	sizeof (LibBalsaMailboxLocal),
	sizeof (LibBalsaMailboxLocalClass),
	(GtkClassInitFunc) libbalsa_mailbox_local_class_init,
	(GtkObjectInitFunc) libbalsa_mailbox_local_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      mailbox_type = gtk_type_unique(libbalsa_mailbox_get_type(), &mailbox_info);
    }

  return mailbox_type;
}

static void
libbalsa_mailbox_local_class_init (LibBalsaMailboxLocalClass *klass)
{
  GtkObjectClass *object_class;
  LibBalsaMailboxClass *libbalsa_mailbox_class;

  object_class = GTK_OBJECT_CLASS(klass);
  libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

  parent_class = gtk_type_class(libbalsa_mailbox_get_type());

  object_class->destroy = libbalsa_mailbox_local_destroy;

  libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_local_open;

}

static void
libbalsa_mailbox_local_init(LibBalsaMailboxLocal *mailbox)
{
  mailbox->path = NULL;
}

static void
libbalsa_mailbox_local_destroy (GtkObject *object)
{
  LibBalsaMailboxLocal *mailbox = LIBBALSA_MAILBOX_LOCAL(object);

  if (!mailbox)
    return;

  g_free(mailbox->path);

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

GtkObject* libbalsa_mailbox_local_new(LibBalsaMailboxType type)
{
  LibBalsaMailbox *mailbox;

  switch (type) {
  case MAILBOX_MBOX:
  case MAILBOX_MH:
  case MAILBOX_MAILDIR:
    break;
  default:
    g_warning ("Unknown local mailbox type\n");
    return NULL;
  }
    
  mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_LOCAL);

  mailbox->type = type;
  return GTK_OBJECT(mailbox);

}

static void libbalsa_mailbox_local_open(LibBalsaMailbox *mailbox, gboolean append)
{
  struct stat st;
  LibBalsaMailboxLocal *local;

  g_return_if_fail ( LIBBALSA_IS_MAILBOX_LOCAL(mailbox) );

  LOCK_MAILBOX (mailbox);

  local = LIBBALSA_MAILBOX_LOCAL(mailbox);

  if (CLIENT_CONTEXT_OPEN (mailbox))
  {
    if ( append ) 
    {
      /* we need the mailbox to be opened fresh i think */
      mx_close_mailbox( CLIENT_CONTEXT(mailbox), NULL);
    } else {
      /* incriment the reference count */
      mailbox->open_ref++;
      
      UNLOCK_MAILBOX (mailbox);
      return;
    }
  }

  if (stat (local->path, &st) == -1)
  {
    UNLOCK_MAILBOX (mailbox);
    return;
  }

  if ( append ) 
    CLIENT_CONTEXT (mailbox) = mx_open_mailbox (local->path, M_APPEND, NULL);
  else
    CLIENT_CONTEXT (mailbox) = mx_open_mailbox (local->path, 0, NULL);

  if (CLIENT_CONTEXT_OPEN (mailbox))
    {
      mailbox->messages = 0;
      mailbox->total_messages = 0;
      mailbox->unread_messages = 0;
      mailbox->has_unread_messages = FALSE; /* has_unread_messages will be reset
					       by load_messages anyway */
      mailbox->new_messages = CLIENT_CONTEXT (mailbox)->msgcount;
      libbalsa_mailbox_load_messages (mailbox); /*0*/

      /* increment the reference count */
      mailbox->open_ref++;

#ifdef DEBUG
      g_print (_("LibBalsaMailboxLocal: Opening %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif

      UNLOCK_MAILBOX (mailbox);
    }
  
  UNLOCK_MAILBOX (mailbox);

}

