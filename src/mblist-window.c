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
#include <gtk/gtkfeatures.h>

#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "index-child.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "mblist-window.h"
#include "misc.h"

#include "pixmaps/plain-folder.xpm"
#include "pixmaps/trash.xpm"


static GdkPixmap *trashpix;
static GdkBitmap *trash_mask;
static GdkPixmap *tray_empty;
static GdkBitmap *tray_empty_mask;

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
static GtkWidget *create_menu (GtkCTree * ctree, Mailbox * mailbox);

static void open_cb (GtkWidget *, gpointer);
static void close_cb (GtkWidget *, gpointer);

void
mblist_open_window (GnomeMDI * mdi)
{
  GdkImlibImage *im;
  GtkWidget *bbox;
  GtkWidget *button;
  gint height;

  if (mblw)
    {
      gdk_window_raise (mblw->window->window);
      return;
    }

  mblw = g_malloc0 (sizeof (MBListWindow));

  im = gdk_imlib_create_image_from_xpm_data (plain_folder_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  tray_empty = gdk_imlib_copy_image (im);
  tray_empty_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  im = gdk_imlib_create_image_from_xpm_data (trash_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  trashpix = gdk_imlib_copy_image (im);
  trash_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);


  mblw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (mblw->window), "Mailboxes");

  gtk_widget_set_usize (GTK_WIDGET (mblw->window), balsa_app.mblist_width, balsa_app.mblist_height);
  gtk_window_set_policy (GTK_WINDOW (mblw->window), TRUE, TRUE, TRUE);

  mblw->mdi = mdi;
  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_mblist_window,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mblw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);

  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());

  mblw->ctree = GTK_CTREE (balsa_mblist_new ());
  balsa_app.mblist = BALSA_MBLIST (mblw->ctree);

  gtk_widget_pop_colormap ();
  gtk_widget_pop_visual ();

  gtk_ctree_show_stub (mblw->ctree, FALSE);
  gtk_ctree_set_line_style (mblw->ctree, GTK_CTREE_LINES_DOTTED);
  gtk_clist_set_policy (GTK_CLIST (mblw->ctree), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_clist_set_row_height (GTK_CLIST (mblw->ctree), 16);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mblw->window)->vbox), GTK_WIDGET (mblw->ctree), TRUE, TRUE, 0);
  gtk_widget_show (GTK_WIDGET (mblw->ctree));

  balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));

  height = GTK_CLIST (mblw->ctree)->rows * GTK_CLIST (mblw->ctree)->row_height;

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

  button = gtk_button_new_with_label ("Open box");
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (open_cb), NULL);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Close box");
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
  main_window_set_cursor (-1);
}

static void
close_cb (GtkWidget * widget, gpointer data)
{
  GtkCTreeNode *ctnode;
  Mailbox *mailbox;

  if (!GTK_CLIST (mblw->ctree)->selection)
    return;

  ctnode = GTK_CLIST (mblw->ctree)->selection->data;
  mailbox = gtk_ctree_node_get_row_data (mblw->ctree, ctnode);

  mblist_close_mailbox (mailbox);
}

void
mblist_close_mailbox (Mailbox * mailbox)
{
  GnomeMDIChild *child;

  if (!mblw)
    return;

  if (mailbox)
    {
      child = gnome_mdi_find_child (mblw->mdi, mailbox->name);
      if (child)
	{
	  mailbox_watcher_remove (mailbox, BALSA_INDEX (INDEX_CHILD (child)->index)->watcher_id);
	  gnome_mdi_remove_child (mblw->mdi, child, TRUE);
	}
    }
}


static void
close_mblist_window (GtkWidget * widget)
{
  if (!mblw)
    return;

  balsa_app.mblist_width = mblw->window->allocation.width;
  balsa_app.mblist_height = mblw->window->allocation.height;

  gtk_widget_destroy (mblw->window);
  gtk_widget_destroy (GTK_WIDGET (mblw->ctree));
}

static void
destroy_mblist_window (GtkWidget * widget)
{
  if (!mblw)
    return;

  close_mblist_window (widget);
  g_free (mblw);
  mblw = NULL;
  balsa_app.mblist = NULL;
}

static void
mailbox_select_cb (GtkCTree * ctree, GtkCTreeNode * row, gint column)
{
  IndexChild *index_child;
  Mailbox *mailbox;
  GdkEventButton *bevent = (GdkEventButton *) gtk_get_current_event ();

  if (!mblw)
    return;

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
      main_window_set_cursor (-1);

      gtk_ctree_set_node_info (ctree, row, mailbox->name, 5,
			       NULL, NULL,
			       tray_empty, tray_empty_mask,
			       FALSE, TRUE);
    }
}

static void
button_event_press_cb (GtkCList * clist, GdkEventButton * event, gpointer data)
{
  gint row, column;
  Mailbox *mailbox;

  if (!mblw)
    return;

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
  mailbox_conf_new (mailbox, FALSE, MAILBOX_UNKNOWN);
}

static void
mb_add_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mailbox_conf_new (mailbox, TRUE, MAILBOX_UNKNOWN);
}

static void
mb_del_cb (GtkWidget * wifget, Mailbox * mailbox)
{
  if (mailbox->type == MAILBOX_UNKNOWN)
    return;
  mailbox_conf_delete (mailbox);
}


static GtkWidget *
create_menu (GtkCTree * ctree, Mailbox * mailbox)
{
  GtkWidget *menu, *menuitem;

  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label (_ ("Add Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_add_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label (_ ("Edit Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_conf_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label (_ ("Delete Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_del_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  return menu;
}
