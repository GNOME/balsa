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

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include "balsa-app.h"
#include "balsa-index.h"


/* constants */
#define BUFFER_SIZE 1024


/* gtk widget */
static void balsa_index_class_init (BalsaIndexClass * klass);
static void balsa_index_init (BalsaIndex * bindex);
static void balsa_index_size_request (GtkWidget * widget, GtkRequisition * requisition);
static void balsa_index_size_allocate (GtkWidget * widget, GtkAllocation * allocation);


/* clist callbacks */
static void select_message (GtkWidget * widget,
			    gint row,
			    gint column,
			    GdkEventButton * bevent,
			    gpointer * data);
static void unselect_message (GtkWidget * widget,
			      gint row,
			      gint column,
			      GdkEventButton * bevent,
			      gpointer * data);


/* signals */
enum
  {
    SELECT_MESSAGE,
    LAST_SIGNAL
  };


/* marshallers */
typedef void (*BalsaIndexSignal1) (GtkObject * object,
				   Message * arg1,
				   gpointer data);

static void balsa_index_marshal_signal_1 (GtkObject * object,
					  GtkSignalFunc func,
					  gpointer func_data,
					  GtkArg * args);

static gint balsa_index_signals[LAST_SIGNAL] =
{0};
static GtkBinClass *parent_class = NULL;


guint
balsa_index_get_type ()
{
  static guint balsa_index_type = 0;

  if (!balsa_index_type)
    {
      GtkTypeInfo balsa_index_info =
      {
	"BalsaIndex",
	sizeof (BalsaIndex),
	sizeof (BalsaIndexClass),
	(GtkClassInitFunc) balsa_index_class_init,
	(GtkObjectInitFunc) balsa_index_init,
	(GtkArgSetFunc) NULL,
	(GtkArgGetFunc) NULL
      };

      balsa_index_type = gtk_type_unique (gtk_bin_get_type (), &balsa_index_info);
    }

  return balsa_index_type;
}


static void
balsa_index_class_init (BalsaIndexClass * klass)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;

  object_class = (GtkObjectClass *) klass;
  widget_class = (GtkWidgetClass *) klass;
  container_class = (GtkContainerClass *) klass;

  parent_class = gtk_type_class (gtk_widget_get_type ());

  balsa_index_signals[SELECT_MESSAGE] =
    gtk_signal_new ("select_message",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (BalsaIndexClass, select_message),
		    balsa_index_marshal_signal_1,
		    GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_LONG);

  gtk_object_class_add_signals (object_class, balsa_index_signals, LAST_SIGNAL);

  widget_class->size_request = balsa_index_size_request;
  widget_class->size_allocate = balsa_index_size_allocate;

  container_class->add = NULL;
  container_class->remove = NULL;

  klass->select_message = NULL;
}


static void
balsa_index_marshal_signal_1 (GtkObject * object,
			      GtkSignalFunc func,
			      gpointer func_data,
			      GtkArg * args)
{
  BalsaIndexSignal1 rfunc;

  rfunc = (BalsaIndexSignal1) func;
  (*rfunc) (object, GTK_VALUE_POINTER (args[0]), func_data);
}


