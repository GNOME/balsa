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

/* Sorting */
static gint date_compare(LibBalsaMessage * m1, LibBalsaMessage * m2);
static gint numeric_compare(LibBalsaMessage * m1, LibBalsaMessage * m2);
static gint size_compare(LibBalsaMessage * m1, LibBalsaMessage * m2);
/* Column numbers (used for sort_column_id): */
typedef enum {
    BNDX_TREE_COLUMN_NO = 1,
    BNDX_TREE_COLUMN_STATUS,    /* not used */
    BNDX_TREE_COLUMN_ATTACH,    /* not used */
    BNDX_TREE_COLUMN_SENDER,
    BNDX_TREE_COLUMN_SUBJECT,
    BNDX_TREE_COLUMN_DATE,
    BNDX_TREE_COLUMN_SIZE
} BndxTreeColumnId;

/* Setting style */
static void bndx_set_col_images(BalsaIndex * index, GtkTreeIter * iter,
                                LibBalsaMessage * message);
static void bndx_set_style(BalsaIndex * index, GtkTreePath * path,
                           GtkTreeIter * iter);
static void bndx_set_parent_style(BalsaIndex * index, GtkTreeIter * iter);

/* Manage the tree view */
static void bndx_check_visibility(BalsaIndex * index);
static void bndx_scroll_to_row(BalsaIndex * index, GtkTreePath * path);
static gboolean bndx_row_is_viewable(BalsaIndex * index,
                                     GtkTreePath * path);
static void bndx_expand_to_row_and_select(BalsaIndex * index,
                                          GtkTreeIter * iter,
                                          gboolean select);
static void bndx_find_row_and_select(BalsaIndex * index,
                                     LibBalsaMessageFlag flag,
                                     FilterOpType op, GSList * conditions,
                                     gboolean previous);
static gboolean bndx_find_row(BalsaIndex * index,
                              GtkTreeIter * pos, gboolean reverse_search,
                              LibBalsaMessageFlag flag,
                              FilterOpType op, GSList * conditions,
                              GList * exclude);
static gboolean bndx_find_row_func(LibBalsaMessage * message,
                                   LibBalsaMessageFlag flag,
                                   GSList * conditions, FilterOpType op,
                                   GList * exclude, gboolean viewable);
static gboolean bndx_find_next(GtkTreeView * tree_view, GtkTreePath * path,
                               GtkTreeIter * iter, gboolean wrap);
static gboolean bndx_find_prev(GtkTreeView * tree_view, GtkTreePath * path,
                               GtkTreeIter * iter);
static void bndx_select_message(BalsaIndex * index,
                                LibBalsaMessage * message);
static void bndx_changed_find_row(BalsaIndex * index);
static void bndx_add_message(BalsaIndex * index, LibBalsaMessage * message);
static void bndx_messages_remove(BalsaIndex * index, GList * messages);
static gboolean bndx_refresh_size_func(GtkTreeModel * model,
                                       GtkTreePath * path,
                                       GtkTreeIter * iter, gpointer data);
static gboolean bndx_refresh_date_func(GtkTreeModel * model,
                                       GtkTreePath * path,
                                       GtkTreeIter * iter, gpointer data);
static void bndx_hide_deleted(BalsaIndex * index, gboolean hide);

/* mailbox callbacks */
static void mailbox_messages_changed_status_cb(LibBalsaMailbox * mb,
					       GList * messages,
					       gint flag,
					       BalsaIndex * index);
static void mailbox_messages_added_cb  (BalsaIndex * index, GList * messages);
static void mailbox_messages_removed_cb(BalsaIndex * index, GList * messages);

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
static void bndx_tree_collapse_cb(GtkTreeView * tree_view,
                                  GtkTreeIter * iter, GtkTreePath * path,
                                  gpointer user_data);
static void bndx_column_click(GtkTreeViewColumn * column, gpointer data);
static gint bndx_row_compare(GtkTreeModel * model, GtkTreeIter * iter1,
                             GtkTreeIter * iter2, gpointer data);

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
static void bndx_moveto(BalsaIndex * index);

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
static void bndx_load_and_thread(BalsaIndex * index, int thtype);
static void bndx_set_tree_store(BalsaIndex * index);
static void bndx_set_sort_order(BalsaIndex * index,
				LibBalsaMailboxSortFields field,
				LibBalsaMailboxSortType order);
static void bndx_set_threading_type(BalsaIndex * index, int thtype);
static GNode *bndx_make_tree(BalsaIndex * index, GtkTreeIter * iter,
                             GtkTreePath * path);
static void bndx_copy_tree(BalsaIndex * index, GNode * node,
                           GtkTreeIter * parent_iter);

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
    LibBalsaMailbox* mailbox;

    g_return_if_fail(obj != NULL);
    index = BALSA_INDEX(obj);

    /*page->window references our owner */
    if (index->mailbox_node && (mailbox = index->mailbox_node->mailbox) ) {
        g_signal_handlers_disconnect_matched(G_OBJECT(mailbox),
                                             G_SIGNAL_MATCH_DATA,
                                             0, 0, NULL, NULL,
                                             index);
	libbalsa_mailbox_close(mailbox);
        g_object_unref(G_OBJECT(index->mailbox_node));
	index->mailbox_node = NULL;
    }

    if (index->popup_menu) {
        gtk_widget_destroy(index->popup_menu);
        index->popup_menu = NULL;
    }

    if (index->ref_table) {
        g_hash_table_destroy(index->ref_table);
        index->ref_table = NULL;
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
static void
bndx_instance_init(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    /* Index column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "#");
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", BNDX_INDEX_COLUMN,
                                        NULL);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BNDX_TREE_COLUMN_NO);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click), index);
    gtk_tree_view_append_column(tree_view, column);

    /* Status icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column =
        gtk_tree_view_column_new_with_attributes("S", renderer,
                                                 "pixbuf", BNDX_STATUS_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_append_column(tree_view, column);

    /* Attachment icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column =
        gtk_tree_view_column_new_with_attributes("A", renderer,
                                                 "pixbuf", BNDX_ATTACH_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_append_column(tree_view, column);
    
    /* From/To column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("From"), renderer,
                                                 "text", BNDX_FROM_COLUMN,
						 "weight",
						 BNDX_WEIGHT_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BNDX_TREE_COLUMN_SENDER);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click), index);
    gtk_tree_view_append_column(tree_view, column);

    /* Subject column--contains tree expanders */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("Subject"), renderer,
                                                 "text",
                                                 BNDX_SUBJECT_COLUMN,
                                                 "foreground-gdk",
                                                 BNDX_COLOR_COLUMN,
                                                 "weight",
                                                 BNDX_WEIGHT_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BNDX_TREE_COLUMN_SUBJECT);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click), index);
    gtk_tree_view_append_column(tree_view, column);
    gtk_tree_view_set_expander_column(tree_view, column);

    /* Date column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("Date"), renderer,
                                                 "text", BNDX_DATE_COLUMN,
						 "weight",BNDX_WEIGHT_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BNDX_TREE_COLUMN_DATE);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click), index);
    gtk_tree_view_append_column(tree_view, column);

    /* Size column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Size"));
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", BNDX_SIZE_COLUMN,
					"weight",BNDX_WEIGHT_COLUMN,
                                        NULL);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BNDX_TREE_COLUMN_SIZE);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click), index);
    gtk_tree_view_append_column(tree_view, column);

    /* Initialize some other members */
    index->mailbox_node = NULL;
    index->popup_menu = bndx_popup_menu_create(index);
    /* The ref table will be populated in the initial threading. */
    index->ref_table =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                              (GDestroyNotify)
                              gtk_tree_row_reference_free);
    
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

    /* catch thread expand/collapse events, to set the head node style
     * for unread messages */
    index->row_expanded_id =
        g_signal_connect_after(tree_view, "row-expanded",
                               G_CALLBACK(bndx_tree_expand_cb), NULL);
    index->row_collapsed_id =
        g_signal_connect_after(tree_view, "row-collapsed",
                               G_CALLBACK(bndx_tree_collapse_cb), NULL);

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

