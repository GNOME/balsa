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

static struct mapping_t AliasHelp[] = {
  { "Exit",   OP_EXIT },
  { "Select", OP_GENERIC_SELECT_ENTRY },
  { "Help",   OP_HELP },
  { NULL }
};

static const char *
alias_format_str (char *dest, size_t destlen, char op, const char *src,
		  const char *fmt, const char *ifstring, const char *elsestring,
		  unsigned long data, format_flag flags)
{
  char tmp[SHORT_STRING], adr[SHORT_STRING];
  ALIAS *alias = (ALIAS *) data;

  switch (op)
  {
    case 'a':
      snprintf (tmp, sizeof (tmp), "%%%ss", fmt);
      snprintf (dest, destlen, tmp, alias->name);
      break;
    case 'r':
      adr[0] = 0;
      rfc822_write_address (adr, sizeof (adr), alias->addr);
      snprintf (tmp, sizeof (tmp), "%%%ss", fmt);
      snprintf (dest, destlen, tmp, adr);
      break;
    case 'n':
      snprintf (tmp, sizeof (tmp), "%%%sd", fmt);
      snprintf (dest, destlen, tmp, alias->num + 1);
      break;
    case 't':
      dest[0] = alias->tagged ? '*' : ' ';
      dest[1] = 0;
      break;
  }

  return (src);
}

int alias_search (MUTTMENU *m, regex_t *re, int n)
{
  char s[LONG_STRING];
  int slen = sizeof(s);

  mutt_FormatString (s, slen, NONULL (AliasFmt), alias_format_str,
                    (unsigned long) ((ALIAS **) m->data)[n], 0);
  return regexec (re, s, 0, NULL, 0);
}


void alias_entry (char *s, size_t slen, MUTTMENU *m, int num)
{
  mutt_FormatString (s, slen, NONULL (AliasFmt), alias_format_str, (unsigned long) ((ALIAS **) m->data)[num], 0);
}

int alias_tag (MUTTMENU *menu, int n)
{
  return (((ALIAS **) menu->data)[n]->tagged = !((ALIAS **) menu->data)[n]->tagged);
}

static int alias_SortAlias (const void *a, const void *b)
{
  ALIAS *pa = *(ALIAS **) a;
  ALIAS *pb = *(ALIAS **) b;
  int r = strcasecmp (pa->name, pb->name);

  return (RSORT (r));
}

static int alias_SortAddress (const void *a, const void *b)
{
  ADDRESS *pa = (*(ALIAS **) a)->addr;
  ADDRESS *pb = (*(ALIAS **) b)->addr;
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
  ALIAS **AliasTable = NULL;
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
  {
    aliasp->tagged = 0;
    menu->max++;
  }

  menu->data = AliasTable = (ALIAS **) safe_calloc (menu->max, sizeof (ALIAS *));

  for (i = 0, aliasp = aliases; aliasp; aliasp = aliasp->next, i++)
    AliasTable[i] = aliasp;

  if ((SortAlias & SORT_MASK) != SORT_ORDER)
  {
    qsort (AliasTable, i, sizeof (ALIAS *),
	 (SortAlias & SORT_MASK) == SORT_ADDRESS ? alias_SortAddress : alias_SortAlias);
  }

  for (i=0; i<menu->max; i++) AliasTable[i]->num = i;

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
    if (AliasTable[i]->tagged)
    {
      rfc822_write_address (buf, buflen, AliasTable[i]->addr);
      t = -1;
    }
  }

  if(t != -1)
    rfc822_write_address (buf, buflen, AliasTable[t]->addr);
  
  mutt_menuDestroy (&menu);
  safe_free ((void **) &AliasTable);
}
