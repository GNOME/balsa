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
#include <string.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-mblist.h"
#include "misc.h"
#include "mailbox.h"


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
				  gpointer user_data);

static BalsaMBListClass *parent_class = NULL;


/* callbacks */
static gboolean mailbox_nodes_to_ctree(GtkCTree *, guint, GNode *,
				       GtkCTreeNode *, gpointer);
static void balsa_mblist_class_init(BalsaMBListClass * class);
static void balsa_mblist_init(BalsaMBList * tree);
static void balsa_mblist_set_arg(GtkObject * object, 
				 GtkArg * arg, 
				 guint arg_id);
static void balsa_mblist_get_arg(GtkObject * object, 
				 GtkArg * arg, 
				 guint arg_id);
static void balsa_mblist_column_resize (GtkCList * clist, gint column, 
					gint size, gpointer data);
static void balsa_mblist_column_click (GtkCList * clist, gint column, 
				       gpointer data);

/* static void mailbox_tree_expand(GtkCTree *, GtkCTreeNode *, gpointer); */
/* static void mailbox_tree_collapse(GtkCTree *, GtkCTreeNode *, gpointer); */

static void mailbox_tree_expand(GtkCTree *, GList *, gpointer);
static void mailbox_tree_collapse(GtkCTree *, GList *, gpointer);
static void balsa_mblist_check_new (GtkCTree *, GtkCTreeNode *, gpointer);
static void balsa_mblist_mailbox_style (GtkCTree *ctree, GtkCTreeNode *node, Mailbox *mailbox
#ifdef BALSA_SHOW_INFO
, gboolean display_info
#endif
);
#ifdef BALSA_SHOW_INFO
static gint numeric_compare (GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2);
#endif
static gint mblist_mbnode_compare (gconstpointer a, gconstpointer b);

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

/* Set up the mail box list, including the tree's appearance and the callbacks */
static void
balsa_mblist_init (BalsaMBList * tree)
{
  char *titles[3] = {"Mailbox", "Unread", "Total"};
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

  gtk_signal_connect (GTK_OBJECT (tree), "tree_expand",
		      GTK_SIGNAL_FUNC (mailbox_tree_expand), NULL);
  gtk_signal_connect (GTK_OBJECT (tree), "tree_collapse",
		      GTK_SIGNAL_FUNC (mailbox_tree_collapse), NULL);

  gtk_ctree_set_show_stub (GTK_CTREE (tree), FALSE);
  gtk_ctree_set_line_style (GTK_CTREE (tree), GTK_CTREE_LINES_DOTTED);
  gtk_ctree_set_expander_style (GTK_CTREE (tree), GTK_CTREE_EXPANDER_CIRCULAR);
  gtk_clist_set_row_height (GTK_CLIST (tree), 16);
  gtk_clist_set_column_width (GTK_CLIST (tree), 0, balsa_app.mblist_name_width);
#ifdef BALSA_SHOW_INFO
  if (tree->display_content_info)
    {
      gtk_clist_set_column_width (GTK_CLIST (tree), 1, balsa_app.mblist_newmsg_width);
      gtk_clist_set_column_width (GTK_CLIST (tree), 2, balsa_app.mblist_totalmsg_width);
      gtk_clist_set_sort_column (GTK_CLIST (tree),0);
      gtk_clist_set_sort_type (GTK_CLIST (tree), GTK_SORT_ASCENDING);
      gtk_clist_set_compare_func (GTK_CLIST (tree), NULL);
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

  gtk_signal_connect (GTK_OBJECT (tree),
		      "resize_column",
		      GTK_SIGNAL_FUNC (balsa_mblist_column_resize),
		      (gpointer) NULL);
  
  gtk_signal_connect (GTK_OBJECT (tree),
		      "click_column",
		      GTK_SIGNAL_FUNC (balsa_mblist_column_click),
		      (gpointer) tree);
  
  balsa_mblist_redraw (tree);
}

/* balsa_mblist_insert_mailbox 
 *   
 * mblist:  The mailbox list where the mailbox is to be inserted
 * mailbox:  The mailbox to be inserted
 * icon:  The icon to be shown next to the mailbox name
 *
 * Description: This function is for inserting one of the several main
 * mailboxes.  It get's inserted under the "Main Mailboxes" node, and a
 * mailbox watcher is added to the mailbox.  
 * */
static void
balsa_mblist_insert_mailbox (BalsaMBList * mblist,
			     Mailbox * mailbox,
			     BalsaIconName icon)
{
  GtkCTreeNode *ctnode;
  MailboxNode *mbnode;
  gchar *text[1];
  
  text[0] = mailbox->name;

#ifdef BALSA_SHOW_INFO
  if (mblist->display_content_info)
  {
    /* balsa_mblist_add_watched_mailbox (mblist, mailbox); */ 
  }
#endif

  ctnode = gtk_ctree_insert_node (GTK_CTREE (mblist),
				  NULL, NULL, text, 4,
				  balsa_icon_get_pixmap (icon),
				  balsa_icon_get_bitmap (icon),
				  NULL, NULL,
				  FALSE, FALSE);
  mbnode = mailbox_node_new (mailbox->name, mailbox, FALSE);
  gtk_ctree_node_set_row_data (GTK_CTREE (mblist), ctnode, mbnode);

#ifdef BALSA_SHOW_INFO
  if (mblist->display_content_info){
    gtk_ctree_node_set_text (GTK_CTREE (mblist), ctnode, 1, "");
    gtk_ctree_node_set_text (GTK_CTREE (mblist), ctnode, 2, "");
  }
#endif
}

/* balsa_mblist_redraw 
 *
 * bmbl:  the BalsaMBList that needs redrawing
 *
 * Description: Called whenever a new mailbox is added to the mailbox
 * list, it clears the ctree, and draws all the entire tree from
 * scratch.
 * */
void balsa_mblist_redraw (BalsaMBList * bmbl) 
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
    GtkCTreeNode *node;
        
    walk = g_node_last_child (balsa_app.mailbox_nodes);
    while (walk)
    {
      node = gtk_ctree_insert_gnode (ctree, NULL, NULL, walk, mailbox_nodes_to_ctree, NULL);
      
      gtk_ctree_node_set_text (ctree, node, 1, "");
      gtk_ctree_node_set_text (ctree, node, 2, "");
      walk = walk->prev;
    }
  }
  gtk_ctree_sort_recursive (ctree, NULL);
  gtk_ctree_pre_recursive (ctree, NULL,
			   balsa_mblist_check_new, NULL);
  gtk_clist_thaw (GTK_CLIST (ctree));
}

