/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include "filter.h"
#include "filter-funcs.h"

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
static void bndx_match_struct_destroy(BalsaIndex * index);
static void balsa_index_set_sort_order(BalsaIndex * bindex, int column, 
				    GtkSortType order);
static void balsa_index_set_first_new_message(BalsaIndex * bindex);
/* adds a new message */
static void balsa_index_add(BalsaIndex * bindex, LibBalsaMessage * message);
/* retrieve the selection */
static void balsa_index_get_selected_rows(BalsaIndex * bindex,
					  GtkCTreeNode *** rows,
					  guint * nb_rows);
static void balsa_index_set_col_images(BalsaIndex *, GtkCTreeNode*,
				       LibBalsaMessage *);
static void balsa_index_set_style(BalsaIndex * bindex, GtkCTreeNode *node);
static void balsa_index_set_style_recursive(BalsaIndex * bindex, GtkCTreeNode *node);
static void balsa_index_set_parent_style(BalsaIndex *bindex, GtkCTreeNode *node);
static void balsa_index_check_visibility(GtkCList * clist,
                                         GtkCTreeNode * node,
                                         gfloat row_align);
static GtkCTreeNode *balsa_index_find_node(BalsaIndex * bindex,
                                           gboolean previous,
                                           LibBalsaMessageFlag flag,
					   gint op,GSList * conditions);
static void balsa_index_select_next_threaded(BalsaIndex * bindex);
static void balsa_index_transfer_messages(BalsaIndex * bindex,
                                          LibBalsaMailbox * mailbox);
static void balsa_index_idle_remove(gpointer data);
static void balsa_index_idle_add(gpointer data, gpointer message);
static gboolean balsa_index_idle_clear(gpointer data);
static gint balsa_index_most_recent_message(GtkCList * clist);
static void refresh_size(GtkCTree * ctree,
			 GtkCTreeNode *node,
			 gpointer data);
static void refresh_date(GtkCTree * ctree,
			 GtkCTreeNode *node,
			 gpointer data);



/* mailbox callbacks */
static void balsa_index_del (BalsaIndex * bindex, LibBalsaMessage * message);
static void mailbox_messages_changed_status_cb(LibBalsaMailbox * mb,
					       GList * messages,
					       gint flag,
					       BalsaIndex * bindex);
static void mailbox_messages_added(BalsaIndex * bindex, GList* messages);
static void mailbox_messages_added_cb(BalsaIndex * bindex, GList* messages);
static void mailbox_message_delete_cb(BalsaIndex * bindex, 
				      LibBalsaMessage * message);
static void mailbox_messages_removed(BalsaIndex * bindex, 
				    GList  * message);
static void mailbox_messages_removed_cb(BalsaIndex * bindex, 
				       GList  * message);

/* clist callbacks */
static void button_event_press_cb(GtkWidget * clist, GdkEventButton * event,
				  gpointer data);
static void select_message(GtkWidget * widget, GtkCTreeNode *row, gint column,
			   gpointer data);
static void unselect_message(GtkWidget * widget, GtkCTreeNode *row, 
                             gint column,
			     gpointer data);
static void unselect_all_messages (GtkCList* clist, gpointer user_data);

static void resize_column_event_cb(GtkCList * clist, gint column,
				   gint width, gpointer data);
static void tree_expand_cb(GtkCTree * ctree, GList * node,
                           gpointer user_data);
static void tree_collapse_cb(GtkCTree * ctree, GList * node,
                           gpointer user_data);
static void hide_deleted(BalsaIndex * bindex, gboolean hide);

/* formerly balsa-index-page stuff */
enum {
    TARGET_MESSAGES
};

