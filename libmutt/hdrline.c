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


#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

int mutt_is_mail_list (ADDRESS *addr)
{
  LIST *p;

  if (addr->mailbox)
  {
    for (p = MailLists; p; p = p->next)
      if (strncasecmp (addr->mailbox, p->data, strlen (p->data)) == 0)
	return 1;
  }
  return 0;
}

static int
check_for_mailing_list (ADDRESS *adr, char *pfx, char *buf, int buflen)
{
  for (; adr; adr = adr->next)
  {
    if (mutt_is_mail_list (adr))
    {
      snprintf (buf, buflen, "%s%s", pfx, mutt_get_name (adr));
      return 1;
    }
  }
  return 0;
}

static void make_from (ENVELOPE *hdr, char *buf, size_t len, int do_lists)
{
  int me;

  me = mutt_addr_is_user (hdr->from);

  if (do_lists || me)
  {
    if (check_for_mailing_list (hdr->to, "To ", buf, len))
      return;
    if (check_for_mailing_list (hdr->cc, "Cc ", buf, len))
      return;
  }

  if (me && hdr->to)
    snprintf (buf, len, "To %s", mutt_get_name (hdr->to));
  else if (me && hdr->cc)
    snprintf (buf, len, "Cc %s", mutt_get_name (hdr->cc));
  else if (hdr->from)
    strfcpy (buf, mutt_get_name (hdr->from), len);
  else
    *buf = 0;
}

int mutt_user_is_recipient (ADDRESS *a)
{
  for (; a; a = a->next)
    if (mutt_addr_is_user (a))
      return 1;
  return 0;
}

/* Return values:
 * 0: user is not in list
 * 1: user is unique recipient
 * 2: user is in the TO list
 * 3: user is in the CC list
 * 4: user is originator
 */
static int user_is_recipient (ENVELOPE *hdr)
{
  if (mutt_addr_is_user (hdr->from))
    return 4;

  if (mutt_user_is_recipient (hdr->to))
  {
    if (hdr->to->next || hdr->cc)
      return 2; /* non-unique recipient */
    else
      return 1; /* unique recipient */
  }

  if (mutt_user_is_recipient (hdr->cc))
    return 3;

  return (0);
}

/*
 * %a = address of author
 * %c = size of message in bytes
 * %C = current message number
 * %d = date and time of message (using strftime)
 * %f = entire from line
 * %F = like %n, unless from self
 * %i = message-id
 * %l = number of lines in the message
 * %L = like %F, except `lists' are displayed first
 * %m = number of messages in the mailbox
 * %n = name of author
 * %N = score
 * %s = subject
 * %S = short message status (e.g., N/O/D/!/r/-)
 * %t = `to:' field (recipients)
 * %T = $to_chars
 * %u = user (login) name of author
 * %Z = status flags
 *
 * %>X = right justify rest of line and pad with character X
 * %|X = pad to end of line with character X
 *
 */

