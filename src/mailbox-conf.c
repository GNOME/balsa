/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
   https://mail.gnome.org/archives/balsa-list/2002-June/msg00044.html

   The mailbox_name field is displayed only for special mailboxes
   and POP3 mailboxes.
*/
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "mailbox-conf.h"

#include <gtk/gtk.h>
#include <string.h>

#include "balsa-app.h"
#include "balsa-mblist.h"
#include "mailbox-node.h"
#include "pref-manager.h"
#include "save-restore.h"
#include "server-config.h"

#include "libbalsa.h"
#include "imap-server.h"
#include "mailbox-filter.h"
#include "libbalsa-conf.h"
#include <glib/gi18n.h>

struct _BalsaMailboxConfView {
    GtkWidget *identity_combo_box;
    GtkWidget *show_to;
    GtkWidget *subscribe;
    GtkWidget *chk_crypt;
    GtkWidget *thread_messages;
    GtkWidget *subject_gather;
};

typedef struct _MailboxConfWindow MailboxConfWindow;

struct _MailboxConfWindow {
    LibBalsaMailbox *mailbox;

    GtkDialog *window;

    void (*ok_handler)(MailboxConfWindow*);
    const gchar *ok_button_name;
    GType mailbox_type;
    BalsaMailboxConfView *view_info;

    union {
	/* for local mailboxes */
	struct {
	    GtkWidget *mailbox_name;
	} local;

	/* for pop3 mailboxes */
	struct {
		LibBalsaServerCfg *server_cfg;
	    GtkWidget *check;
	    GtkWidget *delete_from_server;
	    GtkWidget *disable_apop;
	    GtkWidget *enable_pipe;
	    GtkWidget *filter;
	    GtkWidget *filter_cmd;
	} pop3;
    } mb_data;
};

static void mailbox_conf_update(MailboxConfWindow *conf_window);
static void mailbox_conf_add(MailboxConfWindow *conf_window);

static void update_pop_mailbox(MailboxConfWindow *mcw);
static BalsaMailboxConfView *
    mailbox_conf_view_new_full(LibBalsaMailbox * mailbox,
                               GtkWindow * window,
                               GtkWidget * grid, gint row,
                               GtkSizeGroup * size_group,
                               MailboxConfWindow * mcw,
                               GCallback callback);

/* pages */
static GtkWidget *create_dialog(MailboxConfWindow *mcw);
static GtkWidget *create_local_mailbox_dialog(MailboxConfWindow *mcw);
static GtkWidget *create_pop_mailbox_dialog(MailboxConfWindow *mcw);

static void check_for_blank_fields(GtkWidget *widget, MailboxConfWindow *mcw);


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
mailbox_conf_delete_cb(GtkWidget * widget, gpointer data)
{
    BalsaMailboxNode *mbnode =
        balsa_mblist_get_selected_node(balsa_app.mblist);

    if (balsa_mailbox_node_get_mailbox(mbnode) == NULL)
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                           _("No mailbox selected."));
    else
	mailbox_conf_delete(mbnode);
    g_object_unref(mbnode);
}

/* This can be used  for both mailbox and folder edition */
void
mailbox_conf_edit_cb(GtkWidget * widget, gpointer data)
{
    BalsaMailboxNode *mbnode = 
        balsa_mblist_get_selected_node(balsa_app.mblist);
    if (mbnode) {
        balsa_mailbox_node_show_prop_dialog(mbnode);
        g_object_unref(mbnode);
    }
}

