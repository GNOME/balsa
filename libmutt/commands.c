/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
 * Copyright (C) 2000 Thomas Roessler <roessler@does-not-exist.org>
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
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#include "mutt.h"
#include "mutt_curses.h"
#include "mutt_menu.h"
#include "mime.h"
#include "sort.h"
#include "mailbox.h"
#include "copy.h"
#include "mx.h"
#include "pager.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef USE_IMAP
#include "imap.h"
#endif

#ifdef BUFFY_SIZE
#include "buffy.h"
#endif



#ifdef HAVE_PGP
#include "pgp.h"
#endif



#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#ifndef LIBMUTT

extern char *ReleaseDate;

/* The folder the user last saved to.  Used by ci_save_message() */
static char LastSaveFolder[_POSIX_PATH_MAX] = "";

int mutt_display_message (HEADER *cur)
{
  char tempfile[_POSIX_PATH_MAX], buf[LONG_STRING];
  int rc = 0, builtin = 0;
  int cmflags = M_CM_DECODE | M_CM_DISPLAY | M_CM_CHARCONV;
  FILE *fpout = NULL;
  FILE *fpfilterout = NULL;
  pid_t filterpid = -1;
  int res;

  snprintf (buf, sizeof (buf), "%s/%s", TYPE (cur->content),
	    cur->content->subtype);

  mutt_parse_mime_message (Context, cur);
  mutt_message_hook (Context, cur, M_MESSAGEHOOK);

#ifdef HAVE_PGP
  /* see if PGP is needed for this message.  if so, we should exit curses */
  if (cur->pgp)
  {
    if (cur->pgp & PGPENCRYPT)
    {
      if (!pgp_valid_passphrase ())
	return 0;

      cmflags |= M_CM_VERIFY;
    }
    else if (cur->pgp & PGPSIGN)
    {
      /* find out whether or not the verify signature */
      if (query_quadoption (OPT_VERIFYSIG, _("Verify PGP signature?")) == M_YES)
      {
	cmflags |= M_CM_VERIFY;
      }
    }
  }
  
  if ((cmflags & M_CM_VERIFY) || (cur->pgp & PGPENCRYPT))
  {
    if (cur->env->from)
      pgp_invoke_getkeys (cur->env->from);

    mutt_message _("Invoking PGP...");
  }

#endif

  mutt_mktemp (tempfile);
  if ((fpout = safe_fopen (tempfile, "w")) == NULL)
  {
    mutt_error _("Could not create temporary file!");
    return (0);
  }

  if (DisplayFilter && *DisplayFilter) 
  {
    fpfilterout = fpout;
    fpout = NULL;
    /* mutt_endwin (NULL); */
    filterpid = mutt_create_filter_fd (DisplayFilter, &fpout, NULL, NULL,
				       -1, fileno(fpfilterout), -1);
    if (filterpid < 0)
    {
      mutt_error (_("Cannot create display filter"));
      safe_fclose (&fpfilterout);
      unlink (tempfile);
      return 0;
    }
  }

  if (!Pager || mutt_strcmp (Pager, "builtin") == 0)
    builtin = 1;
  else
  {
    mutt_make_string (buf, sizeof (buf), NONULL(PagerFmt), Context, cur);
    fputs (buf, fpout);
    fputs ("\n\n", fpout);
  }

  res = mutt_copy_message (fpout, Context, cur, cmflags,
       	(option (OPTWEED) ? (CH_WEED | CH_REORDER) : 0) | CH_DECODE | CH_FROM);
  if ((fclose (fpout) != 0 && errno != EPIPE) || res == -1)
  {
    mutt_error (_("Could not copy message"));
    if (fpfilterout != NULL)
      mutt_wait_filter (filterpid);
    mutt_unlink (tempfile);
    return 0;
  }

  if (fpfilterout != NULL && mutt_wait_filter (filterpid) != 0)
    mutt_any_key_to_continue (NULL);

#ifdef HAVE_PGP
  /* update PGP information for this message */
  cur->pgp |= pgp_query (cur->content);
#endif

  if (builtin)
  {
    pager_t info;

#ifdef HAVE_PGP
    if (cmflags & M_CM_VERIFY)
      mutt_message ((cur->pgp & PGPGOODSIGN) ?
		    _("PGP signature successfully verified.") :
		    _("PGP signature could NOT be verified."));
#endif

    /* Invoke the builtin pager */
    memset (&info, 0, sizeof (pager_t));
    info.hdr = cur;
    info.ctx = Context;
    rc = mutt_pager (NULL, tempfile, M_PAGER_MESSAGE, &info);
  }
  else
  {
    int r;

    mutt_endwin (NULL);
    snprintf (buf, sizeof (buf), "%s %s", NONULL(Pager), tempfile);
    if ((r = mutt_system (buf)) == -1)
      mutt_error (_("Error running \"%s\"!"), buf);
    unlink (tempfile);
    keypad (stdscr, TRUE);
    if (r != -1)
      mutt_set_flag (Context, cur, M_READ, 1);
    if (r != -1 && option (OPTPROMPTAFTER))
    {
      mutt_ungetch (mutt_any_key_to_continue _("Command: "), 0);
      rc = km_dokey (MENU_PAGER);
    }
    else
      rc = 0;
  }

  return rc;
}

