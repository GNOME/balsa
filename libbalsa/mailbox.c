/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#include <stdio.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <gnome.h>

#include "mailbackend.h"

#include "balsa-app.h"
#include "mailbox.h"
#include "misc.h"

#define BUFFER_SIZE 1024


#define LOCK_MAILBOX(mailbox)\
do {\
  if (client_mailbox)\
    {\
      g_print (_("*** ERROR: Mailbox Lock Failed: %s ***\n"), __PRETTY_FUNCTION__);\
      return;\
    }\
  else\
    client_mailbox = (mailbox);\
} while (0)


#define LOCK_MAILBOX_RETURN_VAL(mailbox, val)\
do {\
  if (client_mailbox)\
    {\
      g_print (_("*** ERROR: Mailbox Lock Failed: %s ***\n"), __PRETTY_FUNCTION__);\
      return (val);\
    }\
  else\
    client_mailbox = (mailbox);\
} while (0)

#define UNLOCK_MAILBOX()                (client_mailbox = NULL)


#define CLIENT_CONTEXT(mailbox)          (((MailboxPrivate *)((mailbox)->private))->context)
#define CLIENT_CONTEXT_OPEN(mailbox)     (CLIENT_CONTEXT (mailbox) != NULL)
#define CLIENT_CONTEXT_CLOSED(mailbox)   (CLIENT_CONTEXT (mailbox) == NULL)
#define RETURN_IF_CLIENT_CONTEXT_CLOSED(mailbox)\
do {\
  if (CLIENT_CONTEXT_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      UNLOCK_MAILBOX ();\
      return;\
    }\
} while (0)
#define RETURN_VAL_IF_CONTEXT_CLOSED(mailbox, val)\
do {\
  if (CLIENT_CONTEXT_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      UNLOCK_MAILBOX ();\
      return (val);\
    }\
} while (0)


#define WATCHER_LIST(mailbox) (((MailboxPrivate *)((mailbox)->private))->watcher_list)


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


/* 
 * private mailbox data
 */
typedef struct
  {
    CONTEXT *context;
    GList *watcher_list;
  }
MailboxPrivate;


/* 
 * the mailbox to be referenced by any of the c-client
 * callbacks for authorization, new messages, etc... 
 */
static Mailbox *client_mailbox = NULL;


/* 
 * prototypes
 */
static void load_messages (Mailbox * mailbox, gint emit);
static void free_messages (Mailbox * mailbox);

static void send_watcher_mark_clear_message (Mailbox * mailbox, Message * message);
static void send_watcher_mark_answer_message (Mailbox * mailbox, Message * message);
static void send_watcher_mark_read_message (Mailbox * mailbox, Message * message);
static void send_watcher_mark_unread_message (Mailbox * mailbox, Message * message);
static void send_watcher_mark_delete_message (Mailbox * mailbox, Message * message);
static void send_watcher_mark_undelete_message (Mailbox * mailbox, Message * message);
static void send_watcher_new_message (Mailbox * mailbox, Message * message, gint remaining);
static void send_watcher_delete_message (Mailbox * mailbox, Message * message);

static Message *translate_message (HEADER * cur);
static Address *translate_address (ADDRESS * caddr);


/* We're gonna set Mutt global vars here */
void
mailbox_init (gchar * inbox_path)
{
  struct utsname utsname;
  char *p;
  gchar *tmp;
  gchar buffer[256];

  Spoolfile = inbox_path;

  uname (&utsname);

  Username = g_get_user_name ();

  Homedir = g_get_home_dir ();

  Realname = g_get_real_name ();

  Hostname = g_get_host_name();
#if 0
  /* some systems report the FQDN instead of just the hostname */
  if ((p = strchr (utsname.nodename, '.')))
    {
      Hostname = mutt_substrdup (utsname.nodename, p);
      p++;
      strfcpy (buffer, p, sizeof (buffer));	/* save the domain for below */
    }
  else
    Hostname = g_strdup (utsname.nodename);

#ifndef DOMAIN
#define DOMAIN buffer
  if (!p && getdnsdomainname (buffer, sizeof (buffer)) == -1)
    Fqdn = safe_strdup ("@");
  else
#endif /* DOMAIN */
    {
#ifdef HIDDEN_HOST
      Fqdn = safe_strdup (DOMAIN);
#else
      Fqdn = safe_malloc (strlen (DOMAIN) + strlen (Hostname) + 2);
      sprintf (Fqdn, "%s.%s", Hostname, DOMAIN);
#endif /* HIDDEN_HOST */
    }
#endif
  Fqdn = g_strdup(Hostname);

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
  if (mb->type != MAILBOX_IMAP)
    return 0;

  ImapUser = MAILBOX_IMAP (mb)->user;
  ImapPass = MAILBOX_IMAP (mb)->passwd;

  return 1;
}

