/*
 * Program:	UTF-8 routines
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	11 June 1997
 * Last Edited:	11 March 1998
 *
 * Copyright 1998 by the University of Washington
 *
 *  Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notices appear in all copies and that both the
 * above copyright notices and this permission notice appear in supporting
 * documentation, and that the name of the University of Washington not be
 * used in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  This software is made
 * available "as is", and
 * THE UNIVERSITY OF WASHINGTON DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED,
 * WITH REGARD TO THIS SOFTWARE, INCLUDING WITHOUT LIMITATION ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, AND IN
 * NO EVENT SHALL THE UNIVERSITY OF WASHINGTON BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, TORT
 * (INCLUDING NEGLIGENCE) OR STRICT LIABILITY, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */


#include <stdio.h>
#include <ctype.h>
#include "mail.h"
#include "osdep.h"
#include "misc.h"
#include "rfc822.h"
#include "utf8.h"

#include "iso_8859.c"		/* character set conversion tables */
#include "koi8_r.c"
#include "koi8_u.c"
#include "tis_620.c"
#include "gb_2312.c"
#include "jis_0208.c"
#include "jis_0212.c"
#include "ksc_5601.c"
#include "big5.c"
#include "cns11643.c"

/* Convert charset labelled sized text to UTF-8
 * Accepts: source sized text
 *	    character set
 *	    pointer to returned sized text if non-NIL
 *	    flags (currently non-zero if want error for unknown charset)
 * Returns: T if successful, NIL if failure
 */

