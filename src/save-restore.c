/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#include <gnome.h>
#include "balsa-app.h"
#include "save-restore.h"
#include "quote-color.h"

#define BALSA_CONFIG_PREFIX "balsa/"
#define FOLDER_SECTION_PREFIX "folder-"
#define MAILBOX_SECTION_PREFIX "mailbox-"
#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"

static gint config_section_init(const char* section_prefix, 
				gint (*cb)(const char*));
static gint config_global_load(void);
static gint config_folder_init(const gchar * prefix);
static gint config_mailbox_init(const gchar * prefix);
static gchar *config_get_unused_section(const gchar * prefix);

static gchar **mailbox_list_to_vector(GList * mailbox_list);
static void save_color(gchar * key, GdkColor * color);
static void load_color(gchar * key, GdkColor * color);

static void config_address_books_load(void);
static void config_address_books_save(void);

static void config_identities_load(void);
static void config_identities_save(void);

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
    if(!config_global_load())  /* initializes balsa_app.mailbox_nodes */
	return FALSE;          /* needed in the next step             */
				
    config_section_init(MAILBOX_SECTION_PREFIX, config_mailbox_init);
    config_section_init(FOLDER_SECTION_PREFIX,  config_folder_init);

    return TRUE;
}

static gint
d_get_gint(const gchar * key, gint def_val)
{
    gint def;
    gint res = gnome_config_get_int_with_default(key, &def);
    return def ? def_val : res;
}

/* config_mailbox_set_as_special:
   allows to set given mailboxe as one of the special mailboxes
   PS: I am not sure if I should add outbox to the list.
   specialNames must be in sync with the specialType definition.
*/
static gchar *specialNames[] = {
    "Inbox", "Sentbox", "Trash", "Draftbox", "Outbox"
};
void
config_mailbox_set_as_special(LibBalsaMailbox * mailbox, specialType which)
{
    LibBalsaMailbox **special;

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
	config_mailbox_add(*special, NULL);
    }
    config_mailbox_delete(mailbox);
    config_mailbox_add(mailbox, specialNames[which]);

    *special = mailbox;
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
    gnome_config_push_prefix(key);
    libbalsa_mailbox_save_config(mailbox, key);
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
    gnome_config_push_prefix(key);
    balsa_mailbox_node_save_config(mbnode, key);
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

/* Initialize the specified mailbox, creating the internal data
   structures which represent the mailbox. */
static gint
config_mailbox_init(const gchar * prefix)
{
    LibBalsaMailbox *mailbox;
    const gchar *key =
	prefix + strlen(BALSA_CONFIG_PREFIX MAILBOX_SECTION_PREFIX);
    GNode *node;

    g_return_val_if_fail(prefix != NULL, FALSE);

    mailbox = libbalsa_mailbox_new_from_config(prefix);

    if (mailbox == NULL)
	return FALSE;

    if (LIBBALSA_IS_MAILBOX_REMOTE(mailbox))
	gtk_signal_connect(GTK_OBJECT
			   (LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox)),
			   "get-password", GTK_SIGNAL_FUNC(ask_password),
			   mailbox);

    if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	balsa_app.inbox_input =
	    g_list_append(balsa_app.inbox_input, 
			  balsa_mailbox_node_new_from_mailbox(mailbox));
    } else {
	node = g_node_new(balsa_mailbox_node_new_from_mailbox(mailbox));
	g_node_append(balsa_app.mailbox_nodes, node);

	if (strcmp("Inbox/", key) == 0)
	    balsa_app.inbox = mailbox;
	else if (strcmp("Outbox/", key) == 0)
	    balsa_app.outbox = mailbox;
	else if (strcmp("Sentbox/", key) == 0)
	    balsa_app.sentbox = mailbox;
	else if (strcmp("Draftbox/", key) == 0)
	    balsa_app.draftbox = mailbox;
	else if (strcmp("Trash/", key) == 0)
	    balsa_app.trash = mailbox;
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

    if( (folder = balsa_mailbox_node_new_from_config(prefix)) )
	g_node_append(balsa_app.mailbox_nodes, g_node_new(folder));

    return folder != NULL;
}				/* config_folder_init */

