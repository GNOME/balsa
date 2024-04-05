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

#ifndef __BALSA_APP_H__
#define __BALSA_APP_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "libbalsa.h"
#include "identity.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "information-dialog.h"
#include "main-window.h"

/* misc.h for LibBalsaCodeset */
#include "misc.h"

/* Work around nonprivileged installs so we can find icons */
#ifdef BALSA_LOCAL_INSTALL
#define gnome_pixmap_file( s ) g_strconcat( BALSA_RESOURCE_PREFIX, "/pixmaps/", s, NULL ) 
#define gnome_unconditional_pixmap_file( s ) g_strconcat( BALSA_RESOURCE_PREFIX, "/pixmaps", s, NULL ) 
#endif

/* global definitions */
#define BALSA_BUTTON_HEIGHT  30
#define BALSA_BUTTON_WIDTH  70

#define MW_DEFAULT_WIDTH 640
#define MW_DEFAULT_HEIGHT 480

/* column width settings */
#define NUM_DEFAULT_WIDTH 30
#define STATUS_DEFAULT_WIDTH 16
#define ATTACHMENT_DEFAULT_WIDTH 16
#define FROM_DEFAULT_WIDTH 128
#define SUBJECT_DEFAULT_WIDTH 180
#define DATE_DEFAULT_WIDTH 128
#define SIZE_DEFAULT_WIDTH 40

/* default width settings for the mailbox list columns, not fully utilized yet */
#define MBNAME_DEFAULT_WIDTH 80

#define NEWMSGCOUNT_DEFAULT_WIDTH 20
#define TOTALMSGCOUNT_DEFAULT_WIDTH 25
#define INFO_FIELD_LENGTH 10

/*
 * Default colour for quoted text
 * oh no, I used the US spelling.
 */
#define MAX_QUOTED_COLOR 2
#define DEFAULT_QUOTED_COLOR "#055"
#define DEFAULT_QUOTE_REGEX  "^[ \\t]*[|>:}#]"

#define DEFAULT_URL_COLOR    "dark blue"
#define DEFAULT_BAD_ADDRESS_COLOR    "red"

#define MAILBOX_MANAGER_WIDTH 350
#define MAILBOX_MANAGER_HEIGHT 400

#define MESSAGEBOX_WIDTH 450
#define MESSAGEBOX_HEIGHT 150

#define DEFAULT_MESSAGE_FONT "Monospace"
#define DEFAULT_SUBJECT_FONT "Monospace Bold"
#define DEFAULT_DATE_FORMAT "%x %X"
#define DEFAULT_PAPER_SIZE "A4"
#define DEFAULT_PRINT_HEADER_FONT "Monospace Regular 10"
#define DEFAULT_PRINT_BODY_FONT "Monospace Regular 10"
#define DEFAULT_PRINT_FOOTER_FONT "Sans Regular 8"
#define DEFAULT_SELECTED_HDRS "from to date cc subject"
#define DEFAULT_MESSAGE_TITLE_FORMAT "Message from %F: %s"
#define DEFAULT_ENCODING ENC8BIT
#define DEFAULT_LINESIZE 78

#define DEFAULT_CHECK_SIG FALSE
#define DEFAULT_CHECK_QUOTED FALSE

#define DEFAULT_BROKEN_CODESET "0"   /* == west european */


enum {
    WHILERETR,
    UNTILCLOSED,
    NEVER
};


typedef enum _ShownHeaders ShownHeaders;
enum _ShownHeaders {
    HEADERS_NONE = 0,
    HEADERS_SELECTED,
    HEADERS_ALL
};


typedef enum _BalsaMDNReply BalsaMDNReply;
enum _BalsaMDNReply {
    BALSA_MDN_REPLY_NEVER = 0,
    BALSA_MDN_REPLY_ASKME,
    BALSA_MDN_REPLY_ALWAYS,
};

typedef enum _MwActionAfterMove MwActionAfterMove;
enum _MwActionAfterMove {
    NEXT_UNREAD,
    NEXT,
    CLOSE
};

