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
#include <errno.h>
#include <fcntl.h>

#include <gnome.h>
#include <string.h>

#include <sys/stat.h>

#include "balsa-app.h"
#include "balsa-mblist.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "mailbox-node.h"
#include "pref-manager.h"
#include "save-restore.h"

#include "libbalsa.h"

typedef struct _MailboxConfWindow MailboxConfWindow;
struct _MailboxConfWindow {
    LibBalsaMailbox *mailbox;


    GtkWidget *window;

    GtkWidget *mailbox_name;
    GtkType mailbox_type;

    union {
	/* for local mailboxes */
	struct local { 
	    GtkWidget *path;
	} local;
	/* for imap mailboxes & directories */
	struct {
	    GtkWidget *server;
	    GtkWidget *port;
	    GtkWidget *username;
	    GtkWidget *password;
	    GtkWidget *folderpath;
	} imap;

	/* for pop3 mailboxes */
	struct {
	    GtkWidget *server;
	    GtkWidget *port;
	    GtkWidget *username;
	    GtkWidget *password;
	    GtkWidget *check;
	    GtkWidget *delete_from_server;
	    GtkWidget *use_apop;
	    GtkWidget *filter;
	} pop3;
    } mb_data;
};

/* callback */
static void check_for_blank_fields(GtkWidget *widget, MailboxConfWindow *mcw);

static void mailbox_conf_update(MailboxConfWindow *conf_window);
static void mailbox_conf_add(MailboxConfWindow *conf_window);

/* misc functions */
static void mailbox_conf_set_values(MailboxConfWindow *mcw);

static void fill_in_imap_data(MailboxConfWindow *mcw, gchar ** name, gchar ** path);
static void update_imap_mailbox(MailboxConfWindow *mcw);

static void update_pop_mailbox(MailboxConfWindow *mcw);

/* pages */
static GtkWidget *create_page(MailboxConfWindow *mcw);
static GtkWidget *create_local_mailbox_page(MailboxConfWindow *mcw);
static GtkWidget *create_pop_mailbox_page(MailboxConfWindow *mcw);
static GtkWidget *create_imap_mailbox_page(MailboxConfWindow *mcw);

static GtkWidget *create_label(const gchar * label, GtkWidget * table, gint row, guint *keyval);
static GtkWidget *create_entry(MailboxConfWindow *mcw, GtkWidget * table, gint row, const gchar * initval, const guint keyval);
static GtkWidget *create_check(MailboxConfWindow *mcw, const gchar * label, GtkWidget * table, gint row);

#if 0
void mailbox_conf_edit_imap_server(GtkWidget * widget, gpointer data);
#endif

/* BEGIN OF COMMONLY USED CALLBACKS SECTION ---------------------- */

void
mailbox_conf_add_mbox_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MBOX);
}

void
mailbox_conf_add_maildir_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MAILDIR);
}

void
mailbox_conf_add_mh_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MH);
}

void
mailbox_conf_add_imap_cb(GtkWidget * widget, gpointer data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_IMAP);
}

void
mailbox_conf_delete_cb(GtkWidget * widget, gpointer data)
{
    BalsaMailboxNode *mbnode = mblist_get_selected_node(balsa_app.mblist);

    if (mbnode->mailbox == NULL) {
        GtkWidget *err_dialog =
            gnome_error_dialog(_("No mailbox selected."));
        gnome_dialog_run_and_close(GNOME_DIALOG(err_dialog));
        /* gtk_widget_destroy(GTK_WIDGET(err_dialog)); */
    } else {
	g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
	mailbox_conf_delete(mbnode);
    }
}


