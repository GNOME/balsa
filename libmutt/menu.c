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

#include <string.h>
#include <stdlib.h>

#define M_MODEFMT "-- Mutt: %s"

static void print_enriched_string (int attr, unsigned char *s, int do_color)
{
  while (*s)
  {
    if (*s < M_TREE_MAX)
    {
      if (do_color)
	SETCOLOR (MT_COLOR_TREE);
      while (*s && *s < M_TREE_MAX)
      {
	switch (*s)
	{
	  case M_TREE_LLCORNER:
	    addch (option (OPTASCIICHARS) ? '`' : ACS_LLCORNER);
	    break;
	  case M_TREE_ULCORNER:
	    addch (option (OPTASCIICHARS) ? ',' : ACS_ULCORNER);
	    break;
	  case M_TREE_LTEE:
	    addch (option (OPTASCIICHARS) ? '|' : ACS_LTEE);
	    break;
	  case M_TREE_HLINE:
	    addch (option (OPTASCIICHARS) ? '-' : ACS_HLINE);
	    break;
	  case M_TREE_VLINE:
	    addch (option (OPTASCIICHARS) ? '|' : ACS_VLINE);
	    break;
	  case M_TREE_SPACE:
	    addch (' ');
	    break;
	  case M_TREE_RARROW:
	    addch ('>');
	    break;
	  case M_TREE_STAR:
	    addch ('*'); /* fake thread indicator */
	    break;
	  case M_TREE_HIDDEN:
	    addch ('&');
	    break;
	}
	s++;
      }
      if (do_color) attrset(attr);
    }
    else
    {
      addch (*s);
      s++;
    }
  }
}

void menu_pad_string (char *s, size_t l)
{
#if !defined(HAVE_BKGDSET) && !defined (USE_SLANG_CURSES)
  int n = strlen (s);
#endif
  int shift = option (OPTARROWCURSOR) ? 3 : 0;
  
  l--; /* save room for the terminal \0 */
  if (l > COLS - shift)
    l = COLS - shift;
#if !defined (HAVE_BKGDSET) && !defined (USE_SLANG_CURSES)
  /* we have to pad the string with blanks to the end of line */
  if (n < l)
  {
    while (n < l)
      s[n++] = ' ';
    s[n] = 0;
  }
  else
#endif
    s[l] = 0;
}

void menu_redraw_full (MUTTMENU *menu)
{
  SETCOLOR (MT_COLOR_NORMAL);
  /* clear() doesn't optimize screen redraws */
  move (0, 0);
  clrtobot ();

  if (option (OPTHELP))
  {
    SETCOLOR (MT_COLOR_STATUS);
    mvprintw (option (OPTSTATUSONTOP) ? LINES-2 : 0, 0, "%-*.*s", COLS, COLS, menu->help);
    SETCOLOR (MT_COLOR_NORMAL);
    menu->offset = 1;
    menu->pagelen = LINES - 3;
  }
  else
  {
    menu->offset = option (OPTSTATUSONTOP) ? 1 : 0;
    menu->pagelen = LINES - 2;
  }

  mutt_show_error ();

  menu->redraw = REDRAW_INDEX | REDRAW_STATUS;
}

void menu_redraw_index (MUTTMENU *menu)
{
  char buf[STRING];
  int i;

  for (i = menu->top; i < menu->top + menu->pagelen; i++)
  {
    if (i < menu->max)
    {
      menu->make_entry (buf, sizeof (buf), menu, i);
      menu_pad_string (buf, sizeof (buf));

      if (option (OPTARROWCURSOR))
      {
        attrset (menu->color (i));
	CLEARLINE (i - menu->top + menu->offset);

	if (i == menu->current)
	{
	  SETCOLOR (MT_COLOR_INDICATOR);
	  addstr ("->");
          attrset (menu->color (i));
	  addch (' ');
	}
	else
	  move (i - menu->top + menu->offset, 3);

        print_enriched_string (menu->color(i), (unsigned char *) buf, 1);
        SETCOLOR (MT_COLOR_NORMAL);          
      }
      else
      {
	if (i == menu->current)
	{
	  SETCOLOR (MT_COLOR_INDICATOR);
	  BKGDSET (MT_COLOR_INDICATOR);
	}
        else
          attrset (menu->color (i));
            
	CLEARLINE (i - menu->top + menu->offset);
	print_enriched_string (menu->color(i), (unsigned char *) buf, i != menu->current);
        SETCOLOR (MT_COLOR_NORMAL);
        BKGDSET (MT_COLOR_NORMAL);
      }
    }
    else
      CLEARLINE (i - menu->top + menu->offset);
  }
  menu->redraw = 0;
}

