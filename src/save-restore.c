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

#include <string.h>
#include <gconf/gconf-client.h>
#include <gnome.h>
#include "balsa-app.h"
#include "save-restore.h"
#include "quote-color.h"
#include "toolbar-prefs.h"

#include "filter.h"
#include "filter-file.h"
#include "filter-funcs.h"
#include "mailbox-filter.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#define BALSA_CONFIG_PREFIX "balsa/"
#define FOLDER_SECTION_PREFIX "folder-"
#define MAILBOX_SECTION_PREFIX "mailbox-"
#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"
#define IDENTITY_SECTION_PREFIX "identity-"
#define VIEW_SECTION_PREFIX "view-"
#define VIEW_BY_URL_SECTION_PREFIX "viewByUrl-"

static gint config_section_init(const char* section_prefix, 
				gint (*cb)(const char*));
static gint config_global_load(void);
static gint config_folder_init(const gchar * prefix);
static gint config_mailbox_init(const gchar * prefix);
static gchar *config_get_unused_section(const gchar * prefix);

static void save_color(gchar * key, GdkColor * color);
static void load_color(gchar * key, GdkColor * color);
static void save_mru(GList *mru);
static void load_mru(GList **mru);

static void config_address_books_load(void);
static void config_address_books_save(void);
static void config_identities_load(void);

static void check_for_old_sigs(GList * id_list_tmp);

static void config_filters_load(void);

#define folder_section_path(mn) \
    BALSA_MAILBOX_NODE(mn)->config_prefix ? \
    g_strdup(BALSA_MAILBOX_NODE(mn)->config_prefix) : \
    config_get_unused_section(FOLDER_SECTION_PREFIX)

#define mailbox_section_path(mbox) \
    LIBBALSA_MAILBOX(mbox)->config_prefix ? \
    g_strdup(LIBBALSA_MAILBOX(mbox)->config_prefix) : \
    config_get_unused_section(MAILBOX_SECTION_PREFIX)

#define address_book_section_path(ab) \
    LIBBALSA_ADDRESS_BOOK(ab)->config_prefix ? \
    g_strdup(LIBBALSA_ADDRESS_BOOK(ab)->config_prefix) : \
    config_get_unused_section(ADDRESS_BOOK_SECTION_PREFIX)

gint config_load(void)
{
    return config_global_load();
}

void
config_load_sections(void)
{
    config_section_init(MAILBOX_SECTION_PREFIX, config_mailbox_init);
    config_section_init(FOLDER_SECTION_PREFIX,  config_folder_init);
}

static gint
d_get_gint(const gchar * key, gint def_val)
{
    gint def;
    gint res = gnome_config_get_int_with_default(key, &def);
    return def ? def_val : res;
}

/* save/load_toolbars:
   handle customized toolbars for main/message preview and compose windows.
*/
static struct {
    const gchar *key;
    GSList **current;
} toolbars[] = {
    { "toolbar-MainWindow",    &balsa_app.main_window_toolbar_current },
    { "toolbar-ComposeWindow", &balsa_app.compose_window_toolbar_current },
    { "toolbar-MessageWindow", &balsa_app.message_window_toolbar_current }
};

static void
save_toolbars(void)
{
    guint i, j;

    gnome_config_clean_section(BALSA_CONFIG_PREFIX "Toolbars/");
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Toolbars/");
    gnome_config_set_int("WrapButtonText",
                         balsa_app.toolbar_wrap_button_text);
    gnome_config_pop_prefix();

    for (i = 0; i < ELEMENTS(toolbars); i++) {
        GSList *list;
        gchar *key;

        key = g_strconcat(BALSA_CONFIG_PREFIX, toolbars[i].key, "/", NULL);
        gnome_config_clean_section(key);
        gnome_config_push_prefix(key);
        g_free(key);

        for (j = 0, list = *toolbars[i].current; list;
             j++, list = g_slist_next(list)) {
            key = g_strdup_printf("Item%d", j);
            gnome_config_set_string(key, list->data);
            g_free(key);
        }
        gnome_config_pop_prefix();
    }
}

static void
load_toolbars(void)
{
    guint i, j, items;
    char tmpkey[256];
    gchar *key;
    GSList **list;

    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Toolbars/");
    balsa_app.toolbar_wrap_button_text = d_get_gint("WrapButtonText", 1);
    gnome_config_pop_prefix();

    items = 0;
    for (i = 0; i < ELEMENTS(toolbars); i++) {
        key = g_strconcat(BALSA_CONFIG_PREFIX, toolbars[i].key, "/", NULL);
        gnome_config_push_prefix(key);
        g_free(key);

        list = toolbars[i].current;
        for (j = 0;; j++) {
            gchar *item;

            key = g_strdup_printf("Item%d", j);
            item = gnome_config_get_string(key);
            g_free(key);

            if (!item)
                break;
            *list = g_slist_append(*list, item);
            items++;
        }
        gnome_config_pop_prefix();
    }

    if (items)
        return;

    /* We didn't find new-style toolbar configs, so we'll try the old
     * style */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Toolbars/");
    for (i = 0; i < ELEMENTS(toolbars); i++) {
        guint type;

        sprintf(tmpkey, "Toolbar%dID", i);
        type = d_get_gint(tmpkey, -1);

        if (type >= ELEMENTS(toolbars)) {
            continue;
        }

        sprintf(tmpkey, "Toolbar%dItemCount", i);
        items = d_get_gint(tmpkey, 0);

        list = toolbars[type].current;
        for (j = 0; j < items; j++) {
            gchar *item;

            sprintf(tmpkey, "Toolbar%dItem%d", i, j);
            item = gnome_config_get_string(tmpkey);
            *list = g_slist_append(*list, item);
        }
    }
    gnome_config_pop_prefix();
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
        if(balsa_app.local_mail_directory == NULL
           || !LIBBALSA_IS_MAILBOX_LOCAL(*special)
           || strncmp(balsa_app.local_mail_directory, 
                      libbalsa_mailbox_local_get_path(*special),
                      strlen(balsa_app.local_mail_directory)) != 0) 
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
    gchar *prefix;

    prefix = address_book_section_path(ab);

    libbalsa_address_book_save_config(ab, prefix);

    g_free(prefix);

    gnome_config_sync();
}

