/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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
#include <fcntl.h>
#include <unistd.h>

#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-init.h"
#include "local-mailbox.h"
#include "misc.h"
#include "mailbox.h"
#include "save-restore.h"
#include "index-child.h"


/* Global application structure */
struct BalsaApplication balsa_app;


/* prototypes */
static int mailboxes_init ();
static void special_mailboxes ();
static gint read_signature ();
#if 0
static gint check_for_new_messages ();
#endif

void
init_balsa_app (int argc, char *argv[])
{
  gchar *tmp;

  /* 
   * initalize application structure before ALL ELSE 
   * to some reasonable defaults
   */
  balsa_app.real_name = NULL;
  balsa_app.username = NULL;
  balsa_app.hostname = NULL;
  balsa_app.local_mail_directory = NULL;
  balsa_app.smtp_server = NULL;

  balsa_app.inbox = NULL;
  balsa_app.inbox_input = NULL;
  balsa_app.outbox = NULL;
  balsa_app.trash = NULL;

  balsa_app.mailbox_nodes = g_node_new (NULL);
  balsa_app.current_index_child = NULL;

  balsa_app.new_messages_timer = 0;
  balsa_app.new_messages = 0;

  balsa_app.check_mail_timer = 0;

  /* GUI settings */
  balsa_app.mw_width = MW_DEFAULT_WIDTH;
  balsa_app.mw_height = MW_DEFAULT_HEIGHT;
  balsa_app.toolbar_style = GTK_TOOLBAR_BOTH;
  balsa_app.mdi_style = GNOME_MDI_DEFAULT_MODE;

  /* initalize our mailbox access crap */
  restore_global_settings ();

  read_signature ();

  /* Check to see if this is the first time we've run balsa */

  if (!balsa_app.inbox)
    {
      initialize_balsa (argc, argv);
      return;
    }

  do_load_mailboxes ();

  open_main_window ();

  /* start timers */
#if 0
  balsa_app.new_messages_timer = gtk_timeout_add (5, check_for_new_messages, NULL);
  balsa_app.check_mail_timer = gtk_timeout_add (5 * 60 * 1000, current_mailbox_check, NULL);
#endif
}

void
do_load_mailboxes ()
{
  if (!balsa_app.inbox)
    restore_global_settings ();

  switch (balsa_app.inbox->type)
    {
    case MAILBOX_MAILDIR:
    case MAILBOX_MBOX:
    case MAILBOX_MH:
      mailbox_init (MAILBOX_LOCAL (balsa_app.inbox)->path);
      break;

    case MAILBOX_IMAP:
      break;

    case MAILBOX_POP3:
      break;
    }

  mailboxes_init ();
  load_local_mailboxes ();
  read_signature ();
  special_mailboxes ();
}

static gint
read_signature ()
{
  FILE *fp;
  size_t len;

  gchar path[PATH_MAX];
  sprintf (path, "%s/.signature", g_get_home_dir ());
  if (!(fp = fopen (path, "r")))
    return FALSE;
  len = readfile (fp, &balsa_app.signature);
  if (len != 0)
    balsa_app.signature[len - 1] = '\0';
  fclose (fp);
  return TRUE;
}

#if 0
static gint
check_for_new_messages ()
{
  if (!balsa_app.current_mailbox)
    return TRUE;

  if (balsa_app.current_mailbox->stream->lock)
    {
      if (balsa_app.debug)
	fprintf (stderr, "Lock exists, waiting\n");
    }
  else if (balsa_app.new_messages > 0 && balsa_app.current_index_child)
    {
      balsa_index_append_new_messages (BALSA_INDEX (balsa_app.current_index_child->index));
      balsa_app.new_messages = 0;
    }

  return TRUE;
}
#endif



static gint
mailboxes_init (void)
{
  gchar **mailboxes;
  gint num = 0;
  gint i = 0;

  if (gnome_config_get_string ("/balsa/Global/Accounts"))
    gnome_config_get_vector ("/balsa/Global/Accounts", &num, &mailboxes);

  for (i = 0; num > i; i++)
    {
      if (balsa_app.debug)
	fprintf (stderr, "Loaded mailbox: %s\n", mailboxes[i]);
      load_mailboxes (mailboxes[i]);
    }

  return 1;
}


static void
special_mailboxes ()
{
}
