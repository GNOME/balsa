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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-app.h"
#include "balsa-icons.h"

#include <string.h>
#include <stdlib.h>

/* for creat(2) */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "filter-funcs.h"
#include "libbalsa-conf.h"
#include "misc.h"
#include "send.h"
#include "server.h"
#include "smtp-server.h"
#include "save-restore.h"

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

#include <glib/gi18n.h>	/* Must come after balsa-app.h. */

/* Global application structure */
struct BalsaApplication balsa_app;

#define HIG_PADDING 12

/* ask_password:
   asks the user for the password to the mailbox on given remote server.
*/
static gchar *
ask_password_real(LibBalsaServer * server, LibBalsaMailbox * mbox)
{
    GtkWidget *dialog, *entry, *rememb;
    GtkWidget *content_area;
    gchar *prompt, *passwd = NULL;
#if defined(HAVE_LIBSECRET)
    static const gchar *remember_password_message =
        N_("_Remember password in Secret Service");
#else
    static const gchar *remember_password_message =
        N_("_Remember password");
#endif                          /* defined(HAVE_LIBSECRET) */

    g_return_val_if_fail(server != NULL, NULL);
    if (mbox)
	prompt =
	    g_strdup_printf(_("Opening remote mailbox %s.\n"
                              "The _password for %s@%s:"),
			    mbox->name, libbalsa_server_get_username(server), libbalsa_server_get_host(server));
    else
	prompt =
	    g_strdup_printf(_("_Password for %s@%s (%s):"), libbalsa_server_get_username(server),
			    libbalsa_server_get_host(server), libbalsa_server_get_protocol(server));

    dialog = gtk_dialog_new_with_buttons(_("Password needed"),
                                         GTK_WINDOW(balsa_app.main_window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
                                         _("_OK"), GTK_RESPONSE_OK,
                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
                                         NULL); 
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(balsa_app.main_window));
#endif
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(content_area), HIG_PADDING);
    gtk_box_pack_start(GTK_BOX(content_area),
                       gtk_label_new_with_mnemonic(prompt));
    g_free(prompt);

    entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(content_area), entry);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 20);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    rememb =  gtk_check_button_new_with_mnemonic(_(remember_password_message));
    gtk_box_pack_start(GTK_BOX(content_area), rememb);
    if(libbalsa_server_get_remember_passwd(server))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rememb), TRUE);

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus (entry);

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        unsigned old_rem = libbalsa_server_get_remember_passwd(server);
        passwd = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);
        libbalsa_server_set_remember_passwd(server,
            !!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rememb)));
        libbalsa_server_set_password(server, passwd);
        if( libbalsa_server_get_remember_passwd(server) || old_rem )
            libbalsa_server_config_changed(server);
    }
    gtk_widget_destroy(dialog);
    return passwd;
}

typedef struct {
    GCond cond;
    LibBalsaServer* server;
    LibBalsaMailbox* mbox;
    gchar* res;
    gboolean done;
} AskPasswdData;

/* ask_passwd_idle:
   called in MT mode by the main thread.
 */
static gboolean
ask_passwd_idle(gpointer data)
{
    AskPasswdData* apd = (AskPasswdData*)data;
    apd->res = ask_password_real(apd->server, apd->mbox);
    apd->done = TRUE;
    g_cond_signal(&apd->cond);
    return FALSE;
}

/* ask_password_mt:
   GDK lock must not be held.
*/
static gchar *
ask_password_mt(LibBalsaServer * server, LibBalsaMailbox * mbox)
{
    static GMutex ask_passwd_lock;
    AskPasswdData apd;

    g_mutex_lock(&ask_passwd_lock);
    g_cond_init(&apd.cond);
    apd.server = server;
    apd.mbox   = mbox;
    apd.done   = FALSE;
    g_idle_add(ask_passwd_idle, &apd);
    while (!apd.done) {
    	g_cond_wait(&apd.cond, &ask_passwd_lock);
    }
    
    g_cond_clear(&apd.cond);
    g_mutex_unlock(&ask_passwd_lock);
    return apd.res;
}