void
config_address_book_delete(LibBalsaAddressBook * ab)
{
    if (ab->config_prefix) {
	gnome_config_clean_section(ab->config_prefix);
	gnome_config_private_clean_section(ab->config_prefix);
	gnome_config_sync();
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
	tmp = g_strdup_printf(BALSA_CONFIG_PREFIX MAILBOX_SECTION_PREFIX
			      "%s/", key_arg);

    gnome_config_clean_section(tmp);
    gnome_config_push_prefix(tmp);
    libbalsa_mailbox_save_config(mailbox, tmp);
    gnome_config_pop_prefix();
    g_free(tmp);

    gnome_config_sync();
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
	tmp = g_strdup_printf(BALSA_CONFIG_PREFIX FOLDER_SECTION_PREFIX
			      "%s/", key_arg);

    gnome_config_push_prefix(tmp);
    balsa_mailbox_node_save_config(mbnode, tmp);
    gnome_config_pop_prefix();
    g_free(tmp);

    gnome_config_sync();
    return TRUE;
}				/* config_mailbox_add */

/* removes from the configuration only */
gint config_mailbox_delete(const LibBalsaMailbox * mailbox)
{
    gchar *tmp;			/* the key in the mailbox section name */
    gint res;
    tmp = mailbox_section_path(mailbox);
    res = gnome_config_has_section(tmp);
    gnome_config_clean_section(tmp);
    gnome_config_sync();
    g_free(tmp);
    return res;
}				/* config_mailbox_delete */

gint
config_folder_delete(const BalsaMailboxNode * mbnode)
{
    gchar *tmp;			/* the key in the mailbox section name */
    gint res;
    tmp = folder_section_path(mbnode);
    res = gnome_config_has_section(tmp);
    gnome_config_clean_section(tmp);
    gnome_config_sync();
    g_free(tmp);
    return res;
}				/* config_folder_delete */

/* Update the configuration information for the specified mailbox. */
gint config_mailbox_update(LibBalsaMailbox * mailbox)
{
    gchar *key;			/* the key in the mailbox section name */
    gint res;

    key = mailbox_section_path(mailbox);
    res = gnome_config_has_section(key);
    gnome_config_clean_section(key);
    gnome_config_private_clean_section(key);
    gnome_config_push_prefix(key);
    libbalsa_mailbox_save_config(mailbox, key);
    g_free(key);
    gnome_config_pop_prefix();
    gnome_config_sync();
    return res;
}				/* config_mailbox_update */

/* Update the configuration information for the specified folder. */
gint config_folder_update(BalsaMailboxNode * mbnode)
{
    gchar *key;			/* the key in the mailbox section name */
    gint res;

    key = folder_section_path(mbnode);
    res = gnome_config_has_section(key);
    gnome_config_clean_section(key);
    gnome_config_private_clean_section(key);
    gnome_config_push_prefix(key);
    balsa_mailbox_node_save_config(mbnode, key);
    g_free(key);
    gnome_config_pop_prefix();
    gnome_config_sync();
    return res;
}				/* config_folder_update */

/* This function initializes all the mailboxes internally, going through
   the list of all the mailboxes in the configuration file one by one. */

static gint
config_section_init(const char* section_prefix, gint (*cb)(const char*))
{
    void *iterator;
    gchar *key, *val, *tmp;
    int pref_len = strlen(section_prefix);

    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {
	if (strncmp(key, section_prefix, pref_len) == 0) {
	    tmp = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);
	    cb(tmp);
	    g_free(tmp);
	}
	g_free(key);
	g_free(val);
    }
    return TRUE;
}				/* config_section_init */

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
    write(mail_thread_pipes[1], (void *) &message, sizeof(void *));
#else
    while(gtk_events_pending())
        gtk_main_iteration_do(FALSE);
#endif
}

static void
pop3_config_updated(LibBalsaMailboxPop3* mailbox)
{
#ifdef BALSA_USE_THREADS
    MailThreadMessage *threadmsg;
    threadmsg = g_new(MailThreadMessage, 1);
    threadmsg->message_type = LIBBALSA_NTFY_UPDATECONFIG;
    threadmsg->mailbox = (void *) mailbox;
    threadmsg->message_string[0] = '\0';
    write(mail_thread_pipes[1], (void *) &threadmsg,
          sizeof(void *));
#else
    config_mailbox_update(LIBBALSA_MAILBOX(mailbox));
#endif
}

/* Initialize the specified mailbox, creating the internal data
   structures which represent the mailbox. */