long utf8_text (SIZEDTEXT *text,char *charset,SIZEDTEXT *ret,long flags)
{
  unsigned long i,j;
  unsigned char *s;
  unsigned int c,c1,ku,ten;
  if (!(charset && *charset) ||	/* no charset, or ASCII */
      (((charset[0] == 'U') || (charset[0] == 'u')) &&
       ((charset[1] == 'S') || (charset[1] == 's')) && (charset[2] == '-') &&
       ((charset[3] == 'A') || (charset[3] == 'a')) &&
       ((charset[4] == 'S') || (charset[4] == 's')) &&
       ((charset[5] == 'C') || (charset[5] == 'c')) &&
       ((charset[6] == 'I') || (charset[6] == 'i')) &&
       ((charset[7] == 'I') || (charset[7] == 'i')) && !charset[8])) {
    if (ret) {			/* special hack for untagged ISO-2022 */
      if (text->size > 2) for (i = 0; i < text->size - 1; i++)
	if ((text->data[i] == '\033') && (text->data[i+1] == '$'))
	  return utf8_iso2022text (text,ret);
      ret->data = text->data;	/* not ISO-2022, just return identity */
      ret->size = text->size;
    }
    return LONGT;
  }
				/* UTF-8 is always easy */
  else if (((charset[0] == 'U') || (charset[0] == 'u')) &&
	   ((charset[1] == 'T') || (charset[1] == 't')) &&
	   ((charset[2] == 'F') || (charset[2] == 'f')) &&
	   (charset[3] == '-') && (charset[4] == '8') && !charset[5]) {
    if (ret) {			/* that wasn't hard */
      ret->data = text->data;
      ret->size = text->size;
    }
    return LONGT;
  }

				/* if ISO character set */
  else if (((charset[0] == 'I') || (charset[0] == 'i')) &&
	   ((charset[1] == 'S') || (charset[1] == 's')) &&
	   ((charset[2] == 'O') || (charset[2] == 'o')) &&
	   (charset[3] == '-')) {
				/* ISO-8859-? */
    if ((charset[4] == '8') && (charset[5] == '8') && (charset[6] == '5') &&
	(charset[7] == '9') && (charset[8] == '-') && (charset[9] >= '1') &&
	(charset[9] <= '9') && !charset[10]) {
      if (ret) {
	if (charset[9] == '1'){	/* ISO-8859-1 is simplest case */
	  for (ret->size = i = 0; i < text->size;)
	    ret->size += (text->data[i++] & 0x80) ? 2 : 1;
	  s = ret->data = (unsigned char *) fs_get (ret->size + 1);
	  for (i = j = 0; i < text->size;) {
	    if ((c = text->data[i++]) & 0x80) {
	      *s++ = 0xc0 | ((c >> 6) & 0x3f);
	      *s++ = 0x80 | (c & 0x3f);
	    }
	    else *s++ = c;	/* ASCII character */
	  }
	}
	else {			/* do table lookup for other ISO-8859s */
	  unsigned short *tab = iso8859tab[charset[9] - '2'];
	  for (ret->size = i = 0; i < text->size;) {
	    c = tab[text->data[i++]];
	    ret->size += UTF8_SIZE (c);
	  }
	  s = ret->data = (unsigned char *) fs_get (ret->size + 1);
	  for (i = j = 0; i < text->size;) {
	    c = tab[text->data[i++]];
	    UTF8_PUT (s,c);	/* convert Unicode to UTF-8 */
	  }
	}
	if (((unsigned long) (s - ret->data)) != ret->size)
	  fatal ("ISO-8859 to UTF-8 botch");
      }
      return LONGT;
    }

				/* ISO-2022-?? charset */
    if ((charset[4] == '2') && (charset[5] == '0') && (charset[6] == '2') &&
	(charset[7] == '2') && (charset[8] == '-') &&
	((!charset[11] && (
#ifdef JISTOUNICODE
			   (((charset[9] == 'J') || (charset[9] == 'j')) &&
			    ((charset[10] == 'P') || (charset[10] == 'p'))) ||
#endif
#ifdef KSCTOUNICODE
			   (((charset[9] == 'K') || (charset[9] == 'k')) &&
			    ((charset[10] == 'R') || (charset[10] == 'r'))) ||
#endif
#ifdef GBTOUNICODE
#ifdef CNS1TOUNICODE
			   (((charset[9] == 'C') || (charset[9] == 'c')) &&
			    ((charset[10] == 'N') || (charset[10] == 'n'))) ||
#endif
#endif
			   0))
#ifdef JISTOUNICODE
#ifdef JIS0212TOUNICODE
	 || (!charset[13] && ((charset[9] == 'J') || (charset[9] == 'j')) &&
	     ((charset[10] == 'P') || (charset[10] == 'p')) &&
	     (charset[11] == '-') && ((charset[12] == '1')
#ifdef GBTOUNICODE
#ifdef KSCTOUNICODE
				      || (charset[12] == '2')
#endif
#endif
				      ))
#endif
#endif
	 )) return ret ? utf8_iso2022text (text,ret) : LONGT;
  }

#ifdef GBTOUNICODE
				/* GB2312 character set */
  else if ((((charset[0] == 'G') || (charset[0] == 'g')) &&
	    ((charset[1] == 'B') || (charset[1] == 'b')) &&
	    (charset[2] == '2') && (charset[3] == '3') &&
	    (charset[4] == '1') && (charset[5] == '2') && !charset[6]) ||
				/* CN-GB character set */
	   (((charset[0] == 'C') || (charset[0] == 'c')) &&
	    ((charset[1] == 'N') || (charset[1] == 'n')) &&
	    (charset[2] == '-') &&
	    ((charset[3] == 'G') || (charset[3] == 'g')) &&
	    ((charset[4] == 'B') || (charset[4] == 'b')) && !charset[5])) {
    if (ret) {
      for (ret->size = i = 0; i < text->size;) {
	if ((c = text->data[i++]) & 0x80)
	  c = ((i < text->size) && ((c1 = text->data[i++]) & 0x80)) ?
	    GBTOUNICODE (c,c1,ku,ten) : 0xfffd;
	ret->size += UTF8_SIZE (c);
      }
      s = ret->data = (unsigned char *) fs_get (ret->size + 1);
      for (i = 0; i < text->size;) {
	if ((c = text->data[i++]) & 0x80)
	  c = ((i < text->size) && ((c1 = text->data[i++]) & 0x80)) ?
	    GBTOUNICODE (c,c1,ku,ten) : 0xfffd;
	UTF8_PUT (s,c);		/* convert Unicode to UTF-8 */
      }
      if (((unsigned long) (s - ret->data)) != ret->size)
	fatal ("GB2312 to UTF-8 botch");
    }
    return LONGT;
  }	
#endif

#ifdef BIG5TOUNICODE
				/* BIG5 character set */
  else if ((((charset[0] == 'B') || (charset[0] == 'b')) &&
	    ((charset[1] == 'I') || (charset[1] == 'i')) &&
	    ((charset[2] == 'G') || (charset[2] == 'g')) &&
	    (charset[3] == '5') && !charset[4]) ||
				/* CN-BIG5 character set */
	   (((charset[0] == 'C') || (charset[0] == 'c')) &&
	    ((charset[1] == 'N') || (charset[1] == 'n')) &&
	    (charset[2] == '-') &&
	    ((charset[3] == 'B') || (charset[3] == 'b')) &&
	    ((charset[4] == 'I') || (charset[4] == 'i')) &&
	    ((charset[5] == 'G') || (charset[5] == 'g')) &&
	    (charset[6] == '5') && !charset[7])) {
    if (ret) {
      for (ret->size = i = 0; i < text->size;) {
	if ((c = text->data[i++]) & 0x80)
	  c = ((i < text->size) && (c1 = text->data[i++])) ?
	    BIG5TOUNICODE (c,c1,ku,ten) : 0xfffd;
	ret->size += UTF8_SIZE (c);
      }
      s = ret->data = (unsigned char *) fs_get (ret->size + 1);
      for (i = j = 0; i < text->size;) {
	if ((c = text->data[i++]) & 0x80)
	  c = ((i < text->size) && (c1 = text->data[i++])) ?
	    BIG5TOUNICODE (c,c1,ku,ten) : 0xfffd;
	UTF8_PUT (s,c);	/* convert Unicode to UTF-8 */
      }
      if (((unsigned long) (s - ret->data)) != ret->size)
	fatal ("BIG5 to UTF-8 botch");
    }
    return LONGT;
  }
#endif

				/* if EUC character set */
  else if (((charset[0] == 'E') || (charset[0] == 'e')) &&
	   ((charset[1] == 'U') || (charset[1] == 'u')) &&
	   ((charset[2] == 'C') || (charset[2] == 'c')) &&
	   (charset[3] == '-')) {
#ifdef JISTOUNICODE
				/* EUC-JP character set */
    if (((charset[4] == 'J') || (charset[4] == 'j')) &&
	((charset[5] == 'P') || (charset[5] == 'p')) && !charset[6]) {
      if (ret) {
	for (ret->size = i = 0; i < text->size;) {
	  if ((c = text->data[i++]) & 0x80) {
	    if ((i >= text->size) || !((c1 = text->data[i++]) & 0x80))
	      c = 0xfffd;	/* out of space or bogon */
	    else switch (c) {	/* check 8bit code set */
	    case 0x8f:		/* JIS X 0212-1990 */
#ifdef JIS0212TOUNICODDE
	      c = ((i < text->size) && ((c = text->data[i++]) & 0x80)) ?
		JIS0212TOUNICODE (c1,c,ku,ten) : 0xfffd;
#else
	      if (i < text->size) i++;
	      c = 0xfffd;	/* nothing to do but give up */
#endif
	      break;
	    case 0x8e:		/* half-width katakana */
	      c = ((c1 > 0xa0) && (c1 < 0xe0)) ? c1 + 0xfec0 : 0xfffd;
	      break;
	    default:
	      c = JISTOUNICODE (c,c1,ku,ten);
#ifdef JIS0212TOUNICODE
				/* special hack: merge 0208/0212 rows 2-9 */
	      if (!c && ku && (ku < 10))
		c = jis0212tab[ku - (BASE_JIS0212_KU - BASE_JIS0208_KU)][ten];
#endif
	    }
	  }
	  ret->size += UTF8_SIZE (c);
	}

	s = ret->data = (unsigned char *) fs_get (ret->size + 1);
	for (i = 0; i < text->size;) {
	  if ((c = text->data[i++]) & 0x80) {
	    if ((i >= text->size) || !((c1 = text->data[i++]) & 0x80))
	      c = 0xfffd;	/* out of space or bogon */
	    else switch (c) {	/* check 8bit code set */
	    case 0x8f:		/* JIS X 0212-1990 */
#ifdef JIS0212TOUNICODDE
	      c = ((i < text->size) && ((c = text->data[i++]) & 0x80)) ?
		JIS0212TOUNICODE (c1,c,ku,ten) : 0xfffd;
#else
	      if (i < text->size) i++;
	      c = 0xfffd;	/* nothing to do but give up */
#endif
	      break;
	    case 0x8e:		/* half-width katakana */
	      c = ((c1 > 0xa0) && (c1 < 0xe0)) ? c1 + 0xfec0 : 0xfffd;
	      break;
	    default:
	      c = JISTOUNICODE (c,c1,ku,ten);
#ifdef JIS0212TOUNICODE
				/* special hack: merge 0208/0212 rows 2-9 */
	      if (!c && ku && (ku < 10))
		c = jis0212tab[ku - (BASE_JIS0212_KU - BASE_JIS0208_KU)][ten];
#endif
	    }
	  }
	  UTF8_PUT (s,c);	/* convert Unicode to UTF-8 */
	}
	if (((unsigned long) (s - ret->data)) != ret->size)
	  fatal ("EUC-JP to UTF-8 botch");
      }
      return LONGT;
    }
#endif

#ifdef KSCTOUNICODE
				/* EUC-KR character set */
    if (((charset[4] == 'K') || (charset[4] == 'k')) &&
	((charset[5] == 'R') || (charset[5] == 'r')) && !charset[6]) {
      if (ret) {
	for (ret->size = i = 0; i < text->size;) {
	  /* ?? Do we need to worry about half-width hangul ?? */
	  if ((c = text->data[i++]) & 0x80)
	    c = ((i < text->size) && (c1 = text->data[i++])) ?
	      KSCTOUNICODE (c,c1,ku,ten) : 0xfffd;
	  ret->size += UTF8_SIZE (c);
	}
	s = ret->data = (unsigned char *) fs_get (ret->size + 1);
	for (i = 0; i < text->size;) {
	  if ((c = text->data[i++]) & 0x80)
	    c = ((i < text->size) && (c1 = text->data[i++])) ?
	      KSCTOUNICODE (c,c1,ku,ten) : 0xfffd;
	  UTF8_PUT (s,c);	/* convert Unicode to UTF-8 */
	}
	if (((unsigned long) (s - ret->data)) != ret->size)
	  fatal ("EUC-KR to UTF-8 botch");
      }
      return LONGT;
    }
#endif
  }

#ifdef JISTOUNICODE
#ifdef SJISTOJIS
				/* Shift-JIS, *sigh* */
  else if (((charset[0] == 'S') || (charset[0] == 's')) &&
	   ((charset[1] == 'H') || (charset[1] == 'h')) &&
	   ((charset[2] == 'I') || (charset[2] == 'i')) &&
	   ((charset[3] == 'F') || (charset[3] == 'f')) &&
	   ((charset[4] == 'T') || (charset[4] == 't')) &&
	   ((charset[5] == '_') || (charset[5] == '-')) &&
	   ((charset[6] == 'J') || (charset[6] == 'j')) &&
	   ((charset[7] == 'I') || (charset[7] == 'i')) &&
	   ((charset[8] == 'S') || (charset[8] == 's')) && !charset[9]) {
    if (ret) {
      for (ret->size = i = 0; i < text->size;) {
	if ((c = text->data[i++]) & 0x80) {
				/* half-width katakana */
	  if ((c > 0xa0) && (c < 0xe0)) c += 0xfec0;
	  else if (i >= text->size) c = 0xfffd;
	  else {		/* Shift-JIS */
	    c1 = text->data[i++];
	    SJISTOJIS (c,c1);
	    c = JISTOUNICODE (c,c1,ku,ten);
	  }
	}
	ret->size += UTF8_SIZE (c);
      }
      s = ret->data = (unsigned char *) fs_get (ret->size + 1);
      for (i = 0; i < text->size;) {
	if ((c = text->data[i++]) & 0x80) {
				/* half-width katakana */
	  if ((c > 0xa0) && (c < 0xe0)) c += 0xfec0;
	  else {		/* Shift-JIS */
	    c1 = text->data[i++];
	    SJISTOJIS (c,c1);
	    c = JISTOUNICODE (c,c1,ku,ten);
	  }
	}
	UTF8_PUT (s,c);		/* convert Unicode to UTF-8 */
      }
      if (((unsigned long) (s - ret->data)) != ret->size)
	fatal ("Shift-JIS to UTF-8 botch");
    }
    return LONGT;
  }
#endif
#endif

				/* KOI8-R or KOI8-U character set */
  else if (((charset[0] == 'K') || (charset[0] == 'k')) &&
	   ((charset[1] == 'O') || (charset[1] == 'o')) &&
	   ((charset[2] == 'I') || (charset[2] == 'i')) &&
	   (charset[3] == '8') && (charset[4] == '-')) {
    if (((charset[5] == 'R') || (charset[5] == 'r')) && !charset[6]) {
      if (ret) {
	for (ret->size = i = 0; i < text->size;) {
	  c = koi8rtab[text->data[i++]];
	  ret->size += UTF8_SIZE (c);
	}
	s = ret->data = (unsigned char *) fs_get (ret->size + 1);
	for (i = j = 0; i < text->size;) {
	  c = koi8rtab[text->data[i++]];
	  UTF8_PUT (s,c);	/* convert Unicode to UTF-8 */
	}
	if (((unsigned long) (s - ret->data)) != ret->size)
	  fatal ("KOI8-R to UTF-8 botch");
      }
      return LONGT;
    }
				/* KOI8-U character set */
    if ((((charset[5] == 'U') || (charset[5] == 'u')) && !charset[6]) ||
				/* KOI8-RU character set */
	(((charset[5] == 'R') || (charset[5] == 'r')) &&
	 ((charset[6] == 'U') || (charset[6] == 'u')) && !charset[7])) {
      if (ret) {
	for (ret->size = i = 0; i < text->size;) {
	  c = koi8utab[text->data[i++]];
	  ret->size += UTF8_SIZE (c);
	}
	s = ret->data = (unsigned char *) fs_get (ret->size + 1);
	for (i = j = 0; i < text->size;) {
	  c = koi8utab[text->data[i++]];
	  UTF8_PUT (s,c);	/* convert Unicode to UTF-8 */
	}
	if (((unsigned long) (s - ret->data)) != ret->size)
	  fatal ("KOI8-RU to UTF-8 botch");
      }
      return LONGT;
    }
  }

				/* TIS-620 character set */
  else if (((charset[0] == 'T') || (charset[0] == 't')) &&
	   ((charset[1] == 'I') || (charset[1] == 'i')) &&
	   ((charset[2] == 'S') || (charset[2] == 's')) &&
	   (charset[3] == '-') && (charset[4] == '6') &&
	   (charset[5] == '2') && (charset[6] == '0') && !charset[7]) {
    if (ret) {
      for (ret->size = i = 0; i < text->size;) {
	c = tis620tab[text->data[i++]];
	ret->size += UTF8_SIZE (c);
      }
      s = ret->data = (unsigned char *) fs_get (ret->size + 1);
      for (i = j = 0; i < text->size;) {
	c = tis620tab[text->data[i++]];
	UTF8_PUT (s,c);		/* convert Unicode to UTF-8 */
      }
      if (((unsigned long) (s - ret->data)) != ret->size)
	fatal ("TIS-620 to UTF-8 botch");
    }
    return LONGT;
  }
	/* add support for other character sets here */
  if (flags) {
    char tmp[MAILTMPLEN];
    sprintf (tmp,"Unknown character set: %.80s",charset);
    mm_log (tmp,ERROR);
  }
  return NIL;			/* failed */
}

