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
#include "imap-server.h"

struct _BalsaMailboxConfView {
    GtkWindow *window;
    gchar     *identity_name;
    GtkWidget *identity_label;
    GtkWidget *show_from;
    GtkWidget *show_to;
#ifdef HAVE_GPGME
    GtkWidget *chk_crypt;
#endif
};
typedef struct _MailboxConfWindow MailboxConfWindow;
struct _MailboxConfWindow {
    LibBalsaMailbox *mailbox;

    GtkDialog *window;

    void (*ok_handler)(MailboxConfWindow*);
    GtkWidget *mailbox_name;
    GtkType mailbox_type;
    BalsaMailboxConfView *view_info;
    gboolean ok_sensitive;

    union {
	/* for local mailboxes */
	struct local { 
	    GnomeFileEntry *path;
	} local;
	/* for imap mailboxes & directories */
	struct {
	    GtkWidget *port;
	    GtkWidget *username;
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

/* ========================================================= */
/* BEGIN BalsaServerConf =================================== */
struct menu_data {
    const char *label;
    int tag;
};
#if GTK_CHECK_VERSION(2, 4, 0)
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
mailbox_conf_combo_box_make(GtkComboBox * combo_box, unsigned cnt,
                            const struct menu_data *data)
{
    struct mailbox_conf_combo_box_info *info =
        g_new(struct mailbox_conf_combo_box_info, 1);
    gint i;

    info->tags = NULL;
    for (i = cnt; --i >= 0;) {
        gtk_combo_box_prepend_text(combo_box, _(data[i].label));
        info->tags =
            g_slist_prepend(info->tags, GINT_TO_POINTER(data[i].tag));
    }
    g_object_set_data_full(G_OBJECT(combo_box), BALSA_MC_COMBO_BOX_INFO,
                           info,
                           (GDestroyNotify)
                           mailbox_conf_combo_box_info_free);
}

#else /* GTK_CHECK_VERSION(2, 4, 0) */
static GtkWidget*
get_combo_menu(unsigned cnt, const struct menu_data *data)
{
    GtkWidget *menu       = gtk_menu_new();
    unsigned i;
    for(i=0; i<cnt; i++) {
        GtkWidget *menu_item =
            gtk_menu_item_new_with_label(_(data[i].label));
        g_object_set_data(G_OBJECT(menu_item), "balsa-data",
                          GINT_TO_POINTER(data[i].tag));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    }
    gtk_widget_show_all(menu);
    return menu;
}
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

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
    gboolean use_ssl = s && s->use_ssl;

    bsc->table = GTK_TABLE(gtk_table_new(3+extra_rows, 2, FALSE));
    bsc->used_rows = 0;

    bsc->use_ssl = balsa_server_conf_add_checkbox(bsc, _("Use _SSL"));
    if(use_ssl)
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bsc->use_ssl), TRUE);

    label = gtk_label_new_with_mnemonic(_("Use _TLS"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(bsc->table, label, 0, 1, 1, 2,
		     GTK_FILL, GTK_FILL, 0, 0);

#if GTK_CHECK_VERSION(2, 4, 0)
    bsc->tls_option = gtk_combo_box_new_text();
    mailbox_conf_combo_box_make(GTK_COMBO_BOX(bsc->tls_option),
                                ELEMENTS(tls_menu), tls_menu);
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsc->tls_option),
                             s ? s->tls_mode : LIBBALSA_TLS_ENABLED);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    bsc->tls_option = gtk_option_menu_new ();
    bsc->tls_mode = get_combo_menu(ELEMENTS(tls_menu), tls_menu);
    gtk_option_menu_set_menu (GTK_OPTION_MENU(bsc->tls_option),
			      bsc->tls_mode);
    gtk_option_menu_set_history (GTK_OPTION_MENU (bsc->tls_option),
				 s ? s->tls_mode : LIBBALSA_TLS_ENABLED);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_table_attach(bsc->table, bsc->tls_option, 1, 2, 1, 2,
		     GTK_FILL, GTK_FILL, 5, 5);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), bsc->tls_option);

    g_signal_connect(G_OBJECT (bsc->use_ssl), "toggled",
                     G_CALLBACK (bsc_ssl_toggled_cb), bsc);
    gtk_widget_show_all(GTK_WIDGET(bsc->table));
    bsc->used_rows = 2;
    gtk_widget_set_sensitive(bsc->tls_option, !use_ssl);
    return (GtkWidget*)bsc->table;
}