/* END OF COMMONLY USED CALLBACKS SECTION ------------------------ */
void
mailbox_conf_delete(BalsaMailboxNode * mbnode)
{
    gint button;
    GtkWidget *ask;
    LibBalsaMailbox* mailbox = balsa_mailbox_node_get_mailbox(mbnode);
    gchar *url, *group;

    if(BALSA_IS_MAILBOX_SPECIAL(mailbox)) {
	balsa_information(
	    LIBBALSA_INFORMATION_ERROR,
	    _("Mailbox “%s” is used by Balsa and I cannot remove it.\n"
	      "If you really want to remove it, assign its function\n"
	      "to some other mailbox."), libbalsa_mailbox_get_name(mailbox));
	return;
    }

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
        ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_NONE,
                                     _("This will remove the mailbox "
                                       "“%s” from the list "
                                       "of mailboxes. "
                                       "You may also delete the disk "
                                       "file or files associated with "
                                       "this mailbox.\n"
                                       "If you do not remove the file "
                                       "on disk you may “Add Mailbox” "
                                       "to access the mailbox again.\n"
                                       "What would you like to do?"),
                                     libbalsa_mailbox_get_name(mailbox));
        gtk_dialog_add_buttons(GTK_DIALOG(ask),
                               _("Remove from _list"), 0,
                               _("Remove from list and _disk"), 1,
                               _("_Cancel"), GTK_RESPONSE_CANCEL,
                               NULL);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) &&
               libbalsa_mailbox_get_config_prefix(mailbox) == NULL) {
	/* deleting remote IMAP mailbox in a folder set */
        ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_NONE,
	                             _("This will remove the mailbox "
                                       "“%s” and all its messages "
                                       "from your IMAP server. "
	                               "If %s has subfolders, it will "
                                       "still appear as a node in the "
                                       "folder tree.\n"
	                               "You may use "
                                       "“New IMAP subfolder” "
                                       "later to add a mailbox "
                                       "with this name.\n"
	                               "What would you like to do?"),
			             libbalsa_mailbox_get_name(mailbox), libbalsa_mailbox_get_name(mailbox));
        gtk_dialog_add_buttons(GTK_DIALOG(ask),
                               _("_Remove from server"), 0,
                               _("_Cancel"), GTK_RESPONSE_CANCEL,
                               NULL);
    } else { /* deleting other remote mailbox */
        ask = gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window), 0,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_NONE,
	                             _("This will remove the mailbox "
                                       "“%s” from the list "
                                       "of mailboxes.\n"
				       "You may use “Add Mailbox” "
                                       "later to access "
                                       "this mailbox again.\n"
			 	       "What would you like to do?"),
			             libbalsa_mailbox_get_name(mailbox));
        gtk_dialog_add_buttons(GTK_DIALOG(ask),
                               _("_Remove from list"), 0,
                               _("_Cancel"), GTK_RESPONSE_CANCEL,
                               NULL);
    }
    
    button = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);

    /* button < 0 means that the dialog window was closed without pressing
       any button other than CANCEL.
    */
    if ( button < 0)
	return;

    /* Save the mailbox URL */
    url = g_strdup(libbalsa_mailbox_get_url(mailbox) ? libbalsa_mailbox_get_url(mailbox) : libbalsa_mailbox_get_name(mailbox));

    /* Delete it from the config file and internal nodes */
    config_mailbox_delete(mailbox);

    /* Close the mailbox, in case it was open */
    if (!LIBBALSA_IS_MAILBOX_POP3(mailbox))
	balsa_mblist_close_mailbox(mailbox);

    /* Remove mailbox on IMAP server */
    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) &&
        libbalsa_mailbox_get_config_prefix(mailbox) == NULL) {
        GError *err = NULL;
	BalsaMailboxNode *parent = balsa_mailbox_node_get_parent(mbnode);
        if(libbalsa_imap_delete_folder(LIBBALSA_MAILBOX_IMAP(mailbox),
                                       &err)) {
            /* a chain of folders might go away, so we'd better rescan from
             * higher up
             */
            while (balsa_mailbox_node_get_mailbox(parent) == NULL &&
                   balsa_mailbox_node_get_parent(parent) != NULL) {
                mbnode = parent;
                parent = balsa_mailbox_node_get_parent(parent);
            }
            balsa_mblist_mailbox_node_remove(mbnode);
            balsa_mailbox_node_rescan(parent); /* see it as server sees it */
        } else {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Folder deletion failed. Reason: %s"),
                              err ? err->message : "unknown");
            g_clear_error(&err);
            g_free(url);
        }
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
    } else
	balsa_mblist_mailbox_node_remove(mbnode);
    update_mail_servers();

    /* Clean up filters */
    group = mailbox_filters_section_lookup(url);
    if (group) {
        libbalsa_conf_remove_group(group);
        g_free(group);
    }

    /* Remove view */
    config_view_remove(url);

    g_free(url);
}

