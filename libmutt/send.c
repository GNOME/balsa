/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */ 

#include "mutt.h"
#include "mutt_curses.h"
#include "keymap.h"
#include "mime.h"
#include "mailbox.h"
#include "copy.h"
#include "mx.h"

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>






FILE *mutt_open_read (const char *path, pid_t *thepid)
{
  FILE *f;
  int len = strlen (path);

  if (path[len-1] == '|')
  {
    /* read from a pipe */

    char *s = safe_strdup (path);

    s[len-1] = 0;
    endwin ();
    *thepid = mutt_create_filter (s, NULL, &f, NULL);
    free (s);
  }
  else
  {
    f = fopen (path, "r");
    *thepid = -1;
  }
  return (f);
}

static void append_signature (ENVELOPE *env, FILE *f)
{
  FILE *tmpfp;
  pid_t thepid;

  if ((tmpfp = mutt_open_read (Signature, &thepid)))
  {
    if (option (OPTSIGDASHES))
      fputs ("\n-- \n", f);
    mutt_copy_stream (tmpfp, f);
    fclose (tmpfp);
    if (thepid != -1)
      mutt_wait_filter (thepid);
  }
}

/* compare two e-mail addresses and return 1 if they are equivalent */
static int mutt_addrcmp (ADDRESS *a, ADDRESS *b)
{
  if (!a->mailbox || !b->mailbox)
    return 0;
  if (strcasecmp (a->mailbox, b->mailbox))
    return 0;
  return 1;
}

/* search an e-mail address in a list */
static int mutt_addrsrc (ADDRESS *a, ADDRESS *lst)
{
  for (; lst; lst = lst->next)
  {
    if (mutt_addrcmp (a, lst))
      return (1);
  }
  return (0);
}

/* removes addresses from "b" which are contained in "a" */
static ADDRESS *mutt_remove_xrefs (ADDRESS *a, ADDRESS *b)
{
  ADDRESS *top, *p, *prev = NULL;

  top = b;
  while (b)
  {
    p = a;
    while (p)
    {
      if (mutt_addrcmp (p, b))
	break;
      p = p->next;
    }
    if (p)
    {
      if (prev)
      {
	prev->next = b->next;
	b->next = NULL;
	rfc822_free_address (&b);
	b = prev;
      }
      else
      {
	top = top->next;
	b->next = NULL;
	rfc822_free_address (&b);
	b = top;
      }
    }
    else
    {
      prev = b;
      b = b->next;
    }
  }
  return top;
}

/* remove any address which matches the current user.  if `leave_only' is
 * nonzero, don't remove the user's address if it is the only one in the list
 */
static ADDRESS *remove_user (ADDRESS *a, int leave_only)
{
  ADDRESS *top = NULL, *last = NULL;

  while (a)
  {
    if (!mutt_addr_is_user (a))
    {
      if (top)
      {
        last->next = a;
        last = last->next;
      }
      else
        last = top = a;
      a = a->next;
      last->next = NULL;
    }
    else
    {
      ADDRESS *tmp = a;
      
      a = a->next;
      if (!leave_only || a || last)
      {
	tmp->next = NULL;
	rfc822_free_address (&tmp);
      }
      else
	last = top = tmp;
    }
  }
  return top;
}

static ADDRESS *find_mailing_lists (ADDRESS *t, ADDRESS *c)
{
  ADDRESS *top = NULL, *ptr = NULL;

  for (; t || c; t = c, c = NULL)
  {
    for (; t; t = t->next)
    {
      if (mutt_is_mail_list (t))
      {
	if (top)
	{
	  ptr->next = rfc822_cpy_adr_real (t);
	  ptr = ptr->next;
	}
	else
	  ptr = top = rfc822_cpy_adr_real (t);
      }
    }
  }
  return top;
}

