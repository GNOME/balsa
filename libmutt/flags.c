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
#include "sort.h"
#include "mx.h"


int mutt_change_flag (HEADER *h, int bf)
{
  int i, flag;

  mvprintw (LINES - 1, 0, "%s flag? (D/N/O/r/*/!): ", bf ? "Set" : "Clear");
  clrtoeol ();

  if ((i = mutt_getch ()) == ERR)
  {
    CLEARLINE (LINES-1);
    return (-1);
  }

  CLEARLINE (LINES-1);

  switch (i)
  {
    case 'd':
    case 'D':
      flag = M_DELETE;
      break;

    case 'N':
    case 'n':
      flag = M_NEW;
      break;

    case 'o':
    case 'O':
      if (h)
	mutt_set_flag (Context, h, MFLAG_READ, !bf);
      else
	mutt_tag_set_flag (MFLAG_READ, !bf);
      flag = M_OLD;
      break;

    case 'r':
    case 'R':
      flag = M_REPLIED;
      break;

    case '*':
      flag = M_TAG;
      break;

    case '!':
      flag = M_FLAG;
      break;

    default:
      BEEP ();
      return (-1);
  }

  if (h)
    mutt_set_flag (Context, h, flag, bf);
  else
    mutt_tag_set_flag (flag, bf);

  return 0;
}