static void
balsa_index_init (BalsaIndex * bindex)
{
  GtkCList *clist;
  static gchar *titles[] =
  {
    "F",
    "#",
    "From",
    "Subject",
    "Date"
  };


  GTK_WIDGET_SET_FLAGS (bindex, GTK_NO_WINDOW);
  bindex->mailbox = NULL;

  /* create the clist */
  GTK_BIN (bindex)->child =
    (GtkWidget *) clist = gtk_clist_new_with_titles (5, titles);

  gtk_widget_set_parent (GTK_WIDGET (clist), GTK_WIDGET (bindex));
  gtk_clist_set_policy (clist, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_clist_set_selection_mode (clist, GTK_SELECTION_BROWSE);
  gtk_clist_set_column_justification (clist, 0, GTK_JUSTIFY_CENTER);
  gtk_clist_set_column_width (clist, 0, 9);
  gtk_clist_set_column_width (clist, 1, 30);
  gtk_clist_set_column_width (clist, 2, 150);
  gtk_clist_set_column_width (clist, 3, 250);
  gtk_clist_set_column_width (clist, 4, 100);

  gtk_signal_connect (GTK_OBJECT (clist),
		      "select_row",
		      (GtkSignalFunc) select_message,
		      (gpointer) bindex);

  gtk_signal_connect (GTK_OBJECT (clist),
		      "unselect_row",
		      (GtkSignalFunc) unselect_message,
		      (gpointer) bindex);

  gtk_widget_show (GTK_WIDGET (clist));
  gtk_widget_ref (GTK_WIDGET (clist));
}


static void
balsa_index_size_request (GtkWidget * widget,
			  GtkRequisition * requisition)
{
  GtkWidget *child;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (requisition != NULL);

  child = GTK_BIN (widget)->child;

  requisition->width = 0;
  requisition->height = 0;

  if (GTK_WIDGET_VISIBLE (child))
    {
      gtk_widget_size_request (child, &child->requisition);
      requisition->width = child->requisition.width;
      requisition->height = child->requisition.height;
    }
}


static void
balsa_index_size_allocate (GtkWidget * widget,
			   GtkAllocation * allocation)
{
  GtkBin *bin;
  GtkWidget *child;
  GtkAllocation child_allocation;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (allocation != NULL);

  bin = GTK_BIN (widget);
  widget->allocation = *allocation;

  child = bin->child;

  if (GTK_WIDGET_REALIZED (widget))
    {
      if (!GTK_WIDGET_VISIBLE (child))
	gtk_widget_show (child);

      child_allocation.x = allocation->x + GTK_CONTAINER (widget)->border_width;
      child_allocation.y = allocation->y + GTK_CONTAINER (widget)->border_width;
      child_allocation.width = allocation->width -
	2 * GTK_CONTAINER (widget)->border_width;
      child_allocation.height = allocation->height -
	2 * GTK_CONTAINER (widget)->border_width;

      gtk_widget_size_allocate (child, &child_allocation);
    }
}


GtkWidget *
balsa_index_new ()
{
  BalsaIndex *bindex;
  bindex = gtk_type_new (balsa_index_get_type ());
  return GTK_WIDGET (bindex);
}


void
balsa_index_set_mailbox (BalsaIndex * bindex, Mailbox * mailbox)
{
  g_return_if_fail (bindex != NULL);


  if (bindex->mailbox == mailbox)
    return;

  gtk_clist_clear (GTK_CLIST (GTK_BIN (bindex)->child));

  bindex->mailbox = mailbox;
  if (bindex->mailbox == NULL)
    return;

#if 0
  /* here we play a little trick on clist; in GTK_SELECTION_BROWSE mode
   * (the default for this index), the first row appended automagicly gets
   * selected.  this causes a delay in the index getting filled out, and
   * makes it appear as if the message is displayed before the index; so we set
   * the clist selection mode to a mode that doesn't automagicly select, select
   * manually, then switch back */

  gtk_clist_set_selection_mode (GTK_CLIST (GTK_BIN (bindex)->child),
				GTK_SELECTION_SINGLE);

  append_messages (bindex, 1, bindex->last_message);

  if (GTK_CLIST (GTK_BIN (bindex)->child)->rows > 0)
    {
      if (first_new_msgno != 0)
	{
	  gtk_clist_select_row (GTK_CLIST (GTK_BIN (bindex)->child), first_new_msgno - 1, -1);
	  gtk_clist_moveto (GTK_CLIST (GTK_BIN (bindex)->child), first_new_msgno - 1, 0, 0.0, 0.0);
	}
      else
	{
	  gtk_clist_select_row (GTK_CLIST (GTK_BIN (bindex)->child), bindex->last_message - 1, -1);
	  gtk_clist_moveto (GTK_CLIST (GTK_BIN (bindex)->child), bindex->last_message - 1, 0, 1.0, 1.0);
	}
    }
  gtk_clist_set_selection_mode (GTK_CLIST (GTK_BIN (bindex)->child), GTK_SELECTION_BROWSE);
#endif
}


void
balsa_index_add (BalsaIndex * bindex,
		 Message * message)
{
  gchar *text[5];
  gchar *tmp;
  gint row;

  g_return_if_fail (bindex != NULL);
  g_return_if_fail (message != NULL);

  if (bindex->mailbox == NULL)
    return;

  text[0] = NULL;
  text[1] = NULL;
  if (message->from->personal)
    text[2] = message->from->personal;
  else
  {
    text[2] = g_malloc(strlen(message->from->user)+1+strlen(message->from->host)+1);
    sprintf (text[2], "%s@%s", message->from->user, message->from->host);
    }
  text[3] = message->subject;
  text[4] = message->date;

  row = gtk_clist_append (GTK_CLIST (GTK_BIN (bindex)->child), text);
  gtk_clist_set_row_data (GTK_CLIST (GTK_BIN (bindex)->child), row, (gpointer) message);
}


void
balsa_index_select_next (BalsaIndex * bindex)
{
  gint row;
  GtkCList *clist;

  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (GTK_BIN (bindex)->child);

  if (!clist->selection)
    return;

  row = (gint) clist->selection->data + 1;

  gtk_clist_select_row (clist, row, -1);

  if (gtk_clist_row_is_visible (clist, row) != GTK_VISIBILITY_FULL)
    gtk_clist_moveto (clist, row, 0, 1.0, 0.0);
}


void
balsa_index_select_previous (BalsaIndex * bindex)
{
  gint row;
  GtkCList *clist;

  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (GTK_BIN (bindex)->child);

  if (!clist->selection)
    return;

  row = (gint) clist->selection->data - 1;

  gtk_clist_select_row (clist, row, -1);

  if (gtk_clist_row_is_visible (clist, row) != GTK_VISIBILITY_FULL)
    gtk_clist_moveto (clist, row, 0, 0.0, 0.0);
}

void
balsa_index_set_flag (BalsaIndex * bindex, Message * message, gchar * flag)
{
#if 0
  switch (*flag)
    {
    case 'N':
      gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), msgno - 1, 0, flag);
      break;

    case 'D':
      gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), msgno - 1, 0, flag);
      break;

    case ' ':
      gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), msgno - 1, 0, NULL);
      break;
    }
