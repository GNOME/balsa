/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdio.h>
#include <gtk/gtk.h>
#include "mailbox.h"
#include "balsa-app.h"
#include "balsa-index.h"
#include "index.h"

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
      break;
      
    case MAILBOX_NNTP:
      nntp = (MailboxNNTP *) mailbox;
      nntp->type = MAILBOX_NNTP;
      nntp->name = NULL;
      nntp->stream = NIL;
      nntp->user = NULL;
      nntp->passwd = NULL;
      nntp->server = NULL;
      break;
    }
  
  return mailbox;
}

int
mailbox_open (Mailbox * mailbox)
{
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
