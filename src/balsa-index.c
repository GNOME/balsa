/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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

/* TREE_VIEW_FIXED_HEIGHT enables hight-performance mode of GtkTreeView
 * very useful for large mailboxes (#msg >5000) but: a. is available only
 * in gtk2>=2.3.5 b. may expose some bugs in gtk.
 */
#define TREE_VIEW_FIXED_HEIGHT 1

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <gnome.h>
#include <glib.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "main-window.h"
#include "message-window.h"
#include "sendmsg-window.h"
#include "store-address.h"

#include "libbalsa/misc.h"

/* gtk widget */
static void bndx_class_init(BalsaIndexClass * klass);
static void bndx_instance_init(BalsaIndex * index);
static void bndx_destroy(GtkObject * obj);
static gboolean bndx_popup_menu(GtkWidget * widget);

/* statics */

/* Manage the tree view */
static gboolean bndx_row_is_viewable(BalsaIndex * index,
                                     GtkTreePath * path);
static void bndx_expand_to_row_and_select(BalsaIndex * index,
                                          GtkTreeIter * iter);
static void bndx_changed_find_row(BalsaIndex * index);

/* mailbox callbacks */
static void bndx_mailbox_changed_cb(BalsaIndex * index);

/* GtkTree* callbacks */
static void bndx_selection_changed(GtkTreeSelection * selection,
                                   gpointer data);
static void bndx_selection_changed_func(GtkTreeModel * model,
                                        GtkTreePath * path,
                                        GtkTreeIter * iter, gpointer data);
static gboolean bndx_button_event_press_cb(GtkWidget * tree_view,
                                           GdkEventButton * event,
                                           gpointer data);
static void bndx_row_activated(GtkTreeView * tree_view, GtkTreePath * path,
                               GtkTreeViewColumn * column,
                               gpointer user_data);
static void bndx_column_resize(GtkWidget * widget,
                               GtkAllocation * allocation, gpointer data);
static void bndx_tree_expand_cb(GtkTreeView * tree_view,
                                GtkTreeIter * iter, GtkTreePath * path,
                                gpointer user_data);

/* formerly balsa-index-page stuff */
enum {
    TARGET_MESSAGES
};

static GtkTargetEntry index_drag_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

static void bndx_drag_cb(GtkWidget* widget,
                                GdkDragContext* drag_context,
                                GtkSelectionData* data,
                                guint info,
                                guint time,
                                gpointer user_data);

/* Popup menu */
static GtkWidget* bndx_popup_menu_create(BalsaIndex * index);
static void bndx_do_popup(BalsaIndex * index, GdkEventButton * event);
static GtkWidget *create_stock_menu_item(GtkWidget * menu,
                                         const gchar * type,
                                         const gchar * label,
                                         GtkSignalFunc cb, gpointer data);

static void sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data);

/* signals */
enum {
    INDEX_CHANGED,
    LAST_SIGNAL
};

static gint balsa_index_signals[LAST_SIGNAL] = {
    0
};

/* marshallers */
typedef void (*BalsaIndexSignal1) (GtkObject * object,
				   LibBalsaMessage * message,
				   GdkEventButton * bevent, gpointer data);

/* General helpers. */
static gboolean bndx_find_message(BalsaIndex * index, GtkTreePath ** path,
                                  GtkTreeIter * iter,
                                  LibBalsaMessage * message);
static void bndx_expand_to_row(BalsaIndex * index, GtkTreePath * path);
static void bndx_select_row(BalsaIndex * index, GtkTreePath * path);

/* Other callbacks. */
static void bndx_store_address(GtkWidget * widget, gpointer data);

static GtkTreeViewClass *parent_class = NULL;

/* Class type. */
GtkType
balsa_index_get_type(void)
{
    static GtkType balsa_index_type = 0;

    if (!balsa_index_type) {
        static const GTypeInfo balsa_index_info = {
            sizeof(BalsaIndexClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) bndx_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(BalsaIndex),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) bndx_instance_init
        };

        balsa_index_type =
            g_type_register_static(GTK_TYPE_TREE_VIEW,
                                   "BalsaIndex", &balsa_index_info, 0);
    }

    return balsa_index_type;
}

/* BalsaIndex class init method. */
static void
bndx_class_init(BalsaIndexClass * klass)
{
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;

    object_class = (GtkObjectClass *) klass;
    widget_class = (GtkWidgetClass *) klass;

    parent_class = gtk_type_class(GTK_TYPE_TREE_VIEW);

    balsa_index_signals[INDEX_CHANGED] = 
        g_signal_new("index-changed",
                     G_TYPE_FROM_CLASS(object_class),   
		     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaIndexClass, 
                                     index_changed),
                     NULL, NULL,
		     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);

    object_class->destroy = bndx_destroy;
    widget_class->popup_menu = bndx_popup_menu;
    klass->index_changed = NULL;
}

