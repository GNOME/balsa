/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>, and others
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
#include "mapping.h"
#include "keymap.h"
#include "mailbox.h"
#include "copy.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>



#ifdef HAVE_PGP
#include "pgp.h"
#endif



static int eat_regexp (pattern_t *pat, BUFFER *, BUFFER *);
static int eat_date (pattern_t *pat, BUFFER *, BUFFER *);
static int eat_range (pattern_t *pat, BUFFER *, BUFFER *);

#ifndef LIBMUTT
struct pattern_flags
{
  int tag;	/* character used to represent this op */
  int op;	/* operation to perform */
  int class;
  int (*eat_arg) (pattern_t *, BUFFER *, BUFFER *);
}
Flags[] =
{
  { 'A', M_ALL,			0,		NULL },
  { 'b', M_BODY,		M_FULL_MSG,	eat_regexp },
  { 'B', M_WHOLE_MSG,		M_FULL_MSG,	eat_regexp },
  { 'c', M_CC,			0,		eat_regexp },
  { 'C', M_RECIPIENT,		0,		eat_regexp },
  { 'd', M_DATE,		0,		eat_date },
  { 'D', M_DELETED,		0,		NULL },
  { 'e', M_SENDER,		0,		eat_regexp },
  { 'E', M_EXPIRED,		0,		NULL },
  { 'f', M_FROM,		0,		eat_regexp },
  { 'F', M_FLAG,		0,		NULL },
#ifdef HAVE_PGP
  { 'g', M_PGP_SIGN, 		0, 		NULL },
  { 'G', M_PGP_ENCRYPT, 	0, 		NULL },
#endif
  { 'h', M_HEADER,		M_FULL_MSG,	eat_regexp },
  { 'i', M_ID,			0,		eat_regexp },
#ifdef HAVE_PGP
  { 'k', M_PGP_KEY, 		0, 		NULL },
#endif
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
  { 'v', M_COLLAPSED,		0,		NULL },
  { 'x', M_REFERENCE,		0,		eat_regexp },
  { 'y', M_XLABEL,		0,		eat_regexp },
  { 'z', M_SIZE,		0,		eat_range },
  { '=', M_DUPLICATED,		0,		NULL },
  { 0 }
};

static pattern_t *SearchPattern = NULL; /* current search pattern */
static char LastSearch[STRING] = { 0 };	/* last pattern searched for */
static char LastSearchExpn[LONG_STRING] = { 0 }; /* expanded version of
						    LastSearch */

#define M_MAXRANGE -1
#endif /* LIBMUTT - we don't need this search */

/* constants for parse_date_range() */
#define M_PDR_NONE	0x0000
#define M_PDR_MINUS	0x0001
#define M_PDR_PLUS	0x0002
#define M_PDR_WINDOW	0x0004
#define M_PDR_ABSOLUTE	0x0008
#define M_PDR_DONE	0x0010
#define M_PDR_ERROR	0x0100
#define M_PDR_ERRORDONE	(M_PDR_ERROR | M_PDR_DONE)

#ifndef LIBMUTT

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
    if (isalpha ((unsigned char) *s) && isupper ((unsigned char) *s))
      return 0; /* case-sensitive */
    s++;
  }
  return REG_ICASE; /* case-insensitive */
}

