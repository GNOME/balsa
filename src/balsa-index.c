/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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

/* SORTING METHOD discussion:
   auto_sort() is NOT used to sort the messages since the compare methods 
   (numeric_compare, date_compare) use information from attached mailbox
   which is unavailable at the insertion time. We have to sort after every
   insertion which is not a big loose: NlnN process against sorted 
   insersion N (though the prefactor is much bigger in the former case).

   The alternative is to create a hidden column containing the sorting
   key and replace the key on every change of the sort method.  
*/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <glib.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-index-threading.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "main-window.h"
#include "message-window.h"
#include "sendmsg-window.h"
#include "store-address.h"

/* constants */
#define BUFFER_SIZE 1024

#define CLIST_WORKAROUND
#if defined(CLIST_WORKAROUND)
#define DO_CLIST_WORKAROUND(s) if((s)->row_list) (s)->row_list->prev = NULL;
#else
#define DO_CLIST_WORKAROUND(s)
#endif

/* gtk widget */
static void balsa_index_class_init(BalsaIndexClass * klass);
static void balsa_index_init(BalsaIndex * bindex);
static void balsa_index_close_and_destroy(GtkObject * obj);

static gint date_compare(GtkCList * clist, gconstpointer ptr1,
			 gconstpointer ptr2);
static gint numeric_compare(GtkCList * clist, gconstpointer ptr1,
			    gconstpointer ptr2);
static gint size_compare(GtkCList * clist, gconstpointer ptr1,
                            gconstpointer ptr2);
static void clist_click_column(GtkCList * clist, gint column,
			       gpointer data);

/* statics */
static void balsa_index_set_col_images(BalsaIndex *, GtkCTreeNode*,
				       LibBalsaMessage *);

/* mailbox callbacks */
static void balsa_index_del (BalsaIndex * bindex, LibBalsaMessage * message);
static void mailbox_message_changed_status_cb(LibBalsaMailbox * mb,
					      LibBalsaMessage * message,
					      BalsaIndex * bindex);
static void mailbox_message_new_cb(BalsaIndex * bindex,
				   LibBalsaMessage * message);
static void mailbox_messages_new_cb(BalsaIndex * bindex, GList* messages);
static void mailbox_message_delete_cb(BalsaIndex * bindex, 
				      LibBalsaMessage * message);
static void mailbox_messages_delete_cb(BalsaIndex * bindex, 
				       GList  * message);

/* clist callbacks */
static void button_event_press_cb(GtkWidget * clist, GdkEventButton * event,
				  gpointer data);
static void button_event_release_cb(GtkWidget * clist,
				    GdkEventButton * event, gpointer data);
static void select_message(GtkWidget * widget, GtkCTreeNode *row, gint column,
			   gpointer data);
static void unselect_message(GtkWidget * widget, GtkCTreeNode *row, 
                             gint column,
			     gpointer data);
static void unselect_all_messages (GtkCList* clist, gpointer user_data);

static void resize_column_event_cb(GtkCList * clist, gint column,
				   gint width, gpointer data);


/* formerly balsa-index-page stuff */
enum {
    TARGET_MESSAGES
};

static GtkTargetEntry index_drag_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

static gint handler = 0;

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))


static gboolean idle_handler_cb(GtkWidget * widget);
static void balsa_index_drag_cb(GtkWidget* widget,
                                GdkDragContext* drag_context,
                                GtkSelectionData* data,
                                guint info,
                                guint time,
                                gpointer user_data);
static void replace_attached_data(GtkObject * obj, const gchar * key, 
                                  GtkObject * data);
static GtkWidget* create_menu(BalsaIndex * bindex);
static void create_stock_menu_item(GtkWidget * menu, const gchar * type,
                                   const gchar * label, GtkSignalFunc cb,
                                   gpointer data, gboolean sensitive);

/* static void index_button_press_cb(GtkWidget * widget, */
/* 				  GdkEventButton * event, gpointer data); */

/* menu item callbacks */

static gint close_if_transferred_cb(BalsaMBList * bmbl, GdkEvent * event,
				    BalsaIndex * bi);
static void transfer_messages_cb(GtkCTree * ctree, GtkCTreeNode * row, 
				 gint column, gpointer data);

static void sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data);





/* signals */
enum {
    SELECT_MESSAGE,
    UNSELECT_MESSAGE,
    UNSELECT_ALL_MESSAGES,
    LAST_SIGNAL
};


/* marshallers */
typedef void (*BalsaIndexSignal1) (GtkObject * object,
				   LibBalsaMessage * message,
				   GdkEventButton * bevent, gpointer data);

static gint balsa_index_signals[LAST_SIGNAL] = {
    0
};

static GtkScrolledWindowClass *parent_class = NULL;


guint
balsa_index_get_type()
{
    static guint balsa_index_type = 0;

    if (!balsa_index_type) {
	GtkTypeInfo balsa_index_info = {
	    "BalsaIndex",
	    sizeof(BalsaIndex),
	    sizeof(BalsaIndexClass),
	    (GtkClassInitFunc) balsa_index_class_init,
	    (GtkObjectInitFunc) balsa_index_init,
	    (GtkArgSetFunc) NULL,
	    (GtkArgGetFunc) NULL
	};

	balsa_index_type =
	    gtk_type_unique(gtk_scrolled_window_get_type(), &balsa_index_info);
    }

    return balsa_index_type;
}


static void
balsa_index_class_init(BalsaIndexClass * klass)
{
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;
    GtkContainerClass *container_class;

    object_class = (GtkObjectClass *) klass;
    widget_class = (GtkWidgetClass *) klass;
    container_class = (GtkContainerClass *) klass;

    parent_class = gtk_type_class(GTK_TYPE_SCROLLED_WINDOW);

    balsa_index_signals[SELECT_MESSAGE] =
	gtk_signal_new("select_message",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaIndexClass, select_message),
		       gtk_marshal_NONE__POINTER_POINTER,
		       GTK_TYPE_NONE, 2, GTK_TYPE_POINTER,
		       GTK_TYPE_GDK_EVENT);
    balsa_index_signals[UNSELECT_MESSAGE] =
	gtk_signal_new("unselect_message",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaIndexClass,
					 unselect_message),
		       gtk_marshal_NONE__POINTER_POINTER, GTK_TYPE_NONE, 2,
		       GTK_TYPE_POINTER, GTK_TYPE_GDK_EVENT);
    balsa_index_signals[UNSELECT_ALL_MESSAGES] = 
        gtk_signal_new ("unselect_all_messages",
                        GTK_RUN_FIRST,
                        object_class->type,
                        GTK_SIGNAL_OFFSET (BalsaIndexClass, 
                                           unselect_all_messages),
                        gtk_marshal_NONE__NONE, 
                        GTK_TYPE_NONE, 0);

    gtk_object_class_add_signals(object_class, balsa_index_signals,
				 LAST_SIGNAL);

    object_class->destroy = balsa_index_close_and_destroy;
    klass->select_message = NULL;
    klass->unselect_message = NULL;
}