/* Convert charset labelled searchpgm to UTF-8 in place
 * Accepts: search program
 *	    character set
 */

void utf8_searchpgm (SEARCHPGM *pgm,char *charset)
{
  SIZEDTEXT txt;
  SEARCHHEADER *hl;
  SEARCHOR *ol;
  SEARCHPGMLIST *pl;
  if (pgm) {			/* must have a search program */
    utf8_stringlist (pgm->bcc,charset);
    utf8_stringlist (pgm->cc,charset);
    utf8_stringlist (pgm->from,charset);
    utf8_stringlist (pgm->to,charset);
    utf8_stringlist (pgm->subject,charset);
    for (hl = pgm->header; hl; hl = hl->next) {
      if (utf8_text (&hl->line,charset,&txt,NIL)) {
	fs_give ((void **) &hl->line.data);
	hl->line.data = txt.data;
	hl->line.size = txt.size;
      }
      if (utf8_text (&hl->text,charset,&txt,NIL)) {
	fs_give ((void **) &hl->text.data);
	hl->text.data = txt.data;
	hl->text.size = txt.size;
      }
    }
    utf8_stringlist (pgm->body,charset);
    utf8_stringlist (pgm->text,charset);
    for (ol = pgm->or; ol; ol = ol->next) {
      utf8_searchpgm (ol->first,charset);
      utf8_searchpgm (ol->second,charset);
    }
    for (pl = pgm->not; pl; pl = pl->next) utf8_searchpgm (pl->pgm,charset);
  }
}