static int edit_envelope (ENVELOPE *en)
{
  char buf[HUGE_STRING];

  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), en->to);
  if (mutt_get_field ("To: ", buf, sizeof (buf), M_ALIAS) != 0 || !buf[0])
    return (-1);
  rfc822_free_address (&en->to);
  en->to = mutt_parse_adrlist (en->to, buf);
  en->to = mutt_expand_aliases (en->to);

  if (option (OPTASKCC))
  {
    buf[0] = 0;
    rfc822_write_address (buf, sizeof (buf), en->cc);
    if (mutt_get_field ("Cc: ", buf, sizeof (buf), M_ALIAS) != 0)
      return (-1);
    rfc822_free_address (&en->cc);
    en->cc = mutt_parse_adrlist (en->cc, buf);
    en->cc = mutt_expand_aliases (en->cc);
  }

  if (option (OPTASKBCC))
  {
    buf[0] = 0;
    rfc822_write_address (buf, sizeof (buf), en->bcc);
    if (mutt_get_field ("Bcc: ", buf, sizeof (buf), M_ALIAS) != 0)
      return (-1);
    rfc822_free_address (&en->bcc);
    en->bcc = mutt_parse_adrlist (en->bcc, buf);
    en->bcc = mutt_expand_aliases (en->bcc);
  }

  if (en->subject)
  {
    if (option (OPTFASTREPLY))
      return (0);
    else
      strfcpy (buf, en->subject, sizeof (buf));
  }
  else
    buf[0] = 0;
  if (mutt_get_field ("Subject: ", buf, sizeof (buf), 0) != 0 ||
      (!buf[0] && query_quadoption (OPT_SUBJECT, "No subject, abort?") != 0))
    {
      mutt_message ("No subject, aborting.");
      return (-1);
    }
  safe_free ((void **) &en->subject);
  en->subject = safe_strdup (buf);

  return 0;
}

static void process_user_recips (ENVELOPE *env)
{
  LIST *uh = UserHeader;

  for (; uh; uh = uh->next)
  {
    if (strncasecmp ("to:", uh->data, 3) == 0)
      env->to = rfc822_parse_adrlist (env->to, uh->data + 3);
    else if (strncasecmp ("cc:", uh->data, 3) == 0)
      env->cc = rfc822_parse_adrlist (env->cc, uh->data + 3);
    else if (strncasecmp ("bcc:", uh->data, 4) == 0)
      env->bcc = rfc822_parse_adrlist (env->bcc, uh->data + 4);
  }
}

static void process_user_header (ENVELOPE *env)
{
  LIST *uh = UserHeader;
  LIST *last = env->userhdrs;

  if (last)
    while (last->next)
      last = last->next;

  for (; uh; uh = uh->next)
  {
    if (strncasecmp ("from:", uh->data, 5) == 0)
    {
      /* User has specified a default From: address.  Get rid of the
       * default address.
       */
      rfc822_free_address (&env->from);
      env->from = rfc822_parse_adrlist (env->from, uh->data + 5);
    }
    else if (strncasecmp ("reply-to:", uh->data, 9) == 0)
    {
      rfc822_free_address (&env->reply_to);
      env->reply_to = rfc822_parse_adrlist (env->reply_to, uh->data + 9);
    }
    else if (strncasecmp ("to:", uh->data, 3) != 0 &&
	     strncasecmp ("cc:", uh->data, 3) != 0 &&
	     strncasecmp ("bcc:", uh->data, 4) != 0)
    {
      if (last)
      {
	last->next = mutt_new_list ();
	last = last->next;
      }
      else
	last = env->userhdrs = mutt_new_list ();
      last->data = safe_strdup (uh->data);
    }
  }
}

LIST *mutt_copy_list (LIST *p)
{
  LIST *t, *r=NULL, *l=NULL;

  for (; p; p = p->next)
  {
    t = (LIST *) safe_malloc (sizeof (LIST));
    t->data = safe_strdup (p->data);
    t->next = NULL;
    if (l)
    {
      r->next = t;
      r = r->next;
    }
    else
      l = r = t;
  }
  return (l);
}

static int include_forward (CONTEXT *ctx, HEADER *cur, FILE *out)
{
  char buffer[STRING];
  int chflags = CH_DECODE, cmflags = 0;






  fputs ("----- Forwarded message from ", out);
  buffer[0] = 0;
  rfc822_write_address (buffer, sizeof (buffer), cur->env->from);
  fputs (buffer, out);
  fputs (" -----\n\n", out);
  if (option (OPTFORWDECODE))
  {
    cmflags |= M_CM_DECODE;
    chflags |= CH_WEED;
  }
  if (option (OPTFORWQUOTE))
    cmflags |= M_CM_PREFIX;
  mutt_parse_mime_message (ctx, cur);
  mutt_copy_message (out, ctx, cur, cmflags, chflags);
  fputs ("\n----- End forwarded message -----\n", out);
  return 0;
}