/* END OF COMMONLY USED CALLBACKS SECTION ------------------------ */
void
mailbox_conf_delete(BalsaMailboxNode * mbnode)
{
    GNode *gnode;
    gchar *msg;
    gint button;
    GtkWidget *ask;
    gint cancel_button;
    LibBalsaMailbox* mailbox = mbnode->mailbox;

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	/* FIXME: Should prompt to remove file aswell */
	msg = g_strdup_printf(_("This will remove the mailbox %s from the list of mailboxes.\n"
				"You may also delete the disk file or files associated with this mailbox.\n"
				"If you do not remove the file on disk you may \"Add  Mailbox\" to access the mailbox again.\n"
				"What would you like to do?"),
			      mailbox->name);
	ask = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_QUESTION,
				    _("Remove from list"), 
				    _("Remove from list and disk"),
				    _("Cancel"), NULL);
	cancel_button = 3;
	g_free(msg);
    } else {
	msg = g_strdup_printf(_("This will remove the mailbox %s from the list of mailboxes\n"
				"You may use \"Add Mailbox\" later to access this mailbox again\n"
				"What would you like to do?"),
			      mailbox->name);
	ask = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_QUESTION,
				    _("Remove from list"),
				    _("Cancel"),
				    NULL);
	cancel_button = 2;
	g_free(msg);
    }
    
    gnome_dialog_set_parent(GNOME_DIALOG(ask), 
			    GTK_WINDOW(balsa_app.main_window));
    gtk_window_set_modal(GTK_WINDOW(ask), TRUE);

    button = gnome_dialog_run(GNOME_DIALOG(ask));

    if ( button == cancel_button ) 
	return;

    /* Delete it from the config file and internal nodes */
    config_mailbox_delete(mailbox);

    /* Close the mailbox, in case it was open */
    if (!LIBBALSA_IS_MAILBOX_POP3(mailbox))
	balsa_window_close_mailbox(balsa_app.main_window, mailbox);

    /* Delete local files */
    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox) && button == 1)
	libbalsa_mailbox_local_remove_files(LIBBALSA_MAILBOX_LOCAL(
             mailbox));

    if (LIBBALSA_IS_MAILBOX_POP3(mailbox))
	update_pop3_servers();
    else
	balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));

    gtk_object_unref(GTK_OBJECT(mailbox));

    /* Remove the node from balsa's mailbox list */
    if (LIBBALSA_IS_MAILBOX_POP3(mailbox))
	balsa_app.inbox_input = g_list_remove(balsa_app.inbox_input, 
					      mailbox);
    else {
	gnode = find_gnode_in_mbox_list(balsa_app.mailbox_nodes, mailbox);
	if (!gnode) {
	    fprintf(stderr,
		    _("Oooop! mailbox not found in balsa_app.mailbox "
		      "nodes?\n"));
	} else {
	    g_node_unlink(gnode);
	    g_node_destroy(gnode); /* this will remove mbnode */
	}
    }
}

/*
 * Brings up dialog to configure a new mailbox of type mailbox_type.
 * If the used clicks save add the new mailbox to the tree.
 */
void
mailbox_conf_new(GtkType mailbox_type)
{
    GtkWidget *page;
    MailboxConfWindow *mcw;
    gint button;

    g_return_if_fail(gtk_type_is_a(mailbox_type, LIBBALSA_TYPE_MAILBOX));

    mcw = g_new0(MailboxConfWindow, 1);

    mcw->mailbox = NULL;
    mcw->mailbox_type = mailbox_type;

    mcw->window = gnome_dialog_new(_("Mailbox Configurator"),
				   NULL);
    gtk_window_set_wmclass(GTK_WINDOW(mcw->window), "mailbox_config_dialog", "Balsa");

    gnome_dialog_close_hides(GNOME_DIALOG(mcw->window), TRUE);

    gnome_dialog_append_button_with_pixmap(GNOME_DIALOG(mcw->window), 
					   _("Add"),
					   GNOME_STOCK_PIXMAP_NEW);
    gnome_dialog_append_button(GNOME_DIALOG(mcw->window), GNOME_STOCK_BUTTON_CLOSE);
    
    gnome_dialog_set_sensitive(GNOME_DIALOG(mcw->window), 0,
			       FALSE);
    gnome_dialog_set_default(GNOME_DIALOG(mcw->window), 0);

    gnome_dialog_set_parent(GNOME_DIALOG(mcw->window),
			    GTK_WINDOW(balsa_app.main_window));

    page = create_page(mcw);
    gtk_widget_show_all(page);    

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(mcw->window)->vbox),
		       page, TRUE, TRUE, 0);

    gtk_widget_grab_focus(mcw->mailbox_name);

    button = gnome_dialog_run(GNOME_DIALOG(mcw->window));

    if ( button == 0 )
	mailbox_conf_add(mcw);

    /* close the new mailbox window */
    gtk_object_destroy(GTK_OBJECT(mcw->window));

    g_free(mcw);

}

/*
 * Edit an existing mailboxes properties
 */