void
_mutt_make_string (char *dest, size_t destlen, char *s, HEADER *hdr, int flags)
{
  char buf[STRING], buf2[STRING], prefix[SHORT_STRING], fmt[SHORT_STRING];
  char *cp;
  int len, count;
  char *wptr = dest; /* pointer to current writing position */
  int wlen = 0;      /* how many characters written so far */
  int ch;            /* char to use as filler */
  int do_locales;

  destlen--; /* save room for the terminal null (\0) character */
  dest[destlen] = 0;
  while (*s && wlen < destlen)
  {
    if (*s == '%')
    {
      s++;
      if (*s == '%')
      {
	*wptr++ = '%';
	wlen++;
	s++;
	continue;
      }
      /* strip off the formatting commands */
      cp = prefix;
      count = 0;
      while (count < sizeof (prefix) && (*s == '-' || *s == '.' || isdigit (*s)))
      {
	*cp++ = *s++;
        count++;
      }
      *cp = 0;

      switch (*s)
      {
	case '>': /* right justify the rest of the line */
	  s++;
	  ch = *s++;
	  _mutt_make_string (buf, sizeof (buf), s, hdr, flags);
	  len = (COLS < destlen ? COLS : destlen) - wlen - strlen (buf);
	  while (len > 0 && wlen < destlen)
	  {
	    *wptr++ = ch;
	    len--;
	    wlen++;
	  }
	  /* skip over the rest of the string */
	  s += strlen (s);
	  break;

	case '|': /* pad to end of line */
	  s++;
	  ch = *s++;
	  if (destlen > COLS)
	    destlen = COLS;
	  for (; wlen < destlen; wlen++)
	    *wptr++ = ch;
	  break;

	case 'a':
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, hdr->env->from->mailbox);
	  break;

	case 'c':
	  mutt_pretty_size (buf2, sizeof (buf2), (long) hdr->content->length);
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  break;

	case 'C':
	  snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	  snprintf (buf, sizeof (buf), fmt, hdr->msgno + 1);
	  break;

	case 'd':
	case '{':
	case '[':
	case '(':

	  /* preprocess $date_format to handle %Z */
	  {
	    char *p = buf;

	    cp = (*s == 'd') ? DateFmt : (s + 1);
	    if (*cp == '!')
	    {
	      do_locales = 0;
	      cp++;
	    }
	    else
	      do_locales = 1;

	    len = sizeof (buf) - 1;
	    while (len > 0 && ((*s == 'd' && *cp) ||
			       (*s == '{' && *cp != '}') || 
			       (*s == '[' && *cp != ']') ||
			       (*s == '(' && *cp != ')')))
	    {
	      if (*cp == '%')
	      {
		cp++;
		if (*cp == 'Z' && *s != '[' && *s != '(')
		{
		  if (len >= 5)
		  {
		    sprintf (p, "%c%02d%02d", hdr->zoccident ? '-' : '+',
			     hdr->zhours, hdr->zminutes);
		    p += 5;
		    len -= 5;
		  }
		  else
		    break; /* not enough space left */
		}
		else
		{
		  if (len >= 2)
		  {
		    *p++ = '%';
		    *p++ = *cp;
		    len -= 2;
		  }
		  else
		    break; /* not enough space */
		}
		cp++;
	      }
	      else
	      {
		*p++ = *cp++;
		len--;
	      }
	    }
	    *p = 0;
	  }

	  if (do_locales)
	    setlocale (LC_TIME, Locale);

	  {
	    struct tm *tm; 

	    if (*s == '[')
	      tm = localtime (&hdr->date_sent);
	    else if (*s == '(')
	      tm = localtime (&hdr->received);
	    else
	    {
	      time_t T;

	      /* restore sender's time zone */
	      T = hdr->date_sent;
	      if (hdr->zoccident)
		T -= (hdr->zhours * 3600 + hdr->zminutes * 60);
	      else
		T += (hdr->zhours * 3600 + hdr->zminutes * 60);
	      tm = gmtime (&T);
	    }
	    
	    strftime (buf2, sizeof (buf2), buf, tm);
	  }

	  if (do_locales)
	    setlocale (LC_TIME, "C");

	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  if (len > 0 && *s != 'd')
	    s = cp;
	  break;

	case 'f':
	  buf2[0] = 0;
	  rfc822_write_address (buf2, sizeof (buf2), hdr->env->from);
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  break;

	case 'F':
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  make_from (hdr->env, buf2, sizeof (buf2), 0);
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  break;

	case 'i':
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, hdr->env->message_id ? hdr->env->message_id : "<no.id>");
	  break;

	case 'l':
	  snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	  snprintf (buf, sizeof (buf), fmt, (int) hdr->lines);
	  break;

	case 'L':
	  make_from (hdr->env, buf2, sizeof (buf2), 1);
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  break;

	case 'm':
	  snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	  snprintf (buf, sizeof (buf), fmt, Context->msgcount);
	  break;

	case 'n':
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, mutt_get_name (hdr->env->from));
	  break;

	case 'N':
	  snprintf (fmt, sizeof (fmt), "%%%sd", prefix);
	  snprintf (buf, sizeof (buf), fmt, hdr->score);
	  break;

	case 's':
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  if (flags & M_TREE)
	  {
	    if (flags & M_FORCESUBJ)
	    {
	      snprintf (buf2, sizeof (buf2), "%s%s", hdr->tree,
			hdr->env->subject ? hdr->env->subject : "");
	      snprintf (buf, sizeof (buf), fmt, buf2);
	    }
	    else
	      snprintf (buf, sizeof (buf), fmt, hdr->tree);
	  }
	  else
	  {
	    snprintf (buf, sizeof (buf), fmt,
		      hdr->env->subject ? hdr->env->subject : "");
	  }
	  break;

	case 'S':
	  if (hdr->deleted)
	    ch = 'D';
	  else if (hdr->tagged)
	    ch = '*';
	  else if (hdr->flagged)
	    ch = '!';
	  else if (hdr->replied)
	    ch = 'r';
	  else if (hdr->read && (Context->msgnotreadyet != hdr->msgno))
	    ch = '-';
	  else if (hdr->old)
	    ch = 'O';
	  else
	    ch = 'N';
	  snprintf (buf, sizeof (buf), "%c", ch);
	  break;

	case 't':
	  buf2[0] = 0;
	  if (!check_for_mailing_list (hdr->env->to, "To ", buf2, sizeof (buf2)) &&
	      !check_for_mailing_list (hdr->env->cc, "Cc ", buf2, sizeof (buf2)))
	  {
	    if (hdr->env->to)
	      snprintf (buf2, sizeof (buf2), "To %s", mutt_get_name (hdr->env->to));
	    else if (hdr->env->cc)
	      snprintf (buf2, sizeof (buf2), "Cc %s", mutt_get_name (hdr->env->cc));
	  }
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  break;

	case 'T':
	  snprintf (fmt, sizeof (fmt), "%%%sc", prefix);
	  snprintf (buf, sizeof (buf), fmt, Tochars[user_is_recipient (hdr->env)]);
	  break;

	case 'u':
	  if (hdr->env->from && hdr->env->from->mailbox)
	  {
	    strfcpy (buf2, hdr->env->from->mailbox, sizeof (buf));
	    if ((cp = strpbrk (buf2, "%@")))
	      *cp = 0;
	  }
	  else
	    buf2[0] = 0;
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  break;

	case 'Z':
	  if (hdr->mailcap)
	    ch = 'M';
	  else
	    ch = ' ';
	  snprintf (fmt, sizeof (fmt), "%%%ss", prefix);
	  snprintf (buf2, sizeof (buf2),
		    "%c%c%c",
                    (hdr->read && (Context->msgnotreadyet != hdr->msgno))
                     ? (hdr->replied ? 'r' : ' ') : (hdr->old ? 'O' : 'N'),
		    hdr->deleted ? 'D' : ch,
		    hdr->tagged ? '*' :
                     (hdr->flagged ? '!' :
                      Tochars[user_is_recipient (hdr->env)]));
	  snprintf (buf, sizeof (buf), fmt, buf2);
	  break;

	default:
	  snprintf (buf, sizeof (buf), "%%%s%c", prefix, *s);
	  break;
      }

      if ((len = strlen (buf)) + wlen > destlen)
      {
	if ((len = destlen - wlen) < 0)
	  len = 0;
      }
      memcpy (wptr, buf, len);
      wptr += len;
      wlen += len;
    }
    else if (*s == '\\')
    {
      s++;
      if (!*s)
	break;
      if (*s == 'n')
      {
	*wptr++ = '\n';
	wlen++;
      }
      else if (*s == 't')
      {
	*wptr++ = '\t';
	wlen++;
      }
      else
      {
	*wptr++ = *s;
	wlen++;
      }
    }
    else
    {
      *wptr++ = *s;
      wlen++;
    }
    s++;
  }
  *wptr = 0;

  if (flags & M_MAKEPRINT)
  {
    /* Make sure that the string is printable by changing all non-printable
       chars to dots, or spaces for non-printable whitespace */
    for (cp = dest ; *cp ; cp++)
      if (!IsPrint (*cp) &&
	  !((flags & M_TREE) && (*cp <= 8)))
	*cp = isspace ((unsigned char) *cp) ? ' ' : '.';
  }
}
