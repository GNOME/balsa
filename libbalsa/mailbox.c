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
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <dirent.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "../src/balsa-app.h"
#include "mailbackend.h"

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "misc.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#else
#include "src/save-restore.h" /*config_mailbox_update*/
#endif

#define BUFFER_SIZE 1024

#define WATCHER_LIST(mailbox) (((MailboxPrivate *)((mailbox)->private))->watcher_list)

/* update the gui (during loading messages, etc */
void (*update_gui_func) (void);
	
/*
 * watcher information
 */
typedef struct
  {
    guint id;
    guint16 mask;
    MailboxWatcherFunc func;
    gpointer data;
  }
MailboxWatcher;

static void balsa_mailbox_class_init (MailboxClass *klass);
static void balsa_mailbox_init(Mailbox *mailbox);
static void balsa_mailbox_destroy (GtkObject *object);
static void balsa_mailbox_real_open_mailbox(Mailbox *mailbox);
static void balsa_mailbox_real_close_mailbox(Mailbox *mailbox);


/* 
 * prototypes
 */
/* static void load_messages (Mailbox * mailbox, gint emit); */
static void free_messages (Mailbox * mailbox);


static gint _mailbox_open_ref (Mailbox * mailbox, gint flag);

/* PKGW: These were static. Why? */
void send_watcher_mark_clear_message (Mailbox * mailbox, Message * message);
void send_watcher_mark_answer_message (Mailbox * mailbox, Message * message);
void send_watcher_mark_read_message (Mailbox * mailbox, Message * message);
void send_watcher_mark_unread_message (Mailbox * mailbox, Message * message);
void send_watcher_mark_delete_message (Mailbox * mailbox, Message * message);
void send_watcher_mark_undelete_message (Mailbox * mailbox, Message * message);
void send_watcher_new_message (Mailbox * mailbox, Message * message, gint remaining);
void send_watcher_delete_message (Mailbox * mailbox, Message * message);
void send_watcher_append_message (Mailbox * mailbox, Message * message);

static Message *translate_message (HEADER * cur);
static Address *translate_address (ADDRESS * caddr);

Server *server_new(ServerType type);
void server_free(Server *server);

/* We're gonna set Mutt global vars here */
void
mailbox_init (gchar * inbox_path,
		void (*error_func) (const char *fmt,...),
		void (*gui_func) (void))
{
  struct utsname utsname;
  char *p;
  gchar *tmp;

  update_gui_func = gui_func;
  
  Spoolfile = inbox_path;

  uname (&utsname);

  Username = g_get_user_name ();

  Homedir = g_get_home_dir ();

  Realname = g_get_real_name ();

  Hostname = g_get_host_name ();

  mutt_error = error_func;

  Fqdn = g_strdup (Hostname);

  Sendmail = SENDMAIL;

  Shell = g_strdup ((p = getenv ("SHELL")) ? p : "/bin/sh");
  Tempdir = g_get_tmp_dir ();

  if (UserHeader)
    UserHeader = UserHeader->next;
  UserHeader = mutt_new_list ();
  tmp = g_malloc (17 + strlen (VERSION));
  snprintf (tmp, 17 + strlen (VERSION), "X-Mailer: Balsa %s", VERSION);
  UserHeader->data = g_strdup (tmp);
  g_free (tmp);
}

gint
set_imap_username (Mailbox * mb)
{
  if (!MAILBOX_IS_IMAP(mb))
    return 0;

  ImapUser = MAILBOX_IMAP(mb)->server->user;
  ImapPass = MAILBOX_IMAP(mb)->server->passwd;

  return 1;
}