static void
bndx_column_click(GtkTreeViewColumn * column, gpointer data)
{
    LibBalsaMailboxView *view;
    GtkTreeView *tree_view = GTK_TREE_VIEW(data);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    gint col_id;
    GtkSortType gtk_sort;

    view = BALSA_INDEX(tree_view)->mailbox_node->mailbox->view;
    gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(model),
                                         &col_id, &gtk_sort);

    switch ((BndxTreeColumnId)col_id) {
    case BNDX_TREE_COLUMN_NO:
        view->sort_field = LB_MAILBOX_SORT_NO;
        break;
    case BNDX_TREE_COLUMN_SENDER:
        view->sort_field = LB_MAILBOX_SORT_SENDER;
        break;
    case BNDX_TREE_COLUMN_SUBJECT:
        view->sort_field = LB_MAILBOX_SORT_SUBJECT;
        break;
    case BNDX_TREE_COLUMN_DATE:
        view->sort_field = LB_MAILBOX_SORT_DATE;
        break;
    case BNDX_TREE_COLUMN_SIZE:
        view->sort_field = LB_MAILBOX_SORT_SIZE;
        break;
    default:
        view->sort_field = LB_MAILBOX_SORT_NATURAL;
        break;
    }

    view->sort_type =
        (gtk_sort == GTK_SORT_DESCENDING) ? LB_MAILBOX_SORT_TYPE_DESC
                                          : LB_MAILBOX_SORT_TYPE_ASC;

    bndx_changed_find_row(BALSA_INDEX(tree_view));
    bndx_check_visibility(BALSA_INDEX(tree_view));
}

/* Helper for bndx_column_click */
static void
bndx_check_visibility(BalsaIndex * index)
{
    GtkTreePath *path;

    if (bndx_find_message(index, &path, NULL, index->current_message)) {
        bndx_scroll_to_row(index, path);
        gtk_tree_path_free(path);
    }
}

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
    LibBalsaMessage *current_message;
    gboolean current_message_selected;
};

static void
bndx_selection_changed(GtkTreeSelection * selection, gpointer data)
{
    BalsaIndex *index = BALSA_INDEX(data);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    struct BndxSelectionChangedInfo sci;
    GtkTreeIter iter;

    sci.message = NULL;
    sci.current_message = index->current_message;
    sci.current_message_selected = FALSE;
    gtk_tree_selection_selected_foreach(selection,
                                        bndx_selection_changed_func,
                                        &sci);
    if (sci.current_message_selected)
        return;

    /* we don't clear the current message if the tree contains any
     * messages */
    if (sci.message || !gtk_tree_model_get_iter_first(model, &iter)) {
        index->current_message = sci.message;
        bndx_changed_find_row(index);
    }
}

static void
bndx_selection_changed_func(GtkTreeModel * model, GtkTreePath * path,
                            GtkTreeIter * iter, gpointer data)
{
    struct BndxSelectionChangedInfo *sci = data;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &sci->message,
                       -1);
    if (sci->message == sci->current_message)
        sci->current_message_selected = TRUE;
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
    gtk_tree_model_get(model, &iter, BNDX_MESSAGE_COLUMN, &message, -1);

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
    GtkTreePath *current_path = NULL;
    GtkTreeIter child_iter;
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);

    /* if current message has become viewable, reselect it */
    if (bndx_find_message(index, &current_path, NULL,
                          index->current_message)) {
        if (!gtk_tree_selection_path_is_selected(selection, current_path)
            && bndx_row_is_viewable(index, current_path)) {
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(index), current_path,
                                     NULL, FALSE);
            bndx_scroll_to_row(index, current_path);
        }
        gtk_tree_path_free(current_path);
    }

    /* Reset the style of this row... */
    bndx_set_style(index, path, iter);
    /* ...and check the styles of its newly viewable children. */
    if (gtk_tree_model_iter_children(model, &child_iter, iter)) {
        gtk_tree_path_down(path);
        do {
            bndx_set_style(index, path, &child_iter);
            gtk_tree_path_next(path);
        } while (gtk_tree_model_iter_next(model, &child_iter));
        gtk_tree_path_up(path);
    }
    bndx_changed_find_row(index);
}

/* bndx_tree_collapse_cb:
 * callback on collapse events
 * set/reset unread style, as appropriate */
static void
bndx_tree_collapse_cb(GtkTreeView * tree_view, GtkTreeIter * iter,
                      GtkTreePath * path, gpointer user_data)
{
    BalsaIndex *index = BALSA_INDEX(tree_view);

    bndx_set_style(index, path, iter);
    bndx_changed_find_row(index);
}

/* When a column is resized, store the new size for later use */
static void
bndx_column_resize(GtkWidget * widget, GtkAllocation * allocation,
                   gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);

    balsa_app.index_num_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 0));
    balsa_app.index_status_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 1));
    balsa_app.index_attachment_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 2));
    balsa_app.index_from_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 3));
    balsa_app.index_subject_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 4));
    balsa_app.index_date_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 5));
    balsa_app.index_size_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 6));
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
    g_list_free(l);
    
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

/* Public methods */
GtkWidget *
balsa_index_new(void)
{
    BalsaIndex* index = g_object_new(BALSA_TYPE_INDEX, NULL);

    return GTK_WIDGET(index);
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
balsa_index_load_mailbox_node (BalsaIndex * index, BalsaMailboxNode* mbnode)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    LibBalsaMailbox* mailbox;
    gchar *msg;
    gboolean successp;

    g_return_val_if_fail (index != NULL, TRUE);
    g_return_val_if_fail (mbnode != NULL, TRUE);
    g_return_val_if_fail (mbnode->mailbox != NULL, TRUE);

    mailbox = mbnode->mailbox;
    msg = g_strdup_printf(_("Opening mailbox %s. Please wait..."),
			  mbnode->mailbox->name);
    gnome_appbar_push(balsa_app.appbar, msg);
    g_free(msg);
    gdk_threads_leave();
    successp = libbalsa_mailbox_open(mailbox);
    gdk_threads_enter();
    gnome_appbar_pop(balsa_app.appbar);

    if (!successp)
	return TRUE;

    /*
     * release the old mailbox
     */
    if (index->mailbox_node && index->mailbox_node->mailbox) {
        mailbox = index->mailbox_node->mailbox;

	/* This will disconnect all of our signals */
        g_signal_handlers_disconnect_matched(G_OBJECT(mailbox),
                                             G_SIGNAL_MATCH_DATA,
                                             0, 0, NULL, NULL,
                                             index);
	libbalsa_mailbox_close(mailbox);
	gtk_tree_store_clear(GTK_TREE_STORE(model));
        g_hash_table_foreach_remove(index->ref_table, (GHRFunc) gtk_true,
                                    NULL);
    }

    /*
     * set the new mailbox
     */
    index->mailbox_node = mbnode;
    g_object_ref(G_OBJECT(mbnode));
    /*
     * rename "from" column to "to" for outgoing mail
     */
    if (mailbox->view->show == LB_MAILBOX_SHOW_TO) {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(tree_view, 3);

        gtk_tree_view_column_set_title(column, _("To"));
    }

    g_signal_connect(G_OBJECT(mailbox), "messages-status-changed",
		     G_CALLBACK(mailbox_messages_changed_status_cb),
		     (gpointer) index);
    g_signal_connect_swapped(G_OBJECT(mailbox), "messages-added",
			     G_CALLBACK(mailbox_messages_added_cb),
			     (gpointer) index);
    g_signal_connect_swapped(G_OBJECT(mailbox), "messages-removed",
			     G_CALLBACK(mailbox_messages_removed_cb),
			     (gpointer) index);

    /* Set the tree store, load messages, and do threading. The ref
     * table will be populated during this threading. */
    bndx_load_and_thread(index, mailbox->view->threading_type);

    bndx_moveto(index);

    return FALSE;
}

/* Helper for balsa_index_load_mailbox_node.
 * Description: moves to the first unread message in the index, or the
 * last message if none is unread, and selects it.
 */
static void
bndx_moveto(BalsaIndex * index)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));

    if (!gtk_tree_model_get_iter_first(model, &iter))
        return;

    if (!bndx_find_row(index, &iter, FALSE, LIBBALSA_MESSAGE_FLAG_NEW,
                       FILTER_NOOP, NULL, NULL)) {
        GtkTreeIter tmp_iter = iter;
	while (gtk_tree_model_iter_next(model, &tmp_iter))
            iter = tmp_iter;
    }
    bndx_expand_to_row_and_select(index, &iter,
                                  balsa_app.view_message_on_open);
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

void
balsa_index_select_next(BalsaIndex * index)
{
    bndx_find_row_and_select(index, (LibBalsaMessageFlag) 0,
                             FILTER_NOOP, NULL, FALSE);
}

