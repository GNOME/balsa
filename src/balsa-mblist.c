/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Jay Painter and Stuart Parmenter
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

/* object arguments */
enum
{
  ARG_0,
  ARG_SHOW_CONTENT_INFO
};


static gint balsa_mblist_signals[LAST_SIGNAL] = {0};

static void select_mailbox(GtkCTree * ctree, GtkCTreeNode * row, gint column);
static void button_event_press_cb(GtkCTree * ctree, GdkEventButton * event,
				  gpointer data);

static BalsaMBListClass *parent_class = NULL;


/* callbacks */
static gboolean mailbox_nodes_to_ctree(GtkCTree *, guint, GNode *,
				       GtkCTreeNode *, gpointer);
static void balsa_mblist_class_init(BalsaMBListClass * class);
static void balsa_mblist_init(BalsaMBList * tree);
static void balsa_mblist_set_arg(GtkObject * object, GtkArg * arg, guint arg_id);
static void balsa_mblist_get_arg(GtkObject * object, GtkArg * arg, guint arg_id);


static void mailbox_tree_expand(GtkCTree *, GtkCTreeNode *, gpointer);
static void mailbox_tree_collapse(GtkCTree *, GtkCTreeNode *, gpointer);

guint
balsa_mblist_get_type(void)
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
    
    mblist_type = gtk_type_unique (gtk_ctree_get_type(), &mblist_info);
  }
  return mblist_type;
}

GtkWidget *
balsa_mblist_new ()
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

  /* this happens too late.. so these are set to 1x1 */
  /* PKGW: ... so 1x1 is the dimension that gets saved on exit. No.
   * balsa_app.mblist_width = GTK_WIDGET(del)->allocation.width;
   * balsa_app.mblist_height = GTK_WIDGET(del)->allocation.height;
   */

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(del));
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
  object_class->set_arg = balsa_mblist_set_arg;
  object_class->get_arg = balsa_mblist_get_arg;


  gtk_object_add_arg_type ("BalsaMBList::show_content_info", GTK_TYPE_BOOL,
			   GTK_ARG_READWRITE, ARG_SHOW_CONTENT_INFO);
  klass->select_mailbox = NULL;
}


static void
balsa_mblist_set_arg (GtkObject * object, GtkArg * arg, guint arg_id)
{
#ifdef BALSA_SHOW_INFO
  BalsaMBList *bmbl;

  bmbl = BALSA_MBLIST (object);

  switch (arg_id)
  {
  case ARG_SHOW_CONTENT_INFO:
    bmbl->display_content_info = GTK_VALUE_BOOL (*arg);
    balsa_mblist_redraw (bmbl);
    
    break;
    
  default:
    break;
  }
#endif
}

static void
balsa_mblist_get_arg (GtkObject * object, GtkArg * arg, guint arg_id)
{
#ifdef BALSA_SHOW_INFO
  BalsaMBList *bmbl;

  bmbl = BALSA_MBLIST (object);


  switch (arg_id)
  {
  case ARG_SHOW_CONTENT_INFO:
    GTK_VALUE_BOOL (*arg) = bmbl->display_content_info;
    break;
    
  default:
    break;
    
  }
#endif
}


static void
balsa_mblist_init (BalsaMBList * tree)
{
  char *titles[3] =
  {"mailbox", "unread", "total"};
  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());
#ifdef BALSA_SHOW_INFO
  gtk_ctree_construct (GTK_CTREE (tree), 3, 0, titles);
#else
  gtk_ctree_construct (GTK_CTREE (tree), 1, 0, titles);
#endif
#ifdef BALSA_SHOW_INFO
  if (tree->display_content_info)
    gtk_clist_column_titles_show (GTK_CLIST (tree));
  else
#endif
	  /* we want this on by default */
    gtk_clist_column_titles_hide (GTK_CLIST (tree));

  gtk_widget_pop_colormap ();
  gtk_widget_pop_visual ();

  gtk_signal_connect (GTK_OBJECT (tree), "tree_expand",
		      GTK_SIGNAL_FUNC (mailbox_tree_expand), NULL);
  gtk_signal_connect (GTK_OBJECT (tree), "tree_collapse",
		      GTK_SIGNAL_FUNC (mailbox_tree_collapse), NULL);

  gtk_ctree_set_show_stub (GTK_CTREE (tree), FALSE);
  gtk_ctree_set_line_style (GTK_CTREE (tree), GTK_CTREE_LINES_DOTTED);
  gtk_ctree_set_expander_style (GTK_CTREE (tree), GTK_CTREE_EXPANDER_CIRCULAR);
  gtk_clist_set_row_height (GTK_CLIST (tree), 16);
  gtk_clist_set_column_width (GTK_CLIST (tree), 0, 80);
