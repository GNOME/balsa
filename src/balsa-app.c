/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2005 Stuart Parmenter and others,
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-app.h"

#include <string.h>
#include <stdlib.h>
#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

/* for creat(2) */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "filter-funcs.h"
#include "misc.h"
#include "server.h"
#include "smtp-server.h"
#include "save-restore.h"

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

#include <glib/gi18n.h>	/* Must come after balsa-app.h. */

/* Global application structure */
struct BalsaApplication balsa_app;

#if !HAVE_GTKSPELL
const gchar *pspell_modules[] = {
    "ispell",
    "aspell"
};

const gchar *pspell_suggest_modes[] = {
    "fast",
    "normal",
    "bad-spellers"
};
#endif                          /* HAVE_GTKSPELL */

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
#if defined(HAVE_GNOME_KEYRING)
    static const gchar *remember_password_message =
        N_("_Remember password in keyring");
#else
    static const gchar *remember_password_message =
        N_("_Remember password");
#endif

    g_return_val_if_fail(server != NULL, NULL);
    if (mbox)
	prompt =
	    g_strdup_printf(_("Opening remote mailbox %s.\n"
                              "The _password for %s@%s:"),
			    mbox->name, server->user, server->host);
    else
	prompt =
	    g_strdup_printf(_("_Password for %s@%s (%s):"), server->user,
			    server->host, server->protocol);

    dialog = gtk_dialog_new_with_buttons(_("Password needed"),
                                         GTK_WINDOW(balsa_app.main_window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         NULL); 
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(balsa_app.main_window));
#endif
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(content_area), HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(content_area),
                      gtk_label_new_with_mnemonic(prompt));
    g_free(prompt);
    gtk_container_add(GTK_CONTAINER(content_area),
                      entry = gtk_entry_new());
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 20);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    rememb =  gtk_check_button_new_with_mnemonic(_(remember_password_message));
    gtk_container_add(GTK_CONTAINER(content_area), rememb);
    if(server->remember_passwd)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rememb), TRUE);

    gtk_widget_show_all(content_area);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus (entry);

    if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        unsigned old_rem = server->remember_passwd;
        passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
        server->remember_passwd = 
            !!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rememb));
        libbalsa_server_set_password(server, passwd);
        if( server->remember_passwd || old_rem )
            libbalsa_server_config_changed(server);
    }
    gtk_widget_destroy(dialog);
    return passwd;
}

#ifdef BALSA_USE_THREADS
typedef struct {
    pthread_cond_t cond;
    LibBalsaServer* server;
    LibBalsaMailbox* mbox;
    gchar* res;
} AskPasswdData;

/* ask_passwd_idle:
   called in MT mode by the main thread.
 */
static gboolean
ask_passwd_idle(gpointer data)
{
    AskPasswdData* apd = (AskPasswdData*)data;
    gdk_threads_enter();
    apd->res = ask_password_real(apd->server, apd->mbox);
    gdk_threads_leave();
    pthread_cond_signal(&apd->cond);
    return FALSE;
}

/* ask_password_mt:
   GDK lock must not be held.
*/
static gchar *
ask_password_mt(LibBalsaServer * server, LibBalsaMailbox * mbox)
{
    static pthread_mutex_t ask_passwd_lock = PTHREAD_MUTEX_INITIALIZER;
    AskPasswdData apd;

    pthread_mutex_lock(&ask_passwd_lock);
    pthread_cond_init(&apd.cond, NULL);
    apd.server = server;
    apd.mbox   = mbox;
    g_idle_add(ask_passwd_idle, &apd);
    pthread_cond_wait(&apd.cond, &ask_passwd_lock);
    
    pthread_cond_destroy(&apd.cond);
    pthread_mutex_unlock(&ask_passwd_lock);
    return apd.res;
}
#endif

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
        server = LIBBALSA_MAILBOX_REMOTE_SERVER(mbox);
        g_return_val_if_fail(server != NULL, FALSE);
    }
    g_return_val_if_fail(server->host != NULL, FALSE);
    g_return_val_if_fail(server->user != NULL, FALSE);
    if (server->passwd == NULL) return FALSE;

    master = (LibBalsaServer *)data;
    g_return_val_if_fail(LIBBALSA_IS_SERVER(master), FALSE);
    if (master == server) return FALSE;

    g_return_val_if_fail(server->host != NULL, FALSE);
    g_return_val_if_fail(server->user != NULL, FALSE);

    if ((strcmp(server->host, master->host) == 0) &&
	(strcmp(server->user, master->user) == 0)) {
	g_free(master->passwd);
	master->passwd = g_strdup(server->passwd);
	return TRUE;
    };
    
    return FALSE;
}
/* ask_password:
   when called from thread, gdk lock must not be held.
*/
gchar *
ask_password(LibBalsaServer *server, LibBalsaMailbox *mbox)
{
    gchar *password;

    g_return_val_if_fail(server != NULL, NULL);
    
    password = NULL;
    if (mbox) {
	gboolean is_sub_thread = libbalsa_am_i_subthread();

	if (is_sub_thread)
	    gdk_threads_enter();
	gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			       (GtkTreeModelForeachFunc)
			       set_passwd_from_matching_server, server);
	if (is_sub_thread)
	    gdk_threads_leave();

	if (server->passwd != NULL) {
	    password = server->passwd;
	    server->passwd = NULL;
	}
    }

    if (!password)
