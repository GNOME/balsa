/* libimap library.
 * Copyright (C) 2003-2004 Pawel Salek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
/* imap_quote_string: quote string according to IMAP rules:
 *   surround string with quotes, escape " and \ with \ */

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "util.h"

#define SKIPWS(c) while (*(c) && isspace ((unsigned char) *(c))) c++;

void
imap_quote_string(char *dest, size_t dlen, const char *src)
{
  char quote[] = "\"\\", *pt;
  const char *s;

  pt = dest;
  s  = src;

  *pt++ = '"';
  /* save room for trailing quote-char */
  dlen -= 2;
  
  for (; *s && dlen; s++)
  {
    if (strchr (quote, *s))
    {
      dlen -= 2;
      if (!dlen)
	break;
      *pt++ = '\\';
      *pt++ = *s;
    }
    else
    {
      *pt++ = *s;
      dlen--;
    }
  }
  *pt++ = '"';
  *pt = 0;
}

/* imap_unquote_string: 
 *  modify the argument removing quote characters 
 */
void
imap_unquote_string (char *s)
{
  char *d = s;

  if (*s == '\"')
    s++;
  else
    return;

  while (*s)
  {
    if (*s == '\"')
    {
      *d = '\0';
      return;
    }
    if (*s == '\\')
    {
      s++;
    }
    if (*s)
    {
      *d = *s;
      d++;
      s++;
    }
  }
  *d = '\0';
}

/* imap_skip_atom: return index into string where next IMAP atom begins.
 * see RFC for a definition of the atom. */
#define LIST_SPECIALS   "%*"
#define QUOTED_SPECIALS "\"\\"
#define RESP_SPECIALS   "]"
char*
imap_skip_atom(char *s)
{
  static const char ATOM_SPECIALS[] = 
    "(){ " LIST_SPECIALS QUOTED_SPECIALS RESP_SPECIALS;
  while (*s && !strchr(ATOM_SPECIALS,*s))
      s++;
  SKIPWS (s);
  return s;
}

char*
imap_next_word(char *s)
{
  int quoted = 0;

  while (*s) {
    if (*s == '\\') {
      s++;
      if (*s)
        s++;
      continue;
    }
    if (*s == '\"')
      quoted = quoted ? 0 : 1;
    if (!quoted && isspace(*s))
      break;
    s++;
  }

  SKIPWS (s);
  return s;
}
  
#define BAD     -1
#define base64val(c) Index_64[(unsigned int)(c)]
char B64Chars[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
  't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', '+', '/'
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

/* raw bytes to null-terminated base 64 string */
void
lit_conv_to_base64(char *out, const char *in, size_t len,
                   size_t olen)
{
  while (len >= 3 && olen > 10)
  {
    *out++ = B64Chars[in[0] >> 2];
    *out++ = B64Chars[((in[0] << 4) & 0x30) | (in[1] >> 4)];
    *out++ = B64Chars[((in[1] << 2) & 0x3c) | (in[2] >> 6)];
    *out++ = B64Chars[in[2] & 0x3f];
    olen  -= 4;
    len   -= 3;
    in    += 3;
  }

  /* clean up remainder */
  if (len > 0 && olen > 4)
  {
    unsigned char fragment;

    *out++ = B64Chars[in[0] >> 2];
    fragment = (in[0] << 4) & 0x30;
    if (len > 1)
      fragment |= in[1] >> 4;
    *out++ = B64Chars[fragment];
    *out++ = (len < 2) ? '=' : B64Chars[(in[1] << 2) & 0x3c];
    *out++ = '=';
  }
  *out = '\0';
}


/* Convert '\0'-terminated base 64 string to raw bytes.
 * Returns length of returned buffer, or -1 on error */
int
lit_conv_from_base64(char *out, const char *in)
{
  int len = 0, idx=0;
  register unsigned char digit1, digit2, digit3, digit4;

  do {
    digit1 = in[idx];
    if (digit1 > 127 || base64val (digit1) == BAD)
      return -1-idx;
    digit2 = in[idx+1];
    if (digit2 > 127 || base64val (digit2) == BAD)
      return -2-idx;
    digit3 = in[idx+2];
    if (digit3 > 127 || ((digit3 != '=') && (base64val (digit3) == BAD)))
      return -3-idx;
    digit4 = in[idx+3];
    if (digit4 > 127 || ((digit4 != '=') && (base64val (digit4) == BAD)))
      return -4-idx;
    idx += 4;

    /* digits are already sanity-checked */
    *out++ = (base64val(digit1) << 2) | (base64val(digit2) >> 4);
    len++;
    if (digit3 != '=')
    {
      *out++ = ((base64val(digit2) << 4) & 0xf0) | (base64val(digit3) >> 2);
      len++;
      if (digit4 != '=')
      {
	*out++ = ((base64val(digit3) << 6) & 0xc0) | base64val(digit4);
	len++;
      }
    }
  }
  while (in[idx] && in[idx] != 13 && digit4 != '=');

  return len;
}