static GtkTargetEntry index_drag_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

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
bndx_match_struct_destroy(BalsaIndex * index)
{
    if (index->match_struct) {
	if (index->match_struct->conditions)
	    libbalsa_conditions_free(index->match_struct->conditions);
	if (index->match_struct->matching_messages)
	    g_list_free(index->match_struct->matching_messages);
	g_free(index->match_struct);
	index->match_struct = NULL;
    }
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
    gtk_clist_set_column_width(clist, 6, balsa_app.index_size_width);
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

    /* catch thread expand/collapse events, to set the head node style
     * for unread messages */
    gtk_signal_connect_after(GTK_OBJECT(bindex->ctree),
                             "tree-expand",
                             (GtkSignalFunc) tree_expand_cb,
                             (gpointer) bindex);
    gtk_signal_connect_after(GTK_OBJECT(bindex->ctree),
                             "tree-collapse",
                             (GtkSignalFunc) tree_collapse_cb,
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

    /* Initializing the match structure */
    bindex->match_struct = NULL;

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
    GtkCListRow *row1 = (GtkCListRow *) ptr1;
    GtkCListRow *row2 = (GtkCListRow *) ptr2;

    m1 = row1->data;
    m2 = row2->data;
    g_return_val_if_fail(m1 && m2, 0);

    return m2->date - m1->date;
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

    g_return_val_if_fail(m1 && m2, 0);

    t1 = LIBBALSA_MESSAGE_GET_NO(m1);
    t2 = LIBBALSA_MESSAGE_GET_NO(m2);

    return t2-t1;
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

/* balsa_index_most_recent_message:
   helper function, finds the message most recently selected and
   fails with -1, if the selection is empty.
*/
static gint
balsa_index_most_recent_message(GtkCList * clist)
{
    if (clist->selection == NULL)
        return -1;
    else {
        GtkCTreeNode *node = g_list_last(clist->selection)->data;
        gpointer data =
            gtk_ctree_node_get_row_data(GTK_CTREE(clist), node);
        return gtk_clist_find_row_from_data(clist, data);
    }
}


static void
clist_click_column(GtkCList * clist, gint column, gpointer data)
{
    GtkSortType sort_type = clist->sort_type;

    if (column == clist->sort_column)
	sort_type = (sort_type == GTK_SORT_ASCENDING) ?
	    GTK_SORT_DESCENDING : GTK_SORT_ASCENDING;
	
    balsa_index_set_sort_order(BALSA_INDEX(data), column, sort_type);
    gtk_clist_sort(clist);
    DO_CLIST_WORKAROUND(clist);

    balsa_index_check_visibility(clist, NULL, 0.5);
}


/* 
 * Search the index, locate the first unread message, and update the
 * index's first_new_message field to point at that message.  If no
 * unread messages are found, first_new_message is set to NULL
 */
static void
balsa_index_set_first_new_message(BalsaIndex * bindex)
{
    GtkCTreeNode *node =
        balsa_index_find_node(bindex, FALSE, LIBBALSA_MESSAGE_FLAG_NEW,
			      FILTER_NOOP, NULL);

    if (node)
        bindex->first_new_message =
            LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data
                             (bindex->ctree, node));
}

struct BalsaIndexScanInfo {
    BalsaIndex * index;                 /* Index */
    gpointer node;                      /* current ctree node */
    GtkCTreeNode * previous, * next;
    GtkCTreeNode *first, * last;
    GList *selection;                   /* copy of clist->selection */
    LibBalsaMessageFlag flag;           /* look only for matching nodes */
    gint op;
    GSList * conditions;                /* Conditions for the match */
};

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
    GtkCTreeNode *node;
    
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
        
    node = gtk_ctree_find_by_row_data(bindex->ctree, NULL, row_data);
    while (node && !gtk_ctree_is_viewable(bindex->ctree, node)) {
        node = GTK_CTREE_ROW(node)->parent;
        if (balsa_app.view_message_on_open)
            gtk_ctree_expand(bindex->ctree, node);
    }
    if (!balsa_app.view_message_on_open)
        row_data = gtk_ctree_node_get_row_data(bindex->ctree, node);

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
   functional (and that's MOST important; think long before modifying it)
   but perhaps we could write it nicer?
*/

gboolean
balsa_index_load_mailbox_node (BalsaIndex * bindex, BalsaMailboxNode* mbnode)
{
    LibBalsaMailbox* mailbox;
    gchar *msg;
    gboolean successp;

    g_return_val_if_fail (bindex != NULL, TRUE);
    g_return_val_if_fail (mbnode != NULL, TRUE);
    g_return_val_if_fail (mbnode->mailbox != NULL, TRUE);

    mailbox = mbnode->mailbox;
    msg = g_strdup_printf(_("Opening mailbox %s. Please wait..."),
			  mbnode->mailbox->name);
    gnome_appbar_push(balsa_app.appbar, msg);
    g_free(msg);
    successp = libbalsa_mailbox_open(mailbox);
    gnome_appbar_pop(balsa_app.appbar);

    if (!successp)
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

    gtk_signal_connect(GTK_OBJECT(mailbox), "messages-status-changed",
		       GTK_SIGNAL_FUNC(mailbox_messages_changed_status_cb),
		       (gpointer) bindex);
    gtk_signal_connect_object(GTK_OBJECT(mailbox), "messages-added",
		       GTK_SIGNAL_FUNC(mailbox_messages_added_cb),
		       (gpointer) bindex);
    gtk_signal_connect_object(GTK_OBJECT(mailbox), "messages-removed",
		       GTK_SIGNAL_FUNC(mailbox_messages_removed_cb),
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


static void
balsa_index_add(BalsaIndex * bindex, LibBalsaMessage * message)
{
    gchar buff1[32];
    gchar *text[7];
    gchar *name_str=NULL;
    GtkCTreeNode *node, *sibling;
    GList *list;
    LibBalsaAddress *addy = NULL;
    LibBalsaMailbox* mailbox;
    gboolean append_dots;
    

    g_return_if_fail(bindex != NULL);
    g_return_if_fail(message != NULL);

    if (balsa_app.hide_deleted 
        && message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
        return;

    mailbox = bindex->mailbox_node->mailbox;
    
    if (mailbox == NULL)
	return;

    sprintf(buff1, "%ld", LIBBALSA_MESSAGE_GET_NO(message)+1);
    text[0] = buff1;		/* set message number */
    text[1] = NULL;		/* flags */
    text[2] = NULL;		/* attachments */


    append_dots = FALSE;
    if (mailbox == balsa_app.sentbox ||
	mailbox == balsa_app.draftbox ||
	mailbox == balsa_app.outbox) {
	if (message->to_list) {
	    list = g_list_first(message->to_list);
	    addy = list->data;
	    append_dots = list->next != NULL;
	}
    } else {
	if (message->from)
	    addy = message->from;
    }

    if (addy)
	name_str=(gchar *)libbalsa_address_get_name(addy);
    
    if(!name_str)		/* !addy, or addy contained no name/address */
	name_str = "";

    text[3] = append_dots ? g_strconcat(name_str, ",...", NULL)
	: name_str; 

    text[4] = (gchar*)LIBBALSA_MESSAGE_GET_SUBJECT(message);
    text[5] =
	libbalsa_message_date_to_gchar(message, balsa_app.date_string);
    text[6] =
	libbalsa_message_size_to_gchar(message, balsa_app.line_length);

    sibling = GTK_CTREE_NODE(GTK_CLIST(bindex->ctree)->row_list);
    node = gtk_ctree_insert_node(GTK_CTREE(bindex->ctree), NULL, sibling, 
                                 text, 2, NULL, NULL, NULL, NULL, 
                                 FALSE, TRUE);
    if(append_dots) g_free(text[3]);
    g_free(text[5]);
    g_free(text[6]);

    gtk_ctree_node_set_row_data (GTK_CTREE (bindex->ctree), node, 
                                 (gpointer) message);

    balsa_index_set_col_images(bindex, node, message);
    balsa_index_set_parent_style(bindex, node);
}

/*
  See: http://mail.gnome.org/archives/balsa-list/2000-November/msg00212.html
  for discussion of the GtkCtree bug. The bug is triggered when one
  attempts to delete a collapsed message thread.
*/
static void
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


    /* if last message is removed, clear the preview */
    if (GTK_CLIST (bindex->ctree)->rows <= 0) {
        balsa_index_idle_add(bindex, NULL);
    }
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

    if (gtk_clist_row_is_visible (clist, row) != GTK_VISIBILITY_FULL) 
	gtk_clist_moveto(clist, row, -1, 0.5, 0.0);
}

static void
balsa_index_check_visibility(GtkCList *clist, GtkCTreeNode * node,
                             gfloat row_align)
{
    gint row;
    if (node) {
        gpointer tmp = gtk_ctree_node_get_row_data(GTK_CTREE(clist), node);
        row = gtk_clist_find_row_from_data(clist, tmp);
    } else
        row = balsa_index_most_recent_message(clist);
    if (gtk_clist_row_is_visible(clist, row) != GTK_VISIBILITY_FULL) 
	gtk_clist_moveto(clist, row, -1, row_align, 0.0);
}

/*
 * select message interfaces
 *
 * - balsa_index_select_next:
 *   - if any selected,
 *       selects first viewable message after first selected message
 *       no-op if there isn't one
 *   - if no selection,
 *       selects first message
 *   callback for `next message' menu item and `open next' toolbar
 *   button
 *
 * - balsa_index_select_previous:
 *   - if any selected,
 *       selects last viewable message before first selected message
 *       no-op if there isn't one
 *   - if no selection,
 *       selects first message
 *   callback for `previous message' menu item and `open previous'
 *   toolbar button
 *
 * - balsa_index_select_next_threaded: 
 *   - selects first unselected message after first selected message,
 *     expanding thread if necessary
 *   - if none, falls back to last viewable message before first
 *     selected message
 *   called after moving (or deleting) messages, so there's always a
 *   selection
 *
 * - balsa_index_select_next_unread:
 *   - selects first unread unselected message after first selected
 *     message, expanding thread if necessary
 *   - if none, wraps around to the first unread message anywhere 
 *   - no-op if there are no unread messages
 *   callback for `next unread message' menu item and `open next unread
 *   message' toolbar button
 *
 * - balsa_index_select_next_flagged:
 *   like balsa_index_select_next_unread
 */

/* This function retrieve the list of all messages matching the conditions
   stored in the match struct. There are put in the matching_messages list.
   It does the search only for the following and preceding NB_MESSAGES_FORWARD
*/

#define NB_MESSAGES_FORWARD 50

static gboolean
bndx_in_messages_list(BalsaIndex * index, gint msgno)
{
    GList * lst;
    BalsaIndexMatch * match = index->match_struct;
    gint new_begin = -1, new_end = -1;
    gchar * buffer = NULL;

    /* If the msgno is not in the current interval, we extend our
       search domain so that the new interval contains it */
    if (msgno+1 < match->msgno_beg || match->msgno_beg<0)
	new_begin = msgno-NB_MESSAGES_FORWARD <= 0 ? 1 : msgno-NB_MESSAGES_FORWARD;
    /* FIXME : do we have to limit the upper bound (i.e. is this a problem if
       it becomes greater that the number of messages in the mailbox? */
    if (msgno+1> match->msgno_end)
	new_end = msgno+NB_MESSAGES_FORWARD;

    g_print("msgno=%d [%d,%d],[%d,%d]\n",msgno,match->msgno_beg,match->msgno_end,new_begin,new_end);
    /* Extend the interval if needed */
    if (new_begin!=-1 || new_end!=-1) {
	gchar * imap_query =
	    libbalsa_filter_build_imap_query(match->op,
					     match->conditions);
	GList * new = NULL;

	/* Construct the complete query : we add the message sequence number
	   interval that interests us (taking care of not search for messages
	   we have already looked for) */
	buffer = g_strdup_printf("%d:%d %s",
				 new_begin!=-1 ? new_begin : match->msgno_end+1,
				 new_end!=-1 ? new_end : match->msgno_beg-1,
				 imap_query);
	g_free(imap_query);
	libbalsa_mailbox_imap_search(LIBBALSA_MAILBOX_IMAP(index->mailbox_node->mailbox),
				     buffer, &new);
	/* Now new is the list of new messages matching the request, we must
	   concatenate it with the previous matches (keeping the order, this is
	   done by appending it or prepending, depending on which sense we are
	   extending the search interval). Be careful here : all is in REVERSE
	   ORDER!
	*/
	if (new_begin!=-1) {
	    match->matching_messages =
		g_list_concat(match->matching_messages, new);
	    match->msgno_beg = new_begin;
	}
	if (new_end!=-1) {
	    /* This test is there to avoid reconcatenate the new and old lists
	       of messages to make a ring (this happens only on the first load)
	       I'm not sure this remark is understandable ;-)*/
	    if (new_begin==-1)
		match->matching_messages =
		    g_list_concat(new, match->matching_messages);
	    match->msgno_end = new_end;	
	}
    }

    /* The list is in reverse order : seq numbers of matching messages are
       ordered in decreasing order, so we can do the lookup quicker (we stop
       whenever the searched number is smaller than the current item in the
       list)
    */
    for (lst = match->matching_messages;lst && (msgno+1)<GPOINTER_TO_INT(lst->data);
	 lst = g_list_next(lst));

    return lst!=NULL && (msgno+1)==GPOINTER_TO_INT(lst->data);
}

static gboolean
bndx_match_message(BalsaIndex * index, LibBalsaMessage * message, gint op,
		   GSList * conditions)
{
    g_assert(op!=FILTER_NOOP && conditions && message && index);

    /* For local mailboxes, just test the match */
    if (LIBBALSA_IS_MAILBOX_LOCAL(index->mailbox_node->mailbox))
	return match_conditions(op, conditions, message, FALSE);

    else if (LIBBALSA_IS_MAILBOX_IMAP(index->mailbox_node->mailbox)) {
	if (!index->match_struct) {
	    index->match_struct = g_new(BalsaIndexMatch, 1);
	    index->match_struct->op = FILTER_NOOP;
	    index->match_struct->conditions = NULL;
	    index->match_struct->matching_messages = NULL;
	}
	/* See if the conditions have changed since the last search
	   if yes just forget what we have done before, else keep it */
	if ((op != index->match_struct->op) ||
	    !libbalsa_conditions_compare(index->match_struct->conditions,conditions)) {
	    GSList * cnd;
	    /* We make a copy of the conditions list, and first drop the
	       preceding ones */
	    libbalsa_conditions_free(index->match_struct->conditions);
	    index->match_struct->conditions = NULL;
	    for (cnd = conditions;cnd;cnd = g_slist_next(cnd))
		index->match_struct->conditions =
		    g_slist_prepend(index->match_struct->conditions,
				    libbalsa_condition_clone(cnd->data));
	    index->match_struct->conditions =
		g_slist_reverse(index->match_struct->conditions);
	    index->match_struct->op = op;
	    if (index->match_struct->matching_messages)
		g_list_free(index->match_struct->matching_messages);
	    index->match_struct->matching_messages = NULL;
	    index->match_struct->msgno_beg = -1;
	    index->match_struct->msgno_end = -1;
	}
	return bndx_in_messages_list(index, LIBBALSA_MESSAGE_GET_NO(message));
    }
    /* We should not get there (POP3 mailboxes are not associated to indexes) */
    g_assert_not_reached();
    return FALSE;
}

/* balsa_index_scan_node:
 * callback for pre-recursive search for previous and next
 * after search:
 * - b->previous is last viewable message before current, NULL if none,
 *     NULL if no current message
 * - b->next is first viewable message after current, NULL if none,
 *     first message if no current message
 */
static gboolean
balsa_index_scan_node(GtkCTree * ctree, GtkCTreeNode * node,
                      struct BalsaIndexScanInfo *b)
{
    LibBalsaMessage *message =
	LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data(ctree, node));
    /* if we're not looking for flagged messages or we are called
     * by a search function, we want only viewable messages if we
     * are looking for flagged messages, we want those that match,
     * viewable or not */
    gboolean found;

    /* First we're looking for a flag */
    if (b->flag)
	found = (b->flag & message->flags);
    /* We're looking for a filter match */
    else if (b->op != FILTER_NOOP)
	found = bndx_match_message(b->index, message,
				   b->op,
				   b->conditions);
    /* Not looking for flag, neither for filter match so just check
       visibility (This is used by Next or Previous message) */
    else found = gtk_ctree_is_viewable(ctree, node);
    return found;
}

/* balsa_index_select_node:
 * make sure it's viewable, then pass it to balsa_index_select_row
 * no-op if it's NULL
 */
static void
balsa_index_select_node(BalsaIndex * bindex, GtkCTreeNode * node)
{
    if (node) {
        gpointer tmp = gtk_ctree_node_get_row_data(bindex->ctree, node);
        GtkCList *clist = GTK_CLIST(bindex->ctree);
        gint row;

        while (node && !gtk_ctree_is_viewable(bindex->ctree, node)) {
            node = GTK_CTREE_ROW(node)->parent;
            gtk_ctree_expand(bindex->ctree, node);
            balsa_index_set_style(bindex, node);
        }
        /* now that the row we want is viewable, we can look up its
         * number */
        row = gtk_clist_find_row_from_data(clist, tmp);
        balsa_index_select_row(bindex, row);
    }
}

static GtkCTreeNode *
bndx_prev(GtkCTree * ctree, GtkCTreeNode * node, gboolean only_viewable)
{
    GtkCTreeNode * res = GTK_CTREE_NODE_PREV(node);
    GtkCTreeRow * r;

    if (only_viewable || !res)
	return res;
    /* We want to scan all nodes, so we have to enter into the collapse
       subtrees : if the previous node is the root of a different subtree
       scan into it (going to the last of its children) */
    r = GTK_CTREE_ROW(res);
    /* To be the root of a different subtree, res must not be an ancestor
       of node, and have children */
    if (!gtk_ctree_is_ancestor(ctree, res, node) && r->children) {
	/* Go to the last in the subtree, this can mean to go deep */
	do {
	    res = r->children;
	    r = GTK_CTREE_ROW(res);
	    while (r->sibling) {
		res = r->sibling;
		r = GTK_CTREE_ROW(res);
	    }
	} while (r->children);
    }
    return res;
}

static GtkCTreeNode *
bndx_next(GtkCTree * ctree, GtkCTreeNode * node, gboolean only_viewable)
{
    GtkCTreeRow * r;
    GtkCTreeNode * res;

    /* We want to scan all nodes, so we have to enter into the collapse
       subtrees : if the current node is the root of subtree we must scan 
       into it (we have to check if the next we have is indeed its) */
    r = GTK_CTREE_ROW(node);
    /* We want to scan all nodes, so we have to check if the current node
       is the root of a subtree (checking if it has children)
       and we enter in the subtree only if we have to (only_viewable is FALSE)
       else we just use the NEXT macro
     */
    if (only_viewable)
	return GTK_CTREE_NODE_NEXT(node);
    if (r->children)
	return r->children;

    if (r->sibling)
	return r->sibling;
    /* We arrive here when we're at the end of a subtree, so we have to go
       to the parents to find the next node
     */
    res = node;
    do {
	res = r->parent;
	if (!res) return NULL;
	r = GTK_CTREE_ROW(res);
    } while (!r->sibling);
    return r->sibling;
}

/* balsa_index_find_node:
 * common search code--look for next or previous, with or without flag
 */
static GtkCTreeNode *
balsa_index_find_node(BalsaIndex * bindex, gboolean search_backward,
                      LibBalsaMessageFlag flag, gint op, GSList* conditions)
{
    GtkCTreeNode *node;
    struct BalsaIndexScanInfo *bi;
    gboolean one_turn_done = FALSE;
    
    g_return_val_if_fail(bindex != NULL, NULL);
    if(GTK_CLIST(bindex->ctree)->row_list == NULL)
	return NULL;

    bi = g_new0(struct BalsaIndexScanInfo, 1);
    bi->index = bindex;

    /* find the node the search will start from */
    if(search_backward) {
	node =
	    GTK_CLIST(bindex->ctree)->selection
	    ? g_list_first(GTK_CLIST(bindex->ctree)->selection)->data 
	    : gtk_ctree_node_nth(bindex->ctree, 
				 GTK_CLIST(bindex->ctree)->rows-1);
    } else {
	node =
	    GTK_CLIST(bindex->ctree)->selection
	    ? g_list_last(GTK_CLIST(bindex->ctree)->selection)->data 
	    : gtk_ctree_node_nth(bindex->ctree, 0);
    }

    g_return_val_if_fail(node, node);
    bi->flag = flag;
    bi->op = op;
    bi->conditions = conditions;
    do
	if (search_backward)
	    node = bndx_prev(bindex->ctree, node, flag==0);
	else {
	    node = bndx_next(bindex->ctree, node, flag==0);
	    /* When we search using flag, we wrap the search. Do that
	       once, no more (else -> infinite loop)
	    */
	    if (!node && flag!=0 && !one_turn_done) {
		node = gtk_ctree_node_nth(bindex->ctree, 0);
		one_turn_done = TRUE;
	    }
	}
    while (node && !balsa_index_scan_node(bindex->ctree, node, bi));
    g_free(bi);

    /* Display a message if search was unfruitful */
    if (!node && op!=FILTER_NOOP)
	balsa_information(LIBBALSA_INFORMATION_WARNING, NULL,
			  _("No message found\n"));
    return node;
}

void
balsa_index_select_next(BalsaIndex * bindex)
{
    balsa_index_select_node(bindex, 
			    balsa_index_find_node(bindex, FALSE, 0, 
                                                  FILTER_NOOP, NULL));
}

void
balsa_index_select_previous(BalsaIndex * bindex)
{
    balsa_index_select_node(bindex,
			    balsa_index_find_node(bindex, TRUE, 0,
						  FILTER_NOOP, NULL));
}

void
balsa_index_select_next_unread(BalsaIndex * bindex)
{
    balsa_index_select_node(bindex,
			    balsa_index_find_node(bindex, FALSE, 
                                                  LIBBALSA_MESSAGE_FLAG_NEW,
						  FILTER_NOOP, NULL));
}

void
balsa_index_select_next_flagged(BalsaIndex * bindex)
{
    balsa_index_select_node(bindex,
			    balsa_index_find_node(bindex, FALSE, 
                                                  LIBBALSA_MESSAGE_FLAG_FLAGGED,
						  FILTER_NOOP, NULL));
}

void
balsa_index_find(BalsaIndex * bindex,gint op,GSList* conditions,
                 gboolean previous)
{
    balsa_index_select_node(bindex,
                            balsa_index_find_node(bindex, previous, 0, op,
                                                  conditions));
}

/* balsa_index_scan_selection:
 * callback for pre-recursive search for next message after moving one
 * or more message out of the mailbox
 * after search:
 * - b->next is next unselected message after first selected message,
 *     which may be currently non-viewable
 * - b->first is first unselected viewable message
 * - b->last is last unselected viewable message
 * any or all may be NULL--we'll use them in the order next, first, last
 */
static void
balsa_index_scan_selection(GtkCTree * ctree, GtkCTreeNode * node,
                           struct BalsaIndexScanInfo *b)
{
    GList *list;
    LibBalsaMessage *message;

    for (list = b->selection; list; list = g_list_next(list)) {
        if (list->data == node) {
            if (b->previous == NULL) {
                /* this is the first time we found a selected node */
                b->previous = b->last;
                /* clear any `next' node we found before */
                b->next = NULL;
            }
            b->selection = g_list_remove_link(b->selection, list);
            g_list_free_1(list);
            return;
        }
    }

    /* this node isn't selected */
    message = LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data(ctree, node));
    /* skip any DELETED message, as it may really be deleted before we
     * get a chance to show it */
    if (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
        return;

    if (b->next == NULL)
        /* save it whether or not it's viewable */
        b->next = node;

    if (gtk_ctree_is_viewable(ctree, node))
        b->last = node;
}

/* balsa_index_select_next_threaded:
 * finds and selects a message after moving one or more message out of
 * the index
 */
static void
balsa_index_select_next_threaded(BalsaIndex * bindex)
{
    GtkCTreeNode *node;
    GtkCList *clist;
    struct BalsaIndexScanInfo *bi;
    
    g_return_if_fail(bindex != NULL);
    
    clist = GTK_CLIST(bindex->ctree);
    bi = g_new0(struct BalsaIndexScanInfo, 1);

    bi->selection = g_list_copy(clist->selection);
    gtk_ctree_pre_recursive(bindex->ctree, NULL, (GtkCTreeFunc)
                            balsa_index_scan_selection, bi);

    node = bi->next;
    if (node == NULL)
        node = bi->first;
    if (node == NULL)
        node = bi->last;
    g_free(bi);
    if (node)
        balsa_index_select_node(bindex, node);
    else
        /* we're emptying the index, so we must set up the idle handler
         * appropriately */
        balsa_index_idle_add(bindex, NULL);
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
}

static void
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
    /* only ony status icon is shown; they are ordered from most important. */
    const static struct {
        int mask;
        BalsaIconName icon_name;
    } flags[] = {
        { LIBBALSA_MESSAGE_FLAG_DELETED, BALSA_ICON_MBOX_TRASH   },
        { LIBBALSA_MESSAGE_FLAG_FLAGGED, BALSA_ICON_INFO_FLAGGED },
        { LIBBALSA_MESSAGE_FLAG_REPLIED, BALSA_ICON_INFO_REPLIED },
        { LIBBALSA_MESSAGE_FLAG_NEW,     BALSA_ICON_INFO_NEW } };

    ctree = bindex->ctree;

    for(tmp=0; tmp<ELEMENTS(flags) && !(message->flags & flags[tmp].mask);
        tmp++);
    if(tmp<ELEMENTS(flags))
	gtk_ctree_node_set_pixmap(ctree, node, 1,
				  balsa_icon_get_pixmap(flags[tmp].icon_name),
				  balsa_icon_get_bitmap(flags[tmp].icon_name));
    else
        gtk_ctree_node_set_text(ctree, node, 1, NULL);
    /* Alternatively, we could show an READ icon:
     * gtk_ctree_node_set_pixmap(ctree, node, 1,
     * balsa_icon_get_pixmap(BALSA_ICON_INFO_READ),
     * balsa_icon_get_bitmap(BALSA_ICON_INFO_READ)); 
     */

    if (libbalsa_message_has_attachment(message)) {
	gtk_ctree_node_set_pixmap(ctree, node, 2,
				  balsa_icon_get_pixmap(BALSA_ICON_INFO_ATTACHMENT),
				  balsa_icon_get_bitmap(BALSA_ICON_INFO_ATTACHMENT));
    }
}

static gboolean
thread_has_unread(GtkCTree *ctree, GtkCTreeNode *node)
{
    GtkCTreeNode *child;

    for(child=GTK_CTREE_ROW(node)->children; child; 
	child=GTK_CTREE_ROW(child)->sibling) {
	LibBalsaMessage *message=
	    LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data(ctree, child));

	if(message && message->flags & LIBBALSA_MESSAGE_FLAG_NEW ||
	   thread_has_unread(ctree, child)) 
	    return TRUE;
    }
    return FALSE;
} 


static void
balsa_index_set_style(BalsaIndex * bindex, GtkCTreeNode *node)
{
    GtkCTree *ctree;
    GtkStyle *style;
    
    ctree = bindex->ctree;

    /* FIXME: Improve style handling;
              - Consider storing styles locally, with or config setting
	        separate from the "mailbox" one.
	      - Find better way of obtaining default style. Note that ctree
	        style can't be use as it isn't initialised yet the first time
		we call this. */
    
    if(!GTK_CTREE_ROW(node)->expanded && thread_has_unread(ctree, node)) {
	style = balsa_app.mblist->unread_mailbox_style;
    } else
	style = gtk_widget_get_style(GTK_WIDGET(balsa_app.mblist));

    gtk_ctree_node_set_row_style(ctree, node, style);
}

static void
balsa_index_set_style_recursive(BalsaIndex * bindex, GtkCTreeNode *node)
{
    GtkCTreeNode *child;

    balsa_index_set_style(bindex, node);

    for(child=GTK_CTREE_ROW(node)->children; child; 
	child=GTK_CTREE_ROW(child)->sibling) {
	balsa_index_set_style_recursive(bindex, child);
    }
}


static void
balsa_index_set_parent_style(BalsaIndex * bindex, GtkCTreeNode *node)
{
    GtkCTreeNode *parent;

    for(parent=GTK_CTREE_ROW(node)->parent; parent; 
	parent=GTK_CTREE_ROW(parent)->parent) {
	balsa_index_set_style(bindex, parent);
    }
}

/* CLIST callbacks */

static void
button_event_press_cb(GtkWidget * ctree, GdkEventButton * event,
                      gpointer data)
{
    gint row, column;
    LibBalsaMessage *message = NULL;
    BalsaIndex *bindex;
    GtkCList *clist = GTK_CLIST(ctree);

    g_return_if_fail(event);
    if (clist->rows <= 0)
        return;

    bindex = BALSA_INDEX(data);
    if (gtk_clist_get_selection_info(clist, event->x, event->y,
                                     &row, &column))
        message = gtk_clist_get_row_data(clist, row);

    if (event->button == 1 && event->type == GDK_2BUTTON_PRESS && message) {
        /* double click on a message means open a message window,
         * unless we're in the draftbox, in which case it means open
         * a sendmsg window */
        if (bindex->mailbox_node->mailbox == balsa_app.draftbox) {
            /* the simplest way to get a sendmsg window would be:
             * balsa_message_continue(widget, (gpointer) bindex);
             * but it doesn't work--the selection info seems to
             * become invisible
             *
             * instead we'll just use the guts of
             * balsa_message_continue: */
            BalsaSendmsg *sm =
                sendmsg_window_new(GTK_WIDGET(balsa_app.main_window),
                                   message, SEND_CONTINUE);
            gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
                               GTK_SIGNAL_FUNC
                               (sendmsg_window_destroy_cb), NULL);
        } else
            message_window_new(message);
    } else if (event->button == 3) {
        /* pop up the context menu:
         * - if the clicked-on message is already selected, don't change
         *   the selection;
         * - if it isn't, select it (cancelling any previous selection)
         * - create and show the menu only if some message is selected,
         *   at the end of all this */
        if (message) {
            /* select this message, if it's not already selected */
            GtkCTreeNode *node =
                gtk_ctree_find_by_row_data(GTK_CTREE(ctree), NULL,
                                           message);
            if (!g_list_find(clist->selection, node))
                balsa_index_select_row(bindex, row);
        }

        if (clist->selection) {
            balsa_index_idle_remove(bindex);
            gtk_menu_popup(GTK_MENU(create_menu(bindex)),
                           NULL, NULL, NULL, NULL,
                           event->button, event->time);
        }
    }
}