void menu_redraw_motion (MUTTMENU *menu)
{
  char buf[STRING];
  
  move (menu->oldcurrent + menu->offset - menu->top, 0);
  SETCOLOR (MT_COLOR_NORMAL);
  BKGDSET (MT_COLOR_NORMAL);

  if (option (OPTARROWCURSOR))
  {
    /* clear the pointer */
    attrset (menu->color (menu->oldcurrent));
    addstr ("  ");

    if (menu->redraw & REDRAW_MOTION_RESYNCH)
    {
      clrtoeol ();
      menu->make_entry (buf, sizeof (buf), menu, menu->oldcurrent);
      menu_pad_string (buf, sizeof (buf));
      move (menu->oldcurrent + menu->offset - menu->top, 3);
      print_enriched_string (menu->color(menu->oldcurrent), (unsigned char *) buf, 1);
      SETCOLOR (MT_COLOR_NORMAL);
    }

    /* now draw it in the new location */
    move (menu->current + menu->offset - menu->top, 0);
    SETCOLOR (MT_COLOR_INDICATOR);
    addstr ("->");
    SETCOLOR (MT_COLOR_NORMAL);
  }
  else
  {
    /* erase the current indicator */
    attrset (menu->color (menu->oldcurrent));
    clrtoeol ();
    menu->make_entry (buf, sizeof (buf), menu, menu->oldcurrent);
    menu_pad_string (buf, sizeof (buf));
    print_enriched_string (menu->color(menu->oldcurrent), (unsigned char *) buf, 1);

    /* now draw the new one to reflect the change */
    menu->make_entry (buf, sizeof (buf), menu, menu->current);
    menu_pad_string (buf, sizeof (buf));
    SETCOLOR (MT_COLOR_INDICATOR);
    BKGDSET (MT_COLOR_INDICATOR);
    CLEARLINE (menu->current - menu->top + menu->offset);
    print_enriched_string (menu->color(menu->current), (unsigned char *) buf, 0);
    SETCOLOR (MT_COLOR_NORMAL);
    BKGDSET (MT_COLOR_NORMAL);
  }
  menu->redraw &= REDRAW_STATUS;
}

void menu_redraw_current (MUTTMENU *menu)
{
  char buf[STRING];
  
  move (menu->current + menu->offset - menu->top, 0);
  menu->make_entry (buf, sizeof (buf), menu, menu->current);
  menu_pad_string (buf, sizeof (buf));

  if (option (OPTARROWCURSOR))
  {
    int attr = menu->color (menu->current);
    attrset (attr);
    clrtoeol ();
    SETCOLOR (MT_COLOR_INDICATOR);
    addstr ("->");
    attrset (attr);
    addch (' ');
    menu_pad_string (buf, sizeof (buf));
    print_enriched_string (menu->color(menu->current), (unsigned char *) buf, 1);
    SETCOLOR (MT_COLOR_NORMAL);
  }
  else
  {
    SETCOLOR (MT_COLOR_INDICATOR);
    BKGDSET (MT_COLOR_INDICATOR);
    clrtoeol ();
    print_enriched_string (menu->color(menu->current), (unsigned char *) buf, 0);
    SETCOLOR (MT_COLOR_NORMAL);
    BKGDSET (MT_COLOR_NORMAL);
  }
  menu->redraw &= REDRAW_STATUS;
}

void menu_check_recenter (MUTTMENU *menu)
{
  if (menu->current >= menu->top + menu->pagelen)
  {
    if (option (OPTMENUSCROLL))
      menu->top = menu->current - menu->pagelen + 1;
    else
      menu->top += menu->pagelen * ((menu->current - menu->top) / menu->pagelen);
    menu->redraw = REDRAW_INDEX;
  }
  else if (menu->current < menu->top)
  {
    if (option (OPTMENUSCROLL))
      menu->top = menu->current;
    else
    {
      menu->top -= menu->pagelen * ((menu->top + menu->pagelen - 1 - menu->current) / menu->pagelen);
      if (menu->top < 0)
	menu->top = 0;
    }
    menu->redraw = REDRAW_INDEX;
  }
}

