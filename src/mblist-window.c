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
#include "mailbox-conf.h"
#include "main-window.h"
#include "mblist-window.h"
#include "misc.h"

#include "pixmaps/mini_dir_closed.xpm"
#include "pixmaps/mini_dir_open.xpm"
#include "pixmaps/plain-folder.xpm"
#include "pixmaps/full-folder.xpm"
#include "pixmaps/inbox.xpm"
#include "pixmaps/outbox.xpm"
#include "pixmaps/trash.xpm"

static GdkPixmap *open_folder;
static GdkPixmap *closed_folder;
static GdkPixmap *tray_empty;
static GdkPixmap *tray_full;
static GdkPixmap *inboxpix;
static GdkPixmap *outboxpix;
static GdkPixmap *trashpix;

static GdkBitmap *open_mask;
static GdkBitmap *closed_mask;
static GdkBitmap *tray_empty_mask;
static GdkBitmap *tray_full_mask;
static GdkBitmap *inbox_mask;
static GdkBitmap *outbox_mask;
static GdkBitmap *trash_mask;

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
static void button_event_press_cb (GtkCList *, GdkEventButton *, gpointer);
static GtkWidget * create_menu (GtkCTree * ctree, Mailbox * mailbox);

static void open_cb (GtkWidget *, gpointer);
static void close_cb (GtkWidget *, gpointer);

static gboolean mailbox_nodes_to_ctree (GtkCTree *, guint, GNode *, GtkCTreeNode *, gpointer);