void
mailbox_conf_edit(BalsaMailboxNode *mbnode)
{
    GtkWidget *page;
    MailboxConfWindow *mcw;
    gint button;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));

    mcw = g_new0(MailboxConfWindow, 1);

    mcw->mailbox = mbnode->mailbox;
    mcw->mailbox_type = GTK_OBJECT_TYPE(GTK_OBJECT(mbnode->mailbox));

    mcw->window = gnome_dialog_new(_("Mailbox Configurator"), 
				   NULL);
    gtk_window_set_wmclass(GTK_WINDOW(mcw->window), "mailbox_config_dialog", "Balsa");

    gnome_dialog_close_hides(GNOME_DIALOG(mcw->window), TRUE);

    gnome_dialog_append_button_with_pixmap(GNOME_DIALOG(mcw->window), 
					   _("Update"),
					   GNOME_STOCK_PIXMAP_SAVE);
    gnome_dialog_append_button(GNOME_DIALOG(mcw->window), GNOME_STOCK_BUTTON_CLOSE);

    gnome_dialog_set_sensitive(GNOME_DIALOG(mcw->window), 0,
			       TRUE);
    gnome_dialog_set_default(GNOME_DIALOG(mcw->window), 0);

    gnome_dialog_set_parent(GNOME_DIALOG(mcw->window),
			    GTK_WINDOW(balsa_app.main_window));
    
    page = create_page(mcw);
    gtk_widget_show_all(page);

    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(mcw->window)->vbox),
		       page, TRUE, TRUE, 0);

    mailbox_conf_set_values(mcw);
    if(mbnode->parent && LIBBALSA_IS_MAILBOX_LOCAL(mbnode->mailbox))
	gtk_widget_set_sensitive(mcw->mb_data.local.path, FALSE);

    gtk_widget_grab_focus(mcw->mailbox_name);

    button = gnome_dialog_run(GNOME_DIALOG(mcw->window));
    
    if ( button == 0 ) 
	mailbox_conf_update(mcw);

    /* close the new mailbox window */
    gtk_object_destroy(GTK_OBJECT(mcw->window));

    g_free(mcw);
}

/*
 * Initialise the dialogs fields from mcw->mailbox
 */
static void
mailbox_conf_set_values(MailboxConfWindow *mcw)
{
    LibBalsaMailbox * mailbox;
    gchar *port;

    mailbox = mcw->mailbox;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (mailbox->name)
	gtk_entry_set_text(GTK_ENTRY(mcw->mailbox_name),
			   mailbox->name);

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	LibBalsaMailboxLocal *local;

	local = LIBBALSA_MAILBOX_LOCAL(mailbox);

	if (local->path)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.local.path),
			       local->path);
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	LibBalsaMailboxPop3 *pop3;
	LibBalsaServer *server;

	pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
	server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

	port = g_strdup_printf("%d", server->port);

	if (server->host)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.server), server->host);

	gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.port), port);

	if (server->user)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.username), server->user);
	if (server->passwd)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.password),
			       server->passwd);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.use_apop),
				     pop3->use_apop);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.check),
				     pop3->check);

	gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.delete_from_server),
	     pop3->delete_from_server);
	gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.filter),
	     pop3->filter);

	g_free(port);

    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	LibBalsaMailboxImap *imap;
	LibBalsaServer *server;

	imap = LIBBALSA_MAILBOX_IMAP(mailbox);
	server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

	port = g_strdup_printf("%d", server->port);

	if (server->host)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.server), server->host);
	if (server->user)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.username),
			       server->user);
	if (server->passwd)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.password),
			       server->passwd);
	gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.port), port);

	if (imap->path)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.folderpath),
			       imap->path);
	g_free(port);
    }
}


/*
 * Checks for blank fields in the dialog.
 * Sets the sensitivity of the Update/Add button accordingly.
 * This function should be attached to a change event signal 
 * on any widget which can effect the validity of the input.
 */
static void
check_for_blank_fields(GtkWidget *widget, MailboxConfWindow *mcw)
{
    gboolean sensitive = TRUE;

    if (!*gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name)))
	sensitive = FALSE;
    else if ( gtk_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL) ) {
	if (!*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.local.path)))
	    sensitive = FALSE;
    } else if ( gtk_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_IMAP ) ) {
	if (!strcmp
	    (gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.folderpath)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.server)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.username)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.port)), ""))
	    sensitive = FALSE;
    } else if ( gtk_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3) ) {
	if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.username)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.server)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.port)), ""))
	    sensitive = FALSE;
    }

    gnome_dialog_set_sensitive(GNOME_DIALOG(mcw->window),
			       0, sensitive);
}

