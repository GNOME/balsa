/* Balsa E-Mail Library
 * Copyright (C) 1998 Stuart Parmenter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <gnome.h>

#include "mailbox.h"

#include <sys/stat.h>
#include <utime.h>
#include <sys/file.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#if 0
static GList *mbox_parse_mailbox (Mailbox *);

void 
mbox_open_mailbox (Mailbox * mb)
{
  printf ("opening mailbox\n");
  mb->fd = fopen (MAILBOX_LOCAL (mb)->path, "r+");
  mbox_parse_mailbox (mb);
}

static GList *
mbox_parse_mailbox (Mailbox * mb)
{
  struct stat sb;
  char buf[1024];
  Message *msg;
  time_t t;
  int count = 0, lines = 0;
  long loc;
  GList *list;

  /* Save information about the folder at the time we opened it. */
  if (stat (MAILBOX_LOCAL (mb)->path, &sb) == -1)
    {
      perror (MAILBOX_LOCAL (mb)->path);
      return NULL;
    }

  mb->size = sb.st_size;
  mb->mtime = sb.st_mtime;

  loc = ftell (mb->fd);
  while (fgets (buf, sizeof (buf), mb->fd) != NULL)
    {
      if (!strncmp ("From ", buf, 5))
	fprintf (stderr, "%s\n", buf);
    }

  return list;
}
#endif