/* Object class destroy method. */
static void
bndx_destroy(GtkObject * obj)
{
    BalsaIndex *index;

    g_return_if_fail(obj != NULL);
    index = BALSA_INDEX(obj);

    if (index->mailbox_node) {
	LibBalsaMailbox* mailbox;
	
	if ((mailbox = index->mailbox_node->mailbox)) {
	    g_signal_handlers_disconnect_matched(mailbox,
						 G_SIGNAL_MATCH_DATA,
						 0, 0, NULL, NULL, index);
	    gtk_tree_view_set_model(GTK_TREE_VIEW(index), NULL);
	    libbalsa_mailbox_close(mailbox);

	    libbalsa_mailbox_search_iter_free(index->search_iter);
	    index->search_iter = NULL;
	}
        g_object_unref(index->mailbox_node);
	index->mailbox_node = NULL;
    }

    if (index->popup_menu) {
        g_object_unref(index->popup_menu);
        index->popup_menu = NULL;
    }

    if (index->current_message) {
	g_object_remove_weak_pointer(G_OBJECT(index->current_message),
				     (gpointer)&index->current_message);
	index->current_message = NULL;
    }

    if (index->selected) {
	g_slist_foreach(index->selected, (GFunc) g_object_unref, NULL);
	g_slist_free(index->selected);
	index->selected = NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
        (*GTK_OBJECT_CLASS(parent_class)->destroy) (obj);
}

/* Widget class popup menu method. */
static gboolean
bndx_popup_menu(GtkWidget * widget)
{
    bndx_do_popup(BALSA_INDEX(widget), NULL);
    return TRUE;
}

/* BalsaIndex instance init method; no tree store is set on the tree
 * view--that's handled later, when the view is populated. */
#define INDEX_ICON_SZ 16
static void
bndx_instance_init(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

#if GTK_CHECK_VERSION(2,4,0) && defined(TREE_VIEW_FIXED_HEIGHT)
    {
        GValue val = {0};
        g_value_init (&val, G_TYPE_BOOLEAN);
        g_value_set_boolean(&val, TRUE);
        g_object_set_property(G_OBJECT(index), "fixed_height_mode",
                              &val);
        g_value_unset(&val);
    }
#define set_sizing(col) \
      gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED)
#else
#define set_sizing(col)
#endif
    /* Index column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "#");
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", LB_MBOX_MSGNO_COL,
                                        NULL);
    gtk_tree_view_column_set_sort_column_id(column, LB_MBOX_MSGNO_COL);
    set_sizing(column); gtk_tree_view_append_column(tree_view, column);

    /* Status icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_renderer_set_fixed_size(renderer, INDEX_ICON_SZ, INDEX_ICON_SZ);
    column =
        gtk_tree_view_column_new_with_attributes("S", renderer,
                                                 "pixbuf", LB_MBOX_MARKED_COL,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    set_sizing(column); gtk_tree_view_append_column(tree_view, column);

    /* Attachment icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_renderer_set_fixed_size(renderer, INDEX_ICON_SZ, INDEX_ICON_SZ);
    column =
        gtk_tree_view_column_new_with_attributes("A", renderer,
                                                 "pixbuf", LB_MBOX_ATTACH_COL,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
     set_sizing(column); gtk_tree_view_append_column(tree_view, column);
    /* From/To column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("From"), renderer,
                                                 "text", LB_MBOX_FROM_COL,
						 "weight", LB_MBOX_WEIGHT_COL,
						 "style", LB_MBOX_STYLE_COL,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(column,
					    LB_MBOX_FROM_COL);
    gtk_tree_view_append_column(tree_view, column);

    /* Subject column--contains tree expanders */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("Subject"), renderer,
                                                 "text", LB_MBOX_SUBJECT_COL,
						 "weight", LB_MBOX_WEIGHT_COL,
						 "style", LB_MBOX_STYLE_COL,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(column,
                                            LB_MBOX_SUBJECT_COL);
    gtk_tree_view_append_column(tree_view, column);
    gtk_tree_view_set_expander_column(tree_view, column);

    /* Date column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("Date"), renderer,
                                                 "text", LB_MBOX_DATE_COL,
						 "weight", LB_MBOX_WEIGHT_COL,
						 "style", LB_MBOX_STYLE_COL,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(column,
					    LB_MBOX_DATE_COL);
    gtk_tree_view_append_column(tree_view, column);

    /* Size column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Size"));
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", LB_MBOX_SIZE_COL,
					"weight", LB_MBOX_WEIGHT_COL,
					"style", LB_MBOX_STYLE_COL,
                                        NULL);
    gtk_tree_view_column_set_sort_column_id(column,
                                            LB_MBOX_SIZE_COL);
    set_sizing(column); gtk_tree_view_append_column(tree_view, column);

    /* Initialize some other members */
    index->mailbox_node = NULL;
    index->popup_menu = bndx_popup_menu_create(index);
    g_object_ref(index->popup_menu);
    gtk_object_sink(GTK_OBJECT(index->popup_menu));
    
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    /* handle select row signals to display message in the window
     * preview pane */
    index->selection_changed_id =
        g_signal_connect(selection, "changed",
		         G_CALLBACK(bndx_selection_changed), index);

    /* we want to handle button presses to pop up context menus if
     * necessary */
    g_signal_connect(tree_view, "button_press_event",
		     G_CALLBACK(bndx_button_event_press_cb), NULL);
    g_signal_connect(tree_view, "row-activated",
		     G_CALLBACK(bndx_row_activated), NULL);

    /* catch thread expand events */
    index->row_expanded_id =
        g_signal_connect_after(tree_view, "row-expanded",
                               G_CALLBACK(bndx_tree_expand_cb), NULL);

    /* We want to catch column resize attempts to store the new value */
    g_signal_connect_after(tree_view, "size-allocate",
                           G_CALLBACK(bndx_column_resize),
                           NULL);

    gtk_drag_source_set(GTK_WIDGET (index), 
                        GDK_BUTTON1_MASK | GDK_SHIFT_MASK | GDK_CONTROL_MASK,
                        index_drag_types, ELEMENTS(index_drag_types),
                        GDK_ACTION_DEFAULT | GDK_ACTION_COPY | 
                        GDK_ACTION_MOVE);
    g_signal_connect(index, "drag-data-get",
                     G_CALLBACK(bndx_drag_cb), NULL);

    balsa_index_set_column_widths(index);
    g_get_current_time (&index->last_use);
    gtk_widget_show_all (GTK_WIDGET(index));
}

/* Callbacks used by bndx_instance_init. */

/*
 * bndx_selection_changed
 *
 * Callback for the selection "changed" signal.
 *
 * Do nothing if index->current_message is still selected;
 * otherwise, display the last (in tree order) selected message.
 */

struct BndxSelectionChangedInfo {
    LibBalsaMessage *message;
    GSList **selected;
};

static void
bndx_selection_changed(GtkTreeSelection * selection, gpointer data)
{
    BalsaIndex *index = BALSA_INDEX(data);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    struct BndxSelectionChangedInfo sci;
    GtkTreeIter iter;
    GSList *list;
    GSList *next;
    LibBalsaMailboxSearchIter *iter_view;

    /* Search results may be cached in iter_view. */
    iter_view =
	libbalsa_mailbox_search_iter_view(index->mailbox_node->mailbox);
    /* Check currently selected messages. */
    for (list = index->selected; list; list = next) {
	GtkTreePath *path;
	LibBalsaMessage *message = list->data;

	next = list->next;
	if (!bndx_find_message(index, &path, NULL, message)) {
	    /* The message is no longer in the view--it must have been
	     * either expunged or filtered out--in either case, we just
	     * drop it from the list. */
	    g_object_unref(message);
	    index->selected = g_slist_delete_link(index->selected, list);
	    continue;
	}
	if (!gtk_tree_selection_path_is_selected(selection, path)
	    && bndx_row_is_viewable(index, path)) {
	    /* The message has been deselected, and not by collapsing a
	     * thread; we'll notify the mailbox, so it can check whether
	     * the message still matches the view filter. */
	    g_object_unref(message);
	    index->selected = g_slist_delete_link(index->selected, list);
	    if (iter_view)
		libbalsa_mailbox_msgno_filt_check(message->mailbox,
						  message->msgno, 
						  iter_view);
	}
	gtk_tree_path_free(path);
    }
    libbalsa_mailbox_search_iter_free(iter_view);

    sci.selected = &index->selected;
    sci.message = NULL;
    gtk_tree_selection_selected_foreach(selection,
                                        bndx_selection_changed_func,
                                        &sci);
    if (g_slist_find(index->selected, index->current_message))
        return;

    /* we don't clear the current message if the tree contains any
     * messages */
    if (sci.message || !gtk_tree_model_get_iter_first(model, &iter)) {
	if (index->current_message)
	    g_object_remove_weak_pointer(G_OBJECT(index->current_message),
					 (gpointer)&index->current_message);
        index->current_message = sci.message;
        if(index->current_message)
            g_object_add_weak_pointer(G_OBJECT(index->current_message),
                                      (gpointer)&index->current_message);
        bndx_changed_find_row(index);
    }
}

