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
#include "mutt_regex.h"
#include "mapping.h"
#include "keymap.h"
#include "mailbox.h"
#include "pattern.h"
#include "copy.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>






static const char *eat_regexp (pattern_t *pat, const char *s, char *, size_t);
static const char *eat_date (pattern_t *pat, const char *s, char *, size_t);
static const char *eat_range (pattern_t *pat, const char *s, char *, size_t);

struct pattern_flags
{
  int tag;	/* character used to represent this op */
  int op;	/* operation to perform */
  int class;
  const char *(*eat_arg) (pattern_t *, const char *, char *, size_t);
}
Flags[] =
{
  { 'A', M_ALL,			0,		NULL },
  { 'b', M_BODY,		M_FULL_MSG,	eat_regexp },
  { 'c', M_CC,			0,		eat_regexp },
  { 'C', M_RECIPIENT,		0,		eat_regexp },
  { 'd', M_DATE,		0,		eat_date },
  { 'D', M_DELETED,		0,		NULL },
  { 'e', M_SENDER,		0,		eat_regexp },
  { 'E', M_EXPIRED,		0,		NULL },
  { 'f', M_FROM,		0,		eat_regexp },
  { 'F', M_FLAG,		0,		NULL },
  { 'h', M_HEADER,		M_FULL_MSG,	eat_regexp },
  { 'i', M_ID,			0,		eat_regexp },
  { 'L', M_ADDRESS,		0,		eat_regexp },
  { 'l', M_LIST,		0,		NULL },
  { 'm', M_MESSAGE,		0,		eat_range },
  { 'n', M_SCORE,		0,		eat_range },
  { 'N', M_NEW,			0,		NULL },
  { 'O', M_OLD,			0,		NULL },
  { 'p', M_PERSONAL_RECIP,	0,		NULL },
  { 'P', M_PERSONAL_FROM,	0,		NULL },
  { 'Q', M_REPLIED,		0,		NULL },
  { 'R', M_READ,		0,		NULL },
  { 'r', M_DATE_RECEIVED,	0,		eat_date },
  { 's', M_SUBJECT,		0,		eat_regexp },
  { 'S', M_SUPERSEDED,		0,		NULL },
  { 'T', M_TAG,			0,		NULL },
  { 't', M_TO,			0,		eat_regexp },
  { 'U', M_UNREAD,		0,		NULL },
  { 'x', M_REFERENCE,		0,		eat_regexp },
  { 0 }
};

static pattern_t *SearchPattern = NULL; /* current search pattern */
static char LastSearch[STRING] = { 0 };	/* last pattern searched for */
static char LastSearchExpn[LONG_STRING] = { 0 }; /* expanded version of
						    LastSearch */

#define M_MAXRANGE -1

int mutt_getvaluebychar (char ch, struct mapping_t *table)
{
  int i;

  for (i = 0; table[i].name; i++)
  {
    if (ch == table[i].name[0])
      return table[i].value;
  }

  return (-1);
}

/* if no uppercase letters are given, do a case-insensitive search */
int mutt_which_case (const char *s)
{
  while (*s)
  {
    if (isalpha (*s) && isupper (*s))
      return 0; /* case-sensitive */
    s++;
  }
  return REG_ICASE; /* case-insensitive */
}

static int
msg_search (regex_t *rx, char *buf, size_t blen, int is_hdr, int msgno)
{
  char tempfile[_POSIX_PATH_MAX];
  MESSAGE *msg = NULL;
  STATE s;
  struct stat st;
  FILE *fp = NULL;
  long lng;
  int match = 0;

  if ((msg = mx_open_message (Context, msgno)) != NULL)
  {
    if (option (OPTTHOROUGHSRC))
    {
      /* decode the header / body */
      memset (&s, 0, sizeof (s));
      s.fpin = msg->fp;
      mutt_mktemp (tempfile);
      if ((s.fpout = safe_fopen (tempfile, "w+")) == NULL)
      {
	mutt_perror (tempfile);
	return (0);
      }

      if (is_hdr)
	mutt_copy_header (msg->fp, Context->hdrs[msgno], s.fpout, CH_FROM | CH_DECODE, NULL);
      else
      {
	mutt_parse_mime_message (Context, Context->hdrs[msgno]);
	fseek (msg->fp, Context->hdrs[msgno]->offset, 0);
	mutt_body_handler (Context->hdrs[msgno]->content, &s);
      }

      fp = s.fpout;
      fflush (fp);
      fseek (fp, 0, 0);
      fstat (fileno (fp), &st);
      lng = (long) st.st_size;
    }
    else
    {
      /* raw header / body */
      fp = msg->fp;
      if (is_hdr)
      {
	fseek (fp, Context->hdrs[msgno]->offset, 0);
	lng = Context->hdrs[msgno]->content->offset - Context->hdrs[msgno]->offset;
      }
      else
      {
	fseek (msg->fp, Context->hdrs[msgno]->content->offset, 0);
	lng = Context->hdrs[msgno]->content->length;
      }
    }

    /* search the file "fp" */
    while (lng > 0)
    {
      if (fgets (buf, blen - 1, fp) == NULL)
	break; /* don't loop forever */
      if (regexec (rx, buf, 0, NULL, 0) == 0)
      {
	match = 1;
	break;
      }
      lng -= strlen (buf);
    }
    
    mx_close_message (&msg);

    if (option (OPTTHOROUGHSRC))
    {
      fclose (fp);
      unlink (tempfile);
    }
  }

  return match;
}

