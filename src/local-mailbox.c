/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>

#include "balsa-app.h"
#include "local-mailbox.h"
#include "mailbox.h"

static void
add_mailbox (gchar * name, gchar * path, MailboxType type)
{
  Mailbox *mailbox;
  GNode *node;

  if (!strcmp (path, balsa_app.inbox_path))
    return;
  if (!strcmp (path, balsa_app.outbox_path))
    return;
  if (!strcmp (path, balsa_app.trash_path))
    return;
  mailbox = mailbox_new (type);
  mailbox->name = g_strdup (name);
  MAILBOX_LOCAL (mailbox)->path = g_strdup (path);

  node = g_node_new (mailbox);
  if (balsa_app.mailbox_nodes)
    g_node_append (balsa_app.mailbox_nodes, node);
  else
    balsa_app.mailbox_nodes = node;

  if (balsa_app.debug)
    g_print ("Local Mailbox Loaded as: %s\n", mailbox_type_description (mailbox->type));
}

static int
strisnum (gchar * str)
{
  gint i, len;
  len = strlen (str);
  for (len = 0; i <= len; i++)
    {
      if (!isdigit (str[i]))
	return 0;
    }
  return 1;
}

static void
read_dir (gchar * prefix, struct dirent *d)
{
  DIR *dpc;
  struct dirent *dc;

  char filename[PATH_MAX];
  struct stat st;
  MailboxType mailbox_type;

  if (!d)
    return;

  sprintf (filename, "%s/%s", prefix, d->d_name);

  if (stat (filename, &st) == -1)
    return;

  if (S_ISDIR (st.st_mode))
    {
      mailbox_type = mailbox_valid (filename);
      if (mailbox_type == MAILBOX_MH)
	add_mailbox (d->d_name, filename, mailbox_type);
      dpc = opendir (filename);
      if (!dpc)
	return;
      while ((dc = readdir (dpc)) != NULL)
	{
	  if (d->d_name[0] == '.')
	    continue;

	  read_dir (filename, dc);
	}
      closedir (dpc);
    }

  else
    {
      if (strisnum (d->d_name))
	{

	  mailbox_type = mailbox_valid (filename);
	  if (mailbox_type != MAILBOX_UNKNOWN)
	    add_mailbox (d->d_name, filename, mailbox_type);
	}
    }
}

void
load_local_mailboxes ()
{
  DIR *dp;
  struct dirent *d;

  dp = opendir (balsa_app.local_mail_directory);
  if (!dp)
    return;

  while ((d = readdir (dp)) != NULL)
    {
      if (d->d_name[0] == '.')
	continue;
      read_dir (balsa_app.local_mail_directory, d);
    }
  closedir (dp);
}