/* tree_expand_cb:
 * callback on expand events
 * set/reset unread style, as appropriate
 * if current message has become viewable, check its visibility,
 * scrolling only until it is at foot of window;
 * otherwise find last viewable message in thread and check its
 * visibility, scrolling only until node is at foot of window, but not
 * so far that the clicked-on message scrolls off the top */
static void
tree_expand_cb(GtkCTree * ctree, GList * node, gpointer user_data)
{
    GtkCTreeNode *child = NULL;
    GtkCTreeNode *parent = NULL;
    if (GTK_CLIST(ctree)->selection) {
        /* current message... */
        GtkCTreeNode *current =
            g_list_last(GTK_CLIST(ctree)->selection)->data;
        if (gtk_ctree_is_ancestor(ctree, GTK_CTREE_NODE(node), current)
            /* ...is in thread... */
            && gtk_ctree_is_viewable(ctree, current))
            /* ...and viewable:
             * check visibility of current only */
            child = current;
    }
    if (child == NULL) {
        /* get last child... */
        child = gtk_ctree_last(ctree, GTK_CTREE_ROW(node)->children);
        while (child && !gtk_ctree_is_viewable(ctree, child))
            /* ...that is viewable */
            child = GTK_CTREE_ROW(child)->parent;
        /* check visibility of parent, too */
        parent = GTK_CTREE_NODE(node);
    }
    if (child)
        balsa_index_check_visibility(GTK_CLIST(ctree), child, 1.0);
    if (parent)
        balsa_index_check_visibility(GTK_CLIST(ctree), parent, 0.0);
    balsa_index_set_style(BALSA_INDEX(user_data), GTK_CTREE_NODE(node));
}