void ci_bounce_message (HEADER *h, int *redraw)
{
  char prompt[SHORT_STRING];
  char buf[HUGE_STRING] = { 0 };
  ADDRESS *adr = NULL;
  int rc;

  if(h)
    strfcpy(prompt, _("Bounce message to: "), sizeof(prompt));
  else
    strfcpy(prompt, _("Bounce tagged messages to: "), sizeof(prompt));
  
  rc = mutt_get_field (prompt, buf, sizeof (buf), M_ALIAS);

  if (option (OPTNEEDREDRAW))
  {
    unset_option (OPTNEEDREDRAW);
    *redraw = REDRAW_FULL;
  }

  if (rc || !buf[0])
    return;

  if (!(adr = rfc822_parse_adrlist (adr, buf)))
  {
    mutt_error _("Error parsing address!");
    return;
  }

  adr = mutt_expand_aliases (adr);

  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), adr);

#define extra_space (15 + 7 + 2)
  /*
   * This is the printing width of "...? ([y=yes]/n=no): ?" plus 2
   * for good measure. This is not ideal. FIXME.
   */
  snprintf (prompt, sizeof (prompt) - 4,
           (h ? _("Bounce message to %s") : _("Bounce messages to %s")), buf);
  mutt_format_string (prompt, sizeof (prompt) - 4,
		      0, COLS-extra_space, 0, 0,
		      prompt, sizeof (prompt), 0);
  strcat (prompt, "...?");	/* __STRCAT_CHECKED__ */
  if (mutt_yesorno (prompt, 1) != 1)
  {
    rfc822_free_address (&adr);
    CLEARLINE (LINES-1);
    return;
  }

  mutt_bounce_message (NULL, h, adr);
  rfc822_free_address (&adr);
  mutt_message (h ? _("Message bounced.") : _("Messages bounced."));
}

static void pipe_set_flags (int decode, int *cmflags, int *chflags)
{
  if (decode)
  {
    *cmflags |= M_CM_DECODE | M_CM_CHARCONV;
    *chflags |= CH_DECODE | CH_REORDER;
    
    if (option (OPTWEED))
    {
      *chflags |= CH_WEED;
      *cmflags |= M_CM_WEED;
    }
  }
}

void pipe_msg (HEADER *h, FILE *fp, int decode)
{
  int cmflags = 0;
  int chflags = CH_FROM;
  
  pipe_set_flags (decode, &cmflags, &chflags);

#ifdef HAVE_PGP
  
  if (decode && (h->pgp & PGPENCRYPT))
  {
    if (!pgp_valid_passphrase())
      return;
    endwin();
  }
  
#endif

  if (decode)
    mutt_parse_mime_message (Context, h);

  mutt_copy_message (fp, Context, h, cmflags, chflags);
}


/* the following code is shared between printing and piping */