void
check_all_pop3_hosts (Mailbox * to)
{
  GList *list;
  Mailbox *mailbox;

  list = g_list_first (balsa_app.inbox_input);

  if (to->type != MAILBOX_MBOX)
    return;

  Spoolfile = MAILBOX_LOCAL (to)->path;

  while (list)
    {
      mailbox = list->data;
      PopHost = g_strdup (MAILBOX_POP3 (mailbox)->server);
      PopPort = 110;
      PopPass = g_strdup (MAILBOX_POP3 (mailbox)->passwd);
      PopUser = g_strdup (MAILBOX_POP3 (mailbox)->user);
      mutt_fetchPopMail ();
      g_free (PopHost);
      g_free (PopPass);
      g_free (PopUser);
      list = list->next;
    }
}

#ifdef BUFFY_SIZE
/*
 * Incoming is a global var in mutt, needs to be set for
 * mailbox_have_new_messages() to work correctly.
 */
extern int test_new_folder (const char *);
void
add_mailboxes_for_checking (Mailbox * mailbox)
{
  BUFFY **tmp;
  struct stat sb;

  if (!mailbox)
    return;

  if (mailbox->type == MAILBOX_IMAP ||
      mailbox->type == MAILBOX_POP3)
    return;

  for (tmp = &Incoming; *tmp; tmp = &((*tmp)->next))
    {
      if (strcmp (MAILBOX_LOCAL (mailbox)->path, (*tmp)->path) == 0)
	break;
    }

  if (!*tmp)
    {
      *tmp = (BUFFY *) g_malloc (sizeof (BUFFY));
      (*tmp)->path = g_strdup (MAILBOX_LOCAL (mailbox)->path);
      (*tmp)->next = NULL;
    }
  (*tmp)->new = 0;
  (*tmp)->notified = 1;
  (*tmp)->newly_created = 0;

  /* for buffy_size, it is important that if the folder is new (tested
   * by reading it), the size is set to 0 so that later when we check we see
   * that it increased .  without buffy_size we probably don't care.
   */
  if (stat ((*tmp)->path, &sb) == 0 && !test_new_folder ((*tmp)->path))
    {
      /* some systems out there don't have an off_t type */
      (*tmp)->size = (long) sb.st_size;
    }
  else
    (*tmp)->size = 0;

  return;

}

gint
mailbox_have_new_messages (gchar * path)
{
  BUFFY *buffy;
  buffy = mutt_find_mailbox (path);
  if (buffy)
    return (buffy->new);
  else
    return FALSE;
}
#else
void
add_mailboxes_for_checking (Mailbox * mailbox)
{
  return;
}

gint
mailbox_have_new_messages (gchar * path)
{
  return FALSE;
}
#endif /* BUFFY_SIZE */


/*
 * allocate a new mailbox
 */
Mailbox *
mailbox_new (MailboxType type)
{
  Mailbox *mailbox;

  switch (type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      mailbox = (Mailbox *) g_malloc (sizeof (MailboxLocal));
      MAILBOX_LOCAL (mailbox)->path = NULL;
      break;

    case MAILBOX_POP3:
      mailbox = (Mailbox *) g_malloc (sizeof (MailboxPOP3));
      MAILBOX_POP3 (mailbox)->user = NULL;
      MAILBOX_POP3 (mailbox)->passwd = NULL;
      MAILBOX_POP3 (mailbox)->server = NULL;
      break;

    case MAILBOX_IMAP:
      mailbox = (Mailbox *) g_malloc (sizeof (MailboxIMAP));
      MAILBOX_IMAP (mailbox)->user = NULL;
      MAILBOX_IMAP (mailbox)->passwd = NULL;
      MAILBOX_IMAP (mailbox)->server = NULL;
      MAILBOX_IMAP (mailbox)->path = NULL;
      break;

    default:
      return NULL;
    }

  mailbox->type = type;
  mailbox->name = NULL;
  mailbox->private = (void *) g_malloc (sizeof (MailboxPrivate));
  CLIENT_CONTEXT (mailbox) = NULL;
  WATCHER_LIST (mailbox) = NULL;
  mailbox->open_ref = 0;
  mailbox->messages = 0;
  mailbox->new_messages = 0;
  mailbox->message_list = NULL;
  return mailbox;
}


