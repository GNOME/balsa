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
#include "mutt_regex.h"
#include "mx.h"
#include "init.h"
#include "mailbox.h"

#include <pwd.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/utsname.h>
#include <errno.h>
#include <sys/wait.h>

#define toggle_quadoption(opt) QuadOptions ^= (1 << (2 * opt))

void set_quadoption (int opt, int flag)
{
  QuadOptions &= ~(0x3 << (2 * opt)); /* first clear the bits */
  QuadOptions |= (flag & 0x3) << (2 * opt); /* now set them */
}

int quadoption (int opt)
{
  return ((QuadOptions >> (opt * 2)) & 0x3);
}

int query_quadoption (int opt, const char *prompt)
{
  int v = quadoption (opt);

  switch (v)
  {
    case M_YES:
    case M_NO:
      return (v);

    default:
      v = mutt_yesorno (prompt, (v == M_ASKYES));
      CLEARLINE (LINES - 1);
      return (v);
  }

  /* not reached */
}

/* given the variable ``s'', return the index into the rc_vars array which
 * matches, or -1 if the variable is not found.
 */
int mutt_option_index (char *s)
{
  int i;

  for (i=0; MuttVars[i].option; i++)
    if (strcmp (s, MuttVars[i].option) == 0)
      return (i);
  return (-1);
}

void mutt_add_to_list (LIST **list, const char *s)
{
  LIST *t, *last = NULL;
  char buf[SHORT_STRING];
  char expn[LONG_STRING];

  do
  {
    s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

    /* check to make sure the item is not already on this list */
    for (last = *list; last; last = last->next)
    {
      if (strcasecmp (buf, last->data) == 0)
      {
	/* already on the list, so just ignore it */
	last = NULL;
	break;
      }
      if (!last->next)
	break;
    }

    if (!*list || last)
    {
      t = (LIST *) safe_calloc (1, sizeof (LIST));
      t->data = safe_strdup (buf);

      if (last)
      {
	last->next = t;
	last = last->next;
      }
      else
	*list = last = t;
    }
  }
  while (s);
}

static void remove_from_list (LIST **l, const char *s)
{
  LIST *p, *last = NULL;
  char buf[SHORT_STRING];
  char expn[LONG_STRING];

  do
  {
    s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

    if (strcmp ("*", buf) == 0)
      mutt_free_list (l);    /* ``unCMD *'' means delete all current entries */
    else
    {
      p = *l;
      last = NULL;
      while (p)
      {
	if (strcasecmp (buf, p->data) == 0)
	{
	  safe_free ((void **) &p->data);
	  if (last)
	    last->next = p->next;
	  else
	    (*l) = p->next;
	  safe_free ((void **) &p);
	}
	else
	{
	  last = p;
	  p = p->next;
	}
      }
    }
  }
  while (s);
}

static int parse_unignore (const char *s, unsigned long data, char *err, size_t errlen)
{
  mutt_add_to_list (&UnIgnore, s);
  remove_from_list (&Ignore, s);
  return 0;
}

static int parse_ignore (const char *s, unsigned long data, char *err, size_t errlen)
{
  mutt_add_to_list (&Ignore, s);
  remove_from_list (&UnIgnore, s);
  return 0;
}

static int parse_list (const char *s, unsigned long data, char *err, size_t errlen)
{
  mutt_add_to_list ((LIST **) data, s);
  return 0;
}

static int
parse_unlist (const char *s, unsigned long data, char *err, size_t errlen)
{
  remove_from_list ((LIST **) data, s);
  return 0;
}

static int parse_unalias (const char *s, unsigned long data, char *err, size_t errlen)
{
  ALIAS *tmp, *last = NULL;
  char buf[SHORT_STRING];
  char expn[LONG_STRING];

  do
  {
    s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

    tmp = Aliases;
    for (tmp = Aliases; tmp; tmp = tmp->next)
    {
      if (strcasecmp (buf, tmp->name) == 0)
      {
	if (last)
	  last->next = tmp->next;
	else
	  Aliases = tmp->next;
	tmp->next = NULL;
	mutt_free_alias (&tmp);
	break;
      }
      last = tmp;
    }
  }
  while (s);
  return 0;
}

static int parse_alias (const char *s, unsigned long data, char *errmsg, size_t errlen)
{
  ALIAS *tmp = Aliases;
  ALIAS *last = NULL;
  const char *p;
  size_t len;
  char buf[HUGE_STRING];
  char expn[LONG_STRING];

  if ((p = strpbrk (s, " \t")) == NULL)
  {
    strfcpy (errmsg, "alias has no address", errlen);
    return (-1);
  }

  len = p - s;

  /* check to see if an alias with this name already exists */
  for (; tmp; tmp = tmp->next)
  {
    if (strncasecmp (tmp->name, s, len) == 0 && *(tmp->name + len) == 0)
      break;
    last = tmp;
  }

  if (!tmp)
  {
    /* create a new alias */
    tmp = (ALIAS *) safe_calloc (1, sizeof (ALIAS));
    tmp->name = safe_malloc (len + 1);
    memcpy (tmp->name, s, len);
    tmp->name[len] = 0;
  }
  else
  {
    /* override the previous value */
    rfc822_free_address (&tmp->addr);
  }

  mutt_extract_token (buf, sizeof (buf), p, expn, sizeof (expn), M_QUOTE | M_SPACE);

  tmp->addr = rfc822_parse_adrlist (tmp->addr, buf);

  if (last)
    last->next = tmp;
  else
    Aliases = tmp;

  return 0;
}