static void
bndx_selection_changed_func(GtkTreeModel * model, GtkTreePath * path,
                            GtkTreeIter * iter, gpointer data)
{
    struct BndxSelectionChangedInfo *sci = data;

    gtk_tree_model_get(model, iter, LB_MBOX_MESSAGE_COL, &sci->message,
                       -1);
    if (!g_slist_find(*sci->selected, sci->message))
	*sci->selected = g_slist_prepend(*sci->selected, sci->message);
    else {
	g_object_unref(sci->message);
    }
}

static gboolean
bndx_button_event_press_cb(GtkWidget * widget, GdkEventButton * event,
                           gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath *path;
    BalsaIndex *index = BALSA_INDEX(widget);

    g_return_val_if_fail(event, FALSE);
    if (event->type != GDK_BUTTON_PRESS || event->button != 3
        || event->window != gtk_tree_view_get_bin_window(tree_view))
        return FALSE;

    /* pop up the context menu:
     * - if the clicked-on message is already selected, don't change
     *   the selection;
     * - if it isn't, select it (cancelling any previous selection)
     * - then create and show the menu */
    if (gtk_tree_view_get_path_at_pos(tree_view, event->x, event->y,
                                      &path, NULL, NULL, NULL)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(tree_view);

        if (!gtk_tree_selection_path_is_selected(selection, path))
            bndx_select_row(index, path);
        gtk_tree_path_free(path);
    }

    bndx_do_popup(index, event);

    return TRUE;
}

static void
bndx_row_activated(GtkTreeView * tree_view, GtkTreePath * path,
                   GtkTreeViewColumn * column, gpointer user_data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, LB_MBOX_MESSAGE_COL, &message, -1);
    g_return_if_fail(message);
    /* activate a message means open a message window,
     * unless we're in the draftbox, in which case it means open
     * a sendmsg window */
    if (message->mailbox == balsa_app.draftbox) {
        /* the simplest way to get a sendmsg window would be:
         * balsa_message_continue(widget, (gpointer) index);
         *
         * instead we'll just use the guts of
         * balsa_message_continue: */
        BalsaSendmsg *sm =
            sendmsg_window_new(GTK_WIDGET(BALSA_INDEX(tree_view)->window),
                               message, SEND_CONTINUE);
        g_signal_connect(G_OBJECT(sm->window), "destroy",
                         G_CALLBACK(sendmsg_window_destroy_cb), NULL);
    } else
        message_window_new(message);
    g_object_unref(message);
}

/* bndx_tree_expand_cb:
 * callback on expand events
 * set/reset unread style, as appropriate
 */
static void
bndx_tree_expand_cb(GtkTreeView * tree_view, GtkTreeIter * iter,
                    GtkTreePath * path, gpointer user_data)
{
    BalsaIndex *index = BALSA_INDEX(tree_view);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreePath *current_path;
    GSList *list;

    g_signal_handler_block(selection, index->selection_changed_id);
    /* If a message in the selected list has become viewable, reselect
     * it. */
    for (list = index->selected; list; list = list->next) {
	LibBalsaMessage *message = list->data;

	if (bndx_find_message(index, &current_path, NULL, message)) {

	    if (!gtk_tree_selection_path_is_selected
		(selection, current_path)
		&& bndx_row_is_viewable(index, current_path)) {
		gtk_tree_selection_select_path(selection, current_path);
		if (message == index->current_message) {
		    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(index),
						 current_path, NULL, FALSE,
						 0, 0);
		}
	    }
	    gtk_tree_path_free(current_path);
	}
    }
    g_signal_handler_unblock(selection, index->selection_changed_id);
}

/* When a column is resized, store the new size for later use */
static void
bndx_column_resize(GtkWidget * widget, GtkAllocation * allocation,
                   gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);

    balsa_app.index_num_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, LB_MBOX_MSGNO_COL));
    balsa_app.index_status_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, LB_MBOX_MARKED_COL));
    balsa_app.index_attachment_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, LB_MBOX_ATTACH_COL));
    balsa_app.index_from_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, LB_MBOX_FROM_COL));
    balsa_app.index_subject_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, LB_MBOX_SUBJECT_COL));
    balsa_app.index_date_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, LB_MBOX_DATE_COL));
    balsa_app.index_size_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, LB_MBOX_SIZE_COL));
}

/* bndx_drag_cb 
 * 
 * This is the drag_data_get callback for the index widgets.  It
 * copies the list of selected messages to a pointer array, then sets
 * them as the DND data. Currently only supports DND within the
 * application.
 *  */
static void 
bndx_drag_cb (GtkWidget* widget, GdkDragContext* drag_context, 
               GtkSelectionData* data, guint info, guint time, 
               gpointer user_data)
{ 
    LibBalsaMessage* message;
    GPtrArray* message_array = NULL;
    GList* list, *l;
    

    g_return_if_fail (widget != NULL);
    
    message_array = g_ptr_array_new ();
    l = balsa_index_selected_list(BALSA_INDEX(widget));
    for (list = l; list; list = g_list_next(list)) {
        message = list->data;
        g_ptr_array_add (message_array, message);
    }
    g_list_foreach(l, (GFunc)g_object_unref, NULL); /* FIXME: too early? */
    g_list_free(l);
    
    if (message_array) {
        g_ptr_array_add (message_array, NULL);
        gtk_selection_data_set (data, data->target, 8, 
                                (guchar*) message_array->pdata, 
                                (message_array->len)*sizeof (gpointer));
        /* the selection data makes a copy of the data, we 
         * can free it now. */
        g_ptr_array_free (message_array, TRUE);
    }
}

/* Public methods */
GtkWidget *
balsa_index_new(void)
{
    BalsaIndex* index = g_object_new(BALSA_TYPE_INDEX, NULL);

    return GTK_WIDGET(index);
}

/**
 * balsa_index_scroll_on_open()
 * moves to the first unread message in the index, or the
 * last message if none is unread, and selects it.
 */
void
balsa_index_scroll_on_open(BalsaIndex *index)
{
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    GtkTreeIter iter;
    GtkTreePath *path = NULL;
    unsigned msgno;

    balsa_index_update_tree(index, balsa_app.expand_tree);
    if (mailbox->first_unread) {
	msgno = mailbox->first_unread;
	mailbox->first_unread = 0;
    } else
	msgno = libbalsa_mailbox_total_messages(mailbox);
    if(msgno>0 &&
       libbalsa_mailbox_msgno_find(mailbox, msgno, &path, &iter)) {
        bndx_expand_to_row(index, path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(index), path, NULL,
				     FALSE, 0, 0);
        if(balsa_app.view_message_on_open)
            bndx_select_row(index, path);
        gtk_tree_path_free(path);
    }
}