void
check_all_imap_hosts (Mailbox * to, GList *mailboxes)
{
#ifdef BALSA_USE_THREADS
/*  Only check if lock has been set */
  pthread_mutex_lock( &mailbox_lock);
  if( !checking_mail )
  {
    pthread_mutex_unlock( &mailbox_lock);
    return;
  }
  pthread_mutex_unlock( &mailbox_lock );

/*  put IMAP code here */


  return;  
#else
/* non-thread code */

#endif
}

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
check_all_pop3_hosts (Mailbox *to, GList *mailboxes)
{
  GList *list;
  Mailbox *mailbox;
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

  balsa_error_toggle_fatality( FALSE );

  list = g_list_first (mailboxes);
  
  /* 1. Spoolfile is set in mailbox_init; 
     2. the 'to' folder doesn't have to be local, can be IMAP
     3. remove this comment when its meaning become obvious.
     Spoolfile = MAILBOX_LOCAL (to)->path;
  */
     
  while (list)
  {
    mailbox = list->data;
    if (MAILBOX_POP3 (mailbox)->check)
    {
      PopHost = g_strdup (MAILBOX_POP3(mailbox)->server->host);
      PopPort = (MAILBOX_POP3(mailbox)->server->port);
      PopPass = g_strdup (MAILBOX_POP3(mailbox)->server->passwd);
      PopUser = g_strdup (MAILBOX_POP3(mailbox)->server->user);

#ifdef BALSA_USE_THREADS
      sprintf( msgbuf, "POP3: %s", MAILBOX_POP3(mailbox)->mailbox.name );
      MSGMAILTHREAD( threadmsg, MSGMAILTHREAD_SOURCE, msgbuf );
#endif
 
      if( MAILBOX_POP3 (mailbox)->last_popped_uid == NULL)
        uid[0] = 0;
      else
        strcpy( uid, MAILBOX_POP3 (mailbox)->last_popped_uid );

      PopUID = uid;

      /* Delete it if necessary */
      if (MAILBOX_POP3 (mailbox)->delete_from_server)
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

      if( MAILBOX_POP3(mailbox)->last_popped_uid == NULL ||
         strcmp(MAILBOX_POP3(mailbox)->last_popped_uid, uid) != 0)
      {
	      if( MAILBOX_POP3( mailbox )->last_popped_uid )
		      g_free ( MAILBOX_POP3 (mailbox)->last_popped_uid );
        MAILBOX_POP3 (mailbox)->last_popped_uid = g_strdup ( uid );

#ifdef BALSA_USE_THREADS
	threadmsg = malloc( sizeof( MailThreadMessage ) );
	threadmsg->message_type = MSGMAILTHREAD_UPDATECONFIG;
	threadmsg->mailbox = (void *) mailbox;
	/*  MAILBOX_POP3(mailbox)->mailbox.name */
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

  balsa_error_toggle_fatality( TRUE );
  return;
}

/* mailbox_add_for_checking:
   adds given mailbox to the list of mailboxes to be checked for modification
   via mutt's buffy mechanism.
*/
void
mailbox_add_for_checking (Mailbox * mailbox)
{
  BUFFY *tmp;
  gchar *path, *user, *passwd;

  g_return_if_fail(mailbox != NULL);

  if (MAILBOX_IS_LOCAL(mailbox)) {
      path = MAILBOX_LOCAL (mailbox)->path;
      user = passwd = NULL;
  }
  else if (MAILBOX_IS_IMAP(mailbox) && MAILBOX_IMAP(mailbox)->server->user 
           && MAILBOX_IMAP(mailbox)->server->passwd) {
      path = g_strdup_printf("{%s:%i}%s", 
			     MAILBOX_IMAP(mailbox)->server->host,
			     MAILBOX_IMAP(mailbox)->server->port,
			     MAILBOX_IMAP(mailbox)->path);
      user   = MAILBOX_IMAP(mailbox)->server->user;
      passwd = MAILBOX_IMAP(mailbox)->server->passwd;
  } else 
      return;

  tmp = buffy_add_mailbox(path, user, passwd);
  
  if(MAILBOX_IS_IMAP(mailbox)) 
      g_free(path);
}

/* mailbox_have_new_messages:
   assumes that mutt_buffy_notify() has been called - this function
   is expensive and should be called only once 
*/


gint
mailbox_have_new_messages (Mailbox * mailbox)
{
  BUFFY *tmp = NULL;
  gchar * path;

  if(MAILBOX_IS_LOCAL(mailbox))
      path = g_strdup(MAILBOX_LOCAL(mailbox)->path);
  else if(MAILBOX_IS_IMAP(mailbox))
      path = g_strdup_printf("{%s:%i}%s", 
			     MAILBOX_IMAP(mailbox)->server->host,
			     MAILBOX_IMAP(mailbox)->server->port,
			     MAILBOX_IMAP(mailbox)->path);
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
  LAST_SIGNAL
};

static GtkObjectClass *parent_class = NULL;
static guint mailbox_signals[LAST_SIGNAL] = { 0 };

GtkType
balsa_mailbox_get_type (void)
{
  static GtkType mailbox_type = 0;

  if (!mailbox_type)
    {
      static const GtkTypeInfo mailbox_info =
      {
	"Mailbox",
	sizeof (Mailbox),
	sizeof (MailboxClass),
	(GtkClassInitFunc) balsa_mailbox_class_init,
	(GtkObjectInitFunc) balsa_mailbox_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      mailbox_type = gtk_type_unique (gtk_object_get_type(), &mailbox_info);
    }

  return mailbox_type;
}

static void
balsa_mailbox_class_init (MailboxClass *klass)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  mailbox_signals[OPEN_MAILBOX] =
    gtk_signal_new ("open_mailbox",
                    GTK_RUN_FIRST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (MailboxClass, open_mailbox),
		    gtk_marshal_NONE__NONE,
		    GTK_TYPE_NONE, 0);

  mailbox_signals[CLOSE_MAILBOX] =
    gtk_signal_new ("close_mailbox",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (MailboxClass, close_mailbox),
		    gtk_marshal_NONE__NONE,
		    GTK_TYPE_NONE, 0);

  gtk_object_class_add_signals (object_class, mailbox_signals, LAST_SIGNAL);

  object_class->destroy = balsa_mailbox_destroy;

  klass->open_mailbox = balsa_mailbox_real_open_mailbox;
  klass->close_mailbox = balsa_mailbox_real_close_mailbox;
}

static void
balsa_mailbox_init(Mailbox *mailbox)
{
  mailbox->lock = FALSE;
  mailbox->name = NULL;
  mailbox->private = (void *) g_malloc (sizeof (MailboxPrivate));
  CLIENT_CONTEXT (mailbox) = NULL;
  WATCHER_LIST (mailbox) = NULL;
  mailbox->open_ref = 0;
  mailbox->messages = 0;
  mailbox->new_messages = 0;
  mailbox->has_unread_messages = FALSE;
  mailbox->unread_messages = 0;
  mailbox->total_messages = 0;
  mailbox->message_list = NULL;
}

GtkObject*
mailbox_new(MailboxType type)
{
  Mailbox *mailbox;

  switch (type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      mailbox = gtk_type_new(BALSA_TYPE_MAILBOX_LOCAL);
      break;

    case MAILBOX_POP3:
      mailbox = gtk_type_new(BALSA_TYPE_MAILBOX_POP3);
      break;

    case MAILBOX_IMAP:
      mailbox = gtk_type_new(BALSA_TYPE_MAILBOX_IMAP);
      break;

    default:
      return NULL;
    }

  mailbox->type = type;
  return GTK_OBJECT(mailbox);
}

static void balsa_mailbox_destroy (GtkObject *object)
{
  Mailbox *mailbox = BALSA_MAILBOX(object);

  if (!mailbox)
    return;

  if (CLIENT_CONTEXT (mailbox) != NULL)
    while (mailbox->open_ref > 0)
      mailbox_open_unref (mailbox);

  g_free(mailbox->name);
  g_free(mailbox->private);

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

Server *
server_new(ServerType type)
{
  Server *server;

  /* we can create the same thing for now */
  server = (Server *)g_malloc(sizeof(Server));
  server->user = NULL;
  server->passwd = NULL;
  server->host = NULL;
  server->port = -1;

  return server;
}

void
server_free(Server *server)
{
  g_free(server->user);
  g_free(server->passwd);
  g_free(server->host);
}



void balsa_mailbox_open(Mailbox *mailbox)
{
  g_return_if_fail(mailbox != NULL);
  g_return_if_fail(BALSA_IS_MAILBOX(mailbox));

  gtk_signal_emit(GTK_OBJECT(mailbox), mailbox_signals[OPEN_MAILBOX], NULL);
}

void balsa_mailbox_close(Mailbox *mailbox)
{
  g_return_if_fail(mailbox != NULL);
  g_return_if_fail(BALSA_IS_MAILBOX(mailbox));

  gtk_signal_emit(GTK_OBJECT(mailbox), mailbox_signals[CLOSE_MAILBOX], NULL);
}



gint
mailbox_open_ref (Mailbox * mailbox)
{
  return _mailbox_open_ref (mailbox, 0);
}

gint
mailbox_open_append (Mailbox * mailbox)
{
  return _mailbox_open_ref (mailbox, M_APPEND);
}



static void balsa_mailbox_real_open_mailbox(Mailbox *mailbox)
{
  _mailbox_open_ref(mailbox, 0);
}

static void balsa_mailbox_real_close_mailbox(Mailbox *mailbox)
{
  mailbox_open_unref(mailbox);
}

static gint
_mailbox_open_ref (Mailbox * mailbox, gint flag)
{
  gchar *tmp;
  struct stat st;
  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);


  if (CLIENT_CONTEXT_OPEN (mailbox))
    {
      if (flag == M_APPEND)
	{
	  
	  /* we need the mailbox to be opened fresh i think */
	  mx_close_mailbox( CLIENT_CONTEXT(mailbox)  );
	  
	} 
      else 
	{
	  /* incriment the reference count */
	  mailbox->open_ref++;
	  
	  UNLOCK_MAILBOX (mailbox);
	  return TRUE;
	}
    }

  if (MAILBOX_IS_LOCAL(mailbox))
    {
      if (stat (MAILBOX_LOCAL (mailbox)->path, &st) == -1)
	{
	  UNLOCK_MAILBOX (mailbox);
	  return FALSE;
	}
    }

  switch (mailbox->type)
    {
      /* add mail dir */
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      CLIENT_CONTEXT (mailbox) = mx_open_mailbox (MAILBOX_LOCAL (mailbox)->path, flag, NULL);
      break;

    case MAILBOX_POP3:
      CLIENT_CONTEXT (mailbox) = mx_open_mailbox (MAILBOX_POP3(mailbox)->server->host, flag, NULL);
      break;

    case MAILBOX_IMAP:
      tmp = g_strdup_printf("{%s:%i}%s", 
			    MAILBOX_IMAP(mailbox)->server->host,
			    MAILBOX_IMAP(mailbox)->server->port,
			    MAILBOX_IMAP(mailbox)->path);
      set_imap_username (mailbox);
      CLIENT_CONTEXT (mailbox) = mx_open_mailbox (tmp, flag, NULL);
      g_free (tmp);
      break;

    case MAILBOX_UNKNOWN:
      break;
    }

  if (CLIENT_CONTEXT_OPEN (mailbox))
    {
      mailbox->messages = 0;
      mailbox->total_messages = 0;
      mailbox->unread_messages = 0;
      mailbox->has_unread_messages = FALSE; /* has_unread_messages will be reset
					       by load_messages anyway */
      mailbox->new_messages = CLIENT_CONTEXT (mailbox)->msgcount;
      load_messages (mailbox, 0);

      /* increment the reference count */
      mailbox->open_ref++;

#ifdef DEBUG
      g_print (_("Mailbox: Opening %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif

      /* FIXME */
/* mailbox_sort(mailbox, MAILBOX_SORT_DATE); */
      UNLOCK_MAILBOX (mailbox);
      return TRUE;
    }
  else
    {
      UNLOCK_MAILBOX (mailbox);
      return FALSE;
    }
}


void
mailbox_open_unref (Mailbox * mailbox)
{
#ifdef DEBUG
      g_print (_("Mailbox: Closing %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif
  LOCK_MAILBOX (mailbox);

  if (mailbox->open_ref == 0)
    return;

  mailbox->open_ref--;

  if (mailbox->open_ref == 0)
    {
      free_messages (mailbox);
      mailbox->messages = 0;
      mailbox->total_messages = 0;
      mailbox->unread_messages = 0;
      mailbox->has_unread_messages = FALSE;
  
      /* now close the mail stream and expunge deleted
       * messages -- the expunge may not have to be done */
      if (CLIENT_CONTEXT_OPEN (mailbox))
	{
	  /* If it closed we have no context. If it didnt close right
	     don't ask me what to do - AC */

	  if (mx_close_mailbox (CLIENT_CONTEXT (mailbox)) == 0)
	    {
	      free (CLIENT_CONTEXT (mailbox));
	      CLIENT_CONTEXT (mailbox) = NULL;
	    }
	}
    }

  UNLOCK_MAILBOX (mailbox);
}

void
mailbox_sort (Mailbox * mailbox, MailboxSort sort)
{
  mutt_sort_headers (CLIENT_CONTEXT (mailbox), sort);
}

gint
mailbox_check_new_messages (Mailbox * mailbox)
{
  gint i = 0;
  gint index_hint;

  if (!mailbox)
    return FALSE;

  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);
  RETURN_VAL_IF_CONTEXT_CLOSED (mailbox, FALSE);

  index_hint = CLIENT_CONTEXT (mailbox)->vcount;

  if ((i = mx_check_mailbox (CLIENT_CONTEXT (mailbox), &index_hint)) < 0)
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
	  load_messages (mailbox, 1);
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


guint
mailbox_watcher_set (Mailbox * mailbox,
		     MailboxWatcherFunc func,
		     guint16 mask,
		     gpointer data)
{
  GList *list;
  MailboxWatcher *watcher;
  guint id;
  gint bumped;


  /* find a unique id */
  id = 0;
  bumped = TRUE;
  while (1)
    {
      list = WATCHER_LIST (mailbox);
      while (list)
	{
	  watcher = list->data;
	  list = list->next;

	  if (watcher->id == id)
	    {
	      id++;
	      bumped = TRUE;
	      break;
	    }
	}

      if (!bumped)
	break;

      bumped = FALSE;
    }


  /* allocate the new watcher */
  watcher = g_malloc (sizeof (MailboxWatcher));
  watcher->id = id;
  watcher->mask = mask;
  watcher->func = func;
  watcher->data = data;


  /* add it */
  WATCHER_LIST (mailbox) = g_list_append (WATCHER_LIST (mailbox), watcher);

  return id;
}


void
mailbox_watcher_remove (Mailbox * mailbox, guint id)
{
  GList *list;
  MailboxWatcher *watcher;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;

      if (id == watcher->id)
	{
	  g_free (watcher);
	  WATCHER_LIST (mailbox) = g_list_remove_link (WATCHER_LIST (mailbox), list);
	  break;
	}

      list = list->next;
    }
}


void
mailbox_watcher_remove_by_data (Mailbox * mailbox, gpointer data)
{
  GList *list;
  MailboxWatcher *watcher;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;

      if (data == watcher->data)
	{
	  g_free (watcher);
	  WATCHER_LIST (mailbox) = g_list_remove_link (WATCHER_LIST (mailbox), list);
	}

      list = list->next;
    }

}


/*
 * private 
 * PS: called by mail_progress_notify_cb:
 * loads incrementally new messages, if any.
 */
void
load_messages (Mailbox * mailbox, gint emit)
{
  glong msgno;
  Message *message;
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
      mailbox->messages++;

      mailbox->total_messages++;

      if (!cur->read)
      {
	message->flags |= MESSAGE_FLAG_NEW;
        
        mailbox->unread_messages++;

      }

      if (cur->deleted)
	message->flags |= MESSAGE_FLAG_DELETED;

      if (cur->flagged)
	message->flags |= MESSAGE_FLAG_FLAGGED;

      if (cur->replied)
	message->flags |= MESSAGE_FLAG_REPLIED;

      mailbox->message_list = g_list_append (mailbox->message_list, message);
      mailbox->new_messages--;
     
      if (emit)
        send_watcher_new_message (mailbox, message, mailbox->new_messages);
    }

  if (mailbox->unread_messages > 0)
    mailbox->has_unread_messages = TRUE;
}


static void
free_messages (Mailbox * mailbox)
{
  GList *list;
  Message *message;

  list = g_list_first (mailbox->message_list);
  while (list)
    {
      message = list->data;
      list = list->next;

      send_watcher_delete_message (mailbox, message);
      message_destroy (message);
    }
  g_list_free (mailbox->message_list);
  mailbox->message_list = NULL;
}


/*
 * sending messages to watchers
 */
void
send_watcher_mark_clear_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_MARK_CLEAR;
  mw_message.mailbox = mailbox;
  mw_message.message = message;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_MARK_CLEAR_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) (&mw_message);
	}
    }
}

