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




/* 
 * macro returns the c-client mailstream from a 
 * mailbox structure
 */
typedef struct
{
  MAILSTREAM *stream;
} MailboxPrivate;


/* the mailbox to be referenced by any of the c-client
 * callbacks for authorization, new messages, etc... */
static Mailbox *client_mailbox = NULL;


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
  mailbox->new_messages = 0;

  return mailbox;
}


void
mailbox_free (Mailbox * mailbox)
{
  if (!mailbox)
    return;

  if (CLIENT_STREAM (mailbox) != NIL)
    mailbox_close (mailbox);

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


  UNLOCK_MAILBOX ();

  if (CLIENT_STREAM_OPEN (mailbox))
    {
      /* incriment the reference count */
      mailbox->open_ref++;
      return TRUE;
    }
  else
    return FALSE;
}


void
mailbox_open_unref (Mailbox * mailbox)
{
  if (mailbox->open_ref == 0)
    return;

  mailbox->open_ref--;

  if (mailbox->open_ref == 0)
    mailbox_close (mailbox);
}


void
mailbox_close (Mailbox * mailbox)
{
  LOCK_MAILBOX (mailbox);

  if (CLIENT_STREAM_OPEN (mailbox))
    /* now close the mail stream and expunge deleted
     * messages -- the expunge may not have to be done */
    CLIENT_STREAM (mailbox) = mail_close_full (CLIENT_STREAM (mailbox), CL_EXPUNGE);

  UNLOCK_MAILBOX ();
}


gint
mailbox_check_new_messages (Mailbox * mailbox)
{
  LOCK_MAILBOX_RETURN_VAL (mailbox, FALSE);
  RETURN_VAL_IF_CLIENT_STRAM_CLOSED (mailbox, FALSE);

  mail_ping (CLIENT_STREAM (mailbox));

  UNLOCK_MAILBOX ();

  if (mailbox->new_messages)
    return TRUE;
  else
    return FALSE;
}


void
mailbox_message_delete (Mailbox * mailbox, glong msgno)
{
  char tmp[BUFFER_SIZE];

  LOCK_MAILBOX (mailbox);
  RETURN_IF_CLIENT_STRAM_CLOSED (mailbox);

  sprintf (tmp, "%ld", msgno);
  mail_setflag (CLIENT_STREAM (mailbox), tmp, "\\DELETED");

  UNLOCK_MAILBOX ();
}


void
mailbox_message_undelete (Mailbox * mailbox, glong msgno)
{
  char tmp[BUFFER_SIZE];

  LOCK_MAILBOX (mailbox);
  RETURN_IF_CLIENT_STRAM_CLOSED (mailbox);

  sprintf (tmp, "%ld", msgno);
  mail_clearflag (CLIENT_STREAM (mailbox), tmp, "\\DELETED");

  UNLOCK_MAILBOX ();
}


MessageHeader *
mailbox_message_header (Mailbox * mailbox, glong msgno, gint allocate)
{
  static MessageHeader header = {NULL, NULL, NULL};
  ENVELOPE *envelope;

  LOCK_MAILBOX_RETURN_VAL (mailbox, NULL);
  RETURN_VAL_IF_CLIENT_STRAM_CLOSED (mailbox, NULL);

  g_free (header.from);
  g_free (header.subject);
  g_free (header.date);
  header.from = NULL;
  header.subject = NULL;
  header.date = NULL;

  envelope = mail_fetchenvelope (CLIENT_STREAM (mailbox), msgno);

  header.from = g_strdup (envelope->from->personal);
  header.subject = g_strdup (envelope->subject);
  header.date = g_strdup (envelope->date);

  UNLOCK_MAILBOX ();
  return &header;
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
  client_mailbox->messages = stream->nmsgs;
  client_mailbox->new_messages += number;

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
