/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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
#include <stdio.h>

#include "balsa-app.h"
#include "local-mailbox.h"
#include "misc.h"
#include "libbalsa.h"
#include "spell-check.h"
#include "main-window.h"
#include "information.h"

/* Global application structure */
struct BalsaApplication balsa_app;

const gchar *pspell_modules[] = {
    "ispell",
    "aspell"
};

const gchar *pspell_suggest_modes[] = {
    "fast",
    "normal",
    "bad-spellers"
};

/* ask_password:
   asks the user for the password to the mailbox on given remote server.
*/
static void
handle_password(gchar * string, gchar ** target)
{
    *target = string;
}

gchar *
ask_password(LibBalsaServer * server, LibBalsaMailbox * mbox)
{
    GtkWidget *dialog;
    gchar *prompt, *passwd = NULL;

    g_return_val_if_fail(server != NULL, NULL);
    if (mbox)
	prompt =
	    g_strdup_printf(_
			    ("Opening remote mailbox %s.\nThe password for %s@%s:"),
			    mbox->name, server->user, server->host);
    else
	prompt =
	    g_strdup_printf(_("Mailbox password for %s@%s:"), server->user,
			    server->host);

    dialog = gnome_request_dialog(TRUE, prompt, NULL,
				  0,
				  (GnomeStringCallback) handle_password,
				  (gpointer) & passwd,
				  GTK_WINDOW(balsa_app.main_window));
    g_free(prompt);
    gnome_dialog_run_and_close(GNOME_DIALOG(dialog));
    
    return passwd;
}

void
balsa_app_init(void)
{
    /* 
     * initalize application structure before ALL ELSE 
     * to some reasonable defaults
     */
    balsa_app.address = libbalsa_address_new();
    balsa_app.replyto = NULL;
    balsa_app.domain = NULL;
    balsa_app.bcc = NULL;

    balsa_app.local_mail_directory = NULL;
    balsa_app.signature_path = NULL;
    balsa_app.sig_separator = TRUE;
    balsa_app.smtp_server = NULL;
    balsa_app.smtp_port = 25;

    balsa_app.inbox = NULL;
    balsa_app.inbox_input = NULL;
    balsa_app.outbox = NULL;
    balsa_app.sentbox = NULL;
    balsa_app.draftbox = NULL;
    balsa_app.trash = NULL;

    balsa_app.mailbox_nodes = g_node_new(NULL);

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

    gdk_color_parse(MBLIST_UNREAD_COLOR, &balsa_app.mblist_unread_color);

    /* arp */
    balsa_app.quote_str = NULL;

    /* quote regex */
    balsa_app.quote_regex = g_strdup(DEFAULT_QUOTE_REGEX);

    /* font */
    balsa_app.message_font = NULL;
    balsa_app.subject_font = NULL;

    /*encoding */
    balsa_app.encoding_style = 0;

    /* compose: shown headers */
    balsa_app.compose_headers = NULL;

    balsa_app.PrintCommand.breakline = FALSE;
    balsa_app.PrintCommand.linesize = 78;
    balsa_app.PrintCommand.PrintCommand = NULL;

    /* date format */
    balsa_app.date_string = g_strdup(DEFAULT_DATE_FORMAT);

    /* address book */
    balsa_app.address_book_list = NULL;
    balsa_app.default_address_book = NULL;

    /* spell check */
    balsa_app.module = SPELL_CHECK_MODULE_ASPELL;
    balsa_app.suggestion_mode = SPELL_CHECK_SUGGEST_NORMAL;
    balsa_app.ignore_size = 0;
    balsa_app.check_sig = DEFAULT_CHECK_SIG;

    spell_check_modules_name = pspell_modules;
    spell_check_suggest_mode_name = pspell_suggest_modes;

    /* Information messages */
    balsa_app.information_message = 0;
    balsa_app.warning_message = 0;
    balsa_app.error_message = 0;
    balsa_app.debug_message = 0;

    /* Tooltips */
    balsa_app.tooltips = gtk_tooltips_new();
}

gboolean
do_load_mailboxes(void)
{
    if (LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.inbox)) {
	libbalsa_set_spool(LIBBALSA_MAILBOX_LOCAL(balsa_app.inbox)->path);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(balsa_app.inbox)
	       || LIBBALSA_IS_MAILBOX_POP3(balsa_app.inbox)) {
	/* Do nothing */
    } else {
	fprintf(stderr, "do_load_mailboxes: Unknown inbox mailbox type\n");
	return FALSE;
    }

    load_local_mailboxes();

    return TRUE;
}

void
update_timer(gboolean update, guint minutes)
{
    guint32 timeout;
    timeout = minutes * 60 * 1000;

    if (update) {
	if (balsa_app.check_mail_timer_id)
	    gtk_timeout_remove(balsa_app.check_mail_timer_id);

	balsa_app.check_mail_timer_id = gtk_timeout_add(timeout,
							(GtkFunction)
							check_new_messages_auto_cb,
							NULL);
    } else {
	if (balsa_app.check_mail_timer_id)
	    gtk_timeout_remove(balsa_app.check_mail_timer_id);
	balsa_app.check_mail_timer_id = 0;
    }
}



/* open_mailboxes_idle_cb:
   open mailboxes on startup if requested so.
   This is an idle handler. Be sure to use gdk_threads_{enter/leave}
   Release the passed argument when done.
 */
gboolean
open_mailboxes_idle_cb(gchar * names[])
{
    LibBalsaMailbox *mbox;
    gint i = 0;

    g_return_val_if_fail(names, FALSE);

    gdk_threads_enter();

    while (names[i]) {
	mbox = mblist_find_mbox_by_name(balsa_app.mblist, names[i]);
	if (balsa_app.debug)
	    fprintf(stderr, "open_mailboxes_idle_cb: opening %s => %p..\n",
		    names[i], mbox);
	if (mbox)
	    mblist_open_mailbox(mbox);
	i++;
    }
    g_strfreev(names);

    if (gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook)) >=
	0) gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook), 0);
    gdk_threads_leave();

    return FALSE;
}

GtkWidget *
gnome_stock_button_with_label(const char *icon, const char *label)
{
    return gnome_pixmap_button(gnome_stock_new_with_icon(icon), label);
}

static gint
find_mailbox(GNode * g1, gpointer data)
{
    BalsaMailboxNode *n1 = (BalsaMailboxNode *) g1->data;
    gpointer *d = data;
    LibBalsaMailbox *mb = *(LibBalsaMailbox **) data;

    if (!n1 || n1->mailbox != mb)
        return FALSE;

    *(++d) = g1;
    return TRUE;
}

/* find_gnode_in_mbox_list:
   looks for given mailbox in th GNode tree, usually but not limited to
   balsa_app.mailbox_nodes
*/
GNode *
find_gnode_in_mbox_list(GNode * gnode_list, LibBalsaMailbox * mailbox)
{
    gpointer d[2];
    GNode *retval;

    d[0] = mailbox;
    d[1] = NULL;

    g_node_traverse(gnode_list, G_IN_ORDER, G_TRAVERSE_LEAFS, -1,
                    find_mailbox, d);
    retval = d[1];
    return retval;
}