static int _mutt_pipe_message (HEADER *h, char *cmd,
			int decode,
			int split,
			char *sep)
{
  
  int i, rc = 0;
  pid_t thepid;
  FILE *fpout;
  
  mutt_endwin (NULL);
  if (h)
  {

    mutt_message_hook (Context, h, M_MESSAGEHOOK);

#ifdef HAVE_PGP
    if (decode)
    {
      mutt_parse_mime_message (Context, h);
      if(h->pgp & PGPENCRYPT && !pgp_valid_passphrase())
	return 1;
    }
    mutt_endwin (NULL);
#endif

    if ((thepid = mutt_create_filter (cmd, &fpout, NULL, NULL)) < 0)
    {
      mutt_perror _("Can't create filter process");
      return 1;
    }
      
    pipe_msg (h, fpout, decode);
    safe_fclose (&fpout);
    rc = mutt_wait_filter (thepid);
  }
  else
  { /* handle tagged messages */



#ifdef HAVE_PGP

    if (decode)
    {
      for (i = 0; i < Context->vcount; i++)
	if(Context->hdrs[Context->v2r[i]]->tagged)
	{
	  mutt_message_hook (Context, Context->hdrs[Context->v2r[i]], M_MESSAGEHOOK);
	  mutt_parse_mime_message(Context, Context->hdrs[Context->v2r[i]]);
	  if (Context->hdrs[Context->v2r[i]]->pgp & PGPENCRYPT &&
	      !pgp_valid_passphrase())
	    return 1;
	}
    }
#endif
    
    if (split)
    {
      for (i = 0; i < Context->vcount; i++)
      {
        if (Context->hdrs[Context->v2r[i]]->tagged)
        {
	  mutt_message_hook (Context, Context->hdrs[Context->v2r[i]], M_MESSAGEHOOK);
	  mutt_endwin (NULL);
	  if ((thepid = mutt_create_filter (cmd, &fpout, NULL, NULL)) < 0)
	  {
	    mutt_perror _("Can't create filter process");
	    return 1;
	  }
          pipe_msg (Context->hdrs[Context->v2r[i]], fpout, decode);
          /* add the message separator */
          if (sep)  fputs (sep, fpout);
	  safe_fclose (&fpout);
	  if (mutt_wait_filter (thepid) != 0)
	    rc = 1;
        }
      }
    }
    else
    {
      mutt_endwin (NULL);
      if ((thepid = mutt_create_filter (cmd, &fpout, NULL, NULL)) < 0)
      {
	mutt_perror _("Can't create filter process");
	return 1;
      }
      for (i = 0; i < Context->vcount; i++)
      {
        if (Context->hdrs[Context->v2r[i]]->tagged)
        {
	  mutt_message_hook (Context, Context->hdrs[Context->v2r[i]], M_MESSAGEHOOK);
          pipe_msg (Context->hdrs[Context->v2r[i]], fpout, decode);
          /* add the message separator */
          if (sep) fputs (sep, fpout);
        }
      }
      safe_fclose (&fpout);
      if (mutt_wait_filter (thepid) != 0)
	rc = 1;
    }
  }

  if (rc || option (OPTWAITKEY))
    mutt_any_key_to_continue (NULL);
  return rc;
}

void mutt_pipe_message (HEADER *h)
{
  char buffer[LONG_STRING];

  buffer[0] = 0;
  if (mutt_get_field (_("Pipe to command: "), buffer, sizeof (buffer), M_CMD)
      != 0 || !buffer[0])
    return;

  mutt_expand_path (buffer, sizeof (buffer));
  _mutt_pipe_message (h, buffer,
		      option (OPTPIPEDECODE),
		      option (OPTPIPESPLIT),
		      PipeSep);
}

void mutt_print_message (HEADER *h)
{

  if (quadoption (OPT_PRINT) && (!PrintCmd || !*PrintCmd))
  {
    mutt_message (_("No printing command has been defined."));
    return;
  }
  
  if (query_quadoption (OPT_PRINT,
			h ? _("Print message?") : _("Print tagged messages?"))
		  	!= M_YES)
    return;

  if (_mutt_pipe_message (h, PrintCmd,
			  option (OPTPRINTDECODE),
			  option (OPTPRINTSPLIT),
			  "\f") == 0)
    mutt_message (h ? _("Message printed") : _("Messages printed"));
  else
    mutt_message (h ? _("Message could not be printed") :
		  _("Messages could not be printed"));
}


int mutt_select_sort (int reverse)
{
  int method = Sort; /* save the current method in case of abort */

  switch (mutt_multi_choice (reverse ?
			     _("Rev-Sort (d)ate/(f)rm/(r)ecv/(s)ubj/t(o)/(t)hread/(u)nsort/si(z)e/s(c)ore?: ") :
			     _("Sort (d)ate/(f)rm/(r)ecv/(s)ubj/t(o)/(t)hread/(u)nsort/si(z)e/s(c)ore?: "),
			     _("dfrsotuzc")))
  {
  case -1: /* abort - don't resort */
    return -1;

  case 1: /* (d)ate */
    Sort = SORT_DATE;
    break;

  case 2: /* (f)rm */
    Sort = SORT_FROM;
    break;
  
  case 3: /* (r)ecv */
    Sort = SORT_RECEIVED;
    break;
  
  case 4: /* (s)ubj */
    Sort = SORT_SUBJECT;
    break;
  
  case 5: /* t(o) */
    Sort = SORT_TO;
    break;
  
  case 6: /* (t)hread */
    Sort = SORT_THREADS;
    break;
  
  case 7: /* (u)nsort */
    Sort = SORT_ORDER;
    break;
  
  case 8: /* si(z)e */
    Sort = SORT_SIZE;
    break;
  
  case 9: /* s(c)ore */ 
    Sort = SORT_SCORE;
    break;
  }
  if (reverse)
    Sort |= SORT_REVERSE;

  return (Sort != method ? 0 : -1); /* no need to resort if it's the same */
}

