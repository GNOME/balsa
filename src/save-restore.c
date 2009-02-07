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
#include "config.h"

#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <string.h>
#if HAVE_GNOME
#include <gconf/gconf-client.h>
#endif
#include <glib/gi18n.h>
#include "balsa-app.h"
#include "save-restore.h"
#include "server.h"
#include "quote-color.h"
#include "toolbar-prefs.h"

#include "filter.h"
#include "filter-file.h"
#include "filter-funcs.h"
#include "mailbox-filter.h"
#include "libbalsa-conf.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#if ENABLE_ESMTP
#include "smtp-server.h"
#endif                          /* ENABLE_ESMTP */

#define FOLDER_SECTION_PREFIX "folder-"
#define MAILBOX_SECTION_PREFIX "mailbox-"
#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"
#define IDENTITY_SECTION_PREFIX "identity-"
#define VIEW_SECTION_PREFIX "view-"
#define VIEW_BY_URL_SECTION_PREFIX "viewByUrl-"
#define SMTP_SERVER_SECTION_PREFIX "smtp-server-"

static gint config_global_load(void);
static gint config_folder_init(const gchar * group);
static gint config_mailbox_init(const gchar * group);
static gchar *config_get_unused_group(const gchar * group);

static void save_color(gchar * key, GdkColor * color);
static void load_color(gchar * key, GdkColor * color);
static void save_mru(GList  *mru, const gchar * group);
static void load_mru(GList **mru, const gchar * group);

static void config_address_books_save(void);
static void config_identities_load(void);

static void check_for_old_sigs(GList * id_list_tmp);

static void config_filters_load(void);

#define folder_section_path(mn) \
    BALSA_MAILBOX_NODE(mn)->config_prefix ? \
    g_strdup(BALSA_MAILBOX_NODE(mn)->config_prefix) : \
    config_get_unused_group(FOLDER_SECTION_PREFIX)

#define mailbox_section_path(mbox) \
    LIBBALSA_MAILBOX(mbox)->config_prefix ? \
    g_strdup(LIBBALSA_MAILBOX(mbox)->config_prefix) : \
    config_get_unused_group(MAILBOX_SECTION_PREFIX)

#define address_book_section_path(ab) \
    LIBBALSA_ADDRESS_BOOK(ab)->config_prefix ? \
    g_strdup(LIBBALSA_ADDRESS_BOOK(ab)->config_prefix) : \
    config_get_unused_group(ADDRESS_BOOK_SECTION_PREFIX)

gint config_load(void)
{
    return config_global_load();
}

/* This function initializes all the mailboxes internally, going through
   the list of all the mailboxes in the configuration file one by one. */

static gboolean
config_load_section(const gchar * key, const gchar * value, gpointer data)
{
    gint(*cb) (const gchar *) = data;

    cb(key);                    /* FIXME do something about error?? */

    return FALSE;
}

void
config_load_sections(void)
{
    libbalsa_conf_foreach_group(MAILBOX_SECTION_PREFIX,
                                config_load_section,
                                config_mailbox_init);
    libbalsa_conf_foreach_group(FOLDER_SECTION_PREFIX,
                                config_load_section,
                                config_folder_init);
}

static gint
d_get_gint(const gchar * key, gint def_val)
{
    gint def;
    gint res = libbalsa_conf_get_int_with_default(key, &def);
    return def ? def_val : res;
}

/* save/load_toolbars:
   handle customized toolbars for main/message preview and compose windows.
*/

static void
save_toolbars(void)
{
    libbalsa_conf_remove_group("Toolbars");
    libbalsa_conf_push_group("Toolbars");
    libbalsa_conf_set_int("WrapButtonText",
                         balsa_app.toolbar_wrap_button_text);
    libbalsa_conf_pop_group();
}

static void
load_toolbars(void)
{
    libbalsa_conf_push_group("Toolbars");
    balsa_app.toolbar_wrap_button_text = d_get_gint("WrapButtonText", 1);
    libbalsa_conf_pop_group();
}

/* config_mailbox_set_as_special:
   allows to set given mailboxe as one of the special mailboxes
   PS: I am not sure if I should add outbox to the list.
   specialNames must be in sync with the specialType definition.

   WARNING: may destroy mailbox.
*/
#if defined(ENABLE_TOUCH_UI)
#define INBOX_NAME   "In"
#define SENTBOX_NAME "Sent"
#define DRAFTS_NAME  "Drafts"
#define OUTBOX_NAME  "Out"
#else
#define INBOX_NAME   "Inbox"
#define SENTBOX_NAME "Sentbox"
#define DRAFTS_NAME  "Draftbox"
#define OUTBOX_NAME  "Outbox"
#endif /* ENABLE_TOUCH_UI */
#define TRASH_NAME "Trash"

static gchar *specialNames[] = {
    INBOX_NAME, SENTBOX_NAME, TRASH_NAME, DRAFTS_NAME, OUTBOX_NAME
};
void
config_mailbox_set_as_special(LibBalsaMailbox * mailbox, specialType which)
{
    LibBalsaMailbox **special;
    BalsaMailboxNode *mbnode;

    g_return_if_fail(mailbox != NULL);

    switch (which) {
    case SPECIAL_INBOX:
	special = &balsa_app.inbox;
	break;
    case SPECIAL_SENT:
	special = &balsa_app.sentbox;
	break;
    case SPECIAL_TRASH:
	special = &balsa_app.trash;
	break;
    case SPECIAL_DRAFT:
	special = &balsa_app.draftbox;
	break;
    default:
	return;
    }
    if (*special) {
	g_free((*special)->config_prefix); 
	(*special)->config_prefix = NULL;
        if (!LIBBALSA_IS_MAILBOX_LOCAL(*special)
            || !libbalsa_path_is_below_dir(libbalsa_mailbox_local_get_path
                                           (*special),
                                           balsa_app.local_mail_directory))
            config_mailbox_add(*special, NULL);
	g_object_remove_weak_pointer(G_OBJECT(*special), (gpointer) special);

	mbnode = balsa_find_mailbox(*special);
	*special = NULL;
	balsa_mblist_mailbox_node_redraw(mbnode);
	g_object_unref(mbnode);
    }
    config_mailbox_delete(mailbox);
    config_mailbox_add(mailbox, specialNames[which]);

    *special = mailbox;
    g_object_add_weak_pointer(G_OBJECT(*special), (gpointer) special);

    mbnode = balsa_find_mailbox(mailbox);
    balsa_mblist_mailbox_node_redraw(mbnode);
    g_object_unref(mbnode);

    switch(which) {
    case SPECIAL_SENT: 
	balsa_mblist_mru_add(&balsa_app.fcc_mru, mailbox->url); break;
    case SPECIAL_TRASH:
        libbalsa_filters_set_trash(balsa_app.trash); break;
    default: break;
    }
}

void
config_address_book_save(LibBalsaAddressBook * ab)
{
    gchar *group;

    group = address_book_section_path(ab);

    libbalsa_address_book_save_config(ab, group);

    g_free(group);

    libbalsa_conf_sync();
}

void
config_address_book_delete(LibBalsaAddressBook * ab)
{
    if (ab->config_prefix) {
	libbalsa_conf_remove_group(ab->config_prefix);
	libbalsa_conf_private_remove_group(ab->config_prefix);
	libbalsa_conf_sync();
    }
}

/* config_mailbox_add:
   adds the specifed mailbox to the configuration. If the mailbox does
   not have the unique pkey assigned, find one.
*/
gint config_mailbox_add(LibBalsaMailbox * mailbox, const char *key_arg)
{
    gchar *tmp;
    g_return_val_if_fail(mailbox, FALSE);

    if (key_arg == NULL)
	tmp = mailbox_section_path(mailbox);
    else
	tmp = g_strdup_printf(MAILBOX_SECTION_PREFIX "%s", key_arg);

    libbalsa_conf_remove_group(tmp);
    libbalsa_conf_push_group(tmp);
    libbalsa_mailbox_save_config(mailbox, tmp);
    libbalsa_conf_pop_group();
    g_free(tmp);

    libbalsa_conf_sync();
    return TRUE;
}				/* config_mailbox_add */

/* config_folder_add:
   adds the specifed folder to the configuration. If the folder does
   not have the unique pkey assigned, find one.
*/
gint config_folder_add(BalsaMailboxNode * mbnode, const char *key_arg)
{
    gchar *tmp;
    g_return_val_if_fail(mbnode, FALSE);

    if (key_arg == NULL)
	tmp = folder_section_path(mbnode);
    else
	tmp = g_strdup_printf(FOLDER_SECTION_PREFIX "%s", key_arg);

    libbalsa_conf_push_group(tmp);
    balsa_mailbox_node_save_config(mbnode, tmp);
    libbalsa_conf_pop_group();
    g_free(tmp);

    libbalsa_conf_sync();
    return TRUE;
}				/* config_mailbox_add */

/* removes from the configuration only */
gint config_mailbox_delete(const LibBalsaMailbox * mailbox)
{
    gchar *tmp;			/* the key in the mailbox section name */
    gint res;
    tmp = mailbox_section_path(mailbox);
    res = libbalsa_conf_has_group(tmp);
    libbalsa_conf_remove_group(tmp);
    libbalsa_conf_sync();
    g_free(tmp);
    return res;
}				/* config_mailbox_delete */

gint
config_folder_delete(const BalsaMailboxNode * mbnode)
{
    gchar *tmp;			/* the key in the mailbox section name */
    gint res;
    tmp = folder_section_path(mbnode);
    res = libbalsa_conf_has_group(tmp);
    libbalsa_conf_remove_group(tmp);
    libbalsa_conf_sync();
    g_free(tmp);
    return res;
}				/* config_folder_delete */

/* Update the configuration information for the specified mailbox. */
gint config_mailbox_update(LibBalsaMailbox * mailbox)
{
    gchar *key;			/* the key in the mailbox section name */
    gint res;

    key = mailbox_section_path(mailbox);
    res = libbalsa_conf_has_group(key);
    libbalsa_conf_remove_group(key);
    libbalsa_conf_private_remove_group(key);
    libbalsa_conf_push_group(key);
    libbalsa_mailbox_save_config(mailbox, key);
    g_free(key);
    libbalsa_conf_pop_group();
    libbalsa_conf_sync();
    return res;
}				/* config_mailbox_update */

