/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

/* GENERAL NOTES:
   A. treatment of special mailboxes.

   Generally, the displayed mailbox name is same as the file/directory
   name of the mailbox. There is though en exception for special
   mailboxes that have their designated function: Inbox, Sendbox,
   Draftbox, Outbox.  Their default names are translated to a
   localized version. The file on disk should never have a localized
   names to avoid mess when user switches locale.

   - if user modifies the "file name" entry of the special mailbox
   modification dialog, it means it wants to rename the underlying
   file, not that he/she wants to use another file. User can use "Set
   as Inbox" etc to achieve this goal.
   See thread:
   http://mail.gnome.org/archives/balsa-list/2002-June/msg00044.html

   The mailbox_name field is displayed only for special mailboxes
   and POP3 mailboxes.
*/
#include "config.h"

#include <gnome.h>
#include <string.h>

#include "balsa-app.h"
#include "balsa-mblist.h"
#include "mailbox-conf.h"
#include "mailbox-node.h"
#include "pref-manager.h"
#include "save-restore.h"

#include "libbalsa.h"

typedef struct _MailboxConfWindow MailboxConfWindow;
struct _MailboxConfWindow {
    LibBalsaMailbox *mailbox;


    GtkDialog *window;

    void (*ok_handler)(MailboxConfWindow*);
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
            GtkWidget *remember;
	    GtkWidget *password;
	    GtkWidget *folderpath;
#ifdef USE_SSL
	    GtkWidget *use_ssl;
#endif
	} imap;

	/* for pop3 mailboxes */
	struct {
	    GtkWidget *server;
	    GtkWidget *username;
	    GtkWidget *password;
	    GtkWidget *check;
	    GtkWidget *delete_from_server;
	    GtkWidget *use_apop;
	    GtkWidget *filter;
#ifdef USE_SSL
#ifdef USE_SSL_FOR_POP3_IF_WE_EVER_DECIDE_WE_NEED_TO
	    GtkWidget *use_ssl;
#endif
#endif
	} pop3;
    } mb_data;
};

/* callback */
static void check_for_blank_fields(GtkWidget *widget, MailboxConfWindow *mcw);

static void mailbox_conf_update(MailboxConfWindow *conf_window);
static void mailbox_conf_add(MailboxConfWindow *conf_window);

/* misc functions */
static void mailbox_conf_set_values(MailboxConfWindow *mcw);

static void fill_in_imap_data(MailboxConfWindow *mcw, gchar ** name, 
                              gchar ** path);
static void update_imap_mailbox(MailboxConfWindow *mcw);

static void update_pop_mailbox(MailboxConfWindow *mcw);

/* pages */
static GtkWidget *create_page(MailboxConfWindow *mcw);
static GtkWidget *create_local_mailbox_page(MailboxConfWindow *mcw);
static GtkWidget *create_pop_mailbox_page(MailboxConfWindow *mcw);
static GtkWidget *create_imap_mailbox_page(MailboxConfWindow *mcw);

#if 0
void mailbox_conf_edit_imap_server(GtkWidget * widget, gpointer data);
#endif


#ifdef USE_SSL
static void imap_use_ssl_cb(GtkToggleButton* w, MailboxConfWindow * mcw);

static void
imap_use_ssl_cb(GtkToggleButton * button, MailboxConfWindow * mcw)
{
    char *colon, *newhost;
    const gchar* host = 
        gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.server));
    gchar* port = gtk_toggle_button_get_active(button) ? "993" : "143";

    if( (colon=strchr(host,':')) != NULL) 
        *colon = '\0';
    newhost = g_strconcat(host, ":", port, NULL);
    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.server), newhost);
    g_free(newhost);
}

#ifdef USE_SSL_FOR_POP3_IF_WE_EVER_DECIDE_WE_NEED_TO
static void pop3_use_ssl_cb(GtkWidget * w, MailboxConfWindow * mcw);

