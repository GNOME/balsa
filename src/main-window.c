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
#include <gdk/gdkkeysyms.h>

#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-message.h"
#include "filter.h"
#include "index-child.h"
#include "mailbox.h"
#include "misc.h"
#include "main.h"
#include "mailbox-manager.h"
#include "main-window.h"
#include "mblist-window.h"
#include "message-window.h"
#include "pref-manager.h"
#include "sendmsg-window.h"

#define MAILBOX_DATA "mailbox_data"

static GnomeMDI *mdi = NULL;

static gint about_box_visible = FALSE;

/* main window widget components */
static GtkMenuBar *create_menu (GnomeMDI *, GtkWidget * app);
static GtkToolbar *create_toolbar (GnomeMDI *, GtkWidget * app);


/* dialogs */
static void show_about_box (void);


/* callbacks */
static void check_new_messages_cb (GtkWidget *, gpointer data);
static void check_pop3_cb (GtkWidget * widget, gpointer data);

static void new_message_cb (GtkWidget * widget, gpointer data);
static void replyto_message_cb (GtkWidget * widget, gpointer data);
static void forward_message_cb (GtkWidget * widget, gpointer data);

static void next_message_cb (GtkWidget * widget, gpointer data);
static void previous_message_cb (GtkWidget * widget, gpointer data);

static void delete_message_cb (GtkWidget * widget, gpointer data);
static void undelete_message_cb (GtkWidget * widget, gpointer data);

static void filter_dlg_cb (GtkWidget * widget, gpointer data);

static void mblist_window_cb (GtkWidget * widget, gpointer data);
static void mailbox_close_child (GtkWidget * widget, gpointer data);

static void about_box_destroy_cb (void);

static void destroy_window_cb (GnomeMDI * mdi, gpointer data);


void
main_window_set_cursor (gint type)
{
  GdkCursor *cursor;

  if (type == -1)
    {
      gdk_window_set_cursor (GTK_WIDGET(mdi->active_window)->window, NULL);
      return;
    }

  cursor = gdk_cursor_new (type);
  gdk_window_set_cursor (GTK_WIDGET(mdi->active_window)->window, cursor);
  gdk_cursor_destroy (cursor);
}

static void
destroy_window_cb (GnomeMDI * mdi, gpointer data)
{
  gdk_window_get_size (GTK_WIDGET (mdi->active_window)->window,
		       &balsa_app.mw_width, &balsa_app.mw_height);
  balsa_exit ();
}

void
open_main_window (void)
{
  /* main window */
  mdi = GNOME_MDI (gnome_mdi_new ("balsa", "Balsa"));

  gtk_signal_connect (GTK_OBJECT (mdi),
		      "destroy",
		      (GtkSignalFunc) destroy_window_cb,
		      NULL);

  /* meubar and toolbar */
  gtk_signal_connect (GTK_OBJECT (mdi), "create_menus", GTK_SIGNAL_FUNC (create_menu), NULL);
  gtk_signal_connect (GTK_OBJECT (mdi), "create_toolbar", GTK_SIGNAL_FUNC (create_toolbar), NULL);
  gtk_signal_connect (GTK_OBJECT (mdi), "child_changed", GTK_SIGNAL_FUNC (index_child_changed), NULL);
  gnome_mdi_set_child_list_path (mdi, _ ("Mailboxes/<Separator>"));

  gnome_mdi_set_mode (mdi, balsa_app.mdi_style);

  gtk_window_set_policy (GTK_WINDOW (mdi->active_window), TRUE, TRUE, FALSE);
  gtk_widget_set_usize (GTK_WIDGET (mdi->active_window), balsa_app.mw_width, balsa_app.mw_height);

  refresh_main_window ();
}



/*
 * close the main window 
 */
void
close_main_window (void)
{
  if (gnome_mdi_remove_all (mdi, FALSE))
    gtk_object_destroy (GTK_OBJECT (mdi));
  mdi = NULL;
}



/*
 * refresh data in the main window
 */
void
refresh_main_window (void)
{
  /*
   * set the toolbar style
   */
  gtk_toolbar_set_style (GTK_TOOLBAR (GNOME_APP (mdi->active_window)->toolbar), balsa_app.toolbar_style);
  gnome_mdi_set_mode (mdi, balsa_app.mdi_style);
}

/*
 * the menubar for the main window
 */