/*
 * Update an IMAP mailbox with details from the dialog
 */
static void
fill_in_imap_data(MailboxConfWindow *mcw, gchar ** name, gchar ** path)
{
    gchar *fos;
    fos =
	gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.folderpath));

    if (!(*name =
	  g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name))))
	|| strlen(g_strstrip(*name)) == 0) {
	if (*name)
	    g_free(*name);

	*name = g_strdup_printf(_("%s on %s"), fos,
				gtk_entry_get_text(GTK_ENTRY
						   (mcw->mb_data.imap.server)));
    }
    *path = g_strdup(fos);
}

/*
 * Update a pop3 mailbox with details from the dialog
 */
static void
update_pop_mailbox(MailboxConfWindow *mcw)
{
    LibBalsaMailboxPop3 * mailbox;

    mailbox = LIBBALSA_MAILBOX_POP3(mcw->mailbox);

    g_free(LIBBALSA_MAILBOX(mailbox)->name);
    LIBBALSA_MAILBOX(mailbox)->name =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name)));

    libbalsa_server_set_username(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.pop3.username)));
    libbalsa_server_set_password(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.pop3.password)));
    libbalsa_server_set_host(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
			     gtk_entry_get_text(GTK_ENTRY
						(mcw->mb_data.pop3.server)),
			     atoi(gtk_entry_get_text
				  (GTK_ENTRY(mcw->mb_data.pop3.port))));
    mailbox->check =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.check));
    mailbox->use_apop =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.use_apop));
    mailbox->delete_from_server =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
				     (mcw->mb_data.pop3.delete_from_server));
    mailbox->filter =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.filter));
}

/*
 * Update an imap mcw->mailbox with details from the dialog
 */
static void
update_imap_mailbox(MailboxConfWindow *mcw)
{
    gchar *path;
    LibBalsaMailboxImap *mailbox;

    mailbox = LIBBALSA_MAILBOX_IMAP(mcw->mailbox);

    g_free(LIBBALSA_MAILBOX(mailbox)->name);
    fill_in_imap_data(mcw, &LIBBALSA_MAILBOX(mailbox)->name, &path);
    libbalsa_server_set_username(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.imap.username)));
    libbalsa_server_set_password(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.imap.password)));
    libbalsa_server_set_host(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
			     gtk_entry_get_text(GTK_ENTRY
						(mcw->mb_data.imap.server)),
			     atoi(gtk_entry_get_text
				  (GTK_ENTRY(mcw->mb_data.imap.port))));
    gtk_signal_connect(GTK_OBJECT(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox)),
		       "get-password", GTK_SIGNAL_FUNC(ask_password),
		       mailbox);

    libbalsa_mailbox_imap_set_path(mailbox,
				   (path == NULL
				    || path[0] == '\0') ? "INBOX" : path);
    g_free(path);
}

/* conf_update_mailbox:
   if changing path of the local mailbox in the local mail directory, just 
   rename the file, don't insert it to the configuration.
   FIXME: make sure that the rename breaks nothing. 
*/
static void
mailbox_conf_update(MailboxConfWindow *mcw)
{
    LibBalsaMailbox *mailbox;
    int i;
    gboolean update_config;
    
    mailbox = mcw->mailbox;

    update_config = mailbox->config_prefix != NULL;

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	gchar *filename, *name;

	filename =
	    gtk_entry_get_text(GTK_ENTRY((mcw->mb_data.local.path)));
	/* rename */
	if (
	    (i =
	     libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL
					     (mailbox), filename)) != 0) {
	    balsa_information(LIBBALSA_INFORMATION_WARNING,
			      _("Rename of %s to %s failed:\n%s"),
			      LIBBALSA_MAILBOX_LOCAL(mailbox)->path,
			      filename, strerror(i));
	    return;
	}
	/* update mailbox data */
	update_config = balsa_app.local_mail_directory == NULL
	    || strncmp(balsa_app.local_mail_directory, filename,
		       strlen(balsa_app.local_mail_directory)) != 0;
	/* change mailbox name */
	if (update_config)
	    name = gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name));
	else {
	    if (strcmp
		(gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name)),
		 mailbox->name) != 0) {
		balsa_information(LIBBALSA_INFORMATION_WARNING,
				  _
				  ("This is a mailbox in your local directory.\n"
				   "Change the path instead.\n"
				   "Mailbox not Updated.\n"));
		return;
	    } else {
		gchar *ptr = strrchr(filename, '/');
		name = ptr ? ptr + 1 : filename;
	    }
	}
	g_free(mailbox->name);
	mailbox->name = g_strdup(name);
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	update_pop_mailbox(mcw);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	update_imap_mailbox(mcw);
    }

    if (balsa_app.debug)
	g_print("Updating configuration data: %s\n",
		update_config ? "yes" : "no");

    if (update_config)
	config_mailbox_update(mailbox);

    if (LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox))
	/* redraw the pop3 server list */
	update_pop3_servers();
    else /* redraw the main mailbox list */
	balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