/* Update the configuration information for the specified folder. */
gint config_folder_update(BalsaMailboxNode * mbnode)
{
    gchar *key;			/* the key in the mailbox section name */
    gint res;

    key = folder_section_path(mbnode);
    res = libbalsa_conf_has_group(key);
    libbalsa_conf_remove_group(key);
    libbalsa_conf_private_remove_group(key);
    libbalsa_conf_push_group(key);
    balsa_mailbox_node_save_config(mbnode, key);
    g_free(key);
    libbalsa_conf_pop_group();
    libbalsa_conf_sync();
    return res;
}				/* config_folder_update */

static void
pop3_progress_notify(LibBalsaMailbox* mailbox, int msg_type, int prog, int tot,
                     const char* msg)
{
#ifdef BALSA_USE_THREADS
    MailThreadMessage *message;

    message = g_new(MailThreadMessage, 1);
    message->message_type = msg_type;
    memcpy(message->message_string, msg, strlen(msg) + 1);
    message->num_bytes = prog;
    message->tot_bytes = tot;

    /* FIXME: There is potential for a timeout with 
       * the server here, if we don't get the lock back
       * soon enough.. But it prevents the main thread from
       * blocking on the mutt_lock, andthe pipe filling up.
       * This would give us a deadlock.
     */
    if (write(mail_thread_pipes[1], (void *) &message, sizeof(void *))
        != sizeof(void *))
        g_warning("pipe error");
#else
    while(gtk_events_pending())
        gtk_main_iteration_do(FALSE);
#endif
}

/* Initialize the specified mailbox, creating the internal data
   structures which represent the mailbox. */
static void
sr_special_notify(gpointer data, GObject * mailbox)
{
    LibBalsaMailbox **special = data;

    if (special == &balsa_app.trash && !balsa_app.mblist_tree_store
        && balsa_app.empty_trash_on_exit)
        empty_trash(balsa_app.main_window);

    *special = NULL;
}

static gint
config_mailbox_init(const gchar * prefix)
{
    LibBalsaMailbox *mailbox;
    const gchar *key = prefix + strlen(MAILBOX_SECTION_PREFIX);

    g_return_val_if_fail(prefix != NULL, FALSE);

    mailbox = libbalsa_mailbox_new_from_config(prefix);
    if (mailbox == NULL)
	return FALSE;
    if (LIBBALSA_IS_MAILBOX_REMOTE(mailbox)) {
	LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
        libbalsa_server_connect_signals(server,
                                        G_CALLBACK(ask_password), mailbox);
	g_signal_connect_swapped(server, "config-changed",
                                 G_CALLBACK(config_mailbox_update),
				 mailbox);
    }

    if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
        g_signal_connect(G_OBJECT(mailbox),
                         "progress-notify", G_CALLBACK(pop3_progress_notify),
                         mailbox);
	balsa_app.inbox_input =
	    g_list_append(balsa_app.inbox_input, 
			  balsa_mailbox_node_new_from_mailbox(mailbox));
    } else {
        LibBalsaMailbox **special = NULL;
	BalsaMailboxNode *mbnode;

	mbnode = balsa_mailbox_node_new_from_mailbox(mailbox);
	if (strcmp(INBOX_NAME, key) == 0)
	    special = &balsa_app.inbox;
	else if (strcmp(OUTBOX_NAME, key) == 0) {
	    special = &balsa_app.outbox;
            mailbox->no_reassemble = TRUE;
        } else if (strcmp(SENTBOX_NAME, key) == 0)
	    special = &balsa_app.sentbox;
	else if (strcmp(DRAFTS_NAME, key) == 0)
	    special = &balsa_app.draftbox;
	else if (strcmp(TRASH_NAME, key) == 0) {
	    special = &balsa_app.trash;
            libbalsa_filters_set_trash(mailbox);
	}

	if (special) {
	    /* Designate as special before appending to the mblist, so
	     * that view->show gets set correctly for sentbox, draftbox,
	     * and outbox, and view->subscribe gets set correctly for
             * trashbox. */
	    *special = mailbox;
            g_object_weak_ref(G_OBJECT(mailbox),
                              (GWeakNotify) sr_special_notify, special);
	}

        balsa_mblist_mailbox_node_append(NULL, mbnode);
    }
    return TRUE;
}				/* config_mailbox_init */


/* Initialize the specified folder, creating the internal data
   structures which represent the folder. */
static gint
config_folder_init(const gchar * prefix)
{
    BalsaMailboxNode *folder;

    g_return_val_if_fail(prefix != NULL, FALSE);

    if( (folder = balsa_mailbox_node_new_from_config(prefix)) ) {
	g_signal_connect_swapped(folder->server, "config-changed",
                                 G_CALLBACK(config_folder_update),
				 folder);
	balsa_mblist_mailbox_node_append(NULL, folder);
    }

    return folder != NULL;
}				/* config_folder_init */

/* Load Balsa's global settings */
static gboolean
config_warning_idle(const gchar * text)
{
    gdk_threads_enter();
    balsa_information(LIBBALSA_INFORMATION_WARNING, "%s", text);
    gdk_threads_leave();
    return FALSE;
}

#if ENABLE_ESMTP
static gboolean
config_load_smtp_server(const gchar * key, const gchar * value, gpointer data)
{
    GSList **smtp_servers = data;
    LibBalsaSmtpServer *smtp_server;

    libbalsa_conf_push_group(key);
    smtp_server = libbalsa_smtp_server_new_from_config(value);
    libbalsa_server_connect_signals(LIBBALSA_SERVER(smtp_server),
				    G_CALLBACK(ask_password), NULL);
    libbalsa_conf_pop_group();
    libbalsa_smtp_server_add_to_list(smtp_server, smtp_servers);

    return FALSE;
}
#endif                          /* ENABLE_ESMTP */

#ifdef HAVE_GTK_PRINT
static gboolean
load_gtk_print_setting(const gchar * key, const gchar * value, gpointer data)
{
    g_return_val_if_fail(data != NULL, TRUE);
    gtk_print_settings_set((GtkPrintSettings *)data, key, value);
    return FALSE;
}

static GtkPageSetup *
restore_gtk_page_setup()
{
    GtkPageSetup *page_setup;
    GtkPaperSize *paper_size = NULL;
    gdouble width;
    gdouble height;
    gchar *name;
    gchar *ppd_name;
    gchar *display_name;

    width = libbalsa_conf_get_double("Width");
    height = libbalsa_conf_get_double("Height");

    name = libbalsa_conf_get_string("PaperName");
    ppd_name = libbalsa_conf_get_string ("PPDName");
    display_name = libbalsa_conf_get_string("DisplayName");
    page_setup = gtk_page_setup_new();

    if (ppd_name && *ppd_name)
	paper_size = gtk_paper_size_new_from_ppd(ppd_name, display_name,
						 width, height);
    else if (name && *name)
	paper_size = gtk_paper_size_new_custom(name, display_name,
					       width, height, GTK_UNIT_MM);

    /* set defaults if no info is available */
    if (!paper_size) {
	paper_size = gtk_paper_size_new(GTK_PAPER_NAME_A4);
	gtk_page_setup_set_paper_size_and_default_margins(page_setup,
							  paper_size);
    } else {
	gtk_page_setup_set_paper_size(page_setup, paper_size);

	gtk_page_setup_set_top_margin(page_setup,
				      libbalsa_conf_get_double("MarginTop"),
				      GTK_UNIT_MM);
	gtk_page_setup_set_bottom_margin(page_setup,
					 libbalsa_conf_get_double("MarginBottom"),
					 GTK_UNIT_MM);
	gtk_page_setup_set_left_margin(page_setup,
				       libbalsa_conf_get_double("MarginLeft"),
				       GTK_UNIT_MM);
	gtk_page_setup_set_right_margin(page_setup,
					libbalsa_conf_get_double("MarginRight"),
					GTK_UNIT_MM);
	gtk_page_setup_set_orientation(page_setup,
				       (GtkPageOrientation)
				       libbalsa_conf_get_int("Orientation"));
    }
    
    gtk_paper_size_free(paper_size);
    g_free(ppd_name);
    g_free(name);
    g_free(display_name);
    
    return page_setup;
}
#endif

