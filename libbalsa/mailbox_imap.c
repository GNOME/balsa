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
static FILE* libbalsa_mailbox_imap_get_message_stream (LibBalsaMailbox *mailbox, LibBalsaMessage *message);
static void libbalsa_mailbox_imap_check (LibBalsaMailbox *mailbox);

static void server_settings_changed(LibBalsaServer *server, LibBalsaMailbox *mailbox);
static void server_user_settings_changed_cb(LibBalsaServer *server, gchar* string, LibBalsaMailbox *mailbox);
static void server_host_settings_changed_cb(LibBalsaServer *server, gchar* host, gint port, LibBalsaMailbox *mailbox);

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

      mailbox_type = gtk_type_unique(libbalsa_mailbox_remote_get_type(), &mailbox_info);
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

  parent_class = gtk_type_class(libbalsa_mailbox_remote_get_type());

  object_class->destroy = libbalsa_mailbox_imap_destroy;

  libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_imap_open;
  libbalsa_mailbox_class->get_message_stream = libbalsa_mailbox_imap_get_message_stream;

  libbalsa_mailbox_class->check = libbalsa_mailbox_imap_check;
}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap *mailbox)
{
  LibBalsaMailboxRemote *remote;
  mailbox->path = NULL;
  mailbox->tmp_file_path = NULL;

  remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
  remote->server = LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_POP3));

  gtk_signal_connect(GTK_OBJECT(remote->server), "set-username",
		     GTK_SIGNAL_FUNC(server_user_settings_changed_cb), (gpointer)mailbox);
  gtk_signal_connect(GTK_OBJECT(remote->server), "set-password",
		     GTK_SIGNAL_FUNC(server_user_settings_changed_cb), (gpointer)mailbox);
  gtk_signal_connect(GTK_OBJECT(remote->server), "set-host",
		     GTK_SIGNAL_FUNC(server_host_settings_changed_cb), (gpointer)mailbox);
}

static void
libbalsa_mailbox_imap_destroy (GtkObject *object)
{
  LibBalsaMailboxImap *mailbox; 
  LibBalsaMailboxRemote *remote;

  g_return_if_fail ( LIBBALSA_IS_MAILBOX_IMAP (object) );

  mailbox = LIBBALSA_MAILBOX_IMAP(object);

  remote = LIBBALSA_MAILBOX_REMOTE(object);

  g_free(mailbox->path);

  libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));

  gtk_object_destroy(GTK_OBJECT(remote->server));

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

GtkObject*
libbalsa_mailbox_imap_new(void)
{
  LibBalsaMailbox *mailbox;
  mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_IMAP);

  return GTK_OBJECT(mailbox);
}

/* This needs a better name */
static void
set_mutt_username (LibBalsaMailboxImap * mb)
{
  g_return_if_fail (LIBBALSA_IS_MAILBOX_IMAP(mb));

  ImapUser = LIBBALSA_MAILBOX_REMOTE_SERVER(mb)->user;
  ImapPass = LIBBALSA_MAILBOX_REMOTE_SERVER(mb)->passwd;
}

/* Unregister an old notification and add a current one */
static void
server_settings_changed(LibBalsaServer *server, LibBalsaMailbox *mailbox)
{
  libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));

  if ( server->user && server->passwd && server->host )
    libbalsa_notify_register_mailbox(mailbox);
}

static void 
server_user_settings_changed_cb(LibBalsaServer *server, gchar* string, LibBalsaMailbox *mailbox)
{
  server_settings_changed(server, mailbox);
}

static void
server_host_settings_changed_cb(LibBalsaServer *server, gchar* host, gint port, LibBalsaMailbox *mailbox)
{
  server_settings_changed(server, mailbox);
}

static void 
libbalsa_mailbox_imap_open (LibBalsaMailbox *mailbox, gboolean append)
{
  LibBalsaMailboxImap *imap;
  LibBalsaServer *server;

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
  server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

  tmp = g_strdup_printf("{%s:%i}%s", 
			server->host,
			server->port,
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
      mailbox->new_messages = CLIENT_CONTEXT (mailbox)->msgcount;
      libbalsa_mailbox_load_messages (mailbox);

      /* increment the reference count */
      mailbox->open_ref++;

#ifdef DEBUG
      g_print (_("LibBalsaMailboxImap: Opening %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif

    }

  UNLOCK_MAILBOX (mailbox);

}

static FILE*
libbalsa_mailbox_imap_get_message_stream (LibBalsaMailbox *mailbox, LibBalsaMessage *message)
{
  FILE *stream;

  g_return_val_if_fail (LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);
  g_return_val_if_fail (LIBBALSA_IS_MESSAGE (message), NULL);

  stream = fopen (LIBBALSA_MAILBOX_IMAP (message->mailbox)->tmp_file_path, "r");
  
  return stream;
}

static void libbalsa_mailbox_imap_check (LibBalsaMailbox *mailbox)
{
  if ( mailbox->open_ref == 0 )
  {
    mailbox->new_messages = libbalsa_notify_check_mailbox(mailbox);
  }
  else
  {
    gint i = 0;
    gint index_hint;

    LOCK_MAILBOX(mailbox);
    
    index_hint = CLIENT_CONTEXT (mailbox)->vcount;

    if ((i = mx_check_mailbox (CLIENT_CONTEXT (mailbox), &index_hint, 0)) < 0)
    {
      UNLOCK_MAILBOX (mailbox);
      g_print ("error or something\n");
    }
    else if (i == M_NEW_MAIL || i == M_REOPENED)
    {
      /* g_print ("got new mail! yippie!\n"); */
      mailbox->new_messages = CLIENT_CONTEXT (mailbox)->msgcount - mailbox->messages;
      
      if (mailbox->new_messages > 0)
      {
#ifndef BALSA_USE_THREADS
	libbalsa_mailbox_load_messages (mailbox);
#endif
      }
    UNLOCK_MAILBOX (mailbox);
    }
  }

  /* FIXME: Could emit a signal here. To signify that there are new messages in this mailbox
   * If the mailbox if open there is a signal for each new message, but if it is closed then
   * is no signal 
   */

}