static void
pop3_use_ssl_cb(GtkWidget * w, MailboxConfWindow * mcw)
{
    char *colon, *newhost;
    const gchar* host = gtk_editable_get_text(mcw->mb_data.pop3.host);
    GtkToggleButton *button = GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.use_ssl);
    gchar* port = gtk_toggle_button_get_active(button) ? "995" : "110";

    if( (colon=strchr(text,':')) != NULL) 
        *colon = '\0';
    newhost = g_strconcat(host, ":", port);
    gtk_editable_set_text(mcw->mb_data.pop3.host, newhost);
    g_free(newhost);
}
#endif
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
    BalsaMailboxNode *mbnode =
        balsa_mblist_get_selected_node(balsa_app.mblist);

    if (mbnode->mailbox == NULL) {
        GtkWidget *err_dialog =
            gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                                   GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CANCEL,
                                   _("No mailbox selected."));
        gtk_dialog_run(GTK_DIALOG(err_dialog));
        gtk_widget_destroy(err_dialog);
    } else
	mailbox_conf_delete(mbnode);
}

/* This can be used  for both mailbox and folder edition */
void
mailbox_conf_edit_cb(GtkWidget * widget, gpointer data)
{
    BalsaMailboxNode *mbnode = 
        balsa_mblist_get_selected_node(balsa_app.mblist);
    balsa_mailbox_node_show_prop_dialog(mbnode);
}

/* END OF COMMONLY USED CALLBACKS SECTION ------------------------ */
void
mailbox_conf_delete(BalsaMailboxNode * mbnode)
{
    GNode *gnode;
    gint button;
    GtkWidget *ask;
    LibBalsaMailbox* mailbox = mbnode->mailbox;

    if(BALSA_IS_MAILBOX_SPECIAL(mailbox)) {
	balsa_information(
	    LIBBALSA_INFORMATION_ERROR,
	    _("Mailbox \"%s\" is used by balsa and I cannot remove it.\n"
	      "If you really want to remove it, assign its function\n"
	      "to some other mailbox."), mailbox->name);
	return;
    }

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
        ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_NONE,
                                     _("This will remove the mailbox "
                                       "\"%s\" from the list "
                                       "of mailboxes.  "
                                       "You may also delete the disk "
                                       "file or files associated with "
                                       "this mailbox.\n"
                                       "If you do not remove the file "
                                       "on disk you may \"Add  Mailbox\" "
                                       "to access the mailbox again.\n"
                                       "What would you like to do?"),
                                     mailbox->name);
        gtk_dialog_add_buttons(GTK_DIALOG(ask),
                               _("Remove from list"), 0,
                               _("Remove from list and disk"), 1,
                               _("Cancel"), GTK_RESPONSE_CANCEL,
                               NULL);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) && !mailbox->config_prefix) {
	/* deleting remote IMAP mailbox in a folder set */
        ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_NONE,
	                             _("This will remove the mailbox "
                                       "\"%s\" and all its messages "
                                       "from your IMAP server.  "
	                               "If %s has subfolders, it will "
                                       "still appear as a node in the "
                                       "folder tree.\n"
	                               "You may use "
                                       "\"New IMAP subfolder\" "
                                       "later to add a mailbox "
                                       "with this name.\n"
	                               "What would you like to do?"),
			             mailbox->name, mailbox->name);
        gtk_dialog_add_buttons(GTK_DIALOG(ask),
                               _("Remove from server"), 0,
                               _("Cancel"), GTK_RESPONSE_CANCEL,
                               NULL);
    } else { /* deleting other remote mailbox */
        ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_NONE,
	                             _("This will remove the mailbox "
                                       "\"%s\" from the list "
                                       "of mailboxes.\n"
				       "You may use \"Add Mailbox\" "
                                       "later to access "
                                       "this mailbox again.\n"
			 	       "What would you like to do?"),
			             mailbox->name);
        gtk_dialog_add_buttons(GTK_DIALOG(ask),
                               _("Remove from list"), 0,
                               _("Cancel"), GTK_RESPONSE_CANCEL,
                               NULL);
    }
    
    button = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);

    /* button < 0 means that the dialog window was closed without pressing
       any button other than CANCEL.
    */
    if ( button < 0)
	return;

    /* Delete it from the config file and internal nodes */
    config_mailbox_delete(mailbox);

    /* Close the mailbox, in case it was open */
    if (!LIBBALSA_IS_MAILBOX_POP3(mailbox))
	balsa_mblist_close_mailbox(mailbox);

    /* Remove mailbox on IMAP server */
    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) && !mailbox->config_prefix) {
	BalsaMailboxNode *parent = mbnode->parent;
	libbalsa_imap_delete_folder(LIBBALSA_MAILBOX_IMAP(mailbox));
	/* a chain of folders might go away, so we'd better rescan from
	 * higher up
	 */
	while (!parent->mailbox && parent->parent)
		parent = parent->parent;
	balsa_mailbox_node_rescan(parent); /* see it as server sees it */
	return;
    }

    /* Delete local files */
    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox) && button == 1)
	libbalsa_mailbox_local_remove_files(LIBBALSA_MAILBOX_LOCAL(
             mailbox));

    /* Remove the node from balsa's mailbox list */
    if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	balsa_app.inbox_input = g_list_remove(balsa_app.inbox_input, 
					      mbnode);
    } else {
        /* FIXME: shouldn't we get an exclusive lock on the mailbox
         * nodes, and hold it until after we've unlinked the gnode ? */
        balsa_mailbox_nodes_lock(FALSE);
	gnode = balsa_find_mailbox(balsa_app.mailbox_nodes, mailbox);
        balsa_mailbox_nodes_unlock(FALSE);
	if (!gnode) {
	    fprintf(stderr,
		    _("Oooop! mailbox not found in balsa_app.mailbox "
		      "nodes?\n"));
	    return;
	} else {
            balsa_mblist_remove_mailbox_node(balsa_app.mblist_tree_store,
                                             mbnode);
	    g_node_unlink(gnode);
	    g_node_destroy(gnode); /* this will remove mbnode */
 	}
    }
    update_mail_servers();
    gtk_object_destroy(GTK_OBJECT(mbnode));
}

