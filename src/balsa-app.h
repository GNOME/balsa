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
#include "mailbox.h"


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
  /* personal information */
  gchar *real_name;
  gchar *username;
  gchar *hostname;
  gchar *email;
  gchar *organization;
  gchar *local_mail_directory;
  gchar *smtp_server;
  gchar *signature;

  GtkWidget *current_index;

  GList *mailbox_list;
  GList *addressbook_list;


  /* timer for mm_exists callback */
  gint new_messages_timer;
  gint new_messages;


  /* timer for checking mail every xx minutes */
  gint check_mail_timer;


  /* GUI settings */
  gint mw_width;
  gint mw_height;

  GtkToolbarStyle toolbar_style;
  guint mdi_style;

  gint debug;
}
balsa_app;


void init_balsa_app (int argc, char *argv[]);

#endif /* __BALSA_APP_H__ */