static void
balsa_index_init(BalsaIndex * bindex)
{
    GtkCList *clist;
    GtkObject* adj;
    GdkFont *font;
    int row_height;

    
    /* status
     * priority
     * attachments
     */
    static gchar *titles[] = {
	"#",
	"S",
	"A",
	NULL,
	NULL,
	NULL,
	NULL
    };

    /* FIXME: */
    titles[3] = _("From");
    titles[4] = _("Subject");
    titles[5] = _("Date");
    titles[6] = _("Size");

    bindex->mailbox_node = NULL;
    adj = gtk_adjustment_new (0.0, 0.0, 10.0, 1.0, 1.0, 1.0);
    gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (bindex), 
                                         GTK_ADJUSTMENT (adj));
    adj = gtk_adjustment_new (0.0, 0.0, 10.0, 1.0, 1.0, 1.0);
    gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (bindex), 
                                         GTK_ADJUSTMENT (adj));
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (bindex),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    
    /* create the clist */
    bindex->ctree = GTK_CTREE (gtk_ctree_new_with_titles (7, 4, titles));
    clist = GTK_CLIST(bindex->ctree);
    gtk_container_add (GTK_CONTAINER (bindex), GTK_WIDGET (bindex->ctree));

    gtk_signal_connect(GTK_OBJECT(clist), "click_column",
		       GTK_SIGNAL_FUNC(clist_click_column), bindex);

    gtk_clist_set_selection_mode(clist, GTK_SELECTION_EXTENDED);
    gtk_clist_set_column_justification(clist, 0, GTK_JUSTIFY_RIGHT);
    gtk_clist_set_column_justification(clist, 1, GTK_JUSTIFY_CENTER);
    gtk_clist_set_column_justification(clist, 2, GTK_JUSTIFY_CENTER);
    gtk_clist_set_column_justification(clist, 6, GTK_JUSTIFY_RIGHT);

    /* Set the width of any new columns to the current column widths being used */
    gtk_clist_set_column_width(clist, 0, balsa_app.index_num_width);
    gtk_clist_set_column_width(clist, 1, balsa_app.index_status_width);
    gtk_clist_set_column_width(clist, 2, balsa_app.index_attachment_width);
    gtk_clist_set_column_width(clist, 3, balsa_app.index_from_width);
    gtk_clist_set_column_width(clist, 4, balsa_app.index_subject_width);
    gtk_clist_set_column_width(clist, 5, balsa_app.index_date_width);
    font = gtk_widget_get_style (GTK_WIDGET(clist))->font;
    row_height = font->ascent + font->descent+2;
    
    if(row_height<16) /* pixmap height */
	gtk_clist_set_row_height(clist, 16);

    /* Set default sorting behaviour */
    gtk_clist_set_sort_column(clist, 5);
    gtk_clist_set_compare_func(clist, date_compare);
    gtk_clist_set_sort_type(clist, GTK_SORT_DESCENDING);

    /* handle select row signals to display message in the window
     * preview pane */
    gtk_signal_connect(GTK_OBJECT(bindex->ctree),
		       "tree-select-row",
		       (GtkSignalFunc) select_message, (gpointer) bindex);

    gtk_signal_connect(GTK_OBJECT(bindex->ctree),
		       "tree-unselect-row",
		       (GtkSignalFunc) unselect_message,
		       (gpointer) bindex);

    gtk_signal_connect (GTK_OBJECT(bindex->ctree),
                        "unselect-all",
                        (GtkSignalFunc) unselect_all_messages, 
                        (gpointer) bindex);
    
    /* we want to handle button presses to pop up context menus if
     * necessary */
    gtk_signal_connect(GTK_OBJECT(bindex->ctree),
		       "button_press_event",
		       (GtkSignalFunc) button_event_press_cb,
		       (gpointer) bindex);

    gtk_signal_connect(GTK_OBJECT(bindex->ctree),
		       "button_release_event",
		       (GtkSignalFunc) button_event_release_cb,
		       (gpointer) bindex);

    /* We want to catch column resize attempts to store the new value */
    gtk_signal_connect(GTK_OBJECT(clist),
		       "resize_column",
		       GTK_SIGNAL_FUNC(resize_column_event_cb), NULL);

    gtk_drag_source_set(GTK_WIDGET (bindex->ctree), 
                        GDK_BUTTON1_MASK | GDK_SHIFT_MASK | GDK_CONTROL_MASK,
                        index_drag_types, ELEMENTS(index_drag_types),
                        GDK_ACTION_DEFAULT | GDK_ACTION_COPY | 
                        GDK_ACTION_MOVE);
    gtk_signal_connect(GTK_OBJECT(bindex->ctree), "drag-data-get",
                       GTK_SIGNAL_FUNC(balsa_index_drag_cb), NULL);

    g_get_current_time (&bindex->last_use);
    GTK_OBJECT_UNSET_FLAGS (bindex->ctree, GTK_CAN_FOCUS);
    gtk_widget_show_all (GTK_WIDGET(bindex));
    gtk_widget_ref (GTK_WIDGET(bindex));
}

GtkWidget *
balsa_index_new(void)
{
    BalsaIndex* bindex;
    bindex = BALSA_INDEX (gtk_type_new(BALSA_TYPE_INDEX));
    gtk_object_default_construct (GTK_OBJECT (bindex));
    return GTK_WIDGET(bindex);
}


static gint
date_compare(GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2)
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
numeric_compare(GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2)
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

static gint
size_compare(GtkCList * clist, gconstpointer ptr1, gconstpointer ptr2)
{
    LibBalsaMessage *m1, *m2;
    glong t1, t2;

    GtkCListRow *row1 = (GtkCListRow *) ptr1;
    GtkCListRow *row2 = (GtkCListRow *) ptr2;

    m1 = row1->data;
    m2 = row2->data;

    g_return_val_if_fail(m1, 0);
    g_return_val_if_fail(m2, 0);

    if (balsa_app.line_length) {
        t1 = LIBBALSA_MESSAGE_GET_LINES(m1);
        t2 = LIBBALSA_MESSAGE_GET_LINES(m2);
    } else {
        t1 = LIBBALSA_MESSAGE_GET_LENGTH(m1);
        t2 = LIBBALSA_MESSAGE_GET_LENGTH(m2);
    }

    return t2-t1;
}

/* bi_get_largest_selected:
   helper function, finds the message with largest number among selected and
   fails with -1, if the selection is empty.
*/
static gint
bi_get_largest_selected(GtkCList * clist)
{
    GList *list;
    gint i = 0;
    gint h = 0;

    if (!clist->selection)
	return -1;

    list = clist->selection;
    while (list) {
	i = gtk_clist_find_row_from_data(clist,
					 LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data(GTK_CTREE(clist), list->data)));
	if (i > h)
	    h = i;
	list = g_list_next(list);
    }
    return h;
}


static void
clist_click_column(GtkCList * clist, gint column, gpointer data)
{
    gint h;
    GtkSortType sort_type = clist->sort_type;

    if (column == clist->sort_column)
	sort_type = (sort_type == GTK_SORT_ASCENDING) ?
	    GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
	
    balsa_index_set_sort_order(BALSA_INDEX(data), column, sort_type);
    gtk_clist_sort(clist);
    DO_CLIST_WORKAROUND(clist);

    if ((h = bi_get_largest_selected(clist)) >= 0 &&
	gtk_clist_row_is_visible(clist, h) != GTK_VISIBILITY_FULL)
	gtk_clist_moveto(clist, h, 0, 1.0, 0.0);
}


/* 
 * Search the index, locate the first unread message, and update the
 * index's first_new_message field to point at that message.  If no
 * unread messages are found, first_new_message is set to NULL
 */
void 
balsa_index_set_first_new_message(BalsaIndex * bindex)
{

    LibBalsaMessage* message;
    gint i = 0;
    
    g_return_if_fail(bindex != NULL);
    bindex->first_new_message = NULL;
    
    while (i < GTK_CLIST(bindex->ctree)->rows) {
        message = LIBBALSA_MESSAGE(
            gtk_clist_get_row_data(GTK_CLIST(bindex->ctree), i));
        
        if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
            bindex->first_new_message = message;
            return;
        }
        ++i;
    }
}



/* 
 * This is an idle handler. Be sure to use gdk_threads_{enter/leave}
 * 
 * Description: moves to the first unread message in the index, and
 * selects it.
 */
