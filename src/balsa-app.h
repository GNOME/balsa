/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
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

#ifndef __BALSA_APP_H__
#define __BALSA_APP_H__

#include <gnome.h>
#include "libbalsa.h"
#include "identity.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "information-dialog.h"
#include "main-window.h"
#include "toolbar-factory.h"

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
#define FROM_DEFAULT_WIDTH 160
#define SUBJECT_DEFAULT_WIDTH 250
#define DATE_DEFAULT_WIDTH 128
#define SIZE_DEFAULT_WIDTH 40

/* default width settings for the mailbox list columns, not fully utilized yet */
#define MBNAME_DEFAULT_WIDTH 80

#define NEWMSGCOUNT_DEFAULT_WIDTH 45
#define TOTALMSGCOUNT_DEFAULT_WIDTH 45
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

#define DEFAULT_MESSAGE_FONT "monospace 10"
#define DEFAULT_SUBJECT_FONT "helvetica Bold 10"
#define DEFAULT_DATE_FORMAT "%x %X"
#define DEFAULT_PAPER_SIZE "A4"
#define DEFAULT_PRINT_HEADER_FONT "Times Roman 11"
#define DEFAULT_PRINT_BODY_FONT "Courier 11"
#define DEFAULT_PRINT_FOOTER_FONT "Times Roman 7"
#define DEFAULT_SELECTED_HDRS "from to date cc subject"
#define DEFAULT_MESSAGE_TITLE_FORMAT "Message from %F: %s"
#define DEFAULT_ENCODING ENC8BIT
#define DEFAULT_LINESIZE 78

#define DEFAULT_PSPELL_MODULE SPELL_CHECK_MODULE_ISPELL
#define DEFAULT_PSPELL_SUGGEST_MODE SPELL_CHECK_SUGGEST_NORMAL
#define DEFAULT_PSPELL_IGNORE_SIZE 0
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


/* The different spell modules available to the program. */
#define NUM_PSPELL_MODULES 2
typedef enum _SpellCheckModule SpellCheckModule;
enum _SpellCheckModule {
    SPELL_CHECK_MODULE_ISPELL,
    SPELL_CHECK_MODULE_ASPELL
};
const gchar **spell_check_modules_name;


/* The suggestion modes available to spell.  If this is changed,
 * don't forget to also update the array in pref-manager.c containing
 * the labels used in the preferences dialog. 
 * */
#define NUM_SUGGEST_MODES 3
typedef enum _SpellCheckSuggestMode SpellCheckSuggestMode;
enum _SpellCheckSuggestMode {
    SPELL_CHECK_SUGGEST_FAST,
    SPELL_CHECK_SUGGEST_NORMAL,
    SPELL_CHECK_SUGGEST_BAD_SPELLERS
};
const gchar **spell_check_suggest_mode_name;


typedef enum _BalsaMDNReply BalsaMDNReply;
enum _BalsaMDNReply {
    BALSA_MDN_REPLY_NEVER = 0,
    BALSA_MDN_REPLY_ASKME,
    BALSA_MDN_REPLY_ALWAYS,
};


/* global balsa application structure */
extern struct BalsaApplication {
    /* personal information */
    GList* identities;
    LibBalsaIdentity* current_ident;

    GSList* filters;

    gchar *local_mail_directory;
#if ENABLE_ESMTP
    gchar *smtp_server;
    gchar *smtp_user;
    gchar *smtp_passphrase;
    auth_context_t smtp_authctx;
    gint smtp_tls_mode;
    gchar *smtp_certificate_passphrase;
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

    /* automatically close mailboxes after XX minutes */
    gboolean close_mailbox_auto;
    gint close_mailbox_timeout; /* seconds */
    /* automatically commit mailboxes after XX minutes */
    gboolean commit_mailbox_auto;
    gint commit_mailbox_timeout; /* seconds */
    gint check_imap;
    gint check_imap_inbox;
    gint quiet_background_check;

    /* GUI settings */
    gint mw_width;
    gint mw_height;
    gint mblist_width;
    gint sw_width; /* sendmsg window */
    gint sw_height;
    gint message_window_width;
    gint message_window_height;

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

    /* Colour of mailboxes with unread messages in mailbox list */
    GdkVisual *visual;
    GdkColormap *colormap;

    /* Colour of quoted text. */
    gchar *quote_regex;
    GdkColor quoted_color[MAX_QUOTED_COLOR];

    /* text color of URL's */
    GdkColor url_color;

    /* label color of bad addresses */
    GdkColor bad_address_color;

    guint pwindow_option;
    gboolean wordwrap;
    gint wraplength;
    gboolean browse_wrap;
    gint browse_wrap_length;
    ShownHeaders shown_headers;
    gboolean show_all_headers;
    gchar *selected_headers;
    gchar *message_title_format;
    gboolean expand_tree;
    gboolean show_mblist;
    gboolean show_notebook_tabs;
    gboolean alternative_layout;
    gboolean view_message_on_open;
    gboolean line_length;
    gboolean pgdownmod;
    gint pgdown_percent;

    gboolean empty_trash_on_exit;
    gboolean previewpane;
    gboolean source_escape_specials;
    gboolean debug;

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
    
    /* font used to display messages */
    gchar *message_font;
    gchar *subject_font;

    /* encoding stuff */
    guint encoding_style;
    gchar *date_string;

    /* printing */
    gchar* paper_size; /* A4 or Letter */
    gchar* margin_left;
    gchar* margin_top;
    gchar* margin_right;
    gchar* margin_bottom;
    gchar* print_unit;
    gchar* print_layout;
    gchar* paper_orientation;
    gchar* page_orientation;
    gchar* print_header_font;  /* font for printing headers */
    gchar* print_body_font;    /* font for printing text parts */
    gchar* print_footer_font;  /* font for printing footers */
    gboolean print_highlight_cited;

    /* compose */
    gchar *compose_headers;
    gboolean always_queue_sent_mail;
    gboolean copy_to_sentbox;

    /* appbar */
    GnomeAppBar *appbar;
    GtkWidget *notebook;

    /* address book */
    GList *address_book_list;
    LibBalsaAddressBook *default_address_book;

    /* spell checking */
    SpellCheckModule module;
    SpellCheckSuggestMode suggestion_mode;
    guint ignore_size;
    gboolean check_sig;
    gboolean check_quoted;

    /* Information messages */
    BalsaInformationShow information_message;
    BalsaInformationShow warning_message;
    BalsaInformationShow error_message;
    BalsaInformationShow debug_message;
    BalsaInformationShow fatal_message;

    /* Tooltips */
    GtkTooltips *tooltips;

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

    GList *folder_mru;
    GList *fcc_mru;
    gboolean delete_immediately;
    gboolean hide_deleted;

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
gboolean balsa_find_iter_by_data(GtkTreeIter * iter, gpointer data);
BalsaIndex* balsa_find_index_by_mailbox(LibBalsaMailbox* mailbox);

void  balsa_remove_children_mailbox_nodes(BalsaMailboxNode * mbnode);

GtkWidget *create_label(const gchar * label, GtkWidget * table, gint row);
GtkWidget *create_entry(GtkDialog *mcw, GtkWidget * table, 
			GtkSignalFunc func, gpointer data, gint row, 
			const gchar * initval, GtkWidget* hotlabel);
GtkWidget *create_check(GtkDialog *mcw, const gchar * label, 
			GtkWidget * table, gint row, gboolean initval);

#endif				/* __BALSA_APP_H__ */
