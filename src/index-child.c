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
#include <gnome.h>
#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-message.h"
#include "main-window.h"
#include "message-window.h"
#include "misc.h"
#include "index-child.h"

static GnomeMDIChildClass *parent_class = NULL;

static void index_child_class_init (IndexChildClass *);
static void index_child_init (IndexChild *);

static GList *child_list = NULL;

guint
index_child_get_type ()
{
  static guint index_type = 0;

  if (!index_type)
    {

      GtkTypeInfo index_info =
      {
	"IndexChild",
	sizeof (IndexChild),
	sizeof (IndexChildClass),
	(GtkClassInitFunc) index_child_class_init,
	(GtkObjectInitFunc) index_child_init,
	(GtkArgSetFunc) NULL,
	(GtkArgGetFunc) NULL,
      };

      index_type = gtk_type_unique (gnome_mdi_child_get_type (), &index_info);
    }
  return index_type;
}

/* callbacks */
static void refresh_index_window (IndexChild * iw);
static void mailbox_listener (MailboxWatcherMessage * iw_message);
static void index_select_cb (GtkWidget * widget, Message * message, GdkEventButton *);

static void set_index_window_data (GtkObject * object, IndexChild * iw);
static IndexChild *get_index_window_data (GtkObject * object);

static GtkWidget *create_menu (BalsaIndex * bindex, Message * message);

/* index callbacks */
static void next_message_cb (GtkWidget * widget);
static void previous_message_cb (GtkWidget * widget);

/* menu item callbacks */
static void message_status_set_new_cb (GtkWidget *, Message *);
static void message_status_set_read_cb (GtkWidget *, Message *);
static void message_status_set_answered_cb (GtkWidget *, Message *);
static void delete_message_cb (GtkWidget *, Message *);
static void undelete_message_cb (GtkWidget *, Message *);

void 
index_child_changed (GnomeMDI * mdi, GnomeMDIChild * gmdic)
{
  IndexChild *child;
  GList *list;

  if (!mdi || !child_list || !gmdic)
    return;

  list = g_list_first (child_list);

  for (; list != NULL; list = list->next)
    {
      child = (IndexChild *) list->data;
      if (child)
	if (!strcmp (child->mailbox->name, gmdic->name))
	  {
	    balsa_app.current_index = child->index;
	    return;
	  }
    }
  balsa_app.current_index = NULL;
}

IndexChild *
index_child_get_active (GnomeMDI * mdi)
{
  IndexChild *child;
  GnomeMDIChild *gmdic;
  GList *list;

  if (!mdi || !child_list)
    return NULL;

  gmdic = gnome_mdi_active_child (mdi);

  list = g_list_first (child_list);

  for (; list != NULL; list = list->next)
    {
      child = (IndexChild *) list->data;
      if (child)
	if (!strcmp (child->mailbox->name, gmdic->name))
	  return child;
    }

}

IndexChild *
index_child_new (Mailbox * mailbox)
{
  IndexChild *child;

  if (child = gtk_type_new (index_child_get_type ()))
    {
      child->mailbox = mailbox;

      GNOME_MDI_CHILD (child)->name = g_strdup (mailbox->name);
      child_list = g_list_append (child_list, child);
    }

  return child;
}

static GtkWidget *
index_child_create_view (GnomeMDIChild * child)
{
  GList *list;
  GtkWidget *messagebox;
  GtkWidget *vbox;
  IndexChild *iw;

  iw = INDEX_CHILD (child);

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_widget_show (vbox);

  iw->index = balsa_index_new ();
  gtk_box_pack_start (GTK_BOX (vbox), iw->index, TRUE, TRUE, 0);
  gtk_widget_show (iw->index);

  balsa_index_set_mailbox (BALSA_INDEX (iw->index), iw->mailbox);

  gtk_signal_connect (GTK_OBJECT (iw->index),
		      "select_message",
		      (GtkSignalFunc) index_select_cb,
		      NULL);

  iw->watcher_id = mailbox_watcher_set (iw->mailbox,
				      (MailboxWatcherFunc) mailbox_listener,
					MESSAGE_NEW_MASK,
					(gpointer) iw);

  if (!mailbox_open_ref (iw->mailbox))
    {
      mailbox_watcher_remove (iw->mailbox, iw->watcher_id);

      messagebox = gnome_message_box_new ("Unable to Open Mailbox!",
					  GNOME_MESSAGE_BOX_ERROR,
					  GNOME_STOCK_BUTTON_OK,
					  NULL);
      gtk_widget_set_usize (messagebox, MESSAGEBOX_WIDTH, MESSAGEBOX_HEIGHT);
      gtk_window_position (GTK_WINDOW (messagebox), GTK_WIN_POS_CENTER);
      gtk_widget_show (messagebox);
    }

  return (vbox);
}

