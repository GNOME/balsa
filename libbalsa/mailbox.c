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

#include <glib.h>
#include <stdarg.h>
#include <ctype.h>
/* this should be removed.  it is only used for _() for internationalzation */
#include <gnome.h>

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <dirent.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#else
#include "src/save-restore.h" /*config_mailbox_update*/
#endif

/* Class functions */
static void libbalsa_mailbox_class_init (LibBalsaMailboxClass *klass);
static void libbalsa_mailbox_init(LibBalsaMailbox *mailbox);
static void libbalsa_mailbox_destroy (GtkObject *object);

static void libbalsa_mailbox_real_close(LibBalsaMailbox *mailbox);

/* Callbacks */
static void message_status_changed_cb (LibBalsaMessage *message, LibBalsaMailbox *mb );

static LibBalsaMessage *translate_message (HEADER * cur);

#ifdef BALSA_USE_THREADS
static void error_in_thread( const char *format, ... );

static void error_in_thread( const char *format, ... )
{
	va_list val;
	gchar *submessage, *message;
	MailThreadMessage *tmsg;

	va_start( val, format );
	submessage = g_strdup_vprintf( format, val );
	va_end( val );

	message = g_strconcat( _("Error: "), submessage, NULL );
	g_free( submessage );

	MSGMAILTHREAD( tmsg, MSGMAILTHREAD_ERROR, message );
	g_free( message );
}
#endif

void
check_all_pop3_hosts (LibBalsaMailbox *to, GList *mailboxes)
{
  GList *list;
  LibBalsaMailbox *mailbox;
  char uid[80];

#ifdef BALSA_USE_THREADS
  char msgbuf[160];
  MailThreadMessage *threadmsg;
  void (*mutt_error_backup)( const char *, ... );

  /* Otherwise we get multithreaded GTK+ calls... baaad */
  mutt_error_backup = mutt_error;
  mutt_error = error_in_thread;

/*  Only check if lock has been set */
  pthread_mutex_lock( &mailbox_lock);
  if( !checking_mail )
  {
    pthread_mutex_unlock( &mailbox_lock);
    return;
  }
  pthread_mutex_unlock( &mailbox_lock );
#endif BALSA_USE_THREADS

/*    balsa_error_toggle_fatality( FALSE ); */

  list = g_list_first (mailboxes);
  
  /* 1. Spoolfile is set in mailbox_init; 
     2. the 'to' folder doesn't have to be local, can be IMAP
     3. remove this comment when its meaning become obvious.
     Spoolfile = MAILBOX_LOCAL (to)->path;
  */
     
  while (list)
  {
    mailbox = list->data;
    if (LIBBALSA_MAILBOX_POP3 (mailbox)->check)
    {
      PopHost = g_strdup (LIBBALSA_MAILBOX_POP3(mailbox)->host);
      PopPort = (LIBBALSA_MAILBOX_POP3(mailbox)->port);
      PopPass = g_strdup (LIBBALSA_MAILBOX_POP3(mailbox)->passwd);
      PopUser = g_strdup (LIBBALSA_MAILBOX_POP3(mailbox)->user);

#ifdef BALSA_USE_THREADS
      sprintf( msgbuf, "POP3: %s", LIBBALSA_MAILBOX_POP3(mailbox)->mailbox.name );
      MSGMAILTHREAD( threadmsg, MSGMAILTHREAD_SOURCE, msgbuf );
#endif
 
      if( LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid == NULL)
        uid[0] = 0;
      else
        strcpy( uid, LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid );

      PopUID = uid;

      /* Delete it if necessary */
      if (LIBBALSA_MAILBOX_POP3 (mailbox)->delete_from_server)
      {
        set_option(OPTPOPDELETE);
      }
      else
      {
        unset_option(OPTPOPDELETE);
      }

      mutt_fetchPopMail ();
      g_free (PopHost);
      g_free (PopPass);
      g_free (PopUser);

      if( LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid == NULL ||
         strcmp(LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid, uid) != 0)
      {
	      if( LIBBALSA_MAILBOX_POP3( mailbox )->last_popped_uid )
		      g_free ( LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid );
        LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid = g_strdup ( uid );

#ifdef BALSA_USE_THREADS
	threadmsg = g_new (MailThreadMessage, 1);
	threadmsg->message_type = MSGMAILTHREAD_UPDATECONFIG;
	threadmsg->mailbox = (void *) mailbox;
	/*  LIBBALSA_MAILBOX_POP3(mailbox)->mailbox.name */
        write( mail_thread_pipes[1], (void *) &threadmsg, sizeof(void *) );
#else
	config_mailbox_update( 
	    mailbox, mailbox_get_pkey(mailbox) );
#endif
      }
    }
    list = list->next;
  }

#ifdef BALSA_USE_THREADS
  mutt_error = mutt_error_backup;
#endif

/*    balsa_error_toggle_fatality( TRUE ); */
  return;
}