static gint
config_mailbox_init(const gchar * prefix)
{
    LibBalsaMailbox *mailbox;
    const gchar *key =
	prefix + strlen(BALSA_CONFIG_PREFIX MAILBOX_SECTION_PREFIX);

    g_return_val_if_fail(prefix != NULL, FALSE);

    mailbox = libbalsa_mailbox_new_from_config(prefix);
    if (mailbox == NULL)
	return FALSE;
    if (LIBBALSA_IS_MAILBOX_REMOTE(mailbox)) {
	LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
        libbalsa_server_connect_signals(server,
                                        G_CALLBACK(ask_password), mailbox);
	g_signal_connect_swapped(server, "set-host",
                                 G_CALLBACK(config_mailbox_update),
				 mailbox);
    }

    if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
        g_signal_connect(G_OBJECT(mailbox),
                         "config-changed", G_CALLBACK(pop3_config_updated),
                         mailbox);
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
	if (strcmp(INBOX_NAME "/", key) == 0)
	    special = &balsa_app.inbox;
	else if (strcmp(OUTBOX_NAME "/", key) == 0)
	    special = &balsa_app.outbox;
	else if (strcmp(SENTBOX_NAME "/", key) == 0)
	    special = &balsa_app.sentbox;
	else if (strcmp(DRAFTS_NAME "/", key) == 0)
	    special = &balsa_app.draftbox;
	else if (strcmp(TRASH_NAME "/", key) == 0) {
	    special = &balsa_app.trash;
            libbalsa_filters_set_trash(balsa_app.trash);
	}

	if (special) {
	    /* Designate as special before appending to the mblist, so
	     * that view->show gets set correctly for sentbox, draftbox,
	     * and outbox. */
	    *special = mailbox;
	    g_object_add_weak_pointer(G_OBJECT(mailbox), (gpointer) special);
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
	g_signal_connect_swapped(folder->server, "set-host",
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
    balsa_information(LIBBALSA_INFORMATION_WARNING, text);
    gdk_threads_leave();
    return FALSE;
}

static gint
config_global_load(void)
{
    gboolean def_used;
    guint tmp;

    config_address_books_load();
    config_identities_load();

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
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "InformationMessages/");

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

    gnome_config_pop_prefix();

    /* Section for geometry ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Geometry/");

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
    balsa_app.mw_width = gnome_config_get_int("MainWindowWidth=640");
    balsa_app.mw_height = gnome_config_get_int("MainWindowHeight=480");
    balsa_app.mblist_width = gnome_config_get_int("MailboxListWidth=130");
    /* sendmsg window sizes */
    balsa_app.sw_width = gnome_config_get_int("SendMsgWindowWidth=640");
    balsa_app.sw_height = gnome_config_get_int("SendMsgWindowHeight=480");
    balsa_app.message_window_width =
        gnome_config_get_int("MessageWindowWidth=400");
    balsa_app.message_window_height =
        gnome_config_get_int("MessageWindowHeight=500");
    /* FIXME: PKGW: why comment this out? Breaks my Transfer context menu. */
    if (balsa_app.mblist_width < 100)
	balsa_app.mblist_width = 170;

    balsa_app.notebook_height = gnome_config_get_int("NotebookHeight=170");
    /*FIXME: Why is this here?? */
    if (balsa_app.notebook_height < 100)
	balsa_app.notebook_height = 200;

    gnome_config_pop_prefix();

    /* Message View options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MessageDisplay/");

    /* ... How we format dates */
    g_free(balsa_app.date_string);
    balsa_app.date_string =
	gnome_config_get_string("DateFormat=" DEFAULT_DATE_FORMAT);
    libbalsa_mailbox_date_format = balsa_app.date_string;

    /* ... Headers to show */
    balsa_app.shown_headers = d_get_gint("ShownHeaders", HEADERS_SELECTED);

    g_free(balsa_app.selected_headers);
    {                           /* scope */
        gchar *tmp = gnome_config_get_string("SelectedHeaders="
                                             DEFAULT_SELECTED_HDRS);
        balsa_app.selected_headers = g_ascii_strdown(tmp, -1);
        g_free(tmp);
    }

    balsa_app.expand_tree = gnome_config_get_bool("ExpandTree=false");

    {                             /* scope */
        guint type =
            gnome_config_get_int_with_default("SortField", &def_used);
        if (!def_used)
            libbalsa_mailbox_set_sort_field(NULL, type);
        type =
            gnome_config_get_int_with_default("ThreadingType", &def_used);
        if (!def_used)
            libbalsa_mailbox_set_threading_type(NULL, type);
    }

    /* ... Quote colouring */
    g_free(balsa_app.quote_regex);
    balsa_app.quote_regex =
	gnome_config_get_string("QuoteRegex=" DEFAULT_QUOTE_REGEX);

    /* Obsolete. */
    gnome_config_get_bool_with_default("RecognizeRFC2646FormatFlowed=true",
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
	gnome_config_get_string("MessageFont=" DEFAULT_MESSAGE_FONT);
    g_free(balsa_app.subject_font);
    balsa_app.subject_font =
	gnome_config_get_string("SubjectFont=" DEFAULT_SUBJECT_FONT);

    /* ... wrap words */
    balsa_app.browse_wrap = gnome_config_get_bool("WordWrap=true");
    balsa_app.browse_wrap_length = gnome_config_get_int("WordWrapLength=79");
    if (balsa_app.browse_wrap_length < 40)
	balsa_app.browse_wrap_length = 40;

    /* ... handling of Multipart/Alternative */
    balsa_app.display_alt_plain = 
	gnome_config_get_bool("DisplayAlternativeAsPlain=false");

    /* ... handling of broken mails with 8-bit chars */
    balsa_app.convert_unknown_8bit = 
	gnome_config_get_bool("ConvertUnknown8Bit=false");
    balsa_app.convert_unknown_8bit_codeset =
	gnome_config_get_int("ConvertUnknown8BitCodeset=" DEFAULT_BROKEN_CODESET);
    libbalsa_set_fallback_codeset(balsa_app.convert_unknown_8bit_codeset);
    gnome_config_pop_prefix();

    /* Interface Options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Interface/");

    /* ... interface elements to show */
    balsa_app.previewpane = gnome_config_get_bool("ShowPreviewPane=true");
    balsa_app.show_mblist = gnome_config_get_bool("ShowMailboxList=true");
    balsa_app.show_notebook_tabs = gnome_config_get_bool("ShowTabs=false");

    /* ... alternative layout of main window */
    balsa_app.alternative_layout = gnome_config_get_bool("AlternativeLayout=false");
    balsa_app.view_message_on_open = gnome_config_get_bool("ViewMessageOnOpen=true");
    balsa_app.pgdownmod = gnome_config_get_bool("PageDownMod=false");
    balsa_app.pgdown_percent = gnome_config_get_int("PageDownPercent=50");
    if (balsa_app.pgdown_percent < 10)
	balsa_app.pgdown_percent = 10;
#if defined(ENABLE_TOUCH_UI)
    balsa_app.do_file_format_check =
        gnome_config_get_bool("FileFormatCheck=true");
    balsa_app.enable_view_filter = gnome_config_get_bool("ViewFilter=false");
#endif /* ENABLE_TOUCH_UI */

    /* ... Progress Window Dialog */
    balsa_app.pwindow_option = d_get_gint("ProgressWindow", WHILERETR);

    /* ... deleting messages: defaults enshrined here */
    tmp = libbalsa_mailbox_get_filter(NULL);
    if (gnome_config_get_bool("HideDeleted=true"))
	tmp |= (1 << 0);
    else
	tmp &= ~(1 << 0);
    libbalsa_mailbox_set_filter(NULL, tmp);

    balsa_app.expunge_on_close =
        gnome_config_get_bool("ExpungeOnClose=true");
    balsa_app.expunge_auto = gnome_config_get_bool("AutoExpunge=true");
    /* timeout in munutes (was hours, so fall back if needed) */
    balsa_app.expunge_timeout =
        gnome_config_get_int("AutoExpungeMinutes") * 60;
    if (!balsa_app.expunge_timeout)
	balsa_app.expunge_timeout = 
	    gnome_config_get_int("AutoExpungeHours=2") * 3600;

    balsa_app.mw_action_after_move = gnome_config_get_int(
        "MessageWindowActionAfterMove");

    gnome_config_pop_prefix();

    /* Source browsing ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "SourcePreview/");
    balsa_app.source_escape_specials = 
        gnome_config_get_bool("EscapeSpecials=true");
    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");

    /* ... Printing */
    g_free(balsa_app.paper_size);
    balsa_app.paper_size =
	gnome_config_get_string("PaperSize=" DEFAULT_PAPER_SIZE);
    g_free(balsa_app.margin_left);
    balsa_app.margin_left   = gnome_config_get_string("LeftMargin");
    g_free(balsa_app.margin_top);
    balsa_app.margin_top    = gnome_config_get_string("TopMargin");
    g_free(balsa_app.margin_right);
    balsa_app.margin_right  = gnome_config_get_string("RightMargin");
    g_free(balsa_app.margin_bottom);
    balsa_app.margin_bottom = gnome_config_get_string("BottomMargin");
    g_free(balsa_app.print_unit);
    balsa_app.print_unit    = gnome_config_get_string("PrintUnit");
    g_free(balsa_app.print_layout);
    balsa_app.print_layout  = gnome_config_get_string("PrintLayout");
    g_free(balsa_app.paper_orientation);
    balsa_app.paper_orientation  = gnome_config_get_string("PaperOrientation");
    g_free(balsa_app.page_orientation);
    balsa_app.page_orientation   = gnome_config_get_string("PageOrientation");

    g_free(balsa_app.print_header_font);
    balsa_app.print_header_font =
        gnome_config_get_string("PrintHeaderFont="
                                DEFAULT_PRINT_HEADER_FONT);
    g_free(balsa_app.print_footer_font);
    balsa_app.print_footer_font =
        gnome_config_get_string("PrintFooterFont="
                                DEFAULT_PRINT_FOOTER_FONT);
    g_free(balsa_app.print_body_font);
    balsa_app.print_body_font =
        gnome_config_get_string("PrintBodyFont="
                                DEFAULT_PRINT_BODY_FONT);
    balsa_app.print_highlight_cited =
        gnome_config_get_bool("PrintHighlightCited=false");
    gnome_config_pop_prefix();

    /* Spelling options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Spelling/");

    balsa_app.module = d_get_gint("PspellModule", DEFAULT_PSPELL_MODULE);
    balsa_app.suggestion_mode =
	d_get_gint("PspellSuggestMode", DEFAULT_PSPELL_SUGGEST_MODE);
    balsa_app.ignore_size =
	d_get_gint("PspellIgnoreSize", DEFAULT_PSPELL_IGNORE_SIZE);
    balsa_app.check_sig =
	d_get_gint("SpellCheckSignature", DEFAULT_CHECK_SIG);
    balsa_app.check_quoted =
	d_get_gint("SpellCheckQuoted", DEFAULT_CHECK_QUOTED);

    gnome_config_pop_prefix();

    /* Mailbox checking ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MailboxList/");

    /* ... show mailbox content info */
    balsa_app.mblist_show_mb_content_info =
	gnome_config_get_bool("ShowMailboxContentInfo=true");

    gnome_config_pop_prefix();

    /* Maibox checking options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MailboxChecking/");

    balsa_app.notify_new_mail_dialog =
	d_get_gint("NewMailNotificationDialog", 0);
    balsa_app.notify_new_mail_sound =
	d_get_gint("NewMailNotificationSound", 1);
    balsa_app.check_mail_upon_startup =
	gnome_config_get_bool("OnStartup=false");
    balsa_app.check_mail_auto = gnome_config_get_bool("Auto=false");
    balsa_app.check_mail_timer = gnome_config_get_int("AutoDelay=10");
    if (balsa_app.check_mail_timer < 1)
	balsa_app.check_mail_timer = 10;
    if (balsa_app.check_mail_auto)
	update_timer(TRUE, balsa_app.check_mail_timer);
    balsa_app.check_imap=d_get_gint("CheckIMAP", 1);
    balsa_app.check_imap_inbox=d_get_gint("CheckIMAPInbox", 0);
    balsa_app.quiet_background_check=d_get_gint("QuietBackgroundCheck", 0);
    balsa_app.msg_size_limit=d_get_gint("POPMsgSizeLimit", 20000);
    gnome_config_pop_prefix();

    /* folder scanning */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "FolderScanning/");
    balsa_app.local_scan_depth = d_get_gint("LocalScanDepth", 1);
    balsa_app.imap_scan_depth = d_get_gint("ImapScanDepth", 1);
    gnome_config_pop_prefix();

    /* how to react if a message with MDN request is displayed */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MDNReply/");
    balsa_app.mdn_reply_clean = gnome_config_get_int("Clean=1");
    balsa_app.mdn_reply_notclean = gnome_config_get_int("Suspicious=0");
    gnome_config_pop_prefix();

    /* Sending options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Sending/");

#if ENABLE_ESMTP
    /* ... SMTP server */
    g_free(balsa_app.smtp_server);
    balsa_app.smtp_server =
	gnome_config_get_string_with_default("ESMTPServer=localhost:25", 
					     &def_used);
    if(def_used) {
	/* we need to check for old format, 1.1.4-compatible settings and
	   convert them if needed.
	*/
	gchar* old_server = gnome_config_get_string("SMTPServer");
	if(old_server) {
	    int port = gnome_config_get_int("SMTPPort=25");
	    g_free(balsa_app.smtp_server);
	    balsa_app.smtp_server = g_strdup_printf("%s:%d",
						    old_server, port);
	    g_free(old_server);
	g_warning("Converted old SMTP server config to ESMTP format. Verify the correctness.");
	}
    }
    g_free(balsa_app.smtp_user);
    balsa_app.smtp_user = gnome_config_get_string("ESMTPUser");
    g_free(balsa_app.smtp_passphrase);
    balsa_app.smtp_passphrase = 
        gnome_config_private_get_string("ESMTPPassphrase");
    if(balsa_app.smtp_passphrase) {
        gchar* tmp = libbalsa_rot(balsa_app.smtp_passphrase);
        g_free(balsa_app.smtp_passphrase); 
        balsa_app.smtp_passphrase = tmp;
    }
#ifndef BREAK_BACKWARD_COMPATIBILITY_AT_14
    if(!balsa_app.smtp_passphrase)
        balsa_app.smtp_passphrase = 
            gnome_config_get_string("ESMTPPassphrase");
#endif
    /* default set to "Use TLS if possible" */
    balsa_app.smtp_tls_mode = gnome_config_get_int("ESMTPTLSMode=1");
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    balsa_app.smtp_certificate_passphrase = 
        gnome_config_private_get_string("ESMTPCertificatePassphrase");
    if(balsa_app.smtp_certificate_passphrase) {
        gchar* tmp = libbalsa_rot(balsa_app.smtp_certificate_passphrase);
        g_free(balsa_app.smtp_certificate_passphrase);
        balsa_app.smtp_certificate_passphrase = tmp;
    }
#endif
#endif
    /* ... outgoing mail */
    balsa_app.encoding_style = gnome_config_get_int("EncodingStyle=2");
    /* convert libmutt Quoted Printable to GMime type, for compatible */
    if (balsa_app.encoding_style==3)
        balsa_app.encoding_style = GMIME_PART_ENCODING_QUOTEDPRINTABLE;
    balsa_app.wordwrap = gnome_config_get_bool("WordWrap=true");
    balsa_app.wraplength = gnome_config_get_int("WrapLength=72");
    if (balsa_app.wraplength < 40)
	balsa_app.wraplength = 40;

    /* Obsolete. */
    gnome_config_get_bool_with_default("SendRFC2646FormatFlowed=true",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option not to send \"format=flowed\" is now "
                     "on the Options menu of the compose window."));

    balsa_app.autoquote = 
	gnome_config_get_bool("AutoQuote=true");
    balsa_app.reply_strip_html = 
	gnome_config_get_bool("StripHtmlInReply=true");
    balsa_app.forward_attached = 
	gnome_config_get_bool("ForwardAttached=true");

	balsa_app.always_queue_sent_mail = d_get_gint("AlwaysQueueSentMail", 0);
	balsa_app.copy_to_sentbox = d_get_gint("CopyToSentbox", 1);

    gnome_config_pop_prefix();

    /* Compose window ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Compose/");

    balsa_app.edit_headers = 
        gnome_config_get_bool("ExternEditorEditHeaders=false");

    g_free(balsa_app.quote_str);
    balsa_app.quote_str = gnome_config_get_string("QuoteString=> ");
    g_free(balsa_app.compose_headers);
    balsa_app.compose_headers =
	gnome_config_get_string("ComposeHeaders=to subject cc");

    /* Obsolete. */
    gnome_config_get_bool_with_default("RequestDispositionNotification=false",
                                       &def_used);
    if (!def_used)
        g_idle_add((GSourceFunc) config_warning_idle,
                   _("The option to request a MDN is now on "
                     "the Options menu of the compose window."));


    gnome_config_pop_prefix();

    /* Global config options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Globals/");

    /* directory */
    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory = gnome_config_get_string("MailDir");

    if(!balsa_app.local_mail_directory) {
	gnome_config_pop_prefix();
	return FALSE;
    }
    balsa_app.root_node =
        balsa_mailbox_node_new_from_dir(balsa_app.local_mail_directory);

#if defined(ENABLE_TOUCH_UI)
     balsa_app.open_inbox_upon_startup =
	gnome_config_get_bool("OpenInboxOnStartup=true");
#else
     balsa_app.open_inbox_upon_startup =
	gnome_config_get_bool("OpenInboxOnStartup=false");
#endif /* ENABLE_TOUCH_UI */
    /* debugging enabled */
    balsa_app.debug = gnome_config_get_bool("Debug=false");

    balsa_app.close_mailbox_auto = gnome_config_get_bool("AutoCloseMailbox=true");
    /* timeouts in minutes in config file for backwards compat */
    balsa_app.close_mailbox_timeout = gnome_config_get_int("AutoCloseMailboxTimeout=10") * 60;
    
    balsa_app.remember_open_mboxes =
	gnome_config_get_bool("RememberOpenMailboxes=false");

    balsa_app.empty_trash_on_exit =
	gnome_config_get_bool("EmptyTrash=false");

    /* This setting is now per address book */
    gnome_config_clean_key("AddressBookDistMode");

    gnome_config_pop_prefix();
    
    /* Toolbars */
    load_toolbars();

    /* Last used paths options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Paths/");
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = gnome_config_get_string("AttachDir");
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = gnome_config_get_string("SavePartDir");
    gnome_config_pop_prefix();

	/* Folder MRU */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "FolderMRU/");
    load_mru(&balsa_app.folder_mru);
    gnome_config_pop_prefix();

	/* FCC MRU */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "FccMRU/");
    load_mru(&balsa_app.fcc_mru);
    gnome_config_pop_prefix();

    return TRUE;
}				/* config_global_load */

