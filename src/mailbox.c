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
#include <gtk/gtk.h>
#include "mailbox.h"
#include "balsa-app.h"


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


Mailbox *
mailbox_new (MailboxType type)
{
  Mailbox *mailbox;
  MailboxLocal *local;
  MailboxPOP3 *pop3;
  MailboxIMAP *imap;
  MailboxNNTP *nntp;

  mailbox = g_malloc (sizeof (MailboxUnion));

  switch (type)
    {
    case MAILBOX_MBX:
    case MAILBOX_MTX:
    case MAILBOX_TENEX:
    case MAILBOX_MBOX:
    case MAILBOX_MMDF:
    case MAILBOX_UNIX:
    case MAILBOX_MH:
      local = (MailboxLocal *) mailbox;
      local->type = type;
      local->name = NULL;
      local->stream = NIL;
      local->path = NULL;
      break;

    case MAILBOX_POP3:
      pop3 = (MailboxPOP3 *) mailbox;
      pop3->type = MAILBOX_POP3;
      pop3->name = NULL;
      pop3->stream = NIL;
      pop3->user = NULL;
      pop3->passwd = NULL;
      pop3->server = NULL;
      break;

    case MAILBOX_IMAP:
      imap = (MailboxIMAP *) mailbox;
      imap->type = MAILBOX_IMAP;
      imap->name = NULL;
      imap->stream = NIL;
      imap->user = NULL;
      imap->passwd = NULL;
      imap->server = NULL;
      imap->path = NULL;
      break;

    case MAILBOX_NNTP:
      nntp = (MailboxNNTP *) mailbox;
      nntp->type = MAILBOX_NNTP;
      nntp->name = NULL;
      nntp->stream = NIL;
      nntp->user = NULL;
      nntp->passwd = NULL;
      nntp->server = NULL;
      nntp->newsgroup = NULL;
      break;
    }

  return mailbox;
}


void
mailbox_free (Mailbox * mailbox)
{
  MailboxLocal *local;
  MailboxPOP3 *pop3;
  MailboxIMAP *imap;
  MailboxNNTP *nntp;

  if (!mailbox)
    return;

  if (mailbox->stream)
    mailbox_close (mailbox);

  if (mailbox->name)
    g_free (mailbox->name);


  switch (mailbox->type)
    {
    case MAILBOX_MBX:
    case MAILBOX_MTX:
    case MAILBOX_TENEX:
    case MAILBOX_MBOX:
    case MAILBOX_MMDF:
    case MAILBOX_UNIX:
    case MAILBOX_MH:
      local = (MailboxLocal *) mailbox;
      g_free (local->path);
      break;

    case MAILBOX_POP3:
      pop3 = (MailboxPOP3 *) mailbox;
      g_free (pop3->user);
      g_free (pop3->passwd);
      g_free (pop3->server);
      break;

    case MAILBOX_IMAP:
      imap = (MailboxIMAP *) mailbox;
      g_free (imap->user);
      g_free (imap->passwd);
      g_free (imap->server);
      g_free (imap->path);
      break;

    case MAILBOX_NNTP:
      nntp = (MailboxNNTP *) mailbox;
      g_free (nntp->user);
      g_free (nntp->passwd);
      g_free (nntp->server);
      g_free (nntp->newsgroup);
      break;
    }

  g_free (mailbox);
}


int
mailbox_open (Mailbox * mailbox)
{
  gchar buffer[MAILTMPLEN];
  Mailbox *old_mailbox;
  MailboxLocal *local;
  MailboxPOP3 *pop3;
  MailboxIMAP *imap;
  MailboxNNTP *nntp;


  /* don't open a mailbox if it's already open 
   * -- runtime sanity */
  if (balsa_app.current_mailbox == mailbox)
    return TRUE;

  /* only one mailbox open at a time */
  old_mailbox = balsa_app.current_mailbox;
  balsa_app.current_mailbox = mailbox;

  /* try to open the mailbox -- return
   * FALSE on failure */
  switch (mailbox->type)
    {
    case MAILBOX_MBX:
    case MAILBOX_MTX:
    case MAILBOX_TENEX:
    case MAILBOX_MBOX:
    case MAILBOX_MMDF:
    case MAILBOX_UNIX:
      local = (MailboxLocal *) mailbox;

      local->stream = mail_open (NIL, local->path, NIL);
      if (local->stream == NIL)
        {
          balsa_app.current_mailbox = old_mailbox;
          return FALSE;
        }
      break;

    case MAILBOX_MH:
      local = (MailboxLocal *) mailbox;

      sprintf (buffer, "#mh/%s", local->path);

      local->stream = mail_open (NIL, buffer, NIL);
      if (local->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;

    case MAILBOX_POP3:
      pop3 = (MailboxPOP3 *) mailbox;
      balsa_app.auth_mailbox = mailbox;

      sprintf (buffer, "{%s/pop3}INBOX", pop3->server);

      pop3->stream = mail_open (NIL, buffer, NIL);
      if (pop3->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;


    case MAILBOX_IMAP:
      imap = (MailboxIMAP *) mailbox;
      balsa_app.auth_mailbox = mailbox;

      sprintf (buffer, "{%s/imap}%s", imap->server, imap->path);

      imap->stream = mail_open (NIL, buffer, NIL);
      if (imap->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;


    case MAILBOX_NNTP:
      nntp = (MailboxNNTP *) mailbox;
      balsa_app.auth_mailbox = mailbox;

      sprintf (buffer, "{%s/nntp}%s", nntp->server, nntp->newsgroup);

      nntp->stream = mail_open (NIL, buffer, NIL);
      if (nntp->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;


    default:
      balsa_app.current_mailbox = old_mailbox;
      return FALSE;
      break;
    }


  /* close the old open mailbox */
  if (old_mailbox != NIL)
    mailbox_close (old_mailbox);


  return TRUE;
}


void
mailbox_close (Mailbox * mailbox)
{
  /* now close the mail stream and expunge deleted
   * messages -- the expunge may not have to be done */

  mailbox->stream = mail_close_full (mailbox->stream, CL_EXPUNGE);
}


/* this is lame -- we need to replace this with a good
 * mailbox checking mechanism */
gint
current_mailbox_check ()
{
  if (balsa_app.current_mailbox)
    mail_ping (balsa_app.current_mailbox->stream);
  return TRUE;
}