static LibBalsaCondition cond_undeleted =
{
    TRUE,
    CONDITION_FLAG,
    {
       {
           LIBBALSA_MESSAGE_FLAG_DELETED
       }
    }
};

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
balsa_index_load_mailbox_node (BalsaIndex * index,
                               BalsaMailboxNode* mbnode, GError **err)
{
    GtkTreeView *tree_view;
    LibBalsaMailbox* mailbox;
    gchar *msg;
    gboolean successp;

    g_return_val_if_fail(BALSA_IS_INDEX(index), TRUE);
    g_return_val_if_fail(index->mailbox_node == NULL, TRUE);
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), TRUE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox), TRUE);

    mailbox = mbnode->mailbox;
    msg = g_strdup_printf(_("Opening mailbox %s. Please wait..."),
			  mbnode->mailbox->name);
    gnome_appbar_push(balsa_app.appbar, msg);
    g_free(msg);
    gdk_threads_leave();
    successp = libbalsa_mailbox_open(mailbox, err);
    gdk_threads_enter();
    gnome_appbar_pop(balsa_app.appbar);

    if (!successp)
	return TRUE;

    /*
     * set the new mailbox
     */
    index->mailbox_node = mbnode;
    g_object_ref(G_OBJECT(mbnode));
    /*
     * rename "from" column to "to" for outgoing mail
     */
    tree_view = GTK_TREE_VIEW(index);
    if (mailbox->view->show == LB_MAILBOX_SHOW_TO) {
        GtkTreeViewColumn *column =
	    gtk_tree_view_get_column(tree_view, LB_MBOX_FROM_COL);

        gtk_tree_view_column_set_title(column, _("To"));
    }

    g_signal_connect_swapped(G_OBJECT(mailbox), "changed",
			     G_CALLBACK(bndx_mailbox_changed_cb),
			     (gpointer) index);

    balsa_window_enable_mailbox_menus(index);
    /* libbalsa functions must be called with gdk unlocked
     * but balsa_index - locked!
     */
    gdk_flush();
    gdk_threads_leave();
    libbalsa_mailbox_set_view_filter(mailbox,
                                     balsa_window_get_view_filter(balsa_app.main_window),
                                     FALSE);
    libbalsa_mailbox_set_threading(mailbox,
                                   mailbox->view->threading_type);

    gdk_threads_enter();

    /* Set the tree store*/
#ifndef GTK2_FETCHES_ONLY_VISIBLE_CELLS
    g_object_set_data(G_OBJECT(mailbox), "tree-view", tree_view);
#endif
    gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(mailbox));

    /* Create a search-iter for SEARCH UNDELETED. */
    index->search_iter = libbalsa_mailbox_search_iter_new(&cond_undeleted);

    return FALSE;
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
 *
 * - balsa_index_find:
 *   selects next or previous message matching the given conditions
 *   callback for the edit=>find menu action
 */

typedef enum {
    BNDX_SEARCH_DIRECTION_NEXT,
    BNDX_SEARCH_DIRECTION_PREV
} BndxSearchDirection;

typedef enum {
    BNDX_SEARCH_VIEWABLE_ANY,
    BNDX_SEARCH_VIEWABLE_ONLY
} BndxSearchViewable;

typedef enum {
    BNDX_SEARCH_START_ANY,
    BNDX_SEARCH_START_CURRENT
} BndxSearchStart;

typedef enum {
    BNDX_SEARCH_WRAP_YES,
    BNDX_SEARCH_WRAP_NO
} BndxSearchWrap;

static gboolean
bndx_find_root(BalsaIndex * index, GtkTreeIter * iter)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);

    if (!gtk_tree_model_get_iter_first(model, iter))
	return FALSE;

    iter->user_data = NULL;
    return TRUE;
}

static gboolean
bndx_search_iter(BalsaIndex * index,
		 LibBalsaMailboxSearchIter * search_iter,
		 GtkTreeIter * iter,
		 BndxSearchDirection direction,
		 BndxSearchViewable viewable,
		 guint stop_msgno)
{
    gboolean found;

    do {
	GtkTreePath *path;

	found =
	    libbalsa_mailbox_search_iter_step(index->mailbox_node->mailbox,
					      search_iter, iter,
					      direction ==
					      BNDX_SEARCH_DIRECTION_NEXT,
					      stop_msgno);
	if (!found)
	    break;
	if (viewable == BNDX_SEARCH_VIEWABLE_ANY)
	    break;

	path = gtk_tree_model_get_path(GTK_TREE_MODEL
				       (index->mailbox_node->mailbox),
				       iter);
	found = bndx_row_is_viewable(index, path);
	gtk_tree_path_free(path);
    } while (!found);

    return found;
}

static gboolean
bndx_search_iter_and_select(BalsaIndex * index,
			    LibBalsaMailboxSearchIter * search_iter,
			    BndxSearchDirection direction,
			    BndxSearchViewable viewable,
			    BndxSearchStart start,
			    BndxSearchWrap wrap)
{
    GtkTreeIter iter;
    guint stop_msgno;

    if (!(bndx_find_message(index, NULL, &iter, index->current_message)
	  || (start == BNDX_SEARCH_START_ANY
	      && bndx_find_root(index, &iter))))
	return FALSE;

    stop_msgno = 0;
    if (wrap == BNDX_SEARCH_WRAP_YES && index->current_message)
	stop_msgno = index->current_message->msgno;
    if (!bndx_search_iter(index, search_iter, &iter, direction, viewable,
			  stop_msgno))
	return FALSE;

    bndx_expand_to_row_and_select(index, &iter);
    return TRUE;
}

void
balsa_index_select_next(BalsaIndex * index)
{
    bndx_search_iter_and_select(index, index->search_iter,
				BNDX_SEARCH_DIRECTION_NEXT,
				BNDX_SEARCH_VIEWABLE_ONLY,
				BNDX_SEARCH_START_CURRENT,
				BNDX_SEARCH_WRAP_NO);
}

static void
bndx_select_next_threaded(BalsaIndex * index)
{
    if (!bndx_search_iter_and_select(index, index->search_iter,
				     BNDX_SEARCH_DIRECTION_NEXT,
				     BNDX_SEARCH_VIEWABLE_ANY,
				     BNDX_SEARCH_START_CURRENT,
				     BNDX_SEARCH_WRAP_NO))
	bndx_search_iter_and_select(index, index->search_iter,
				    BNDX_SEARCH_DIRECTION_PREV,
				    BNDX_SEARCH_VIEWABLE_ONLY,
				    BNDX_SEARCH_START_CURRENT,
				    BNDX_SEARCH_WRAP_NO);
}

