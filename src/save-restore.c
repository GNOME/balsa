/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* vim:set ts=4 sw=4 ai et: */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
#include "toolbar-prefs.h"

#include "filter-file.h"
#include "filter-funcs.h"
#include "mailbox-filter.h"

#define BALSA_CONFIG_PREFIX "balsa/"
#define FOLDER_SECTION_PREFIX "folder-"
#define MAILBOX_SECTION_PREFIX "mailbox-"
#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"
#define IDENTITY_SECTION_PREFIX "identity-"

static gint config_section_init(const char* section_prefix, 
				gint (*cb)(const char*));
static gint config_global_load(void);
static gint config_folder_init(const gchar * prefix);
static gint config_mailbox_init(const gchar * prefix);
static gchar *config_get_unused_section(const gchar * prefix);

static gchar **mailbox_list_to_vector(GList * mailbox_list);
static void save_color(gchar * key, GdkColor * color);
static void load_color(gchar * key, GdkColor * color);
static void save_mru(GList *mru);
static void load_mru(GList **mru);

static void config_address_books_load(void);
static void config_address_books_save(void);
static void config_identities_load(void);

static void check_for_old_sigs(GList * id_list_tmp);

static void config_filters_load(void);
void config_filters_save(void);

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

static gfloat
d_get_gfloat(const gchar * key, gfloat def_val)
{
    gint def;
    gfloat res = gnome_config_get_int_with_default(key, &def);
    return def ? def_val : res;
}

static void
free_toolbar(int i)
{
    int j;
    for(j=0; balsa_app.toolbars[i][j]; j++)
	g_free(balsa_app.toolbars[i][j]);
    g_free(balsa_app.toolbars[i]);
}
static void
free_toolbars(void)
{
    int i, j;
    
    if(!balsa_app.toolbars == 0)
	return;
    
    for(i=0; i<balsa_app.toolbar_count; i++) 
	free_toolbar(i);
    g_free((void *)balsa_app.toolbars);
    g_free((void *)balsa_app.toolbar_ids);
}