static GtkMenuBar *
create_menu (GnomeMDI * mdi, GtkWidget * app)
{
  GtkWidget *menubar;
  GtkWidget *w;
  GtkWidget *menu;
  GtkAccelGroup *accel;

  accel = gtk_accel_group_get_default ();
  menubar = gtk_menu_bar_new ();

  /* FILE Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RCV, _ ("Get New Mail"));
  gtk_widget_add_accelerator (w, "activate", accel, GDK_Tab, 0, GTK_ACCEL_VISIBLE);
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) check_new_messages_cb, NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gtk_menu_item_new ();
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RCV, _ ("Check POP3"));
  gtk_menu_append (GTK_MENU (menu), w);
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) check_pop3_cb, NULL);

  w = gtk_menu_item_new ();
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);


  w = gnome_stock_menu_item (GNOME_STOCK_MENU_EXIT, _ ("Exit"));
  gtk_widget_add_accelerator (w, "activate", accel, 'Q', 0, GTK_ACCEL_VISIBLE);
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) close_main_window, NULL);
  gtk_menu_append (GTK_MENU (menu), w);


  w = gtk_menu_item_new_with_label (_ ("File"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);



  /* MESSAGE Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL, _ ("New"));
  gtk_widget_add_accelerator (w, "activate", accel, 'M', 0, GTK_ACCEL_VISIBLE);
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) new_message_cb, NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RPL, _ ("Reply"));
  gtk_widget_add_accelerator (w, "activate", accel, 'R', 0, GTK_ACCEL_VISIBLE);
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) replyto_message_cb, NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_FWD, _ ("Forward"));
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) forward_message_cb, NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gtk_menu_item_new ();
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_TRASH, _ ("Delete"));
  gtk_widget_add_accelerator (w, "activate", accel, 'D', 0, GTK_ACCEL_VISIBLE);
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) delete_message_cb, NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_UNDELETE, _ ("Undelete"));
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) undelete_message_cb, NULL);
  gtk_widget_add_accelerator (w, "activate", accel, 'U', 0, GTK_ACCEL_VISIBLE);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gtk_menu_item_new_with_label (_ ("Message"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);



/* Mailbox list */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP, _ ("Mailbox List"));
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) mblist_window_cb, NULL);
  gtk_widget_add_accelerator (w, "activate", accel, 'C', 0, GTK_ACCEL_VISIBLE);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_CLOSE, _ ("Close"));
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) mailbox_close_child, NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gtk_menu_item_new ();
  gtk_menu_append (GTK_MENU (menu), w);

  w = gtk_menu_item_new_with_label (_ ("Mailboxes"));
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);


/* Settings Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP, _ ("Filters..."));
  gtk_signal_connect (GTK_OBJECT (w), "activate", GTK_SIGNAL_FUNC (filter_dlg_cb), NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_PROP, _ ("Preferences..."));
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) open_preferences_manager, NULL);
  gtk_menu_append (GTK_MENU (menu), w);

  w = gtk_menu_item_new_with_label (_ ("Settings"));
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);


/* Help Menu */
  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_ABOUT, _ ("About"));
  gtk_signal_connect (GTK_OBJECT (w), "activate", (GtkSignalFunc) show_about_box, NULL);
  gtk_menu_append (GTK_MENU (menu), w);


  w = gtk_menu_item_new_with_label (_ ("Help"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  gtk_window_add_accel_group (GTK_WINDOW (app), accel);

  gtk_widget_show_all (menubar);

  return GTK_MENU_BAR (menubar);
}




/*
 * the toolbar for the main window
 */
static GtkToolbar *
create_toolbar (GnomeMDI * mdi, GtkWidget * app)
{
  GtkWidget *window;
  GtkWidget *toolbar;
  GtkWidget *toolbarbutton;

  toolbar = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);

  window = app;

  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     _ ("Check"),
			     _ ("Check Email"),
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_RCV),
			     GTK_SIGNAL_FUNC (check_new_messages_cb),
			     NULL);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     _ ("Delete"),
			     _ ("Delete Message"),
			     NULL,
	       gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_TRASH),
			     (GtkSignalFunc) delete_message_cb,
			     NULL);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     _ ("Compose"),
			     _ ("Compose Message"),
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_NEW),
			     (GtkSignalFunc) new_message_cb,
			     NULL);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     _ ("Reply"),
			     _ ("Reply"),
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_RPL),
			     (GtkSignalFunc) replyto_message_cb,
			     NULL);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     _ ("Forward"),
			     _ ("Forward"),
			     NULL,
	    gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_MAIL_FWD),
			     (GtkSignalFunc) forward_message_cb,
			     NULL);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     _ ("Previous"),
			     _ ("Open Previous Message"),
			     NULL,
		gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_BACK),
			     (GtkSignalFunc) previous_message_cb,
			     NULL);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);


  toolbarbutton =
    gtk_toolbar_append_item (GTK_TOOLBAR (toolbar),
			     _ ("Next"),
			     _ ("Open Next Message"),
			     NULL,
	     gnome_stock_pixmap_widget (window, GNOME_STOCK_PIXMAP_FORWARD),
			     (GtkSignalFunc) next_message_cb,
			     NULL);
  GTK_WIDGET_UNSET_FLAGS (toolbarbutton, GTK_CAN_FOCUS);
