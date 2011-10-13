/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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

#include "libbalsa.h"
#include "imap-server.h"
#include "mailbox-filter.h"
#include "libbalsa-conf.h"
#include <glib/gi18n.h>

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

struct _BalsaMailboxConfView {
    GtkWindow *window;
    GtkWidget *identity_combo_box;
    GtkWidget *show_to;
    GtkWidget *subscribe;
#ifdef HAVE_GPGME
    GtkWidget *chk_crypt;
#endif
};
typedef struct _MailboxConfWindow MailboxConfWindow;
struct _MailboxConfWindow {
    LibBalsaMailbox *mailbox;

    GtkDialog *window;

    void (*ok_handler)(MailboxConfWindow*);
    const gchar *ok_button_name;
    GtkWidget *mailbox_name;
    GType mailbox_type;
    BalsaMailboxConfView *view_info;
    gboolean ok_sensitive;

    union {
	/* for imap mailboxes & directories */
	struct {
	    GtkWidget *port;
	    GtkWidget *username;
            GtkWidget *anonymous;
            GtkWidget *remember;
	    GtkWidget *password;
	    GtkWidget *folderpath;
            GtkWidget *enable_persistent, *has_bugs;
            BalsaServerConf bsc;
	} imap;

	/* for pop3 mailboxes */
	struct {
	    GtkWidget *username;
	    GtkWidget *password;
	    GtkWidget *check;
	    GtkWidget *delete_from_server;
            BalsaServerConf bsc;
            GtkWidget *disable_apop;
	    GtkWidget *filter;
	    GtkWidget *filter_cmd;
	} pop3;
    } mb_data;
};

static void mailbox_conf_update(MailboxConfWindow *conf_window);
static void mailbox_conf_add(MailboxConfWindow *conf_window);

/* misc functions */
static void mailbox_conf_set_values(MailboxConfWindow *mcw);

static void fill_in_imap_data(MailboxConfWindow *mcw, gchar ** name, 
                              gchar ** path);
static void update_imap_mailbox(MailboxConfWindow *mcw);

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
static GtkWidget *create_imap_mailbox_dialog(MailboxConfWindow *mcw);

/* ========================================================= */
/* BEGIN BalsaServerConf =================================== */
struct menu_data {
    const char *label;
    int tag;
};
struct mailbox_conf_combo_box_info {
    GSList *tags;
};
#define BALSA_MC_COMBO_BOX_INFO "balsa-mailbox-conf-combo-box-info"

static void
mailbox_conf_combo_box_info_free(struct mailbox_conf_combo_box_info *info)
{
    g_slist_free(info->tags);
    g_free(info);
}

static void
mailbox_conf_combo_box_make(GtkComboBoxText * combo_box, unsigned cnt,
                            const struct menu_data *data)
{
    struct mailbox_conf_combo_box_info *info =
        g_new(struct mailbox_conf_combo_box_info, 1);
    gint i;

    info->tags = NULL;
    for (i = cnt; --i >= 0;) {
        gtk_combo_box_text_prepend_text(combo_box, _(data[i].label));
        info->tags =
            g_slist_prepend(info->tags, GINT_TO_POINTER(data[i].tag));
    }
    g_object_set_data_full(G_OBJECT(combo_box), BALSA_MC_COMBO_BOX_INFO,
                           info,
                           (GDestroyNotify)
                           mailbox_conf_combo_box_info_free);
}


static void
bsc_ssl_toggled_cb(GtkWidget * widget, BalsaServerConf * bsc)
{
    const gchar *host, *colon;
    gboolean newstate =
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

    gtk_widget_set_sensitive(bsc->tls_option, newstate);

    host = gtk_entry_get_text(GTK_ENTRY(bsc->server));
    if ((colon = strchr(host, ':')) != NULL) {
	/* A port was specified... */
	gchar *port = g_ascii_strdown(colon + 1, -1);
        if (strstr(bsc->default_ports, port) != NULL)
	    /* and it is one of the default ports, so strip it. */
	    gtk_editable_delete_text(GTK_EDITABLE(bsc->server),
                                     colon - host, -1);
	g_free(port);
    }
}

GtkWidget*
balsa_server_conf_get_advanced_widget(BalsaServerConf *bsc, LibBalsaServer *s,
                                      int extra_rows)
{
    static const struct menu_data tls_menu[] = {
        { N_("Never"),       LIBBALSA_TLS_DISABLED },
        { N_("If Possible"), LIBBALSA_TLS_ENABLED  },
        { N_("Required"),    LIBBALSA_TLS_REQUIRED }
    };
    GtkWidget *label;
    GtkWidget *box;
    gboolean use_ssl = s && s->use_ssl;

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

#if !defined(USE_SSL)
    gtk_box_pack_start(GTK_BOX(box),
                       gtk_label_new
                       (_("Balsa was built without SSL support.\n"
                          "Neither SSL nor TLS can be used.")),
                       FALSE, FALSE, 0);
#endif                          /* !defined(USE_SSL) */

    bsc->grid = GTK_GRID(libbalsa_create_grid());
    gtk_container_set_border_width(GTK_CONTAINER(bsc->grid), 12);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(bsc->grid),
                       FALSE, FALSE, 0);

    bsc->used_rows = 0;

    bsc->use_ssl = balsa_server_conf_add_checkbox(bsc, _("Use _SSL"));
    if(use_ssl)
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bsc->use_ssl), TRUE);
#if !defined(USE_SSL)
    gtk_widget_set_sensitive(bsc->use_ssl, FALSE);