GtkWidget*
balsa_server_conf_add_checkbox(BalsaServerConf *bsc,
                               const char *label)
{
    GtkWidget *checkbox;
    GtkWidget *lbl = gtk_label_new_with_mnemonic(label);
    gtk_misc_set_alignment(GTK_MISC(lbl), 1.0, 0.5);
    gtk_table_attach(bsc->table, lbl, 0, 1,
                     bsc->used_rows, bsc->used_rows+1,
		     GTK_FILL, GTK_FILL, 5, 5);
    checkbox = gtk_check_button_new();
    gtk_table_attach(bsc->table, checkbox, 1, 2,
                     bsc->used_rows, bsc->used_rows+1,
		     GTK_FILL, GTK_FILL, 5, 5);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), checkbox);
    bsc->used_rows++;
    return checkbox;
}

GtkWidget*
balsa_server_conf_add_spinner(BalsaServerConf *bsc,
                              const char *label, gint lo, gint hi, gint step,
                              gint initial_value)
{
    GtkWidget *spin_button;
    GtkWidget *lbl = gtk_label_new_with_mnemonic(label);
    gtk_misc_set_alignment(GTK_MISC(lbl), 1.0, 0.5);
    gtk_table_attach(bsc->table, lbl, 0, 1,
                     bsc->used_rows, bsc->used_rows+1,
		     GTK_FILL, GTK_FILL, 5, 5);
    spin_button = gtk_spin_button_new_with_range(lo, hi, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button),
			      (float) initial_value);

    gtk_table_attach(bsc->table, spin_button, 1, 2,
                     bsc->used_rows, bsc->used_rows+1,
		     GTK_FILL, GTK_FILL, 5, 5);
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
#if GTK_CHECK_VERSION(2, 4, 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(bsc->tls_option),
                             server->tls_mode);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_option_menu_set_history (GTK_OPTION_MENU (bsc->tls_option),
				 server->tls_mode);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
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
#if GTK_CHECK_VERSION(2, 4, 0)
    struct mailbox_conf_combo_box_info *info =
        g_object_get_data(G_OBJECT(bsc->tls_option),
                          BALSA_MC_COMBO_BOX_INFO);
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(bsc->tls_option));

    return (LibBalsaTlsMode)
        GPOINTER_TO_INT(g_slist_nth_data(info->tags, active));
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    GtkWidget *menu_item = gtk_menu_get_active(GTK_MENU(bsc->tls_mode));
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item),
                                             "balsa-data"));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
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
    balsa_mailbox_node_show_prop_dialog(mbnode);
    g_object_unref(mbnode);
}

/* END OF COMMONLY USED CALLBACKS SECTION ------------------------ */
void
mailbox_conf_delete(BalsaMailboxNode * mbnode)
{
    gint button;
    GtkWidget *ask;
    LibBalsaMailbox* mailbox = mbnode->mailbox;

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
	while (!parent->mailbox && parent->parent) {
	    mbnode = parent;
	    parent = parent->parent;
	}
	balsa_mblist_mailbox_node_remove(mbnode);
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
    } else
	balsa_mblist_mailbox_node_remove(mbnode);
    update_mail_servers();
}