void menu_jump (MUTTMENU *menu)
{
  int n;
  char buf[SHORT_STRING];

  if (menu->max)
  {
    mutt_ungetch (LastKey);
    buf[0] = 0;
    if (mutt_get_field ("Jump to: ", buf, sizeof (buf), 0) == 0 && buf[0])
    {
      n = atoi (buf) - 1;
      if (n >= 0 && n < menu->max)
      {
	menu->current = n;
	menu->redraw = REDRAW_MOTION;
      }
      else
	mutt_error ("Invalid index number.");
    }
  }
  else
    mutt_error ("No entries.");
}

void menu_next_line (MUTTMENU *menu)
{
  if (menu->max)
  {
    if (menu->top < menu->max - 1)
    {
      menu->top++;
      if (menu->current < menu->top)
	menu->current++;
      menu->redraw = REDRAW_INDEX;
    }
    else
      mutt_error ("You cannot scroll down farther.");
  }
  else
    mutt_error ("No entries.");
}

void menu_prev_line (MUTTMENU *menu)
{
  if (menu->top > 0)
  {
    menu->top--;
    if (menu->current >= menu->top + menu->pagelen)
      menu->current--;
    menu->redraw = REDRAW_INDEX;
  }
  else
    mutt_error ("You cannot scroll up farther.");
}

void menu_next_page (MUTTMENU *menu)
{
  if (menu->max)
  {
    if (menu->top + menu->pagelen < menu->max)
    {
      menu->top += menu->pagelen;
      if (menu->current < menu->top)
	menu->current = menu->top;
      menu->redraw = REDRAW_INDEX;
    }
    else if (menu->current != menu->max - 1)
    {
      menu->current = menu->max - 1;
      menu->redraw = REDRAW_MOTION;
    }
    else
      mutt_error ("You are on the last page.");
  }
  else
    mutt_error ("No entries.");
}