/*
 * Add a new mailbox, based on the contents of the dialog.
 */
static void
mailbox_conf_add(MailboxConfWindow *mcw)
{
    GNode *node;

    mcw->mailbox = gtk_type_new(mcw->mailbox_type);
    mcw->mailbox->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name)));

    if ( LIBBALSA_IS_MAILBOX_LOCAL(mcw->mailbox) ) {
	gchar *path;

	path = gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.local.path));
	libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL(mcw->mailbox), path);

	node =g_node_new(balsa_mailbox_node_new_from_mailbox(mcw->mailbox));
	g_node_append(balsa_app.mailbox_nodes, node);
    } else if ( LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox) ) {
	/* POP3 Mailboxes */
	update_pop_mailbox(mcw);
	balsa_app.inbox_input =
	    g_list_append(balsa_app.inbox_input, mcw->mailbox);
    } else if ( LIBBALSA_IS_MAILBOX_IMAP(mcw->mailbox) ) {
	update_imap_mailbox(mcw);

	node = g_node_new(balsa_mailbox_node_new_from_mailbox(mcw->mailbox));
	g_node_append(balsa_app.mailbox_nodes, node);
    } else {
	g_assert_not_reached();
    }
	/* IMAP Mailboxes */
#if 0
    case MC_PAGE_IMAP_DIR:
	{
	    ImapDir *dir = imapdir_new();
	    fill_in_imap_data(&dir->name, &dir->path);

	    dir->user =
		g_strdup(gtk_entry_get_text
			 (GTK_ENTRY(mcw->imap_username)));
	    dir->passwd =
		g_strdup(gtk_entry_get_text
			 (GTK_ENTRY(mcw->imap_password)));
	    dir->host =
		g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->imap_server)));
	    dir->port =
		atoi(gtk_entry_get_text(GTK_ENTRY(mcw->imap_port)));

	    imapdir_scan(dir);
	    if (!G_NODE_IS_LEAF(dir->file_tree)) {
		config_imapdir_add(dir);
		g_node_append(balsa_app.mailbox_nodes, dir->file_tree);
		dir->file_tree = NULL;
		imapdir_destroy(dir);
		return MC_RET_OK;
	    } else
		imapdir_destroy(dir);
	    /* and assume it was ordinary IMAP mailbox */
	}
#endif

    config_mailbox_add(mcw->mailbox, NULL);

    if (LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox))
	/* redraw the pop3 server list */
	update_pop3_servers();
    else /* redraw the main mailbox list */
	balsa_mblist_repopulate(BALSA_MBLIST(balsa_app.mblist));
}

/* Create a label and add it to a table */
static GtkWidget *
create_label(const gchar * label, GtkWidget * table, gint row, guint *keyval)
{
    guint kv;

    GtkWidget *w = gtk_label_new("");
    kv = gtk_label_parse_uline(GTK_LABEL(w), label);
    if ( keyval ) 
	*keyval = kv;

    gtk_misc_set_alignment(GTK_MISC(w), 1.0, 0.5);

    gtk_table_attach(GTK_TABLE(table), w, 0, 1, row, row + 1,
		     GTK_FILL, GTK_FILL, 5, 5);

    gtk_widget_show(w);
    return w;
}

