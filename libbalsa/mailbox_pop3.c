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

static void libbalsa_mailbox_pop3_destroy (GtkObject *object);
static void libbalsa_mailbox_pop3_class_init (LibBalsaMailboxPop3Class *klass);
static void libbalsa_mailbox_pop3_init(LibBalsaMailboxPop3 *mailbox);

static void libbalsa_mailbox_pop3_open (LibBalsaMailbox *mailbox, gboolean append);
static void libbalsa_mailbox_pop3_set_username (LibBalsaMailbox *mailbox, const gchar *username);
static void libbalsa_mailbox_pop3_set_passwd (LibBalsaMailbox *mailbox, const gchar *passwd);
static void libbalsa_mailbox_pop3_set_host (LibBalsaMailbox *mailbox, const gchar *host, gint port);

GtkType
libbalsa_mailbox_pop3_get_type (void)
{
  static GtkType mailbox_type = 0;

  if (!mailbox_type)
    {
      static const GtkTypeInfo mailbox_info =
      {
	"LibBalsaMailboxPOP3",
	sizeof (LibBalsaMailboxPop3),
	sizeof (LibBalsaMailboxPop3Class),
	(GtkClassInitFunc) libbalsa_mailbox_pop3_class_init,
	(GtkObjectInitFunc) libbalsa_mailbox_pop3_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      mailbox_type = gtk_type_unique(libbalsa_mailbox_get_type(), &mailbox_info);
    }

  return mailbox_type;
}

static void
libbalsa_mailbox_pop3_class_init (LibBalsaMailboxPop3Class *klass)
{
  GtkObjectClass *object_class;
  LibBalsaMailboxClass *libbalsa_mailbox_class;

  object_class = GTK_OBJECT_CLASS(klass);
  libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

  parent_class = gtk_type_class(libbalsa_mailbox_get_type());

  object_class->destroy = libbalsa_mailbox_pop3_destroy;

  libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_pop3_open;

  libbalsa_mailbox_class->set_username = libbalsa_mailbox_pop3_set_username;
  libbalsa_mailbox_class->set_password = libbalsa_mailbox_pop3_set_passwd;
  libbalsa_mailbox_class->set_host = libbalsa_mailbox_pop3_set_host;

}

static void
libbalsa_mailbox_pop3_init(LibBalsaMailboxPop3 *mailbox)
{
  mailbox->check = FALSE;
  mailbox->delete_from_server = FALSE;
}

static void
libbalsa_mailbox_pop3_destroy (GtkObject *object)
{
  LibBalsaMailboxPop3 *mailbox = LIBBALSA_MAILBOX_POP3(object);

  if (!mailbox)
    return;

  g_free (mailbox->host);
  g_free (mailbox->user);
  g_free (mailbox->passwd);

  g_free (mailbox->last_popped_uid);

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

GtkObject *libbalsa_mailbox_pop3_new(void)
{
  LibBalsaMailbox *mailbox;

  mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_POP3);

  mailbox->type = MAILBOX_POP3;

  return GTK_OBJECT(mailbox);
}

static void libbalsa_mailbox_pop3_open (LibBalsaMailbox *mailbox, gboolean append)
{
  LibBalsaMailboxPop3 *pop;
  
  g_return_if_fail ( LIBBALSA_IS_MAILBOX_POP3(mailbox) );

  LOCK_MAILBOX (mailbox);


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

  pop = LIBBALSA_MAILBOX_POP3(mailbox);

  if ( append )
    CLIENT_CONTEXT (mailbox) = mx_open_mailbox (pop->host, M_APPEND, NULL);
  else
    CLIENT_CONTEXT (mailbox) = mx_open_mailbox (pop->host, 0, NULL);

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
    g_print (_("LibBalsaMailboxPop3: Opening %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif
    
  }
  
  UNLOCK_MAILBOX (mailbox);

}

static void libbalsa_mailbox_pop3_set_username (LibBalsaMailbox *mailbox, const gchar *username)
{
  LibBalsaMailboxPop3 *pop3;
  g_return_if_fail ( LIBBALSA_IS_MAILBOX_POP3(mailbox) );

  pop3 = LIBBALSA_MAILBOX_POP3(mailbox);

  g_free(pop3->user);
  pop3->user = g_strdup(username);
}

static void libbalsa_mailbox_pop3_set_passwd (LibBalsaMailbox *mailbox, const gchar *passwd)
{
  LibBalsaMailboxPop3 *pop3;
  g_return_if_fail ( LIBBALSA_IS_MAILBOX_POP3(mailbox) );

  pop3 = LIBBALSA_MAILBOX_POP3(mailbox);

  g_free(pop3->passwd);
  pop3->passwd = g_strdup(passwd);
}

static void libbalsa_mailbox_pop3_set_host (LibBalsaMailbox *mailbox, const gchar *host, gint port)
{
  LibBalsaMailboxPop3 *pop3;
  g_return_if_fail ( LIBBALSA_IS_MAILBOX_POP3(mailbox) );

  pop3 = LIBBALSA_MAILBOX_POP3(mailbox);

  g_free(pop3->host);
  pop3->host = g_strdup(host);
  pop3->port = port;
}