void
send_watcher_mark_answer_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_MARK_ANSWER;
  mw_message.mailbox = mailbox;
  mw_message.message = message;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_MARK_ANSWER_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) (&mw_message);
	}
    }
}

void
send_watcher_mark_read_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_MARK_READ;
  mw_message.mailbox = mailbox;
  mw_message.message = message;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_MARK_READ_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) (&mw_message);
	}
    }
}

void
send_watcher_mark_unread_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_MARK_UNREAD;
  mw_message.mailbox = mailbox;
  mw_message.message = message;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_MARK_UNREAD_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) (&mw_message);
	}
    }
}

void
send_watcher_mark_delete_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_MARK_DELETE;
  mw_message.mailbox = mailbox;
  mw_message.message = message;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_MARK_DELETE_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) (&mw_message);
	}
    }
}


void
send_watcher_mark_undelete_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_MARK_UNDELETE;
  mw_message.mailbox = mailbox;
  mw_message.message = message;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_MARK_UNDELETE_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) (&mw_message);
	}
    }
}


void
send_watcher_new_message (Mailbox * mailbox, Message * message, gint remaining)
{
  GList *list;
  MailboxWatcherMessageNew mw_new_message;
  MailboxWatcher *watcher;

  mw_new_message.type = MESSAGE_NEW;
  mw_new_message.mailbox = mailbox;
  mw_new_message.message = message;

  mw_new_message.remaining = remaining;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_NEW_MASK)
	{
	  mw_new_message.data = watcher->data;
	  (*watcher->func) ((MailboxWatcherMessage *) & mw_new_message);
	}
    }
}

