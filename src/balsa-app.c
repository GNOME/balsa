/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others
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
#include "information.h"

/* Global application structure */
struct BalsaApplication balsa_app;

/* prototypes */
static gboolean check_special_mailboxes (void);

/* ask_password:
   asks the user for the password to the mailbox on given remote server.
*/
static void handle_password(gchar * string, gchar **target)
{ *target = string; }
gchar* ask_password(LibBalsaServer* server, LibBalsaMailbox *mbox) 
{
  GtkWidget *dialog;
  gchar * prompt, *passwd = NULL;
  
  g_return_val_if_fail(server != NULL, NULL);
  if(mbox) 
    prompt = g_strdup_printf( 
      _("Opening remote mailbox %s.\nThe password for %s@%s:"), 
      mbox->name, server->user, server->host);
  else 
    prompt = g_strdup_printf( _("Mailbox password for %s@%s:"), 
			      server->user, server->host);
  dialog = gnome_request_dialog (TRUE, prompt, NULL,
				 0,
				 (GnomeStringCallback)handle_password,
				 (gpointer)&passwd,
				 GTK_WINDOW(balsa_app.main_window));
  g_free(prompt);
  gnome_dialog_run_and_close(GNOME_DIALOG(dialog));
  return passwd;
}

void
balsa_app_init (void)
{
  /* 
   * initalize application structure before ALL ELSE 
   * to some reasonable defaults
   */
  balsa_app.address = libbalsa_address_new ();
  balsa_app.replyto = NULL;
  balsa_app.domain = NULL;
  balsa_app.bcc = NULL;

  balsa_app.local_mail_directory = NULL;
  balsa_app.signature_path = NULL;
  balsa_app.sig_separator = TRUE;
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
  balsa_app.show_mblist = TRUE;
  balsa_app.show_notebook_tabs = FALSE;

  balsa_app.index_num_width = NUM_DEFAULT_WIDTH;
  balsa_app.index_status_width = STATUS_DEFAULT_WIDTH;
  balsa_app.index_attachment_width = ATTACHMENT_DEFAULT_WIDTH;
  balsa_app.index_from_width = FROM_DEFAULT_WIDTH;
  balsa_app.index_subject_width = SUBJECT_DEFAULT_WIDTH;
  balsa_app.index_date_width = DATE_DEFAULT_WIDTH;

  /* file paths */
  balsa_app.attach_dir = NULL;

/* Mailbox list column width (not fully implemented) */
  balsa_app.mblist_name_width = MBNAME_DEFAULT_WIDTH;

  balsa_app.mblist_show_mb_content_info = FALSE;
  balsa_app.mblist_newmsg_width = NEWMSGCOUNT_DEFAULT_WIDTH;
  balsa_app.mblist_totalmsg_width = TOTALMSGCOUNT_DEFAULT_WIDTH;

  balsa_app.visual = NULL;
  balsa_app.colormap = NULL;

  balsa_app.mblist_unread_color.red = MBLIST_UNREAD_COLOR_RED;
  balsa_app.mblist_unread_color.blue = MBLIST_UNREAD_COLOR_BLUE;
  balsa_app.mblist_unread_color.green = MBLIST_UNREAD_COLOR_GREEN;

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
  balsa_app.date_string = g_strdup(DEFAULT_DATE_FORMAT);

  /* address book */
  balsa_app.ab_dist_list_mode = FALSE;
  balsa_app.ab_location = 
      gnome_util_prepend_user_home(DEFAULT_ADDRESS_BOOK_PATH);

  /* Information messages */
  balsa_app.information_message = 0;
  balsa_app.warning_message = 0;
  balsa_app.error_message = 0;
  balsa_app.debug_message = 0;

#ifdef ENABLE_LDAP
  balsa_app.ldap_host = NULL;
  balsa_app.ldap_base_dn = NULL;
#endif /* ENABLE_LDAP */
}

gboolean
do_load_mailboxes (void)
{
  if( check_special_mailboxes () )
	return FALSE;

  if ( LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.inbox) )
  {
    libbalsa_set_spool (LIBBALSA_MAILBOX_LOCAL(balsa_app.inbox)->path);
  }
  else if ( LIBBALSA_IS_MAILBOX_IMAP(balsa_app.inbox) || LIBBALSA_IS_MAILBOX_POP3(balsa_app.inbox) )
  {
    /* Do nothing */
  }
  else
  {
    fprintf (stderr, "do_load_mailboxes: Unknown inbox mailbox type\n");
    return FALSE;
  }

  load_local_mailboxes ();

  return TRUE;
}

static gboolean
check_special_mailboxes (void)
{
	gboolean bomb = FALSE;

	if( balsa_app.inbox == NULL ) {
		balsa_information ( LIBBALSA_INFORMATION_WARNING, _("Balsa cannot open your \"%s\" mailbox."),  _("Inbox") );
		bomb = TRUE;
	}

	if( balsa_app.outbox == NULL ) {
		balsa_information( LIBBALSA_INFORMATION_WARNING, _("Balsa cannot open your \"%s\" mailbox."),  _("Outbox") );
		bomb = TRUE;
	} 

	if( balsa_app.sentbox == NULL ) {
		balsa_information( LIBBALSA_INFORMATION_WARNING, _("Balsa cannot open your \"%s\" mailbox."),  _("Sentbox") );
		bomb = TRUE;
	}

	if( balsa_app.draftbox == NULL ) {
		balsa_information( LIBBALSA_INFORMATION_WARNING, _("Balsa cannot open your \"%s\" mailbox."),  _("Draftbox") );
		bomb = TRUE;
	}

	if( balsa_app.trash == NULL ) {
		balsa_information( LIBBALSA_INFORMATION_WARNING, _("Balsa cannot open your \"%s\" mailbox."),  _("Trash") );
		bomb = TRUE;
	}

	return bomb;
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

/* searching mailbox tree code, see balsa_find_mbox_by_name below */
static gboolean
mbox_by_name (gconstpointer a, gconstpointer b)
{
  MailboxNode *mbnode = (MailboxNode *) a;
  const gchar *name = (const gchar *) b;
  g_assert(mbnode != NULL);

  if(mbnode->mailbox == NULL) 
    return TRUE;
  return strcmp(mbnode->mailbox->name, name) != 0;
}

/* mblist_find_mbox_by_name:
   search the mailboxes tree for given name.
*/
LibBalsaMailbox *
balsa_find_mbox_by_name (const gchar *name) {
  GtkCTreeNode *node;
  LibBalsaMailbox *res = NULL;

  
  if (balsa_app.mailbox_nodes && name && *name) {
      if (!strcmp (name, balsa_app.sentbox->name))
	  res = balsa_app.sentbox;
      else if (!strcmp (name, balsa_app.draftbox->name))
	  res = balsa_app.draftbox;
      else if (!strcmp (name, balsa_app.outbox->name))
	  res = balsa_app.outbox;
      else if (!strcmp (name, balsa_app.trash->name))
	  res = balsa_app.trash;
      else {
	  node = gtk_ctree_find_by_row_data_custom (
	      GTK_CTREE(balsa_app.mblist), NULL,
	      (gchar*)name, mbox_by_name);
	  if(node) {
	      MailboxNode * mbnode = gtk_ctree_node_get_row_data(
		  GTK_CTREE(balsa_app.mblist),node);
	      res = mbnode->mailbox;
	  } else
	      g_print("balsa_find_mbox_by_name: Mailbox %s not found\n",name);
      }
  }
  return res;
}
