/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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
#include "balsa-mblist.h"
#include "balsa-index-page.h"

/* Work around nonprivileged installs so we can find icons */
#ifdef BALSA_LOCAL_INSTALL
#define gnome_pixmap_file( s ) g_strdup( g_strconcat( BALSA_RESOURCE_PREFIX, "/pixmaps/", s, NULL ) )
#define gnome_unconditional_pixmap_file( s ) g_strdup( g_strconcat( BALSA_RESOURCE_PREFIX, "/pixmaps", s, NULL ) )
#endif

/* global definitions */
#define BALSA_BUTTON_HEIGHT  30
#define BALSA_BUTTON_WIDTH  70

#define MW_DEFAULT_WIDTH 640
#define MW_DEFAULT_HEIGHT 480

/* column width settings */
#define NUM_DEFAULT_WIDTH 40
#define STATUS_DEFAULT_WIDTH 16
#define ATTACHMENT_DEFAULT_WIDTH 16
#define FROM_DEFAULT_WIDTH 170
#define SUBJECT_DEFAULT_WIDTH 260
#define DATE_DEFAULT_WIDTH 138

/* default width settings for the mailbox list columns, not fully utilized yet */
#define MBNAME_DEFAULT_WIDTH 80
#ifdef BALSA_SHOW_INFO
#define NEWMSGCOUNT_DEFAULT_WIDTH 45
#define TOTALMSGCOUNT_DEFAULT_WIDTH 45
#define INFO_FIELD_LENGTH 10
#endif

/* Default colour for mailboxes with unread messages */
#define MBLIST_UNREAD_COLOR_RED 0
#define MBLIST_UNREAD_COLOR_BLUE 65535
#define MBLIST_UNREAD_COLOR_GREEN 0

#define MAILBOX_MANAGER_WIDTH 350
#define MAILBOX_MANAGER_HEIGHT 400

#define MESSAGEBOX_WIDTH 450
#define MESSAGEBOX_HEIGHT 150

#define DEFAULT_MESSAGE_FONT "-*-fixed-medium-r-normal-*-*-*-*-*-c-*-iso8859-1"
#define DEFAULT_DATE_FORMAT "%a, %d %b %Y %H:%M:%S"
#define DEFAULT_SELECTED_HDRS "from to date cc subject"
#define DEFAULT_CHARSET "ISO-8859-1"
#define DEFAULT_ENCODING ENC8BIT
#define DEFAULT_LINESIZE 78
#define DEFAULT_QUOTE "> "
#define DEFAULT_COMPOSE_HEADERS "to subject cc"
#define DEFAULT_SMTP_SERVER "localhost"

#define DEFAULT_MBLIST_WIDTH 120
#define DEFAULT_NOTEBOOK_HEIGHT 200
#define DEFAULT_WRAPLENGTH 79
#define DEFAULT_PRINTCOMMAND "a2ps -d -q %s"
enum
{
  WHILERETR,
  UNTILCLOSED,
  NEVER
};

typedef struct stPrinting Printing_t;
struct stPrinting{
    gint  breakline;
    gint  linesize;
    gchar *PrintCommand;
};


enum ShownHeaders {
   HEADERS_NONE = 0,
   HEADERS_SELECTED,
   HEADERS_ALL 
};

/* global balsa application structure */
extern struct BalsaApplication
{
  /* personal information */
  Address *address;
  gchar *replyto;
  gchar *bcc;
  
  gchar *local_mail_directory;
  gchar *smtp_server;

  /* signature stuff */
  gboolean sig_sending;
  gboolean sig_whenforward;
  gboolean sig_whenreply;
  gboolean sig_separator;
  gchar *signature_path;

  BalsaWindow* main_window;
  BalsaMBList *mblist;
  
  Mailbox *inbox;
  GList *inbox_input;		/* mailboxes such as POP3, etc that will be appending into inbox */
  Mailbox *sentbox;
  Mailbox *draftbox;
  Mailbox *outbox;
  Mailbox *trash;
  
  GNode *mailbox_nodes;
  
  /* timer for mm_exists callback */
  gint new_messages_timer;
  gint new_messages;
  
  /* timer for checking mail every xx minutes */
  gboolean check_mail_auto;
  gint check_mail_timer;
  gint check_mail_timer_id;
  
#ifdef BALSA_SHOW_INFO
  gboolean mblist_show_mb_content_info;
#endif

  /* Colour of mailboxes with unread messages in mailbox list */
  GdkColor mblist_unread_color;
  
  GtkToolbarStyle toolbar_style;
  GnomeMDIMode mdi_style;
  gint pwindow_option;
  gboolean wordwrap;
  gint wraplength;
  gboolean browse_wrap;
  enum ShownHeaders shown_headers;
  gchar * selected_headers;
  gboolean show_mblist;
  gboolean show_notebook_tabs;

  gboolean empty_trash_on_exit;
  gboolean previewpane;
  gboolean debug;
  gboolean smtp;
  
  /* arp --- string to prefix "replied to" messages. */
  gchar *quote_str;

  /* command line options */

  gint check_mail_upon_startup;
  gint open_unread_mailbox;
  gchar *open_mailbox;
  gchar* compose_email;

  /* font used to display messages */
  gchar *message_font;

  /* encoding stuff */
  gint encoding_style;
  gchar *charset;
	gchar *date_string;

  gint checkbox;

  /* printing */
  Printing_t PrintCommand;

  /* compose: shown headers */
  gchar* compose_headers; 

  /* appbar */
  GnomeAppBar* appbar;
  GtkWidget* notebook;

  
}
balsa_app;

typedef struct BalsaFudgeColor_t
{
	gint pixel;
	gint red;
	gint green;
	gint blue;
} BalsaFudgeColor;

void balsa_app_init (void);
gint do_load_mailboxes (void);
void update_timer( gboolean update, guint minutes );

#endif /* __BALSA_APP_H__ */
