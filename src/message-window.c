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
#include "misc.h"

typedef struct _MessageWindow MessageWindow;
struct _MessageWindow
  {
    GtkWidget *window;
    GtkWidget *index;

    GtkWidget *bmessage;
  };

static GList *message_window_list = NULL;


void message_window_new (Message *message);

/* callbacks */
static void destroy_message_window (GtkWidget * widget);
static void close_message_window (GtkWidget * widget);
static void refresh_message_window (MessageWindow * mw);

static void mailbox_listener (MailboxWatcherMessage * iw_message);

void
message_window_new(Message *message)
{
  MessageWindow *mw;
  GtkWidget *vbox;

  if (!message)
    return;

  mw = g_malloc (sizeof (MessageWindow));

  /* TODO check to see if already open */

  mw->window = gnome_app_new ("balsa", "message");

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_message_window,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  vbox = gtk_vbox_new (TRUE, 0);
  gnome_app_set_contents (GNOME_APP (mw->window), vbox);
  gtk_widget_show (vbox);

  mw->bmessage = balsa_message_new ();
  gtk_box_pack_start (GTK_BOX (vbox), mw->bmessage, TRUE, TRUE, 0);
  balsa_message_set (BALSA_MESSAGE (mw->bmessage), message);
  gtk_widget_show(mw->bmessage);
  
  gtk_widget_show (mw->window);
}

/*
 * set/get data convience functions used for attaching the
 * IndexWindow structure to GTK objects so it can be retrieved
 * in callbacks
 */
static void
set_message_window_data (GtkObject * object, MessageWindow * mw)
{
  gtk_object_set_data (object, "message_window_data", (gpointer) mw);
}


static MessageWindow *
get_index_window_data (GtkObject * object)
{
  return gtk_object_get_data (object, "message_window_data");
}


static void
close_message_window (GtkWidget * widget)
{
  MessageWindow *mw = get_index_window_data (GTK_OBJECT (widget));
  gtk_widget_destroy (mw->window);
  gtk_widget_destroy (mw->bmessage);
}

static void
destroy_message_window (GtkWidget * widget)
{
  MessageWindow *mw = get_index_window_data (GTK_OBJECT (widget));

#if 0
  /* remove the mailbox from the open mailbox list */
  open_mailbox_list = g_list_remove (open_mailbox_list, nmw);

#endif

  close_message_window (widget);

  g_free (mw);
}

