/*
 * Copyright (C) 1999-2000 Thomas Roessler <roessler@guug.de>
 *
 *     This program is free software; you can redistribute it
 *     and/or modify it under the terms of the GNU General Public
 *     License as published by the Free Software Foundation; either
 *     version 2 of the License, or (at your option) any later
 *     version.
 *
 *     This program is distributed in the hope that it will be
 *     useful, but WITHOUT ANY WARRANTY; without even the implied
 *     warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *     PURPOSE.  See the GNU General Public License for more
 *     details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 59 Temple Place - Suite 330,
 *     Boston, MA  02111, USA.
 */


#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <ctype.h>

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>


#include "mutt.h"
#include "charset.h"

#ifndef EILSEQ
# define EILSEQ EINVAL
#endif

/* 
 * The following list has been created manually from the data under:
 * http://www.isi.edu/in-notes/iana/assignments/character-sets
 * Last update: 2000-09-07
 *
 * Note that it includes only the subset of character sets for which
 * a preferred MIME name is given.
 */

static struct 
{
  char *key;
  char *pref;
}
PreferredMIMENames[] = 
{
  { "ansi_x3.4-1968", 	"us-ascii"     	},
  { "iso-ir-6",		"us-ascii" 	},
  { "iso_646.irv:1991",	"us-ascii" 	},
  { "ascii",		"us-ascii" 	},
  { "iso646-us",	"us-ascii" 	},
  { "us",		"us-ascii" 	},
  { "ibm367",		"us-ascii" 	},
  { "cp367",		"us-ascii" 	},
  { "csASCII",		"us-ascii" 	},
  
  { "csISO2022KR",	"iso-2022-kr" 	},
  { "csEUCKR",		"euc-kr"      	},
#ifdef LIBMUTT
  /* this is probably wrong but seems to work */
  { "ks_c_5601-1987",	"euc-kr"      	},
#endif
  { "csISO2022JP",	"iso-2022-jp"	},
  { "csISO2022JP2",	"iso-2022-jp-2" },

  { "ISO_8859-1:1987",	"iso-8859-1"	},
  { "iso-ir-100",	"iso-8859-1"	},
  { "iso_8859-1",	"iso-8859-1"	},
  { "latin1",		"iso-8859-1"	},
  { "l1",		"iso-8859-1"	},
  { "IBM819",		"iso-8859-1"	},
  { "CP819",		"iso-8859-1"	},
  { "csISOLatin1",	"iso-8859-1"	},
  
  { "ISO_8859-2:1987",	"iso-8859-2"	},
  { "iso-ir-101",	"iso-8859-2"	},
  { "iso_8859-2",	"iso-8859-2"	},
  { "latin2",		"iso-8859-2"	},
  { "l2",		"iso-8859-2"	},
  { "csISOLatin2",	"iso-8859-2"	},
  
  { "ISO_8859-3:1988",	"iso-8859-3"	},
  { "iso-ir-109",	"iso-8859-3"	},
  { "ISO_8859-3",	"iso-8859-3"	},
  { "latin3",		"iso-8859-3"	},
  { "l3",		"iso-8859-3"	},
  { "csISOLatin3",	"iso-8859-3"	},

  { "ISO_8859-4:1988",	"iso-8859-4"	},
  { "iso-ir-110",	"iso-8859-4"	},
  { "ISO_8859-4",	"iso-8859-4"	},
  { "latin4",		"iso-8859-4"	},
  { "l4",		"iso-8859-4"	},
  { "csISOLatin4",	"iso-8859-4"	},

  { "ISO_8859-6:1987",	"iso-8859-6"	},
  { "iso-ir-127",	"iso-8859-6"	},
  { "iso_8859-6",	"iso-8859-6"	},
  { "ECMA-114",		"iso-8859-6"	},
  { "ASMO-708",		"iso-8859-6"	},
  { "arabic",		"iso-8859-6"	},
  { "csISOLatinArabic",	"iso-8859-6"	},
  
  { "ISO_8859-7:1987",	"iso-8859-7"	},
  { "iso-ir-126",	"iso-8859-7"	},
  { "ISO_8859-7",	"iso-8859-7"	},
  { "ELOT_928",		"iso-8859-7"	},
  { "ECMA-118",		"iso-8859-7"	},
  { "greek",		"iso-8859-7"	},
  { "greek8",		"iso-8859-7"	},
  { "csISOLatinGreek",	"iso-8859-7"	},
  
  { "ISO_8859-8:1988",	"iso-8859-8"	},
  { "iso-ir-138",	"iso-8859-8"	},
  { "ISO_8859-8",	"iso-8859-8"	},
  { "hebrew",		"iso-8859-8"	},
  { "csISOLatinHebrew",	"iso-8859-8"	},

  { "ISO_8859-5:1988",	"iso-8859-5"	},
  { "iso-ir-144",	"iso-8859-5"	},
  { "ISO_8859-5",	"iso-8859-5"	},
  { "cyrillic",		"iso-8859-5"	},
  { "csISOLatinCyrillic", "iso8859-5"	},

  { "ISO_8859-9:1989",	"iso-8859-9"	},
  { "iso-ir-148",	"iso-8859-9"	},
  { "ISO_8859-9",	"iso-8859-9"	},
  { "latin5",		"iso-8859-9"	}, /* this is not a bug */
  { "l5",		"iso-8859-9"	},
  { "csISOLatin5",	"iso-8859-9"	},
  
  { "ISO_8859-10:1992",	"iso-8859-10"	},
  { "iso-ir-157",	"iso-8859-10"	},
  { "latin6",		"iso-8859-10"	}, /* this is not a bug */
  { "l6",		"iso-8859-10"	},
  { "csISOLatin6"	"iso-8859-10"	}, 
  
  { "csKOI8r",		"koi8-r"	},
  
  { "MS_Kanji",		"Shift_JIS"	}, /* Note the underscore! */
  { "csShiftJis",	"Shift_JIS"	},
  
  { "Extended_UNIX_Code_Packed_Format_for_Japanese",
      			"EUC-JP"	},
  { "csEUCPkdFmtJapanese", 
      			"EUC-JP"	},
  
  { "csGB2312",		"gb2312"	},
  { "csbig5",		"big5"		},

  /* 
   * End of official brain damage.  What follows has been taken
   * from glibc's localedata files. 
   */

  { "iso_8859-13",	"iso-8859-13"	},
  { "iso-ir-179",	"iso-8859-13"	},
  { "latin7",		"iso-8859-13"	}, /* this is not a bug */
  { "l7",		"iso-8859-13"	},
  
  { "iso_8859-14",	"iso-8859-14"	},
  { "latin8",		"iso-8859-14"	}, /* this is not a bug */
  { "l8",		"iso-8859-14"	},

  { "iso_8859-15",	"iso-8859-15"	},
  { "latin9",		"iso-8859-15"	}, /* this is not a bug */
  
  
  /* Suggested by Ionel Mugurel Ciobica <tgakic@sg10.chem.tue.nl> */
  { "latin0",            "iso-8859-15"   }, /* this is not a bug */
  
  { "iso_8859-16",      "iso-8859-16"   },
  { "latin10",          "iso-8859-16"   }, /* this is not a bug */
   
  
  /* 
   * David Champion <dgc@uchicago.edu> has observed this with
   * nl_langinfo under SunOS 5.8. 
   */

  { "646",		"us-ascii"	},
  
  /* 
   * http://www.sun.com/software/white-papers/wp-unicode/
   */
  
  { "eucJP",		"euc-jp"	},
  { "PCK",		"Shift_JIS"	},
  { "ko_KR-euc",	"euc-kr"	},
  { "zh_TW-big5",	"big5"		},

  /* seems to be common on some systems */

  { "sjis",		"Shift_JIS"	},

  
  /*
   * If you happen to encounter system-specific brain-damage with
   * respect to character set naming, please add it here, and
   * submit a patch to <mutt-dev@mutt.org>. 
   */

  /* End of aliases.  Please keep this line last. */
  
  { NULL, 		NULL		}
};