static gboolean
moveto_handler(BalsaIndex * bindex)
{
    gint row = -1;
    gpointer row_data = NULL;
    GList* list = NULL;
    

    if (!GTK_WIDGET_VISIBLE(GTK_WIDGET(bindex)))
	return TRUE;

    gdk_threads_enter();

    if (bindex->first_new_message != NULL) {
        row_data = bindex->first_new_message;
    } else if ((list = GTK_CLIST (bindex->ctree)->selection) != NULL) {
        list = g_list_last (list);
        row_data = list->data;
    } else {
        row_data = gtk_clist_get_row_data(GTK_CLIST(bindex->ctree),
                                          GTK_CLIST(bindex->ctree)->rows - 1);
    }
        
        
    if (row_data) {
        row = gtk_clist_find_row_from_data (GTK_CLIST (bindex->ctree),
                                            row_data);
	
        if (balsa_app.view_message_on_open)
            balsa_index_select_row (bindex, row);
	else
	   gtk_clist_moveto (GTK_CLIST (bindex->ctree), row, -1, 0.5, 0.0);
    }
    
    gdk_threads_leave();

    return FALSE;
}

/* balsa_index_load_mailbox_node:
   open mailbox_node, the opening is done in thread to keep UI alive.
   NOTES:
   it uses module-wide is_opening variable. This variable, as well as the
   mutex should be a property of BalsaIndex but since we cannot open 
   two indexes at once because of libmutt limits, we get away with this
   solution. When the backend is changed, feel free to introduce these
   changes.
   Also, the waiting list is not a top hack. I mean it is perfectly 
   functional (and that's MOST important; think long before modyfying it)
   but perhaps we could write it nicer?
*/

gboolean
balsa_index_load_mailbox_node (BalsaIndex * bindex, BalsaMailboxNode* mbnode)
{
    LibBalsaMailbox* mailbox;
    gchar *msg;

    g_return_val_if_fail (bindex != NULL, TRUE);
    g_return_val_if_fail (mbnode != NULL, TRUE);
    g_return_val_if_fail (mbnode->mailbox != NULL, TRUE);

    mailbox = mbnode->mailbox;
    msg = g_strdup_printf(_("Opening mailbox %s. Please wait..."),
			  mbnode->mailbox->name);
    gnome_appbar_push(balsa_app.appbar, msg);
    g_free(msg);
    libbalsa_mailbox_open(mailbox);
    gnome_appbar_pop(balsa_app.appbar);

    if (mailbox->open_ref == 0)
	return TRUE;

    /*
     * release the old mailbox
     */
    if (bindex->mailbox_node && bindex->mailbox_node->mailbox) {
        mailbox = bindex->mailbox_node->mailbox;

	/* This will disconnect all of our signals */
	gtk_signal_disconnect_by_data(GTK_OBJECT(mailbox), bindex);
	libbalsa_mailbox_close(mailbox);
	gtk_clist_clear(GTK_CLIST(bindex->ctree));
    }

    /*
     * set the new mailbox
     */
    bindex->mailbox_node = mbnode;
    /*
     * rename "from" column to "to" for outgoing mail
     */
    if (mailbox == balsa_app.sentbox ||
	mailbox == balsa_app.draftbox || mailbox == balsa_app.outbox) {

	gtk_clist_set_column_title(GTK_CLIST(bindex->ctree), 3, _("To"));
    }

    gtk_signal_connect(GTK_OBJECT(mailbox), "message-status-changed",
		       GTK_SIGNAL_FUNC(mailbox_message_changed_status_cb),
		       (gpointer) bindex);
    gtk_signal_connect_object(GTK_OBJECT(mailbox), "message-new",
		       GTK_SIGNAL_FUNC(mailbox_message_new_cb),
		       (gpointer) bindex);
    gtk_signal_connect_object(GTK_OBJECT(mailbox), "messages-new",
		       GTK_SIGNAL_FUNC(mailbox_messages_new_cb),
		       (gpointer) bindex);
    gtk_signal_connect_object(GTK_OBJECT(mailbox), "message-delete",
		       GTK_SIGNAL_FUNC(mailbox_message_delete_cb),
		       (gpointer) bindex);
    gtk_signal_connect_object(GTK_OBJECT(mailbox), "messages-delete",
		       GTK_SIGNAL_FUNC(mailbox_messages_delete_cb),
		       (gpointer) bindex);

    /* do threading */
    balsa_index_set_sort_order(bindex, mbnode->sort_field, 
			       mbnode->sort_type);
    /* FIXME: this is an ugly way of doing it:
       override default mbost threading type with the global balsa
       default setting
    */
    mbnode->threading_type = balsa_app.threading_type;
    balsa_index_set_threading_type(bindex, mbnode->threading_type);
    balsa_index_set_first_new_message(bindex);

    gtk_idle_add((GtkFunction) moveto_handler, bindex);

    return FALSE;
}


void
balsa_index_add(BalsaIndex * bindex, LibBalsaMessage * message)
{
    gchar buff1[32];
    gchar *text[7];
    GtkCTreeNode *node;
    GList *list;
    LibBalsaAddress *addy = NULL;
    LibBalsaMailbox* mailbox;
    

    g_return_if_fail(bindex != NULL);
    g_return_if_fail(message != NULL);
    mailbox = bindex->mailbox_node->mailbox;
    
    if (mailbox == NULL)
	return;

    sprintf(buff1, "%ld", message->msgno + 1);
    text[0] = buff1;		/* set message number */
    text[1] = NULL;		/* flags */
    text[2] = NULL;		/* attachments */


    if (mailbox == balsa_app.sentbox ||
	mailbox == balsa_app.draftbox ||
	mailbox == balsa_app.outbox) {
	if (message->to_list) {
	    list = g_list_first(message->to_list);
	    addy = list->data;
	}
    } else {
	if (message->from)
	    addy = message->from;
    }

    if (addy) {
	if (addy->full_name)
	    text[3] = addy->full_name;
	else if (addy->address_list)
	    text[3] = addy->address_list->data;
	else
	    text[3] = "";
    } else
	text[3] = "";

    text[4] = (gchar*)LIBBALSA_MESSAGE_GET_SUBJECT(message);
    text[5] =
	libbalsa_message_date_to_gchar(message, balsa_app.date_string);
    text[6] =
	libbalsa_message_size_to_gchar(message, balsa_app.line_length);

    node = gtk_ctree_insert_node(GTK_CTREE(bindex->ctree), NULL, NULL, 
                                 text, 2, NULL, NULL, NULL, NULL, 
                                 FALSE, TRUE);
    g_free(text[5]);
    g_free(text[6]);

    gtk_ctree_node_set_row_data (GTK_CTREE (bindex->ctree), node, 
                                 (gpointer) message);

    balsa_index_set_col_images(bindex, node, message);

    DO_CLIST_WORKAROUND(GTK_CLIST(bindex->ctree));
}

