/*
 * Copyright (C) 1996-1998 Michael R. Elkins <me@cs.hmc.edu>
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
#include "mutt_menu.h"
#include "rfc1524.h"
#include "mime.h"
#include "mailbox.h"
#include "attach.h"
#include "mapping.h"
#include "mx.h"
#include "copy.h"






#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>

static struct mapping_t AttachHelp[] = {
  { "Exit",  OP_EXIT },
  { "Save",  OP_SAVE },
  { "Pipe",  OP_PIPE },
  { "Print", OP_PRINT },
  { "Help",  OP_HELP },
  { NULL }
};

void mutt_update_tree (ATTACHPTR **idx, short idxlen)
{
  char buf[STRING];
  char *s;
  int x;

  for (x = 0; x < idxlen; x++)
  {
    if (2 * (idx[x]->level + 2) < sizeof (buf))
    {
      if (idx[x]->level)
      {
	s = buf + 2 * (idx[x]->level - 1);
	*s++ = (idx[x]->content->next) ? M_TREE_LTEE : M_TREE_LLCORNER;
	*s++ = M_TREE_HLINE;
	*s++ = M_TREE_RARROW;
      }
      else
	s = buf;
      *s = 0;
    }

    if (idx[x]->tree)
    {
      if (strcmp (idx[x]->tree, buf) != 0)
      {
	safe_free ((void **) &idx[x]->tree);
	idx[x]->tree = safe_strdup (buf);
      }
    }
    else
      idx[x]->tree = safe_strdup (buf);

    if (2 * (idx[x]->level + 2) < sizeof (buf) && idx[x]->level)
    {
      s = buf + 2 * (idx[x]->level - 1);
      *s++ = (idx[x]->content->next) ? '\005' : '\006';
      *s++ = '\006';
    }
  }
}

ATTACHPTR **mutt_gen_attach_list (BODY *m,
				  ATTACHPTR **idx,
				  short *idxlen,
				  short *idxmax,
				  int level,
				  int compose)
{
  ATTACHPTR *new;

  for (; m; m = m->next)
  {
    if (*idxlen == *idxmax)
      safe_realloc ((void **) &idx, sizeof (ATTACHPTR *) * (*idxmax += 5));

    if (m->type == TYPEMULTIPART && m->parts)
    {
      idx = mutt_gen_attach_list (m->parts, idx, idxlen, idxmax, level, compose);
    }
    else
    {
      new = idx[(*idxlen)++] = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
      new->content = m;
      new->level = level;

      /* We don't support multipart messages in the compose menu yet */
      if (!compose && m->type == TYPEMESSAGE &&
	  (!strcasecmp (m->subtype, "rfc822") ||
	   !strcasecmp (m->subtype, "news")) && is_multipart (m->parts))
      {
	idx = mutt_gen_attach_list (m->parts, idx, idxlen, idxmax, level + 1, compose);
      }
    }
  }

  if (level == 0)
    mutt_update_tree (idx, *idxlen);

  return (idx);
}

void attach_entry (char *b, size_t blen, MUTTMENU *menu, int num)
{
  char t[SHORT_STRING];
  char s[SHORT_STRING];
  char size[SHORT_STRING];
  ATTACHPTR **idx = (ATTACHPTR **) menu->data;
  BODY *m;

  m = idx[num]->content;
  s[0] = 0;
  if (m->type == TYPEMESSAGE && (!strcasecmp ("rfc822", m->subtype) ||
      !strcasecmp ("news", m->subtype)) && MsgFmt[0])
    _mutt_make_string (s, sizeof (s), MsgFmt, NULL, m->hdr,
		       M_FORMAT_FORCESUBJ | M_FORMAT_MAKEPRINT);

  mutt_pretty_size (size, sizeof (size), m->length);
  snprintf (t, sizeof (t), "[%.7s/%.10s, %.6s, %s]",
	    TYPE (m->type), m->subtype, ENCODING (m->encoding), size);
  snprintf (b, blen, " %c%c %2d %-34.34s %s%s",
	    m->deleted ? 'D' : ' ',
	    m->tagged ? '*' : ' ',
	    num + 1,
	    t,
	    idx[num]->tree ? idx[num]->tree : "",
	    s[0] ? s : (m->description ? m->description :
			(m->filename ? m->filename : "<no description>")));
}