static const char *
eat_regexp (pattern_t *pat, const char *s, char *err, size_t errlen)
{
  char buf[SHORT_STRING];
  const char *ps;
  int r;

  ps = mutt_extract_token (buf, sizeof (buf), s, NULL, 0,
			   M_PATTERN | M_COMMENT | M_NULL);
  
  pat->rx = safe_malloc (sizeof (regex_t));
  if ((r = REGCOMP (pat->rx, buf, REG_NEWLINE | REG_NOSUB | mutt_which_case (buf))) != 0)
  {
    regerror (r, pat->rx, err, errlen);
    regfree (pat->rx);
    safe_free ((void **) &pat->rx);
    return NULL;
  }

  return ps;
}

static const char *
eat_range (pattern_t *pat, const char *s, char *err, size_t errlen)
{
  const char *p;
  char *tmp;

  if (*s != '-')
  {
    /* range minimum */
    pat->min = strtol (s, &tmp, 0);
    p = tmp;
  }
  else
  {
    s++;
    p = s;
  }

  if (*p != '-')
  {
    pat->max = pat->min;
    return p;
  }
  p++;

  if (isdigit (*p))
  {
    /* range max */
    pat->max = strtol (p, &tmp, 0);
    p = tmp;
  }
  else
    pat->max = M_MAXRANGE;

  return p;
}

static const char *
getDate (const char *s, struct tm *t, char *err, size_t errlen)
{
  char *p;
  time_t now = time (NULL);
  struct tm *tm = localtime (&now);

  t->tm_mday = strtol (s, &p, 0);
  if (t->tm_mday < 1 || t->tm_mday > 31)
  {
    snprintf (err, errlen, "Invalid day of month: %s", s);
    return NULL;
  }
  if (*p != '/')
  {
    /* fill in today's month and year */
    t->tm_mon = tm->tm_mon;
    t->tm_year = tm->tm_year;
    return p;
  }
  p++;
  t->tm_mon = strtol (p, &p, 0) - 1;
  if (t->tm_mon < 0 || t->tm_mon > 11)
  {
    snprintf (err, errlen, "Invalid month: %s", p);
    return NULL;
  }
  if (*p != '/')
  {
    t->tm_year = tm->tm_year;
    return p;
  }
  p++;
  t->tm_year = strtol (p, &p, 0);
  if (t->tm_year < 70) /* year 2000+ */
    t->tm_year += 100;
  else if (t->tm_year > 1900)
    t->tm_year -= 1900;
  return p;
}

/* Ny	years
   Nm	months
   Nw	weeks
   Nd	days */
static const char *get_offset (struct tm *tm, const char *s)
{
  char *ps;
  int offset = strtol (s, &ps, 0);

  switch (*ps)
  {
    case 'y':
      tm->tm_year -= offset;
      break;
    case 'm':
      tm->tm_mon -= offset;
      break;
    case 'w':
      tm->tm_mday -= 7 * offset;
      break;
    case 'd':
      tm->tm_mday -= offset;
      break;
  }
  mutt_normalize_time (tm);
  return (ps + 1);
}

