/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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
#include "index-child.h"
#include "main-window.h"
#include "misc.h"

typedef struct _MBListWindow MBListWindow;
struct _MBListWindow
  {
    GtkWidget *window;
    GnomeMDI *mdi;
    GtkCTree *ctree;
    GtkCTreeNode *parent;
  };

static MBListWindow *mblw = NULL;

/* callbacks */
static void destroy_mblist_window (GtkWidget * widget);
static void close_mblist_window (GtkWidget * widget);
static void mailbox_select_cb (GtkCTree *, GtkCTreeNode *, gint);


static void open_cb (GtkWidget *, gpointer);
static void close_cb (GtkWidget *, gpointer);

void
mblist_open_window (GnomeMDI * mdi)
{
  GtkWidget *bbox;
  GtkWidget *button;
  gchar *text[] =
  {NULL, "Balsa"};

  if (mblw)
    return;

  mblw = g_malloc (sizeof (MBListWindow));

  mblw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (mblw->window), "Mailboxes");

  mblw->mdi = mdi;
  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_mblist_window,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  mblw->ctree = GTK_CTREE (gtk_ctree_new (2, 1));
  gtk_ctree_set_line_style (mblw->ctree, GTK_CTREE_LINES_DOTTED);
  gtk_clist_set_policy (GTK_CLIST (mblw->ctree), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mblw->window)->vbox), GTK_WIDGET (mblw->ctree), TRUE, TRUE, 0);
  gtk_widget_show (GTK_WIDGET (mblw->ctree));

  mblw->parent = gtk_ctree_insert (mblw->ctree, NULL, NULL, text, 0, NULL,
				   NULL, NULL, NULL, FALSE, TRUE);

  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "tree_select_row",
		      (GtkSignalFunc) mailbox_select_cb,
		      (gpointer) NULL);

  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mblw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 2);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH / 2,
				 BALSA_BUTTON_HEIGHT / 2);
  gtk_widget_show (bbox);

  button = gtk_button_new_with_label ("Open");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (open_cb), NULL);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Close");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (close_cb), NULL);
  gtk_widget_show (button);

  gtk_widget_show (mblw->window);
}

static void
open_cb (GtkWidget * widget, gpointer data)
{
  IndexChild *index_child;
  GtkCTreeNode *ctnode;
  Mailbox *mailbox;
  GnomeMDIChild *child;

  if (!GTK_CLIST (mblw->ctree)->selection)
    return;

  ctnode = GTK_CLIST (mblw->ctree)->selection->data;
  mailbox = gtk_ctree_get_row_data (mblw->ctree, ctnode);

  index_child = index_child_new (mblw->mdi, mailbox);
  if (index_child)
    {
      gnome_mdi_add_child (mblw->mdi, GNOME_MDI_CHILD (index_child));
      gnome_mdi_add_view (mblw->mdi, GNOME_MDI_CHILD (index_child));
    }
}

static void
close_cb (GtkWidget * widget, gpointer data)
{
  GtkCTreeNode *ctnode;
  Mailbox *mailbox;
  GnomeMDIChild *child;

  if (!GTK_CLIST (mblw->ctree)->selection)
    return;

  ctnode = GTK_CLIST (mblw->ctree)->selection->data;
  mailbox = gtk_ctree_get_row_data (mblw->ctree, ctnode);

  if (mailbox)
    {
      child = gnome_mdi_find_child (mblw->mdi, mailbox->name);
      if (child)
	gnome_mdi_remove_child (mblw->mdi, child, TRUE);
    }
}

void
mblist_add_mailbox (Mailbox * mailbox)
{
  GtkCTreeNode *sibling;
  gchar *text[2];

  if (mailbox)
    {
      if (mailbox->type == MAILBOX_MBOX)
	if (mailbox_have_new_messages (MAILBOX_LOCAL (mailbox)->path))
	  text[0] = "N";
	else
	  text[0] = NULL;
      text[1] = mailbox->name;
      sibling = gtk_ctree_insert (mblw->ctree, mblw->parent, NULL, text, 0, NULL,
				  NULL, NULL, NULL, TRUE, TRUE);
      gtk_ctree_set_row_data (mblw->ctree, sibling, mailbox);
    }
}

void
mblist_remove_mailbox (Mailbox * mailbox)
{

}

static void
close_mblist_window (GtkWidget * widget)
{
  gtk_widget_destroy (mblw->window);
  gtk_widget_destroy (GTK_WIDGET (mblw->ctree));
}

static void
destroy_mblist_window (GtkWidget * widget)
{
  close_mblist_window (widget);
  g_free (mblw);
  mblw = NULL;
}

static void
mailbox_select_cb (GtkCTree * ctree, GtkCTreeNode * row, gint column)
{
  IndexChild *index_child;
  Mailbox *mailbox;
  GdkEventButton *bevent = (GdkEventButton *) gtk_get_current_event ();

  if (bevent && bevent->button == 1 && bevent->type == GDK_2BUTTON_PRESS)
    {
      mailbox = gtk_ctree_get_row_data (ctree, row);

      /* bail now if the we've been called without a valid
       * mailbox */
      if (!mailbox)
	return;

      index_child = index_child_new (mblw->mdi, mailbox);
      if (index_child)
	{
	  gnome_mdi_add_child (mblw->mdi, GNOME_MDI_CHILD (index_child));
	  gnome_mdi_add_view (mblw->mdi, GNOME_MDI_CHILD (index_child));
	}
/* TODO TODO FIXME remove index-child from list here */
    }
}