int mutt_tag_attach (MUTTMENU *menu, int n)
{
  return (((ATTACHPTR **) menu->data)[n]->content->tagged = !((ATTACHPTR **) menu->data)[n]->content->tagged);
}

static void mutt_query_save_attachment (FILE *fp, BODY *body)
{
  char buf[_POSIX_PATH_MAX], tfile[_POSIX_PATH_MAX];

  if (fp && body->filename)
    strfcpy (buf, body->filename, sizeof (buf));
  else
    buf[0] = 0;
  if (mutt_get_field ("Save to file: ", buf, sizeof (buf), M_FILE | M_CLEAR) != 0 || !buf[0])
    return;
  mutt_expand_path (buf, sizeof (buf));
  if (mutt_check_overwrite (body->filename, buf, tfile, sizeof (tfile), 0))
    return;
  mutt_message ("Saving...");
  if (mutt_save_attachment (fp, body, tfile, 0) == 0)
    mutt_message ("Attachment saved.");
}

void mutt_save_attachment_list (FILE *fp, int tag, BODY *top)
{
  for (; top; top = top->next)
  {
    if (!tag || top->tagged)
      mutt_query_save_attachment (fp, top);
    else if (top->parts)
      mutt_save_attachment_list (fp, 1, top->parts);
    if (!tag)
      return;
  }
}

static void
mutt_query_pipe_attachment (char *command, FILE *fp, BODY *body, int filter)
{
  char tfile[_POSIX_PATH_MAX];
  char warning[STRING+_POSIX_PATH_MAX];

  if (filter)
  {
    snprintf (warning, sizeof (warning),
	      "WARNING!  You are about to overwrite %s, continue?",
	      body->filename);
    if (mutt_yesorno (warning, M_NO) != M_YES) {
      CLEARLINE (LINES-1);
      return;
    }
    mutt_mktemp (tfile);
  }
  else
    tfile[0] = 0;

  if (mutt_pipe_attachment (fp, body, command, tfile))
  {
    if (filter)
    {
      mutt_unlink (body->filename);
      mutt_rename_file (tfile, body->filename);
      mutt_update_encoding (body);
      mutt_message ("Attachment filtered.");
    }
  }
  else
  {
    if (filter && tfile[0])
      mutt_unlink (tfile);
  }
}

static void
pipe_attachment_list (char *command, FILE *fp, int tag, BODY *top, int filter)
{
  for (; top; top = top->next)
  {
    if (!tag || top->tagged)
      mutt_query_pipe_attachment (command, fp, top, filter);
    else if (top->parts)
      pipe_attachment_list (command, fp, tag, top->parts, filter);
    if (!tag)
      break;
  }
}

void mutt_pipe_attachment_list (FILE *fp, int tag, BODY *top, int filter)
{
  char buf[SHORT_STRING];

  if (fp)
    filter = 0; /* sanity check: we can't filter in the recv case yet */

  buf[0] = 0;
  if (mutt_get_field ((filter ? "Filter through: " : "Pipe to: "),
				  buf, sizeof (buf), 0) != 0 || !buf[0])
    return;
  mutt_expand_path (buf, sizeof (buf));
  pipe_attachment_list (buf, fp, tag, top, filter);
}

static void print_attachment_list (FILE *fp, int tag, BODY *top)
{
  for (; top; top = top->next)
  {
    if (!tag || top->tagged)
      mutt_print_attachment (fp, top);
    else if (top->parts)
      mutt_print_attachment_list (fp, tag, top->parts);
    if (!tag)
      return;
  }
}