/*
   gtk_toolbar_set_button_relief(GTK_TOOLBAR(toolbar), GTK_RELIEF_NONE);
 */
  return GTK_TOOLBAR (toolbar);
}


/*
 * show the about box for Balsa
 */
static void
show_about_box (void)
{
  GtkWidget *about;
  const gchar *authors[] =
  {
    "Stuart Parmenter <pavlov@pavlov.net>",
    "Jay Painter <jpaint@gimp.org>",
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
			   _ ("The Balsa email client is part of the GNOME desktop environment.  Information on Balsa can be found at http://www.balsa.net/\n\nIf you need to report bugs, please do so at: http://www.gnome.org/cgi-bin/bugs"),
			   "balsa_logo.png");

  gtk_signal_connect (GTK_OBJECT (about),
		      "destroy",
		      (GtkSignalFunc) about_box_destroy_cb,
		      NULL);

  gtk_widget_show (about);
}


/*
 * Callbacks
 */

static void
check_new_messages_cb (GtkWidget * widget, gpointer data)
{
  if (!balsa_app.current_index_child)
    return;

  mailbox_check_new_messages (BALSA_INDEX (balsa_app.current_index_child->index)->mailbox);
}

static void
check_pop3_cb (GtkWidget * widget, gpointer data)
{
  check_all_pop3_hosts (balsa_app.inbox);
}

static void
new_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  sendmsg_window_new (widget, NULL, 0);
}


static void
replyto_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  sendmsg_window_new (widget, BALSA_INDEX (balsa_app.current_index_child->index), 1);
}


static void
forward_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  sendmsg_window_new (widget, BALSA_INDEX (balsa_app.current_index_child->index), 2);
}


static void
next_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  balsa_index_select_next (BALSA_INDEX (balsa_app.current_index_child->index));
}


static void
previous_message_cb (GtkWidget * widget, gpointer data)
{
  g_return_if_fail (widget != NULL);

  if (!balsa_app.current_index_child)
    return;

  balsa_index_select_previous (BALSA_INDEX (balsa_app.current_index_child->index));
}


static void
delete_message_cb (GtkWidget * widget, gpointer data)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  if (!balsa_app.current_index_child)
    return;

  clist = GTK_CLIST (GTK_BIN (balsa_app.current_index_child->index)->child);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, (gint) list->data);
      message_delete (message);
      list = list->next;
    }

  balsa_index_select_next (BALSA_INDEX (balsa_app.current_index_child->index));
}


static void
undelete_message_cb (GtkWidget * widget, gpointer data)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  if (!balsa_app.current_index_child)
    return;

  clist = GTK_CLIST (GTK_BIN (balsa_app.current_index_child->index)->child);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, (gint) list->data);
      message_undelete (message);
      list = list->next;
    }
  balsa_index_select_next (BALSA_INDEX (balsa_app.current_index_child->index));
}

static void
filter_dlg_cb (GtkWidget * widget, gpointer data)
{
  filter_edit_dialog (NULL);
}

static void
mblist_window_cb (GtkWidget * widget, gpointer data)
{
  mblist_open_window (mdi);
}

static void
mailbox_close_child (GtkWidget * widget, gpointer data)
{
  if (balsa_app.current_index_child)
    gnome_mdi_remove_child (mdi, GNOME_MDI_CHILD (balsa_app.current_index_child),
			    TRUE);
}

static void
about_box_destroy_cb (void)
{
  about_box_visible = FALSE;
}