void
balsa_index_select_previous(BalsaIndex * index)
{
    bndx_find_row_and_select(index, (LibBalsaMessageFlag) 0,
                             FILTER_NOOP, NULL, TRUE);
}

void
balsa_index_select_next_unread(BalsaIndex * index)
{
    bndx_find_row_and_select(index, LIBBALSA_MESSAGE_FLAG_NEW,
                             FILTER_NOOP, NULL, FALSE);
}

void
balsa_index_select_next_flagged(BalsaIndex * index)
{
    bndx_find_row_and_select(index, LIBBALSA_MESSAGE_FLAG_FLAGGED,
                             FILTER_NOOP, NULL, FALSE);
}

void
balsa_index_find(BalsaIndex * index, FilterOpType op, GSList * conditions,
                 gboolean previous)
{
    bndx_find_row_and_select(index, (LibBalsaMessageFlag) 0,
                             op, conditions, previous);
}

/* Helpers for the message selection methods. */
static void
bndx_find_row_and_select(BalsaIndex * index,
                         LibBalsaMessageFlag flag,
                         FilterOpType op,
                         GSList * conditions,
                         gboolean previous)
{
    GtkTreeIter pos;

    if (bndx_find_row(index, &pos, previous, flag, op, conditions, NULL))
        bndx_expand_to_row_and_select(index, &pos, TRUE);
}

/* bndx_find_row:
 * common search code--look for next or previous, with or without flag
 *
 * Returns TRUE if the search succeeds.
 * On success, *pos points to the new message.
 * On failure, *pos is unchanged.
 */
static gboolean
bndx_find_row(BalsaIndex * index, GtkTreeIter * pos,
              gboolean reverse_search, LibBalsaMessageFlag flag,
              FilterOpType op, GSList * conditions, GList * exclude)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreePath *path;
    GtkTreeIter iter;
    gboolean first_time = TRUE;
    gboolean found;
    gboolean wrap = (flag != (LibBalsaMessageFlag) 0);

    g_return_val_if_fail(index != NULL, FALSE);

    if (!bndx_find_message(index, &path, &iter, index->current_message)) {
        /* The current_message is NULL (or we otherwise can't find
         * it); if we're looking for a plain next or prev, just return
         * FALSE. */
        if (!(flag || conditions || exclude))
            return FALSE;

        wrap = FALSE;
        /* Search from the first message, unless... */
        if (!gtk_tree_model_get_iter_first(model, &iter))
            /* ...index is empty, or... */
            return FALSE;
        if (reverse_search) {
            /* ...search from the last message. */
            GtkTreeIter tmp = iter;

	    for (;;)
		if (gtk_tree_model_iter_next(model, &tmp))
		    iter = tmp;
                else if (gtk_tree_model_iter_children(model, &tmp, &iter))
		    iter = tmp;
		else break;
        }
        path = gtk_tree_model_get_path(model, &iter);
    }

    found = FALSE;
    do {
        LibBalsaMessage *message;

        gtk_tree_model_get(model, &iter, BNDX_MESSAGE_COLUMN, &message, -1);
        /* Check if we have wrapped and reached the current message */
        if (message == index->current_message) {
            if (first_time)
                first_time = FALSE;
            else
                break;
        } else
            found = bndx_find_row_func(message, flag, conditions, op, exclude,
                                       bndx_row_is_viewable(index, path));
    } while (!found &&
             (reverse_search ? bndx_find_prev(tree_view, path, &iter)
              : bndx_find_next(tree_view, path, &iter, wrap)));

    gtk_tree_path_free(path);

    if (found) {
        if (pos)
            *pos = iter;
    } else if (!reverse_search && exclude)
        /* for next message when deleting or moving, fall back to previous */
        return bndx_find_row(index, pos, TRUE, flag, op, conditions,
                             exclude);

    return found;
}

static gboolean
bndx_find_row_func(LibBalsaMessage * message,
                   LibBalsaMessageFlag flag,
                   GSList * conditions,
                   FilterOpType op,
                   GList * exclude,
                   gboolean viewable)
{
    if (LIBBALSA_MESSAGE_IS_DELETED(message))
        return FALSE;

    if (flag) {
        /* looking for flagged messages */
        if (!LIBBALSA_MESSAGE_HAS_FLAG(message, flag))
            return FALSE;
    } else if (conditions) {
	if (!libbalsa_mailbox_message_match(message->mailbox, message,
					    op, conditions))
	    return FALSE;
   } else if (exclude) {
        /* looking for messages not in the excluded list */
        if (g_list_find(exclude, message))
            return FALSE;
    } else {
        /* if there are no flags, no conditions, and no excluded list,
         * we want any viewable message */
        if (!viewable)
            return FALSE;
    }

    return TRUE;
}

/* Two helpers : pass to next (previous) message.
   Return : FALSE if search is done (this depends
   on the search type : certain wrap around,
   others not)

   On success, path and iter both point to the new message row.
   On failure:
       bndx_find_next leaves them pointing to the head of the last thread;
       bndx_find_prev leaves them unchanged, pointing to the first message.
*/
static gboolean
bndx_find_next(GtkTreeView * tree_view, GtkTreePath * path,
               GtkTreeIter * iter, gboolean wrap)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter tmp;

    /* First go deeper (children) */
    if (gtk_tree_model_iter_children(model, &tmp, iter)) {
	gtk_tree_path_down(path);
	*iter = tmp;
	return TRUE;
    }
    /* If no children, check sibling */
    else if (tmp = *iter, gtk_tree_model_iter_next(model, &tmp)) {
	gtk_tree_path_next(path);
        *iter = tmp;
	return TRUE;
    }
    /* if no children, no sibling, go back up */
    else while (gtk_tree_model_iter_parent(model, &tmp, iter)) {
	gtk_tree_path_up(path);
	*iter = tmp;
	/* look for an "uncle" ie the next node after the parent */
	if (gtk_tree_model_iter_next(model, &tmp)) {
	    gtk_tree_path_next(path);
	    *iter = tmp;
	    return TRUE;
	}
    }
    /* No more next, so wrap if we are doing a flag search */
    if (wrap) {
        while (gtk_tree_path_up(path))
            /* Nothing. */;
        gtk_tree_path_down(path);
	gtk_tree_model_get_iter_first(model, iter);
	return TRUE;
    }
    return FALSE;
}

static gboolean
bndx_find_prev(GtkTreeView * tree_view, GtkTreePath * path,
               GtkTreeIter * iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter tmp;

    /* First check sibling */
    if (gtk_tree_path_prev(path)) {
	gtk_tree_model_get_iter(model, iter, path);
	/* OK There is a previous subtree, so go to the last leaf
	   of this subtree */
	if (gtk_tree_model_iter_children(model, &tmp, iter)) {
	    gtk_tree_path_down(path);
	    *iter = tmp;
	    while (TRUE)
		if (gtk_tree_model_iter_next(model, &tmp)) {
		    gtk_tree_path_next(path);
		    *iter = tmp;
                } else if (gtk_tree_model_iter_children(model, &tmp, iter)) {
		    gtk_tree_path_down(path);
		    *iter = tmp;
		}
		else break;
	}
	return TRUE;
    }
    /* If no prev sibling, the previous is the parent (if it exists) */
    if (gtk_tree_model_iter_parent(model, &tmp, iter)) {
	gtk_tree_path_up(path);
	*iter = tmp;
	return TRUE;
    }
    return FALSE;
}

/* bndx_expand_to_row_and_select:
 * make sure it's viewable, then pass it to bndx_select_row
 * no-op if it's NULL
 *
 * Note: iter must be valid; it isn't checked here.
 */
static void
bndx_expand_to_row_and_select(BalsaIndex * index, GtkTreeIter * iter,
                              gboolean select)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    GtkTreePath *path;

    path = gtk_tree_model_get_path(model, iter);
    bndx_expand_to_row(index, path);
    if (select)
        bndx_select_row(index, path);
    else
        bndx_scroll_to_row(index, path);
    gtk_tree_path_free(path);
}

/* End of select message interfaces. */

static void
balsa_index_update_flag(BalsaIndex * index, LibBalsaMessage * message)
{
    GtkTreeIter iter;

    g_return_if_fail(index != NULL);
    g_return_if_fail(message != NULL);

    if (bndx_find_message(index, NULL, &iter, message))
	bndx_set_col_images(index, &iter, message);
}

