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
#include "mailbox.h"

/* pixmaps */
#include "pixmaps/gball.xpm"


/* constants */
#define BUFFER_SIZE 1024

static int first_new_mesgno;

/* gtk widget */
static void balsa_index_class_init (BalsaIndexClass * klass);
static void balsa_index_init (BalsaIndex * bindex);
static void balsa_index_size_request (GtkWidget * widget,
				      GtkRequisition * requisition);
static void balsa_index_size_allocate (GtkWidget * widget,
				       GtkAllocation * allocation);


/* internal functions */
static void append_messages (BalsaIndex * bindex,
			     glong first,
			     glong last);
/*
   static void update_new_message_pixmap (BalsaIndex * bindex,
   glong mesgno);
 */
static void update_message_flag (BalsaIndex * bindex,
				 glong mesgno, gchar *flag);


/* clist callbacks */
static void realize_clist (GtkWidget * widget,
			   gpointer * data);
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

typedef void (*BalsaIndexSignal1) (GtkObject * object,
				   MAILSTREAM * arg1,
				   glong arg2,
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
    "F",
    "#",
    "From",
    "Subject",
    "Date"
  };


  GTK_WIDGET_SET_FLAGS (bindex, GTK_NO_WINDOW);
  bindex->stream = NIL;
  bindex->last_message = 0;
  bindex->new_xpm = NULL;
  bindex->new_xpm_mask = NULL;
  bindex->progress_bar = NULL;


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


  gtk_signal_connect_after (GTK_OBJECT (clist),
			    "realize",
			    (GtkSignalFunc) realize_clist,
			    (gpointer) bindex);

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
      if (first_new_mesgno != 0)
	{
	  gtk_clist_select_row (GTK_CLIST (GTK_BIN (bindex)->child), first_new_mesgno - 1, -1);
	  gtk_clist_moveto (GTK_CLIST (GTK_BIN (bindex)->child), first_new_mesgno - 1, 0, 0.0, 0.0);
	}
      else
	{
	  gtk_clist_select_row (GTK_CLIST (GTK_BIN (bindex)->child), bindex->last_message - 1, -1);
	  gtk_clist_moveto (GTK_CLIST (GTK_BIN (bindex)->child), bindex->last_message - 1, 0, 1.0, 1.0);
	}
    }
  gtk_clist_set_selection_mode (GTK_CLIST (GTK_BIN (bindex)->child),
				GTK_SELECTION_BROWSE);
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
balsa_index_set_progress_bar (BalsaIndex * bindex,
			      GtkProgressBar * progress_bar)
{
  g_return_if_fail (bindex != NULL);
  g_return_if_fail (progress_bar != NULL);

  if (bindex->progress_bar)
    gtk_widget_unref (GTK_WIDGET (bindex->progress_bar));

  gtk_widget_ref (GTK_WIDGET (progress_bar));
  bindex->progress_bar = progress_bar;
}


