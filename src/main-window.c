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
#include "config.h"

#include <string.h>
#include <gnome.h>

#include "addrbook-manager.h"
#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-message.h"
#include "index-window.h"
#include "mailbox.h"
#include "mailbox-manager.h"
#include "main-window.h"
#include "mblist-window.h"
#include "message-window.h"
#include "misc.h"
#include "pref-manager.h"
#include "sendmsg-window.h"


#define MAILBOX_DATA "mailbox_data"


typedef struct _MainWindow MainWindow;
struct _MainWindow
  {
    GtkWidget *window;
    GtkWidget *menubar;
    GtkWidget *toolbar;
    GtkWidget *mailbox_option_menu;
    GtkWidget *mailbox_menu;
    GtkWidget *move_menu;
    GtkWidget *index;
    GtkWidget *message_area;
    GtkWidget *status_bar;

    /* non-widgets */
    Mailbox *mailbox;
    guint watcher_id;
  };
static MainWindow *mw = NULL;

static gint about_box_visible = FALSE;



/* external decs */
extern void balsa_exit ();


/* main window widget components */
static void create_menu ();
static void create_toolbar ();


/* dialogs */
static void show_about_box ();


/* callbacks */
static void move_resize_cb ();

static void index_select_cb (GtkWidget * widget, Message * message);
static void next_message_cb (GtkWidget * widget);
static void previous_message_cb (GtkWidget * widget);

static void new_message_cb (GtkWidget * widget);
static void replyto_message_cb (GtkWidget * widget);
static void forward_message_cb (GtkWidget * widget);

static void delete_message_cb (GtkWidget * widget);
static void undelete_message_cb (GtkWidget * widget);

static void mailbox_select_cb (GtkWidget * widget);

static void mblist_window_cb (GtkWidget * widget);

static void about_box_destroy_cb ();

static void mailbox_listener (MailboxWatcherMessage * mw_message);


void
open_main_window ()
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *vpane;
  GtkWidget *progress_bar;


  if (mw)
    return;

  mw = g_malloc (sizeof (MainWindow));

  /* structure initalizations */
  mw->mailbox_menu = NULL;
  mw->move_menu = NULL;
  mw->mailbox = NULL;


  /* main window */
  mw->window = gnome_app_new ("balsa", "Balsa");
  gtk_window_set_wmclass (GTK_WINDOW (mw->window), "balsa_app", "Balsa");

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "destroy",
		      (GtkSignalFunc) balsa_exit,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "move_resize",
		      (GtkSignalFunc) move_resize_cb,
		      NULL);



  /* meubar and toolbar */
  create_menu ();
  create_toolbar ();


  /* contents widget */
  vbox = gtk_vbox_new (FALSE, 0);

  gtk_widget_show (vbox);



  /* panned widget */
  vpane = gtk_vpaned_new ();
  gtk_box_pack_start (GTK_BOX (vbox), vpane, TRUE, TRUE, 0);
  gtk_widget_show (vpane);



  /* message index */
  hbox = gtk_hbox_new (TRUE, 0);
  mw->index = balsa_index_new ();

  if (gdk_screen_width () > 640 && gdk_screen_height () > 480)
    gtk_widget_set_usize (mw->index, 1, 200);
  else
    gtk_widget_set_usize (mw->index, 1, 133);

  gtk_container_border_width (GTK_CONTAINER (mw->index), 2);
  gtk_box_pack_start (GTK_BOX (hbox), mw->index, TRUE, TRUE, 2);

  gtk_object_set_user_data (GTK_OBJECT (mw->index), (gpointer) mw);

  gtk_signal_connect (GTK_OBJECT (mw->index),
		      "select_message",
		      (GtkSignalFunc) index_select_cb,
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
  gtk_box_pack_start (GTK_BOX (mw->status_bar), progress_bar, FALSE, FALSE, 0);
  gtk_widget_show (progress_bar);



  refresh_main_window ();
  gtk_widget_show (mw->window);
}



/*
 * close the main window 
 */
void
close_main_window ()
{
  if (!mw)
    return;

  gtk_widget_destroy (mw->window);
  g_free (mw);
  mw = NULL;
}



/*
 * refresh data in the main window
 */
void
refresh_main_window ()
{
  GList *list;
  GtkWidget *menuitem;
  Mailbox *mailbox;

  /*
   * set the toolbar style
   */
  gtk_toolbar_set_style (GTK_TOOLBAR (mw->toolbar), balsa_app.toolbar_style);
}

/*
 * the menubar for the main window
 */
