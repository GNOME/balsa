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
#include "balsa-mblist.h"
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


static BalsaMBListClass *parent_class = NULL;
/* callbacks */
static gboolean mailbox_nodes_to_ctree (GtkCTree *, guint, GNode *, GtkCTreeNode *, gpointer);
static void balsa_mblist_class_init (BalsaMBListClass * class);
static void balsa_mblist_init (BalsaMBList * tree);

guint
balsa_mblist_get_type (void)
{
  static guint mblist_type = 0;

  if (!mblist_type)
    {

      GtkTypeInfo mblist_info =
      {
	"BalsaMBList",
	sizeof (BalsaMBList),
	sizeof (BalsaMBListClass),
	(GtkClassInitFunc) balsa_mblist_class_init,
	(GtkObjectInitFunc) balsa_mblist_init,
	(GtkArgSetFunc) NULL,
	(GtkArgGetFunc) NULL,
      };

      mblist_type = gtk_type_unique (gtk_ctree_get_type (), &mblist_info);
    }
  return mblist_type;
}

GtkWidget *
balsa_mblist_new (void)
{
  BalsaMBList *new;

  new = gtk_type_new (balsa_mblist_get_type ());

  return GTK_WIDGET (new);
}

static void
balsa_mblist_destroy (GtkObject * obj)
{
  BalsaMBList *del;

  del = BALSA_MBLIST (obj);

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (*GTK_OBJECT_CLASS (parent_class)->destroy) (GTK_OBJECT (del));
}


static void
balsa_mblist_class_init (BalsaMBListClass * class)
{
  GtkObjectClass *object_class;
  GtkCTreeClass *tree_class;

  object_class = (GtkObjectClass *) class;
  tree_class = GTK_CTREE_CLASS (class);

  object_class->destroy = balsa_mblist_destroy;

  parent_class = gtk_type_class (gtk_ctree_get_type ());
}

static void
balsa_mblist_init (BalsaMBList * tree)
{
  GdkImlibImage *im;

  im = gdk_imlib_create_image_from_xpm_data (mini_dir_open_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  open_folder = gdk_imlib_copy_image (im);
  open_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  im = gdk_imlib_create_image_from_xpm_data (mini_dir_closed_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  closed_folder = gdk_imlib_copy_image (im);
  closed_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  im = gdk_imlib_create_image_from_xpm_data (plain_folder_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  tray_empty = gdk_imlib_copy_image (im);
  tray_empty_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  im = gdk_imlib_create_image_from_xpm_data (full_folder_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  tray_full = gdk_imlib_copy_image (im);
  tray_full_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  im = gdk_imlib_create_image_from_xpm_data (inbox_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  inboxpix = gdk_imlib_copy_image (im);
  inbox_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  im = gdk_imlib_create_image_from_xpm_data (outbox_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  outboxpix = gdk_imlib_copy_image (im);
  outbox_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  im = gdk_imlib_create_image_from_xpm_data (trash_xpm);
  gdk_imlib_render (im, im->rgb_width, im->rgb_height);
  trashpix = gdk_imlib_copy_image (im);
  trash_mask = gdk_imlib_copy_mask (im);
  gdk_imlib_destroy_image (im);

  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());

  gtk_ctree_construct (GTK_CTREE(tree), 1, 0, NULL);

  gtk_widget_pop_colormap ();
  gtk_widget_pop_visual ();

  gtk_ctree_show_stub (GTK_CTREE (tree), FALSE);
  gtk_ctree_set_line_style (GTK_CTREE (tree), GTK_CTREE_LINES_DOTTED);
  gtk_clist_set_policy (GTK_CLIST (tree), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_clist_set_row_height (GTK_CLIST (tree), 16);

  balsa_mblist_redraw (tree);
}

void
balsa_mblist_redraw (BalsaMBList *bmbl)
{
  GtkCTreeNode *ctnode;
  gchar *text[1];
  GtkCTree *ctree;

  if (!BALSA_IS_MBLIST (bmbl))
    return;

  ctree = GTK_CTREE(bmbl);

  gtk_clist_freeze (GTK_CLIST (ctree));

  /* inbox */
  text[0] = "Inbox";
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5, inboxpix,
			     inbox_mask, inboxpix, inbox_mask, FALSE, TRUE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.inbox);

  /* outbox */
  text[0] = "Outbox";
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5, outboxpix,
			  outbox_mask, outboxpix, outbox_mask, FALSE, TRUE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.outbox);

  /* trash */
  text[0] = "Trash";
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5, trashpix,
			     trash_mask, trashpix, trash_mask, FALSE, TRUE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.trash);

  if (balsa_app.mailbox_nodes)
    {
      GNode *walk;

      walk = g_node_last_child (balsa_app.mailbox_nodes);
      while (walk)
	{
	  gtk_ctree_insert_gnode (ctree, NULL, NULL,
				  walk, mailbox_nodes_to_ctree, NULL);
	  walk = walk->prev;
	}
    }

  gtk_clist_thaw (GTK_CLIST (ctree));
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
      if (mbnode->mailbox->type == MAILBOX_POP3)
	return FALSE;
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
	      if (mailbox_have_new_messages (MAILBOX_LOCAL (mbnode->mailbox)->path))
		{
		  GdkFont *font;
		  GtkStyle *style;
		  style = gtk_style_new ();
		  font = gdk_font_load ("-adobe-courier-medium-r-*-*-*-120-*-*-*-*-iso8859-1");
		  style->font = font;

		  gtk_widget_set_style (GTK_CELL_WIDGET ((GTK_CTREE_ROW (cnode)->row).cell)->widget, style);

		  gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
					   NULL, NULL,
					   tray_full, tray_full_mask,
					   FALSE, TRUE);
		}
	      else
		{
		  gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
					   NULL, NULL,
					   tray_empty, tray_empty_mask,
					   FALSE, TRUE);
		}

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
