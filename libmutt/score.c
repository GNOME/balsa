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
#include "sort.h"
#include "pattern.h"
#include <string.h>
#include <stdlib.h>

typedef struct score_t
{
  char *str;
  pattern_t *pat;
  int val;
  int exact;		/* if this rule matches, don't evaluate any more */
  struct score_t *next;
} SCORE;

SCORE *Score = NULL;

static void score_CheckResort (void)
{
  /* Conditions under which we should resort the mailbox */
  if ((Sort & SORT_MASK) == SORT_SCORE || (SortAux & SORT_MASK) == SORT_SCORE)
  {
    set_option (OPTNEEDRESORT);
    set_option (OPTNEEDRESCORE);
    if ((Sort & SORT_MASK) == SORT_THREADS)
      set_option (OPTSORTSUBTHREADS);
  }
}

int mutt_parse_score (const char *s, unsigned long data, char *errmsg, size_t errlen)
{
  char pattern[LONG_STRING];
  char score[SHORT_STRING];
  char expn[LONG_STRING];
  char *pc;
  SCORE *ptr, *last;
  struct pattern_t *pat;

  s = mutt_extract_token (pattern, sizeof (pattern), s, expn, sizeof (expn), 0);
  if (!s)
  {
    strfcpy (errmsg, "too few arguments", errlen);
    return (-1);
  }
  s = mutt_extract_token (score, sizeof (score), s, expn, sizeof (expn), 0);
  if (s)
  {
    strfcpy (errmsg, "too many arguments", errlen);
    return (-1);
  }

  /* look for an existing entry and update the value, else add it to the end
     of the list */
  for (ptr = Score, last = NULL; ptr; last = ptr, ptr = ptr->next)
    if (strcmp (pattern, ptr->str) == 0)
      break;
  if (!ptr)
  {
    if ((pat = mutt_pattern_comp (pattern, 0, errmsg, errlen)) == NULL)
      return (-1);
    ptr = safe_calloc (1, sizeof (SCORE));
    if (last)
      last->next = ptr;
    else
      Score = ptr;
    ptr->pat = pat;
    ptr->str = safe_strdup (pattern);
  }
  pc = score;
  if (*pc == '=')
  {
    ptr->exact = 1;
    pc++;
  }
  ptr->val = atoi (pc);
  score_CheckResort ();
  return 0;
}

void mutt_score_message (HEADER *hdr)
{
  SCORE *tmp;

  hdr->score = 0; /* in case of re-scoring */
  for (tmp = Score; tmp; tmp = tmp->next)
  {
    if (mutt_pattern_exec (tmp->pat, 0, NULL, hdr) > 0)
    {
      if (tmp->exact || tmp->val == 9999 || tmp->val == -9999)
      {
	hdr->score = tmp->val;
	break;
      }
      hdr->score += tmp->val;
    }
  }
  if (hdr->score < 0)
    hdr->score = 0;
}

int mutt_parse_unscore (const char *s, unsigned long data, char *errmsg, size_t errlen)
{
  char buf[SHORT_STRING];
  char expn[LONG_STRING];
  SCORE *tmp, *last = NULL;

  while (s)
  {
    s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);
    if (!strcmp ("*", buf))
    {
      for (tmp = Score; tmp; )
      {
	last = tmp;
	tmp = tmp->next;
	mutt_pattern_free (&last->pat);
	safe_free ((void **) &last);
      }
      Score = NULL;
      break; /* nothing else to do */
    }
    else
    {
      for (tmp = Score; tmp; last = tmp, tmp = tmp->next)
      {
	if (!strcmp (buf, tmp->str))
	{
	  if (last)
	    last->next = tmp->next;
	  else
	    Score = tmp->next;
	  mutt_pattern_free (&tmp->pat);
	  safe_free ((void **) &tmp);
	  /* there should only be one score per pattern, so we can stop here */
	  break;
	}
      }
    }
  }
  score_CheckResort ();
  return 0;
}