/* global balsa application structure */
extern struct BalsaApplication {
    GtkApplication *application;

    /* personal information */
    GList* identities;
    LibBalsaIdentity* current_ident;

    GSList* filters;

    gchar *local_mail_directory;

    GSList *smtp_servers;

    /* folder scanning */
    guint local_scan_depth;
    guint imap_scan_depth;

    BalsaWindow *main_window;
    BalsaMBList *mblist;
    GtkTreeStore *mblist_tree_store;

    LibBalsaMailbox *inbox;
    GList *inbox_input;		/* mailboxes such as POP3, etc that will be appending into inbox */
    LibBalsaMailbox *sentbox;
    LibBalsaMailbox *draftbox;
    LibBalsaMailbox *outbox;
    LibBalsaMailbox *trash;

    BalsaMailboxNode *root_node;

    /* timer for mm_exists callback */
    gint new_messages_timer;
    gint new_messages;

    /* timer for checking mail every xx minutes */
    gboolean check_mail_auto;
    gint check_mail_timer;
    gint check_mail_timer_id;

#ifdef HAVE_CANBERRA
    gint notify_new_mail_sound;
#endif
    
    gint notify_new_mail_dialog;

#ifdef ENABLE_SYSTRAY
    gint enable_systray_icon;
#endif
    gint enable_dkim_checks;

    /* automatically close mailboxes after XX minutes */
    gboolean close_mailbox_auto;
    gint close_mailbox_timeout; /* seconds */

    /* automatically expunge mailboxes after XX hours */
    gboolean expunge_auto;
    gint expunge_timeout;	/* seconds */

    gint check_imap;
    gint check_imap_inbox;
    gint quiet_background_check;
    gint msg_size_limit; /* for POP mailboxes; in kB */

    /* GUI settings (note: window sizes are tracked by the geometry-manager) */
    gint mblist_width;

    /* toolbars */
    int toolbar_wrap_button_text;
    GSList *main_window_toolbar_current;
    GSList *compose_window_toolbar_current;
    GSList *message_window_toolbar_current;

    /* file paths */
    gchar *attach_dir;
    gchar *save_dir;

    /* Column width settings */
    gint index_num_width;
    gint index_status_width;
    gint index_attachment_width;
    gint index_from_width;
    gint index_subject_width;
    gint index_date_width;
    gint index_size_width;

    /*gint mblist_height; PKGW: unused */
    gint notebook_height;	/* PKGW: used :-) */

    /* Column width settings for mailbox list window, not fully implemented yet */
    gint mblist_name_width;
    gboolean mblist_show_mb_content_info;
    gint mblist_newmsg_width;
    gint mblist_totalmsg_width;

    /* Colour of quoted text. */
    gboolean mark_quoted;
    gchar *quote_regex;
    GdkRGBA quoted_color[MAX_QUOTED_COLOR];

    /* text color of URL's */
    GdkRGBA url_color;

    gboolean send_progress_dialog;
    gboolean recv_progress_dialog;
    gboolean wordwrap;
    gint wraplength;
    gboolean browse_wrap;
    gint browse_wrap_length;
    ShownHeaders shown_headers;
    gboolean show_all_headers;
    gchar *selected_headers;
    gboolean expand_tree;
    gboolean show_mblist;
    gboolean show_notebook_tabs;
    enum { LAYOUT_DEFAULT, LAYOUT_WIDE_MSG, LAYOUT_WIDE_SCREEN } layout_type;
    gboolean view_message_on_open;
    gboolean ask_before_select;
    gboolean pgdownmod;
    gint pgdown_percent;

    /* Show toolbars, status bar, and subject-or-sender search bar */
    gboolean show_main_toolbar;
    gboolean show_message_toolbar;
    gboolean show_compose_toolbar;
    gboolean show_statusbar;
    gboolean show_sos_bar;

    gboolean empty_trash_on_exit;
    gboolean previewpane;

    /* Source viewer */
    gboolean source_escape_specials;

    /* MRU mailbox tree */
    gint mru_tree_width;
    gint mru_tree_height;

    /* what to do with message window after moving the message */
    MwActionAfterMove mw_action_after_move;

    /* external editor */
    gboolean edit_headers;

    /* arp --- string to prefix "replied to" messages. */
    gchar *quote_str;

    /* reply/forward: automatically quote original when replying */
    gboolean autoquote;

    /* reply/forward: don't include text/html parts */
    gboolean reply_strip_html;

    /* forward attached by default */
    gboolean forward_attached;

    /* command line options */
    gint open_inbox_upon_startup;
    gint check_mail_upon_startup;
    gint remember_open_mboxes;
    gint open_unread_mailbox;

    /* list of currently open mailboxes */
    GList *open_mailbox_list;  /* data is a pointer to the mailbox */
    gchar *current_mailbox_url;/* remember for next session */
    
    /* fonts */
    gboolean use_system_fonts;
    gchar *message_font;
    gchar *subject_font;

    /* encoding stuff */
    gchar *date_string;

    /* printing */
    GtkPageSetup *page_setup;
    GtkPrintSettings *print_settings;
    gdouble margin_left;
    gdouble margin_top;
    gdouble margin_right;
    gdouble margin_bottom;
    gchar* print_header_font;  /* font for printing headers */
    gchar* print_body_font;    /* font for printing text parts */
    gchar* print_footer_font;  /* font for printing footers */
    gboolean print_highlight_cited;
    gboolean print_highlight_phrases;

    /* compose */
    gchar *compose_headers;
    gboolean always_queue_sent_mail;
    gboolean copy_to_sentbox;

    /* timer for sending mail every xx minutes */
    gboolean send_mail_auto;
    guint send_mail_timer;

    /* mailbox indices */
    GtkWidget *notebook;

    /* address book */
    GList *address_book_list;
    LibBalsaAddressBook *default_address_book;

    /* spell checking */
    gchar   *spell_check_lang;
#if HAVE_GSPELL || HAVE_GTKSPELL
    gboolean spell_check_active;
#else                           /* HAVE_GSPELL */
    gboolean check_sig;
    gboolean check_quoted;
#endif                          /* HAVE_GSPELL */

    /* Information messages */
    BalsaInformationShow information_message;
    BalsaInformationShow warning_message;
    BalsaInformationShow error_message;
    BalsaInformationShow debug_message;
    BalsaInformationShow fatal_message;

    /* how to act if a MDN request is received */
    BalsaMDNReply mdn_reply_clean;
    BalsaMDNReply mdn_reply_notclean;

    /* how to handle multipart/alternative */
    gboolean display_alt_plain;

    /* how to handle broken mails with 8-bit chars */
    gboolean convert_unknown_8bit;
    LibBalsaCodeset convert_unknown_8bit_codeset;

    /* gpgme stuff */
    gboolean has_openpgp;
    gboolean has_smime;
    gboolean warn_reply_decrypted;

    /* Most recently used lists */
    GList *folder_mru;
    GList *fcc_mru;
    GList *pipe_cmds;

    gboolean expunge_on_close;

    /* use as default email client for GNOME */
    int default_client;

    gboolean in_destruction;
} balsa_app;