/* tree_collapse_cb:
 * callback on collapse events
 * set/reset unread style, as appropriate */
static void
tree_collapse_cb(GtkCTree * ctree, GList * node, gpointer user_data)
{
    balsa_index_set_style(BALSA_INDEX(user_data), GTK_CTREE_NODE(node));
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
    if (message) {
	gtk_signal_emit(GTK_OBJECT(bindex),
			balsa_index_signals[SELECT_MESSAGE],
			message, NULL);
    }

    balsa_index_idle_add(bindex, message);
}


static void
unselect_message(GtkWidget * widget, GtkCTreeNode *row, gint column,
		 gpointer data)
{
    BalsaIndex *bindex;
    LibBalsaMessage *message;
    GList *sel;

    bindex = BALSA_INDEX (data);
    message =
	LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data (GTK_CTREE(widget), row));

    if (message)
	gtk_signal_emit(GTK_OBJECT(bindex),
			balsa_index_signals[UNSELECT_MESSAGE],
			message, NULL);

    if ((sel = GTK_CLIST(widget)->selection) && !g_list_next(sel)) {
        message =
            LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data
                             (GTK_CTREE(widget),
                              GTK_CTREE_NODE(sel->data)));
        balsa_index_idle_add(bindex, message);
    }
}


static void
unselect_all_messages(GtkCList* clist, gpointer user_data)
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

    case 6:
	balsa_app.index_size_width = width;
	break;

    default:
	if (balsa_app.debug)
	    fprintf(stderr, "** Error: Unknown column resize\n");
    }
}