static gboolean
set_passwd_from_matching_server(GtkTreeModel *model,
				GtkTreePath *path,
				GtkTreeIter *iter,
				gpointer data)
{
    LibBalsaServer *server;
    LibBalsaServer *master;
    LibBalsaMailbox *mbox;
    BalsaMailboxNode *node;

    gtk_tree_model_get(model, iter, 0, &node, -1);
    g_return_val_if_fail(node != NULL, FALSE);
    if(node->server) {
        server = node->server;
	g_object_unref(node);
    } else {
        mbox = node->mailbox;
	g_object_unref(node);
        if(!mbox) /* eg. a collection of mboxes */
            return FALSE;
        g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mbox), FALSE);

        if (!LIBBALSA_IS_MAILBOX_REMOTE(mbox)) return FALSE;
        server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mbox);
        g_return_val_if_fail(server != NULL, FALSE);
    }
    g_return_val_if_fail(libbalsa_server_get_host(server) != NULL, FALSE);
    g_return_val_if_fail(libbalsa_server_get_username(server) != NULL, FALSE);
    if (libbalsa_server_get_password(server) == NULL) return FALSE;

    master = (LibBalsaServer *)data;
    g_return_val_if_fail(LIBBALSA_IS_SERVER(master), FALSE);
    if (master == server) return FALSE;

    g_return_val_if_fail(libbalsa_server_get_host(server) != NULL, FALSE);
    g_return_val_if_fail(libbalsa_server_get_username(server) != NULL, FALSE);

    if ((strcmp(libbalsa_server_get_host(server), libbalsa_server_get_host(master)) == 0) &&
	(strcmp(libbalsa_server_get_username(server), libbalsa_server_get_username(master)) == 0)) {
	libbalsa_server_set_password(master, libbalsa_server_get_password(server));
	return TRUE;
    };

    return FALSE;
}
/* ask_password:
*/
gchar *
ask_password(LibBalsaServer *server, LibBalsaMailbox *mbox)
{
    gchar *password;

    g_return_val_if_fail(server != NULL, NULL);

    password = NULL;
    if (mbox) {
	gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			       (GtkTreeModelForeachFunc)
			       set_passwd_from_matching_server, server);

	if (libbalsa_server_get_password(server) != NULL) {
	    password = g_strdup(libbalsa_server_get_password(server));
	    libbalsa_server_set_password(server, NULL);
	}
    }

    if (!password) {
        G_LOCK_DEFINE_STATIC(ask_password);

        G_LOCK(ask_password);
	password = !libbalsa_am_i_subthread() ?
            ask_password_real(server, mbox) : ask_password_mt(server, mbox);
        G_UNLOCK(ask_password);
	return password;
    }
	return password;
}


/* Note: data indicates if the function shall be re-scheduled (NULL) or not (!= NULL) */
static gboolean
send_queued_messages_auto_cb(gpointer data)
{
	g_debug("%s: %p", __func__, data);
	libbalsa_process_queue(balsa_app.outbox, balsa_find_sentbox_by_url, balsa_app.smtp_servers, FALSE, NULL);
    return (data == NULL);
}


