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
#include "mutt_curses.h"
#include "mapping.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* globals */
int *ColorQuote;
int ColorQuoteUsed;
int ColorDefs[MT_COLOR_MAX];
COLOR_LINE *ColorHdrList = NULL;
COLOR_LINE *ColorBodyList = NULL;

/* local to this file */
static int ColorQuoteSize;

#ifdef HAVE_COLOR

#define COLOR_DEFAULT (-2)

typedef struct color_list
{
  short fg;
  short bg;
  short index;
  short count;
  struct color_list *next;
} COLOR_LIST;

static COLOR_LIST *ColorList = NULL;
static int UserColors = 0;

static struct mapping_t Colors[] =
{
  { "black",	COLOR_BLACK },
  { "blue",	COLOR_BLUE },
  { "cyan",	COLOR_CYAN },
  { "green",	COLOR_GREEN },
  { "magenta",	COLOR_MAGENTA },
  { "red",	COLOR_RED },
  { "white",	COLOR_WHITE },
  { "yellow",	COLOR_YELLOW },
#if defined (USE_SLANG_CURSES) || defined (HAVE_USE_DEFAULT_COLORS)
  { "default",	COLOR_DEFAULT },
#endif
  { 0, 0 }
};

#endif /* HAVE_COLOR */

static struct mapping_t Fields[] =
{
  { "hdrdefault",	MT_COLOR_HDEFAULT },
  { "quoted",		MT_COLOR_QUOTED },
  { "signature",	MT_COLOR_SIGNATURE },
  { "indicator",	MT_COLOR_INDICATOR },
  { "status",		MT_COLOR_STATUS },
  { "tree",		MT_COLOR_TREE },
  { "error",		MT_COLOR_ERROR },
  { "normal",		MT_COLOR_NORMAL },
  { "tilde",		MT_COLOR_TILDE },
  { "markers",		MT_COLOR_MARKERS },
  { "header",		MT_COLOR_HEADER },
  { "body",		MT_COLOR_BODY },
  { "message",		MT_COLOR_MESSAGE },
  { "attachment",	MT_COLOR_ATTACHMENT },
  { "search",		MT_COLOR_SEARCH },
  { "bold",		MT_COLOR_BOLD },
  { "underline",	MT_COLOR_UNDERLINE },
  { NULL,		0 }
};

#define COLOR_QUOTE_INIT	8

void ci_start_color (void)
{
  memset (ColorDefs, A_NORMAL, sizeof (int) * MT_COLOR_MAX);
  ColorQuote = (int *) safe_malloc (COLOR_QUOTE_INIT * sizeof (int));
  memset (ColorQuote, A_NORMAL, sizeof (int) * COLOR_QUOTE_INIT);
  ColorQuoteSize = COLOR_QUOTE_INIT;
  ColorQuoteUsed = 0;

  /* set some defaults */
  ColorDefs[MT_COLOR_STATUS] = A_REVERSE;
  ColorDefs[MT_COLOR_INDICATOR] = A_REVERSE;
  ColorDefs[MT_COLOR_SEARCH] = A_REVERSE;
  ColorDefs[MT_COLOR_MARKERS] = A_REVERSE;
  /* special meaning: toggle the relevant attribute */
  ColorDefs[MT_COLOR_BOLD] = 0;
  ColorDefs[MT_COLOR_UNDERLINE] = 0;

#ifdef HAVE_COLOR
  start_color ();
#endif
}

#ifdef HAVE_COLOR

#ifdef USE_SLANG_CURSES
static char * get_color_name (int val)
{
  static char * missing[3] = {"brown", "lightgray", ""};
  int i;

  switch (val)
  {
    case COLOR_YELLOW:
      return (missing[0]);

    case COLOR_WHITE:
      return (missing[1]);
      
    case COLOR_DEFAULT:
      return (missing[2]);
  }

  for (i = 0; Colors[i].name; i++)
  {
    if (Colors[i].value == val)
      return (Colors[i].name);
  }
  return (Colors[0].name);
}
#endif