/* Mailbox Callbacks... */
/* mailbox_message_changed_status_cb:
   We must be *extremely* careful here - message might have changed 
   its status because the mailbox was forcibly closed and message
   became invalid. See for example #70807.

*/

static void
mailbox_messages_changed_status_cb(LibBalsaMailbox * mb,
				   GList * messages,
				   gint flag,
				   BalsaIndex * bindex)
{
    if (!libbalsa_mailbox_is_valid(mb)) return;

    if (flag == LIBBALSA_MESSAGE_FLAG_DELETED &&
	(balsa_app.hide_deleted || balsa_app.delete_immediately)) {
	/* These messages are flagged as deleted, but we must remove them from
	   the index because of the prefs
	 */
	gtk_clist_freeze(GTK_CLIST(bindex->ctree));
	for(;messages;messages = g_list_next(messages))
	    balsa_index_del(bindex, LIBBALSA_MESSAGE(messages->data));
	balsa_index_check_visibility(GTK_CLIST(bindex->ctree), NULL, 0.5);
	gtk_clist_thaw(GTK_CLIST(bindex->ctree));
    }
    else 
	for(;messages;messages = g_list_next(messages))
	    balsa_index_update_flag(bindex, LIBBALSA_MESSAGE(messages->data));
}

/* mailbox_messages_added_cb : callback for sync with backend; the signal
   is emitted by the mailbox when new messages has been retrieved (either
   after opening the mailbox, or after "check new messages").
*/
/* Helper of the callback (also used directly) */
static void
mailbox_messages_added(BalsaIndex * bindex, GList *messages)
{
    LibBalsaMessage * message;

    gtk_clist_freeze(GTK_CLIST (bindex->ctree));
    while (messages) {
	message = LIBBALSA_MESSAGE(messages->data);
	balsa_index_add(bindex, message);
	messages = g_list_next(messages);
    }
    balsa_index_threading(bindex);
    gtk_clist_sort(GTK_CLIST (bindex->ctree));
    DO_CLIST_WORKAROUND(GTK_CLIST (bindex->ctree));
    gtk_clist_thaw(GTK_CLIST (bindex->ctree));

    balsa_mblist_update_mailbox(balsa_app.mblist, 
				bindex->mailbox_node->mailbox);
}

