
/* Balsa E-Mail Client
 * Copyright (C) 1998 Stuart Parmenter
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

guint
index_child_get_type (void)
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
static void index_select_cb (GtkWidget * widget, Message * message, GdkEventButton *, gpointer data);
static GtkWidget *create_menu (BalsaIndex * bindex);

/* menu item callbacks */
static void message_status_set_new_cb (GtkWidget *, Message *);
static void message_status_set_read_cb (GtkWidget *, Message *);
static void message_status_set_answered_cb (GtkWidget *, Message *);
static void delete_message_cb (GtkWidget *, BalsaIndex *);
static void undelete_message_cb (GtkWidget *, BalsaIndex *);
static void transfer_messages_cb (BalsaMBList *, Mailbox *, GtkCTreeNode *, GdkEventButton *, BalsaIndex *);

void
index_child_changed (GnomeMDI * mdi, GnomeMDIChild * mdi_child)
{
  if (mdi->active_child)
    balsa_app.current_index_child = INDEX_CHILD (mdi->active_child);
  else
    balsa_app.current_index_child = NULL;
}

IndexChild *
index_child_get_active (GnomeMDI * mdi)
{
  if (mdi)
    return INDEX_CHILD (mdi->active_child);
  else
    return NULL;
}

static void
set_password (GtkWidget * widget, GtkWidget * entry)
{
  Mailbox *mailbox;

  mailbox = gtk_object_get_data (GTK_OBJECT (entry), "mailbox");
  if (!mailbox)
    return;
  if (mailbox->type == MAILBOX_IMAP)
    MAILBOX_IMAP (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
  if (mailbox->type == MAILBOX_POP3)
    MAILBOX_POP3 (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
  gtk_object_remove_data (GTK_OBJECT (entry), "mailbox");
}


IndexChild *
index_child_new (GnomeMDI * mdi, Mailbox * mailbox)
{
  IndexChild *child;
  GnomeMDIChild *mdichild;
  GtkWidget *messagebox;

  main_window_set_cursor (GDK_WATCH);

  {
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *dialog;
    GtkWidget *entry;

    if ((mailbox->type == MAILBOX_IMAP && MAILBOX_IMAP (mailbox)->passwd == NULL)
	||
	(mailbox->type == MAILBOX_POP3 && MAILBOX_POP3 (mailbox)->passwd == NULL))
      {

	dialog = gnome_dialog_new (_ ("Mailbox password:"),
		    GNOME_STOCK_BUTTON_OK, GNOME_STOCK_BUTTON_CANCEL, NULL);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox), hbox, FALSE, FALSE, 10);

	label = gtk_label_new ("Password:");
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 10);

	entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
	gtk_object_set_data (GTK_OBJECT (entry), "mailbox", mailbox);
	gtk_box_pack_start (GTK_BOX (hbox), entry, FALSE, FALSE, 10);

	gtk_widget_show_all (dialog);
	gnome_dialog_button_connect (GNOME_DIALOG (dialog), 0, set_password, entry);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
	gnome_dialog_run (GNOME_DIALOG (dialog));
	gtk_widget_destroy(dialog);
	dialog = NULL;
      }
  }

  /* check to see if its still null */
  if ((mailbox->type == MAILBOX_IMAP && MAILBOX_IMAP (mailbox)->passwd == NULL)
      ||
  (mailbox->type == MAILBOX_POP3 && MAILBOX_POP3 (mailbox)->passwd == NULL))
    {
      return NULL;
    }

  if (!mailbox_open_ref (mailbox))
    {
      messagebox = gnome_message_box_new (_ ("Unable to Open Mailbox!"),
					  GNOME_MESSAGE_BOX_ERROR,
					  GNOME_STOCK_BUTTON_OK,
					  NULL);
      gtk_widget_set_usize (messagebox, MESSAGEBOX_WIDTH, MESSAGEBOX_HEIGHT);
      gtk_window_position (GTK_WINDOW (messagebox), GTK_WIN_POS_CENTER);
      gtk_widget_show (messagebox);
      return NULL;
    }

  mdichild = gnome_mdi_find_child (mdi, mailbox->name);
  if (mdichild)
    return NULL;

  child = gtk_type_new (index_child_get_type ());
  if (child)
    {
      child->mailbox = mailbox;
      child->mdi = mdi;

      GNOME_MDI_CHILD (child)->name = g_strdup (mailbox->name);
    }

  return child;
}

