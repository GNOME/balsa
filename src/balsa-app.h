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
#include "c-client.h"
#include "mailbox.h"
#include "main-window.h"


#define DEFAULT_MAIL_SUBDIR "mail"


/* global balsa application structure */
struct
  {
    gchar *user_name;
    gchar *local_mail_directory;

    gchar *smtp_server;

    Mailbox *current_mailbox;
    GList *mailbox_list;

    MainWindow *main_window;
  }
balsa_app;

void init_balsa_app (int argc, char *argv[]);

#endif /* __BALSA_APP_H__ */
