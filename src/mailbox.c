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
#include "balsa-index.h"
#include "index.h"


gchar *
mailbox_type_description (MailboxType type)
{
  switch (type)
    {
    case MAILBOX_MBOX:
      return "mbox";
      break;

    case MAILBOX_POP3:
      return "POP3";
      break;

    case MAILBOX_IMAP:
      return "IMAP";
      break;
      
    case MAILBOX_NNTP:
      return "NNTP";
      break;

    default:
      return "";
    }
}


Mailbox *
mailbox_new (MailboxType type)
{
  Mailbox *mailbox;
  MailboxMBox *mbox;
  MailboxPOP3 *pop3;
  MailboxIMAP *imap;
  MailboxNNTP *nntp;

  mailbox = g_malloc (sizeof (MailboxUnion));

  switch (type)
    {
    case MAILBOX_MBOX:
      mbox = (MailboxMBox *) mailbox;
      mbox->type = MAILBOX_MBOX;
      mbox->name = NULL;
      mbox->stream = NIL;
      mbox->path = NULL;
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
  MailboxMBox *mbox;
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
    case MAILBOX_MBOX:
      mbox = (MailboxMBox *) mailbox;
      if (mbox->path)
	g_free(mbox->path);
      break;
      
    case MAILBOX_POP3:
      pop3 = (MailboxPOP3 *) mailbox;
      if (pop3->user)
	g_free (pop3->user);

      if (pop3->passwd)
	g_free (pop3->passwd);

      if (pop3->server)
	g_free (pop3->server);
      break;
      
    case MAILBOX_IMAP:
      imap = (MailboxIMAP *) mailbox;
      if (imap->user)
	g_free (imap->user);

      if (imap->passwd)
	g_free (imap->passwd);

      if (imap->server)
	g_free (imap->server);
      break;
      
    case MAILBOX_NNTP:
      nntp = (MailboxNNTP *) mailbox;
      if (nntp->user)
	g_free (nntp->user);

      if (nntp->passwd)
	g_free (nntp->passwd);

      if (nntp->server)
	g_free (nntp->server);
      break;
    }

  g_free (mailbox);
}


int
mailbox_open (Mailbox * mailbox)
{
  gchar buffer[MAILTMPLEN];
  Mailbox *old_mailbox;
  MailboxMBox *mbox;
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
    case MAILBOX_MBOX:
      mbox = (MailboxMBox *) mailbox;

      mbox->stream = mail_open (NIL, mbox->path, NIL);
      if (mbox->stream == NIL)
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
      if (pop3->stream == NIL)
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

  /* update the index */
  balsa_index_set_stream (BALSA_INDEX (balsa_app.main_window->index),
			  mailbox->stream);

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
void
current_mailbox_check ()
{
  mail_ping (balsa_app.current_mailbox->stream);
}
