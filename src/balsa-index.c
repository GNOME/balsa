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
#include "balsa-index.h"


#define BUFFER_SIZE 1024

/* gtk widget */
static void balsa_index_class_init (BalsaIndexClass * klass);
static void balsa_index_init (BalsaIndex * bindex);
static void balsa_index_size_request (GtkWidget * widget,
				      GtkRequisition * requisition);
static void balsa_index_size_allocate (GtkWidget * widget,
				       GtkAllocation * allocation);


static void append_messages (BalsaIndex *bindex,
			     glong first,
			     glong last);

static void select_message (GtkWidget * widget, 
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

typedef void (*BalsaIndexSignal1) (GtkObject * object,
				   MAILSTREAM * arg1,
				   glong arg2,
				   gpointer data);

static void balsa_index_marshal_signal_1 (GtkObject * object,
					  GtkSignalFunc func,
					  gpointer func_data,
					  GtkArg * args);


static gint balsa_index_signals[LAST_SIGNAL] = {0};
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

  (*rfunc) (object, 
	    GTK_VALUE_POINTER (args[0]),
	    GTK_VALUE_LONG (args[1]),
            func_data);
}

static void
balsa_index_init (BalsaIndex * bindex)
{
  GtkCList *clist;

  static gchar *titles[] =
  {
    "#",
    "From",
    "Subject",
    "Date"
  };

  GTK_WIDGET_SET_FLAGS (bindex, GTK_NO_WINDOW);

  bindex->stream = NIL;
  bindex->last_message = 0;

  GTK_BIN (bindex)->child = (GtkWidget *) clist = gtk_clist_new_with_titles (4, titles);
  gtk_widget_set_parent (GTK_WIDGET (clist), GTK_WIDGET (bindex));
  gtk_clist_set_policy (clist, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_clist_set_selection_mode (clist, GTK_SELECTION_BROWSE);
  gtk_clist_set_column_justification (clist, 0, GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_width (clist, 0, 25);
  gtk_clist_set_column_width (clist, 1, 150);
  gtk_clist_set_column_width (clist, 2, 250);
  gtk_clist_set_column_width (clist, 3, 100);

  gtk_signal_connect (GTK_OBJECT (clist),
                      "select_row",
                      (GtkSignalFunc) select_message,
                      (gpointer) bindex);

  gtk_widget_show (GTK_BIN (bindex)->child);
  gtk_widget_ref (GTK_BIN (bindex)->child);
}

GtkWidget *
balsa_index_new ()
{
  BalsaIndex *bindex;
  bindex = gtk_type_new (balsa_index_get_type ());
  return GTK_WIDGET (bindex);
}

void
balsa_index_set_stream (BalsaIndex * bindex,
			MAILSTREAM * stream)
{
  g_return_if_fail (bindex != NULL);
  g_return_if_fail (stream != NULL);

  if (bindex->stream == stream)
    return;

  bindex->stream = stream;
  bindex->last_message = stream->nmsgs;

  /* clear list and append messages */
  gtk_clist_clear (GTK_CLIST (GTK_BIN (bindex)->child));
  append_messages (bindex, 1, bindex->last_message);
}

void
balsa_index_append_new_messages (BalsaIndex * bindex)
{
  glong first;

  g_return_if_fail (bindex != NULL);

  if (bindex->stream == NIL)
    return;

  if (bindex->stream->nmsgs > bindex->last_message)
    {
      first = bindex->last_message + 1;
      bindex->last_message = bindex->stream->nmsgs;
      append_messages (bindex, first, bindex->last_message);
    }
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


static void
append_messages (BalsaIndex *bindex,
		 glong first,
		 glong last)
{
  glong i;
  gchar *text[4];
  MESSAGECACHE *cache;

  text[0] = g_malloc (BUFFER_SIZE);
  text[1] = g_malloc (BUFFER_SIZE);
  text[2] = g_malloc (BUFFER_SIZE);
  text[3] = g_malloc (BUFFER_SIZE);

  gtk_clist_freeze (GTK_CLIST (GTK_BIN (bindex)->child));

  for (i = first; i <= last; i++)
    {
      sprintf (text[0], "%d", i);
      mail_fetchfrom (text[1], bindex->stream, i, (long) BUFFER_SIZE);
      mail_fetchsubject (text[2], bindex->stream, i, (long) BUFFER_SIZE);

      mail_fetchstructure (bindex->stream, i, NIL);
      cache = mail_elt (bindex->stream, i);
      mail_date (text[3], cache);

      gtk_clist_append (GTK_CLIST (GTK_BIN (bindex)->child), text);
    }

  gtk_clist_thaw (GTK_CLIST (GTK_BIN (bindex)->child));

  g_free (text[0]);
  g_free (text[1]);
  g_free (text[2]);
  g_free (text[3]);
}

static void
select_message (GtkWidget * widget, 
		gint row,
		gint column,
		GdkEventButton * bevent,
		gpointer * data)
{
  BalsaIndex *bindex;
  glong mesgno;

  bindex = BALSA_INDEX (data);

  /* the message number is going to be one more
   * than the row selected -- until the message list
   * starts getting sorted! */
  mesgno = row + 1;

  gtk_signal_emit (GTK_OBJECT (bindex), 
		   balsa_index_signals[SELECT_MESSAGE],
		   bindex->stream,
		   mesgno,
		   NULL);
}