gint
config_save(void)
{
    gint i;

    config_address_books_save();
    config_identities_save();

    /* Section for the balsa_information() settings... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "InformationMessages/");
    gnome_config_set_int("ShowInformationMessages",
			 balsa_app.information_message);
    gnome_config_set_int("ShowWarningMessages", balsa_app.warning_message);
    gnome_config_set_int("ShowErrorMessages", balsa_app.error_message);
    gnome_config_set_int("ShowDebugMessages", balsa_app.debug_message);
    gnome_config_set_int("ShowFatalMessages", balsa_app.fatal_message);
    gnome_config_pop_prefix();

    /* Section for geometry ... */
    /* FIXME: Saving window sizes is the WM's job?? */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Geometry/");

    /* ... column width settings */
    gnome_config_set_int("MailboxListNameWidth",
			 balsa_app.mblist_name_width);
    gnome_config_set_int("MailboxListNewMsgWidth",
			 balsa_app.mblist_newmsg_width);
    gnome_config_set_int("MailboxListTotalMsgWidth",
			 balsa_app.mblist_totalmsg_width);
    gnome_config_set_int("IndexNumWidth", balsa_app.index_num_width);
    gnome_config_set_int("IndexStatusWidth", balsa_app.index_status_width);
    gnome_config_set_int("IndexAttachmentWidth",
			 balsa_app.index_attachment_width);
    gnome_config_set_int("IndexFromWidth", balsa_app.index_from_width);
    gnome_config_set_int("IndexSubjectWidth",
			 balsa_app.index_subject_width);
    gnome_config_set_int("IndexDateWidth", balsa_app.index_date_width);
    gnome_config_set_int("IndexSizeWidth", balsa_app.index_size_width);
    gnome_config_set_int("MainWindowWidth", balsa_app.mw_width);
    gnome_config_set_int("MainWindowHeight", balsa_app.mw_height);
    gnome_config_set_int("MailboxListWidth", balsa_app.mblist_width);
    gnome_config_set_int("SendMsgWindowWidth", balsa_app.sw_width);
    gnome_config_set_int("SendMsgWindowHeight", balsa_app.sw_height);
    gnome_config_set_int("MessageWindowWidth",
                         balsa_app.message_window_width);
    gnome_config_set_int("MessageWindowHeight",
                         balsa_app.message_window_height);
    gnome_config_set_int("NotebookHeight", balsa_app.notebook_height);

    gnome_config_pop_prefix();

    /* Message View options ... */
    gnome_config_clean_section(BALSA_CONFIG_PREFIX "MessageDisplay/");
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MessageDisplay/");

    gnome_config_set_string("DateFormat", balsa_app.date_string);
    gnome_config_set_int("ShownHeaders", balsa_app.shown_headers);
    gnome_config_set_string("SelectedHeaders", balsa_app.selected_headers);
    gnome_config_set_bool("ExpandTree", balsa_app.expand_tree);
    gnome_config_set_int("SortField",
			 libbalsa_mailbox_get_sort_field(NULL));
    gnome_config_set_int("ThreadingType",
			 libbalsa_mailbox_get_threading_type(NULL));
    gnome_config_set_string("QuoteRegex", balsa_app.quote_regex);
    gnome_config_set_string("MessageFont", balsa_app.message_font);
    gnome_config_set_string("SubjectFont", balsa_app.subject_font);
    gnome_config_set_bool("WordWrap", balsa_app.browse_wrap);
    gnome_config_set_int("WordWrapLength", balsa_app.browse_wrap_length);

    for(i=0;i<MAX_QUOTED_COLOR;i++) {
	gchar *text = g_strdup_printf("QuotedColor%d", i);
	save_color(text, &balsa_app.quoted_color[i]);
	g_free(text);
    }

    save_color("UrlColor", &balsa_app.url_color);
    save_color("BadAddressColor", &balsa_app.bad_address_color);

    /* ... handling of Multipart/Alternative */
    gnome_config_set_bool("DisplayAlternativeAsPlain",
			  balsa_app.display_alt_plain);

    /* ... handling of broken mails with 8-bit chars */
    gnome_config_set_bool("ConvertUnknown8Bit", balsa_app.convert_unknown_8bit);
    gnome_config_set_int("ConvertUnknown8BitCodeset", 
			    balsa_app.convert_unknown_8bit_codeset);

    gnome_config_pop_prefix();

    /* Interface Options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Interface/");

    gnome_config_set_bool("ShowPreviewPane", balsa_app.previewpane);
    gnome_config_set_bool("ShowMailboxList", balsa_app.show_mblist);
    gnome_config_set_bool("ShowTabs", balsa_app.show_notebook_tabs);
    gnome_config_set_int("ProgressWindow", balsa_app.pwindow_option);
    gnome_config_set_bool("AlternativeLayout", balsa_app.alternative_layout);
    gnome_config_set_bool("ViewMessageOnOpen", balsa_app.view_message_on_open);
    gnome_config_set_bool("PageDownMod", balsa_app.pgdownmod);
    gnome_config_set_int("PageDownPercent", balsa_app.pgdown_percent);
#if defined(ENABLE_TOUCH_UI)
    gnome_config_set_bool("FileFormatCheck", balsa_app.do_file_format_check);
    gnome_config_set_bool("ViewFilter",      balsa_app.enable_view_filter);
#endif /* ENABLE_TOUCH_UI */
    gnome_config_set_bool("HideDeleted", libbalsa_mailbox_get_filter(NULL) & 1);
    gnome_config_set_bool("ExpungeOnClose", balsa_app.expunge_on_close);
    gnome_config_set_bool("AutoExpunge", balsa_app.expunge_auto);
    gnome_config_set_int("AutoExpungeMinutes",
                         balsa_app.expunge_timeout / 60);
    gnome_config_set_int("MessageWindowActionAfterMove",
        balsa_app.mw_action_after_move);

    gnome_config_pop_prefix();

    /* Source browsing ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "SourcePreview/");
    gnome_config_set_bool("EscapeSpecials",
                          balsa_app.source_escape_specials);
    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");
    gnome_config_set_string("PaperSize",balsa_app.paper_size);
    if(balsa_app.margin_left)
	gnome_config_set_string("LeftMargin",   balsa_app.margin_left);
    if(balsa_app.margin_top)
	gnome_config_set_string("TopMargin",    balsa_app.margin_top);
    if(balsa_app.margin_bottom)
	gnome_config_set_string("RightMargin",  balsa_app.margin_right);
    if(balsa_app.margin_bottom)
	gnome_config_set_string("BottomMargin", balsa_app.margin_bottom);
    if(balsa_app.print_unit)
	gnome_config_set_string("PrintUnit",    balsa_app.print_unit);
    if(balsa_app.print_layout)
	gnome_config_set_string("PrintLayout", balsa_app.print_layout);
    if(balsa_app.margin_bottom)
	gnome_config_set_string("PaperOrientation", 
				balsa_app.paper_orientation);
    if(balsa_app.margin_bottom)
	gnome_config_set_string("PageOrientation", balsa_app.page_orientation);

    gnome_config_set_string("PrintHeaderFont",
                            balsa_app.print_header_font);
    gnome_config_set_string("PrintBodyFont", balsa_app.print_body_font);
    gnome_config_set_string("PrintFooterFont",
                            balsa_app.print_footer_font);
    gnome_config_set_bool("PrintHighlightCited",
                          balsa_app.print_highlight_cited);
    gnome_config_pop_prefix();

    /* Spelling options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Spelling/");

    gnome_config_set_int("PspellModule", balsa_app.module);
    gnome_config_set_int("PspellSuggestMode", balsa_app.suggestion_mode);
    gnome_config_set_int("PspellIgnoreSize", balsa_app.ignore_size);
    gnome_config_set_int("SpellCheckSignature", balsa_app.check_sig);
    gnome_config_set_int("SpellCheckQuoted", balsa_app.check_quoted);

    gnome_config_pop_prefix();

    /* Mailbox list options */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MailboxList/");

    gnome_config_set_bool("ShowMailboxContentInfo",
			  balsa_app.mblist_show_mb_content_info);

    gnome_config_pop_prefix();

    /* Maibox checking options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MailboxChecking/");

	gnome_config_set_int("NewMailNotificationDialog",
				balsa_app.notify_new_mail_dialog);
	gnome_config_set_int("NewMailNotificationSound",
				balsa_app.notify_new_mail_sound);
    gnome_config_set_bool("OnStartup", balsa_app.check_mail_upon_startup);
    gnome_config_set_bool("Auto", balsa_app.check_mail_auto);
    gnome_config_set_int("AutoDelay", balsa_app.check_mail_timer);
    gnome_config_set_int("CheckIMAP", balsa_app.check_imap);
    gnome_config_set_int("CheckIMAPInbox", balsa_app.check_imap_inbox);
    gnome_config_set_int("QuietBackgroundCheck",
			 balsa_app.quiet_background_check);
    gnome_config_set_int("POPMsgSizeLimit", balsa_app.msg_size_limit);

    gnome_config_pop_prefix();

    /* folder scanning */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "FolderScanning/");
    gnome_config_set_int("LocalScanDepth", balsa_app.local_scan_depth);
    gnome_config_set_int("ImapScanDepth", balsa_app.imap_scan_depth);
    gnome_config_pop_prefix();

    /* how to react if a message with MDN request is displayed */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MDNReply/");
    gnome_config_set_int("Clean", balsa_app.mdn_reply_clean);
    gnome_config_set_int("Suspicious", balsa_app.mdn_reply_notclean);
    gnome_config_pop_prefix();

    /* Sending options ... */
    gnome_config_clean_section(BALSA_CONFIG_PREFIX "Sending/");
    gnome_config_private_clean_section(BALSA_CONFIG_PREFIX "Sending/");
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Sending/");
#if ENABLE_ESMTP
    gnome_config_set_string("ESMTPServer", balsa_app.smtp_server);
    gnome_config_set_string("ESMTPUser", balsa_app.smtp_user);
    if(balsa_app.smtp_passphrase) {
        gchar* tmp = libbalsa_rot(balsa_app.smtp_passphrase);
        gnome_config_private_set_string("ESMTPPassphrase", tmp);
        g_free(tmp);
    }
    gnome_config_set_int("ESMTPTLSMode", balsa_app.smtp_tls_mode);