static const char *
eat_date (pattern_t *pat, const char *s, char *err, size_t errlen)
{
  char buffer[SHORT_STRING];
  struct tm min, max;

  s = mutt_extract_token (buffer, sizeof (buffer), s, NULL, 0,
			  M_NULL | M_COMMENT | M_PATTERN);

  memset (&min, 0, sizeof (min));
  /* the `0' time is Jan 1, 1970 UTC, so in order to prevent a negative time
     when doing timezone conversion, we use Jan 2, 1970 UTC as the base
     here */
  min.tm_mday = 2;
  min.tm_year = 70;

  memset (&max, 0, sizeof (max));

  /* Arbitrary year in the future.  Don't set this too high
     or mutt_mktime() returns something larger than will
     fit in a time_t on some systems */
  max.tm_year = 130;
  max.tm_mon = 11;
  max.tm_mday = 31;
  max.tm_hour = 23;
  max.tm_min = 59;
  max.tm_sec = 59;

  if (strchr ("<>=", buffer[0]))
  {
    /* offset from current time
       <3d	less than three days ago
       >3d	more than three days ago
       =3d	exactly three days ago */
    time_t now = time (NULL);
    struct tm *tm = localtime (&now);
    int exact = 0;

    if (buffer[0] == '<')
    {
      memcpy (&min, tm, sizeof (min));
      tm = &min;
    }
    else
    {
      memcpy (&max, tm, sizeof (max));
      tm = &max;

      if (buffer[0] == '=')
	exact++;
    }
    tm->tm_hour = 23;
    tm->tm_min = max.tm_sec = 59;

    get_offset (tm, buffer + 1);

    if (exact)
    {
      /* start at the beginning of the day in question */
      memcpy (&min, &max, sizeof (max));
      min.tm_hour = min.tm_sec = min.tm_min = 0;
    }
  }
  else
  {
    const char *pc = buffer;

    if (*pc != '-')
    {
      /* mininum date specified */
      if ((pc = getDate (pc, &min, err, errlen)) == NULL)
	return NULL;
    }

    if (*pc && *pc == '-')
    {
      /* max date */
      pc++; /* skip the `-' */
      SKIPWS (pc);
      if (*pc)
	if (!getDate (pc, &max, err, errlen))
	  return NULL;
    }
    else
    {
      /* search for messages on a specific day */
      max.tm_year = min.tm_year;
      max.tm_mon = min.tm_mon;
      max.tm_mday = min.tm_mday;
    }
  }

  pat->min = mutt_mktime (&min, 1);
  pat->max = mutt_mktime (&max, 1);

  return s;
}

static struct pattern_flags *lookup_tag (char tag)
{
  int i;

  for (i = 0; Flags[i].tag; i++)
    if (Flags[i].tag == tag)
      return (&Flags[i]);
  return NULL;
}

static const char *find_matching_paren (const char *s)
{
  int level = 1;

  for (; *s; s++)
  {
    if (*s == '(')
      level++;
    else if (*s == ')')
    {
      level--;
      if (!level)
	break;
    }
  }
  return s;
}

void mutt_pattern_free (pattern_t **pat)
{
  pattern_t *tmp;

  while (*pat)
  {
    tmp = *pat;
    *pat = (*pat)->next;

    if (tmp->rx)
    {
      regfree (tmp->rx);
      safe_free ((void **) &tmp->rx);
    }
    if (tmp->child)
      mutt_pattern_free (&tmp->child);
    safe_free ((void **) &tmp);
  }
}

