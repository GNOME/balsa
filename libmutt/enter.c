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
#include "mutt_menu.h"
#include "mutt_curses.h"
#include "keymap.h"
#include "history.h"

#include <termios.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

/* macro to print control chars in reverse-video */
#define ADDCH(x) addch (IsPrint (x) ? x : (ColorDefs[MT_COLOR_MARKERS] | (x + '@')))


/* redraw flags for mutt_enter_string() */
enum
{
  M_REDRAW_INIT = 1,	/* recalculate lengths */
  M_REDRAW_LINE,	/* redraw entire line */
  M_REDRAW_EOL,		/* redraw from current position to eol */
  M_REDRAW_PREV_EOL	/* redraw from curpos-1 to eol */
};

/* Returns:
 *	1 need to redraw the screen and call me again
 *	0 if input was given
 * 	-1 if abort.
 *
 */
int mutt_enter_string (unsigned char *buf, size_t buflen, int y, int x,
		       int flags)
{
  int curpos = 0;		/* the location of the cursor */
  int lastchar = 0; 		/* offset of the last char in the string */
  int begin = 0;		/* first character displayed on the line */
  int ch;			/* last typed character */
  int width = COLS - x - 1;	/* width of field */
  int redraw = M_REDRAW_INIT;	/* when/what to redraw */
  int pass = (flags == M_PASS);
  int first = 1;
  int j;
  char tempbuf[_POSIX_PATH_MAX] = "";
  history_class_t hclass;
  
  if(flags & (M_FILE | M_EFILE))
    hclass = HC_FILE;
  else if(flags & M_CMD)
    hclass = HC_CMD;
  else if(flags & M_ALIAS)
    hclass = HC_ALIAS;
  else 
    hclass = HC_OTHER;

  FOREVER
  {
    if (redraw)
    {
      if (redraw == M_REDRAW_INIT)
      {
	/* full redraw */
	lastchar = curpos = strlen ((char *) buf);
	begin = lastchar - width;
      }
      if (begin < 0)
	begin = 0;
      switch (redraw)
      {
	case M_REDRAW_PREV_EOL:
	  j = curpos - 1;
	  break;
	case M_REDRAW_EOL:
	  j = curpos;
	  break;
	default:
	  j = begin;
      }
      move (y, x + j - begin);
      for (; j < lastchar && j < begin + width; j++)
	ADDCH (buf[j]);
      clrtoeol ();
      if (redraw != M_REDRAW_INIT)
	move (y, x + curpos - begin);
      redraw = 0;
    }
    mutt_refresh ();

    /* first look to see if a keypress is an editor operation.  km_dokey()
     * returns 0 if there is no entry in the keymap, so restore the last
     * keypress and continue normally.
     */
    if ((ch = km_dokey (MENU_EDITOR)) == -1)
    {
      buf[curpos] = 0;
      return (-1);
    }

    if (ch != 0)
    {
      first = 0; /* make sure not to clear the buffer */
      switch (ch)
      {
	case OP_EDITOR_HISTORY_UP:
	  if (!pass)
	  {
	    strfcpy ((char *) buf, mutt_history_prev (hclass), buflen);
	    redraw = M_REDRAW_INIT;
	  }
	  break;
	case OP_EDITOR_HISTORY_DOWN:
	  if (!pass)
	  {
	    strfcpy ((char *) buf, mutt_history_next (hclass), buflen);
	    redraw = M_REDRAW_INIT;
	  }
	  break;
	case OP_EDITOR_BACKSPACE:
	  if (curpos == 0)
	  {
	    BEEP ();
	    break;
	  }
	  for (j = curpos ; j < lastchar ; j++)
	    buf[j - 1] = buf[j];
	  curpos--;
	  lastchar--;
	  if (!pass)
	  {
	    if (curpos > begin)
	    {
	      if (lastchar == curpos)
	      {
		move (y, x + curpos - begin);
		delch ();
	      }
	      else
		redraw = M_REDRAW_EOL;
	    }
	    else
	    {
	      begin -= width / 2;
	      redraw = M_REDRAW_LINE;
	    }
	  }
	  break;
	case OP_EDITOR_BOL:
	  /* reposition the cursor at the begininning of the line */
	  curpos = 0;
	  if (!pass)
	  {
	    if (begin)
	    {
	      /* the first char is not displayed, so readjust */
	      begin = 0;
	      redraw = M_REDRAW_LINE;
	    }
	    else
	      move (y, x);
	  }
	  break;
	case OP_EDITOR_EOL:
	  curpos = lastchar;
	  if (!pass)
	  {
	    if (lastchar < begin + width)
	      move (y, x + lastchar - begin);
	    else
	    {
	      begin = lastchar - width / 2;
	      redraw = M_REDRAW_LINE;
	    }
	  }
	  break;
	case OP_EDITOR_KILL_LINE:
	  lastchar = curpos = 0;
	  if (!pass)
	  {
	    begin = 0;
	    redraw = M_REDRAW_LINE;
	  }
	  break;
	case OP_EDITOR_KILL_EOL:
	  lastchar = curpos;
	  if (!pass)
	    clrtoeol ();
	  break;
	case OP_EDITOR_BACKWARD_CHAR:
	  if (curpos == 0)
	  {
	    BEEP ();
	  }
	  else
	  {
	    curpos--;
	    if (!pass)
	    {
	      if (curpos < begin)
	      {
		begin -= width / 2;
		redraw = M_REDRAW_LINE;
	      }
	      else
		move (y, x + curpos - begin);
	    }
	  }
	  break;
	case OP_EDITOR_FORWARD_CHAR:
	  if (curpos == lastchar)
	  {
	    BEEP ();
	  }
	  else
	  {
	    curpos++;
	    if (!pass)
	    {
	      if (curpos >= begin + width)
	      {
		begin = curpos - width / 2;
		redraw = M_REDRAW_LINE;
	      }
	      else
		move (y, x + curpos - begin);
	    }
	  }
	  break;
	case OP_EDITOR_DELETE_CHAR:
	  if (curpos != lastchar)
	  {
	    for (j = curpos; j < lastchar; j++)
	      buf[j] = buf[j + 1];
	    lastchar--;
	    if (!pass)
	      redraw = M_REDRAW_EOL;
	  }
	  else
	    BEEP ();
	  break;
	case OP_EDITOR_KILL_WORD:
	  /* delete to begining of word */
	  if (curpos != 0)
	  {
	    j = curpos;
	    while (j > 0 && ISSPACE (buf[j - 1]))
	      j--;
	    if (j > 0)
	    {
	      if (isalnum (buf[j - 1]))
	      {
		for (j--; j > 0 && isalnum (buf[j - 1]); j--)
		  ;
	      }
	      else
		j--;
	    }
	    ch = j; /* save current position */
	    while (curpos < lastchar)
	      buf[j++] = buf[curpos++];
	    lastchar = j;
	    curpos = ch; /* restore current position */
	    /* update screen */
	    if (!pass)
	    {
	      if (curpos < begin)
	      {
		begin = curpos - width / 2;
		redraw = M_REDRAW_LINE;
	      }
	      else
		redraw = M_REDRAW_EOL;
	    }
	  }
	  break;
	case OP_EDITOR_BUFFY_CYCLE:
	  if (flags & M_EFILE)
	  {
	    first = 1; /* clear input if user types a real key later */
	    buf[curpos] = 0;
	    mutt_buffy ((char *) buf);
	    redraw = M_REDRAW_INIT;
	    break;
	  }
	  else if (!(flags & M_FILE))
	    goto self_insert;
	  /* fall through to completion routine (M_FILE) */

	case OP_EDITOR_COMPLETE:
	  if (flags & M_CMD)
	  {
	    buf[curpos] = 0;
	    for (j = curpos - 1; j >= 0 && buf[j] != ' '; j--);
	    if (strcmp (tempbuf, (char *) buf) == 0)
	    {
	      mutt_select_file ((char *) buf + j + 1, buflen - j - 1, 0);
	      set_option (OPTNEEDREDRAW);
	      return (1);
	    }
	    if (mutt_complete ((char *) buf + j + 1) == 0)
	      strfcpy (tempbuf, (char *) buf + j + 1, sizeof (tempbuf));
	    else
	      BEEP ();
	    redraw = M_REDRAW_INIT;
	  }
	  else if (flags & M_ALIAS)
	  {
	    /* invoke the alias-menu to get more addresses */
	    buf[curpos] = 0;
	    if (curpos)
	    {
	      for (j = curpos - 1 ; j >= 0 && buf[j] != ' ' && buf[j] != ',' ; j--);
	      if (mutt_alias_complete ((char *) buf + j + 1, buflen - j - 1))
	      {
		redraw = M_REDRAW_INIT;
		continue;
	      }
	    }
	    else
	      mutt_alias_menu ((char *) buf, buflen, Aliases);
	    return (1);
	  }
	  else if (flags & M_COMMAND)
	  {
	    buf[curpos] = 0;
	    if (mutt_command_complete ((char *) buf, buflen, curpos))
	    {
	      redraw = M_REDRAW_INIT;
	      continue;
	    }
	  }
	  else if (flags & (M_FILE | M_EFILE))
	  {
	    buf[curpos] = 0;

	    /* see if the path has changed from the last time */
	    if (strcmp (tempbuf, (char *) buf) == 0)
	    {
	      mutt_select_file ((char *) buf, buflen, 0);
	      set_option (OPTNEEDREDRAW);
	      if (buf[0])
	      {
		mutt_pretty_mailbox ((char *) buf);
		mutt_history_add (hclass, (char *) buf);
		return (0);
	      }
	      return (-1);
	    }

	    if (mutt_complete ((char *) buf) == 0)
	      strfcpy (tempbuf, (char *) buf, sizeof (tempbuf));
	    else
	      BEEP (); /* let the user know that nothing matched */
	    redraw = M_REDRAW_INIT;
	  }
	  else
	    goto self_insert;
	  break;

	case OP_EDITOR_COMPLETE_QUERY:
	  if (flags & M_ALIAS)
	  {
	    /* invoke the query-menu to get more addresses */
	    buf[curpos] = 0;
	    if (curpos)
	    {
	      for (j = curpos - 1 ; j >= 0 && buf[j] != ' ' && buf[j] != ',' ; j--);
	      mutt_query_complete ((char *) buf + j + 1, buflen - j - 1);
	    }
	    else
	      mutt_query_menu ((char *) buf, buflen);
	    return (1);
	  }
	  else
	    goto self_insert;

	case OP_EDITOR_QUOTE_CHAR:
	  ADDCH (LastKey);
	  LastKey = mutt_getch ();
	  move (y, x + curpos - begin);
	  goto self_insert;

	default:
	  BEEP ();
      }
    }
    else
    {

self_insert:

      /* use the raw keypress */
      ch = LastKey;

      if (first && (flags & M_CLEAR))
      {
	first = 0;
	if (IsPrint (ch))
	{
	  mutt_ungetch (ch);
	  buf[0] = 0;
	  redraw = M_REDRAW_INIT;
	  continue;
	}
      }

      if (CI_is_return (ch))
      {
	buf[lastchar] = 0;
	if (!pass)
	  mutt_history_add (hclass, (char *) buf);
	return (0);
      }
      else if ((ch < ' ' || IsPrint (ch)) && (lastchar + 1 < buflen))
      {
	for (j = lastchar; j > curpos; j--)
	  buf[j] = buf[j - 1];
	buf[curpos++] = ch;
	lastchar++;

	if (!pass)
	{
	  if (curpos >= begin + width)
	  {
	    begin = curpos - width / 2;
	    redraw = M_REDRAW_LINE;
	  }
	  else if (curpos == lastchar)
	    ADDCH (ch);
	  else
	    redraw = M_REDRAW_PREV_EOL;
	}
      }
      else
      {
	mutt_flushinp ();
	BEEP ();
      }
    }
  }
  /* not reached */
}