#define MCW_RESPONSE 1
#define BALSA_MAILBOX_CONF_DIALOG "balsa-mailbox-conf-dialog"
static void
conf_response_cb(GtkDialog* dialog, gint response, MailboxConfWindow * mcw)
{
    switch(response) {
    case MCW_RESPONSE:
    	/* add or update */
    	mcw->ok_handler(mcw);
        g_object_set_data(G_OBJECT(mcw->mailbox), BALSA_MAILBOX_CONF_DIALOG, NULL);
        break;
    default:
    	/* everything else */
    	if (mcw->ok_handler == mailbox_conf_add) {
    		/* add: destroy intermediate mailbox */
    		g_object_unref(mcw->mailbox);
    		mcw->mailbox = NULL;
    	} else {
            g_object_set_data(G_OBJECT(mcw->mailbox), BALSA_MAILBOX_CONF_DIALOG, NULL);
    	}
        break;
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static GtkWidget *
run_mailbox_conf(BalsaMailboxNode* mbnode, GType mailbox_type, 
		 gboolean update)
{
    MailboxConfWindow* mcw;

    g_return_val_if_fail(g_type_is_a(mailbox_type, LIBBALSA_TYPE_MAILBOX),
                         NULL);

    mcw = g_new0(MailboxConfWindow, 1);

    if (update) {
        mcw->ok_handler = mailbox_conf_update;
        mcw->ok_button_name = _("_Apply");
        mcw->mailbox = balsa_mailbox_node_get_mailbox(mbnode);
    } else {
        mcw->ok_handler = mailbox_conf_add;
        mcw->ok_button_name = _("_Add");
        mcw->mailbox = g_object_new(mailbox_type, NULL);
    }
    mcw->mailbox_type = mailbox_type;

    mcw->window = GTK_DIALOG(create_dialog(mcw));
    g_object_weak_ref(G_OBJECT(mcw->window), (GWeakNotify) g_free, mcw);
    
    if (!g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3)) {
    	gtk_dialog_set_response_sensitive(mcw->window, MCW_RESPONSE, FALSE);
    }
    gtk_dialog_set_default_response(mcw->window,
                                    update ? GTK_RESPONSE_CANCEL :
                                    MCW_RESPONSE);

    g_signal_connect(mcw->window, "response", 
                     G_CALLBACK(conf_response_cb), mcw);
    gtk_widget_show_all(GTK_WIDGET(mcw->window));

    return GTK_WIDGET(mcw->window);
}
/*
 * Brings up dialog to configure a new mailbox of type mailbox_type.
 * If the used clicks save add the new mailbox to the tree.
 */
void
mailbox_conf_new(GType mailbox_type)
{
    static GtkWidget *dialog;

    if (dialog) {
        gtk_window_present_with_time(GTK_WINDOW(dialog),
                                     gtk_get_current_event_time());
        return;
    }

    dialog = run_mailbox_conf(NULL, mailbox_type, FALSE);
    g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer *) &dialog);
}

/*
 * Edit an existing mailboxes properties
 */
void
mailbox_conf_edit(BalsaMailboxNode * mbnode)
{
    GtkWidget *dialog;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(balsa_mailbox_node_get_mailbox(mbnode)));

    dialog = g_object_get_data(G_OBJECT(balsa_mailbox_node_get_mailbox(mbnode)),
                               BALSA_MAILBOX_CONF_DIALOG);
    if (dialog) {
        gtk_window_present_with_time(GTK_WINDOW(dialog),
                                     gtk_get_current_event_time());
        return;
    }

    dialog =
        run_mailbox_conf(mbnode, G_OBJECT_TYPE(G_OBJECT(balsa_mailbox_node_get_mailbox(mbnode))),
                         TRUE);
    g_object_set_data(G_OBJECT(balsa_mailbox_node_get_mailbox(mbnode)), BALSA_MAILBOX_CONF_DIALOG,
                      dialog);
}


/*
 * Checks for blank fields in the dialog.
 * Sets the sensitivity of the Update/Add button accordingly.
 * This function should be attached to a change event signal 
 * on any widget which can affect the validity of the input.
 */
static void
check_for_blank_fields(GtkWidget G_GNUC_UNUSED *widget,
					   MailboxConfWindow       *mcw)
{
    gboolean sensitive;

    if ((mcw == NULL) || (mcw->window == NULL)) {
        return;
    }

    if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3)) {
    	gboolean enable_filter;

    	sensitive = libbalsa_server_cfg_valid(mcw->mb_data.pop3.server_cfg);

    	/* procmail filter */
    	enable_filter = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.filter));
    	gtk_widget_set_sensitive(mcw->mb_data.pop3.filter_cmd, enable_filter);
    	if (enable_filter) {
    		sensitive = sensitive && (*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.filter_cmd)) != '\0');
    	}
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL)) {
    	sensitive = TRUE;

        if ((mcw->mb_data.local.mailbox_name != NULL) &&
        	(*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.local.mailbox_name)) == '\0')) {
        	sensitive = FALSE;
        } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL)) {
        	gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(mcw->window));
        	if (filename != NULL) {
        		g_free(filename);
        	} else {
        		sensitive = FALSE;
        	}
        }
    } else {
    	g_assert_not_reached();
    }

    gtk_dialog_set_response_sensitive(mcw->window, MCW_RESPONSE, sensitive);
    gtk_dialog_set_default_response(mcw->window, sensitive ? MCW_RESPONSE : GTK_RESPONSE_CANCEL);
}

