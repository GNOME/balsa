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
#include "mutt_menu.h"
#include "rfc1524.h"
#include "mime.h"
#include "attach.h"
#include "mapping.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#define CHECK_COUNT if (idxlen == 0) { mutt_error ("There are no attachments."); break; }



enum
{
  HDR_FROM  = 1,
  HDR_TO,
  HDR_CC,
  HDR_BCC,
  HDR_SUBJECT,
  HDR_REPLYTO,
  HDR_FCC,


  
  

  HDR_ATTACH  = (HDR_FCC + 5) /* where to start printing the attachments */
};

#define HDR_XOFFSET 10

static struct mapping_t ComposeHelp[] = {
  { "Send",    OP_COMPOSE_SEND_MESSAGE },
  { "Abort",   OP_EXIT },
  { "To",      OP_COMPOSE_EDIT_TO },
  { "CC",      OP_COMPOSE_EDIT_CC },
  { "Subj",    OP_COMPOSE_EDIT_SUBJECT },
  { "Attach",  OP_COMPOSE_ATTACH_FILE },
  { "Descrip", OP_COMPOSE_EDIT_DESCRIPTION },
  { "Help",    OP_HELP },
  { NULL }
};

void snd_entry (char *b, size_t blen, MUTTMENU *menu, int num)
{
  char t[SHORT_STRING], size[SHORT_STRING];
  char tmp[_POSIX_PATH_MAX];
  BODY *m;
  ATTACHPTR **idx = (ATTACHPTR **) menu->data;
  struct stat finfo;

  m = idx[num]->content;

  if (m->filename && m->filename[0])
  {
    if (stat (m->filename, &finfo) != -1)
      mutt_pretty_size (size, sizeof (size), finfo.st_size);
    else
      strcpy (size, "0K");
    strfcpy (tmp, m->filename, sizeof (tmp));
  }
  else
  {
    strcpy (size, "0K");
    strcpy (tmp, "<no file>");
  }
  mutt_pretty_mailbox (tmp);

  snprintf (t, sizeof (t), "[%.7s/%.10s, %.6s, %s]",
	    TYPE (m->type), m->subtype, ENCODING (m->encoding), size);

  snprintf (b, blen, "%c%c%2d %-34.34s %s%s <%s>",
	    m->unlink ? '-' : ' ',
	    m->tagged ? '*' : ' ',
	    num + 1,
	    t,
	    idx[num]->tree ? idx[num]->tree : "",
	    tmp,
	    m->description ? m->description : "no description");
}






static void draw_envelope (HEADER *msg, char *fcc)
{
  char buf[STRING];
  int w = COLS - HDR_XOFFSET;

  mvaddstr (HDR_FROM, 0,    "    From: ");
  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), msg->env->from);
  printw ("%-*.*s", w, w, buf);

  mvaddstr (HDR_TO, 0,      "      To: ");
  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), msg->env->to);
  printw ("%-*.*s", w, w, buf);

  mvaddstr (HDR_CC, 0,      "      Cc: ");
  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), msg->env->cc);
  printw ("%-*.*s", w, w, buf);

  mvaddstr (HDR_BCC, 0,     "     Bcc: ");
  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), msg->env->bcc);
  printw ("%-*.*s", w, w, buf);

  mvaddstr (HDR_SUBJECT, 0, " Subject: ");
  if (msg->env->subject)
    printw ("%-*.*s", w, w, msg->env->subject);
  else
    clrtoeol ();

  mvaddstr (HDR_REPLYTO, 0, "Reply-To: ");
  if (msg->env->reply_to)
  {
    buf[0] = 0;
    rfc822_write_address (buf, sizeof (buf), msg->env->reply_to);
    printw ("%-*.*s", w, w, buf);
  }
  else
    clrtoeol ();

  mvaddstr (HDR_FCC, 0,     "     Fcc: ");
  addstr (fcc);














  mvaddstr (HDR_ATTACH - 1, 0, "===== Attachments =====");
}