void
send_watcher_append_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage  mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_APPEND;
  mw_message.mailbox = mailbox;
  mw_message.message = message;


  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_APPEND_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) ((MailboxWatcherMessage *) & mw_message);
	}
    }
}


void
send_watcher_delete_message (Mailbox * mailbox, Message * message)
{
  GList *list;
  MailboxWatcherMessage mw_message;
  MailboxWatcher *watcher;

  mw_message.type = MESSAGE_DELETE;
  mw_message.mailbox = mailbox;
  mw_message.message = message;

  list = WATCHER_LIST (mailbox);
  while (list)
    {
      watcher = list->data;
      list = list->next;

      if (watcher->mask & MESSAGE_DELETE_MASK)
	{
	  mw_message.data = watcher->data;
	  (*watcher->func) (&mw_message);
	}
    }
}



/*
 * MISC
 */
MailboxType
mailbox_type_from_description (gchar * description)
{
  if (!strcmp (description, "mbox"))
    return MAILBOX_MBOX;

  else if (!strcmp (description, "mh"))
    return MAILBOX_MH;

  else if (!strcmp (description, "maildir"))
    return MAILBOX_MAILDIR;

  else if (!strcmp (description, "pop3"))
    return MAILBOX_POP3;

  else if (!strcmp (description, "imap"))
    return MAILBOX_IMAP;

  /* if no match */
  return MAILBOX_UNKNOWN;
}