static void
conf_response_cb(GtkDialog* dialog, gint response, gpointer data)
{
    MailboxConfWindow *mcw =(MailboxConfWindow*)data;
    switch(response) {
    case GTK_RESPONSE_OK: mcw->ok_handler(mcw); 
        /* fall through */
    default:
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void
free_data(GtkWidget* w, gpointer data)
{
    g_free(data);
}

#define BALSA_OK_SENSITIVE_KEY "balsa-ok-sensitive"

static void
run_mailbox_conf(BalsaMailboxNode* mbnode, GtkType mailbox_type, 
		 void (*ok_handler)(MailboxConfWindow*), 
		 const char* ok_button_name,
		 const char* ok_pixmap)
{
    MailboxConfWindow* mcw;
    GtkWidget* page;

    g_return_if_fail(g_type_is_a(mailbox_type, LIBBALSA_TYPE_MAILBOX));

    mcw = g_new0(MailboxConfWindow, 1);

    mcw->ok_handler = ok_handler;
    mcw->mailbox = mbnode ? mbnode->mailbox : NULL;
    mcw->mailbox_type = mailbox_type;

    mcw->window = 
        GTK_DIALOG(gtk_dialog_new_with_buttons
                   (_("Mailbox Configurator"),
                    GTK_WINDOW(balsa_app.main_window),
                    GTK_DIALOG_DESTROY_WITH_PARENT,
                    ok_button_name, GTK_RESPONSE_OK,
                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                    NULL));
    gtk_window_set_wmclass(GTK_WINDOW(mcw->window), 
                           "mailbox_config_dialog", "Balsa");

    
    gtk_dialog_set_response_sensitive(mcw->window, GTK_RESPONSE_OK, FALSE);
    g_object_set_data(G_OBJECT(mcw->window), BALSA_OK_SENSITIVE_KEY,
                      GINT_TO_POINTER(FALSE));
    gtk_dialog_set_default_response(mcw->window, GTK_RESPONSE_OK);

    page = create_page(mcw);
    gtk_widget_show_all(page);    

    gtk_box_pack_start(GTK_BOX(mcw->window->vbox),
		       page, TRUE, TRUE, 0);

    if(mbnode)
        mailbox_conf_set_values(mcw);

    g_signal_connect(G_OBJECT(mcw->window), "response", 
                     G_CALLBACK(conf_response_cb), mcw);
    g_signal_connect(G_OBJECT(mcw->window), "destroy", 
                     G_CALLBACK(free_data), mcw);
    gtk_widget_show_all(GTK_WIDGET(mcw->window));
}
/*
 * Brings up dialog to configure a new mailbox of type mailbox_type.
 * If the used clicks save add the new mailbox to the tree.
 */
void
mailbox_conf_new(GtkType mailbox_type)
{
    run_mailbox_conf(NULL, mailbox_type,
		     mailbox_conf_add, _("_Add"), GTK_STOCK_NEW);
}

/*
 * Edit an existing mailboxes properties
 */
void
mailbox_conf_edit(BalsaMailboxNode *mbnode)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));
    run_mailbox_conf(mbnode, G_OBJECT_TYPE(G_OBJECT(mbnode->mailbox)),
		     mailbox_conf_update, _("_Update"),
		     GTK_STOCK_SAVE);
}