pattern_t *mutt_pattern_comp (const char *s,
			      int flags,
			      char *err,
			      size_t errlen)
{
  pattern_t *curlist = NULL;
  pattern_t *tmp;
  pattern_t *last = NULL;
  int not = 0;
  int or = 0;
  int implicit = 1;	/* used to detect logical AND operator */
  struct pattern_flags *entry;
  const char *p;
  char buf[STRING];

  while (*s)
  {
    SKIPWS (s);
    switch (*s)
    {
      case '!':
	s++;
	not = !not;
	break;
      case '|':
	if (!or)
	{
	  if (!curlist)
	  {
	    snprintf (err, errlen, "error in pattern at: %s", s);
	    return NULL;
	  }
	  if (curlist->next)
	  {
	    /* A & B | C == (A & B) | C */
	    tmp = new_pattern ();
	    tmp->op = M_AND;
	    tmp->child = curlist;

	    curlist = tmp;
	    last = curlist;
	  }

	  or = 1;
	}
	s++;
	implicit = 0;
	not = 0;
	break;
      case '~':
	if (implicit && or)
	{
	  /* A | B & C == (A | B) & C */
	  tmp = new_pattern ();
	  tmp->op = M_OR;
	  tmp->child = curlist;
	  curlist = tmp;
	  last = tmp;
	  or = 0;
	}

	tmp = new_pattern ();
	tmp->not = not;
	not = 0;

	if (last)
	  last->next = tmp;
	else
	  curlist = tmp;
	last = tmp;

	s++; /* move past the ~ */
	if ((entry = lookup_tag (*s)) == NULL)
	{
	  snprintf (err, errlen, "%c: invalid command", *s);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	if (entry->class && (flags & entry->class) == 0)
	{
	  snprintf (err, errlen, "%c: not supported in this mode", *s);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	tmp->op = entry->op;

	s++; /* eat the operator and any optional whitespace */
	SKIPWS (s);

	if (entry->eat_arg)
	{
	  if (!*s)
	  {
	    snprintf (err, errlen, "missing parameter");
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	  if ((s = entry->eat_arg (tmp, s, err, errlen)) == NULL)
	  {
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	}
	implicit = 1;
	break;
      case '(':
	p = find_matching_paren (s + 1);
	if (*p != ')')
	{
	  snprintf (err, errlen, "mismatched parenthesis: %s", s);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	mutt_substrcpy (buf, s + 1, p, sizeof (buf));
	/* compile the sub-expression */
	if ((tmp = mutt_pattern_comp (buf, flags, err, errlen)) == NULL)
	{
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	if (last)
	  last->next = tmp;
	else
	  curlist = tmp;
	last = tmp;
	tmp->not = not;
	not = 0;
	s = p + 1; /* restore location */
	break;
      default:
	snprintf (err, errlen, "error in pattern at: %s", s);
	mutt_pattern_free (&curlist);
	return NULL;
    }
  }
  if (!curlist)
  {
    strfcpy (err, "empty pattern", errlen);
    return NULL;
  }
  if (curlist->next)
  {
    tmp = new_pattern ();
    tmp->op = or ? M_OR : M_AND;
    tmp->child = curlist;
    curlist = tmp;
  }
  return curlist;
}

static int
perform_and (pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *hdr)
{
  for (; pat; pat = pat->next)
    if (mutt_pattern_exec (pat, flags, ctx, hdr) <= 0)
      return 0;
  return 1;
}

static int
perform_or (struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *hdr)
{
  for (; pat; pat = pat->next)
    if (mutt_pattern_exec (pat, flags, ctx, hdr) > 0)
      return 1;
  return 0;
}

static int match_adrlist (regex_t *rx, int match_personal, ADDRESS *a)
{
  for (; a; a = a->next)
  {
    if ((a->mailbox && regexec (rx, a->mailbox, 0, NULL, 0) == 0) ||
	(match_personal && a->personal && regexec (rx, a->personal, 0, NULL, 0) == 0))
      return 1;
  }
  return 0;
}

static int match_reference (regex_t *rx, LIST *refs)
{
  for (; refs; refs = refs->next)
    if (regexec (rx, refs->data, 0, NULL, 0) == 0)
      return 1;
  return 0;
}

static int match_user (ADDRESS *p)
{
  for (; p; p = p->next)
    if (mutt_addr_is_user (p))
      return 1;
  return 0;
}

/* flags
   	M_MATCH_FULL_ADDRESS	match both personal and machine address */
int
mutt_pattern_exec (struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *h)
{
  char buf[STRING];

  switch (pat->op)
  {
    case M_AND:
      return (pat->not ^ (perform_and (pat->child, flags, ctx, h) > 0));
    case M_OR:
      return (pat->not ^ (perform_or (pat->child, flags, ctx, h) > 0));
    case M_ALL:
      return (!pat->not);
    case M_EXPIRED:
      return (pat->not ^ h->expired);
    case M_SUPERSEDED:
      return (pat->not ^ h->superseded);
    case M_FLAG:
      return (pat->not ^ h->flagged);
    case M_TAG:
      return (pat->not ^ h->tagged);
    case M_NEW:
      return (pat->not ? h->old || h->read : !(h->old || h->read));
    case M_UNREAD:
      return (pat->not ? h->read : !h->read);
    case M_REPLIED:
      return (pat->not ^ h->replied);
    case M_OLD:
      return (pat->not ? (!h->old || h->read) : (h->old && !h->read));
    case M_READ:
      return (pat->not ^ h->read);
    case M_DELETED:
      return (pat->not ^ h->deleted);
    case M_MESSAGE:
      return (pat->not ^ (h->msgno >= pat->min - 1 && (pat->max == M_MAXRANGE ||
						   h->msgno <= pat->max - 1)));
    case M_DATE:
      return (pat->not ^ (h->date_sent >= pat->min && h->date_sent <= pat->max));
    case M_DATE_RECEIVED:
      return (pat->not ^ (h->received >= pat->min && h->received <= pat->max));
    case M_BODY:
      return (pat->not ^ msg_search (pat->rx, buf, sizeof (buf), 0, h->msgno));
    case M_HEADER:
      return (pat->not ^ msg_search (pat->rx, buf, sizeof (buf), 1, h->msgno));
    case M_SENDER:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->sender));
    case M_FROM:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->from));
    case M_TO:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->to));
    case M_CC:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->cc));
    case M_SUBJECT:
      return (pat->not ^ (h->env->subject && regexec (pat->rx, h->env->subject, 0, NULL, 0) == 0));
    case M_ID:
      return (pat->not ^ (h->env->message_id && regexec (pat->rx, h->env->message_id, 0, NULL, 0) == 0));
    case M_SCORE:
      return (pat->not ^ (h->score >= pat->min && (pat->max == M_MAXRANGE ||
						   h->score <= pat->max)));
    case M_REFERENCE:
      return (pat->not ^ match_reference (pat->rx, h->env->references));
    case M_ADDRESS:
      return (pat->not ^ (match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->from) ||
			  match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->sender) ||
			  match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->to) ||
			  match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->cc)));
      break;
    case M_RECIPIENT:
      return (pat->not ^ (match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->to) ||
			  match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS, h->env->cc)));
      break;
    case M_LIST:
      return (pat->not ^ (mutt_is_list_recipient (h->env->to) ||
			  mutt_is_list_recipient (h->env->cc)));
    case M_PERSONAL_RECIP:
      return (pat->not ^ (match_user (h->env->to) || match_user (h->env->cc)));
      break;
    case M_PERSONAL_FROM:
      return (pat->not ^ (match_user (h->env->from)));
      break;
  }
  mutt_error ("error: unknown op %d (report this error).", pat->op);
  return (-1);
}

