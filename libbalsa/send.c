/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Stuart Parmenter
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

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>

#include "mailbox.h"
#include "misc.h"

#include "mailbackend.h"

gboolean
send_message (Message * message, gchar * smtp_server, glong debug)
{
  FILE *tempfp = NULL;
  BODY *pbody;
  HEADER *msg;
  gchar *text;
  gchar buffer[PATH_MAX];
  gchar *tmp;

  msg = mutt_new_header ();

  if (!msg->env)
    msg->env = mutt_new_envelope ();

  pbody = mutt_new_body ();
  pbody->next = msg->content;	/* don't kill command-line attachments */
  msg->content = pbody;

  msg->content->type = TYPETEXT;
  msg->content->subtype = safe_strdup ("plain");
  msg->content->unlink = 1;
  msg->content->use_disp = 0;

  mutt_mktemp (buffer);
  tempfp = safe_fopen (buffer, "w+");
  msg->content->filename = safe_strdup (buffer);
/*
  process_user_header (msg->env);
*/
  tmp = address_to_gchar(message->from);
  msg->env->from = rfc822_parse_adrlist(msg->env->from, tmp);
  g_free(tmp);
  msg->env->subject = g_strdup(message->subject);

  msg->env->to = rfc822_parse_adrlist (msg->env->to, make_string_from_list(message->to_list));
  msg->env->cc = rfc822_parse_adrlist (msg->env->cc, make_string_from_list(message->cc_list));
  msg->env->bcc = rfc822_parse_adrlist (msg->env->bcc, make_string_from_list(message->bcc_list));
	  
  fclose (tempfp);
  tempfp = NULL;

#if 0

  body->type = TYPETEXT;

  /* FIXME: mabey problem */
  text = ((Body *) (g_list_first (message->body_list)->data))->buffer;

  body->contents.text.data = g_strdup (text);
  body->contents.text.size = strlen (body->contents.text.data);

  if (msg->env->to)
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

  if (tempfp)
    fclose (tempfp);
  mutt_free_header (&msg);

  return TRUE;
}
