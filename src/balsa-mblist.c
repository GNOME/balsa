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
static void balsa_mblist_release_watchers (BalsaMBList *bmbl);
static void balsa_mblist_add_watched_mailbox (BalsaMBList *bmbl, Mailbox *mailbox);

static BalsaMBListClass *parent_class = NULL;


/* callbacks */
static gboolean mailbox_nodes_to_ctree (GtkCTree *, guint, GNode *, GtkCTreeNode *, gpointer);
static void balsa_mblist_class_init (BalsaMBListClass * class);
static void balsa_mblist_init (BalsaMBList * tree);

static void mailbox_tree_expand (GtkCTree *, GtkCTreeNode *, gpointer);
static void mailbox_tree_collapse (GtkCTree *, GtkCTreeNode *, gpointer);
static void mailbox_listener (MailboxWatcherMessage * mw_message);

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
  balsa_mblist_release_watchers (del);
  
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
  char *titles[3]={"mailbox", "unread", "total"};
  gtk_widget_push_visual (gdk_imlib_get_visual ());
  gtk_widget_push_colormap (gdk_imlib_get_colormap ());
  
  gtk_ctree_construct (GTK_CTREE (tree), 3, 0, titles);

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
  gtk_clist_set_column_width (GTK_CLIST (tree), 1, 45);
  gtk_clist_set_column_width (GTK_CLIST (tree), 2, 45);

  gtk_signal_connect (GTK_OBJECT (tree), "tree_select_row",
		      GTK_SIGNAL_FUNC (select_mailbox),
		      (gpointer) NULL);

  /*gtk_signal_connect (GTK_OBJECT (tree),
		      "button_press_event",
		      (GtkSignalFunc) button_event_press_cb,
		      (gpointer) NULL);*/

  balsa_mblist_redraw (tree);
}


static void
balsa_mblist_add_watched_mailbox (BalsaMBList *bmbl, Mailbox *mailbox)
{
  bmbl->watched_mailbox = g_list_append(  bmbl->watched_mailbox, mailbox );
  mailbox_watcher_set (mailbox,
		       (MailboxWatcherFunc) mailbox_listener,
		       MESSAGE_MARK_READ_MASK |
		       MESSAGE_MARK_UNREAD_MASK |
		       MESSAGE_MARK_DELETE_MASK |
		       MESSAGE_MARK_UNDELETE_MASK |
		       MESSAGE_DELETE_MASK |
		       MESSAGE_NEW_MASK |
		       MESSAGE_APPEND_MASK,
		       (gpointer) bmbl );
}


static void
balsa_mblist_release_watchers (BalsaMBList *bmbl)
{
  GList *wmb;
  GList *new_wmb;
  guint nb_wmb;
  guint cur_mb_num;
  Mailbox *cur_mb;
  
  new_wmb = bmbl->watched_mailbox;
  if (!new_wmb) return;
  nb_wmb = g_list_length(new_wmb);
  for (cur_mb_num=0; cur_mb_num<nb_wmb; cur_mb_num++)
    {
      wmb = new_wmb;
      cur_mb = wmb->data;
      mailbox_watcher_remove_by_data(cur_mb, (gpointer) bmbl);
      new_wmb=wmb->next;
    }

  g_list_free(bmbl->watched_mailbox);
  bmbl->watched_mailbox = NULL;
}

#define INFO_FIELD_LENGTH 6
static void 
balsa_mblist_set_row_info_fields (BalsaMBList *bmbl, GtkCTreeNode *cnode, Mailbox *mailbox)
{
  gchar *info_field_text;
  GtkCTree *ctree;

  info_field_text = g_new(gchar, INFO_FIELD_LENGTH);
  ctree = GTK_CTREE (bmbl);

  g_snprintf( info_field_text, INFO_FIELD_LENGTH, "%ld", mailbox->unread_messages );
  gtk_ctree_node_set_text (ctree, cnode, 1, info_field_text);

  g_snprintf( info_field_text, INFO_FIELD_LENGTH, "%ld", mailbox->total_messages );
  gtk_ctree_node_set_text (ctree, cnode, 2, info_field_text);

  g_free(info_field_text);
}