static void
create_menu ()
{
  gint i = 0;
  GtkWidget *menubar;
  GtkWidget *w;
  GtkWidget *menu;
  GtkWidget *menu1;
  GtkWidget *menu_items[14];
  GtkAccelGroup *accel;


  accel = gtk_accel_group_new ();
  menubar = gtk_menu_bar_new ();
  gnome_app_set_menus (GNOME_APP (mw->window), GTK_MENU_BAR (menubar));
  gtk_widget_show (menubar);


  /* FILE Menu */
  menu = gtk_menu_new ();
#if 0
  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RCV, _ ("Get New Mail"));
  gtk_menu_append (GTK_MENU (menu), w);
  gtk_widget_add_accelerator (w, "activate", accel, 'M', GDK_CONTROL_MASK, 0);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) check_new_messages_cb,
		      NULL);
  gtk_object_set_user_data (GTK_OBJECT (w), mw);
  gtk_widget_show (w);


  menu_items[i++] = w;
  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
#endif

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_EXIT, _ ("Exit"));
  gtk_widget_show (w);
  gtk_widget_add_accelerator (w, "activate", accel, 'Q', GDK_CONTROL_MASK, 0);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) close_main_window,
		      NULL);

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
  gtk_widget_add_accelerator (w, "activate", accel, 'N', GDK_CONTROL_MASK, 0);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) new_message_cb,
		      NULL);

  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;


  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RPL, _ ("Reply"));
  gtk_widget_show (w);
  gtk_widget_add_accelerator (w, "activate", accel, 'R', GDK_CONTROL_MASK, 0);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) replyto_message_cb,
		      NULL);

  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;


  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_FWD, _ ("Foward"));
  gtk_widget_show (w);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) forward_message_cb,
		      NULL);

  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;


  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_TRASH, _ ("Delete"));
  gtk_widget_show (w);

  gtk_object_set_user_data (GTK_OBJECT (w), (gpointer) mw);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) delete_message_cb,
		      NULL);

  gtk_widget_add_accelerator (w, "activate", accel, 'D', GDK_CONTROL_MASK, 0);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;



  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Undelete"));
  gtk_widget_show (w);

  gtk_object_set_user_data (GTK_OBJECT (w), (gpointer) mw);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) undelete_message_cb,
		      NULL);

  /* gtk_widget_install_accelerator (w, accel, "activate", 'D', GDK_CONTROL_MASK); */
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;


  w = gtk_menu_item_new_with_label (_ ("Message"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);


  /* Settings Menu */
  menu = gtk_menu_new ();


  w = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP, _ ("Mailbox List"));
  gtk_widget_show (w);

  gtk_signal_connect (GTK_OBJECT (w),
		      "activate",
		      (GtkSignalFunc) mblist_window_cb,
		      NULL);

  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

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


  /* HELP Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_ABOUT, _ ("About"));
  gtk_widget_show (w);

  gtk_signal_connect (GTK_OBJECT (w),
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
  gtk_window_add_accel_group (GTK_WINDOW (mw->window), accel);
}




/*
 * the toolbar for the main window
 */
static void
create_toolbar ()
{
  GtkWidget *window = mw->window;
  GtkWidget *toolbar;
  GtkWidget *toolbarbutton;
  GtkWidget *label;


  gtk_widget_realize (mw->window);

  mw->toolbar = toolbar = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
  gnome_app_set_toolbar (GNOME_APP (mw->window), GTK_TOOLBAR (toolbar));
  gtk_widget_show (toolbar);
#if 0
  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Check",
			     "Check Email",
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_RCV),
			     (GtkSignalFunc) check_new_messages_cb,
			     "Check Email");
  gtk_object_set_user_data (GTK_OBJECT (toolbarbutton), mw);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));

#endif
  
  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Delete",
			     "Delete Message",
			     NULL,
	       gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_TRASH),
			     (GtkSignalFunc) delete_message_cb,
			     NULL);
  gtk_object_set_user_data (GTK_OBJECT (toolbarbutton), (gpointer) mw);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Compose",
			     "Compose Message",
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_NEW),
			     (GtkSignalFunc) new_message_cb,
			     "Compose Message");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Reply",
			     "Reply",
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_RPL),
			     (GtkSignalFunc) replyto_message_cb,
			     "Reply");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Forward",
			     "Forward",
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_FWD),
			     (GtkSignalFunc) forward_message_cb,
			     "Forward");
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Previous",
			     "Open Previous Message",
			     NULL,
		gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_BACK),
			     (GtkSignalFunc) previous_message_cb,
			     "Open Previous Message");
  gtk_object_set_user_data (GTK_OBJECT (toolbarbutton), (gpointer) mw);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     "Next",
			     "Open Next Message",
			     NULL,
	     gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_FORWARD),
			     (GtkSignalFunc) next_message_cb,
			     "Open Next Message");
  gtk_object_set_user_data (GTK_OBJECT (toolbarbutton), (gpointer) mw);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);
}