static void
conf_response_cb(GtkDialog* dialog, gint response, MailboxConfWindow * mcw)
{
    switch(response) {
    case GTK_RESPONSE_OK: mcw->ok_handler(mcw); 
        /* fall through */
    default:
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void
ident_updated_cb(BalsaMailboxConfView * view_info, LibBalsaIdentity * identity)
{
    if(identity) {
	g_free(view_info->identity_name);
	view_info->identity_name = g_strdup(identity->identity_name);
	gtk_label_set_text(GTK_LABEL(view_info->identity_label),
                           view_info->identity_name);
    }
}

static void
ident_change_button_cb(GtkWidget * widget, BalsaMailboxConfView * view_info)
{
    LibBalsaIdentity* ident = balsa_app.current_ident;
    GList *l;
    if(view_info->identity_name) {
	for(l=balsa_app.identities; l; l = l = g_list_next(l))
	    if( strcmp(view_info->identity_name, 
		       LIBBALSA_IDENTITY(l->data)->identity_name)==0) {
		ident = LIBBALSA_IDENTITY(l->data);
		break;
	    }
    }
		
    libbalsa_identity_select_dialog(GTK_WINDOW(view_info->window),
				    _("Select Identity"),
				    balsa_app.identities,
				    ident,
				    (LibBalsaIdentityCallback)ident_updated_cb,
				    view_info);
}

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
    if (mbnode)
        mcw->mailbox = mbnode->mailbox;
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
    g_object_weak_ref(G_OBJECT(mcw->window), (GWeakNotify) g_free, mcw);
    
    gtk_dialog_set_response_sensitive(mcw->window, GTK_RESPONSE_OK, FALSE);
    mcw->ok_sensitive = FALSE;
    gtk_dialog_set_default_response(mcw->window, GTK_RESPONSE_OK);

    page = create_page(mcw);
    gtk_box_pack_start(GTK_BOX(mcw->window->vbox),
		       page, TRUE, TRUE, 0);

    if(mbnode)
        mailbox_conf_set_values(mcw);

    g_signal_connect(G_OBJECT(mcw->window), "response", 
                     G_CALLBACK(conf_response_cb), mcw);
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
	    gnome_file_entry_set_filename(mcw->mb_data.local.path,
					  libbalsa_mailbox_local_get_path(local));
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
	if (!*gtk_entry_get_text(GTK_ENTRY(gnome_file_entry_gtk_entry
                                           (mcw->mb_data.local.path))))
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

    gtk_dialog_set_response_sensitive(mcw->window, GTK_RESPONSE_OK, sensitive);
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
	g_signal_connect_swapped(server, "set-host",
                                 G_CALLBACK(config_mailbox_update),
				 mailbox);
    }
    g_free(LIBBALSA_MAILBOX(mailbox)->name);
    fill_in_imap_data(mcw, &LIBBALSA_MAILBOX(mailbox)->name, &path);
    libbalsa_server_set_username(server,
				 gtk_entry_get_text(GTK_ENTRY
						    (mcw->mb_data.imap.username)));
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
	filename = gnome_file_entry_get_full_path(mcw->mb_data.local.path,
						  FALSE);
	path = g_strdup(libbalsa_mailbox_local_get_path(mailbox));
        if (strcmp(filename, path)) {
            /* rename */
	    gchar *file_dir, *path_dir;

            i = libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL
                                                (mailbox), filename);
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
	    balsa_window_update_tab(mbnode);
	} else
	    g_free(name);

	g_object_unref(mbnode);
        g_free(filename);
	g_free(path);
    } else if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	update_pop_mailbox(mcw);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	update_imap_mailbox(mcw);
	/* Calling libbalsa_server_set_host already triggered an update,
	 * so we do not need to here: */
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
	int res;

	path = gnome_file_entry_get_full_path(GNOME_FILE_ENTRY(mcw->mb_data.local.path), FALSE);
	res = libbalsa_mailbox_local_set_path(ml, path);

	if( res != 0) {
	    g_object_unref(G_OBJECT(mcw->mailbox));
	    mcw->mailbox = NULL;
	    return;
	}

	save_to_config = balsa_app.local_mail_directory == NULL
	    || strncmp(balsa_app.local_mail_directory, path,
		       strlen(balsa_app.local_mail_directory)) != 0;
        printf("Save to config: %d\n", save_to_config);
	mcw->mailbox->name = g_path_get_basename(path);
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
    gint row = -1;

    table = gtk_table_new(3, 2, FALSE);

    /* mailbox name */
    if(mcw->mailbox && BALSA_IS_MAILBOX_SPECIAL(mcw->mailbox)) {
        label = create_label(_("Mailbox _Name:"), table, ++row);
        mcw->mailbox_name = 
            create_entry(mcw->window, table,
                         GTK_SIGNAL_FUNC(check_for_blank_fields),
                         mcw, row, NULL, label);
    } else mcw->mailbox_name = NULL;

    /* path to file */
    label = create_label(_("Mailbox _Path:"), table, ++row);

    file = gnome_file_entry_new("MailboxPath", _("Mailbox Path"));
    mcw->mb_data.local.path = GNOME_FILE_ENTRY(file);

    gtk_label_set_mnemonic_widget(GTK_LABEL(label), file);
    gnome_file_entry_set_default_path(GNOME_FILE_ENTRY(file),
                                      balsa_app.local_mail_directory);

    g_signal_connect(G_OBJECT(mcw->mb_data.local.path), "changed",
		     G_CALLBACK(check_for_blank_fields), mcw);

    /* FIXME: set_dafault_path does not activate unless the entry is
     * changed */
    gnome_file_entry_set_filename(GNOME_FILE_ENTRY(file),"filename");

    gtk_table_attach(GTK_TABLE(table), file, 1, 2, row, row + 1,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    mcw->view_info =
        mailbox_conf_view_new(mcw->mailbox, GTK_WINDOW(mcw->window),
                              table, ++row);

    gtk_widget_grab_focus(mcw->mailbox_name ? mcw->mailbox_name :
			  GTK_WIDGET(mcw->mb_data.local.path));
    return table;
}

