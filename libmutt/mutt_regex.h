/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
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

/*
 * A (more) generic interface to regular expression matching
 */

#ifndef MUTT_REGEX_H
#define MUTT_REGEX_H

/* LIBMUTT */
/* BALSA: use PERL compatible Regular Expressions if available */
#ifdef HAVE_PCRE
#  include <pcreposix.h>
#else
#  ifdef USE_GNU_REGEX
#    include "_regex.h"
#  else
#    include <sys/types.h>
#    include <regex.h>
#  endif
#endif

/* this is a non-standard option supported by Solaris 2.5.x which allows
 * patterns of the form \<...\>
 */
#ifndef REG_WORDS
#define REG_WORDS 0
#endif

#define REGCOMP(X,Y,Z) regcomp(X, Y, REG_WORDS|REG_EXTENDED|(Z))
#define REGEXEC(X,Y) regexec(&X, Y, (size_t)0, (regmatch_t *)0, (int)0)

typedef struct
{
  char *pattern;	/* printable version */
  regex_t *rx; 		/* compiled expression */
  int not;		/* do not match */
} REGEXP;

WHERE REGEXP Alternates;
/* LIBMUTT - Mask clashes with X.h */
WHERE REGEXP FileMask;
WHERE REGEXP QuoteRegexp;
WHERE REGEXP ReplyRegexp;
WHERE REGEXP Smileys;
WHERE REGEXP GecosMask;

#endif /* MUTT_REGEX_H */