static void
index_child_destroy (GtkObject * obj)
{
  IndexChild *ic;

  ic = INDEX_CHILD (obj);

  mailbox_open_unref (ic->mailbox);
  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (*GTK_OBJECT_CLASS (parent_class)->destroy) (GTK_OBJECT (ic));
}

static gboolean
check_for_new_mail (GtkWidget * widget)
{
  g_return_val_if_fail (BALSA_IS_INDEX (widget), FALSE);

  mailbox_check_new_messages (BALSA_INDEX (widget)->mailbox);
  g_print ("Checking for new mail in: %s\n", BALSA_INDEX (widget)->mailbox->name);

  return TRUE;
}

static GtkWidget *
index_child_create_view (GnomeMDIChild * child)
{
  GtkWidget *vpane;
  GtkWidget *table;
  GtkWidget *hscrollbar;
  GtkWidget *vscrollbar;
  IndexChild *ic;

  ic = INDEX_CHILD (child);

  if (balsa_app.previewpane)
    {

      vpane = gtk_vpaned_new ();

      ic->index = balsa_index_new ();

      gtk_widget_set_usize (ic->index, 1, 200);

      gtk_paned_add1 (GTK_PANED (vpane), ic->index);

      table = gtk_table_new (2, 2, FALSE);

      ic->message = balsa_message_new ();

      gtk_table_attach_defaults (GTK_TABLE (table), ic->message, 0, 1, 0, 1);

      hscrollbar = gtk_hscrollbar_new (GTK_LAYOUT (ic->message)->hadjustment);
      GTK_WIDGET_UNSET_FLAGS (hscrollbar, GTK_CAN_FOCUS);
      gtk_table_attach (GTK_TABLE (table), hscrollbar, 0, 1, 1, 2,
			GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

      vscrollbar = gtk_vscrollbar_new (GTK_LAYOUT (ic->message)->vadjustment);
      GTK_WIDGET_UNSET_FLAGS (vscrollbar, GTK_CAN_FOCUS);
      gtk_table_attach (GTK_TABLE (table), vscrollbar, 1, 2, 0, 1,
			GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

      gtk_paned_add2 (GTK_PANED (vpane), table);
      gtk_widget_set_usize (vpane, 1, 250);
      gtk_widget_show_all (vpane);

    }
  else
    {
      ic->index = balsa_index_new ();
      gtk_widget_show (ic->index);
    }


  balsa_index_set_mailbox (BALSA_INDEX (ic->index), ic->mailbox);

  gtk_signal_connect (GTK_OBJECT (ic->index), "select_message",
		      (GtkSignalFunc) index_select_cb, ic);

  if (balsa_app.previewpane)
    return (vpane);
  else
    return ic->index;
}

static gint handler = 0;
static gboolean
idle_handler_cb (GtkWidget * widget)
{
  GdkEventButton *bevent;
  Message *message;
  gpointer data;

  bevent = gtk_object_get_data (GTK_OBJECT (widget), "bevent");
  message = gtk_object_get_data (GTK_OBJECT (widget), "message");
  data = gtk_object_get_data (GTK_OBJECT (widget), "data");

  if (bevent && bevent->button == 3)
    gtk_menu_popup (GTK_MENU (create_menu (BALSA_INDEX (widget))),
		    NULL, NULL, NULL, NULL,
		    bevent->button, bevent->time);

  else if (INDEX_CHILD (data)->message)
    {
      if (BALSA_MESSAGE (INDEX_CHILD (data)->message))
	balsa_message_set (BALSA_MESSAGE (INDEX_CHILD (data)->message), message);
    }

  handler = 0;

  gtk_object_remove_data (GTK_OBJECT (widget), "bevent");
  gtk_object_remove_data (GTK_OBJECT (widget), "message");
  gtk_object_remove_data (GTK_OBJECT (widget), "data");
  return FALSE;
}

static void
index_select_cb (GtkWidget * widget,
		 Message * message,
		 GdkEventButton * bevent,
		 gpointer data)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (message != NULL);

  set_imap_username (message->mailbox);

  if (bevent && bevent->type == GDK_2BUTTON_PRESS)
    {
      message_window_new (message);
      return;
    }
  gtk_object_set_data (GTK_OBJECT (widget), "message", message);
  gtk_object_set_data (GTK_OBJECT (widget), "bevent", bevent);
  gtk_object_set_data (GTK_OBJECT (widget), "data", data);

  /* this way we only display one message, not lots and lots */
  if (!handler)
    handler = gtk_idle_add ((GtkFunction) idle_handler_cb, widget);

}

/*
 * CLIST Callbacks
 */
static GtkWidget *
create_menu (BalsaIndex * bindex)
{
  GtkWidget *menu, *menuitem, *submenu, *smenuitem;
  GtkWidget *bmbl;

  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label (_ ("Transfer"));

  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new ();
  bmbl = balsa_mblist_new ();
  gtk_signal_connect (GTK_OBJECT (bmbl), "select_mailbox",
		      (GtkSignalFunc) transfer_messages_cb,
		      (gpointer) bindex);

  gtk_widget_set_usize (GTK_WIDGET (bmbl), balsa_app.mblist_width, balsa_app.mblist_height);
  gtk_container_add (GTK_CONTAINER (smenuitem), bmbl);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (bmbl);
  gtk_widget_show (smenuitem);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

#if 0				/* FIXME */
  menuitem = gtk_menu_item_new_with_label (_ ("Change Status"));

  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new_with_label (_ ("Unread"));
  gtk_signal_connect (GTK_OBJECT (smenuitem), "activate",
		      (GtkSignalFunc) message_status_set_new_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label (_ ("Read"));
  gtk_signal_connect (GTK_OBJECT (smenuitem), "activate",
		      (GtkSignalFunc) message_status_set_read_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label (_ ("Replied"));
  gtk_signal_connect (GTK_OBJECT (smenuitem), "activate",
		   (GtkSignalFunc) message_status_set_answered_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label (_ ("Forwarded"));
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

#endif
  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_TRASH, _ ("Delete"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) delete_message_cb,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_UNDELETE, _ ("Undelete"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) undelete_message_cb,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  return menu;
}

static void
message_status_set_new_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_unread (message);
  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
message_status_set_read_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_read (message);

  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
message_status_set_answered_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_reply (message);
}

static void
transfer_messages_cb (BalsaMBList * bmbl, Mailbox * mailbox, GtkCTreeNode * row, GdkEventButton * event, BalsaIndex * bindex)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  g_return_if_fail (bmbl != NULL);
  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (GTK_BIN (bindex)->child);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT(list->data));
      message_move (message, mailbox);
      list = list->next;
    }
}


static void
delete_message_cb (GtkWidget * widget, BalsaIndex * bindex)
{
  GtkCList *clist;
  GList *list;
  Message *message;
  gint i = 0;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (GTK_BIN (bindex)->child);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT(list->data));
      message_delete (message);
      i++;
      list = list->next;
    }

  if (i == 1)
    balsa_index_select_next (bindex);
}

static void
undelete_message_cb (GtkWidget * widget, BalsaIndex * bindex)
{
  GtkCList *clist;
  GList *list;
  Message *message;
  gint i = 0;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (GTK_BIN (bindex)->child);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT(list->data));
      message_undelete (message);
      list = list->next;
    }

  if (i == 1)
    balsa_index_select_next (bindex);
}

static void
index_child_class_init (IndexChildClass * class)
{
  GtkObjectClass *object_class;
  GnomeMDIChildClass *child_class;

  object_class = (GtkObjectClass *) class;
  child_class = GNOME_MDI_CHILD_CLASS (class);

  object_class->destroy = index_child_destroy;
  child_class->create_view = index_child_create_view;

  parent_class = gtk_type_class (gnome_mdi_child_get_type ());
}

static void
index_child_init (IndexChild * child)
{

}
