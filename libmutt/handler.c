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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "mutt.h"
#include "mutt_curses.h"
#include "rfc1524.h"
#include "keymap.h"
#include "mime.h"
#include "copy.h"
#include "charset.h"
#include "ascii.h"


#ifdef HAVE_PGP
#include "pgp.h"
#endif

#ifdef LIBMUTT
#define COLS 80
#define _(a) (a)
#define FALSE 0
#endif

#define BUFI_SIZE 1000
#define BUFO_SIZE 2000


typedef void handler_f (BODY *, STATE *);
typedef handler_f *handler_t;

int Index_hex[128] = {
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
     0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,
    -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

int Index_64[128] = {
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};

static void state_prefix_put (const char *d, size_t dlen, STATE *s)
{
  if (s->prefix)
    while (dlen--)
      state_prefix_putc (*d++, s);
  else
    fwrite (d, dlen, 1, s->fpout);
}

static void convert_to_state(iconv_t cd, char *bufi, size_t *l, STATE *s)
{
  char bufo[BUFO_SIZE];
  ICONV_CONST char *ib;
  char *ob;
  size_t ibl, obl;

  if (!bufi)
  {
    if (cd != (iconv_t)(-1))
    {
      ob = bufo, obl = sizeof (bufo);
      iconv (cd, 0, 0, &ob, &obl);
      if (ob != bufo)
	state_prefix_put (bufo, ob - bufo, s);
    }
    return;
  }

  if (cd == (iconv_t)(-1))
  {
    state_prefix_put (bufi, *l, s);
    *l = 0;
    return;
  }

  ib = bufi, ibl = *l;
  for (;;)
  {
    ob = bufo, obl = sizeof (bufo);
    mutt_iconv (cd, &ib, &ibl, &ob, &obl, 0, "?");
    if (ob == bufo)
      break;
    state_prefix_put (bufo, ob - bufo, s);
  }
  memmove (bufi, ib, ibl);
  *l = ibl;
}

void mutt_decode_xbit (STATE *s, long len, int istext, iconv_t cd)
{
  int c, ch;
  char bufi[BUFI_SIZE];
  size_t l = 0;

  if (istext)
  {
    state_set_prefix(s);

    while ((c = fgetc(s->fpin)) != EOF && len--)
    {
      if(c == '\r' && len)
      {
	if((ch = fgetc(s->fpin)) == '\n')
	{
	  c = ch;
	  len--;
	}
	else 
	  ungetc(ch, s->fpin);
      }

      bufi[l++] = c;
      if (l == sizeof (bufi))
	convert_to_state (cd, bufi, &l, s);
    }

    convert_to_state (cd, bufi, &l, s);
    convert_to_state (cd, 0, 0, s);

    state_reset_prefix (s);
  }
  else
    mutt_copy_bytes (s->fpin, s->fpout, len);
}

static int qp_decode_triple (char *s, char *d)
{
  /* soft line break */
  if (*s == '=' && !(*(s+1)))
    return 1;
  
  /* quoted-printable triple */
  if (*s == '=' && isxdigit (*(s+1)) && isxdigit (*(s+2)))
  {
    *d = (hexval (*(s+1)) << 4) | hexval (*(s+2));
    return 0;
  }
  
  /* something else */
  return -1;
}

static void qp_decode_line (char *dest, char *src, size_t *l,
			    int last)
{
  char *d, *s;
  char c;

  int kind;
  int soft = 0;

  /* chop trailing whitespace */

  if (*src)
  {
    s = src + strlen(src) - 1;
    while (s >= src && ISSPACE(*s))
      s--;
    *(++s) = '\0';
  }

  /* decode the line */
  
  for (d = dest, s = src; *s;)
  {
    switch ((kind = qp_decode_triple (s, &c)))
    {
      case  0: *d++ = c; s += 3; break;	/* qp triple */
      case -1: *d++ = *s++;      break; /* single character */
      case  1: soft = 1; s++;	 break; /* soft line break */
    }
  }

  if (!soft && last == '\n')
    *d++ = '\n';
  
  *d = '\0';
  *l = d - dest;
}

/* 
 * Decode an attachment encoded with quoted-printable.
 * 
 * Why doesn't this overflow any buffers?  First, it's guaranteed
 * that the length of a line grows when you _en_-code it to
 * quoted-printable.  That means that we always can store the
 * result in a buffer of at most the _same_ size.
 * 
 * Now, we don't special-case if the line we read with fgets()
 * isn't terminated.  We don't care about this, since STRING > 78,
 * so corrupted input will just be corrupted a bit more.  That
 * implies that STRING+1 bytes are always sufficient to store the
 * result of qp_decode_line.
 * 
 * Finally, at soft line breaks, some part of a multibyte character
 * may have been left over by convert_to_state().  This shouldn't
 * be more than 6 characters, so STRING + 7 should be sufficient
 * memory to store the decoded data.
 * 
 * Just to make sure that I didn't make some off-by-one error
 * above, we just use STRING*2 for the target buffer's size.
 * 
 */

void mutt_decode_quoted (STATE *s, long len, int istext, iconv_t cd)
{
  char line[STRING];
  char decline[2*STRING];
  size_t l = 0;
  size_t l2;
  size_t l3;

  int last;

  if (istext)
    state_set_prefix(s);

  while (len > 0)
  {
    last = 0;
    
    if (fgets (line, MIN (sizeof (line), len + 1), s->fpin) == NULL)
      break;

    len -= (l2 = strlen (line));

    /* 
     * Skip over pending input data until end of line - at this
     * point, the only thing we may see is trailing whitespace,
     * i.e. garbage.
     */

    if (l2 && (last = line[l2 - 1]) != '\n')
      while (len > 0 && (last = fgetc (s->fpin)) != '\n')
 	len--;
    
    /* 
     * decode and do character set conversion
     */
    
    qp_decode_line (decline + l, line, &l3, last);
    l += l3;
    convert_to_state (cd, decline, &l, s);
  }

  convert_to_state (cd, 0, 0, s);
  state_reset_prefix(s);
}

void mutt_decode_base64 (STATE *s, long len, int istext, iconv_t cd)
{
  char buf[5];
  int c1, c2, c3, c4, ch, cr = 0, i;
  char bufi[BUFI_SIZE];
  size_t l = 0;

  buf[4] = 0;

  if (istext) 
    state_set_prefix(s);

  while (len > 0)
  {
    for (i = 0 ; i < 4 && len > 0 ; len--)
    {
      if ((ch = fgetc (s->fpin)) == EOF)
	return;
      if (!ISSPACE (ch))
	buf[i++] = ch;
    }
    if (i != 4)
    {
      dprint (2, (debugfile, "%s:%d [mutt_decode_base64()]: "
		  "didn't get a multiple of 4 chars.\n", __FILE__, __LINE__));
      break;
    }

    c1 = base64val (buf[0]);
    c2 = base64val (buf[1]);
    ch = (c1 << 2) | (c2 >> 4);

    if (cr && ch != '\n') 
      bufi[l++] = '\r';

    cr = 0;
      
    if (istext && ch == '\r')
      cr = 1;
    else
      bufi[l++] = ch;

    if (buf[2] == '=')
      break;
    c3 = base64val (buf[2]);
    ch = ((c2 & 0xf) << 4) | (c3 >> 2);

    if (cr && ch != '\n')
      bufi[l++] = '\r';

    cr = 0;

    if (istext && ch == '\r')
      cr = 1;
    else
      bufi[l++] = ch;

    if (buf[3] == '=') break;
    c4 = base64val (buf[3]);
    ch = ((c3 & 0x3) << 6) | c4;

    if (cr && ch != '\n')
      bufi[l++] = '\r';
    cr = 0;

    if (istext && ch == '\r')
      cr = 1;
    else
      bufi[l++] = ch;
    
    if (l + 8 >= sizeof (bufi))
      convert_to_state (cd, bufi, &l, s);
  }

  if (cr) bufi[l++] = '\r';

  convert_to_state (cd, bufi, &l, s);
  convert_to_state (cd, 0, 0, s);

  state_reset_prefix(s);
}

unsigned char decode_byte (char ch)
{
  if (ch == 96)
    return 0;
  return ch - 32;
}

void mutt_decode_uuencoded (STATE *s, long len, int istext, iconv_t cd)
{
  char tmps[SHORT_STRING];
  char linelen, c, l, out;
  char *pt;
  char bufi[BUFI_SIZE];
  size_t k = 0;

  if(istext)
    state_set_prefix(s);
  
  while(len > 0)
  {
    if ((fgets(tmps, sizeof(tmps), s->fpin)) == NULL)
      return;
    len -= mutt_strlen(tmps);
    if ((!mutt_strncmp (tmps, "begin", 5)) && ISSPACE (tmps[5]))
      break;
  }
  while(len > 0)
  {
    if ((fgets(tmps, sizeof(tmps), s->fpin)) == NULL)
      return;
    len -= mutt_strlen(tmps);
    if (!mutt_strncmp (tmps, "end", 3))
      break;
    pt = tmps;
    linelen = decode_byte (*pt);
    pt++;
    for (c = 0; c < linelen;)
    {
      for (l = 2; l <= 6; l += 2)
      {
	out = decode_byte (*pt) << l;
	pt++;
	out |= (decode_byte (*pt) >> (6 - l));
	bufi[k++] = out;
	c++;
	if (c == linelen)
	  break;
      }
      convert_to_state (cd, bufi, &k, s);
      pt++;
    }
  }

  convert_to_state (cd, bufi, &k, s);
  convert_to_state (cd, 0, 0, s);
  
  state_reset_prefix(s);
}

/* ----------------------------------------------------------------------------
 * A (not so) minimal implementation of RFC1563.
 */


#define IndentSize (4)
    
enum { RICH_PARAM=0, RICH_BOLD, RICH_UNDERLINE, RICH_ITALIC, RICH_NOFILL, 
  RICH_INDENT, RICH_INDENT_RIGHT, RICH_EXCERPT, RICH_CENTER, RICH_FLUSHLEFT,
  RICH_FLUSHRIGHT, RICH_COLOR, RICH_LAST_TAG };

static struct {
  const char *tag_name;
  int index;
} EnrichedTags[] = {
  { "param",		RICH_PARAM },
  { "bold",		RICH_BOLD },
  { "italic",		RICH_ITALIC },
  { "underline",	RICH_UNDERLINE },
  { "nofill",		RICH_NOFILL },
  { "excerpt",		RICH_EXCERPT },
  { "indent",		RICH_INDENT },
  { "indentright",	RICH_INDENT_RIGHT },
  { "center",		RICH_CENTER },
  { "flushleft",	RICH_FLUSHLEFT },
  { "flushright",	RICH_FLUSHRIGHT },
  { "flushboth",	RICH_FLUSHLEFT },
  { "color",		RICH_COLOR },
  { "x-color",		RICH_COLOR },
  { NULL,		-1 }
};

struct enriched_state
{
  char *buffer;
  char *line;
  char *param;
  size_t buff_len;
  size_t line_len;
  size_t line_used;
  size_t line_max;
  size_t indent_len;
  size_t word_len;
  size_t buff_used;
  size_t param_used;
  size_t param_len;
  int tag_level[RICH_LAST_TAG];
  int WrapMargin;
  STATE *s;
};

static void enriched_wrap (struct enriched_state *stte)
{
  int x;
  int extra;

  if (stte->line_len)
  {
    if (stte->tag_level[RICH_CENTER] || stte->tag_level[RICH_FLUSHRIGHT])
    {
      /* Strip trailing white space */
      size_t y = stte->line_used - 1;

      while (y && ISSPACE (stte->line[y]))
      {
	stte->line[y] = '\0';
	y--;
	stte->line_used--;
	stte->line_len--;
      }
      if (stte->tag_level[RICH_CENTER])
      {
	/* Strip leading whitespace */
	y = 0;

	while (stte->line[y] && ISSPACE (stte->line[y]))
	  y++;
	if (y)
	{
	  size_t z;

	  for (z = y ; z <= stte->line_used; z++)
	  {
	    stte->line[z - y] = stte->line[z];
	  }

	  stte->line_len -= y;
	  stte->line_used -= y;
	}
      }
    }

    extra = stte->WrapMargin - stte->line_len - stte->indent_len -
      (stte->tag_level[RICH_INDENT_RIGHT] * IndentSize);
    if (extra > 0) 
    {
      if (stte->tag_level[RICH_CENTER]) 
      {
	x = extra / 2;
	while (x)
	{
	  state_putc (' ', stte->s);
	  x--;
	}
      } 
      else if (stte->tag_level[RICH_FLUSHRIGHT])
      {
	x = extra-1;
	while (x)
	{
	  state_putc (' ', stte->s);
	  x--;
	}
      }
    }
    state_puts (stte->line, stte->s);
  }

  state_putc ('\n', stte->s);
  stte->line[0] = '\0';
  stte->line_len = 0;
  stte->line_used = 0;
  stte->indent_len = 0;
  if (stte->s->prefix)
  {
    state_puts (stte->s->prefix, stte->s);
    stte->indent_len += mutt_strlen (stte->s->prefix);
  }

  if (stte->tag_level[RICH_EXCERPT])
  {
    x = stte->tag_level[RICH_EXCERPT];
    while (x) 
    {
      if (stte->s->prefix)
      {
	state_puts (stte->s->prefix, stte->s);
	    stte->indent_len += mutt_strlen (stte->s->prefix);
      }
      else
      {
	state_puts ("> ", stte->s);
	stte->indent_len += mutt_strlen ("> ");
      }
      x--;
    }
  }
  else
    stte->indent_len = 0;
  if (stte->tag_level[RICH_INDENT])
  {
    x = stte->tag_level[RICH_INDENT] * IndentSize;
    stte->indent_len += x;
    while (x) 
    {
      state_putc (' ', stte->s);
      x--;
    }
  }
}

static void enriched_flush (struct enriched_state *stte, int wrap)
{
  if (!stte->tag_level[RICH_NOFILL] && (stte->line_len + stte->word_len > 
      (stte->WrapMargin - (stte->tag_level[RICH_INDENT_RIGHT] * IndentSize) - 
       stte->indent_len)))
    enriched_wrap (stte);

  if (stte->buff_used)
  {
    stte->buffer[stte->buff_used] = '\0';
    stte->line_used += stte->buff_used;
    if (stte->line_used > stte->line_max)
    {
      stte->line_max = stte->line_used;
      safe_realloc ((void **) &stte->line, stte->line_max + 1);
    }
    strcat (stte->line, stte->buffer);	/* __STRCAT_CHECKED__ */
    stte->line_len += stte->word_len;
    stte->word_len = 0;
    stte->buff_used = 0;
  }
  if (wrap) 
    enriched_wrap(stte);
}


static void enriched_putc (int c, struct enriched_state *stte)
{
  if (stte->tag_level[RICH_PARAM]) 
  {
    if (stte->tag_level[RICH_COLOR]) 
    {
      if (stte->param_used + 1 >= stte->param_len)
	safe_realloc ((void **) &stte->param, (stte->param_len += STRING));

      stte->param[stte->param_used++] = c;
    }
    return; /* nothing to do */
  }

  /* see if more space is needed (plus extra for possible rich characters) */
  if (stte->buff_len < stte->buff_used + 3)
  {
    stte->buff_len += LONG_STRING;
    safe_realloc ((void **) &stte->buffer, stte->buff_len + 1);
  }

  if ((!stte->tag_level[RICH_NOFILL] && ISSPACE (c)) || c == '\0' )
  {
    if (c == '\t')
      stte->word_len += 8 - (stte->line_len + stte->word_len) % 8;
    else
      stte->word_len++;
    
    stte->buffer[stte->buff_used++] = c;
    enriched_flush (stte, 0);
  }
  else
  {
    if (stte->s->flags & M_DISPLAY)
    {
      if (stte->tag_level[RICH_BOLD])
      {
	stte->buffer[stte->buff_used++] = c;
	stte->buffer[stte->buff_used++] = '\010';
	stte->buffer[stte->buff_used++] = c;
      }
      else if (stte->tag_level[RICH_UNDERLINE])
      {

	stte->buffer[stte->buff_used++] = '_';
	stte->buffer[stte->buff_used++] = '\010';
	stte->buffer[stte->buff_used++] = c;
      }
      else if (stte->tag_level[RICH_ITALIC])
      {
	stte->buffer[stte->buff_used++] = c;
	stte->buffer[stte->buff_used++] = '\010';
	stte->buffer[stte->buff_used++] = '_';
      }
      else
      {
	stte->buffer[stte->buff_used++] = c;
      }
    }
    else
    {
      stte->buffer[stte->buff_used++] = c;
    }
    stte->word_len++;
  }
}

static void enriched_puts (char *s, struct enriched_state *stte)
{
  char *c;

  if (stte->buff_len < stte->buff_used + mutt_strlen(s))
  {
    stte->buff_len += LONG_STRING;
    safe_realloc ((void **) &stte->buffer, stte->buff_len + 1);
  }
  c = s;
  while (*c)
  {
    stte->buffer[stte->buff_used++] = *c;
    c++;
  }
}

static void enriched_set_flags (const char *tag, struct enriched_state *stte)
{
  const char *tagptr = tag;
  int i, j;

  if (*tagptr == '/')
    tagptr++;
  
  for (i = 0, j = -1; EnrichedTags[i].tag_name; i++)
    if (ascii_strcasecmp (EnrichedTags[i].tag_name,tagptr) == 0)
    {
      j = EnrichedTags[i].index;
      break;
    }

  if (j != -1)
  {
    if (j == RICH_CENTER || j == RICH_FLUSHLEFT || j == RICH_FLUSHRIGHT)
      enriched_flush (stte, 1);

    if (*tag == '/')
    {
      if (stte->tag_level[j]) /* make sure not to go negative */
	stte->tag_level[j]--;
      if ((stte->s->flags & M_DISPLAY) && j == RICH_PARAM && stte->tag_level[RICH_COLOR])
      {
	stte->param[stte->param_used] = '\0';
	if (!ascii_strcasecmp(stte->param, "black"))
	{
	  enriched_puts("\033[30m", stte);
	}
	else if (!ascii_strcasecmp(stte->param, "red"))
	{
	  enriched_puts("\033[31m", stte);
	}
	else if (!ascii_strcasecmp(stte->param, "green"))
	{
	  enriched_puts("\033[32m", stte);
	}
	else if (!ascii_strcasecmp(stte->param, "yellow"))
	{
	  enriched_puts("\033[33m", stte);
	}
	else if (!ascii_strcasecmp(stte->param, "blue"))
	{
	  enriched_puts("\033[34m", stte);
	}
	else if (!ascii_strcasecmp(stte->param, "magenta"))
	{
	  enriched_puts("\033[35m", stte);
	}
	else if (!ascii_strcasecmp(stte->param, "cyan"))
	{
	  enriched_puts("\033[36m", stte);
	}
	else if (!ascii_strcasecmp(stte->param, "white"))
	{
	  enriched_puts("\033[37m", stte);
	}
      }
      if ((stte->s->flags & M_DISPLAY) && j == RICH_COLOR)
      {
	enriched_puts("\033[0m", stte);
      }

      /* flush parameter buffer when closing the tag */
      if (j == RICH_PARAM)
      {
	stte->param_used = 0;
	stte->param[0] = '\0';
      }
    }
    else
      stte->tag_level[j]++;

    if (j == RICH_EXCERPT)
      enriched_flush(stte, 1);
  }
}

void text_enriched_handler (BODY *a, STATE *s)
{
  enum {
    TEXT, LANGLE, TAG, BOGUS_TAG, NEWLINE, ST_EOF, DONE
  } state = TEXT;

  long bytes = a->length;
  struct enriched_state stte;
  int c = 0;
  int tag_len = 0;
  char tag[LONG_STRING + 1];

  memset (&stte, 0, sizeof (stte));
  stte.s = s;
  stte.WrapMargin = ((s->flags & M_DISPLAY) ? (COLS-4) : ((COLS-4)<72)?(COLS-4):72);
  stte.line_max = stte.WrapMargin * 4;
  stte.line = (char *) safe_calloc (1, stte.line_max + 1);
  stte.param = (char *) safe_calloc (1, STRING);

  stte.param_len = STRING;
  stte.param_used = 0;

  if (s->prefix)
  {
    state_puts (s->prefix, s);
    stte.indent_len += mutt_strlen (s->prefix);
  }

  while (state != DONE)
  {
    if (state != ST_EOF)
    {
      if (!bytes || (c = fgetc (s->fpin)) == EOF)
	state = ST_EOF;
      else
	bytes--;
    }

    switch (state)
    {
      case TEXT :
	switch (c)
	{
	  case '<' :
	    state = LANGLE;
	    break;

	  case '\n' :
	    if (stte.tag_level[RICH_NOFILL])
	    {
	      enriched_flush (&stte, 1);
	    }
	    else 
	    {
	      enriched_putc (' ', &stte);
	      state = NEWLINE;
	    }
	    break;

	  default:
	    enriched_putc (c, &stte);
	}
	break;

      case LANGLE :
	if (c == '<')
	{
	  enriched_putc (c, &stte);
	  state = TEXT;
	  break;
	}
	else
	{
	  tag_len = 0;
	  state = TAG;
	}
	/* Yes, fall through (it wasn't a <<, so this char is first in TAG) */
      case TAG :
	if (c == '>')
	{
	  tag[tag_len] = '\0';
	  enriched_set_flags (tag, &stte);
	  state = TEXT;
	}
	else if (tag_len < LONG_STRING)  /* ignore overly long tags */
	  tag[tag_len++] = c;
	else
	  state = BOGUS_TAG;
	break;

      case BOGUS_TAG :
	if (c == '>')
	  state = TEXT;
	break;

      case NEWLINE :
	if (c == '\n')
	  enriched_flush (&stte, 1);
	else
	{
	  ungetc (c, s->fpin);
	  bytes++;
	  state = TEXT;
	}
	break;

      case ST_EOF :
	enriched_putc ('\0', &stte);
        enriched_flush (&stte, 1);
	state = DONE;
	break;

      case DONE: /* not reached, but gcc complains if this is absent */
	break;
    }
  }

  state_putc ('\n', s); /* add a final newline */

  FREE (&(stte.buffer));
  FREE (&(stte.line));
  FREE (&(stte.param));
}                                                                              

#ifndef LIBMUTT 
/*
 * An implementation of RFC 2646.
 * 
 *
 * NOTE: This still has to be made UTF-8 aware.
 *
 */

#define FLOWED_MAX 77

static void flowed_quote (STATE *s, int level)
{
  int i;
  
  if (s->prefix)
  {
    if (option (OPTTEXTFLOWED))
      level++;
    else
      state_puts (s->prefix, s);
  }
  
  for (i = 0; i < level; i++)
    state_putc ('>', s);
}

static int flowed_maybe_quoted (char *cont)
{
  return regexec ((regex_t *) QuoteRegexp.rx, cont, 0, NULL, 0) == 0;
}

static void flowed_stuff (STATE *s, char *cont, int level)
{
  if (!option (OPTTEXTFLOWED) && !(s->flags & M_DISPLAY))
    return;

  if (s->flags & M_DISPLAY)
  {
    /* 
     * Hack: If we are in the beginning of the line and there is 
     * some text on the line which looks like it's quoted, turn off 
     * ANSI colors, so quote coloring doesn't affect this line. 
     */
    if (*cont && !level && !mutt_strcmp (Pager, "builtin") && flowed_maybe_quoted (cont))
      state_puts ("\033[0m",s);  
  }
  else if ((*cont == ' ') || (*cont == '>') || (!level && !mutt_strncmp (cont, "From ", 5)))
    state_putc (' ', s);
}

static char *flowed_skip_indent (char *prefix, char *cont)
{
  for (; *cont == ' ' || *cont == '\t'; cont++)
    *prefix++ = *cont;
  *prefix = '\0';
  return cont;
}


static int flowed_visual_strlen (char *l, int i)
{
  int j;
  for (j = 0; *l; l++)
  {
    if (*l == '\t')
      j += 8 - ((i + j) % 8);
    else
      j++;
  }
  
  return j;
}



static void text_plain_flowed_handler (BODY *a, STATE *s)
{
  char line[LONG_STRING];
  char indent[LONG_STRING];

  int  quoted = -1;
  int  last_quoted;
  int  full = 1;
  int  last_full;
  int  col = 0, tmpcol;

  int  i_add = 0;
  int  add = 0;
  int  soft = 0;
  int  l, rl;
  
  int  flowed_max;
  int  bytes = a->length;
  int  actually_wrap = 0;
  
  char *cont = NULL;
  char *tail = NULL;
  char *lc = NULL;
  char *t;

  *indent = '\0';
  
  if (s->prefix)
    add = 1;
  
  if ((flowed_max = FLOWED_MAX) > COLS - 3)
    flowed_max = COLS - 3;
  if (flowed_max > COLS - WrapMargin)
    flowed_max = COLS - WrapMargin;
  if (flowed_max <= 0)
    flowed_max = COLS;

  while (bytes > 0 && fgets (line, sizeof (line), s->fpin))
  {
    bytes        -= strlen (line);
    tail          = NULL;

    last_full     = full;
    
    /* 
     * If the last line wasn't fully read, this is the
     * tail of some line. 
     */
    actually_wrap = !last_full; 
    
    if ((t = strrchr (line, '\r')) || (t = strrchr (line, '\n')))
    {
      *t   = '\0';
      full = 1;
    }
    else if ((t = strrchr (line, ' ')) || (t = strrchr (line, '\t')))
    {
      /* 
       * Bad: We have a line of more than LONG_STRING characters.
       * (Which SHOULD NOT happen, since lines SHOULD be <= 79
       * characters long.)
       * 
       * Try to simulate a soft line break at a word boundary.
       * Handle the rest of the line next time.
       * 
       * Give up when we have a single word which is longer than
       * LONG_STRING characters.  It will just be split into parts,
       * with a hard line break in between. 
       */

      full = 0;
      l    = strlen (t + 1);
      t[0] = ' ';
      t[1] = '\0';

      if (l)
      {
	fseek (s->fpin, -l, SEEK_CUR);
	bytes += l;
      }
    }
    else
      full = 0;

    last_quoted = quoted;

    if (last_full)
    {
      /* 
       * We are in the beginning of a new line. Determine quote level
       * and indentation prefix 
       */
      for (quoted = 0; line[quoted] == '>'; quoted++)
	;
      
      cont = line + quoted;
      
      /* undo space stuffing */
      if (*cont == ' ')
	cont++;

      /* If there is an indentation, record it. */
      cont = flowed_skip_indent (indent, cont);
      i_add = flowed_visual_strlen (indent, quoted + add);
    }
    else
    {
      /* 
       * This is just the tail of some over-long line. Keep
       * indentation and quote levels.  Don't unstuff.
       */
      cont = line;
    }

    /* If we have a change in quoting depth, wrap. */

    if (col && last_quoted != quoted && last_quoted >= 0)
    {
      state_putc ('\n', s);
      col = 0;
    }
    
    do 
    {
      if (tail)
	cont = tail;

      SKIPWS (cont);
      
      tail = NULL;
      soft = 0;
      
      /* try to find a point for word wrapping */

    retry_wrap:
      l  = flowed_visual_strlen (cont, quoted + i_add + add + col);
      rl = mutt_strlen (cont);
      if (quoted + i_add + add + col + l > flowed_max)
      {
	actually_wrap = 1;
	for (tmpcol = quoted + i_add + add + col, t = cont;
	     *t && tmpcol < flowed_max; t++)
	{
	  if (*t == ' ' || *t == '\t')
	    tail = t;
	}
	if (*t == '\t')
	  tmpcol = (tmpcol & ~7) + 8;
	else
	  tmpcol++;
	if (tail)
	{
	  *tail++ = '\0';
	  soft = 2;
	}
      }

      /* We seem to be desperate.  Get me a new line, and retry. */
      if (!tail && (quoted + add + col + i_add + l > flowed_max) && col)
      {
	state_putc ('\n', s);
	col = 0;
	goto retry_wrap;
      }

      /* Detect soft line breaks. */
      if (!soft && ascii_strcmp (cont, "-- "))
      {
	lc = strrchr (cont, ' ');
	if (lc && lc[1] == '\0')
	  soft = 1;
      }

      /* 
       * If we are in the beginning of an output line, do quoting
       * and stuffing. 
       * 
       * We have to temporarily assemble the line since display
       * stuffing (i.e., turning off quote coloring) may depend on
       * the line's actual content.  You never know what people put
       * into their regular expressions. 
       */
      if (!col)
      {
	char tmp[LONG_STRING];
	snprintf (tmp, sizeof (tmp), "%s%s", indent, cont);

	flowed_quote (s, quoted);
	flowed_stuff (s, tmp, quoted + add);

	state_puts (indent, s);
      }

      /* output the text */
      state_puts (cont, s);
      col += flowed_visual_strlen (cont, quoted + i_add + add + col);
       
      /* possibly indicate a soft line break */
      if (soft == 2)
      {
	state_putc (' ', s);
	col++;
      }
      
      /* 
       * Wrap if this display line corresponds to a 
       * text line. Don't wrap if we changed the line.
       */
      if (!soft || (!actually_wrap && full))
      {
	state_putc ('\n', s);
	col = 0;
      }
    }
    while (tail);
  }

  if (col)
    state_putc ('\n', s);
  
}

#define TXTHTML     1
#define TXTPLAIN    2
#define TXTENRICHED 3

static void alternative_handler (BODY *a, STATE *s)
{
  BODY *choice = NULL;
  BODY *b;
  LIST *t;
  char buf[STRING];
  int type = 0;

  /* First, search list of prefered types */
  t = AlternativeOrderList;
  while (t && !choice)
  {
    char *c;
    int btlen;  /* length of basetype */
    int wild;	/* do we have a wildcard to match all subtypes? */

    c = strchr (t->data, '/');
    if (c)
    {
      wild = (c[1] == '*' && c[2] == 0);
      btlen = c - t->data;
    }
    else
    {
      wild = 1;
      btlen = mutt_strlen (t->data);
    }

    if (a && a->parts) 
      b = a->parts;
    else
      b = a;
    while (b)
    {
      const char *bt = TYPE(b);
      if (!ascii_strncasecmp (bt, t->data, btlen) && bt[btlen] == 0)
      {
	/* the basetype matches */
	if (wild || !ascii_strcasecmp (t->data + btlen + 1, b->subtype))
	{
	  choice = b;
	}
      }
      b = b->next;
    }
    t = t->next;
  }

  /* Next, look for an autoviewable type */
  if (!choice)
  {
    if (a && a->parts) 
      b = a->parts;
    else
      b = a;
    while (b)
    {
      snprintf (buf, sizeof (buf), "%s/%s", TYPE (b), b->subtype);
      if (mutt_is_autoview (b, buf))
      {
	rfc1524_entry *entry = rfc1524_new_entry ();

	if (rfc1524_mailcap_lookup (b, buf, entry, M_AUTOVIEW))
	{
	  choice = b;
	}
	rfc1524_free_entry (&entry);
      }
      b = b->next;
    }
  }

  /* Then, look for a text entry */
  if (!choice)
  {
    if (a && a->parts) 
      b = a->parts;
    else
      b = a;
    while (b)
    {
      if (b->type == TYPETEXT)
      {
	if (! ascii_strcasecmp ("plain", b->subtype) && type <= TXTPLAIN)
	{
	  choice = b;
	  type = TXTPLAIN;
	}
	else if (! ascii_strcasecmp ("enriched", b->subtype) && type <= TXTENRICHED)
	{
	  choice = b;
	  type = TXTENRICHED;
	}
	else if (! ascii_strcasecmp ("html", b->subtype) && type <= TXTHTML)
	{
	  choice = b;
	  type = TXTHTML;
	}
      }
      b = b->next;
    }
  }

  /* Finally, look for other possibilities */
  if (!choice)
  {
    if (a && a->parts) 
      b = a->parts;
    else
      b = a;
    while (b)
    {
      if (mutt_can_decode (b))
	choice = b;
      b = b->next;
    }
  }

  if (choice)
  {
    if (s->flags & M_DISPLAY && !option (OPTWEED))
    {
      fseek (s->fpin, choice->hdr_offset, 0);
      mutt_copy_bytes(s->fpin, s->fpout, choice->offset-choice->hdr_offset);
    }
    mutt_body_handler (choice, s);
  }
  else if (s->flags & M_DISPLAY)
  {
    /* didn't find anything that we could display! */
    state_mark_attach (s);
    state_puts(_("[-- Error:  Could not display any parts of Multipart/Alternative! --]\n"), s);
  }
}

/* handles message/rfc822 body parts */
void message_handler (BODY *a, STATE *s)
{
  struct stat st;
  BODY *b;
  long off_start;

  off_start = ftell (s->fpin);
  if (a->encoding == ENCBASE64 || a->encoding == ENCQUOTEDPRINTABLE || 
      a->encoding == ENCUUENCODED)
  {
    fstat (fileno (s->fpin), &st);
    b = mutt_new_body ();
    b->length = (long) st.st_size;
    b->parts = mutt_parse_messageRFC822 (s->fpin, b);
  }
  else
    b = a;

  if (b->parts)
  {
    (((s->flags & M_WEED) || ((s->flags & (M_DISPLAY|M_PRINTING)) && option (OPTWEED))) ? (CH_WEED | CH_REORDER) : 0) |
      (s->prefix ? CH_PREFIX : 0) | CH_DECODE | CH_FROM, s->prefix);

    if (s->prefix)
      state_puts (s->prefix, s);
    state_putc ('\n', s);

    mutt_body_handler (b->parts, s);
  }

  if (a->encoding == ENCBASE64 || a->encoding == ENCQUOTEDPRINTABLE ||
      a->encoding == ENCUUENCODED)
    mutt_free_body (&b);
}

