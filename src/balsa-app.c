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
#include "local-mailbox.h"
#include "misc.h"
#include "main.h"
#include "libbalsa.h"
#include "save-restore.h"
#include "balsa-index-page.h"
#include "main-window.h"

/* Global application structure */
struct BalsaApplication balsa_app;


/* prototypes */
static void special_mailboxes (void);


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
  balsa_app.bcc = NULL;

  balsa_app.local_mail_directory = NULL;
  balsa_app.signature_path = NULL;
  balsa_app.smtp_server = NULL;

  balsa_app.inbox = NULL;
  balsa_app.inbox_input = NULL;
  balsa_app.outbox = NULL;
  balsa_app.sentbox = NULL;
  balsa_app.draftbox = NULL;
  balsa_app.trash = NULL;

  balsa_app.mailbox_nodes = g_node_new (NULL);

  balsa_app.new_messages_timer = 0;
  balsa_app.new_messages = 0;

  balsa_app.check_mail_auto = FALSE;
  balsa_app.check_mail_timer = 0;

  balsa_app.debug = FALSE;
  balsa_app.previewpane = TRUE;

  /* GUI settings */
  balsa_app.mblist_width = 100;
  balsa_app.mw_width = MW_DEFAULT_WIDTH;
  balsa_app.mw_height = MW_DEFAULT_HEIGHT;
  balsa_app.toolbar_style = GTK_TOOLBAR_BOTH;
  balsa_app.pwindow_option = WHILERETR;
  balsa_app.wordwrap = TRUE;
  balsa_app.wraplength = 79;
  balsa_app.browse_wrap = TRUE;
  balsa_app.shown_headers = HEADERS_SELECTED;
  balsa_app.selected_headers = g_strdup(DEFAULT_SELECTED_HDRS);

  balsa_app.index_num_width = NUM_DEFAULT_WIDTH;
  balsa_app.index_status_width = STATUS_DEFAULT_WIDTH;
  balsa_app.index_attachment_width = ATTACHMENT_DEFAULT_WIDTH;
  balsa_app.index_from_width = FROM_DEFAULT_WIDTH;
  balsa_app.index_subject_width = SUBJECT_DEFAULT_WIDTH;
  balsa_app.index_date_width = DATE_DEFAULT_WIDTH;

/* Mailbox list column width (not fully implemented) */
  balsa_app.mblist_name_width = MBNAME_DEFAULT_WIDTH;
#ifdef BALSA_SHOW_INFO
  balsa_app.mblist_show_mb_content_info = FALSE;
  balsa_app.mblist_newmsg_width = NEWMSGCOUNT_DEFAULT_WIDTH;
  balsa_app.mblist_totalmsg_width = TOTALMSGCOUNT_DEFAULT_WIDTH;
#endif

  /* arp */
  balsa_app.quote_str = NULL;

  /* font */
  balsa_app.message_font = NULL;

  /*encoding */
  balsa_app.encoding_style = 0;
  balsa_app.charset = NULL;

  balsa_app.checkbox = 0;

  /* compose: shown headers */
  balsa_app.compose_headers = NULL;

  balsa_app.PrintCommand.breakline = FALSE;
  balsa_app.PrintCommand.linesize = 78;
  balsa_app.PrintCommand.PrintCommand = NULL;

  /* date format */
  balsa_app.date_string = NULL;
}

gint
do_load_mailboxes (void)
{
  special_mailboxes ();

  /* load_local_mailboxes does not work well without trash */
  if (!balsa_app.trash) 
    return FALSE;
  load_local_mailboxes ();

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

static void
special_mailboxes (void)
{
}

void 
update_timer( gboolean update, guint minutes )
{
  guint32 timeout;
  timeout = minutes * 60 * 1000;

  if( update )
    {
      if( balsa_app.check_mail_timer_id )
	gtk_timeout_remove( balsa_app.check_mail_timer_id );
      balsa_app.check_mail_timer_id = gtk_timeout_add( timeout, 
	       (GtkFunction) check_new_messages_auto_cb, NULL);
    }
  else
    {
      if( balsa_app.check_mail_timer_id )
	gtk_timeout_remove( balsa_app.check_mail_timer_id );
      balsa_app.check_mail_timer_id = 0;
    }

}