/*
  See: http://mail.gnome.org/archives/balsa-list/2000-November/msg00212.html
  for discussion of the GtkCtree bug. The bug is triggered when one
  attempts to delete a collapsed message thread.
*/
void
balsa_index_del(BalsaIndex * bindex, LibBalsaMessage * message)
{
    gint row;
    gpointer row_data;
    GtkCTreeNode *node;

    g_return_if_fail(bindex != NULL);
    g_return_if_fail(message != NULL);

    if (bindex->mailbox_node->mailbox == NULL)
	return;

    node = gtk_ctree_find_by_row_data (GTK_CTREE (bindex->ctree), NULL, 
                                       (gpointer) message);

    if (node == NULL)
	return;

    if(bindex->first_new_message == message){
	bindex->first_new_message=NULL;
    }

    {
	GtkCTreeNode *children=GTK_CTREE_ROW(node)->children;
	if(children!=NULL){
	    GtkCTreeNode *sibling=GTK_CTREE_ROW(node)->parent;
	    GtkCTreeNode *next;
	    while(sibling!=NULL){
		if(GTK_CTREE_ROW(sibling)->parent==NULL)break;
		sibling=GTK_CTREE_ROW(sibling)->parent;
	    }
	    
	    if(sibling!=NULL)sibling=GTK_CTREE_ROW(sibling)->sibling;
	    else{sibling=GTK_CTREE_ROW(node)->sibling;}


	    /* BEGIN GtkCTree bug workaround. */
           if(sibling==NULL){
             gboolean expanded;
             gtk_ctree_get_node_info(GTK_CTREE(bindex->ctree), node,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     &expanded);
             if(!expanded)
               gtk_ctree_expand(GTK_CTREE(bindex->ctree), node);
           }
	   /* end GtkCTRee bug workaround. */

	    while(1){
		if(children==NULL)break;
		next=GTK_CTREE_ROW(children)->sibling;
		gtk_ctree_move(GTK_CTREE (bindex->ctree), 
                               children, NULL, sibling);
		children=next;
	    }
	    node=gtk_ctree_find_by_row_data (GTK_CTREE (bindex->ctree), 
					     NULL, (gpointer) message);
	}
    }


    row_data = gtk_ctree_node_get_row_data (bindex->ctree, node);
    row = gtk_clist_find_row_from_data (GTK_CLIST (bindex->ctree), row_data);
    gtk_clist_unselect_row (GTK_CLIST (bindex->ctree), row, -1);
    gtk_ctree_remove_node(GTK_CTREE(bindex->ctree), node);


}


/* balsa_index_select_row ()
 * 
 * Takes care of the actual selection, unselecting other messages and
 * making sure the selected row is within bounds and made visible.
 * */
void
balsa_index_select_row(BalsaIndex * bindex, gint row)
{
    GtkCList *clist;

    g_return_if_fail(bindex != NULL);
    g_return_if_fail(BALSA_IS_INDEX(bindex));

    clist = GTK_CLIST(bindex->ctree);

    if (row < 0) {
	if (clist->rows > 0)
	    row = 0;
	else
	    return;
    }

    if (row >= clist->rows) {
	if (clist->rows > 0)
	    row = clist->rows - 1;
	else
	    return;
    }

    gtk_clist_unselect_all(clist);
    gtk_clist_select_row(clist, row, -1);

    /* if (gtk_clist_row_is_visible (clist, row) != GTK_VISIBILITY_FULL) */
    gtk_clist_moveto(clist, row, -1, 0.5, 0.0);
}


/* balsa_index_select_next:
 * 
 * selects next message or last message when no messages are selected.
 * */
void
balsa_index_select_next(BalsaIndex * bindex)
{
    GtkCList *clist;
    gint h;

    g_return_if_fail(bindex != NULL);
    clist = GTK_CLIST(bindex->ctree);

    /* [MBG] check this part, it might need to be h - 2 instead */
    if ((h = bi_get_largest_selected(clist)) < 0 || h + 1 >= clist->rows)
	h = clist->rows - 1;

    balsa_index_select_row(bindex, h + 1);
}


/* 
 * select the first unread message in the index, otherwise select the
 * last message.
 */
void
balsa_index_select_first_unread(BalsaIndex* bindex)
{
    gint row;
    

    g_return_if_fail(bindex != NULL);
    
    balsa_index_set_first_new_message(bindex);

    if (bindex->first_new_message != NULL) {
        row = gtk_clist_find_row_from_data (GTK_CLIST(bindex->ctree), 
                                            bindex->first_new_message);
    } else {
        row = GTK_CLIST(bindex->ctree)->rows - 1;
    }
    
    balsa_index_select_row(bindex, row);
}



/* balsa_index_select_next_unread:
 * 
 * search for the next unread in the current mailbox.
 * wraps over if the selected message was the last one.
 * */
void
balsa_index_select_next_unread(BalsaIndex * bindex)
{
    GtkCList *clist;
    LibBalsaMessage *message;
    gint h, start_row;

    g_return_if_fail(bindex != NULL);
    clist = GTK_CLIST(bindex->ctree);

    if ((h = bi_get_largest_selected(clist) + 1) <= 0)
	h = 0;

    if (h >= clist->rows)
	h = 0;

    start_row = h;

    while (h < clist->rows) {
	message = LIBBALSA_MESSAGE(gtk_clist_get_row_data(clist, h));
	if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
	    balsa_index_select_row(bindex, h);
	    return;
	}
	++h;
    }

    /* We couldn't find it below our start position, try starting from
     * the beginning.
     * */
    h = 0;

    while (h < start_row) {
	message = LIBBALSA_MESSAGE(gtk_clist_get_row_data(clist, h));

	if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
	    balsa_index_select_row(bindex, h);
	    return;
	}
	++h;
    }
}

/* balsa_index_select_previous:
 * 
 * selects previous message or first message when no messages are selected.
 * */
void
balsa_index_select_previous(BalsaIndex * bindex)
{
    GtkCList *clist;
    GList *list;
    gint i = 0;
    gint h = 0;

    g_return_if_fail(bindex != NULL);
    clist = GTK_CLIST(bindex->ctree);

    if (!clist->selection)
	h = 1;
    else {
	h = clist->rows;	/* set this to the max number of rows */

	list = clist->selection;
	while (list) {		
            /* look for the selected row with the lowest number */
	    i = gtk_clist_find_row_from_data(clist,
					     LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data(bindex->ctree, list->data)));
	    if (i < h)
		h = i;
	    list = list->next;
	}

	/* avoid unselecting everything, and then not selecting a valid row */
	if (h < 1)
	    h = 1;
    }
    /* FIXME, if it is already on row 1, we shouldn't unselect all/reselect */
    balsa_index_select_row(bindex, h - 1);
}

/* balsa_index_redraw_current redraws currently selected message,
   called when for example the message wrapping was switched on/off,
   the message canvas width has changed etc.
   FIXME: find a simpler way to do it.
*/
void
balsa_index_redraw_current(BalsaIndex * bindex)
{
    GtkCList *clist;
    gint h = 0;

    g_return_if_fail(bindex != NULL);

    clist = GTK_CLIST(bindex->ctree);

    if (!clist->selection)
	return;

    h = gtk_clist_find_row_from_data(clist,
				     LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data(bindex->ctree, g_list_first(clist->selection)->data)));
    gtk_clist_select_row(clist, h, -1);

    if (gtk_clist_row_is_visible(clist, h) != GTK_VISIBILITY_FULL)
	gtk_clist_moveto(clist, h, 0, 0.0, 0.0);
}

void
balsa_index_update_flag(BalsaIndex * bindex, LibBalsaMessage * message)
{
    GtkCTreeNode* node;
    g_return_if_fail(bindex != NULL);
    g_return_if_fail(message != NULL);

    if( (node=gtk_ctree_find_by_row_data(GTK_CTREE(bindex->ctree), 
					 NULL, message)) )
	balsa_index_set_col_images(bindex, node, message);
}