GtkProgressBar *
balsa_index_get_progress_bar (BalsaIndex * bindex)
{
  g_return_if_fail (bindex != NULL);

  return bindex->progress_bar;
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


void
balsa_index_delete_message (BalsaIndex * bindex)
{
  GtkCList *clist;
  glong row;
  char tmp[10];

  clist = GTK_CLIST (GTK_BIN (bindex)->child);

  if (!clist->selection)
    return;

  row = (glong) clist->selection->data;

  update_message_flag (bindex, row + 1, "D");

  sprintf (tmp, "%ld", row + 1);
  mail_setflag (bindex->stream, tmp, "\\DELETED");
  gtk_clist_select_row (clist, row + 1, -1);
  gtk_clist_moveto (clist, row + 1, 0, 0.5, 0.0);
}

void
balsa_index_undelete_message (BalsaIndex * bindex)
{
  GtkCList *clist;
  glong row;
  char tmp[10];

  clist = GTK_CLIST (GTK_BIN (bindex)->child);

  if (!clist->selection)
    return;

  row = (glong) clist->selection->data;

  update_message_flag (bindex, row + 1, " ");

  sprintf (tmp, "%ld", row + 1);
  mail_clearflag (bindex->stream, tmp, "\\DELETED");
  gtk_clist_select_row (clist, row + 1, -1);
  gtk_clist_moveto (clist, row + 1, 0, 0.5, 0.0);
}

static void
append_messages (BalsaIndex * bindex,
		 glong first,
		 glong last)
{
  glong i;
  gchar *text[5];
  MESSAGECACHE *cache;

  text[0] = NULL;
  text[1] = g_malloc0 (BUFFER_SIZE);
  text[2] = g_malloc0 (BUFFER_SIZE);
  text[3] = g_malloc0 (BUFFER_SIZE);
  text[4] = g_malloc0 (BUFFER_SIZE);

  first_new_mesgno = 0;

  gtk_clist_freeze (GTK_CLIST (GTK_BIN (bindex)->child));

  for (i = first; i <= last; i++)
    {
      sprintf (text[1], "%d", i);
      mail_fetchfrom (text[2], bindex->stream, i, (long) BUFFER_SIZE);
      mail_fetchsubject (text[3], bindex->stream, i, (long) BUFFER_SIZE);

      if (bindex->progress_bar)
	{
	  gtk_progress_bar_update (bindex->progress_bar, (gfloat) i / last);
	  gtk_widget_draw (GTK_WIDGET (bindex->progress_bar), NULL);
	}

      mail_fetchstructure (bindex->stream, i, NIL);
      cache = mail_elt (bindex->stream, i);
      mail_date (text[4], cache);

      gtk_clist_append (GTK_CLIST (GTK_BIN (bindex)->child), text);
      update_message_flag (bindex, i, "N");

      /* give time to gtk so the GUI isn't blocked */
      while (gtk_events_pending ())
	gtk_main_iteration ();
    }

  gtk_clist_thaw (GTK_CLIST (GTK_BIN (bindex)->child));

  /* re-set the progress bar to 0.0 */
  if (bindex->progress_bar)
    gtk_progress_bar_update (bindex->progress_bar, 0.0);

  g_free (text[1]);
  g_free (text[2]);
  g_free (text[3]);
  g_free (text[4]);
}


#if 0
static void
update_new_message_pixmap (BalsaIndex * bindex,
			   glong mesgno)
{
  MESSAGECACHE *elt;

  elt = mail_elt (bindex->stream, mesgno);

  if (!elt->seen)
    {
      gtk_clist_set_pixmap (GTK_CLIST (GTK_BIN (bindex)->child),
			    mesgno - 1, 0,
			    bindex->new_xpm,
			    bindex->new_xpm_mask);
      if (first_new_mesgno == 0)
	first_new_mesgno = mesgno;
    }
  else
    gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child),
			mesgno - 1, 0,
			NULL);
}
#endif


static void
update_message_flag (BalsaIndex * bindex,
		     glong mesgno, gchar * flag)
{
  MESSAGECACHE *elt;
  switch (*flag)
    {
    case 'N':
      elt = mail_elt (bindex->stream, mesgno);
      if (!elt->seen)
	{
	  gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), mesgno - 1, 0, flag);
	  if (first_new_mesgno == 0)
	    first_new_mesgno = mesgno;
	}
      break;
    case 'D':
      gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), mesgno - 1, 0, flag);
      break;
    case ' ':
      gtk_clist_set_text (GTK_CLIST (GTK_BIN (bindex)->child), mesgno - 1, 0, NULL);
      break;
    }
}




/*
 * CLIST Callbacks
 */
static void
realize_clist (GtkWidget * widget,
	       gpointer * data)
{
  BalsaIndex *bindex;

  bindex = BALSA_INDEX (data);

  if (!bindex->new_xpm)
    bindex->new_xpm =
      gdk_pixmap_create_from_xpm_d (GTK_CLIST (widget)->clist_window,
				    &bindex->new_xpm_mask,
				    &widget->style->white,
				    gball_xpm);
}


static GtkWidget *
create_menu (BalsaIndex * bindex, glong mesgno)
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

  if (bevent)
    {
      if (bevent->button == 3)
	{
	  gtk_menu_popup (GTK_MENU (create_menu (bindex, mesgno)), NULL, NULL, NULL, NULL, 3, bevent->time);
	}
    }
}

static void
unselect_message (GtkWidget * widget,
		  gint row,
		  gint column,
		  GdkEventButton * bevent,
		  gpointer * data)
{
  BalsaIndex *bindex;
  glong mesgno;
  gchar *foo=g_malloc(sizeof(char *)*2);

  bindex = BALSA_INDEX (data);

  /* the message number is going to be one more
   * than the row selected -- until the message list
   * starts getting sorted! */
  mesgno = row + 1;

  /* update the index to show any changes in the message
   * state */
  gtk_clist_get_text(GTK_CLIST (GTK_BIN (bindex)->child),row,0,&foo);
  if (*foo!='D')
  update_message_flag (bindex, mesgno, " ");
}