int mutt_alloc_color (int fg, int bg)
{
  COLOR_LIST *p = ColorList;
  int i;

  /* check to see if this color is already allocated to save space */
  while (p)
  {
    if (p->fg == fg && p->bg == bg)
    {
      (p->count)++;
      return (COLOR_PAIR (p->index));
    }
    p = p->next;
  }

  /* check to see if there are colors left */
  if (++UserColors > COLOR_PAIRS) return (A_NORMAL);

  /* find the smallest available index (object) */
  i = 1;
  FOREVER
  {
    p = ColorList;
    while (p)
    {
      if (p->index == i) break;
      p = p->next;
    }
    if (p == NULL) break;
    i++;
  }

  p = (COLOR_LIST *) safe_malloc (sizeof (COLOR_LIST));
  p->next = ColorList;
  ColorList = p;

  p->index = i;
  p->count = 1;
  p->bg = bg;
  p->fg = fg;

#if defined (USE_SLANG_CURSES)
  if (fg == COLOR_DEFAULT || bg == COLOR_DEFAULT)
    SLtt_set_color (i, NULL, get_color_name (fg), get_color_name (bg));
  else
#elif defined (HAVE_USE_DEFAULT_COLORS)
  if (fg == COLOR_DEFAULT)
    fg = -1;
  if (bg == COLOR_DEFAULT)
    bg = -1;
#endif

  init_pair (i, fg, bg);
  return (COLOR_PAIR (p->index));
}

void mutt_free_color (int fg, int bg)
{
  COLOR_LIST *p, *q;

  p = ColorList;
  while (p)
  {
    if (p->fg == fg && p->bg == bg)
    {
      (p->count)--;
      if (p->count > 0) return;

      UserColors--;
      if (p == ColorList)
      {
	ColorList = ColorList->next;
	safe_free ((void **) &p);
	return;
      }
      q = ColorList;
      while (q)
      {
	if (q->next == p)
	{
	  q->next = p->next;
	  safe_free ((void **) &p);
	  return;
	}
	q = q->next;
      }
      /* can't get here */
    }
    p = p->next;
  }
}

#endif /* HAVE_COLOR */

static COLOR_LINE *mutt_new_color_line (void)
{
  COLOR_LINE *p = safe_calloc (1, sizeof (COLOR_LINE));

  return (p);
}

static int add_pattern (COLOR_LINE **top, const char *s, int sensitive,
			int fg, int bg, int attr, char *err, size_t errlen)
{
  COLOR_LINE *tmp = *top;

  while (tmp)
  {
    if (sensitive)
    {
      if (strcmp (s, tmp->pattern) == 0)
	break;
    }
    else
    {
      if (strcasecmp (s, tmp->pattern) == 0)
	break;
    }
    tmp = tmp->next;
  }

  if (tmp)
  {
#ifdef HAVE_COLOR
    if (fg != -1 && bg != -1)
    {
      if (tmp->fg != fg || tmp->bg != bg)
      {
	mutt_free_color (tmp->fg, tmp->bg);
	tmp->fg = fg;
	tmp->bg = bg;
	attr |= mutt_alloc_color (fg, bg);
      }
      else
	attr |= (tmp->pair & ~A_BOLD);
    }
#endif /* HAVE_COLOR */
    tmp->pair = attr;
  }
  else
  {
    int r;

    tmp = mutt_new_color_line ();
    if ((r = REGCOMP (&tmp->rx, s, (sensitive ? mutt_which_case (s) : REG_ICASE))) != 0)
    {
      regerror (r, &tmp->rx, err, errlen);
      regfree (&tmp->rx);
      safe_free ((void **) &tmp);
      return (-1);
    }
    tmp->next = *top;
    tmp->pattern = safe_strdup (s);
#ifdef HAVE_COLOR
    tmp->fg = fg;
    tmp->bg = bg;
    attr |= mutt_alloc_color (fg, bg);
#endif
    tmp->pair = attr;

    *top = tmp;
  }

  return 0;
}

#ifdef HAVE_COLOR