void menu_prev_page (MUTTMENU *menu)
{
  if (menu->top > 0)
  {
    if ((menu->top -= menu->pagelen) < 0)
      menu->top = 0;
    if (menu->current >= menu->top + menu->pagelen)
      menu->current = menu->top + menu->pagelen - 1;
    menu->redraw = REDRAW_INDEX;
  }
  else if (menu->current)
  {
    menu->current = 0;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("You are on the first page.");
}

void menu_top_page (MUTTMENU *menu)
{
  if (menu->current != menu->top)
  {
    menu->current = menu->top;
    menu->redraw = REDRAW_MOTION;
  }
}

void menu_bottom_page (MUTTMENU *menu)
{
  if (menu->max)
  {
    menu->current = menu->top + menu->pagelen - 1;
    if (menu->current > menu->max - 1)
      menu->current = menu->max - 1;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("No entries.");
}

void menu_middle_page (MUTTMENU *menu)
{
  int i;

  if (menu->max)
  {
    i = menu->top + menu->pagelen;
    if (i > menu->max - 1)
      i = menu->max - 1;
    menu->current = menu->top + (i - menu->top) / 2;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("No entries.");
}

void menu_first_entry (MUTTMENU *menu)
{
  if (menu->max)
  {
    menu->current = 0;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("No entries.");
}

void menu_last_entry (MUTTMENU *menu)
{
  if (menu->max)
  {
    menu->current = menu->max - 1;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("No entries.");
}

void menu_half_up (MUTTMENU *menu)
{
  if (menu->top > 0)
  {
    if ((menu->top -= menu->pagelen / 2) < 0)
      menu->top = 0;
    if (menu->current >= menu->top + menu->pagelen)
      menu->current = menu->top + menu->pagelen - 1;
    menu->redraw = REDRAW_INDEX;
  }
  else if (menu->current)
  {
    menu->current = 0;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("First entry is shown.");
}

void menu_half_down (MUTTMENU *menu)
{
  if (menu->max)
  {
    if (menu->top + menu->pagelen < menu->max)
    {
      menu->top += menu->pagelen / 2;
      if (menu->current < menu->top)
	menu->current = menu->top;
      menu->redraw = REDRAW_INDEX;
    }
    else if (menu->current != menu->max - 1)
    {
      menu->current = menu->max - 1;
      menu->redraw = REDRAW_INDEX;
    }
    else
      mutt_error ("Last entry is shown.");
  }
  else
    mutt_error ("No entries.");
}

void menu_current_top (MUTTMENU *menu)
{
  if (menu->max)
  {
    menu->top = menu->current;
    menu->redraw = REDRAW_INDEX;
  }
  else
    mutt_error ("No entries.");
}

void menu_current_middle (MUTTMENU *menu)
{
  if (menu->max)
  {
    menu->top = menu->current - menu->pagelen / 2;
    if (menu->top < 0)
      menu->top = 0;
    menu->redraw = REDRAW_INDEX;
  }
  else
    mutt_error ("No entries.");
}

void menu_current_bottom (MUTTMENU *menu)
{
  if (menu->max)
  {
    menu->top = menu->current - menu->pagelen + 1;
    if (menu->top < 0)
      menu->top = 0;
    menu->redraw = REDRAW_INDEX;
  }
  else
    mutt_error ("No entries.");
}

void menu_next_entry (MUTTMENU *menu)
{
  if (menu->current < menu->max - 1)
  {
    menu->current++;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("You are on the last entry.");
}

void menu_prev_entry (MUTTMENU *menu)
{
  if (menu->current)
  {
    menu->current--;
    menu->redraw = REDRAW_MOTION;
  }
  else
    mutt_error ("You are on the first entry.");
}

static int default_color (int i)
{
   return ColorDefs[MT_COLOR_NORMAL];
}

MUTTMENU *mutt_new_menu (void)
{
  MUTTMENU *p = (MUTTMENU *) safe_calloc (1, sizeof (MUTTMENU));

  p->offset = 1;
  p->redraw = REDRAW_FULL;
  p->pagelen = PAGELEN;
  p->color = default_color;
  return (p);
}

void mutt_menuDestroy (MUTTMENU **p)
{
  safe_free ((void **) &(*p)->searchBuf);
  safe_free ((void **) p);
}

#define M_SEARCH_UP   1
#define M_SEARCH_DOWN 2

static int menu_search (MUTTMENU *menu, int op)
{
  int r;
  int searchDir = (menu->searchDir == M_SEARCH_UP) ? -1 : 1;
  regex_t re;
  char buf[SHORT_STRING];

  if (op != OP_SEARCH_NEXT && op != OP_SEARCH_OPPOSITE)
  {
    strfcpy (buf, menu->searchBuf ? menu->searchBuf : "", sizeof (buf));
    if (mutt_get_field ("Search for: ", buf, sizeof (buf), M_CLEAR) != 0 || !buf[0])
      return (-1);
    safe_free ((void **) &menu->searchBuf);
    menu->searchBuf = safe_strdup (buf);
    menu->searchDir = (op == OP_SEARCH) ? M_SEARCH_DOWN : M_SEARCH_UP;
  }
  else 
  {
    if (!menu->searchBuf)
    {
      mutt_error ("No search pattern.");
      return (-1);
    }

    if (op == OP_SEARCH_OPPOSITE)
      searchDir = -searchDir;
  }

  if ((r = REGCOMP (&re, menu->searchBuf, REG_NOSUB | mutt_which_case (menu->searchBuf))) != 0)
  {
    regerror (r, &re, buf, sizeof (buf));
    regfree (&re);
    mutt_error ("%s", buf);
    return (-1);
  }

  r = menu->current + searchDir;
  while (r >= 0 && r < menu->max)
  {
    if (menu->search (menu, &re, r) == 0)
    {
      regfree (&re);
      return r;
    }

    r += searchDir;
  }

  regfree (&re);
  mutt_error ("Not found.");
  return (-1);
}

int mutt_menuLoop (MUTTMENU *menu)
{
  int i = OP_NULL;
  char buf[STRING];

  FOREVER
  {
    mutt_curs_set (0);

    /* See if all or part of the screen needs to be updated.  */
    if (menu->redraw & REDRAW_FULL)
    {
      menu_redraw_full (menu);
      /* allow the caller to do any local configuration */
      return (OP_REDRAW);
    }

    menu_check_recenter (menu);

    if (menu->redraw & REDRAW_STATUS)
    {
      snprintf (buf, sizeof (buf), M_MODEFMT, menu->title);
      SETCOLOR (MT_COLOR_STATUS);
      mvprintw (option (OPTSTATUSONTOP) ? 0 : LINES - 2, 0, "%-*.*s", COLS, COLS, buf);
      SETCOLOR (MT_COLOR_NORMAL);
      menu->redraw &= ~REDRAW_STATUS;
    }

    if (menu->redraw & REDRAW_INDEX)
      menu_redraw_index (menu);
    else if (menu->redraw & (REDRAW_MOTION | REDRAW_MOTION_RESYNCH))
      menu_redraw_motion (menu);
    else if (menu->redraw == REDRAW_CURRENT)
      menu_redraw_current (menu);

    menu->oldcurrent = menu->current;

    /* move the cursor out of the way */
    move (menu->current - menu->top + menu->offset,
	  (option (OPTARROWCURSOR) ? 2 : COLS-1));

    mutt_refresh ();
    i = km_dokey (menu->menu);
    if (i == OP_TAG_PREFIX)
    {
      mvaddstr (LINES - 1, 0, "Tag-");
      i = km_dokey (menu->menu);
      menu->tagprefix = 1;
      CLEARLINE (LINES - 1);
    }
    else if (menu->tagged && option (OPTAUTOTAG))
      menu->tagprefix = 1;
    else
      menu->tagprefix = 0;

    mutt_curs_set (1);

#if defined (USE_SLANG_CURSES) || defined (HAVE_RESIZETERM)
    if (Signals & S_SIGWINCH)
    {
      mutt_resize_screen ();
      menu->redraw = REDRAW_FULL;
      Signals &= ~S_SIGWINCH;
    }
#endif

    if (i == -1)
      continue;

    mutt_clear_error ();

    switch (i)
    {
      case OP_NEXT_ENTRY:
	menu_next_entry (menu);
	break;
      case OP_PREV_ENTRY:
	menu_prev_entry (menu);
	break;
      case OP_HALF_DOWN:
	menu_half_down (menu);
	break;
      case OP_HALF_UP:
	menu_half_up (menu);
	break;
      case OP_NEXT_PAGE:
	menu_next_page (menu);
	break;
      case OP_PREV_PAGE:
	menu_prev_page (menu);
	break;
      case OP_NEXT_LINE:
	menu_next_line (menu);
	break;
      case OP_PREV_LINE:
	menu_prev_line (menu);
	break;
      case OP_FIRST_ENTRY:
	menu_first_entry (menu);
	break;
      case OP_LAST_ENTRY:
	menu_last_entry (menu);
	break;
      case OP_TOP_PAGE:
	menu_top_page (menu);
	break;
      case OP_MIDDLE_PAGE:
	menu_middle_page (menu);
	break;
      case OP_BOTTOM_PAGE:
	menu_bottom_page (menu);
	break;
      case OP_CURRENT_TOP:
	menu_current_top (menu);
	break;
      case OP_CURRENT_MIDDLE:
	menu_current_middle (menu);
	break;
      case OP_CURRENT_BOTTOM:
	menu_current_bottom (menu);
	break;
      case OP_SEARCH:
      case OP_SEARCH_REVERSE:
      case OP_SEARCH_NEXT:
      case OP_SEARCH_OPPOSITE:
	if (menu->search)
	{
	  menu->oldcurrent = menu->current;
	  if ((menu->current = menu_search (menu, i)) != -1)
	    menu->redraw = REDRAW_MOTION;
	  else
	    menu->current = menu->oldcurrent;
	}
	else
	  mutt_error ("Search is not implemented for this menu.");
	break;

      case OP_JUMP:
	menu_jump (menu);
	break;

      case OP_ENTER_COMMAND:
	mutt_enter_command ();
	if (option (OPTFORCEREDRAWINDEX))
	{
	  menu->redraw = REDRAW_FULL;
	  unset_option (OPTFORCEREDRAWINDEX);
	  unset_option (OPTFORCEREDRAWPAGER);
	}
	break;

      case OP_TAG:
	if (menu->tag)
	{
	  if (menu->max)
	  {
	    if (menu->tag (menu, menu->current))
	      menu->tagged++;
	    else
	      menu->tagged--;
	    if (option (OPTRESOLVE) && menu->current < menu->max - 1)
	    {
	      menu->current++;
	      menu->redraw = REDRAW_MOTION_RESYNCH;
	    }
	    else
	      menu->redraw = REDRAW_CURRENT;
	  }
	  else
	    mutt_error ("No entries.");
	}
	else
	  mutt_error ("Tagging is not supported.");
	break;

      case OP_SHELL_ESCAPE:
	mutt_shell_escape ();
	MAYBE_REDRAW (menu->redraw);
	break;

      case OP_REDRAW:
	clearok (stdscr, TRUE);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_HELP:
	mutt_help (menu->menu);
	menu->redraw = REDRAW_FULL;
	break;

      case OP_NULL:
	km_error_key (menu->menu);
	break;

      default:
	return (i);
    }
  }
  /* not reached */
}
