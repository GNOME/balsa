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
#include "index.h"
#include "mailbox.h"


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
  if (!balsa_app.main_window)
    return;
  if (number > 0)
    balsa_index_append_new_messages (BALSA_INDEX (balsa_app.main_window->index));
}


void
mm_expunged (MAILSTREAM * stream, unsigned long number)
{
}


void
mm_flags (MAILSTREAM * stream, unsigned long number)
{
}


void
mm_notify (MAILSTREAM * stream, char *string, long errflg)
{
}


void
mm_list (MAILSTREAM * stream, int delimiter, char *mailbox, long attributes)
{
}


void
mm_lsub (MAILSTREAM * stream, int delimiter, char *mailbox, long attributes)
{
}


void
mm_status (MAILSTREAM * stream, char *mailbox, MAILSTATUS * status)
{
}


void
mm_log (char *string, long errflg)
{
}


void
mm_dlog (char *string)
{

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
}


void
mm_nocritical (MAILSTREAM * stream)
{
}


long
mm_diskerror (MAILSTREAM * stream, long errcode, long serious)
{
  return NIL;
}


void
mm_fatal (char *string)
{
}
