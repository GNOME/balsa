/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Jay Painter and Stuart Parmenter
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
#include "libbalsa.h"
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

static gboolean
traverse_find_path (GNode * node, gpointer* d)
{
  const gchar * path;
  if (!node->data ||
      !LIBBALSA_IS_MAILBOX_LOCAL( ((MailboxNode *) node->data)->mailbox ) )
     return FALSE;

  path = LIBBALSA_MAILBOX_LOCAL( ((MailboxNode *) node->data)->mailbox )->path;
  if( strcmp ( path, (gchar *) d[0]) )
     return FALSE;
  d[1] = node;
  return TRUE;
}

static GNode *
mb_node_find_path (GNode * root, GTraverseType order, GTraverseFlags flags,
		   const gchar* data)
{
  gpointer d[2];

  d[0] = (gchar*)data; d[1] = NULL;
  g_node_traverse (root, order, flags, -1, 
		   (GNodeTraverseFunc)traverse_find_path, d);

  return d[1];
}

/* add_mailbox
   the function scans the local mail directory (LMD) and adds them to the 
   list of mailboxes. Takes care not to duplicate any of the "standard"
   mailboxes (inbox, outbox etc). Avoids also problems with aliasing 
   (someone added a local mailbox - possibly aliased - located in LMD 
   to the configuration).
*/
static void
add_mailbox (const gchar * name, const gchar * path, LibBalsaMailboxType type, 
	     gint isdir)
{
  LibBalsaMailbox *mailbox;
  GNode *rnode;
  GNode *node;

  if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.inbox))
    if (strcmp (path, LIBBALSA_MAILBOX_LOCAL (balsa_app.inbox)->path) == 0)
      return;
  if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.outbox))
    if (strcmp (path, LIBBALSA_MAILBOX_LOCAL (balsa_app.outbox)->path) == 0)
      return;

  if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.sentbox))
    if (strcmp (path, LIBBALSA_MAILBOX_LOCAL (balsa_app.sentbox)->path) == 0)
      return;

  if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.draftbox))
    if (strcmp (path, LIBBALSA_MAILBOX_LOCAL (balsa_app.draftbox)->path) == 0)
      return;

  if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.trash))
    if (strcmp (path, LIBBALSA_MAILBOX_LOCAL (balsa_app.trash)->path) == 0)
      return;
  
  /* don't add if the mailbox is already in the configuration */
  if (mb_node_find_path(balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
		   G_TRAVERSE_ALL, path)) return;

  if (isdir && type == MAILBOX_UNKNOWN)
    {
      gchar *tmppath;
      gchar *dirname;
      MailboxNode *mbnode;

      mbnode = mailbox_node_new (path, NULL, TRUE);
      tmppath = g_strdup_printf ("%s/.expanded", path);

      if (access (tmppath, F_OK) != -1)
	mbnode->expanded = TRUE;
      else
	mbnode->expanded = FALSE;
      node = g_node_new (mbnode);

      g_free(tmppath);

      dirname = g_dirname(path);
      rnode = find_my_node (balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
			    G_TRAVERSE_ALL, dirname);
      g_free(dirname);

    }
  else
    {
      switch ( type ) {
      case MAILBOX_MH:
      case MAILBOX_MBOX:
      case MAILBOX_MAILDIR:
	mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_local_new(path, FALSE));
	break;
      case MAILBOX_IMAP:
      case MAILBOX_POP3:
	g_warning ("Error: can't have a local IMAP or POP mailbox\n");
      default:
	g_warning("Unknown mailbox type\n");
	mailbox = NULL;
      }
      mailbox->name = g_strdup (name);

      if (isdir && type == MAILBOX_MH)
	{
	  /*      g_strdup (g_basename (g_dirname (myfile))) */
	  node = g_node_new (mailbox_node_new (path, mailbox, TRUE));
	  rnode = find_my_node (balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
				G_TRAVERSE_ALL, g_dirname (path));
	}
      else
	{
	  char *dirname = g_dirname (path);
	  node = g_node_new (mailbox_node_new (path, mailbox, FALSE));
	  rnode = find_my_node (balsa_app.mailbox_nodes, G_LEVEL_ORDER, 
				G_TRAVERSE_ALL, dirname);
	  g_free (dirname);
	}

      if (balsa_app.debug)
	g_print (_ ("Local Mailbox Loaded as: %s\n"), 
		 gtk_type_name (GTK_OBJECT_TYPE(mailbox)));
    }
  if (rnode)
      g_node_append (rnode, node);
  else
      g_node_append (balsa_app.mailbox_nodes, node);
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
      if (!isdigit ((unsigned char)(str[i])))
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
  LibBalsaMailboxType mailbox_type;


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
      mailbox_type = libbalsa_mailbox_valid (filename);
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

	  mailbox_type = libbalsa_mailbox_valid (filename);
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

  g_return_if_fail(balsa_app.local_mail_directory != NULL);
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
