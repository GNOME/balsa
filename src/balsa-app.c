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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-app.h"
#include "balsa-icons.h"

#include <string.h>
#include <stdlib.h>

#include "filter-funcs.h"
#include "libbalsa-conf.h"
#include "misc.h"
#include "send.h"
#include "server.h"
#include "smtp-server.h"
#include "save-restore.h"

#if HAVE_GCR
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>
#else
#include <gnutls/x509.h>
#endif

#include <glib/gi18n.h>	/* Must come after balsa-app.h. */

/* Global application structure */
struct BalsaApplication balsa_app;

#if HAVE_GCR

static gchar *
ask_password_real(LibBalsaServer *server, const gchar *cert_subject)
{
    static const gchar *remember_password_message[2] = {
        N_("_Remember password in Secret Service"),
        N_("_Remember password")
    };
	GcrPromptDialog *dialog;
	gchar *prompt;
	gboolean remember;
	gchar *passwd;
        const gchar *user;
        const gchar *host;
        const gchar *protocol;
        int use_remember_idx;

    g_return_val_if_fail(server != NULL, NULL);

#if defined(HAVE_LIBSECRET)
    use_remember_idx = libbalsa_conf_use_libsecret() ? 0 : 1;
#else
    use_remember_idx = 1;
#endif                          /* defined(HAVE_LIBSECRET) */
    user = libbalsa_server_get_user(server);
    host = libbalsa_server_get_host(server);
    protocol = libbalsa_server_get_protocol(server);
    if (cert_subject != NULL) {
		/* Translators: #1 Certificate CN, #2 user name; #3 protocol (imap, etc); #4 server name */
    	prompt = g_strdup_printf(_("Password to unlock the user certificate\n%s\nfor user %s, protocol “%s”, server “%s”"),
    		cert_subject, user, protocol, host);
    	remember = libbalsa_server_get_remember_cert_passphrase(server);
    } else {
		/* Translators: #1 user name; #2 protocol (imap, etc); #3 server name */
    	prompt = g_strdup_printf(_("Password for user %s, protocol “%s”, server “%s”"),
                user, protocol, host);
    	remember = libbalsa_server_get_remember_password(server);
    }
	dialog = g_object_new(GCR_TYPE_PROMPT_DIALOG,
						  "use-header-bar", libbalsa_use_headerbar(),
						  "title", _("Password needed"),
						  "cancel-label", _("_Cancel"),
						  "continue-label", _("_OK"),
						  "choice-chosen", remember,
						  "destroy-with-parent", TRUE,
						  "transient-for", GTK_WINDOW(balsa_app.main_window),
						  "modal", TRUE,
						  NULL);
	/* for some strange reason, the following property strings are not displayed when they are
	 * added to the call above - no idea why */
	gcr_prompt_set_message(GCR_PROMPT(dialog), _("Password needed"));
	gcr_prompt_set_description(GCR_PROMPT(dialog), prompt);
	gcr_prompt_set_choice_label(GCR_PROMPT(dialog),remember_password_message[use_remember_idx]);
	g_free(prompt);
	passwd = g_strdup(gcr_prompt_password_run(GCR_PROMPT(dialog), NULL, NULL));
	if (passwd != NULL) {
		gboolean old_remember;

		old_remember = remember;
		remember = gcr_prompt_get_choice_chosen(GCR_PROMPT(dialog));
                if (cert_subject != NULL)
                    libbalsa_server_set_remember_cert_passphrase(server, remember);
                else
                    libbalsa_server_set_remember_password(server, remember);
		libbalsa_server_set_password(server, passwd, cert_subject != NULL);
		if (remember || old_remember) {
			libbalsa_server_config_changed(server);
		}
	}
    gtk_widget_destroy(GTK_WIDGET(dialog));
    return passwd;
}

#else

/* ask_password:
   asks the user for the password to the mailbox on given remote server.
*/
static gchar *
ask_password_real(LibBalsaServer * server, const gchar *cert_subject)
{
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *grid;
	GtkWidget *label;
    GtkWidget *entry;
    GtkWidget *rememb_check;
    gchar *prompt;
    gchar *passwd;
	gboolean remember;
    int use_remember_idx;
    static const gchar *remember_password_message[2] = {
        N_("_Remember password in Secret Service"),
        N_("_Remember password")
    };

    g_return_val_if_fail(server != NULL, NULL);
    if (cert_subject != NULL) {
		/* Translators: #1 Certificate CN, #2 user name; #3 protocol (imap, etc); #4 server name */
    	prompt = g_strdup_printf(_("Password to unlock the user certificate\n%s\nfor user %s, protocol “%s”, server “%s”"),
    		cert_subject, libbalsa_server_get_user(server), libbalsa_server_get_protocol(server), libbalsa_server_get_host(server));
    	remember = libbalsa_server_get_remember_cert_passphrase(server);
    } else {
		/* Translators: #1 user name; #2 protocol (imap, etc); #3 server name */
    	prompt = g_strdup_printf(_("Password for user %s, protocol “%s”, server “%s”"),
    		libbalsa_server_get_user(server), libbalsa_server_get_protocol(server), libbalsa_server_get_host(server));
       	remember = libbalsa_server_get_remember_password(server);
    }

    dialog = gtk_dialog_new_with_buttons(_("Password needed"),
                                         GTK_WINDOW(balsa_app.main_window),
                                         GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
                                         _("_OK"), GTK_RESPONSE_OK,
                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
                                         NULL); 

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 2 * HIG_PADDING);

    grid = libbalsa_create_grid();
    gtk_container_add(GTK_CONTAINER(content), grid);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(prompt), 0, 0, 2, 1);
    g_free(prompt);

    label = libbalsa_create_grid_label(_("Password:"), grid, 1);
    entry = libbalsa_create_grid_entry(grid, NULL, NULL, 1, NULL, label);
    libbalsa_entry_config_passwd(GTK_ENTRY(entry));
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_widget_grab_focus(entry);