/* Load Balsa's global settings */
static gint
config_global_load(void)
{
    gchar **open_mailbox_vector;
    gint open_mailbox_count;
#if ENABLE_ESMTP
    gboolean def_used;
#endif
    config_address_books_load();
    config_identities_load();

    /* Section for the balsa_information() settings... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "InformationMessages/");

    balsa_app.information_message =
	d_get_gint("ShowInformationMessages", BALSA_INFORMATION_SHOW_NONE);
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

    /* ... window sizes */
    balsa_app.mw_width = gnome_config_get_int("MainWindowWidth=640");
    balsa_app.mw_height = gnome_config_get_int("MainWindowHeight=480");
    balsa_app.mblist_width = gnome_config_get_int("MailboxListWidth=100");
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

    /* ... Headers to show */
    balsa_app.shown_headers = d_get_gint("ShownHeaders", HEADERS_SELECTED);

    g_free(balsa_app.selected_headers);
    balsa_app.selected_headers =
	gnome_config_get_string("SelectedHeaders=" DEFAULT_SELECTED_HDRS);
    g_strdown(balsa_app.selected_headers);

    /* ... Quote colouring */
    g_free(balsa_app.quote_regex);
    balsa_app.quote_regex =
	gnome_config_get_string("QuoteRegex=" DEFAULT_QUOTE_REGEX);

    {
	int i;
#if MAX_QUOTED_COLOR != 6
#warning 'default_quoted_color' array needs to be updated
#endif
	gchar *default_quoted_color[6] = {
	    "rgb:0000/8000/8000", "rgb:8000/0000/0000", "rgb:0000/8000/0000",
	    "rgb:0000/0000/8000", "rgb:8000/8000/0000", "rgb:8000/0000/8000"};
	for(i=0;i<MAX_QUOTED_COLOR;i++) {
	    gchar *text = g_strdup_printf("QuotedColor%d=%s", i, i<6 ?
			  default_quoted_color[i] : DEFAULT_QUOTED_COLOR);
	    load_color(text, &balsa_app.quoted_color[i]);
	    g_free(text);
	}
    }

    /* URL coloring */
    load_color("UrlColor=" DEFAULT_URL_COLOR, &balsa_app.url_color);

    /* ... font used to display messages */
    g_free(balsa_app.message_font);
    balsa_app.message_font =
	gnome_config_get_string("MessageFont=" DEFAULT_MESSAGE_FONT);
    g_free(balsa_app.subject_font);
    balsa_app.subject_font =
	gnome_config_get_string("SubjectFont=" DEFAULT_SUBJECT_FONT);

    /* ... wrap words */
    balsa_app.browse_wrap = gnome_config_get_bool("WordWrap=true");

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

    /* ... style */
    balsa_app.toolbar_style = d_get_gint("ToolbarStyle", GTK_TOOLBAR_BOTH);
    /* ... Progress Window Dialog */
    balsa_app.pwindow_option = d_get_gint("ProgressWindow", WHILERETR);

    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");

    /* ... Printing */
    g_free(balsa_app.paper_size);
    balsa_app.paper_size =
	gnome_config_get_string("PaperSize=" DEFAULT_PAPER_SIZE);

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

    /* ... color */
    load_color("UnreadColor=" MBLIST_UNREAD_COLOR,
	       &balsa_app.mblist_unread_color);
    /* ... show mailbox content info */
    balsa_app.mblist_show_mb_content_info =
	gnome_config_get_bool("ShowMailboxContentInfo=true");

    gnome_config_pop_prefix();

    /* Maibox checking options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MailboxChecking/");

    balsa_app.check_mail_upon_startup =
	gnome_config_get_bool("OnStartup=false");
    balsa_app.check_mail_auto = gnome_config_get_bool("Auto=false");
    balsa_app.check_mail_timer = gnome_config_get_int("AutoDelay=10");
    if (balsa_app.check_mail_timer < 1)
	balsa_app.check_mail_timer = 10;
    if (balsa_app.check_mail_auto)
	update_timer(TRUE, balsa_app.check_mail_timer);

    gnome_config_pop_prefix();

#ifdef BALSA_MDN_REPLY
    /* how to react if a message with MDN request is displayed */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MDNReply/");
    balsa_app.mdn_reply_clean = gnome_config_get_int("Clean=1");
    balsa_app.mdn_reply_notclean = gnome_config_get_int("Suspicious=0");

    gnome_config_pop_prefix();
#endif

    /* Sending options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Sending/");

#if ENABLE_ESMTP
    /* ... SMTP server */
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
    balsa_app.smtp_user = gnome_config_get_string("ESMTPUser");
    balsa_app.smtp_passphrase = gnome_config_get_string("ESMTPPassphrase");
#endif
    /* ... outgoing mail */
    balsa_app.encoding_style = gnome_config_get_int("EncodingStyle=2");
    balsa_app.wordwrap = gnome_config_get_bool("WordWrap=true");
    balsa_app.wraplength = gnome_config_get_int("WrapLength=75");
    if (balsa_app.wraplength < 40)
	balsa_app.wraplength = 40;

    gnome_config_pop_prefix();

    /* Compose window ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Compose/");

    g_free(balsa_app.quote_str);
    balsa_app.quote_str = gnome_config_get_string("QuoteString=> ");
    g_free(balsa_app.compose_headers);
    balsa_app.compose_headers =
	gnome_config_get_string("ComposeHeaders=to subject cc");
    balsa_app.req_dispnotify = 
	gnome_config_get_bool("RequestDispositionNotification=false");

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
    balsa_app.mailbox_nodes = g_node_new(balsa_mailbox_node_new_from_dir(
        balsa_app.local_mail_directory));

    /* debugging enabled */
    balsa_app.debug = gnome_config_get_bool("Debug=false");

    balsa_app.close_mailbox_auto = gnome_config_get_bool("AutoCloseMailbox=true");
    balsa_app.close_mailbox_timeout = gnome_config_get_int("AutoCloseMailboxTimeout=10");

    balsa_app.remember_open_mboxes =
	gnome_config_get_bool("RememberOpenMailboxes=false");
    gnome_config_get_vector("OpenMailboxes", &open_mailbox_count,
			    &open_mailbox_vector);
    if (balsa_app.remember_open_mboxes && open_mailbox_count > 0) {
	/* FIXME: Open the mailboxes.... */
	printf("Opening %d mailboxes on startup.\n", open_mailbox_count);
	gtk_idle_add((GtkFunction) open_mailboxes_idle_cb,
		     open_mailbox_vector);
    } else
	g_strfreev(open_mailbox_vector);

    balsa_app.empty_trash_on_exit =
	gnome_config_get_bool("EmptyTrash=false");

    /* This setting is now per address book */
    gnome_config_clean_key("AddressBookDistMode");

    gnome_config_pop_prefix();

    /* Last used paths options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Paths/");
    g_free(balsa_app.attach_dir);
    balsa_app.attach_dir = gnome_config_get_string("AttachDir");
    g_free(balsa_app.save_dir);
    balsa_app.save_dir = gnome_config_get_string("SavePartDir");
    gnome_config_pop_prefix();

    return TRUE;
}				/* config_global_load */

gint config_save(void)
{
    gchar **open_mailboxes_vector;
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
    gnome_config_set_int("MainWindowWidth", balsa_app.mw_width);
    gnome_config_set_int("MainWindowHeight", balsa_app.mw_height);
    gnome_config_set_int("MailboxListWidth", balsa_app.mblist_width);
    gnome_config_set_int("NotebookHeight", balsa_app.notebook_height);

    gnome_config_pop_prefix();

    /* Message View options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MessageDisplay/");

    gnome_config_set_string("DateFormat", balsa_app.date_string);
    gnome_config_set_int("ShownHeaders", balsa_app.shown_headers);
    gnome_config_set_string("SelectedHeaders", balsa_app.selected_headers);
    gnome_config_set_string("QuoteRegex", balsa_app.quote_regex);
    gnome_config_set_string("MessageFont", balsa_app.message_font);
    gnome_config_set_string("SubjectFont", balsa_app.subject_font);
    gnome_config_set_bool("WordWrap", balsa_app.browse_wrap);

    for(i=0;i<MAX_QUOTED_COLOR;i++) {
	gchar *text = g_strdup_printf("QuotedColor%d", i);
	save_color(text, &balsa_app.quoted_color[i]);
	g_free(text);
    }

    save_color("UrlColor", &balsa_app.url_color);

    gnome_config_pop_prefix();

    /* Interface Options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Interface/");

    gnome_config_set_bool("ShowPreviewPane", balsa_app.previewpane);
    gnome_config_set_bool("ShowMailboxList", balsa_app.show_mblist);
    gnome_config_set_bool("ShowTabs", balsa_app.show_notebook_tabs);
    gnome_config_set_int("ToolbarStyle", balsa_app.toolbar_style);
    gnome_config_set_int("ProgressWindow", balsa_app.pwindow_option);
    gnome_config_set_bool("AlternativeLayout", balsa_app.alternative_layout);
    gnome_config_set_bool("ViewMessageOnOpen", balsa_app.view_message_on_open);

    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");
    gnome_config_set_string("PaperSize",balsa_app.paper_size);
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

    save_color("UnreadColor", &balsa_app.mblist_unread_color);
    gnome_config_set_bool("ShowMailboxContentInfo",
			  balsa_app.mblist_show_mb_content_info);

    gnome_config_pop_prefix();

    /* Maibox checking options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MailboxChecking/");

    gnome_config_set_bool("OnStartup", balsa_app.check_mail_upon_startup);
    gnome_config_set_bool("Auto", balsa_app.check_mail_auto);
    gnome_config_set_int("AutoDelay", balsa_app.check_mail_timer);

    gnome_config_pop_prefix();

#ifdef BALSA_MDN_REPLY
    /* how to react if a message with MDN request is displayed */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MDNReply/");
    gnome_config_set_int("Clean", balsa_app.mdn_reply_clean);
    gnome_config_set_int("Suspicious", balsa_app.mdn_reply_notclean);

    gnome_config_pop_prefix();
#endif

    /* Sending options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Sending/");
#if ENABLE_ESMTP
    gnome_config_set_string("ESMTPServer", balsa_app.smtp_server);
    gnome_config_set_string("ESMTPUser", balsa_app.smtp_user);
    gnome_config_set_string("ESMTPPassphrase", balsa_app.smtp_passphrase);
#endif 
   gnome_config_set_int("EncodingStyle", balsa_app.encoding_style);
    gnome_config_set_bool("WordWrap", balsa_app.wordwrap);
    gnome_config_set_int("WrapLength", balsa_app.wraplength);

    gnome_config_pop_prefix();

    /* Compose window ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Compose/");

    gnome_config_set_string("ComposeHeaders", balsa_app.compose_headers);
    gnome_config_set_bool("RequestDispositionNotification", balsa_app.req_dispnotify);
    gnome_config_set_string("QuoteString", balsa_app.quote_str);

    gnome_config_pop_prefix();

    /* Global config options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Globals/");

    gnome_config_set_string("MailDir", balsa_app.local_mail_directory);

    gnome_config_set_bool("Debug", balsa_app.debug);

    gnome_config_set_bool("AutoCloseMailbox", balsa_app.close_mailbox_auto);
    gnome_config_set_int("AutoCloseMailboxTimeout", balsa_app.close_mailbox_timeout);

    open_mailboxes_vector =
	mailbox_list_to_vector(balsa_app.open_mailbox_list);
    gnome_config_set_vector("OpenMailboxes",
			    g_list_length(balsa_app.open_mailbox_list),
			    (const char **) open_mailboxes_vector);
    g_strfreev(open_mailboxes_vector);
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

    
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Paths/");
    if(balsa_app.attach_dir)
	gnome_config_set_string("AttachDir", balsa_app.attach_dir);
    if(balsa_app.save_dir)
	gnome_config_set_string("SavePartDir", balsa_app.save_dir);
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
    LibBalsaAddressBook *ab;

    list = balsa_app.address_book_list;
    while (list) {
	ab = LIBBALSA_ADDRESS_BOOK(list->data);

	config_address_book_save(ab);
	list = g_list_next(list);

    }
}


static void
config_identity_load(const gchar* ident_name)
{
    BalsaIdentity* ident;
    BalsaIdentity* tmp_ident;
    BalsaIdentity* loaded_ident = NULL;
    LibBalsaAddress* ad;
    GList* list;
    gchar* tmpstr;
    gchar* prefix;

    
    g_return_if_fail(ident_name != NULL);
    list = balsa_app.identities;

    while (list != NULL) {
        tmp_ident = BALSA_IDENTITY(list->data);
        
        if (g_strcasecmp(ident_name, tmp_ident->identity_name) == 0) {
            loaded_ident = tmp_ident;
            break;
        }
            
        list = g_list_next(list);
    }

    prefix = g_strconcat(BALSA_CONFIG_PREFIX, "identity-", 
                         ident_name, "/", NULL);
    gnome_config_push_prefix(prefix);

    ident = BALSA_IDENTITY(balsa_identity_new_with_name(ident_name));
    ident->address->full_name = gnome_config_get_string("FullName");
    ident->address->address_list = 
        g_list_append(ident->address->address_list, 
                      gnome_config_get_string("Address"));
    ident->replyto = gnome_config_get_string("ReplyTo");
    ident->domain = gnome_config_get_string("Domain");
    ident->bcc = gnome_config_get_string("Bcc");

    /* 
     * these two have defaults, so we need to use the appropriate
     * functions to manage the memory. 
     */
    if ((tmpstr = gnome_config_get_string("ReplyString"))) {
        balsa_identity_set_reply_string(ident, tmpstr);
        g_free(tmpstr);
    }
    
    if ((tmpstr = gnome_config_get_string("ForwardString"))) {
        balsa_identity_set_forward_string(ident, tmpstr);
        g_free(tmpstr);
    }
    
    ident->signature_path = gnome_config_get_string("SignaturePath");

    ident->sig_sending = gnome_config_get_bool("SigSending");
    ident->sig_whenforward = gnome_config_get_bool("SigForward");
    ident->sig_whenreply = gnome_config_get_bool("SigReply");
    ident->sig_separator = gnome_config_get_bool("SigSeparator");
    ident->sig_prepend = gnome_config_get_bool("SigPrepend");
    gnome_config_pop_prefix();
    g_free(prefix);

    if (loaded_ident != NULL) {
        ad = libbalsa_address_new();
        ad->full_name = g_strdup(ident->address->full_name);
        ad->address_list = g_list_append(ad->address_list,
                                         g_strdup(ident->address->address_list->data));
        balsa_identity_set_address(loaded_ident, ad);
        balsa_identity_set_replyto(loaded_ident, ident->replyto);
        balsa_identity_set_domain(loaded_ident, ident->domain);
        balsa_identity_set_bcc(loaded_ident, ident->bcc);
        balsa_identity_set_reply_string(loaded_ident, ident->reply_string);
        balsa_identity_set_forward_string(loaded_ident, ident->forward_string);
        balsa_identity_set_signature_path(loaded_ident, ident->signature_path);
        balsa_identity_set_sig_sending(loaded_ident, ident->sig_sending);
        balsa_identity_set_sig_whenforward(loaded_ident, 
                                           ident->sig_whenforward);
        balsa_identity_set_sig_whenreply(loaded_ident, ident->sig_whenreply);
        balsa_identity_set_sig_separator(loaded_ident, ident->sig_separator);
        balsa_identity_set_sig_prepend(loaded_ident, ident->sig_prepend);
        gtk_object_destroy(GTK_OBJECT(ident));
    } else {
        balsa_app.identities = g_list_append(balsa_app.identities, ident);

        if (balsa_app.current_ident == NULL) 
            balsa_identity_set_current(ident);    
    }
}


static void
config_identities_load()
{
    BalsaIdentity* ident;
    GList* list;
    gchar** ident_keys;
    gchar* default_ident;
    gint key_count;
    gint i;


    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "identity/");
    default_ident = gnome_config_get_string("CurrentIdentity");
    gnome_config_get_vector("Identities", &key_count, &ident_keys);
    gnome_config_pop_prefix();

    if (key_count == 0 && g_list_length(balsa_app.identities) == 0) {
        config_identity_load("default");
    } 
    
    for (i = 0; i < key_count; ++i) {
        config_identity_load(ident_keys[i]);
    }

    list = balsa_app.identities;

    while (list != NULL) {
        ident = BALSA_IDENTITY(list->data);
        
        if (default_ident != NULL && g_strcasecmp(default_ident, ident->identity_name) == 0) {
            balsa_identity_set_current(ident);
            break;
        }

        list = g_list_next(list);
    }
    
    g_free(default_ident);
    g_strfreev(ident_keys);
}