static int parse_color_name (const char *s, int *col, int *attr, int brite,
			     char *errmsg, size_t errlen)
{
  char *eptr;

  if (strncasecmp (s, "bright", 6) == 0)
  {
    *attr |= brite;
    s += 6;
  }

  /* allow aliases for xterm color resources */
  if (strncasecmp (s, "color", 5) == 0)
  {
    s += 5;
    *col = strtol (s, &eptr, 10);
    if (!*s || *eptr || *col < 0 || *col >= COLORS)
    {
      snprintf (errmsg, errlen, "%s: color not supported by term", s);
      return (-1);
    }
  }
  else if ((*col = mutt_getvaluebyname (s, Colors)) == -1)
  {
    snprintf (errmsg, errlen, "%s: no such color", s);
    return (-1);
  }

#ifdef HAVE_USE_DEFAULT_COLORS
  if (*col == COLOR_DEFAULT && use_default_colors () != OK)
  {
    strfcpy (errmsg, "default colors not supported", errlen);
    return (-1);
  }
#endif

  return 0;
}

/* usage: color <object> <fg> <bg> [ <regexp> ] */
int mutt_parse_color (const char *s, unsigned long data, char *errmsg, size_t errlen)
{
  int object = 0, bold = 0, fg = 0, bg = 0, q_level = 0;
  int r = 0;
  char buf[SHORT_STRING];
  char expn[SHORT_STRING];
  char *eptr;

  /* don't parse curses command if we're not using the screen */
  if (option (OPTNOCURSES))
    return 0;

  /* ignore color commands if we're running on a mono terminal */
  if (!has_colors ())
    return 0;
  
  s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

  if (strncmp (buf, "quoted", 6) == 0)
  {
    if (buf[6])
    {
      q_level = strtol (buf + 6, &eptr, 10);
      if (*eptr || q_level < 0)
      {
	snprintf (errmsg, errlen, "%s: no such object", buf);
	return (-1);
      }
    }
    object = MT_COLOR_QUOTED;
  }
  else if ((object = mutt_getvaluebyname (buf, Fields)) == -1)
  {
    snprintf (errmsg, errlen, "%s: no such object", buf);
    return (-1);
  }

  /* first color */
  if (!s)
  {
    strfcpy (errmsg, "too few arguments", errlen);
    return (-1);
  }
  s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

  if (parse_color_name (buf, &fg, &bold, A_BOLD, errmsg, errlen) != 0)
    return (-1);

  /* second color */
  if (!s)
  {
    strfcpy (errmsg, "too few arguments", errlen);
    return (-1);
  }
  s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

  /* A_BLINK turns the background color brite on some terms */
  if (parse_color_name (buf, &bg, &bold, A_BLINK, errmsg, errlen) != 0)
    return (-1);

  if (object == MT_COLOR_HEADER || object == MT_COLOR_BODY)
  {
    if (!s)
    {
      strfcpy (errmsg, "too few arguments", errlen);
      return (-1);
    }

    s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);
    if (s)
    {
      strfcpy (errmsg, "too many arguments", errlen);
      return (-1);
    }

    if (object == MT_COLOR_HEADER)
      r = add_pattern (&ColorHdrList, buf, 0, fg, bg, bold, errmsg, errlen);
    else
      r = add_pattern (&ColorBodyList, buf, 1, fg, bg, bold, errmsg, errlen);
  }
  else if (object == MT_COLOR_QUOTED)
  {
    if (q_level >= ColorQuoteSize)
    {
      safe_realloc ((void **) &ColorQuote, (ColorQuoteSize += 2) * sizeof (int));
      ColorQuote[ColorQuoteSize-2] = ColorDefs[MT_COLOR_QUOTED];
      ColorQuote[ColorQuoteSize-1] = ColorDefs[MT_COLOR_QUOTED];
    }
    if (q_level >= ColorQuoteUsed)
      ColorQuoteUsed = q_level + 1;
    if (q_level == 0)
    {
      ColorDefs[MT_COLOR_QUOTED] = bold | mutt_alloc_color (fg, bg);

      ColorQuote[0] = ColorDefs[MT_COLOR_QUOTED];
      for (q_level = 1; q_level < ColorQuoteUsed; q_level++)
      {
	if (ColorQuote[q_level] == A_NORMAL)
	  ColorQuote[q_level] = ColorDefs[MT_COLOR_QUOTED];
      }
    }
    else
      ColorQuote[q_level] = bold | mutt_alloc_color (fg, bg);
  }
  else
    ColorDefs[object] = bold | mutt_alloc_color (fg, bg);