#endif
}



/*
 * CLIST Callbacks
 */
static GtkWidget *
create_menu (BalsaIndex * bindex, glong msgno)
{
  GtkWidget *menu, *menuitem, *submenu, *smenuitem;
  Mailbox *mailbox;
  GList *list;

  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label ("Transfer");

  list = g_list_first (balsa_app.mailbox_list);
  submenu = gtk_menu_new ();
  while (list)
    {
      mailbox = list->data;
      smenuitem = gtk_menu_item_new_with_label (mailbox->name);
      gtk_menu_append (GTK_MENU (submenu), smenuitem);
      gtk_widget_show (smenuitem);
      list = list->next;
    }

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label ("Change Status");

  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new_with_label ("Unread");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);
  smenuitem = gtk_menu_item_new_with_label ("Read");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);
  smenuitem = gtk_menu_item_new_with_label ("Replied");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);
  smenuitem = gtk_menu_item_new_with_label ("Forwarded");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label ("Change Priority");

  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new_with_label ("Highest");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);
  smenuitem = gtk_menu_item_new_with_label ("High");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);
  smenuitem = gtk_menu_item_new_with_label ("Normal");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);
  smenuitem = gtk_menu_item_new_with_label ("Low");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);
  smenuitem = gtk_menu_item_new_with_label ("Lowest");
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label ("Delete");
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  return menu;
}


static void
select_message (GtkWidget * widget,
		gint row,
		gint column,
		GdkEventButton * bevent,
		gpointer * data)
{
  BalsaIndex *bindex;
  Message *message;

  bindex = BALSA_INDEX (data);
  message = (Message *) gtk_clist_get_row_data (GTK_CLIST (widget), row);

  gtk_signal_emit (GTK_OBJECT (bindex),
		   balsa_index_signals[SELECT_MESSAGE],
		   message,
		   NULL);
}


static void
unselect_message (GtkWidget * widget,
		  gint row,
		  gint column,
		  GdkEventButton * bevent,
		  gpointer * data)
{
  BalsaIndex *bindex;
  Message *message;

  bindex = BALSA_INDEX (data);
  message = (Message *) gtk_clist_get_row_data (GTK_CLIST (widget), row);
}