static void
bndx_set_col_images(BalsaIndex * index, GtkTreeIter * iter,
		    LibBalsaMessage * message)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    GdkPixbuf *status_pixbuf = NULL;
    GdkPixbuf *attach_pixbuf = NULL;
    guint tmp;
    /* only ony status icon is shown; they are ordered from most important. */
    const static struct {
        int mask;
        const gchar *icon_name;
    } flags[] = {
        { LIBBALSA_MESSAGE_FLAG_DELETED, BALSA_PIXMAP_TRASH   },
        { LIBBALSA_MESSAGE_FLAG_FLAGGED, BALSA_PIXMAP_INFO_FLAGGED },
        { LIBBALSA_MESSAGE_FLAG_REPLIED, BALSA_PIXMAP_INFO_REPLIED }};

    for (tmp = 0; tmp < ELEMENTS(flags)
         && !LIBBALSA_MESSAGE_HAS_FLAG(message, flags[tmp].mask);
         tmp++);

    if (tmp < ELEMENTS(flags))
        status_pixbuf =
            gtk_widget_render_icon(GTK_WIDGET(index->window),
                                   flags[tmp].icon_name,
                                   GTK_ICON_SIZE_MENU, NULL);
    /* Alternatively, we could show an READ icon:
     * gtk_ctree_node_set_pixmap(ctree, node, 1,
     * balsa_icon_get_pixmap(BALSA_PIXMAP_INFO_READ),
     * balsa_icon_get_bitmap(BALSA_PIXMAP_INFO_READ)); 
     */

#ifdef HAVE_GPGME
    if (message->prot_state != LIBBALSA_MSG_PROTECT_NONE) {
	switch (message->prot_state)
	    {
	    case LIBBALSA_MSG_PROTECT_CRYPT:
		attach_pixbuf =
		    gtk_widget_render_icon(GTK_WIDGET(index->window),
					   BALSA_PIXMAP_INFO_ENCR,
					   GTK_ICON_SIZE_MENU, NULL);
		break;
	    case LIBBALSA_MSG_PROTECT_SIGN_UNKNOWN:
		attach_pixbuf =
		    gtk_widget_render_icon(GTK_WIDGET(index->window),
					   BALSA_PIXMAP_INFO_SIGN,
					   GTK_ICON_SIZE_MENU, NULL);
		break;
	    case LIBBALSA_MSG_PROTECT_SIGN_GOOD:
		attach_pixbuf =
		    gtk_widget_render_icon(GTK_WIDGET(index->window),
					   BALSA_PIXMAP_INFO_SIGN_GOOD,
					   GTK_ICON_SIZE_MENU, NULL);
		break;
	    case LIBBALSA_MSG_PROTECT_SIGN_NOTRUST:
		attach_pixbuf =
		    gtk_widget_render_icon(GTK_WIDGET(index->window),
					   BALSA_PIXMAP_INFO_SIGN_NOTRUST,
					   GTK_ICON_SIZE_MENU, NULL);
		break;
	    case LIBBALSA_MSG_PROTECT_SIGN_BAD:
		attach_pixbuf =
		    gtk_widget_render_icon(GTK_WIDGET(index->window),
					   BALSA_PIXMAP_INFO_SIGN_BAD,
					   GTK_ICON_SIZE_MENU, NULL);
		break;
	    default:
		g_warning("%s:%s:%d: message->prot_state == %d", __FILE__,
			  __FUNCTION__, __LINE__, message->prot_state);
	    }
    } else if (libbalsa_message_is_pgp_signed(message))
	attach_pixbuf =
	    gtk_widget_render_icon(GTK_WIDGET(index->window),
				   BALSA_PIXMAP_INFO_SIGN,
				   GTK_ICON_SIZE_MENU, NULL);
    else if (libbalsa_message_is_pgp_encrypted(message))
        attach_pixbuf =
            gtk_widget_render_icon(GTK_WIDGET(index->window),
                                   BALSA_PIXMAP_INFO_ENCR,
                                   GTK_ICON_SIZE_MENU, NULL);
    else
#endif
    if (libbalsa_message_has_attachment(message))
        attach_pixbuf =
            gtk_widget_render_icon(GTK_WIDGET(index->window),
                                   BALSA_PIXMAP_INFO_ATTACHMENT,
                                   GTK_ICON_SIZE_MENU, NULL);

    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                       BNDX_STATUS_COLUMN, status_pixbuf,
                       BNDX_ATTACH_COLUMN, attach_pixbuf,
		       BNDX_WEIGHT_COLUMN,
		       LIBBALSA_MESSAGE_HAS_FLAG(message,
						 LIBBALSA_MESSAGE_FLAG_NEW) ?
		       PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                       -1);
	
}

static gboolean
thread_has_unread(BalsaIndex * index, GtkTreeIter * iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    GtkTreeIter child_iter;

    if (!gtk_tree_model_iter_children(model, &child_iter, iter))
        return FALSE;

    do {
        LibBalsaMessage *message;

        gtk_tree_model_get(model, &child_iter,
                           BNDX_MESSAGE_COLUMN, &message, -1);

        if (LIBBALSA_MESSAGE_IS_UNREAD(message) ||
            thread_has_unread(index, &child_iter))
            return TRUE;
    } while (gtk_tree_model_iter_next(model, &child_iter));

    return FALSE;
}

/* Helper for bndx_set_style; also a gtk_tree_model_foreach callback. */
static gboolean
bndx_set_style_func(GtkTreeModel * model, GtkTreePath * path,
                    GtkTreeIter * iter, BalsaIndex * index)
{
    GtkTreeStore *store = GTK_TREE_STORE(model);
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);

    /* FIXME: Improve style handling;
              - Consider storing styles locally, with or config setting
	        separate from the "mailbox" one.  */
    
    if (!gtk_tree_view_row_expanded(tree_view, path)
        && thread_has_unread(index, iter)) {
        gtk_tree_store_set(store, iter,
                           BNDX_COLOR_COLUMN, &balsa_app.mblist_unread_color,
                           -1);
    } else
        gtk_tree_store_set(store, iter,
                           BNDX_COLOR_COLUMN, NULL,
                           -1);

    return FALSE;
}

static void
bndx_set_style(BalsaIndex * index, GtkTreePath * path, GtkTreeIter * iter)
{
    bndx_set_style_func(gtk_tree_view_get_model(GTK_TREE_VIEW(index)),
                        path, iter, index);
}

static void
bndx_set_parent_style(BalsaIndex * index, GtkTreeIter * iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    GtkTreeIter parent_iter;
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    gboolean first_parent = TRUE;

    while (gtk_tree_model_iter_parent(model, &parent_iter, iter)) {
        gtk_tree_path_up(path);
	if (first_parent) {
	    if (balsa_app.expand_tree)
		gtk_tree_view_expand_row(GTK_TREE_VIEW(index), path, FALSE);
	    first_parent = FALSE;
	}
	bndx_set_style(index, path, &parent_iter);
        *iter = parent_iter;
    }
    gtk_tree_path_free(path);
}

void
balsa_index_set_column_widths(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);

    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, 3),
                                         balsa_app.index_from_width);
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, 4),
                                         balsa_app.index_subject_width);
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, 5),
                                         balsa_app.index_date_width);
}

/* Mailbox Callbacks... */
/* mailbox_messages_changed_status:
   We must be *extremely* careful here - message might have changed 
   its status because the mailbox was forcibly closed and message
   became invalid. See for example #70807.
   Some of the messages might have been expunged (mailbox==null).
*/
static void
mailbox_messages_changed_status(LibBalsaMailbox * mb,
				GList * messages,
				gint flag,
				BalsaIndex * bindex)
{
    GList *list;

    if (!libbalsa_mailbox_is_valid(mb)) return;

    if (flag == LIBBALSA_MESSAGE_FLAG_DELETED &&
        LIBBALSA_MESSAGE_IS_DELETED(messages->data) &&
	(balsa_app.hide_deleted || balsa_app.delete_immediately)) {
	/* These messages are flagged as deleted, but we must remove them from
	   the index because of the prefs
	 */
	bndx_messages_remove(bindex,messages);
        return;
    }

    for (list = messages; list; list = g_list_next(list)) {
        LibBalsaMessage *msg = LIBBALSA_MESSAGE(list->data);
        if(msg->mailbox)
            balsa_index_update_flag(bindex, msg);
    }

    if (flag == LIBBALSA_MESSAGE_FLAG_DELETED
        && bindex->current_message
        && LIBBALSA_MESSAGE_IS_DELETED(bindex->current_message)) {
        GtkTreeIter iter;

        if (bndx_find_row(bindex, &iter, FALSE, (LibBalsaMessageFlag) 0,
                          FILTER_NOOP, NULL, messages))
            bndx_expand_to_row_and_select(bindex, &iter, TRUE);
        return;
    }

    bndx_changed_find_row(bindex);
    g_get_current_time (&bindex->last_use);
}