static int parse_unmy_hdr (const char *s, unsigned long data, char *err, size_t errlen)
{
  LIST *last = NULL;
  LIST *tmp = UserHeader;
  LIST *ptr;
  char buf[SHORT_STRING];
  char expn[LONG_STRING];
  size_t l;

  do
  {
    s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

    if (strcmp ("*", buf) == 0)
      mutt_free_list (&UserHeader);
    else
    {
      tmp = UserHeader;
      last = NULL;

      l = strlen (buf);
      if (buf[l - 1] == ':')
	l--;

      while (tmp)
      {
	if (strncasecmp (buf, tmp->data, l) == 0 && tmp->data[l] == ':')
	{
	  ptr = tmp;
	  if (last)
	    last->next = tmp->next;
	  else
	    UserHeader = tmp->next;
	  tmp = tmp->next;
	  ptr->next = NULL;
	  mutt_free_list (&ptr);
	}
	else
	{
	  last = tmp;
	  tmp = tmp->next;
	}
      }
    }
  }
  while (s);
  return 0;
}

static int parse_my_hdr (const char *s, unsigned long data, char *errmsg, size_t errlen)
{
  LIST *tmp;
  size_t keylen;
  char *p;
  char buf[LONG_STRING];
  char expn[LONG_STRING];

  mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), M_SPACE | M_QUOTE);

  if ((p = strpbrk (buf, ": \t")) == NULL || *p != ':')
  {
    strfcpy (errmsg, "invalid header field", errlen);
    return (-1);
  }
  keylen = p - buf + 1;

  p++;
  SKIPWS (p);
  if (!*p)
  {
    snprintf (errmsg, errlen, "ignoring empty header field: %s", s);
    return (-1);
  }

  if (UserHeader)
  {
    for (tmp = UserHeader; ; tmp = tmp->next)
    {
      /* see if there is already a field by this name */
      if (strncasecmp (buf, tmp->data, keylen) == 0)
      {
	/* replace the old value */
	safe_free ((void **) &tmp->data);
	tmp->data = safe_strdup (buf);
	return 0;
      }

      if (!tmp->next)
	break;
    }

    tmp->next = mutt_new_list ();
    tmp = tmp->next;
  }
  else
  {
    tmp = mutt_new_list ();
    UserHeader = tmp;
  }

  tmp->data = safe_strdup (buf);

  return 0;
}

static int
parse_sort (short *val,
	    const char *s,
	    const struct mapping_t *map,
	    char *err,
	    size_t errlen)
{
  int i, flags = 0;

  if (strncmp ("reverse-", s, 8) == 0)
  {
    s += 8;
    flags = SORT_REVERSE;
  }
  
  if (strncmp ("last-", s, 5) == 0)
  {
    s += 5;
    flags |= SORT_LAST;
  }

  if ((i = mutt_getvaluebyname (s, map)) == -1)
  {
    snprintf (err, errlen, "%s: unknown sorting method", s);
    return (-1);
  }

  *val = i | flags;

  return 0;
}

/* flags
 *	M_EQUAL		'=' is a terminator
 *	M_CONDENSE	condense ^(char) runs
 *	M_SPACE		don't treat whitespace as a terminator
 *	M_QUOTE		don't interpret quotes
 *	M_PATTERN	!)~| are terminators (used for patterns)
 *	M_COMMENT	don't reap comments
 *	M_NULL		don't return NULL when \0 is reached
 */