static void
mailbox_messages_added_cb(BalsaIndex * bindex, GList *messages)
{
    mailbox_messages_added(bindex, messages);
}

/* mailbox_messages_remove_cb : callback to sync with backend; the signal is
   emitted by the mailbox to tell the frontend that it has removed mails
   (this is in general when the mailbox is committed because it then removes
   all mails flagged as deleted; the other case is when prefs about how to handle
   deletions are changed : "hide deleted messages/delete immediately")
 */
/* Helper of the callback (also used directly) */
static void
mailbox_messages_removed(BalsaIndex * bindex, GList * messages)
{
    LibBalsaMessage * message;
    gtk_clist_freeze(GTK_CLIST(bindex->ctree));

    while (messages) {
	message = LIBBALSA_MESSAGE(messages->data);
	balsa_index_del(bindex, message);
	messages = g_list_next(messages);
    }

    balsa_index_check_visibility(GTK_CLIST(bindex->ctree), NULL, 0.5);
    gtk_clist_thaw(GTK_CLIST(bindex->ctree));
}
static void
mailbox_messages_removed_cb(BalsaIndex * bindex, GList * messages)
{
    mailbox_messages_removed(bindex, messages);
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
static void
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
    balsa_index_idle_remove(bindex);
    message = gtk_object_get_data(obj, "message");
    if (message != NULL) {
        gtk_object_remove_data (obj, "message");
        gtk_object_unref (message);
    }

    /* Destroy match_struct */
    bndx_match_struct_destroy(bindex);

    /*page->window references our owner */
    if (bindex->mailbox_node && (mailbox = bindex->mailbox_node->mailbox) ) {
        gtk_signal_disconnect_by_data (GTK_OBJECT (mailbox), bindex);
	libbalsa_mailbox_close(mailbox);
	bindex->mailbox_node = NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
        (*GTK_OBJECT_CLASS(parent_class)->destroy) (obj);
}

static void
balsa_message_view_source(GtkWidget * widget, gpointer user_data)
{
    GList *list;
    BalsaIndex* index;
    LibBalsaMessage *message;
    
    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;
    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	list = list->next;
	libbalsa_show_message_source(message);
    }	
}

static void
compose_foreach(GtkWidget* w, BalsaIndex* index, SendType send_type)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;

    for(list = GTK_CLIST(index->ctree)->selection; list; list = list->next) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	sm = sendmsg_window_new(w, message, send_type);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
    }
}

void
balsa_message_reply(GtkWidget * widget, gpointer user_data)
{
    compose_foreach(widget, BALSA_INDEX (user_data), SEND_REPLY);
}

void
balsa_message_replytoall(GtkWidget * widget, gpointer user_data)
{
    compose_foreach(widget, BALSA_INDEX (user_data), SEND_REPLY_ALL);
}

void
balsa_message_replytogroup(GtkWidget * widget, gpointer user_data)
{
    compose_foreach(widget, BALSA_INDEX (user_data), SEND_REPLY_GROUP);
}

static void
compose_from_list(GtkWidget * w, BalsaIndex * index, SendType send_type)
{
    GList *sel = GTK_CLIST(index->ctree)->selection;
    GList *list = NULL;
    BalsaSendmsg *sm;

    while (sel) {
        LibBalsaMessage *message =
            gtk_ctree_node_get_row_data(index->ctree,
                                        GTK_CTREE_NODE(sel->data));
        list = g_list_prepend(list, message);
        sel = g_list_next(sel);
    }
    list = g_list_reverse(list);
    sm = sendmsg_window_new_from_list(w, list, send_type);
    gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
                       GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
                       NULL);

    g_list_free(list);
}

void
balsa_message_forward_attached(GtkWidget * widget, gpointer user_data)
{
    compose_from_list(widget, BALSA_INDEX (user_data), SEND_FORWARD_ATTACH);
}

void
balsa_message_forward_inline(GtkWidget * widget, gpointer user_data)
{
    compose_from_list(widget, BALSA_INDEX (user_data), SEND_FORWARD_INLINE);
}

void
balsa_message_forward_default(GtkWidget * widget, gpointer user_data)
{
    compose_from_list(widget, BALSA_INDEX (user_data), 
                      balsa_app.forward_attached 
                      ? SEND_FORWARD_ATTACH : SEND_FORWARD_INLINE);
}

void
balsa_message_continue(GtkWidget * widget, gpointer user_data)
{
    compose_foreach(widget, BALSA_INDEX (user_data), SEND_CONTINUE);
}


static void
do_delete(BalsaIndex* index, gboolean move_to_trash)
{
    GList *list;
    BalsaIndex *trash = balsa_find_index_by_mailbox(balsa_app.trash);
    LibBalsaMessage *message;
    GList *messages = NULL;

    for(list = GTK_CLIST(index->ctree)->selection; list; list = list->next) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	messages= g_list_prepend(messages, message);
    }
    balsa_index_select_next_threaded(index);
    messages = g_list_reverse(messages);
    if(messages) {
	if (move_to_trash && (index != trash)) {
	    libbalsa_messages_move(messages, balsa_app.trash);
	    enable_empty_trash(TRASH_FULL);
	} else {
	    libbalsa_messages_delete(messages, TRUE);
	    if (index == trash)
		enable_empty_trash(TRASH_CHECK);
	}
	g_list_free(messages);
    }
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
    GList * messages = NULL;
    BalsaIndex* index;


    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);
    list = GTK_CLIST(index->ctree)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	messages = g_list_prepend(messages, message);
	list = g_list_next(list);
    }
    libbalsa_messages_delete(messages, FALSE);
    g_list_free(messages);
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

/* This function toggles the given attribute of a list of messages,
   using given callback.
 */
static void
balsa_message_toggle_flag(BalsaIndex* index, LibBalsaMessageFlag flag,
                          void(*cb)(GList*, gboolean))
{
    GList *list;
    LibBalsaMessage * message;
    GList * messages  = NULL;
    int is_all_flagged = TRUE;
    gboolean new_flag;

    /* First see if we should set given flag or unset */
    for(list=GTK_CLIST(index->ctree)->selection; list; list = g_list_next(list)) {
	message = gtk_ctree_node_get_row_data(index->ctree, list->data);
	if (!(message->flags & flag))
	    is_all_flagged = FALSE;
	messages = g_list_prepend(messages, message);
    }

    /* If they all have the flag set, then unset them. Otherwise, set
     * them all.
     * Note: the callback for `toggle unread' changes the `read' flag,
     * but the callback for `toggle flagged' changes `flagged'
     */

    new_flag =
        (flag ==
         LIBBALSA_MESSAGE_FLAG_NEW ? is_all_flagged : !is_all_flagged);

    gtk_clist_freeze(GTK_CLIST(balsa_app.mblist));
    (*cb) (messages, new_flag);
    gtk_clist_thaw(GTK_CLIST(balsa_app.mblist));
    g_list_free(messages);
}