#if HAVE_SMTP_TLS_CLIENT_CERTIFICATE
    if(balsa_app.smtp_certificate_passphrase) {
        gchar* tmp = libbalsa_rot(balsa_app.smtp_certificate_passphrase);
        gnome_config_private_set_string("ESMTPCertificatePassphrase", tmp);
        g_free(tmp);
    }
#endif 
#endif 
    gnome_config_set_int("EncodingStyle", balsa_app.encoding_style);
    gnome_config_set_bool("WordWrap", balsa_app.wordwrap);
    gnome_config_set_int("WrapLength", balsa_app.wraplength);
    gnome_config_set_bool("AutoQuote", balsa_app.autoquote);
    gnome_config_set_bool("StripHtmlInReply", balsa_app.reply_strip_html);
    gnome_config_set_bool("ForwardAttached", balsa_app.forward_attached);

	gnome_config_set_int("AlwaysQueueSentMail", balsa_app.always_queue_sent_mail);
	gnome_config_set_int("CopyToSentbox", balsa_app.copy_to_sentbox);
    gnome_config_pop_prefix();

    /* Compose window ... */
    gnome_config_clean_section(BALSA_CONFIG_PREFIX "Compose/");
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Compose/");

    gnome_config_set_string("ComposeHeaders", balsa_app.compose_headers);
    gnome_config_set_bool("ExternEditorEditHeaders", balsa_app.edit_headers);
    gnome_config_set_string("QuoteString", balsa_app.quote_str);

    gnome_config_pop_prefix();

    /* Global config options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Globals/");

    gnome_config_set_string("MailDir", balsa_app.local_mail_directory);

    gnome_config_set_bool("OpenInboxOnStartup", 
                          balsa_app.open_inbox_upon_startup);
    gnome_config_set_bool("Debug", balsa_app.debug);

    gnome_config_set_bool("AutoCloseMailbox", balsa_app.close_mailbox_auto);
    gnome_config_set_int("AutoCloseMailboxTimeout", balsa_app.close_mailbox_timeout/60);

    gnome_config_set_bool("RememberOpenMailboxes",
			  balsa_app.remember_open_mboxes);
    gnome_config_set_bool("EmptyTrash", balsa_app.empty_trash_on_exit);

    if (balsa_app.default_address_book) {
	gnome_config_set_string("DefaultAddressBook",
				balsa_app.default_address_book->
				config_prefix +
				strlen(BALSA_CONFIG_PREFIX));
    } else {
	gnome_config_clean_key("DefaultAddressBook");
    }

    gnome_config_pop_prefix();

    /* Toolbars */
    save_toolbars();

    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Paths/");
    if(balsa_app.attach_dir)
	gnome_config_set_string("AttachDir", balsa_app.attach_dir);
    if(balsa_app.save_dir)
	gnome_config_set_string("SavePartDir", balsa_app.save_dir);
    gnome_config_pop_prefix();

	
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "FolderMRU/");
    save_mru(balsa_app.folder_mru);
    gnome_config_pop_prefix();

    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "FccMRU/");
    save_mru(balsa_app.fcc_mru);
    gnome_config_pop_prefix();

    gnome_config_sync();
    return TRUE;
}				/* config_global_save */


