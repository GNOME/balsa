/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <glib.h>
#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "main-window.h"

/* constants */
#define BUFFER_SIZE 1024


/* gtk widget */
static void balsa_index_class_init (BalsaIndexClass * klass);
static void balsa_index_init (BalsaIndex * bindex);

static gint date_compare (GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2);
static gint numeric_compare (GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2);
static void clist_click_column (GtkCList * clist, gint column, gpointer data);

/* statics */
static void clist_set_col_img_from_flag (BalsaIndex *, gint, LibBalsaMessage *);

/* mailbox callbacks */
static void mailbox_message_changed_status_cb(LibBalsaMailbox *mb, LibBalsaMessage *message, BalsaIndex *bindex);
static void mailbox_message_new_cb (LibBalsaMailbox *mb, LibBalsaMessage *message, BalsaIndex *bindex);
static void mailbox_message_delete_cb (LibBalsaMailbox *mb, LibBalsaMessage *message, BalsaIndex *bindex);

/* clist callbacks */
static void
  button_event_press_cb (GtkCList * clist,
			 GdkEventButton * event,
			 gpointer data);
static void
  button_event_release_cb (GtkCList * clist,
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
static void resize_column_event_cb (GtkCList * clist, 
				    gint column, 
				    gint width, 
				    gpointer * data);

/* signals */
enum
  {
    SELECT_MESSAGE,
    UNSELECT_MESSAGE,
    LAST_SIGNAL
  };


/* marshallers */
typedef void (*BalsaIndexSignal1) (GtkObject * object,
				   LibBalsaMessage * message,
				   GdkEventButton * bevent,
				   gpointer data);

static gint balsa_index_signals[LAST_SIGNAL] =
{0};
static GtkCListClass *parent_class = NULL;

static gint
date_compare (GtkCList * clist,
	      gconstpointer ptr1,
	      gconstpointer ptr2)
{
  LibBalsaMessage *m1, *m2;
  time_t t1, t2;

  GtkCListRow *row1 = (GtkCListRow *) ptr1;
  GtkCListRow *row2 = (GtkCListRow *) ptr2;

  m1 = row1->data;
  m2 = row2->data;

  if (!m1 || !m2)
    return 0;

  t1 = m1->date;
  t2 = m2->date;

  if (t1 < t2)
    return 1;
  if (t1 > t2)
    return -1;

  return 0;
}

static gint
numeric_compare (GtkCList * clist,
		 gconstpointer ptr1,
		 gconstpointer ptr2)
{
  LibBalsaMessage *m1, *m2;
  glong t1, t2;

  GtkCListRow *row1 = (GtkCListRow *) ptr1;
  GtkCListRow *row2 = (GtkCListRow *) ptr2;

  m1 = row1->data;
  m2 = row2->data;

  if (!m1 || !m2)
    return 0;

  t1 = m1->msgno;
  t2 = m2->msgno;

  if (t1 < t2)
    return 1;
  if (t1 > t2)
    return -1;

  return 0;
}

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

      balsa_index_type = gtk_type_unique (gtk_clist_get_type (), &balsa_index_info);
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
		    gtk_marshal_NONE__POINTER_POINTER,
		    GTK_TYPE_NONE, 2, GTK_TYPE_POINTER,
		    GTK_TYPE_GDK_EVENT);
  balsa_index_signals[UNSELECT_MESSAGE] =
    gtk_signal_new ("unselect_message",
		    GTK_RUN_LAST,
		    object_class->type,
		    GTK_SIGNAL_OFFSET (BalsaIndexClass, unselect_message),
		    gtk_marshal_NONE__POINTER_POINTER,
		    GTK_TYPE_NONE, 2, GTK_TYPE_POINTER,
		    GTK_TYPE_GDK_EVENT);
  gtk_object_class_add_signals (object_class, balsa_index_signals,
                                LAST_SIGNAL);

  klass->select_message = NULL;
  klass->unselect_message = NULL;
}