const char *
mutt_extract_token (char *d, size_t dlen,	/* destination */
		     const char *s,		/* source */
		     char *expn, size_t expnlen,/* buf for backtic expn */
		     int flags)			/* parser flags */
{
  char qc = 0; /* quote char */
  char buf[HUGE_STRING];
  char *pc;
  const char *ps;
  size_t len;

  *d = 0;

  if (!s || (*s == '#' && (flags & M_COMMENT) == 0))
    return NULL; /* nothing to do */

  SKIPWS (s);

  dlen--; /* save room for the terminal \0 */

  while (*s)
  {
    if (!qc)
    {
      if ((ISSPACE (*s) && (flags & M_SPACE) == 0) ||
	  ((flags & M_COMMENT) == 0 && *s == '#') ||
	  ((flags & M_EQUAL) && *s == '=') ||
	  ((flags & M_PATTERN) && strchr ("~!|)", *s)))
	break;
    }

    if (*s == qc)
    {
      qc = 0; /* end quote */
      s++;
    }
    else if (!qc && (flags & M_QUOTE) == 0 && (*s == '\'' || *s == '"'))
    {
      qc = *s; /* begin quote */
      s++;
    }
    else if (*s == '\\' && qc != '\'')
    {
      if (! *++s)
	break;
      switch (*s)
      {
	case 'c': /* control char: \cX */
	case 'C':
	  if (*++s)
	  {
	    if (dlen)
	    {
	      *d++ = (toupper (*s) - '@') & 0x7f;
	      dlen--;
	    }
	  }
	  break;
	case 'r':
	  if (dlen)
	  {
	    *d++ = '\r';
	    dlen--;
	  }
	  break;
	case 'n':
	  if (dlen)
	  {
	    *d++ = '\n';
	    dlen--;
	  }
	  break;
	case 't':
	  if (dlen)
	  {
	    *d++ = '\t';
	    dlen--;
	  }
	  break;
	case 'f': /* formfeed */
	  if (dlen)
	  {
	    *d++ = '\f';
	    dlen--;
	  }
	  break;
	case 'e': /* escape */
	  if (dlen)
	  {
	    *d++ = '\033';
	    dlen--;
	  }
	  break;
	default:
	  if (dlen)
	  {
	    if (isdigit (s[0]) &&
		s[1] && isdigit (s[1]) &&
		s[2] && isdigit (s[2]))
	    {
	      /* octal number */
	      *d++ = s[2] + (s[1] << 3) + (s[0] << 6) - 3504;
	      s += 2;
	    }
	    else
	      *d++ = *s;
	    dlen--;
	  }
	  break;
      }
      s++;
    }
    else if (*s == '^' && (flags & M_CONDENSE))
    {
      if (! *++s)
	break;

      if (*s == '^')
      {
	if (dlen)
	{
	  *d++ = *s;
	  dlen--;
	}
      }
      else if (*s == '[')
      {
	if (dlen)
	{
	  *d++ = '\033';
	  dlen--;
	}
      }
      else if (isalpha (*s))
      {
	if (dlen)
	{
	  *d++ = toupper (*s) - '@';
	  dlen--;
	}
      }
      else
      {
	dprint (1, (debugfile, "mutt_extract_token(): unknown escape sequence: ^%c\n", *s));
	if (dlen > 1)
	{
	  *d++ = '^';
	  *d++ = *s;
	  dlen -= 2;
	}
      }
      s++;
    }
    else if (*s == '`' && (!qc || qc == '\"') && expn)
    {
      FILE *fp;
      pid_t thepid;
      char rest[LONG_STRING];

      /* locate the end quote */
      s++;
      ps = s;
      do
      {
	if ((ps = strpbrk (ps, "\\`")))
	{
	  /* skip any quoted chars */
	  if (*ps == '\\')
	  {
	    ps++;
	    if (*ps)
	      ps++;
	  }
	}
      }
      while (ps && *ps && *ps != '`');
      if (!ps || !*ps)
      {
	/* snprintf (err, errlen, "mismatched backtics: %s", s); */
	dprint (1, (debugfile, "mismatched backtics: %s", s));
	return NULL;
      }

      mutt_substrcpy (buf, s, ps, sizeof (buf));

      if ((thepid = mutt_create_filter (buf, NULL, &fp, NULL)) < 0)
      {
	/* snprintf (err, errlen ,"unable to fork command: %s", buf); */
	dprint (1, (debugfile, "unable to fork command: %s", buf));
	return NULL;
      }

      fgets (buf, sizeof (buf) - 1, fp);
      buf[sizeof (buf) - 1] = 0; /* just to be safe */
      len = strlen (buf) - 1;
      buf[len] = 0; /* kill the newline */
      fclose (fp);
      mutt_wait_filter (thepid);

      /* make a copy of the rest of the input string */
      strfcpy (rest, ps + 1, sizeof (rest));

      /* copy the expanded version */
      if (len > expnlen - 1)
	len = expnlen - 1;
      memcpy (expn, buf, len);

      /* append the rest of the unexpanded string */
      strfcpy (expn + len, rest, expnlen - len);

      s = expn;
    }
    else if ((!qc || qc == '\"') && *s == '$' && (*(s + 1) == '{' || isalpha (*(s + 1))))
    {
      s++;
      if (*s == '{')
      {
	s++;
	if ((ps = strchr (s, '}')))
	{
	  mutt_substrcpy (buf, s, ps, sizeof (buf));
	  s = ps;
	}
      }
      else
      {
	for (pc = buf; ; s++)
	{
	  *pc++ = *s;
	  if (!isalpha (*(s + 1)) && *(s + 1) != '_')
	    break;
	}
	*pc = 0;
      }
      if ((pc = getenv (buf)) == NULL)
	if (Context && strcmp ("thisfolder", buf) == 0)
	  pc = Context->path;
      if (pc)
      {
	len = strlen (pc);
	if (len > dlen)
	  len = dlen;
	memcpy (d, pc, len);
	dlen -= len;
	d += len;
      }
      else
      {
	dprint (1, (debugfile, "mutt_extract_token: %s: unable to find in environment\n", buf));
      }
      s++;
    }
    else
    {
      if (dlen)
      {
	*d++ = *s;
	dlen--;
      }
      s++;
    }
  }

  *d = 0;

  if (qc && *s == qc)
    s++;

  SKIPWS (s);

  if ((!*s && (flags & M_NULL) == 0) || ((flags & M_COMMENT) == 0 && *s == '#'))
    s = NULL;

  return (s);
}

