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
#include <stdio.h>
#include <gnome.h>

#include "balsa-app.h"
#include "mailbox.h"

/* c-client shit includes */
#include "c-client.h"


#define BUFFER_SIZE 1024


#define LOCK_MAILBOX(mailbox)\
do {\
  if (client_mailbox)\
    {\
      g_print ("*** ERROR: Mailbox Lock Failed: %s ***\n", __PRETTY_FUNCTION__);\
      return;\
    }\
  else\
    client_mailbox = (mailbox);\
} while (0)


#define LOCK_MAILBOX_RETURN_VAL(mailbox, val)\
do {\
  if (client_mailbox)\
    {\
      g_print ("*** ERROR: Mailbox Lock Failed: %s ***\n", __PRETTY_FUNCTION__);\
      return (val);\
    }\
  else\
    client_mailbox = (mailbox);\
} while (0)

#define UNLOCK_MAILBOX()                (client_mailbox = NULL)


#define CLIENT_STREAM(mailbox)          (((MailboxPrivate *)((mailbox)->private))->stream)
#define CLIENT_STREAM_OPEN(mailbox)     (CLIENT_STREAM (mailbox) != NIL)
#define CLIENT_STREAM_CLOSED(mailbox)   (CLIENT_STREAM (mailbox) == NIL)
#define RETURN_IF_CLIENT_STRAM_CLOSED(mailbox)\
do {\
  if (CLIENT_STREAM_CLOSED (mailbox))\
    {\
      g_print ("*** ERROR: Mailbox Stream Closed: %s ***\n", __PRETTY_FUNCTION__);\
      UNLOCK_MAILBOX ();\
      return;\
    }\
} while (0)
#define RETURN_VAL_IF_CLIENT_STRAM_CLOSED(mailbox, val)\
do {\
  if (CLIENT_STREAM_CLOSED (mailbox))\
    {\
      g_print ("*** ERROR: Mailbox Stream Closed: %s ***\n", __PRETTY_FUNCTION__);\
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
    MAILSTREAM *stream;
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

static void send_watcher_mark_read_message (Mailbox * mailbox, Message * message);
static void send_watcher_mark_delete_message (Mailbox * mailbox, Message * message);
static void send_watcher_mark_undelete_message (Mailbox * mailbox, Message * message);
static void send_watcher_new_message (Mailbox * mailbox, Message * message, gint remaining);
static void send_watcher_delete_message (Mailbox * mailbox, Message * message);

static Message *translate_message (ENVELOPE * cenv);
static Address *translate_address (ADDRESS * caddr);


/*
 * necessary for initalizing the fucking c-client library
 */
void
mailbox_init ()
{
#include "linkage.c"
}


/*
 * allocate a new mailbox
 */
Mailbox *
mailbox_new (MailboxType type)
{
  Mailbox *mailbox;

  switch (type)
    {
    case MAILBOX_MBX:
    case MAILBOX_MTX:
    case MAILBOX_TENEX:
    case MAILBOX_MBOX:
    case MAILBOX_MMDF:
    case MAILBOX_UNIX:
    case MAILBOX_MH:
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

    case MAILBOX_NNTP:
      mailbox = (Mailbox *) g_malloc (sizeof (MailboxNNTP));
      MAILBOX_NNTP (mailbox)->server = NULL;
      MAILBOX_NNTP (mailbox)->newsgroup = NULL;
      break;

    default:
      return NULL;
    }

  mailbox->type = type;
  mailbox->name = NULL;
  mailbox->private = (void *) g_malloc (sizeof (MailboxPrivate));
  CLIENT_STREAM (mailbox) = NIL;
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

  if (CLIENT_STREAM (mailbox) != NIL)
    while (mailbox->open_ref > 0)
      mailbox_open_unref (mailbox);

  g_free (mailbox->name);
  g_free (mailbox->private);

  switch (mailbox->type)
    {
    case MAILBOX_MBX:
    case MAILBOX_MTX:
    case MAILBOX_TENEX:
    case MAILBOX_MBOX:
    case MAILBOX_MMDF:
    case MAILBOX_UNIX:
    case MAILBOX_MH:
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

    case MAILBOX_NNTP:
      g_free (MAILBOX_NNTP (mailbox)->server);
      g_free (MAILBOX_NNTP (mailbox)->newsgroup);
      break;
    }


  g_free (mailbox);
}


gint
mailbox_open_ref (Mailbox * mailbox)
{
  gchar buffer[MAILTMPLEN];
  Mailbox *old_mailbox;

  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);


  if (CLIENT_STREAM_OPEN (mailbox))
    {
      /* incriment the reference count */
      mailbox->open_ref++;

      UNLOCK_MAILBOX ();
      return TRUE;
    }


  switch (mailbox->type)
    {
    case MAILBOX_MBX:
    case MAILBOX_MTX:
    case MAILBOX_TENEX:
    case MAILBOX_MBOX:
    case MAILBOX_MMDF:
    case MAILBOX_UNIX:
      CLIENT_STREAM (mailbox) = mail_open (NIL, MAILBOX_LOCAL (mailbox)->path, NIL);
      break;

    case MAILBOX_MH:
      sprintf (buffer, "#mh/%s", MAILBOX_LOCAL (mailbox)->path);
      CLIENT_STREAM (mailbox) = mail_open (NIL, buffer, NIL);
      break;

    case MAILBOX_POP3:
      sprintf (buffer, "{%s/pop3}INBOX", MAILBOX_POP3 (mailbox)->server);
      CLIENT_STREAM (mailbox) = mail_open (NIL, buffer, NIL);
      break;

    case MAILBOX_IMAP:
      sprintf (buffer, "{%s/imap}%s", MAILBOX_IMAP (mailbox)->server, MAILBOX_IMAP (mailbox)->path);
      CLIENT_STREAM (mailbox) = mail_open (NIL, buffer, NIL);
      break;

    case MAILBOX_NNTP:
      sprintf (buffer, "{%s/nntp}%s", MAILBOX_NNTP (mailbox)->server, MAILBOX_NNTP (mailbox)->newsgroup);
      CLIENT_STREAM (mailbox) = mail_open (NIL, buffer, NIL);
      break;
    }


  if (CLIENT_STREAM_OPEN (mailbox))
    {
      load_messages (mailbox, 0);

      /* incriment the reference count */
      mailbox->open_ref++;

      g_print ("Mailbox: Opening %s Refcount: %d\n", mailbox->name, mailbox->open_ref);

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
      g_print ("Mailbox: Closing %s Refcount: %d\n", mailbox->name, mailbox->open_ref);

      free_messages (mailbox);
      mailbox->messages = 0;

      /* now close the mail stream and expunge deleted
       * messages -- the expunge may not have to be done */
      if (CLIENT_STREAM_OPEN (mailbox))
	CLIENT_STREAM (mailbox) = mail_close_full (CLIENT_STREAM (mailbox), CL_EXPUNGE);
    }

  UNLOCK_MAILBOX ();
}


gint
mailbox_check_new_messages (Mailbox * mailbox)
{
  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);
  RETURN_VAL_IF_CLIENT_STRAM_CLOSED (mailbox, FALSE);

  mail_ping (CLIENT_STREAM (mailbox));

  UNLOCK_MAILBOX ();

  if (mailbox->new_messages > 0)
    {
      load_messages (mailbox, 1);
      return TRUE;
    }
  else
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
  ENVELOPE *envelope;
  MESSAGECACHE *elt;

  for (msgno = mailbox->messages - mailbox->new_messages + 1;
       msgno <= mailbox->messages;
       msgno++)
    {
      envelope = mail_fetchenvelope (CLIENT_STREAM (mailbox), msgno);
      elt = mail_elt (CLIENT_STREAM (mailbox), msgno);

      message = translate_message (envelope);
      message->mailbox = mailbox;
      message->msgno = msgno;

      if (!elt->seen)
	message->flags |= MESSAGE_FLAG_NEW;

      if (elt->deleted)
	message->flags |= MESSAGE_FLAG_DELETED;

      if (elt->flagged)
	message->flags |= MESSAGE_FLAG_FLAGGED;

      if (elt->answered)
	message->flags |= MESSAGE_FLAG_ANSWERED;

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

  list = mailbox->message_list;
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
  if (!strcmp (description, "mbx"))
    return MAILBOX_MBX;

  else if (!strcmp (description, "mtx"))
    return MAILBOX_MTX;

  else if (!strcmp (description, "tenex"))
    return MAILBOX_TENEX;

  else if (!strcmp (description, "mbox"))
    return MAILBOX_MBOX;

  else if (!strcmp (description, "mmdf"))
    return MAILBOX_MMDF;

  else if (!strcmp (description, "unix"))
    return MAILBOX_UNIX;

  else if (!strcmp (description, "mh"))
    return MAILBOX_MH;

  else if (!strcmp (description, "pop3"))
    return MAILBOX_POP3;

  else if (!strcmp (description, "imap"))
    return MAILBOX_IMAP;

  else if (!strcmp (description, "nntp"))
    return MAILBOX_NNTP;

  /* if no match */
  return MAILBOX_UNKNOWN;
}


gchar *
mailbox_type_description (MailboxType type)
{
  switch (type)
    {
    case MAILBOX_MBX:
      return "mbx";
      break;

    case MAILBOX_MTX:
      return "mtx";
      break;

    case MAILBOX_TENEX:
      return "tenex";
      break;

    case MAILBOX_MMDF:
      return "mmdf";
      break;

    case MAILBOX_UNIX:
      return "unix";
      break;

    case MAILBOX_MBOX:
      return "mbox";
      break;

    case MAILBOX_MH:
      return "mh";
      break;

    case MAILBOX_POP3:
      return "pop3";
      break;

    case MAILBOX_IMAP:
      return "imap";
      break;

    case MAILBOX_NNTP:
      return "nntp";
      break;

    default:
      return "";
    }
}


MailboxType
mailbox_valid (gchar * filename)
{
  DRIVER *drv = NIL;

  drv = mail_valid (NIL, filename, NIL);

  if (balsa_app.debug)
    {
      if (drv)
	g_print ("mailbox_vaild: %s type %s\n", filename, drv->name);
      else
	g_print ("mailbox_valid: %s invalid mailbox\n", filename);
    }


  if (drv)
    return mailbox_type_from_description (drv->name);
  else
    return MAILBOX_UNKNOWN;
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
  message->newsgroups = NULL;
  message->followup_to = NULL;
  message->references = NULL;
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

  g_free (message->in_reply_to);
  g_free (message->message_id);
  g_free (message->newsgroups);
  g_free (message->followup_to);
  g_free (message->references);

  /* finally free the message */
  g_free (message);
}


void
message_move (Message * message, Mailbox * mailbox)
{
  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_STRAM_CLOSED (message->mailbox);
  UNLOCK_MAILBOX ();
}


void
message_delete (Message * message)
{
  char tmp[BUFFER_SIZE];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_STRAM_CLOSED (message->mailbox);

  sprintf (tmp, "%ld", message->msgno);
  mail_setflag (CLIENT_STREAM (message->mailbox), tmp, "\\DELETED");

  message->flags |= MESSAGE_FLAG_DELETED;
  send_watcher_mark_delete_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
}


void
message_undelete (Message * message)
{
  char tmp[BUFFER_SIZE];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_STRAM_CLOSED (message->mailbox);

  sprintf (tmp, "%ld", message->msgno);
  mail_clearflag (CLIENT_STREAM (message->mailbox), tmp, "\\DELETED");

  message->flags &= !MESSAGE_FLAG_DELETED;
  send_watcher_mark_undelete_message (message->mailbox, message);

  UNLOCK_MAILBOX ();
}


/* internal c-client translation */
static Message *
translate_message (ENVELOPE * cenv)
{
  Message *message;

  if (!cenv)
    return NULL;

  message = message_new ();

  message->remail = g_strdup (cenv->remail);
  message->date = g_strdup (cenv->date);

  message->from = translate_address (cenv->from);
  message->sender = translate_address (cenv->sender);
  message->reply_to = translate_address (cenv->reply_to);

  message->subject = g_strdup (cenv->subject);

  /* more! */


  return message;
}


void
message_body_ref (Message * message)
{
  Body *body;


  if (!message)
    return;

  if (message->body_ref > 0)
    {
      message->body_ref++;
      return;
    }

  /*
   * load message body -- lameness
   */
  body = body_new ();
  body->buffer = g_strdup (mail_fetchtext (CLIENT_STREAM (message->mailbox),
					   message->msgno));
  message->body_list = g_list_append (message->body_list, body);
  message->body_ref++;

  /*
   * emit read message
   */
  if (message->flags & MESSAGE_FLAG_NEW)
    {
      message->flags &= !MESSAGE_FLAG_NEW;
      send_watcher_mark_read_message (message->mailbox, message);
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

	  g_free (body->mime);
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
  address->user = NULL;
  address->host = NULL;

  return address;
}


void
address_free (Address * address)
{
  if (!address)
    return;

  g_free (address->personal);
  g_free (address->user);
  g_free (address->host);

  g_free (address);
}



/* internal c-client translation */
static Address *
translate_address (ADDRESS * caddr)
{
  Address *address;

  address = address_new ();
  if (!caddr)
    {
      address->personal = g_strdup ("");
      address->user = g_strdup ("");
      address->host = g_strdup ("");
    }
  else
    {
      address->personal = g_strdup (caddr->personal);
      address->user = g_strdup (caddr->mailbox);
      address->host = g_strdup (caddr->host);
    }
  return address;
}



/*
 * body
 */
Body *
body_new ()
{
  Body *body;

  body = g_malloc (sizeof (Body));
  body->mime = NULL;
  body->buffer = NULL;
  return body;
}


void
body_free (Body * body)
{
  if (!body)
    return;

  g_free (body->mime);
  g_free (body->buffer);
  g_free (body);
}











/*
 * c-client callback fucking shit
 */
void
mm_searched (MAILSTREAM * stream, unsigned long number)
{
}


/* 
 * this is the callback function which returns the number of new mail
 * messages in a mail box
 */
void
mm_exists (MAILSTREAM * stream, unsigned long number)
{
  if (!client_mailbox)
    {
      g_print ("mm_exists: ** why are we here? **\n");
      return;
    }

  /* 
   * set the number of messages and the number of new
   * messages
   */
  client_mailbox->new_messages += number - client_mailbox->messages;
  client_mailbox->messages = stream->nmsgs;

  if (balsa_app.debug)
    {
      g_print ("mm_exists: %s %d messages %d new_messages\n",
	       client_mailbox->name,
	       client_mailbox->messages,
	       client_mailbox->new_messages);
    }
}


void
mm_expunged (MAILSTREAM * stream, unsigned long number)
{
  if (!client_mailbox)
    return;
}


void
mm_flags (MAILSTREAM * stream, unsigned long number)
{
  if (!client_mailbox)
    return;

  if (balsa_app.debug)
    g_print ("Message %d in mailbox %s changed.\n", number, stream->mailbox);
}


void
mm_notify (MAILSTREAM * stream, char *string, long errflg)
{
  if (!client_mailbox)
    return;

  if (balsa_app.debug)
    g_print ("%s\n", string);
}


void
mm_list (MAILSTREAM * stream, int delimiter, char *mailbox, long attributes)
{
  if (!client_mailbox)
    return;
}


void
mm_lsub (MAILSTREAM * stream, int delimiter, char *mailbox, long attributes)
{
  if (!client_mailbox)
    return;
}


void
mm_status (MAILSTREAM * stream, char *mailbox, MAILSTATUS * status)
{
  if (!client_mailbox)
    return;
}


void
mm_log (char *string, long errflg)
{
  if (!client_mailbox)
    return;
}


void
mm_dlog (char *string)
{
  if (!client_mailbox)
    return;

  if (balsa_app.debug)
    g_print ("%s\n", string);
}


void
mm_login (NETMBX * mb, char *user, char *pwd, long trial)
{
  if (!client_mailbox)
    return;

  if (balsa_app.debug)
    g_print ("mm_login: \n");

  switch (client_mailbox->type)
    {
    case MAILBOX_POP3:
      strcpy (user, MAILBOX_POP3 (client_mailbox)->user);
      strcpy (pwd, MAILBOX_POP3 (client_mailbox)->passwd);
      break;

    case MAILBOX_IMAP:
      strcpy (user, MAILBOX_IMAP (client_mailbox)->user);
      strcpy (pwd, MAILBOX_IMAP (client_mailbox)->passwd);
      break;

    default:
      break;
    }
}


void
mm_critical (MAILSTREAM * stream)
{
  if (!client_mailbox)
    return;

  if (balsa_app.debug)
    g_print ("Mailbox: %s - entering critical mode.\n", stream->mailbox);
}


void
mm_nocritical (MAILSTREAM * stream)
{
  if (!client_mailbox)
    return;

  if (balsa_app.debug)
    g_print ("Mailbox: %s - exiting critical mode.\n", stream->mailbox);
}


long
mm_diskerror (MAILSTREAM * stream, long errcode, long serious)
{
  if (balsa_app.debug)
    g_print ("*** C-Client Disk Error ***\n");

  return NIL;
}


void
mm_fatal (char *string)
{
  if (balsa_app.debug)
    g_print ("*** C-Client Fatel Error: %s ***\n", string);
}
