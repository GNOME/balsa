/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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
#include "balsa-message.h"
#include "main-window.h"
#include "sendmsg-window.h"
#include "message-window.h"
#include "misc.h"

/* callbacks */
static void destroy_message_window (GtkWidget * widget, gpointer data);
static void close_message_window(GtkWidget * widget, gpointer data);

static void replyto_message_cb(GtkWidget * widget, gpointer data);
static void replytoall_message_cb(GtkWidget * widget, gpointer data);
static void forward_message_cb(GtkWidget * widget, gpointer data);

/*
 * The list of messages which are being displayed.
 */
static GHashTable * displayed_messages = NULL;

static GnomeUIInfo file_menu[] =
{
  GNOMEUIINFO_MENU_CLOSE_ITEM(close_message_window, NULL),

  GNOMEUIINFO_END
};

static GnomeUIInfo message_menu[] =
{
    /* R */
  {
    GNOME_APP_UI_ITEM, N_ ("_Reply"), N_("Reply to this message"),
    replyto_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_RPL, 'R', 0, NULL
  },
    /* A */
  {
    GNOME_APP_UI_ITEM, N_ ("Reply to _all"),
    N_("Reply to all recipients of this message"),
    replytoall_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_RPL, 'A', 0, NULL
  },
    /* F */
  {
    GNOME_APP_UI_ITEM, N_ ("_Forward"), N_("Forward this message"),
    forward_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
    GNOME_STOCK_MENU_MAIL_FWD, 'F', 0, NULL
  },

  GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] =
{
  GNOMEUIINFO_MENU_FILE_TREE(file_menu),
  GNOMEUIINFO_SUBTREE ("_Message", message_menu),
  
  GNOMEUIINFO_END
};

typedef struct _MessageWindow MessageWindow;
struct _MessageWindow
  {
    GtkWidget *window;

    GtkWidget *bmessage;

    Message * message;
  };

void
message_window_new (Message * message)
{
  MessageWindow *mw;
  GtkWidget *sw;

  if (!message)
    return;

  /*
   * Check to see if this message is already displayed
   */
  if (displayed_messages != NULL)
    {
      mw = (MessageWindow *) g_hash_table_lookup(displayed_messages,
						 message);
      if (mw != NULL)
	{
	  /*
	   * The message is already displayed in a window, so just use
	   * that one.
	   */
	  gdk_window_raise(GTK_WIDGET(mw->window)->window);
	  return;
	}
    }
  else
    {
      /*
       * We've never displayed a message before; initialize the hash
       * table.
       */
      displayed_messages = g_hash_table_new(g_direct_hash, g_direct_equal);
    }

  mw = g_malloc0 (sizeof (MessageWindow));

  g_hash_table_insert(displayed_messages, message, mw);

  mw->message = message;

  mw->window = gnome_app_new ("balsa", "Message");
  gtk_object_set_data (GTK_OBJECT (mw->window), "msgwin", mw);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "destroy",
		      GTK_SIGNAL_FUNC(destroy_message_window),
		      mw);

  gnome_app_create_menus_with_data (GNOME_APP (mw->window),
		  main_menu, mw->window);


  sw = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
  mw->bmessage = balsa_message_new ();
  gtk_container_add(GTK_CONTAINER(sw), mw->bmessage);

  balsa_message_set (BALSA_MESSAGE (mw->bmessage), message);

  gnome_app_set_contents (GNOME_APP (mw->window), sw);

  /* FIXME: set it to the size of the canvas, unless it is
   * bigger than the desktop, in which case it should be at about a
   * 2/3 proportional size based on the size of the desktop and the
   * height and width of the canvas.  [save and restore window size too]
   */

  gtk_window_set_default_size(GTK_WINDOW(mw->window), 400, 500);

  gtk_widget_show_all (mw->window);
}

static void
destroy_message_window (GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *) data;

  g_hash_table_remove(displayed_messages, mw->message);
  
  gtk_widget_destroy (mw->window);
  gtk_widget_destroy (mw->bmessage);

  g_free (mw);
}

static void
replyto_message_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *) gtk_object_get_data (GTK_OBJECT (data),
							     "msgwin");

  g_return_if_fail (widget != NULL);
  g_return_if_fail (mw != NULL);

  sendmsg_window_new (widget, mw->message, SEND_REPLY);
}

static void
replytoall_message_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *) gtk_object_get_data (GTK_OBJECT (data),
							     "msgwin");

  g_return_if_fail (widget != NULL);
  g_return_if_fail (mw != NULL);

  sendmsg_window_new (widget, mw->message, SEND_REPLY_ALL);
}

static void
forward_message_cb(GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = (MessageWindow *) gtk_object_get_data (GTK_OBJECT (data),
							     "msgwin");

  g_return_if_fail (widget != NULL);
  g_return_if_fail (mw != NULL);

  sendmsg_window_new (widget, mw->message, SEND_FORWARD);
}

static void
close_message_window(GtkWidget * widget, gpointer data)
{
  gtk_widget_destroy(GTK_WIDGET(data));
}