/* mailbox_messages_changed_status_cb: 
 * it can be called from a * thread. Assure we do the actual work in
 * the main thread.
 */

struct msg_changed_data {
    LibBalsaMailbox * mb;
    GList * messages;
    gint flag;
    BalsaIndex * bindex;
};

static gboolean
mailbox_messages_changed_status_idle(struct msg_changed_data* arg)
{
    gdk_threads_enter();
    if (arg->bindex) {
        g_object_remove_weak_pointer(G_OBJECT(arg->bindex),
                                     (gpointer) &arg->bindex);
        mailbox_messages_changed_status(arg->mb, arg->messages, arg->flag,
				        arg->bindex);
    }
    g_list_foreach(arg->messages, (GFunc)g_object_unref, NULL);
    gdk_threads_leave();
    g_list_free(arg->messages);
    g_free(arg);
    return FALSE;
}
    
static void
mailbox_messages_changed_status_cb(LibBalsaMailbox * mb,
				   GList * messages,
				   gint flag,
				   BalsaIndex * bindex)
{
    struct msg_changed_data *arg = g_new(struct msg_changed_data,1);
    arg->mb = mb;       arg->messages = g_list_copy(messages);
    arg->flag = flag;   arg->bindex = bindex;
    g_object_add_weak_pointer(G_OBJECT(bindex), (gpointer) &arg->bindex);
    g_list_foreach(arg->messages, (GFunc)g_object_ref, NULL);
    g_idle_add((GSourceFunc)mailbox_messages_changed_status_idle, arg);
}

/* mailbox_messages_added_cb : callback for sync with backend; the signal
   is emitted by the mailbox when new messages has been retrieved (either
   after opening the mailbox, or after "check new messages").
*/
/* Helper of the callback (also used directly) */
static void
bndx_messages_add(BalsaIndex * bindex, GList *messages)
{
    GList *list;

    for (list = messages; list; list = g_list_next(list)) {
        LibBalsaMessage *msg = (LibBalsaMessage *) list->data;
	if (msg->mailbox)
	    bndx_add_message(bindex, msg);
    }
    balsa_index_threading(bindex, 
			  bindex->mailbox_node->mailbox->view->threading_type);
    for (list = messages; list; list = g_list_next(list)) {
        LibBalsaMessage *msg = (LibBalsaMessage *) list->data;
        GtkTreeIter iter;
        if(!msg->mailbox) continue;
        if (bndx_find_message(bindex, NULL, &iter, msg))
            bndx_set_parent_style(bindex, &iter);
    }
    bndx_select_message(bindex, bindex->current_message);

    balsa_mblist_update_mailbox(balsa_app.mblist_tree_store, 
				bindex->mailbox_node->mailbox);
    bndx_changed_find_row(bindex);
    g_get_current_time (&bindex->last_use);
}


/* mailbox_messages_added_cb:
   may be called from a thread. Use idle callback to update the view.
   We must ref messages so they do not get destroyed between
   filing the callback and actually calling it.
*/
struct index_list_pair {
    void (*func)(BalsaIndex*, GList*);
    BalsaIndex * bindex;
    GList* messages;
};

static gboolean
mailbox_messages_func_idle(struct index_list_pair* arg)
{
    gdk_threads_enter();
    if(arg->bindex) {
        g_object_remove_weak_pointer(G_OBJECT(arg->bindex),
                                     (gpointer) &arg->bindex);
	(arg->func)(arg->bindex, arg->messages);
    }
    g_list_foreach(arg->messages, (GFunc)g_object_unref, NULL);
    gdk_threads_leave();
    g_list_free(arg->messages);
    g_free(arg);
    return FALSE;
}
    
static void
mailbox_messages_added_cb(BalsaIndex * bindex, GList *messages)
{
    struct index_list_pair *arg = g_new(struct index_list_pair,1);
    arg->func = bndx_messages_add; 
    arg->bindex  = bindex; arg->messages = g_list_copy(messages);
    g_object_add_weak_pointer(G_OBJECT(bindex), (gpointer) &arg->bindex);
    g_list_foreach(arg->messages, (GFunc)g_object_ref, NULL);
    g_idle_add((GSourceFunc)mailbox_messages_func_idle, arg);
}

/* mailbox_messages_remove_cb : callback to sync with backend; the signal is
   emitted by the mailbox to tell the frontend that it has removed mails
   (this is in general when the mailbox is committed because it then removes
   all mails flagged as deleted; the other case is when prefs about how to handle
   deletions are changed : "hide deleted messages/delete immediately")
 */
static void
mailbox_messages_removed_cb(BalsaIndex * bindex, GList * messages)
{
    struct index_list_pair *arg = g_new(struct index_list_pair,1);
    arg->func = bndx_messages_remove; 
    arg->bindex  = bindex; arg->messages = g_list_copy(messages);
    g_object_add_weak_pointer(G_OBJECT(bindex), (gpointer) &arg->bindex);
    g_list_foreach(arg->messages, (GFunc)g_object_ref, NULL);
    g_idle_add((GSourceFunc)mailbox_messages_func_idle, arg);
}

static void
bndx_view_source_func(GtkTreeModel *model, GtkTreePath *path,
                      GtkTreeIter *iter, gpointer data)
{
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    libbalsa_show_message_source(message, balsa_app.message_font,
                                 &balsa_app.source_escape_specials);
}

static void
bndx_view_source(GtkWidget * widget, gpointer data)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
    
    gtk_tree_selection_selected_foreach(selection, bndx_view_source_func,
                                        NULL);
}

static void
bndx_store_address(GtkWidget * widget, gpointer data)
{
    GList *messages = balsa_index_selected_list(BALSA_INDEX(data));

    balsa_store_address(messages);
    g_list_free(messages);
}

static void
balsa_index_selected_list_func(GtkTreeModel * model, GtkTreePath * path,
                        GtkTreeIter * iter, gpointer data)
{
    GList **list = data;
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    *list = g_list_prepend(*list, message);
}

/*
 * balsa_index_selected_list: create a GList of selected messages
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

        if (LIBBALSA_MESSAGE_IS_DELETED(message))
            messages = g_list_delete_link(messages, list);

        list = next;
    }

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

/*
 * Public message delete methods
 */
void
balsa_message_move_to_trash(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);
    bndx_do_delete(BALSA_INDEX(user_data), TRUE);
}

void
balsa_message_delete(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);
    bndx_do_delete(BALSA_INDEX(user_data), FALSE);
}

