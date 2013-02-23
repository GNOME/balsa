/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

#if ENABLE_ESMTP
#include <libesmtp.h>			/* part of libESMTP */
#include <auth-client.h>		/* part of libESMTP */
#endif

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
    /* personal information */
    GList* identities;
    LibBalsaIdentity* current_ident;

    GSList* filters;

    gchar *local_mail_directory;

#if ENABLE_ESMTP
    GSList *smtp_servers;
#endif

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

    /* This can be configured from the gnome control panel */
    /* It's here just in case some other app also uses the same */
    /* system wide sound event and you want Balsa to behave differently */
    /* There is no prefs setting for this item */
    gint notify_new_mail_sound;
    
    gint notify_new_mail_dialog;
    gint notify_new_mail_icon;

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

    /* GUI settings */
    gint mw_width;
    gint mw_height;
    gboolean mw_maximized;
    gint mblist_width;
    gint sw_width; /* sendmsg window */
    gint sw_height;
    gboolean sw_maximized;
    gint message_window_width;
    gint message_window_height;
    gboolean message_window_maximized;

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

    guint pwindow_option;
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
#if defined(ENABLE_TOUCH_UI)
    gboolean do_file_format_check; /* do file format check on attaching */
    gboolean enable_view_filter;   /* enable quick view filter */
#endif

    /* Show toolbars, status bar, and subject-or-sender search bar */
    gboolean show_main_toolbar;
    gboolean show_message_toolbar;
    gboolean show_compose_toolbar;
    gboolean show_statusbar;
    gboolean show_sos_bar;

    gboolean empty_trash_on_exit;
    gboolean previewpane;
    gboolean debug;

    /* Source viewer */
    gboolean source_escape_specials;
    gint source_width;
    gint source_height;

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
    
    /* font used to display messages */
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

    /* mailbox indices */
    GtkWidget *notebook;

    /* address book */
    GList *address_book_list;
    LibBalsaAddressBook *default_address_book;

    /* spell checking */
#if HAVE_GTKSPELL
    gchar   *spell_check_lang;
    gboolean spell_check_active;
#else                           /* HAVE_GTKSPELL */
    gboolean check_sig;
    gboolean check_quoted;
#endif                          /* HAVE_GTKSPELL */

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

#ifdef HAVE_GPGME
    /* gpgme stuff */
    gboolean has_openpgp;
    gboolean has_smime;
#endif 

    /* Most recently used lists */
    GList *folder_mru;
    GList *fcc_mru;
    GList *pipe_cmds;

    gboolean expunge_on_close;

    /* use as default email client for GNOME */
    int default_client;
} balsa_app;

#define BALSA_IS_MAILBOX_SPECIAL(a) ((a)==balsa_app.inbox || (a)==balsa_app.trash || (a)==balsa_app.outbox||(a)==balsa_app.draftbox || (a)==balsa_app.sentbox)

void balsa_app_init(void);
void balsa_app_destroy(void);
void update_timer(gboolean update, guint minutes);

gchar *ask_password(LibBalsaServer * server, LibBalsaMailbox * mbox);
GtkWidget *balsa_stock_button_with_label(const char *icon,
					 const char *label);
gboolean open_mailboxes_idle_cb(gchar * names[]);

/* Search functions */
BalsaMailboxNode *balsa_find_mailbox(LibBalsaMailbox * mailbox);
BalsaMailboxNode *balsa_find_dir(LibBalsaServer *server, const gchar * path);
BalsaMailboxNode *balsa_find_url(const gchar * url);
LibBalsaMailbox *balsa_find_mailbox_by_url(const gchar * url);
LibBalsaMailbox *balsa_find_sentbox_by_url(const gchar * url);

/** Returns a short mailbox name that identifies the host. This is
    longer than LibBalsaMailbox::name which contains only filename
    without information about mailbox's location in the hierarchy. */
gchar *balsa_get_short_mailbox_name(const gchar * url);
gboolean balsa_find_iter_by_data(GtkTreeIter * iter, gpointer data);
BalsaIndex* balsa_find_index_by_mailbox(LibBalsaMailbox* mailbox);

void  balsa_remove_children_mailbox_nodes(BalsaMailboxNode * mbnode);

#if USE_GREGEX
GRegex *balsa_quote_regex_new(void);
#endif                          /* USE_GREGEX */

#endif				/* __BALSA_APP_H__ */