void
mblist_open_window (GnomeMDI * mdi)
{
  GtkWidget *bbox;
  GtkWidget *button;
  GtkCTreeNode *ctnode;
  GdkColor *transparent = NULL;
  gint height;
  gchar *text[] =
  {"Balsa"};

  if (mblw)
    {
      gdk_window_raise (mblw->window->window);
      return;
    }

  mblw = g_malloc (sizeof (MBListWindow));

  mblw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (mblw->window), "Mailboxes");

  gtk_widget_realize (mblw->window);

  open_folder = gdk_pixmap_create_from_xpm_d (mblw->window->window,
				&open_mask, transparent, mini_dir_open_xpm);

  closed_folder = gdk_pixmap_create_from_xpm_d (mblw->window->window,
			    &closed_mask, transparent, mini_dir_closed_xpm);

  tray_empty = gdk_pixmap_create_from_xpm_d (mblw->window->window,
			   &tray_empty_mask, transparent, plain_folder_xpm);

  tray_full = gdk_pixmap_create_from_xpm_d (mblw->window->window,
			     &tray_full_mask, transparent, full_folder_xpm);

  inboxpix = gdk_pixmap_create_from_xpm_d (mblw->window->window,
				       &inbox_mask, transparent, inbox_xpm);

  outboxpix = gdk_pixmap_create_from_xpm_d (mblw->window->window,
				     &outbox_mask, transparent, outbox_xpm);

  trashpix = gdk_pixmap_create_from_xpm_d (mblw->window->window,
				       &trash_mask, transparent, trash_xpm);

  mblw->mdi = mdi;
  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_mblist_window,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  mblw->ctree = GTK_CTREE (gtk_ctree_new (1, 0));
  gtk_ctree_set_line_style (mblw->ctree, GTK_CTREE_LINES_DOTTED);
  gtk_clist_set_policy (GTK_CLIST (mblw->ctree), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_clist_set_row_height (GTK_CLIST (mblw->ctree), 16);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mblw->window)->vbox), GTK_WIDGET (mblw->ctree), TRUE, TRUE, 0);
  gtk_widget_show (GTK_WIDGET (mblw->ctree));

  mblw->parent = gtk_ctree_insert_node (mblw->ctree, NULL, NULL, text, 0, NULL,
					NULL, NULL, NULL, FALSE, TRUE);

  gtk_clist_freeze (GTK_CLIST (mblw->ctree));

  /* inbox */
  text[0] = "Inbox";
  ctnode = gtk_ctree_insert_node (mblw->ctree, mblw->parent, NULL, text, 5, inboxpix,
			     inbox_mask, inboxpix, inbox_mask, FALSE, TRUE);
  gtk_ctree_node_set_row_data (mblw->ctree, ctnode, balsa_app.inbox);

  /* outbox */
  text[0] = "Outbox";
  ctnode = gtk_ctree_insert_node (mblw->ctree, mblw->parent, NULL, text, 5, outboxpix,
			  outbox_mask, outboxpix, outbox_mask, FALSE, TRUE);
  gtk_ctree_node_set_row_data (mblw->ctree, ctnode, balsa_app.outbox);

  /* inbox */
  text[0] = "Trash";
  ctnode = gtk_ctree_insert_node (mblw->ctree, mblw->parent, NULL, text, 5, trashpix,
			     trash_mask, trashpix, trash_mask, FALSE, TRUE);
  gtk_ctree_node_set_row_data (mblw->ctree, ctnode, balsa_app.trash);

  if (balsa_app.mailbox_nodes)
    {
      GNode *walk;

      walk = g_node_last_child (balsa_app.mailbox_nodes);
      while (walk)
	{
	  gtk_ctree_insert_gnode (mblw->ctree, mblw->parent, NULL,
				  walk, mailbox_nodes_to_ctree, NULL);
	  walk = walk->prev;
	}
    }

  gtk_clist_thaw (GTK_CLIST (mblw->ctree));

  height = GTK_CLIST (mblw->ctree)->rows * GTK_CLIST (mblw->ctree)->row_height;
  if (height > 300)
    height = 300;
  gtk_widget_set_usize (GTK_WIDGET (mblw->ctree), -1, height);

  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "tree_select_row",
		      (GtkSignalFunc) mailbox_select_cb,
		      (gpointer) NULL);

  gtk_signal_connect (GTK_OBJECT (GTK_WIDGET (GTK_CLIST (mblw->ctree))),
		      "button_press_event",
		      (GtkSignalFunc) button_event_press_cb,
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

static gboolean
mailbox_nodes_to_ctree (GtkCTree * ctree,
			guint depth,
			GNode * gnode,
			GtkCTreeNode * cnode,
			gpointer data)
{
  MailboxNode *mbnode;

  if (!gnode || (!(mbnode = gnode->data)))
    return FALSE;

  if (mbnode->mailbox)
    {
      if (mbnode->mailbox->type == MAILBOX_IMAP)
	{
	  gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
				   NULL, NULL,
				   NULL, NULL,
				   G_NODE_IS_LEAF (gnode), TRUE);
	  gtk_ctree_node_set_row_data (ctree, cnode, mbnode->mailbox);
	}
      else if (mbnode->mailbox && mbnode->name)
	{
	  if (mbnode->mailbox->type == MAILBOX_MH ||
	      mbnode->mailbox->type == MAILBOX_MAILDIR)
	    {
	      gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
				       NULL, NULL,
				       NULL, NULL,
				       G_NODE_IS_LEAF (gnode), TRUE);
	      gtk_ctree_node_set_row_data (ctree, cnode, mbnode->mailbox);
	    }
	  else
	    {
	      /* normal mailbox */
	      add_mailboxes_for_checking(mbnode->mailbox);
	      if (mailbox_have_new_messages (MAILBOX_LOCAL (mbnode->mailbox)->path))
		gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
					 NULL, NULL,
					 tray_full, tray_full_mask,
					 FALSE, TRUE);
	      else
		gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
					 NULL, NULL,
					 tray_empty, tray_empty_mask,
					 FALSE, TRUE);

	      gtk_ctree_node_set_row_data (ctree, cnode, mbnode->mailbox);
	    }
	}
    }
  if (mbnode->name && !mbnode->mailbox)
    {
      /* new directory, but not a mailbox */
      gtk_ctree_set_node_info (ctree, cnode, g_basename (mbnode->name), 5,
			       closed_folder, closed_mask,
			       open_folder, open_mask,
			       G_NODE_IS_LEAF (gnode), TRUE);
    }
  return TRUE;
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
  mailbox = gtk_ctree_node_get_row_data (mblw->ctree, ctnode);

  if (!mailbox)
    return;

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
  mailbox = gtk_ctree_node_get_row_data (mblw->ctree, ctnode);

  if (mailbox)
    {
      child = gnome_mdi_find_child (mblw->mdi, mailbox->name);
      if (child)
	gnome_mdi_remove_child (mblw->mdi, child, TRUE);
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
      mailbox = gtk_ctree_node_get_row_data (ctree, row);

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
    }
}

static void
button_event_press_cb (GtkCList * clist, GdkEventButton * event, gpointer data)
{
  gint row, column;
  Mailbox *mailbox;

  if (event->window != clist->clist_window)
    return;

  if (!event || event->button != 3)
    return;

  gtk_clist_get_selection_info (clist, event->x, event->y, &row, &column);
  mailbox = gtk_clist_get_row_data (clist, row);

  gtk_clist_select_row (clist, row, -1);

  gtk_menu_popup (GTK_MENU (create_menu (GTK_CTREE (clist), mailbox)), NULL, NULL, NULL, NULL, event->button, event->time);
}

static void
mb_conf_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mailbox_conf_new (mailbox);
}

static GtkWidget *
create_menu (GtkCTree * ctree, Mailbox * mailbox)
{
  GtkWidget *menu, *menuitem;
  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label (_ ("Add Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_conf_cb), NULL);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show(menuitem);
  menuitem = gtk_menu_item_new_with_label (_ ("Edit Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_conf_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show(menuitem);

  return menu;
}