#ifdef HAVE_LANGINFO_CODESET
# include <langinfo.h>


void mutt_set_langinfo_charset (void)
{
  char buff[LONG_STRING];
  char buff2[LONG_STRING];
  
  strfcpy (buff, nl_langinfo (CODESET), sizeof (buff));
  mutt_canonical_charset (buff2, sizeof (buff2), buff);
  
  /* finally, set $charset */
  if (!(Charset = safe_strdup (buff2)))
    Charset = safe_strdup ("iso-8859-1");
}

#else

void mutt_set_langinfo_charset (void)
{
  Charset = safe_strdup ("iso-8859-1");
}

#endif

void mutt_canonical_charset (char *dest, size_t dlen, const char *name)
{
  size_t i;
  char *p;
  char scratch[LONG_STRING];

  /* catch some common iso-8859-something misspellings */
  if (!ascii_strncasecmp (name, "8859", 4) && name[4] != '-')
    snprintf (scratch, sizeof (scratch), "iso-8859-%s", name +4);
  else if (!ascii_strncasecmp (name, "8859-", 5))
    snprintf (scratch, sizeof (scratch), "iso-8859-%s", name + 5);
  else if (!ascii_strncasecmp (name, "iso8859", 7) && name[7] != '-')
    snprintf (scratch, sizeof (scratch), "iso_8859-%s", name + 7);
  else if (!ascii_strncasecmp (name, "iso8859-", 8))
    snprintf (scratch, sizeof (scratch), "iso_8859-%s", name + 8);
  else
    strfcpy (scratch, NONULL(name), sizeof (scratch));

  for (i = 0; PreferredMIMENames[i].key; i++)
    if (!ascii_strcasecmp (scratch, PreferredMIMENames[i].key) ||
	!mutt_strcasecmp (scratch, PreferredMIMENames[i].key))
    {
      strfcpy (dest, PreferredMIMENames[i].pref, dlen);
      return;
    }

  strfcpy (dest, scratch, dlen);

  /* for cosmetics' sake, transform to lowercase. */
  for (p = dest; *p; p++)
    *p = ascii_tolower (*p);
}