void
balsa_index_select_previous(BalsaIndex * index)
{
    bndx_search_iter_and_select(index, index->search_iter,
				BNDX_SEARCH_DIRECTION_PREV,
				BNDX_SEARCH_VIEWABLE_ONLY,
				BNDX_SEARCH_START_CURRENT,
				BNDX_SEARCH_WRAP_NO);
}

void
balsa_index_find(BalsaIndex * index,
		 LibBalsaMailboxSearchIter * search_iter,
		 gboolean previous)
{
    bndx_search_iter_and_select(index, search_iter,
				(previous ? BNDX_SEARCH_DIRECTION_PREV :
				 BNDX_SEARCH_DIRECTION_NEXT),
				BNDX_SEARCH_VIEWABLE_ANY,
				BNDX_SEARCH_START_ANY,
				BNDX_SEARCH_WRAP_NO);
}

static void
bndx_select_next_with_flag(BalsaIndex * index, LibBalsaMessageFlag flag)
{
    LibBalsaCondition cond_flag, cond_and;
    LibBalsaMailboxSearchIter *search_iter;

    cond_flag.negate      = FALSE;
    cond_flag.type        = CONDITION_FLAG;
    cond_flag.match.flags = flag;
    cond_and.negate            = FALSE;
    cond_and.type              = CONDITION_AND;
    cond_and.match.andor.left  = &cond_flag;
    cond_and.match.andor.right = &cond_undeleted;
    search_iter = libbalsa_mailbox_search_iter_new(&cond_and);

    bndx_search_iter_and_select(index, search_iter,
				BNDX_SEARCH_DIRECTION_NEXT,
				BNDX_SEARCH_VIEWABLE_ANY,
				BNDX_SEARCH_START_ANY,
				BNDX_SEARCH_WRAP_YES);

    libbalsa_mailbox_search_iter_free(search_iter);
}

void
balsa_index_select_next_unread(BalsaIndex * index)
{
    bndx_select_next_with_flag(index, LIBBALSA_MESSAGE_FLAG_NEW);
}

void
balsa_index_select_next_flagged(BalsaIndex * index)
{
    bndx_select_next_with_flag(index, LIBBALSA_MESSAGE_FLAG_FLAGGED);
}

/* bndx_expand_to_row_and_select:
 * make sure it's viewable, then pass it to bndx_select_row
 * no-op if it's NULL
 *
 * Note: iter must be valid; it isn't checked here.
 */
static void
bndx_expand_to_row_and_select(BalsaIndex * index, GtkTreeIter * iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    GtkTreePath *path;

    path = gtk_tree_model_get_path(model, iter);
    bndx_expand_to_row(index, path);
    bndx_select_row(index, path);
    gtk_tree_path_free(path);
}

/* End of select message interfaces. */

void
balsa_index_set_column_widths(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);

#if GTK_CHECK_VERSION(2,4,0) && defined(TREE_VIEW_FIXED_HEIGHT)
    /* so that fixed width works properly */
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_MSGNO_COL),
                                         40); /* get a better guess */ 
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_SIZE_COL),
                                         40); /* get a better guess */ 
#endif
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_MARKED_COL),
                                         INDEX_ICON_SZ+2); 
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_ATTACH_COL),
                                         INDEX_ICON_SZ+2);
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_FROM_COL),
                                         balsa_app.index_from_width);
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_SUBJECT_COL),
                                         balsa_app.index_subject_width);
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_DATE_COL),
                                         balsa_app.index_date_width);
}

/* Mailbox Callbacks... */

/* bndx_mailbox_changed_cb : callback for sync with backend; the signal
   is emitted by the mailbox when new messages has been retrieved (either
   after opening the mailbox, or after "check new messages").
*/
/* Helper of the callback */
static void
bndx_mailbox_changed_func(BalsaIndex * bindex)
{
    LibBalsaMailbox *mailbox = bindex->mailbox_node->mailbox;
    GtkTreePath *path;

    if (!GTK_WIDGET_REALIZED(GTK_WIDGET(bindex)))
	return;

    if (bindex->current_message
	&& LIBBALSA_MESSAGE_IS_DELETED(bindex->current_message))
	bndx_select_next_threaded(bindex);

    if (mailbox->first_unread
	&& libbalsa_mailbox_msgno_find(mailbox, mailbox->first_unread,
				       &path, NULL)) {
	bndx_expand_to_row(bindex, path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(bindex), path, NULL,
		FALSE, 0, 0);
        gtk_tree_path_free(path);
	mailbox->first_unread = 0;
    }

    if (bndx_find_message(bindex, &path, NULL, bindex->current_message)) {
	GtkTreeSelection *selection =
	    gtk_tree_view_get_selection(GTK_TREE_VIEW(bindex));
	bndx_expand_to_row(bindex, path);
	/* Selection is somehow lost when adding a message: fix it up
	 * here. */
	if (!gtk_tree_selection_path_is_selected(selection, path))
	    bndx_select_row(bindex, path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(bindex), path, NULL,
				     FALSE, 0, 0);
        gtk_tree_path_free(path);
    }

    g_get_current_time (&bindex->last_use);
    bndx_changed_find_row(bindex);
}


/* bndx_mailbox_changed_cb:
   may be called from a thread. Use idle callback to update the view.
*/
struct index_info {
    BalsaIndex * bindex;
};

static gboolean
bndx_mailbox_changed_idle(struct index_info* arg)
{
    gdk_threads_enter();
    if(arg->bindex) {
        g_object_remove_weak_pointer(G_OBJECT(arg->bindex),
                                     (gpointer) &arg->bindex);
	bndx_mailbox_changed_func(arg->bindex);
    }
    gdk_threads_leave();
    g_free(arg);
    return FALSE;
}
    
static void
bndx_mailbox_changed_cb(BalsaIndex * bindex)
{
    struct index_info *arg = g_new(struct index_info,1);
    arg->bindex  = bindex;
    g_object_add_weak_pointer(G_OBJECT(bindex), (gpointer) &arg->bindex);
    g_idle_add((GSourceFunc) bndx_mailbox_changed_idle, arg);
}

static void
bndx_view_source(GtkWidget * widget, gpointer data)
{
    GList *messages = balsa_index_selected_list(BALSA_INDEX(data));
    GList *list;

    for (list = messages; list; list = list->next) {
	LibBalsaMessage *message = list->data;

	libbalsa_show_message_source(message, balsa_app.message_font,
				     &balsa_app.source_escape_specials);
    }
    g_list_foreach(messages, (GFunc)g_object_unref, NULL);
    g_list_free(messages);
}

static void
bndx_store_address(GtkWidget * widget, gpointer data)
{
    GList *messages = balsa_index_selected_list(BALSA_INDEX(data));

    balsa_store_address(messages);
    g_list_foreach(messages, (GFunc)g_object_unref, NULL);
    g_list_free(messages);
}