void
balsa_message_undelete(GtkWidget * widget, gpointer user_data)
{
    GList *l;
     BalsaIndex* index;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);

    l = balsa_index_selected_list(index);
    libbalsa_messages_delete(l, FALSE);
    g_list_free(l);
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
static void
balsa_message_toggle_flag(BalsaIndex* index, LibBalsaMessageFlag flag,
                          void(*cb)(GList*, gboolean))
{
    GList *list, *l;
    int is_all_flagged = TRUE;
    gboolean new_flag;

    /* First see if we should set given flag or unset */
    l = balsa_index_selected_list(index);
    for (list = l; list; list = g_list_next(list)) {
	if (!LIBBALSA_MESSAGE_HAS_FLAG(list->data, flag)) {
	    is_all_flagged = FALSE;
	    break;
	}
    }

    /* If they all have the flag set, then unset them. Otherwise, set
     * them all.
     * Note: the callback for `toggle unread' changes the `read' flag,
     * but the callback for `toggle flagged' changes `flagged'
     */

    new_flag =
        (flag ==
         LIBBALSA_MESSAGE_FLAG_NEW ? is_all_flagged : !is_all_flagged);

    (*cb) (l, new_flag);
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

static void
mru_menu_cb(gchar * url, BalsaIndex * index)
{
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);

    g_return_if_fail(mailbox != NULL);

    if (index->mailbox_node->mailbox != mailbox) {
        GList *messages = balsa_index_selected_list(index);
        balsa_index_transfer(index, messages, mailbox, FALSE);
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
                               GTK_SIGNAL_FUNC(balsa_message_delete),
                               index);
    index->undelete_item =
        create_stock_menu_item(menu, GTK_STOCK_UNDELETE,
                               _("_Undelete"),
                               GTK_SIGNAL_FUNC(balsa_message_undelete),
                               index);
    index->move_to_trash_item =
        create_stock_menu_item(menu, GNOME_STOCK_TRASH,
                               _("Move To _Trash"),
                               GTK_SIGNAL_FUNC
                               (balsa_message_move_to_trash), index);

    menuitem = gtk_menu_item_new_with_mnemonic(_("T_oggle"));
    submenu = gtk_menu_new();
    create_stock_menu_item(submenu, BALSA_PIXMAP_MENU_FLAGGED,
                           _("_Flagged"),
                           GTK_SIGNAL_FUNC(balsa_message_toggle_flagged),
                           index);
    create_stock_menu_item(submenu, BALSA_PIXMAP_MENU_NEW, _("_Unread"),
                           GTK_SIGNAL_FUNC(balsa_message_toggle_new),
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
    gulong handler =
        expand ? index->row_expanded_id : index->row_collapsed_id;

    g_signal_handler_block(index, handler);

    if (expand)
        gtk_tree_view_expand_all(tree_view);
    else
        gtk_tree_view_collapse_all(tree_view);

    gtk_tree_model_foreach(gtk_tree_view_get_model(tree_view),
                           (GtkTreeModelForeachFunc) bndx_set_style_func,
                           index);

    /* Re-expand msg_node's thread; cf. Remarks */
    /* expand_to_row is redundant in the expand_all case, but the
     * overhead is slight
     * select is needed in both cases, as a previous collapse could have
     * deselected the current message */
    bndx_select_message(index, index->current_message);

    g_signal_handler_unblock(index, handler);
}

/* balsa_index_set_threading_type: public method. */
void
balsa_index_set_threading_type(BalsaIndex * index, int thtype)
{
    LibBalsaMailbox *mailbox;

    g_return_if_fail(index != NULL);
    g_return_if_fail(index->mailbox_node != NULL);
    mailbox = index->mailbox_node->mailbox;
    g_return_if_fail(mailbox != NULL);

    if (thtype == LB_MAILBOX_THREADING_FLAT
        && mailbox->view->threading_type != LB_MAILBOX_THREADING_FLAT)
        /* Changing to flat: it's faster to reload from scratch. */
        bndx_load_and_thread(index, thtype);
    else
        bndx_set_threading_type(index, thtype);
}

/* Find messages with the same ID, and remove all but one of them; if
 * any has the `replied' flag set, make sure the one we keep is one of
 * them.
 *
 * NOTE: assumes that index != NULL. */
void
balsa_index_remove_duplicates(BalsaIndex * index)
{
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
	    libbalsa_messages_delete(messages, TRUE);
            enable_empty_trash(TRASH_CHECK);
	}
	g_list_free(messages);
    }

    g_hash_table_destroy(table);
}

/* Public method. */
void
balsa_index_refresh_size(BalsaIndex * index)
{
    GtkTreeModel *model;

    if (index->line_length == balsa_app.line_length)
        return;

    index->line_length = balsa_app.line_length;
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    gtk_tree_model_foreach(model, bndx_refresh_size_func,
                           GINT_TO_POINTER(index->line_length));
}

static gboolean
bndx_refresh_size_func(GtkTreeModel * model, GtkTreePath * path,
                       GtkTreeIter * iter, gpointer data)
{
    gchar *txt_new;
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    txt_new =
        libbalsa_message_size_to_gchar(message, GPOINTER_TO_INT(data));
    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                       BNDX_SIZE_COLUMN, txt_new,
                       -1);
    g_free(txt_new);

    return FALSE;
}

/* Public method. */
void
balsa_index_refresh_date(BalsaIndex * index)
{
    GtkTreeModel *model;

    if (!strcmp (index->date_string, balsa_app.date_string))
        return;

    g_free (index->date_string);
    index->date_string = g_strdup (balsa_app.date_string);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    gtk_tree_model_foreach(model, bndx_refresh_date_func,
                           index->date_string);
}

static gboolean
bndx_refresh_date_func(GtkTreeModel * model, GtkTreePath * path,
                       GtkTreeIter * iter, gpointer data)
{
    gchar *txt_new;
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    txt_new = libbalsa_message_date_to_gchar(message, (gchar*) data);
    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                       BNDX_DATE_COLUMN, txt_new,
                       -1);
    g_free(txt_new);

    return FALSE;
}

/* balsa_index_hide_deleted:
 * called from pref manager when balsa_app.hide_deleted is changed.
 */
void
balsa_index_hide_deleted(gboolean hide)
{
    gint i;
    GtkWidget *page;
    GtkWidget *index;

    for (i = 0; (page =
                 gtk_notebook_get_nth_page(GTK_NOTEBOOK
                                           (balsa_app.notebook),
                                           i)) != NULL; ++i) {
        index = gtk_bin_get_child(GTK_BIN(page));
        bndx_hide_deleted(BALSA_INDEX(index), hide);
    }
}

/* bndx_hide_deleted:
 * hide (or show, if hide is FALSE) deleted messages.
 */
static void
bndx_hide_deleted(BalsaIndex * index, gboolean hide)
{
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    GList *list;
    GList *messages = NULL;

    for (list = mailbox->message_list; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;

        if (LIBBALSA_MESSAGE_IS_DELETED(message))
            messages = g_list_prepend(messages, message);
    }

    if (hide)
        bndx_messages_remove(index, messages);
    else
        bndx_messages_add(index, messages);

    g_list_free(messages);
}

/* Transfer messages. */
void
balsa_index_transfer(BalsaIndex *index, GList * messages,
                     LibBalsaMailbox * to_mailbox, gboolean copy)
{
    LibBalsaMailbox *from_mailbox;

    if (messages == NULL)
        return;

    if (copy)
        libbalsa_messages_copy(messages, to_mailbox);
    else {
        libbalsa_messages_move(messages, to_mailbox);
    }

    from_mailbox = index->mailbox_node->mailbox;
    balsa_mblist_set_status_bar(from_mailbox);

    if (from_mailbox == balsa_app.trash && !copy)
        enable_empty_trash(TRASH_CHECK);
    else if (to_mailbox == balsa_app.trash)
        enable_empty_trash(TRASH_FULL);
}

/*
 * balsa_index_move_subtree
 *
 * model:       the GtkTreeModel for the index's tree;
 * root:        GtkTreePath pointing to the root of the subtree to be
 *              moved;
 * new_parent:  GtkTreePath pointing to the row that will be the new
 *              parent of the subtree;
 * ref_table:   a GHashTable used by the threading code; ignored if
 *              NULL.
 *
 * balsa_index_move_subtre moves the subtree.
 * bndx_make_tree and bndx_copy_tree are helpers.
 */

void
balsa_index_move_subtree(BalsaIndex * index,
                         GtkTreePath * root, 
                         GtkTreePath * new_parent)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    GtkTreeIter root_iter;
    GtkTreeIter iter;
    GtkTreeIter *parent_iter = NULL;
    GNode *node;

    g_assert(gtk_tree_model_get_iter(model, &root_iter, root));
    node = bndx_make_tree(index, &root_iter, root);

    if (new_parent) {
        gtk_tree_model_get_iter(model, &iter, new_parent);
        parent_iter = &iter;
    }

    bndx_copy_tree(index, node, parent_iter);
    g_node_destroy(node);
    gtk_tree_store_remove(GTK_TREE_STORE(model), &root_iter);
}

static GNode *
bndx_make_tree(BalsaIndex * index, GtkTreeIter * iter,
               GtkTreePath * path)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    LibBalsaMessage *message;
    GNode *node;
    GtkTreeIter child_iter;
    
    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    node = g_node_new(message);

    if (gtk_tree_model_iter_children(model, &child_iter, iter)) {
        gtk_tree_path_down(path);
        do {
            g_node_prepend(node, bndx_make_tree(index, &child_iter, path));
            gtk_tree_path_next(path);
        } while (gtk_tree_model_iter_next(model, &child_iter));
        gtk_tree_path_up(path);
    }

    return node;
}