/*
 * Update a pop3 mailbox with details from the dialog
 */
static void
update_pop_mailbox(MailboxConfWindow *mcw)
{
	LibBalsaMailboxPOP3 *mailbox_pop3;
	LibBalsaMailbox *mailbox;
	LibBalsaServer *server;
        gchar *name;

	mailbox_pop3 = LIBBALSA_MAILBOX_POP3(mcw->mailbox);
	server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox_pop3);
        mailbox = (LibBalsaMailbox *) mailbox_pop3;

	/* basic data */
	name = g_strdup(libbalsa_server_cfg_get_name(mcw->mb_data.pop3.server_cfg));
        libbalsa_mailbox_set_name(mailbox, name);
        g_free(name);

	libbalsa_server_cfg_assign_server(mcw->mb_data.pop3.server_cfg, server);
	libbalsa_server_config_changed(server);

	libbalsa_mailbox_pop3_set_check(mailbox_pop3, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.check)));
	libbalsa_mailbox_pop3_set_delete_from_server(mailbox_pop3, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (mcw->mb_data.pop3.delete_from_server)));
	libbalsa_mailbox_pop3_set_filter(mailbox_pop3, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.filter)));
	libbalsa_mailbox_pop3_set_filter_cmd(mailbox_pop3, gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.filter_cmd)));

	/* advanced settings */
	libbalsa_mailbox_pop3_set_disable_apop(mailbox_pop3, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.disable_apop)));
	libbalsa_mailbox_pop3_set_enable_pipe(mailbox_pop3, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.enable_pipe)));
}

/* conf_update_mailbox:
   if changing path of the local mailbox in the local mail directory, just 
   rename the file, don't insert it to the configuration.
   FIXME: make sure that the rename breaks nothing. 
*/
static void
mailbox_conf_update(MailboxConfWindow *mcw)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mcw->mailbox);

    mailbox_conf_view_check(mcw->view_info, mailbox);

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	BalsaMailboxNode *mbnode;
	gchar *filename;
	gchar *path;
	gchar *name;

	mbnode = balsa_find_mailbox(mailbox);
        filename =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(mcw->window));
	path =
            g_strdup(libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox));
        if (strcmp(filename, path) != 0) {
            /* rename */
            int i;
	    gchar *file_dir, *path_dir;

            i = libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL
                                                (mailbox), filename, FALSE);
            if (i != 0) {
                balsa_information(LIBBALSA_INFORMATION_WARNING,
                                  _("Rename of %s to %s failed:\n%s"),
                                  path, filename, g_strerror(i));
                g_free(filename);
		g_free(path);
                return;
            }

	    file_dir = g_path_get_dirname(filename);
	    path_dir = g_path_get_dirname(path);
            if (strcmp(file_dir, path_dir)) {
		/* Actual move. */
		balsa_mblist_mailbox_node_remove(mbnode);
		g_object_ref(mailbox);
		g_object_unref(mbnode);
		balsa_mailbox_local_append(mailbox);

		/* We might have moved a subtree. */
		mbnode = balsa_find_mailbox(mailbox);
		balsa_mailbox_node_rescan(mbnode);
            } 

            g_free(file_dir);
            g_free(path_dir);
	}

        name = mcw->mb_data.local.mailbox_name ?
            gtk_editable_get_chars(GTK_EDITABLE(mcw->mb_data.local.mailbox_name), 0, -1)
            : g_path_get_basename(filename);
	if (strcmp(name, libbalsa_mailbox_get_name(mailbox)) != 0) {
	    /* Change name. */
	    libbalsa_mailbox_set_name(mailbox, name);
	    balsa_mblist_mailbox_node_redraw(mbnode);
	}
        g_free(name);

	g_object_unref(mbnode);
        g_free(filename);
	g_free(path);
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	update_pop_mailbox(mcw);
    } else {
    	g_assert_not_reached();
    }

    if (libbalsa_mailbox_get_config_prefix(mailbox) != NULL)
	config_mailbox_update(mailbox);

    if (LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox))
	/* redraw the pop3 server list */
	update_mail_servers();
}

/*
 * Add a new mailbox, based on the contents of the dialog.
 */