static int include_reply (CONTEXT *ctx, HEADER *cur, FILE *out)
{
  char buffer[STRING];
  int flags = M_CM_PREFIX | M_CM_DECODE;






  if (Attribution[0])
  {
    mutt_make_string (buffer, sizeof (buffer), Attribution, cur);
    fputs (buffer, out);
    fputc ('\n', out);
  }
  if (!option (OPTHEADER))
    flags |= M_CM_NOHEADER;
  mutt_parse_mime_message (ctx, cur);
  mutt_copy_message (out, ctx, cur, flags, CH_DECODE);
  if (PostIndentString[0])
  {
    mutt_make_string (buffer, sizeof (buffer), PostIndentString, cur);
    fputs (buffer, out);
    fputc ('\n', out);
  }
  return 0;
}

static BODY *make_forward (CONTEXT *ctx, HEADER *hdr)
{
  char buffer[LONG_STRING];
  BODY *body;
  FILE *fpout;

  mutt_mktemp (buffer);
  if ((fpout = safe_fopen (buffer, "w")) == NULL)
    return NULL;

  body = mutt_new_body ();
  body->type = TYPEMESSAGE;
  body->subtype = safe_strdup ("rfc822");
  body->filename = safe_strdup (buffer);
  body->unlink = 1;
  body->use_disp = 0;

  /* this MUST come after setting ->filename because we reuse buffer[] */
  strfcpy (buffer, "Forwarded message from ", sizeof (buffer));
  rfc822_write_address (buffer + 23, sizeof (buffer) - 23, hdr->env->from);
  body->description = safe_strdup (buffer);

  mutt_parse_mime_message (ctx, hdr);
  mutt_copy_message (fpout, ctx, hdr,
		     option (OPTFORWDECODE) ? M_CM_DECODE : 0,
		     CH_XMIT | (option (OPTFORWDECODE) ? (CH_MIME | CH_TXTPLAIN ) : 0));
  
  fclose (fpout);
  mutt_update_encoding (body);
  return (body);
}

/* append list 'b' to list 'a' and return the last element in the new list */
static ADDRESS *rfc822_append (ADDRESS **a, ADDRESS *b)
{
  ADDRESS *tmp = *a;

  while (tmp && tmp->next)
    tmp = tmp->next;
  if (!b)
    return tmp;
  if (tmp)
    tmp->next = rfc822_cpy_adr (b);
  else
    tmp = *a = rfc822_cpy_adr (b);
  while (tmp && tmp->next)
    tmp = tmp->next;
  return tmp;
}

static int default_to (ADDRESS **to, ENVELOPE *env, int group)
{
  char prompt[STRING];
  int i = 0;

  if (group)
  {
    if (env->mail_followup_to)
    {
      rfc822_append (to, env->mail_followup_to);
      return 0;
    }
    if (mutt_addr_is_user (env->from))
      return 0;
  }

  if (!group && mutt_addr_is_user (env->from))
  {
    /* mail is from the user, assume replying to recipients */
    rfc822_append (to, env->to);
  }
  else if (env->reply_to)
  {
    if (option (OPTIGNORELISTREPLYTO) &&
	mutt_is_mail_list (env->reply_to) &&
	mutt_addrsrc (env->reply_to, env->to))
    {
      /* If the Reply-To: address is a mailing list, assume that it was
       * put there by the mailing list, and use the From: address
       */
      rfc822_append (to, env->from);
    }
    else if (!mutt_addrcmp (env->from, env->reply_to) &&
	     quadoption (OPT_REPLYTO) != M_YES)
    {
      /* There are quite a few mailing lists which set the Reply-To:
       * header field to the list address, which makes it quite impossible
       * to send a message to only the sender of the message.  This
       * provides a way to do that.
       */
      snprintf (prompt, sizeof (prompt), "Reply to %s?", env->reply_to->mailbox);
      if ((i = query_quadoption (OPT_REPLYTO, prompt)) == M_YES)
	rfc822_append (to, env->reply_to);
      else if (i == M_NO)
	rfc822_append (to, env->from);
      else
	return (-1); /* abort */
    }
    else
      rfc822_append (to, env->reply_to);
  }
  else
    rfc822_append (to, env->from);

  return (0);
}

