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
      return "MMDF";
      break;

    case MAILBOX_UNIX:
      return "UNIX";
      break;

    case MAILBOX_MBOX:
      return "mbox";
      break;

    case MAILBOX_MH:
      return "mh";
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
  MailboxMBX *mbx;
  MailboxMTX *mtx;
  MailboxTENEX *tenex;
  MailboxMBox *mbox;
  MailboxMMDF *mmdf;
  MailboxUNIX *mbunix;
  MailboxMH *mh;
  MailboxPOP3 *pop3;
  MailboxIMAP *imap;
  MailboxNNTP *nntp;

  mailbox = g_malloc (sizeof (MailboxUnion));

  switch (type)
    {
    case MAILBOX_MBX:
      mbx = (MailboxMBX *) mailbox;
      mbx->type = MAILBOX_MBX;
      mbx->name = NULL;
      mbx->stream = NIL;
      mbx->path = NULL;
      break;

    case MAILBOX_MTX:
      mtx = (MailboxMTX *) mailbox;
      mtx->type = MAILBOX_MTX;
      mtx->name = NULL;
      mtx->stream = NIL;
      mtx->path = NULL;
      break;

    case MAILBOX_TENEX:
      tenex = (MailboxTENEX *) mailbox;
      tenex->type = MAILBOX_TENEX;
      tenex->name = NULL;
      tenex->stream = NIL;
      tenex->path = NULL;
      break;

    case MAILBOX_MBOX:
      mbox = (MailboxMBox *) mailbox;
      mbox->type = MAILBOX_MBOX;
      mbox->name = NULL;
      mbox->stream = NIL;
      mbox->path = NULL;
      break;

    case MAILBOX_MMDF:
      mmdf = (MailboxMMDF *) mailbox;
      mmdf->type = MAILBOX_MMDF;
      mmdf->name = NULL;
      mmdf->stream = NIL;
      mmdf->path = NULL;
      break;

    case MAILBOX_UNIX:
      mbunix = (MailboxUNIX *) mailbox;
      mbunix->type = MAILBOX_UNIX;
      mbunix->name = NULL;
      mbunix->stream = NIL;
      mbunix->path = NULL;
      break;

    case MAILBOX_MH:
      mh = (MailboxMH *) mailbox;
      mh->type = MAILBOX_MH;
      mh->name = NULL;
      mh->stream = NIL;
      mh->path = NULL;
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
  MailboxMBX *mbx;
  MailboxMTX *mtx;
  MailboxTENEX *tenex;
  MailboxMBox *mbox;
  MailboxMMDF *mmdf;
  MailboxUNIX *mbunix;
  MailboxMH *mh;

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
      mbx = (MailboxMBX *) mailbox;
      if (mbx->path)
	g_free(mbx->path);
      break;

    case MAILBOX_MTX:
      mtx = (MailboxMTX *) mailbox;
      if (mtx->path)
	g_free(mtx->path);
      break;

    case MAILBOX_TENEX:
      tenex = (MailboxTENEX *) mailbox;
      if (tenex->path)
	g_free(tenex->path);
      break;

    case MAILBOX_MBOX:
      mbox = (MailboxMBox *) mailbox;
      if (mbox->path)
	g_free(mbox->path);
      break;
      
    case MAILBOX_MMDF:
      mmdf = (MailboxMMDF *) mailbox;
      if (mmdf->path)
	g_free(mmdf->path);
      break;

    case MAILBOX_UNIX:
      mbunix = (MailboxUNIX *) mailbox;
      if (mbunix->path)
	g_free(mbunix->path);
      break;

    case MAILBOX_MH:
      mh = (MailboxMH *) mailbox;
      if (mh->path)
	g_free(mh->path);
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
  MailboxMBX *mbx;
  MailboxMTX *mtx;
  MailboxTENEX *tenex;
  MailboxMBox *mbox;
  MailboxMMDF *mmdf;
  MailboxUNIX *mbunix;
  MailboxMH *mh;

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
      mbx = (MailboxMBX *) mailbox;

      mbx->stream = mail_open (NIL, mbx->path, NIL);
      if (mbx->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;

    case MAILBOX_MTX:
      mtx = (MailboxMTX *) mailbox;

      mtx->stream = mail_open (NIL, mtx->path, NIL);
      if (mtx->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;

    case MAILBOX_TENEX:
      tenex = (MailboxTENEX *) mailbox;

      tenex->stream = mail_open (NIL, tenex->path, NIL);
      if (tenex->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;

    case MAILBOX_MBOX:
      mbox = (MailboxMBox *) mailbox;

      mbox->stream = mail_open (NIL, mbox->path, NIL);
      if (mbox->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;

    case MAILBOX_MMDF:
      mmdf = (MailboxMMDF *) mailbox;

      mmdf->stream = mail_open (NIL, mmdf->path, NIL);
      if (mmdf->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;

    case MAILBOX_UNIX:
      mbunix = (MailboxUNIX *) mailbox;

      mbunix->stream = mail_open (NIL, mbunix->path, NIL);
      if (mbunix->stream == NIL)
	{
	  balsa_app.current_mailbox = old_mailbox;
	  return FALSE;
	}
      break;

    case MAILBOX_MH:
      mh = (MailboxMH *) mailbox;

      sprintf (buffer, "#mh/%s", mh->path);

      mh->stream = mail_open (NIL, buffer, NIL);
      if (mh->stream == NIL)
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
  mail_ping (balsa_app.current_mailbox->stream);
  return TRUE;
}