static void
mailbox_conf_add(MailboxConfWindow * mcw)
{
    BalsaMailboxNode *mbnode;
    gboolean save_to_config = TRUE;

    mailbox_conf_view_check(mcw->view_info, mcw->mailbox);

    if ( LIBBALSA_IS_MAILBOX_LOCAL(mcw->mailbox) ) {
	LibBalsaMailboxLocal *ml  = LIBBALSA_MAILBOX_LOCAL(mcw->mailbox);
	gchar *path;
	gchar *basename;

        path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(mcw->window));

        if (libbalsa_mailbox_local_set_path(ml, path, TRUE) != 0) {
            g_free(path);
	    g_object_unref(mcw->mailbox);
	    mcw->mailbox = NULL;
	    return;
	}

	save_to_config =
            !libbalsa_path_is_below_dir(path,
                                        balsa_app.local_mail_directory);
        g_debug("Save to config: %d", save_to_config);
        basename = g_path_get_basename(path);
        g_free(path);

        libbalsa_mailbox_set_name(mcw->mailbox, basename);
        g_free(basename);

	balsa_mailbox_local_append(mcw->mailbox);
    }
    mbnode = balsa_mailbox_node_new_from_mailbox(mcw->mailbox);
    if ( LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox) ) {
	/* POP3 Mailboxes */
	update_pop_mailbox(mcw);
	balsa_app.inbox_input =
	    g_list_append(balsa_app.inbox_input, mbnode);
    } else if ( LIBBALSA_IS_MAILBOX_IMAP(mcw->mailbox) ) {
    	g_assert_not_reached();
    } else if ( !LIBBALSA_IS_MAILBOX_LOCAL(mcw->mailbox) ) {
	g_assert_not_reached();
    }

    if(save_to_config)
	config_mailbox_add(mcw->mailbox, NULL);

    if (LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox))
	/* redraw the pop3 server list */
	update_mail_servers();
}

/* Create a page for the type of mailbox... */
static GtkWidget *
create_dialog(MailboxConfWindow *mcw)
{
	if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL)) {
		return create_local_mailbox_dialog(mcw);
	} else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3)) {
		return create_pop_mailbox_dialog(mcw);
	} else {
		g_warning("Unknown mailbox type: %s", g_type_name(mcw->mailbox_type));
		return NULL;
	}
}

static void
balsa_get_entry(GtkWidget * widget, GtkWidget ** entry)
{
    if (GTK_IS_ENTRY(widget))
        *entry = widget;
    else if (GTK_IS_CONTAINER(widget))
        gtk_container_foreach((GtkContainer *) widget,
                              (GtkCallback) balsa_get_entry, entry);
}

/*
 * Callback for the file chooser's "selection-changed" signal and its
 * entry's "changed" signal
 *
 * If the path has really changed, call check_for_blank_fields to set
 * the sensitivity of the buttons appropriately.  If it hasn't, this is
 * probably just the file chooser being initialized in an idle callback,
 * so we don't change button sensitivity.
 */
static void
local_mailbox_dialog_cb(GtkWidget         *widget,
						MailboxConfWindow *mcw)
{
    gchar *filename = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(mcw->window));

    if (filename != NULL) {
        gboolean changed = TRUE;

        if (mcw->mailbox != NULL)
            changed = g_strcmp0(filename, libbalsa_mailbox_get_url(mcw->mailbox)) != 0;
        g_free(filename);

        if (changed)
            check_for_blank_fields(widget, mcw);
    }
}

static GtkWidget *
create_local_mailbox_dialog(MailboxConfWindow *mcw)
{
    GtkWidget *dialog;
    GtkWidget *grid;
    gint row = -1;
    GtkFileChooserAction action;
    GtkWidget *entry = NULL;
    GtkSizeGroup *size_group;
    const gchar *type;
    gchar *title;

    grid = libbalsa_create_grid();

    type = g_type_name(mcw->mailbox_type) + 15;
    title = g_strdup_printf(mcw->mailbox ?
                            _("Local %s Mailbox Properties") :
                            _("New Local %s Mailbox"), type);

    action = mcw->mailbox_type == LIBBALSA_TYPE_MAILBOX_MBOX ?
        GTK_FILE_CHOOSER_ACTION_SAVE :
		GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER;
    dialog =
        gtk_file_chooser_dialog_new(title,
                                    GTK_WINDOW(balsa_app.main_window),
                                    action,
                                    mcw->ok_button_name, MCW_RESPONSE,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    NULL);
    g_free(title);

    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), grid);
    if (mcw->mailbox != NULL && libbalsa_mailbox_get_url(mcw->mailbox) != NULL) {
		const gchar *path = libbalsa_mailbox_local_get_path(LIBBALSA_MAILBOX_LOCAL(mcw->mailbox));
		gchar *basename = g_path_get_basename(path);

		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), path);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), basename);
		g_free(basename);
    } else {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), balsa_app.local_mail_directory);
    }
    g_signal_connect(dialog, "selection-changed",
                     G_CALLBACK(local_mailbox_dialog_cb), mcw);

    size_group = libbalsa_create_size_group(dialog);
    if (libbalsa_mailbox_get_config_prefix(mcw->mailbox) != NULL) {
        GtkWidget *label;

        label = libbalsa_create_grid_label(_("_Mailbox Name:"), grid, ++row);
        mcw->mb_data.local.mailbox_name =
            libbalsa_create_grid_entry(grid,
                                       G_CALLBACK(check_for_blank_fields),
                                       mcw, row, libbalsa_mailbox_get_name(mcw->mailbox), label);
        gtk_size_group_add_widget(size_group, label);
    } else {
    	mcw->mb_data.local.mailbox_name = NULL;
    }

    balsa_get_entry(dialog, &entry);
    if (entry)
	g_signal_connect(entry, "changed",
                         G_CALLBACK(local_mailbox_dialog_cb), mcw);

    mcw->view_info =
        mailbox_conf_view_new_full(mcw->mailbox, GTK_WINDOW(dialog), grid,
                                   ++row, size_group, mcw, NULL);

    return dialog;
}