gchar *
mailbox_type_description (MailboxType type)
{
  switch (type)
    {
    case MAILBOX_MBOX:
      return "mbox";
      break;

    case MAILBOX_MH:
      return "mh";
      break;

    case MAILBOX_MAILDIR:
      return "maildir";
      break;

    case MAILBOX_POP3:
      return "pop3";
      break;

    case MAILBOX_IMAP:
      return "imap";
      break;

    case MAILBOX_UNKNOWN:
    default:
      return "";
    }
}


MailboxType
mailbox_valid (gchar * filename)
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

/**
 * mailbox_gather_content_info:
 *
 * @mailbox : the mailbox to scan
 *
 * gather informations about the content of a mailbox such as the
 * total nuber of messages, the number of unread messages 
 * 
 *
 * Return value: if the mailbox could be scanned, returns true. 
 **/
gboolean
mailbox_gather_content_info(Mailbox *mailbox)
{
	/* this code is far too slow, and mutt does not provide a good way to
	 * do this.  we will not use it for now */
  GList *message_list;
  Message *current_message;

  mailbox_open_ref (mailbox);

  mailbox->total_messages = 0;
  mailbox->unread_messages = 0;

  /* examine all the message in the mailbox */
  message_list = mailbox->message_list;
  while (message_list)
    {
      current_message = (Message *) message_list->data;
      if ( current_message->flags & MESSAGE_FLAG_NEW ) 
	mailbox->unread_messages++ ;
      mailbox->total_messages++ ;
      message_list = message_list->next;
      
    }

  mailbox_open_unref (mailbox);

  return TRUE;
}