/* must use a sensible prefix, or this goes weird */
static gchar *
config_get_unused_section(const gchar * prefix)
{
    int max = 0, curr;
    void *iterator;
    gchar *name, *key, *val;
    int pref_len = strlen(prefix);

    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);

    max = 0;
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {
	if (strncmp(key, prefix, pref_len) == 0) {
	    if (strlen(key + (pref_len - 1)) > 1
		&& (curr = atoi(key + pref_len) + 1) && curr > max)
		max = curr;
	}
	g_free(key);
	g_free(val);
    }
    name = g_strdup_printf(BALSA_CONFIG_PREFIX "%s%d/", prefix, max);
    if (balsa_app.debug)
	g_print("config_mailbox_get_highest_number: name='%s'\n", name);
    return name;
}

static void
config_clean_sections(const gchar* section_prefix)
{
    void *iterator;
    gchar *key, *val, *prefix;
    int pref_len = strlen(section_prefix);
    GList* old_sections = NULL, *list;

    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {
	if (strncmp(key, section_prefix, pref_len) == 0) {
	    prefix = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);
	    old_sections = g_list_prepend(old_sections, prefix);
	}
	g_free(key);
	g_free(val);
    }
    for(list=old_sections; list; list = g_list_next(list)) {
	gnome_config_clean_section(list->data);
	g_free(list->data);
    }
    g_list_free(old_sections);
}