void mutt_print_attachment_list (FILE *fp, int tag, BODY *top)
{
  if (query_quadoption (OPT_PRINT, tag ? "Print tagged attachment(s)?" : "Print attachment?") != M_YES)
    return;
  print_attachment_list (fp, tag, top);
}

int mutt_is_message_type (int type, char *subtype)
{
  if (type != TYPEMESSAGE)
    return 0;
  if (strcasecmp (subtype, "rfc822") == 0 || strcasecmp (subtype, "news") == 0)
    return 1;
  return 0;
}

static void
bounce_attachment_list (ADDRESS *adr, int tag, BODY *body, HEADER *hdr)
{
  for (; body; body = body->next)
  {
    if (!tag || body->tagged)
    {
      if (!mutt_is_message_type (body->type, body->subtype))
      {
	mutt_error ("You may only bounce message/rfc822 parts.");
	continue;
      }
      body->hdr->msgno = hdr->msgno;
      mutt_bounce_message (body->hdr, adr);
    }
    else if (body->parts)
      bounce_attachment_list (adr, tag, body->parts, hdr);
    if (!tag)
      break;
  }
}

static void query_bounce_attachment (int tag, BODY *top, HEADER *hdr)
{
  char prompt[SHORT_STRING];
  char buf[HUGE_STRING];
  ADDRESS *adr = NULL;
  int rc;

  buf[0] = 0;
  snprintf (prompt, sizeof (prompt), "Bounce %smessage%s to: ",
	    tag ? "tagged " : "", tag ? "s" : "");
  rc = mutt_get_field (prompt, buf, sizeof (buf), M_ALIAS);

  if (rc || !buf[0])
    return;

  adr = rfc822_parse_adrlist (adr, buf);
  adr = mutt_expand_aliases (adr);
  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), adr);
  snprintf (prompt, sizeof (prompt), "Bounce message%s to %s...?", (tag ? "s" : ""), buf);
  if (mutt_yesorno (prompt, 1) != 1)
  {
    rfc822_free_address (&adr);
    CLEARLINE (LINES-1);
    return;
  }
  bounce_attachment_list (adr, tag, top, hdr);
  rfc822_free_address (&adr);
}

static void
copy_tagged_attachments (FILE *fpout, FILE *fpin, const char *boundary, BODY *bdy)
{
  for (; bdy; bdy = bdy->next)
  {
    if (bdy->tagged)
    {
      fprintf (fpout, "--%s\n", boundary);
      fseek (fpin, bdy->hdr_offset, 0);
      mutt_copy_bytes (fpin, fpout, bdy->length + bdy->offset - bdy->hdr_offset);
    }
    else if (bdy->parts)
      copy_tagged_attachments (fpout, fpin, boundary, bdy->parts);
  }
}

static int
create_tagged_message (const char *tempfile,
		       int tag,
		       CONTEXT *ctx,
		       HEADER *cur,
		       BODY *body)
{
  char *boundary;
  MESSAGE *msg, *src;
  CONTEXT tmpctx;
  int magic;

  magic = DefaultMagic;
  DefaultMagic = M_MBOX;
  mx_open_mailbox (tempfile, M_APPEND, &tmpctx);
  msg = mx_open_new_message (&tmpctx, cur, M_ADD_FROM);
  src = mx_open_message (ctx, cur->msgno);

  if (tag)
  {
    mutt_copy_header (src->fp, cur, msg->fp, CH_XMIT, NULL);
    boundary = mutt_get_parameter ("boundary", cur->content->parameter);
    copy_tagged_attachments (msg->fp, src->fp, boundary, cur->content->parts);
    fprintf (msg->fp, "--%s--\n", boundary);
  }
  else
  {
    /* single attachment */
    mutt_copy_header (src->fp, cur, msg->fp, CH_XMIT | CH_MIME | CH_NONEWLINE, NULL);
    fputs ("Mime-Version: 1.0\n", msg->fp);
    mutt_write_mime_header (body, msg->fp);
    fputc ('\n', msg->fp);
    fseek (src->fp, body->offset, 0);
    mutt_copy_bytes (src->fp, msg->fp, body->length);
  }

  mx_close_message (&msg);
  mx_close_message (&src);
  mx_close_mailbox (&tmpctx);
  DefaultMagic = magic;
  return 0;
}