void
balsa_app_init(void)
{
    /* 
     * initalize application structure before ALL ELSE 
     * to some reasonable defaults
     */
    balsa_app.identities = NULL;
    balsa_app.current_ident = NULL;
    balsa_app.local_mail_directory = NULL;
    balsa_app.smtp_servers = NULL;
    balsa_app.inbox = NULL;
    balsa_app.inbox_input = NULL;
    balsa_app.outbox = NULL;
    balsa_app.sentbox = NULL;
    balsa_app.draftbox = NULL;
    balsa_app.trash = NULL;

    balsa_app.new_messages_timer = 0;
    balsa_app.new_messages = 0;

    balsa_app.check_mail_auto = TRUE;
    balsa_app.check_mail_timer = 10;

    balsa_app.debug = FALSE;
    balsa_app.previewpane = TRUE;
    balsa_app.pgdownmod = FALSE;
    balsa_app.pgdown_percent = 50;

    /* GUI settings */
    balsa_app.mblist = NULL;
    balsa_app.mblist_width = 100;
    balsa_app.mw_width = MW_DEFAULT_WIDTH;
    balsa_app.mw_height = MW_DEFAULT_HEIGHT;
    balsa_app.mw_maximized = FALSE;

    balsa_app.sw_width = 0;
    balsa_app.sw_height = 0;
    balsa_app.sw_maximized = FALSE;

    balsa_app.toolbar_wrap_button_text = TRUE;
    balsa_app.send_progress_dialog = TRUE;
    balsa_app.recv_progress_dialog = TRUE;
    balsa_app.wordwrap = FALSE; /* default to format=flowed. */
    balsa_app.wraplength = 72;
    balsa_app.browse_wrap = FALSE; /* GtkTextView will wrap for us. */
    balsa_app.browse_wrap_length = 79;
    balsa_app.shown_headers = HEADERS_SELECTED;
    balsa_app.show_all_headers = FALSE;
    balsa_app.selected_headers = g_strdup(DEFAULT_SELECTED_HDRS);
    balsa_app.expand_tree = FALSE;
    balsa_app.show_mblist = TRUE;
    balsa_app.show_notebook_tabs = FALSE;
    balsa_app.layout_type = LAYOUT_DEFAULT;
    balsa_app.view_message_on_open = TRUE;
    balsa_app.ask_before_select = FALSE;
    balsa_app.mw_action_after_move = NEXT_UNREAD;

    balsa_app.index_num_width = NUM_DEFAULT_WIDTH;
    balsa_app.index_status_width = STATUS_DEFAULT_WIDTH;
    balsa_app.index_attachment_width = ATTACHMENT_DEFAULT_WIDTH;
    balsa_app.index_from_width = FROM_DEFAULT_WIDTH;
    balsa_app.index_subject_width = SUBJECT_DEFAULT_WIDTH;
    balsa_app.index_date_width = DATE_DEFAULT_WIDTH;
    balsa_app.index_size_width = SIZE_DEFAULT_WIDTH;

    /* file paths */
    balsa_app.attach_dir = NULL;
    balsa_app.save_dir = NULL;

    /* Mailbox list column width (not fully implemented) */
    balsa_app.mblist_name_width = MBNAME_DEFAULT_WIDTH;

    balsa_app.mblist_show_mb_content_info = FALSE;
    balsa_app.mblist_newmsg_width = NEWMSGCOUNT_DEFAULT_WIDTH;
    balsa_app.mblist_totalmsg_width = TOTALMSGCOUNT_DEFAULT_WIDTH;

    /* arp */
    balsa_app.quote_str = NULL;

    /* quote regex */
    balsa_app.quote_regex = g_strdup(DEFAULT_QUOTE_REGEX);

    /* font */
    balsa_app.use_system_fonts = TRUE;
    balsa_app.message_font = NULL;
    balsa_app.subject_font = NULL;

    /* compose: shown headers */
    balsa_app.compose_headers = NULL;

    /* date format */
    balsa_app.date_string = g_strdup(DEFAULT_DATE_FORMAT);

    /* printing */
    balsa_app.print_settings = gtk_print_settings_new();
    balsa_app.page_setup = gtk_page_setup_new();

    balsa_app.print_header_font = g_strdup(DEFAULT_PRINT_HEADER_FONT);
    balsa_app.print_footer_font = g_strdup(DEFAULT_PRINT_FOOTER_FONT);
    balsa_app.print_body_font   = g_strdup(DEFAULT_PRINT_BODY_FONT);
    balsa_app.print_highlight_cited = FALSE;
    balsa_app.print_highlight_phrases = FALSE;

    /* address book */
    balsa_app.address_book_list = NULL;
    balsa_app.default_address_book = NULL;

    /* Filters */
    balsa_app.filters=NULL;

    /* spell check */
#if HAVE_GSPELL || HAVE_GTKSPELL
    balsa_app.spell_check_lang = NULL;
    balsa_app.spell_check_active = FALSE;
#else                           /* HAVE_GSPELL */
    balsa_app.check_sig = DEFAULT_CHECK_SIG;
    balsa_app.check_quoted = DEFAULT_CHECK_QUOTED;
#endif                          /* HAVE_GSPELL */

    /* Information messages */
    balsa_app.information_message = 0;
    balsa_app.warning_message = 0;
    balsa_app.error_message = 0;
    balsa_app.debug_message = 0;

    balsa_app.notify_new_mail_sound = 1;
    balsa_app.notify_new_mail_dialog = 0;
    balsa_app.notify_new_mail_icon = 1;

    /* Local and IMAP */
    balsa_app.local_scan_depth = 1;
    balsa_app.check_imap = 1;
    balsa_app.check_imap_inbox = 0;
    balsa_app.imap_scan_depth = 1;

#ifdef HAVE_GPGME
    /* gpgme stuff */
    balsa_app.has_openpgp = FALSE;
    balsa_app.has_smime = FALSE;
#endif

    /* Message filing */
    balsa_app.folder_mru=NULL;
    balsa_app.fcc_mru=NULL;

    libbalsa_auto_send_init(send_queued_messages_auto_cb);
}