static GtkWidget *
create_pop_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *notebook, *table, *label, *advanced;

    notebook = gtk_notebook_new();
    table = gtk_table_new(9, 2, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new_with_mnemonic(_("_Basic")));

    /* mailbox name */
    label = create_label(_("Mailbox _Name:"), table, 0);
    mcw->mailbox_name = create_entry(mcw->window, table,
				     GTK_SIGNAL_FUNC(check_for_blank_fields),
				     mcw, 0, NULL, label);
    /* pop server */
    label = create_label(_("_Server:"), table, 1);
    mcw->mb_data.pop3.bsc.server = create_entry(mcw->window, table,
				     GTK_SIGNAL_FUNC(check_for_blank_fields),
				     mcw, 1, "localhost", label);
    mcw->mb_data.pop3.bsc.default_ports = POP3_DEFAULT_PORTS;


    /* username  */
    label= create_label(_("Use_r name:"), table, 3);
    mcw->mb_data.pop3.username = create_entry(mcw->window, table,
				     GTK_SIGNAL_FUNC(check_for_blank_fields),
				     mcw, 3, g_get_user_name(), label);

    /* password field */
    label = create_label(_("Pass_word:"), table, 4);
    mcw->mb_data.pop3.password = create_entry(mcw->window, table, 
					      NULL, NULL, 4, NULL, label);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.pop3.password), FALSE);

    /* toggle for deletion from server */
    mcw->mb_data.pop3.delete_from_server = 
	create_check(mcw->window, 
		     _("_Delete messages from server after download"),
		     table, 6, TRUE);

    /* Procmail */
    mcw->mb_data.pop3.filter = 
	create_check(mcw->window, _("_Filter messages through procmail"),
		     table, 7, FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.pop3.filter), "toggled", 
                     G_CALLBACK(pop3_enable_filter_cb), mcw);
    label = create_label(_("Fi_lter Command:"), table, 8);
    mcw->mb_data.pop3.filter_cmd =
	create_entry(mcw->window, table,
		     GTK_SIGNAL_FUNC(check_for_blank_fields),
		     mcw, 8, "procmail -f -", label);

    /* toggle for check */
    mcw->mb_data.pop3.check = 
	create_check(mcw->window, _("_Enable check for new mail"), 
		     table, 9, TRUE);

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
    return notebook;
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
        gtk_dialog_response(GTK_DIALOG(mcw->window), GTK_RESPONSE_OK);
}

static GtkWidget *
create_imap_mailbox_page(MailboxConfWindow *mcw)
{
    GtkWidget *notebook, *advanced, *table;
    GtkWidget *label;
    GtkWidget *entry;
    gint row = -1;

    notebook = gtk_notebook_new();
    table = gtk_table_new(7, 2, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new_with_mnemonic(_("_Basic")));

    /* mailbox name */
    label = create_label(_("Mailbox _Name:"), table, ++row);
    mcw->mailbox_name =  create_entry(mcw->window, table,
                                      GTK_SIGNAL_FUNC(check_for_blank_fields),
                                      mcw, row, NULL, label);

    /* imap server */
    label = create_label(_("_Server:"), table, ++row);
    mcw->mb_data.imap.bsc.server = 
	create_entry(mcw->window, table,
		     GTK_SIGNAL_FUNC(check_for_blank_fields),
		     mcw, row, "localhost", label);
    mcw->mb_data.imap.bsc.default_ports = IMAP_DEFAULT_PORTS;

    /* username  */
    label = create_label(_("_Username:"), table, ++row);
    mcw->mb_data.imap.username = 
	create_entry(mcw->window, table,
		     GTK_SIGNAL_FUNC(check_for_blank_fields),
		     mcw, row, g_get_user_name(), label);

    /* toggle for remember password */
    mcw->mb_data.imap.remember = 
	create_check(mcw->window, _("_Remember Password"), table, ++row,
		     FALSE);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.remember), "toggled",
                     G_CALLBACK(remember_toggle_cb), mcw);

   /* password field */
    label = create_label(_("Pass_word:"), table, ++row);
    mcw->mb_data.imap.password = 
	create_entry(mcw->window, table, NULL, NULL, row, NULL, label);
    gtk_entry_set_visibility(GTK_ENTRY(mcw->mb_data.imap.password), FALSE);

    label = create_label(_("F_older Path:"), table, ++row);