#endif                          /* !defined(USE_SSL) */

    label =
        libbalsa_create_grid_label(_("Use _TLS:"), GTK_WIDGET(bsc->grid), 1);

    bsc->tls_option = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(bsc->tls_option, TRUE);
    mailbox_conf_combo_box_make(GTK_COMBO_BOX_TEXT(bsc->tls_option),
                                ELEMENTS(tls_menu), tls_menu);
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsc->tls_option),
                             s ? s->tls_mode : LIBBALSA_TLS_ENABLED);
    gtk_grid_attach(bsc->grid, bsc->tls_option, 1, 1, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), bsc->tls_option);

    g_signal_connect(G_OBJECT (bsc->use_ssl), "toggled",
                     G_CALLBACK (bsc_ssl_toggled_cb), bsc);
    gtk_widget_show_all(GTK_WIDGET(bsc->grid));
    bsc->used_rows = 2;
    gtk_widget_set_sensitive(bsc->tls_option, !use_ssl);
#if !defined(USE_TLS)
    gtk_widget_set_sensitive(label, FALSE);
    gtk_widget_set_sensitive(bsc->tls_option, FALSE);
#endif                          /* !defined(USE_TLS) */

    return box;
}

GtkWidget*
balsa_server_conf_add_checkbox(BalsaServerConf *bsc,
                               const char *label)
{
    return libbalsa_create_grid_check(label, GTK_WIDGET(bsc->grid),
                                      bsc->used_rows++, FALSE);
}

GtkWidget*
balsa_server_conf_add_spinner(BalsaServerConf *bsc,
                              const char *label, gint lo, gint hi, gint step,
                              gint initial_value)
{
    GtkWidget *spin_button;
    GtkWidget *lbl = libbalsa_create_grid_label(label, GTK_WIDGET(bsc->grid),
                                                bsc->used_rows);
    spin_button = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_widget_set_hexpand(spin_button, TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button),
			      (float) initial_value);

    gtk_grid_attach(bsc->grid, spin_button, 1, bsc->used_rows, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), spin_button);
    bsc->used_rows++;
    return spin_button;
}

void
balsa_server_conf_set_values(BalsaServerConf *bsc, LibBalsaServer *server)
{
    g_return_if_fail(server);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bsc->use_ssl), 
                                 server->use_ssl);
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsc->tls_option),
                             server->tls_mode);
    gtk_widget_set_sensitive(bsc->tls_option, !server->use_ssl);
}


gboolean
balsa_server_conf_get_use_ssl(BalsaServerConf *bsc)
{
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bsc->use_ssl));
}

LibBalsaTlsMode
balsa_server_conf_get_tls_mode(BalsaServerConf *bsc)
{
    struct mailbox_conf_combo_box_info *info =
        g_object_get_data(G_OBJECT(bsc->tls_option),
                          BALSA_MC_COMBO_BOX_INFO);
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(bsc->tls_option));

    return (LibBalsaTlsMode)
        GPOINTER_TO_INT(g_slist_nth_data(info->tags, active));
}

/* END BalsaServerConf ===================================== */
/* ========================================================= */