#if defined(HAVE_LIBSECRET)
    use_remember_idx = libbalsa_conf_use_libsecret() ? 0 : 1;
#else
    use_remember_idx = 1;
#endif                          /* defined(HAVE_LIBSECRET) */
    rememb_check = libbalsa_create_grid_check(remember_password_message[use_remember_idx], grid, 2, remember);

    gtk_widget_show_all(grid);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		gboolean old_remember;

		old_remember = remember;
		passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
		remember = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rememb_check));
                if (cert_subject != NULL)
                    libbalsa_server_set_remember_cert_passphrase(server, remember);
                else
                    libbalsa_server_set_remember_password(server, remember);
		libbalsa_server_set_password(server, passwd, cert_subject != NULL);
		if (remember || old_remember) {
			libbalsa_server_config_changed(server);
		}
    } else {
    	passwd = NULL;
    }
    gtk_widget_destroy(dialog);
    return passwd;
}

#endif

typedef struct {
    GCond cond;
    LibBalsaServer* server;
    const gchar *cert_subject;
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
    apd->res = ask_password_real(apd->server, apd->cert_subject);
    apd->done = TRUE;
    g_cond_signal(&apd->cond);
    return FALSE;
}

/* ask_password_mt:
   GDK lock must not be held.
*/
static gchar *
ask_password_mt(LibBalsaServer * server, const gchar *cert_subject)
{
    static GMutex ask_passwd_lock;
    AskPasswdData apd;

    g_mutex_lock(&ask_passwd_lock);
    g_cond_init(&apd.cond);
    apd.server = server;
    apd.cert_subject = cert_subject;
    apd.done   = FALSE;
    g_idle_add(ask_passwd_idle, &apd);
    while (!apd.done) {
    	g_cond_wait(&apd.cond, &ask_passwd_lock);
    }
    
    g_cond_clear(&apd.cond);
    g_mutex_unlock(&ask_passwd_lock);
    return apd.res;
}


/* ask_password:
   when called from thread, gdk lock must not be held.
   @param cert_data
*/
gchar *
ask_password(LibBalsaServer *server, const gchar *cert_subject, gpointer user_data)
{
    G_LOCK_DEFINE_STATIC(ask_password);
    gchar *password;

    g_return_val_if_fail(server != NULL, NULL);

    G_LOCK(ask_password);
    if (libbalsa_am_i_subthread()) {
    	password = ask_password_mt(server, cert_subject);
    } else {
    	password = ask_password_real(server, cert_subject);
    }
	G_UNLOCK(ask_password);

	return password;
}


/* Note: data indicates if the function shall be re-scheduled (NULL) or not (!= NULL) */
static gboolean
send_queued_messages_auto_cb(gpointer data)
{
	g_debug("%s: %p", __func__, data);

        if (balsa_app.outbox == NULL)
            return G_SOURCE_REMOVE;

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

    balsa_app.previewpane = TRUE;
    balsa_app.pgdownmod = FALSE;
    balsa_app.pgdown_percent = 50;

    /* GUI settings */
    balsa_app.mblist = NULL;
    balsa_app.mblist_width = 100;

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

#ifdef HAVE_CANBERRA
    balsa_app.notify_new_mail_sound = 0;
#endif

    balsa_app.notify_new_mail_dialog = 0;

#ifdef ENABLE_SYSTRAY
    balsa_app.enable_systray_icon = 0;
#endif

    balsa_app.enable_dkim_checks = 0;

    /* Local and IMAP */
    balsa_app.local_scan_depth = 1;
    balsa_app.check_imap = 1;
    balsa_app.check_imap_inbox = 0;
    balsa_app.imap_scan_depth = 1;

    /* gpgme stuff */
    balsa_app.has_openpgp = FALSE;
    balsa_app.has_smime = FALSE;

    /* Message filing */
    balsa_app.folder_mru=NULL;
    balsa_app.fcc_mru=NULL;

    libbalsa_auto_send_init(send_queued_messages_auto_cb);
}