/*
 * Initialise the dialogs fields from mcw->mailbox
 */
static void
mailbox_conf_set_values(MailboxConfWindow *mcw)
{
    LibBalsaMailbox * mailbox;

    mailbox = mcw->mailbox;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (mcw->mailbox_name && mailbox->name)
	gtk_entry_set_text(GTK_ENTRY(mcw->mailbox_name),
			   mailbox->name);

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);

	if (mailbox->url)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.local.path),
			       libbalsa_mailbox_local_get_path(local));
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	LibBalsaMailboxPop3 *pop3;
	LibBalsaServer *server;

	pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
	server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);


	if (server->host)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.server), 
                               server->host);
	if (server->user)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.username), 
                               server->user);
	if (server->passwd)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.password),
			       server->passwd);
#ifdef USE_SSL
#ifdef USE_SSL_FOR_POP3_IF_WE_EVER_DECIDE_WE_NEED_TO
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.use_ssl),
				     server->use_ssl);
#endif
#endif

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

    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	LibBalsaMailboxImap *imap;
	LibBalsaServer *server;

	imap = LIBBALSA_MAILBOX_IMAP(mailbox);
	server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

	if (server->host)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.server), 
                               server->host);
	if (server->user)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.username),
			       server->user);
	gtk_toggle_button_set_active
            (GTK_TOGGLE_BUTTON(mcw->mb_data.imap.remember),
	     server->remember_passwd);
	if (server->passwd)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.password),
			       server->passwd);
	if (imap->path)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.folderpath),
			       imap->path);
#ifdef USE_SSL
	gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON(mcw->mb_data.imap.use_ssl),
	     server->use_ssl);
#endif
        if(!server->remember_passwd)
            gtk_widget_set_sensitive(GTK_WIDGET(mcw->mb_data.imap.password),
                                     FALSE);
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

    if (mcw->mailbox_name &&!*gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name)))
	sensitive = FALSE;
    else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL) ) {
	if (!*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.local.path)))
	    sensitive = FALSE;
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_IMAP ) ) {
	if (!strcmp
	    (gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.folderpath)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.server)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.username)), ""))
	    sensitive = FALSE;
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3) ) {
	if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.username)), ""))
	    sensitive = FALSE;
	else if (!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.server)), ""))
	    sensitive = FALSE;
    }

    gtk_dialog_set_response_sensitive(mcw->window, GTK_RESPONSE_OK, sensitive);
    g_object_set_data(G_OBJECT(mcw->window), BALSA_OK_SENSITIVE_KEY,
                      GINT_TO_POINTER(sensitive));
}

/*
 * Update an IMAP mailbox with details from the dialog
 */
static void
fill_in_imap_data(MailboxConfWindow *mcw, gchar ** name, gchar ** path)
{
    *path =
	gtk_editable_get_chars(GTK_EDITABLE(mcw->mb_data.imap.folderpath),
                               0, -1);

    if (mcw->mailbox_name && (!(*name =
	  g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name))))
	|| *(g_strstrip(*name)) == '\0')) {
	if (*name)
	    g_free(*name);

	*name = g_strdup_printf(_("%s on %s"), *path,
				gtk_entry_get_text(GTK_ENTRY
						   (mcw->mb_data.imap.server)));
    }
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
						(mcw->mb_data.pop3.server))