/* load_toolbars:
   loads customized toolbars for main/message preview and compose windows.
*/
static void
load_toolbars(void)
{
    gint i, j, items;
    char tmpkey[256];

    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Toolbars/");
    
    balsa_app.toolbar_wrap_button_text=d_get_gint("WrapButtonText", 1);
    
    free_toolbars();
    
    balsa_app.toolbar_ids=
	(BalsaToolbarType *)g_malloc(sizeof(BalsaToolbarType)*MAXTOOLBARS);
    balsa_app.toolbars=	(char ***)g_malloc(sizeof(char *)*MAXTOOLBARS);
    
    balsa_app.toolbar_count=d_get_gint("ToolbarCount", 0);
    if(balsa_app.toolbar_count>=MAXTOOLBARS)   /* most likely, this means   */
        balsa_app.toolbar_count=MAXTOOLBARS-1; /* configure file corruption */

    for(i=0; i<balsa_app.toolbar_count; i++) {
	balsa_app.toolbars[i]=
	    (char **)g_malloc(sizeof(char *)*MAXTOOLBARITEMS);
	
	sprintf(tmpkey, "Toolbar%dID", i);
	balsa_app.toolbar_ids[i] = d_get_gint(tmpkey, TOOLBAR_INVALID);
	
	if(balsa_app.toolbar_ids[i] == TOOLBAR_INVALID) {
	    balsa_app.toolbars[i][0]=NULL;
	    continue;
	}
	
	sprintf(tmpkey, "Toolbar%dItemCount", i);
	items=d_get_gint(tmpkey, 0);
        if(items>=MAXTOOLBARITEMS)        /* most likely, this means   */
            items=MAXTOOLBARITEMS-1;      /* configure file corruption */
	
	for(j=0; j<items; j++) {
	    sprintf(tmpkey, "Toolbar%dItem%d", i, j);
	    balsa_app.toolbars[i][j]=  gnome_config_get_string(tmpkey);
	}
	balsa_app.toolbars[i][j]=NULL;
	/* validate */
	for(j=0; balsa_app.toolbars[i][j]; j++) {
	    if(get_toolbar_button_index(balsa_app.toolbars[i][j])<0) {
		/* validation failed: roll the toolbar back. */
		free_toolbar(i); 
		balsa_app.toolbars[i] = NULL;
		balsa_app.toolbar_ids[i] = TOOLBAR_INVALID;
		i--;
		balsa_app.toolbar_count--;
		g_warning("I dropped a toolbar. Are you up/downgrading?");
		break;
	    }
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
static gchar *specialNames[] = {
    "Inbox", "Sentbox", "Trash", "Draftbox", "Outbox"
};
void
config_mailbox_set_as_special(LibBalsaMailbox * mailbox, specialType which)
{
    LibBalsaMailbox **special, *do_rescan_for = NULL;

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
        else do_rescan_for = *special;
	gtk_object_unref(GTK_OBJECT(*special));
    }
    config_mailbox_delete(mailbox);
    config_mailbox_add(mailbox, specialNames[which]);

    *special = mailbox;
    gtk_object_ref(GTK_OBJECT(mailbox));
    if(do_rescan_for) balsa_mailbox_local_rescan_parent(do_rescan_for);
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
        gboolean special = TRUE;

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
	else
            special = FALSE;

	if (special)
            gtk_object_ref(GTK_OBJECT(mailbox));
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

    /* We must load filters before mailboxes, because they refer to the filters list */
    config_filters_load();
    if (filter_errno!=FILTER_NOERR) {
	filter_perror(_("Error during filters loading : "));
 	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Error during filters loading : %s\n%s"),
 			     filter_strerror(filter_errno),
			     _("Filters may not be correct"));
    }

    /* find and convert old-style signature entries */
    check_for_old_sigs(balsa_app.identities);

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
    balsa_app.index_size_width =
	d_get_gint("IndexSizeWidth", SIZE_DEFAULT_WIDTH);

    /* ... window sizes */
    balsa_app.mw_width = gnome_config_get_int("MainWindowWidth=640");
    balsa_app.mw_height = gnome_config_get_int("MainWindowHeight=480");
    balsa_app.mblist_width = gnome_config_get_int("MailboxListWidth=100");
    /* sendmsg window sizes */
    balsa_app.sw_width = gnome_config_get_int("SendMsgWindowWidth=640");
    balsa_app.sw_height = gnome_config_get_int("SendMsgWindowHeight=480");
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

    /* ... Message window title format */
    g_free(balsa_app.message_title_format);
    balsa_app.message_title_format =
        gnome_config_get_string("MessageTitleFormat="
                                DEFAULT_MESSAGE_TITLE_FORMAT);

    balsa_app.expand_tree = gnome_config_get_bool("ExpandTree=false");
    balsa_app.threading_type = d_get_gint("ThreadingType", 
					  BALSA_INDEX_THREADING_JWZ);

    /* ... Quote colouring */
    g_free(balsa_app.quote_regex);
    balsa_app.quote_regex =
	gnome_config_get_string("QuoteRegex=" DEFAULT_QUOTE_REGEX);
    balsa_app.recognize_rfc2646_format_flowed =
	gnome_config_get_bool("RecognizeRFC2646FormatFlowed=true");

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
    balsa_app.line_length = gnome_config_get_bool("MsgSizeAsLines=true");
    balsa_app.pgdownmod = gnome_config_get_bool("PageDownMod=false");
    balsa_app.pgdown_percent = gnome_config_get_int("PageDownPercent=50");
    if (balsa_app.pgdown_percent < 10)
	balsa_app.pgdown_percent = 10;

    /* ... style */
    balsa_app.toolbar_style = d_get_gint("ToolbarStyle", GTK_TOOLBAR_BOTH);
    /* ... Progress Window Dialog */
    balsa_app.pwindow_option = d_get_gint("ProgressWindow", WHILERETR);
    balsa_app.drag_default_is_move = d_get_gint("DragDefaultIsMove", 0);
    balsa_app.delete_immediately =
        gnome_config_get_bool("DeleteImmediately=false");
    balsa_app.hide_deleted =
        gnome_config_get_bool("HideDeleted=false");

    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");

    /* ... Printing */
    g_free(balsa_app.paper_size);
    balsa_app.paper_size =
	gnome_config_get_string("PaperSize=" DEFAULT_PAPER_SIZE);
    g_free(balsa_app.print_header_font);
    balsa_app.print_header_font =
	gnome_config_get_string("PrintHeaderFont=" DEFAULT_PRINT_HEADER_FONT);
    balsa_app.print_header_size = 
	d_get_gfloat("PrintHeaderSize", DEFAULT_PRINT_HEADER_SIZE);
    balsa_app.print_footer_size =
	d_get_gfloat("PrintFooterSize", DEFAULT_PRINT_FOOTER_SIZE);
    g_free(balsa_app.print_body_font);
    balsa_app.print_body_font =
	gnome_config_get_string("PrintBodyFont=" DEFAULT_PRINT_BODY_FONT);
    balsa_app.print_body_size =
	d_get_gfloat("PrintBodySize", DEFAULT_PRINT_BODY_SIZE);
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

    /* ... color */
    load_color("UnreadColor=" MBLIST_UNREAD_COLOR,
	       &balsa_app.mblist_unread_color);
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
    gnome_config_pop_prefix();

    /* IMAP folder scanning */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "IMAPFolderScanning/");
    balsa_app.imap_scan_depth = d_get_gint("ScanDepth", 1);
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
    balsa_app.smtp_tls_mode = gnome_config_get_int("ESMTPTLSMode=0");
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
    balsa_app.wordwrap = gnome_config_get_bool("WordWrap=true");
    balsa_app.wraplength = gnome_config_get_int("WrapLength=72");
    if (balsa_app.wraplength < 40)
	balsa_app.wraplength = 40;
    balsa_app.send_rfc2646_format_flowed =
	gnome_config_get_bool("SendRFC2646FormatFlowed=true");
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

    g_free(balsa_app.extern_editor_command);
    balsa_app.extern_editor_command = 
        gnome_config_get_string("ExternEditorCommand=gnome-edit %s");
    balsa_app.edit_headers = 
        gnome_config_get_bool("ExternEditorEditHeaders=false");

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

    balsa_app.open_inbox_upon_startup =
	gnome_config_get_bool("OpenInboxOnStartup=false");
    /* debugging enabled */
    balsa_app.debug = gnome_config_get_bool("Debug=false");

    balsa_app.close_mailbox_auto = gnome_config_get_bool("AutoCloseMailbox=true");
    balsa_app.close_mailbox_timeout = gnome_config_get_int("AutoCloseMailboxTimeout=10");

    balsa_app.remember_open_mboxes =
	gnome_config_get_bool("RememberOpenMailboxes=false");
    gnome_config_get_vector("OpenMailboxes", &open_mailbox_count,
			    &open_mailbox_vector);
    if (balsa_app.remember_open_mboxes && open_mailbox_count > 0
        && **open_mailbox_vector) {
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

gint config_save(void)
{
    gchar **open_mailboxes_vector, *tmp;
    gint i, j;
	char tmpkey[32];

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
    gnome_config_set_int("NotebookHeight", balsa_app.notebook_height);

    gnome_config_pop_prefix();

    /* Message View options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "MessageDisplay/");

    gnome_config_set_string("DateFormat", balsa_app.date_string);
    gnome_config_set_int("ShownHeaders", balsa_app.shown_headers);
    gnome_config_set_string("SelectedHeaders", balsa_app.selected_headers);
    gnome_config_set_string("MessageTitleFormat",
                            balsa_app.message_title_format);
    gnome_config_set_bool("ExpandTree", balsa_app.expand_tree);
    gnome_config_set_int("ThreadingType", balsa_app.threading_type);
    gnome_config_set_string("QuoteRegex", balsa_app.quote_regex);
    gnome_config_set_bool("RecognizeRFC2646FormatFlowed", 
			  balsa_app.recognize_rfc2646_format_flowed);
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
    gnome_config_set_bool("MsgSizeAsLines", balsa_app.line_length);
    gnome_config_set_bool("PageDownMod", balsa_app.pgdownmod);
    gnome_config_set_int("PageDownPercent", balsa_app.pgdown_percent);
    gnome_config_set_int("DragDefaultIsMove", balsa_app.drag_default_is_move);
    gnome_config_set_bool("DeleteImmediately",
                          balsa_app.delete_immediately);
    gnome_config_set_bool("HideDeleted",
                          balsa_app.hide_deleted);

    gnome_config_pop_prefix();

    /* Printing options ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Printing/");
    gnome_config_set_string("PaperSize",balsa_app.paper_size);
    gnome_config_set_string("PrintHeaderFont", balsa_app.print_header_font);
    gnome_config_set_float("PrintHeaderSize", balsa_app.print_header_size);
    gnome_config_set_float("PrintFooterSize", balsa_app.print_footer_size);
    gnome_config_set_string("PrintBodyFont", balsa_app.print_body_font);
    gnome_config_set_float("PrintBodySize", balsa_app.print_body_size);
    gnome_config_set_bool("PrintHighlightCited", balsa_app.print_highlight_cited);
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

    gnome_config_pop_prefix();

    /* IMAP folder scanning */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "IMAPFolderScanning/");
    gnome_config_set_int("ScanDepth", balsa_app.imap_scan_depth);
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
    gnome_config_set_bool("SendRFC2646FormatFlowed",
			   balsa_app.send_rfc2646_format_flowed);
    gnome_config_set_bool("AutoQuote", balsa_app.autoquote);
    gnome_config_set_bool("StripHtmlInReply", balsa_app.reply_strip_html);
    gnome_config_set_bool("ForwardAttached", balsa_app.forward_attached);

	gnome_config_set_int("AlwaysQueueSentMail", balsa_app.always_queue_sent_mail);
	gnome_config_set_int("CopyToSentbox", balsa_app.copy_to_sentbox);
    gnome_config_pop_prefix();

    /* Compose window ... */
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Compose/");

    gnome_config_set_string("ComposeHeaders", balsa_app.compose_headers);
    gnome_config_set_bool("RequestDispositionNotification", 
                          balsa_app.req_dispnotify);
    gnome_config_set_string("ExternEditorCommand", 
                            balsa_app.extern_editor_command);
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

    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "Toolbars/");
    gnome_config_set_int("WrapButtonText", balsa_app.toolbar_wrap_button_text);

    gnome_config_set_int("ToolbarCount", balsa_app.toolbar_count);
    
    for(i=0; i<balsa_app.toolbar_count; i++) {
        sprintf(tmpkey, "Toolbar%dID", i);
        gnome_config_set_int(tmpkey, balsa_app.toolbar_ids[i]);
        
        for(j=0;balsa_app.toolbars[i][j];j++) {
            sprintf(tmpkey, "Toolbar%dItem%d", i, j);
            gnome_config_set_string(tmpkey, balsa_app.toolbars[i][j]);
        }
        sprintf(tmpkey, "Toolbar%dItemCount", i);
        gnome_config_set_int(tmpkey, j);
    }
    gnome_config_pop_prefix();
    
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
	    if(g_strcasecmp(default_ident, ident->identity_name) == 0)
		balsa_app.current_ident = ident;
	}
	g_free(key);
	g_free(val);
    }

    if (g_list_length(balsa_app.identities) == 0)
        balsa_app.identities = 
	    g_list_append(balsa_app.identities,
			  libbalsa_identity_new_config(BALSA_CONFIG_PREFIX
						       "identity-default/",
						       "default"));
    
    if(balsa_app.current_ident == NULL)
	balsa_app.current_ident = 
	    LIBBALSA_IDENTITY(balsa_app.identities->data);

    g_free(default_ident);
}