int mutt_chscmp (const char *s, const char *chs)
{
  char buffer[STRING];

  if (!s) return 0;

  mutt_canonical_charset (buffer, sizeof (buffer), s);
  return !ascii_strcasecmp (buffer, chs);
}


#ifndef HAVE_ICONV

iconv_t iconv_open (const char *tocode, const char *fromcode)
{
  return (iconv_t)(-1);
}

size_t iconv (iconv_t cd, ICONV_CONST **inbuf, size_t *inbytesleft,
	      char **outbuf, size_t *outbytesleft)
{
  return 0;
}

int iconv_close (iconv_t cd)
{
  return 0;
}

#endif /* !HAVE_ICONV */


/*
 * Like iconv_open, but canonicalises the charsets
 */

iconv_t mutt_iconv_open (const char *tocode, const char *fromcode, int flags)
{
  char tocode1[SHORT_STRING];
  char fromcode1[SHORT_STRING];
  char *tocode2, *fromcode2;
  char *tmp;

  iconv_t cd;
  
  mutt_canonical_charset (tocode1, sizeof (tocode1), tocode);
  /* BALSA: hooks not supported */
#ifndef LIBMUTT
  if ((flags & M_ICONV_HOOK_TO) && (tmp = mutt_charset_hook (tocode1)))
    mutt_canonical_charset (tocode1, sizeof (tocode1), tmp);
#endif
  mutt_canonical_charset (fromcode1, sizeof (fromcode1), fromcode);
 /* BALSA: hooks not supported */
#ifndef LIBMUTT
  if ((flags & M_ICONV_HOOK_FROM) && (tmp = mutt_charset_hook (fromcode1)))
    mutt_canonical_charset (fromcode1, sizeof (fromcode1), tmp);
#endif

  if ((cd = iconv_open (tocode1, fromcode1)) != (iconv_t) -1)
    return cd;
#ifndef LIBMUTT
  if ((tocode2 = mutt_iconv_hook (tocode1)) && (fromcode2 = mutt_iconv_hook (fromcode1)))
    return iconv_open (tocode2, fromcode2);
#endif
  
  return (iconv_t) -1;
}


/*
 * Like iconv, but keeps going even when the input is invalid
 * If you're supplying inrepls, the source charset should be stateless;
 * if you're supplying an outrepl, the target charset should be.
 */

size_t mutt_iconv (iconv_t cd, ICONV_CONST char **inbuf, size_t *inbytesleft,
		   char **outbuf, size_t *outbytesleft,
		   const char **inrepls, const char *outrepl)
{
  size_t ret = 0, ret1;
  ICONV_CONST char *ib = *inbuf;
  size_t ibl = *inbytesleft;
  char *ob = *outbuf;
  size_t obl = *outbytesleft;

  for (;;)
  {
    ret1 = iconv (cd, &ib, &ibl, &ob, &obl);
    if (ret1 != (size_t)-1)
      ret += ret1;
    if (ibl && obl && errno == EILSEQ)
    {
      if (inrepls)
      {
	/* Try replacing the input */
	const char **t;
	for (t = inrepls; *t; t++)
	{
	  ICONV_CONST char *ib1 = *t;
	  size_t ibl1 = strlen (*t);
	  char *ob1 = ob;
	  size_t obl1 = obl;
	  iconv (cd, &ib1, &ibl1, &ob1, &obl1);
	  if (!ibl1)
	  {
	    ++ib, --ibl;
	    ob = ob1, obl = obl1;
	    ++ret;
	    break;
	  }
	}
	if (*t)
	  continue;
      }
      if (outrepl)
      {
	/* Try replacing the output */
	int n = strlen (outrepl);
	if (n <= obl)
	{
	  memcpy (ob, outrepl, n);
	  ++ib, --ibl;
	  ob += n, obl -= n;
	  ++ret;
	  continue;
	}
      }
    }
    *inbuf = ib, *inbytesleft = ibl;
    *outbuf = ob, *outbytesleft = obl;
    return ret;
  }
}