/*
 * show the about box for Balsa
 */
static void
show_about_box ()
{
  GtkWidget *about;
  gchar *authors[] =
  {
    "Jay Painter <jpaint@gimp.org>",
    "Stuart Parmenter <pavlov@pavlov.net>",
    NULL
  };


  /* only show one about box at a time */
  if (about_box_visible)
    return;
  else
    about_box_visible = TRUE;


  about = gnome_about_new ("Balsa",
			   BALSA_VERSION,
			   "Copyright (C) 1997-98",
			   authors,
			   "Balsa is a E-Mail Client",
			   NULL);

  gtk_signal_connect (GTK_OBJECT (about),
		      "destroy",
		      (GtkSignalFunc) about_box_destroy_cb,
		      NULL);

  gtk_widget_show (about);
}


/*
 * callbacks
 */
static void
move_resize_cb ()
{
  gdk_window_get_geometry (mw->window->window,
			   NULL,
			   NULL,
			   &balsa_app.mw_width,
			   &balsa_app.mw_height,
			   NULL);
}

static void
index_select_cb (GtkWidget * widget,
		 Message * message)
{
  MainWindow *mainwindow;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (message != NULL);

  mainwindow = (MainWindow *) gtk_object_get_user_data (GTK_OBJECT (widget));
  balsa_message_set (BALSA_MESSAGE (mainwindow->message_area), message);
}


static void
new_message_cb (GtkWidget * widget)
{
  g_return_if_fail (widget != NULL);

  sendmsg_window_new (widget, NULL, 0);
}


static void
replyto_message_cb (GtkWidget * widget)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index)
    return;
  sendmsg_window_new (widget, BALSA_INDEX (balsa_app.current_index), 1);
}


static void
forward_message_cb (GtkWidget * widget)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index)
    return;
  sendmsg_window_new (widget, BALSA_INDEX (balsa_app.current_index), 2);
}


static void
next_message_cb (GtkWidget * widget)
{
  MainWindow *mainwindow;

  g_return_if_fail (widget != NULL);

  mainwindow = (MainWindow *) gtk_object_get_user_data (GTK_OBJECT (widget));
  balsa_index_select_next (BALSA_INDEX (mainwindow->index));
}


static void
previous_message_cb (GtkWidget * widget)
{
  MainWindow *mainwindow;

  g_return_if_fail (widget != NULL);

  mainwindow = (MainWindow *) gtk_object_get_user_data (GTK_OBJECT (widget));
  balsa_index_select_previous (BALSA_INDEX (mainwindow->index));
}


static void
delete_message_cb (GtkWidget * widget)
{
  GList *list;
  MainWindow *mainwindow;

  g_return_if_fail (widget != NULL);

  mainwindow = (MainWindow *) gtk_object_get_user_data (GTK_OBJECT (widget));

  list = BALSA_INDEX (mainwindow->index)->selection;
  while (list)
    {
      message_delete ((Message *) list->data);
      list = list->next;
    }

  balsa_index_select_next (BALSA_INDEX (mainwindow->index));
}


static void
undelete_message_cb (GtkWidget * widget)
{
  GList *list;
  MainWindow *mainwindow;

  g_return_if_fail (widget != NULL);

  mainwindow = (MainWindow *) gtk_object_get_user_data (GTK_OBJECT (widget));
  balsa_index_select_next (BALSA_INDEX (mainwindow->index));
}


static void
mblist_window_cb (GtkWidget * widget)
{
  GList *list;
  Mailbox *mailbox;

  mblist_open_window ();

  list = balsa_app.mailbox_list;
  while (list)
    {
      mailbox = list->data;
      list = list->next;

      mblist_add_mailbox (mailbox);
    }
}

static void
mailbox_select_cb (GtkWidget * widget)
{
  MainWindow *mainwindow;
  Mailbox *mailbox;
  GtkWidget *messagebox;
  guint watcher_id;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_MENU_ITEM (widget));

  mainwindow = (MainWindow *) gtk_object_get_user_data (GTK_OBJECT (widget));
  mailbox = (Mailbox *) gtk_object_get_data (GTK_OBJECT (widget), MAILBOX_DATA);


  /* 
   * bail now if the we've been called without a valid mailbox
   */
  if (!mailbox)
    return;

  create_new_index (mailbox);
}


static void
about_box_destroy_cb ()
{
  about_box_visible = FALSE;
}


static void
mailbox_listener (MailboxWatcherMessage * mw_message)
{
  MainWindow *mainwindow = (MainWindow *) mw_message->data;

  switch (mw_message->type)
    {
    default:
      break;
    }
}