/* convert a simple search into a real request */
void mutt_check_simple (char *s, size_t len, const char *simple)
{
  char tmp[LONG_STRING];

  if (!strchr (s, '~')) /* yup, so spoof a real request */
  {
    /* convert old tokens into the new format */
    if (strcasecmp ("all", s) == 0 ||
	!strcmp ("^", s) || !strcmp (".", s)) /* ~A is more efficient */
      strfcpy (s, "~A", len);
    else if (strcasecmp ("del", s) == 0)
      strfcpy (s, "~D", len);
    else if (strcasecmp ("flag", s) == 0)
      strfcpy (s, "~F", len);
    else if (strcasecmp ("new", s) == 0)
      strfcpy (s, "~N", len);
    else if (strcasecmp ("old", s) == 0)
      strfcpy (s, "~O", len);
    else if (strcasecmp ("repl", s) == 0)
      strfcpy (s, "~Q", len);
    else if (strcasecmp ("read", s) == 0)
      strfcpy (s, "~R", len);
    else if (strcasecmp ("tag", s) == 0)
      strfcpy (s, "~T", len);
    else if (strcasecmp ("unread", s) == 0)
      strfcpy (s, "~U", len);
    else
    {
      const char *p = s;
      int i = 0;

      tmp[i++] = '"';
      while (*p && i < sizeof (tmp) - 2)
      {
	if (*p == '\\' || *p == '"')
	  tmp[i++] = '\\';
	tmp[i++] = *p++;
      }
      tmp[i++] = '"';
      tmp[i] = 0;
      mutt_expand_fmt (s, len, simple, tmp);
    }
  }
}