/* This function toggles the FLAGGED attribute of a list of messages
 */
void
balsa_message_toggle_flagged(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_message_toggle_flag(BALSA_INDEX(user_data), 
                              LIBBALSA_MESSAGE_FLAG_FLAGGED,
                              libbalsa_messages_flag);
}


/* This function toggles the NEW attribute of a list of messages
 */
void
balsa_message_toggle_new(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_message_toggle_flag(BALSA_INDEX(user_data), 
                              LIBBALSA_MESSAGE_FLAG_NEW,
                              libbalsa_messages_read);
}


/* balsa_index_update_message:
   update preview window to currently selected message of index.
*/
void
balsa_index_update_message(BalsaIndex * index)
{
    gint row;
    GtkObject *message;
    GtkCList *list;

    list = GTK_CLIST(index->ctree);
    row = balsa_index_most_recent_message(list);

    message = 
	(row < 0) ? NULL : GTK_OBJECT(gtk_clist_get_row_data(list, row));

    balsa_index_idle_add(index, message);
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

    gdk_threads_enter();
    
    if (!widget || !balsa_index_idle_clear(widget)) {
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
		    (LIBBALSA_INFORMATION_ERROR, NULL,
		     _("Cannot access the message's body\n"));
	}
	else
	    balsa_message_clear(BALSA_MESSAGE (bmsg));
    }

    /* replace_attached_data (GTK_OBJECT (widget), "message", NULL); */
    if (message != NULL) {
        gtk_object_remove_data (GTK_OBJECT (widget), "message");
        gtk_object_unref (GTK_OBJECT (message));
    }
    
    /* Update the style and message counts in the mailbox list */
    /*    if(index->mailbox_node)
	balsa_mblist_update_mailbox(balsa_app.mblist, 
	index->mailbox_node->mailbox);*/

    gdk_threads_leave();
    return FALSE;
}

static void
mru_menu_cb(gchar * url, gpointer data)
{
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);
    
    g_return_if_fail(mailbox != NULL);
    balsa_index_transfer_messages(data, mailbox);
}


