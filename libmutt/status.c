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
#include "sort.h"

#include <string.h>
#include <ctype.h>
#include <unistd.h>

static char *get_sort_str (char *buf, size_t buflen, int method)
{
  snprintf (buf, buflen, "%s%s%s",
	    (method & SORT_REVERSE) ? "reverse-" : "",
	    (method & SORT_LAST) ? "last-" : "",
	    mutt_getnamebyvalue (method & SORT_MASK, SortMethods));
  return buf;
}

/*
 * %b = number of incoming folders with unread messages [option]
 * %d = number of deleted messages [option]
 * %f = full mailbox path
 * %F = number of flagged messages [option]
 * %h = hostname
 * %l = length of mailbox (in bytes) [option]
 * %m = total number of messages [option]
 * %M = number of messages shown (virutal message count when limiting) [option]
 * %n = number of new messages [option]
 * %p = number of postponed messages [option]
 * %P = percent of way through index
 * %r = readonly/wontwrite/changed flag
 * %s = current sorting method ($sort)
 * %S = current aux sorting method ($sort_aux)
 * %t = # of tagged messages [option]
 * %v = Mutt version
 *
 * %>X = right justify and pad with "X"
 * %|X = pad with "X" to end of line
 *
 * optional fields:
 *    %?<format_char>?<optional_string>?
 */

void menu_status_line (char *buf, size_t buflen, MUTTMENU *menu, const char *p)
{
  char *cp;
  char *wptr = buf;
  char tmp[SHORT_STRING];
  char tmp2[SHORT_STRING];
  char prefix[SHORT_STRING];
  char fmt[SHORT_STRING];
  char optstring[SHORT_STRING];
  char ch;
  int wlen = 0;
  int optional = 0;
  int count;
  int len;

  buflen--; /* save room for the trailing \0 */
  while (*p && wlen < buflen)
  {
    if (*p == '%')
    {
      p++;

      if (*p == '?')
      {
	optional = 1;
	p++;
      }
      else
      {
	optional = 0;

	/* eat the format string */
	cp = prefix;
	while (*p && (isdigit (*p) || *p == '.' || *p == '-'))
	  *cp++ = *p++;
	*cp = 0;
      }

      if (!*p)
	break; /* bad format */

      ch = *p++; /* save the character to switch on */

      if (optional)
      {
	if (*p != '?')
	  break; /* bad format */
	p++;

	/* eat the optional part of the string */
	cp = optstring;
	while (*p && *p != '?')
	  *cp++ = *p++;
	*cp = 0;
	if (!*p)
	  break; /* bad format */
	p++; /* move past the trailing `?' */
      }

      tmp[0] = 0;
      switch (ch)
      {
	case '>': /* right justify rest of line */

	  if (!*p)
	    break; /* bad format */
	  ch = *p++; /* pad character */
	  menu_status_line (tmp, sizeof (tmp), menu, p);
	  len = COLS - wlen - strlen (tmp);
	  while (len > 0)
	  {
	    *wptr++ = ch; /* pad character */
	    wlen++;
	    len--;
	  }
	  break;

	case '|': /* pad to end of line */

	  if (!*p)
	    break; /* bad format */
	  ch = *p++;
	  if (buflen > COLS)
	    buflen = COLS;
	  while (wlen < buflen)
	  {
	    *wptr++ = ch; /* pad character */
	    wlen++;
	  }
	  break;

        case 'b':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, mutt_buffy_check (0));
	  }
	  else if (!mutt_buffy_check (0))
	    optional = 0;
	  break;

	case 'd':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, Context ? Context->deleted : 0);
	  }
	  else if (!Context || !Context->deleted)
	    optional = 0;
	  break;

	case 'h':

	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (tmp, sizeof (tmp), fmt, Hostname);
	  break;

	case 'f':

	  snprintf (fmt, sizeof(fmt), "%%%ss", prefix);
	  if (Context && Context->path)
	  {
	    strfcpy (prefix, Context->path, sizeof (prefix));
	    mutt_pretty_mailbox (prefix);
	  }
	  else
	    strfcpy (prefix, "(no mailbox)", sizeof (prefix));
	  snprintf (tmp, sizeof (tmp), fmt, prefix);
	  break;

	case 'F':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, Context ? Context->flagged : 0);
	  }
	  else if (!Context || !Context->flagged)
	    optional = 0;
	  break;

	case 'l':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	    mutt_pretty_size (tmp2, sizeof (tmp2), Context ? Context->size : 0);
	    snprintf (tmp, sizeof (tmp), fmt, tmp2);
	  }
	  else if (!Context || !Context->size)
	    optional = 0;
	  break;

	case 'm':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, Context ? Context->msgcount : 0);
	  }
	  else if (!Context || !Context->msgcount)
	    optional = 0;
	  break;

	case 'M':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof(fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof(tmp), fmt, Context ? Context->vcount : 0);
	  }
	  else if (!Context || Context->vcount == Context->msgcount)
	    optional = 0;
	  break;

	case 'n':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, Context ? Context->new : 0);
	  }
	  else if (!Context || !Context->new)
	    optional = 0;
	  break;

	case 'o':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, Context ? Context->unread - Context->new : 0);
	  }
	  else if (!Context || !(Context->unread - Context->new))
	    optional = 0;
	  break;
	  
	case 'p':

	  count = mutt_num_postponed ();
	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, count);
	  }
	  else if (!count)
	    optional = 0;
	  break;

	case 'P':
	
	  if (menu->top + menu->pagelen >= menu->max)
	    cp = menu->top ? "end" : "all";
	  else
	  {
	    count = (100 * (menu->top + menu->pagelen)) / menu->max;
	    snprintf (tmp2, sizeof (tmp2), "%d%%", count);
            cp = tmp2;
	  }
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (tmp, sizeof (tmp), fmt, cp);
	  break;
  
	case 'r':

	  if (Context)
	    tmp[0] = (Context->readonly || Context->dontwrite) ? StChars[2] :
		      (Context->changed || Context->deleted) ? StChars[1] : StChars[0];
	  else
	    tmp[0] = StChars[0];
	  tmp[1] = 0;
	  break;

	case 's':
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (tmp, sizeof (tmp), fmt,
		    get_sort_str (prefix, sizeof (prefix), Sort));
	  break;

	case 'S':
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (tmp, sizeof (tmp), fmt,
		    get_sort_str (prefix, sizeof (prefix), SortAux));
	  break;

	case 't':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, Context ? Context->tagged : 0);
	  }
	  else if (!Context || !Context->tagged)
	    optional = 0;
	  break;

	case 'u':

	  if (!optional)
	  {
	    snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	    snprintf (tmp, sizeof (tmp), fmt, Context ? Context->unread : 0);
	  }
	  else if (!Context || !Context->unread)
	    optional = 0;
	  break;
	  
	case 'v':

	  snprintf (fmt, sizeof (fmt), "Mutt %%s");
	  snprintf (tmp, sizeof (tmp), fmt, VERSION);
	  break;

	case 0:

	  *buf = 0;
	  return;

	default:

	  snprintf (tmp, sizeof (tmp), "%%%s%c", prefix, ch);
	  break;
      }

      if (optional)
	menu_status_line (tmp, sizeof (tmp), menu, optstring);

      if ((len = strlen (tmp)))
      {
	if (len + wlen > buflen)
	{
	  if ((len = buflen - wlen) < 0)
	    len = 0;
	}
	memcpy (wptr, tmp, len);
	wptr += len;
	wlen += len;
      }
    }
    else
    {
      *wptr++ = *p++;
      wlen++;
    }
  }
  *wptr = 0;
}