void
mailbox_free (Mailbox * mailbox)
{
  if (!mailbox)
    return;

  if (CLIENT_CONTEXT (mailbox) != NULL)
    while (mailbox->open_ref > 0)
      mailbox_open_unref (mailbox);

  g_free (mailbox->name);
  g_free (mailbox->private);

  switch (mailbox->type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      g_free (MAILBOX_LOCAL (mailbox)->path);
      break;

    case MAILBOX_POP3:
      g_free (MAILBOX_POP3 (mailbox)->user);
      g_free (MAILBOX_POP3 (mailbox)->passwd);
      g_free (MAILBOX_POP3 (mailbox)->server);
      break;

    case MAILBOX_IMAP:
      g_free (MAILBOX_IMAP (mailbox)->user);
      g_free (MAILBOX_IMAP (mailbox)->passwd);
      g_free (MAILBOX_IMAP (mailbox)->server);
      g_free (MAILBOX_IMAP (mailbox)->path);
      break;

    case MAILBOX_UNKNOWN:
      break;
    }

  g_free (mailbox);
}


gint
mailbox_open_ref (Mailbox * mailbox)
{
  GString *tmp;
  struct stat st;
  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);


  if (CLIENT_CONTEXT_OPEN (mailbox))
    {
      /* incriment the reference count */
      mailbox->open_ref++;

      UNLOCK_MAILBOX ();
      return TRUE;
    }

  if (mailbox->type != MAILBOX_IMAP && mailbox->type != MAILBOX_POP3)
    {
      if (stat (MAILBOX_LOCAL (mailbox)->path, &st) == -1)
	{
	  UNLOCK_MAILBOX ();
	  return FALSE;
	}
    }

  switch (mailbox->type)
    {
      /* add mail dir */
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      CLIENT_CONTEXT (mailbox) = mx_open_mailbox (MAILBOX_LOCAL (mailbox)->path, 0, NULL);
      break;

    case MAILBOX_POP3:
      CLIENT_CONTEXT (mailbox) = mx_open_mailbox (MAILBOX_POP3 (mailbox)->server, 0, NULL);
      break;

    case MAILBOX_IMAP:
      tmp = g_string_new (NULL);
      g_string_append_c (tmp, '{');
      g_string_append (tmp, MAILBOX_IMAP (mailbox)->server);
      g_string_append_c (tmp, '}');
      g_string_append (tmp, MAILBOX_IMAP (mailbox)->path);
      set_imap_username (mailbox);
      CLIENT_CONTEXT (mailbox) = mx_open_mailbox (tmp->str, 0, NULL);
      g_string_free (tmp, TRUE);
      break;

    case MAILBOX_UNKNOWN:
      break;
    }

  if (CLIENT_CONTEXT_OPEN (mailbox))
    {
      mailbox->messages = 0;
      mailbox->new_messages = CLIENT_CONTEXT (mailbox)->msgcount;
      load_messages (mailbox, 0);

      /* incriment the reference count */
      mailbox->open_ref++;

      g_print (_ ("Mailbox: Opening %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);

      UNLOCK_MAILBOX ();
      return TRUE;
    }
  else
    {
      UNLOCK_MAILBOX ();
      return FALSE;
    }
}


void
mailbox_open_unref (Mailbox * mailbox)
{
  LOCK_MAILBOX (mailbox);

  if (mailbox->open_ref == 0)
    return;

  mailbox->open_ref--;

  if (mailbox->open_ref == 0)
    {
      g_print (_ ("Mailbox: Closing %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);

      free_messages (mailbox);
      mailbox->messages = 0;

      /* now close the mail stream and expunge deleted
       * messages -- the expunge may not have to be done */
      if (CLIENT_CONTEXT_OPEN (mailbox))
	{
	  /* If it closed we have no context. If it didnt close right
	     don't ask me what to do - AC */

	  if (mx_close_mailbox (CLIENT_CONTEXT (mailbox)) == 0)
	    CLIENT_CONTEXT (mailbox) = NULL;
	}
    }

  UNLOCK_MAILBOX ();
}

gint
mailbox_check_new_messages (Mailbox * mailbox)
{
  gint i = 0;

  if (!mailbox)
    return FALSE;

  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);
  RETURN_VAL_IF_CONTEXT_CLOSED (mailbox, FALSE);

  if ((i = mx_check_mailbox (CLIENT_CONTEXT (mailbox), NULL)) < 0)
    {
      UNLOCK_MAILBOX ();
      g_print ("error or something\n");
    }
  else if (i == M_NEW_MAIL || i == M_REOPENED)
    {

      mailbox->new_messages = CLIENT_CONTEXT (mailbox)->msgcount - mailbox->messages;

      if (mailbox->new_messages > 0)
	{
	  load_messages (mailbox, 1);
	  UNLOCK_MAILBOX ();
	  return TRUE;
	}
      else
	{
	  UNLOCK_MAILBOX ();
	  return FALSE;
	}
    }
  UNLOCK_MAILBOX ();
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
 */
static void
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

	message = translate_message (cur);
	message->mailbox = mailbox;
	message->msgno = msgno;
	mailbox->messages++;

	if (!cur->read)
	  message->flags |= MESSAGE_FLAG_NEW;

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

	/* 
	 * give time to gtk so the GUI isn't blocked
	 * this is kinda a hack right now
	 */
	while (gtk_events_pending ())
	  gtk_main_iteration ();
      }
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
      message_free (message);
    }
  g_list_free (mailbox->message_list);
  mailbox->message_list = NULL;
}