static void
clist_click_column (GtkCList * clist, gint column, gpointer data)
{
  if (column == clist->sort_column)
    {
       clist->sort_type = (clist->sort_type == GTK_SORT_ASCENDING) ?
	  GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
    }
  else
    gtk_clist_set_sort_column (clist, column);

  switch(column) {
     case 0:
	gtk_clist_set_compare_func (clist, numeric_compare);
	break;
     case 5:
	gtk_clist_set_compare_func (clist, date_compare);
	break;
     default:
	gtk_clist_set_compare_func (clist, NULL);
  }

  gtk_clist_sort (clist);
}

static void
balsa_index_init (BalsaIndex * bindex)
{
  GtkCList *clist;
/*
 * status
 * priority
 * attachments
 */
  static gchar *titles[] =
  {
    "#",
    "S",
    "A",
    NULL,
    NULL,
    NULL
  };

  titles[3] = _("From");
  titles[4] = _("Subject");
  titles[5] = _("Date");

  bindex->mailbox = NULL;

  /* create the clist */
  gtk_clist_construct (GTK_CLIST(bindex), 6, titles);
  clist = GTK_CLIST(bindex);

  gtk_signal_connect (GTK_OBJECT (clist), "click_column",
		      GTK_SIGNAL_FUNC (clist_click_column),
		      NULL);

  gtk_clist_set_selection_mode (clist, GTK_SELECTION_EXTENDED);
  gtk_clist_set_column_justification (clist, 0, GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_justification (clist, 1, GTK_JUSTIFY_CENTER);
  gtk_clist_set_column_justification (clist, 2, GTK_JUSTIFY_CENTER);

/* Set the width of any new columns to the current column widths being used */
  gtk_clist_set_column_width (clist, 0, balsa_app.index_num_width);
  gtk_clist_set_column_width (clist, 1, balsa_app.index_status_width);
  gtk_clist_set_column_width (clist, 2, balsa_app.index_attachment_width);
  gtk_clist_set_column_width (clist, 3, balsa_app.index_from_width);
  gtk_clist_set_column_width (clist, 4, balsa_app.index_subject_width);
  gtk_clist_set_column_width (clist, 5, balsa_app.index_date_width);
  gtk_clist_set_row_height (clist, 16);

  /* Set default sorting behaviour */
  gtk_clist_set_sort_column (clist, 5);
  gtk_clist_set_compare_func (clist, date_compare);
  gtk_clist_set_sort_type (clist, GTK_SORT_DESCENDING);
    
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
  
  gtk_signal_connect (GTK_OBJECT (clist),
		      "button_release_event",
		      (GtkSignalFunc) button_event_release_cb,
		      (gpointer) bindex);

  /* We want to catch column resize attempts to store the new value */
  gtk_signal_connect (GTK_OBJECT (clist),
		      "resize_column",
		      GTK_SIGNAL_FUNC (resize_column_event_cb),
		      NULL);
  
  gtk_widget_show (GTK_WIDGET (clist));
  gtk_widget_ref (GTK_WIDGET (clist));
}

GtkWidget *
balsa_index_new (void)
{
  BalsaIndex *bindex;
  bindex = gtk_type_new (balsa_index_get_type ());
  return GTK_WIDGET (bindex);
}

/* 
 * This is an idle handler. Be sure to use gdk_threads_{enter/leave}
 */
static gboolean
moveto_handler (BalsaIndex * bindex)
{
  if (!GTK_WIDGET_VISIBLE (GTK_WIDGET (bindex)))
    return TRUE;

  gdk_threads_enter();
  gtk_clist_moveto (GTK_CLIST (bindex), bindex->first_new_message - 1, -1, 0.0, 0.0);
  gdk_threads_leave();

  return FALSE;
}

void
balsa_index_set_mailbox (BalsaIndex * bindex, LibBalsaMailbox * mailbox)
{
  GList *list;
  guint i = 0;
  
  g_return_if_fail (bindex != NULL);

  if (bindex->mailbox == mailbox)
    return;

  if (mailbox == NULL)
    return;

  /*
   * release the old mailbox
   */
  if (bindex->mailbox)
    {
      /* This will disconnect all of our signals */
      gtk_signal_disconnect_by_data (GTK_OBJECT(mailbox), bindex);

      libbalsa_mailbox_close (bindex->mailbox);

      gtk_clist_clear (GTK_CLIST (bindex));
    }

  /*
   * set the new mailbox
   */
  bindex->mailbox = mailbox;

  /*
   * rename "from" column to "to" for outgoing mail
   */
  if (mailbox == balsa_app.sentbox ||
      mailbox == balsa_app.draftbox ||
      mailbox == balsa_app.outbox)
    gtk_clist_set_column_title (GTK_CLIST (bindex), 3, _("To") );
  
  if (mailbox->open_ref == 0)
    libbalsa_mailbox_open (mailbox, FALSE);

  gtk_signal_connect(GTK_OBJECT(mailbox), "message-status-changed", 
		     GTK_SIGNAL_FUNC(mailbox_message_changed_status_cb),
		     (gpointer)bindex);
  gtk_signal_connect(GTK_OBJECT(mailbox), "message-new", 
		     GTK_SIGNAL_FUNC(mailbox_message_new_cb),
		     (gpointer)bindex);
  gtk_signal_connect(GTK_OBJECT(mailbox), "message-delete",
		     GTK_SIGNAL_FUNC(mailbox_message_delete_cb),
		     (gpointer)bindex);

  gtk_clist_freeze(GTK_CLIST (bindex));
  list = mailbox->message_list;
  while (list)
  {
    balsa_index_add(bindex, LIBBALSA_MESSAGE(list->data));
    list = list->next;
    i++;
  }
  gtk_clist_sort (GTK_CLIST (bindex));
  gtk_clist_thaw (GTK_CLIST (bindex));

  /* FIXME this might could be cleaned up some */
  if (bindex->first_new_message == 0)
    bindex->first_new_message = i;

  gtk_idle_add ((GtkFunction) moveto_handler, bindex);
}

void
balsa_index_add (BalsaIndex * bindex,
		 LibBalsaMessage * message)
{
  gchar buff1[32];
  gchar *text[6];
  gint row;
  GList *list;
  LibBalsaAddress *addy = NULL;
;
    
  g_return_if_fail (bindex != NULL);
  g_return_if_fail (message != NULL);

  if (bindex->mailbox == NULL)
    return;

  text[0] = "";
  text[1] = NULL;		/* flags */
  text[2] = NULL;		/* attachments */


  if (bindex->mailbox ==  balsa_app.sentbox ||
      bindex->mailbox ==  balsa_app.draftbox ||
      bindex->mailbox ==  balsa_app.outbox)
  {
      if (message->to_list)
      {
          list = g_list_first (message->to_list);
          addy = list->data;
      }
  } else {
      if (message->from)
	  addy = message->from;
  }

  if(addy)
      text[3] = addy->personal ? addy->personal : addy->mailbox;
  else
      text[3] = "";
  
  text[4] = message->subject;
  text[5] = libbalsa_message_date_to_gchar (message, balsa_app.date_string);

  row = gtk_clist_append (GTK_CLIST (bindex), text);

  g_free(text[5]);

  /* set message number */
  sprintf (buff1, "%d", row + 1);
  gtk_clist_set_text(GTK_CLIST (bindex), row, 0, buff1);

  gtk_clist_set_row_data (GTK_CLIST (bindex), row, (gpointer) message);

  clist_set_col_img_from_flag (bindex, row, message);

  if (bindex->first_new_message == 0)
    if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
      bindex->first_new_message = row + 1;
}

void
balsa_index_del (BalsaIndex * bindex,
		 LibBalsaMessage * message)
{
  gint row;

  g_return_if_fail (bindex != NULL);
  g_return_if_fail (message != NULL);

  if (bindex->mailbox == NULL)
    return;

  row = gtk_clist_find_row_from_data (GTK_CLIST (bindex), (gpointer) message);
  if (row<0) return;
  
  if (row == (bindex->first_new_message) )
      bindex->first_new_message = 0;
  
  gtk_clist_remove (GTK_CLIST (bindex),  row);
}

/* bi_get_largest_selected:
   helper function, finds the message with largest number among selected and
   fails with -1, if the selection is empty.
*/
static gint
bi_get_largest_selected(GtkCList * clist) {
  GList *list;
  gint i = 0;
  gint h = 0;

  if (!clist->selection)
     return -1;
  
  list = clist->selection;
  while (list)
    {
      i = GPOINTER_TO_INT (list->data);
      if (i > h) 
	h = i;
      list = g_list_next(list);
    }
  return h;
}


void
balsa_index_select_next (BalsaIndex * bindex)
{
  GtkCList *clist;
  gint h;
  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (bindex);

  if( (h=bi_get_largest_selected(clist)) < 0
     || h + 1 >= clist->rows)
     return;
  
  gtk_clist_unselect_all (clist);

  gtk_clist_select_row (clist, h + 1, -1);

  if (gtk_clist_row_is_visible (clist, h + 1) != GTK_VISIBILITY_FULL)
    gtk_clist_moveto (clist, h + 1, 0, 1.0, 0.0);
}

/* balsa_index_select_next_unread:
   search for the next unread in the current mailbox.
   wraps over if the selected message was the last one.
*/
void
balsa_index_select_next_unread (BalsaIndex * bindex)
{
  GtkCList *clist;
  LibBalsaMessage* message;
  gint h;

  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (bindex);

  if( (h=bi_get_largest_selected(clist)+1) <= 0)
     return;

  if (h >= clist->rows) 
    h = 0;

  while (h < clist->rows) {
    message = LIBBALSA_MESSAGE (gtk_clist_get_row_data (clist, h));
    if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
      gtk_clist_unselect_all (clist);
      
      gtk_clist_select_row (clist, h, -1);
      
      if (gtk_clist_row_is_visible (clist, h) != GTK_VISIBILITY_FULL)
        gtk_clist_moveto (clist, h, 0, 1.0, 0.0);
      return;
    }
    ++h;
  }
}