/* invoke a command in a subshell */
void mutt_shell_escape (void)
{
  char buf[LONG_STRING];

  buf[0] = 0;
  if (mutt_get_field (_("Shell command: "), buf, sizeof (buf), M_CMD) == 0)
  {
    if (!buf[0] && Shell)
      strfcpy (buf, Shell, sizeof (buf));
    if(buf[0])
    {
      CLEARLINE (LINES-1);
      mutt_endwin (NULL);
      fflush (stdout);
      if (mutt_system (buf) != 0 || option (OPTWAITKEY))
	mutt_any_key_to_continue (NULL);
    }
  }
}

/* enter a mutt command */
void mutt_enter_command (void)
{
  BUFFER err, token;
  char buffer[LONG_STRING], errbuf[SHORT_STRING];
  int r;
  int old_strictthreads = option (OPTSTRICTTHREADS);
  int old_sortre	= option (OPTSORTRE);

  buffer[0] = 0;
  if (mutt_get_field (":", buffer, sizeof (buffer), M_COMMAND) != 0 || !buffer[0])
    return;
  err.data = errbuf;
  err.dsize = sizeof (errbuf);
  memset (&token, 0, sizeof (token));
  r = mutt_parse_rc_line (buffer, &token, &err);
  FREE (&token.data);
  if (errbuf[0])
  {
    /* since errbuf could potentially contain printf() sequences in it,
       we must call mutt_error() in this fashion so that vsprintf()
       doesn't expect more arguments that we passed */
    if (r == 0)
      mutt_message ("%s", errbuf);
    else
      mutt_error ("%s", errbuf);
  }
  if (option (OPTSTRICTTHREADS) != old_strictthreads ||
      option (OPTSORTRE) != old_sortre)
    set_option (OPTNEEDRESORT);
}

void mutt_display_address (ENVELOPE *env)
{
  char *pfx = NULL;
  char buf[SHORT_STRING];
  ADDRESS *adr = NULL;

  adr = mutt_get_address (env, &pfx);

  if (!adr) return;

  buf[0] = 0;
  rfc822_write_address (buf, sizeof (buf), adr);
  mutt_message ("%s: %s", pfx, buf);
}

#endif

static void set_copy_flags (HEADER *hdr, int decode, int decrypt, int *cmflags, int *chflags)
{
  *cmflags = 0;
  *chflags = CH_UPDATE_LEN;
  
#ifdef HAVE_PGP
  if (!decode && decrypt && (hdr->pgp & PGPENCRYPT))
  {
    if (mutt_is_multipart_encrypted(hdr->content))
    {
      *chflags = CH_NONEWLINE | CH_XMIT | CH_MIME;
      *cmflags = M_CM_DECODE_PGP;
    }
    else if (mutt_is_application_pgp(hdr->content) & PGPENCRYPT)
      decode = 1;
  }
#endif

  if (decode)
  {
    *chflags = CH_XMIT | CH_MIME | CH_TXTPLAIN;
    *cmflags = M_CM_DECODE | M_CM_CHARCONV;
  }

  /* respect $weed only if decode doesn't kick in
   * for decrypt.
   */

  if (decode && !decrypt && option (OPTWEED))
  {
    *chflags |= CH_WEED;
    *cmflags |= M_CM_WEED;
  }
}


void _mutt_save_message (HEADER *h, CONTEXT *ctx, int delete, int decode, int decrypt)
{
  int cmflags, chflags;
  
  set_copy_flags (h, decode, decrypt, &cmflags, &chflags);

  if (decode || decrypt)
    mutt_parse_mime_message (Context, h);

  if (mutt_append_message (ctx, Context, h, cmflags, chflags) == 0 && delete)
  {
    mutt_set_flag (Context, h, M_DELETE, 1);
    if (option (OPTDELETEUNTAG))
      mutt_set_flag (Context, h, M_TAG, 0);
  }
}