/* config_identities_save:
   saves the balsa_app.identites list.
*/
void
config_identities_save(void)
{
    LibBalsaIdentity* ident;
    GList* list;
    gchar** conf_vec, *prefix, *key, *val;
    GList* old_identities = NULL;
    gint i = 0;
    void* iterator;
    int pref_len = strlen(IDENTITY_SECTION_PREFIX);

    conf_vec = g_malloc(sizeof(gchar*) * g_list_length(balsa_app.identities));

    g_assert(conf_vec != NULL);
    
    gnome_config_push_prefix(BALSA_CONFIG_PREFIX "identity/");
    gnome_config_set_string("CurrentIdentity", 
                            balsa_app.current_ident->identity_name);
    gnome_config_pop_prefix();
    g_free(conf_vec);

    /* clean removed sections */
    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {
	if (strncmp(key, IDENTITY_SECTION_PREFIX, pref_len) == 0) {
	    prefix = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);
	    old_identities = g_list_prepend(old_identities, prefix);
	}
	g_free(key);
	g_free(val);
    }
    for(list=old_identities; list; list = g_list_next(list)) {
	gnome_config_clean_section(list->data);
	g_free(list->data);
    }
    g_list_free(old_identities);

    /* save current */
    for (list = balsa_app.identities; list; list = g_list_next(list)) {
	ident = LIBBALSA_IDENTITY(list->data);
	prefix = g_strconcat(BALSA_CONFIG_PREFIX "identity-", 
			     ident->identity_name, "/", NULL);
        libbalsa_identity_save(ident, prefix);
	g_free(prefix);
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

    res[--i] = NULL;
    for(list = mailbox_list; list; list = g_list_next(list))
	res[--i] = g_strdup(LIBBALSA_MAILBOX(list->data)->url);

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
config_filters_load(void)
{
    LibBalsaFilter* fil;
    void *iterator;
    gchar * key,* tmp;
    gint pref_len = strlen(FILTER_SECTION_PREFIX);
    gint non_critical_error=FILTER_NOERR;

    filter_errno=FILTER_NOERR;
    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((filter_errno==FILTER_NOERR) &&
	   (iterator = gnome_config_iterator_next(iterator, &key, NULL))) {

	if (strncmp(key, FILTER_SECTION_PREFIX, pref_len) == 0) {
	    tmp=g_strconcat(BALSA_CONFIG_PREFIX,key,"/",NULL);
	    gnome_config_push_prefix(tmp);
	    g_free(tmp);	    
	    fil = libbalsa_filter_new_from_config();
	    FILTER_SETFLAG(fil,FILTER_VALID);
	    FILTER_SETFLAG(fil,FILTER_COMPILED);
	    gnome_config_pop_prefix();

	    if (fil) {
		libbalsa_conditions_new_from_config(BALSA_CONFIG_PREFIX,key,fil);
		if (filter_errno==FILTER_EFILESYN) {
		    /* Don't abort on syntax error, just remember it
		     * FIXME : we could make a GSList of errors to report them to user 
		     */
		    non_critical_error=FILTER_EFILESYN;
		    filter_errno=FILTER_NOERR;
		}
		else if (filter_errno!=FILTER_NOERR) {
		    /* Critical error, we abort the loading process
		     */
		    libbalsa_filter_free(fil,NULL);
		    return;
		}
		balsa_app.filters = g_slist_prepend(balsa_app.filters,(gpointer)fil);
	    }
	}
	g_free(key);	
    }
    if (filter_errno==FILTER_NOERR)
	filter_errno=non_critical_error;
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

	/* We suppress the final "/", this is necessary in order that
	 * libbalsa_conditions_save_config can construct the condition
	 * section name */
	tmp[i-1]='\0';
	libbalsa_conditions_save_config(fil->conditions,
					BALSA_CONFIG_PREFIX,section_name);
    }
    gnome_config_sync();
    /* This loop takes care of cleaning up old filter sections */
    while (TRUE) {
	i=snprintf(tmp,tmp_len,"%d/",nb++);
	if (gnome_config_has_section(buffer)) {
	    gnome_config_clean_section(buffer);
	    /* We suppress the final "/", this is necessary in order
	     * that libbalsa_clean_condition_sections can construct
	     * the condition section name */
	    tmp[i-1]='\0';
	    libbalsa_clean_condition_sections(BALSA_CONFIG_PREFIX,
					      section_name);
	}
	else break;
    }
    gnome_config_sync();
    g_free(buffer);
}

void config_mailbox_filters_save(LibBalsaMailbox * mbox)
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
	sprintf(tmpkey, "MRU%d", i+1);
	(*mru)=g_list_append((*mru), gnome_config_get_string(tmpkey));
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
