/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#include "libbalsa.h"

#include <gnome.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "balsa-app.h"
#include "save-restore.h"
#include "quote-color.h"

#define BALSA_CONFIG_PREFIX "balsa/"
#define MAILBOX_SECTION_PREFIX "mailbox-"
#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"

static gint config_mailboxes_init(void);
static gint config_global_load(void);
static gint config_mailbox_init(const gchar * prefix);
static gchar *config_get_unused_section(const gchar * prefix);

static gchar **mailbox_list_to_vector(GList * mailbox_list);
static void save_color(gchar * key, GdkColor * color);
static void load_color(gchar * key, GdkColor * color);

static void config_address_books_load(void);
static void config_address_books_save(void);

static void config_identities_load(void);
static void config_identities_save(void);

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
    config_mailboxes_init();
    config_global_load();
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
    GNode *node;

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
	config_mailbox_add(*special, NULL);
	node = g_node_new(mailbox_node_new(
					   (*special)->name, *special,
					   (*special)->is_directory));
	g_node_append(balsa_app.mailbox_nodes, node);
    }
    config_mailbox_delete(mailbox);
    config_mailbox_add(mailbox, specialNames[which]);

    node = find_gnode_in_mbox_list(balsa_app.mailbox_nodes, mailbox);
    g_node_unlink(node);

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
	tmp =
	    g_strdup_printf(BALSA_CONFIG_PREFIX MAILBOX_SECTION_PREFIX
			    "%s/", key_arg);

    libbalsa_mailbox_save_config(mailbox, tmp);
    g_free(tmp);

    gnome_config_sync();
    return TRUE;
}				/* config_mailbox_add */

/* removes from the ocnfiguration only */
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

/* This function initializes all the mailboxes internally, going through
   the list of all the mailboxes in the configuration file one by one. */
static gint
config_mailboxes_init(void)
{
    void *iterator;
    gchar *key, *val, *tmp;
    int pref_len = strlen(MAILBOX_SECTION_PREFIX);

    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {
	if (strncmp(key, MAILBOX_SECTION_PREFIX, pref_len) == 0) {
	    tmp = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);
	    config_mailbox_init(tmp);
	    g_free(tmp);
	}
	g_free(key);
	g_free(val);
    }
    return TRUE;		/* hm... check_basic_mailboxes? */
}				/* config_mailboxes_init */

/* Initialize the specified mailbox, creating the internal data
   structures which represent the mailbox. */
static gint
config_mailbox_init(const gchar * prefix)
{
    LibBalsaMailbox *mailbox;
    const gchar *key =
	prefix + strlen(BALSA_CONFIG_PREFIX MAILBOX_SECTION_PREFIX);
    GNode *node;
    gboolean is_mh;

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
	    g_list_append(balsa_app.inbox_input, mailbox);
    } else if (strcmp("Inbox/", key) == 0)
	balsa_app.inbox = mailbox;
    else if (strcmp("Outbox/", key) == 0)
	balsa_app.outbox = mailbox;
    else if (strcmp("Sentbox/", key) == 0)
	balsa_app.sentbox = mailbox;
    else if (strcmp("Draftbox/", key) == 0)
	balsa_app.draftbox = mailbox;
    else if (strcmp("Trash/", key) == 0)
	balsa_app.trash = mailbox;
    else {
	is_mh = LIBBALSA_IS_MAILBOX_LOCAL(mailbox)
	    && LIBBALSA_MAILBOX_LOCAL(mailbox)->type ==
	    LIBBALSA_MAILBOX_LOCAL_MH;

	node = g_node_new(mailbox_node_new(g_strdup(mailbox->name),
					   mailbox, is_mh));
	g_node_append(balsa_app.mailbox_nodes, node);
    }
    return TRUE;
}				/* config_mailbox_init */