static void
pop3_enable_filter_cb(GtkWidget * w, MailboxConfWindow * mcw)
{
    GtkToggleButton *button = GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.filter);
    gtk_widget_set_sensitive(mcw->mb_data.pop3.filter_cmd,
                             gtk_toggle_button_get_active(button));
}

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

    if (mbnode->mailbox == NULL)
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
    LibBalsaMailbox* mailbox = mbnode->mailbox;
    gchar *url, *group;

    if(BALSA_IS_MAILBOX_SPECIAL(mailbox)) {
	balsa_information(
	    LIBBALSA_INFORMATION_ERROR,
	    _("Mailbox \"%s\" is used by Balsa and I cannot remove it.\n"
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
                               _("Remove from _list"), 0,
                               _("Remove from list and _disk"), 1,
                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
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
                               _("_Remove from server"), 0,
                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
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
                               _("_Remove from list"), 0,
                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                               NULL);
    }
    
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(ask, GTK_WINDOW(balsa_app.main_window));
#endif
    button = gtk_dialog_run(GTK_DIALOG(ask));
    gtk_widget_destroy(ask);

    /* button < 0 means that the dialog window was closed without pressing
       any button other than CANCEL.
    */
    if ( button < 0)
	return;

    /* Save the mailbox URL */
    url = g_strdup(mailbox->url ? mailbox->url : mailbox->name);

    /* Delete it from the config file and internal nodes */
    config_mailbox_delete(mailbox);

    /* Close the mailbox, in case it was open */
    if (!LIBBALSA_IS_MAILBOX_POP3(mailbox))
	balsa_mblist_close_mailbox(mailbox);

    /* Remove mailbox on IMAP server */
    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) && !mailbox->config_prefix) {
        GError *err = NULL;
	BalsaMailboxNode *parent = mbnode->parent;
        if(libbalsa_imap_delete_folder(LIBBALSA_MAILBOX_IMAP(mailbox),
                                       &err)) {
            /* a chain of folders might go away, so we'd better rescan from
             * higher up
             */
            while (!parent->mailbox && parent->parent) {
                mbnode = parent;
                parent = parent->parent;
            }
            balsa_mblist_mailbox_node_remove(mbnode);
            balsa_mailbox_node_rescan(parent); /* see it as server sees it */
        } else {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Folder deletion failed. Reason: %s"),
                              err ? err->message : "unknown");
            g_clear_error(&err);
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
    case MCW_RESPONSE: mcw->ok_handler(mcw); 
        /* fall through */
    default:
        if (mcw->mailbox)
            g_object_set_data(G_OBJECT(mcw->mailbox),
                              BALSA_MAILBOX_CONF_DIALOG, NULL);
        gtk_widget_destroy(GTK_WIDGET(dialog));
        /* fall through */
    case 0:
        break;
    }
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
        mcw->ok_button_name = _("_Update");
        mcw->mailbox = mbnode->mailbox;
    } else {
        mcw->ok_handler = mailbox_conf_add;
        mcw->ok_button_name = _("_Add");
    }
    mcw->mailbox_type = mailbox_type;

    mcw->window = GTK_DIALOG(create_dialog(mcw));
    g_object_weak_ref(G_OBJECT(mcw->window), (GWeakNotify) g_free, mcw);
    
    gtk_dialog_set_response_sensitive(mcw->window, MCW_RESPONSE, FALSE);
    mcw->ok_sensitive = FALSE;
    gtk_dialog_set_default_response(mcw->window,
                                    update ? GTK_RESPONSE_CLOSE :
                                    MCW_RESPONSE);

    if(mbnode)
        mailbox_conf_set_values(mcw);

    g_signal_connect(G_OBJECT(mcw->window), "response", 
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
        gtk_window_present(GTK_WINDOW(dialog));
        return;
    }

    dialog = run_mailbox_conf(NULL, mailbox_type, FALSE);
    g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer) &dialog);
}

/*
 * Edit an existing mailboxes properties
 */
void
mailbox_conf_edit(BalsaMailboxNode * mbnode)
{
    GtkWidget *dialog;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox));

    dialog = g_object_get_data(G_OBJECT(mbnode->mailbox),
                               BALSA_MAILBOX_CONF_DIALOG);
    if (dialog) {
        gtk_window_present(GTK_WINDOW(dialog));
        return;
    }

    dialog =
        run_mailbox_conf(mbnode, G_OBJECT_TYPE(G_OBJECT(mbnode->mailbox)),
                         TRUE);
    g_object_set_data(G_OBJECT(mbnode->mailbox), BALSA_MAILBOX_CONF_DIALOG,
                      dialog);
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
	gtk_entry_set_text(GTK_ENTRY(mcw->mailbox_name), mailbox->name);

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
        if (mailbox->url) {
            GtkFileChooser *chooser = GTK_FILE_CHOOSER(mcw->window);
	    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
            const gchar *path = libbalsa_mailbox_local_get_path(local);
            gchar *basename = g_path_get_basename(path);
            gtk_file_chooser_set_filename(chooser, path);
            gtk_file_chooser_set_current_name(chooser, basename);
            g_free(basename);
        }
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	LibBalsaMailboxPop3 *pop3;
	LibBalsaServer *server;

	pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
	server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);


	if (server->host)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.bsc.server), 
                               server->host);
	if (server->user)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.username), 
                               server->user);
	if (server->passwd)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.password),
			       server->passwd);
        balsa_server_conf_set_values(&mcw->mb_data.pop3.bsc, server);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.disable_apop),
				     pop3->disable_apop);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.check),
				     pop3->check);

	gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.delete_from_server),
	     pop3->delete_from_server);
	gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.filter),
	     pop3->filter);
        gtk_widget_set_sensitive(mcw->mb_data.pop3.filter_cmd, pop3->filter);
	if (pop3->filter_cmd)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.pop3.filter_cmd),
			       pop3->filter_cmd);

    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	LibBalsaMailboxImap *imap;
	LibBalsaServer *server;
        const gchar *path;
	imap = LIBBALSA_MAILBOX_IMAP(mailbox);
	server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

	if (server->host)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.bsc.server), 
                               server->host);
	if (server->user)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.username),
			       server->user);
	gtk_toggle_button_set_active
            (GTK_TOGGLE_BUTTON(mcw->mb_data.imap.anonymous),
	     server->try_anonymous);
	gtk_toggle_button_set_active
            (GTK_TOGGLE_BUTTON(mcw->mb_data.imap.remember),
	     server->remember_passwd);
	if (server->passwd)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.password),
			       server->passwd);
        path = libbalsa_mailbox_imap_get_path(imap);
	if (path)
	    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.folderpath),
			       path);
        balsa_server_conf_set_values(&mcw->mb_data.imap.bsc, server);
