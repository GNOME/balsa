/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include <gnome.h>

#include "libbalsa.h"
#include "misc.h"

MailboxNode *
mailbox_node_new (const gchar * name, Mailbox * mb, gint i)
{
  MailboxNode *mbn;
  mbn = g_malloc (sizeof (MailboxNode));
  mbn->ismbnode = TRUE;
  mbn->name = g_strdup (name);
  if (mb)
    mbn->mailbox = mb;
  else
    mbn->mailbox = NULL;
  mbn->IsDir = i;
  mbn->expanded = FALSE;
  return mbn;
}

gchar *
g_get_host_name (void)
{
  struct utsname utsname;
  uname (&utsname);
  return g_strdup (utsname.nodename);
}

gchar *
address_to_gchar (Address * addr)
{
  gchar *retc;

  GString *gs = g_string_new (NULL);

  if (addr->personal)
    {
      gs = g_string_append (gs, addr->personal);
      gs = g_string_append_c (gs, ' ');
    }
  if (addr->mailbox)
    {
      if (addr->personal)
	gs = g_string_append_c (gs, '<');
      gs = g_string_append (gs, addr->mailbox);
      if (addr->personal)
	gs = g_string_append_c (gs, '>');
    }
  retc = g_strdup (gs->str);
  g_string_free (gs, TRUE);
  return retc;
}

gchar *
make_string_from_list (GList * the_list)
{
  gchar *retc;
  GList *list;
  GString *gs = g_string_new (NULL);
  Address *addy;

  list = g_list_first (the_list);

  while (list)
    {
      addy = list->data;
      gs = g_string_append (gs, address_to_gchar (addy));

      if (list->next)
	gs = g_string_append (gs, ", ");

      list = list->next;
    }
  retc = g_strdup (gs->str);
  g_string_free (gs, 1);
  return retc;
}

size_t
readfile (FILE * fp, char **buf)
{
  size_t size;
  off_t offset;
  int r;
  int fd = fileno (fp);
  struct stat statbuf;

  if (fstat (fd, &statbuf) == -1)
    return -1;

  size = statbuf.st_size;

  if (!size)
    {
      *buf = NULL;
      return size;
    }

  lseek (fd, 0, SEEK_SET);

  *buf = (char *) g_malloc (size);
  if (*buf == NULL)
    {
      return -1;
    }

  offset = 0;
  while (offset < size)
    {
      r = read (fd, *buf + offset, size - offset);
      if (r == 0)
	return offset;

      if (r > 0)
	{
	  offset += r;
	}
      else if ((errno != EAGAIN) && (errno != EINTR))
	{
	  perror ("Error reading file:");
	  return -1;
	}
    }

  return size;
}
