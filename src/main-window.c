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
#include "main-window.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "sendmsg-window.h"
#include "index.h"
#include "mailbox.h"
#include "pref-manager.h"
#include "mailbox-manager.h"
#include "addrbook-manager.h"
#include "../config.h"


/* pixmaps for the toolbar */
#include "pixmaps/tb_mail_compose.xpm"

GtkWidget *bottom_pbar;

void show_about_box (GtkWidget *, gpointer);
GtkWidget *new_icon (gchar **, GtkWidget *);
static GtkWidget *create_toolbar (MainWindow *);
static GtkWidget *create_menu (MainWindow *);

extern void balsa_exit ();
static GtkWidget *menu_items[18];


MainWindow *
create_main_window ()
{
  MainWindow *mw;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *vpane;
  GtkWidget *progress_bar;


  mw = g_malloc (sizeof (MainWindow));


  /* structure initalizations */
  mw->mailbox_menu = NULL;
  mw->move_menu = NULL;


  /* main window */
  mw->window = gnome_app_new ("balsa", "Balsa");
  gtk_window_set_wmclass (GTK_WINDOW (mw->window), "balsa_app", "Balsa");
  gtk_widget_set_usize (mw->window, 660, 400);

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


  /* status bar */
  mw->status_bar = gtk_statusbar_new ();
  gtk_box_pack_start (GTK_BOX (vbox), mw->status_bar, FALSE, FALSE, 3);
  gtk_widget_show (mw->status_bar);

  progress_bar = gtk_progress_bar_new ();
  balsa_index_set_progress_bar (BALSA_INDEX (mw->index),
				GTK_PROGRESS_BAR (progress_bar));
  gtk_box_pack_start (GTK_BOX (mw->status_bar), progress_bar,
		      FALSE, FALSE, 0);
  gtk_widget_show (progress_bar);


  /* set the various parts of the GNOME APP up */
  gnome_app_set_contents (GNOME_APP (mw->window), vbox);
  mw->toolbar = create_toolbar (mw);
  gnome_app_set_menus (GNOME_APP (mw->window),
		       GTK_MENU_BAR (create_menu (mw)));
  gnome_app_set_toolbar (GNOME_APP (mw->window), GTK_TOOLBAR (mw->toolbar));



  gtk_widget_show (mw->window);
  return mw;
}



GtkWidget *
new_icon (gchar ** xpm, GtkWidget * window)
{
  GdkPixmap *pixmap;
  GtkWidget *pixmapwid;
  GdkBitmap *mask;

  pixmap = gdk_pixmap_create_from_xpm_d (window->window, &mask, 0, xpm);

  pixmapwid = gtk_pixmap_new (pixmap, mask);
  return pixmapwid;
}


static void
set_toolbar_icons (GtkWidget * widget,
		   GtkWidget * data)
{
  gtk_toolbar_set_style (GTK_TOOLBAR (data), GTK_TOOLBAR_ICONS);
}

static void
set_toolbar_text (GtkWidget * widget,
		  GtkWidget * data)
{
  gtk_toolbar_set_style (GTK_TOOLBAR (data), GTK_TOOLBAR_TEXT);
}

static void
set_toolbar_both (GtkWidget * widget,
		  GtkWidget * data)
{
  gtk_toolbar_set_style (GTK_TOOLBAR (data), GTK_TOOLBAR_BOTH);
}


static GtkWidget *
create_toolbar (MainWindow * mw)
{
  GtkWidget *window = mw->window;
  GtkWidget *toolbar;
  GtkWidget *toolbarbutton;
  GtkWidget *label;

  gtk_widget_realize (window);

  toolbar = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  label = gtk_label_new ("Mailbox:");
  gtk_toolbar_append_widget (GTK_TOOLBAR (toolbar), label, NULL, NULL);
  gtk_widget_show (label);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  mw->mailbox_option_menu = gtk_option_menu_new ();
  gtk_toolbar_append_widget (GTK_TOOLBAR (toolbar), mw->mailbox_option_menu, NULL, NULL);
  gtk_widget_show (mw->mailbox_option_menu);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Check",
			     "Check Email",
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_RCV),
			     GTK_SIGNAL_FUNC (current_mailbox_check),
			     "Check Email");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Delete",
			     "Delete Message",
			     NULL,
	       gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_TRASH),
			     GTK_SIGNAL_FUNC (index_delete_message),
			     mw);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Compose",
			     "Compose Message",
			     NULL,
			     new_icon (tb_mail_compose_xpm, window),
			     GTK_SIGNAL_FUNC (new_message),
			     "Compose Message");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Reply",
			     "Reply",
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_RPL),
			     GTK_SIGNAL_FUNC (replyto_message),
			     "Reply");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Forward",
			     "Forward",
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_FWD),
			     GTK_SIGNAL_FUNC (forward_message),
			     "Forward");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Previous",
			     "Open Previous Message",
			     NULL,
		gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_BACK),
			     GTK_SIGNAL_FUNC (index_previous_message),
			     "Open Previous Message");


  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Next",
			     "Open Next Message",
			     NULL,
	     gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_FORWARD),
			     GTK_SIGNAL_FUNC (index_next_message),
			     "Open Next Message");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));
/*
   toolbarbutton =
   gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
   "Addresses",
   "Address Book",
   NULL,
   new_icon (p14_xpm, window),
   GTK_SIGNAL_FUNC (addressbook_window_new),
   "Address Book");
   GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);

   gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));
 */

  gtk_widget_show (toolbar);
  return toolbar;
}


