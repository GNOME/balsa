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

#include "balsa-app.h"
#include "mailbox.h"
#include "misc.h"
#include "mailbackend.h"
#include "send.h"

BODY *
add_mutt_body_plain (void)
{
  BODY *body;
  gchar buffer[PATH_MAX];

  body = mutt_new_body ();

  body->type = TYPETEXT;
  body->subtype = g_strdup ("plain");
  body->unlink = 1;
  body->use_disp = 0;

  mutt_mktemp (buffer);
  body->filename = g_strdup (buffer);
  mutt_update_encoding (body);

  return body;
}

gboolean
balsa_send_message (Message * message, gchar * smtp_server, glong debug)
{
  HEADER *msg;
  BODY *last, *newbdy;
  gchar *tmp;
  GList *list;

  msg = mutt_new_header ();

  if (!msg->env)
    msg->env = mutt_new_envelope ();

  msg->env->userhdrs = mutt_new_list ();
  {
    LIST *sptr = UserHeader;
    LIST *dptr = msg->env->userhdrs;
    LIST *delptr = 0;
    while (sptr)
      {
	dptr->data = g_strdup (sptr->data);
	sptr = sptr->next;
	delptr = dptr;
	dptr->next = mutt_new_list ();
	dptr = dptr->next;
      }
    g_free (delptr->next);
    delptr->next = 0;
  }

  tmp = address_to_gchar (message->from);
  msg->env->from = rfc822_parse_adrlist (msg->env->from, tmp);
  g_free (tmp);

  tmp = address_to_gchar (message->reply_to);
  msg->env->reply_to = rfc822_parse_adrlist (msg->env->reply_to, tmp);
  g_free (tmp);

  msg->env->subject = g_strdup (message->subject);

  msg->env->to = rfc822_parse_adrlist (msg->env->to, make_string_from_list (message->to_list));
  msg->env->cc = rfc822_parse_adrlist (msg->env->cc, make_string_from_list (message->cc_list));
  msg->env->bcc = rfc822_parse_adrlist (msg->env->bcc, make_string_from_list (message->bcc_list));

  list = message->body_list;

  last = msg->content;
  while (last && last->next)
    last = last->next;

  while (list)
    {
      FILE *tempfp = NULL;
      Body *body;
      newbdy = NULL;

      body = list->data;

      if (body->filename)
	newbdy = mutt_make_file_attach (body->filename);

      else if (body->buffer)
	{
	  newbdy = add_mutt_body_plain ();
	  tempfp = safe_fopen (newbdy->filename, "w+");
	  fputs (body->buffer, tempfp);
	  fclose (tempfp);
	  tempfp = NULL;
	}

      if (newbdy)
	{
	  if (last)
	    last->next = newbdy;
	  else
	    msg->content = newbdy;

	  last = newbdy;
	}

      list = list->next;
    }

  msg->content = mutt_make_multipart (msg->content);

  switch (balsa_app.outbox->type)
    {
    case MAILBOX_MAILDIR:
    case MAILBOX_MH:
    case MAILBOX_MBOX:
      /*
         send_message (msg, MAILBOX_LOCAL(balsa_app.outbox)->path);
       */
      mutt_send_message (msg);
      break;
    case MAILBOX_IMAP:
      mutt_send_message (msg);
      break;
    default:
      break;
    }

  mutt_free_header (&msg);

  return TRUE;
}