static void
balsa_index_set_col_images(BalsaIndex * bindex, GtkCTreeNode *node,
			   LibBalsaMessage * message)
{
    guint tmp;
    GtkCTree* ctree;
    GdkPixmap* pixmap;
    GdkPixmap* bitmap;

    ctree = bindex->ctree;
    
    if (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
	gtk_ctree_node_set_pixmap(ctree, node, 1,
				  balsa_icon_get_pixmap(BALSA_ICON_TRASH),
				  balsa_icon_get_bitmap(BALSA_ICON_TRASH));
    else if (message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED) {
        gnome_stock_pixmap_gdk (BALSA_PIXMAP_FLAGGED, "regular", 
                                &pixmap, &bitmap);
	gtk_ctree_node_set_pixmap(ctree, node, 1, pixmap, bitmap);
    }
    
    else if (message->flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
	gtk_ctree_node_set_pixmap(ctree, node, 1,
				  balsa_icon_get_pixmap(BALSA_ICON_REPLIED),
				  balsa_icon_get_bitmap(BALSA_ICON_REPLIED));

    else if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
	gtk_ctree_node_set_pixmap(ctree, node, 1,
				  balsa_icon_get_pixmap(BALSA_ICON_ENVELOPE),
				  balsa_icon_get_bitmap(BALSA_ICON_ENVELOPE));
    else
	gtk_ctree_node_set_text(ctree, node, 1, NULL);

    tmp = libbalsa_message_has_attachment(message);

    if (tmp) {
	gtk_ctree_node_set_pixmap(ctree, node, 2,
				  balsa_icon_get_pixmap(BALSA_ICON_MULTIPART),
				  balsa_icon_get_bitmap(BALSA_ICON_MULTIPART));
    }
}


/* CLIST callbacks */

static void
button_event_press_cb(GtkWidget * widget, GdkEventButton * event, 
                      gpointer data)
{
    gint row, column;
    gint on_message;
    LibBalsaMessage *message;
    BalsaIndex *bindex;
    GtkCList* clist;

    if (!event)
	return;
    
    bindex = BALSA_INDEX(data);
    clist = GTK_CLIST (bindex->ctree);
    on_message = gtk_clist_get_selection_info(clist, event->x, event->y, 
                                              &row, &column);
    if (event && event->button == 3) {
        if (handler != 0)
            gtk_idle_remove(handler);

	gtk_menu_popup(GTK_MENU(create_menu(bindex)),
		       NULL, NULL, NULL, NULL,
		       event->button, event->time);
        return;
    } 

    if (on_message && 
	(message = LIBBALSA_MESSAGE(gtk_clist_get_row_data(clist, row))) ) {
	if (event && event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
            message_window_new (message);
        }
    }
}


static void
button_event_release_cb(GtkWidget * clist, GdkEventButton * event,
			gpointer data)
{
    gtk_grab_remove(clist);
    gdk_pointer_ungrab(event->time);
}


static void
select_message(GtkWidget * widget, GtkCTreeNode *row, gint column,
	       gpointer data)
{
    BalsaIndex *bindex;
    LibBalsaMessage *message;

    bindex = BALSA_INDEX(data);
    message = 
	LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data (GTK_CTREE(widget), row));

    replace_attached_data (GTK_OBJECT(bindex), "message", GTK_OBJECT(message));
    handler = gtk_idle_add ((GtkFunction) idle_handler_cb, bindex);

    if (message) {
	gtk_signal_emit(GTK_OBJECT(bindex),
			balsa_index_signals[SELECT_MESSAGE],
			message, NULL);
    }
}


static void
unselect_message(GtkWidget * widget, GtkCTreeNode *row, gint column,
		 gpointer data)
{
    BalsaIndex *bindex;
    LibBalsaMessage *message;

    bindex = BALSA_INDEX (data);
    message =
	LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data (GTK_CTREE(widget), row));

    if (message)
	gtk_signal_emit(GTK_OBJECT(bindex),
			balsa_index_signals[UNSELECT_MESSAGE],
			message, NULL);
}


static void
unselect_all_messages (GtkCList* clist, gpointer user_data)
{
    BalsaIndex *bindex;
    LibBalsaMessage *message;

    bindex = BALSA_INDEX (user_data);
    message =
	LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data (GTK_CTREE(clist), 0));

    if (message)
	gtk_signal_emit(GTK_OBJECT(bindex),
			balsa_index_signals[UNSELECT_ALL_MESSAGES], 
                        NULL);
}


/* When a column is resized, store the new size for later use */
static void
resize_column_event_cb(GtkCList * clist, gint column, gint width,
		       gpointer data)
{
    switch (column) {
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
	    fprintf(stderr, "** Error: Unknown column resize\n");
    }
}

/* Mailbox Callbacks... */
static void
mailbox_message_changed_status_cb(LibBalsaMailbox * mb,
				  LibBalsaMessage * message,
				  BalsaIndex * bindex)
{
    balsa_index_update_flag(bindex, message);
}

static void
mailbox_message_new_cb(BalsaIndex * bindex, LibBalsaMessage * message)
{
    gtk_clist_freeze(GTK_CLIST (bindex->ctree));
    balsa_index_add(bindex, message);
    if(bindex->mailbox_node->mailbox->new_messages==0){
      balsa_index_threading(bindex);
      gtk_clist_sort (GTK_CLIST (bindex->ctree));
      DO_CLIST_WORKAROUND(GTK_CLIST (bindex->ctree));
    }
    gtk_clist_thaw (GTK_CLIST (bindex->ctree));
    balsa_mblist_update_mailbox(balsa_app.mblist, 
                                bindex->mailbox_node->mailbox);
}

static void
mailbox_messages_new_cb(BalsaIndex * bindex, GList *messages)
{
    LibBalsaMessage * message;

    gtk_clist_freeze(GTK_CLIST (bindex->ctree));
    while(messages){
	message=(LibBalsaMessage *)(messages->data);
	balsa_index_add(bindex, message);
	messages=g_list_next(messages);
    }
    balsa_index_threading(bindex);
    gtk_clist_sort (GTK_CLIST (bindex->ctree));
    DO_CLIST_WORKAROUND(GTK_CLIST (bindex->ctree));
    gtk_clist_thaw (GTK_CLIST (bindex->ctree));

    balsa_mblist_update_mailbox(balsa_app.mblist, 
                                bindex->mailbox_node->mailbox);
}

static void
mailbox_message_delete_cb(BalsaIndex * bindex, LibBalsaMessage * message)
{
    balsa_index_del(bindex, message);
}

static void
mailbox_messages_delete_cb(BalsaIndex * bindex, GList * messages)
{
    LibBalsaMessage * message;
    gtk_clist_freeze(GTK_CLIST(bindex->ctree));

    while(messages){
	message=(LibBalsaMessage *)(messages->data);
	balsa_index_del(bindex, message);
	messages=g_list_next(messages);
    }

    gtk_clist_thaw(GTK_CLIST(bindex->ctree));
}

/* 
 * get_selected_rows :
 *
 * return the rows currently selected in the index
 *
 * @bindex : balsa index widget to retrieve the selection from
 * @rows : a pointer on the return array of rows. This array will
 *        contain the selected rows.
 * @nb_rows : a pointer on the returned number of selected rows  
 *
 */