#ifdef BALSA_SHOW_INFO
  if (tree->display_content_info)
    {
      gtk_clist_set_column_width (GTK_CLIST (tree), 1, 45);
      gtk_clist_set_column_width (GTK_CLIST (tree), 2, 45);
    }
  else
    {
      gtk_clist_set_column_visibility (GTK_CLIST (tree), 1, FALSE);
      gtk_clist_set_column_visibility (GTK_CLIST (tree), 2, FALSE);
    }
#endif

  gtk_signal_connect (GTK_OBJECT (tree), "tree_select_row",
		      GTK_SIGNAL_FUNC (select_mailbox),
		      (gpointer) NULL);

  gtk_signal_connect (GTK_OBJECT (tree),
		      "button_press_event",
		      (GtkSignalFunc) button_event_press_cb,
		      (gpointer) NULL);

  balsa_mblist_redraw (tree);
}

static void
balsa_mblist_insert_mailbox (BalsaMBList * mblist,
			     Mailbox * mailbox,
			     BalsaIconName icon)
{
  GtkCTreeNode *ctnode;
  gchar *text[1];
  text[0] = mailbox->name;
  ctnode = gtk_ctree_insert_node (GTK_CTREE (mblist),
				  NULL, NULL, text, 5,
				  balsa_icon_get_pixmap (icon),
				  balsa_icon_get_bitmap (icon),
				  NULL, NULL,
				  FALSE, FALSE);
  gtk_ctree_node_set_row_data (GTK_CTREE (mblist), ctnode, mailbox);
#ifdef BALSA_SHOW_INFO
  if (mblist->display_content_info)
    {
      mailbox_gather_content_info (balsa_app.trash);
//      g_snprintf (text[1], INFO_FIELD_LENGTH, "%ld", (balsa_app.trash)->unread_messages);
//      g_snprintf (text[2], INFO_FIELD_LENGTH, "%ld", (balsa_app.trash)->total_messages);
//      balsa_mblist_add_watched_mailbox (mblist, balsa_app.trash);
    }
#endif
}