void
balsa_app_destroy(void)
{
    config_save();

    libbalsa_clear_list(&balsa_app.address_book_list, g_object_unref);

    /* now free filters */
    g_slist_foreach(balsa_app.filters, (GFunc)libbalsa_filter_free,
		    GINT_TO_POINTER(TRUE));
    g_clear_pointer(&balsa_app.filters, (GDestroyNotify) g_slist_free);

    libbalsa_clear_list(&balsa_app.identities, g_object_unref);
    libbalsa_clear_list(&balsa_app.folder_mru, g_free);
    libbalsa_clear_list(&balsa_app.fcc_mru, g_free);

    if(balsa_app.debug) g_print("balsa_app: Finished cleaning up.\n");
}

static gboolean
check_new_messages_auto_cb(gpointer data)
{
    check_new_messages_real(balsa_app.main_window, TRUE);

    if (balsa_app.debug)
        fprintf(stderr, "Auto-checked for new messages…\n");

    /*  preserver timer */
    return TRUE;
}


void
update_timer(gboolean update, guint minutes)
{
    libbalsa_clear_source_id(&balsa_app.check_mail_timer_id);

    if (update) {
        balsa_app.check_mail_timer_id =
            g_timeout_add_seconds(minutes * 60, check_new_messages_auto_cb,
                                  NULL);
    }
}


/*
 * balsa_open_mailbox_list:
 * Called on startup if remember_open_mboxes is set, and also after
 * rescanning.
 * Frees the passed argument when done.
 */

static gboolean
append_url_if_open(const gchar * group, const gchar * encoded_url,
                   GPtrArray * array)
{
    gchar *url;

    url = libbalsa_urldecode(encoded_url);

    if (config_mailbox_was_open(url))
        g_ptr_array_add(array, url);
    else
        g_free(url);

    return FALSE;
}

static void
open_mailbox_by_url(const gchar * url, gboolean hidden)
{
    LibBalsaMailbox *mailbox;

    mailbox = balsa_find_mailbox_by_url(url);
    if (balsa_app.debug)
        fprintf(stderr, "balsa_open_mailbox_list: opening %s => %p..\n",
                url, mailbox);
    if (mailbox) {
        if (hidden)
            balsa_mblist_open_mailbox_hidden(mailbox);
        else
            balsa_mblist_open_mailbox(mailbox);
    } else {
        /* Do not try to open it next time. */
        LibBalsaMailboxView *view = config_load_mailbox_view(url);
        /* The mailbox may have been requested to be open because its
         * stored view might say so or the user requested it from the
         * command line - in which case, view may or may not be present.
         * We will be careful here. */
        if (view) {
            view->open = FALSE;
            view->in_sync = FALSE;
            config_save_mailbox_view(url, view);
            libbalsa_mailbox_view_free(view);
        }
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Couldn’t open mailbox “%s”"), url);
    }
}

void
balsa_open_mailbox_list(gchar ** urls)
{
    gboolean hidden = FALSE;
    gchar **tmp;

    g_return_if_fail(urls != NULL);

    for (tmp = urls; *tmp; ++tmp) {
        gchar **p;

        /* Have we already seen this URL? */
        for (p = urls; p < tmp; ++p)
            if (!strcmp(*p, *tmp))
                break;
        if (p == tmp) {
            open_mailbox_by_url(*tmp, hidden);
            hidden = TRUE;
        }
    }

    g_strfreev(urls);
}

void
balsa_add_open_mailbox_urls(GPtrArray * url_array)
{
    libbalsa_conf_foreach_group(VIEW_BY_URL_SECTION_PREFIX,
                                (LibBalsaConfForeachFunc)
                                append_url_if_open, url_array);
}

/* 
 * Utilities for searching a GNode tree of BalsaMailboxNodes
 *
 * First a structure for the search info
 */
struct _BalsaFind {
    gconstpointer data;
    LibBalsaServer   *server;
    BalsaMailboxNode *mbnode;
};
typedef struct _BalsaFind BalsaFind;

