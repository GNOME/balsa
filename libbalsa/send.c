/* Balsa E-Mail Library
 * Copyright (C) 1998 Stuart Parmenter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>

#include "misc.h"
#include "mailbox.h"

#include "mailbackend.h"

gboolean
send_message (Message * message, gchar *smtp_server, glong debug)
{
#if 0
  char line[MAILTMPLEN];

  SENDSTREAM *stream = NIL;
  ENVELOPE *envelope = mail_newenvelope ();
  BODY *body = mail_newbody ();

  gchar *text;

  char *hostlist[] =
  {				/* SMTP server host list */
    NULL,
    "localhost",
    NIL
  };
  hostlist[0] = g_strdup(smtp_server);

  envelope->from = mail_newaddr ();
  envelope->from->personal = g_strdup (message->from->personal);
  envelope->from->mailbox = g_strdup (message->from->user);
  envelope->from->host = g_strdup (message->from->host);
  envelope->return_path = mail_newaddr ();
  envelope->return_path->mailbox = g_strdup (message->from->user);
  envelope->return_path->host = g_strdup (message->from->host);

  rfc822_parse_adrlist (&envelope->to,
			make_string_from_list (message->to_list),
			message->from->host);
  if (message->cc_list)
    rfc822_parse_adrlist (&envelope->cc,
			  make_string_from_list (message->cc_list),
			  message->from->host);
  envelope->subject = g_strdup (message->subject);
  body->type = TYPETEXT;

  /* FIXME: mabey problem */
  text = ((Body *) (g_list_first (message->body_list)->data))->buffer;

  body->contents.text.data = g_strdup (text);
  body->contents.text.size = strlen (body->contents.text.data);

  rfc822_date (line);
  envelope->date = (char *) fs_get (1 + strlen (line));
  strcpy (envelope->date, line);
  if (envelope->to)
    {
      fprintf (stderr, "Sending...\n");
      if (stream = smtp_open (hostlist, debug))
	{
	  if (smtp_mail (stream, "MAIL", envelope, body))
	    fprintf (stderr, "[Ok]\n");
	  else
	    fprintf (stderr, "[Failed - %s]\n", stream->reply);
	}
    }
  if (stream)
    smtp_close (stream);
  else
    fprintf (stderr, "[Can't open connection to any server]\n");
  mail_free_envelope (&envelope);
  mail_free_body (&body);
#endif
  return TRUE;
}