void
balsa_mblist_redraw (BalsaMBList * bmbl)
{
  GtkCTree *ctree;

  if (!BALSA_IS_MBLIST (bmbl))
    return;

  ctree = GTK_CTREE (bmbl);

  gtk_clist_freeze (GTK_CLIST (ctree));

  gtk_clist_clear (GTK_CLIST (ctree));

#ifdef BALSA_SHOW_INFO
  if (bmbl->display_content_info)
    {
      gtk_clist_column_titles_show (GTK_CLIST (ctree));
      gtk_clist_set_column_visibility (GTK_CLIST (ctree), 1, TRUE);
      gtk_clist_set_column_visibility (GTK_CLIST (ctree), 2, TRUE);
    }
  else
    {
      gtk_clist_column_titles_hide (GTK_CLIST (ctree));
      gtk_clist_set_column_visibility (GTK_CLIST (ctree), 1, FALSE);
      gtk_clist_set_column_visibility (GTK_CLIST (ctree), 2, FALSE);
    }
#endif

  balsa_mblist_insert_mailbox (bmbl, balsa_app.inbox, BALSA_ICON_INBOX);
  balsa_mblist_insert_mailbox (bmbl, balsa_app.outbox, BALSA_ICON_OUTBOX);
  balsa_mblist_insert_mailbox (bmbl, balsa_app.sentbox, BALSA_ICON_TRAY_EMPTY);
  balsa_mblist_insert_mailbox (bmbl, balsa_app.draftbox, BALSA_ICON_TRAY_EMPTY);
  balsa_mblist_insert_mailbox (bmbl, balsa_app.trash, BALSA_ICON_TRASH);

  if (balsa_app.mailbox_nodes)
  {
    GNode *walk;

    walk = g_node_last_child (balsa_app.mailbox_nodes);
    while (walk)
    {
      gtk_ctree_insert_gnode (ctree, NULL, NULL, walk, mailbox_nodes_to_ctree, NULL);
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
			       FALSE,
			       FALSE);
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
				 G_NODE_IS_LEAF (gnode),
				 mbnode->expanded);
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
	  gdk_font_unref(style->font);
	  font = gdk_font_load
	    ("-adobe-courier-medium-r-*-*-*-120-*-*-*-*-iso8859-1");
	  style->font = font;
	  gdk_font_ref(style->font);
	  
	  gtk_ctree_node_set_row_style (ctree, cnode, style);
	  gtk_ctree_set_node_info (ctree, cnode,
				   mbnode->mailbox->name, 5,
				   balsa_icon_get_pixmap(BALSA_ICON_TRAY_FULL),
				   balsa_icon_get_bitmap(BALSA_ICON_TRAY_FULL),
				   NULL, NULL,
				   FALSE,
				   FALSE);
	}
	else
	{
	  gtk_ctree_set_node_info (ctree, cnode,
				   mbnode->mailbox->name, 5,
				   balsa_icon_get_pixmap(BALSA_ICON_TRAY_EMPTY),
				   balsa_icon_get_bitmap(BALSA_ICON_TRAY_EMPTY),
				   NULL, NULL,
				   FALSE,
				   FALSE);
	}
	
	gtk_ctree_node_set_row_data(ctree, cnode, mbnode->mailbox);
	
      }
    }
  }
  if (mbnode->name && !mbnode->mailbox)
  {
    /* new directory, but not a mailbox */
    gtk_ctree_set_node_info (ctree, cnode, g_basename (mbnode->name), 5,
			     balsa_icon_get_pixmap(BALSA_ICON_DIR_CLOSED),
			     balsa_icon_get_bitmap(BALSA_ICON_DIR_CLOSED),
			     balsa_icon_get_pixmap(BALSA_ICON_DIR_OPEN),
			     balsa_icon_get_bitmap(BALSA_ICON_DIR_OPEN),
			     G_NODE_IS_LEAF (gnode),
			     mbnode->expanded);
    gtk_ctree_node_set_row_data (ctree, cnode, mbnode);
  }
  return TRUE;
}

static void
button_event_press_cb (GtkCTree * ctree, GdkEventButton * event, gpointer data)
{
  gint row, column;
  GtkObject *data;
  GtkCTreeNode *ctrow;

  if (!event || event->button != 3)
    return;

  if (event->button == 1 && event->type == GDK_2BUTTON_PRESS)
  {
    gtk_clist_get_selection_info (GTK_CLIST (ctree), event->x, event->y,
				  &row, &column);
    ctrow = gtk_ctree_node_nth (ctree, row);
    data = gtk_ctree_node_get_row_data (ctree, ctrow);
    
    gtk_ctree_select (ctree, ctrow);
    
    if (!BALSA_IS_MAILBOX(data))
      return;
    
    if (data)
      gtk_signal_emit (GTK_OBJECT (BALSA_MBLIST (ctree)),
		       balsa_mblist_signals[SELECT_MAILBOX],
		       data,
		       ctrow,
		       event);
  }
}


static void
select_mailbox (GtkCTree * ctree, GtkCTreeNode * row, gint column)
{
  BalsaMBList *bmbl;
  GdkEventButton *bevent = (GdkEventButton *) gtk_get_current_event ();
  GtkObject *data;

  bmbl = BALSA_MBLIST(ctree);

  data = gtk_ctree_node_get_row_data (ctree, row);

  if (!BALSA_IS_MAILBOX(data))
    return;

  if (bevent && bevent->button == 1)
  {
    if (data)
      gtk_signal_emit(GTK_OBJECT (bmbl),
		      balsa_mblist_signals[SELECT_MAILBOX],
		      data,
		      row,
		      bevent);
  }
}

static void
mailbox_tree_expand (GtkCTree * ctree, GtkCTreeNode * node, gpointer data)
{
  MailboxNode *mbnode;
  mbnode = gtk_ctree_node_get_row_data (ctree, node);
  mbnode->expanded = TRUE;
}

static void
mailbox_tree_collapse (GtkCTree * ctree, GtkCTreeNode * node, gpointer data)
{
  MailboxNode *mbnode;
  mbnode = gtk_ctree_node_get_row_data (ctree, node);
  mbnode->expanded = FALSE;
}