void
balsa_index_select_previous (BalsaIndex * bindex)
{
  GtkCList *clist;
  GList *list;
  gint i = 0;
  gint h = 0;

  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (bindex);

  if (!clist->selection)
    return;

  h = clist->rows;		/* set this to the max number of rows */

  list = clist->selection;
  while (list)			/* look for the selected row with the lowest number */
    {
      i = GPOINTER_TO_INT (list->data);
      if (i < h)
	h = i;
      list = list->next;
    }

  /* avoid unselecting everything, and then not selecting a valid row */
  if (h < 1)
    h = 1;

  /* FIXME, if it is already on row 1, we shouldn't unselect all/reselect */
  gtk_clist_unselect_all (clist);

  gtk_clist_select_row (clist, h - 1, -1);

  if (gtk_clist_row_is_visible (clist, h - 1) != GTK_VISIBILITY_FULL)
    gtk_clist_moveto (clist, h - 1, 0, 0.0, 0.0);
}

/* balsa_index_redraw_current redraws currently selected message,
   called when for example the message wrapping was switched on/off,
   the message canvas width has changed etc.
   FIXME: find a simpler way to do it.
*/
void
balsa_index_redraw_current (BalsaIndex * bindex)
{
  GtkCList *clist;
  gint h = 0;

  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (bindex);

  if (!clist->selection)
    return;

  h = GPOINTER_TO_INT(g_list_first(clist->selection)->data);
  gtk_clist_select_row (clist, h, -1);

  if (gtk_clist_row_is_visible (clist, h) != GTK_VISIBILITY_FULL)
    gtk_clist_moveto (clist, h, 0, 0.0, 0.0);
}