#ifndef LIBMUTT

/* returns 0 if the copy/save was successful, or -1 on error/abort */
int mutt_save_message (HEADER *h, int delete, 
		       int decode, int decrypt, int *redraw)
{
  int i, need_buffy_cleanup;
#ifdef HAVE_PGP
  int need_passphrase = 0;
#endif
  char prompt[SHORT_STRING], buf[_POSIX_PATH_MAX];
  CONTEXT ctx;
  struct stat st;
#ifdef BUFFY_SIZE
  BUFFY *tmp = NULL;
#else
  struct utimbuf ut;
#endif

  *redraw = 0;

  
  snprintf (prompt, sizeof (prompt), _("%s%s to mailbox"),
	    decode ? (delete ? _("Decode-save") : _("Decode-copy")) :
	    (decrypt ? (delete ? _("Decrypt-save") : _("Decrypt-copy")):
	     (delete ? _("Save") : _("Copy"))), h ? "" : _(" tagged"));
  
  if (h)
  {
#ifdef HAVE_PGP
    need_passphrase = h->pgp & PGPENCRYPT;
#endif
    mutt_message_hook (Context, h, M_MESSAGEHOOK);
    mutt_default_save (buf, sizeof (buf), h);
  }
  else
  {
    /* look for the first tagged message */

    for (i = 0; i < Context->vcount; i++)
    {
      if (Context->hdrs[Context->v2r[i]]->tagged)
      {
	h = Context->hdrs[Context->v2r[i]];
	break;
      }
    }

    if (h)
    {
      mutt_message_hook (Context, h, M_MESSAGEHOOK);
      mutt_default_save (buf, sizeof (buf), h);
#ifdef HAVE_PGP
      need_passphrase |= h->pgp & PGPENCRYPT;
#endif
      h = NULL;
    }
  }

  mutt_pretty_mailbox (buf);
  if (mutt_enter_fname (prompt, buf, sizeof (buf), redraw, 0) == -1)
    return (-1);

  if (*redraw != REDRAW_FULL)
  {
    if (!h)
      *redraw = REDRAW_INDEX | REDRAW_STATUS;
    else
      *redraw = REDRAW_STATUS;
  }

  if (!buf[0])
    return (-1);
 
  /* This is an undocumented feature of ELM pointed out to me by Felix von
   * Leitner <leitner@prz.fu-berlin.de>
   */
  if (mutt_strcmp (buf, ".") == 0)
    strfcpy (buf, LastSaveFolder, sizeof (buf));
  else
    strfcpy (LastSaveFolder, buf, sizeof (LastSaveFolder));

  mutt_expand_path (buf, sizeof (buf));

  /* check to make sure that this file is really the one the user wants */
  if (!mutt_save_confirm (buf, &st))
    return -1;

#ifdef HAVE_PGP
  if(need_passphrase && (decode || decrypt) && !pgp_valid_passphrase())
    return -1;
#endif
  
  mutt_message (_("Copying to %s..."), buf);
  
#ifdef USE_IMAP
  if (Context->magic == M_IMAP && 
      !(decode || decrypt) && mx_is_imap (buf))
  {
    switch (imap_copy_messages (Context, h, buf, delete))
    {
      /* success */
      case 0: mutt_clear_error (); return 0;
      /* non-fatal error: fall through to fetch/append */
      case 1: break;
      /* fatal error, abort */
      case -1: return -1;
    }
  }
#endif

  if (mx_open_mailbox (buf, M_APPEND, &ctx) != NULL)
  {
    if (h)
      _mutt_save_message(h, &ctx, delete, decode, decrypt);
    else
    {
      for (i = 0; i < Context->vcount; i++)
      {
	if (Context->hdrs[Context->v2r[i]]->tagged)
	{
	  mutt_message_hook (Context, Context->hdrs[Context->v2r[i]], M_MESSAGEHOOK);
	  _mutt_save_message(Context->hdrs[Context->v2r[i]],
			     &ctx, delete, decode, decrypt);
	}
      }
    }

    need_buffy_cleanup = (ctx.magic == M_MBOX || ctx.magic == M_MMDF || ctx.magic == M_KENDRA);

    mx_close_mailbox (&ctx, NULL);

    if (need_buffy_cleanup)
    {
#ifdef BUFFY_SIZE
      tmp = mutt_find_mailbox (buf);
      if (tmp && !tmp->new)
	mutt_update_mailbox (tmp);
#else
      /* fix up the times so buffy won't get confused */
      if (st.st_mtime > st.st_atime)
      {
	ut.actime = st.st_atime;
	ut.modtime = time (NULL);
	utime (buf, &ut); 
      }
      else
	utime (buf, NULL);
#endif
    }

    mutt_clear_error ();
    return (0);
  }
  
  return -1;
}