/* Convert charset labelled stringlist to UTF-8 in place
 * Accepts: string list
 *	    character set
 */

void utf8_stringlist (STRINGLIST *st,char *charset)
{
  SIZEDTEXT txt;
				/* convert entire stringstruct */
  if (st) do if (utf8_text (&st->text,charset,&txt,NIL)) {
    fs_give ((void **) &st->text.data);
    st->text.data = txt.data; /* transfer this text */
    st->text.size = txt.size;
  } while (st = st->next);
}

/* Convert ISO-2022 sized text to UTF-8
 * Accepts: source sized text
 *	    pointer to returned sized text
 * Returns: T if successful, NIL if failure
 */

long utf8_iso2022text (SIZEDTEXT *text,SIZEDTEXT *ret)
{
  unsigned long i;
  unsigned char *s;
  unsigned int pass,state,c,co,gi,gl,gr,g[4],ku,ten;
  for (pass = 0,ret->size = 0; pass <= 1; pass++) {
    gi = 0;			/* quell compiler warnings */
    state = I2S_CHAR;		/* initialize engine */
    g[0]= g[2] = I2CS_ASCII;	/* G0 and G2 are ASCII */
    g[1]= g[3] = I2CS_ISO8859_1;/* G1 and G3 are ISO-8850-1 */
    gl = I2C_G0; gr = I2C_G1;	/* left is G0, right is G1 */
    for (i = 0; i < text->size;) {
      c = text->data[i++];
      switch (state) {		/* dispatch based upon engine state */
      case I2S_ESC:		/* ESC seen */
	switch (c) {		/* process intermediate character */
	case I2C_MULTI:		/* multibyte character? */
	  state = I2S_MUL;	/* mark multibyte flag seen */
	  break;
        case I2C_SS2:		/* single shift GL to G2 */
	  gl |= I2C_SG2;
	  break;
        case I2C_SS3:		/* single shift GL to G3 */
	  gl |= I2C_SG3;
	  break;
	case I2C_G0_94: case I2C_G1_94: case I2C_G2_94:	case I2C_G3_94:
	  g[gi = c - I2C_G0_94] = (state == I2S_MUL) ? I2CS_94x94 : I2CS_94;
	  state = I2S_INT;	/* ready for character set */
	  break;
	case I2C_G0_96:	case I2C_G1_96: case I2C_G2_96:	case I2C_G3_96:
	  g[gi = c - I2C_G0_96] = (state == I2S_MUL) ? I2CS_96x96 : I2CS_96;
	  state = I2S_INT;	/* ready for character set */
	  break;
	default:		/* bogon */
	  if (pass) *s++ = I2C_ESC,*s++ = c;
	  else ret->size += 2;
	  state = I2S_CHAR;	/* return to previous state */
	}
	break;

      case I2S_MUL:		/* ESC $ */
	switch (c) {		/* process multibyte intermediate character */
	case I2C_G0_94: case I2C_G1_94: case I2C_G2_94:	case I2C_G3_94:
	  g[gi = c - I2C_G0_94] = I2CS_94x94;
	  state = I2S_INT;	/* ready for character set */
	  break;
	case I2C_G0_96:	case I2C_G1_96: case I2C_G2_96:	case I2C_G3_96:
	  g[gi = c - I2C_G0_96] = I2CS_96x96;
	  state = I2S_INT;	/* ready for character set */
	  break;
	default:		/* probably omitted I2CS_94x94 */
	  g[gi = I2C_G0] = I2CS_94x94 | c;
	  state = I2S_CHAR;	/* return to character state */
	}
	break;
      case I2S_INT:
	state = I2S_CHAR;	/* return to character state */
	g[gi] |= c;		/* set character set */
	break;

      case I2S_CHAR:		/* character data */
	switch (c) {
	case I2C_ESC:		/* ESC character */
	  state = I2S_ESC;	/* see if ISO-2022 prefix */
	  break;
	case I2C_SI:		/* shift GL to G0 */
	  gl = I2C_G0;
	  break;
	case I2C_SO:		/* shift GL to G1 */
	  gl = I2C_G1;
	  break;
#if 0				/* don't have these defined */
        case I2C_LS2:		/* shift GL to G2 */
	  gl = I2C_G2;
	  break;
        case I2C_LS3:		/* shift GL to G3 */
	  gl = I2C_G3;
	  break;
        case LS1R:		/* shift GR to G1 */
	  gr = I2C_G1;
	  break;
        case LS2R:		/* shift GR to G2 */
	  gr = I2C_G2;
	  break;
        case LS3R:		/* shift GR to G3 */
	  gr = I2C_G3;
	  break;
#endif

	default:		/* ordinary character */
	  co = c;		/* note original character */
	  if (gl & (3 << 2)) {	/* single shifted? */
	    gi = g[gl >> 2];	/* get shifted character set */
	    gl &= 0x3;		/* cancel shift */
	  }
	  else if (c & 0x80) {	/* right half? */
	    gi = g[gr];		/* yes, use right */
	    c &= 0x7f;		/* make 7-bit */
	  }
	  else gi = g[gl];	/* left half */
	  switch (gi) {		/* interpret in character set */
	  case I2CS_ASCII:	/* ASCII */
	    break;		/* easy! */
	  case I2CS_JIS_ROMAN:	/* JIS Roman */
	  case I2CS_JIS_BUGROM:	/* old bugs */
	    switch (c) {	/* two exceptions to ASCII */
	    case 0x5c:		/* Yen sign */
	      c = 0x00a5;
	      break;
	    case 0x7e:		/* overline */
	      c = 0x203e;
	      break;
	    }
	    break;
	  case I2CS_JIS_KANA:	/* JIS hankaku katakana */
	    if ((c >= 0x21) && (c <= 0x5f)) c += 0xff40;
	    break;
	  case I2CS_ISO8859_1:	/* Latin-1 */
	    c |= 0x80;		/* just turn on high bit */
	    break;
	  case I2CS_ISO8859_2:	/* Latin-2 */
	    c = iso8859tab[0][c | 0x80];
	    break;
	  case I2CS_ISO8859_3:	/* Latin-3 */
	    c = iso8859tab[1][c | 0x80];
	    break;
	  case I2CS_ISO8859_4:	/* Latin-4 */
	    c = iso8859tab[2][c | 0x80];
	    break;
	  case I2CS_ISO8859_5:	/* Cyrillic */
	    c = iso8859tab[3][c | 0x80];
	    break;
	  case I2CS_ISO8859_6:	/* Arabic */
	    c = iso8859tab[4][c | 0x80];
	    break;
	  case I2CS_ISO8859_7:	/* Greek */
	    c = iso8859tab[5][c | 0x80];
	    break;
	  case I2CS_ISO8859_8:	/* Hebrew */
	    c = iso8859tab[6][c | 0x80];
	    break;
	  case I2CS_ISO8859_9:	/* Latin-5 */
	    c = iso8859tab[7][c | 0x80];
	    break;

	  default:		/* all other character sets */
				/* multibyte character set */
	    if ((gi & I2CS_MUL) && !(c & 0x80) && (c > 0x20)) {
	      c = (i < text->size) ? text->data[i++] : 0;
	      switch (gi) {
#ifdef GBTOUNICODE
	      case I2CS_GB:	/* GB 2312 */
		c = GBTOUNICODE (co,c,ku,ten);
		break;
#endif
#ifdef JISTOUNICODE
	      case I2CS_JIS_OLD:/* JIS X 0208-1978 */
	      case I2CS_JIS_NEW:/* JIS X 0208-1983 */
		c = JISTOUNICODE (co,c,ku,ten);
		break;
#endif
#ifdef JIS0212TOUNICODE
	      case I2CS_JIS_EXT:/* JIS X 0212-1990 */
		c = JIS0212TOUNICODE (co,c,ku,ten);
		break;
#endif
#ifdef KSCTOUNICODE
	      case I2CS_KSC:	/* KSC 5601 */
		co |= 0x80;	/* make into EUC */
		c |= 0x80;
		c = KSCTOUNICODE (co,c,ku,ten);
		break;
#endif
#ifdef CNS1TOUNICODE
	      case I2CS_CNS1:	/* CNS 11643 plane 1 */
		c = CNS1TOUNICODE (co,c,ku,ten);
		break;
#endif
#ifdef CNS2TOUNICODE
	      case I2CS_CNS2:	/* CNS 11643 plane 2 */
		c = CNS2TOUNICODE (co,c,ku,ten);
		break;
#endif
	      default:		/* unknown multibyte, treat as UCS-2 */
		c |= (co << 8);	/* wrong, but nothing else to do */
		break;
	      }
	    }
	    else c = co;	/* unknown single byte, treat as 8859-1 */
	  }
	  if (pass) UTF8_PUT (s,c)
	  else ret->size += UTF8_SIZE (c);
	}
      }
    }
    if (!pass) s = ret->data = (unsigned char *) fs_get (ret->size + 1);
    else if (((unsigned long) (s - ret->data)) != ret->size)
      fatal ("ISO-2022 to UTF-8 botch");
  }
  return LONGT;
}