static gint
find_mailbox(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
	     gpointer user_data)
{
    BalsaFind *bf = user_data;
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    if (mbnode->mailbox == bf->data) {
	bf->mbnode = mbnode;
	return TRUE;
    }
    g_object_unref(mbnode);

    return FALSE;
}

/* balsa_find_mailbox:
   looks for given mailbox in the GNode tree, usually but not limited to
   balsa_app.mailbox_nodes; caller must unref mbnode if non-NULL.
*/
BalsaMailboxNode *
balsa_find_mailbox(LibBalsaMailbox * mailbox)
{
    BalsaFind bf;

    bf.data = mailbox;
    bf.mbnode = NULL;
    if (balsa_app.mblist_tree_store)
        gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                               find_mailbox, &bf);

    return bf.mbnode;
}

/* balsa_find_dir:
   looks for a mailbox node with dir equal to path.
   returns NULL on failure; caller must unref mbnode when non-NULL.
*/
static gint
find_path(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
	  BalsaFind * bf)
{
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    if (mbnode->server == bf->server &&
        g_strcmp0(mbnode->dir, (const gchar *) bf->data) == 0) {
	bf->mbnode = mbnode;
	return TRUE;
    }
    g_object_unref(mbnode);

    return FALSE;
}

BalsaMailboxNode *
balsa_find_dir(LibBalsaServer *server, const gchar * path)
{
    BalsaFind bf;

    bf.data = path;
    bf.server = server;
    bf.mbnode = NULL;
    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc) find_path, &bf);

    return bf.mbnode;
}

static gint
find_url(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
	 BalsaFind * bf)
{
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    if ((mailbox = mbnode->mailbox) && !strcmp(mailbox->url, bf->data)) {
        bf->mbnode = mbnode;
        return TRUE;
    }
    g_object_unref(mbnode);

    return FALSE;
}

/* balsa_find_url:
 * looks for a mailbox node with the given url.
 * returns NULL on failure; caller must unref mbnode when non-NULL.
 */

BalsaMailboxNode *
balsa_find_url(const gchar * url)
{
    BalsaFind bf;

    bf.data = url;
    bf.mbnode = NULL;

    if (balsa_app.mblist_tree_store)
        g_object_ref(balsa_app.mblist_tree_store);
    /*
     * Check again, in case the main thread managed to finalize
     * balsa_app.mblist_tree_store between the check and the object-ref.
     */
    if (balsa_app.mblist_tree_store) {
        gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                               (GtkTreeModelForeachFunc) find_url,
                               &bf);
        g_object_unref(balsa_app.mblist_tree_store);
    }

    return bf.mbnode;
}

/* balsa_find_mailbox_by_url:
 * looks for a mailbox with the given url.
 * returns NULL on failure
 */
LibBalsaMailbox *
balsa_find_mailbox_by_url(const gchar * url)
{
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox = NULL;

    if ((mbnode = balsa_find_url(url))) {
	mailbox = mbnode->mailbox;
	g_object_unref(mbnode);
    }
    return mailbox;
}

LibBalsaMailbox*
balsa_find_sentbox_by_url(const gchar *url)
{
    LibBalsaMailbox *res = balsa_find_mailbox_by_url(url);
    return res ? res : balsa_app.sentbox;
}

gchar*
balsa_get_short_mailbox_name(const gchar *url)
{
    BalsaMailboxNode *mbnode;

    if ((mbnode = balsa_find_url(url)) && mbnode->mailbox) {
        if (mbnode->server) {
            return g_strconcat(libbalsa_server_get_host(mbnode->server), ":",
                               mbnode->mailbox->name, NULL);
        } else {
            return g_strdup(mbnode->mailbox->name);
        }
    }
    return g_strdup(url);
}

struct balsa_find_iter_by_data_info {
    GtkTreeIter *iter;
    gpointer data;
    gboolean found;
};

static gboolean
balsa_find_iter_by_data_func(GtkTreeModel * model, GtkTreePath * path,
			       GtkTreeIter * iter, gpointer user_data)
{
    struct balsa_find_iter_by_data_info *bf = user_data;
    BalsaMailboxNode *mbnode = NULL;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    if(!mbnode)
        return FALSE;
    if (mbnode == bf->data || mbnode->mailbox == bf->data) {
	*bf->iter = *iter;
	bf->found = TRUE;
    }
    g_object_unref(mbnode);

    return bf->found;
}

