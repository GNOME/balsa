/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter
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

#include "../src/balsa-app.h"
#include "mailbox.h"
#include "misc.h"
#include "mailbackend.h"
#include "send.h"

/* prototype this so that this file doesn't whine.  this function isn't in
 * mutt any longer, so we had to provide it inside mutt for libmutt :-)
 */
int mutt_send_message (HEADER * msg);
static void encode_descriptions (BODY * b);

BODY *add_mutt_body_plain (void);

/* from mutt's send.c */
static void
encode_descriptions (BODY * b)
{
  BODY *t;
  char tmp[LONG_STRING];

  for (t = b; t; t = t->next)
    {
      if (t->description)
	{
	  rfc2047_encode_string (tmp, sizeof (tmp), (unsigned char *) t->description
	    );
	  safe_free ((void **) &t->description);
	  t->description = safe_strdup (tmp);
	}
      if (t->parts)
	encode_descriptions (t->parts);
    }
}


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
  
  body->encoding = balsa_app.encoding_style;
  body->parameter = mutt_new_parameter();
  body->parameter->attribute = g_strdup("charset");
  body->parameter->value = g_strdup(balsa_app.charset);
  body->parameter->next = NULL;

  mutt_mktemp (buffer);
  body->filename = g_strdup (buffer);
  mutt_update_encoding (body);

  return body;
}

gboolean
balsa_send_message (Message * message)
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

  if (msg->content)
    {
      if (msg->content->next)
	msg->content = mutt_make_multipart (msg->content);
    }

  mutt_prepare_envelope (msg->env);

  encode_descriptions (msg->content);

/* FIXME */
  mutt_send_message (msg);
  
  if ((balsa_app.sentbox->type == MAILBOX_MAILDIR ||
       balsa_app.sentbox->type == MAILBOX_MH ||
       balsa_app.sentbox->type == MAILBOX_MBOX) &&
       message->fcc_mailbox != NULL) {
    mutt_write_fcc (MAILBOX_LOCAL (message->fcc_mailbox)->path, msg, NULL, 0);
    if (message->fcc_mailbox->open_ref > 0)
        mailbox_check_new_messages (message->fcc_mailbox);
  }
#if 0
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
#endif

  mutt_free_header (&msg);

  return TRUE;
}

gboolean
balsa_postpone_message (Message * message)
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

  if (msg->content)
    {
      if (msg->content->next)
	msg->content = mutt_make_multipart (msg->content);
    }

  mutt_prepare_envelope (msg->env);

  encode_descriptions (msg->content);

  mutt_write_fcc (MAILBOX_LOCAL (balsa_app.draftbox)->path, msg, NULL, 1);
  if (balsa_app.draftbox->open_ref > 0)
    mailbox_check_new_messages (balsa_app.draftbox);
  mutt_free_header (&msg);

  return TRUE;
}
