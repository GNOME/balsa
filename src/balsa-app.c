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
#include <proplist.h>
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

  balsa_app.proplist = PLGetProplistWithPath ("~/.balsarc");

  if (!balsa_app.proplist)
    {
      initialize_balsa (argc, argv);
      return;
    }

  /* Load all the global settings.  If there's an error, then some crucial
     piece of the global settings was not available, and we need to run
     balsa-init. */
  if (restore_global_settings() == FALSE)
    {
      initialize_balsa (argc, argv);
      return;
    }

  /* initalize our mailbox access crap */
  do_load_mailboxes ();

  /* At this point, if inbox/outbox/trash are still null, then we
     were not able to locate the settings for them anywhere in our
     configuartion and should run balsa-init. */
  if (balsa_app.inbox == NULL || balsa_app.outbox == NULL ||
      balsa_app.trash == NULL)
    {
      initialize_balsa (argc, argv);
      return;
    }

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
  read_signature ();
  mailboxes_init ();

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
  gint num = 0;
  gint i = 0;
  proplist_t accts, elem, temp_str;

  temp_str = PLMakeString("accounts");
  accts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease(temp_str);
  if (accts == NULL)
    num = 0;
  else
    num = PLGetNumberOfElements (accts);

  for (i = 0; i < num; i++)
    {
      elem = PLGetArrayElement (accts, i);
      if (load_mailboxes (PLGetString (elem)) == FALSE)
	{
	  fprintf(stderr, "*** An error occurred while loading the "
		  "mailbox: %s\n", PLGetString(elem));
	  return 0;
	}
      if (balsa_app.debug)
	fprintf (stderr, "Loaded mailbox: %s\n", PLGetString (elem));
    }

  return 1;
}


static void
special_mailboxes ()
{
}