void
balsa_app_destroy(void)
{
    /* Be paranoid - do not overwrite a "good" config with all-NULL values:
     * the list of identities will never be NULL if the config has been loaded. */
    if (balsa_app.identities != NULL) {
        config_save();
    }

    g_list_free_full(balsa_app.address_book_list, g_object_unref);
    balsa_app.address_book_list = NULL;

    /* now free filters */
    g_slist_foreach(balsa_app.filters, (GFunc)libbalsa_filter_free, 
		    GINT_TO_POINTER(TRUE));
    g_slist_free(balsa_app.filters);
    balsa_app.filters = NULL;

    g_list_free_full(balsa_app.identities, g_object_unref);
    balsa_app.identities = NULL;

    g_list_free_full(balsa_app.inbox_input, g_object_unref);
    balsa_app.inbox_input = NULL;


    g_list_free_full(balsa_app.folder_mru, g_free);
    balsa_app.folder_mru = NULL;

    g_list_free_full(balsa_app.fcc_mru, g_free);
    balsa_app.fcc_mru = NULL;


    g_debug("balsa_app: Finished cleaning up.");
}

static gboolean
check_new_messages_auto_cb(gpointer data)
{
    check_new_messages_real(balsa_app.main_window, TRUE);

    g_debug("Auto-checked for new messages…");

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
                              check_new_messages_auto_cb,
                              NULL) : 0;
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
    g_debug("balsa_open_mailbox_list: opening %s => %p.", url, mailbox);
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

static gint
position_compare_func(gconstpointer a,
                      gconstpointer b)
{
    const gchar *url_a = *(const gchar **) a;
    const gchar *url_b = *(const gchar **) b;

    return config_mailbox_get_position(url_a) - config_mailbox_get_position(url_b);
}

void
balsa_add_open_mailbox_urls(GPtrArray * url_array)
{
    libbalsa_conf_foreach_group(VIEW_BY_URL_SECTION_PREFIX,
                                (LibBalsaConfForeachFunc)
                                append_url_if_open, url_array);

    g_ptr_array_sort(url_array, position_compare_func);
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
    if (balsa_mailbox_node_get_mailbox(mbnode) == bf->data) {
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
    if (balsa_mailbox_node_get_server(mbnode) == bf->server &&
        g_strcmp0(balsa_mailbox_node_get_dir(mbnode), bf->data) == 0) {
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
    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL &&
        strcmp(libbalsa_mailbox_get_url(mailbox), bf->data) == 0) {
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
	mailbox = balsa_mailbox_node_get_mailbox(mbnode);
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
    gchar *short_name = NULL;

    if ((mbnode = balsa_find_url(url)) != NULL) {
        LibBalsaMailbox *mailbox = balsa_mailbox_node_get_mailbox(mbnode);

        if (mailbox != NULL) {
            const gchar *name = libbalsa_mailbox_get_name(mailbox);
            LibBalsaServer *server = balsa_mailbox_node_get_server(mbnode);

            if (server != NULL) {
                short_name = g_strconcat(libbalsa_server_get_host(server), ":",
                                     name, NULL);
            } else {
                short_name = g_strdup(name);
            }
        }
    }

    if (short_name == NULL)
        short_name = g_strdup(url);

    return short_name;
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
    if (mbnode == bf->data || balsa_mailbox_node_get_mailbox(mbnode) == bf->data) {
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
        LibBalsaMailbox *mailbox;

	gtk_tree_model_get(model, &iter, 0, &mbnode, -1);
	mailbox = balsa_mailbox_node_get_mailbox(mbnode);

	if (balsa_mailbox_node_get_parent(mbnode) != NULL) {
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
	    g_debug("sparing %s %s",
		   mailbox != NULL ? "mailbox" : "folder ",
		   mailbox != NULL ?
                   libbalsa_mailbox_get_name(mailbox) :
                   balsa_mailbox_node_get_name(mbnode));
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
    const gchar *name;

	g_debug("Destroying children of %p %s", mbnode,
               (mbnode != NULL &&
                (name = balsa_mailbox_node_get_name(mbnode)) != NULL) ? name : "");

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
    GtkWidget *child;
    guint i;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(GTK_IS_NOTEBOOK(balsa_app.notebook), NULL);

    for (i = 0;
	 (page = gtk_notebook_get_nth_page((GtkNotebook *) balsa_app.notebook, i)) != NULL;
	 i++) {
        child = gtk_bin_get_child(GTK_BIN(page));
	if (child != NULL) {
            BalsaIndex *bindex = BALSA_INDEX(child);
            LibBalsaMailbox *this_mailbox = balsa_index_get_mailbox(bindex);

            if (this_mailbox == mailbox)
                return bindex;
        }
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
        g_clear_pointer(&string, g_free);
        g_clear_pointer(&regex, g_regex_unref);
    }

    if (regex == NULL) {
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

gboolean
balsa_autocrypt_in_use(void)
{
	gboolean result = FALSE;
#ifdef ENABLE_AUTOCRYPT
	GList *ident;

	for (ident = balsa_app.identities; !result && (ident != NULL); ident = ident->next) {
                LibBalsaIdentity *identity = LIBBALSA_IDENTITY(ident->data);
                AutocryptMode autocrypt_mode = libbalsa_identity_get_autocrypt_mode(identity);
		result = autocrypt_mode != AUTOCRYPT_DISABLE;
	}
#endif  /* ENABLE_AUTOCRYPT */
	return result;
}