void mutt_version (void)
{
  mutt_message ("Mutt %s (%s)", MUTT_VERSION, ReleaseDate);
}

void mutt_edit_content_type (HEADER *h, BODY *b, FILE *fp)
{
  char buf[LONG_STRING];
  char obuf[LONG_STRING];
  char tmp[STRING];
  PARAMETER *p;

  char charset[STRING];
  char *cp;

  short charset_changed = 0;
  short type_changed = 0;
  
  cp = mutt_get_parameter ("charset", b->parameter);
  strfcpy (charset, NONULL (cp), sizeof (charset));

  snprintf (buf, sizeof (buf), "%s/%s", TYPE (b), b->subtype);
  strfcpy (obuf, buf, sizeof (obuf));
  if (b->parameter)
  {
    size_t l;
    
    for (p = b->parameter; p; p = p->next)
    {
      l = strlen (buf);

      rfc822_cat (tmp, sizeof (tmp), p->value, MimeSpecials);
      snprintf (buf + l, sizeof (buf) - l, "; %s=%s", p->attribute, tmp);
    }
  }
  
  if (mutt_get_field ("Content-Type: ", buf, sizeof (buf), 0) != 0 ||
      buf[0] == 0)
    return;
  
  /* clean up previous junk */
  mutt_free_parameter (&b->parameter);
  FREE (&b->subtype);
  
  mutt_parse_content_type (buf, b);

  
  snprintf (tmp, sizeof (tmp), "%s/%s", TYPE (b), NONULL (b->subtype));
  type_changed = ascii_strcasecmp (tmp, obuf);
  charset_changed = ascii_strcasecmp (charset, mutt_get_parameter ("charset", b->parameter));

  /* if in send mode, check for conversion - current setting is default. */

  if (!h && b->type == TYPETEXT && charset_changed)
  {
    snprintf (tmp, sizeof (tmp), _("Convert to %s upon sending?"),
	      mutt_get_parameter ("charset", b->parameter));
    b->noconv = !mutt_yesorno (tmp, !b->noconv);
  }

  /* inform the user */
  
  if (type_changed)
    mutt_message (_("Content-Type changed to %s."), tmp);
  else if (b->type == TYPETEXT && charset_changed)
    mutt_message (_("Character set changed to %s; %s."),
		  mutt_get_parameter ("charset", b->parameter),
		  b->noconv ? _("not converting") : _("converting"));

  b->force_charset |= charset_changed ? 1 : 0;

  if (!is_multipart (b) && b->parts)
    mutt_free_body (&b->parts);
  if (!mutt_is_message_type (b->type, b->subtype) && b->hdr)
  {
    b->hdr->content = NULL;
    mutt_free_header (&b->hdr);
  }

  if (fp && (is_multipart (b) || mutt_is_message_type (b->type, b->subtype)))
    mutt_parse_part (fp, b);
  
#ifdef HAVE_PGP
  if (h)
  {
    if (h->content == b)
      h->pgp = 0;
    h->pgp |= pgp_query (b);
  }
#endif /* HAVE_PGP */

}


#ifdef HAVE_PGP

static int _mutt_check_traditional_pgp (HEADER *h, int *redraw)
{
  MESSAGE *msg;
  int rv = 0;
  
  mutt_parse_mime_message (Context, h);
  if ((msg = mx_open_message (Context, h->msgno)) == NULL)
    return 0;
  if (pgp_check_traditional (msg->fp, h->content, 0))
  {
    h->pgp = pgp_query (h->content);
    *redraw |= REDRAW_FULL;
    rv = 1;
  }
  
  mx_close_message (&msg);
  return rv;
}

int mutt_check_traditional_pgp (HEADER *h, int *redraw)
{
  int i;
  int rv = 0;
  if (h)
    rv = _mutt_check_traditional_pgp (h, redraw);
  else
  {
    for (i = 0; i < Context->vcount; i++)
      if (Context->hdrs[Context->v2r[i]]->tagged)
	rv = _mutt_check_traditional_pgp (Context->hdrs[Context->v2r[i]], redraw)
	  || rv;
  }
  return rv;
}

#endif


#endif