/* Convert MIME-2 sized text to UTF-8
 * Accepts: source sized text
 *	    character set
 * Returns: T if successful, NIL if failure
 */

#define MINENCWORD 9

long utf8_mime2text (SIZEDTEXT *src,SIZEDTEXT *dst)
{
  unsigned char *s,*se,*e,*ee,*t,*te;
  char *cs,*ce,*ls;
  SIZEDTEXT txt,rtxt;
  unsigned long i;
  dst->data = NIL;		/* default is no encoded words */
				/* look for encoded words */
  for (s = src->data, se = src->data + src->size; s < se; s++) {
    if (((se - s) > MINENCWORD) && (*s == '=') && (s[1] == '?') &&
      (cs = (char *) mime2_token (s+2,se,(unsigned char **) &ce)) &&
	(e = mime2_token ((unsigned char *) ce+1,se,&ee)) &&
	(t = mime2_text (e+2,se,&te)) && (ee == e + 1)) {
      if (mime2_decode (e,t,te,&txt)) {
	*ce = '\0';		/* temporarily tie off character set */
	if (ls = strchr (cs,'*')) *ls = '\0';
	if (utf8_text (&txt,cs,&rtxt,NIL)) {
	  if (!dst->data) {	/* need to create buffer now? */
				/* allocate for worst case */
	    dst->data = (unsigned char *)
	      fs_get ((size_t) ((src->size / 8) + 1) * 9);
	    memcpy (dst->data,src->data,(size_t) (dst->size = s - src->data));
	  }
	  for (i=0; i < rtxt.size; i++) dst->data[dst->size++] = rtxt.data[i];
				/* all done with converted text */
	  if (rtxt.data != txt.data) fs_give ((void **) &rtxt.data);
	}
	if (ls) *ls = '*';	/* restore language tag delimiter */
	*ce = '?';		/* restore character set delimiter */
				/* all done with decoded text */
	fs_give ((void **) &txt.data);
	s = te+1;		/* continue scan after encoded word */

				/* skip leading whitespace */
	for (t = s + 1; (t < se) && ((*t == ' ') || (*t == '\t')); t++);
				/* see if likely continuation encoded word */
	if (t < (se - MINENCWORD)) switch (*t) {
	case '=':		/* possible encoded word? */
	  if (t[1] == '?') s = t - 1;
	  break;
	case '\015':		/* CR, eat a following LF */
	  if (t[1] == '\012') t++;
	case '\012':		/* possible end of logical line */
	  if ((t[1] == ' ') || (t[1] == '\t')) {
	    do t++;
	    while ((t < (se - MINENCWORD)) && ((t[1] == ' ')||(t[1] == '\t')));
	    if ((t < (se - MINENCWORD)) && (t[1] == '=') && (t[2] == '?'))
	      s = t;		/* definitely looks like continuation */
	  }
	}
      }
      else {			/* restore original text */
	if (dst->data) fs_give ((void **) &dst->data);
	dst->data = src->data;
	dst->size = src->size;
	return NIL;		/* syntax error: MIME-2 decoding failure */
      }
    }
				/* stash ordinary character */
    else if (dst->data) dst->data[dst->size++] = *s;
  }
  if (dst->data) dst->data[dst->size] = '\0';
  else {			/* nothing converted, return identity */
    dst->data = src->data;
    dst->size = src->size;
  }
  return T;			/* success */
}

