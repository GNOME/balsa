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
#include <string.h>
#include "c-client.h"
#include "balsa-app.h"
#include "balsa-index.h"
#include "mailbox.h"


/* memmove needs to be 2 more than the length in the STRINGLIST 
 * so that it removes the header, the : and the space
 *
 * FIXME (mabey)
 * We are cutting off the last two characters beacuse they appear
 * to always be two spaces.  This may be bad.
 */

char *
get_header (char *header, MAILSTREAM * stream, unsigned long mesgno)
{
  char *t;
  int olen = strlen (header);
  int len;
  STRINGLIST headerline =
  {
    {(unsigned char *) header, olen}, NIL};
  t = mail_fetch_header (stream, mesgno, NIL, &headerline, NIL, FT_INTERNAL | FT_PEEK);
  len = strlen (t);
  if (len < 3)
    return "";
  memmove (t, t + olen + 2, len - olen + 1);
  t[strlen (t) - 2] = '\0';
  return t;
}



char *
get_header_from (MAILSTREAM * stream, unsigned long mesgno)
{
  char *t;
  int len;
  static STRINGLIST mailrnfromline =
  {
    {(unsigned char *) ">from", 5}, NIL};
  static STRINGLIST mailfromline =
  {
    {(unsigned char *) "from", 4,}, &mailrnfromline};
  t = mail_fetch_header (stream, mesgno, NIL, &mailfromline, NIL, FT_INTERNAL | FT_PEEK);
  len = strlen (t);
  if (len < 3)
    return "";
  memmove (t, t + 6, strlen (t) - 5);
  t[strlen (t) - 2] = '\0';
  return t;
}


char *
get_header_replyto (MAILSTREAM * stream, unsigned long mesgno)
{
  char *t;
  int len;
  static STRINGLIST mailreplytoline =
  {
    {(unsigned char *) "reply-to", 8}, NIL};
  t = mail_fetch_header (stream, mesgno, NIL, &mailreplytoline, NIL, FT_INTERNAL | FT_PEEK);
  len = strlen (t);
  if (len < 3)
    return get_header_from (stream, mesgno);
  memmove (t, t + 10, len - 9);
  t[strlen (t) - 2] = '\0';
  return t;
}



/* 
 * Callbacks from the C-CLIENT Library
 */

void
mm_searched (MAILSTREAM * stream,
	     unsigned long number)
{
}

/* this is the callback function which returns the number of new mail
 * messages in a mail box */
void
mm_exists (MAILSTREAM * stream,
	   unsigned long number)
{
  if (number > 0 && balsa_app.current_index)
    {
      balsa_app.new_messages = number;
    }
  else
    {
      balsa_app.new_messages = 0;
    }
}


void
mm_expunged (MAILSTREAM * stream, unsigned long number)
{
  if (balsa_app.debug)
    {

    }
}


void
mm_flags (MAILSTREAM * stream, unsigned long number)
{
  if (balsa_app.debug)
    {
      g_print ("Message %d in mailbox %s changed.\n", number, stream->mailbox);
    }
}


void
mm_notify (MAILSTREAM * stream, char *string, long errflg)
{
  if (balsa_app.debug)
    {
      g_print ("%s\n", string);
    }
}


void
mm_list (MAILSTREAM * stream, int delimiter, char *mailbox, long attributes)
{
  if (balsa_app.debug)
    {

    }
}


void
mm_lsub (MAILSTREAM * stream, int delimiter, char *mailbox, long attributes)
{
  if (balsa_app.debug)
    {

    }
}


void
mm_status (MAILSTREAM * stream, char *mailbox, MAILSTATUS * status)
{
  if (balsa_app.debug)
    {

    }
}


void
mm_log (char *string, long errflg)
{
  if (balsa_app.debug)
    {
      g_print ("%s\n", string);
    }
}


void
mm_dlog (char *string)
{
  if (balsa_app.debug)
    {
      g_print ("%s\n", string);
    }
}


void
mm_login (NETMBX * mb, char *user, char *pwd, long trial)
{
  MailboxPOP3 *pop3;
  MailboxIMAP *imap;

  if (!balsa_app.auth_mailbox)
    return;

  switch (balsa_app.auth_mailbox->type)
    {
    case MAILBOX_POP3:
      pop3 = (MailboxPOP3 *) balsa_app.auth_mailbox;
      strcpy (user, pop3->user);
      strcpy (pwd, pop3->passwd);
      break;

    case MAILBOX_IMAP:
      imap = (MailboxIMAP *) balsa_app.auth_mailbox;
      strcpy (user, imap->user);
      strcpy (pwd, imap->passwd);
      break;

    default:
      break;
    }
}



void
mm_critical (MAILSTREAM * stream)
{
  if (balsa_app.debug)
    {
      g_print ("Mailbox: %s - entering critical mode.\n", stream->mailbox);
    }
}


void
mm_nocritical (MAILSTREAM * stream)
{
  if (balsa_app.debug)
    {
      g_print ("Mailbox: %s - exiting critical mode.\n", stream->mailbox);
    }
}


long
mm_diskerror (MAILSTREAM * stream, long errcode, long serious)
{
  return NIL;
}


void
mm_fatal (char *string)
{
  if (balsa_app.debug)
    {
      g_print ("%s\n", string);
    }
}