static int edit_address_list (int line, ENVELOPE *env)
{
  char buf[HUGE_STRING] = ""; /* needs to be large for alias expansion */
  ADDRESS **addr;
  char *prompt;

  switch (line)
  {
    case HDR_FROM:
      prompt = "From: ";
      addr = &env->from;
      break;
    case HDR_TO:
      prompt = "To: ";
      addr = &env->to;
      break;
    case HDR_CC:
      prompt = "Cc: ";
      addr = &env->cc;
      break;
    case HDR_BCC:
      prompt = "Bcc: ";
      addr = &env->bcc;
      break;
    case HDR_REPLYTO:
      prompt = "Reply-To: ";
      addr = &env->reply_to;
      break;
    default:
      return 0;
  }

  rfc822_write_address (buf, sizeof (buf), *addr);
  if (mutt_get_field (prompt, buf, sizeof (buf), M_ALIAS) != 0)
    return 0;

  rfc822_free_address (addr);
  *addr = mutt_parse_adrlist (*addr, buf);
  *addr = mutt_expand_aliases (*addr);

  if (option (OPTNEEDREDRAW))
  {
    unset_option (OPTNEEDREDRAW);
    return (REDRAW_FULL);
  }

  /* redraw the expanded list so the user can see the result */
  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), *addr);
  move (line, HDR_XOFFSET);
  printw ("%-*.*s", COLS - HDR_XOFFSET, COLS - HDR_XOFFSET, buf);

  return 0;
}

static int delete_attachment (MUTTMENU *menu, short *idxlen, int x)
{
  ATTACHPTR **idx = (ATTACHPTR **) menu->data;
  int y;

  menu->redraw = REDRAW_INDEX | REDRAW_STATUS;

  if (x == 0 && menu->max == 1)
  {
    mutt_error ("You may not delete the only attachment.");
    idx[x]->content->tagged = 0;
    return (-1);
  }

  for (y = 0; y < *idxlen; y++)
  {
    if (idx[y]->content->next == idx[x]->content)
    {
      idx[y]->content->next = idx[x]->content->next;
      break;
    }
  }

  idx[x]->content->next = NULL;
  idx[x]->content->parts = NULL;
  mutt_free_body (&(idx[x]->content));
  safe_free ((void **) &idx[x]->tree);
  safe_free ((void **) &idx[x]);
  for (; x < *idxlen - 1; x++)
    idx[x] = idx[x+1];
  menu->max = --(*idxlen);
  
  return (0);
}

/* return values:
 *
 * 1	message should be postponed
 * 0	normal exit
 * -1	abort message
 */