/* Decode MIME-2 text
 * Accepts: Encoding
 *	    text
 *	    text end
 *	    destination sized text
 * Returns: T if successful, else NIL
 */

long mime2_decode (unsigned char *e,unsigned char *t,unsigned char *te,
		   SIZEDTEXT *txt)
{
  unsigned char *q;
  txt->data = NIL;		/* initially no returned data */
  switch (*e) {			/* dispatch based upon encoding */
  case 'Q': case 'q':		/* sort-of QUOTED-PRINTABLE */
    txt->data = (unsigned char *) fs_get ((size_t) (te - t) + 1);
    for (q = t,txt->size = 0; q < te; q++) switch (*q) {
    case '=':			/* quoted character */
				/* both must be hex */
      if (!isxdigit (q[1]) || !isxdigit (q[2])) {
	fs_give ((void **) &txt->data);
	return NIL;		/* syntax error: bad quoted character */
      }
      txt->data[txt->size++] =	/* assemble character */
	((q[1] - (isdigit (q[1]) ? '0' :
		  ((isupper (q[1]) ? 'A' : 'a') - 10))) << 4) +
		    (q[2] - (isdigit (q[2]) ? '0' :
			     ((isupper (q[2]) ? 'A' : 'a') - 10)));
      q += 2;			/* advance past quoted character */
      break;
    case '_':			/* convert to 0x20 */
      txt->data[txt->size++] = 0x20;
      break;
    default:			/* ordinary character */
      txt->data[txt->size++] = *q;
      break;
    }
    txt->data[txt->size] = '\0';
    break;
  case 'B': case 'b':		/* BASE64 */
    if (txt->data = rfc822_base64 (t,te - t,&txt->size)) break;
  default:			/* any other encoding is unknown */
    return NIL;			/* syntax error: unknown encoding */
  }
  return T;
}