static int fetch_recips (ENVELOPE *out, ENVELOPE *in, int flags)
{
  ADDRESS *tmp;
  if (flags & SENDLISTREPLY)
  {
    tmp = find_mailing_lists (in->to, in->cc);
    rfc822_append (&out->to, tmp);
    rfc822_free_address (&tmp);
  }
  else
  {
    if (default_to (&out->to, in, flags & SENDGROUPREPLY) == -1)
      return (-1); /* abort */

    if ((flags & SENDGROUPREPLY) && !in->mail_followup_to)
    {
      rfc822_append (&out->to, in->to);
      rfc822_append (&out->cc, in->cc);
    }
  }
  return 0;
}

static int
envelope_defaults (ENVELOPE *env, CONTEXT *ctx, HEADER *cur, int flags)
{
  ENVELOPE *curenv = NULL;
  LIST *tmp;
  char buffer[STRING];
  int i = 0, tag = 0;

  if (!cur)
  {
    tag = 1;
    for (i = 0; i < ctx->vcount; i++)
      if (ctx->hdrs[ctx->v2r[i]]->tagged)
      {
	cur = ctx->hdrs[ctx->v2r[i]];
	curenv = cur->env;
	break;
      }

    if (!cur)
    {
      /* This could happen if the user tagged some messages and then did
       * a limit such that none of the tagged message are visible.
       */
      mutt_error ("No tagged messages are visible!");
      return (-1);
    }
  }
  else
    curenv = cur->env;

  if (flags & SENDREPLY)
  {
    if (tag)
    {
      HEADER *h;

      for (i = 0; i < ctx->vcount; i++)
      {
	h = ctx->hdrs[ctx->v2r[i]];
	if (h->tagged && fetch_recips (env, h->env, flags) == -1)
	  return -1;
      }
    }
    else if (fetch_recips (env, curenv, flags) == -1)
      return -1;

    if ((flags & SENDLISTREPLY) && !env->to)
    {
      mutt_error ("No mailing lists found!");
      return (-1);
    }

    if (! option (OPTMETOO))
    {
      /* the order is important here.  do the CC: first so that if the
       * the user is the only recipient, it ends up on the TO: field
       */
      env->cc = remove_user (env->cc, (env->to == NULL));
      env->to = remove_user (env->to, (env->cc == NULL));
    }

    /* the CC field can get cluttered, especially with lists */
    env->to = mutt_remove_duplicates (env->to);
    env->cc = mutt_remove_duplicates (env->cc);
    env->cc = mutt_remove_xrefs (env->to, env->cc);

    if (curenv->real_subj)
    {
      env->subject = safe_malloc (strlen (curenv->real_subj) + 5);
      sprintf (env->subject, "Re: %s", curenv->real_subj);
    }
    else
      env->subject = safe_strdup ("Re: your mail");

    /* add the In-Reply-To field */
    strfcpy (buffer, "In-Reply-To: ", sizeof (buffer));
    mutt_make_string (buffer + 13, sizeof (buffer) - 13, InReplyTo, cur);
    tmp = env->userhdrs;
    while (tmp && tmp->next)
      tmp = tmp->next;
    if (tmp)
    {
      tmp->next = mutt_new_list ();
      tmp = tmp->next;
    }
    else
      tmp = env->userhdrs = mutt_new_list ();
    tmp->data = safe_strdup (buffer);

    env->references = mutt_copy_list (curenv->references);

    if (curenv->message_id)
    {
      LIST *t;

      t = mutt_new_list ();
      t->data = safe_strdup (curenv->message_id);
      t->next = env->references;
      env->references = t;
    }
  }
  else if (flags & SENDFORWARD)
  {
    /* set the default subject for the message. */
    mutt_make_string (buffer, sizeof (buffer), ForwFmt, cur);
    env->subject = safe_strdup (buffer);
  }

  return (0);
}