#ifdef USE_SSL
#ifdef USE_SSL_FOR_POP3_IF_WE_EVER_DECIDE_WE_NEED_TO
			     , gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.use_ssl))
#else
			     , FALSE
#endif
#endif
);
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
    LibBalsaServer* server;

    mailbox = LIBBALSA_MAILBOX_IMAP(mcw->mailbox);
    server  = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    g_free(LIBBALSA_MAILBOX(mailbox)->name);
    fill_in_imap_data(mcw, &LIBBALSA_MAILBOX(mailbox)->name, &path);
    libbalsa_server_set_username(server,
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.imap.username)));
    server->remember_passwd = 
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->
                                                       mb_data.imap.remember));
    libbalsa_server_set_password(server,
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.imap.password)));
    libbalsa_server_set_host(server,
			     gtk_entry_get_text(GTK_ENTRY
						(mcw->mb_data.imap.server))
#ifdef USE_SSL
			     , gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.imap.use_ssl))
#endif
);

    g_signal_connect(G_OBJECT(server), "get-password",
                     G_CALLBACK(ask_password), mailbox);

    libbalsa_mailbox_imap_set_path(mailbox,
				   (path == NULL || path[0] == '\0') 
				   ? "INBOX" : path);
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

    mailbox = mcw->mailbox;

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	const gchar *filename, *name;

	filename =
	    gtk_entry_get_text(GTK_ENTRY((mcw->mb_data.local.path)));
	/* rename */
	if (
	    (i =
	     libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL
					     (mailbox), filename)) != 0) {
	    balsa_information(LIBBALSA_INFORMATION_WARNING,
			      _("Rename of %s to %s failed:\n%s"),
			      libbalsa_mailbox_local_get_path(mailbox),
			      filename, strerror(i));
	    return;
	}
        if(mcw->mailbox_name) {
	    name = gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name));
            g_free(mailbox->name);
            mailbox->name = g_strdup(name);
        } else { /* shortcut: this will destroy mailbox */
            balsa_mailbox_local_rescan_parent(mailbox); 
            balsa_mblist_repopulate(balsa_app.mblist_tree_store);
            return;
        }
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	update_pop_mailbox(mcw);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	update_imap_mailbox(mcw);
    }

    if (mailbox->config_prefix)
	config_mailbox_update(mailbox);

    if (LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox))
	/* redraw the pop3 server list */
	update_mail_servers();
    else /* redraw the main mailbox list */
	balsa_mblist_repopulate(balsa_app.mblist_tree_store);
}

/*
 * Add a new mailbox, based on the contents of the dialog.
 */
static void
mailbox_conf_add(MailboxConfWindow *mcw)
{
    GNode *node;
    BalsaMailboxNode * mbnode;
    gboolean save_to_config = TRUE;

    mcw->mailbox = g_object_new(mcw->mailbox_type, NULL);

    mbnode = balsa_mailbox_node_new_from_mailbox(mcw->mailbox);
    if ( LIBBALSA_IS_MAILBOX_LOCAL(mcw->mailbox) ) {
	const gchar *path;

	path = gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.local.path));
	if(libbalsa_mailbox_local_set_path(
					   LIBBALSA_MAILBOX_LOCAL(mcw->mailbox), path) != 0) {
	    g_object_unref(G_OBJECT(mcw->mailbox));
	    mcw->mailbox = NULL;
	    return;
	}

	save_to_config = balsa_app.local_mail_directory == NULL
	    || strncmp(balsa_app.local_mail_directory, path,
		       strlen(balsa_app.local_mail_directory)) != 0;

	if(save_to_config) {
	    node =g_node_new(mbnode);
            balsa_mailbox_nodes_lock(TRUE);
	    g_node_append(balsa_app.mailbox_nodes, node);
            balsa_mailbox_nodes_unlock(TRUE);
	}
    } else if ( LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox) ) {
	/* POP3 Mailboxes */
	update_pop_mailbox(mcw);
	balsa_app.inbox_input =
	    g_list_append(balsa_app.inbox_input, mbnode);
    } else if ( LIBBALSA_IS_MAILBOX_IMAP(mcw->mailbox) ) {
	update_imap_mailbox(mcw);

	node = g_node_new(mbnode);
        balsa_mailbox_nodes_lock(TRUE);
	g_node_append(balsa_app.mailbox_nodes, node);
        balsa_mailbox_nodes_unlock(TRUE);
	update_mail_servers();
    } else {
	g_assert_not_reached();
    }

    if(save_to_config)
	config_mailbox_add(mcw->mailbox, NULL);
    else 
        balsa_mailbox_local_rescan_parent(mcw->mailbox);

    if (LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox))
	/* redraw the pop3 server list */
	update_mail_servers();
    else /* redraw the main mailbox list */
	balsa_mblist_repopulate(balsa_app.mblist_tree_store);
}