/*
 * sending messages to watchers
 */
static void
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

static void
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

static void
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

static void
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

static void
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


static void
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


static void
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


static void
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


/*
 * messages
 */
Message *
message_new ()
{
  Message *message;

  message = g_malloc (sizeof (Message));

  message->flags = 0;
  message->msgno = 0;
  message->mailbox = NULL;
  message->remail = NULL;
  message->date = NULL;
  message->from = NULL;
  message->sender = NULL;
  message->reply_to = NULL;
  message->subject = NULL;
  message->to_list = NULL;
  message->cc_list = NULL;
  message->bcc_list = NULL;
  message->in_reply_to = NULL;
  message->message_id = NULL;
  message->body_ref = 0;
  message->body_list = NULL;

  return message;
}


void
message_free (Message * message)
{
  g_free (message->remail);
  g_free (message->date);
  address_free (message->from);
  address_free (message->sender);
  address_free (message->reply_to);
  g_free (message->subject);

  g_list_free (message->to_list);
  g_list_free (message->cc_list);

  g_free (message->in_reply_to);
  g_free (message->message_id);

  /* finally free the message */
  g_free (message);
}

gchar *
message_pathname (Message * message)
{
  return CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno]->path;
}

/* TODO and/or FIXME look at the flags for mutt_append_message */
void
message_copy (Message * message, Mailbox * dest)
{
  HEADER *cur;

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  mailbox_open_ref (dest);

  mutt_append_message (CLIENT_CONTEXT (dest),
		       CLIENT_CONTEXT (message->mailbox),
		       cur, 0, 0);

  mailbox_open_unref (dest);

  UNLOCK_MAILBOX ();
}

void
message_move (Message * message, Mailbox * dest)
{
  HEADER *cur;

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  mailbox_open_ref (dest);

  mutt_append_message (CLIENT_CONTEXT (dest),
		       CLIENT_CONTEXT (message->mailbox),
		       cur, 0, 0);

  mailbox_open_unref (dest);

  UNLOCK_MAILBOX ();

  message_delete (message);
}

void
message_reply (Message * message)
{
  HEADER *cur;

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_REPLIED, TRUE);

  message->flags |= MESSAGE_FLAG_REPLIED;
  send_watcher_mark_answer_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
}

void
message_clear_flags (Message * message)
{
#if 0
  char tmp[BUFFER_SIZE];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_REPLIED, 1);
  sprintf (tmp, "%ld", message->msgno);
  mail_clearflag (CLIENT_STREAM (message->mailbox), tmp, "\\DELETED");
  mail_clearflag (CLIENT_STREAM (message->mailbox), tmp, "\\ANSWERED");

  message->flags = 0;
  send_watcher_mark_clear_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
#endif
}


void
message_read (Message * message)
{
  HEADER *cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_READ, TRUE);
  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_OLD, FALSE);

  message->flags &= ~MESSAGE_FLAG_NEW;
  send_watcher_mark_read_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
}

void
message_unread (Message * message)
{
  HEADER *cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_READ, TRUE);

  message->flags |= MESSAGE_FLAG_NEW;
  send_watcher_mark_unread_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
}

void
message_delete (Message * message)
{
  HEADER *cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_DELETE, TRUE);

  message->flags |= MESSAGE_FLAG_DELETED;
  send_watcher_mark_delete_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
}


void
message_undelete (Message * message)
{
  HEADER *cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_DELETE, FALSE);

  message->flags &= ~MESSAGE_FLAG_DELETED;
  send_watcher_mark_undelete_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
}