static GtkWidget *
create_menu(BalsaIndex * bindex)
{
    const static struct {             /* this is a invariable part of */
        const char* icon, *label;     /* the context message menu.    */
        GtkSignalFunc func;
    } entries[] = {
        { GNOME_STOCK_MENU_BOOK_OPEN,    N_("View Source"), 
          balsa_message_view_source },
        { BALSA_PIXMAP_MENU_REPLY,       N_("Reply..."), 
          balsa_message_reply },
        { BALSA_PIXMAP_MENU_REPLY_ALL,   N_("Reply To All..."), 
          balsa_message_replytoall },
        { BALSA_PIXMAP_MENU_REPLY_GROUP, N_("Reply To Group..."), 
          balsa_message_replytogroup },
        { BALSA_PIXMAP_MENU_FORWARD,     N_("Forward Attached..."), 
          balsa_message_forward_attached },
        { BALSA_PIXMAP_MENU_FORWARD,     N_("Forward Inline..."), 
          balsa_message_forward_inline },
        { GNOME_STOCK_MENU_BOOK_RED,     N_("Store Address..."), 
          balsa_store_address } };
    GtkWidget *menu, *menuitem, *submenu;
    LibBalsaMailbox* mailbox;
    unsigned i;
    GList *list;
    gboolean any_deleted = FALSE;
    gboolean any_not_deleted = FALSE;
 
    BALSA_DEBUG();
    mailbox = bindex->mailbox_node->mailbox;

    menu = gtk_menu_new();
    /* it's a single-use menu, so we must destroy it when we're done */
    gtk_signal_connect(GTK_OBJECT(menu), "selection-done",
                       gtk_object_destroy, NULL);

    for(i=0; i<ELEMENTS(entries); i++)
        create_stock_menu_item(menu, entries[i].icon, _(entries[i].label),
                               entries[i].func, bindex, TRUE);

    for (list = GTK_CLIST(bindex->ctree)->selection; list;
         list = g_list_next(list)) {
        LibBalsaMessage *message =
            LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data
                             (GTK_CTREE(bindex->ctree), list->data));
        if (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
            any_deleted = TRUE;
        else
            any_not_deleted = TRUE;
    }
    if (any_not_deleted) {
        create_stock_menu_item(menu, GNOME_STOCK_MENU_TRASH,
                               _("Delete"), balsa_message_delete, bindex,
                               !mailbox->readonly);
    }
    if (any_deleted) {
	create_stock_menu_item(menu, GNOME_STOCK_MENU_UNDELETE,
			       _("Undelete"), balsa_message_undelete,
			       bindex, !mailbox->readonly);
    }
    if (mailbox != balsa_app.trash) {
	create_stock_menu_item(menu, GNOME_STOCK_MENU_TRASH,
			       _("Move To Trash"), balsa_message_move_to_trash,
			       bindex, !mailbox->readonly);
    }

    menuitem = gtk_menu_item_new_with_label(_("Toggle"));
    submenu = gtk_menu_new();
    create_stock_menu_item( submenu, BALSA_PIXMAP_MENU_FLAGGED, _("Flagged"),
			    balsa_message_toggle_flagged, bindex, TRUE);
    create_stock_menu_item( submenu, BALSA_PIXMAP_MENU_NEW, _("Unread"),
			    balsa_message_toggle_new, bindex, TRUE);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_menu_append(GTK_MENU(menu), menuitem);

    menuitem = gtk_menu_item_new_with_label(_("Move"));
    gtk_widget_set_sensitive(menuitem, !mailbox->readonly);
    gtk_menu_append(GTK_MENU(menu), menuitem);

    submenu = balsa_mblist_mru_menu(GTK_WINDOW(balsa_app.main_window),
                                    &balsa_app.folder_mru,
                                    GTK_SIGNAL_FUNC(mru_menu_cb),
                                    bindex);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

    gtk_widget_show_all(menu);

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
balsa_index_transfer_messages(BalsaIndex * bindex,
                              LibBalsaMailbox * mailbox)
{
    GtkCList* clist;
    GList *list, *messages;
    LibBalsaMessage* message;
    BalsaMailboxNode *mbnode;

    g_return_if_fail(bindex != NULL);
    if (mailbox == NULL)
        return;
    clist = GTK_CLIST(bindex->ctree);

    /*Transferring to same mailbox? */
    if (bindex->mailbox_node->mailbox == mailbox)
	return;

    messages=NULL;
    for (list = clist->selection; list;list = list->next) {
	message = gtk_ctree_node_get_row_data(GTK_CTREE(bindex->ctree), 
					      list->data);
	messages=g_list_prepend(messages, message);
    }
    messages = g_list_reverse(messages);
    balsa_index_transfer(messages, bindex->mailbox_node->mailbox,
                         mailbox, bindex, FALSE);
    g_list_free(messages);
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

void
balsa_index_update_tree(BalsaIndex *bindex, gboolean expand)
/* Remarks: In the "collapse" case, we still expand current thread to the
	    extent where viewed message is visible. An alternative
	    approach would be to change preview, e.g. to top of thread. */
{
    GtkCTree *tree=GTK_CTREE(bindex->ctree);
    GtkCList *clist=GTK_CLIST(bindex->ctree);
    BalsaMessage *msg=BALSA_MESSAGE(balsa_app.main_window->preview);
    GtkCTreeNode *node, *msg_node=NULL;

    gtk_clist_freeze(clist);
    gtk_signal_handler_block_by_func(GTK_OBJECT(bindex->ctree),
                                     expand ? tree_expand_cb :
                                     tree_collapse_cb, bindex);
    for(node=gtk_ctree_node_nth(tree, 0); node; 
	node=GTK_CTREE_NODE_NEXT(node)) {
	if(expand)
	    gtk_ctree_expand_recursive(tree, node);
	else
	    gtk_ctree_collapse_recursive(tree, node);
	if(!msg_node && msg && msg->message)
	    msg_node=gtk_ctree_find_by_row_data(tree, node, msg->message);

	balsa_index_set_style_recursive( bindex, node); /* chbm */
    }
    
    if (msg_node) {
	if(!expand) {		/* Re-expand msg_node's thread; cf. Remarks */
	    for(node=GTK_CTREE_ROW(msg_node)->parent; node;
		node=GTK_CTREE_ROW(node)->parent) {
		gtk_ctree_expand(tree, node);
	    }
	}
        gtk_clist_thaw(clist);
        balsa_index_check_visibility(clist, msg_node, 0.5);
    } else
        gtk_clist_thaw(clist);
    gtk_signal_handler_unblock_by_func(GTK_OBJECT(bindex->ctree),
                                       expand ? tree_expand_cb :
                                       tree_collapse_cb, bindex);
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
    GtkCList *clist;

    g_return_if_fail (bindex);
    g_return_if_fail (GTK_IS_CLIST(bindex->ctree));
    g_return_if_fail (bindex->mailbox_node != NULL);
    g_return_if_fail (bindex->mailbox_node->mailbox != NULL);

    clist = GTK_CLIST(bindex->ctree);
    bindex->threading_type = thtype;
    
    gtk_ctree_set_line_style (
            bindex->ctree,
            (thtype == BALSA_INDEX_THREADING_FLAT)?
                GTK_CTREE_LINES_NONE: GTK_CTREE_LINES_SOLID);
    
    mailbox = bindex->mailbox_node->mailbox;

    gtk_clist_freeze(clist);
    gtk_clist_clear(clist);
    
    for (list = mailbox->message_list; list; list = list->next)
	balsa_index_add(bindex, LIBBALSA_MESSAGE(list->data));
    /* do threading */
    balsa_index_threading(bindex);
    gtk_clist_sort(clist);
    DO_CLIST_WORKAROUND(clist);
    gtk_clist_thaw(clist);
    balsa_index_update_tree(bindex, 
                            balsa_app.expand_tree 
                            /* *** Config: "Expand tree by default" */);


    /* set the menu apriopriately */
    balsa_window_set_threading_menu(thtype);
}

static void
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

static void
refresh_size(GtkCTree * ctree,
	     GtkCTreeNode *node,
	     gpointer data)
{
    gchar *txt_new;
    LibBalsaMessage * message;
    
    message = LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data (ctree, node));
    txt_new = libbalsa_message_size_to_gchar(message,
					     GPOINTER_TO_INT(data));
    gtk_ctree_node_set_text (ctree, node, 6, txt_new);
    g_free (txt_new);
}

void
balsa_index_refresh_size (GtkNotebook *notebook,
			  GtkNotebookPage *page,
			  gint page_num,
			  gpointer data)
{
    BalsaIndex *bindex;
    GtkWidget *index;
    GtkCTreeNode *node;
    GtkCTree *ctree;

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
    if (ctree)
	gtk_ctree_pre_recursive(ctree,
				NULL,
				refresh_size,
				GINT_TO_POINTER(bindex->line_length));
}

static void
refresh_date(GtkCTree * ctree,
	     GtkCTreeNode *node,
	     gpointer data)
{
    LibBalsaMessage * message;
    gchar *txt_new;

    message = LIBBALSA_MESSAGE(gtk_ctree_node_get_row_data (ctree, node));
    txt_new = libbalsa_message_date_to_gchar(message,
					     (gchar*) data);
    gtk_ctree_node_set_text (ctree, node, 5, txt_new);
    g_free (txt_new);
}

void
balsa_index_refresh_date (GtkNotebook *notebook,
			  GtkNotebookPage *page,
			  gint page_num,
			  gpointer data)
{
    BalsaIndex *bindex;
    GtkWidget *index;
    GtkCTreeNode *node;
    GtkCTree *ctree;

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
    if (ctree)	
	gtk_ctree_pre_recursive(ctree,
				NULL,
				refresh_date,
				bindex->date_string);
}

/* idle handler wrappers
 *
 * first a structure for holding the relevant info 
 */
static struct {
    gpointer data;      /* data supplied to gtk_idle_add */
    gint id;            /* id returned by gtk_idle_add   */
} handler = {NULL, 0};

/* balsa_index_idle_remove:
 * if an idle handler has been set, with matching data, remove it
 */
static void
balsa_index_idle_remove(gpointer data)
{
    if (handler.id && handler.data == data) {
        gtk_idle_remove(handler.id);
        handler.id = 0;
    }
}

/* balsa_index_idle_add:
 * first remove any existing handler with matching data, then set a new
 * one
 */
static void
balsa_index_idle_add(gpointer data, gpointer message)
{
    if (!balsa_app.previewpane)
        return;
    balsa_index_idle_remove(data);
    replace_attached_data(GTK_OBJECT(data), "message", message);
    handler.id = gtk_idle_add((GtkFunction) idle_handler_cb, data);
    handler.data = data;
}

/* balsa_index_idle_clear:
 * if the handler id is set and the data match, clear the id;
 * return value shows success
 */
static gboolean
balsa_index_idle_clear(gpointer data)
{
    gboolean ret = handler.id && handler.data == data;
    if (ret)
        handler.id = 0;
    return ret;
}

/* balsa_index_hide_deleted:
 * called from pref manager when balsa_app.hide_deleted is changed.
 */
void
balsa_index_hide_deleted(gboolean hide)
{
    gint i;
    GtkWidget *page;

    for (i = 0; (page =
                 gtk_notebook_get_nth_page(GTK_NOTEBOOK
                                           (balsa_app.notebook),
                                           i)) != NULL; ++i)
        hide_deleted(BALSA_INDEX(page), hide);
}

/* hide_deleted:
 * hide (or show, if hide is FALSE) deleted messages.
 */
static void
hide_deleted(BalsaIndex * bindex, gboolean hide)
{
    LibBalsaMailbox *mailbox = bindex->mailbox_node->mailbox;
    GList *list;
    GList *messages = NULL;

    for (list = mailbox->message_list; list; list = g_list_next(list)) {
        LibBalsaMessage *message = LIBBALSA_MESSAGE(list->data);

        if (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
            messages = g_list_prepend(messages, message);
    }

    if (hide)
        mailbox_messages_removed(bindex, messages);
    else
        mailbox_messages_added(bindex, messages);

    g_list_free(messages);
}

/* balsa_index_sync_backend:
 * wrapper for libbalsa_mailbox_sync_backend.
 */

void
balsa_index_sync_backend(LibBalsaMailbox *mailbox)
{
    if (balsa_app.hide_deleted || balsa_app.delete_immediately)
        libbalsa_mailbox_sync_backend(mailbox, balsa_app.delete_immediately);
}

void
balsa_index_transfer(GList * messages, LibBalsaMailbox * from_mailbox,
                     LibBalsaMailbox * to_mailbox, BalsaIndex *bindex,
                     gboolean copy)
{
    if (messages == NULL)
        return;

    if (copy)
        libbalsa_messages_copy(messages, to_mailbox);
    else {
        balsa_index_select_next_threaded(bindex);
        libbalsa_messages_move(messages, to_mailbox);
    }

    if (from_mailbox == balsa_app.trash && !copy)
        enable_empty_trash(TRASH_CHECK);
    else if (to_mailbox == balsa_app.trash)
        enable_empty_trash(TRASH_FULL);
}
