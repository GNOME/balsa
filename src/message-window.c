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

#include <gnome.h>
#include "balsa-app.h"
#include "balsa-message.h"
#include "main-window.h"
#include "misc.h"

static void close_message_window(GtkWidget * widget, gpointer data);

static GnomeUIInfo file_menu[] =
{
  {
    GNOME_APP_UI_ITEM, N_ ("_Close"), NULL, close_message_window, NULL,
    NULL, GNOME_APP_PIXMAP_STOCK, GNOME_STOCK_MENU_CLOSE, 'C', 0, NULL
  },
  GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] =
{
  GNOMEUIINFO_SUBTREE ("_File", file_menu),
  GNOMEUIINFO_END
};

typedef struct _MessageWindow MessageWindow;
struct _MessageWindow
  {
    GtkWidget *window;

    GtkWidget *bmessage;
  };

void message_window_new (Message * message);

/* callbacks */
static void destroy_message_window (GtkWidget * widget, gpointer data);

void
message_window_new (Message * message)
{
  MessageWindow *mw;
  GtkWidget *sw;

  if (!message)
    return;

  mw = g_malloc0 (sizeof (MessageWindow));

  mw->window = gnome_app_new ("balsa", "Message");
  gtk_object_set_data (GTK_OBJECT (mw->window), "msgwin", mw->window);

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

  gtk_widget_show_all (mw->window);
}

static void
destroy_message_window (GtkWidget * widget, gpointer data)
{
  MessageWindow *mw = data;

  gtk_widget_destroy (mw->window);
  gtk_widget_destroy (mw->bmessage);

  g_free (mw);
}

static void
close_message_window(GtkWidget * widget, gpointer data)
{
  gtk_widget_destroy(GTK_WIDGET(data));
}