/* internal c-client translation */
static Message *
translate_message (HEADER * cur)
{
  Message *message;
  ADDRESS *addy;
  Address *addr;
  ENVELOPE *cenv;
  gchar rettime[27];
  struct tm *footime;

  if (!cur)
    return NULL;

  cenv = cur->env;

  message = message_new ();
/*
   message->remail = g_strdup (cenv->remail);
 */
  footime = localtime (&cur->date_sent);

  strftime (rettime, sizeof (rettime), "%a, %d %b %Y %H:%M:%S", footime);

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

  message->subject = g_strdup (cenv->subject);

  /* more! */


  return message;
}




char *
mime_content_type2str (int contenttype)
{
  switch (contenttype)
    {
    case TYPEOTHER:
      return "OTHER";
    case TYPEAUDIO:
      return "AUDIO";
    case TYPEAPPLICATION:
      return "APPLICATION";
    case TYPEIMAGE:
      return "IMAGE";
    case TYPEMULTIPART:
      return "MULTIPART";
    case TYPETEXT:
      return "TEXT";
    case TYPEVIDEO:
      return "VIDEO";
    default:
      return "";
    }
}





void
message_body_ref (Message * message)
{
  Body *body;
  HEADER *cur;
  MESSAGE *msg;
  BODY *b;

  if (!message)
    return;

  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  if (message->body_ref > 0)
    {
      message->body_ref++;
      return;
    }

  /*
   * load message body
   */
  msg = mx_open_message (CLIENT_CONTEXT (message->mailbox), cur->msgno);
  fseek (msg->fp, cur->content->offset, 0);
  if (cur->content->type == TYPEMULTIPART)
    {
      cur->content->parts = mutt_parse_multipart (msg->fp,
		   mutt_get_parameter ("boundary", cur->content->parameter),
				cur->content->offset + cur->content->length,
			 strcasecmp ("digest", cur->content->subtype) == 0);
    }
  if (msg != NULL)
    {
      BODY *bdy = cur->content;

      if (balsa_app.debug)
	g_print (_ ("Loading message: %s/%s\n"), TYPE (b->type), b->subtype);
      b = cur->content;
      if (balsa_app.debug)
	{
	  fprintf (stderr, "After loading message\n");
	  fprintf (stderr, "header->mime    = %d\n", cur->mime);
	  fprintf (stderr, "header->offset  = %d\n", cur->offset);
	  fprintf (stderr, "header->content = %p\n", cur->content);
	  fprintf (stderr, " \n\nDumping Message\n");
	  fprintf (stderr, "Dumping a %s[%d] message\n",
		   mime_content_type2str (cur->content->type),
		   cur->content->type);
	}
      if (cur->content->type != TYPEMULTIPART)
	{
	  body = body_new ();
	  body->mutt_body = cur->content;
	  message->body_list = g_list_append (message->body_list, body);
	}
      else
	{
	  bdy = cur->content->parts;
	  while (bdy)
	    {

	      if (balsa_app.debug)
		{
		  fprintf (stderr, "h->c->type      = %s[%d]\n", mime_content_type2str (bdy->type), bdy->type);
		  fprintf (stderr, "h->c->subtype   = %s\n", bdy->subtype);
		  fprintf (stderr, "======\n");
		}
	      body = body_new ();
	      body->mutt_body = bdy;
	      message->body_list = g_list_append (message->body_list, body);
	      bdy = bdy->next;

	    }
	}
      message->body_ref++;
      mx_close_message (&msg);
    }
  /*
   * emit read message
   */
  if (message->flags & MESSAGE_FLAG_NEW)
    {
      message_read (message);
    }

  if (message->mailbox->type == MAILBOX_IMAP)
    {
      if (MAILBOX_IMAP (message->mailbox)->tmp_file_path)
	g_free (MAILBOX_IMAP (message->mailbox)->tmp_file_path);
      MAILBOX_IMAP (message->mailbox)->tmp_file_path =
	g_strdup (cur->content->filename);
    }
}


void
message_body_unref (Message * message)
{
  GList *list;
  Body *body;

  if (!message)
    return;

  if (message->body_ref == 0)
    return;

  if (--message->body_ref == 0)
    {
      list = message->body_list;
      while (list)
	{
	  body = (Body *) list->data;
	  list = list->next;

	  if (body->htmlized)
	    g_free (body->htmlized);
	  if (body->buffer)
	    g_free (body->buffer);
	}

      g_list_free (message->body_list);
      message->body_list = NULL;
    }
}

/*
 * addresses
 */
Address *
address_new ()
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
  return list;
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
  return body;
}


void
body_free (Body * body)
{
  if (!body)
    return;

  if (body->htmlized)
    g_free (body->htmlized);
  if (body->buffer)
    g_free (body->buffer);
  g_free (body);
}