/* mailbox_add_for_checking:
   adds given mailbox to the list of mailboxes to be checked for modification
   via mutt's buffy mechanism.
*/
void
mailbox_add_for_checking (LibBalsaMailbox * mailbox)
{
  BUFFY *tmp;
  gchar *path, *user, *passwd;

  g_return_if_fail(mailbox != NULL);

  if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
      path = LIBBALSA_MAILBOX_LOCAL (mailbox)->path;
      user = passwd = NULL;
  }
  else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) && LIBBALSA_MAILBOX_IMAP(mailbox)->user 
           && LIBBALSA_MAILBOX_IMAP(mailbox)->passwd) {
      path = g_strdup_printf("{%s:%i}%s", 
			     LIBBALSA_MAILBOX_IMAP(mailbox)->host,
			     LIBBALSA_MAILBOX_IMAP(mailbox)->port,
			     LIBBALSA_MAILBOX_IMAP(mailbox)->path);
      user   = LIBBALSA_MAILBOX_IMAP(mailbox)->user;
      passwd = LIBBALSA_MAILBOX_IMAP(mailbox)->passwd;
  } else 
      return;

  tmp = buffy_add_mailbox(path, user, passwd);
  
  if(LIBBALSA_IS_MAILBOX_IMAP(mailbox)) 
      g_free(path);
}

/* mailbox_have_new_messages:
   assumes that mutt_buffy_notify() has been called - this function
   is expensive and should be called only once 
*/
gint
libbalsa_mailbox_has_new_messages (LibBalsaMailbox * mailbox)
{
  BUFFY *tmp = NULL;
  gchar * path;

  if(LIBBALSA_IS_MAILBOX_LOCAL(mailbox))
      path = g_strdup(LIBBALSA_MAILBOX_LOCAL(mailbox)->path);
  else if(LIBBALSA_IS_MAILBOX_IMAP(mailbox))
      path = g_strdup_printf("{%s:%i}%s", 
			     LIBBALSA_MAILBOX_IMAP(mailbox)->host,
			     LIBBALSA_MAILBOX_IMAP(mailbox)->port,
			     LIBBALSA_MAILBOX_IMAP(mailbox)->path);
  else return FALSE;

  for (tmp = Incoming; tmp; tmp = tmp->next)
  {
      if (strcmp (tmp->path, path) == 0) {
	  g_free(path);
	  return tmp->new;
      }
  }
  return FALSE;
}

enum {
  OPEN_MAILBOX,
  CLOSE_MAILBOX,
  MESSAGE_STATUS_CHANGED,
  MESSAGE_NEW,
  MESSAGE_DELETE,
  SET_USERNAME,
  SET_PASSWORD,
  SET_HOST,
  LAST_SIGNAL
};

static GtkObjectClass *parent_class = NULL;
static guint libbalsa_mailbox_signals[LAST_SIGNAL];