static void
balsa_index_selected_list_func(GtkTreeModel * model, GtkTreePath * path,
                        GtkTreeIter * iter, gpointer data)
{
    GList **list = data;
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, LB_MBOX_MESSAGE_COL, &message, -1);
    *list = g_list_prepend(*list, message);
}

/*
 * balsa_index_selected_list: create a GList of selected messages
 *
 * Free with g_list_free.
 */
GList *
balsa_index_selected_list(BalsaIndex * index)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
    GList *list = NULL;

    gtk_tree_selection_selected_foreach(selection,
                                        balsa_index_selected_list_func,
                                        &list);
    if (index->current_message
        && !g_list_find(list, index->current_message))
        list = g_list_prepend(list, index->current_message);
 
    return list;
}

/*
 * bndx_compose_foreach: create a compose window for each selected
 * message
 */
static void
bndx_compose_foreach(GtkWidget * w, BalsaIndex * index,
                     SendType send_type)
{
    GList *list, *l = balsa_index_selected_list(index);

    for (list = l; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;
        BalsaSendmsg *sm = sendmsg_window_new(NULL, message, send_type);

        g_signal_connect(G_OBJECT(sm->window), "destroy",
                         G_CALLBACK(sendmsg_window_destroy_cb), NULL);
    }
    g_list_foreach(l, (GFunc)g_object_unref, NULL);
    g_list_free(l);
}

/*
 * Public `reply' methods
 */
void
balsa_message_reply(GtkWidget * widget, gpointer user_data)
{
    bndx_compose_foreach(widget, BALSA_INDEX (user_data), SEND_REPLY);
}

void
balsa_message_replytoall(GtkWidget * widget, gpointer user_data)
{
    bndx_compose_foreach(widget, BALSA_INDEX (user_data), SEND_REPLY_ALL);
}

void
balsa_message_replytogroup(GtkWidget * widget, gpointer user_data)
{
    bndx_compose_foreach(widget, BALSA_INDEX (user_data), SEND_REPLY_GROUP);
}

void
balsa_message_continue(GtkWidget * widget, gpointer user_data)
{
    bndx_compose_foreach(widget, BALSA_INDEX (user_data), SEND_CONTINUE);
}

/*
 * bndx_compose_from_list: create a single compose window for the
 * selected messages
 */
static void
bndx_compose_from_list(GtkWidget * w, BalsaIndex * index,
                       SendType send_type)
{
    BalsaSendmsg *sm;
    GList *list = balsa_index_selected_list(index);

    if (list) {
        sm = sendmsg_window_new_from_list(w, list, send_type);
        g_signal_connect(G_OBJECT(sm->window), "destroy",
                         G_CALLBACK(sendmsg_window_destroy_cb), NULL);

	g_list_foreach(list, (GFunc)g_object_unref, NULL);
        g_list_free(list);
    }
}

/*
 * Public forwarding methods
 */
void
balsa_message_forward_attached(GtkWidget * widget, gpointer user_data)
{
    bndx_compose_from_list(widget, BALSA_INDEX(user_data),
                           SEND_FORWARD_ATTACH);
}

void
balsa_message_forward_inline(GtkWidget * widget, gpointer user_data)
{
    bndx_compose_from_list(widget, BALSA_INDEX(user_data),
                           SEND_FORWARD_INLINE);
}

void
balsa_message_forward_default(GtkWidget * widget, gpointer user_data)
{
    bndx_compose_from_list(widget, BALSA_INDEX(user_data),
                           balsa_app.forward_attached
                           ? SEND_FORWARD_ATTACH : SEND_FORWARD_INLINE);
}

/*
 * bndx_do_delete: helper for message delete methods
 */
static void
bndx_do_delete(BalsaIndex* index, gboolean move_to_trash)
{
    BalsaIndex *trash = balsa_find_index_by_mailbox(balsa_app.trash);
    GList *messages = balsa_index_selected_list(index);
    GList *list = messages;

    while (list) {
        GList *next = g_list_next(list);
        LibBalsaMessage * message = list->data;

        if (LIBBALSA_MESSAGE_IS_DELETED(message)) {
            messages = g_list_delete_link(messages, list);
	    g_object_unref(message);
	}

        list = next;
    }

    if(messages) {
	if (move_to_trash && (index != trash)) {
	    libbalsa_messages_move(messages, balsa_app.trash);
	    enable_empty_trash(TRASH_FULL);
	} else {
	    libbalsa_messages_change_flag(messages,
                                          LIBBALSA_MESSAGE_FLAG_DELETED,
                                          TRUE);
	    if (index == trash)
		enable_empty_trash(TRASH_CHECK);
	}
	g_list_foreach(messages, (GFunc)g_object_unref, NULL);
	g_list_free(messages);
    }
}

/*
 * Public message delete methods
 */
void
balsa_message_move_to_trash(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);
    bndx_do_delete(BALSA_INDEX(user_data), TRUE);
}

gint
balsa_find_notebook_page_num(LibBalsaMailbox * mailbox)
{
    GtkWidget *page;
    guint i;

    for (i = 0;
         (page =
          gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
         i++) {
        GtkWidget *index = gtk_bin_get_child(GTK_BIN(page));

        if (BALSA_INDEX(index)->mailbox_node->mailbox == mailbox)
            return i;
    }

    /* didn't find a matching mailbox */
    return -1;
}

/* This function toggles the given attribute of a list of messages,
   using given callback.
 */
void
balsa_index_toggle_flag(BalsaIndex* index, LibBalsaMessageFlag flag)
{
    GList *list, *l;
    int is_all_flagged = TRUE;

    /* First see if we should set given flag or unset */
    l = balsa_index_selected_list(index);
    for (list = l; list; list = g_list_next(list)) {
	if (!LIBBALSA_MESSAGE_HAS_FLAG(list->data, flag)) {
	    is_all_flagged = FALSE;
	    break;
	}
    }

    libbalsa_messages_change_flag(l, flag, !is_all_flagged);

    g_list_foreach(l, (GFunc)g_object_unref, NULL);
    g_list_free(l);
}

static void
bi_toggle_deleted_cb(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_index_toggle_flag(BALSA_INDEX(user_data), 
                            LIBBALSA_MESSAGE_FLAG_DELETED);
}
/* This function toggles the FLAGGED attribute of a list of messages
 */
static void
bi_toggle_flagged_cb(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_index_toggle_flag(BALSA_INDEX(user_data), 
                            LIBBALSA_MESSAGE_FLAG_FLAGGED);
}

static void
bi_toggle_new_cb(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_index_toggle_flag(BALSA_INDEX(user_data), 
                            LIBBALSA_MESSAGE_FLAG_NEW);
}


static void
mru_menu_cb(gchar * url, BalsaIndex * index)
{
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);

    g_return_if_fail(mailbox != NULL);

    if (index->mailbox_node->mailbox != mailbox) {
        GList *messages = balsa_index_selected_list(index);
        balsa_index_transfer(index, messages, mailbox, FALSE);
	g_list_foreach(messages, (GFunc)g_object_unref, NULL);
        g_list_free(messages);
    }
}

