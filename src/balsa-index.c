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


/* statics */
static char *flag_str (MessageFlags flags);
static void mailbox_listener (MailboxWatcherMessage * mw_message);


/* clist callbacks */
static void
  button_event_press_cb (GtkCList * clist,
			 GdkEventButton * event,
			 gpointer data);
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
				   Message * message,
				   GdkEventButton * bevent,
				   gpointer data);

static void balsa_index_marshal_signal_1 (GtkObject * object,
					  GtkSignalFunc func,
					  gpointer func_data,
					  GtkArg * args);

static gint balsa_index_signals[LAST_SIGNAL] =
{0};
static GtkBinClass *parent_class = NULL;

static guint first_new_message;

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
		    GTK_TYPE_NONE, 2, GTK_TYPE_POINTER,
		    GTK_TYPE_GDK_EVENT);
  gtk_object_class_add_signals (object_class, balsa_index_signals, LAST_SIGNAL);

  widget_class->size_request = balsa_index_size_request;
  widget_class->size_allocate = balsa_index_size_allocate;

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
  (*rfunc) (object, GTK_VALUE_POINTER (args[0]),
	    GTK_VALUE_BOXED (args[1]), func_data);
}


static void
balsa_index_init (BalsaIndex * bindex)
{
/*
 * status
 * priority
 * attachments
 */
  GtkCList *clist;
  static gchar *titles[] =
  {
    "#",
    "S",
    "A",
    "From",
    "Subject",
    "Date"
  };


  GTK_WIDGET_SET_FLAGS (bindex, GTK_NO_WINDOW);
  bindex->mailbox = NULL;
  bindex->selection = NULL;

  /* create the clist */
  GTK_BIN (bindex)->child =
    (GtkWidget *) clist = gtk_clist_new_with_titles (6, titles);

  gtk_widget_set_parent (GTK_WIDGET (clist), GTK_WIDGET (bindex));
  gtk_clist_set_policy (clist, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_clist_set_selection_mode (clist, GTK_SELECTION_BROWSE);
  gtk_clist_set_column_justification (clist, 0, GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_justification (clist, 1, GTK_JUSTIFY_CENTER);
  gtk_clist_set_column_justification (clist, 2, GTK_JUSTIFY_CENTER);
  gtk_clist_set_column_width (clist, 0, 30);
  gtk_clist_set_column_width (clist, 1, 9);
  gtk_clist_set_column_width (clist, 2, 9);
  gtk_clist_set_column_width (clist, 3, 150);
  gtk_clist_set_column_width (clist, 4, 250);
  gtk_clist_set_column_width (clist, 5, 100);

  gtk_signal_connect (GTK_OBJECT (clist),
		      "select_row",
		      (GtkSignalFunc) select_message,
		      (gpointer) bindex);

  gtk_signal_connect (GTK_OBJECT (clist),
		      "unselect_row",
		      (GtkSignalFunc) unselect_message,
		      (gpointer) bindex);

  gtk_signal_connect (GTK_OBJECT (clist),
		      "button_press_event",
		      (GtkSignalFunc) button_event_press_cb,
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
  GList *list;
  guint i = 0;
  g_return_if_fail (bindex != NULL);

  if (bindex->mailbox == mailbox)
    return;

  /*
   * release the old mailbox
   */
  if (bindex->mailbox)
    {
      mailbox_watcher_remove (mailbox, bindex->watcher_id);
      mailbox_open_unref (bindex->mailbox);
      g_list_free (bindex->selection);
      bindex->selection = NULL;
      gtk_clist_clear (GTK_CLIST (GTK_BIN (bindex)->child));
    }

  /*
   * set the new mailbox
   */
  bindex->mailbox = mailbox;
  if (mailbox == NULL)
    return;

  mailbox_open_ref (mailbox);
  bindex->watcher_id =
    mailbox_watcher_set (mailbox,
			 (MailboxWatcherFunc) mailbox_listener,
			 MESSAGE_MARK_ANSWER_MASK |
			 MESSAGE_MARK_READ_MASK |
			 MESSAGE_MARK_DELETE_MASK |
			 MESSAGE_MARK_UNDELETE_MASK |
			 MESSAGE_DELETE_MASK |
			 MESSAGE_NEW_MASK |
			 MESSAGE_FLAGGED_MASK |
			 MESSAGE_REPLIED_MASK,
			 (gpointer) bindex);


  /* here we play a little trick on clist; in GTK_SELECTION_BROWSE mode
   * (the default for this index), the first row appended automagicly gets
   * selected.  this causes a delay in the index getting filled out, and
   * makes it appear as if the message is displayed before the index; so we set
   * the clist selection mode to a mode that doesn't automagicly select, select
   * manually, then switch back */
  gtk_clist_set_selection_mode (GTK_CLIST (GTK_BIN (bindex)->child),
				GTK_SELECTION_SINGLE);

  list = mailbox->message_list;
  while (list)
    {
      balsa_index_add (bindex, (Message *) list->data);
      list = list->next;
      i++;
    }

  gtk_clist_set_selection_mode (GTK_CLIST (GTK_BIN (bindex)->child),
				GTK_SELECTION_BROWSE);

  if (first_new_message != 0)
    gtk_clist_moveto (GTK_CLIST (GTK_BIN (bindex)->child), first_new_message, 0, 0.0, 0.0);
  else
    {
      g_print ("%i\n", i);
      gtk_clist_moveto (GTK_CLIST (GTK_BIN (bindex)->child), i - 1, 0, 0.0, 0.0);
    }
  first_new_message = 0;
}


void
balsa_index_add (BalsaIndex * bindex,
		 Message * message)
{
  gchar buff1[1024], buff2[1024];
  gchar *text[6];
  gint row;

  g_return_if_fail (bindex != NULL);
  g_return_if_fail (message != NULL);

  if (bindex->mailbox == NULL)
    return;

  text[0] = buff1;
  text[1] = flag_str (message->flags);
  text[2] = NULL;		/* attachments */
  text[3] = buff2;

  if (message->from)
    {
      if (message->from->personal)
	snprintf (text[3], 1024, "%s", message->from->personal);
      else
	snprintf (text[3], 1024, "%s", message->from->mailbox);
    }
  else
    text[3] = NULL;
  text[4] = message->subject;

  text[5] = message->date;

  row = gtk_clist_append (GTK_CLIST (GTK_BIN (bindex)->child), text);

  /* set message number */
  sprintf (text[0], "%d", row + 1);
  gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), row, 0, text[0]);

  gtk_clist_set_row_data (GTK_CLIST (GTK_BIN (bindex)->child), row, (gpointer) message);

  if (first_new_message == 0)
    if (message->flags & MESSAGE_FLAG_NEW)
      first_new_message = row;
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
balsa_index_update_flag (BalsaIndex * bindex, Message * message)
{
  gint row;

  g_return_if_fail (bindex != NULL);
  g_return_if_fail (message != NULL);

  row = gtk_clist_find_row_from_data (GTK_CLIST (GTK_BIN (bindex)->child), message);
  if (row < 0)
    return;

  gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), row, 1, flag_str (message->flags));
}