GtkType
libbalsa_mailbox_get_type (void)
{
  static GtkType mailbox_type = 0;

  if (!mailbox_type)
    {
      static const GtkTypeInfo mailbox_info =
      {
	"LibBalsaMailbox",
	sizeof (LibBalsaMailbox),
	sizeof (LibBalsaMailboxClass),
	(GtkClassInitFunc) libbalsa_mailbox_class_init,
	(GtkObjectInitFunc) libbalsa_mailbox_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      mailbox_type = gtk_type_unique (gtk_object_get_type(), &mailbox_info);
    }

  return mailbox_type;
}

static void
libbalsa_mailbox_class_init (LibBalsaMailboxClass *klass)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED] =
    gtk_signal_new ("message-status-changed",
		    GTK_RUN_FIRST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET(LibBalsaMailboxClass, message_status_changed),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1, LIBBALSA_TYPE_MESSAGE);

  libbalsa_mailbox_signals[OPEN_MAILBOX] =
    gtk_signal_new ("open_mailbox",
                    GTK_RUN_FIRST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, open_mailbox),
		    gtk_marshal_NONE__BOOL,
		    GTK_TYPE_NONE, 1, GTK_TYPE_BOOL);

  libbalsa_mailbox_signals[CLOSE_MAILBOX] =
    gtk_signal_new ("close_mailbox",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, close_mailbox),
		    gtk_marshal_NONE__NONE,
		    GTK_TYPE_NONE, 0);

  libbalsa_mailbox_signals[MESSAGE_NEW] =
    gtk_signal_new ("message-new",
		    GTK_RUN_FIRST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, message_new),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1, LIBBALSA_TYPE_MESSAGE);

  libbalsa_mailbox_signals[MESSAGE_DELETE] =
    gtk_signal_new ("message-delete",
		    GTK_RUN_FIRST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, message_delete),
		    gtk_marshal_NONE__POINTER,
		    GTK_TYPE_NONE, 1, LIBBALSA_TYPE_MESSAGE);
  libbalsa_mailbox_signals[SET_USERNAME] =
    gtk_signal_new ("set-username",
		    GTK_RUN_FIRST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, set_username),
		    gtk_marshal_NONE__STRING,
		    GTK_TYPE_NONE, 1, GTK_TYPE_STRING);
  libbalsa_mailbox_signals[SET_PASSWORD] =
    gtk_signal_new ("set-password",
		    GTK_RUN_FIRST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, set_password),
		    gtk_marshal_NONE__STRING,
		    GTK_TYPE_NONE, 1, GTK_TYPE_STRING);
  libbalsa_mailbox_signals[SET_HOST] =
    gtk_signal_new ("set-host",
		    GTK_RUN_FIRST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, set_host),
		    gtk_marshal_NONE__POINTER_INT,
		    GTK_TYPE_NONE, 2, GTK_TYPE_STRING, GTK_TYPE_INT);
  
  gtk_object_class_add_signals (object_class, libbalsa_mailbox_signals, LAST_SIGNAL);

  object_class->destroy = libbalsa_mailbox_destroy;

  klass->open_mailbox = NULL;
  klass->close_mailbox = libbalsa_mailbox_real_close;

  klass->message_status_changed = NULL;
  klass->message_new = NULL;
  klass->message_delete = NULL;

  klass->set_username = NULL;
  klass->set_password = NULL;
  klass->set_host = NULL;
}

static void
libbalsa_mailbox_init(LibBalsaMailbox *mailbox)
{
  mailbox->lock = FALSE;
  mailbox->name = NULL;
  CLIENT_CONTEXT (mailbox) = NULL;
  mailbox->open_ref = 0;
  mailbox->messages = 0;
  mailbox->new_messages = 0;
  mailbox->has_unread_messages = FALSE;
  mailbox->unread_messages = 0;
  mailbox->total_messages = 0;
  mailbox->message_list = NULL;
}

static void 
libbalsa_mailbox_destroy (GtkObject *object)
{
  LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);

  if (!mailbox)
    return;

  if (CLIENT_CONTEXT (mailbox) != NULL)
    while (mailbox->open_ref > 0)
      libbalsa_mailbox_close(mailbox);

  g_free(mailbox->name);
  g_free(mailbox->private);

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

void 
libbalsa_mailbox_open(LibBalsaMailbox *mailbox, gboolean append)
{
  g_return_if_fail(mailbox != NULL);
  g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

  gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[OPEN_MAILBOX], append);
}

void 
libbalsa_mailbox_close(LibBalsaMailbox *mailbox)
{
  g_return_if_fail(mailbox != NULL);
  g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

  gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[CLOSE_MAILBOX]);
}

void 
libbalsa_mailbox_set_username(LibBalsaMailbox *mailbox, const gchar *name)
{
  g_return_if_fail(mailbox != NULL);
  g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

  gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[SET_USERNAME], name);
}

void 
libbalsa_mailbox_set_password(LibBalsaMailbox *mailbox, const gchar *passwd)
{
  g_return_if_fail(mailbox != NULL);
  g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

  gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[SET_PASSWORD], passwd);
}

void 
libbalsa_mailbox_set_host(LibBalsaMailbox *mailbox, const gchar *host, gint port)
{
  g_return_if_fail(mailbox != NULL);
  g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

  gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[SET_HOST], host, port);
}