static int
msg_search (CONTEXT *ctx, regex_t *rx, char *buf, size_t blen, int op, int msgno)
{
  char tempfile[_POSIX_PATH_MAX];
  MESSAGE *msg = NULL;
  STATE s;
  struct stat st;
  FILE *fp = NULL;
  long lng = 0;
  int match = 0;
  HEADER *h = ctx->hdrs[msgno];

  if ((msg = mx_open_message (ctx, msgno)) != NULL)
  {
    if (option (OPTTHOROUGHSRC))
    {
      /* decode the header / body */
      memset (&s, 0, sizeof (s));
      s.fpin = msg->fp;
      s.flags = M_CHARCONV;
      mutt_mktemp (tempfile);
      if ((s.fpout = safe_fopen (tempfile, "w+")) == NULL)
      {
	mutt_perror (tempfile);
	return (0);
      }

      if (op != M_BODY)
	mutt_copy_header (msg->fp, h, s.fpout, CH_FROM | CH_DECODE, NULL);

      if (op != M_HEADER)
      {
	mutt_parse_mime_message (ctx, h);



#ifdef HAVE_PGP
	if (h->pgp & PGPENCRYPT && !pgp_valid_passphrase())
	{
	  mx_close_message (&msg);
	  if (fp)
	  {
	    fclose (fp);
	    unlink (tempfile);
	  }
	  return (0);
	}
#endif



	fseek (msg->fp, h->offset, 0);
	mutt_body_handler (h->content, &s);
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
      if (op != M_BODY)
      {
	fseek (fp, h->offset, 0);
	lng = h->content->offset - h->offset;
      }
      if (op != M_HEADER)
      {
	if (op == M_BODY)
	  fseek (fp, h->content->offset, 0);
	lng += h->content->length;
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
      lng -= mutt_strlen (buf);
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

int eat_regexp (pattern_t *pat, BUFFER *s, BUFFER *err)
{
  BUFFER buf;
  int r;

  memset (&buf, 0, sizeof (buf));
  if (mutt_extract_token (&buf, s, M_TOKEN_PATTERN | M_TOKEN_COMMENT) != 0 ||
      !buf.data)
  {
    snprintf (err->data, err->dsize, _("Error in expression: %s"), s->dptr);
    return (-1);
  }
  pat->rx = safe_malloc (sizeof (regex_t));
  r = REGCOMP (pat->rx, buf.data, REG_NEWLINE | REG_NOSUB | mutt_which_case (buf.data));
  FREE (&buf.data);
  if (r)
  {
    regerror (r, pat->rx, err->data, err->dsize);
    regfree (pat->rx);
    safe_free ((void **) &pat->rx);
    return (-1);
  }
  return 0;
}

int eat_range (pattern_t *pat, BUFFER *s, BUFFER *err)
{
  char *tmp;
  int do_exclusive = 0;
  int skip_quote = 0;
  
  /*
   * If simple_search is set to "~m %s", the range will have double quotes 
   * around it...
   */
  if (*s->dptr == '"')
  {
    s->dptr++;
    skip_quote = 1;
  }
  if (*s->dptr == '<')
    do_exclusive = 1;
  if ((*s->dptr != '-') && (*s->dptr != '<'))
  {
    /* range minimum */
    if (*s->dptr == '>')
    {
      pat->max = M_MAXRANGE;
      pat->min = strtol (s->dptr + 1, &tmp, 0) + 1; /* exclusive range */
    }
    else
      pat->min = strtol (s->dptr, &tmp, 0);
    if (toupper (*tmp) == 'K') /* is there a prefix? */
    {
      pat->min *= 1024;
      tmp++;
    }
    else if (toupper (*tmp) == 'M')
    {
      pat->min *= 1048576;
      tmp++;
    }
    if (*s->dptr == '>')
    {
      s->dptr = tmp;
      return 0;
    }
    if (*tmp != '-')
    {
      /* exact value */
      pat->max = pat->min;
      s->dptr = tmp;
      return 0;
    }
    tmp++;
  }
  else
  {
    s->dptr++;
    tmp = s->dptr;
  }
  
  if (isdigit ((unsigned char) *tmp))
  {
    /* range maximum */
    pat->max = strtol (tmp, &tmp, 0);
    if (toupper (*tmp) == 'K')
    {
      pat->max *= 1024;
      tmp++;
    }
    else if (toupper (*tmp) == 'M')
    {
      pat->max *= 1048576;
      tmp++;
    }
    if (do_exclusive)
      (pat->max)--;
  }
  else
    pat->max = M_MAXRANGE;

  if (skip_quote && *tmp == '"')
    tmp++;

  SKIPWS (tmp);
  s->dptr = tmp;
  return 0;
}

static const char *getDate (const char *s, struct tm *t, BUFFER *err)
{
  char *p;
  time_t now = time (NULL);
  struct tm *tm = localtime (&now);

  t->tm_mday = strtol (s, &p, 10);
  if (t->tm_mday < 1 || t->tm_mday > 31)
  {
    snprintf (err->data, err->dsize, _("Invalid day of month: %s"), s);
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
  t->tm_mon = strtol (p, &p, 10) - 1;
  if (t->tm_mon < 0 || t->tm_mon > 11)
  {
    snprintf (err->data, err->dsize, _("Invalid month: %s"), p);
    return NULL;
  }
  if (*p != '/')
  {
    t->tm_year = tm->tm_year;
    return p;
  }
  p++;
  t->tm_year = strtol (p, &p, 10);
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
static const char *get_offset (struct tm *tm, const char *s, int sign)
{
  char *ps;
  int offset = strtol (s, &ps, 0);
  if ((sign < 0 && offset > 0) || (sign > 0 && offset < 0))
    offset = -offset;

  switch (*ps)
  {
    case 'y':
      tm->tm_year += offset;
      break;
    case 'm':
      tm->tm_mon += offset;
      break;
    case 'w':
      tm->tm_mday += 7 * offset;
      break;
    case 'd':
      tm->tm_mday += offset;
      break;
    default:
      return s;
  }
  mutt_normalize_time (tm);
  return (ps + 1);
}

static void adjust_date_range (struct tm *min, struct tm *max)
{
  if (min->tm_year > max->tm_year
      || (min->tm_year == max->tm_year && min->tm_mon > max->tm_mon)
      || (min->tm_year == max->tm_year && min->tm_mon == max->tm_mon
	&& min->tm_mday > max->tm_mday))
  {
    int tmp;
    
    tmp = min->tm_year;
    min->tm_year = max->tm_year;
    max->tm_year = tmp;
      
    tmp = min->tm_mon;
    min->tm_mon = max->tm_mon;
    max->tm_mon = tmp;
      
    tmp = min->tm_mday;
    min->tm_mday = max->tm_mday;
    max->tm_mday = tmp;
    
    min->tm_hour = min->tm_min = min->tm_sec = 0;
    max->tm_hour = 23;
    max->tm_min = max->tm_sec = 59;
  }
}

static const char * parse_date_range (const char* pc, struct tm *min,
    struct tm *max, int haveMin, struct tm *baseMin, BUFFER *err)
{
  int flag = M_PDR_NONE;	
  while (*pc && ((flag & M_PDR_DONE) == 0))
  {
    const char *pt;
    char ch = *pc++;
    SKIPWS (pc);
    switch (ch)
    {
      case '-':
      {
	/* try a range of absolute date minus offset of Ndwmy */
	pt = get_offset (min, pc, -1);
	if (pc == pt)
	{
	  if (flag == M_PDR_NONE)
	  { /* nothing yet and no offset parsed => absolute date? */
	    if (!getDate (pc, max, err))
	      flag |= (M_PDR_ABSOLUTE | M_PDR_ERRORDONE);  /* done bad */
	    else
	    {
	      /* reestablish initial base minimum if not specified */
	      if (!haveMin)
		memcpy (min, baseMin, sizeof(struct tm));
	      flag |= (M_PDR_ABSOLUTE | M_PDR_DONE);  /* done good */
	    }
	  }
	  else
	    flag |= M_PDR_ERRORDONE;
	}
	else
	{
	  pc = pt;
	  if (flag == M_PDR_NONE && !haveMin)
	  { /* the very first "-3d" without a previous absolute date */
	    max->tm_year = min->tm_year;
	    max->tm_mon = min->tm_mon;
	    max->tm_mday = min->tm_mday;
	  }
	  flag |= M_PDR_MINUS;
	}
      }
      break;
      case '+':
      { /* enlarge plusRange */
	pt = get_offset (max, pc, 1);
	if (pc == pt)
	  flag |= M_PDR_ERRORDONE;
	else
	{
	  pc = pt;
	  flag |= M_PDR_PLUS;
	}
      }
      break;
      case '*':
      { /* enlarge window in both directions */
	pt = get_offset (min, pc, -1);
	if (pc == pt)
	  flag |= M_PDR_ERRORDONE;
	else
	{
	  pc = get_offset (max, pc, 1);
	  flag |= M_PDR_WINDOW;
	}
      }
      break;
      default:
	flag |= M_PDR_ERRORDONE;
    }
    SKIPWS (pc);
  }
  if ((flag & M_PDR_ERROR) && !(flag & M_PDR_ABSOLUTE))
  { /* getDate has its own error message, don't overwrite it here */
    snprintf (err->data, err->dsize, _("Invalid relative date: %s"), pc-1);
  }
  return ((flag & M_PDR_ERROR) ? NULL : pc);
}

static int eat_date (pattern_t *pat, BUFFER *s, BUFFER *err)
{
  BUFFER buffer;
  struct tm min, max;

  memset (&buffer, 0, sizeof (buffer));
  if (mutt_extract_token (&buffer, s, M_TOKEN_COMMENT | M_TOKEN_PATTERN) != 0
      || !buffer.data)
  {
    strfcpy (err->data, _("error in expression"), err->dsize);
    return (-1);
  }

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

  if (strchr ("<>=", buffer.data[0]))
  {
    /* offset from current time
       <3d	less than three days ago
       >3d	more than three days ago
       =3d	exactly three days ago */
    time_t now = time (NULL);
    struct tm *tm = localtime (&now);
    int exact = 0;

    if (buffer.data[0] == '<')
    {
      memcpy (&min, tm, sizeof (min));
      tm = &min;
    }
    else
    {
      memcpy (&max, tm, sizeof (max));
      tm = &max;

      if (buffer.data[0] == '=')
	exact++;
    }
    tm->tm_hour = 23;
    tm->tm_min = tm->tm_sec = 59;

    /* force negative offset */
    get_offset (tm, buffer.data + 1, -1);

    if (exact)
    {
      /* start at the beginning of the day in question */
      memcpy (&min, &max, sizeof (max));
      min.tm_hour = min.tm_sec = min.tm_min = 0;
    }
  }
  else
  {
    const char *pc = buffer.data;

    int haveMin = FALSE;
    int untilNow = FALSE;
    if (isdigit ((unsigned char)*pc))
    {
      /* mininum date specified */
      if ((pc = getDate (pc, &min, err)) == NULL)
      {
	FREE (&buffer.data);
	return (-1);
      }
      haveMin = TRUE;
      SKIPWS (pc);
      if (*pc == '-')
      {
        const char *pt = pc + 1;
	SKIPWS (pt);
	untilNow = (*pt == '\0');
      }
    }

    if (!untilNow)
    { /* max date or relative range/window */

      struct tm baseMin;

      if (!haveMin)
      { /* save base minimum and set current date, e.g. for "-3d+1d" */
	time_t now = time (NULL);
	struct tm *tm = localtime (&now);
	memcpy (&baseMin, &min, sizeof(baseMin));
	memcpy (&min, tm, sizeof (min));
	min.tm_hour = min.tm_sec = min.tm_min = 0;
      }
      
      /* preset max date for relative offsets,
	 if nothing follows we search for messages on a specific day */
      max.tm_year = min.tm_year;
      max.tm_mon = min.tm_mon;
      max.tm_mday = min.tm_mday;

      if (!parse_date_range (pc, &min, &max, haveMin, &baseMin, err))
      { /* bail out on any parsing error */
	FREE (&buffer.data);
	return (-1);
      }
    }
  }

  /* Since we allow two dates to be specified we'll have to adjust that. */
  adjust_date_range (&min, &max);

  pat->min = mutt_mktime (&min, 1);
  pat->max = mutt_mktime (&max, 1);

  FREE (&buffer.data);

  return 0;
}

static struct pattern_flags *lookup_tag (char tag)
{
  int i;

  for (i = 0; Flags[i].tag; i++)
    if (Flags[i].tag == tag)
      return (&Flags[i]);
  return NULL;
}

static /* const */ char *find_matching_paren (/* const */ char *s)
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
#endif /* LIBMUTT */

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

#ifndef LIBMUTT
pattern_t *mutt_pattern_comp (/* const */ char *s, int flags, BUFFER *err)
{
  pattern_t *curlist = NULL;
  pattern_t *tmp;
  pattern_t *last = NULL;
  int not = 0;
  int alladdr = 0;
  int or = 0;
  int implicit = 1;	/* used to detect logical AND operator */
  struct pattern_flags *entry;
  char *p;
  char *buf;
  BUFFER ps;

  memset (&ps, 0, sizeof (ps));
  ps.dptr = s;
  ps.dsize = mutt_strlen (s);

  while (*ps.dptr)
  {
    SKIPWS (ps.dptr);
    switch (*ps.dptr)
    {
      case '^':
	ps.dptr++;
	alladdr = !alladdr;
	break;
      case '!':
	ps.dptr++;
	not = !not;
	break;
      case '|':
	if (!or)
	{
	  if (!curlist)
	  {
	    snprintf (err->data, err->dsize, _("error in pattern at: %s"), ps.dptr);
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
	ps.dptr++;
	implicit = 0;
	not = 0;
	alladdr = 0;
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
	tmp->alladdr = alladdr;
	not = 0;
	alladdr=0;

	if (last)
	  last->next = tmp;
	else
	  curlist = tmp;
	last = tmp;

	ps.dptr++; /* move past the ~ */
	if ((entry = lookup_tag (*ps.dptr)) == NULL)
	{
	  snprintf (err->data, err->dsize, _("%c: invalid command"), *ps.dptr);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	if (entry->class && (flags & entry->class) == 0)
	{
	  snprintf (err->data, err->dsize, _("%c: not supported in this mode"), *ps.dptr);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	tmp->op = entry->op;

	ps.dptr++; /* eat the operator and any optional whitespace */
	SKIPWS (ps.dptr);

	if (entry->eat_arg)
	{
	  if (!*ps.dptr)
	  {
	    snprintf (err->data, err->dsize, _("missing parameter"));
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	  if (entry->eat_arg (tmp, &ps, err) == -1)
	  {
	    mutt_pattern_free (&curlist);
	    return NULL;
	  }
	}
	implicit = 1;
	break;
      case '(':
	p = find_matching_paren (ps.dptr + 1);
	if (*p != ')')
	{
	  snprintf (err->data, err->dsize, _("mismatched parenthesis: %s"), ps.dptr);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	/* compile the sub-expression */
	buf = mutt_substrdup (ps.dptr + 1, p);
	if ((tmp = mutt_pattern_comp (buf, flags, err)) == NULL)
	{
	  FREE (&buf);
	  mutt_pattern_free (&curlist);
	  return NULL;
	}
	FREE (&buf);
	if (last)
	  last->next = tmp;
	else
	  curlist = tmp;
	last = tmp;
	tmp->not ^= not;
	tmp->alladdr |= alladdr;
	not = 0;
	alladdr = 0;
	ps.dptr = p + 1; /* restore location */
	break;
      default:
	snprintf (err->data, err->dsize, _("error in pattern at: %s"), ps.dptr);
	mutt_pattern_free (&curlist);
	return NULL;
    }
  }
  if (!curlist)
  {
    strfcpy (err->data, _("empty pattern"), err->dsize);
    return NULL;
  }
  if (curlist->next)
  {
    tmp = new_pattern ();
    tmp->op = or ? M_OR : M_AND;
    tmp->child = curlist;
    curlist = tmp;
  }
  return (curlist);
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

static int match_adrlist (regex_t *rx, int match_personal, int alladdr,
			  int n, ...)
{
  va_list ap;
  ADDRESS *a;

  va_start (ap, n);
  for ( ; n ; n --)
  {
    for (a = va_arg (ap, ADDRESS *) ; a ; a = a->next)
    {
      if (alladdr^
	  ((a->mailbox && regexec (rx, a->mailbox, 0, NULL, 0) == 0) ||
	   (match_personal && a->personal &&
	    regexec (rx, a->personal, 0, NULL, 0) == 0)))
      {
	va_end (ap);
	return (! alladdr); /* Found match, or non-match if alladdr */
      }
    }
  }
  va_end (ap);
  return alladdr; /* No matches, or all matches if alladdr */
}

static int match_reference (regex_t *rx, LIST *refs)
{
  for (; refs; refs = refs->next)
    if (regexec (rx, refs->data, 0, NULL, 0) == 0)
      return 1;
  return 0;
}

int mutt_is_list_recipient (int alladdr, ADDRESS *a1, ADDRESS *a2)
{
  for (; a1 ; a1 = a1->next)
    if (alladdr ^ mutt_is_subscribed_list (a1))
      return (! alladdr);
  for (; a2 ; a2 = a2->next)
    if (alladdr ^ mutt_is_subscribed_list (a2))
      return (! alladdr);
  return alladdr;
}

int mutt_is_list_cc (int alladdr, ADDRESS *a1, ADDRESS *a2)
{
  for (; a1 ; a1 = a1->next)
    if (alladdr ^ mutt_is_mail_list (a1))
      return (! alladdr);
  for (; a2 ; a2 = a2->next)
    if (alladdr ^ mutt_is_mail_list (a2))
      return (! alladdr);
  return alladdr;
}

static int match_user (int alladdr, ADDRESS *a1, ADDRESS *a2)
{
  for (; a1 ; a1 = a1->next)
    if (alladdr ^ mutt_addr_is_user (a1))
      return (! alladdr);
  for (; a2 ; a2 = a2->next)
    if (alladdr ^ mutt_addr_is_user (a2))
      return (! alladdr);
  return alladdr;
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
    case M_HEADER:
    case M_WHOLE_MSG:
      return (pat->not ^ msg_search (ctx, pat->rx, buf, sizeof (buf), pat->op, h->msgno));
    case M_SENDER:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS,
					pat->alladdr, 1, h->env->sender));
    case M_FROM:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS,
					pat->alladdr, 1, h->env->from));
    case M_TO:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS,
					pat->alladdr, 1, h->env->to));
    case M_CC:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS,
					pat->alladdr, 1, h->env->cc));
    case M_SUBJECT:
      return (pat->not ^ (h->env->subject && regexec (pat->rx, h->env->subject, 0, NULL, 0) == 0));
    case M_ID:
      return (pat->not ^ (h->env->message_id && regexec (pat->rx, h->env->message_id, 0, NULL, 0) == 0));
    case M_SCORE:
      return (pat->not ^ (h->score >= pat->min && (pat->max == M_MAXRANGE ||
						   h->score <= pat->max)));
    case M_SIZE:
      return (pat->not ^ (h->content->length >= pat->min && (pat->max == M_MAXRANGE || h->content->length <= pat->max)));
    case M_REFERENCE:
      return (pat->not ^ match_reference (pat->rx, h->env->references));
    case M_ADDRESS:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS,
					pat->alladdr, 4, h->env->from,
					h->env->sender, h->env->to, h->env->cc));
    case M_RECIPIENT:
      return (pat->not ^ match_adrlist (pat->rx, flags & M_MATCH_FULL_ADDRESS,
					pat->alladdr, 2, h->env->to, h->env->cc));
    case M_LIST:
      return (pat->not ^ mutt_is_list_recipient (pat->alladdr, h->env->to, h->env->cc));
    case M_PERSONAL_RECIP:
      return (pat->not ^ match_user (pat->alladdr, h->env->to, h->env->cc));
    case M_PERSONAL_FROM:
      return (pat->not ^ match_user (pat->alladdr, h->env->from, NULL));
    case M_COLLAPSED:
      return (pat->not ^ (h->collapsed && h->num_hidden > 1));
#ifdef HAVE_PGP
   case M_PGP_SIGN:
     return (pat->not ^ (h->pgp & PGPSIGN));
   case M_PGP_ENCRYPT:
     return (pat->not ^ (h->pgp & PGPENCRYPT));
   case M_PGP_KEY:
     return (pat->not ^ (h->pgp & PGPKEY));
#endif
    case M_XLABEL:
      return (pat->not ^ (h->env->x_label && regexec (pat->rx, h->env->x_label, 0, NULL, 0) == 0));
    case M_DUPLICATED:
      return (pat->not ^ (h->thread && h->thread->duplicate_thread));
  }
  mutt_error (_("error: unknown op %d (report this error)."), pat->op);
  return (-1);
}

static void quote_simple(char *tmp, size_t len, const char *p)
{
  int i = 0;
  
  tmp[i++] = '"';
  while (*p && i < len - 3)
  {
    if (*p == '\\' || *p == '"')
      tmp[i++] = '\\';
    tmp[i++] = *p++;
  }
  tmp[i++] = '"';
  tmp[i] = 0;
}
  
/* convert a simple search into a real request */
void mutt_check_simple (char *s, size_t len, const char *simple)
{
  char tmp[LONG_STRING];

  /* XXX - is ascii_strcasecmp() right here, or should we use locale's
   * equivalences?
   */
  
  if (!strchr (s, '~')) /* yup, so spoof a real request */
  {
    /* convert old tokens into the new format */
    if (ascii_strcasecmp ("all", s) == 0 ||
	!mutt_strcmp ("^", s) || !mutt_strcmp (".", s)) /* ~A is more efficient */
      strfcpy (s, "~A", len);
    else if (ascii_strcasecmp ("del", s) == 0)
      strfcpy (s, "~D", len);
    else if (ascii_strcasecmp ("flag", s) == 0)
      strfcpy (s, "~F", len);
    else if (ascii_strcasecmp ("new", s) == 0)
      strfcpy (s, "~N", len);
    else if (ascii_strcasecmp ("old", s) == 0)
      strfcpy (s, "~O", len);
    else if (ascii_strcasecmp ("repl", s) == 0)
      strfcpy (s, "~Q", len);
    else if (ascii_strcasecmp ("read", s) == 0)
      strfcpy (s, "~R", len);
    else if (ascii_strcasecmp ("tag", s) == 0)
      strfcpy (s, "~T", len);
    else if (ascii_strcasecmp ("unread", s) == 0)
      strfcpy (s, "~U", len);
    else
    {
      quote_simple (tmp, sizeof(tmp), s);
      mutt_expand_fmt (s, len, simple, tmp);
    }
  }
}

int mutt_pattern_func (int op, char *prompt)
{
  pattern_t *pat;
  char buf[LONG_STRING] = "", *simple, error[STRING];
  BUFFER err;
  int i;

  strfcpy (buf, NONULL (Context->pattern), sizeof (buf));
  if (mutt_get_field (prompt, buf, sizeof (buf), M_PATTERN | M_CLEAR) != 0 || !buf[0])
    return (-1);

  mutt_message _("Compiling search pattern...");
  
  simple = safe_strdup (buf);
  mutt_check_simple (buf, sizeof (buf), NONULL (SimpleSearch));

  err.data = error;
  err.dsize = sizeof (error);
  if ((pat = mutt_pattern_comp (buf, M_FULL_MSG, &err)) == NULL)
  {
    FREE (&simple);
    mutt_error ("%s", err.data);
    return (-1);
  }

  mutt_message _("Executing command on matching messages...");

#define THIS_BODY Context->hdrs[i]->content

  if (op == M_LIMIT)
  {
    Context->vcount    = 0;
    Context->vsize     = 0;
    Context->collapsed = 0;

    for (i = 0; i < Context->msgcount; i++)
    {
      /* new limit pattern implicitly uncollapses all threads */
      Context->hdrs[i]->virtual = -1;
      Context->hdrs[i]->limited = 0;
      Context->hdrs[i]->collapsed = 0;
      Context->hdrs[i]->num_hidden = 0;
      if (mutt_pattern_exec (pat, M_MATCH_FULL_ADDRESS, Context, Context->hdrs[i]))
      {
	Context->hdrs[i]->virtual = Context->vcount;
	Context->hdrs[i]->limited = 1;
	Context->v2r[Context->vcount] = i;
	Context->vcount++;
	Context->vsize+=THIS_BODY->length + THIS_BODY->offset -
	  THIS_BODY->hdr_offset;
      }
    }
  }
  else
  {
    for (i = 0; i < Context->vcount; i++)
    {
      if (mutt_pattern_exec (pat, M_MATCH_FULL_ADDRESS, Context, Context->hdrs[Context->v2r[i]]))
      {
	switch (op)
	{
	  case M_DELETE:
	  case M_UNDELETE:
	    mutt_set_flag (Context, Context->hdrs[Context->v2r[i]], M_DELETE, 
			  (op == M_DELETE));
	    break;
	  case M_TAG:
	  case M_UNTAG:
	    mutt_set_flag (Context, Context->hdrs[Context->v2r[i]], M_TAG, 
			   (op == M_TAG));
	    break;
	}
      }
    }
  }

#undef THIS_BODY

  mutt_clear_error ();

  if (op == M_LIMIT)
  {
    safe_free ((void **) &Context->pattern);
    if (Context->limit_pattern) 
      mutt_pattern_free (&Context->limit_pattern);
    if (!Context->vcount)
    {
      Context->vcount = Context->msgcount;
      mutt_error _("No messages matched criteria.");
      /* restore full display */
      for (i = 0; i < Context->msgcount; i++)
      {
	Context->hdrs[i]->virtual = i;
	Context->v2r[i] = i;
      }
    }
    else if (mutt_strncmp (buf, "~A", 2) != 0)
    {
      Context->pattern = simple;
      simple = NULL; /* don't clobber it */
      Context->limit_pattern = mutt_pattern_comp (buf, M_FULL_MSG, &err);
    }
  }
  FREE (&simple);
  mutt_pattern_free (&pat);
  return 0;
}

int mutt_search_command (int cur, int op)
{
  int i, j;
  char buf[STRING];
  char temp[LONG_STRING];
  char error[STRING];
  BUFFER err;
  int incr;
  HEADER *h;
  
  if (op != OP_SEARCH_NEXT && op != OP_SEARCH_OPPOSITE)
  {
    strfcpy (buf, LastSearch, sizeof (buf));
    if (mutt_get_field ((op == OP_SEARCH) ? _("Search for: ") :
		      "Reverse search for: ", buf, sizeof (buf),
		      M_CLEAR | M_PATTERN) != 0 || !buf[0])
      return (-1);

    if (op == OP_SEARCH)
      unset_option (OPTSEARCHREVERSE);
    else
      set_option (OPTSEARCHREVERSE);

    /* compare the *expanded* version of the search pattern in case 
       $simple_search has changed while we were searching */
    strfcpy (temp, buf, sizeof (temp));
    mutt_check_simple (temp, sizeof (temp), NONULL (SimpleSearch));

    if (!SearchPattern || mutt_strcmp (temp, LastSearchExpn))
    {
      set_option (OPTSEARCHINVALID);
      strfcpy (LastSearch, buf, sizeof (LastSearch));
      mutt_message _("Compiling search pattern...");
      mutt_pattern_free (&SearchPattern);
      err.data = error;
      err.dsize = sizeof (error);
      if ((SearchPattern = mutt_pattern_comp (temp, M_FULL_MSG, &err)) == NULL)
      {
	mutt_error ("%s", error);
	return (-1);
      }
      mutt_clear_error ();
    }
  }
  else if (!SearchPattern)
  {
    mutt_error _("No search pattern.");
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
      if (option (OPTWRAPSEARCH))
        mutt_message _("Search wrapped to top.");
      else 
      {
        mutt_message _("Search hit bottom without finding match");
	return (-1);
      }
    }
    else if (i < 0)
    {
      i = Context->vcount - 1;
      if (option (OPTWRAPSEARCH))
        mutt_message _("Search wrapped to bottom.");
      else 
      {
        mutt_message _("Search hit top without finding match");
	return (-1);
      }
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

    if (SigInt)
    {
      mutt_error _("Search interrupted.");
      SigInt = 0;
      return (-1);
    }

    i += incr;
  }

  mutt_error _("Not found.");
  return (-1);
}
#endif  /* LIBMUTT */