/* Load Balsa's global settings */
static gint
config_global_load(void)
{
    gchar **open_mailbox_vector;
    gint open_mailbox_count;

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
    load_color("QuotedColorStart=" DEFAULT_QUOTED_COLOR,
	       &balsa_app.quoted_color[0]);
    load_color("QuotedColorEnd=" DEFAULT_QUOTED_COLOR,
	       &balsa_app.quoted_color[MAX_QUOTED_COLOR - 1]);
    make_gradient(balsa_app.quoted_color, 0, MAX_QUOTED_COLOR - 1);

    /* ... font used to display messages */
    g_free(balsa_app.message_font);
    balsa_app.message_font =
	gnome_config_get_string("MessageFont=" DEFAULT_MESSAGE_FONT);

    /* ... wrap words */
    balsa_app.browse_wrap = gnome_config_get_bool("WordWrap=true");

    gnome_config_pop_prefix();

    /* Interface Options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Interface/");

    /* ... interface elements to show */
    balsa_app.previewpane = gnome_config_get_bool("ShowPreviewPane=true");
    balsa_app.show_mblist = gnome_config_get_bool("ShowMailboxList=true");
    balsa_app.show_notebook_tabs = gnome_config_get_bool("ShowTabs=false");

    /* ... style */
    balsa_app.toolbar_style = d_get_gint("ToolbarStyle", GTK_TOOLBAR_BOTH);
    /* ... Progress Window Dialog */
    balsa_app.pwindow_option = d_get_gint("ProgressWindow", WHILERETR);

    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");

    g_free(balsa_app.PrintCommand.PrintCommand);
    balsa_app.PrintCommand.PrintCommand =
	gnome_config_get_string("PrintCommand=a2ps -d -q %s");
    balsa_app.PrintCommand.linesize =
	d_get_gint("PrintLinesize", DEFAULT_LINESIZE);
    balsa_app.PrintCommand.breakline =
	gnome_config_get_bool("PrintBreakline=false");

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

    /* Sending options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Sending/");

    /* ... SMTP server */
    balsa_app.smtp_server = gnome_config_get_string("SMTPServer");
    balsa_app.smtp = gnome_config_get_bool("SMTP=false");

    /* ... outgoing mail */
    balsa_app.encoding_style = gnome_config_get_int("EncodingStyle=2");
    g_free(balsa_app.charset);
    balsa_app.charset =
	gnome_config_get_string("Charset=" DEFAULT_CHARSET);
    libbalsa_set_charset(balsa_app.charset);
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

    gnome_config_pop_prefix();

    /* Global config options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Globals/");

    /* directory */
    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory = gnome_config_get_string("MailDir");

    /* debugging enabled */
    balsa_app.debug = gnome_config_get_bool("Debug=false");

    balsa_app.remember_open_mboxes =
	gnome_config_get_bool("RememberOpenMailboxes=false");
    gnome_config_get_vector("OpenMailboxes", &open_mailbox_count,
			    &open_mailbox_vector);
    if (balsa_app.remember_open_mboxes && open_mailbox_count > 0) {
	/* FIXME: Open the mailboxes.... */
	gtk_idle_add((GtkFunction) open_mailboxes_idle_cb,
		     open_mailbox_vector);
    } else
	g_strfreev(open_mailbox_vector);

    balsa_app.empty_trash_on_exit =
	gnome_config_get_bool("EmptyTrash=false");
    balsa_app.ab_dist_list_mode =
	gnome_config_get_bool("AddressBookDistMode=false");

    gnome_config_pop_prefix();
    return TRUE;
}				/* config_global_load */