void
balsa_index_get_selected_rows(BalsaIndex * bindex, GtkCTreeNode ***rows,
			      guint * nb_rows)
{
    GList *list_of_selected_rows;
    GtkCList *clist;
    guint nb_selected_rows;
    GtkCTreeNode **selected_rows;
    guint row_count;

    clist = GTK_CLIST(bindex->ctree);

    /* retreive the selection  */
    list_of_selected_rows = clist->selection;
    nb_selected_rows = g_list_length(list_of_selected_rows);

    selected_rows = (GtkCTreeNode **) g_malloc(nb_selected_rows * sizeof(GtkCTreeNode *));
    for (row_count = 0; row_count < nb_selected_rows; row_count++) {
	selected_rows[row_count] = (GtkCTreeNode *) (list_of_selected_rows->data);
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
balsa_index_refresh(BalsaIndex * bindex)
{
    GtkCList* clist;
    GList *list;
    gint i;
    gint newrow;
    LibBalsaMessage *old_message;

    g_return_if_fail(bindex != NULL);
    g_return_if_fail(bindex->mailbox_node->mailbox != NULL);

    clist = GTK_CLIST (bindex->ctree);
    gtk_clist_freeze(clist);

    old_message = 
        gtk_clist_get_row_data(clist, bi_get_largest_selected(clist));
    gtk_clist_unselect_all(clist);
    gtk_clist_clear(clist);

    list = bindex->mailbox_node->mailbox->message_list;
    i = 0;
    while (list) {
	balsa_index_add(bindex, LIBBALSA_MESSAGE(list->data));
	list = list->next;
	i++;
    }

    balsa_index_threading(bindex);
    gtk_clist_sort(clist);
    DO_CLIST_WORKAROUND(clist);

    if (old_message)
	newrow =
	    gtk_clist_find_row_from_data(clist, old_message);
    else
	newrow = -1;

    if (newrow >= 0) {
	gtk_clist_select_row(clist,
			     gtk_clist_find_row_from_data(clist, old_message),
			     -1);
	i = newrow;
    } else {
	gtk_clist_select_row(clist, i, -1);
    }

    if (gtk_clist_row_is_visible(clist, i) != GTK_VISIBILITY_FULL) 
        gtk_clist_moveto(clist, i, -1, 0.5, 0.0);

    gtk_clist_thaw(clist);
}

/* balsa_index_close_and_destroy:
 */

static void
balsa_index_close_and_destroy(GtkObject * obj)
{
    BalsaIndex *bindex;
    LibBalsaMailbox* mailbox;
    GtkObject* message;

    g_return_if_fail(obj != NULL);
    bindex = BALSA_INDEX(obj);

    /* remove idle callbacks and attached data */
    if(handler) 
        gtk_idle_remove(handler);
    message = gtk_object_get_data(obj, "message");
    if (message != NULL) {
        gtk_object_remove_data (obj, "message");
        gtk_object_unref (message);
    }

    /*page->window references our owner */
    if (bindex->mailbox_node && (mailbox = bindex->mailbox_node->mailbox) ) {
        gtk_signal_disconnect_by_data (GTK_OBJECT (mailbox), bindex);
	libbalsa_mailbox_close(mailbox);
	bindex->mailbox_node = NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
        (*GTK_OBJECT_CLASS(parent_class)->destroy) (obj);
}


void
balsa_message_reply(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;
    BalsaIndex* index;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	sm = sendmsg_window_new(widget, message, SEND_REPLY);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}


void
balsa_message_replytoall(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;
    BalsaIndex* index;


    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;
    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	sm = sendmsg_window_new(widget, message, SEND_REPLY_ALL);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}


void
balsa_message_replytogroup(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;
    BalsaIndex* index;


    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;
    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	sm = sendmsg_window_new(widget, message, SEND_REPLY_GROUP);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}


void
balsa_message_forward(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;
    BalsaIndex* index;
    

    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;
    while (list) {
	message =
	    gtk_ctree_node_get_row_data(index->ctree, list->data);
	sm = sendmsg_window_new(widget, message, SEND_FORWARD);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}


void
balsa_message_continue(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;
    BalsaIndex* index;


    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	sm = sendmsg_window_new(widget, message, SEND_CONTINUE);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}


static void
do_delete(BalsaIndex* index, gboolean move_to_trash)
{
    GList *list;
    BalsaIndex *trash = balsa_find_index_by_mailbox(balsa_app.trash);
    LibBalsaMessage *message;
    gboolean select_next = TRUE;
    GList *messages=NULL;
    LibBalsaMailbox *orig;
    gint message_count = 0;

    /* select the previous message if we're at the bottom of the index */
    if (GTK_CLIST(index->ctree)->rows - 1 == 
        bi_get_largest_selected(GTK_CLIST(index->ctree)))
        select_next = FALSE;

    
    for(list = GTK_CLIST(index->ctree)->selection; list; list = list->next) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	messages= g_list_append(messages, message);
	orig = message->mailbox; /* sloppy way */
	++message_count;
    }
    if(messages) {
	if (move_to_trash && (index != trash)) {
	    libbalsa_messages_move(messages, balsa_app.trash);
	} else
	    libbalsa_messages_delete(messages);
	g_list_free(messages);
    }
    
    /* select another message depending on where we are in the list */
    if (GTK_CLIST(index->ctree)->rows > message_count) {
        if (select_next)
            balsa_index_select_next(index);
	else
            balsa_index_select_previous(index);
    } 
    /* sync with backend AFTER adjacent message is selected.
       Update the style and message counts in the mailbox list */
    libbalsa_mailbox_sync_backend(index->mailbox_node->mailbox);
    balsa_mblist_update_mailbox(balsa_app.mblist, 
                                    index->mailbox_node->mailbox);
    
    // balsa_index_redraw_current(index);
}

void
balsa_message_move_to_trash(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);
    do_delete(BALSA_INDEX(user_data), TRUE);
}

void
balsa_message_delete(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);
    do_delete(BALSA_INDEX(user_data), FALSE);
}

void
balsa_message_undelete(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaIndex* index;


    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	libbalsa_message_undelete(message);
	list = list->next;
    }

    balsa_index_select_next(index);
}

gint
balsa_find_notebook_page_num(LibBalsaMailbox * mailbox)
{
    GtkWidget *index;
    guint i;

    for (i = 0;
	 (index =
	  gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
	 i++) {

	if (index != NULL && 
            BALSA_INDEX(index)->mailbox_node->mailbox == mailbox)

	    return i;
    }

    /* didn't find a matching mailbox */
    return -1;
}

/* This function toggles the FLAGGED attribute of a list of messages
 */
void
balsa_message_toggle_flagged(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    int is_all_flagged = TRUE;
    BalsaIndex* index;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;

    /* First see if we should unselect or select */
    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);

	if (!(message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)) {
	    is_all_flagged = FALSE;
	    break;
	}
	list = list->next;
    }

    /* If they are all flagged, then unflag them. Otherwise, flag them all */
    list = GTK_CLIST(index->ctree)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);

	if (is_all_flagged) {
	    libbalsa_message_unflag(message);
	} else {
	    libbalsa_message_flag(message);
	}

	list = list->next;
    }
    libbalsa_mailbox_sync_backend(index->mailbox_node->mailbox);
}


/* This function toggles the NEW attribute of a list of messages
 */
void
balsa_message_toggle_new(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    LibBalsaMessage *message;
    int is_all_read = TRUE;
    BalsaIndex* index;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;

    /* First see if we should mark as read or unread */
    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);

	if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
	    is_all_read = FALSE;
	    break;
	}
	list = list->next;
    }

    /* if all read mark as new, otherwise mark as read */
    list = GTK_CLIST(index->ctree)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);

	if (is_all_read) {
	    libbalsa_message_unread(message);
	} else {
	    libbalsa_message_read(message);
	}

	list = list->next;
    }
    libbalsa_mailbox_sync_backend(index->mailbox_node->mailbox);
}

/* balsa_index_reset:
   reset the mailbox content the hard way.
   DEPRECATED.

   This function should NEVER be used because it it time-consuming and
   flips the notebook pages. There are simpler ways to obtain
   equivalent effect.  
*/
void
balsa_index_reset(BalsaIndex * index)
{
    GtkWidget *current_index, *window;
    BalsaMailboxNode *mbnode;
    gint i, page_num;

    g_return_if_fail (index != NULL);

    mbnode = index->mailbox_node;
    window = index->window;

    i = gtk_notebook_current_page(GTK_NOTEBOOK(balsa_app.notebook));
    current_index =
	gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i);

    balsa_window_close_mbnode(BALSA_WINDOW(window), mbnode);
    balsa_window_open_mbnode(BALSA_WINDOW(window), mbnode);

    page_num = balsa_find_notebook_page_num(mbnode->mailbox);
    gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook), page_num);
}