/* returns 1 if decoding the attachment will produce output */
int mutt_can_decode (BODY *a)
{
  char type[STRING];

  snprintf (type, sizeof (type), "%s/%s", TYPE (a), a->subtype);
  if (mutt_is_autoview (a, type))
    return (rfc1524_mailcap_lookup (a, type, NULL, M_AUTOVIEW));
  else if (a->type == TYPETEXT)
    return (1);
  else if (a->type == TYPEMESSAGE)
    return (1);
  else if (a->type == TYPEMULTIPART)
  {



#ifdef HAVE_PGP
    if (ascii_strcasecmp (a->subtype, "signed") == 0 ||
	ascii_strcasecmp (a->subtype, "encrypted") == 0)
      return (1);
    else
#endif



    {
      BODY *p;

      for (p = a->parts; p; p = p->next)
      {
	if (mutt_can_decode (p))
	  return (1);
      }
    }
  }



#ifdef HAVE_PGP
  else if (a->type == TYPEAPPLICATION)
  {
    if (mutt_is_application_pgp(a))
      return (1);
  }
#endif



  return (0);
}
#endif /* LIBMUTT */


#ifndef LIBMUTT
void multipart_handler (BODY *a, STATE *s)
{
  BODY *b, *p;
  char length[5];
  struct stat st;
  int count;

  if (a->encoding == ENCBASE64 || a->encoding == ENCQUOTEDPRINTABLE ||
      a->encoding == ENCUUENCODED)
  {
    fstat (fileno (s->fpin), &st);
    b = mutt_new_body ();
    b->length = (long) st.st_size;
    b->parts = mutt_parse_multipart (s->fpin,
		  mutt_get_parameter ("boundary", a->parameter),
		  (long) st.st_size, ascii_strcasecmp ("digest", a->subtype) == 0);
  }
  else
    b = a;

  for (p = b->parts, count = 1; p; p = p->next, count++)
  {
    if (s->flags & M_DISPLAY)
    {
      state_mark_attach (s);
      state_printf (s, _("[-- Attachment #%d"), count);
      if (p->description || p->filename || p->form_name)
      {
	state_puts (": ", s);
	state_puts (p->description ? p->description :
		    p->filename ? p->filename : p->form_name, s);
      }
      state_puts (" --]\n", s);

      mutt_pretty_size (length, sizeof (length), p->length);
      
      state_printf (s, _("[-- Type: %s/%s, Encoding: %s, Size: %s --]\n"),
		    TYPE (p), p->subtype, ENCODING (p->encoding), length);
      if (!option (OPTWEED))
      {
	fseek (s->fpin, p->hdr_offset, 0);
	mutt_copy_bytes(s->fpin, s->fpout, p->offset-p->hdr_offset);
      }
      else
	state_putc ('\n', s);
    }
    else
    {
      if (p->description && mutt_can_decode (p))
	state_printf (s, "Content-Description: %s\n", p->description);

      if (p->form_name)
	state_printf(s, "%s: \n", p->form_name);

    }
    mutt_body_handler (p, s);
    state_putc ('\n', s);
  }

  if (a->encoding == ENCBASE64 || a->encoding == ENCQUOTEDPRINTABLE ||
      a->encoding == ENCUUENCODED)
    mutt_free_body (&b);
}