/*
 * bndx_popup_menu_create: create the popup menu at init time
 */
static GtkWidget *
bndx_popup_menu_create(BalsaIndex * index)
{
    const static struct {       /* this is a invariable part of */
        const char *icon, *label;       /* the context message menu.    */
        GtkSignalFunc func;
    } entries[] = {
        {
        BALSA_PIXMAP_MENU_REPLY, N_("_Reply..."),
                GTK_SIGNAL_FUNC(balsa_message_reply)}, {
        BALSA_PIXMAP_MENU_REPLY_ALL, N_("Reply To _All..."),
                GTK_SIGNAL_FUNC(balsa_message_replytoall)}, {
        BALSA_PIXMAP_MENU_REPLY_GROUP, N_("Reply To _Group..."),
                GTK_SIGNAL_FUNC(balsa_message_replytogroup)}, {
        BALSA_PIXMAP_MENU_FORWARD, N_("_Forward Attached..."),
                GTK_SIGNAL_FUNC(balsa_message_forward_attached)}, {
        BALSA_PIXMAP_MENU_FORWARD, N_("Forward _Inline..."),
                GTK_SIGNAL_FUNC(balsa_message_forward_inline)}, {
        GNOME_STOCK_BOOK_RED, N_("_Store Address..."),
                GTK_SIGNAL_FUNC(bndx_store_address)}};
    GtkWidget *menu, *menuitem, *submenu;
    unsigned i;

    menu = gtk_menu_new();

    for (i = 0; i < ELEMENTS(entries); i++)
        create_stock_menu_item(menu, entries[i].icon, _(entries[i].label),
                               entries[i].func, index);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), 
                          gtk_separator_menu_item_new());
    index->delete_item =
        create_stock_menu_item(menu, GNOME_STOCK_TRASH,
                               _("_Delete"),
                               GTK_SIGNAL_FUNC(bi_toggle_deleted_cb),
                               index);
    index->undelete_item =
        create_stock_menu_item(menu, GTK_STOCK_UNDELETE,
                               _("_Undelete"),
                               GTK_SIGNAL_FUNC(bi_toggle_deleted_cb),
                               index);
    index->move_to_trash_item =
        create_stock_menu_item(menu, GNOME_STOCK_TRASH,
                               _("Move To _Trash"),
                               GTK_SIGNAL_FUNC
                               (balsa_message_move_to_trash), index);

    menuitem = gtk_menu_item_new_with_mnemonic(_("T_oggle"));
    index->toggle_item = menuitem;
    submenu = gtk_menu_new();
    create_stock_menu_item(submenu, BALSA_PIXMAP_MENU_FLAGGED,
                           _("_Flagged"),
                           GTK_SIGNAL_FUNC(bi_toggle_flagged_cb),
                           index);
    create_stock_menu_item(submenu, BALSA_PIXMAP_MENU_NEW, _("_Unread"),
                           GTK_SIGNAL_FUNC(bi_toggle_new_cb),
                           index);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_mnemonic(_("_Move to"));
    index->move_to_item = menuitem;
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), 
                          gtk_separator_menu_item_new());
    create_stock_menu_item(menu, GNOME_STOCK_BOOK_OPEN,
                           _("_View Source"),
                           GTK_SIGNAL_FUNC(bndx_view_source),
                           index);

    return menu;
}

/* bndx_do_popup: common code for the popup menu;
 * set sensitivity of menuitems on the popup
 * menu, and populate the mru submenu
 */
static void
bndx_do_popup(BalsaIndex * index, GdkEventButton * event)
{
    GtkWidget *menu = index->popup_menu;
    GtkWidget *submenu;
    LibBalsaMailbox* mailbox;
    GList *list, *l;
    gboolean any;
    gboolean any_deleted = FALSE;
    gboolean any_not_deleted = FALSE;
    gint event_button;
    guint event_time;
 
    BALSA_DEBUG();

    l = balsa_index_selected_list(index);
    for (list = l; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;

        if (LIBBALSA_MESSAGE_IS_DELETED(message))
            any_deleted = TRUE;
        else
            any_not_deleted = TRUE;
    }
    g_list_foreach(l, (GFunc)g_object_unref, NULL);
    g_list_free(l);
    any = (l != NULL);

    l = gtk_container_get_children(GTK_CONTAINER(menu));
    for (list = l; list; list = g_list_next(list))
        gtk_widget_set_sensitive(GTK_WIDGET(list->data), any);
    g_list_free(l);

    mailbox = index->mailbox_node->mailbox;
    gtk_widget_set_sensitive(index->delete_item,
                             any_not_deleted && !mailbox->readonly);
    gtk_widget_set_sensitive(index->undelete_item,
                             any_deleted && !mailbox->readonly);
    gtk_widget_set_sensitive(index->move_to_trash_item,
                             any && mailbox != balsa_app.trash
                             && !mailbox->readonly);
    gtk_widget_set_sensitive(index->toggle_item,
                             any && !mailbox->readonly);
    gtk_widget_set_sensitive(index->move_to_item,
                             any && !mailbox->readonly);

    submenu = balsa_mblist_mru_menu(GTK_WINDOW(index->window),
                                    &balsa_app.folder_mru,
                                    G_CALLBACK(mru_menu_cb),
                                    index);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(index->move_to_item),
                              submenu);

    gtk_widget_show_all(menu);

    if (event) {
        event_button = event->button;
        event_time = event->time;
    } else {
        event_button = 0;
        event_time = gtk_get_current_event_time();
    }
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   event_button, event_time);
}

static GtkWidget *
create_stock_menu_item(GtkWidget * menu, const gchar * type,
		       const gchar * label, GtkSignalFunc cb,
		       gpointer data)
{
    GtkWidget *menuitem = gtk_image_menu_item_new_with_mnemonic(label);
    GtkWidget *image = gtk_image_new_from_stock(type, GTK_ICON_SIZE_MENU);

    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem), image);

    g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(cb), data);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    return menuitem;
}

static void
sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data)
{
    balsa_window_enable_continue();
}

void
balsa_index_update_tree(BalsaIndex * index, gboolean expand)
/* Remarks: In the "collapse" case, we still expand current thread to the
	    extent where viewed message is visible. An alternative
	    approach would be to change preview, e.g. to top of thread. */
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeIter iter;

    if (expand) {
	g_signal_handler_block(index, index->row_expanded_id);
        gtk_tree_view_expand_all(tree_view);
	g_signal_handler_unblock(index, index->row_expanded_id);
    } else
        gtk_tree_view_collapse_all(tree_view);

    /* Re-expand msg_node's thread; cf. Remarks */
    /* expand_to_row is redundant in the expand_all case, but the
     * overhead is slight
     * select is needed in both cases, as a previous collapse could have
     * deselected the current message */
    if (bndx_find_message(index, NULL, &iter, index->current_message))
        bndx_expand_to_row_and_select(index, &iter);
}