/*
 * set/get data convience functions used for attaching the
 * IndexChild structure to GTK objects so it can be retrieved
 * in callbacks
 */
static void
set_index_window_data (GtkObject * object, IndexChild * iw)
{
  gtk_object_set_data (object, "index_window_data", (gpointer) iw);
}


static IndexChild *
get_index_window_data (GtkObject * object)
{
  return gtk_object_get_data (object, "index_window_data");
}


static void
mailbox_listener (MailboxWatcherMessage * iw_message)
{
  IndexChild *iw = (IndexChild *) iw_message->data;

  switch (iw_message->type)
    {
    case MESSAGE_NEW:
      balsa_index_add (BALSA_INDEX (iw->index), iw_message->message);
      break;

    default:
      break;
    }
}


static void
index_select_cb (GtkWidget * widget,
		 Message * message,
		 GdkEventButton * bevent)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (message != NULL);
printf("selected\n");
  if (bevent && bevent->button == 1 && bevent->type == GDK_2BUTTON_PRESS)
    message_window_new (message);
  else if (bevent && bevent->button == 3)
    gtk_menu_popup (GTK_MENU (create_menu (BALSA_INDEX (widget), message)), NULL, NULL, NULL, NULL, bevent->button, bevent->time);
}
/*
 * CLIST Callbacks
 */
static GtkWidget *
create_menu (BalsaIndex * bindex, Message * message)
{
  GtkWidget *menu, *menuitem, *submenu, *smenuitem;
  Mailbox *mailbox;
  GList *list;

  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label ("Transfer");

  list = g_list_first (balsa_app.mailbox_list);
  submenu = gtk_menu_new ();
  while (list)
    {
      mailbox = list->data;
      smenuitem = gtk_menu_item_new_with_label (mailbox->name);
      gtk_menu_append (GTK_MENU (submenu), smenuitem);
      gtk_widget_show (smenuitem);
      list = list->next;
    }

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label ("Change Status");

  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new_with_label ("Unread");
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      (GtkSignalFunc) message_status_set_new_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label ("Read");
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      (GtkSignalFunc) message_status_set_read_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label ("Replied");
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		   (GtkSignalFunc) message_status_set_answered_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label ("Forwarded");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  if (message->flags & MESSAGE_FLAG_DELETED)
    {
      menuitem = gtk_menu_item_new_with_label ("Undelete");
      gtk_signal_connect (GTK_OBJECT (menuitem),
			  "activate",
			  (GtkSignalFunc) undelete_message_cb,
			  message);
    }
  else
    {
      menuitem = gtk_menu_item_new_with_label ("Delete");
      gtk_signal_connect (GTK_OBJECT (menuitem),
			  "activate",
			  (GtkSignalFunc) delete_message_cb,
			  message);
    }
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  return menu;
}

static void
message_status_set_new_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);
#if 0
  message_new (message);
#endif
  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
message_status_set_read_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);
#if 0
  message_read (message);
#endif
  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
message_status_set_answered_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_answer (message);
  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}


static void
delete_message_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_delete (message);
  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
undelete_message_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_undelete (message);
  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
next_message_cb (GtkWidget * widget)
{
  IndexChild *iw = get_index_window_data (GTK_OBJECT (widget));
  g_return_if_fail (widget != NULL);
  balsa_index_select_next (BALSA_INDEX (iw->index));
}

static void
previous_message_cb (GtkWidget * widget)
{
  IndexChild *iw = get_index_window_data (GTK_OBJECT (widget));
  g_return_if_fail (widget != NULL);
  balsa_index_select_previous (BALSA_INDEX (iw->index));
}

static void
index_child_class_init (IndexChildClass * class)
{
  GtkObjectClass *object_class;
  GnomeMDIChildClass *child_class;

  object_class = (GtkObjectClass *) class;
  child_class = GNOME_MDI_CHILD_CLASS (class);

  child_class->create_view = index_child_create_view;

  parent_class = gtk_type_class (gnome_mdi_child_get_type ());
}

static void
index_child_init (IndexChild * child)
{

}