void autoview_handler (BODY *a, STATE *s)
{
  rfc1524_entry *entry = rfc1524_new_entry ();
  char buffer[LONG_STRING];
  char type[STRING];
  char command[LONG_STRING];
  char tempfile[_POSIX_PATH_MAX] = "";
  char *fname;
  FILE *fpin = NULL;
  FILE *fpout = NULL;
  FILE *fperr = NULL;
  int piped = FALSE;
  pid_t thepid;

  snprintf (type, sizeof (type), "%s/%s", TYPE (a), a->subtype);
  rfc1524_mailcap_lookup (a, type, entry, M_AUTOVIEW);

  fname = safe_strdup (a->filename);
  mutt_sanitize_filename (fname, 1);
  rfc1524_expand_filename (entry->nametemplate, fname, tempfile, sizeof (tempfile));
  FREE (&fname);

  if (entry->command)
  {
    strfcpy (command, entry->command, sizeof (command));

    /* rfc1524_expand_command returns 0 if the file is required */
    piped = rfc1524_expand_command (a, tempfile, type, command, sizeof (command));

    if (s->flags & M_DISPLAY)
    {
      state_printf (s, _("[-- Autoview using %s --]\n"), command);
      mutt_message(_("Invoking autoview command: %s"),command);
    }

    if ((fpin = safe_fopen (tempfile, "w+")) == NULL)
    {
      mutt_perror ("fopen");
      rfc1524_free_entry (&entry);
      return;
    }
    
    mutt_copy_bytes (s->fpin, fpin, a->length);

    if(!piped)
    {
      safe_fclose (&fpin);
      thepid = mutt_create_filter (command, NULL, &fpout, &fperr);
    }
    else
    {
      unlink (tempfile);
      fflush (fpin);
      rewind (fpin);
      thepid = mutt_create_filter_fd (command, NULL, &fpout, &fperr,
				      fileno(fpin), -1, -1);
    }

    if (thepid < 0)
    {
      mutt_perror _("Can't create filter");
      if (s->flags & M_DISPLAY)
	state_printf (s, _("[-- Can't run %s. --]\n"), command);
      goto bail;
    }
    
    if (s->prefix)
    {
      while (fgets (buffer, sizeof(buffer), fpout) != NULL)
      {
        state_puts (s->prefix, s);
        state_puts (buffer, s);
      }
      /* check for data on stderr */
      if (fgets (buffer, sizeof(buffer), fperr)) 
      {
	if (s->flags & M_DISPLAY) {
	   state_mark_attach (s);
	  state_printf (s, _("[-- Autoview stderr of %s --]\n"), command);
	}

	state_puts (s->prefix, s);
	state_puts (buffer, s);
	while (fgets (buffer, sizeof(buffer), fperr) != NULL)
	{
	  state_puts (s->prefix, s);
	  state_puts (buffer, s);
	}
      }
    }
    else
    {
      mutt_copy_stream (fpout, s->fpout);
      /* Check for stderr messages */
      if (fgets (buffer, sizeof(buffer), fperr))
      {
	if (s->flags & M_DISPLAY) {
	  state_mark_attach(s);
	  state_printf (s, _("[-- Autoview stderr of %s --]\n"), 
			command);
	}
	
	state_puts (buffer, s);
	mutt_copy_stream (fperr, s->fpout);
      }
    }

  bail:
    safe_fclose (&fpout);
    safe_fclose (&fperr);

    mutt_wait_filter (thepid);
    if (piped)
      safe_fclose (&fpin);
    else
      mutt_unlink (tempfile);

    if (s->flags & M_DISPLAY) 
    {
      state_mark_attach (s);
      mutt_clear_error ();
    }
  }
  rfc1524_free_entry (&entry);
}
#endif