static void
config_address_books_load(void)
{
    LibBalsaAddressBook *address_book;
    gchar *default_address_book_prefix;
    void *iterator;
    gchar *key, *val, *tmp;
    int pref_len = strlen(ADDRESS_BOOK_SECTION_PREFIX);

    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Globals/");
    tmp = gnome_config_get_string("DefaultAddressBook");
    default_address_book_prefix =
	g_strconcat(BALSA_CONFIG_PREFIX, tmp, NULL);
    g_free(tmp);
    gnome_config_pop_prefix();

    /* Free old data in case address books were set by eg. config druid. */
    if(balsa_app.address_book_list) {
        g_list_foreach(balsa_app.address_book_list,
                       (GFunc)g_object_unref, NULL);
        g_list_free(balsa_app.address_book_list);
        balsa_app.address_book_list = NULL;
    }
    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {

	if (strncmp(key, ADDRESS_BOOK_SECTION_PREFIX, pref_len) == 0) {
	    tmp = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);

	    address_book = libbalsa_address_book_new_from_config(tmp);

	    if (address_book) {
		balsa_app.address_book_list =
		    g_list_append(balsa_app.address_book_list,
				  address_book);

		if (default_address_book_prefix
		    && strcmp(tmp, default_address_book_prefix) == 0) {
		    balsa_app.default_address_book = address_book;
		}
	    }

	    g_free(tmp);
	}
	g_free(key);
	g_free(val);
    }
    g_free(default_address_book_prefix);
}

static void
config_address_books_save(void)
{
    GList *list;

    for (list=balsa_app.address_book_list; list; list=g_list_next(list))
	config_address_book_save(LIBBALSA_ADDRESS_BOOK(list->data));
}


static void
config_identities_load()
{
    LibBalsaIdentity* ident;
    gchar *default_ident;
    void *iterator;
    gchar *key, *val, *tmp;
    int pref_len = strlen(IDENTITY_SECTION_PREFIX);

    /* Free old data in case identities were set by eg. config druid. */
    if(balsa_app.identities) {
        g_list_foreach(balsa_app.identities, (GFunc)g_object_unref, NULL);
        g_list_free(balsa_app.identities);
        balsa_app.identities = NULL;
    }
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "identity/");
    default_ident = gnome_config_get_string("CurrentIdentity");
    gnome_config_pop_prefix();

    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {
	if (strncmp(key, IDENTITY_SECTION_PREFIX, pref_len) == 0) {
	    tmp = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);
	    ident = libbalsa_identity_new_config(tmp, key+pref_len);
	    balsa_app.identities = g_list_prepend(balsa_app.identities, ident);
	    g_free(tmp);
	    if(g_ascii_strcasecmp(default_ident, ident->identity_name) == 0)
		balsa_app.current_ident = ident;
	}
	g_free(key);
	g_free(val);
    }

    if (!balsa_app.identities)
        balsa_app.identities = 
	    g_list_append(balsa_app.identities,
			  libbalsa_identity_new_config(BALSA_CONFIG_PREFIX
						       "identity-default/",
						       "default"));
    
    if(balsa_app.current_ident == NULL)
	balsa_app.current_ident = 
	    LIBBALSA_IDENTITY(balsa_app.identities->data);

    g_free(default_ident);
    if(balsa_app.main_window)
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
    
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "identity/");
    gnome_config_set_string("CurrentIdentity", 
                            balsa_app.current_ident->identity_name);
    gnome_config_pop_prefix();
    g_free(conf_vec);

    config_clean_sections(IDENTITY_SECTION_PREFIX);

    /* save current */
    for (list = balsa_app.identities; list; list = g_list_next(list)) {
	ident = LIBBALSA_IDENTITY(list->data);
	prefix = g_strconcat(BALSA_CONFIG_PREFIX IDENTITY_SECTION_PREFIX, 
			     ident->identity_name, "/", NULL);
        libbalsa_identity_save(ident, prefix);
	g_free(prefix);
    }
}

static void
config_views_load_with_prefix(const gchar * prefix, gboolean compat)
{
    void *iterator;
    gchar *key, *val, *tmp;
    int pref_len = strlen(prefix);
    int def;

    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {
	if (strncmp(key, prefix, pref_len) == 0) {
	    gchar *url;
	    tmp = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);
	    gnome_config_push_prefix(tmp);
	    g_free(tmp);
	    url = compat
		? gnome_config_get_string_with_default("URL", &def)
		: libbalsa_urldecode(&key[pref_len]);
	    if (!compat || !def) {
                LibBalsaMailboxView *view;
		gint tmp;
                gchar *address;

                view = libbalsa_mailbox_view_new();
		/* In compatibility mode, mark as not in sync, to force
		 * the view to be saved in the config file. */
		view->in_sync = !compat;
                g_hash_table_insert(libbalsa_mailbox_view_table,
                                    g_strdup(url), view);

                address =
                    gnome_config_get_string_with_default
                    ("MailingListAddress", &def);
                view->mailing_list_address =
                    def ? NULL : libbalsa_address_new_from_string(address);
                g_free(address);

                view->identity_name = gnome_config_get_string("Identity");

		tmp = gnome_config_get_int_with_default("Threading", &def);
                if (!def)
		    view->threading_type = tmp;

		tmp = gnome_config_get_int_with_default("GUIFilter", &def);
                if (!def)
		    view->filter = tmp;

		tmp = gnome_config_get_int_with_default("SortType", &def);
                if (!def)
		    view->sort_type = tmp;

		tmp = gnome_config_get_int_with_default("SortField", &def);
                if (!def)
		    view->sort_field = tmp;

		tmp = gnome_config_get_int_with_default("Show", &def);
                if (!def)
		    view->show = tmp;

		tmp = gnome_config_get_bool_with_default("Exposed", &def);
                if (!def)
		    view->exposed = tmp;

		tmp = gnome_config_get_bool_with_default("Open", &def);
                if (!def)
		    view->open = tmp;
#ifdef HAVE_GPGME
		tmp = gnome_config_get_int_with_default("CryptoMode", &def);
                if (!def)
		    view->gpg_chk_mode = tmp;
#endif
            }
            gnome_config_pop_prefix();
            g_free(url);
        }
        g_free(key);
        g_free(val);
    }
}

void
config_views_load(void)
{
    /* Load old-style config sections in compatibility mode. */
    config_views_load_with_prefix(VIEW_SECTION_PREFIX, TRUE);
    config_views_load_with_prefix(VIEW_BY_URL_SECTION_PREFIX, FALSE);
}