static gint
config_global_load(void)
{
    gboolean def_used;
    guint tmp;
    static gboolean new_user = FALSE;

    config_address_books_load();
#if ENABLE_ESMTP
    /* Load SMTP servers before identities. */
    libbalsa_conf_foreach_group(SMTP_SERVER_SECTION_PREFIX,
	                        config_load_smtp_server,
	                        &balsa_app.smtp_servers);
#endif                          /* ENABLE_ESMTP */

    /* We must load filters before mailboxes, because they refer to the filters list */
    config_filters_load();
    if (filter_errno!=FILTER_NOERR) {
	filter_perror(_("Error during filters loading: "));
 	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Error during filters loading: %s\n"
			       "Filters may not be correct."),
 			     filter_strerror(filter_errno));
    }

    /* find and convert old-style signature entries */
    check_for_old_sigs(balsa_app.identities);

    /* Section for the balsa_information() settings... */
    libbalsa_conf_push_group("InformationMessages");

    balsa_app.information_message =
	d_get_gint("ShowInformationMessages", BALSA_INFORMATION_SHOW_BAR);
    balsa_app.warning_message =
	d_get_gint("ShowWarningMessages", BALSA_INFORMATION_SHOW_LIST);
    balsa_app.error_message =
	d_get_gint("ShowErrorMessages", BALSA_INFORMATION_SHOW_DIALOG);
    balsa_app.debug_message =
	d_get_gint("ShowDebugMessages", BALSA_INFORMATION_SHOW_NONE);
    balsa_app.fatal_message = 
	d_get_gint("ShowFatalMessages", BALSA_INFORMATION_SHOW_DIALOG);

    libbalsa_conf_pop_group();

    /* Section for geometry ... */
    libbalsa_conf_push_group("Geometry");

    /* ... column width settings */
    balsa_app.mblist_name_width =
	d_get_gint("MailboxListNameWidth", MBNAME_DEFAULT_WIDTH);
    balsa_app.mblist_newmsg_width =
	d_get_gint("MailboxListNewMsgWidth", NEWMSGCOUNT_DEFAULT_WIDTH);
    balsa_app.mblist_totalmsg_width =
	d_get_gint("MailboxListTotalMsgWidth",
		   TOTALMSGCOUNT_DEFAULT_WIDTH);
    balsa_app.index_num_width =
	d_get_gint("IndexNumWidth", NUM_DEFAULT_WIDTH);
    balsa_app.index_status_width =
	d_get_gint("IndexStatusWidth", STATUS_DEFAULT_WIDTH);
    balsa_app.index_attachment_width =
	d_get_gint("IndexAttachmentWidth", ATTACHMENT_DEFAULT_WIDTH);
    balsa_app.index_from_width =
	d_get_gint("IndexFromWidth", FROM_DEFAULT_WIDTH);
    balsa_app.index_subject_width =
	d_get_gint("IndexSubjectWidth", SUBJECT_DEFAULT_WIDTH);
    balsa_app.index_date_width =
	d_get_gint("IndexDateWidth", DATE_DEFAULT_WIDTH);
    balsa_app.index_size_width =
	d_get_gint("IndexSizeWidth", SIZE_DEFAULT_WIDTH);

    /* ... window sizes */
    balsa_app.mw_width = libbalsa_conf_get_int("MainWindowWidth=640");
    balsa_app.mw_height = libbalsa_conf_get_int("MainWindowHeight=480");
    balsa_app.mw_maximized =
        libbalsa_conf_get_bool("MainWindowMaximized=false");
    balsa_app.mblist_width = libbalsa_conf_get_int("MailboxListWidth=130");
    /* sendmsg window sizes */
    balsa_app.sw_width = libbalsa_conf_get_int("SendMsgWindowWidth=640");
    balsa_app.sw_height = libbalsa_conf_get_int("SendMsgWindowHeight=480");
    balsa_app.sw_maximized =
        libbalsa_conf_get_bool("SendmsgWindowMaximized=false");
    /* message window sizes */
    balsa_app.message_window_width =
        libbalsa_conf_get_int("MessageWindowWidth=400");
    balsa_app.message_window_height =
        libbalsa_conf_get_int("MessageWindowHeight=500");
    balsa_app.message_window_maximized =
        libbalsa_conf_get_bool("MessageWindowMaximized=false");
    /* FIXME: PKGW: why comment this out? Breaks my Transfer context menu. */
    if (balsa_app.mblist_width < 100)
	balsa_app.mblist_width = 170;

    balsa_app.notebook_height = libbalsa_conf_get_int("NotebookHeight=170");
    /*FIXME: Why is this here?? */
    if (balsa_app.notebook_height < 100)
	balsa_app.notebook_height = 200;

    libbalsa_conf_pop_group();

    /* Message View options ... */
    libbalsa_conf_push_group("MessageDisplay");

    /* ... How we format dates */
    g_free(balsa_app.date_string);
    balsa_app.date_string =
	libbalsa_conf_get_string("DateFormat=" DEFAULT_DATE_FORMAT);
    libbalsa_mailbox_date_format = balsa_app.date_string;

    /* ... Headers to show */
    balsa_app.shown_headers = d_get_gint("ShownHeaders", HEADERS_SELECTED);

    g_free(balsa_app.selected_headers);
    {                           /* scope */
        gchar *tmp = libbalsa_conf_get_string("SelectedHeaders="
                                             DEFAULT_SELECTED_HDRS);
        balsa_app.selected_headers = g_ascii_strdown(tmp, -1);
        g_free(tmp);
    }

    balsa_app.expand_tree = libbalsa_conf_get_bool("ExpandTree=false");

    {                             /* scope */
        guint type =
            libbalsa_conf_get_int_with_default("SortField", &def_used);
        if (!def_used)
            libbalsa_mailbox_set_sort_field(NULL, type);
        type =
            libbalsa_conf_get_int_with_default("ThreadingType", &def_used);
        if (!def_used)
            libbalsa_mailbox_set_threading_type(NULL, type);
    }

    /* ... Quote colouring */
    g_free(balsa_app.quote_regex);
    balsa_app.quote_regex =
	libbalsa_conf_get_string("QuoteRegex=" DEFAULT_QUOTE_REGEX);

    /* Obsolete. */
    libbalsa_conf_get_bool_with_default("RecognizeRFC2646FormatFlowed=true",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option not to recognize \"format=flowed\" "
                     "text has been removed."));

    {
	int i;
	gchar *default_quoted_color[MAX_QUOTED_COLOR] = {"#088", "#800"};
	for(i=0;i<MAX_QUOTED_COLOR;i++) {
	    gchar *text = g_strdup_printf("QuotedColor%d=%s", i, i<6 ?
			  default_quoted_color[i] : DEFAULT_QUOTED_COLOR);
	    load_color(text, &balsa_app.quoted_color[i]);
	    g_free(text);
	}
    }

    /* URL coloring */
    load_color("UrlColor=" DEFAULT_URL_COLOR, &balsa_app.url_color);

    /* bad address coloring */
    load_color("BadAddressColor=" DEFAULT_BAD_ADDRESS_COLOR,
               &balsa_app.bad_address_color);

    /* ... font used to display messages */
    g_free(balsa_app.message_font);
    balsa_app.message_font =
	libbalsa_conf_get_string("MessageFont=" DEFAULT_MESSAGE_FONT);
    g_free(balsa_app.subject_font);
    balsa_app.subject_font =
	libbalsa_conf_get_string("SubjectFont=" DEFAULT_SUBJECT_FONT);

    /* ... wrap words */
    balsa_app.browse_wrap = libbalsa_conf_get_bool("WordWrap=true");
    balsa_app.browse_wrap_length = libbalsa_conf_get_int("WordWrapLength=79");
    if (balsa_app.browse_wrap_length < 40)
	balsa_app.browse_wrap_length = 40;

    /* ... handling of Multipart/Alternative */
    balsa_app.display_alt_plain = 
	libbalsa_conf_get_bool("DisplayAlternativeAsPlain=false");

    /* ... handling of broken mails with 8-bit chars */
    balsa_app.convert_unknown_8bit = 
	libbalsa_conf_get_bool("ConvertUnknown8Bit=false");
    balsa_app.convert_unknown_8bit_codeset =
	libbalsa_conf_get_int("ConvertUnknown8BitCodeset=" DEFAULT_BROKEN_CODESET);
    libbalsa_set_fallback_codeset(balsa_app.convert_unknown_8bit_codeset);
    libbalsa_conf_pop_group();

    /* Interface Options ... */
    libbalsa_conf_push_group("Interface");

    /* ... interface elements to show */
    balsa_app.previewpane = libbalsa_conf_get_bool("ShowPreviewPane=true");
    balsa_app.show_mblist = libbalsa_conf_get_bool("ShowMailboxList=true");
    balsa_app.show_notebook_tabs = libbalsa_conf_get_bool("ShowTabs=false");

    /* ... alternative layout of main window */
    balsa_app.alternative_layout = libbalsa_conf_get_bool("AlternativeLayout=false");
    balsa_app.view_message_on_open = libbalsa_conf_get_bool("ViewMessageOnOpen=true");
    balsa_app.pgdownmod = libbalsa_conf_get_bool("PageDownMod=false");
    balsa_app.pgdown_percent = libbalsa_conf_get_int("PageDownPercent=50");
    if (balsa_app.pgdown_percent < 10)
	balsa_app.pgdown_percent = 10;
#if defined(ENABLE_TOUCH_UI)
    balsa_app.do_file_format_check =
        libbalsa_conf_get_bool("FileFormatCheck=true");
    balsa_app.enable_view_filter = libbalsa_conf_get_bool("ViewFilter=false");
#endif /* ENABLE_TOUCH_UI */

    /* ... Progress Window Dialog */
    balsa_app.pwindow_option = d_get_gint("ProgressWindow", WHILERETR);

    /* ... deleting messages: defaults enshrined here */
    tmp = libbalsa_mailbox_get_filter(NULL);
    if (libbalsa_conf_get_bool("HideDeleted=true"))
	tmp |= (1 << 0);
    else
	tmp &= ~(1 << 0);
    libbalsa_mailbox_set_filter(NULL, tmp);

    balsa_app.expunge_on_close =
        libbalsa_conf_get_bool("ExpungeOnClose=true");
    balsa_app.expunge_auto = libbalsa_conf_get_bool("AutoExpunge=true");
    /* timeout in munutes (was hours, so fall back if needed) */
    balsa_app.expunge_timeout =
        libbalsa_conf_get_int("AutoExpungeMinutes") * 60;
    if (!balsa_app.expunge_timeout)
	balsa_app.expunge_timeout = 
	    libbalsa_conf_get_int("AutoExpungeHours=2") * 3600;

    balsa_app.mw_action_after_move = libbalsa_conf_get_int(
        "MessageWindowActionAfterMove");

    libbalsa_conf_pop_group();

    /* Source browsing ... */
    libbalsa_conf_push_group("SourcePreview");
    balsa_app.source_escape_specials = 
        libbalsa_conf_get_bool("EscapeSpecials=true");
    balsa_app.source_width  = libbalsa_conf_get_int("Width=500");
    balsa_app.source_height = libbalsa_conf_get_int("Height=400");
    libbalsa_conf_pop_group();

    /* Printing options ... */
    libbalsa_conf_push_group("Printing");

    /* ... Printing */
#ifdef HAVE_GTK_PRINT
    if (balsa_app.page_setup)
	g_object_unref(G_OBJECT(balsa_app.page_setup));
    balsa_app.page_setup = restore_gtk_page_setup();
    balsa_app.margin_left = libbalsa_conf_get_double("LeftMargin");
    balsa_app.margin_top = libbalsa_conf_get_double("TopMargin");
    balsa_app.margin_right = libbalsa_conf_get_double("RightMargin");
    balsa_app.margin_bottom = libbalsa_conf_get_double("BottomMargin");
#else
    g_free(balsa_app.paper_size);
    balsa_app.paper_size =
	libbalsa_conf_get_string("PaperSize=" DEFAULT_PAPER_SIZE);
    g_free(balsa_app.margin_left);
    balsa_app.margin_left   = libbalsa_conf_get_string("LeftMargin");
    g_free(balsa_app.margin_top);
    balsa_app.margin_top    = libbalsa_conf_get_string("TopMargin");
    g_free(balsa_app.margin_right);
    balsa_app.margin_right  = libbalsa_conf_get_string("RightMargin");
    g_free(balsa_app.margin_bottom);
    balsa_app.margin_bottom = libbalsa_conf_get_string("BottomMargin");
    g_free(balsa_app.print_unit);
    balsa_app.print_unit    = libbalsa_conf_get_string("PrintUnit");
    g_free(balsa_app.print_layout);
    balsa_app.print_layout  = libbalsa_conf_get_string("PrintLayout");
    g_free(balsa_app.paper_orientation);
    balsa_app.paper_orientation  = libbalsa_conf_get_string("PaperOrientation");
    g_free(balsa_app.page_orientation);
    balsa_app.page_orientation   = libbalsa_conf_get_string("PageOrientation");
