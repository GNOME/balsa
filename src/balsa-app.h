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

#ifndef __BALSA_APP_H__
#define __BALSA_APP_H__

#include <gnome.h>
#include "libbalsa.h"
#include "identity.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "main-window.h"
#include "information-dialog.h"

#if ENABLE_ESMTP
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

/* Default colour for mailboxes with unread messages */
#define MBLIST_UNREAD_COLOR "rgb:0000/FFFF/0000"

/*
 * Default colour for quoted text
 * oh no, I used the US spelling.
 */
#define MAX_QUOTED_COLOR 6
#define DEFAULT_QUOTED_COLOR "rgb:0000/5000/5000"
#define DEFAULT_QUOTE_REGEX  "^(([ \tA-Z])\1*[|>:}#])"

#define DEFAULT_URL_COLOR    "rgb:A000/0000/0000"

#define MAILBOX_MANAGER_WIDTH 350
#define MAILBOX_MANAGER_HEIGHT 400

#define MESSAGEBOX_WIDTH 450
#define MESSAGEBOX_HEIGHT 150

#define DEFAULT_MESSAGE_FONT "-*-fixed-medium-r-normal-*-*-*-*-*-c-*-iso8859-1"
#define DEFAULT_SUBJECT_FONT "-*-fixed-bold-r-normal-*-*-*-*-*-c-*-iso8859-1"
#define DEFAULT_DATE_FORMAT "%Y.%m.%d %H:%M"
#define DEFAULT_PAPER_SIZE "A4"
#define DEFAULT_SELECTED_HDRS "from to date cc subject"
#define DEFAULT_ENCODING ENC8BIT
#define DEFAULT_LINESIZE 78

#define DEFAULT_PSPELL_MODULE SPELL_CHECK_MODULE_ISPELL
#define DEFAULT_PSPELL_SUGGEST_MODE SPELL_CHECK_SUGGEST_NORMAL
#define DEFAULT_PSPELL_IGNORE_SIZE 0
#define DEFAULT_CHECK_SIG FALSE
#define DEFAULT_CHECK_QUOTED FALSE


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


/* The different pspell modules available to the program. */
#define NUM_PSPELL_MODULES 2
typedef enum _SpellCheckModule SpellCheckModule;
enum _SpellCheckModule {
    SPELL_CHECK_MODULE_ISPELL,
    SPELL_CHECK_MODULE_ASPELL
};
const gchar **spell_check_modules_name;


/* The suggestion modes available to pspell.  If this is changed,
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


#ifdef BALSA_MDN_REPLY
typedef enum _BalsaMDNReply BalsaMDNReply;
enum _BalsaMDNReply {
    BALSA_MDN_REPLY_NEVER = 0,
    BALSA_MDN_REPLY_ASKME,
    BALSA_MDN_REPLY_ALWAYS,
};
#endif



/* global balsa application structure */
extern struct BalsaApplication {
    /* personal information */
    GList* identities;
    LibBalsaIdentity* current_ident;

    gchar *local_mail_directory;
#if ENABLE_ESMTP
    gchar *smtp_server;
    gchar *smtp_user;
    gchar *smtp_passphrase;
    auth_context_t smtp_authctx;
#endif

    BalsaWindow *main_window;
    BalsaMBList *mblist;

    LibBalsaMailbox *inbox;
    GList *inbox_input;		/* mailboxes such as POP3, etc that will be appending into inbox */
    LibBalsaMailbox *sentbox;
    LibBalsaMailbox *draftbox;
    LibBalsaMailbox *outbox;
    LibBalsaMailbox *trash;

    GNode *mailbox_nodes;

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
    gint close_mailbox_timeout;
    gint check_imap;
    gint check_imap_inbox;
    gint quiet_background_check;

    /* GUI settings */
    gint mw_width;
    gint mw_height;
    gint mblist_width;
    gint sw_width; /* sendmsg window */
    gint sw_height;
    int toolbar_count;
    int *toolbar_ids;
    char ***toolbars;
    int toolbar_wrap_button_text;

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
    GdkColor mblist_unread_color;

    /* Colour of quoted text. */
    gchar *quote_regex;
    GdkColor quoted_color[MAX_QUOTED_COLOR];

    /* text color of URL's */
    GdkColor url_color;

    GtkToolbarStyle toolbar_style;
    GnomeMDIMode mdi_style;
    gint pwindow_option;
    gboolean wordwrap;
    gint wraplength;
    gboolean browse_wrap;
    ShownHeaders shown_headers;
    gchar *selected_headers;
    BalsaIndexThreadingType threading_type;
    gboolean show_mblist;
    gboolean show_notebook_tabs;
    gboolean alternative_layout;
    gboolean view_message_on_open;
    gboolean line_length;
    gboolean pgdownmod;
    gint pgdown_percent;

    gboolean empty_trash_on_exit;
    gboolean previewpane;
    gboolean debug;

    /* arp --- string to prefix "replied to" messages. */
    gchar *quote_str;

    /* reply/forward: don't include text/html parts */
    gboolean reply_strip_html;

    /* command line options */
    gint check_mail_upon_startup;
    gint remember_open_mboxes;
    gint open_unread_mailbox;
    GList *open_mailbox_list;	/* data is a pointer to the mailbox */
    gchar *compose_email;
    gchar *attach_file;

    /* font used to display messages */
    gchar *message_font;
    gchar *subject_font;

    /* encoding stuff */
    gint encoding_style;
    gchar *date_string;

    /* printing */
    gchar* paper_size; /* A4 or Letter */

    /* compose: shown headers */
    gchar *compose_headers;

    /* compose: request a disposition notification */
    gboolean req_dispnotify;   
    gboolean always_queue_sent_mail;
 

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

#ifdef BALSA_MDN_REPLY
    /* how to act if a MDN request is received */
    BalsaMDNReply mdn_reply_clean;
    BalsaMDNReply mdn_reply_notclean;
#endif

} balsa_app;

#define BALSA_IS_MAILBOX_SPECIAL(a) ((a)==balsa_app.inbox || (a)==balsa_app.trash || (a)==balsa_app.outbox||(a)==balsa_app.draftbox)

void balsa_app_init(void);
gboolean do_load_mailboxes(void);
void update_timer(gboolean update, guint minutes);

gchar *ask_password(LibBalsaServer * server, LibBalsaMailbox * mbox);
GtkWidget *gnome_stock_button_with_label(const char *icon,
					 const char *label);
gboolean open_mailboxes_idle_cb(gchar * names[]);

GNode *find_gnode_in_mbox_list(GNode * gnode_list, LibBalsaMailbox * mailbox);
GNode *balsa_find_mbnode(GNode* gnode, BalsaMailboxNode* mbnode);
void  balsa_remove_children_mailbox_nodes(GNode* gnode);
BalsaIndex* balsa_find_index_by_mailbox(LibBalsaMailbox* mailbox);

GtkWidget *create_label(const gchar * label, GtkWidget * table, 
			       gint row, guint *keyval);
GtkWidget *create_entry(GnomeDialog *mcw, GtkWidget * table, 
			GtkSignalFunc func, gpointer data, gint row, 
			const gchar * initval, const guint keyval);
GtkWidget *create_check(GnomeDialog *mcw, const gchar * label, 
			GtkWidget * table, gint row, gboolean initval);

#endif				/* __BALSA_APP_H__ */