/* config_views_save:
   iterates over all mailbox views.
*/
static void
save_view(const gchar * url, LibBalsaMailboxView * view)
{
    gchar *url_enc;
    gchar *prefix;

    if (!view || view->in_sync)
	return;
    view->in_sync = TRUE;

    url_enc = libbalsa_urlencode(url);
    prefix = g_strconcat(BALSA_CONFIG_PREFIX VIEW_BY_URL_SECTION_PREFIX,
			 url_enc, "/", NULL);
    g_free(url_enc);

    gnome_config_clean_section(prefix);
    gnome_config_push_prefix(prefix);
    g_free(prefix);

    if (view->mailing_list_address !=
	libbalsa_mailbox_get_mailing_list_address(NULL)) {
       gchar* tmp = libbalsa_address_to_gchar(view->mailing_list_address, 0);
       gnome_config_set_string("MailingListAddress", tmp);
       g_free(tmp);
    }
    if (view->identity_name  != libbalsa_mailbox_get_identity_name(NULL))
	gnome_config_set_string("Identity", view->identity_name);
    if (view->threading_type != libbalsa_mailbox_get_threading_type(NULL))
	gnome_config_set_int("Threading",   view->threading_type);
    if (view->filter         != libbalsa_mailbox_get_filter(NULL))
	gnome_config_set_int("GUIFilter",   view->filter);
    if (view->sort_type      != libbalsa_mailbox_get_sort_type(NULL))
	gnome_config_set_int("SortType",    view->sort_type);
    if (view->sort_field     != libbalsa_mailbox_get_sort_field(NULL))
	gnome_config_set_int("SortField",   view->sort_field);
    /* initial value for show is UNSET, but that's always replaced, 
     * and we want it to default to FROM. */
    if (view->show           != LB_MAILBOX_SHOW_FROM)
	gnome_config_set_int("Show",	    view->show);
    if (view->exposed        != libbalsa_mailbox_get_exposed(NULL))
	gnome_config_set_bool("Exposed",    view->exposed);
    if (view->open           != libbalsa_mailbox_get_open(NULL))
	gnome_config_set_bool("Open",	    view->open);
#ifdef HAVE_GPGME
    if (view->gpg_chk_mode   != libbalsa_mailbox_get_crypto_mode(NULL))
	gnome_config_set_int("CryptoMode",  view->gpg_chk_mode);
#endif

    gnome_config_pop_prefix();
}

void
config_views_save(void)
{
    config_clean_sections(VIEW_SECTION_PREFIX);
    /* save current */
    g_hash_table_foreach(libbalsa_mailbox_view_table, (GHFunc) save_view,
			 NULL);
}

static void
save_color(gchar * key, GdkColor * color)
{
    gchar *str;

    str = g_strdup_printf("#%04x%04x%04x", color->red, color->green,
                          color->blue);
    gnome_config_set_string(key, str);
    g_free(str);
}

static void
config_filters_load(void)
{
    LibBalsaFilter* fil;
    void *iterator;
    gchar * key,* tmp;
    gint pref_len = strlen(FILTER_SECTION_PREFIX);
    guint save = 0;

    filter_errno=FILTER_NOERR;
    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((filter_errno==FILTER_NOERR) &&
	   (iterator = gnome_config_iterator_next(iterator, &key, NULL))) {

	if (strncmp(key, FILTER_SECTION_PREFIX, pref_len) == 0) {
	    tmp=g_strconcat(BALSA_CONFIG_PREFIX,key,"/",NULL);
	    gnome_config_push_prefix(tmp);
	    g_free(tmp);	    
	    fil = libbalsa_filter_new_from_config();
	    if (!fil->condition) {
		/* Try pre-2.1 style: */
		FilterOpType op = gnome_config_get_int("Operation");
		ConditionMatchType cmt =
		    op == FILTER_OP_OR ? CONDITION_OR : CONDITION_AND;
		gnome_config_pop_prefix();
		fil->condition =
		    libbalsa_condition_new_2_0(BALSA_CONFIG_PREFIX, key, cmt);
		gnome_config_push_prefix(tmp);
		if (fil->condition)
		    ++save;
	    }
	    if (!fil->condition) {
		g_idle_add((GSourceFunc) config_warning_idle,
			   _("Filter with no condition was ignored"));
		libbalsa_filter_free(fil, GINT_TO_POINTER(FALSE));
	    } else {
		FILTER_SETFLAG(fil,FILTER_VALID);
		FILTER_SETFLAG(fil,FILTER_COMPILED);
		balsa_app.filters = g_slist_prepend(balsa_app.filters, fil);
	    }
	    gnome_config_pop_prefix();
	}
	g_free(key);	
    }
    if (save)
	config_filters_save();
}

#define FILTER_SECTION_MAX "9999"

void
config_filters_save(void)
{
    GSList *list;
    LibBalsaFilter* fil;
    gchar * buffer,* tmp,* section_name;
    gint i,nb=0,tmp_len=strlen(FILTER_SECTION_MAX)+2;

    /* We allocate once for all a buffer to store conditions sections names */
    buffer=g_strdup_printf(BALSA_CONFIG_PREFIX FILTER_SECTION_PREFIX "%s/",FILTER_SECTION_MAX);
    /* section_name points to the beginning of the filter section name */
    section_name=buffer+strlen(BALSA_CONFIG_PREFIX);
    /* tmp points to the space where filter number is appended */
    tmp=section_name+strlen(FILTER_SECTION_PREFIX);

    for(list = balsa_app.filters; list; list = g_slist_next(list)) {
	fil = (LibBalsaFilter*)(list->data);
	i=snprintf(tmp,tmp_len,"%d/",nb++);
	gnome_config_push_prefix(buffer);
	libbalsa_filter_save_config(fil);
	gnome_config_pop_prefix();
    }
    gnome_config_sync();
    /* This loop takes care of cleaning up old filter sections */
    while (TRUE) {
	i=snprintf(tmp,tmp_len,"%d/",nb++);
	if (gnome_config_has_section(buffer)) {
	    gnome_config_clean_section(buffer);
	}
	else break;
    }
    gnome_config_sync();
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
	    gnome_config_clean_section(tmp);
	    g_free(tmp);
	}
	return;
    }
    if (!tmp) {
	/* If there was no existing filters section for this mailbox we create one */
	tmp=config_get_unused_section(MAILBOX_FILTERS_SECTION_PREFIX);
	gnome_config_push_prefix(tmp);
	g_free(tmp);
	gnome_config_set_string(MAILBOX_FILTERS_URL_KEY,mbox->url);
    }
    else {
	gnome_config_push_prefix(tmp);
	g_free(tmp);
    }
    libbalsa_mailbox_filters_save_config(mbox);
    gnome_config_pop_prefix();
    gnome_config_sync();
}

static void
load_color(gchar * key, GdkColor * color)
{
    gchar *str;

    str = gnome_config_get_string(key);
    if (g_ascii_strncasecmp(str, "rgb:", 4)
        || sscanf(str + 4, "%4hx/%4hx/%4hx", &color->red, &color->green,
                  &color->blue) != 3)
        gdk_color_parse(str, color);
    g_free(str);
}

static void
load_mru(GList **mru)
{
    int count, i;
    char tmpkey[32];
    
    count=d_get_gint("MRUCount", 0);
    for(i=0;i<count;i++) {
        gchar *val;
	sprintf(tmpkey, "MRU%d", i+1);
        if( (val = gnome_config_get_string(tmpkey)) != NULL )
            (*mru)=g_list_append((*mru), val);
    }
}

static void
save_mru(GList *mru)
{
    int i;
    char tmpkey[32];
    GList *ltmp;
    
    for(ltmp=g_list_first(mru),i=0;
	ltmp; ltmp=g_list_next(ltmp),i++) {
	sprintf(tmpkey, "MRU%d", i+1);
	gnome_config_set_string(tmpkey, (gchar *)(ltmp->data));
    }
    gnome_config_set_int("MRUCount", i);
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
         id_list_tmp = g_list_next(id_list_tmp)) {
       
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