static void 
libbalsa_mailbox_real_close(LibBalsaMailbox *mailbox)
{
  int check;
#ifdef DEBUG
      g_print (_("LibBalsaMailbox: Closing %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif
  LOCK_MAILBOX (mailbox);

  if (mailbox->open_ref == 0)
    return;

  mailbox->open_ref--;

  if (mailbox->open_ref == 0)
    {
      libbalsa_mailbox_free_messages (mailbox);
      mailbox->messages = 0;
      mailbox->total_messages = 0;
      mailbox->unread_messages = 0;
      mailbox->has_unread_messages = FALSE;
  
      /* now close the mail stream and expunge deleted
       * messages -- the expunge may not have to be done */
      if (CLIENT_CONTEXT_OPEN (mailbox))
      {
	  while( (check=mx_close_mailbox (CLIENT_CONTEXT (mailbox), NULL) )) {
	      UNLOCK_MAILBOX (mailbox);
	      g_print("libbalsa_mailbox_real_close: close failed, retrying...\n");
	      libbalsa_mailbox_check_for_new_messages(mailbox);
	      LOCK_MAILBOX (mailbox);
	  }
	  free (CLIENT_CONTEXT (mailbox));
	  CLIENT_CONTEXT (mailbox) = NULL;
      }
    }

  UNLOCK_MAILBOX (mailbox);
}

void
libbalsa_mailbox_sort (LibBalsaMailbox * mailbox, LibBalsaMailboxSort sort)
{
  mutt_sort_headers (CLIENT_CONTEXT (mailbox), sort);
}

gint
libbalsa_mailbox_check_for_new_messages (LibBalsaMailbox * mailbox)
{
  gint i = 0;
  gint index_hint;

  if (!mailbox)
    return FALSE;

  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);
  RETURN_VAL_IF_CONTEXT_CLOSED (mailbox, FALSE);

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
	  libbalsa_mailbox_load_messages (mailbox); /*1*/
#endif
	  UNLOCK_MAILBOX(mailbox);
	  return TRUE;
	}
      else
	{
	  UNLOCK_MAILBOX (mailbox);
	  return FALSE;
	}
    }
  UNLOCK_MAILBOX (mailbox);
  return FALSE;
}

/*
 * private 
 * PS: called by mail_progress_notify_cb:
 * loads incrementally new messages, if any.
 */