/* op		flag to ci_send_message()
   tag		operate on tagged attachments?
   hdr		current message
   body		current attachment */
static void reply_attachment_list (int op, int tag, HEADER *hdr, BODY *body)
{
  HEADER *hn;
  char tempfile[_POSIX_PATH_MAX];
  CONTEXT *ctx;

  if (!tag && body->hdr)
  {
    hn = body->hdr;
    hn->msgno = hdr->msgno; /* required for MH/maildir */
    ctx = Context;
  }
  else
  {
    /* build a fake message which consists of only the tagged attachments */
    mutt_mktemp (tempfile);
    create_tagged_message (tempfile, tag, Context, hdr, body);
    ctx = mx_open_mailbox (tempfile, M_QUIET, NULL);
    hn = ctx->hdrs[0];
  }

  ci_send_message (op, NULL, NULL, ctx, hn);

  if (hn->replied && !hdr->replied)
    mutt_set_flag (Context, hdr, M_REPLIED, 1);

  if (ctx != Context)
  {
    mx_fastclose_mailbox (ctx);
    safe_free ((void **) &ctx);
    unlink (tempfile);
  }
}

void
mutt_attach_display_loop (MUTTMENU *menu, int op, FILE *fp, ATTACHPTR **idx)
{
  int old_optweed = option (OPTWEED);

  set_option (OPTWEED);
  do
  {
    switch (op)
    {
      case OP_DISPLAY_HEADERS:
	toggle_option (OPTWEED);
	/* fall through */

      case OP_VIEW_ATTACH:
	op = mutt_view_attachment (fp, idx[menu->current]->content, M_REGULAR);
	break;

      case OP_NEXT_ENTRY:
      case OP_MAIN_NEXT_UNDELETED: /* hack */
	if (menu->current < menu->max - 1)
	{
	  menu->current++;
	  op = OP_VIEW_ATTACH;
	}
	else
	  op = OP_NULL;
	break;
      case OP_PREV_ENTRY:
      case OP_MAIN_PREV_UNDELETED: /* hack */
	if (menu->current > 0)
	{
	  menu->current--;
	  op = OP_VIEW_ATTACH;
	}
	else
	  op = OP_NULL;
	break;
      default:
	op = OP_NULL;
    }
  }
  while (op != OP_NULL);

  if (option (OPTWEED) != old_optweed)
    toggle_option (OPTWEED);
}

