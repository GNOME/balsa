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
#include <fcntl.h>
#include <proplist.h>
#include <unistd.h>
#include <stdio.h>

#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-init.h"
#include "local-mailbox.h"
#include "misc.h"
#include "main.h"
#include "mailbox.h"
#include "save-restore.h"
#include "index-child.h"
#include "main-window.h"

/* Global application structure */
struct BalsaApplication balsa_app;


/* prototypes */
static void special_mailboxes (void);
static gint read_signature (void);


static void
error_exit_cb (GtkWidget * widget, gpointer data)
{
  balsa_exit ();
}

static void
update_gui(void)
{
    while (gtk_events_pending ())
         gtk_main_iteration ();
}
static void
balsa_error (const char *fmt,...)
{
  GtkWidget *messagebox;
  gchar outstr[522];
  va_list ap;

  va_start (ap, fmt);
  vsprintf (outstr, fmt, ap);
  va_end (ap);

  g_warning (outstr);

  messagebox = gnome_message_box_new (outstr,
				      GNOME_MESSAGE_BOX_ERROR,
				      GNOME_STOCK_BUTTON_OK,
				      NULL);
  gtk_widget_set_usize (messagebox, MESSAGEBOX_WIDTH, MESSAGEBOX_HEIGHT);
  gtk_window_set_position (GTK_WINDOW (messagebox), GTK_WIN_POS_CENTER);
  gtk_widget_show (messagebox);

  gtk_signal_connect (GTK_OBJECT (messagebox), "clicked",
		      GTK_SIGNAL_FUNC (error_exit_cb), NULL);
}


void
balsa_app_init (void)
{
  /* 
   * initalize application structure before ALL ELSE 
   * to some reasonable defaults
   */
  balsa_app.address = address_new ();
  balsa_app.replyto = NULL;

  balsa_app.local_mail_directory = NULL;
  balsa_app.signature_path = NULL;
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

  balsa_app.debug = FALSE;
  balsa_app.previewpane = TRUE;

  /* GUI settings */
  balsa_app.mw_width = MW_DEFAULT_WIDTH;
  balsa_app.mw_height = MW_DEFAULT_HEIGHT;
  balsa_app.toolbar_style = GTK_TOOLBAR_BOTH;

  /* arp */
  balsa_app.quote_str = NULL;
}

gint
do_load_mailboxes (void)
{
  load_local_mailboxes ();
  read_signature ();
  special_mailboxes ();

  if (!balsa_app.inbox)
    return FALSE;

  switch (balsa_app.inbox->type)
    {
    case MAILBOX_MAILDIR:
    case MAILBOX_MBOX:
    case MAILBOX_MH:
      mailbox_init (MAILBOX_LOCAL (balsa_app.inbox)->path,
		      balsa_error,
		      update_gui);
      break;

    case MAILBOX_IMAP:
      break;

    case MAILBOX_POP3:
      break;
    default:
      fprintf (stderr, "do_load_mailboxes: Unknown mailbox type\n");
      break;
    }

  return TRUE;
}

static gint
read_signature (void)
{
  FILE *fp;
  size_t len;

  g_free (balsa_app.signature);

  if (!(fp = fopen (balsa_app.signature_path, "r")))
    return FALSE;
  len = readfile (fp, &balsa_app.signature);
  if (len != 0)
    balsa_app.signature[len - 1] = '\0';
  fclose (fp);
  return TRUE;
}

static void
special_mailboxes (void)
{
}