static GtkWidget *
create_menu (MainWindow * mw)
{
  GtkWidget *window = mw->window;
  GtkWidget *menubar, *w, *menu, *menu1;
  GtkAcceleratorTable *accel;
  int i = 0;

  accel = gtk_accelerator_table_new ();
  menubar = gtk_menu_bar_new ();
  gtk_widget_show (menubar);


  /* FILE Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RCV, _ ("Get New Mail"));
  gtk_menu_append (GTK_MENU (menu), w);
  gtk_widget_install_accelerator (w, accel, "activate", 'M', GDK_CONTROL_MASK);
  gtk_signal_connect_object (GTK_OBJECT (w),
			     "activate",
			     (GtkSignalFunc) current_mailbox_check,
			     GTK_OBJECT (window));
  gtk_widget_show (w);

  menu_items[i++] = w;

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
/*
   w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Save mailbox..."));
   gtk_widget_show (w);
   gtk_menu_append (GTK_MENU (menu), w);
   menu_items[i++] = w;

   w = gtk_menu_item_new ();
   gtk_widget_show (w);
   gtk_menu_append (GTK_MENU (menu), w);

   w = gnome_stock_menu_item (GNOME_STOCK_MENU_PRINT, _ ("Print..."));
   gtk_widget_show (w);
   gtk_menu_append (GTK_MENU (menu), w);
   menu_items[i++] = w;

   w = gtk_menu_item_new ();
   gtk_widget_show (w);
   gtk_menu_append (GTK_MENU (menu), w);
 */
  w = gnome_stock_menu_item (GNOME_STOCK_MENU_EXIT, _ ("Exit"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate", 'Q', GDK_CONTROL_MASK);
  gtk_signal_connect_object (GTK_OBJECT (w),
			     "activate",
			     (GtkSignalFunc) gtk_widget_destroy,
			     GTK_OBJECT (window));
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("File"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  /* MESSAGE Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL, _ ("New"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate", 'N', GDK_CONTROL_MASK);
  gtk_signal_connect_object (GTK_OBJECT (w),
			     "activate",
			     GTK_SIGNAL_FUNC (new_message),
			     NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RPL, _ ("Reply"));
  gtk_widget_show (w);
  gtk_widget_install_accelerator (w, accel, "activate", 'R', GDK_CONTROL_MASK);
  gtk_signal_connect_object (GTK_OBJECT (w),
			     "activate",
			     GTK_SIGNAL_FUNC (replyto_message),
			     NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;
/*
   w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Reply to All"));
   gtk_widget_show (w);
   gtk_menu_append (GTK_MENU (menu), w);
   menu_items[i++] = w;
 */
  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_FWD, _ ("Foward"));
  gtk_widget_show (w);
  gtk_signal_connect_object (GTK_OBJECT (w),
			     "activate",
			     GTK_SIGNAL_FUNC (forward_message),
			     NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;
/*
   w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Redirect"));
   gtk_widget_show (w);
   gtk_menu_append (GTK_MENU (menu), w);
   menu_items[i++] = w;

   w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Send again"));
   gtk_widget_show (w);
   gtk_menu_append (GTK_MENU (menu), w);
   menu_items[i++] = w;
 */
  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_TRASH, _ ("Delete"));
  gtk_widget_show (w);
  gtk_signal_connect_object (GTK_OBJECT (w),
			     "activate",
			     GTK_SIGNAL_FUNC (index_delete_message),
			     NULL);
  gtk_widget_install_accelerator (w, accel, "activate", 'D', GDK_CONTROL_MASK);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Message"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);


  /* Settings Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP, _ ("Preferences..."));
  gtk_widget_show (w);
  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) open_preferences_manager,
		      NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;


  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Mailbox Manager..."));
  gtk_widget_show (w);
  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) open_mailbox_manager,
		      NULL);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;


  w = gtk_menu_item_new_with_label (_ ("Settings"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  /* View Menu */

  menu = gtk_menu_new ();
  menu1 = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Toolbar"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu1);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Icons"));
  gtk_widget_show (w);
  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      GTK_SIGNAL_FUNC (set_toolbar_icons),
		      mw->toolbar);
  gtk_menu_append (GTK_MENU (menu1), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Text"));
  gtk_widget_show (w);
  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      GTK_SIGNAL_FUNC (set_toolbar_text),
		      mw->toolbar);
  gtk_menu_append (GTK_MENU (menu1), w);
  menu_items[i++] = w;

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Both"));
  gtk_widget_show (w);
  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      GTK_SIGNAL_FUNC (set_toolbar_both),
		      mw->toolbar);
  gtk_menu_append (GTK_MENU (menu1), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("View"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);


  /* HELP Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_ABOUT, _ ("About"));
  gtk_widget_show (w);
  gtk_signal_connect_object (GTK_OBJECT (w),
			     "activate",
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
/*  g_print ("%d menu items\n", i); */

  gtk_window_add_accelerator_table (GTK_WINDOW (window), accel);

  return menubar;
}


void
show_about_box (GtkWidget * widget, gpointer data)
{
  GtkWidget *about;
  gchar *authors[] =
  {
    "Jay Painter <jpaint@gimp.org>",
    "Stuart Parmenter <pavlov@pavlov.net>",
    NULL
  };

  about = gnome_about_new ("Balsa",
			   BALSA_VERSION,
			   "Copyright (C) 1997-98",
			   authors,
			   "Balsa is a E-Mail Client",
			   NULL);
  gtk_widget_show (about);
}