static gboolean
mailbox_nodes_to_ctree (GtkCTree * ctree,
			guint depth,
			GNode * gnode,
			GtkCTreeNode * cnode,
			gpointer data)
{
  MailboxNode *mbnode=NULL;

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
      gtk_ctree_node_set_row_data (ctree, cnode, mbnode);
    }
    else if (mbnode->mailbox && mbnode->name)
    {
      if(balsa_app.open_mailbox && strcmp(balsa_app.open_mailbox,mbnode->mailbox->name) == 0)  {
        mblist_open_mailbox(mbnode->mailbox); 
        gtk_ctree_select(ctree,cnode);
      }

      if (mbnode->mailbox->type == MAILBOX_MH ||
	  mbnode->mailbox->type == MAILBOX_MAILDIR)
      {
	gtk_ctree_set_node_info (ctree, cnode, mbnode->mailbox->name, 5,
                                 balsa_icon_get_pixmap(BALSA_ICON_TRAY_EMPTY), 
                                 balsa_icon_get_bitmap(BALSA_ICON_TRAY_EMPTY),
				 NULL, NULL,
				 G_NODE_IS_LEAF (gnode),
				 mbnode->expanded);
	gtk_ctree_node_set_row_data (ctree, cnode, mbnode);
      }
      else
      {
	/* normal mailbox */
	if (mailbox_have_new_messages (MAILBOX_LOCAL (mbnode->mailbox)->path))
	{
	  GdkFont *font;
	  GtkStyle *style;
	  
      if(balsa_app.open_unread_mailbox)  {
        balsa_app.open_unread_mailbox=FALSE;
        mblist_open_mailbox(mbnode->mailbox); 
      }

	  style = gtk_style_copy (gtk_widget_get_style (GTK_WIDGET (ctree)));
	  gdk_font_unref(style->font);
	  font = gdk_font_load ("-adobe-courier-medium-r-*-*-*-120-*-*-*-*-iso8859-1");
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
	
	gtk_ctree_node_set_row_data (ctree, cnode, mbnode);
	
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
    /* Make sure this gets set */
    mbnode->IsDir = TRUE;
    gtk_ctree_node_set_row_data (ctree, cnode, mbnode);
    gtk_ctree_node_set_selectable (ctree, cnode, FALSE);
  }
  return TRUE;
}

static void
button_event_press_cb (GtkCTree * ctree, GdkEventButton * event, gpointer user_data)
{
  gint row, column;
  GtkCTreeNode *ctrow;
  MailboxNode *mbnode;

  if (!event || event->button != 3)
    return;

  if (event->button == 1 && event->type == GDK_2BUTTON_PRESS)
  {
    gtk_clist_get_selection_info (GTK_CLIST (ctree), event->x, event->y,
				  &row, &column);
    ctrow = gtk_ctree_node_nth (ctree, row);
    mbnode = gtk_ctree_node_get_row_data (ctree, ctrow);
    
    gtk_ctree_select (ctree, ctrow);
    
    if (mbnode->IsDir || !BALSA_IS_MAILBOX (mbnode->mailbox))
      return;
    
    if (mbnode->mailbox)
      gtk_signal_emit (GTK_OBJECT (BALSA_MBLIST (ctree)),
		       balsa_mblist_signals[SELECT_MAILBOX],
		       mbnode->mailbox,
		       ctrow,
		       event);
  }
}

/* select_mailbox
 *
 * This function is called when the user clicks on the mailbox list,
 * propogates the select mailbox signal on to the mailboxes.
 * */
static void
select_mailbox (GtkCTree * ctree, GtkCTreeNode * row, gint column)
{
  BalsaMBList *bmbl;
  GdkEventButton *bevent = (GdkEventButton *) gtk_get_current_event ();
  MailboxNode *mbnode;

  bmbl = BALSA_MBLIST(ctree);

  mbnode = gtk_ctree_node_get_row_data (ctree, row);

  if (mbnode->IsDir || !BALSA_IS_MAILBOX(mbnode->mailbox))
    return;

  if (bevent && bevent->button == 1)
  {
    if (mbnode->mailbox)
      gtk_signal_emit(GTK_OBJECT (bmbl),
		      balsa_mblist_signals[SELECT_MAILBOX],
		      mbnode->mailbox,
		      row,
		      bevent);
  }
}

/* mailbox_tree_expand
 * mailbox_tree_collapse
 *
 * These are callbacks that sets the expanded flag on the mailbox node, we use
 * this to save whether the folder was expanded or collapsed between
 * sessions. 
 * */
static void
mailbox_tree_expand (GtkCTree * ctree, GList * node, gpointer data)
{
  MailboxNode* mbnode;
  
  mbnode = gtk_ctree_node_get_row_data (ctree, GTK_CTREE_NODE (node));
  mbnode->expanded = TRUE;
}

static void
mailbox_tree_collapse (GtkCTree * ctree, GList * node, gpointer data)
{
  MailboxNode* mbnode;
    
  mbnode = gtk_ctree_node_get_row_data (ctree, GTK_CTREE_NODE (node));
  mbnode->expanded = FALSE;
}

/* balsa_mblist_column_resize [MBG]
 *
 * clist: The clist (in this case ctree), that is having it's columns resized.
 * column: The column being resized
 * size:  The new size of the column
 * data:  The data passed on to the callback when it was connected (NULL)
 *
 * Description: This callback assigns the new column widths to the balsa_app,
 * so they can be saved and restored between sessions.
 * */
static void balsa_mblist_column_resize (GtkCList * clist, gint column, 
					gint size, gpointer data)
{
  switch (column)
  {
  case 0:
    balsa_app.mblist_name_width = size;
    break;
#ifdef BALSA_SHOW_INFO
  case 1:
    balsa_app.mblist_newmsg_width = size;
    break;
  case 2:
    balsa_app.mblist_totalmsg_width = size;
    break;
#endif
  default:
    if (balsa_app.debug)
      fprintf (stderr, "** Error: Unknown column resize\n");
  }
}

/* balsa_mblist_column_click [MBG]
 *  
 *  Description: This causes the columns to be sorted depending on
 *  which columns are clicked.  If a column is already selected as the
 *  sorting column when clicked, the order of sorting is changed.
 * */
static void balsa_mblist_column_click (GtkCList * clist, gint column, 
				       gpointer data)
{
#ifdef BALSA_SHOW_INFO
  if (clist->sort_column == column){
    if (clist->sort_type == GTK_SORT_DESCENDING){
      gtk_clist_set_sort_type (clist, GTK_SORT_ASCENDING);
    } else {
      gtk_clist_set_sort_type (clist, GTK_SORT_DESCENDING);
    }
  } else {
    gtk_clist_set_sort_column (clist, column);
    gtk_clist_set_sort_type (clist, GTK_SORT_ASCENDING);
  }
  
  if (column == 0)
    gtk_clist_set_compare_func (clist, NULL);
  else{
    gtk_clist_set_compare_func (clist, numeric_compare);
  }
  
  gtk_ctree_sort_recursive (GTK_CTREE (data), NULL);
#endif
}

/* balsa_mblist_check_new [MBG]
 * (GtkCTreeFunc)
 *  
 * ctree:  The ctree that contains the mailbox nodes
 * node:  The node that is currently being called by the recursion
 * data:  The data that was passed on by the gtk_ctree_(pre|post)_recursive
 *
 * Description: This function is meant to be called as a part of a
 * recursive call gtk_ctree_(pre|post)_recursive, to traverse through
 * the tree and change the fonts of any mailboxes that have at least
 * one new message. It currently calls mailbox_check_new_messages on
 * the mailbox, but this may change.
 * */
static void
balsa_mblist_check_new (GtkCTree *ctree, GtkCTreeNode *node, gpointer data)
{

  MailboxNode *cnode_data;
  Mailbox *mailbox;

/* Get the mailbox */
  cnode_data = gtk_ctree_node_get_row_data (ctree, node);

  if (cnode_data->IsDir || !BALSA_IS_MAILBOX (cnode_data->mailbox))
    return;

  mailbox = BALSA_MAILBOX (cnode_data->mailbox);

/* FIXME:  This doesn't currently work, mailbox_check_new_messages 
 * continually segfaults.*/
/* See if the mailbox has any new mail (but don't count it) */
/*   mailbox_check_new_messages(mailbox);  */

  balsa_mblist_mailbox_style (ctree, node,  mailbox
#ifdef BALSA_SHOW_INFO
                              ,FALSE
#endif
                              );
}

/* balsa_mblist_update_mailbox [MBG]
 * 
 * mblist: the mailbox list that contains the mailbox
 * mbnode:  the mailbox node that you wish to update
 * 
 * Description: the function looks for the mailbox in the mblist, if
 * it's there it changes the style (and fills the info columns)
 * depending on the mailbox variables unread_messages and
 * total_messages
 * */
void 
balsa_mblist_update_mailbox (BalsaMBList * mblist, Mailbox * mailbox)
{
  GtkCTreeNode *node;

/* try and find the mailbox in both sub trees */
  node = gtk_ctree_find_by_row_data_custom (GTK_CTREE (&(mblist->ctree)), NULL, mailbox, mblist_mbnode_compare);
  
  if (node == NULL)
    return;
  
  balsa_mblist_mailbox_style (GTK_CTREE (&(mblist->ctree)), 
                              node, 
                              mailbox
#ifdef BALSA_SHOW_INFO
                              ,balsa_app.mblist->display_content_info
#endif
                              );
}

/* balsa_mblist_mailbox_style [MBG]
 * 
 * ctree:  The ctree containing the mailbox
 * node:  The ctreenode that is associated with the mailbox
 * mailbox:  The mailbox that is to have it's style changed
 * display_info:  whether or not to display the columns
 * 
 * Description: A function to actually do the changing of the style,
 * and is called by both balsa_mblist_update_mailbox, and
 * balsa_mblist_check_new, hence the (slightly) strange arguments.
 * */
static void balsa_mblist_mailbox_style (GtkCTree *ctree, GtkCTreeNode *node, Mailbox *mailbox 
#ifdef BALSA_SHOW_INFO
                                        , gboolean display_info
#endif
                                        )
{
  GtkStyle *style;
  GdkFont *font;
  gboolean tmp_is_leaf, tmp_expanded;
#ifdef BALSA_SHOW_INFO
  gchar *text;
#endif
  
  if (node == NULL)
    return;

  gtk_clist_freeze (GTK_CLIST (ctree));

  if (mailbox->unread_messages > 0){
    style = gtk_style_copy (gtk_widget_get_style (GTK_WIDGET (ctree)));
    gdk_font_unref (style->font);

/* want to make this font independent (maybe a preference setting?) */
    font = gdk_font_load ("-adobe-helvetica-bold-r-*-*-*-100-*-*-p-*-iso8859-1");
    style->font = font;
    gdk_font_ref (style->font);
    gtk_ctree_node_set_row_style (ctree, node, style);
    gtk_style_unref (style);
    tmp_is_leaf = GTK_CTREE_ROW (node)->is_leaf;
    tmp_expanded = GTK_CTREE_ROW (node)->expanded;
    gtk_ctree_set_node_info (ctree, node, mailbox->name, 5,  
                             balsa_icon_get_pixmap (BALSA_ICON_TRAY_FULL),
                             balsa_icon_get_bitmap (BALSA_ICON_TRAY_FULL),
                             NULL, NULL, tmp_is_leaf, tmp_expanded);


#ifdef BALSA_SHOW_INFO
    if (display_info){
      text = g_strdup_printf ("%ld", mailbox->unread_messages);
      gtk_ctree_node_set_text (ctree, node, 1, text);
      g_free(text);
    }
#endif

  } else {
    style = gtk_style_copy (gtk_widget_get_style (GTK_WIDGET (ctree)));
    gtk_ctree_node_set_row_style (ctree, node, style);
    gtk_style_unref (style);
    tmp_is_leaf = GTK_CTREE_ROW (node)->is_leaf;
    tmp_expanded = GTK_CTREE_ROW (node)->expanded;
    gtk_ctree_set_node_info (ctree, node, mailbox->name, 5,  
                             balsa_icon_get_pixmap (BALSA_ICON_TRAY_EMPTY),
                             balsa_icon_get_bitmap (BALSA_ICON_TRAY_EMPTY),
                             NULL, NULL, tmp_is_leaf, tmp_expanded);
                             
#ifdef BALSA_SHOW_INFO
    if (display_info){
      gtk_ctree_node_set_text (ctree, node, 1, "");
    }
#endif
  }
  
#ifdef BALSA_SHOW_INFO
  if (display_info){
    if (mailbox->total_messages > 0){
      text = g_strdup_printf ("%ld", mailbox->total_messages);
      gtk_ctree_node_set_text (ctree, node, 2, text);
      g_free (text);
    } else {
      gtk_ctree_node_set_text (ctree, node, 2, "");
    }
  }
#endif
  gtk_clist_thaw (GTK_CLIST (ctree));
}

#ifdef BALSA_SHOW_INFO                
static gint
numeric_compare (GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2)
{
  MailboxNode* mb1;
  MailboxNode* mb2;
  Mailbox* m1;
  Mailbox* m2;
  glong t1, t2;
  
  GtkCListRow *row1 = (GtkCListRow *) ptr1;
  GtkCListRow *row2 = (GtkCListRow *) ptr2;
  
  mb1 = row1->data;
  mb2 = row2->data;

  m1 = mb1->mailbox;
  m2 = mb2->mailbox;

  if (!m1 || !m2)
    return 0;
  
  if (clist->sort_column == 1){
    t1 = m1->unread_messages;
    t2 = m2->unread_messages;
    if (t1 < t2)
      return 1;
    if (t1 > t2)
      return -1;
  } else if (clist->sort_column == 2) {
    t1 = m1->total_messages;
    t2 = m2->total_messages;
    if (t1 < t2)
      return 1;
    if (t1 > t2)
      return -1;
  }
  
  return 0;
}
#endif

static gint
mblist_mbnode_compare (gconstpointer a, gconstpointer b)
{
  MailboxNode *mbnode = (MailboxNode *) a;
  Mailbox *mailbox = (Mailbox *) b;
  
  if (mailbox == mbnode->mailbox)
    return 0;
  else
    return 1;
}