#if GTK_CHECK_VERSION(2, 4, 0)
    mcw->mb_data.imap.folderpath = entry = gtk_entry_new();
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    entry = gnome_entry_new("IMAPFolderHistory");
    mcw->mb_data.imap.folderpath = gnome_entry_gtk_entry(GNOME_ENTRY(entry));
    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX.Sent");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX.Draft");
    gnome_entry_append_history(GNOME_ENTRY(entry), 1, "INBOX.outbox");
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_entry_set_text(GTK_ENTRY(mcw->mb_data.imap.folderpath), "INBOX");

    gtk_label_set_mnemonic_widget(GTK_LABEL(label), 
                                  mcw->mb_data.imap.folderpath);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.folderpath), "activate",
                     G_CALLBACK(entry_activated), mcw);
    g_signal_connect(G_OBJECT(mcw->mb_data.imap.folderpath), "changed",
                     G_CALLBACK(check_for_blank_fields), mcw);

    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, row, row + 1,
		     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

    mcw->view_info = mailbox_conf_view_new(mcw->mailbox,
                                           GTK_WINDOW(mcw->window),
                                           table, ++row);

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
    return notebook;
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
    g_free(view_info->identity_name);
    g_free(view_info);
}

/* Create the dialog items in the dialog's table, and allocate and
 * populate a BalsaMailboxConfView with the info that needs to be passed
 * around. The memory is deallocated when the window is finalized. 
 *
 * mailbox:     the mailbox whose properties are being displayed;
 * window:      the dialog, which will be the transient parent of the
 *              identity dialog, if needed, and also owns the
 *              BalsaMailboxConfView.
 * table:       the table in which to place the widgets;
 * row:         the row of the table in which to start.
 */
