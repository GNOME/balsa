/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gnome.h>
#include "balsa-message.h"
#include "sendmsg-window.h"

gint delete_event (GtkWidget *, gpointer);

static GtkMenuEntry main_menu_items[] =
{
  {"<Balsa>/Message/Send", NULL, NULL, NULL},
  {"<Balsa>/Message/Save as...", NULL, NULL, NULL},
  {"<Balsa>/Message/Print", NULL, NULL, NULL},
  {"<Balsa>/Message/<separator>", NULL, NULL, NULL},
  {"<Balsa>/Message/Attach file...", NULL, NULL, NULL},
  {"<Balsa>/Message/<separator>", NULL, NULL, NULL},
  {"<Balsa>/Message/Close", NULL, delete_event, NULL},
  {"<Balsa>/Edit/Copy", "<control>C", NULL, "Copy"},
  {"<Balsa>/Edit/Cut", "<control>X", NULL, "Cut"},
  {"<Balsa>/Edit/Paste", "<control>V", NULL, "Paste"},
  {"<Balsa>/PGP/FFU", NULL, NULL, NULL},
  {"<Balsa>/Help/Contents", NULL, NULL, NULL},
};
static int main_nmenu_items = sizeof (main_menu_items) / sizeof (main_menu_items[0]);


void 
sendmsg_window_new (GtkWidget * widget, gpointer data)
{
  BalsaSendmsg *msg = g_malloc0 (sizeof (BalsaSendmsg));
  GtkWidget *vbox;
  GtkWidget *table, *table1;
  GtkWidget *label;
  GtkWidget *menubar;
  GtkMenuFactory *factory;
  GtkMenuFactory *subfactories[1];

  msg->window = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_window_set_title (GTK_WINDOW (msg->window), "New message");
  gtk_container_border_width (GTK_CONTAINER (msg->window), 1);

  gtk_window_set_title (GTK_WINDOW (msg->window), "New Message");
  gtk_widget_set_usize (GTK_WIDGET (msg->window), 540, 380);
  gtk_signal_connect (GTK_OBJECT (msg->window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event), NULL);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (msg->window), vbox);
  gtk_widget_show (vbox);

  factory = gtk_menu_factory_new (GTK_MENU_FACTORY_MENU_BAR);
  subfactories[0] = gtk_menu_factory_new (GTK_MENU_FACTORY_MENU_BAR);
  gtk_menu_factory_add_subfactory (factory, subfactories[0], "<Balsa>");
  gtk_menu_factory_add_entries (factory, main_menu_items, main_nmenu_items);
  menubar = subfactories[0]->widget;
  gtk_box_pack_start (GTK_BOX (vbox), menubar, FALSE, TRUE, 0);
  gtk_widget_show (menubar);

  table = gtk_table_new (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  msg->to = gtk_entry_new ();
  label = gtk_label_new ("To:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 0, 1);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->to, 1, 2, 0, 1);
  gtk_widget_show (label);
  gtk_widget_show (msg->to);

  msg->from = gtk_entry_new ();
  label = gtk_label_new ("From:");
/*
   if (fOptions->realname && fOptions->emailaddy)
   {
   sprintf (buffer, "%s <%s>", fOptions->realname, fOptions->emailaddy);
   gtk_entry_set_text (GTK_ENTRY (msg->from), buffer);
   }
 */
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 1, 2);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->from, 1, 2, 1, 2);
  gtk_widget_show (label);
  gtk_widget_show (msg->from);

  msg->subject = gtk_entry_new ();
  label = gtk_label_new ("Subject:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 2, 3);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->subject, 1, 2, 2, 3);
  gtk_widget_show (label);
  gtk_widget_show (msg->subject);

  msg->cc = gtk_entry_new ();
  label = gtk_label_new ("cc:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 3, 4);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->cc, 1, 2, 3, 4);
  gtk_widget_show (label);
  gtk_widget_show (msg->cc);

  msg->bcc = gtk_entry_new ();
  label = gtk_label_new ("Bcc:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach_defaults (GTK_TABLE (table), label, 0, 1, 4, 5);
  gtk_table_attach_defaults (GTK_TABLE (table), msg->bcc, 1, 2, 4, 5);
  gtk_widget_show (label);
  gtk_widget_show (msg->bcc);

/*
 * Compose Text Area
 */

  table1 = gtk_table_new (2, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table1, TRUE, TRUE, 0);
  gtk_widget_show (table1);

  msg->text = gtk_text_new (NULL, NULL);
  gtk_text_set_editable (GTK_TEXT (msg->text), TRUE);
  gtk_table_attach_defaults (GTK_TABLE (table1), msg->text, 0, 1, 0, 1);
  msg->hscrollbar = gtk_hscrollbar_new (GTK_TEXT (msg->text)->hadj);
  gtk_table_attach (GTK_TABLE (table1), msg->hscrollbar, 0, 1, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (msg->hscrollbar);

  msg->vscrollbar = gtk_vscrollbar_new (GTK_TEXT (msg->text)->vadj);
  gtk_table_attach (GTK_TABLE (table1), msg->vscrollbar, 1, 2, 0, 1,
		    GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
  gtk_widget_show (msg->vscrollbar);

  gtk_widget_show (msg->text);
  gtk_widget_show (table1);
  gtk_widget_show (msg->window);
  return;
}