#if !defined(ENABLE_TOUCH_UI)
        if(libbalsa_imap_server_has_persistent_cache
           (LIBBALSA_IMAP_SERVER(server)))
            gtk_toggle_button_set_active
                (GTK_TOGGLE_BUTTON(mcw->mb_data.imap.enable_persistent),
                 TRUE);
#endif
        if(libbalsa_imap_server_has_bug(LIBBALSA_IMAP_SERVER(server),
                                        ISBUG_FETCH))
            gtk_toggle_button_set_active
                (GTK_TOGGLE_BUTTON(mcw->mb_data.imap.has_bugs),
                 TRUE);
        if(!server->try_anonymous)
            gtk_widget_set_sensitive(GTK_WIDGET(mcw->mb_data.imap.anonymous),
                                     FALSE);
        if(!server->remember_passwd)
            gtk_widget_set_sensitive(GTK_WIDGET(mcw->mb_data.imap.password),
                                     FALSE);
    }
}


/*
 * Checks for blank fields in the dialog.
 * Sets the sensitivity of the Update/Add button accordingly.
 * This function should be attached to a change event signal 
 * on any widget which can affect the validity of the input.
 */
static void
check_for_blank_fields(GtkWidget *widget, MailboxConfWindow *mcw)
{
    gboolean sensitive = TRUE;

    if (mcw->mailbox_name &&!*gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name)))
	sensitive = FALSE;
    else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL) ) {
        gchar *filename =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(mcw->window));
	if (filename)
	    g_free(filename);
	else
	    sensitive = FALSE;
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_IMAP ) ) {
	if (!*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.folderpath))
            || !*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.bsc.server))
            || !*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.imap.username)))
	    sensitive = FALSE;
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3) ) {
	if (!*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.username))
            || !*gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.bsc.server)))
	    sensitive = FALSE;
    }

    gtk_dialog_set_response_sensitive(mcw->window, MCW_RESPONSE, sensitive);
    mcw->ok_sensitive = sensitive;
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
						   (mcw->mb_data.imap.bsc.server)));
    }
}

/*
 * Update a pop3 mailbox with details from the dialog
 */
static void
update_pop_mailbox(MailboxConfWindow *mcw)
{
    LibBalsaMailboxPop3 * mailbox;
    const gchar *cmd;
    LibBalsaServer *server;
    mailbox = LIBBALSA_MAILBOX_POP3(mcw->mailbox);

    g_free(LIBBALSA_MAILBOX(mailbox)->name);
    LIBBALSA_MAILBOX(mailbox)->name =
	g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->mailbox_name)));

    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    libbalsa_server_set_username(server,
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.pop3.username)));
    libbalsa_server_set_password(server,
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.pop3.password)));
    libbalsa_server_set_host(server,
			     gtk_entry_get_text(GTK_ENTRY
						(mcw->mb_data.pop3.bsc.server)),
                             balsa_server_conf_get_use_ssl
                             (&mcw->mb_data.pop3.bsc));
    libbalsa_server_config_changed(server);
    server->tls_mode = balsa_server_conf_get_tls_mode(&mcw->mb_data.pop3.bsc);
    mailbox->check =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.check));
    mailbox->disable_apop =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.disable_apop));
    mailbox->delete_from_server =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
				     (mcw->mb_data.pop3.delete_from_server));
    mailbox->filter =
	gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->mb_data.pop3.filter));
    cmd = gtk_entry_get_text(GTK_ENTRY(mcw->mb_data.pop3.filter_cmd));
    g_free(mailbox->filter_cmd);
    mailbox->filter_cmd = cmd ? g_strdup(cmd) : NULL;
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
    if (!server) {
	server = LIBBALSA_SERVER(libbalsa_imap_server_new("",""));
	libbalsa_mailbox_remote_set_server(LIBBALSA_MAILBOX_REMOTE(mailbox),
					   server);
	g_signal_connect_swapped(server, "config-changed",
                                 G_CALLBACK(config_mailbox_update),
				 mailbox);
    }
    g_free(LIBBALSA_MAILBOX(mailbox)->name);
    fill_in_imap_data(mcw, &LIBBALSA_MAILBOX(mailbox)->name, &path);
    libbalsa_server_set_username(server,
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.imap.username)));
    server->try_anonymous = 
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->
                                                       mb_data.imap.anonymous));
    server->remember_passwd = 
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mcw->
                                                       mb_data.imap.remember));
    server->tls_mode = balsa_server_conf_get_tls_mode(&mcw->mb_data.imap.bsc);
    libbalsa_server_set_password(server,
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.imap.password)));
#if !defined(ENABLE_TOUCH_UI)
    libbalsa_imap_server_enable_persistent_cache
        (LIBBALSA_IMAP_SERVER(server),
         gtk_toggle_button_get_active
         GTK_TOGGLE_BUTTON(mcw->mb_data.imap.enable_persistent));