void mutt_view_attachments (HEADER *hdr)
{






  char helpstr[SHORT_STRING];
  MUTTMENU *menu;
  BODY *cur;
  MESSAGE *msg;
  FILE *fp;
  ATTACHPTR **idx = NULL;
  short idxlen = 0;
  short idxmax = 0;
  int flags = 0;
  int op;
  
  /* make sure we have parsed this message */
  mutt_parse_mime_message (Context, hdr);

  if ((msg = mx_open_message (Context, hdr->msgno)) == NULL)
    return;
















  {
    fp = msg->fp;
    cur = hdr->content;
  }

  idx = mutt_gen_attach_list (cur, idx, &idxlen, &idxmax, 0, 0);

  menu = mutt_new_menu ();
  menu->max = idxlen;
  menu->make_entry = attach_entry;
  menu->tag = mutt_tag_attach;
  menu->menu = MENU_ATTACH;
  menu->title = "Attachments";
  menu->data = idx;
  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_ATTACH, AttachHelp);

  FOREVER
  {
    switch (op = mutt_menuLoop (menu))
    {
      case OP_DISPLAY_HEADERS:
      case OP_VIEW_ATTACH:
	mutt_attach_display_loop (menu, op, fp, idx);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_ATTACH_VIEW_MAILCAP:
	mutt_view_attachment (fp, idx[menu->current]->content, M_MAILCAP);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_ATTACH_VIEW_TEXT:
	mutt_view_attachment (fp, idx[menu->current]->content, M_AS_TEXT);
	menu->redraw = REDRAW_FULL;
	break;



      


      case OP_PRINT:
	mutt_print_attachment_list (fp, menu->tagprefix, menu->tagprefix ? cur : idx[menu->current]->content);
	break;

      case OP_PIPE:
	mutt_pipe_attachment_list (fp, menu->tagprefix, menu->tagprefix ? cur : idx[menu->current]->content, 0);
	break;

      case OP_SAVE:
	mutt_save_attachment_list (fp, menu->tagprefix, menu->tagprefix ? cur : idx[menu->current]->content);
	if (option (OPTRESOLVE) && menu->current < menu->max - 1)
	{
	  menu->current++;
	  menu->redraw = REDRAW_MOTION_RESYNCH;
	}
	else
	  menu->redraw = REDRAW_CURRENT;
	break;

      case OP_DELETE:

       if (menu->max == 1)
       {
         mutt_message ("Only deletion of multipart attachments is supported.");
       }
       else
       {
         {
	   if (!menu->tagprefix)
	   {
	     idx[menu->current]->content->deleted = 1;
	     if (option (OPTRESOLVE) && menu->current < menu->max - 1)
	     {
	       menu->current++;
	       menu->redraw = REDRAW_MOTION_RESYNCH;
	     }
	     else
	       menu->redraw = REDRAW_CURRENT;
	   }
	   else
	   {
	     int x;

	     for (x = 0; x < menu->max; x++)
	     {
	       if (idx[x]->content->tagged)
	       {
		 idx[x]->content->deleted = 1;
		 menu->redraw = REDRAW_INDEX;
	       }
	     }
	   }
         }
       }
       break;

      case OP_UNDELETE:
       if (!menu->tagprefix)
       {
	 idx[menu->current]->content->deleted = 0;
	 if (option (OPTRESOLVE) && menu->current < menu->max - 1)
	 {
	   menu->current++;
	   menu->redraw = REDRAW_MOTION_RESYNCH;
	 }
	 else
	   menu->redraw = REDRAW_CURRENT;
       }
       else
       {
	 int x;

	 for (x = 0; x < menu->max; x++)
	 {
	   if (idx[x]->content->tagged)
	   {
	     idx[x]->content->deleted = 0;
	     menu->redraw = REDRAW_INDEX;
	   }
	 }
       }
       break;

      case OP_BOUNCE_MESSAGE:
	query_bounce_attachment (menu->tagprefix, menu->tagprefix ? cur : idx[menu->current]->content, hdr);
	break;

      case OP_REPLY:
      case OP_GROUP_REPLY:
      case OP_LIST_REPLY:
      case OP_FORWARD_MESSAGE:






	if (op == OP_FORWARD_MESSAGE)
	  flags = SENDFORWARD;
	else
	  flags = SENDREPLY | 
		  (op == OP_GROUP_REPLY ? SENDGROUPREPLY : 0) |
		  (op == OP_LIST_REPLY ? SENDLISTREPLY : 0);
	reply_attachment_list (flags,
			       menu->tagprefix,
			       hdr,
			       menu->tagprefix ? cur : idx[menu->current]->content);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_EXIT:
	mx_close_message (&msg);
	hdr->attach_del = 0;
	while (idxlen-- > 0)
	{
	  if (idx[idxlen]->content->deleted)
	    hdr->attach_del = 1;
	  safe_free ((void **) &idx[idxlen]->tree);
	  safe_free ((void **) &idx[idxlen]);
	}
	if (hdr->attach_del)
	  hdr->changed = 1;
	safe_free ((void **) &idx);
	idxmax = 0;














	mutt_menuDestroy  (&menu);
	return;
    }
  }

  /* not reached */
}
