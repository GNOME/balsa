/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <string.h>
#include "main-window.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "sendmsg-window.h"
#include "index.h"
#include "mailbox.h"
#include "options.h"
#include "../config.h"


void show_about_box (GtkWidget * widget, gpointer data);

/* from main.c -- exit balsa */
extern void balsa_exit ();

static GtkWidget *menu_items[18];

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

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Get New Mail"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'M', GDK_CONTROL_MASK);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
			     (GtkSignalFunc) current_mailbox_check,
			     GTK_OBJECT (window));
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("New mailbox..."));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Save mailbox..."));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Print..."));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_EXIT, _ ("Exit"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'Q', GDK_CONTROL_MASK);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
			     (GtkSignalFunc) gtk_widget_destroy,
			     GTK_OBJECT (window));
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("File"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_CUT, _ ("Cut"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'X', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_COPY, _ ("Copy"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'C', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_PASTE, _ ("Paste"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'V', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Edit"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("New"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'N', GDK_CONTROL_MASK);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
			     (GtkSignalFunc) sendmsg_window_new,
			     NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Reply"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'R', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Reply to All"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Foward"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Redirect"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Send again"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Delete"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate",
				  'D', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Message"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Account manager"));
  gtk_widget_show (w);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
			     (GtkSignalFunc) personality_box,
			     NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Settings"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Tools"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_ABOUT, _ ("About"));
  gtk_widget_show (w);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
			     (GtkSignalFunc) show_about_box,
			     NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Help"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu_items[i] = NULL;
/*      g_print("%d menu items\n", i); */
  gtk_window_add_accelerator_table (GTK_WINDOW (window), accel);
  return menubar;
}


MainWindow *
create_main_window ()
{
  MainWindow *mw;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *vpane;

  mw = g_malloc (sizeof (MainWindow));

  /* structure initalizations */
  mw->mailbox_menu = NULL;
  mw->move_menu = NULL;

  /* main window */
  mw->window = gnome_app_new ("balsa", "Balsa");
  gtk_window_set_wmclass (GTK_WINDOW (mw->window), "balsa_app",
			  "Balsa");

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "destroy",
		      (GtkSignalFunc) balsa_exit,
		      &mw->window);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "delete_event",
		      (GtkSignalFunc) balsa_exit,
		      &mw->window);


  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);


  /* toolbar */
  mw->toolbar = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_BOTH);
  gtk_box_pack_start (GTK_BOX (vbox), mw->toolbar, FALSE, TRUE, 2);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  label = gtk_label_new ("Mailbox:");
  gtk_toolbar_append_widget (GTK_TOOLBAR (mw->toolbar), label, NULL, NULL);
  gtk_widget_show (label);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  mw->mailbox_option_menu = gtk_option_menu_new ();
  gtk_toolbar_append_widget (GTK_TOOLBAR (mw->toolbar), mw->mailbox_option_menu, NULL, NULL);
  gtk_widget_show (mw->mailbox_option_menu);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Check Mail",
			   NULL, NULL, NULL, current_mailbox_check, NULL);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Compose",
			   NULL, NULL, NULL, NULL, NULL);
  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Reply",
			   NULL, NULL, NULL, NULL, NULL);
  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Delete",
			   NULL, NULL, NULL, NULL, NULL);

  gtk_widget_show (mw->toolbar);


  /* panned widget */
  vpane = gtk_vpaned_new ();
  gtk_box_pack_start (GTK_BOX (vbox), vpane, TRUE, TRUE, 0);
  gtk_widget_show (vpane);


  /* message index */
  hbox = gtk_hbox_new (TRUE, 0);
  mw->index = balsa_index_new ();
  gtk_container_border_width (GTK_CONTAINER (mw->index), 2);
  gtk_box_pack_start (GTK_BOX (hbox), mw->index, TRUE, TRUE, 2);

  gtk_signal_connect (GTK_OBJECT (mw->index),
		      "select_message",
		      (GtkSignalFunc) index_select,
		      NULL);

  gtk_paned_add1 (GTK_PANED (vpane), hbox);
  gtk_widget_show (hbox);
  gtk_widget_show (mw->index);


  /* message body */
  hbox = gtk_hbox_new (FALSE, 0);

  mw->message_area = balsa_message_new ();
  gtk_container_border_width (GTK_CONTAINER (mw->message_area), 1);
  gtk_box_pack_start (GTK_BOX (hbox), mw->message_area, TRUE, TRUE, 2);
  gtk_widget_show (mw->message_area);

  gtk_paned_add2 (GTK_PANED (vpane), hbox);
  gtk_widget_show (hbox);

  gnome_app_set_contents (GNOME_APP (mw->window), vbox);

  gnome_app_set_menus (GNOME_APP (mw->window),
		       GTK_MENU_BAR (create_menu (mw->window)));
/*
   gnome_app_set_toolbar(GNOME_APP(mw->window),
   GTK_TOOLBAR(create_toolbar(mw->window)));

 */
  gtk_widget_show (mw->window);
  return mw;
}

void
show_about_box (GtkWidget * widget, gpointer data)
{
  gchar *authors[] =
  {
    "Jay Painter <jpaint@gimp.org>",
    "Stuart Parmenter <pavlov@pavlov.net>",
    NULL
  };

  GtkWidget *about = gnome_about_new ("Balsa",
				      BALSA_VERSION,
				      "Copyright (C) 1997-98",
				      authors,
				      "The flimsey e-mail client!",
				      "balsa.xpm");
  gtk_widget_show (about);
}