/* Create a page for the type of mailbox... */
static GtkWidget *
create_page(MailboxConfWindow *mcw)
{
    if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL) ) {
	return create_local_mailbox_page(mcw);
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3) ) {
	return create_pop_mailbox_page(mcw);
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_IMAP) ) {
	return create_imap_mailbox_page(mcw);
    } else {
	g_warning("Unknown mailbox type: %s\n",
                  g_type_name(mcw->mailbox_type));
	return NULL;
    }
}

static GtkWidget *
create_local_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *table;
    GtkWidget *file, *label;
    table = gtk_table_new(2, 2, FALSE);

    /* mailbox name */
    if(mcw->mailbox && BALSA_IS_MAILBOX_SPECIAL(mcw->mailbox)) {
        label = create_label(_("Mailbox _Name:"), table, 0);
        mcw->mailbox_name = 
            create_entry(mcw->window, table,
                         GTK_SIGNAL_FUNC(check_for_blank_fields),
                         mcw, 0, NULL, label);
    } else mcw->mailbox_name = NULL;
    /* path to file */
    label = create_label(_("Mailbox _Path:"), table, 1);

    file = gnome_file_entry_new("MailboxPath", _("Mailbox Path"));
    mcw->mb_data.local.path =
	gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(file));

    gtk_label_set_mnemonic_widget(GTK_LABEL(label), file);
    gnome_file_entry_set_default_path(GNOME_FILE_ENTRY(file),
                                      balsa_app.local_mail_directory);

    g_signal_connect_swapped(G_OBJECT(mcw->mb_data.local.path),
                             "activate",
                             G_CALLBACK(gtk_window_activate_default),
                             mcw->window);
    g_signal_connect(G_OBJECT(mcw->mb_data.local.path), "changed",
		     G_CALLBACK(check_for_blank_fields), mcw);

    gtk_table_attach(GTK_TABLE(table), file, 1, 2, 1, 2,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    gtk_widget_grab_focus(mcw->mailbox_name ? 
                          mcw->mailbox_name : mcw->mb_data.local.path);
    return table;
}

static GtkWidget *
create_pop_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *table, *label;

    table = gtk_table_new(9, 2, FALSE);

    /* mailbox name */
    label = create_label(_("Mailbox _Name:"), table, 0);
    mcw->mailbox_name = create_entry(mcw->window, table,
				     GTK_SIGNAL_FUNC(check_for_blank_fields),
				     mcw, 0, NULL, label);

    /* pop server */
    label = create_label(_("_Server:"), table, 1);
    mcw->mb_data.pop3.server = create_entry(mcw->window, table,
				     GTK_SIGNAL_FUNC(check_for_blank_fields),
				     mcw, 1, "localhost:110", label);


    /* username  */
    label= create_label(_("_Username:"), table, 3);
    mcw->mb_data.pop3.username = create_entry(mcw->window, table,
				     GTK_SIGNAL_FUNC(check_for_blank_fields),
				     mcw, 3, g_get_user_name(), label);

    /* password field */
    label = create_label(_("Pass_word:"), table, 4);
    mcw->mb_data.pop3.password = create_entry(mcw->window, table, 
					      NULL, NULL, 4, NULL, label);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.pop3.password), FALSE);

    /* toggle for apop */
    mcw->mb_data.pop3.use_apop = 
	create_check(mcw->window, _("Use _APOP Authentication"), table, 5,
		     FALSE);

    /* toggle for deletion from server */
    mcw->mb_data.pop3.delete_from_server = 
	create_check(mcw->window, 
		     _("_Delete messages from server after download"),
		     table, 6, TRUE);

    /* Procmail */
    mcw->mb_data.pop3.filter = 
	create_check(mcw->window, _("_Filter messages through procmail"),
		     table, 7, FALSE);

    /* toggle for check */
    mcw->mb_data.pop3.check = 
	create_check(mcw->window, _("_Enable check for new mail"), 
		     table, 8, TRUE);