#endif

    g_free(balsa_app.print_header_font);
    balsa_app.print_header_font =
        libbalsa_conf_get_string("PrintHeaderFont="
                                DEFAULT_PRINT_HEADER_FONT);
    g_free(balsa_app.print_footer_font);
    balsa_app.print_footer_font =
        libbalsa_conf_get_string("PrintFooterFont="
                                DEFAULT_PRINT_FOOTER_FONT);
    g_free(balsa_app.print_body_font);
    balsa_app.print_body_font =
        libbalsa_conf_get_string("PrintBodyFont="
                                DEFAULT_PRINT_BODY_FONT);
    balsa_app.print_highlight_cited =
        libbalsa_conf_get_bool("PrintHighlightCited=false");
#ifdef HAVE_GTK_PRINT
    balsa_app.print_highlight_phrases =
        libbalsa_conf_get_bool("PrintHighlightPhrases=false");
#endif
    libbalsa_conf_pop_group();

#ifdef HAVE_GTK_PRINT
    /* GtkPrint printing */
    libbalsa_conf_push_group("GtkPrint");
    libbalsa_conf_foreach_keys("GtkPrint", load_gtk_print_setting,
			       balsa_app.print_settings);
    libbalsa_conf_pop_group();
#endif

    /* Spelling options ... */
    libbalsa_conf_push_group("Spelling");

#if HAVE_GTKSPELL
    balsa_app.spell_check_lang =
        libbalsa_conf_get_string("SpellCheckLanguage");
    balsa_app.spell_check_active =
        libbalsa_conf_get_bool_with_default("SpellCheckActive", &def_used);
    if (def_used)
        balsa_app.spell_check_active = balsa_app.spell_check_lang != NULL;
#else                           /* HAVE_GTKSPELL */
    balsa_app.module = d_get_gint("PspellModule", DEFAULT_PSPELL_MODULE);
    balsa_app.suggestion_mode =
	d_get_gint("PspellSuggestMode", DEFAULT_PSPELL_SUGGEST_MODE);
    balsa_app.ignore_size =
	d_get_gint("PspellIgnoreSize", DEFAULT_PSPELL_IGNORE_SIZE);
    balsa_app.check_sig =
	d_get_gint("SpellCheckSignature", DEFAULT_CHECK_SIG);
    balsa_app.check_quoted =
	d_get_gint("SpellCheckQuoted", DEFAULT_CHECK_QUOTED);
#endif                          /* HAVE_GTKSPELL */

    libbalsa_conf_pop_group();

    /* Mailbox checking ... */
    libbalsa_conf_push_group("MailboxList");

    /* ... show mailbox content info */
    balsa_app.mblist_show_mb_content_info =
	libbalsa_conf_get_bool("ShowMailboxContentInfo=false");

    libbalsa_conf_pop_group();

    /* Maibox checking options ... */
    libbalsa_conf_push_group("MailboxChecking");

    balsa_app.notify_new_mail_dialog =
	d_get_gint("NewMailNotificationDialog", 0);
    balsa_app.notify_new_mail_sound =
	d_get_gint("NewMailNotificationSound", 1);
#if GTK_CHECK_VERSION(2, 10, 0)
    balsa_app.notify_new_mail_icon =
	d_get_gint("NewMailNotificationIcon", 1);
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */
    balsa_app.check_mail_upon_startup =
	libbalsa_conf_get_bool("OnStartup=false");
    balsa_app.check_mail_auto = libbalsa_conf_get_bool("Auto=false");
    balsa_app.check_mail_timer = libbalsa_conf_get_int("AutoDelay=10");
    if (balsa_app.check_mail_timer < 1)
	balsa_app.check_mail_timer = 10;
    if (balsa_app.check_mail_auto)
	update_timer(TRUE, balsa_app.check_mail_timer);
    balsa_app.check_imap=d_get_gint("CheckIMAP", 1);
    balsa_app.check_imap_inbox=d_get_gint("CheckIMAPInbox", 0);
    balsa_app.quiet_background_check=d_get_gint("QuietBackgroundCheck", 0);
    balsa_app.msg_size_limit=d_get_gint("POPMsgSizeLimit", 20000);
    libbalsa_conf_pop_group();

    /* folder scanning */
    libbalsa_conf_push_group("FolderScanning");
    balsa_app.local_scan_depth = d_get_gint("LocalScanDepth", 1);
    balsa_app.imap_scan_depth = d_get_gint("ImapScanDepth", 1);
    libbalsa_conf_pop_group();

    /* how to react if a message with MDN request is displayed */
    libbalsa_conf_push_group("MDNReply");
    balsa_app.mdn_reply_clean = libbalsa_conf_get_int("Clean=1");
    balsa_app.mdn_reply_notclean = libbalsa_conf_get_int("Suspicious=0");
    libbalsa_conf_pop_group();

    /* Sending options ... */
    libbalsa_conf_push_group("Sending");

#if ENABLE_ESMTP
    /* ... SMTP servers */
    if (!balsa_app.smtp_servers) {
	/* Transition code */
	LibBalsaSmtpServer *smtp_server;
	LibBalsaServer *server;
	gchar *passphrase;

	smtp_server = libbalsa_smtp_server_new();
	libbalsa_smtp_server_set_name(smtp_server,
		libbalsa_smtp_server_get_name(NULL));
	balsa_app.smtp_servers =
	    g_slist_prepend(NULL, smtp_server);
	server = LIBBALSA_SERVER(smtp_server);

	libbalsa_server_set_host
	(server,
	 libbalsa_conf_get_string_with_default("ESMTPServer=localhost:25", 
					     &def_used),
	 FALSE);
	libbalsa_server_set_username(server,
		libbalsa_conf_get_string("ESMTPUser"));

        passphrase = libbalsa_conf_private_get_string("ESMTPPassphrase");
	if (passphrase) {
            gchar* tmp = libbalsa_rot(passphrase);
            g_free(passphrase); 
            libbalsa_server_set_password(server, tmp);
	    g_free(tmp);
        }

        /* default set to "Use TLS if possible" */
	server->tls_mode = libbalsa_conf_get_int("ESMTPTLSMode=1");

#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
	passphrase =
	    libbalsa_conf_private_get_string("ESMTPCertificatePassphrase");
	if (passphrase) {
            gchar* tmp = libbalsa_rot(passphrase);
            g_free(passphrase);
	    libbalsa_smtp_server_set_cert_passphrase(smtp_server, tmp);
	}
#endif
    }
#endif                          /* ENABLE_ESMTP */
    /* ... outgoing mail */
    balsa_app.wordwrap = libbalsa_conf_get_bool("WordWrap=true");
    balsa_app.wraplength = libbalsa_conf_get_int("WrapLength=72");
    if (balsa_app.wraplength < 40)
	balsa_app.wraplength = 40;

    /* Obsolete. */
    libbalsa_conf_get_bool_with_default("SendRFC2646FormatFlowed=true",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option not to send \"format=flowed\" text is now "
                     "on the Options menu of the compose window."));

    balsa_app.autoquote = 
	libbalsa_conf_get_bool("AutoQuote=true");
    balsa_app.reply_strip_html = 
	libbalsa_conf_get_bool("StripHtmlInReply=true");
    balsa_app.forward_attached = 
	libbalsa_conf_get_bool("ForwardAttached=true");

	balsa_app.always_queue_sent_mail = d_get_gint("AlwaysQueueSentMail", 0);
	balsa_app.copy_to_sentbox = d_get_gint("CopyToSentbox", 1);

    libbalsa_conf_pop_group();

    /* Compose window ... */
    libbalsa_conf_push_group("Compose");

    balsa_app.edit_headers = 
        libbalsa_conf_get_bool("ExternEditorEditHeaders=false");

    g_free(balsa_app.quote_str);
    balsa_app.quote_str = libbalsa_conf_get_string("QuoteString=> ");
    g_free(balsa_app.compose_headers);
    balsa_app.compose_headers =
	libbalsa_conf_get_string("ComposeHeaders=Recipients Subject");

    /* Obsolete. */
    libbalsa_conf_get_bool_with_default("RequestDispositionNotification=false",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option to request a MDN is now on "
                     "the Options menu of the compose window."));


    libbalsa_conf_pop_group();

    /* Global config options ... */
    libbalsa_conf_push_group("Globals");

    /* directory */
    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory = libbalsa_conf_get_string("MailDir");

    if(!balsa_app.local_mail_directory) {
	libbalsa_conf_pop_group();
        new_user = TRUE;
	return FALSE;
    }
    balsa_app.root_node =
        balsa_mailbox_node_new_from_dir(balsa_app.local_mail_directory);

#if defined(ENABLE_TOUCH_UI)
     balsa_app.open_inbox_upon_startup =
	libbalsa_conf_get_bool("OpenInboxOnStartup=true");
#else
     balsa_app.open_inbox_upon_startup =
	libbalsa_conf_get_bool("OpenInboxOnStartup=false");