int mutt_pattern_func (int op, char *prompt, HEADER *hdr)
{
  pattern_t *pat;
  char buf[STRING] = "";
  char error[STRING];
  int i;

  if (mutt_get_field (prompt, buf, sizeof (buf), 0) != 0 || !buf[0])
    return (-1);

  mutt_message ("Compiling search pattern...");

  mutt_check_simple (buf, sizeof (buf), SimpleSearch);

  if ((pat = mutt_pattern_comp (buf, M_FULL_MSG, error, sizeof (error))) == NULL)
  {
    mutt_error ("%s", error);
    return (-1);
  }

  mutt_message ("Executing command on matching messages...");

  if (op == M_LIMIT)
  {
    for (i = 0; i < Context->msgcount; i++)
      Context->hdrs[i]->virtual = -1;
    Context->vcount = 0;
  }

  for (i = 0; i < Context->msgcount; i++)
    if (mutt_pattern_exec (pat, M_MATCH_FULL_ADDRESS, Context, Context->hdrs[i]))
    {
      switch (op)
      {
	case M_DELETE:
	  mutt_set_flag (Context, Context->hdrs[i], M_DELETE, 1);
	  break;
	case M_UNDELETE:
	  mutt_set_flag (Context, Context->hdrs[i], M_DELETE, 0);
	  break;
	case M_TAG:
	  mutt_set_flag (Context, Context->hdrs[i], M_TAG, 1);
	  break;
	case M_UNTAG:
	  mutt_set_flag (Context, Context->hdrs[i], M_TAG, 0);
	  break;
	case M_LIMIT:
	  Context->hdrs[i]->virtual = Context->vcount;
	  Context->v2r[Context->vcount] = i;
	  Context->vcount++;
	  break;
      }
    }

  mutt_clear_error ();

  if (op == M_LIMIT && !Context->vcount)
  {
    mutt_error ("No messages matched criteria.");
    /* restore full display */
    Context->vcount = 0;
    for (i = 0; i < Context->msgcount; i++)
    {
      Context->hdrs[i]->virtual = i;
      Context->v2r[i] = i;
    }

    Context->vcount = Context->msgcount;
  }

  mutt_pattern_free (&pat);

  return 0;
}

int mutt_search_command (int cur, int op)
{
  int i, j;
  char buf[STRING];
  char temp[LONG_STRING];
  char error[STRING];
  int incr;
  HEADER *h;
  
  if (op != OP_SEARCH_NEXT && op != OP_SEARCH_OPPOSITE)
  {
    strfcpy (buf, LastSearch, sizeof (buf));
    if (mutt_get_field ((op == OP_SEARCH) ? "Search for: " : "Reverse search for: ",
		      buf, sizeof (buf), M_CLEAR) != 0 || !buf[0])
      return (-1);

    if (op == OP_SEARCH)
      unset_option (OPTSEARCHREVERSE);
    else
      set_option (OPTSEARCHREVERSE);

    /* compare the *expanded* version of the search pattern in case 
       $simple_search has changed while we were searching */
    strfcpy (temp, buf, sizeof (temp));
    mutt_check_simple (temp, sizeof (temp), SimpleSearch);

    if (!SearchPattern || strcmp (temp, LastSearchExpn))
    {
      set_option (OPTSEARCHINVALID);
      strfcpy (LastSearch, buf, sizeof (LastSearch));
      mutt_message ("Compiling search pattern...");
      mutt_pattern_free (&SearchPattern);
      if ((SearchPattern = mutt_pattern_comp (temp, M_FULL_MSG, error, sizeof (error))) == NULL)
      {
	mutt_error ("%s", error);
	return (-1);
      }
      mutt_clear_error ();
    }
  }
  else if (!SearchPattern)
  {
    mutt_error ("No search pattern.");
    return (-1);
  }

  if (option (OPTSEARCHINVALID))
  {
    for (i = 0; i < Context->msgcount; i++)
      Context->hdrs[i]->searched = 0;
    unset_option (OPTSEARCHINVALID);
  }

  incr = (option (OPTSEARCHREVERSE)) ? -1 : 1;
  if (op == OP_SEARCH_OPPOSITE)
    incr = -incr;

  for (i = cur + incr, j = 0 ; j != Context->vcount; j++)
  {
    if (i > Context->vcount - 1)
    {
      i = 0;
      mutt_message ("Search wrapped to top.");
    }
    else if (i < 0)
    {
      i = Context->vcount - 1;
      mutt_message ("Search wrapped to bottom.");
    }

    h = Context->hdrs[Context->v2r[i]];
    if (h->searched)
    {
      /* if we've already evaulated this message, use the cached value */
      if (h->matched)
	return i;
    }
    else
    {
      /* remember that we've already searched this message */
      h->searched = 1;
      if ((h->matched = (mutt_pattern_exec (SearchPattern, M_MATCH_FULL_ADDRESS, Context, h) > 0)))
	return i;
    }

    i += incr;
  }

  mutt_error ("Not found.");
  return (-1);
}