#ifdef USE_SSL_FOR_POP3_IF_WE_EVER_DECIDE_WE_NEED_TO
    /*
     * chbm: we don't do pop3s. i did all the necessary config stuff 
     * and then realized libbalsa/pop3.c can't use libmutt/ stuff
     */
    /* toggle for ssl */
    mcw->mb_data.pop3.use_ssl = 
	create_check(mcw->window, _("_Use SSL (pop3s)"), 
		     table, 9, FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.pop3.use_ssl), "toggled", 
                     G_CALLBACK(pop3_use_ssl_cb), mcw);
#endif

    gtk_widget_grab_focus(mcw->mailbox_name);
    return table;
}

static void
remember_toggle_cb(GtkToggleButton *remember_button, MailboxConfWindow *mcw)
{
    gtk_widget_set_sensitive(GTK_WIDGET(mcw->mb_data.imap.password),
                             gtk_toggle_button_get_active(remember_button));
}

static void
entry_activated(GtkEntry * entry, gpointer data)
{
    MailboxConfWindow *mcw = data;

    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mcw->window),
                                          BALSA_OK_SENSITIVE_KEY)))
        gtk_dialog_response(GTK_DIALOG(mcw->window), GTK_RESPONSE_OK);
}

static GtkWidget *
create_imap_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *table, *label;
    GtkWidget *entry;

    table = gtk_table_new(7, 2, FALSE);

    /* mailbox name */
    label = create_label(_("Mailbox _Name:"), table, 0);
    mcw->mailbox_name =  create_entry(mcw->window, table,
                                      GTK_SIGNAL_FUNC(check_for_blank_fields),
                                      mcw, 0, NULL, label);

    /* imap server */
    label = create_label(_("_Server:"), table, 1);
    mcw->mb_data.imap.server = 
	create_entry(mcw->window, table,
		     GTK_SIGNAL_FUNC(check_for_blank_fields),
		     mcw, 1, "localhost", label);

    /* username  */
    label = create_label(_("_Username:"), table, 3);
    mcw->mb_data.imap.username = 
	create_entry(mcw->window, table,
		     GTK_SIGNAL_FUNC(check_for_blank_fields),
		     mcw, 3, g_get_user_name(), label);

    /* toggle for remember password */
    mcw->mb_data.imap.remember = 
	create_check(mcw->window, _("_Remember Password"), table, 4,
		     FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.remember), "toggled",
                     G_CALLBACK(remember_toggle_cb), mcw);

   /* password field */
    label = create_label(_("Pass_word:"), table, 5);
    mcw->mb_data.imap.password = 
	create_entry(mcw->window, table, NULL, NULL, 5, NULL, label);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.imap.password), FALSE);

    label = create_label(_("F_older Path:"), table, 6);

    entry = gnome_entry_new("IMAPFolderHistory");
    mcw->mb_data.imap.folderpath = gnome_entry_gtk_entry(GNOME_ENTRY(entry));
    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.folderpath), "INBOX");

    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  mcw->mb_data.imap.folderpath);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.folderpath), "activate",
                     G_CALLBACK(entry_activated), mcw);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.folderpath), "changed",
                     G_CALLBACK(check_for_blank_fields), mcw);

    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX.Sent");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX.Draft");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX.outbox");
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 6, 7,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

#ifdef USE_SSL
    /* toggle for SSL */
    mcw->mb_data.imap.use_ssl = 
        create_check(mcw->window, _("Use SSL (imaps)"), table, 7, FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.use_ssl), "toggled", 
                     G_CALLBACK(imap_use_ssl_cb), mcw);
#endif

    gtk_widget_grab_focus(mcw->mailbox_name? 
                          mcw->mailbox_name : mcw->mb_data.imap.server);
    return table;
}