void
balsa_mblist_redraw (BalsaMBList * bmbl)
{
  GtkCTreeNode *ctnode;
  gchar *text[3];
  GtkCTree *ctree;
  
  if (!BALSA_IS_MBLIST (bmbl))
    return;

  ctree = GTK_CTREE (bmbl);

  gtk_clist_clear (GTK_CLIST (ctree));
  balsa_mblist_release_watchers (bmbl);

  gtk_clist_freeze (GTK_CLIST (ctree));

  text[1] = g_new(gchar, INFO_FIELD_LENGTH);
  text[2] = g_new(gchar, INFO_FIELD_LENGTH);
  /* inbox */
  text[0] = "Inbox";
  mailbox_gather_content_info( balsa_app.inbox );
  g_snprintf( text[1], INFO_FIELD_LENGTH, "%ld",  (balsa_app.inbox)->unread_messages );
  g_snprintf( text[2], INFO_FIELD_LENGTH, "%ld",  (balsa_app.inbox)->total_messages );
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5,
				  balsa_icon_get_pixmap (BALSA_ICON_INBOX),
				  balsa_icon_get_bitmap (BALSA_ICON_INBOX),
				  NULL, NULL,
				  FALSE, FALSE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.inbox);
  balsa_mblist_add_watched_mailbox(bmbl, balsa_app.inbox);

  /* outbox */
  text[0] = "Outbox";
  mailbox_gather_content_info( balsa_app.outbox );
  g_snprintf( text[1], INFO_FIELD_LENGTH, "%ld",  (balsa_app.outbox)->unread_messages );
  g_snprintf( text[2], INFO_FIELD_LENGTH, "%ld",  (balsa_app.outbox)->total_messages );
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5,
				  balsa_icon_get_pixmap (BALSA_ICON_OUTBOX),
				  balsa_icon_get_bitmap (BALSA_ICON_OUTBOX),
				  NULL, NULL,
				  FALSE, FALSE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.outbox);
  balsa_mblist_add_watched_mailbox(bmbl, balsa_app.outbox);

  /* trash */
  text[0] = "Trash";
  mailbox_gather_content_info( balsa_app.trash );
  g_snprintf( text[1], INFO_FIELD_LENGTH, "%ld",  (balsa_app.trash)->unread_messages );
  g_snprintf( text[2], INFO_FIELD_LENGTH, "%ld",  (balsa_app.trash)->total_messages );
  ctnode = gtk_ctree_insert_node (ctree, NULL, NULL, text, 5,
				  balsa_icon_get_pixmap (BALSA_ICON_TRASH),
				  balsa_icon_get_bitmap (BALSA_ICON_TRASH),
				  NULL, NULL,
				  FALSE, FALSE);
  gtk_ctree_node_set_row_data (ctree, ctnode, balsa_app.trash);
  balsa_mblist_add_watched_mailbox(bmbl, balsa_app.trash);


  g_free( text[1] );
  g_free( text[2] );
  
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
  gchar *info_field_text;
  
  info_field_text = g_new(gchar, INFO_FIELD_LENGTH);
  
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

		  font = gdk_font_load ("-adobe-courier-medium-r-*-*-*-120-*-*-*-*-iso8859-1");
		  style->font = font;

		  gtk_ctree_node_set_row_style (ctree, cnode, style);
		  gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
			       balsa_icon_get_pixmap (BALSA_ICON_TRAY_FULL),
			       balsa_icon_get_bitmap (BALSA_ICON_TRAY_FULL),
					   NULL, NULL,
					   FALSE,
					   FALSE);
		}
	      else
		{
		  gtk_ctree_set_node_info (ctree, cnode,mbnode->mailbox->name, 5,
					   balsa_icon_get_pixmap (BALSA_ICON_TRAY_EMPTY),
					   balsa_icon_get_bitmap (BALSA_ICON_TRAY_EMPTY),
					   NULL, NULL,
					   FALSE,
					   FALSE);
		}
	      /* get and display the information fields for this mailbox */
	      mailbox_gather_content_info(mbnode->mailbox);
	      balsa_mblist_set_row_info_fields (BALSA_MBLIST(ctree), cnode, mbnode->mailbox);
	      gtk_ctree_node_set_row_data (ctree, cnode, mbnode->mailbox);
	      balsa_mblist_add_watched_mailbox(BALSA_MBLIST(ctree), mbnode->mailbox);

      
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
			       G_NODE_IS_LEAF (gnode),
			       mbnode->expanded);
      gtk_ctree_node_set_row_data (ctree, cnode, mbnode);
    }
  g_free( info_field_text );
  return TRUE;
}

static void
button_event_press_cb (GtkCList * clist, GdkEventButton * event, gpointer data)
{
  gint row, column;
  Mailbox *mailbox;
  GtkCTreeNode *ctrow;
  printf("bim bim\n");
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


/**
 * mailbox_listener:
 * @mw_message: data field sent by the watched mailbox
 *
 * listen for mailbox messages and react accordingly 
 */
static void
mailbox_listener (MailboxWatcherMessage * mw_message)
{
  BalsaMBList *mbl = (BalsaMBList *)mw_message->data;
  GtkCTree *ctree;
  Mailbox *sender_mailbox;
  GtkCTreeNode *cnode;

  /* retrieve the mailbox who sent the message */
  sender_mailbox = mw_message->mailbox;
  ctree = GTK_CTREE (mbl);
  /* find the row concerned by this message */
  cnode = gtk_ctree_find_by_row_data ( ctree, NULL, sender_mailbox);
  /* the row may not be found (for example when the mblist 
     ctree is not constructed yet), in this case exit */
  if (!cnode) return;
  
  switch (mw_message->type)
    {
    case MESSAGE_MARK_READ:
    case MESSAGE_MARK_UNREAD:
    case MESSAGE_MARK_DELETE:
    case MESSAGE_MARK_UNDELETE:
    case MESSAGE_NEW:
    case MESSAGE_DELETE:
    case MESSAGE_APPEND:
      balsa_mblist_set_row_info_fields (mbl, cnode, sender_mailbox);

      break;     
    default:
      break;
    }
}