static int
parse_set (const char *s, unsigned long data, char *errmsg, size_t errlen)
{
  char keyword[STRING];
  char tmp[LONG_STRING];
  char expn[LONG_STRING];
  char *pc;
  int idx;
  int query;
  int unset;
  int inv;

  while (s && *s != '#')
  {
    /* reset state variables */
    query = 0;
    unset = (data) & M_SET_UNSET;
    inv = (data) & M_SET_INV;

    if (*s == '?')
    {
      query = 1;
      s++;
    }
    else if (strncmp ("no", s, 2) == 0)
    {
      s += 2;
      unset = !unset;
    }
    else if (strncmp ("inv", s, 3) == 0)
    {
      s += 3;
      inv = !inv;
    }

    /* get the variable name */
    s = mutt_extract_token (keyword, sizeof (keyword), s, expn, sizeof (expn), M_EQUAL);

    if ((idx = mutt_option_index (keyword)) == -1)
    {
      snprintf (errmsg, errlen, "%s: unknown variable", keyword);
      return (-1);
    }

    if (DTYPE(MuttVars[idx].type) == DT_BOOL)
    {
      if (s && *s == '=')
      {
	snprintf (errmsg, errlen, "%s is a boolean var!", keyword);
	return (-1);
      }

      if (query)
      {
	snprintf (errmsg, errlen, "%s is %sset", keyword,
		  option (MuttVars[idx].data) ? "" : "un");
	return 0;
      }

      if (unset)
	unset_option (MuttVars[idx].data);
      else if (inv)
	toggle_option (MuttVars[idx].data);
      else
	set_option (MuttVars[idx].data);
    }
    else if (DTYPE(MuttVars[idx].type) == DT_STR || DTYPE(MuttVars[idx].type) == DT_PATH)
    {
      char *pc = (char *) MuttVars[idx].data;

      if (query || !s)
      {
	/* user requested the value of this variable */
	snprintf (errmsg, errlen, "%s=\"%s\"", MuttVars[idx].option, pc);
	return 0;
      }

      if (*s != '=')
      {
	strfcpy (errmsg, "missing =", errlen);
	return (-1);
      }

      s++;

      /* copy the value of the string */
      s = mutt_extract_token (pc, MuttVars[idx].size, s, expn, sizeof (expn), 0);

      if (MuttVars[idx].type == DT_PATH)
	mutt_expand_path (pc, MuttVars[idx].size);
      else if (strcmp ("strict_threads", MuttVars[idx].option) == 0)
	set_option (OPTNEEDRESORT);
    }
    else if (DTYPE(MuttVars[idx].type) == DT_RX)
    {
      REGEXP *ptr = (REGEXP *) MuttVars[idx].data;
      regex_t *rx;
      int err, flags = 0;

      if (query || !s)
      {
	/* user requested the value of this variable */
	snprintf (errmsg, errlen, "%s=\"%s\"", MuttVars[idx].option, ptr->pattern);
	return 0;
      }

      if (*s != '=')
      {
	strfcpy (errmsg, "missing =", errlen);
	return (-1);
      }

      s++;

      /* copy the value of the string */
      s = mutt_extract_token (tmp, sizeof (tmp), s, expn, sizeof (expn), 0);

      rx = (regex_t *) safe_malloc (sizeof (regex_t));
      if (!ptr->pattern || strcmp (ptr->pattern, tmp) != 0)
      {
	/* $alternates is case-insensitive, * $mask is case-sensitive */
	if (strcmp (MuttVars[idx].option, "alternates") == 0)
	  flags |= REG_ICASE;
	else if (strcmp (MuttVars[idx].option, "mask") != 0)
	  flags |= mutt_which_case (tmp);
	
	if ((err = REGCOMP (rx, tmp, flags)) != 0)
	{
	  regerror (err, rx, errmsg, errlen);
	  regfree (rx);
	  safe_free ((void **) &rx);
	  return (-1);
	}

	/* get here only if everything went smootly */
	if (ptr->pattern)
	{
	  safe_free ((void **) &ptr->pattern);
	  regfree ((regex_t *) ptr->rx);
	  safe_free ((void **) &ptr->rx);
	}

	ptr->pattern = safe_strdup (tmp);
	ptr->rx = rx;

	/* $reply_regexp requires special treatment */
	if (Context && Context->msgcount &&
	    strcmp (MuttVars[idx].option, "reply_regexp") == 0)
	{
	  regmatch_t pmatch[1];
	  int i;
	  
#define CUR_ENV Context->hdrs[i]->env
	  for (i = 0; i < Context->msgcount; i++)
	  {
	    if (CUR_ENV && CUR_ENV->subject)
	    {
	      CUR_ENV->real_subj = (regexec (ReplyRegexp.rx,
				    CUR_ENV->subject, 1, pmatch, 0)) ?
				    CUR_ENV->subject : 
				    CUR_ENV->subject + pmatch[0].rm_eo;
	    }
	  }
#undef CUR_ENV
	  if ((!option (OPTSTRICTTHREADS) && Sort == SORT_THREADS) || Sort == SORT_SUBJECT)
	    set_option (OPTNEEDRESORT);
	}
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_MAGIC)
    {
      if (query || !s)
      {
	switch (DefaultMagic)
	{
	  case M_MBOX:
	    pc = "mbox";
	    break;
	  case M_MMDF:
	    pc = "MMDF";
	    break;
	  case M_MH:
	    pc = "MH";
	    break;
	  case M_MAILDIR:
	    pc = "Maildir";
	    break;
	  default:
	    pc = "unknown";
	    break;
	}
	/* user requested the value of this variable */
	snprintf (errmsg, errlen, "%s=\"%s\"", MuttVars[idx].option, pc);
	return 0;
      }

      if (*s != '=')
      {
	strfcpy (errmsg, "missing =", errlen);
	return (-1);
      }

      s++;

      /* copy the value of the string */
      s = mutt_extract_token (tmp, sizeof (tmp), s, expn, sizeof (expn), 0);
      if (mx_set_magic (tmp))
      {
	strfcpy (errmsg, "invalid mailbox type", errlen);
	return (-1);
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_NUM)
    {
      short *ptr = (short *) MuttVars[idx].data;

      if (query || !s)
      {
	snprintf (errmsg, errlen, "%s=%d", MuttVars[idx].option, *ptr);
	return 0;
      }

      if (*s != '=')
      {
	strfcpy (errmsg, "missing =", errlen);
	return (-1);
      }

      s++;

      s = mutt_extract_token (tmp, sizeof (tmp), s, expn, sizeof (expn), 0);
      *ptr = (short) atoi (tmp);

      /* these ones need a sanity check */
      if (strcmp (MuttVars[idx].option, "history") == 0)
      {
	if (*ptr < 0)
	  *ptr = 0;
	mutt_init_history ();
      }
      else if (strcmp (MuttVars[idx].option, "pager_index_lines") == 0)
      {
	if (*ptr < 0)
	  *ptr = 0;
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_QUAD)
    {
      if (query)
      {
	char *vals[] = { "no", "yes", "ask-no", "ask-yes" };

	snprintf (errmsg, errlen, "%s=%s", MuttVars[idx].option,
		  vals [ quadoption (MuttVars[idx].data) ]);
	return 0;
      }

      if (s && *s == '=')
      {
	s = mutt_extract_token (tmp, sizeof (tmp), s + 1, expn, sizeof (expn), 0);

	if (strcasecmp ("yes", tmp) == 0)
	  set_quadoption (MuttVars[idx].data, M_YES);
	else if (strcasecmp ("no", tmp) == 0)
	  set_quadoption (MuttVars[idx].data, M_NO);
	else if (strcasecmp ("ask-yes", tmp) == 0)
	  set_quadoption (MuttVars[idx].data, M_ASKYES);
	else if (strcasecmp ("ask-no", tmp) == 0)
	  set_quadoption (MuttVars[idx].data, M_ASKNO);
	else
	{
	  snprintf (errmsg, errlen, "%s: invalid value", tmp);
	  return (-1);
	}
      }
      else
      {
	if (inv)
	  toggle_quadoption (MuttVars[idx].data);
	else if (unset)
	  set_quadoption (MuttVars[idx].data, M_NO);
	else
	  set_quadoption (MuttVars[idx].data, M_YES);
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_SORT)
    {
      const struct mapping_t *map;

      switch (MuttVars[idx].type & DT_SUBTYPE_MASK)
      {
	case DT_SORT_ALIAS:
	  map = SortAliasMethods;
	  break;
	default:
	  map = SortMethods;
	  break;
      }

      if (query || !s)
      {
	pc = mutt_getnamebyvalue (*((short *) MuttVars[idx].data) & SORT_MASK,
				  map);

	snprintf (errmsg, errlen, "%s=%s%s%s", MuttVars[idx].option,
		  (*((short *) MuttVars[idx].data) & SORT_REVERSE) ? "reverse-" : "",
		  (*((short *) MuttVars[idx].data) & SORT_LAST) ? "last-" : "",
		  pc);
	return 0;
      }

      if (*s != '=')
      {
	snprintf (errmsg, errlen, "missing =");
	return (-1);
      }

      s = mutt_extract_token (tmp, sizeof (tmp), s + 1, expn, sizeof (expn), 0);

      if (parse_sort ((short *) MuttVars[idx].data, tmp, map, errmsg, errlen) == -1)
	return (-1);

      /* if we are in threaded mode, we need to resort all subthreads
	 when $sort_aux or $sort_re changes */
      if (!strcmp ("sort_aux", MuttVars[idx].option) ||
	  !strcmp ("sort_re", MuttVars[idx].option))
      {
	set_option (OPTSORTSUBTHREADS);
	set_option (OPTNEEDRESORT);
      }
      else if (!strcmp (MuttVars[idx].option, "sort"))
	set_option (OPTNEEDRESORT);
    }
    else
    {
      snprintf (errmsg, errlen, "%s: unknown type!", MuttVars[idx].option);
      return (-1);
    }

    if (MuttVars[idx].redraw & R_INDEX)
      set_option (OPTFORCEREDRAWINDEX);
    if (MuttVars[idx].redraw & R_PAGER)
      set_option (OPTFORCEREDRAWPAGER);
  }

  return 0;
}

/* reads the specified initialization file.  returns -1 if errors were found
 * so that we can pause to let the user know...
 */
static int source_rc (const char *rcfile)
{
  FILE *f;
  int line = 0, rc = 0;
  char errbuf[SHORT_STRING];
  char *linebuf = NULL;
  size_t buflen;

  if ((f = fopen (rcfile, "r")) == NULL)
  {
    fprintf (stderr, "%s: unable to open file\n", rcfile);
    return (-1);
  }

  while ((linebuf = mutt_read_line (linebuf, &buflen, f, &line)) != NULL)
  {
    if (mutt_parse_rc_line (linebuf, errbuf, sizeof (errbuf)) == -1)
    {
      if (!option (OPTNOCURSES))
	fprintf (stderr, "Error in %s, line %d: %s\n", rcfile, line, errbuf);
      rc = -1;
    }
  }

  safe_free ((void **) &linebuf);

  fclose (f);
  return (rc);
}

static int
parse_source (const char *s, unsigned long data, char *err, size_t errlen)
{
  char path[_POSIX_PATH_MAX];
  char expn[LONG_STRING];

  s = mutt_extract_token (path, sizeof (path), s, expn, sizeof (expn), 0);
  if (s)
  {
    strfcpy (err, "too many arguments to source command", errlen);
    return (-1);
  }
  mutt_expand_path (path, sizeof (path));
  if (access (path, R_OK) == -1)
  {
    strfcpy (err, strerror (errno), errlen);
    return (-1);
  }

  return (source_rc (path));
}

int mutt_parse_rc_line (const char *cmd, char *errmsg, int errlen)
{
  char tmp[HUGE_STRING];
  const char *q;
  char *p;
  int i;
  size_t len;

  *errmsg = 0;

  SKIPWS (cmd);
  if (*cmd == '#')
    return 0; /* rest of line is a comment */

  /* skip any trailing whitespace */
  q = cmd + strlen (cmd);
  while ((q > cmd) && ISSPACE (*(q - 1)))
    q--;
  if (q == cmd)
    return 0; /* nothing on this line */
  mutt_substrcpy (tmp, cmd, q, sizeof (tmp));

  p = tmp;
  for (i = 0; Commands[i].name; i++)
  {
    len = strlen (Commands[i].name);
    if (strncmp (p, Commands[i].name, len) == 0)
    {
      p += len;
      SKIPWS (p);
      if (!*p || *p == '#')
      {
	snprintf (errmsg, errlen, "%s: missing parameter", p);
	return (-1);
      }
      return (Commands[i].func (p, Commands[i].data, errmsg, errlen));
    }
  }

  /* unknown command */

  snprintf (errmsg, errlen, "%s: unknown command", p);
  return (-1);
}

char *mutt_getnamebyvalue (int val, const struct mapping_t *map)
{
  int i;

  for (i=0; map[i].name; i++)
    if (map[i].value == val)
      return (map[i].name);
  return NULL;
}

int mutt_getvaluebyname (const char *name, const struct mapping_t *map)
{
  int i;

  for (i=0; map[i].name; i++)
    if (strcmp (map[i].name, name) == 0)
      return (map[i].value);
  return (-1);
}

#ifdef DEBUG
static void start_debug (void)
{
  time_t t;
  int i;
  char buf[_POSIX_PATH_MAX];
  char buf2[_POSIX_PATH_MAX];

  /* rotate the old debug logs */
  for (i=3; i>=0; i--)
  {
    snprintf (buf, sizeof(buf), "%s/.muttdebug%d", Homedir, i);
    snprintf (buf2, sizeof(buf2), "%s/.muttdebug%d", Homedir, i+1);
    rename (buf, buf2);
  }
  if ((debugfile = safe_fopen(buf, "w")) != NULL)
  {
    t = time (0);
    fprintf (debugfile, "Mutt %s started at %s.\nDebugging at level %d.\n\n",
	     VERSION, asctime (localtime (&t)), debuglevel);
  }
}
#endif

static int mutt_execute_commands (LIST *p)
{
  char errmsg[SHORT_STRING];

  for (; p; p = p->next)
  {
    if (mutt_parse_rc_line (p->data, errmsg, sizeof (errmsg)) != 0)
    {
      fprintf (stderr, "Error in command line: %s\n", errmsg);
      return (-1);
    }
  }
  return 0;
}

void mutt_init (int skip_sys_rc, LIST *commands)
{
  struct passwd *pw;
  struct utsname utsname;
  char buffer[STRING];
  char *p;
  int default_rc = 0;
  int need_pause = 0;

  /* Get some information about the user */
  if ((pw = getpwuid (getuid ())) == NULL)
  {
    mutt_error ("Out of memory!");
    sleep (1);
    mutt_exit (1);
  }
  strfcpy (Username, pw->pw_name, sizeof (Username));

  /* on one of the systems I use, getcwd() does not return the same prefix
   * as is listed in the passwd file
   */
  if ((p = getenv ("HOME")) != NULL)
    strfcpy (Homedir, p, sizeof (Homedir));
  else
    strfcpy (Homedir, pw->pw_dir, sizeof (Homedir));

  strfcpy (Realname, pw->pw_gecos, sizeof (Realname));
  if ((p = strchr (Realname, ','))) *p = 0;
  strfcpy (Shell, pw->pw_shell, sizeof (Shell));

#ifdef DEBUG
  /* Start up debugging mode if requested */
  if (debuglevel > 0)
    start_debug ();
#endif

  /* And about the host... */
  uname (&utsname);
  strfcpy (Hostname, utsname.nodename, sizeof (Hostname));

  /* some systems report the FQDN instead of just the hostname */
  if ((p = strchr (Hostname, '.')))
  {
    *p++ = 0;
    strfcpy (buffer, p, sizeof (buffer)); /* save the domain for below */
  }

#ifndef DOMAIN
#define DOMAIN buffer
  if (!p && getdnsdomainname (buffer, sizeof (buffer)) == -1)
  {
    Fqdn[0] = '@';
    Fqdn[1] = 0;
  }
  else
#endif /* DOMAIN */
  {
# ifdef HIDDEN_HOST
    strfcpy (Fqdn, DOMAIN, sizeof (Fqdn));
# else
    snprintf (Fqdn, sizeof (Fqdn), "%s.%s", Hostname, DOMAIN);
# endif /* HIDDEN_HOST */
  }

#ifndef LOCALES_HACK
  /* Do we have a locale definition? */
  if (((p = getenv ("LC_ALL")) != NULL && p[0]) ||
      ((p = getenv ("LANG")) != NULL && p[0]) ||
      ((p = getenv ("LC_CTYPE")) != NULL && p[0]))
    set_option (OPTLOCALES);
#endif

  /* Set some defaults */

  set_option (OPTALLOW8BIT);
  set_option (OPTATTACHSPLIT);
  set_option (OPTBEEP);
  set_option (OPTCHECKNEW);
  set_option (OPTCONFIRMCREATE);
  set_option (OPTCONFIRMAPPEND);
  set_option (OPTFCCATTACH);
  set_option (OPTHDRS);
  set_option (OPTHELP);
  set_option (OPTMARKERS);
  set_option (OPTMARKOLD);
  set_option (OPTPROMPTAFTER);
  set_option (OPTRESOLVE);
  set_option (OPTSAVEEMPTY);
  set_option (OPTSIGDASHES);
  set_option (OPTSORTRE);
  set_option (OPTSUSPEND);
  set_option (OPTUSEDOMAIN);
  set_option (OPTUSEFROM);
  set_option (OPTWAITKEY);
  set_option (OPTWEED);
  set_option (OPTWRAP);
  

  set_quadoption (OPT_USEMAILCAP, M_ASKYES);
  set_quadoption (OPT_INCLUDE, M_ASKYES);
  set_quadoption (OPT_RECALL, M_ASKYES);
  set_quadoption (OPT_PRINT, M_ASKNO);
  set_quadoption (OPT_POSTPONE, M_ASKYES);
  set_quadoption (OPT_DELETE, M_ASKYES);
  set_quadoption (OPT_REPLYTO, M_ASKYES);
  set_quadoption (OPT_COPY, M_YES);
  set_quadoption (OPT_ABORT, M_YES);
  set_quadoption (OPT_SUBJECT, M_ASKYES);

  if ((p = getenv ("MAIL")))
    strfcpy (Spoolfile, p, sizeof (Spoolfile));
  else
  {
#ifdef HOMESPOOL
    snprintf (Spoolfile, sizeof (Spoolfile), "%s/%s", Homedir, MAILPATH);
#else
    snprintf (Spoolfile, sizeof (Spoolfile), "%s/%s", MAILPATH, Username);
#endif
  }

  strfcpy (AliasFmt, "%2n %t %-10a   %r", sizeof (AliasFmt));
  strfcpy (Attribution, "On %d, %n wrote:", sizeof (Attribution));
  strfcpy (AttachSep, "\n", sizeof (AttachSep));
  strfcpy (Charset, "iso-8859-1", sizeof (Charset));
  strfcpy (DateFmt, "!%a, %b %d, %Y at %I:%M:%S%p %Z", sizeof (DateFmt));
  strfcpy (DecodeFmt, "[-- Decoded from message %i --]\n", sizeof (DecodeFmt));
  strfcpy (DefaultHook, "~f %s !~P | (~P ~C %s)", sizeof (DefaultHook));
  strfcpy (ForwFmt, "[%a: %s]", sizeof (ForwFmt));
  strfcpy (HdrFmt, "%4C %Z %{%b %d} %-15.15L (%4l) %s", sizeof (HdrFmt));
  strfcpy (InReplyTo, "%i; from %n on %{!%a, %b %d, %Y at %I:%M:%S%p %Z}", sizeof (InReplyTo));
  snprintf (Inbox, sizeof (Inbox), "%s/mbox", Homedir);

#ifdef ISPELL
  strfcpy (Ispell, ISPELL, sizeof (Ispell));
#else
  strfcpy (Ispell, "ispell", sizeof (Ispell));
#endif /* ISPELL */
  strfcpy (Locale, "C", sizeof (Locale));  

  if ((p = getenv ("MAILCAPS")))
    strfcpy (MailcapPath, p, sizeof (MailcapPath));
  else
  {
    /* Default search path from RFC1524 */
    strfcpy (MailcapPath, "~/.mailcap:" SHAREDIR "/mailcap:/etc/mailcap:/usr/etc/mailcap:/usr/local/etc/mailcap", sizeof (MailcapPath));
  }

  snprintf (Maildir, sizeof (Maildir), "%s/Mail", Homedir);
  strfcpy (MsgFmt, "%s", sizeof (MsgFmt));
  strfcpy (Pager, "builtin", sizeof (Pager));  
  strfcpy (PagerFmt, "-%S- %C/%m: %-20.20n   %s", sizeof (PagerFmt));
  strfcpy (PipeSep, "\n", sizeof (PipeSep));
  snprintf (Postponed, sizeof (Postponed), "%s/postponed", Homedir);
  strfcpy (Prefix, "> ", sizeof (Prefix));
  strfcpy (PrintCmd, "lpr", sizeof (PrintCmd));
  snprintf (Sendmail, sizeof (Sendmail), "%s -oi -oem -t", SENDMAIL);
  snprintf (SendmailBounce, sizeof (Sendmail), "%s -oi -oem", SENDMAIL);
  snprintf (Signature, sizeof (Signature), "%s/.signature", Homedir);
  strfcpy (SimpleSearch, "~f %s | ~s %s", sizeof (SimpleSearch));
  strfcpy (StChars, "-*%", sizeof (StChars));
  strfcpy (Status, "-%r-Mutt: %f [Msgs:%?M?%M/?%m%?n? New:%n?%?o? Old:%o?%?d? Del:%d?%?F? Flag:%F?%?t? Tag:%t?%?p? Post:%p?%?b? Inc:%b?%?l? %l?]---(%s/%S)-%>-(%P)---", sizeof (Status));

  if ((p = getenv ("TMPDIR")) != NULL)
    strfcpy (Tempdir, p, sizeof (Tempdir));
  else
    strfcpy (Tempdir, "/tmp", sizeof (Tempdir));

  strfcpy (Tochars, " +TCF", sizeof (Tochars));
#ifdef USE_POP
  strfcpy (PopUser, Username, sizeof (PopUser));
#endif

  Alternates.rx = NULL;
  
  QuoteRegexp.pattern = safe_strdup ("^([ \t]*[|>:}#])+");
  QuoteRegexp.rx = safe_malloc (sizeof (regex_t));
  REGCOMP (QuoteRegexp.rx, QuoteRegexp.pattern, 
	   mutt_which_case (QuoteRegexp.pattern));

  Mask.pattern = safe_strdup ("^(\\.\\.$|[^.])");
  Mask.rx = safe_malloc (sizeof (regex_t));
  REGCOMP (Mask.rx, Mask.pattern, REG_NOSUB);

  ReplyRegexp.pattern = safe_strdup ("^(re([\\[0-9\\]+])*|aw):[ \t]*");
  ReplyRegexp.rx = safe_malloc (sizeof (regex_t));
  REGCOMP (ReplyRegexp.rx, ReplyRegexp.pattern,
	   mutt_which_case (ReplyRegexp.pattern));

  if ((p = getenv ("EDITOR")) != NULL)
    strfcpy (Editor, p, sizeof (Editor));
  else
    strfcpy (Editor, "vi", sizeof (Editor));

  if ((p = getenv ("VISUAL")) != NULL)
    strfcpy (Visual, p, sizeof (Visual));
  else
    strfcpy (Visual, Editor, sizeof (Visual));

  if ((p = getenv ("REPLYTO")) != NULL)
  {
    char error[SHORT_STRING];

    snprintf (buffer, sizeof (buffer), "Reply-To: %s", p);
    parse_my_hdr (buffer, 0, error, sizeof (error));
  }

  mutt_init_history ();

  if (!Muttrc[0])
  {
    snprintf (Muttrc, sizeof (Muttrc), "%s/.muttrc-%s", Homedir, VERSION);
    if (access (Muttrc, F_OK) == -1)
      snprintf (Muttrc, sizeof (Muttrc), "%s/.muttrc", Homedir);
    default_rc = 1;
  }
  else
    mutt_expand_path (Muttrc, sizeof (Muttrc));
  strfcpy (AliasFile, Muttrc, sizeof (AliasFile));

  /* Process the global rc file if it exists and the user hasn't explicity
     requested not to via "-n".  */
  if (!skip_sys_rc)
  {
    snprintf (buffer, sizeof (buffer), "%s/Muttrc-%s", SHAREDIR, VERSION);
    if (access (buffer, F_OK) == -1)
      snprintf (buffer, sizeof (buffer), "%s/Muttrc", SHAREDIR);
    if (access (buffer, F_OK) != -1)
    {
      if (source_rc (buffer) != 0)
	need_pause = 1;
    }
  }

  /* Read the user's initialization file.  */
  if (access (Muttrc, F_OK) != -1)
  {
    if (!option (OPTNOCURSES))
      endwin ();
    if (source_rc (Muttrc) != 0)
      need_pause = 1;
  }
  else if (!default_rc)
  {
    /* file specified by -F does not exist */
    snprintf (buffer, sizeof (buffer), "%s: %s", Muttrc, strerror (errno));
    mutt_endwin (buffer);
    exit (1);
  }
  
  if (mutt_execute_commands (commands) != 0)
    need_pause = 1;
  
  if (need_pause && !option(OPTNOCURSES))
  {
    if (mutt_any_key_to_continue (NULL) == -1)
      mutt_exit(1);
  }
}