void mailbox_commit_flagged_changes( Mailbox *mailbox )
{
  GList *message_list;
  GList *tmp_message_list;
  Message *current_message;


  mailbox_open_ref (mailbox);

  /* examine all the message in the mailbox */
  message_list = mailbox->message_list;
  while (message_list)
    {
      current_message = (Message *) message_list->data;
      tmp_message_list =  message_list->next;
      if ( current_message->flags & MESSAGE_FLAG_DELETED ) 
	{
	   send_watcher_delete_message (mailbox, current_message);
	   message_destroy (current_message);
	   mailbox->message_list = g_list_remove_link( mailbox->message_list, message_list);
	}
      message_list = tmp_message_list;
      
    }

  /* [MBG] This should prevent segfaults */
/*   if (CLIENT_CONTEXT (mailbox) != NULL) */
/*     mx_sync_mailbox (CLIENT_CONTEXT(mailbox)); */

  mailbox_open_unref (mailbox);
}

/* internal c-client translation */
static Message *
translate_message (HEADER * cur)
{
  Message *message;
  ADDRESS *addy;
  Address *addr;
  ENVELOPE *cenv;
  LIST *tmp;
  gchar rettime[128], *p;
  struct tm *footime;

  if (!cur)
    return NULL;

  cenv = cur->env;

  message = message_new ();

  footime = localtime (&cur->date_sent);

  strftime (rettime, sizeof (rettime), balsa_app.date_string, footime);

  message->datet = cur->date_sent;
  message->date = g_strdup (rettime);
  message->from = translate_address (cenv->from);
  message->sender = translate_address (cenv->sender);
  message->reply_to = translate_address (cenv->reply_to);

  for (addy = cenv->to; addy; addy = addy->next)
    {
      addr = translate_address (addy);
      message->to_list = g_list_append (message->to_list, addr);
    }
  for (addy = cenv->cc; addy; addy = addy->next)
    {
      addr = translate_address (addy);
      message->cc_list = g_list_append (message->cc_list, addr);
    }
  for (addy = cenv->bcc; addy; addy = addy->next)
    {
      addr = translate_address (addy);
      message->bcc_list = g_list_append (message->bcc_list, addr);
    }
  
  /* Get fcc from message */
  for (tmp = cenv->userhdrs; tmp; )
    {
      if (mutt_strncasecmp ("X-Mutt-Fcc:", tmp->data, 11) == 0)
        {
          p = tmp->data + 11;
          SKIPWS (p);

          message->fcc_mailbox = NULL;
          if (balsa_app.mailbox_nodes && p != NULL) {
            if (!strcmp (p, balsa_app.sentbox->name))
              message->fcc_mailbox = balsa_app.sentbox;
            else if (!strcmp (p, balsa_app.draftbox->name))
              message->fcc_mailbox = balsa_app.draftbox;
            else if (!strcmp (p, balsa_app.outbox->name))
              message->fcc_mailbox = balsa_app.outbox;
            else if (!strcmp (p, balsa_app.trash->name))
              message->fcc_mailbox = balsa_app.trash;
            else {
                GNode *walk;
    
                walk = g_node_last_child (balsa_app.mailbox_nodes);
                while (walk) {
                  if (!strcmp (p, ((MailboxNode *)((walk)->data))->name)) {
                    message->fcc_mailbox =
                        ((MailboxNode *)((walk)->data))->mailbox;
                    break;
                  } else
                    walk = walk->prev;
                }
            }
          }
        }
      else if (mutt_strncasecmp ("X-Mutt-Fcc:", tmp->data, 18) == 0)
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




/*
 * addresses
 */
Address *
address_new (void)
{
  Address *address;

  address = g_malloc (sizeof (Address));

  address->personal = NULL;
  address->mailbox = NULL;

  return address;
}


void
address_free (Address * address)
{

  if (!address)
    return;

  g_free (address->personal);
  g_free (address->mailbox);

  g_free (address);
}

void address_list_free(GList * address_list)
{
  GList *list;
  for (list = g_list_first (address_list); list; list = g_list_next (list))
    if(list->data) address_free (list->data);
  g_list_free (address_list);
}

static Address *
translate_address (ADDRESS * caddr)
{
  Address *address;

  if (!caddr)
    return NULL;
  address = address_new ();
  address->personal = g_strdup (caddr->personal);
  address->mailbox = g_strdup (caddr->mailbox);

  return address;
}


GList *
make_list_from_string (gchar * the_str)
{
  ADDRESS *address = NULL;
  Address *addr = NULL;
  GList *list = NULL;
  address = rfc822_parse_adrlist (address, the_str);

  while (address)
    {
      addr = translate_address (address);
      list = g_list_append (list, addr);
      address = address->next;
    }
  rfc822_free_address( &address );
  return list;
}

/* returns only first address on the list; ignores remaining ones */
Address*
make_address_from_string(gchar* str) {
  ADDRESS *address = NULL;
  Address *addr = NULL;

  address = rfc822_parse_adrlist (address, str);
  addr = translate_address (address);
  rfc822_free_address (&address);
  return addr;
}

/*
 * contacts
 */
Contact *
contact_new (void)
{
  Contact *contact;

  contact = g_malloc (sizeof (Contact));

  contact->card_name = NULL;
  contact->first_name = NULL;
  contact->last_name = NULL;
  contact->organization = NULL;
  contact->email_address = NULL;
  
  return contact;
}


void
contact_free (Contact * contact)
{

  if (!contact)
    return;

  g_free(contact->card_name);
  g_free(contact->first_name);
  g_free(contact->last_name);  
  g_free(contact->organization);
  g_free(contact->email_address);
  
  g_free(contact);
}

void contact_list_free(GList * contact_list)
{
  GList *list;
  for (list = g_list_first (contact_list); list; list = g_list_next (list))
    if(list->data) contact_free (list->data);
  g_list_free (contact_list);
}

gint
contact_store(Contact *contact)
{
    FILE *gc; 
    gchar string[256];
    gint in_vcard = FALSE;

    if(strlen(contact->card_name) == 0)
        return CONTACT_CARD_NAME_FIELD_EMPTY;

    gc = fopen(gnome_util_prepend_user_home(".gnome/GnomeCard.gcrd"), "r+");
    
    if (!gc) 
        return CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE; 
            
    while (fgets(string, sizeof(string), gc)) 
    { 
        if ( g_strncasecmp(string, "BEGIN:VCARD", 11) == 0 ) {
            in_vcard = TRUE;
            continue;
        }
                
        if ( g_strncasecmp(string, "END:VCARD", 9) == 0 ) {
            in_vcard = FALSE;
            continue;
        }
        
        if (!in_vcard) continue;
        
        g_strchomp(string);
                
        if ( g_strncasecmp(string, "FN:", 3) == 0 )
        {
            gchar *id = g_strdup(string+3);
            if(g_strcasecmp(id, contact->card_name) == 0)
            {
                g_free(id);
                fclose(gc);
                return CONTACT_CARD_NAME_EXISTS;
            }
            g_free(id);
            continue;
        }
    }

    fprintf(gc, "\nBEGIN:VCARD\n");
    fprintf(gc, g_strdup_printf( "FN:%s\n", contact->card_name));

    if(strlen(contact->first_name) || strlen(contact->last_name))
        fprintf(gc, g_strdup_printf( "N:%s;%s\n", contact->last_name, contact->first_name));

    if(strlen(contact->organization))
        fprintf(gc, g_strdup_printf( "ORG:%s\n", contact->organization));
            
    if(strlen(contact->email_address))
        fprintf(gc, g_strdup_printf( "EMAIL;INTERNET:%s\n", contact->email_address));
            
    fprintf(gc, "END:VCARD\n");
    
    fclose(gc);
    return CONTACT_CARD_STORED_SUCCESSFULLY;
}

/*
 * body
 */
Body *
body_new ()
{
  Body *body;

  body = g_malloc (sizeof (Body));
  body->htmlized = NULL;
  body->buffer = NULL;
  body->mutt_body = NULL;
  body->filename = NULL;
  body->charset  = NULL;
  return body;
}


void
body_free (Body * body)
{
  if (!body)
    return;

  g_free (body->htmlized);
  g_free (body->buffer);
  g_free (body->filename);
  g_free (body->charset);
  g_free (body);
}

