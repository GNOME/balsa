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
#include "index-window.h"
#include "main-window.h"
#include "message-window.h"
#include "misc.h"

typedef struct _IndexWindow IndexWindow;
struct _IndexWindow
  {
    Mailbox *mailbox;

    guint watcher_id;
    GtkWidget *window;
    GtkWidget *index;
  };

static GList *open_mailbox_list = NULL;


/* notebook pages */
void create_new_index (Mailbox * mailbox);

/* callbacks */
static void destroy_index_window (GtkWidget * widget);
static void close_index_window (GtkWidget * widget);
static void refresh_index_window (IndexWindow * iw);
static void mailbox_listener (MailboxWatcherMessage * iw_message);
static void index_select_cb (GtkWidget * widget, Message * message);

void
create_new_index (Mailbox * mailbox)
{
  IndexWindow *iw;
  GList *list;
  GtkWidget *messagebox;
  GtkWidget *vbox;

  iw = g_malloc (sizeof (IndexWindow));
  /* DISABLE EDITING FOR NOW */
  if (!mailbox)
    return;

  /* TODO check to see if already open */

  iw->mailbox = mailbox;

  iw->window = gnome_app_new ("balsa", iw->mailbox->name);

  set_index_window_data(GTK_OBJECT(iw->window),iw);
  gtk_signal_connect (GTK_OBJECT (iw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_index_window,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (iw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  vbox = gtk_vbox_new (TRUE, 0);
  gnome_app_set_contents (GNOME_APP (iw->window), vbox);
  gtk_widget_show (vbox);

  iw->index = balsa_index_new ();
  gtk_box_pack_start (GTK_BOX (vbox), iw->index, TRUE, TRUE, 0);
  gtk_widget_show (iw->index);

  balsa_index_set_mailbox (BALSA_INDEX (iw->index), iw->mailbox);

  gtk_signal_connect (GTK_OBJECT (iw->index),
		      "select_message",
		      (GtkSignalFunc) index_select_cb,
		      NULL);

  iw->watcher_id = mailbox_watcher_set (mailbox,
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

  gtk_widget_show (iw->window);
}

/*
 * set/get data convience functions used for attaching the
 * IndexWindow structure to GTK objects so it can be retrieved
 * in callbacks
 */
static void
set_index_window_data (GtkObject * object, IndexWindow * iw)
{
  gtk_object_set_data (object, "index_window_data", (gpointer) iw);
}


static IndexWindow *
get_index_window_data (GtkObject * object)
{
  return gtk_object_get_data (object, "index_window_data");
}


static void
close_index_window (GtkWidget * widget)
{
  IndexWindow *iw = get_index_window_data (GTK_OBJECT (widget));
  gtk_widget_destroy (iw->window);
  gtk_widget_destroy (iw->index);
}

static void
destroy_index_window (GtkWidget * widget)
{
  IndexWindow *iw = get_index_window_data (GTK_OBJECT (widget));

#if 0
  /* remove the mailbox from the open mailbox list */
  open_mailbox_list = g_list_remove (open_mailbox_list, nmw);

#endif

  close_index_window (widget);

  g_free (iw);
}


static void
mailbox_listener (MailboxWatcherMessage * iw_message)
{
  IndexWindow *iw = (IndexWindow *) iw_message->data;

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
		 Message * message)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (message != NULL);

  message_window_new (message);
}