#ifdef HAVE_BKGDSET
  if (object == MT_COLOR_NORMAL)
    BKGDSET (MT_COLOR_NORMAL);
#endif

  return (r);
}

#endif /* HAVE_COLOR */

/*
 * command: mono <object> <attribute>
 *
 * set attribute for an object when using a terminal with no color support
 */
int mutt_parse_mono (const char *s, unsigned long data, char *err, size_t errlen)
{
  int r = 0;
  int object;
  int q_level = 0;
  int attr = A_NORMAL;
  char buf[SHORT_STRING];
  char expn[SHORT_STRING];
  char *eptr;

  if (option (OPTNOCURSES))
    return 0;

#ifdef HAVE_COLOR
  /* if we have color, ignore the mono commands */
  if (has_colors ())
    return 0;
#endif

  s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

  if (strncmp (buf, "quoted", 6) == 0)
  {
    if (buf[6])
    {
      q_level = strtol (buf + 6, &eptr, 10);
      if (*eptr || q_level < 0)
      {
	snprintf (err, errlen, "%s: no such object", buf);
	return (-1);
      }
    }
    object = MT_COLOR_QUOTED;
  }
  else if ((object = mutt_getvaluebyname (buf, Fields)) == -1)
  {
    snprintf (err, errlen, "%s: no such object", s);
    return (-1);
  }

  if (!s)
  {
    strfcpy (err, "too few arguments", errlen);
    return (-1);
  }

  s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);

  if (strcasecmp ("bold", buf) == 0)
    attr |= A_BOLD;
  else if (strcasecmp ("underline", buf) == 0)
    attr |= A_UNDERLINE;
  else if (strcasecmp ("none", buf) == 0)
    attr = A_NORMAL;
  else if (strcasecmp ("reverse", buf) == 0)
    attr |= A_REVERSE;
  else if (strcasecmp ("standout", buf) == 0)
    attr |= A_STANDOUT;
  else if (strcasecmp ("normal", buf) == 0)
    attr = A_NORMAL; /* needs use = instead of |= to clear other bits */
  else
  {
    snprintf (err, errlen, "%s: no such attribute", s);
    return (-1);
  }

  if (object == MT_COLOR_HEADER || object == MT_COLOR_BODY)
  {
    if (!s)
    {
      snprintf (err, errlen, "missing regexp");
      return (-1);
    }

    s = mutt_extract_token (buf, sizeof (buf), s, expn, sizeof (expn), 0);
    if (s)
    {
      strfcpy (err, "too many arguments", errlen);
      return (-1);
    }

    if (object == MT_COLOR_HEADER)
      r = add_pattern (&ColorHdrList, buf, 0, -1, -1, attr, err, errlen);
    else
      r = add_pattern (&ColorBodyList, buf, 1, -1, -1, attr, err, errlen);
  }
  else if (object == MT_COLOR_QUOTED)
  {
    if (q_level >= ColorQuoteSize)
    {
      safe_realloc ((void **) &ColorQuote, (ColorQuoteSize += 2) * sizeof (int));
      ColorQuote[ColorQuoteSize-2] = ColorDefs[MT_COLOR_QUOTED];
      ColorQuote[ColorQuoteSize-1] = ColorDefs[MT_COLOR_QUOTED];
    }
    if (q_level >= ColorQuoteUsed)
      ColorQuoteUsed = q_level + 1;
    if (q_level == 0)
    {
      ColorDefs[MT_COLOR_QUOTED] = attr;

      ColorQuote[0] = ColorDefs[MT_COLOR_QUOTED];
      for (q_level = 1; q_level < ColorQuoteUsed; q_level++)
      {
	if (ColorQuote[q_level] == A_NORMAL)
	  ColorQuote[q_level] = ColorDefs[MT_COLOR_QUOTED];
      }
    }
    else
      ColorQuote[q_level] = attr;
  }
  else
    ColorDefs[object] = attr;

  return (r);
}