static GtkWidget *
create_pop_mailbox_dialog(MailboxConfWindow *mcw)
{
	LibBalsaMailbox *mailbox = mcw->mailbox;
    LibBalsaMailboxPOP3 *mailbox_pop3 = LIBBALSA_MAILBOX_POP3(mailbox);

	mcw->window = GTK_DIALOG(gtk_dialog_new_with_buttons(_("Remote Mailbox Configurator"),
        GTK_WINDOW(balsa_app.main_window),
		GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
        mcw->ok_button_name, MCW_RESPONSE,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
        NULL));

    mcw->mb_data.pop3.server_cfg =
        libbalsa_server_cfg_new(LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox),
                                libbalsa_mailbox_get_name(mailbox));
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(mcw->window))), GTK_WIDGET(mcw->mb_data.pop3.server_cfg));
    g_signal_connect(mcw->mb_data.pop3.server_cfg, "changed", G_CALLBACK(check_for_blank_fields), mcw);

    /* toggle for deletion from server */
    mcw->mb_data.pop3.delete_from_server = libbalsa_server_cfg_add_check(mcw->mb_data.pop3.server_cfg, TRUE,
    	_("_Delete messages from server after download"), libbalsa_mailbox_pop3_get_delete_from_server(mailbox_pop3), NULL, NULL);

    /* toggle for check */
    mcw->mb_data.pop3.check = libbalsa_server_cfg_add_check(mcw->mb_data.pop3.server_cfg, TRUE, _("_Enable check for new mail"),
    	libbalsa_mailbox_pop3_get_check(mailbox_pop3), NULL, NULL);

    /* Procmail */
    mcw->mb_data.pop3.filter = libbalsa_server_cfg_add_check(mcw->mb_data.pop3.server_cfg, TRUE,
    	_("_Filter messages through procmail"), libbalsa_mailbox_pop3_get_filter(mailbox_pop3), G_CALLBACK(check_for_blank_fields), mcw);
    mcw->mb_data.pop3.filter_cmd = libbalsa_server_cfg_add_entry(mcw->mb_data.pop3.server_cfg, TRUE, _("Fi_lter Command:"),
    	libbalsa_mailbox_pop3_get_filter_cmd(mailbox_pop3), G_CALLBACK(check_for_blank_fields), mcw);

    /* advanced - toggle for apop */
    mcw->mb_data.pop3.disable_apop = libbalsa_server_cfg_add_check(mcw->mb_data.pop3.server_cfg, FALSE, _("Disable _APOP"),
    	libbalsa_mailbox_pop3_get_disable_apop(mailbox_pop3), NULL, NULL);

    /* toggle for enabling pipeling */
    mcw->mb_data.pop3.enable_pipe = libbalsa_server_cfg_add_check(mcw->mb_data.pop3.server_cfg, FALSE, _("Overlap commands"),
    	libbalsa_mailbox_pop3_get_enable_pipe(mailbox_pop3), NULL, NULL);

    /* initially call the check */
    check_for_blank_fields(NULL, mcw);
    gtk_dialog_set_response_sensitive(mcw->window, MCW_RESPONSE, FALSE);

    return GTK_WIDGET(mcw->window);
}

/* Manage the widgets that control aspects of the view, not the config.
 * Currently the mailbox default identity and whether the address column
 * shows the sender or the recipient can be controlled in this way.
 * Other aspects like sort column and sort order are just remembered
 * when the user changes them with the GtkTreeView controls. */