void
balsa_index_update_message(BalsaIndex * index)
{
    gint row;
    GtkObject *message;
    GtkCList *list;

    list = GTK_CLIST(index->ctree);
    row = bi_get_largest_selected (list);

    if (row < 0)
	message = NULL;
    else
	message = GTK_OBJECT(gtk_clist_get_row_data(list, row));

    replace_attached_data(GTK_OBJECT(index), "message", message);

    if(handler) 
        gtk_idle_remove(handler);

    handler = gtk_idle_add ((GtkFunction) idle_handler_cb, index);
}


/* balsa_index_drag_cb 
 * 
 * This is the drag_data_get callback for the index widgets.  It
 * copies the list of selected messages to a pointer array, then sets
 * them as the DND data. Currently only supports DND within the
 * application.
 *  */
static void 
balsa_index_drag_cb (GtkWidget* widget, GdkDragContext* drag_context, 
               GtkSelectionData* data, guint info, guint time, 
               gpointer user_data)
{ 
    LibBalsaMessage* message;
    GPtrArray* message_array = NULL;
    GList* list = NULL;
    GtkCTree* ctree = NULL;
    

    g_return_if_fail (widget != NULL);
    ctree = GTK_CTREE (widget);
    list = GTK_CLIST (ctree)->selection;
    message_array = g_ptr_array_new ();
    
    while (list) {
        message = gtk_ctree_node_get_row_data (ctree, 
                                               list->data);
        g_ptr_array_add (message_array, message);
        list = list->next;
    }
    
    if (message_array) {
        g_ptr_array_add (message_array, NULL);
        gtk_selection_data_set (data, data->target, 8, 
                                (guchar*) message_array->pdata, 
                                (message_array->len)*sizeof (gpointer));
        /* the selection data makes a copy of the data, we 
         * can free it now. */
        g_ptr_array_free (message_array, FALSE);
    }
}


/* idle_handler_cb:
 * This is an idle handler, be sure to call use gdk_threads_{enter/leave}
 * No assumptions should be made about validity of the attached_data.
 */
static gboolean
idle_handler_cb(GtkWidget * widget)
{
    BalsaMessage *bmsg;
    LibBalsaMessage *message;
    BalsaIndex* index;
    /* gpointer data; */

    if (handler == 0)
        return FALSE;

    gdk_threads_enter();
    
    if (!widget) {
	handler = 0;
	gdk_threads_leave();
	return FALSE;
    }

    message = gtk_object_get_data(GTK_OBJECT(widget), "message");
 
    /* get the preview pane from the index page's BalsaWindow parent */
    index = BALSA_INDEX (widget);
    bmsg = BALSA_MESSAGE (BALSA_WINDOW (index->window)->preview);

    if (bmsg && BALSA_MESSAGE (bmsg)) {
	if (message) {
	    if(!balsa_message_set(BALSA_MESSAGE (bmsg), message))
		balsa_information
		    (LIBBALSA_INFORMATION_ERROR,
		     _("Cannot access the message's body\n"));
	}
	else
	    balsa_message_clear(BALSA_MESSAGE (bmsg));
    }

    handler = 0;
    /* replace_attached_data (GTK_OBJECT (widget), "message", NULL); */
    if (message != NULL) {
        gtk_object_remove_data (GTK_OBJECT (widget), "message");
        gtk_object_unref (GTK_OBJECT (message));
    }
    
    /* Update the style and message counts in the mailbox list */
    balsa_mblist_update_mailbox(balsa_app.mblist, 
                                index->mailbox_node->mailbox);

    gdk_threads_leave();
    return FALSE;
}



static GtkWidget *
create_menu(BalsaIndex * bindex)
{
    GtkWidget *menu, *menuitem, *submenu, *smenuitem;
    GtkWidget *bmbl, *scroll;
    GtkRequisition req;
    LibBalsaMailbox* mailbox;
    

    BALSA_DEBUG();
    mailbox = bindex->mailbox_node->mailbox;
    
    menu = gtk_menu_new();

    create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_RPL, _("Reply..."),
			   balsa_message_reply, bindex, TRUE);

    create_stock_menu_item(menu, BALSA_PIXMAP_MAIL_RPL_ALL_MENU,
			   _("Reply To All..."), balsa_message_replytoall,
			   bindex, TRUE);

    create_stock_menu_item(menu, BALSA_PIXMAP_MAIL_RPL_ALL_MENU,
			   _("Reply To Group..."), balsa_message_replytogroup,
			   bindex, TRUE);

    create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_FWD,
			   _("Forward..."), balsa_message_forward, bindex,
			   TRUE);

    create_stock_menu_item(menu, GNOME_STOCK_MENU_TRASH,
			   _("Delete"), balsa_message_delete, bindex,
			   !mailbox->readonly);
    if (mailbox == balsa_app.trash) {
	create_stock_menu_item(menu, GNOME_STOCK_MENU_UNDELETE,
			       _("Undelete"), balsa_message_undelete,
			       bindex, !mailbox->readonly);
    } else {
	create_stock_menu_item(menu, GNOME_STOCK_MENU_TRASH,
			       _("Move To Trash"), balsa_message_move_to_trash,
			       bindex, !mailbox->readonly);
    }

    create_stock_menu_item(menu, GNOME_STOCK_MENU_BOOK_RED,
			   _("Store Address..."),
			   balsa_store_address, bindex, TRUE);

    menuitem = gtk_menu_item_new_with_label(_("Toggle"));
    submenu = gtk_menu_new();
    
    create_stock_menu_item( submenu, BALSA_PIXMAP_FLAGGED, _("Flagged"),
			    balsa_message_toggle_flagged, bindex, TRUE);
     
    create_stock_menu_item( submenu, BALSA_PIXMAP_ENVELOPE, _("Unread"),
		    balsa_message_toggle_new, bindex, TRUE);

    gtk_widget_show(submenu);

    gtk_widget_show(menuitem);
    
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

    gtk_menu_append(GTK_MENU(menu), menuitem);
    
    menuitem = gtk_menu_item_new_with_label(_("Transfer"));
    gtk_widget_set_sensitive(menuitem, !mailbox->readonly);
    submenu = gtk_menu_new();

    smenuitem = gtk_menu_item_new();
    gtk_signal_connect (GTK_OBJECT(smenuitem), "button_release_event",
                        (GtkSignalFunc) close_if_transferred_cb,
                        (gpointer) bindex);

    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    bmbl = balsa_mblist_new();
    gtk_signal_connect(GTK_OBJECT(bmbl), "tree_select_row",
		       (GtkSignalFunc) transfer_messages_cb,
		       (gpointer) bindex);
    
    /* Force the mailbox list to be a reasonable size. */
    gtk_widget_size_request(bmbl, &req);
    if ( req.height > balsa_app.mw_height )
	req.height = balsa_app.mw_height;
	/* For the mailbox list width, we use the one used on the main window
	 * This is the user choice and required because the mblist widget
	 *  save the size in balsa_app.mblist_width */
	req.width=balsa_app.mblist_width;
    gtk_widget_set_usize(GTK_WIDGET(bmbl), req.width, req.height);

    gtk_container_add(GTK_CONTAINER(scroll), bmbl);
    gtk_container_add(GTK_CONTAINER(smenuitem), scroll);
    gtk_menu_append(GTK_MENU(submenu), smenuitem);

    gtk_widget_show(bmbl);
    gtk_widget_show(scroll);
    gtk_widget_show(smenuitem);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);

    return menu;
}


static void
create_stock_menu_item(GtkWidget * menu, const gchar * type,
		       const gchar * label, GtkSignalFunc cb,
		       gpointer data, gboolean sensitive)
{
    GtkWidget *menuitem = gnome_stock_menu_item(type, label);
    gtk_widget_set_sensitive(menuitem, sensitive);

    gtk_signal_connect(GTK_OBJECT(menuitem),
		       "activate", (GtkSignalFunc) cb, data);

    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);
}