static int
generate_body (FILE *tempfp,	/* stream for outgoing message */
	       HEADER *msg,	/* header for outgoing message */
	       int flags,	/* compose mode */
	       CONTEXT *ctx,	/* current mailbox */
	       HEADER *cur)	/* current message */
{
  int i;
  HEADER *h;
  BODY *tmp;

  if (flags & SENDREPLY)
  {
    if ((i = query_quadoption (OPT_INCLUDE, "Include message in reply?")) == -1)
      return (-1);

    if (i == M_YES)
    {
      if (!cur)
      {
	for (i = 0; i < ctx->vcount; i++)
	{
	  h = ctx->hdrs[ctx->v2r[i]];
	  if (h->tagged)
	  {
	    if (include_reply (ctx, h, tempfp) == -1)
	    {
	      mutt_error ("Could not include all requested messages!");
	      return (-1);
	    }
	    fputc ('\n', tempfp);
	  }
	}
      }
      else
	include_reply (ctx, cur, tempfp);
    }
  }
  else if (flags & SENDFORWARD)
  {
    if (option (OPTMIMEFWD))
    {
      BODY *last = msg->content;

      while (last && last->next)
	last = last->next;

      if (cur)
      {
	tmp = make_forward (ctx, cur);
	if (last)
	  last->next = tmp;
	else
	  msg->content = tmp;
      }
      else
      {
	for (i = 0; i < ctx->vcount; i++)
	{
	  if (ctx->hdrs[ctx->v2r[i]]->tagged)
	  {
	    tmp = make_forward (ctx, ctx->hdrs[ctx->v2r[i]]);
	    if (last)
	    {
	      last->next = tmp;
	      last = tmp;
	    }
	    else
	      last = msg->content = tmp;
	  }
	}
      }
    }
    else
    {
      if (cur)
	include_forward (ctx, cur, tempfp);
      else
	for (i=0; i < ctx->vcount; i++)
	  if (ctx->hdrs[ctx->v2r[i]]->tagged)
	    include_forward (ctx, ctx->hdrs[ctx->v2r[i]], tempfp);
    }
  }






  return (0);
}

static ADDRESS *mutt_set_followup_to (ENVELOPE *e)
{
  ADDRESS *t = NULL;

  if (mutt_is_list_recipient (e->to) || mutt_is_list_recipient (e->cc))
  {
    /* i am a list recipient, so set the Mail-Followup-To: field so that
     * i don't end up getting multiple copies of responses to my mail
     */
    t = rfc822_append (&e->mail_followup_to, e->to);
    rfc822_append (&t, e->cc);
    /* the following is needed if $metoo is set, because the ->to and ->cc
       may contain the user's private address(es) */
    e->mail_followup_to = remove_user (e->mail_followup_to, 0);
  }
  return (e->mail_followup_to);
}

/* remove the multipart body if it exists */
static BODY *remove_multipart (BODY *b)
{
  BODY *t;

  if (b->parts)
  {
    t = b;
    b = b->parts;
    t->parts = NULL;
    mutt_free_body (&t);
  }
  return b;
}