#endif /* ENABLE_TOUCH_UI */
    /* debugging enabled */
    balsa_app.debug = libbalsa_conf_get_bool("Debug=false");

    balsa_app.close_mailbox_auto = libbalsa_conf_get_bool("AutoCloseMailbox=true");
    /* timeouts in minutes in config file for backwards compat */
    balsa_app.close_mailbox_timeout = libbalsa_conf_get_int("AutoCloseMailboxTimeout=10") * 60;
    
    balsa_app.remember_open_mboxes =
	libbalsa_conf_get_bool("RememberOpenMailboxes=false");

    balsa_app.empty_trash_on_exit =
	libbalsa_conf_get_bool("EmptyTrash=false");

    /* This setting is now per address book */
    libbalsa_conf_clean_key("AddressBookDistMode");

    libbalsa_conf_pop_group();
    
    /* Toolbars */
    load_toolbars();

    /* Last used paths options ... */
    libbalsa_conf_push_group("Paths");
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = libbalsa_conf_get_string("AttachDir");
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = libbalsa_conf_get_string("SavePartDir");
    libbalsa_conf_pop_group();

	/* Folder MRU */
    load_mru(&balsa_app.folder_mru, "FolderMRU");

	/* FCC MRU */
    load_mru(&balsa_app.fcc_mru, "FccMRU");

    /* Pipe commands */
    load_mru(&balsa_app.pipe_cmds, "PipeCommands");
    if (!balsa_app.pipe_cmds)
        balsa_app.pipe_cmds = g_list_prepend(NULL, 
                                             g_strdup("sa-learn --spam"));

    /* We load identities at the end because they may refer to SMTP
     * servers. This is also critical when handling damaged config
     * files with no smtp server defined (think switching from
     * --without-esmtp build). */
    config_identities_load();

    if (!new_user) {
        /* Notify user about any changes */
        libbalsa_conf_push_group("Notifications");
        if (!libbalsa_conf_get_bool("GtkUIManager")) {
            g_idle_add((GSourceFunc) config_warning_idle,
                       _("This version of Balsa uses a new user interface; "
                         "if you have changed Balsa's keyboard accelerators, "
                         "you will need to set them again."));
            libbalsa_conf_set_bool("GtkUIManager", TRUE);
        }
        if (!libbalsa_conf_get_bool("LibBalsaAddressView")) {
            /* No warning */
            if (!libbalsa_find_word("Recipients",
                                    balsa_app.compose_headers)) {
                gchar *compose_headers =
                    g_strconcat(balsa_app.compose_headers, " Recipients",
                                NULL);
                g_free(balsa_app.compose_headers);
                balsa_app.compose_headers = compose_headers;
            }
            libbalsa_conf_set_bool("LibBalsaAddressView", TRUE);
        }
        libbalsa_conf_pop_group();
    }

    return TRUE;
}				/* config_global_load */

#ifdef HAVE_GTK_PRINT
static void
save_gtk_print_setting(const gchar *key, const gchar *value, gpointer user_data)
{
    libbalsa_conf_set_string(key, value);
}