void
libbalsa_mailbox_load_messages (LibBalsaMailbox * mailbox)
{
  glong msgno;
  LibBalsaMessage *message;
  HEADER *cur = 0;

  for (msgno = mailbox->messages;
       mailbox->new_messages > 0;
       msgno++)
    {
      cur = CLIENT_CONTEXT (mailbox)->hdrs[msgno];

      if (!cur)
	continue;
      if (cur->env->subject && 
          !strcmp (cur->env->subject, 
                   "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA")) {
          mailbox->new_messages--;
          mailbox->messages++;
          continue;
      }

      message = translate_message (cur);
      message->mailbox = mailbox;
      message->msgno = msgno;

      gtk_signal_connect ( GTK_OBJECT (message), "clear-flags",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
      gtk_signal_connect ( GTK_OBJECT (message), "set-answered",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
      gtk_signal_connect ( GTK_OBJECT (message), "set-read",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
      gtk_signal_connect ( GTK_OBJECT (message), "set-deleted",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
      gtk_signal_connect ( GTK_OBJECT (message), "set-flagged",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);

      mailbox->messages++;
      mailbox->total_messages++;

      if (!cur->read)
      {
	message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
        
        mailbox->unread_messages++;

      }

      if (cur->deleted)
	message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;

      if (cur->flagged)
	message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;

      if (cur->replied)
	message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;

      mailbox->message_list = g_list_append (mailbox->message_list, message);
      mailbox->new_messages--;
     
      gtk_signal_emit (GTK_OBJECT(mailbox), libbalsa_mailbox_signals[MESSAGE_NEW], message);
    }

  if (mailbox->unread_messages > 0)
    mailbox->has_unread_messages = TRUE;
}


void
libbalsa_mailbox_free_messages (LibBalsaMailbox * mailbox)
{
  GList *list;
  LibBalsaMessage *message;

  list = g_list_first (mailbox->message_list);
  while (list)
    {
      message = list->data;
      list = list->next;

      gtk_signal_emit( GTK_OBJECT(mailbox), libbalsa_mailbox_signals[MESSAGE_DELETE], message);
      gtk_object_destroy (GTK_OBJECT(message));
    }
  g_list_free (mailbox->message_list);
  mailbox->message_list = NULL;
}

LibBalsaMailboxType
libbalsa_mailbox_valid (gchar * filename)
{
  struct stat st;

  if (stat (filename, &st) == -1)
    return MAILBOX_UNKNOWN;

  switch (mx_get_magic (filename))
    {
    case M_MBOX:
      return MAILBOX_MBOX;
      break;
    case M_MMDF:
      return MAILBOX_MBOX;
      break;
    case M_MH:
      return MAILBOX_MH;
      break;
    case M_MAILDIR:
      return MAILBOX_MAILDIR;
      break;
    case M_IMAP:
      return MAILBOX_IMAP;
      break;
    default:
      return MAILBOX_UNKNOWN;
      break;
    }
}

void libbalsa_mailbox_commit_changes( LibBalsaMailbox *mailbox )
{
  GList *message_list;
  GList *tmp_message_list;
  LibBalsaMessage *current_message;

  libbalsa_mailbox_open (mailbox, FALSE);

  /* examine all the message in the mailbox */
  message_list = mailbox->message_list;
  while (message_list)
    {
      current_message = LIBBALSA_MESSAGE(message_list->data);
      tmp_message_list =  message_list->next;
      if ( current_message->flags & LIBBALSA_MESSAGE_FLAG_DELETED ) 
	{
	  gtk_signal_emit( GTK_OBJECT(mailbox), libbalsa_mailbox_signals[MESSAGE_DELETE], current_message);
	  gtk_object_destroy (GTK_OBJECT(current_message));
	  mailbox->message_list = g_list_remove_link( mailbox->message_list, message_list);
	}
      message_list = tmp_message_list;
      
    }

  /* [MBG] This should prevent segfaults */
  /*   if (CLIENT_CONTEXT (mailbox) != NULL) */
  /*     mx_sync_mailbox (CLIENT_CONTEXT(mailbox)); */

  libbalsa_mailbox_close (mailbox);
}

/* internal c-client translation */
static LibBalsaMessage *
translate_message (HEADER * cur)
{
  LibBalsaMessage *message;
  ADDRESS *addy;
  LibBalsaAddress *addr;
  ENVELOPE *cenv;
  LIST *tmp;
  gchar *p;

  if (!cur)
    return NULL;

  cenv = cur->env;

  message = libbalsa_message_new ();

  message->date = cur->date_sent;
  message->from = libbalsa_address_new_from_libmutt (cenv->from);
  message->sender = libbalsa_address_new_from_libmutt (cenv->sender);
  message->reply_to = libbalsa_address_new_from_libmutt (cenv->reply_to);

  for (addy = cenv->to; addy; addy = addy->next)
    {
      addr = libbalsa_address_new_from_libmutt (addy);
      message->to_list = g_list_append (message->to_list, addr);
    }
  for (addy = cenv->cc; addy; addy = addy->next)
    {
      addr = libbalsa_address_new_from_libmutt (addy);
      message->cc_list = g_list_append (message->cc_list, addr);
    }
  for (addy = cenv->bcc; addy; addy = addy->next)
    {
      addr = libbalsa_address_new_from_libmutt (addy);
      message->bcc_list = g_list_append (message->bcc_list, addr);
    }
  
  /* Get fcc from message */
  for (tmp = cenv->userhdrs; tmp; )
    {
      if (mutt_strncasecmp ("X-Mutt-Fcc:", tmp->data, 11) == 0)
        {
          p = tmp->data + 11;
          SKIPWS (p);

	  if (p)
	    message->fcc_mailbox = g_strdup(p);
	  else 
	    message->fcc_mailbox = NULL;
        }
      else if (mutt_strncasecmp ("X-Mutt-Fcc:", tmp->data, 18) == 0)
	/* Is X-Mutt-Fcc correct? */
        {
          p = tmp->data + 18;
          SKIPWS (p);

          message->in_reply_to = g_strdup (p);
        }
      tmp = tmp->next;
    }

  message->subject = g_strdup (cenv->subject);
  message->message_id = g_strdup (cenv->message_id);

  /* more! */

  return message;
}

static void message_status_changed_cb (LibBalsaMessage *message, LibBalsaMailbox *mb )
{
  gtk_signal_emit ( GTK_OBJECT(message->mailbox), libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED], message);
}
