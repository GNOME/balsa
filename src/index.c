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

#include <string.h>
#include "index.h"
#include "balsa-message.h"

void mailbox_menu_select (GtkWidget * menuitem);

/* callback for index selection; displays a message */
void
index_select (GtkWidget * widget, 
	      MAILSTREAM * stream,
	      glong mesgno)
{
  balsa_message_set (BALSA_MESSAGE (balsa_app.main_window->message_area),
		     stream, mesgno);
}

void 
index_next_message ()
{
  balsa_index_select_next (balsa_app.main_window->index);
}

void
index_previous_message ()
{
 balsa_index_select_previous (balsa_app.main_window->index);
}

void
index_delete_message ()
{
 balsa_delete_message (balsa_app.main_window->index);
}

void
index_undelete_message ()
{
 balsa_undelete_message (balsa_app.main_window->index);
}

/* remove the old mailbox menu, and create a new one
 * from the current postoffice  */
void
mailbox_menu_update ()
{
  GList *list;
  GtkWidget *menuitem;
  Mailbox *mailbox;

  /* get rid of the old... */
  gtk_option_menu_remove_menu
    (GTK_OPTION_MENU (balsa_app.main_window->mailbox_option_menu));

  if (balsa_app.main_window->mailbox_menu)
    gtk_widget_destroy (balsa_app.main_window->mailbox_menu);

  if (balsa_app.main_window->move_menu)
    gtk_widget_destroy (balsa_app.main_window->move_menu);

  balsa_app.main_window->mailbox_menu = gtk_menu_new ();
  balsa_app.main_window->move_menu = gtk_menu_new ();


  /* create the new menu */
  list = balsa_app.mailbox_list;
  while (list)
    {
      mailbox = list->data;
      list = list->next;

      menuitem = gtk_menu_item_new_with_label (mailbox->name);
      gtk_object_set_user_data (GTK_OBJECT (menuitem), mailbox);

      gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
			  (GtkSignalFunc) mailbox_menu_select,
			  menuitem);

      gtk_menu_append (GTK_MENU (balsa_app.main_window->mailbox_menu), menuitem);
      gtk_widget_show (menuitem);

      menuitem = gtk_menu_item_new_with_label (mailbox->name);
      gtk_menu_append (GTK_MENU (balsa_app.main_window->move_menu), menuitem);
      gtk_widget_show (menuitem);
    }

  gtk_option_menu_set_menu (GTK_OPTION_MENU (balsa_app.main_window->mailbox_option_menu),
			    balsa_app.main_window->mailbox_menu);
}

void
mailbox_menu_select (GtkWidget * menuitem)
{
  Mailbox *mailbox;

  mailbox = (Mailbox *) gtk_object_get_user_data (GTK_OBJECT (menuitem));
  mailbox_open (mailbox);
}
