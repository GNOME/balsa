/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <string.h>
#include "main-window.h"
#include "balsa-message.h"
#include "balsa-index.h"
#include "sendmsg-window.h"
#include "index.h"
#include "mailbox.h"
#include "options.h"
#include "../config.h"


void show_about_box (GtkWidget * widget, gpointer data);

/* from main.c -- exit balsa */
extern void balsa_exit ();

/* menu structure for menu factory */
static GtkMenuEntry main_menu_items[] =
{
  {"<Balsa>/File/Get New Mail", "<control>M", current_mailbox_check, NULL},
  {"<Balsa>/File/<separator>", NULL, NULL, NULL},
  {"<Balsa>/File/New Folder...", NULL, NULL, NULL},
  {"<Balsa>/File/Open Folder...", NULL, NULL, NULL},
  {"<Balsa>/File/Save Folder", NULL, NULL, NULL},
  {"<Balsa>/File/<separator>", NULL, NULL, NULL},
  {"<Balsa>/File/Print", NULL, NULL, NULL},
  {"<Balsa>/File/<separator>", NULL, NULL, NULL},
  {"<Balsa>/File/Exit", "<control>Q", balsa_exit, NULL},
  {"<Balsa>/Edit/Copy", "<control>C", NULL, "Copy"},
  {"<Balsa>/Edit/Cut", "<control>X", NULL, "Cut"},
  {"<Balsa>/Edit/Paste", "<control>V", NULL, "Paste"},
  {"<Balsa>/Message/New", "<control>N", sendmsg_window_new, NULL},
  {"<Balsa>/Message/Reply", "<control>R", NULL, NULL},
  {"<Balsa>/Message/Reply to All", NULL, NULL, NULL},
  {"<Balsa>/Message/Foward", NULL, NULL, NULL},
  {"<Balsa>/Message/Redirect", NULL, NULL, NULL},
  {"<Balsa>/Message/Send Again", NULL, NULL, NULL},
  {"<Balsa>/Message/<separator>", NULL, NULL, NULL},
/*
   This needs to be part of the menu on the new_message window, not the main one
   -pav
   { "<Balsa>/Message/Attach File to New Message", "<control>H", NULL, NULL },
 */
  {"<Balsa>/Message/Delete", "<control>D", NULL, NULL},
  {"<Balsa>/Tools/Personalities...", NULL, personality_box, NULL},
  {"<Balsa>/Tools/Settings...", NULL, NULL, NULL},
  {"<Balsa>/Help/Contents", NULL, NULL, NULL},
  {"<Balsa>/Help/About", NULL, show_about_box, NULL}
};
static int main_nmenu_items = sizeof (main_menu_items) / sizeof (main_menu_items[0]);


MainWindow *
create_main_window ()
{
  MainWindow *mw;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *vpane;

  mw = g_malloc (sizeof (MainWindow));

  /* structure initalizations */
  mw->mailbox_menu = NULL;
  mw->move_menu = NULL;

  /* main window */
  mw->window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (mw->window), "Balsa");
  gtk_container_border_width (GTK_CONTAINER (mw->window), 0);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "destroy",
		      (GtkSignalFunc) balsa_exit,
		      &mw->window);

  gtk_signal_connect (GTK_OBJECT (mw->window),
		      "delete_event",
		      (GtkSignalFunc) balsa_exit,
		      &mw->window);


  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (mw->window), vbox);
  gtk_widget_show (vbox);


  /* set menu bar */
  mw->factory = gtk_menu_factory_new (GTK_MENU_FACTORY_MENU_BAR);
  mw->subfactories[0] = gtk_menu_factory_new (GTK_MENU_FACTORY_MENU_BAR);
  gtk_menu_factory_add_subfactory (mw->factory, mw->subfactories[0], "<Balsa>");
  gtk_menu_factory_add_entries (mw->factory, main_menu_items, main_nmenu_items);
  mw->menubar = mw->subfactories[0]->widget;
  gtk_box_pack_start (GTK_BOX (vbox), mw->menubar, FALSE, TRUE, 0);
  gtk_widget_show (mw->menubar);


  /* toolbar */
  mw->toolbar = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_BOTH);
  gtk_box_pack_start (GTK_BOX (vbox), mw->toolbar, FALSE, TRUE, 2);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  label = gtk_label_new ("Mailbox:");
  gtk_toolbar_append_widget (GTK_TOOLBAR (mw->toolbar), label, NULL, NULL);
  gtk_widget_show (label);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  mw->mailbox_option_menu = gtk_option_menu_new ();
  gtk_toolbar_append_widget (GTK_TOOLBAR (mw->toolbar), mw->mailbox_option_menu, NULL, NULL);
  gtk_widget_show (mw->mailbox_option_menu);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Check Mail",
			   NULL, NULL, NULL, current_mailbox_check, NULL);

  gtk_toolbar_append_space (GTK_TOOLBAR (mw->toolbar));

  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Compose", 
			   NULL, NULL, NULL, NULL, NULL);
  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Reply", 
			   NULL, NULL, NULL, NULL, NULL);
  gtk_toolbar_append_item (GTK_TOOLBAR (mw->toolbar), "Delete", 
			   NULL, NULL, NULL, NULL, NULL);

  gtk_widget_show (mw->toolbar);


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

  gtk_widget_show (mw->window);
  return mw;
}

void
show_about_box (GtkWidget * widget, gpointer data)
{
  gchar *authors[] =
  {
    "Jay Painter <jpaint@gimp.org>",
    "Stuart Parmenter <pavlov@pavlov.net>",
    NULL
  };

  GtkWidget *about = gnome_about_new ("Balsa",
				      BALSA_VERSION,
				      "Copyright (C) 1997-98",
				      authors,
				      "The flimsey e-mail client!",
				      "/tmp/balsa.xpm");
  gtk_widget_show (about);
}