#endif
    libbalsa_imap_server_set_bug
        (LIBBALSA_IMAP_SERVER(server), ISBUG_FETCH,
         gtk_toggle_button_get_active
         GTK_TOGGLE_BUTTON(mcw->mb_data.imap.has_bugs));
    /* Set host after all other server changes, as it triggers
     * save-to-config for any folder or mailbox using this server. */
    libbalsa_server_set_host(server,
			     gtk_entry_get_text(GTK_ENTRY
						(mcw->mb_data.imap.bsc.server)),
                             balsa_server_conf_get_use_ssl
                             (&mcw->mb_data.imap.bsc));
    libbalsa_server_config_changed(server);
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
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mcw->mailbox);
    int i;

    mailbox_conf_view_check(mcw->view_info, mailbox);

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	BalsaMailboxNode *mbnode;
	gchar *filename;
	gchar *path;
	gchar *name;

	mbnode = balsa_find_mailbox(mailbox);
        filename =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(mcw->window));
	path = g_strdup(libbalsa_mailbox_local_get_path(mailbox));
        if (strcmp(filename, path)) {
            /* rename */
	    gchar *file_dir, *path_dir;

            i = libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL
                                                (mailbox), filename, FALSE);
            if (i != 0) {
                balsa_information(LIBBALSA_INFORMATION_WARNING,
                                  _("Rename of %s to %s failed:\n%s"),
                                  path, filename, strerror(i));
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

        name = mcw->mailbox_name ?
            gtk_editable_get_chars(GTK_EDITABLE(mcw->mailbox_name), 0, -1)
            : g_path_get_basename(filename);
	if (strcmp(name, mailbox->name)) {
	    /* Change name. */
            g_free(mailbox->name);
	    mailbox->name = name;
	    balsa_mblist_mailbox_node_redraw(mbnode);
	} else
	    g_free(name);

	g_object_unref(mbnode);
        g_free(filename);
	g_free(path);
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	update_pop_mailbox(mcw);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	update_imap_mailbox(mcw);
        /* update_imap_mailbox saved the config so we do not need to here: */
	return;
    }

    if (mailbox->config_prefix)
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

    mcw->mailbox = g_object_new(mcw->mailbox_type, NULL);
    mailbox_conf_view_check(mcw->view_info, mcw->mailbox);

    if ( LIBBALSA_IS_MAILBOX_LOCAL(mcw->mailbox) ) {
	LibBalsaMailboxLocal *ml  = LIBBALSA_MAILBOX_LOCAL(mcw->mailbox);
	gchar *path;

        path =
            gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(mcw->window));

        if (libbalsa_mailbox_local_set_path(ml, path, TRUE) != 0) {
            g_free(path);
	    g_object_unref(G_OBJECT(mcw->mailbox));
	    mcw->mailbox = NULL;
	    return;
	}

	save_to_config =
            !libbalsa_path_is_below_dir(path,
                                        balsa_app.local_mail_directory);
        printf("Save to config: %d\n", save_to_config);
	mcw->mailbox->name = g_path_get_basename(path);
        g_free(path);

	balsa_mailbox_local_append(mcw->mailbox);
    }
    mbnode = balsa_mailbox_node_new_from_mailbox(mcw->mailbox);
    if ( LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox) ) {
	/* POP3 Mailboxes */
	update_pop_mailbox(mcw);
	balsa_app.inbox_input =
	    g_list_append(balsa_app.inbox_input, mbnode);
    } else if ( LIBBALSA_IS_MAILBOX_IMAP(mcw->mailbox) ) {
	update_imap_mailbox(mcw);
	balsa_mblist_mailbox_node_append(NULL, mbnode);
	update_mail_servers();
    } else if ( !LIBBALSA_IS_MAILBOX_LOCAL(mcw->mailbox) ) {
	g_assert_not_reached();
    }

    if (!LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox)) {
	/* Mailbox must have an URL by now... */
	g_assert(mcw->mailbox->url != NULL);
	/* ...and a view... */
	g_assert(mcw->mailbox->view != NULL);
	/* ...and if it's not already in the table, insert it. */
	if (mcw->mailbox->view !=
	    g_hash_table_lookup(libbalsa_mailbox_view_table,
				mcw->mailbox->url))
	    g_hash_table_insert(libbalsa_mailbox_view_table,
				g_strdup(mcw->mailbox->url),
				mcw->mailbox->view);
    }

    if(save_to_config)
	config_mailbox_add(mcw->mailbox, NULL);

    if (LIBBALSA_IS_MAILBOX_POP3(mcw->mailbox))
	/* redraw the pop3 server list */
	update_mail_servers();
    else {/* redraw the main mailbox list */
	/* If the new mailbox is in the local mail tree, its view will
	 * already be in the mailbox-views, in which case inserting it
	 * again would cause the view to be freed, so we'd better
	 * check... */
	if (!g_hash_table_lookup(libbalsa_mailbox_view_table,
				 mcw->mailbox->url))
	    g_hash_table_insert(libbalsa_mailbox_view_table,
				g_strdup(mcw->mailbox->url),
				mcw->mailbox->view);
    }
}

