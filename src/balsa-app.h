/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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
#include "mailbox.h"
#include "balsa-mblist.h"
#include "index-child.h"

/* global definitions */
#define BALSA_BUTTON_HEIGHT  30
#define BALSA_BUTTON_WIDTH  70

#define MW_DEFAULT_WIDTH 640
#define MW_DEFAULT_HEIGHT 480

#define MAILBOX_MANAGER_WIDTH 350
#define MAILBOX_MANAGER_HEIGHT 400

#define MESSAGEBOX_WIDTH 450
#define MESSAGEBOX_HEIGHT 150



/* global balsa application structure */
extern struct BalsaApplication
  {
    proplist_t proplist;
    /* personal information */
    gchar *real_name;
    gchar *email;
    gchar *replyto;

    gchar *local_mail_directory;
    gchar *smtp_server;

    gchar *signature_path;
    gchar *signature;

    BalsaMBList *mblist;
    IndexChild *current_index_child;

    Mailbox *inbox;
    GList *inbox_input;		/* mailboxes such as POP3, etc that will be appending into inbox */
    Mailbox *outbox;
    Mailbox *trash;

    GNode *mailbox_nodes;

    /* timer for mm_exists callback */
    gint new_messages_timer;
    gint new_messages;

    /* timer for checking mail every xx minutes */
    gint check_mail_timer;

    /* GUI settings */
    gint mw_width;
    gint mw_height;
    gint mblist_width;
    gint mblist_height;

    GtkToolbarStyle toolbar_style;
    GnomeMDIMode mdi_style;

    gboolean previewpane;
    gboolean debug;

  /* arp --- string to prefix "replied to" messages. */
  gchar *leadin_str;
  }
balsa_app;


void balsa_app_init (void);
gint do_load_mailboxes (void);

#endif /* __BALSA_APP_H__ */