static GtkWidget *
create_check(MailboxConfWindow *mcw, const gchar * label, GtkWidget * table, gint row)
{
    guint kv;
    GtkWidget *cb, *l;
    
    cb = gtk_check_button_new();

    l = gtk_label_new("");
    kv = gtk_label_parse_uline(GTK_LABEL(l), label);
    gtk_misc_set_alignment(GTK_MISC(l), 0.0, 0.5);
    gtk_widget_show(l);

    gtk_container_add(GTK_CONTAINER(cb), l);

    gtk_widget_add_accelerator(cb, "grab_focus",
			       GNOME_DIALOG(mcw->window)->accelerators,
			       kv, GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);

    gtk_table_attach(GTK_TABLE(table), cb, 1, 2, row, row+1,
		     GTK_FILL, GTK_FILL, 5, 5);

    return cb;
}

/* Create a text entry and add it to the table */
static GtkWidget *
create_entry(MailboxConfWindow *mcw, GtkWidget * table, gint row, const gchar * initval, const guint keyval)
{
    GtkWidget *entry = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, row, row + 1,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);
    if (initval)
	gtk_entry_append_text(GTK_ENTRY(entry), initval);

    gtk_widget_add_accelerator(entry, "grab_focus",
			       GNOME_DIALOG(mcw->window)->accelerators,
			       keyval, GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);

    gnome_dialog_editable_enters(GNOME_DIALOG(mcw->window),
				 GTK_EDITABLE(entry));

    /* Watch for changes... */
    gtk_signal_connect(GTK_OBJECT(entry), "changed", 
		       GTK_SIGNAL_FUNC(check_for_blank_fields), mcw);

    gtk_widget_show(entry);
    return entry;
}

/* Create a page for the type of mailbox... */
static GtkWidget *
create_page(MailboxConfWindow *mcw)
{
    if ( gtk_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL) ) {
	return create_local_mailbox_page(mcw);
    } else if ( gtk_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3) ) {
	return create_pop_mailbox_page(mcw);
    } else if ( gtk_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_IMAP) ) {
	return create_imap_mailbox_page(mcw);
    } else {
	g_warning("Unknown mailbox type: %s\n", gtk_type_name(mcw->mailbox_type));
	return NULL;
    }
}

static GtkWidget *
create_local_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *table;
    GtkWidget *file;
    guint keyval;
    table = gtk_table_new(2, 2, FALSE);

    /* mailbox name */
    create_label(_("Mailbox _Name:"), table, 0, &keyval);
    mcw->mailbox_name = create_entry(mcw, table, 0, NULL, keyval);

    /* path to file */
    create_label(_("Mailbox _Path:"), table, 1, &keyval);

    file = gnome_file_entry_new("Mailbox Path", "Mailbox Path");
    mcw->mb_data.local.path =
	gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(file));
    gtk_widget_add_accelerator(mcw->mb_data.local.path, 
			       "grab_focus",
			       GNOME_DIALOG(mcw->window)->accelerators,
			       keyval, GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);
    gnome_dialog_editable_enters(GNOME_DIALOG(mcw->window),
				 GTK_EDITABLE(mcw->mb_data.local.path));

    gtk_signal_connect(GTK_OBJECT(mcw->mb_data.local.path),
		       "changed",
		       GTK_SIGNAL_FUNC(check_for_blank_fields),
		       mcw);

    gtk_table_attach(GTK_TABLE(table), file, 1, 2, 1, 2,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    return table;
}

static GtkWidget *
create_pop_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *table;
    guint keyval;

    table = gtk_table_new(9, 2, FALSE);

    /* mailbox name */
    create_label(_("Mailbox _Name:"), table, 0, &keyval);
    mcw->mailbox_name = create_entry(mcw, table, 0, NULL, keyval);

    /* pop server */
    create_label(_("_Server:"), table, 1, &keyval);
    mcw->mb_data.pop3.server = create_entry(mcw, table, 1, "localhost", keyval);

    /* pop port */
    create_label(_("_Port:"), table, 2, &keyval);
    mcw->mb_data.pop3.port = create_entry(mcw, table, 2, "110", keyval);

    /* username  */
    create_label(_("_Username:"), table, 3, &keyval);
    mcw->mb_data.pop3.username = create_entry(mcw, table, 3, g_get_user_name(), keyval);

    /* password field */
    create_label(_("Pass_word:"), table, 4, &keyval);
    mcw->mb_data.pop3.password = create_entry(mcw, table, 4, NULL, keyval);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.pop3.password), FALSE);

    /* toggle for apop */
    mcw->mb_data.pop3.use_apop = create_check(mcw, _("Use _APOP Authentication"), table, 5);

    /* toggle for deletion from server */
    mcw->mb_data.pop3.delete_from_server = create_check(mcw, _("_Delete messages from server after download"),
							table, 6);

    /* Procmail */
    mcw->mb_data.pop3.filter = create_check(mcw,_("_Filter messages through procmail"),
					    table, 7);

    /* toggle for check */
    mcw->mb_data.pop3.check = create_check(mcw, _("Periodically _Check This Mailbox For New Mail"), 
					   table, 8);

    return table;
}