/*
 * Convert a string
 * Used in rfc2047.c and rfc2231.c
 */

int mutt_convert_string (char **ps, const char *from, const char *to, int flags)
{
  iconv_t cd;
  const char *repls[] = { "\357\277\275", "?", 0 };
  char *s = *ps;

  if (!s || !*s)
    return 0;

  if (to && from && (cd = mutt_iconv_open (to, from, flags)) != (iconv_t)-1)
  {
    int len;
    ICONV_CONST char *ib;
    char *buf, *ob;
    size_t ibl, obl;
    const char **inrepls = 0;
    char *outrepl = 0;

    if (mutt_is_utf8 (to))
      outrepl = "\357\277\275";
    else if (mutt_is_utf8 (from))
      inrepls = repls;
    else
      outrepl = "?";
      
    len = strlen (s);
    ib = s, ibl = len + 1;
    obl = MB_LEN_MAX * ibl;
    ob = buf = safe_malloc (obl + 1);
    
    mutt_iconv (cd, &ib, &ibl, &ob, &obl, inrepls, outrepl);
    iconv_close (cd);

    *ob = '\0';

    safe_free ((void **) ps);
    *ps = buf;
    
    mutt_str_adjust (ps);
    return 0;
  }
  else
    return -1;
}


/*
 * FGETCONV stuff for converting a file while reading it
 * Used in sendlib.c for converting from mutt's Charset
 */

struct fgetconv_s
{
  FILE *file;
  iconv_t cd;
  char bufi[512];
  char bufo[512];
  char *p;
  char *ob;
  char *ib;
  size_t ibl;
  const char **inrepls;
};

struct fgetconv_not
{
  FILE *file;
  iconv_t cd;
};

FGETCONV *fgetconv_open (FILE *file, const char *from, const char *to, int flags)
{
  struct fgetconv_s *fc;
  iconv_t cd = (iconv_t)-1;
  static const char *repls[] = { "\357\277\275", "?", 0 };

  if (from && to)
    cd = mutt_iconv_open (to, from, flags);

  if (cd != (iconv_t)-1)
  {
    fc = safe_malloc (sizeof (struct fgetconv_s));
    fc->p = fc->ob = fc->bufo;
    fc->ib = fc->bufi;
    fc->ibl = 0;
    fc->inrepls = mutt_is_utf8 (to) ? repls : repls + 1;
  }
  else
    fc = safe_malloc (sizeof (struct fgetconv_not));
  fc->file = file;
  fc->cd = cd;
  return (FGETCONV *)fc;
}

int fgetconv (FGETCONV *_fc)
{
  struct fgetconv_s *fc = (struct fgetconv_s *)_fc;

  if (!fc)
    return EOF;
  if (fc->cd == (iconv_t)-1)
    return fgetc (fc->file);
  if (!fc->p)
    return EOF;
  if (fc->p < fc->ob)
    return (unsigned char)*(fc->p)++;

  /* Try to convert some more */
  fc->p = fc->ob = fc->bufo;
  if (fc->ibl)
  {
    size_t obl = sizeof (fc->bufo);
    iconv (fc->cd, (ICONV_CONST char **)&fc->ib, &fc->ibl, &fc->ob, &obl);
    if (fc->p < fc->ob)
      return (unsigned char)*(fc->p)++;
  }

  /* If we trusted iconv a bit more, we would at this point
   * ask why it had stopped converting ... */

  /* Try to read some more */
  if (fc->ibl == sizeof (fc->bufi) ||
      (fc->ibl && fc->ib + fc->ibl < fc->bufi + sizeof (fc->bufi)))
  {
    fc->p = 0;
    return EOF;
  }
  if (fc->ibl)
    memcpy (fc->bufi, fc->ib, fc->ibl);
  fc->ib = fc->bufi;
  fc->ibl += fread (fc->ib + fc->ibl, 1, sizeof (fc->bufi) - fc->ibl, fc->file);

  /* Try harder this time to convert some */
  if (fc->ibl)
  {
    size_t obl = sizeof (fc->bufo);
    mutt_iconv (fc->cd, (ICONV_CONST char **)&fc->ib, &fc->ibl, &fc->ob, &obl,
		fc->inrepls, 0);
    if (fc->p < fc->ob)
      return (unsigned char)*(fc->p)++;
  }

  /* Either the file has finished or one of the buffers is too small */
  fc->p = 0;
  return EOF;
}

void fgetconv_close (FGETCONV **_fc)
{
  struct fgetconv_s *fc = (struct fgetconv_s *) *_fc;

  if (fc->cd != (iconv_t)-1)
    iconv_close (fc->cd);
  safe_free ((void **) _fc);
}
