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
#include "index-child.h"
#include "main-window.h"
#include "misc.h"

typedef struct _MBListWindow MBListWindow;
struct _MBListWindow
  {
    GtkWidget *window;
    GnomeMDI *mdi;
    GtkCTree *ctree;
    GList *parent;
  };

static MBListWindow *mblw = NULL;

void mblist_open_window (GnomeMDI *);

void mblist_add_mailbox (Mailbox * mailbox);
void mblist_remove_mailbox (Mailbox * mailbox);

/* callbacks */
static void destroy_mblist_window (GtkWidget * widget);
static void close_mblist_window (GtkWidget * widget);
static void mailbox_select_cb (GtkCTree * ctree, GList * row, gint column);

void
mblist_open_window (GnomeMDI * mdi)
{
  GtkWidget *vbox;
  gchar *text[] =
  {"Balsa"};

  guint handler;

  if (mblw)
    return;

  mblw = g_malloc (sizeof (MBListWindow));

  mblw->window = gnome_app_new ("balsa", "Mailboxes");

  mblw->mdi = mdi;
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

  mblw->ctree = GTK_CTREE (gtk_ctree_new (1, 0));
  gtk_ctree_set_line_style (mblw->ctree, GTK_CTREE_LINES_DOTTED);
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (mblw->ctree), TRUE, TRUE, 0);
  gtk_widget_show (GTK_WIDGET (mblw->ctree));

  mblw->parent = gtk_ctree_insert (mblw->ctree, NULL, NULL, text, 1, NULL,
				   NULL, NULL, NULL, FALSE, TRUE);

  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "tree_select_row",
		      (GtkSignalFunc) mailbox_select_cb,
		      (gpointer) NULL);

  gtk_widget_show (mblw->window);
}


void
mblist_add_mailbox (Mailbox * mailbox)
{
  GList *sibling;
  gchar *text[1];
  text[0] = mailbox->name;
  sibling = gtk_ctree_insert (mblw->ctree, mblw->parent, NULL, text, 1, NULL,
			      NULL, NULL, NULL, FALSE, TRUE);
  gtk_ctree_set_row_data (mblw->ctree, sibling, mailbox);
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
mailbox_select_cb (GtkCTree * ctree, GList * row, gint column)
{
  IndexChild *index_child;
  Mailbox *mailbox;

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