static void 
config_identity_save(BalsaIdentity* ident)
{
    gchar* config_key;

    
    config_key = g_strconcat(BALSA_CONFIG_PREFIX, "identity-", 
                             ident->identity_name, "/", NULL);
    gnome_config_push_prefix(config_key);
    gnome_config_set_string("FullName", ident->address->full_name);
    
    if (ident->address->address_list != NULL)
        gnome_config_set_string("Address", ident->address->address_list->data);

    gnome_config_set_string("ReplyTo", ident->replyto);
    gnome_config_set_string("Domain", ident->domain);
    gnome_config_set_string("Bcc", ident->bcc);
    gnome_config_set_string("ReplyString", ident->reply_string);
    gnome_config_set_string("ForwardString", ident->forward_string);
    gnome_config_set_string("SignaturePath", ident->signature_path);

    gnome_config_set_bool("SigSending", ident->sig_sending);
    gnome_config_set_bool("SigForward", ident->sig_whenforward);
    gnome_config_set_bool("SigReply", ident->sig_whenreply);
    gnome_config_set_bool("SigSeparator", ident->sig_separator);
    gnome_config_set_bool("SigPrepend", ident->sig_prepend);

    gnome_config_pop_prefix();
    g_free(config_key);
}


static void
config_identities_save(void)
{
    BalsaIdentity* ident;
    GList* list;
    gchar** conf_vec;
    gint i = 0;


    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "identity/");
    list = balsa_app.identities;
    conf_vec = g_malloc(sizeof(gchar*) * g_list_length(list));
    g_assert(conf_vec != NULL);

    while (list != NULL) {
        ident = BALSA_IDENTITY(list->data);
        conf_vec[i] = ident->identity_name;
        list = g_list_next(list);
        ++i;
    }
    
    gnome_config_set_vector("Identities", i, (const char**) conf_vec);
    gnome_config_set_string("CurrentIdentity", 
                            balsa_app.current_ident->identity_name);
    list = balsa_app.identities;
    gnome_config_pop_prefix();
    g_free(conf_vec);

    while (list != NULL) {
        ident = BALSA_IDENTITY(list->data);
        config_identity_save(ident);
        list = g_list_next(list);
    }
    

}

static gchar **
mailbox_list_to_vector(GList * mailbox_list)
{
    GList *list;
    gchar **res;
    gint i;

    i = g_list_length(mailbox_list) + 1;
    res = g_new0(gchar *, i);

    i = 0;
    list = mailbox_list;
    while (list) {
	res[i] = g_strdup(LIBBALSA_MAILBOX(list->data)->name);
	i++;
	list = g_list_next(list);
    }
    res[i] = NULL;
    return res;
}

static void
save_color(gchar * key, GdkColor * color)
{
    gchar *str;

    str =
	g_strdup_printf("rgb:%04x/%04x/%04x", color->red, color->green,
			color->blue);
    gnome_config_set_string(key, str);
    g_free(str);
}

static void
load_color(gchar * key, GdkColor * color)
{
    gchar *str;

    str = gnome_config_get_string(key);
    gdk_color_parse(str, color);
    g_free(str);
}