/* Get MIME-2 token from encoded word
 * Accepts: current text pointer
 *	    text limit pointer
 *	    pointer to returned end pointer
 * Returns: current text pointer & end pointer if success, else NIL
 */

unsigned char *mime2_token (unsigned char *s,unsigned char *se,
			    unsigned char **t)
{
  for (*t = s; **t != '?'; ++*t) {
    if ((*t < se) && isgraph (**t)) switch (**t) {
    case '(': case ')': case '<': case '>': case '@': case ',': case ';':
    case ':': case '\\': case '"': case '/': case '[': case ']': case '.':
    case '=':
      return NIL;		/* none of these are valid in tokens */
    }
    else return NIL;		/* out of text or CTL or space */
  }
  return s;
}


/* Get MIME-2 text from encoded word
 * Accepts: current text pointer
 *	    text limit pointer
 *	    pointer to returned end pointer
 * Returns: current text pointer & end pointer if success, else NIL
 */

unsigned char *mime2_text (unsigned char *s,unsigned char *se,
			   unsigned char **t)
{
				/* make sure valid, search for closing ? */
  for (*t = s; **t != '?'; ++*t) if ((*t >= se) || !isgraph (**t)) return NIL;
				/* make sure terminated properly */
  if ((*t)[1] != '=') return NIL;
  return s;
}