BalsaMailboxConfView *
mailbox_conf_view_new(LibBalsaMailbox * mailbox,
                      GtkWindow * window, GtkWidget * table, gint row)
{
    BalsaMailboxConfView *view_info;
    GtkWidget *box;
    GtkWidget *button;
    const gchar *identity_name;

    view_info = g_new(BalsaMailboxConfView, 1);
    g_object_weak_ref(G_OBJECT(window),
                      (GWeakNotify) mailbox_conf_view_free, view_info);
    view_info->window = window;

    create_label(_("Identity:"), table, row);
    box = gtk_hbox_new(FALSE, 12);
    view_info->identity_name = NULL;
    identity_name = libbalsa_mailbox_get_identity_name(mailbox);
    view_info->identity_label =
	gtk_label_new(identity_name ? identity_name :
		      _("(No identity set)"));
    gtk_box_pack_start(GTK_BOX(box), view_info->identity_label, TRUE, TRUE,
                       0);

    button = gtk_button_new_with_mnemonic(_("C_hange..."));
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(ident_change_button_cb), view_info);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, TRUE, 0);
    gtk_table_attach(GTK_TABLE(table), box, 1, 2, row, row + 1,
                     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 5);

    create_label(_("Show address:"), table, ++row);
    box = gtk_hbox_new(FALSE, 12);
    view_info->show_from = button =
        gtk_radio_button_new_with_mnemonic(NULL, _("_From"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    view_info->show_to = button =
        gtk_radio_button_new_with_mnemonic(gtk_radio_button_get_group
                                           (GTK_RADIO_BUTTON(button)),
                                           _("_To"));
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    gtk_table_attach(GTK_TABLE(table), box, 1, 2, row, row + 1,
                     GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

    button = (libbalsa_mailbox_get_show(mailbox) == LB_MAILBOX_SHOW_TO ?
	      view_info->show_to : view_info->show_from);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);

#ifdef HAVE_GPGME
    {
	/* scope */
	static const struct menu_data chk_crypt_menu[] = {
	    { N_("Never"),       LB_MAILBOX_CHK_CRYPT_NEVER  },
	    { N_("If Possible"), LB_MAILBOX_CHK_CRYPT_MAYBE  },
	    { N_("Always"),      LB_MAILBOX_CHK_CRYPT_ALWAYS }
	};
#if !GTK_CHECK_VERSION(2, 4, 0)
	GtkWidget * chk_crypt_w;
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
	
	create_label(_("decrypt and check\nsignatures automatically"), table, ++row);
	
	box = gtk_hbox_new(FALSE, 12);
#if GTK_CHECK_VERSION(2, 4, 0)
        view_info->chk_crypt = gtk_combo_box_new_text();
        mailbox_conf_combo_box_make(GTK_COMBO_BOX(view_info->chk_crypt),
                                    ELEMENTS(chk_crypt_menu),
                                    chk_crypt_menu);
        gtk_combo_box_set_active(GTK_COMBO_BOX(view_info->chk_crypt),
                                 libbalsa_mailbox_get_crypto_mode
                                 (mailbox));
        gtk_box_pack_start(GTK_BOX(box), view_info->chk_crypt, FALSE,
                           FALSE, 0);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
	chk_crypt_w = gtk_option_menu_new ();
	view_info->chk_crypt = get_combo_menu(ELEMENTS(chk_crypt_menu), chk_crypt_menu);
	gtk_option_menu_set_menu (GTK_OPTION_MENU(chk_crypt_w),
				  view_info->chk_crypt);
	gtk_option_menu_set_history (GTK_OPTION_MENU (chk_crypt_w),
				     libbalsa_mailbox_get_crypto_mode(mailbox));
	gtk_box_pack_start(GTK_BOX(box), chk_crypt_w, FALSE, FALSE, 0);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
	gtk_table_attach(GTK_TABLE(table), box, 1, 2, row, row + 1,
			 GTK_FILL, 0, 0, 0);
    }
#endif

    return view_info;
}

#ifdef HAVE_GPGME
static LibBalsaChkCryptoMode
balsa_mailbox_conf_get_crypto_mode(BalsaMailboxConfView *view_info)
{
#if GTK_CHECK_VERSION(2, 4, 0)
    struct mailbox_conf_combo_box_info *info =
        g_object_get_data(G_OBJECT(view_info->chk_crypt),
                          BALSA_MC_COMBO_BOX_INFO);
    gint active =
        gtk_combo_box_get_active(GTK_COMBO_BOX(view_info->chk_crypt));

    return (LibBalsaChkCryptoMode)
        GPOINTER_TO_INT(g_slist_nth_data(info->tags, active));
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    GtkWidget *menu_item = gtk_menu_get_active(GTK_MENU(view_info->chk_crypt));
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item),
                                             "balsa-data"));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
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

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    if (view_info == NULL)	/* POP3 mailboxes do not have view_info */
	return;

    changed = FALSE;

    if (!mailbox->view)
	/* The mailbox may not have its URL yet, so we can't insert it
	 * into libbalsa_mailbox_view_table yet. */
	mailbox->view = libbalsa_mailbox_view_new();

    if (view_info->identity_name) {
	libbalsa_mailbox_set_identity_name(mailbox,
                                           view_info->identity_name);
	g_free(view_info->identity_name);
	view_info->identity_name = NULL;
    }

    if (libbalsa_mailbox_set_show(mailbox,
				  gtk_toggle_button_get_active
				  (GTK_TOGGLE_BUTTON(view_info->show_from))
				  ? LB_MAILBOX_SHOW_FROM
				  : LB_MAILBOX_SHOW_TO))
	changed = TRUE;

#ifdef HAVE_GPGME
    if (libbalsa_mailbox_set_crypto_mode(mailbox,
					 balsa_mailbox_conf_get_crypto_mode(view_info)))
	changed = TRUE;
#endif

    if (!changed || !libbalsa_mailbox_get_open(mailbox))
	return;

    /* Redraw the mailbox; we temporarily increase its open_ref to keep
     * the backend open. */
    libbalsa_mailbox_open(mailbox, NULL);
    balsa_mblist_close_mailbox(mailbox);
    balsa_mblist_open_mailbox(mailbox);
    libbalsa_mailbox_close(mailbox, FALSE);
}
