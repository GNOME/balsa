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

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gnome.h>
#include "addrbook-window.h"

gint delete_event (GtkWidget *, gpointer);

static GtkWidget *menu_items[9];

extern void close_window (GtkWidget *, gpointer);

static GtkWidget *
create_menu (GtkWidget * window)
{
  GtkWidget *menubar, *w, *menu;
  GtkAcceleratorTable *accel;
  int i = 0;

  accel = gtk_accelerator_table_new ();
  menubar = gtk_menu_bar_new ();
  gtk_widget_show (menubar);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Close"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
                             GTK_SIGNAL_FUNC (close_window),
                             GTK_OBJECT(window));
  menu_items[i++] = w;
  
  w = gtk_menu_item_new_with_label (_ ("File"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_ABOUT, _ ("Contents"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;
  
  w = gtk_menu_item_new_with_label (_ ("Help"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);   
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);
  
  menu_items[i] = NULL;

/*  g_print("%d menu items\n", i); */

  gtk_window_add_accelerator_table (GTK_WINDOW (window), accel);
  return menubar;
}


void addressbook_window_new(GtkWidget *widget, gpointer data)
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *vbox1;
  GtkWidget *hbox1;
  GtkWidget *label;
  GtkWidget *email_list;
  static char *titles[] =
  {
    "Email address"
  };

  window = gnome_app_new ("balsa_addressbook_window","Addressbook");
  gtk_window_set_wmclass (GTK_WINDOW (window), "balsa_app",
			  "Balsa");

  gtk_signal_connect (GTK_OBJECT (window), "destroy",
		      GTK_SIGNAL_FUNC(delete_event), NULL);

  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event), NULL);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 2);
  gtk_widget_show (hbox);

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), vbox1, TRUE, TRUE, 2);
  gtk_widget_show (vbox1);

  hbox1 = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox1), hbox1, TRUE, TRUE, 2);
  gtk_widget_show (hbox1);

  label=gtk_label_new("Name:");
  gtk_box_pack_start (GTK_BOX (hbox1), label, TRUE, TRUE, 10);
  gtk_widget_show(label);

  email_list = gtk_clist_new_with_titles (1, titles);
  gtk_clist_column_titles_passive (GTK_CLIST (email_list));
  gtk_clist_set_selection_mode (GTK_CLIST (email_list), GTK_SELECTION_BROWSE);
  
  gtk_clist_set_column_width (GTK_CLIST (email_list), 0, 200);
  
  gtk_clist_set_policy (GTK_CLIST (email_list),
                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (vbox1), email_list, TRUE, TRUE, 10);
  gtk_widget_show (email_list);
  



  gnome_app_set_contents (GNOME_APP (window), vbox);

  gnome_app_set_menus (GNOME_APP (window),
		       GTK_MENU_BAR (create_menu (window)));

  gtk_widget_show (window);
}