static GtkWidget *
create_imap_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *table;
    guint keyval;
    GtkWidget *entry;
    /*  GtkWidget *label; */

    table = gtk_table_new(6, 2, FALSE);

    /* mailbox name */
    create_label(_("Mailbox _Name:"), table, 0, &keyval);
    mcw->mailbox_name = create_entry(mcw, table, 0, NULL, keyval);

    /* imap server */
    create_label(_("_Server:"), table, 1, &keyval);
    mcw->mb_data.imap.server = create_entry(mcw, table, 1, "localhost", keyval);

    /* imap server port number */
    create_label(_("_Port:"), table, 2, &keyval);
    mcw->mb_data.imap.port = create_entry(mcw, table, 2, "143", keyval);

    /* username  */
    create_label(_("_Username:"), table, 3, &keyval);
    mcw->mb_data.imap.username = create_entry(mcw, table, 3, g_get_user_name(), keyval);

    /* password field */
    create_label(_("Pass_word:"), table, 4, &keyval);
    mcw->mb_data.imap.password = create_entry(mcw, table, 4, NULL, keyval);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.imap.password), FALSE);

    create_label(_("F_older Path:"), table, 5, &keyval);

    entry = gnome_entry_new("IMAP Folder History");
    mcw->mb_data.imap.folderpath = gnome_entry_gtk_entry(GNOME_ENTRY(entry));
    gtk_entry_append_text(GTK_ENTRY(mcw->mb_data.imap.folderpath), "INBOX");

    gtk_widget_add_accelerator(mcw->mb_data.imap.folderpath, 
			       "grab_focus",
			       GNOME_DIALOG(mcw->window)->accelerators,
			       keyval, GDK_MOD1_MASK, GTK_ACCEL_VISIBLE);
    gnome_dialog_editable_enters(GNOME_DIALOG(mcw->window),
				 GTK_EDITABLE(mcw->mb_data.imap.folderpath));

    gtk_signal_connect(GTK_OBJECT(mcw->mb_data.imap.folderpath),
		       "changed", GTK_SIGNAL_FUNC(check_for_blank_fields),
		       mcw);

    gnome_entry_append_history(GNOME_ENTRY(entry), 1,
			       "INBOX");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1,
			       "INBOX.Sent");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1,
			       "INBOX.Draft");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1,
			       "INBOX.outbox");

    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 5, 6,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    return table;
}

#if 0
void
mailbox_conf_edit_imap_server(GtkWidget * widget, gpointer data)
{
    GtkWidget *window;
    GtkWidget *clist;
    gchar *titles[2] = { "S", N_("Mailbox") };

    gint clicked_button;

    window = gnome_dialog_new("IMAP", GNOME_STOCK_BUTTON_CLOSE, NULL);
    gnome_dialog_set_parent(GNOME_DIALOG(window),
			    GTK_WINDOW(balsa_app.main_window));

#ifdef ENABLE_NLS
    titles[1] = _(titles[1]);
#endif
    clist = gtk_clist_new_with_titles(2, titles);
    gtk_clist_set_column_width(GTK_CLIST(clist), 1, 16);
    /*
       gtk_clist_set_policy (GTK_CLIST (clist), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
     */
    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(window)->vbox), clist, TRUE,
		       TRUE, 0);
    gtk_widget_show(clist);

    titles[0] = NULL;
    titles[1] = "INBOX";
    gtk_clist_append(GTK_CLIST(clist), titles);

    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    clicked_button = gnome_dialog_run(GNOME_DIALOG(window));
    if (clicked_button == 0) {
	return;
    }
}
#endif