gint config_save(void)
{
    gchar **open_mailboxes_vector;

    config_address_books_save();
    config_identities_save();

    /* Section for the balsa_information() settings... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "InformationMessages/");
    gnome_config_set_int("ShowInformationMessages",
			 balsa_app.information_message);
    gnome_config_set_int("ShowWarningMessages", balsa_app.warning_message);
    gnome_config_set_int("ShowErrorMessages", balsa_app.error_message);
    gnome_config_set_int("ShowDebugMessages", balsa_app.debug_message);
    gnome_config_pop_prefix();

/* Section for geometry ... *//* FIXME: Saving window sizes is the WM's job?? */
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
    gnome_config_set_bool("WordWrap", balsa_app.browse_wrap);
    save_color("QuotedColorStart", &balsa_app.quoted_color[0]);
    save_color("QuotedColorEnd",
	       &balsa_app.quoted_color[MAX_QUOTED_COLOR - 1]);

    gnome_config_pop_prefix();

    /* Interface Options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Interface/");

    gnome_config_set_bool("ShowPreviewPane", balsa_app.previewpane);
    gnome_config_set_bool("ShowMailboxList", balsa_app.show_mblist);
    gnome_config_set_bool("ShowTabs", balsa_app.show_notebook_tabs);
    gnome_config_set_int("ToolbarStyle", balsa_app.toolbar_style);
    gnome_config_set_int("ProgressWindow", balsa_app.pwindow_option);

    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");

    gnome_config_set_string("PrintCommand",
			    balsa_app.PrintCommand.PrintCommand);
    gnome_config_set_int("PrintLinesize", balsa_app.PrintCommand.linesize);
    gnome_config_set_bool("PrintBreakline",
			  balsa_app.PrintCommand.breakline);

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

    /* Sending options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Sending/");

    gnome_config_set_bool("SMTP", balsa_app.smtp);
    gnome_config_set_string("SMTPServer", balsa_app.smtp_server);
    gnome_config_set_int("EncodingStyle", balsa_app.encoding_style);
    gnome_config_set_string("Charset", balsa_app.charset);
    gnome_config_set_bool("WordWrap", balsa_app.wordwrap);
    gnome_config_set_int("WrapLength", balsa_app.wraplength);

    gnome_config_pop_prefix();

    /* Compose window ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Compose/");

    gnome_config_set_string("ComposeHeaders", balsa_app.compose_headers);
    gnome_config_set_string("QuoteString", balsa_app.quote_str);

    gnome_config_pop_prefix();

    /* Global config options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Globals/");

    gnome_config_set_string("MailDir", balsa_app.local_mail_directory);

    gnome_config_set_bool("Debug", balsa_app.debug);

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

    /* address book */
    gnome_config_set_bool("AddressBookDistMode",
			  balsa_app.ab_dist_list_mode);

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
config_identities_load(void)
{
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "identity-default/");

    if (balsa_app.address)
	gtk_object_destroy(GTK_OBJECT(balsa_app.address));
    balsa_app.address = libbalsa_address_new();

    /* user's real name */
    balsa_app.address->full_name = gnome_config_get_string("FullName");

    /* user's email address */
    balsa_app.address->address_list =
	g_list_append(balsa_app.address->address_list,
		      gnome_config_get_string("Address"));

    /* users's replyto address */
    balsa_app.replyto = gnome_config_get_string("ReplyTo");

    /* users's domain */
    balsa_app.domain = gnome_config_get_string("Domain");

    /* bcc field for outgoing mails; optional */
    balsa_app.bcc = gnome_config_get_string("Bcc");

    /* signature file path */
    balsa_app.signature_path = gnome_config_get_string("SignaturePath");
    if (balsa_app.signature_path == NULL) {
	balsa_app.signature_path =
	    gnome_util_prepend_user_home(".signature");
    }

    balsa_app.sig_sending = gnome_config_get_bool("SigSending=true");
    balsa_app.sig_whenreply = gnome_config_get_bool("SigReply=true");
    balsa_app.sig_whenforward = gnome_config_get_bool("SigForward=true");
    balsa_app.sig_separator = gnome_config_get_bool("SigSeparator=true");

    gnome_config_pop_prefix();
}

static void
config_identities_save(void)
{
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "identity-default/");

    gnome_config_set_string("FullName", balsa_app.address->full_name);
    gnome_config_set_string("Address",
			    balsa_app.address->address_list->data);
    gnome_config_set_string("ReplyTo", balsa_app.replyto);
    gnome_config_set_string("Domain", balsa_app.domain);
    gnome_config_set_string("Bcc", balsa_app.bcc);
    gnome_config_set_string("SignaturePath", balsa_app.signature_path);

    gnome_config_set_bool("SigSending", balsa_app.sig_sending);
    gnome_config_set_bool("SigForward", balsa_app.sig_whenforward);
    gnome_config_set_bool("SigReply", balsa_app.sig_whenreply);
    gnome_config_set_bool("SigSeparator", balsa_app.sig_separator);

    gnome_config_pop_prefix();

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