void
ci_send_message (int flags,		/* send mode */
		 HEADER *msg,		/* template to use for new message */
		 char *tempfile,	/* file specified by -i or -H */
		 CONTEXT *ctx,		/* current mailbox */
		 HEADER *cur)		/* current message */
{
  char buffer[LONG_STRING];
  char fcc[_POSIX_PATH_MAX] = ""; /* where to copy this message */
  FILE *tempfp = NULL;
  BODY *pbody;
  int i;
  int killfrom = 0;

   
  if (msg)
  {
    msg->env->to = mutt_expand_aliases (msg->env->to);
    msg->env->cc = mutt_expand_aliases (msg->env->cc);
    msg->env->bcc = mutt_expand_aliases (msg->env->bcc);
  }
  else
  {
    msg = mutt_new_header ();

    if (flags == SENDPOSTPONED)
    {
      if ((flags = mutt_get_postponed (ctx, msg, &cur)) < 0)
	goto cleanup;
    }
    else if (!flags && quadoption (OPT_RECALL) != M_NO && mutt_num_postponed ())
    {
      /* If the user is composing a new message, check to see if there
       * are any postponed messages first.
       */
      if ((i = query_quadoption (OPT_RECALL, "Recall postponed message?")) == -1)
	goto cleanup;

      if (i == M_YES)
      {
	if ((flags = mutt_get_postponed (ctx, msg, &cur)) < 0)
	  flags = 0;
      }
    }

    if (flags & SENDPOSTPONED)
    {
      if ((tempfp = safe_fopen (msg->content->filename, "a+")) == NULL)
      {
	mutt_perror (msg->content->filename);
	goto cleanup;
      }
    }

    if (!msg->env)
      msg->env = mutt_new_envelope ();
  }

  if (! (flags & (SENDKEY | SENDPOSTPONED)))
  {
    pbody = mutt_new_body ();
    pbody->next = msg->content; /* don't kill command-line attachments */
    msg->content = pbody;
    
    msg->content->type = TYPETEXT;
    msg->content->subtype = safe_strdup ("plain");
    msg->content->unlink = 1;
    msg->content->use_disp = 0;
    
    if (!tempfile)
    {
      mutt_mktemp (buffer);
      tempfp = safe_fopen (buffer, "w+");
      msg->content->filename = safe_strdup (buffer);
    }
    else
    {
      tempfp = safe_fopen (tempfile, "a+");
      msg->content->filename = safe_strdup (tempfile);
    }

    if (!tempfp)
    {
      dprint(1,(debugfile, "newsend_message: can't create tempfile %s (errno=%d)\n", tempfile, errno));
      mutt_perror (tempfile);
      goto cleanup;
    }
  }

  if (flags & SENDBATCH) 
  {
    mutt_copy_stream (stdin, tempfp);
    if (option (OPTHDRS))
    {
      process_user_recips (msg->env);
      process_user_header (msg->env);
    }
    if (option (OPTUSEFROM) && !msg->env->from)
      msg->env->from = mutt_default_from ();
  }
  else if (! (flags & SENDPOSTPONED))
  {
    if ((flags & (SENDREPLY | SENDFORWARD)) &&
	envelope_defaults (msg->env, ctx, cur, flags) == -1)
      goto cleanup;

    if (option (OPTHDRS))
      process_user_recips (msg->env);

    if (! (flags & SENDMAILX) &&
	! (option (OPTAUTOEDIT) && option (OPTEDITHDRS)) &&
	! ((flags & SENDREPLY) && option (OPTFASTREPLY)))
    {
      if (edit_envelope (msg->env) == -1)
	goto cleanup;
    }

    /* the from address must be set here regardless of whether or not
       $use_from is set so that the `~P' (from you) operator in send-hook
       patterns will work.  if $use_from is unset, the from address is killed
       after send-hooks are evaulated */
    if (!msg->env->from)
    {
      msg->env->from = mutt_default_from ();
      if (!option (OPTUSEFROM))
	killfrom = 1;
    }

    /* change settings based upon recipients */
    mutt_send_hook (msg);

    if (killfrom)
    {
      rfc822_free_address (&msg->env->from);
      killfrom = 0;
    }

    if (option (OPTHDRS))
      process_user_header (msg->env);






    /* include replies/forwarded messages */
    if (generate_body (tempfp, msg, flags, ctx, cur) == -1)
      goto cleanup;

    if (! (flags & (SENDMAILX | SENDKEY)) && strcmp (Editor, "builtin") != 0)
      append_signature (msg->env, tempfp);
  }

    
    fclose (tempfp);
    tempfp = NULL;
    
  if (cur && option (OPTREVNAME))
  {
    ADDRESS *tmp = NULL;

    for (tmp = cur->env->to; tmp; tmp = tmp->next)
    {
      if (mutt_addr_is_user (tmp))
	break;
    }

    if (!tmp)
    {
      for (tmp = cur->env->cc; tmp; tmp = tmp->next)
      {
	if (mutt_addr_is_user (tmp))
	  break;
      }
    }

    if (tmp)
    {
      ADDRESS *ptr;

      rfc822_free_address (&msg->env->from);

      /* rfc822_cpy_adr() copies the whole list, not just one element */
      ptr = tmp->next;
      tmp->next = NULL;
      msg->env->from = rfc822_cpy_adr (tmp);
      tmp->next = ptr;

      if (!msg->env->from->personal)
	msg->env->from->personal = safe_strdup (Realname);
    }
  }

  if (flags & SENDMAILX)
  {
    if (mutt_builtin_editor (msg->content->filename, msg, cur) == -1)
      goto cleanup;
  }
  else if (! (flags & SENDBATCH))
  {
    struct stat st;
    time_t mtime;

    stat (msg->content->filename, &st);
    mtime = st.st_mtime;

    mutt_update_encoding (msg->content);

    /* If the this isn't a text message, look for a mailcap edit command */
    if(! (flags & SENDKEY))
    {
      if (mutt_needs_mailcap (msg->content))
	mutt_edit_attachment (msg->content, 0);
      else if (strcmp ("builtin", Editor) == 0)
	mutt_builtin_editor (msg->content->filename, msg, cur);
      else if (option (OPTEDITHDRS))
	mutt_edit_headers (Editor, msg->content->filename, msg, fcc, sizeof (fcc));
      else
	mutt_edit_file (Editor, msg->content->filename);
    }

    if (! (flags & (SENDPOSTPONED | SENDFORWARD | SENDKEY)))
    {
      if (stat (msg->content->filename, &st) == 0)
      {
	/* if the file was not modified, bail out now */
	if (mtime == st.st_mtime &&
	    query_quadoption (OPT_ABORT, "Abort unmodified message?") == M_YES)
	{
	  mutt_message ("Aborted unmodified message.");
	  goto cleanup;
	}
      }
      else
	mutt_perror (msg->content->filename);
    }
  }

  if (!fcc[0])
  {
    /* set the default FCC */
    if (!msg->env->from)
    {
      msg->env->from = mutt_default_from ();
      killfrom = 1; /* no need to check $use_from because if the user specified
		       a from address it would have already been set by now */
    }
    mutt_select_fcc (fcc, sizeof (fcc), msg);
    if (killfrom)
    {
      rfc822_free_address (&msg->env->from);
      killfrom = 0;
    }
  }

  mutt_update_encoding (msg->content);

  if (! (flags & (SENDMAILX | SENDBATCH)))
  {
main_loop:

    if ((i = mutt_send_menu (msg, fcc, sizeof (fcc), cur)) == -1)
    {
      /* abort */
      mutt_message ("Mail not sent.");
      goto cleanup;
    }
    else if (i == 1)
    {
      /* postpone the message until later. */
      if (msg->content->next)
	msg->content = mutt_make_multipart (msg->content);
      if (mutt_write_fcc (Postponed, msg, (cur && (flags & SENDREPLY)) ? cur->env->message_id : NULL, 1) < 0)
      {
	msg->content = remove_multipart (msg->content);
	goto main_loop;
      }
      mutt_message ("Message postponed.");
      goto cleanup;
    }
  }

  if (!msg->env->to && !msg->env->cc && !msg->env->bcc)
  {
    if (! (flags & SENDBATCH))
    {
      mutt_error ("No recipients are specified!");
      goto main_loop;
    }
    else
    {
      puts ("No recipients were specified.");
      goto cleanup;
    }
  }

  if (!msg->env->subject && ! (flags & SENDBATCH) &&
      (i = query_quadoption (OPT_SUBJECT, "No subject, abort sending?")) != M_NO)
  {
    /* if the abort is automatic, print an error message */
    if (quadoption (OPT_SUBJECT) == M_YES)
      mutt_error ("No subject specified.");
    goto main_loop;
  }

  if (msg->content->next)
    msg->content = mutt_make_multipart (msg->content);






  mutt_expand_path (fcc, sizeof (fcc));

  if (!option (OPTNOCURSES) && ! (flags & SENDMAILX))
    mutt_message ("Sending message...");

  if (msg->env->bcc && ! (msg->env->to || msg->env->cc))
  {
    /* some MTA's will put an Apparently-To: header field showing the Bcc:
     * recipients if there is no To: or Cc: field, so attempt to suppress
     * it by using an empty To: field.
     */
    msg->env->to = rfc822_new_address ();
    msg->env->to->mailbox = safe_strdup ("undisclosed-recipients");
    msg->env->to->group = 1;
    msg->env->to->next = rfc822_new_address ();
  }

  rfc822_free_address (&msg->env->mail_followup_to);
  msg->env->mail_followup_to = mutt_set_followup_to (msg->env);

  if (mutt_send_message (msg, fcc) == -1)
  {
    msg->content = remove_multipart (msg->content);
    sleep (1); /* allow user to read the error message */
    goto main_loop;
  }

  if (!option (OPTNOCURSES) && ! (flags & SENDMAILX))
    mutt_message ("Mail sent.");

  /* now free up the memory used to generate this message. */
  mutt_free_header (&msg);

  if (flags & SENDREPLY)
  {
    if (cur)
      mutt_set_flag (ctx, cur, M_REPLIED, 1);
    else if (!(flags & SENDPOSTPONED) && ctx && ctx->tagged)
    {
      for (i = 0; i < ctx->vcount; i++)
	if (ctx->hdrs[ctx->v2r[i]]->tagged)
	  mutt_set_flag (ctx, ctx->hdrs[ctx->v2r[i]], M_REPLIED, 1);
    }
  }

   
  return; /* all done */

cleanup:

   
  if (tempfp)
    fclose (tempfp);
  mutt_free_header (&msg);
}
