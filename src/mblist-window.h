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

#ifndef MBLIST_WINDOW_H
#define MBLIST_WINDOW_H 1


#include "config.h"


#include <gnome.h>
#include "mailbox.h"
#include "main-window.h"


void mblist_menu_add_cb (GtkWidget * widget, gpointer data);
void mblist_menu_edit_cb (GtkWidget * widget, gpointer data);
void mblist_menu_delete_cb (GtkWidget * widget, gpointer data);
void mblist_menu_open_cb (GtkWidget * widget, gpointer data);
void mblist_menu_close_cb (GtkWidget * widget, gpointer data);

GtkWidget *balsa_mailbox_list_window_new(BalsaWindow *window);

Mailbox *mblist_get_selected_mailbox (void);



#endif /* MBLIST_WINDOW_H */