#ifdef BALSA_USE_THREADS
	return (pthread_self() == libbalsa_get_main_thread()) ?
            ask_password_real(server, mbox) : ask_password_mt(server, mbox);
#else
	return ask_password_real(server, mbox);
#endif
    else
	return password;
}

#if ENABLE_ESMTP
static void
authapi_exit (void)
{
    g_slist_foreach(balsa_app.smtp_servers, (GFunc) g_object_unref, NULL);
    g_slist_free(balsa_app.smtp_servers);
    balsa_app.smtp_servers = NULL;
    auth_client_exit ();
}
#endif /* ESMTP */

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

#if ENABLE_ESMTP
    balsa_app.smtp_servers = NULL;

    /* Do what's needed at application level to allow libESMTP
       to use authentication.  */
    auth_client_init ();
    atexit (authapi_exit);
#endif

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
    balsa_app.pwindow_option = WHILERETR;
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
    balsa_app.message_font = NULL;
    balsa_app.subject_font = NULL;

    /* compose: shown headers */
    balsa_app.compose_headers = NULL;

    /* command line options */
#if defined(ENABLE_TOUCH_UI)
    balsa_app.open_inbox_upon_startup = TRUE;
#endif /* ENABLE_TOUCH_UI */

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
#if HAVE_GTKSPELL
    balsa_app.spell_check_lang = NULL;
    balsa_app.spell_check_active = FALSE;
#else                           /* HAVE_GTKSPELL */
    balsa_app.module = SPELL_CHECK_MODULE_ASPELL;
    balsa_app.suggestion_mode = SPELL_CHECK_SUGGEST_NORMAL;
    balsa_app.ignore_size = 0;
    balsa_app.check_sig = DEFAULT_CHECK_SIG;

    spell_check_modules_name = pspell_modules;
    spell_check_suggest_mode_name = pspell_suggest_modes;
#endif                          /* HAVE_GTKSPELL */

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

    g_object_set(gtk_settings_get_for_screen(gdk_screen_get_default()),
                 "gtk-fallback-icon-theme", "gnome", NULL);
}

void
balsa_app_destroy(void)
{
    config_views_save();
    config_save();

    g_list_foreach(balsa_app.address_book_list, (GFunc)g_object_unref, NULL);
    g_list_free(balsa_app.address_book_list);
    balsa_app.address_book_list = NULL;

    /* now free filters */
    g_slist_foreach(balsa_app.filters, (GFunc)libbalsa_filter_free, 
		    GINT_TO_POINTER(TRUE));
    g_slist_free(balsa_app.filters);
    balsa_app.filters = NULL;


    g_list_foreach(balsa_app.identities, (GFunc)g_object_unref, NULL);
    g_list_free(balsa_app.identities);
    balsa_app.identities = NULL;


    if(balsa_app.debug) g_print("balsa_app: Finished cleaning up.\n");
}

static gint
check_new_messages_auto_cb(gpointer data)
{
    check_new_messages_real(balsa_app.main_window, TYPE_BACKGROUND);

    if (balsa_app.debug)
        fprintf(stderr, "Auto-checked for new messages...\n");

    /*  preserver timer */
    return TRUE;
}


void
update_timer(gboolean update, guint minutes)
{
    if (balsa_app.check_mail_timer_id)
        g_source_remove(balsa_app.check_mail_timer_id);

    balsa_app.check_mail_timer_id = update ?
        g_timeout_add_seconds(minutes * 60,
                              (GSourceFunc) check_new_messages_auto_cb,
                              NULL) : 0;
}



/* open_mailboxes_idle_cb:
   open mailboxes on startup if requested so.
   This is an idle handler. Be sure to use gdk_threads_{enter/leave}
   Release the passed argument when done.
 */

static void
append_url_if_open(const gchar * url, LibBalsaMailboxView * view,
                   GPtrArray * array)
{
    if (view->open)
        g_ptr_array_add(array, g_strdup(url));
}