gboolean
balsa_find_iter_by_data(GtkTreeIter * iter , gpointer data)
{
    struct balsa_find_iter_by_data_info bf;
    GtkTreeModel *model;

    /* We may call it from initial config, it's ok for
       mblist_tree_store not to exist. */
    if(!balsa_app.mblist_tree_store)
        return FALSE;

    model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);

    bf.iter = iter;
    bf.data = data;
    bf.found = FALSE;
    gtk_tree_model_foreach(model, balsa_find_iter_by_data_func, &bf);

    return bf.found;
}

/* End of search utilities. */

/* balsa_remove_children_mailbox_nodes:
   remove all children of given node leaving the node itself intact.
 */
static void
ba_remove_children_mailbox_nodes(GtkTreeModel * model, GtkTreeIter * parent,
				 GSList ** specials)
{
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;
    gboolean valid;

    if (!gtk_tree_model_iter_children(model, &iter, parent))
	return;

    do {
	gtk_tree_model_get(model, &iter, 0, &mbnode, -1);
	if (mbnode->parent) {
	    LibBalsaMailbox *mailbox = mbnode->mailbox;
	    if (mailbox == balsa_app.inbox
		|| mailbox == balsa_app.outbox
		|| mailbox == balsa_app.sentbox
		|| mailbox == balsa_app.draftbox
		|| mailbox == balsa_app.trash) {
		g_object_ref(mailbox);
		*specials = g_slist_prepend(*specials, mailbox);
	    }
	    ba_remove_children_mailbox_nodes(model, &iter, specials);
	    valid =
		gtk_tree_store_remove(balsa_app.mblist_tree_store, &iter);
	} else {
	    printf("sparing %s %s\n",
		   mbnode->mailbox ? "mailbox" : "folder ",
		   mbnode->mailbox ? mbnode->mailbox->name : mbnode->name);
	    valid = gtk_tree_model_iter_next(model, &iter);
	}
	g_object_unref(mbnode); 
    } while (valid);
}

void
balsa_remove_children_mailbox_nodes(BalsaMailboxNode * mbnode)
{
    GtkTreeModel *model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);
    GtkTreeIter parent;
    GtkTreeIter *iter = NULL;
    GSList *specials = NULL, *l;

    if (balsa_app.debug)
	printf("Destroying children of %p %s\n",
	       mbnode, mbnode && mbnode->name ? mbnode->name : "");

    if (mbnode && balsa_find_iter_by_data(&parent, mbnode))
	iter = &parent;

    ba_remove_children_mailbox_nodes(model, iter, &specials);

    for (l = specials; l; l = l->next)
        balsa_mblist_mailbox_node_append(NULL,
                                         balsa_mailbox_node_new_from_mailbox
                                         (l->data));
    g_slist_free(specials);
}

/* balsa_find_index_by_mailbox:
   returns BalsaIndex displaying passed mailbox, or NULL, if mailbox is 
   not displayed.
*/
BalsaIndex*
balsa_find_index_by_mailbox(LibBalsaMailbox * mailbox)
{
    GtkWidget *page;
    GtkWidget *index;
    guint i;
    g_return_val_if_fail(balsa_app.notebook, NULL);

    for (i = 0;
	 (page =
	  gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
	 i++) {
        index = gtk_bin_get_child(GTK_BIN(page));
	if (index && BALSA_INDEX(index)->mailbox_node
            && BALSA_INDEX(index)->mailbox_node->mailbox == mailbox)
	    return BALSA_INDEX(index);
    }

    /* didn't find a matching mailbox */
    return NULL;
}

GRegex *
balsa_quote_regex_new(void)
{
    static GRegex *regex  = NULL;
    static gchar  *string = NULL;

    if (g_strcmp0(string, balsa_app.quote_regex) != 0) {
        /* We have not initialized the GRegex, or balsa_app.quote_regex
         * has changed. */
        g_clear_pointer(&string, (GDestroyNotify) g_free);
        g_clear_pointer(&regex,  (GDestroyNotify) g_regex_unref);
    }

    if (regex == NULL) {
        GError *err = NULL;

        regex = g_regex_new(balsa_app.quote_regex, 0, 0, &err);
        if (err != NULL) {
            g_warning("quote regex compilation failed: %s", err->message);
            g_error_free(err);
            return NULL;
        }
        string = g_strdup(balsa_app.quote_regex);
    }

    return g_regex_ref(regex);
}