static void
bndx_copy_tree(BalsaIndex * index, GNode * node, GtkTreeIter * parent_iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    LibBalsaMessage *message;
    GtkTreeRowReference *reference;
    GtkTreePath *path;
    GtkTreeIter old_iter;
    gchar *num, *from, *subject, *date, *size;
    GdkPixbuf *status, *attach;
    GdkColor *color;
    PangoWeight weight;
    GtkTreeIter new_iter;

    message = node->data;
    reference = g_hash_table_lookup(index->ref_table, message);
    path = gtk_tree_row_reference_get_path(reference);
    gtk_tree_model_get_iter(model, &old_iter, path);
    gtk_tree_path_free(path);

    gtk_tree_model_get(model, &old_iter,
                       BNDX_INDEX_COLUMN, &num,
                       BNDX_STATUS_COLUMN, &status,
                       BNDX_ATTACH_COLUMN, &attach,
                       BNDX_FROM_COLUMN, &from,
                       BNDX_SUBJECT_COLUMN, &subject,
                       BNDX_DATE_COLUMN, &date,
                       BNDX_SIZE_COLUMN, &size,
                       BNDX_COLOR_COLUMN, &color,
                       BNDX_WEIGHT_COLUMN, &weight, -1);

    gtk_tree_store_append(GTK_TREE_STORE(model), &new_iter, parent_iter);

    gtk_tree_store_set(GTK_TREE_STORE(model), &new_iter,
                       BNDX_MESSAGE_COLUMN, message,
                       BNDX_INDEX_COLUMN, num,
                       BNDX_STATUS_COLUMN, status,
                       BNDX_ATTACH_COLUMN, attach,
                       BNDX_FROM_COLUMN, from,
                       BNDX_SUBJECT_COLUMN, subject,
                       BNDX_DATE_COLUMN, date,
                       BNDX_SIZE_COLUMN, size,
                       BNDX_COLOR_COLUMN, color,
                       BNDX_WEIGHT_COLUMN, weight, -1);
    g_free(num);
    if (status)
        g_object_unref(status);
    if (attach)
        g_object_unref(attach);
    g_free(from);
    g_free(subject);
    g_free(date);
    g_free(size);
    if (color)
        gdk_color_free(color);

    path = gtk_tree_model_get_path(model, &new_iter);
    g_hash_table_replace(index->ref_table, message,
                         gtk_tree_row_reference_new(model, path));
    gtk_tree_path_free(path);

    for (node = node->children; node; node = node->next)
        bndx_copy_tree(index, node, &new_iter);

    if (balsa_app.expand_tree && parent_iter) {
	GtkTreePath *parent_path =
	    gtk_tree_model_get_path(model, parent_iter);
	gtk_tree_view_expand_row(GTK_TREE_VIEW(index), parent_path, TRUE);
	gtk_tree_path_free(parent_path);
    }
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
bndx_add_message(BalsaIndex * index, LibBalsaMessage * message)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    GtkTreeIter iter;
    gchar *num, *from, *subject, *date, *size;
    const gchar *name_str = NULL;
    GList *list;
    LibBalsaAddress *addy = NULL;
    LibBalsaMailbox* mailbox;
    gboolean append_dots;

    g_return_if_fail(index != NULL);
    g_return_if_fail(message != NULL);

    if (balsa_app.hide_deleted && LIBBALSA_MESSAGE_IS_DELETED(message))
        return;

    mailbox = index->mailbox_node->mailbox;
    
    if (mailbox == NULL)
	return;

    num = g_strdup_printf("%ld", LIBBALSA_MESSAGE_GET_NO(message) + 1);

    append_dots = FALSE;
    if (mailbox->view->show == LB_MAILBOX_SHOW_TO) {
	if (message->headers && message->headers->to_list) {
	    list = g_list_first(message->headers->to_list);
	    addy = list->data;
	    append_dots = list->next != NULL;
	}
    } else {
 	if (message->headers && message->headers->from)
	    addy = message->headers->from;
    }
    if (addy)
	name_str = libbalsa_address_get_name(addy);
    if(!name_str)		/* !addy, or addy contained no name/address */
	name_str = "";

    from = append_dots ? g_strconcat(name_str, ",...", NULL)
                       : g_strdup(name_str);
    libbalsa_utf8_sanitize(&from, balsa_app.convert_unknown_8bit, balsa_app.convert_unknown_8bit_codeset, NULL);

    subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
    libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit, balsa_app.convert_unknown_8bit_codeset, NULL);

    date = libbalsa_message_date_to_gchar(message, balsa_app.date_string);
    size = libbalsa_message_size_to_gchar(message, balsa_app.line_length);

    gtk_tree_store_insert_before(GTK_TREE_STORE(model), &iter, NULL, NULL);
    gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
                       BNDX_MESSAGE_COLUMN, message,
                       BNDX_INDEX_COLUMN, num,
                       BNDX_FROM_COLUMN, from,
                       BNDX_SUBJECT_COLUMN, subject,
                       BNDX_DATE_COLUMN, date,
                       BNDX_SIZE_COLUMN, size,
                       BNDX_COLOR_COLUMN, NULL,
                       BNDX_WEIGHT_COLUMN, PANGO_WEIGHT_NORMAL,
                       -1);
    g_free(num);
    g_free(from);
    g_free(subject);
    g_free(date);
    g_free(size);

    bndx_set_col_images(index, &iter, message);
}

static void
bndx_messages_remove(BalsaIndex * index, GList * messages)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
    GtkTreeModel *model;
    GtkTreeIter iter;
    GList *children = NULL;
    GList *list;
    LibBalsaMessage *next_message;
    GtkTreePath *path;

    g_return_if_fail(index != NULL);
    g_return_if_fail(index->mailbox_node != NULL);

    if (index->mailbox_node->mailbox == NULL)
        return;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));

    if (!(next_message = index->current_message)
        || g_list_find(messages, next_message)) {
        /* Current message is either NULL, or being removed. */
        if (!bndx_find_row(index, &iter, FALSE, 0, FILTER_NOOP, NULL,
                           messages)) {
            /* All messages are being removed: just clear the index. */
            g_signal_handler_block(selection, index->selection_changed_id);
            gtk_tree_store_clear(GTK_TREE_STORE(model));
            g_hash_table_foreach_remove(index->ref_table,
                                        (GHRFunc) gtk_true, NULL);
            g_signal_handler_unblock(selection,
                                     index->selection_changed_id);
            g_signal_emit_by_name(selection, "changed");
            return;
        }
        if (next_message)
            /* Current message is being removed: display the one we
             * found. */
            gtk_tree_model_get(model, &iter,
                               BNDX_MESSAGE_COLUMN, &next_message, -1);
        /* If no message is currently being displayed, we'll leave it
         * that way; that is, next_message is NULL. */
    }

    /* check the list of messages to be removed */
    for (list = messages; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;
        GtkTreeIter child_iter;

        if (!bndx_find_message(index, NULL, &iter, message)
            || !gtk_tree_model_iter_children(model, &child_iter, &iter))
            continue;

        /* message is in the index, and it has children */
        do {
            LibBalsaMessage *child_message;

            gtk_tree_model_get(model, &child_iter,
                               BNDX_MESSAGE_COLUMN, &child_message, -1);
            if (!g_list_find(messages, child_message)) {
                /* this child isn't being removed, so we must
                 * move it up */
                g_print("Adding child %s\n",
                        LIBBALSA_MESSAGE_GET_SUBJECT(child_message));
                children = g_list_prepend(children, child_message);
            }
        } while (gtk_tree_model_iter_next(model, &child_iter));
    }

    /* move the children to the top level */
    for (list = children; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;

        if (bndx_find_message(index, &path, NULL, message)) {
            g_print("Moving %s to top level\n",
                    LIBBALSA_MESSAGE_GET_SUBJECT(message));
            balsa_index_move_subtree(index, path, NULL);
            gtk_tree_path_free(path);
        }
    }
    g_list_free(children);

    /* remove the messages */
    g_signal_handler_block(selection, index->selection_changed_id);
    for (list = messages; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;

        if (bndx_find_message(index, NULL, &iter, message))
            gtk_tree_store_remove(GTK_TREE_STORE(model), &iter);
        g_hash_table_remove(index->ref_table, message);
    }
    g_signal_handler_unblock(selection, index->selection_changed_id);

    /* rethread and select the next message */
    balsa_index_threading(index,
                          index->mailbox_node->mailbox->view->threading_type);
    if (next_message)
        bndx_select_message(index, next_message);
    g_get_current_time (&index->last_use);
}