/* balsa_index_set_threading_type: public method. */
void
balsa_index_set_threading_type(BalsaIndex * index, int thtype)
{
    LibBalsaMailbox *mailbox;
    GtkTreeSelection *selection;

    g_return_if_fail(index != NULL);
    g_return_if_fail(index->mailbox_node != NULL);
    mailbox = index->mailbox_node->mailbox;
    g_return_if_fail(mailbox != NULL);

    mailbox->view->threading_type = thtype;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
    g_signal_handler_block(selection, index->selection_changed_id);
    libbalsa_mailbox_set_threading(mailbox, thtype);
    balsa_index_update_tree(index, balsa_app.expand_tree);
    g_signal_handler_unblock(selection, index->selection_changed_id);
}

/* Find messages with the same ID, and remove all but one of them; if
 * any has the `replied' flag set, make sure the one we keep is one of
 * them.
 *
 * NOTE: assumes that index != NULL. */
void
balsa_index_remove_duplicates(BalsaIndex * index)
{
#if 1
    g_warning("balsa_index_remove_duplicates requires knowledge of "
              "msg-id of of all messages\n which may not be available."
              "This method should be backend-dependent");
#else
    LibBalsaMailbox *mailbox;
    GHashTable *table;
    GList *list;
    GList *messages = NULL;

    mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    table = g_hash_table_new(g_str_hash, g_str_equal);
    for (list = mailbox->message_list; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;
        LibBalsaMessage *master;

        if (LIBBALSA_MESSAGE_IS_DELETED(message) || !message->message_id)
            continue;

        master =g_hash_table_lookup(table, message->message_id);
        if (!master || LIBBALSA_MESSAGE_IS_REPLIED(message)) {
            g_hash_table_insert(table, message->message_id, message);
            message = master;
        }

        if (message) {
            GtkTreePath *path;

            messages = g_list_prepend(messages, message);
            if (!balsa_app.hide_deleted
                && bndx_find_message(index, &path, NULL, message)) {
                bndx_expand_to_row(index, path);
                gtk_tree_path_free(path);
            }
        }
    }

    if (messages) {
	if (mailbox != balsa_app.trash) {
	    libbalsa_messages_move(messages, balsa_app.trash);
	    enable_empty_trash(TRASH_FULL);
	} else {
	    libbalsa_messages_change_flag(messages, 
                                          LIBBALSA_MESSAGE_FLAG_DELETED,
                                          TRUE);
            enable_empty_trash(TRASH_CHECK);
	}
	g_list_free(messages);
    }

    g_hash_table_destroy(table);
#endif
}

/* Public method. */
void
balsa_index_refresh_size(BalsaIndex * index)
{
}


/* Public method. */
void
balsa_index_refresh_date(BalsaIndex * index)
{
}

/* Transfer messages. */
void
balsa_index_transfer(BalsaIndex *index, GList * messages,
                     LibBalsaMailbox * to_mailbox, gboolean copy)
{
    gboolean success;
    LibBalsaMailbox *from_mailbox;

    if (messages == NULL)
        return;

    success = copy ? libbalsa_messages_copy(messages, to_mailbox)
		   : libbalsa_messages_move(messages, to_mailbox);
    if (!success) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  messages->next 
			  ? _("Failed to copy messages to mailbox \"%s\".")
			  : _("Failed to copy message to mailbox \"%s\"."),
			  to_mailbox->name);
	return;
    }

    from_mailbox = index->mailbox_node->mailbox;
    balsa_mblist_set_status_bar(from_mailbox);

    if (from_mailbox == balsa_app.trash && !copy)
        enable_empty_trash(TRASH_CHECK);
    else if (to_mailbox == balsa_app.trash)
        enable_empty_trash(TRASH_FULL);
}

/* General helpers. */
static void
bndx_expand_to_row(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreePath *tmp = gtk_tree_path_copy(path);

    if (gtk_tree_path_up(tmp) && gtk_tree_path_get_depth(tmp) > 0
        && !gtk_tree_view_row_expanded(GTK_TREE_VIEW(index), tmp)) {
        bndx_expand_to_row(index, tmp);
        gtk_tree_view_expand_row(GTK_TREE_VIEW(index), tmp, FALSE);
    }

    gtk_tree_path_free(tmp);
}

static void
bndx_changed_find_row(BalsaIndex * index)
{
    GtkTreeIter iter;

    if (bndx_find_message(index, NULL, &iter, index->current_message)) {
	gpointer tmp = iter.user_data;
	index->next_message =
	    bndx_search_iter(index, index->search_iter, &iter,
			     BNDX_SEARCH_DIRECTION_NEXT,
			     BNDX_SEARCH_VIEWABLE_ONLY, 0);
	iter.user_data = tmp;
	index->prev_message =
	    bndx_search_iter(index, index->search_iter, &iter,
			     BNDX_SEARCH_DIRECTION_PREV,
			     BNDX_SEARCH_VIEWABLE_ONLY, 0);
    } else {
	index->next_message = FALSE;
	index->prev_message = FALSE;
    }

    g_signal_emit(G_OBJECT(index), balsa_index_signals[INDEX_CHANGED], 0);
}

/*
 * bndx_find_message
 *
 * index:       the BalsaIndex to be searched;
 * path:        location to store the GtkTreePath to the message (may be
 *              NULL);
 * iter:        location to store a GtkTreeIter for accessing the
 *              message (may be NULL);
 * message:     the LibBalsaMessage for which we're searching.
 *
 * Returns FALSE if the message is not found, in which case *path and
 * *iter are not changed.
 */
static gboolean
bndx_find_message(BalsaIndex * index, GtkTreePath ** path,
		  GtkTreeIter * iter, LibBalsaMessage * message)
{
    return message
	&& libbalsa_mailbox_msgno_find(index->mailbox_node->mailbox,
				       message->msgno, path, iter);
}

/* Make the actual selection,
 * making sure the selected row is within bounds and made visible.
 */
static void
bndx_select_row(BalsaIndex * index, GtkTreePath * path)
{
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(index), path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(index), path, NULL,
                                 FALSE, 0, 0);
}

/* Check that all parents are expanded. */
static gboolean
bndx_row_is_viewable(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreePath *tmp_path = gtk_tree_path_copy(path);
    gboolean ret_val = TRUE;

    while (ret_val && gtk_tree_path_up(tmp_path)
           && gtk_tree_path_get_depth(tmp_path) > 0)
        ret_val =
            gtk_tree_view_row_expanded(GTK_TREE_VIEW(index), tmp_path);

    gtk_tree_path_free(tmp_path);
    return ret_val;
}