static gint
close_if_transferred_cb(BalsaMBList * bmbl, GdkEvent * event,
			BalsaIndex * bi)
{
    if (gtk_object_get_data(GTK_OBJECT(bi), "transferredp") == NULL) {
        return TRUE;
    } else {
        gtk_object_remove_data (GTK_OBJECT (bi), "transferredp");
        return FALSE;
    }
}

static void
transfer_messages_cb(GtkCTree * ctree, GtkCTreeNode * row, gint column, 
		     gpointer data)
{
    GtkCList* clist;
    BalsaIndex* bindex = NULL;
    GList *list, *messages;
    LibBalsaMessage* message;
    BalsaMailboxNode *mbnode;
    gboolean select_next = TRUE;
    gint message_count = 0;

    g_return_if_fail(data != NULL);

    bindex = BALSA_INDEX (data);
    clist = GTK_CLIST(bindex->ctree);

    mbnode = gtk_ctree_node_get_row_data(ctree, row);

    if(mbnode->mailbox == NULL) return;

   /*Transferring to same mailbox? */
    if (bindex->mailbox_node->mailbox == mbnode->mailbox)
	return;

    /* select the previous message if we're at the bottom of the index */
    if (clist->rows - 1 == bi_get_largest_selected(clist))
        select_next = FALSE;


    messages=NULL;
    for (list = clist->selection; list;list = list->next) {
	message = gtk_ctree_node_get_row_data(GTK_CTREE(bindex->ctree), 
					      list->data);
	messages=g_list_append(messages, message);
	++message_count;
    }

    if(messages!=NULL) {
	libbalsa_messages_move(messages, mbnode->mailbox);
	g_list_free(messages);
    }
   
    /* select another message depending on where we are in the list */
    if (clist->rows > message_count) {
        if (select_next)
            balsa_index_select_next(bindex);
        else
            balsa_index_select_previous(bindex);
    }


    libbalsa_mailbox_sync_backend(bindex->mailbox_node->mailbox);

    gtk_object_set_data(GTK_OBJECT(bindex), "transferredp", (gpointer) 1);
}

static void
sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data)
{
    balsa_window_enable_continue();
}


/* replace_attached_data: 
   ref messages so the don't get destroyed in meantime.  
   QUESTION: is it possible that the idle is scheduled but
   then entire balsa-index object is destroyed before the idle
   function is executed? One can first try to handle all pending
   messages before closing...
*/

static void
replace_attached_data(GtkObject * obj, const gchar * key, GtkObject * data)
{
    GtkObject *old;

    if ((old = gtk_object_get_data(obj, key)))
	gtk_object_unref(old);

    gtk_object_set_data(obj, key, data);

    if (data)
	gtk_object_ref(data);
}

/* balsa_index_set_threading_type:
   FIXME: balsa_index_threading() requires that the index has been freshly
   recreated. This should not be necessary.
*/
void
balsa_index_set_threading_type(BalsaIndex * bindex, int thtype)
{
    GList *list;
    LibBalsaMailbox* mailbox = NULL;
    guint i=0;
    GtkCList *clist;
    BalsaMessage *msg;

    g_return_if_fail (bindex);
    g_return_if_fail (GTK_IS_CLIST(bindex->ctree));
    g_return_if_fail (bindex->mailbox_node != NULL);
    g_return_if_fail (bindex->mailbox_node->mailbox != NULL);

    clist = GTK_CLIST(bindex->ctree);
    bindex->threading_type = thtype;
    
    mailbox = bindex->mailbox_node->mailbox;

    gtk_clist_freeze(clist);
    gtk_clist_clear(clist);
    list = mailbox->message_list;
    
    while (list) {
	balsa_index_add(bindex, LIBBALSA_MESSAGE(list->data));
	list = list->next;
	i++;
    }

    /* do threading */
    balsa_index_threading(bindex);
    gtk_clist_sort(clist);
    DO_CLIST_WORKAROUND(clist);
    gtk_clist_thaw(clist);

    msg = BALSA_MESSAGE(balsa_app.main_window->preview);
    if ( msg && msg->message &&
	(i=gtk_clist_find_row_from_data(clist, msg->message))>=0
	&& gtk_clist_row_is_visible(clist, i) != GTK_VISIBILITY_FULL) {
	gtk_clist_moveto(clist, i, 0, 1.0, 0.0);
	gtk_clist_select_row(clist, i, 0);
    }

    /* set the menu apriopriately */
    balsa_window_set_threading_menu(thtype);
}

void
balsa_index_set_sort_order(BalsaIndex * bindex, int column, GtkSortType order)
{
    GtkCList * clist;
    g_return_if_fail(bindex->mailbox_node);
    g_return_if_fail(column>=0 && column <=6);
    g_return_if_fail(order == GTK_SORT_DESCENDING || 
		     order == GTK_SORT_ASCENDING);

    clist = GTK_CLIST(bindex->ctree);
    bindex->mailbox_node->sort_field = column;
    bindex->mailbox_node->sort_type  = order;
    clist->sort_type = order;
    gtk_clist_set_sort_column(clist, column);

    switch (column) {
    case 0:
	gtk_clist_set_compare_func(clist, numeric_compare);
	break;
    case 5:
	gtk_clist_set_compare_func(clist, date_compare);
	break;
    case 6:
        gtk_clist_set_compare_func(clist, size_compare);
        break;
    default:
	gtk_clist_set_compare_func(clist, NULL);
    }
}

void
balsa_index_refresh_size (GtkNotebook *notebook,
			  GtkNotebookPage *page,
			  gint page_num,
			  gpointer data)
{
    BalsaIndex *bindex;
    LibBalsaMessage * message;
    GtkWidget *index;
    GtkCTreeNode *node;
    GtkCTree *ctree;
    gchar *txt_new;
    gint j;

    if (page)
	index = page->child;
    else
	index = GTK_WIDGET(data);

    bindex = BALSA_INDEX(index);
    if (!bindex)
        return;

    if (bindex->line_length == balsa_app.line_length)
        return;

    bindex->line_length = balsa_app.line_length;

    ctree = GTK_CTREE(bindex->ctree);
    if (ctree) {
	j = 0;
	while ((node = gtk_ctree_node_nth (ctree, j))) {
	    message = (LibBalsaMessage*)
		gtk_ctree_node_get_row_data (ctree, node);
	    txt_new = libbalsa_message_size_to_gchar(message,
						     bindex->line_length);
	    gtk_ctree_node_set_text (ctree, node, 6, txt_new);
	    g_free (txt_new);
	    j++;
	}
    }
}

void
balsa_index_refresh_date (GtkNotebook *notebook,
			  GtkNotebookPage *page,
			  gint page_num,
			  gpointer data)
{
    BalsaIndex *bindex;
    LibBalsaMessage * message;
    GtkWidget *index;
    GtkCTreeNode *node;
    GtkCTree *ctree;
    gchar *txt_new;
    gint j;

    if (page)
	index = page->child;
    else
	index = GTK_WIDGET(data);

    bindex = BALSA_INDEX(index);
    if (!bindex)
        return;

    if (!strcmp (bindex->date_string, balsa_app.date_string))
        return;

    g_free (bindex->date_string);
    bindex->date_string = g_strdup (balsa_app.date_string);

    ctree = GTK_CTREE(bindex->ctree);
    if (ctree) {
	j = 0;
	while ((node = gtk_ctree_node_nth (ctree, j))) {
	    message = (LibBalsaMessage*)
		gtk_ctree_node_get_row_data (ctree, node);
	    txt_new = libbalsa_message_date_to_gchar(message,
						     bindex->date_string);
	    gtk_ctree_node_set_text (ctree, node, 5, txt_new);
	    g_free (txt_new);
	    j++;
	}
    }
}

