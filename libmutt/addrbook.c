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
#include "mutt_menu.h"
#include "sort.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define RSORT(x) (SortAlias & SORT_REVERSE) ? -x : x

typedef struct entry
{
  int tagged; /* has the user already added this alias to the list? */
  ALIAS *alias;
} ENTRY;

static struct mapping_t AliasHelp[] = {
  { "Exit",   OP_EXIT },
  { "Select", OP_GENERIC_SELECT_ENTRY },
  { "Help",   OP_HELP },
  { NULL }
};

int alias_search (MUTTMENU *m, regex_t *re, int n)
{
  ENTRY *table = (ENTRY *) m->data;

  return (regexec (re, table[n].alias->name, 0, NULL, 0));
}

/* %a	alias
   %n	number
   %r	address
   %t	tag */
static void
alias_fmt_str (char *d, size_t dlen, const char *s, ENTRY *table, int num)
{
  char *pd = d;
  char *pfmt;
  char fmt[SHORT_STRING];
  char buf[SHORT_STRING];
  char adr[SHORT_STRING];
  char tmp[SHORT_STRING];
  size_t len;

  dlen--; /* save room for the trailing \0 */
  while (*s && dlen > 0)
  {
    if (*s == '%')
    {
      s++;

      pfmt = fmt;
      while ((isdigit (*s) || *s == '.' || *s == '-') &&
	     (pfmt - fmt < sizeof (fmt) - 1))
	*pfmt++ = *s++;
      *pfmt = 0;

      switch (*s)
      {
	case '%':
	  buf[0] = '%';
	  buf[1] = 0;
	  break;
	case 'a':
	  snprintf (tmp, sizeof (tmp), "%%%ss", fmt);
	  snprintf (buf, sizeof (buf), tmp, table[num].alias->name);
	  break;
	case 'r':
	  adr[0] = 0;
	  rfc822_write_address (adr, sizeof (adr), table[num].alias->addr);
	  snprintf (tmp, sizeof (tmp), "%%%ss", fmt);
	  snprintf (buf, sizeof (buf), tmp, adr);
	  break;
	case 'n':
	  snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
	  snprintf (buf, sizeof (buf), tmp, num + 1);
	  break;
	case 't':
	  buf[0] = table[num].tagged ? '*' : ' ';
	  buf[1] = 0;
	  break;
      }
      len = strlen (buf);
      if (len > dlen)
	len = dlen;
      memcpy (pd, buf, len);
      pd += len;
      dlen -= len;
    }
    else
    {
      *pd++ = *s;
      dlen--;
    }
    s++;
  }
  *pd = 0;
}

/* This is the callback routine from mutt_menuLoop() which is used to generate
 * a menu entry for the requested item number.
 */
void alias_entry (char *s, size_t slen, MUTTMENU *m, int num)
{
  alias_fmt_str (s, slen, AliasFmt, (ENTRY *) m->data, num);
}

int alias_tag (MUTTMENU *menu, int n)
{
  return (((ENTRY *) menu->data)[n].tagged = !((ENTRY *) menu->data)[n].tagged);
}

static int alias_SortAlias (const void *a, const void *b)
{
  ALIAS *pa = ((ENTRY *) a)->alias;
  ALIAS *pb = ((ENTRY *) b)->alias;
  int r = strcasecmp (pa->name, pb->name);

  return (RSORT (r));
}

static int alias_SortAddress (const void *a, const void *b)
{
  ADDRESS *pa = ((ENTRY *) a)->alias->addr;
  ADDRESS *pb = ((ENTRY *) b)->alias->addr;
  int r;

  if (pa->personal)
  { 
    if (pb->personal)
      r = strcasecmp (pa->personal, pb->personal);
    else
      r = 1;
  }
  else if (pb->personal)
    r = -1;
  else
    r = strcasecmp (pa->mailbox, pb->mailbox);
  return (RSORT (r));
}

void mutt_alias_menu (char *buf, size_t buflen, ALIAS *aliases)
{
  ALIAS *aliasp;
  MUTTMENU *menu;
  ENTRY *AliasTable = NULL;
  int t = -1;
  int i, done = 0;
  char helpstr[SHORT_STRING];

  if (!aliases)
  {
    mutt_error ("You have no aliases!");
    return;
  }

  /* tell whoever called me to redraw the screen when I return */
  set_option (OPTNEEDREDRAW);

  menu = mutt_new_menu ();
  menu->make_entry = alias_entry;
  menu->search = alias_search;
  menu->tag = alias_tag;
  menu->menu = MENU_ALIAS;
  menu->title = "Aliases";
  menu->help = mutt_compile_help (helpstr, sizeof (helpstr), MENU_ALIAS, AliasHelp);

  /* count the number of aliases */
  for (aliasp = aliases; aliasp; aliasp = aliasp->next)
    menu->max++;

  menu->data = AliasTable = (ENTRY *) safe_calloc (menu->max, sizeof (ENTRY));

  for (i = 0, aliasp = aliases; aliasp; aliasp = aliasp->next, i++)
    AliasTable[i].alias = aliasp;

  if ((SortAlias & SORT_MASK) != SORT_ORDER)
  {
    qsort (AliasTable, i, sizeof (ENTRY),
	 (SortAlias & SORT_MASK) == SORT_ADDRESS ? alias_SortAddress : alias_SortAlias);
  }

  while (!done)
  {
    switch (mutt_menuLoop (menu))
    {
      case OP_GENERIC_SELECT_ENTRY:
        t = menu->current;
      case OP_EXIT:
	done = 1;
	break;
    }
  }

  for (i = 0; i < menu->max; i++)
  {
    if (AliasTable[i].tagged)
    {
      rfc822_write_address (buf, buflen, AliasTable[i].alias->addr);
      t = -1;
    }
  }

  if(t != -1)
    rfc822_write_address (buf, buflen, AliasTable[t].alias->addr);
  
  mutt_menuDestroy (&menu);
  safe_free ((void **) &AliasTable);
}
