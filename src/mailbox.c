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
#include "balsa-index.h"
#include "index.h"


Mailbox *
mailbox_new ( gchar * name,
	     gchar * mbox)
{
  Mailbox *mailbox;

  mailbox = g_malloc (sizeof (Mailbox));
  mailbox->name = g_strdup (name);
  mailbox->path = g_strdup (mbox);
  mailbox->stream = NIL;

  return mailbox;
};


int
mailbox_open (Mailbox * mailbox)
{
  Mailbox *old_mailbox;

  /* don't open a mailbox if it's already open 
   * -- runtime sanity */
  if (balsa_app.current_mailbox == mailbox)
    return TRUE;

  /* only one mailbox open at a time */
  old_mailbox = balsa_app.current_mailbox;
  balsa_app.current_mailbox = mailbox;

  /* try to open the mailbox -- return
   * FALSE on failure */
  mailbox->stream = mail_open (NIL, mailbox->path, NIL);
  if (mailbox->stream == NIL)
    {
      balsa_app.current_mailbox = old_mailbox;
      return FALSE;
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

void
current_mailbox_check ()
{
  mail_ping (balsa_app.current_mailbox->stream);
}