#define BALSA_IS_MAILBOX_SPECIAL(a) ((a)==balsa_app.inbox || (a)==balsa_app.trash || (a)==balsa_app.outbox||(a)==balsa_app.draftbox || (a)==balsa_app.sentbox)

void balsa_app_init(void);
void balsa_app_destroy(void);
void update_timer(gboolean update, guint minutes);

gchar *ask_password(LibBalsaServer *server,
	                const gchar    *cert_subject,
					gpointer        user_data);
void balsa_open_mailbox_list(gchar ** urls);

/* Search functions */
BalsaMailboxNode *balsa_find_mailbox(LibBalsaMailbox * mailbox);
BalsaMailboxNode *balsa_find_dir(LibBalsaServer *server, const gchar * path);
BalsaMailboxNode *balsa_find_url(const gchar * url);
LibBalsaMailbox *balsa_find_mailbox_by_url(const gchar * url);
LibBalsaMailbox *balsa_find_sentbox_by_url(const gchar * url);
void balsa_add_open_mailbox_urls(GPtrArray * url_array);

/** Returns a short mailbox name that identifies the host. This is
    longer than LibBalsaMailbox::name which contains only filename
    without information about mailbox's location in the hierarchy. */
gchar *balsa_get_short_mailbox_name(const gchar * url);
gboolean balsa_find_iter_by_data(GtkTreeIter * iter, gpointer data);
BalsaIndex* balsa_find_index_by_mailbox(LibBalsaMailbox* mailbox);

void  balsa_remove_children_mailbox_nodes(BalsaMailboxNode * mbnode);

GRegex *balsa_quote_regex_new(void);

/* return TRUE iff Balsa is built with Autocrypt support and any identity uses it */
gboolean balsa_autocrypt_in_use(void);

#endif				/* __BALSA_APP_H__ */