static void
save_gtk_page_setup(GtkPageSetup *page_setup)
{
    GtkPaperSize *paper_size;

    if (!page_setup)
	return;

    paper_size = gtk_page_setup_get_paper_size(page_setup);
    g_assert(paper_size != NULL);

    libbalsa_conf_set_string("PaperName",
			     gtk_paper_size_get_name (paper_size));
    libbalsa_conf_set_string("DisplayName",
			     gtk_paper_size_get_display_name (paper_size));
    libbalsa_conf_set_string("PPDName",
			     gtk_paper_size_get_ppd_name (paper_size));

    libbalsa_conf_set_double("Width",
			     gtk_paper_size_get_width(paper_size,
						      GTK_UNIT_MM));
    libbalsa_conf_set_double("Height",
			     gtk_paper_size_get_height(paper_size,
						       GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginTop",
			     gtk_page_setup_get_top_margin(page_setup,
							   GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginBottom",
			     gtk_page_setup_get_bottom_margin(page_setup,
							      GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginLeft",
			     gtk_page_setup_get_left_margin(page_setup,
							    GTK_UNIT_MM));
    libbalsa_conf_set_double("MarginRight",
			     gtk_page_setup_get_right_margin(page_setup,
							     GTK_UNIT_MM));
    libbalsa_conf_set_int("Orientation",
			  gtk_page_setup_get_orientation(page_setup));
}
#endif

gint
config_save(void)
{
    gint i;
#if ENABLE_ESMTP
    GSList *list;
#endif                          /* ENABLE_ESMTP */

    config_address_books_save();
    config_identities_save();

    /* Section for the balsa_information() settings... */
    libbalsa_conf_push_group("InformationMessages");
    libbalsa_conf_set_int("ShowInformationMessages",
			 balsa_app.information_message);
    libbalsa_conf_set_int("ShowWarningMessages", balsa_app.warning_message);
    libbalsa_conf_set_int("ShowErrorMessages", balsa_app.error_message);
    libbalsa_conf_set_int("ShowDebugMessages", balsa_app.debug_message);
    libbalsa_conf_set_int("ShowFatalMessages", balsa_app.fatal_message);
    libbalsa_conf_pop_group();

    /* Section for geometry ... */
    /* FIXME: Saving window sizes is the WM's job?? */
    libbalsa_conf_push_group("Geometry");

    /* ... column width settings */
    libbalsa_conf_set_int("MailboxListNameWidth",
			 balsa_app.mblist_name_width);
    libbalsa_conf_set_int("MailboxListNewMsgWidth",
			 balsa_app.mblist_newmsg_width);
    libbalsa_conf_set_int("MailboxListTotalMsgWidth",
			 balsa_app.mblist_totalmsg_width);
    libbalsa_conf_set_int("IndexNumWidth", balsa_app.index_num_width);
    libbalsa_conf_set_int("IndexStatusWidth", balsa_app.index_status_width);
    libbalsa_conf_set_int("IndexAttachmentWidth",
			 balsa_app.index_attachment_width);
    libbalsa_conf_set_int("IndexFromWidth", balsa_app.index_from_width);
    libbalsa_conf_set_int("IndexSubjectWidth",
			 balsa_app.index_subject_width);
    libbalsa_conf_set_int("IndexDateWidth", balsa_app.index_date_width);
    libbalsa_conf_set_int("IndexSizeWidth", balsa_app.index_size_width);

    libbalsa_conf_set_int("MainWindowWidth", balsa_app.mw_width);
    libbalsa_conf_set_int("MainWindowHeight", balsa_app.mw_height);
    libbalsa_conf_set_bool("MainWindowMaximized",
                           !!balsa_app.mw_maximized);
    libbalsa_conf_set_int("MailboxListWidth", balsa_app.mblist_width);

    libbalsa_conf_set_int("SendMsgWindowWidth", balsa_app.sw_width);
    libbalsa_conf_set_int("SendMsgWindowHeight", balsa_app.sw_height);
    libbalsa_conf_set_bool("SendmsgWindowMaximized",
                           !!balsa_app.sw_maximized);

    libbalsa_conf_set_int("MessageWindowWidth",
                         balsa_app.message_window_width);
    libbalsa_conf_set_int("MessageWindowHeight",
                         balsa_app.message_window_height);
    libbalsa_conf_set_bool("MessageWindowMaximized",
                           !!balsa_app.message_window_maximized);

    libbalsa_conf_set_int("NotebookHeight", balsa_app.notebook_height);

    libbalsa_conf_pop_group();

    /* Message View options ... */
    libbalsa_conf_remove_group("MessageDisplay");
    libbalsa_conf_push_group("MessageDisplay");

    libbalsa_conf_set_string("DateFormat", balsa_app.date_string);
    libbalsa_conf_set_int("ShownHeaders", balsa_app.shown_headers);
    libbalsa_conf_set_string("SelectedHeaders", balsa_app.selected_headers);
    libbalsa_conf_set_bool("ExpandTree", balsa_app.expand_tree);
    libbalsa_conf_set_int("SortField",
			 libbalsa_mailbox_get_sort_field(NULL));
    libbalsa_conf_set_int("ThreadingType",
			 libbalsa_mailbox_get_threading_type(NULL));
    libbalsa_conf_set_string("QuoteRegex", balsa_app.quote_regex);
    libbalsa_conf_set_string("MessageFont", balsa_app.message_font);
    libbalsa_conf_set_string("SubjectFont", balsa_app.subject_font);
    libbalsa_conf_set_bool("WordWrap", balsa_app.browse_wrap);
    libbalsa_conf_set_int("WordWrapLength", balsa_app.browse_wrap_length);

    for(i=0;i<MAX_QUOTED_COLOR;i++) {
	gchar *text = g_strdup_printf("QuotedColor%d", i);
	save_color(text, &balsa_app.quoted_color[i]);
	g_free(text);
    }

    save_color("UrlColor", &balsa_app.url_color);
    save_color("BadAddressColor", &balsa_app.bad_address_color);

    /* ... handling of Multipart/Alternative */
    libbalsa_conf_set_bool("DisplayAlternativeAsPlain",
			  balsa_app.display_alt_plain);

    /* ... handling of broken mails with 8-bit chars */
    libbalsa_conf_set_bool("ConvertUnknown8Bit", balsa_app.convert_unknown_8bit);
    libbalsa_conf_set_int("ConvertUnknown8BitCodeset", 
			    balsa_app.convert_unknown_8bit_codeset);

    libbalsa_conf_pop_group();

    /* Interface Options ... */
    libbalsa_conf_push_group("Interface");

    libbalsa_conf_set_bool("ShowPreviewPane", balsa_app.previewpane);
    libbalsa_conf_set_bool("ShowMailboxList", balsa_app.show_mblist);
    libbalsa_conf_set_bool("ShowTabs", balsa_app.show_notebook_tabs);
    libbalsa_conf_set_int("ProgressWindow", balsa_app.pwindow_option);
    libbalsa_conf_set_bool("AlternativeLayout", balsa_app.alternative_layout);
    libbalsa_conf_set_bool("ViewMessageOnOpen", balsa_app.view_message_on_open);
    libbalsa_conf_set_bool("PageDownMod", balsa_app.pgdownmod);
    libbalsa_conf_set_int("PageDownPercent", balsa_app.pgdown_percent);
#if defined(ENABLE_TOUCH_UI)
    libbalsa_conf_set_bool("FileFormatCheck", balsa_app.do_file_format_check);
    libbalsa_conf_set_bool("ViewFilter",      balsa_app.enable_view_filter);
#endif /* ENABLE_TOUCH_UI */
    libbalsa_conf_set_bool("HideDeleted", libbalsa_mailbox_get_filter(NULL) & 1);
    libbalsa_conf_set_bool("ExpungeOnClose", balsa_app.expunge_on_close);
    libbalsa_conf_set_bool("AutoExpunge", balsa_app.expunge_auto);
    libbalsa_conf_set_int("AutoExpungeMinutes",
                         balsa_app.expunge_timeout / 60);
    libbalsa_conf_set_int("MessageWindowActionAfterMove",
        balsa_app.mw_action_after_move);

    libbalsa_conf_pop_group();

    /* Source browsing ... */
    libbalsa_conf_push_group("SourcePreview");
    libbalsa_conf_set_bool("EscapeSpecials",
                          balsa_app.source_escape_specials);
    libbalsa_conf_set_int("Width",  balsa_app.source_width);
    libbalsa_conf_set_int("Height", balsa_app.source_height);
    libbalsa_conf_pop_group();

    /* Printing options ... */
    libbalsa_conf_push_group("Printing");
#ifdef HAVE_GTK_PRINT
    save_gtk_page_setup(balsa_app.page_setup);
    libbalsa_conf_set_double("LeftMargin", balsa_app.margin_left);
    libbalsa_conf_set_double("TopMargin", balsa_app.margin_top);
    libbalsa_conf_set_double("RightMargin", balsa_app.margin_right);
    libbalsa_conf_set_double("BottomMargin", balsa_app.margin_bottom);
#else
    libbalsa_conf_set_string("PaperSize",balsa_app.paper_size);
    if(balsa_app.margin_left)
	libbalsa_conf_set_string("LeftMargin",   balsa_app.margin_left);
    if(balsa_app.margin_top)
	libbalsa_conf_set_string("TopMargin",    balsa_app.margin_top);
    if(balsa_app.margin_bottom)
	libbalsa_conf_set_string("RightMargin",  balsa_app.margin_right);
    if(balsa_app.margin_bottom)
	libbalsa_conf_set_string("BottomMargin", balsa_app.margin_bottom);
    if(balsa_app.print_unit)
	libbalsa_conf_set_string("PrintUnit",    balsa_app.print_unit);
    if(balsa_app.print_layout)
	libbalsa_conf_set_string("PrintLayout", balsa_app.print_layout);
    if(balsa_app.margin_bottom)
	libbalsa_conf_set_string("PaperOrientation", 
				balsa_app.paper_orientation);
    if(balsa_app.margin_bottom)
	libbalsa_conf_set_string("PageOrientation", balsa_app.page_orientation);
#endif

    libbalsa_conf_set_string("PrintHeaderFont",
                            balsa_app.print_header_font);
    libbalsa_conf_set_string("PrintBodyFont", balsa_app.print_body_font);
    libbalsa_conf_set_string("PrintFooterFont",
                            balsa_app.print_footer_font);
    libbalsa_conf_set_bool("PrintHighlightCited",
                          balsa_app.print_highlight_cited);
#ifdef HAVE_GTK_PRINT
    libbalsa_conf_set_bool("PrintHighlightPhrases",
                          balsa_app.print_highlight_phrases);
#endif
    libbalsa_conf_pop_group();

#ifdef HAVE_GTK_PRINT
    /* GtkPrintSettings stuff */
    libbalsa_conf_remove_group("GtkPrint");
    libbalsa_conf_push_group("GtkPrint");
    if (balsa_app.print_settings)
	gtk_print_settings_foreach(balsa_app.print_settings,
				   save_gtk_print_setting, NULL);
    libbalsa_conf_pop_group();
#endif

    /* Spelling options ... */
    libbalsa_conf_remove_group("Spelling");
    libbalsa_conf_push_group("Spelling");

#if HAVE_GTKSPELL
    libbalsa_conf_set_string("SpellCheckLanguage",
                             balsa_app.spell_check_lang);
    libbalsa_conf_set_bool("SpellCheckActive", 
                           balsa_app.spell_check_active);
#else                           /* HAVE_GTKSPELL */
    libbalsa_conf_set_int("PspellModule", balsa_app.module);
    libbalsa_conf_set_int("PspellSuggestMode", balsa_app.suggestion_mode);
    libbalsa_conf_set_int("PspellIgnoreSize", balsa_app.ignore_size);
    libbalsa_conf_set_int("SpellCheckSignature", balsa_app.check_sig);
    libbalsa_conf_set_int("SpellCheckQuoted", balsa_app.check_quoted);
#endif                          /* HAVE_GTKSPELL */

    libbalsa_conf_pop_group();

    /* Mailbox list options */
    libbalsa_conf_push_group("MailboxList");

    libbalsa_conf_set_bool("ShowMailboxContentInfo",
			  balsa_app.mblist_show_mb_content_info);

    libbalsa_conf_pop_group();

    /* Maibox checking options ... */
    libbalsa_conf_push_group("MailboxChecking");

    libbalsa_conf_set_int("NewMailNotificationDialog",
                          balsa_app.notify_new_mail_dialog);
    libbalsa_conf_set_int("NewMailNotificationSound",
                          balsa_app.notify_new_mail_sound);
#if GTK_CHECK_VERSION(2, 10, 0)
    libbalsa_conf_set_int("NewMailNotificationIcon",
                          balsa_app.notify_new_mail_icon);
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */
    libbalsa_conf_set_bool("OnStartup", balsa_app.check_mail_upon_startup);
    libbalsa_conf_set_bool("Auto", balsa_app.check_mail_auto);
    libbalsa_conf_set_int("AutoDelay", balsa_app.check_mail_timer);
    libbalsa_conf_set_int("CheckIMAP", balsa_app.check_imap);
    libbalsa_conf_set_int("CheckIMAPInbox", balsa_app.check_imap_inbox);
    libbalsa_conf_set_int("QuietBackgroundCheck",
			 balsa_app.quiet_background_check);
    libbalsa_conf_set_int("POPMsgSizeLimit", balsa_app.msg_size_limit);

    libbalsa_conf_pop_group();

    /* folder scanning */
    libbalsa_conf_push_group("FolderScanning");
    libbalsa_conf_set_int("LocalScanDepth", balsa_app.local_scan_depth);
    libbalsa_conf_set_int("ImapScanDepth", balsa_app.imap_scan_depth);
    libbalsa_conf_pop_group();

    /* how to react if a message with MDN request is displayed */
    libbalsa_conf_push_group("MDNReply");
    libbalsa_conf_set_int("Clean", balsa_app.mdn_reply_clean);
    libbalsa_conf_set_int("Suspicious", balsa_app.mdn_reply_notclean);
    libbalsa_conf_pop_group();

    /* Sending options ... */
#if ENABLE_ESMTP
    for (list = balsa_app.smtp_servers; list; list = list->next) {
        LibBalsaSmtpServer *smtp_server = LIBBALSA_SMTP_SERVER(list->data);
        gchar *group;

        group =
            g_strconcat(SMTP_SERVER_SECTION_PREFIX,
                        libbalsa_smtp_server_get_name(smtp_server), NULL);
        libbalsa_conf_push_group(group);
	g_free(group);
        libbalsa_smtp_server_save_config(smtp_server);
        libbalsa_conf_pop_group();
    }
#endif                          /* ENABLE_ESMTP */

    libbalsa_conf_remove_group("Sending");
    libbalsa_conf_private_remove_group("Sending");
    libbalsa_conf_push_group("Sending");
    libbalsa_conf_set_bool("WordWrap", balsa_app.wordwrap);
    libbalsa_conf_set_int("WrapLength", balsa_app.wraplength);
    libbalsa_conf_set_bool("AutoQuote", balsa_app.autoquote);
    libbalsa_conf_set_bool("StripHtmlInReply", balsa_app.reply_strip_html);
    libbalsa_conf_set_bool("ForwardAttached", balsa_app.forward_attached);

	libbalsa_conf_set_int("AlwaysQueueSentMail", balsa_app.always_queue_sent_mail);
	libbalsa_conf_set_int("CopyToSentbox", balsa_app.copy_to_sentbox);
    libbalsa_conf_pop_group();

    /* Compose window ... */
    libbalsa_conf_remove_group("Compose");
    libbalsa_conf_push_group("Compose");

    libbalsa_conf_set_string("ComposeHeaders", balsa_app.compose_headers);
    libbalsa_conf_set_bool("ExternEditorEditHeaders", balsa_app.edit_headers);
    libbalsa_conf_set_string("QuoteString", balsa_app.quote_str);

    libbalsa_conf_pop_group();

    /* Global config options ... */
    libbalsa_conf_push_group("Globals");

    libbalsa_conf_set_string("MailDir", balsa_app.local_mail_directory);

    libbalsa_conf_set_bool("OpenInboxOnStartup", 
                          balsa_app.open_inbox_upon_startup);
    libbalsa_conf_set_bool("Debug", balsa_app.debug);

    libbalsa_conf_set_bool("AutoCloseMailbox", balsa_app.close_mailbox_auto);
    libbalsa_conf_set_int("AutoCloseMailboxTimeout", balsa_app.close_mailbox_timeout/60);

    libbalsa_conf_set_bool("RememberOpenMailboxes",
			  balsa_app.remember_open_mboxes);
    libbalsa_conf_set_bool("EmptyTrash", balsa_app.empty_trash_on_exit);

    if (balsa_app.default_address_book)
        libbalsa_conf_set_string("DefaultAddressBook",
                                 balsa_app.default_address_book->
                                 config_prefix);
    else
	libbalsa_conf_clean_key("DefaultAddressBook");

    libbalsa_conf_pop_group();

    /* Toolbars */
    save_toolbars();

    libbalsa_conf_push_group("Paths");
    if(balsa_app.attach_dir)
	libbalsa_conf_set_string("AttachDir", balsa_app.attach_dir);
    if(balsa_app.save_dir)
	libbalsa_conf_set_string("SavePartDir", balsa_app.save_dir);
    libbalsa_conf_pop_group();

    save_mru(balsa_app.folder_mru, "FolderMRU");
    save_mru(balsa_app.fcc_mru,    "FccMRU");
    save_mru(balsa_app.pipe_cmds,  "PipeCommands");

    libbalsa_conf_sync();
    return TRUE;
}				/* config_global_save */


/* must use a sensible prefix, or this goes weird */

static gboolean
config_get_used_group(const gchar * key, const gchar * value,
                        gpointer data)
{
    gint *max = data;
    gint curr;

    if (*value && (curr = atoi(value)) > *max)
        *max = curr;

    return FALSE;
}

static gchar *
config_get_unused_group(const gchar * prefix)
{
    gint max = 0;
    gchar *name;

    libbalsa_conf_foreach_group(prefix, config_get_used_group, &max);

    name = g_strdup_printf("%s%d", prefix, ++max);
    if (balsa_app.debug)
        g_print("config_mailbox_get_highest_number: name='%s'\n", name);

    return name;
}

static gboolean
config_list_section(const gchar * key, const gchar * value, gpointer data)
{
    GSList **l = data;

    *l = g_slist_prepend(*l, g_strdup(key));

    return FALSE;
}

static void
config_remove_groups(const gchar * section_prefix)
{
    GSList *sections = NULL, *list;

    libbalsa_conf_foreach_group(section_prefix, config_list_section,
                                &sections);
    for (list = sections; list; list = list->next) {
	libbalsa_conf_remove_group(list->data);
	g_free(list->data);
    }
    g_slist_free(sections);
}

static gboolean
config_address_book_load(const gchar * key, const gchar * value,
                         gpointer data)
{
    const gchar *default_address_book_prefix = data;
    LibBalsaAddressBook *address_book;

    address_book = libbalsa_address_book_new_from_config(key);

    if (address_book) {
        balsa_app.address_book_list =
            g_list_append(balsa_app.address_book_list, address_book);

        if (default_address_book_prefix
            && strcmp(key, default_address_book_prefix) == 0) {
            balsa_app.default_address_book = address_book;
        }
    }

    return FALSE;
}

void
config_address_books_load(void)
{
    gchar *default_address_book_prefix;

    libbalsa_conf_push_group("Globals");
    default_address_book_prefix =
        libbalsa_conf_get_string("DefaultAddressBook");
    libbalsa_conf_pop_group();

    /* Free old data in case address books were set by eg. config druid. */
    g_list_foreach(balsa_app.address_book_list, (GFunc) g_object_unref,
                   NULL);
    g_list_free(balsa_app.address_book_list);
    balsa_app.address_book_list = NULL;

    libbalsa_conf_foreach_group(ADDRESS_BOOK_SECTION_PREFIX,
                                config_address_book_load,
                                default_address_book_prefix);

    g_free(default_address_book_prefix);
}

static void
config_address_books_save(void)
{
    g_list_foreach(balsa_app.address_book_list,
                   (GFunc) config_address_book_save, NULL);
}

#if ENABLE_ESMTP
static LibBalsaSmtpServer *
find_smtp_server_by_name(const gchar * name)
{
    GSList *list;

    if (!name)
        name = libbalsa_smtp_server_get_name(NULL);

    for (list = balsa_app.smtp_servers; list; list = list->next) {
        LibBalsaSmtpServer *smtp_server =
            LIBBALSA_SMTP_SERVER(list->data);
        if (strcmp(name,
                   libbalsa_smtp_server_get_name(smtp_server)) == 0)
            return smtp_server;
    }

    /* Use the first in the list, if any (there really must be one by
     * construction). */
    g_return_val_if_fail(balsa_app.smtp_servers, NULL);
    return LIBBALSA_SMTP_SERVER(balsa_app.smtp_servers->data);
}
#endif                          /* ESMTP */

static gboolean
config_identity_load(const gchar * key, const gchar * value, gpointer data)
{
    const gchar *default_ident = data;
    LibBalsaIdentity *ident;
#if ENABLE_ESMTP
    gchar *smtp_server_name;
#endif                          /* ENABLE_ESMTP */

    libbalsa_conf_push_group(key);
    ident = libbalsa_identity_new_config(value);
#if ENABLE_ESMTP
    smtp_server_name = libbalsa_conf_get_string("SmtpServer");
    libbalsa_identity_set_smtp_server(ident,
                                      find_smtp_server_by_name
                                      (smtp_server_name));
    g_free(smtp_server_name);
#endif                          /* ENABLE_ESMTP */
    libbalsa_conf_pop_group();
    balsa_app.identities = g_list_prepend(balsa_app.identities, ident);
    if (g_ascii_strcasecmp(default_ident, ident->identity_name) == 0)
        balsa_app.current_ident = ident;

    return FALSE;
}

static void
config_identities_load()
{
    gchar *default_ident;

    /* Free old data in case identities were set by eg. config druid. */
    g_list_foreach(balsa_app.identities, (GFunc) g_object_unref, NULL);
    g_list_free(balsa_app.identities);
    balsa_app.identities = NULL;

    libbalsa_conf_push_group("identity");
    default_ident = libbalsa_conf_get_string("CurrentIdentity");
    libbalsa_conf_pop_group();

    libbalsa_conf_foreach_group(IDENTITY_SECTION_PREFIX,
                                config_identity_load,
                                default_ident);

    if (!balsa_app.identities) {
	libbalsa_conf_push_group("identity-default");
        balsa_app.identities =
            g_list_prepend(NULL,
                           libbalsa_identity_new_config("default"));
	libbalsa_conf_pop_group();
    }

    if (balsa_app.current_ident == NULL)
        balsa_app.current_ident =
            LIBBALSA_IDENTITY(balsa_app.identities->data);

    g_free(default_ident);
    if (balsa_app.main_window)
        balsa_identities_changed(balsa_app.main_window);
}

/* config_identities_save:
   saves the balsa_app.identites list.
*/
void
config_identities_save(void)
{
    LibBalsaIdentity* ident;
    GList* list;
    gchar** conf_vec, *prefix;

    conf_vec = g_malloc(sizeof(gchar*) * g_list_length(balsa_app.identities));

    g_assert(conf_vec != NULL);
    
    libbalsa_conf_push_group("identity");
    libbalsa_conf_set_string("CurrentIdentity", 
                            balsa_app.current_ident->identity_name);
    libbalsa_conf_pop_group();
    g_free(conf_vec);

    config_remove_groups(IDENTITY_SECTION_PREFIX);

    /* save current */
    for (list = balsa_app.identities; list; list = list->next) {
	ident = LIBBALSA_IDENTITY(list->data);
	prefix = g_strconcat(IDENTITY_SECTION_PREFIX, 
			     ident->identity_name, NULL);
        libbalsa_identity_save(ident, prefix);
	g_free(prefix);
    }
}

static gboolean
config_view_load(const gchar * key, const gchar * value, gpointer data)
{
    gboolean compat = GPOINTER_TO_INT(data);
    gchar *url;
    gint def;

    libbalsa_conf_push_group(key);

    url = compat ? libbalsa_conf_get_string_with_default("URL", &def) :
        libbalsa_urldecode(value);

    if (!compat || !def) {
        LibBalsaMailboxView *view;
        gint tmp;
        gchar *address;

        view = libbalsa_mailbox_view_new();
        /* In compatibility mode, mark as not in sync, to force
         * the view to be saved in the config file. */
        view->in_sync = !compat;
        g_hash_table_insert(libbalsa_mailbox_view_table, g_strdup(url),
                            view);

        address =
            libbalsa_conf_get_string_with_default("MailingListAddress",
                                                  &def);
        view->mailing_list_address =
            def ? NULL : internet_address_parse_string(address);
        g_free(address);

        view->identity_name = libbalsa_conf_get_string("Identity");

        tmp = libbalsa_conf_get_int_with_default("Threading", &def);
        if (!def)
            view->threading_type = tmp;

        tmp = libbalsa_conf_get_int_with_default("GUIFilter", &def);
        if (!def)
            view->filter = tmp;

        tmp = libbalsa_conf_get_int_with_default("SortType", &def);
        if (!def)
            view->sort_type = tmp;

        tmp = libbalsa_conf_get_int_with_default("SortField", &def);
        if (!def)
            view->sort_field = tmp;

        tmp = libbalsa_conf_get_int_with_default("Show", &def);
        if (!def)
            view->show = tmp;

        tmp = libbalsa_conf_get_int_with_default("Subscribe", &def);
        if (!def)
            view->subscribe = tmp;

        tmp = libbalsa_conf_get_bool_with_default("Exposed", &def);
        if (!def)
            view->exposed = tmp;

        tmp = libbalsa_conf_get_bool_with_default("Open", &def);
        if (!def)
            view->open = tmp;
#ifdef HAVE_GPGME
        tmp = libbalsa_conf_get_int_with_default("CryptoMode", &def);
        if (!def)
            view->gpg_chk_mode = tmp;
#endif

        tmp = libbalsa_conf_get_int_with_default("Total", &def);
        if (!def)
            view->total = tmp;

        tmp = libbalsa_conf_get_int_with_default("Unread", &def);
        if (!def)
            view->unread = tmp;

        tmp = libbalsa_conf_get_int_with_default("ModTime", &def);
        if (!def)
            view->mtime = tmp;
    }

    libbalsa_conf_pop_group();
    g_free(url);

    return FALSE;
}

void
config_views_load(void)
{
    /* Load old-style config sections in compatibility mode. */
    libbalsa_conf_foreach_group(VIEW_SECTION_PREFIX,
                                config_view_load,
                                GINT_TO_POINTER(TRUE));
    libbalsa_conf_foreach_group(VIEW_BY_URL_SECTION_PREFIX,
                                config_view_load,
                                GINT_TO_POINTER(FALSE));
}

/* Get viewByUrl prefix */
static gchar *
view_by_url_prefix(const gchar * url)
{
    gchar *url_enc;
    gchar *prefix;

    url_enc = libbalsa_urlencode(url);
    prefix = g_strconcat(VIEW_BY_URL_SECTION_PREFIX, url_enc, NULL);
    g_free(url_enc);

    return prefix;
}

/* config_views_save:
   iterates over all mailbox views.
*/
static void
save_view(const gchar * url, LibBalsaMailboxView * view)
{
    gchar *prefix;

    if (!view || (view->in_sync && view->used))
	return;
    view->in_sync = TRUE;

    prefix = view_by_url_prefix(url);

    /* Remove the view--it will be recreated if any member needs to be
     * saved. */
    libbalsa_conf_remove_group(prefix);
    libbalsa_conf_push_group(prefix);
    g_free(prefix);

    if (view->mailing_list_address !=
	libbalsa_mailbox_get_mailing_list_address(NULL)) {
       gchar* tmp =
	   internet_address_list_to_string(view->mailing_list_address,
		                           FALSE);
       libbalsa_conf_set_string("MailingListAddress", tmp);
       g_free(tmp);
    }
    if (view->identity_name  != libbalsa_mailbox_get_identity_name(NULL))
	libbalsa_conf_set_string("Identity", view->identity_name);
    if (view->threading_type != libbalsa_mailbox_get_threading_type(NULL))
	libbalsa_conf_set_int("Threading",   view->threading_type);
    if (view->filter         != libbalsa_mailbox_get_filter(NULL))
	libbalsa_conf_set_int("GUIFilter",   view->filter);
    if (view->sort_type      != libbalsa_mailbox_get_sort_type(NULL))
	libbalsa_conf_set_int("SortType",    view->sort_type);
    if (view->sort_field     != libbalsa_mailbox_get_sort_field(NULL))
	libbalsa_conf_set_int("SortField",   view->sort_field);
    if (view->show           == LB_MAILBOX_SHOW_TO)
	libbalsa_conf_set_int("Show",        view->show);
    if (view->subscribe      == LB_MAILBOX_SUBSCRIBE_NO)
	libbalsa_conf_set_int("Subscribe",   view->subscribe);
    if (view->exposed        != libbalsa_mailbox_get_exposed(NULL))
	libbalsa_conf_set_bool("Exposed",    view->exposed);
    if (view->open           != libbalsa_mailbox_get_open(NULL))
	libbalsa_conf_set_bool("Open",       view->open);
#ifdef HAVE_GPGME
    if (view->gpg_chk_mode   != libbalsa_mailbox_get_crypto_mode(NULL))
	libbalsa_conf_set_int("CryptoMode",  view->gpg_chk_mode);
#endif
    /* To avoid accumulation of config entries with only message counts,
     * we save them only if used in this session. */
    if (view->used && view->mtime != 0) {
        gboolean save_mtime = FALSE;
        if (view->unread     != libbalsa_mailbox_get_unread(NULL)) {
            libbalsa_conf_set_int("Unread",  view->unread);
            save_mtime = TRUE;
        }
        if (view->total      != libbalsa_mailbox_get_total(NULL)) {
            libbalsa_conf_set_int("Total",   view->total);
            save_mtime = TRUE;
        }
        if (save_mtime)
            libbalsa_conf_set_int("ModTime", view->mtime);
    }

    libbalsa_conf_pop_group();
}

void
config_views_save(void)
{
    config_remove_groups(VIEW_SECTION_PREFIX);
    /* save current */
    g_hash_table_foreach(libbalsa_mailbox_view_table, (GHFunc) save_view,
			 NULL);
}

void
config_view_remove(const gchar * url)
{
    gchar *prefix = view_by_url_prefix(url);
    libbalsa_conf_remove_group(prefix);
    g_free(prefix);
    g_hash_table_remove(libbalsa_mailbox_view_table, url);
}

static void
save_color(gchar * key, GdkColor * color)
{
    gchar *str;

    str = g_strdup_printf("#%04x%04x%04x", color->red, color->green,
                          color->blue);
    libbalsa_conf_set_string(key, str);
    g_free(str);
}

static gboolean
config_filter_load(const gchar * key, const gchar * value, gpointer data)
{
    char *endptr;
    guint *save = data;
    LibBalsaFilter *fil;
    long int dummy;

    dummy = strtol(value, &endptr, 10);
    if (*endptr) {              /* Bad format. */
        libbalsa_conf_remove_group(key);
        return FALSE;
    }

    libbalsa_conf_push_group(key);

    fil = libbalsa_filter_new_from_config();
    if (!fil->condition) {
        /* Try pre-2.1 style: */
        FilterOpType op = libbalsa_conf_get_int("Operation");
        ConditionMatchType cmt =
            op == FILTER_OP_OR ? CONDITION_OR : CONDITION_AND;
        fil->condition = libbalsa_condition_new_2_0(key, cmt);
        if (fil->condition) {
            if (fil->action > FILTER_TRASH)
                /* Some 2.0.x versions had a new action code which
                 * changed the value of FILTER_TRASH. */
                fil->action = FILTER_TRASH;
            ++*save;
        }
    }
    if (!fil->condition) {
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("Filter with no condition was omitted"));
        libbalsa_filter_free(fil, GINT_TO_POINTER(FALSE));
    } else {
        FILTER_SETFLAG(fil, FILTER_VALID);
        FILTER_SETFLAG(fil, FILTER_COMPILED);
        balsa_app.filters = g_slist_prepend(balsa_app.filters, fil);
    }
    libbalsa_conf_pop_group();

    return filter_errno != FILTER_NOERR;
}

static void
config_filters_load(void)
{
    guint save = 0;

    filter_errno = FILTER_NOERR;
    libbalsa_conf_foreach_group(FILTER_SECTION_PREFIX,
                                config_filter_load, &save);

    if (save)
        config_filters_save();
}

#define FILTER_SECTION_MAX "9999"

void
config_filters_save(void)
{
    GSList *list;
    LibBalsaFilter* fil;
    gchar * buffer,* tmp;
    gint i,nb=0,tmp_len=strlen(FILTER_SECTION_MAX)+2;

    /* We allocate once for all a buffer to store conditions sections names */
    buffer=g_strdup_printf(FILTER_SECTION_PREFIX "%s", FILTER_SECTION_MAX);
    /* section_name points to the beginning of the filter section name */
    /* tmp points to the space where filter number is appended */
    tmp = buffer + strlen(FILTER_SECTION_PREFIX);

    for(list = balsa_app.filters; list; list = list->next) {
	fil = (LibBalsaFilter*)(list->data);
	i=snprintf(tmp,tmp_len,"%d",nb++);
	libbalsa_conf_push_group(buffer);
	libbalsa_filter_save_config(fil);
	libbalsa_conf_pop_group();
    }
    libbalsa_conf_sync();
    /* This loop takes care of cleaning up old filter sections */
    while (TRUE) {
	i=snprintf(tmp,tmp_len,"%d",nb++);
	if (libbalsa_conf_has_group(buffer)) {
	    libbalsa_conf_remove_group(buffer);
	}
	else break;
    }
    libbalsa_conf_sync();
    g_free(buffer);
}

void
config_mailbox_filters_save(LibBalsaMailbox * mbox)
{
    gchar * tmp;

    g_return_if_fail(mbox);
    tmp = mailbox_filters_section_lookup(mbox->url ? mbox->url : mbox->name);
    if (!mbox->filters) {
	if (tmp) {
	    libbalsa_conf_remove_group(tmp);
	    g_free(tmp);
	}
	return;
    }
    if (!tmp) {
	/* If there was no existing filters section for this mailbox we create one */
	tmp=config_get_unused_group(MAILBOX_FILTERS_SECTION_PREFIX);
	libbalsa_conf_push_group(tmp);
	g_free(tmp);
	libbalsa_conf_set_string(MAILBOX_FILTERS_URL_KEY,mbox->url);
    }
    else {
	libbalsa_conf_push_group(tmp);
	g_free(tmp);
    }
    libbalsa_mailbox_filters_save_config(mbox);
    libbalsa_conf_pop_group();
    libbalsa_conf_sync();
}

static void
load_color(gchar * key, GdkColor * color)
{
    gchar *str;

    str = libbalsa_conf_get_string(key);
    if (g_ascii_strncasecmp(str, "rgb:", 4)
        || sscanf(str + 4, "%4hx/%4hx/%4hx", &color->red, &color->green,
                  &color->blue) != 3)
        gdk_color_parse(str, color);
    g_free(str);
}

static void
load_mru(GList **mru, const gchar * group)
{
    int count, i;
    char tmpkey[32];
    
    tmpkey[sizeof tmpkey - 1] = '\0';

    libbalsa_conf_push_group(group);
    count=d_get_gint("MRUCount", 0);
    for(i=0;i<count;i++) {
        gchar *val;
	snprintf(tmpkey, sizeof tmpkey - 1, "MRU%d", i + 1);
        if( (val = libbalsa_conf_get_string(tmpkey)) != NULL )
            (*mru)=g_list_append((*mru), val);
    }
    libbalsa_conf_pop_group();
}

static void
save_mru(GList * mru, const gchar * group)
{
    int i;
    char tmpkey[32];
    GList *ltmp;

    tmpkey[sizeof tmpkey - 1] = '\0';

    libbalsa_conf_push_group(group);
    for (ltmp = mru, i = 0; ltmp; ltmp = ltmp->next, i++) {
	snprintf(tmpkey, sizeof tmpkey - 1, "MRU%d", i + 1);
        libbalsa_conf_set_string(tmpkey, (gchar *) (ltmp->data));
    }

    libbalsa_conf_set_int("MRUCount", i);
    libbalsa_conf_pop_group();
}

/* check_for_old_sigs:
   function for old style signature conversion (executable sigs prefixed
   with '|') to new style (filename and a checkbox).
   this function just strips the leading '|'.
*/
static void 
check_for_old_sigs(GList * id_list_tmp)
{
    /* strip pipes and spaces,set executable flag if warranted */
    /* FIXME remove after a few stable releases.*/
    
    LibBalsaIdentity* id_tmp = NULL;
    
    for (id_list_tmp = balsa_app.identities; id_list_tmp; 
         id_list_tmp = id_list_tmp->next) {
       
        id_tmp = LIBBALSA_IDENTITY(id_list_tmp->data);
        if(!id_tmp->signature_path) continue;

        id_tmp->signature_path = g_strstrip(id_tmp->signature_path);
        if(*id_tmp->signature_path == '|'){
            printf("Found old style signature for identity: %s\n"\
                   "Converting: %s --> ", id_tmp->identity_name, 
                   id_tmp->signature_path);
            id_tmp->signature_path = g_strchug(id_tmp->signature_path+1);
            printf("%s \n", id_tmp->signature_path);
            
            /* set new-style executable var*/
            id_tmp->sig_executable=TRUE;
            printf("Setting converted signature as executable.\n");
        }
    }
}

#if HAVE_GNOME
void
config_defclient_save(void)
{
    static struct {
        const char *key, *val;
    } gconf_string[] = {
        {"/desktop/gnome/url-handlers/mailto/command",     "balsa -m \"%s\""},
        {"/desktop/gnome/url-handlers/mailto/description", "Email" }};
    static struct {
        const char *key; gboolean val;
    } gconf_bool[] = {
        {"/desktop/gnome/url-handlers/mailto/need-terminal", FALSE},
        {"/desktop/gnome/url-handlers/mailto/enabled",       TRUE}};

    if (balsa_app.default_client) {
        GError *err = NULL;
        GConfClient *gc;
        unsigned i;
        gc = gconf_client_get_default(); /* FIXME: error handling */
        if (gc == NULL) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Error opening GConf database\n"));
            return;
        }
        for(i=0; i<ELEMENTS(gconf_string); i++) {
            gconf_client_set_string(gc, gconf_string[i].key, 
                                    gconf_string[i].val, &err);
            if (err) {
                balsa_information(LIBBALSA_INFORMATION_WARNING,
                                  _("Error setting GConf field: %s\n"),
                                  err->message);
                g_error_free(err);
                return;
            }
        }
        for(i=0; i<ELEMENTS(gconf_bool); i++) {
            gconf_client_set_bool(gc, gconf_bool[i].key,
                                  gconf_bool[i].val, &err);
            if (err) {
                balsa_information(LIBBALSA_INFORMATION_WARNING,
                                  _("Error setting GConf field: %s\n"),
                                  err->message);
                g_error_free(err);
                return;
            }
            g_object_unref(gc);
        }
    }
}
#endif /* HAVE_GNOME */
