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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <ctype.h>

#include "balsa-app.h"
#include "local-mailbox.h"
#include "mailbox.h"
#include "misc.h"


static gboolean
do_traverse (GNode * node, gpointer data)
{
  gpointer *d = data;
  if (!node->data)
    return FALSE;
  if (strcmp (((MailboxNode *) node->data)->name, (gchar *) d[0]))
    return FALSE;

  *(++d) = node;
  return TRUE;
}

static GNode *
find_my_node (GNode * root,
	      GTraverseType order,
	      GTraverseFlags flags,
	      gpointer data)
{
  gpointer d[2];

  g_return_val_if_fail (root != NULL, NULL);
  g_return_val_if_fail (order <= G_LEVEL_ORDER, NULL);
  g_return_val_if_fail (flags <= G_TRAVERSE_MASK, NULL);

  d[0] = data;
  d[1] = NULL;

  g_node_traverse (root, order, flags, -1, do_traverse, d);

  return d[1];
}

static void
add_mailbox (gchar * name, gchar * path, MailboxType type, gint isdir)
{
  Mailbox *mailbox;
  GNode *rnode;
  GNode *node;

  if (balsa_app.inbox->type != MAILBOX_IMAP &&
      balsa_app.inbox->type != MAILBOX_POP3)
    if (strcmp (path, MAILBOX_LOCAL (balsa_app.inbox)->path) == 0)
      return;
  if (balsa_app.outbox->type != MAILBOX_IMAP &&
      balsa_app.outbox->type != MAILBOX_POP3)
    if (strcmp (path, MAILBOX_LOCAL (balsa_app.outbox)->path) == 0)
      return;

  if (balsa_app.trash->type != MAILBOX_IMAP &&
      balsa_app.trash->type != MAILBOX_POP3)
    if (strcmp (path, MAILBOX_LOCAL (balsa_app.trash)->path) == 0)
      return;

  if (isdir && type == MAILBOX_UNKNOWN)
    {
      node = g_node_new (mailbox_node_new (g_strdup (path), NULL, TRUE));
      rnode = find_my_node (balsa_app.mailbox_nodes, G_LEVEL_ORDER, G_TRAVERSE_ALL, g_dirname (path));
      if (rnode)
	g_node_append (rnode, node);
      else
	g_node_append (balsa_app.mailbox_nodes, node);
    }
  else
    {
      mailbox = mailbox_new (type);
      mailbox->name = g_strdup (name);
      MAILBOX_LOCAL (mailbox)->path = g_strdup (path);

      if (isdir && type == MAILBOX_MH)
	{
	  /*      g_strdup (g_basename (g_dirname (myfile))) */
	  node = g_node_new (mailbox_node_new (g_strdup (path), mailbox, TRUE));
	  rnode = find_my_node (balsa_app.mailbox_nodes, G_LEVEL_ORDER, G_TRAVERSE_ALL, g_dirname (path));
	  if (rnode)
	    {
	      add_mailboxes_for_checking (mailbox);
	      g_node_append (rnode, node);
	    }
	  else
	    {
	      add_mailboxes_for_checking (mailbox);
	      g_node_append (balsa_app.mailbox_nodes, node);
	    }
	}
      else
	{
	  node = g_node_new (mailbox_node_new (g_strdup (path), mailbox, FALSE));
	  rnode = find_my_node (balsa_app.mailbox_nodes, G_LEVEL_ORDER, G_TRAVERSE_ALL, g_dirname (path));
	  if (rnode)
	    {
	      add_mailboxes_for_checking (mailbox);
	      g_node_append (rnode, node);
	    }
	  else
	    {
	      add_mailboxes_for_checking (mailbox);
	      g_node_append (balsa_app.mailbox_nodes, node);
	    }
	}
      if (balsa_app.debug)
	g_print (_ ("Local Mailbox Loaded as: %s\n"), mailbox_type_description (mailbox->type));
    }
}

static int
is_mh_message (gchar * str)
{
  gint i, len;
  len = strlen (str);

  /* check for ,[0-9]+ deleted messages */
  if (len && *str == ',' && is_mh_message (&str[1]))
    return 1;

  for (i = 0; i < len; i++)
    {
      if (!isdigit (str[i]))
	{
	  return 0;
	}
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

  if (d->d_name[0] == '.')
    return;


  snprintf (filename, PATH_MAX, "%s/%s", prefix, d->d_name);

  /* ignore file if it can't be read. */

  if (stat (filename, &st) == -1 || access (filename, R_OK) == -1)
    return;

  if (S_ISDIR (st.st_mode))
    {
      mailbox_type = mailbox_valid (filename);
      if (balsa_app.debug)
	fprintf (stderr, "Mailbox name = %s,  mailbox type = %d\n", filename, mailbox_type);
      if (mailbox_type == MAILBOX_MH || mailbox_type == MAILBOX_MAILDIR)
	{
	  add_mailbox (d->d_name, filename, mailbox_type, 1);
	}
      else
	{
	  add_mailbox (d->d_name, filename, MAILBOX_UNKNOWN, 1);
	}
      if (mailbox_type != MAILBOX_MAILDIR)
	{
	  dpc = opendir (filename);
	  if (!dpc)
	    return;
	  while ((dc = readdir (dpc)) != NULL)
	    read_dir (filename, dc);
	  closedir (dpc);
	}
    }
  else
    {
      if (!is_mh_message (d->d_name))
	{

	  mailbox_type = mailbox_valid (filename);
	  if (mailbox_type != MAILBOX_UNKNOWN)
	    {
	      add_mailbox (d->d_name, filename, mailbox_type, 0);
	    }
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