static void
bndx_scroll_to_row(BalsaIndex * index, GtkTreePath * path)
{
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(index), path, NULL,
                                 FALSE, 0, 0);
}

static void
bndx_changed_find_row(BalsaIndex * index)
{
    index->next_message =
        bndx_find_row(index, NULL, FALSE, 0, FILTER_NOOP, NULL, NULL);
    index->prev_message =
        bndx_find_row(index, NULL, TRUE,  0, FILTER_NOOP, NULL, NULL);

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
    GtkTreeRowReference *reference;
    GtkTreeModel *model;
    GtkTreePath *tmp_path;
    GtkTreeIter tmp_iter;

    reference = g_hash_table_lookup(index->ref_table, message);
    if (!reference)
        return FALSE;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    tmp_path = gtk_tree_row_reference_get_path(reference);
    if (!tmp_path)
        return FALSE;

    gtk_tree_model_get_iter(model, &tmp_iter, tmp_path);

    if (path)
        *path = tmp_path;
    else
        gtk_tree_path_free(tmp_path);

    if (iter)
        *iter = tmp_iter;

    return TRUE;
}

/* Make the actual selection, unselecting other messages and
 * making sure the selected row is within bounds and made visible.
 */
static void
bndx_select_row(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreeSelection *selection;

    g_return_if_fail(index != NULL);
    g_return_if_fail(BALSA_IS_INDEX(index));

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
    gtk_tree_selection_unselect_all(selection);
    /* select path, and make sure this path gets the keyboard focus */
    gtk_tree_selection_select_path(selection, path);
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(index), path, NULL, FALSE);

    bndx_scroll_to_row(index, path);
}

/* Set the tree store, load the messages, and thread. */
static void
bndx_load_and_thread(BalsaIndex * index, int thtype)
{
    LibBalsaMailbox *mailbox;
    GList *list;

    g_hash_table_foreach_remove(index->ref_table, (GHRFunc) gtk_true, NULL);
    bndx_set_tree_store(index);
    mailbox = index->mailbox_node->mailbox;
    for (list = mailbox->message_list; list; list = list->next)
        bndx_add_message(index, list->data);
    bndx_set_threading_type(index, thtype);
    bndx_set_sort_order(index, mailbox->view->sort_field,
                        mailbox->view->sort_type);
}

/* Set a tree store for the tree view, replacing the current one if
 * there is one; helper for bndx_load_and_thread. */
static void
bndx_set_tree_store(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeStore *tree_store;

    tree_store =
        gtk_tree_store_new(BNDX_N_COLUMNS,
                           G_TYPE_POINTER,   /* BNDX_MESSAGE_COLUMN */
                           G_TYPE_STRING,    /* BNDX_INDEX_COLUMN   */
                           GDK_TYPE_PIXBUF,  /* BNDX_STATUS_COLUMN  */
                           GDK_TYPE_PIXBUF,  /* BNDX_ATTACH_COLUMN  */
                           G_TYPE_STRING,    /* BNDX_FROM_COLUMN    */
                           G_TYPE_STRING,    /* BNDX_SUBJECT_COLUMN */
                           G_TYPE_STRING,    /* BNDX_DATE_COLUMN    */
                           G_TYPE_STRING,    /* BNDX_SIZE_COLUMN    */
                           GDK_TYPE_COLOR,   /* BNDX_COLOR_COLUMN   */
                           PANGO_TYPE_WEIGHT /* BNDX_WEIGHT_COLUMN  */
            );
    gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(tree_store));
    /* the BalsaIndex will hold the only ref on tree_store: */
    g_object_unref(tree_store);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree_store),
                                    BNDX_TREE_COLUMN_NO,
                                    bndx_row_compare,
                                    GINT_TO_POINTER(BNDX_TREE_COLUMN_NO),
                                    NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree_store),
                                    BNDX_TREE_COLUMN_DATE,
                                    bndx_row_compare,
                                    GINT_TO_POINTER(BNDX_TREE_COLUMN_DATE),
                                    NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree_store),
                                    BNDX_TREE_COLUMN_SIZE,
                                    bndx_row_compare,
                                    GINT_TO_POINTER(BNDX_TREE_COLUMN_SIZE),
                                    NULL);
}

/* Sort callback and helpers. */
static gint
bndx_row_compare(GtkTreeModel * model, GtkTreeIter * iter1,
                 GtkTreeIter * iter2, gpointer data)
{
    gint sort_column = GPOINTER_TO_INT(data);
    LibBalsaMessage *m1 = NULL;
    LibBalsaMessage *m2 = NULL;

    gtk_tree_model_get(model, iter1, BNDX_MESSAGE_COLUMN, &m1, -1);
    gtk_tree_model_get(model, iter2, BNDX_MESSAGE_COLUMN, &m2, -1);

    switch (sort_column) {
        case BNDX_TREE_COLUMN_NO:
            return numeric_compare(m1, m2);
        case BNDX_TREE_COLUMN_DATE:
            return date_compare(m1, m2);
        case BNDX_TREE_COLUMN_SIZE:
            return size_compare(m1, m2);
        default:
            return 0;
    }
}

static gint
date_compare(LibBalsaMessage * m1, LibBalsaMessage * m2)
{
    g_return_val_if_fail(m1 && m2 && m1->headers && m2->headers, 0);
    return m1->headers->date - m2->headers->date;
}


static gint
numeric_compare(LibBalsaMessage * m1, LibBalsaMessage * m2)
{
    glong t1, t2;

    g_return_val_if_fail(m1 && m2, 0);

    t1 = LIBBALSA_MESSAGE_GET_NO(m1);
    t2 = LIBBALSA_MESSAGE_GET_NO(m2);

    return t1-t2;
}

static gint
size_compare(LibBalsaMessage * m1, LibBalsaMessage * m2)
{
    glong t1, t2;

    g_return_val_if_fail(m1 && m2, 0);

    if (balsa_app.line_length) {
        t1 = LIBBALSA_MESSAGE_GET_LINES(m1);
        t2 = LIBBALSA_MESSAGE_GET_LINES(m2);
    } else {
        t1 = LIBBALSA_MESSAGE_GET_LENGTH(m1);
        t2 = LIBBALSA_MESSAGE_GET_LENGTH(m2);
    }

    return t1-t2;
}

/* Helper for bndx_load_and_thread. */
static void
bndx_set_sort_order(BalsaIndex * index, LibBalsaMailboxSortFields field, 
		    LibBalsaMailboxSortType order)
{
    BndxTreeColumnId col_id;
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeSortable *sortable =
        GTK_TREE_SORTABLE(gtk_tree_view_get_model(tree_view));
    GtkSortType gtk_sort;

    g_return_if_fail(index->mailbox_node);
    g_return_if_fail(order == LB_MAILBOX_SORT_TYPE_DESC || 
		     order == LB_MAILBOX_SORT_TYPE_ASC);

    
    index->mailbox_node->mailbox->view->sort_field = field;
    index->mailbox_node->mailbox->view->sort_type  = order;
    
    switch(field) {
    case LB_MAILBOX_SORT_NO:      col_id = BNDX_TREE_COLUMN_NO;      break;
    case LB_MAILBOX_SORT_SENDER:  col_id = BNDX_TREE_COLUMN_SENDER;  break;
    case LB_MAILBOX_SORT_SUBJECT: col_id = BNDX_TREE_COLUMN_SUBJECT; break;
    case LB_MAILBOX_SORT_SIZE:    col_id = BNDX_TREE_COLUMN_SIZE;    break;
    default:
    case LB_MAILBOX_SORT_DATE:    col_id = BNDX_TREE_COLUMN_DATE;    break;
    }
    gtk_sort = (order == LB_MAILBOX_SORT_TYPE_ASC) 
	? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;

    gtk_tree_sortable_set_sort_column_id(sortable, col_id, gtk_sort);
}

static void
bndx_set_threading_type(BalsaIndex * index, int thtype)
{
    index->mailbox_node->mailbox->view->threading_type = thtype;
    balsa_index_threading(index, thtype);

    /* expand tree if specified in config */
    balsa_index_update_tree(index, balsa_app.expand_tree);
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

/* Find the message's row, expand to it, and select. */
static void
bndx_select_message(BalsaIndex * index, LibBalsaMessage * message)
{
    GtkTreeIter iter;

    if (bndx_find_message(index, NULL, &iter, message))
        bndx_expand_to_row_and_select(index, &iter, TRUE);
}
