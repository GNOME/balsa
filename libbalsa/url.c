/*
 * Copyright (C) 2000 Thomas Roessler <roessler@guug.de>
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
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 */ 

/*
 * A simple URL parser.
 */

#define _BSD_SOURCE 1

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <glib.h>

#include "url.h"



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
#define hexval(c) Index_hex[(unsigned int)(c)]

struct mapping_t
{
  char *name;
  int value;
};

static struct mapping_t UrlMap[] =
{
  { "file", 	U_FILE },
  { "imap", 	U_IMAP },
  { "imaps", 	U_IMAPS },
  { "pop",  	U_POP  },
  { "pops", 	U_POPS  },
  { "mailto",	U_MAILTO },
  { NULL,	U_UNKNOWN}
};

# define STRING          256


static int mutt_getvaluebyname (const char *name, const struct mapping_t *map);
static char *mutt_getnamebyvalue (int, const struct mapping_t *);


static int 
mutt_getvaluebyname (const char *name, const struct mapping_t *map)
{
  int i;
 
  for (i = 0; map[i].name; i++)
    if (g_ascii_strcasecmp (map[i].name, name) == 0)
      return (map[i].value);
  return (-1);
}

static char *
mutt_getnamebyvalue (int val, const struct mapping_t *map)
{
  int i;
                                                                                
  for (i=0; map[i].name; i++)
    if (map[i].value == val)
      return (map[i].name);
  return NULL;
}


static void url_pct_decode (char *s)
{
  char *d;

  if (!s)
    return;
  
  for (d = s; *s; s++)
  {
    if (*s == '%' && s[1] && s[2] &&
	hexval (s[1]) >= 0 && hexval(s[2]) >= 0)
    {
      *d++ = (hexval (s[1]) << 4) | (hexval (s[2]));
      s += 2;
    }
    else
      *d++ = *s;
  }
  *d ='\0';
}

url_scheme_t url_check_scheme (const char *s)
{
  char sbuf[STRING];
  char *t;
  int i;
  
  if (!s || !(t = strchr (s, ':')))
    return U_UNKNOWN;
  if ((t - s) + 1 >= (glong) sizeof (sbuf))
    return U_UNKNOWN;
  
  g_stpcpy (sbuf, s);
  for (t = sbuf; *t; t++)
    *t = tolower (*t);

  if ((i = mutt_getvaluebyname (sbuf, UrlMap)) == -1)
    return U_UNKNOWN;
  else
    return (url_scheme_t) i;
}

int url_parse_file (char *d, const char *src, size_t dl)
{
  if (strncasecmp (src, "file:", 5))
    return -1;
  else if (!g_ascii_strncasecmp (src, "file://", 7))
    /* we don't support remote files */
    return -1;
  else
    g_stpcpy (d, src + 5);
  
  url_pct_decode (d);
  return 0;
}

/* ciss_parse_userhost: fill in components of ciss with info from src. Note
 *   these are pointers into src, which is altered with '\0's. Port of 0
 *   means no port given. */
static char *ciss_parse_userhost (ciss_url_t *ciss, char *src)
{
  char *t;
  char *p;
  char *path;

  ciss->user = NULL;
  ciss->pass = NULL;
  ciss->host = NULL;
  ciss->port = 0;

  if (strncmp (src, "//", 2))
    return src;
  
  src += 2;

  if ((path = strchr (src, '/')))
    *path++ = '\0';
  
  if ((t = strchr (src, '@')))
  {
    *t = '\0';
    if ((p = strchr (src, ':')))
    {
      *p = '\0';
      ciss->pass = p + 1;
      url_pct_decode (ciss->pass);
    }
    ciss->user = src;
    url_pct_decode (ciss->user);
    t++;
  }
  else
    t = src;
  
  if ((p = strchr (t, ':')))
  {
    *p++ = '\0';
    ciss->port = atoi (p);
  }
  else
    ciss->port = 0;
  
  ciss->host = t;
  url_pct_decode (ciss->host);
  return path;
}

int url_parse_ciss (ciss_url_t *ciss, char *src)
{
  char *tmp;

  if ((ciss->scheme = url_check_scheme (src)) == U_UNKNOWN)
    return -1;

  tmp = strchr (src, ':') + 1;

  ciss->path = ciss_parse_userhost (ciss, tmp);
  url_pct_decode (ciss->path);
  
  return 0;
}

/* url_ciss_tostring: output the URL string for a given CISS object. */
int url_ciss_tostring (ciss_url_t* ciss, char* dest, size_t len, int flags)
{
  if (ciss->scheme == U_UNKNOWN)
    return -1;

  snprintf (dest, len, "%s:", mutt_getnamebyvalue (ciss->scheme, UrlMap));

  if (ciss->host)
  {
    strncat (dest, "//", len - strlen (dest));
    if (ciss->user) {
      if (flags & U_DECODE_PASSWD && ciss->pass)
	snprintf (dest + strlen (dest), len - strlen (dest), "%s:%s@",
		  ciss->user, ciss->pass);
      else
	snprintf (dest + strlen (dest), len - strlen (dest), "%s@",
		  ciss->user);
    }

    if (ciss->port)
      snprintf (dest + strlen (dest), len - strlen (dest), "%s:%hu/",
		ciss->host, ciss->port);
    else
      snprintf (dest + strlen (dest), len - strlen (dest), "%s/", ciss->host);
  }

  if (ciss->path)
    strncat (dest, ciss->path, len - strlen (dest));

  return 0;
}