/* Create the dialog items in the dialog's grid, and allocate and
 * populate a BalsaMailboxConfView with the info that needs to be passed
 * around. The memory is deallocated when the window is finalized. 
 *
 * mailbox:     the mailbox whose properties are being displayed;
 * window:      the dialog, which will be the transient parent of the
 *              identity dialog, if needed, and also owns the
 *              BalsaMailboxConfView.
 * grid:       the grid in which to place the widgets;
 * row:         the row of the grid in which to start.
 */
enum {
    IDENTITY_COMBO_BOX_ADDRESS_COLUMN = 0,
    IDENTITY_COMBO_BOX_IDENTITY_NAME_COLUMN,
    IDENTITY_COMBO_BOX_N_COLUMNS
};

static void
thread_messages_toggled(GtkWidget * widget,
                        BalsaMailboxConfView * view_info)
{
    gboolean thread_messages;

    thread_messages = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    gtk_widget_set_sensitive(view_info->subject_gather, thread_messages);
}


static BalsaMailboxConfView *
mailbox_conf_view_new_full(LibBalsaMailbox * mailbox,
                           GtkWindow * window,
                           GtkWidget * grid, gint row,
                           GtkSizeGroup * size_group,
                           MailboxConfWindow * mcw,
                           GCallback callback)
{
    GtkWidget *label;
    BalsaMailboxConfView *view_info;
    GtkWidget *widget;
    const gchar *identity_name;
    gboolean thread_messages;

    view_info = g_new(BalsaMailboxConfView, 1);
    g_object_weak_ref(G_OBJECT(window), (GWeakNotify) g_free, view_info);

    label = libbalsa_create_grid_label(_("_Identity:"), grid, row);
    if (size_group)
        gtk_size_group_add_widget(size_group, label);

    identity_name = libbalsa_mailbox_get_identity_name(mailbox);
    view_info->identity_combo_box = widget =
        libbalsa_identity_combo_box(balsa_app.identities, identity_name,
                                    G_CALLBACK(check_for_blank_fields), mcw);

    gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);

    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);

    if (callback)
    	g_signal_connect_swapped(widget, "changed", callback, window);

    label =
    	libbalsa_create_grid_label(_("_Decrypt and check\n"
    		"signatures automatically:"),
    		grid, ++row);
    if (size_group)
    	gtk_size_group_add_widget(size_group, label);

    view_info->chk_crypt = gtk_combo_box_text_new();
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), view_info->chk_crypt);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view_info->chk_crypt), _("Never"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view_info->chk_crypt), _("If Possible"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view_info->chk_crypt), _("Always"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(view_info->chk_crypt),
    	libbalsa_mailbox_get_crypto_mode(mailbox));
    if (mcw)
    	g_signal_connect(view_info->chk_crypt, "changed",
    		G_CALLBACK(check_for_blank_fields), mcw);
    if (callback)
    	g_signal_connect_swapped(view_info->chk_crypt, "changed",
    		callback, window);
    gtk_widget_set_hexpand(view_info->chk_crypt, TRUE);
    gtk_grid_attach(GTK_GRID(grid), view_info->chk_crypt, 1, row, 1, 1);

    /* Show address check button */
    view_info->show_to =
        libbalsa_create_grid_check(_("Show _Recipient column"
                                     " instead of Sender"),
                                   grid, ++row,
                                   libbalsa_mailbox_get_show(mailbox) ==
                                   LB_MAILBOX_SHOW_TO);
    if (mcw)
        g_signal_connect(view_info->show_to, "toggled",
                         G_CALLBACK(check_for_blank_fields), mcw);
    if (callback)
        g_signal_connect_swapped(view_info->show_to, "toggled",
                                 callback, window);

    /* Subscribe check button */
    view_info->subscribe =
        libbalsa_create_grid_check(_("_Subscribe for new mail check"),
                                   grid, ++row,
                                   libbalsa_mailbox_get_subscribe(mailbox)
                                   != LB_MAILBOX_SUBSCRIBE_NO);
    if (mcw)
        g_signal_connect(view_info->subscribe, "toggled",
                         G_CALLBACK(check_for_blank_fields), mcw);
    if (callback)
        g_signal_connect_swapped(view_info->subscribe, "toggled",
                                 callback, window);

    /* Thread messages check button */
    thread_messages =
        libbalsa_mailbox_get_threading_type(mailbox) !=
        LB_MAILBOX_THREADING_FLAT;
    view_info->thread_messages =
        libbalsa_create_grid_check(_("_Thread messages"), grid, ++row,
                                   thread_messages);
    if (mcw != NULL) {
        g_signal_connect(view_info->thread_messages, "toggled",
                         G_CALLBACK(check_for_blank_fields), mcw);
    }
    if (callback != NULL) {
        g_signal_connect_swapped(view_info->thread_messages, "toggled",
                                 callback, window);
    }
    g_signal_connect(view_info->thread_messages, "toggled",
                     G_CALLBACK(thread_messages_toggled), view_info);

    /* Subject gather check button */
    view_info->subject_gather =
        libbalsa_create_grid_check(_("_Merge threads with the same subject"),
                                   grid, ++row,
                                   libbalsa_mailbox_get_subject_gather(mailbox));
    if (mcw != NULL) {
        g_signal_connect(view_info->subject_gather, "toggled",
                         G_CALLBACK(check_for_blank_fields), mcw);
    }
    if (callback != NULL) {
        g_signal_connect_swapped(view_info->subject_gather, "toggled",
                                 callback, window);
    }
    gtk_widget_set_sensitive(view_info->subject_gather, thread_messages);

    return view_info;
}