int mutt_send_menu (HEADER *msg,   /* structure for new message */
		    char *fcc,     /* where to save a copy of the message */
		    size_t fcclen,
		    HEADER *cur)   /* current message */
{
  char helpstr[SHORT_STRING];
  char buf[LONG_STRING];
  char fname[_POSIX_PATH_MAX];
  MUTTMENU *menu;
  ATTACHPTR **idx = NULL;
  short idxlen = 0;
  short idxmax = 0;
  int i;
  int r = -1;		/* return value */
  int op = 0;
  int loop = 1;
  int fccSet = 0;	/* has the user edited the Fcc: field ? */

  idx = mutt_gen_attach_list (msg->content, idx, &idxlen, &idxmax, 0, 1);

  menu = mutt_new_menu ();
  menu->menu = MENU_COMPOSE;
  menu->offset = HDR_ATTACH;
  menu->max = idxlen;
  menu->make_entry = snd_entry;
  menu->tag = mutt_tag_attach;
  menu->title = "Compose";
  menu->data = idx;
  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_COMPOSE, ComposeHelp);
  
  while (loop)
  {
    switch (op = mutt_menuLoop (menu))
    {
      case OP_REDRAW:
	draw_envelope (msg, fcc);
	menu->offset = HDR_ATTACH;
	menu->pagelen = LINES - HDR_ATTACH - 2;
	break;
      case OP_COMPOSE_EDIT_FROM:
	menu->redraw = edit_address_list (HDR_FROM, msg->env);
	break;
      case OP_COMPOSE_EDIT_TO:
	menu->redraw = edit_address_list (HDR_TO, msg->env);
	break;
      case OP_COMPOSE_EDIT_BCC:
	menu->redraw = edit_address_list (HDR_BCC, msg->env);
	break;
      case OP_COMPOSE_EDIT_CC:
	menu->redraw = edit_address_list (HDR_CC, msg->env);
	break;
      case OP_COMPOSE_EDIT_SUBJECT:
	if (msg->env->subject)
	  strfcpy (buf, msg->env->subject, sizeof (buf));
	else
	  buf[0] = 0;
	if (mutt_get_field ("Subject: ", buf, sizeof (buf), 0) == 0)
	{
	  safe_free ((void **) &msg->env->subject);
	  msg->env->subject = safe_strdup (buf);
	  move (HDR_SUBJECT, HDR_XOFFSET);
	  clrtoeol ();
	  if (msg->env->subject)
	    printw ("%-*.*s", COLS-HDR_XOFFSET, COLS-HDR_XOFFSET,
		    msg->env->subject);
	}
	break;
      case OP_COMPOSE_EDIT_REPLY_TO:
	menu->redraw = edit_address_list (HDR_REPLYTO, msg->env);
	break;
      case OP_COMPOSE_EDIT_FCC:
	strfcpy (buf, fcc, sizeof (buf));
	if (mutt_get_field ("Fcc: ", buf, sizeof (buf), M_FILE | M_CLEAR) == 0)
	{
	  strfcpy (fcc, buf, _POSIX_PATH_MAX);
	  mutt_pretty_mailbox (fcc);
	  mvprintw (HDR_FCC, HDR_XOFFSET, "%-*.*s",
		    COLS - HDR_XOFFSET, COLS - HDR_XOFFSET, fcc);
	  fccSet = 1;
	}
	MAYBE_REDRAW (menu->redraw);
	break;
      case OP_COMPOSE_EDIT_MESSAGE:
	if (strcmp ("builtin", Editor) != 0 && !option (OPTEDITHDRS))
	{
	  mutt_edit_file (Editor, msg->content->filename);
	  mutt_update_encoding (msg->content);
	  menu->redraw = REDRAW_CURRENT;
	  break;
	}
	/* fall through */
      case OP_COMPOSE_EDIT_HEADERS:
	if (op == OP_COMPOSE_EDIT_HEADERS ||
	    (op == OP_COMPOSE_EDIT_MESSAGE && option (OPTEDITHDRS)))
	{
	  mutt_edit_headers (strcmp ("builtin", Editor) == 0 ? Visual : Editor,
			     msg->content->filename, msg, fcc, fcclen);
	}
	else
	{
	  /* this is grouped with OP_COMPOSE_EDIT_HEADERS because the
	     attachment list could change if the user invokes ~v to edit
	     the message with headers, in which we need to execute the
	     code below to regenerate the index array */
	  mutt_builtin_editor (msg->content->filename, msg, cur);
	}
	mutt_update_encoding (msg->content);

	/* attachments may have been added */
	if (idxlen && idx[idxlen - 1]->content->next)
	{
	  for (i = 0; i < idxlen; i++)
	    safe_free ((void **) &idx[i]);
	  idxlen = 0;
	  idx = mutt_gen_attach_list (msg->content, idx, &idxlen, &idxmax, 0, 1);
	  menu->max = idxlen;
	}

	menu->redraw = REDRAW_FULL;
	break;
















      case OP_COMPOSE_ATTACH_FILE:
	fname[0] = 0;
	if (mutt_enter_fname ("Attach file", fname, sizeof (fname),
			      &menu->redraw, 0) == -1)
	  break;
	if (!fname[0])
	  continue;
	mutt_expand_path (fname, sizeof (fname));

	/* check to make sure the file exists and is readable */
	if (access (fname, R_OK) == -1)
	{
	  mutt_perror (fname);
	  break;
	}

	if (idxlen == idxmax)
	{
	  safe_realloc ((void **) &idx, sizeof (ATTACHPTR *) * (idxmax += 5));
	  menu->data = idx;
	}

	idx[idxlen] = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
	if ((idx[idxlen]->content = mutt_make_attach (fname)) != NULL)
	{
	  idx[idxlen]->level = (idxlen > 0) ? idx[idxlen-1]->level : 0;

	  if (idxlen)
	    idx[idxlen - 1]->content->next = idx[idxlen]->content;

	  menu->current = idxlen++;
	  mutt_update_tree (idx, idxlen);
	  menu->max = idxlen;
	  menu->redraw |= REDRAW_INDEX | REDRAW_STATUS;
	}
	else
	{
	  mutt_error ("Unable to attach file!");
	  safe_free ((void **) &idx[idxlen]);
	}
	break;

      case OP_DELETE:
	CHECK_COUNT;
	if (delete_attachment (menu, &idxlen, menu->current) == -1)
	  break;
	mutt_update_tree (idx, idxlen);
	if (idxlen)
	{
	  if (menu->current > idxlen - 1)
	    menu->current = idxlen - 1;
	}
	else
	  menu->current = 0;

	if (menu->current == 0)
	  msg->content = idx[0]->content;
	break;

      case OP_COMPOSE_EDIT_DESCRIPTION:
	CHECK_COUNT;
	strfcpy (buf,
		 idx[menu->current]->content->description ?
		 idx[menu->current]->content->description : "",
		 sizeof (buf));
	if (mutt_get_field ("Description: ", buf, sizeof (buf), 0) == 0)
	{
	  safe_free ((void **) &idx[menu->current]->content->description);
	  idx[menu->current]->content->description = safe_strdup (buf);
	  menu->redraw = REDRAW_CURRENT;
	}
	break;

      case OP_COMPOSE_EDIT_TYPE:
	CHECK_COUNT;
	snprintf (buf, sizeof (buf), "%s/%s",
		  TYPE (idx[menu->current]->content->type),
		  idx[menu->current]->content->subtype);
	if (mutt_get_field ("Content-Type: ", buf, sizeof (buf), 0) == 0 && buf[0])
	{
	  char *p = strchr (buf, '/');

	  if (p)
	  {
	    *p++ = 0;
	    if ((i = mutt_check_mime_type (buf)) != TYPEOTHER)
	    {
	      idx[menu->current]->content->type = i;
	      safe_free ((void **) &idx[menu->current]->content->subtype);
	      idx[menu->current]->content->subtype = safe_strdup (p);
	      menu->redraw = REDRAW_CURRENT;
	    }
	  }
	}
	break;

      case OP_COMPOSE_EDIT_ENCODING:
	CHECK_COUNT;
	strfcpy (buf, ENCODING (idx[menu->current]->content->encoding),
							      sizeof (buf));
	if (mutt_get_field ("Content-Transfer-Encoding: ", buf,
					    sizeof (buf), 0) == 0 && buf[0])
	{
	  if ((i = mutt_check_encoding (buf)) != ENCOTHER)
	  {
	    idx[menu->current]->content->encoding = i;
	    menu->redraw = REDRAW_CURRENT;
	  }
	  else
	    mutt_error ("Invalid encoding.");
	}
	break;

      case OP_COMPOSE_SEND_MESSAGE:
	if (!fccSet && *fcc)
	{
	  if ((i = query_quadoption (OPT_COPY, "Save a copy of this message?"))
									  == -1)
	    break;
	  else if (i == M_NO)
	    *fcc = 0;
	}

	loop = 0;
	r = 0;
	break;

      case OP_COMPOSE_EDIT_FILE:
	CHECK_COUNT;
	mutt_edit_file (strcmp ("builtin", Editor) == 0 ? Visual : Editor,
			idx[menu->current]->content->filename);
	mutt_update_encoding (idx[menu->current]->content);
	menu->redraw = REDRAW_CURRENT;
	break;

      case OP_COMPOSE_TOGGLE_UNLINK:
	CHECK_COUNT;
	idx[menu->current]->content->unlink = !idx[menu->current]->content->unlink;
	menu->redraw = REDRAW_INDEX;
	break;

      case OP_COMPOSE_RENAME_FILE:
	CHECK_COUNT;
	strfcpy (fname, idx[menu->current]->content->filename, sizeof (fname));
	mutt_pretty_mailbox (fname);
	if (mutt_get_field ("Rename to: ", fname, sizeof (fname), M_FILE) == 0
								  && fname[0])
	{
	  mutt_expand_path (fname, sizeof (fname));
	  if (!mutt_rename_file (idx[menu->current]->content->filename, fname))
	  {
	    safe_free ((void **) &idx[menu->current]->content->filename);
	    idx[menu->current]->content->filename = safe_strdup (fname);
	    menu->redraw = REDRAW_CURRENT;
	  }
	}
	break;

      case OP_COMPOSE_NEW_MIME:
	{
	  char type[STRING];
	  char *p;
	  int itype;
	  FILE *fp;

	  CLEARLINE (LINES-1);
	  fname[0] = 0;
	  if (mutt_get_field ("New file: ", fname, sizeof (fname), M_FILE) != 0
	      || !fname[0])
	    continue;
	  mutt_expand_path (fname, sizeof (fname));

	  /* Call to lookup_mime_type () ?  maybe later */
	  type[0] = 0;
	  if (mutt_get_field ("Content-Type: ", type, sizeof (type), 0) != 0 
	      || !type[0])
	    continue;

	  if (!(p = strchr (type, '/')))
	  {
	    mutt_error ("Content-Type is of the form base/sub");
	    continue;
	  }
	  *p++ = 0;
	  if ((itype = mutt_check_mime_type (type)) == TYPEOTHER)
	  {
	    mutt_error ("Unknown Content-Type %s", type);
	    continue;
	  }
	  if (idxlen == idxmax)
	  {
	    safe_realloc ((void **) &idx, sizeof (ATTACHPTR *) * (idxmax += 5));
	    menu->data = idx;
	  }

	  idx[idxlen] = (ATTACHPTR *) safe_calloc (1, sizeof (ATTACHPTR));
	  /* Touch the file */
	  if (!(fp = safe_fopen (fname, "w")))
	  {
	    mutt_error ("Can't create file %s", fname);
	    safe_free ((void **) &idx[idxlen]);
	    continue;
	  }
	  fclose (fp);

	  if ((idx[idxlen]->content = mutt_make_attach (fname)) == NULL)
	  {
	    mutt_error ("What we have here is a failure to make an attachment");
	    continue;
	  }
	  
	  idx[idxlen]->level = (idxlen > 0) ? idx[idxlen-1]->level : 0;
	  if (idxlen)
	    idx[idxlen - 1]->content->next = idx[idxlen]->content;
	  
	  menu->current = idxlen++;
	  mutt_update_tree (idx, idxlen);
	  menu->max = idxlen;

	  idx[menu->current]->content->type = itype;
	  safe_free ((void **) &idx[menu->current]->content->subtype);
	  idx[menu->current]->content->subtype = safe_strdup (p);
	  idx[menu->current]->content->unlink = 1;
	  menu->redraw |= REDRAW_INDEX | REDRAW_STATUS;

	  if (mutt_compose_attachment (idx[menu->current]->content))
	  {
	    mutt_update_encoding (idx[menu->current]->content);
	    menu->redraw = REDRAW_FULL;
	  }
	}
	break;






      case OP_COMPOSE_EDIT_MIME:
	CHECK_COUNT;
	if (mutt_edit_attachment (idx[menu->current]->content, 0))
	{
	  mutt_update_encoding (idx[menu->current]->content);
	  menu->redraw = REDRAW_FULL;
	}
	break;

      case OP_VIEW_ATTACH:
      case OP_DISPLAY_HEADERS:
	CHECK_COUNT;
	mutt_attach_display_loop (menu, op, NULL, idx);
	menu->redraw = REDRAW_FULL;
	mutt_clear_error ();
	break;

      case OP_SAVE:
	CHECK_COUNT;
	mutt_save_attachment_list (NULL, menu->tagprefix, menu->tagprefix ? msg->content : idx[menu->current]->content);
	break;

      case OP_PRINT:
	CHECK_COUNT;
	mutt_print_attachment_list (NULL, menu->tagprefix, menu->tagprefix ? msg->content : idx[menu->current]->content);
	break;

      case OP_PIPE:
      case OP_FILTER:
        CHECK_COUNT;
	mutt_pipe_attachment_list (NULL, menu->tagprefix, menu->tagprefix ? msg->content : idx[menu->current]->content, op == OP_FILTER);
	if (op == OP_FILTER)
	  menu->redraw = REDRAW_CURRENT; /* cte might have changed */
	break;






      case OP_EXIT:
	if ((i = query_quadoption (OPT_POSTPONE, "Postpone this message?")) == M_NO)
	{
	  while (idxlen-- > 0)
	  {
	    /* avoid freeing other attachments */
	    idx[idxlen]->content->next = NULL;
	    idx[idxlen]->content->parts = NULL;
	    mutt_free_body (&idx[idxlen]->content);
	    safe_free ((void **) &idx[idxlen]->tree);
	    safe_free ((void **) &idx[idxlen]);
	  }
	  safe_free ((void **) &idx);
	  idxlen = 0;
	  idxmax = 0;
	  r = -1;
	  loop = 0;
	  break;
	}
	else if (i == -1)
	  break; /* abort */

	/* fall through to postpone! */

      case OP_COMPOSE_POSTPONE_MESSAGE:
	loop = 0;
	r = 1;
	break;

      case OP_COMPOSE_ISPELL:
	endwin ();
	snprintf (buf, sizeof (buf), "%s -x %s", Ispell, msg->content->filename);
	mutt_system (buf);
	break;






    }
  }

  mutt_menuDestroy (&menu);

  if (idxlen)
  {
    msg->content = idx[0]->content;
    for (i = 0; i < idxlen; i++)
      safe_free ((void **) &idx[i]);
  }
  else
    msg->content = NULL;

  safe_free ((void **) &idx);

  return (r);
}
