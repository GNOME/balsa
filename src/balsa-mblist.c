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
#include "balsa-icons.h"
#include "balsa-mblist.h"
#include "misc.h"

enum
  {
    SELECT_MAILBOX,
    LAST_SIGNAL
  };

/* marshallers */
typedef void (*BalsaMBListSignal1) (GtkObject * object,
				    Message * message,
				    GtkCTreeNode * row,
				    GdkEventButton * bevent,
				    gpointer data);

static gint balsa_mblist_signals[LAST_SIGNAL] =
{0};

static void select_mailbox (GtkCTree * ctree, GtkCTreeNode * row, gint column);
static void button_event_press_cb (GtkCList * clist, GdkEventButton * event, gpointer data);


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
balsa_mblist_class_init (BalsaMBListClass * klass)
{
  GtkObjectClass *object_class;
  GtkCTreeClass *tree_class;

  object_class = (GtkObjectClass *) klass;
  tree_class = GTK_CTREE_CLASS (klass);

  balsa_mblist_signals[SELECT_MAILBOX] =
    gtk_signal_new ("select_mailbox",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (BalsaMBListClass, select_mailbox),
		    gtk_marshal_NONE__POINTER_POINTER_POINTER,
		    GTK_TYPE_NONE, 3, GTK_TYPE_POINTER,
		    GTK_TYPE_POINTER, GTK_TYPE_GDK_EVENT);
  gtk_object_class_add_signals (object_class, balsa_mblist_signals, LAST_SIGNAL);

  object_class->destroy = balsa_mblist_destroy;
  parent_class = gtk_type_class (gtk_ctree_get_type ());

  klass->select_mailbox = NULL;
}

static void
balsa_mblist_init (BalsaMBList * tree)
{
  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());

  gtk_ctree_construct (GTK_CTREE (tree), 1, 0, NULL);

  gtk_widget_pop_colormap ();
  gtk_widget_pop_visual ();

  gtk_ctree_set_show_stub (GTK_CTREE (tree), FALSE);
  gtk_ctree_set_line_style (GTK_CTREE (tree), GTK_CTREE_LINES_DOTTED);
  gtk_ctree_set_expander_style (GTK_CTREE (tree), GTK_CTREE_EXPANDER_CIRCULAR);
  gtk_clist_set_row_height (GTK_CLIST (tree), 16);

  gtk_signal_connect (GTK_OBJECT (tree), "tree_select_row",
		      GTK_SIGNAL_FUNC (select_mailbox),
		      (gpointer) NULL);

  gtk_signal_connect (GTK_OBJECT (tree),
		      "button_press_event",
		      (GtkSignalFunc) button_event_press_cb,
		      (gpointer) NULL);

  balsa_mblist_redraw (tree);
}

void
balsa_mblist_redraw (BalsaMBList * bmbl)
{
  GtkCTreeNode *ctnode;
  gchar *text[1];
  GtkCTree *ctree;

  if (!BALSA_IS_MBLIST (bmbl))
    return;

  ctree = GTK_CTREE (bmbl);

  gtk_clist_clear (GTK_CLIST (ctree));

  gtk_clist_freeze (GTK_CLIST (ctree));

  /* inbox */
  text[0] = "Inbox";
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5,
				  balsa_icon_get_pixmap (BALSA_ICON_INBOX),
				  balsa_icon_get_bitmap (BALSA_ICON_INBOX),
				  NULL, NULL,
				  FALSE, FALSE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.inbox);

  /* outbox */
  text[0] = "Outbox";
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5,
				  balsa_icon_get_pixmap (BALSA_ICON_OUTBOX),
				  balsa_icon_get_bitmap (BALSA_ICON_OUTBOX),
				  NULL, NULL,
				  FALSE, FALSE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.outbox);

  /* trash */
  text[0] = "Trash";
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5,
				  balsa_icon_get_pixmap (BALSA_ICON_TRASH),
				  balsa_icon_get_bitmap (BALSA_ICON_TRASH),
				  NULL, NULL,
				  FALSE, FALSE);
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

		  style = gtk_style_copy (gtk_widget_get_style (GTK_WIDGET (ctree)));

		  font = gdk_font_load ("-adobe-courier-medium-r-*-*-*-120-*-*-*-*-iso8859-1");
		  style->font = font;

		  gtk_ctree_node_set_row_style (ctree, cnode, style);

		  gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
					   NULL, NULL,
			       balsa_icon_get_pixmap (BALSA_ICON_TRAY_FULL),
			       balsa_icon_get_bitmap (BALSA_ICON_TRAY_FULL),
					   FALSE, TRUE);
		}
	      else
		{
		  gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
					   NULL, NULL,
			      balsa_icon_get_pixmap (BALSA_ICON_TRAY_EMPTY),
			      balsa_icon_get_bitmap (BALSA_ICON_TRAY_EMPTY),
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
			       balsa_icon_get_pixmap (BALSA_ICON_DIR_CLOSED),
			       balsa_icon_get_bitmap (BALSA_ICON_DIR_CLOSED),
			       balsa_icon_get_pixmap (BALSA_ICON_DIR_OPEN),
			       balsa_icon_get_bitmap (BALSA_ICON_DIR_OPEN),
			       G_NODE_IS_LEAF (gnode), TRUE);
    }
  return TRUE;
}

static void
button_event_press_cb (GtkCList * clist, GdkEventButton * event, gpointer data)
{
  gint row, column;
  Mailbox *mailbox;
  GtkCTreeNode *ctrow;

  if (event->window != clist->clist_window)
    return;

  if (!event || event->button != 3)
    return;

  gtk_clist_get_selection_info (clist, event->x, event->y, &row, &column);
  mailbox = gtk_clist_get_row_data (clist, row);

  gtk_clist_select_row (clist, row, -1);

  ctrow = gtk_ctree_find_by_row_data (GTK_CTREE (clist), NULL, mailbox);

  if (mailbox)
    gtk_signal_emit (GTK_OBJECT (clist),
		     balsa_mblist_signals[SELECT_MAILBOX],
		     mailbox,
		     ctrow,
		     event);
}


static void
select_mailbox (GtkCTree * ctree, GtkCTreeNode * row, gint column)
{
  BalsaMBList *bmbl;
  GdkEventButton *bevent = (GdkEventButton *) gtk_get_current_event ();
  Mailbox *mailbox;

  bmbl = BALSA_MBLIST (ctree);

  mailbox = gtk_ctree_node_get_row_data (ctree, row);

  if (bevent && bevent->button == 1)
    {

      if (mailbox)
	gtk_signal_emit (GTK_OBJECT (bmbl),
			 balsa_mblist_signals[SELECT_MAILBOX],
			 mailbox,
			 row,
			 bevent);
    }
}