BalsaMailboxConfView *
mailbox_conf_view_new(LibBalsaMailbox * mailbox,
                      GtkWindow * window, GtkWidget * grid, gint row,
                      GCallback callback)
{
    return mailbox_conf_view_new_full(mailbox, window, grid, row,
                                      NULL, NULL, callback);
}

/* When closing the dialog, check whether any view items were changed,
 * and carry out the changes if necessary.
 *
 * view_info:   the BalsaMailboxConfView with the info;
 * mailbox:     the mailbox whose properties we're changing.
 */
void
mailbox_conf_view_check(BalsaMailboxConfView * view_info,
			LibBalsaMailbox * mailbox)
{
    gboolean changed;
    LibBalsaMailboxView *view;
    GtkComboBox *combo_box;
    GtkTreeIter iter;
    gint active;
    LibBalsaMailboxThreadingType threading_type;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    if (view_info == NULL)	/* POP3 mailboxes do not have view_info */
	return;

    changed = FALSE;

    view = config_load_mailbox_view(libbalsa_mailbox_get_url(mailbox));
    /* The mailbox may not have its URL yet: */
    if (view == NULL)
        view = libbalsa_mailbox_view_new();
    libbalsa_mailbox_set_view(mailbox, view);

    combo_box = GTK_COMBO_BOX(view_info->identity_combo_box);
    if (gtk_combo_box_get_active_iter(combo_box, &iter)) {
        GtkTreeModel *model;
        LibBalsaIdentity *ident;

        model = gtk_combo_box_get_model(combo_box);
        gtk_tree_model_get(model, &iter, 2, &ident, -1);
        libbalsa_mailbox_set_identity_name(mailbox,
                                           libbalsa_identity_get_identity_name(ident));
        g_object_unref(ident);
        changed = TRUE;
    }

    active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                          (view_info->show_to));
    if (libbalsa_mailbox_set_show(mailbox, active ?
                                  LB_MAILBOX_SHOW_TO :
                                  LB_MAILBOX_SHOW_FROM))
        changed = TRUE;

    active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                          (view_info->subscribe));
    if (libbalsa_mailbox_set_subscribe(mailbox, active ?
				       LB_MAILBOX_SUBSCRIBE_YES :
				       LB_MAILBOX_SUBSCRIBE_NO))
	changed = TRUE;

    /* Threading */

    active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                          (view_info->subject_gather));
    libbalsa_mailbox_set_subject_gather(mailbox, active);
    threading_type = active ? LB_MAILBOX_THREADING_JWZ : LB_MAILBOX_THREADING_SIMPLE;

    active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                          (view_info->thread_messages));
    /* Set the threading type directly, not through the UI: */
    libbalsa_mailbox_set_threading_type(mailbox,
                                        active ? threading_type : LB_MAILBOX_THREADING_FLAT);

    active = gtk_combo_box_get_active(GTK_COMBO_BOX(view_info->chk_crypt));
    if (libbalsa_mailbox_set_crypto_mode(mailbox, active))
	changed = TRUE;

    if (!changed || !libbalsa_mailbox_get_open(mailbox))
	return;

    /* Redraw the mailbox if it is open already - we MUST NOT attempt
     * opening closed mailboxes for both performance and security
     * reasons. Performance is obvious. Security is relevant here too:
     * the user might have realized that the password must be sent
     * encrypted and clicked on "Use SSL". we should not attempt to
     * open the connection with old settings requesting unencrypted
     * connection. We temporarily increase its open_ref to keep the
     * backend open. */
    if(MAILBOX_OPEN(mailbox)) {
        libbalsa_mailbox_open(mailbox, NULL);
        balsa_mblist_close_mailbox(mailbox);
        balsa_mblist_open_mailbox(mailbox);
        libbalsa_mailbox_close(mailbox, FALSE);
    }
}
