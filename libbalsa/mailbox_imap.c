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

static void libbalsa_mailbox_imap_destroy (GtkObject *object);
static void libbalsa_mailbox_imap_class_init (LibBalsaMailboxImapClass *klass);
static void libbalsa_mailbox_imap_init(LibBalsaMailboxImap *mailbox);

static void libbalsa_mailbox_imap_open (LibBalsaMailbox *mailbox, gboolean append);
static void libbalsa_mailbox_imap_set_username (LibBalsaMailbox *mailbox, const gchar *username);
static void libbalsa_mailbox_imap_set_passwd (LibBalsaMailbox *mailbox, const gchar *passwd);
static void libbalsa_mailbox_imap_set_host (LibBalsaMailbox *mailbox, const gchar *host, gint port);

static void set_mutt_username (LibBalsaMailboxImap *mailbox);

GtkType
libbalsa_mailbox_imap_get_type (void)
{
  static GtkType mailbox_type = 0;

  if (!mailbox_type)
    {
      static const GtkTypeInfo mailbox_info =
      {
	"LibBalsaMailboxImap",
	sizeof (LibBalsaMailboxImap),
	sizeof (LibBalsaMailboxImapClass),
	(GtkClassInitFunc) libbalsa_mailbox_imap_class_init,
	(GtkObjectInitFunc) libbalsa_mailbox_imap_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      mailbox_type = gtk_type_unique(libbalsa_mailbox_get_type(), &mailbox_info);
    }

  return mailbox_type;
}

static void
libbalsa_mailbox_imap_class_init (LibBalsaMailboxImapClass *klass)
{
  GtkObjectClass *object_class;
  LibBalsaMailboxClass *libbalsa_mailbox_class;

  object_class = GTK_OBJECT_CLASS(klass);
  libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

  parent_class = gtk_type_class(libbalsa_mailbox_get_type());

  object_class->destroy = libbalsa_mailbox_imap_destroy;

  libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_imap_open;

  libbalsa_mailbox_class->set_username = libbalsa_mailbox_imap_set_username;
  libbalsa_mailbox_class->set_password = libbalsa_mailbox_imap_set_passwd;
  libbalsa_mailbox_class->set_host = libbalsa_mailbox_imap_set_host;

}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap *mailbox)
{
  mailbox->path = NULL;
  mailbox->tmp_file_path = NULL;

  mailbox->host = NULL;
  mailbox->user = NULL;
  mailbox->passwd = NULL;
}

static void
libbalsa_mailbox_imap_destroy (GtkObject *object)
{
  LibBalsaMailboxImap *mailbox; 

  g_return_if_fail ( LIBBALSA_IS_MAILBOX_IMAP (object) );

  mailbox = LIBBALSA_MAILBOX_IMAP(object);

  g_free (mailbox->host);
  g_free (mailbox->user);
  g_free (mailbox->passwd);

  g_free(mailbox->path);

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

GtkObject*
libbalsa_mailbox_imap_new(void)
{
  LibBalsaMailbox *mailbox;
  mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_IMAP);

  mailbox->type = MAILBOX_IMAP;
  return GTK_OBJECT(mailbox);
}

/* This needs a better name */
static void
set_mutt_username (LibBalsaMailboxImap * mb)
{
  g_return_if_fail (LIBBALSA_IS_MAILBOX_IMAP(mb));

  ImapUser = LIBBALSA_MAILBOX_IMAP(mb)->user;
  ImapPass = LIBBALSA_MAILBOX_IMAP(mb)->passwd;
}

static void libbalsa_mailbox_imap_open (LibBalsaMailbox *mailbox, gboolean append)
{
  LibBalsaMailboxImap *imap;
  gchar *tmp;

  g_return_if_fail ( LIBBALSA_IS_MAILBOX_IMAP (mailbox) );

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

  imap = LIBBALSA_MAILBOX_IMAP(mailbox);

  tmp = g_strdup_printf("{%s:%i}%s", 
			imap->host,
			imap->port,
			imap->path);

  set_mutt_username ( imap );

  if ( append ) 
    CLIENT_CONTEXT (mailbox) = mx_open_mailbox (tmp, M_APPEND, NULL);
  else
    CLIENT_CONTEXT (mailbox) = mx_open_mailbox (tmp, 0, NULL);

  g_free (tmp);

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
      g_print (_("LibBalsaMailboxImap: Opening %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif

    }

  UNLOCK_MAILBOX (mailbox);

}

static void libbalsa_mailbox_imap_set_username (LibBalsaMailbox *mailbox, const gchar *username)
{
  LibBalsaMailboxImap *imap;
  g_return_if_fail ( LIBBALSA_IS_MAILBOX_IMAP(mailbox) );

  imap = LIBBALSA_MAILBOX_IMAP(mailbox);

  g_free(imap->user);
  imap->user = g_strdup(username);
}

static void libbalsa_mailbox_imap_set_passwd (LibBalsaMailbox *mailbox, const gchar *passwd)
{
  LibBalsaMailboxImap *imap;
  g_return_if_fail ( LIBBALSA_IS_MAILBOX_IMAP(mailbox) );

  imap = LIBBALSA_MAILBOX_IMAP(mailbox);

  g_free(imap->passwd);
  imap->passwd = g_strdup(passwd);
}

static void libbalsa_mailbox_imap_set_host (LibBalsaMailbox *mailbox, const gchar *host, gint port)
{
  LibBalsaMailboxImap *imap;
  g_return_if_fail ( LIBBALSA_IS_MAILBOX_IMAP(mailbox) );

  imap = LIBBALSA_MAILBOX_IMAP(mailbox);

  g_free(imap->host);
  imap->host = g_strdup(host);
  imap->port = port;
}

#if 0
/* mailbox_imap_has_new_messages:
   returns non-zero when the IMAP mbox in question has new messages.
   should it load new messages, too?

   REMARK: imap is now checked as ordinary file mailboxes, via Buffy system.
*/
gint
mailbox_imap_has_new_messages(LibBalsaMailboxIMAP *mailbox)
{
    gint res;
    gchar * tmp;

    g_assert(mailbox!=NULL);

    if(MAILBOX(mailbox)->has_unread_messages)
	return MAILBOX(mailbox)->has_unread_messages;

    tmp = g_strdup_printf("{%s:%i}%s", 
			  mailbox->server->host,
			  mailbox->server->port,
			  mailbox->path);
    libbalsa_mailbox_imap_set_username ( mailbox );
    res = imap_buffy_check (tmp);
    g_free(tmp);
    /* if(res) MAILBOX(mailbox)->has_unread_messages = res; */
    return res;
}
#endif