/* Create a page for the type of mailbox... */
static GtkWidget *
create_dialog(MailboxConfWindow *mcw)
{
    if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_LOCAL) ) {
	return create_local_mailbox_dialog(mcw);
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_POP3) ) {
	return create_pop_mailbox_dialog(mcw);
    } else if (g_type_is_a(mcw->mailbox_type, LIBBALSA_TYPE_MAILBOX_IMAP) ) {
	return create_imap_mailbox_dialog(mcw);
    } else {
	g_warning("Unknown mailbox type: %s\n",
                  g_type_name(mcw->mailbox_type));
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
local_mailbox_dialog_cb(GtkWidget * widget, MailboxConfWindow * mcw)
{
    gchar *filename =
        gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(mcw->window));

    if (filename) {
        gboolean changed = TRUE;
        if (mcw->mailbox) {
            LibBalsaMailboxLocal *local =
                LIBBALSA_MAILBOX_LOCAL(mcw->mailbox);
            const gchar *path = libbalsa_mailbox_local_get_path(local);
            changed = strcmp(filename, path);
        }
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
    GtkWidget *label = NULL;
    gint row = -1;
    GtkFileChooserAction action;
    GtkWidget *entry = NULL;
    GtkSizeGroup *size_group;
    const gchar *type;
    gchar *title;

    grid = libbalsa_create_grid();

    /* mailbox name */
    if(mcw->mailbox && mcw->mailbox->config_prefix) {
        label = libbalsa_create_grid_label(_("_Mailbox Name:"), grid, ++row);
        mcw->mailbox_name =
            libbalsa_create_grid_entry(grid,
                                       G_CALLBACK(check_for_blank_fields),
                                       mcw, row, NULL, label);
    } else mcw->mailbox_name = NULL;

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
                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                    NULL);
    g_free(title);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(balsa_app.main_window));
#endif

    size_group = libbalsa_create_size_group(dialog);
    if (label)
        gtk_size_group_add_widget(size_group, label);

    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), grid);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
	                                balsa_app.local_mail_directory);
    g_signal_connect(G_OBJECT(dialog), "selection-changed",
                     G_CALLBACK(local_mailbox_dialog_cb), mcw);
    balsa_get_entry(dialog, &entry);
    if (entry)
	g_signal_connect(G_OBJECT(entry), "changed",
                         G_CALLBACK(local_mailbox_dialog_cb), mcw);

    mcw->view_info =
        mailbox_conf_view_new_full(mcw->mailbox, GTK_WINDOW(dialog), grid,
                                   ++row, size_group, mcw, NULL);

    return dialog;
}

static GtkWidget *
create_generic_dialog(MailboxConfWindow * mcw)
{
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(_("Remote Mailbox Configurator"),
                                    GTK_WINDOW(balsa_app.main_window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    mcw->ok_button_name, MCW_RESPONSE,
                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(balsa_app.main_window));
#endif
    return dialog;
}

static GtkWidget *
create_pop_mailbox_dialog(MailboxConfWindow *mcw)
{
    GtkWidget *dialog;
    GtkWidget *notebook, *grid, *label, *advanced;

    notebook = gtk_notebook_new();
    grid = libbalsa_create_grid();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid,
                             gtk_label_new_with_mnemonic(_("_Basic")));

    /* mailbox name */
    label = libbalsa_create_grid_label(_("Mailbox _name:"), grid, 0);
    mcw->mailbox_name =
        libbalsa_create_grid_entry(grid, G_CALLBACK(check_for_blank_fields),
				   mcw, 0, NULL, label);
    /* pop server */
    label = libbalsa_create_grid_label(_("_Server:"), grid, 1);
    mcw->mb_data.pop3.bsc.server =
        libbalsa_create_grid_entry(grid, G_CALLBACK(check_for_blank_fields),
				   mcw, 1, "localhost", label);
    mcw->mb_data.pop3.bsc.default_ports = POP3_DEFAULT_PORTS;


    /* username  */
    label= libbalsa_create_grid_label(_("Use_r name:"), grid, 2);
    mcw->mb_data.pop3.username =
        libbalsa_create_grid_entry(grid, G_CALLBACK(check_for_blank_fields),
                                   mcw, 2, g_get_user_name(), label);

    /* password field */
    label = libbalsa_create_grid_label(_("Pass_word:"), grid, 3);
    mcw->mb_data.pop3.password =
        libbalsa_create_grid_entry(grid, NULL, NULL, 3, NULL, label);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.pop3.password), FALSE);

    /* toggle for deletion from server */
    mcw->mb_data.pop3.delete_from_server =
	libbalsa_create_grid_check(_("_Delete messages from server"
                                     " after download"),
                                   grid, 4, TRUE);

    /* toggle for check */
    mcw->mb_data.pop3.check =
	libbalsa_create_grid_check(_("_Enable check for new mail"),
                                   grid, 5, TRUE);

    /* Procmail */
    mcw->mb_data.pop3.filter =
	libbalsa_create_grid_check(_("_Filter messages through procmail"),
                                   grid, 6, FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.pop3.filter), "toggled",
                     G_CALLBACK(pop3_enable_filter_cb), mcw);
    label = libbalsa_create_grid_label(_("Fi_lter Command:"), grid, 7);
    mcw->mb_data.pop3.filter_cmd =
	libbalsa_create_grid_entry(grid, G_CALLBACK(check_for_blank_fields),
                                   mcw, 7, "procmail -f -", label);

    advanced =
        balsa_server_conf_get_advanced_widget(&mcw->mb_data.pop3.bsc,
                                              NULL, 2);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), advanced,
                             gtk_label_new_with_mnemonic(_("_Advanced")));
    /* toggle for apop */
    mcw->mb_data.pop3.disable_apop = 
        balsa_server_conf_add_checkbox(&mcw->mb_data.pop3.bsc,
                                       _("Disable _APOP"));

    gtk_widget_show_all(notebook);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
    gtk_widget_grab_focus(mcw->mailbox_name);

    dialog = create_generic_dialog(mcw);
    gtk_container_add(GTK_CONTAINER
                      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      notebook);
    return dialog;
}

