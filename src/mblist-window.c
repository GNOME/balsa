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

typedef struct _MBListWindow MBListWindow;
struct _MBListWindow
  {
    GtkWidget *window;
    GtkWidget *tree;
  };

static MBListWindow *mblw = NULL;

void mblist_open_window (void);

void mblist_add_mailbox (Mailbox * mailbox);
void mblist_remove_mailbox (Mailbox * mailbox);

/* callbacks */
static void destroy_mblist_window (GtkWidget * widget);
static void close_mblist_window (GtkWidget * widget);

void
mblist_open_window (void)
{
  GtkWidget *vbox;
  GtkWidget *scrolled_win;

  if (mblw)
    return;

  mblw = g_malloc (sizeof (MBListWindow));

  mblw->window = gnome_app_new ("balsa", "Mailboxes");

  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_mblist_window,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  vbox = gtk_vbox_new (TRUE, 0);
  gnome_app_set_contents (GNOME_APP (mblw->window), vbox);
  gtk_widget_show (vbox);

  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled_win, TRUE, TRUE, 3);
  gtk_widget_set_usize (scrolled_win, 200, 200);
  gtk_widget_show (scrolled_win);


  mblw->tree = gtk_tree_new ();
  gtk_container_add (GTK_CONTAINER (scrolled_win), mblw->tree);
  gtk_widget_show (mblw->tree);

  gtk_widget_show (mblw->window);
}


void
mblist_add_mailbox (Mailbox * mailbox)
{
  GtkWidget *tree_item;
  tree_item = gtk_tree_item_new_with_label (mailbox->name);
  gtk_tree_append (GTK_TREE (mblw->tree), tree_item);
  gtk_widget_show(tree_item);
  gtk_object_set_user_data (GTK_OBJECT (tree_item), mailbox);
}

void
mblist_remove_mailbox (Mailbox * mailbox)
{

}

static void
close_mblist_window (GtkWidget * widget)
{
  gtk_widget_destroy (mblw->window);
  gtk_widget_destroy (mblw->tree);
}

static void
destroy_mblist_window (GtkWidget * widget)
{
  close_mblist_window (widget);
  g_free (mblw);
  mblw = NULL;
}