#ifndef LIBMUTT
static void external_body_handler (BODY *b, STATE *s)
{
  const char *access_type;
  const char *expiration;
  time_t expire;

  access_type = mutt_get_parameter ("access-type", b->parameter);
  if (!access_type)
  {
    if (s->flags & M_DISPLAY)
    {
      state_mark_attach (s);
      state_puts (_("[-- Error: message/external-body has no access-type parameter --]\n"), s);
    }
    return;
  }

  expiration = mutt_get_parameter ("expiration", b->parameter);
  if (expiration)
    expire = mutt_parse_date (expiration, NULL);
  else
    expire = -1;

  if (!ascii_strcasecmp (access_type, "x-mutt-deleted"))
  {
    if (s->flags & (M_DISPLAY|M_PRINTING))
    {
      char *length;
      char pretty_size[10];

      state_mark_attach (s);
      state_printf (s, _("[-- This %s/%s attachment "),
	       TYPE(b->parts), b->parts->subtype);
      length = mutt_get_parameter ("length", b->parameter);
      if (length)
      {
	mutt_pretty_size (pretty_size, sizeof (pretty_size),
			  strtol (length, NULL, 10));
	state_printf (s, _("(size %s bytes) "), pretty_size);
      }
      state_puts (_("has been deleted --]\n"), s);

      if (expire != -1)
      {
	state_mark_attach (s);
 	state_printf (s, _("[-- on %s --]\n"), expiration);
      }
      if (b->parts->filename)
      {
	state_mark_attach (s);
	state_printf (s, _("[-- on %s --]\n"), expiration);
      }

      mutt_copy_hdr (s->fpin, s->fpout, ftell (s->fpin), b->parts->offset,
		     (option (OPTWEED) ? (CH_WEED | CH_REORDER) : 0) |
		     CH_DECODE , NULL);
    }
  }
  else if(expiration && expire < time(NULL))
  {
    if (s->flags & M_DISPLAY)
    {
      state_printf (s, _("[-- This %s/%s attachment is not included, --]\n"
			 "[-- and the indicated external source has --]\n"
			 "[-- expired. --]\n"),
		    TYPE(b->parts), b->parts->subtype);
      mutt_copy_hdr(s->fpin, s->fpout, ftell (s->fpin), b->parts->offset,
		    (option (OPTWEED) ? (CH_WEED | CH_REORDER) : 0) |
		    CH_DECODE, NULL);
    }
  }
  else
  {
    if (s->flags & M_DISPLAY)
    {
      state_printf (s,
		    _("[-- This %s/%s attachment is not included, --]\n"),
		    TYPE (b->parts), b->parts->subtype);
      state_mark_attach (s);
      state_printf (s, 
		    _("[-- and the indicated access-type %s is unsupported --]\n"),
		    access_type);
      mutt_copy_hdr (s->fpin, s->fpout, ftell (s->fpin), b->parts->offset,
		     (option (OPTWEED) ? (CH_WEED | CH_REORDER) : 0) |
		     CH_DECODE , NULL);
    }
  }
}
#endif