static void
open_mailbox_by_url(const gchar * url, gboolean hidden)
{
    LibBalsaMailbox *mailbox;

    if (!(url && *url))
        return;

    mailbox = balsa_find_mailbox_by_url(url);
    if (balsa_app.debug)
        fprintf(stderr, "open_mailboxes_idle_cb: opening %s => %p..\n",
                url, mailbox);
    if (mailbox) {
        if (hidden)
            balsa_mblist_open_mailbox_hidden(mailbox);
        else
            balsa_mblist_open_mailbox(mailbox);
    } else {
        /* Do not try to open it next time. */
        LibBalsaMailboxView *view =
            g_hash_table_lookup(libbalsa_mailbox_view_table, url);
        /* The mailbox may have been requested to be open because its
         * stored view might say so or the user requested it from the
         * command line - in which case, view may or may not be present.
         * We will be careful here. */
        if (view) {
            view->open = FALSE;
            view->in_sync = FALSE;
        }
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Couldn't open mailbox \"%s\""), url);
    }
}

gboolean
open_mailboxes_idle_cb(gchar ** urls)
{
    gchar **tmp;

    gdk_threads_enter();

    if (!urls) {
        GPtrArray *array;

        if (!libbalsa_mailbox_view_table) {
            gdk_threads_leave();
            return FALSE;
        }

        array = g_ptr_array_new();
        g_hash_table_foreach(libbalsa_mailbox_view_table,
                             (GHFunc) append_url_if_open, array);
        g_ptr_array_add(array, NULL);
        urls = (gchar **) g_ptr_array_free(array, FALSE);
    }

    if (urls) {
        if (*urls) {
            open_mailbox_by_url(balsa_app.current_mailbox_url, FALSE);

            for (tmp = urls; *tmp; ++tmp)
                if (!balsa_app.current_mailbox_url
                    || strcmp(*tmp, balsa_app.current_mailbox_url))
                    open_mailbox_by_url(*tmp, TRUE);
        }

        g_strfreev(urls);
    }

    gdk_threads_leave();

    return FALSE;
}

GtkWidget *
balsa_stock_button_with_label(const char *icon, const char *text)
{
    GtkWidget *button;
    GtkWidget *pixmap = gtk_image_new_from_stock(icon, GTK_ICON_SIZE_BUTTON);
    GtkWidget *align = gtk_alignment_new(0.5, 0.5, 0, 0);
    GtkWidget *hbox = gtk_hbox_new(FALSE, 0);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), align);
    gtk_container_add(GTK_CONTAINER(align), hbox);

    gtk_box_pack_start(GTK_BOX(hbox), pixmap, FALSE, FALSE, 0);
    if (text && *text) {
        GtkWidget *label = gtk_label_new_with_mnemonic(text);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);
        gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 2);
    }

    gtk_widget_show_all(button);
    return button;
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

    gdk_threads_enter();

    bf.data = mailbox;
    bf.mbnode = NULL;
    if (balsa_app.mblist_tree_store)
        gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                               find_mailbox, &bf);

    gdk_threads_leave();

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
        mbnode->dir && !strcmp(mbnode->dir, bf->data)) {
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
    gboolean is_sub_thread = libbalsa_am_i_subthread();

    if (is_sub_thread)
	gdk_threads_enter();

    bf.data = path;
    bf.server = server;
    bf.mbnode = NULL;
    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc) find_path, &bf);

    if (is_sub_thread)
	gdk_threads_leave();

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

    gboolean is_sub_thread = libbalsa_am_i_subthread();

    if (is_sub_thread)
	gdk_threads_enter();

    bf.data = url;
    bf.mbnode = NULL;
    if (balsa_app.mblist_tree_store)
        gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                               (GtkTreeModelForeachFunc) find_url, &bf);
    if (is_sub_thread)
	gdk_threads_leave();

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

#if USE_GREGEX
GRegex *
balsa_quote_regex_new(void)
{
    static GRegex *regex  = NULL;
    static gchar  *string = NULL;

    if (string && strcmp(string, balsa_app.quote_regex) != 0) {
        g_free(string);
        string = NULL;
        g_regex_unref(regex);
        regex = NULL;
    }

    if (!regex) {
        GError *err = NULL;

        regex = g_regex_new(balsa_app.quote_regex, 0, 0, &err);
        if (err) {
            g_warning("quote regex compilation failed: %s", err->message);
            g_error_free(err);
            return NULL;
        }
        string = g_strdup(balsa_app.quote_regex);
    }

    return g_regex_ref(regex);
}
#endif                          /* USE_GREGEX */