void
balsa_index_update_flag (BalsaIndex * bindex, LibBalsaMessage * message)
{
  gint row;

  g_return_if_fail (bindex != NULL);
  g_return_if_fail (message != NULL);

  row = gtk_clist_find_row_from_data (GTK_CLIST (bindex), message);
  if (row < 0)
    return;

  clist_set_col_img_from_flag (bindex, row, message);
}


static void
clist_set_col_img_from_flag (BalsaIndex * bindex, gint row, LibBalsaMessage * message)
{
  guint tmp;
  /* HEADER* current; */

  if (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
    gtk_clist_set_pixmap (GTK_CLIST (bindex), row, 1,
			  balsa_icon_get_pixmap (BALSA_ICON_TRASH),
			  balsa_icon_get_bitmap (BALSA_ICON_TRASH));
  else if (message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
    gtk_clist_set_pixmap (GTK_CLIST (bindex), row, 1,
        balsa_icon_get_pixmap (BALSA_ICON_FLAGGED),
        balsa_icon_get_bitmap (BALSA_ICON_FLAGGED));
/*
   if (message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
   gtk_clist_set_pixmap (GTK_CLIST (bindex), row, 1, , mailbox_mask);
 */
  else if (message->flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
    gtk_clist_set_pixmap (GTK_CLIST (bindex), row, 1,
			  balsa_icon_get_pixmap (BALSA_ICON_REPLIED),
			  balsa_icon_get_bitmap (BALSA_ICON_REPLIED));

  else if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
    gtk_clist_set_pixmap (GTK_CLIST (bindex), row, 1,
			  balsa_icon_get_pixmap (BALSA_ICON_ENVELOPE),
			  balsa_icon_get_bitmap (BALSA_ICON_ENVELOPE));
  else
    gtk_clist_set_text (GTK_CLIST (bindex), row, 1, NULL);

  tmp = libbalsa_message_has_attachment (message);
  
  if ( tmp ) {
          gtk_clist_set_pixmap (GTK_CLIST (bindex), row, 2,
                                balsa_icon_get_pixmap (BALSA_ICON_MULTIPART),
                                balsa_icon_get_bitmap (BALSA_ICON_MULTIPART));
  }
}


/* CLIST callbacks */

static void
button_event_press_cb (GtkCList * clist, GdkEventButton * event, gpointer data)
{
  gint row, column;
  LibBalsaMessage *message;
  BalsaIndex *bindex;

  if (event->window != clist->clist_window)
    return;

  if (!event || event->button != 3)
    return;
  
  gtk_clist_get_selection_info (clist, event->x, event->y, &row, &column);
  bindex = BALSA_INDEX (data);
  message = LIBBALSA_MESSAGE(gtk_clist_get_row_data (clist, row));

  gtk_clist_select_row (clist, row, -1);

  if (message)
    gtk_signal_emit (GTK_OBJECT (bindex),
		     balsa_index_signals[SELECT_MESSAGE],
		     message,
		     event);

}

static void
button_event_release_cb (GtkCList * clist, GdkEventButton * event,
                         gpointer data)
{
  gtk_grab_remove (GTK_WIDGET(clist));
  gdk_pointer_ungrab (event->time);
}

static void
select_message (GtkWidget * widget,
		gint row,
		gint column,
		GdkEventButton * bevent,
		gpointer * data)
{
  BalsaIndex *bindex;
  LibBalsaMessage *message;

  bindex = BALSA_INDEX (data);
  message = LIBBALSA_MESSAGE(gtk_clist_get_row_data (GTK_CLIST (widget), row));
  
  if (message){
    gtk_signal_emit (GTK_OBJECT (bindex),
		     balsa_index_signals[SELECT_MESSAGE],
		     message,
		     bevent);
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
  LibBalsaMessage *message;

  bindex = BALSA_INDEX (data);
  message = LIBBALSA_MESSAGE(gtk_clist_get_row_data (GTK_CLIST (widget), row));

  if (message)
    gtk_signal_emit (GTK_OBJECT (bindex),
		     balsa_index_signals[UNSELECT_MESSAGE],
		     message,
		     bevent);
}

/* When a column is resized, store the new size for later use */
static void 
resize_column_event_cb (GtkCList * clist, 
				    gint column, 
				    gint width, 
				    gpointer * data)
{
  switch (column)
  {
  case 0:
    balsa_app.index_num_width = width;
    break;
    
  case 1:
    balsa_app.index_status_width = width;
    break;
    
  case 2:
    balsa_app.index_attachment_width = width;
    break;
    
  case 3:
    balsa_app.index_from_width = width;
    break;
    
  case 4:
    balsa_app.index_subject_width = width;
    break;
    
  case 5:
    balsa_app.index_date_width = width;
    break;

  default:
    if (balsa_app.debug)
      fprintf (stderr, "** Error: Unknown column resize\n");
  }
}

/* Mailbox Callbacks... */
static void mailbox_message_changed_status_cb(LibBalsaMailbox *mb, LibBalsaMessage *message, BalsaIndex *bindex)
{
  balsa_index_update_flag (bindex, message);

}

static void mailbox_message_new_cb (LibBalsaMailbox *mb, LibBalsaMessage *message, BalsaIndex *bindex)
{
  gnome_triggers_do ("You have new mail!", "email", "newmail", NULL);
  balsa_index_add (bindex, message);
  
}

static void mailbox_message_delete_cb (LibBalsaMailbox *mb, LibBalsaMessage *message, BalsaIndex *bindex)
{
  balsa_index_del (bindex, message);
}

/* 
 * get_selected_rows :
 *
 * return the rows currently selected in the index
 *
 * @bindex : balsa index widget to retrieve the selection from
 * @rows : a pointer on the return array of rows. This array will
 *        contain tyhe selected rows.
 * @nb_rows : a pointer on the returned number of selected rows  
 *
 */
void 
balsa_index_get_selected_rows( BalsaIndex *bindex, guint **rows, guint *nb_rows )
{
  GList *list_of_selected_rows;
  GtkCList *clist;
  guint nb_selected_rows;
  guint *selected_rows;
  guint row_count;

  clist = GTK_CLIST(bindex);

  /* retreive the selection  */
  list_of_selected_rows = clist->selection;
  nb_selected_rows = g_list_length( list_of_selected_rows );

  selected_rows = (guint *)g_malloc( nb_selected_rows * sizeof(guint) );
  for (row_count=0; row_count<nb_selected_rows; row_count++)
    {
      selected_rows[row_count] = (guint)(list_of_selected_rows->data);
      list_of_selected_rows = list_of_selected_rows->next;
    }

  /* return the result of the search */
  *nb_rows = nb_selected_rows;
  *rows = selected_rows;

  return;
}


/* balsa_index_refresh [MBG]
 * 
 * bindex:  The BalsaIndex that is to be updated
 * 
 * Description: This function updates the mailbox index, used in
 * situations such as when we are loading a number of new messages
 * into a mailbox that is already open.
 * 
 * */
void
balsa_index_refresh (BalsaIndex * bindex)
{
        GList* list;
        gint i;
        gint newrow;
        LibBalsaMessage* old_message;
        
        g_return_if_fail (bindex != NULL);
        g_return_if_fail (bindex->mailbox != NULL);        

        gtk_clist_freeze (GTK_CLIST (bindex));

        old_message = gtk_clist_get_row_data (GTK_CLIST (bindex),  bi_get_largest_selected (GTK_CLIST (bindex)) );
        gtk_clist_unselect_all (GTK_CLIST (bindex));
        gtk_clist_clear (GTK_CLIST (bindex));

        list = bindex->mailbox->message_list;
        i = 0;
        while (list)
        {
                balsa_index_add(bindex, LIBBALSA_MESSAGE(list->data));
                list = list->next;
                i++;
        }

        gtk_clist_sort (GTK_CLIST (bindex));

        if (old_message)
                newrow = gtk_clist_find_row_from_data (GTK_CLIST (bindex), old_message);
        else
                newrow = -1;

        if (newrow >= 0) {
                gtk_clist_select_row (GTK_CLIST (bindex), gtk_clist_find_row_from_data (GTK_CLIST (bindex), old_message), -1);
                i = newrow;
        } else {
                gtk_clist_select_row (GTK_CLIST (bindex), i, -1);
        }

        if (gtk_clist_row_is_visible (GTK_CLIST (bindex), i) != GTK_VISIBILITY_FULL)
          gtk_clist_moveto (GTK_CLIST (bindex), i, 0, 0.0, 0.0);
        
        gtk_clist_thaw(GTK_CLIST (bindex));
}