void mutt_decode_attachment (BODY *b, STATE *s)
{
  int istext = mutt_is_text_type (b->type, b->subtype);
  iconv_t cd = (iconv_t)(-1);

  if (istext && s->flags & M_CHARCONV)
  {
    char *charset = mutt_get_parameter ("charset", b->parameter);
#ifdef LIBMUTT
    /* a reasonable default for Charset! */
    if (Charset)
      cd = mutt_iconv_open (Charset, charset ? charset : "us-ascii",
                            M_ICONV_HOOK_FROM);
#else
    if (charset && Charset)
      cd = mutt_iconv_open (Charset, charset, M_ICONV_HOOK_FROM);
#endif
  }

  fseek (s->fpin, b->offset, 0);
  switch (b->encoding)
  {
    case ENCQUOTEDPRINTABLE:
      mutt_decode_quoted (s, b->length, istext, cd);
      break;
    case ENCBASE64:
      mutt_decode_base64 (s, b->length, istext, cd);
      break;
    case ENCUUENCODED:
      mutt_decode_uuencoded (s, b->length, istext, cd);
      break;
    default:
      mutt_decode_xbit (s, b->length, istext, cd);
      break;
  }

  if (cd != (iconv_t)(-1))
    iconv_close (cd);
}


#ifndef LIBMUTT
void mutt_body_handler (BODY *b, STATE *s)
{
  int decode = 0;
  int plaintext = 0;
  FILE *fp = NULL;
  char tempfile[_POSIX_PATH_MAX];
  handler_t handler = NULL;
  long tmpoffset = 0;
  size_t tmplength = 0;
  char type[STRING];

  int oflags = s->flags;

  /* first determine which handler to use to process this part */

  snprintf (type, sizeof (type), "%s/%s", TYPE (b), b->subtype);
  if (mutt_is_autoview (b, type))
  {
    rfc1524_entry *entry = rfc1524_new_entry ();

    if (rfc1524_mailcap_lookup (b, type, entry, M_AUTOVIEW))
    {
      handler   = autoview_handler;
      s->flags &= ~M_CHARCONV;
    }
    rfc1524_free_entry (&entry);
  }
  else if (b->type == TYPETEXT)
  {
    if (ascii_strcasecmp ("plain", b->subtype) == 0)
    {
      /* avoid copying this part twice since removing the transfer-encoding is
       * the only operation needed.
       */
      if (ascii_strcasecmp ("flowed", mutt_get_parameter ("format", b->parameter)) == 0)
	handler = text_plain_flowed_handler;
      else
	plaintext = 1;
    }
    else if (ascii_strcasecmp ("enriched", b->subtype) == 0)
      handler = text_enriched_handler;
    else if (ascii_strcasecmp ("rfc822-headers", b->subtype) == 0)
      plaintext = 1;
  }
  else if (b->type == TYPEMESSAGE)
  {
    if(mutt_is_message_type(b->type, b->subtype))
      handler = message_handler;
    else if (!ascii_strcasecmp ("delivery-status", b->subtype))
      plaintext = 1;
    else if (!ascii_strcasecmp ("external-body", b->subtype))
      handler = external_body_handler;
  }
  else if (b->type == TYPEMULTIPART)
  {



#ifdef HAVE_PGP
    char *p;
#endif /* HAVE_PGP */



    if (ascii_strcasecmp ("alternative", b->subtype) == 0)
      handler = alternative_handler;



#ifdef HAVE_PGP
    else if (ascii_strcasecmp ("signed", b->subtype) == 0)
    {
      p = mutt_get_parameter ("protocol", b->parameter);

      if (!p)
        mutt_error _("Error: multipart/signed has no protocol.");
      else if (ascii_strcasecmp ("application/pgp-signature", p) == 0 ||
	       ascii_strcasecmp ("multipart/mixed", p) == 0)
      {
	if (s->flags & M_VERIFY)
	  handler = pgp_signed_handler;
      }
    }
    else if (ascii_strcasecmp ("encrypted", b->subtype) == 0)
    {
      p = mutt_get_parameter ("protocol", b->parameter);

      if (!p)
        mutt_error _("Error: multipart/encrypted has no protocol parameter!");
      else if (ascii_strcasecmp ("application/pgp-encrypted", p) == 0)
        handler = pgp_encrypted_handler;
    }
#endif /* HAVE_PGP */



    if (!handler)
      handler = multipart_handler;
  }



#ifdef HAVE_PGP
  else if (b->type == TYPEAPPLICATION)
  {
    if (mutt_is_application_pgp(b))
      handler = pgp_application_pgp_handler;
  }
#endif /* HAVE_PGP */



  if (plaintext || handler)
  {
    fseek (s->fpin, b->offset, 0);

    /* see if we need to decode this part before processing it */
    if (b->encoding == ENCBASE64 || b->encoding == ENCQUOTEDPRINTABLE ||
	b->encoding == ENCUUENCODED || plaintext || 
	mutt_is_text_type (b->type, b->subtype))	/* text subtypes may
							 * require character
							 * set conversion even
							 * with 8bit encoding.
							 */
    {
      int origType = b->type;
      char *savePrefix = NULL;

      if (!plaintext)
      {
	/* decode to a tempfile, saving the original destination */
	fp = s->fpout;
	mutt_mktemp (tempfile);
	if ((s->fpout = safe_fopen (tempfile, "w")) == NULL)
	{
	  mutt_error _("Unable to open temporary file!");
	  goto bail;
	}
	/* decoding the attachment changes the size and offset, so save a copy
	 * of the "real" values now, and restore them after processing
	 */
	tmplength = b->length;
	tmpoffset = b->offset;

	/* if we are decoding binary bodies, we don't want to prefix each
	 * line with the prefix or else the data will get corrupted.
	 */
	savePrefix = s->prefix;
	s->prefix = NULL;

	decode = 1;
      }
      else
	b->type = TYPETEXT;

      mutt_decode_attachment (b, s);

      if (decode)
      {
	b->length = ftell (s->fpout);
	b->offset = 0;
	fclose (s->fpout);

	/* restore final destination and substitute the tempfile for input */
	s->fpout = fp;
	fp = s->fpin;
	s->fpin = fopen (tempfile, "r");
	unlink (tempfile);

	/* restore the prefix */
	s->prefix = savePrefix;
      }

      b->type = origType;
    }

    /* process the (decoded) body part */
    if (handler)
    {
      handler (b, s);

      if (decode)
      {
	b->length = tmplength;
	b->offset = tmpoffset;

	/* restore the original source stream */
	fclose (s->fpin);
	s->fpin = fp;
      }
    }
  }
#ifndef LIBMUTT
  else if (s->flags & M_DISPLAY)
  {
    state_mark_attach (s);
    state_printf (s, _("[-- %s/%s is unsupported "), TYPE (b), b->subtype);
    if (!option (OPTVIEWATTACH))
    {
      if (km_expand_key (type, sizeof(type),
			km_find_func (MENU_PAGER, OP_VIEW_ATTACHMENTS)))
	fprintf (s->fpout, _("(use '%s' to view this part)"), type);
      else
	fputs (_("(need 'view-attachments' bound to key!)"), s->fpout);
    }
    fputs (" --]\n", s->fpout);
  }
#endif
 bail:
  s->flags = oflags;
}
#endif /* LIBMUTT */




