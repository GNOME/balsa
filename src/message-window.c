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
static void destroy_message_window (GtkWidget * widget);

void
message_window_new (Message * message)
{
  MessageWindow *mw;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *hscrollbar;
  GtkWidget *vscrollbar;

  if (!message)
    return;

  mw = g_malloc0 (sizeof (MessageWindow));

  mw->window = gnome_app_new ("balsa", "Message");
  gtk_object_set_data (GTK_OBJECT (mw->window), "msgwin", mw);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_message_window,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  gnome_app_create_menus_with_data ( GNOME_APP (mw->window), main_menu,
  				     mw->window );

  vbox = gtk_vbox_new (TRUE, 0);
  gnome_app_set_contents (GNOME_APP (mw->window), vbox);

  table = gtk_table_new (2, 2, FALSE);

  mw->bmessage = balsa_message_new ();

  gtk_table_attach_defaults (GTK_TABLE (table), mw->bmessage, 0, 1, 0, 1);

  hscrollbar = gtk_hscrollbar_new (GTK_LAYOUT (mw->bmessage)->hadjustment);
  GTK_WIDGET_UNSET_FLAGS (hscrollbar, GTK_CAN_FOCUS);
  gtk_table_attach (GTK_TABLE (table), hscrollbar, 0, 1, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);

  vscrollbar = gtk_vscrollbar_new (GTK_LAYOUT (mw->bmessage)->vadjustment);
  GTK_WIDGET_UNSET_FLAGS (vscrollbar, GTK_CAN_FOCUS);
  gtk_table_attach (GTK_TABLE (table), vscrollbar, 1, 2, 0, 1,
		    GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);


  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  balsa_message_set (BALSA_MESSAGE (mw->bmessage), message);

  gtk_widget_show_all (mw->window);
}

static void
destroy_message_window (GtkWidget * widget)
{
  MessageWindow *mw = gtk_object_get_data (GTK_OBJECT (widget), "msgwin");
  gtk_object_remove_data (GTK_OBJECT (widget), "msgwin");

  gtk_widget_destroy (mw->window);
  gtk_widget_destroy (mw->bmessage);

  g_free (mw);
}

static void
close_message_window(GtkWidget * widget, gpointer data)
{
  destroy_message_window(GTK_WIDGET(data));
}