static void
anon_toggle_cb(GtkToggleButton *anon_button, MailboxConfWindow *mcw)
{
    gtk_widget_set_sensitive(GTK_WIDGET(mcw->mb_data.imap.anonymous),
                             gtk_toggle_button_get_active(anon_button));
}
static void
remember_toggle_cb(GtkToggleButton *remember_button, MailboxConfWindow *mcw)
{
    gtk_widget_set_sensitive(GTK_WIDGET(mcw->mb_data.imap.password),
                             gtk_toggle_button_get_active(remember_button));
}

static void
entry_activated(GtkEntry * entry, MailboxConfWindow * mcw)
{
    if (mcw->ok_sensitive)
        gtk_dialog_response(GTK_DIALOG(mcw->window), MCW_RESPONSE);
}

static GtkWidget *
create_imap_mailbox_dialog(MailboxConfWindow *mcw)
{
    GtkWidget *dialog;
    GtkWidget *notebook, *advanced, *grid;
    GtkWidget *label;
    GtkWidget *entry;
    gint row = -1;

#if defined(HAVE_GNOME_KEYRING)
    static const gchar *remember_password_message =
        N_("_Remember password in keyring");
#else
    static const gchar *remember_password_message =
        N_("_Remember password");
#endif

    notebook = gtk_notebook_new();
    grid = libbalsa_create_grid();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid,
                             gtk_label_new_with_mnemonic(_("_Basic")));

    /* mailbox name */
    label = libbalsa_create_grid_label(_("Mailbox _name:"), grid, ++row);
    mcw->mailbox_name =
        libbalsa_create_grid_entry(grid, G_CALLBACK(check_for_blank_fields),
                                   mcw, row, NULL, label);

    /* imap server */
    label = libbalsa_create_grid_label(_("_Server:"), grid, ++row);
    mcw->mb_data.imap.bsc.server =
	libbalsa_create_grid_entry(grid, G_CALLBACK(check_for_blank_fields),
                                   mcw, row, "localhost", label);
    mcw->mb_data.imap.bsc.default_ports = IMAP_DEFAULT_PORTS;

    /* username  */
    label = libbalsa_create_grid_label(_("_Username:"), grid, ++row);
    mcw->mb_data.imap.username =
	libbalsa_create_grid_entry(grid, G_CALLBACK(check_for_blank_fields),
                                   mcw, row, g_get_user_name(), label);

    /* toggle for anonymous password */
    mcw->mb_data.imap.anonymous =
	libbalsa_create_grid_check(_("_Anonymous access"), grid,
                                   ++row, FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.anonymous), "toggled",
                     G_CALLBACK(anon_toggle_cb), mcw);
    /* toggle for remember password */
    mcw->mb_data.imap.remember =
	libbalsa_create_grid_check(_(remember_password_message), grid,
                                   ++row, FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.remember), "toggled",
                     G_CALLBACK(remember_toggle_cb), mcw);

   /* password field */
    label = libbalsa_create_grid_label(_("Pass_word:"), grid, ++row);
    mcw->mb_data.imap.password =
	libbalsa_create_grid_entry(grid, NULL, NULL, row, NULL, label);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.imap.password), FALSE);

    label = libbalsa_create_grid_label(_("F_older path:"), grid, ++row);

    mcw->mb_data.imap.folderpath = entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.folderpath), "INBOX");

    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  mcw->mb_data.imap.folderpath);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.folderpath), "activate",
                     G_CALLBACK(entry_activated), mcw);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.folderpath), "changed",
                     G_CALLBACK(check_for_blank_fields), mcw);

    gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);

    advanced =
        balsa_server_conf_get_advanced_widget(&mcw->mb_data.imap.bsc,
                                              NULL, 1);
#if !defined(ENABLE_TOUCH_UI)
    mcw->mb_data.imap.enable_persistent = 
        balsa_server_conf_add_checkbox(&mcw->mb_data.imap.bsc,
                                       _("Enable _persistent cache"));
#endif
    mcw->mb_data.imap.has_bugs = 
        balsa_server_conf_add_checkbox(&mcw->mb_data.imap.bsc,
                                       _("Enable _bug workarounds"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), advanced,
                             gtk_label_new_with_mnemonic(_("_Advanced")));

    gtk_widget_show_all(notebook);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
    gtk_widget_grab_focus(mcw->mailbox_name? 
                          mcw->mailbox_name : mcw->mb_data.imap.bsc.server);

    dialog = create_generic_dialog(mcw);
    gtk_container_add(GTK_CONTAINER
                      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                      notebook);

    mcw->view_info =
        mailbox_conf_view_new_full(mcw->mailbox, GTK_WINDOW(dialog), grid,
                                   ++row, NULL, mcw, NULL);

    return dialog;
}

