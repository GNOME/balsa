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
#include <proplist.h>
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

#define MAILBOX_MANAGER_WIDTH 350
#define MAILBOX_MANAGER_HEIGHT 400

#define MESSAGEBOX_WIDTH 450
#define MESSAGEBOX_HEIGHT 150

#define DEFAULT_MESSAGE_FONT "-*-fixed-medium-r-normal-*-*-*-*-*-c-*-iso8859-1"
#define DEFAULT_CHARSET "ISO-8859-1"
#define DEFAULT_ENCODING ENC8BIT
#define DEFAULT_LINESIZE 78

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



/* global balsa application structure */
extern struct BalsaApplication
{
  proplist_t proplist;
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
  gchar *signature_path;
  
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
  
  /* GUI settings */
  gint mw_width;
  gint mw_height;
  gint mblist_width;
/*  gint mblist_height; PKGW: unused */
    gint notebook_height; /* PKGW: used :-) */

#ifdef BALSA_SHOW_INFO
  gboolean mblist_show_mb_content_info;
#endif

  GtkToolbarStyle toolbar_style;
  GnomeMDIMode mdi_style;
  gint pwindow_option;
  gboolean wordwrap;
  gint wraplength;
  
  gboolean empty_trash_on_exit;
  gboolean previewpane;
  gboolean debug;
  gboolean smtp;
  
  /* arp --- string to prefix "replied to" messages. */
  gchar *quote_str;

  gint check_mail_upon_startup;

  /* font used to display messages */
  gchar *message_font;

  /* encoding stuff */
  gint encoding_style;
  gchar *charset;

  gint checkbox;
  /* printing */
  Printing_t PrintCommand;

  /* appbar */
  GnomeAppBar* appbar;
  GtkWidget* notebook;

  
}
balsa_app;


void balsa_app_init (void);
gint do_load_mailboxes (void);
void update_timer( gboolean update, guint minutes );

#endif /* __BALSA_APP_H__ */
