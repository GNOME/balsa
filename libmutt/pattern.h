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

#define new_pattern() calloc(1, sizeof (pattern_t))

/* flag to mutt_pattern_comp() */
#define M_FULL_MSG	1	/* enable body and header matching */

typedef struct pattern_t
{
  short op;
  short not;
  int min;
  int max;
  struct pattern_t *next;
  struct pattern_t *child;		/* arguments to logical op */
  regex_t *rx;
} pattern_t;

typedef enum {
  M_MATCH_FULL_ADDRESS = 1
} pattern_exec_flag;

int mutt_pattern_exec (struct pattern_t *pat, pattern_exec_flag flags, CONTEXT *ctx, HEADER *h);

pattern_t *mutt_pattern_comp (const char *s,
			      int flags,
			      char *err,
			      size_t errlen);

void mutt_check_simple (char *s, size_t len, const char *simple);

void mutt_pattern_free (pattern_t **pat);