static char *
flag_str (MessageFlags flags)
{
  if (flags & MESSAGE_FLAG_DELETED)
    return "D";

  if (flags & MESSAGE_FLAG_FLAGGED)
    return "F";

  if (flags & MESSAGE_FLAG_REPLIED)
    return "R";

  if (flags & MESSAGE_FLAG_NEW)
    return "N";

  return NULL;
}


/* CLIST callbacks */

static void
button_event_press_cb (GtkCList * clist, GdkEventButton * event, gpointer data)
{
  gint row, column;
  Message *message;
  BalsaIndex *bindex;

  if (event->window != clist->clist_window)
    return;

  if (!event || event->button != 3)
	  return;

  gtk_clist_get_selection_info (clist, event->x, event->y + clist->voffset, &row, &column);

  bindex = BALSA_INDEX (data);
  message = (Message *) gtk_clist_get_row_data (clist, row);

  bindex->selection = g_list_append (bindex->selection, message);

  gtk_clist_select_row (clist, row, -1);

  if (message)
    gtk_signal_emit (GTK_OBJECT (bindex),
		     balsa_index_signals[SELECT_MESSAGE],
		     message,
		     event);
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

  bindex->selection = g_list_append (bindex->selection, message);

  if (message)
    gtk_signal_emit (GTK_OBJECT (bindex),
		     balsa_index_signals[SELECT_MESSAGE],
		     message,
		     bevent);
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

  bindex->selection = g_list_remove (bindex->selection, message);
}


/*
 * listen for mailbox messages
 */
static void
mailbox_listener (MailboxWatcherMessage * mw_message)
{
  BalsaIndex *bindex = (BalsaIndex *) mw_message->data;

  switch (mw_message->type)
    {
    case MESSAGE_MARK_READ:
    case MESSAGE_MARK_DELETE:
    case MESSAGE_MARK_UNDELETE:
    case MESSAGE_FLAGGED:
    case MESSAGE_REPLIED:
      balsa_index_update_flag (bindex, mw_message->message);
      break;

    case MESSAGE_NEW:
      balsa_index_add (bindex, mw_message->message);
      break;

    case MESSAGE_DELETE:
      break;

    default:
      break;
    }
}