/* Manage the widgets that control aspects of the view, not the config.
 * Currently the mailbox default identity and whether the address column
 * shows the sender or the recipient can be controlled in this way.
 * Other aspects like sort column and sort order are just remembered
 * when the user changes them with the GtkTreeView controls. */

/* Free a BalsaMailboxConfView. */
static void
mailbox_conf_view_free(BalsaMailboxConfView *view_info)
{
    g_free(view_info);
}

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
    GList *list;
    const gchar *identity_name;
    gint active;

    view_info = g_new(BalsaMailboxConfView, 1);
    g_object_weak_ref(G_OBJECT(window),
                      (GWeakNotify) mailbox_conf_view_free, view_info);
    view_info->window = window;

    label = libbalsa_create_grid_label(_("_Identity:"), grid, row);
    if (size_group)
        gtk_size_group_add_widget(size_group, label);

    view_info->identity_combo_box = widget = gtk_combo_box_text_new();
    if (mcw)
        g_signal_connect(view_info->identity_combo_box, "changed",
                         G_CALLBACK(check_for_blank_fields), mcw);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
    identity_name = libbalsa_mailbox_get_identity_name(mailbox);
    for (list = balsa_app.identities, active = 0; list;
         list = list->next, ++active) {
        LibBalsaIdentity *ident = list->data;
        gchar *name;

        name = internet_address_to_string(ident->ia, FALSE);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), name);
        g_free(name);
        if (identity_name
            && strcmp(identity_name, ident->identity_name) == 0)
            gtk_combo_box_set_active(GTK_COMBO_BOX(widget), active);
    }
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    if (callback)
        g_signal_connect_swapped(view_info->identity_combo_box, "changed",
                                 callback, window);

#ifdef HAVE_GPGME
    {
	/* scope */
	static const struct menu_data chk_crypt_menu[] = {
	    { N_("Never"),       LB_MAILBOX_CHK_CRYPT_NEVER  },
	    { N_("If Possible"), LB_MAILBOX_CHK_CRYPT_MAYBE  },
	    { N_("Always"),      LB_MAILBOX_CHK_CRYPT_ALWAYS }
	};

        label =
            libbalsa_create_grid_label(_("_Decrypt and check\n"
                                         "signatures automatically:"),
                                       grid, ++row);
        if (size_group)
            gtk_size_group_add_widget(size_group, label);

        view_info->chk_crypt = gtk_combo_box_text_new();
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), view_info->chk_crypt);
        mailbox_conf_combo_box_make(GTK_COMBO_BOX_TEXT(view_info->chk_crypt),
                                    ELEMENTS(chk_crypt_menu),
                                    chk_crypt_menu);
        gtk_combo_box_set_active(GTK_COMBO_BOX(view_info->chk_crypt),
                                 libbalsa_mailbox_get_crypto_mode
                                 (mailbox));
        if (mcw)
            g_signal_connect(view_info->chk_crypt, "changed",
                             G_CALLBACK(check_for_blank_fields), mcw);
        if (callback)
            g_signal_connect_swapped(view_info->chk_crypt, "changed",
                                     callback, window);
        gtk_widget_set_hexpand(view_info->chk_crypt, TRUE);
	gtk_grid_attach(GTK_GRID(grid), view_info->chk_crypt, 1, row, 1, 1);
    }
#endif

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

#ifdef HAVE_GPGME
static LibBalsaChkCryptoMode
balsa_mailbox_conf_get_crypto_mode(BalsaMailboxConfView *view_info)
{
    struct mailbox_conf_combo_box_info *info =
        g_object_get_data(G_OBJECT(view_info->chk_crypt),
                          BALSA_MC_COMBO_BOX_INFO);
    gint active =
        gtk_combo_box_get_active(GTK_COMBO_BOX(view_info->chk_crypt));

    return (LibBalsaChkCryptoMode)
        GPOINTER_TO_INT(g_slist_nth_data(info->tags, active));
}
#endif

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
    gint active;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    if (view_info == NULL)	/* POP3 mailboxes do not have view_info */
	return;

    changed = FALSE;

    if (!mailbox->view)
	/* The mailbox may not have its URL yet, so we can't insert it
	 * into libbalsa_mailbox_view_table yet. */
	mailbox->view = libbalsa_mailbox_view_new();

    active =
        gtk_combo_box_get_active(GTK_COMBO_BOX
                                 (view_info->identity_combo_box));
    if (active >= 0) {
        LibBalsaIdentity *ident =
            g_list_nth_data(balsa_app.identities, active);
        libbalsa_mailbox_set_identity_name(mailbox, ident->identity_name);
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

#ifdef HAVE_GPGME
    if (libbalsa_mailbox_set_crypto_mode(mailbox,
					 balsa_mailbox_conf_get_crypto_mode(view_info)))
	changed = TRUE;
#endif

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
