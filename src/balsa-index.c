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
   insertion which is not a big lost: NlnN process against sorted 
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

#include "filter-funcs.h"
#include "misc.h"

/* TREE_VIEW_FIXED_HEIGHT enables hight-performance mode of GtkTreeView
 * very useful for large mailboxes (#msg >5000) but: a. is available only
 * in gtk2>=2.3.5 b. may expose some bugs in gtk.
 * gtk-2.4.9 has been tested with a positive result.
 */
#if GTK_CHECK_VERSION(2,4,9)
#define TREE_VIEW_FIXED_HEIGHT 1
#endif


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

struct BndxSelectionChangedInfo {
    guint msgno;
    GArray *selected;
    GArray *new_selected;
};

static void bndx_selection_changed_func(GtkTreeModel * model,
                                        GtkTreePath * path,
                                        GtkTreeIter * iter,
                                        struct BndxSelectionChangedInfo
                                        *sci);
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
	    libbalsa_mailbox_close(mailbox, balsa_app.expunge_on_close);

	    libbalsa_mailbox_search_iter_free(index->search_iter);
	    index->search_iter = NULL;
	    libbalsa_mailbox_unregister_msgnos(mailbox, index->selected);
	}
	g_object_weak_unref(G_OBJECT(index->mailbox_node),
			    (GWeakNotify) gtk_widget_destroy, index);
	index->mailbox_node = NULL;
    }

    if (index->popup_menu) {
        g_object_unref(index->popup_menu);
        index->popup_menu = NULL;
    }

    if (index->current_message) {
	g_object_unref(index->current_message);
	index->current_message = NULL;
    }

    if (index->selected) {
	g_array_free(index->selected, TRUE);
	index->selected = NULL;
    }

    g_free(index->sos_filter); index->sos_filter = NULL;

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

static void
bi_apply_other_column_settings(GtkTreeViewColumn *column,
                               gboolean sortable, gint typeid)
{
#if !defined(ENABLE_TOUCH_UI)
    if(sortable)
        gtk_tree_view_column_set_sort_column_id(column, typeid);
#endif /* ENABLE_TOUCH_UI */

    gtk_tree_view_column_set_alignment(column, 0.5);

#if GTK_CHECK_VERSION(2,4,0) && defined(TREE_VIEW_FIXED_HEIGHT)
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
#endif
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
    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes("#", renderer,
                                                 "text", LB_MBOX_MSGNO_COL,
                                                 NULL);
    g_object_set(renderer, "xalign", 1.0, NULL);
    set_sizing(column); gtk_tree_view_append_column(tree_view, column);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_MSGNO_COL);

    /* Status icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_renderer_set_fixed_size(renderer, INDEX_ICON_SZ, INDEX_ICON_SZ);
    column =
        gtk_tree_view_column_new_with_attributes("S", renderer,
                                                 "pixbuf", LB_MBOX_MARKED_COL,
                                                 NULL);
    set_sizing(column); gtk_tree_view_append_column(tree_view, column);
    bi_apply_other_column_settings(column, FALSE, 0);

    /* Attachment icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_renderer_set_fixed_size(renderer, INDEX_ICON_SZ, INDEX_ICON_SZ);
    column =
        gtk_tree_view_column_new_with_attributes("A", renderer,
                                                 "pixbuf", LB_MBOX_ATTACH_COL,
                                                 NULL);
    set_sizing(column); gtk_tree_view_append_column(tree_view, column);
    bi_apply_other_column_settings(column, FALSE, 0);

    /* From/To column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("From"), renderer,
                                                 "text", LB_MBOX_FROM_COL,
						 "weight", LB_MBOX_WEIGHT_COL,
						 "style", LB_MBOX_STYLE_COL,
                                                 NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(tree_view, column);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_FROM_COL);

    /* Subject column--contains tree expanders */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("Subject"), renderer,
                                                 "text", LB_MBOX_SUBJECT_COL,
						 "weight", LB_MBOX_WEIGHT_COL,
						 "style", LB_MBOX_STYLE_COL,
                                                 NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(tree_view, column);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_SUBJECT_COL);
    gtk_tree_view_set_expander_column(tree_view, column);

    /* Date column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_("Date"), renderer,
                                                 "text", LB_MBOX_DATE_COL,
						 "weight", LB_MBOX_WEIGHT_COL,
						 "style", LB_MBOX_STYLE_COL,
                                                 NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(tree_view, column);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_DATE_COL);

    /* Size column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Size"));
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", LB_MBOX_SIZE_COL,
					"weight", LB_MBOX_WEIGHT_COL,
					"style", LB_MBOX_STYLE_COL,
                                        NULL);
    set_sizing(column); gtk_tree_view_append_column(tree_view, column);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_SIZE_COL);

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
#if GTK_CHECK_VERSION(2,4,9)
    gtk_tree_view_set_enable_search(tree_view, FALSE);
#endif

    gtk_drag_source_set(GTK_WIDGET (index), 
                        GDK_BUTTON1_MASK | GDK_SHIFT_MASK | GDK_CONTROL_MASK,
                        index_drag_types, ELEMENTS(index_drag_types),
                        GDK_ACTION_DEFAULT | GDK_ACTION_COPY | 
                        GDK_ACTION_MOVE);
    g_signal_connect(index, "drag-data-get",
                     G_CALLBACK(bndx_drag_cb), NULL);

    balsa_index_set_column_widths(index);
    gtk_widget_show_all (GTK_WIDGET(index));
    index->selected = g_array_new(FALSE, FALSE, sizeof(guint));
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

/* First some helpers. */

#if !GTK_CHECK_VERSION(2, 2, 0)
static void
bndx_selection_any(GtkTreeModel * model, GtkTreePath * path,
		   GtkTreeIter * iter, gpointer data)
{
    gboolean *have_selected = data;
    *have_selected = TRUE;
}
#endif				/* !GTK_CHECK_VERSION(2, 2, 0) */

static void
bndx_deselected_free(GArray * deselected)
{
    g_array_free(deselected, TRUE);
}

#define BALSA_INDEX_DESELECTED_ARRAY "balsa-index-deselected-array"
static gboolean
bndx_deselected_idle(BalsaIndex * index)
{
    GArray *deselected;

    gdk_threads_enter();

    deselected =
        g_object_get_data(G_OBJECT(index), BALSA_INDEX_DESELECTED_ARRAY);
    g_assert(deselected != NULL);

    /* Check whether the index was destroyed. */
    if (!index->mailbox_node) {
        g_object_set_data(G_OBJECT(index), BALSA_INDEX_DESELECTED_ARRAY,
                          NULL);
        g_object_unref(index);
        gdk_threads_leave();
        return FALSE;
    }

    libbalsa_mailbox_messages_change_flags(index->mailbox_node->mailbox,
                                           deselected, 0,
                                           LIBBALSA_MESSAGE_FLAG_SELECTED);

    if (index->current_message) {
        GtkTreePath *path;
        if (bndx_find_message(index, &path, NULL, index->current_message)) {
            GtkTreeSelection *selection =
                gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
            if (!gtk_tree_selection_path_is_selected(selection, path)) {
                bndx_expand_to_row(index, path);
                bndx_select_row(index, path);
            }
            gtk_tree_path_free(path);
        }                       /* else ??? */
    }

    g_object_set_data(G_OBJECT(index), BALSA_INDEX_DESELECTED_ARRAY, NULL);
    g_object_unref(index);

    gdk_threads_leave();
    return FALSE;
}

static void
bndx_selection_changed(GtkTreeSelection * selection, gpointer data)
{
    BalsaIndex *index = BALSA_INDEX(data);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(model);
    struct BndxSelectionChangedInfo sci;
    gboolean have_selected;
    GArray *deselected;
    gint i;
    gint current_depth = 0;
    guint current_msgno = 0;
    gboolean current_selected = FALSE;

    if (mailbox->state == LB_MAILBOX_STATE_TREECLEANING)
        return;

#if GTK_CHECK_VERSION(2, 2, 0)
    have_selected =
	gtk_tree_selection_count_selected_rows(selection) > 0;
#else
    have_selected = FALSE;

    gtk_tree_selection_selected_foreach(selection, bndx_selection_any,
					&have_selected);
#endif /* GTK_CHECK_VERSION(2, 2, 0) */

    /* Check previously selected messages. */
    deselected =
	g_object_get_data(G_OBJECT(index), BALSA_INDEX_DESELECTED_ARRAY);
    if (index->current_message)
	current_msgno = index->current_message->msgno;
    for (i = index->selected->len; --i >= 0; ) {
	GtkTreePath *path;
	guint msgno = g_array_index(index->selected, guint, i);

	if (!libbalsa_mailbox_msgno_find(mailbox, msgno, &path, NULL)) {
	    g_array_remove_index(index->selected, i);
	    continue;
	}

	/* Remove a deselected message from the list, unless it's not
	 * viewable and no rows are currently selected. */
	if (!gtk_tree_selection_path_is_selected(selection, path)
	    && (bndx_row_is_viewable(index, path) || have_selected)) {
	    /* The message has been deselected, and not by collapsing a
	     * thread; we'll notify the mailbox, so it can check whether
	     * the message still matches the view filter. */
	    /* Check filtering in an idle handler. */
	    if (!deselected) {
		deselected = g_array_new(FALSE, FALSE, sizeof(guint));
		g_object_set_data_full(G_OBJECT(index),
                                       BALSA_INDEX_DESELECTED_ARRAY,
				       deselected,
				       (GDestroyNotify) bndx_deselected_free);
		g_object_ref(index);
		g_idle_add((GSourceFunc) bndx_deselected_idle, index);
	    }
	    g_array_append_val(deselected, msgno);
	    g_array_remove_index(index->selected, i);
	    if (msgno == current_msgno)
		current_depth = gtk_tree_path_get_depth(path);
	} else if (msgno == current_msgno)
	    ++current_selected;
	gtk_tree_path_free(path);
    }

    /* Check currently selected messages. */
    sci.selected = index->selected;
    sci.new_selected = g_array_new(FALSE, FALSE, sizeof(guint));
    sci.msgno = 0;
    gtk_tree_selection_selected_foreach(selection,
                                        (GtkTreeSelectionForeachFunc)  
					bndx_selection_changed_func,
                                        &sci);
    libbalsa_mailbox_messages_change_flags(mailbox, sci.new_selected,
                                           LIBBALSA_MESSAGE_FLAG_SELECTED,
                                           0);
    g_array_append_vals(index->selected, sci.new_selected->data,
	                sci.new_selected->len);
    g_array_free(sci.new_selected, TRUE);

    /* If current message is still selected, return. */
    if (current_selected)
	return;

    /* sci.msgno is the msgno of the new current message;
     * if sci.msgno == 0, either:
     * - no messages remain in the view, in which case we clear
     *   index->current_message,
     * or:
     * - the thread containing the current message was collapsed, in
     *   which case we leave index->current_message unchanged;
     * we detect the latter case by checking the depth of the current
     * message.  */
    if (sci.msgno || current_depth <= 1) {
	if (index->current_message) {
	    g_object_unref(index->current_message);
	    index->current_message = NULL;
	}
        if (sci.msgno
            && (index->current_message =
                libbalsa_mailbox_get_message(mailbox, sci.msgno)))
	    index->current_message_is_deleted =
		LIBBALSA_MESSAGE_IS_DELETED(index->current_message);
        bndx_changed_find_row(index);
    }
}

static void
bndx_selection_changed_func(GtkTreeModel * model, GtkTreePath * path,
                            GtkTreeIter * iter,
			    struct BndxSelectionChangedInfo *sci)
{
    gint i;

    gtk_tree_model_get(model, iter, LB_MBOX_MSGNO_COL, &sci->msgno, -1);
    for (i = sci->selected->len; --i >= 0; )
        if (g_array_index(sci->selected, guint, i) == sci->msgno)
            return;
    g_array_append_val(sci->new_selected, sci->msgno);
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
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    GtkTreePath *current_path;
    gint i;

    g_signal_handler_block(selection, index->selection_changed_id);
    /* If a message in the selected list has become viewable, reselect
     * it. */
    for (i = index->selected->len; --i >= 0;) {
        guint msgno = g_array_index(index->selected, guint, i);

        if (libbalsa_mailbox_msgno_find(mailbox, msgno, &current_path,
                                        NULL)) {
            if (!gtk_tree_selection_path_is_selected(selection,
                                                     current_path)
                && bndx_row_is_viewable(index, current_path)) {
                gtk_tree_selection_select_path(selection, current_path);
                if (msgno == (guint) index->current_message->msgno) {
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
    BalsaIndex *index;
    
    g_return_if_fail (widget != NULL);
    index = BALSA_INDEX(widget);

    if (index->selected->len > 0)
        gtk_selection_data_set(data, data->target, 8,
                               (const guchar *) &index,
                               sizeof(BalsaIndex *));
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

/* Callback for the mailbox's "row-inserted" signal; queue an idle
 * handler to expand the thread. */
struct bndx_mailbox_row_inserted_info {
    LibBalsaMailbox *mailbox;
    guint msgno;
    BalsaIndex *index;
};

static gboolean
bndx_mailbox_row_inserted_idle(struct bndx_mailbox_row_inserted_info *info)
{
    GtkTreePath *path;
    gdk_threads_enter();
    if (libbalsa_mailbox_msgno_find(info->mailbox, info->msgno,
                                    &path, NULL)) {
        bndx_expand_to_row(info->index, path);
        gtk_tree_path_free(path);
    }
    g_object_unref(info->mailbox);
    g_object_unref(info->index);
    g_free(info);
    gdk_threads_leave();
    return FALSE;
}

static void
bndx_mailbox_row_inserted_cb(LibBalsaMailbox * mailbox, GtkTreePath * path,
                             GtkTreeIter * iter, BalsaIndex * index)
{
    guint msgno;
#ifdef BALSA_EXPAND_TO_NEW_UNREAD_MESSAGE
    LibBalsaMessage *message;

    if (mailbox->state != LB_MAILBOX_STATE_OPEN)
        return;

    gtk_tree_model_get(GTK_TREE_MODEL(mailbox), iter,
	               LB_MBOX_MSGNO_COL,   &msgno,
		       LB_MBOX_MESSAGE_COL, &message,
		       -1);

    if (balsa_app.expand_tree
        || (balsa_app.expand_to_new_unread
            && LIBBALSA_MESSAGE_IS_UNREAD(message)))
#else  /* BALSA_EXPAND_TO_NEW_UNREAD_MESSAGE */
    if (!balsa_app.expand_tree || mailbox->state != LB_MAILBOX_STATE_OPEN)
        return;

    gtk_tree_model_get(GTK_TREE_MODEL(mailbox), iter,
	               LB_MBOX_MSGNO_COL, &msgno,
		       -1);

#endif /* BALSA_EXPAND_TO_NEW_UNREAD_MESSAGE */
    {
	struct bndx_mailbox_row_inserted_info *info =
	    g_new(struct bndx_mailbox_row_inserted_info, 1);
	info->mailbox = mailbox;
	g_object_ref(mailbox);
	info->index = index;
	g_object_ref(index);
	info->msgno = msgno;
	g_idle_add_full(G_PRIORITY_LOW, /* to run after threading */
		        (GSourceFunc) bndx_mailbox_row_inserted_idle,
			info, NULL);
    }
#ifdef BALSA_EXPAND_TO_NEW_UNREAD_MESSAGE
    g_object_unref(message);
#endif /* BALSA_EXPAND_TO_NEW_UNREAD_MESSAGE */
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
balsa_index_load_mailbox_node (BalsaIndex * index,
                               BalsaMailboxNode* mbnode, GError **err)
{
    GtkTreeView *tree_view;
    LibBalsaMailbox* mailbox;
    gchar *msg;
    gboolean successp;
    gint try_cnt;

    g_return_val_if_fail(BALSA_IS_INDEX(index), TRUE);
    g_return_val_if_fail(index->mailbox_node == NULL, TRUE);
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), TRUE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox), TRUE);

    mailbox = mbnode->mailbox;
    msg = g_strdup_printf(_("Opening mailbox %s. Please wait..."),
			  mbnode->mailbox->name);
    gnome_appbar_push(balsa_app.appbar, msg);
    g_free(msg);
    try_cnt = 0;
    do {
        g_clear_error(err);
        gdk_threads_leave();
        successp = libbalsa_mailbox_open(mailbox, err);
        gdk_threads_enter();
        if (!balsa_app.main_window)
            return FALSE;
        if(successp &&
           !(*err && (*err)->code == LIBBALSA_MAILBOX_TOOMANYOPEN_ERROR))
            break;
        balsa_mblist_close_lru_peer_mbx(balsa_app.mblist, mailbox);
    } while(try_cnt++<3);
    gnome_appbar_pop(balsa_app.appbar);

    if (!successp)
	return TRUE;

    /*
     * set the new mailbox
     */
    index->mailbox_node = mbnode;
    g_object_weak_ref(G_OBJECT(mbnode),
		      (GWeakNotify) gtk_widget_destroy, index);
    libbalsa_mailbox_register_msgnos(mailbox, index->selected);
    /*
     * rename "from" column to "to" for outgoing mail
     */
    tree_view = GTK_TREE_VIEW(index);
    if (libbalsa_mailbox_get_show(mailbox) == LB_MAILBOX_SHOW_TO) {
        GtkTreeViewColumn *column =
	    gtk_tree_view_get_column(tree_view, LB_MBOX_FROM_COL);

        gtk_tree_view_column_set_title(column, _("To"));
    }

    g_signal_connect_swapped(G_OBJECT(mailbox), "changed",
			     G_CALLBACK(bndx_mailbox_changed_cb),
			     (gpointer) index);
    g_signal_connect(mailbox, "row-inserted",
	    	     G_CALLBACK(bndx_mailbox_row_inserted_cb), index);

    balsa_window_enable_mailbox_menus(balsa_app.main_window, index);
    /* libbalsa functions must be called with gdk unlocked
     * but balsa_index - locked!
     */
#if GTK_CHECK_VERSION(2, 4, 0)
    gdk_display_flush(gdk_display_get_default());
#else
    gdk_flush();
#endif
    gdk_threads_leave();
    libbalsa_mailbox_set_view_filter(mailbox,
                                     balsa_window_get_view_filter
                                     (balsa_app.main_window), FALSE);
    libbalsa_mailbox_set_threading(mailbox,
                                   libbalsa_mailbox_get_threading_type
                                   (mailbox));

    gdk_threads_enter();

    /* Set the tree store*/
#ifndef GTK2_FETCHES_ONLY_VISIBLE_CELLS
    g_object_set_data(G_OBJECT(mailbox), "tree-view", tree_view);
#endif
    gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(mailbox));

    /* Create a search-iter for SEARCH UNDELETED. */
    index->search_iter = libbalsa_mailbox_search_iter_new(&cond_undeleted);
    /* Note when this mailbox was opened, for use in auto-closing. */
    time(&index->mailbox_node->last_use);

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
    g_return_if_fail(BALSA_IS_INDEX(index));

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
                                     BNDX_SEARCH_WRAP_NO)
        && !bndx_search_iter_and_select(index, index->search_iter,
                                        BNDX_SEARCH_DIRECTION_PREV,
                                        BNDX_SEARCH_VIEWABLE_ONLY,
                                        BNDX_SEARCH_START_CURRENT,
                                        BNDX_SEARCH_WRAP_NO))
	/* Nowhere to go--unselect current, so it can be filtered out of
	 * the view. */
        gtk_tree_selection_unselect_all(gtk_tree_view_get_selection
                                        (GTK_TREE_VIEW(index)));
}

void
balsa_index_select_previous(BalsaIndex * index)
{
    g_return_if_fail(BALSA_IS_INDEX(index));

    bndx_search_iter_and_select(index, index->search_iter,
				BNDX_SEARCH_DIRECTION_PREV,
				BNDX_SEARCH_VIEWABLE_ONLY,
				BNDX_SEARCH_START_CURRENT,
				BNDX_SEARCH_WRAP_NO);
}

void
balsa_index_find(BalsaIndex * index,
		 LibBalsaMailboxSearchIter * search_iter,
		 gboolean previous, gboolean wrap)
{
    g_return_if_fail(BALSA_IS_INDEX(index));

    bndx_search_iter_and_select(index, search_iter,
				(previous ?
				 BNDX_SEARCH_DIRECTION_PREV :
				 BNDX_SEARCH_DIRECTION_NEXT),
				BNDX_SEARCH_VIEWABLE_ANY,
				BNDX_SEARCH_START_ANY,
				(wrap ?
				 BNDX_SEARCH_WRAP_YES :
				 BNDX_SEARCH_WRAP_NO));
}

static void
bndx_select_next_with_flag(BalsaIndex * index, LibBalsaMessageFlag flag)
{
    LibBalsaCondition cond_flag, cond_and;
    LibBalsaMailboxSearchIter *search_iter;

    g_return_if_fail(BALSA_IS_INDEX(index));

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

    if (bindex->current_message) {
	if (!LIBBALSA_MESSAGE_IS_DELETED(bindex->current_message))
	    bindex->current_message_is_deleted = FALSE;
	else if (!bindex->current_message_is_deleted)
	    bndx_select_next_threaded(bindex);
    }

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
	/* The thread containing the current message may have been
	 * collapsed by rethreading; re-expand it. */
	bndx_expand_to_row(bindex, path);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(bindex), path, NULL,
				     FALSE, 0, 0);
        gtk_tree_path_free(path);
    }

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
    BalsaIndex *index = BALSA_INDEX(data);
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    gint i;

    for (i = index->selected->len; --i >= 0;) {
        guint msgno = g_array_index(index->selected, guint, i);
        LibBalsaMessage *message =
            libbalsa_mailbox_get_message(mailbox, msgno);

	libbalsa_show_message_source(message, balsa_app.message_font,
				     &balsa_app.source_escape_specials);
        g_object_unref(message);
    }
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
 * Free with g_list_foreach(l,g_object_unref)/g_list_free.
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
        && !g_list_find(list, index->current_message)) {
        list = g_list_prepend(list, index->current_message);
        g_object_ref(index->current_message);
    }
 
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
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    gint i;

    for (i = index->selected->len; --i >= 0;) {
        guint msgno = g_array_index(index->selected, guint, i);
        LibBalsaMessage *message =
            libbalsa_mailbox_get_message(mailbox, msgno);
        BalsaSendmsg *sm = sendmsg_window_new(NULL, message, send_type);

        g_signal_connect(G_OBJECT(sm->window), "destroy",
                         G_CALLBACK(sendmsg_window_destroy_cb), NULL);
        g_object_unref(message);
    }
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
    GArray *messages;
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    gint i;

    messages = g_array_new(FALSE, FALSE, sizeof(guint));
    for (i = index->selected->len; --i >= 0;) {
        guint msgno = g_array_index(index->selected, guint, i);
	if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno, 0,
                                             LIBBALSA_MESSAGE_FLAG_DELETED))
            g_array_append_val(messages, msgno);
    }

    if (messages->len) {
	if (move_to_trash && (index != trash)) {
            libbalsa_mailbox_messages_move(mailbox, messages,
                                           balsa_app.trash);
	    enable_empty_trash(balsa_app.main_window, TRASH_FULL);
	} else {
            libbalsa_mailbox_messages_change_flags
		(mailbox, messages, LIBBALSA_MESSAGE_FLAG_DELETED,
		 (LibBalsaMessageFlag) 0);
	    if (index == trash)
		enable_empty_trash(balsa_app.main_window, TRASH_CHECK);
	}
    }
    g_array_free(messages, TRUE);
}

/*
 * Public message delete methods
 */
void
balsa_message_move_to_trash(GtkWidget * widget, gpointer user_data)
{
    BalsaIndex *index;

    g_return_if_fail(user_data != NULL);
    index = BALSA_INDEX(user_data);
    bndx_do_delete(index, TRUE);
    /* Note when message was flagged as deleted, for use in
     * auto-expunge. */
    time(&index->mailbox_node->last_use);
}

gint
balsa_find_notebook_page_num(LibBalsaMailbox * mailbox)
{
    GtkWidget *page;
    gint i;

    if (!balsa_app.notebook)
	return -1;

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
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    int is_all_flagged = TRUE;
    gint i;

    /* First see if we should set given flag or unset */
    for (i = index->selected->len; --i >= 0;) {
        guint msgno = g_array_index(index->selected, guint, i);
        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno, 0, flag)) {
	    is_all_flagged = FALSE;
	    break;
	}
    }

    libbalsa_mailbox_messages_change_flags(mailbox, index->selected,
                                           is_all_flagged ? 0 : flag,
                                           is_all_flagged ? flag : 0);

    if (flag == LIBBALSA_MESSAGE_FLAG_DELETED)
	/* Note when deleted flag was changed, for use in
	 * auto-expunge. */
	time(&index->mailbox_node->last_use);
}

static void
bi_toggle_deleted_cb(GtkWidget * widget, gpointer user_data)
{
    BalsaIndex *index;

    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX(user_data);
    balsa_index_toggle_flag(index, LIBBALSA_MESSAGE_FLAG_DELETED);

    if (widget == index->undelete_item && index->selected->len > 0) {
	LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
        guint msgno = g_array_index(index->selected, guint, 0);
        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                             LIBBALSA_MESSAGE_FLAG_DELETED,
					     0))
	    /* Oops! */
	    balsa_index_toggle_flag(index, LIBBALSA_MESSAGE_FLAG_DELETED);
    }
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

    if (index->selected->len > 0
        && index->mailbox_node->mailbox != mailbox)
        balsa_index_transfer(index, index->selected, mailbox, FALSE);
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
    gint i;

    BALSA_DEBUG();

    mailbox = index->mailbox_node->mailbox;
    for (i = index->selected->len; --i >= 0;) {
        guint msgno = g_array_index(index->selected, guint, i);
        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                             LIBBALSA_MESSAGE_FLAG_DELETED,
					     0))
            any_deleted = TRUE;
        else
            any_not_deleted = TRUE;
    }
    any = index->selected->len > 0;

    l = gtk_container_get_children(GTK_CONTAINER(menu));
    for (list = l; list; list = list->next)
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
    balsa_window_enable_continue(balsa_app.main_window);
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

    libbalsa_mailbox_set_threading_type(mailbox, thtype);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
    g_signal_handler_block(selection, index->selection_changed_id);
    libbalsa_mailbox_set_threading(mailbox, thtype);
    balsa_index_update_tree(index, balsa_app.expand_tree);
    g_signal_handler_unblock(selection, index->selection_changed_id);
}

void
balsa_index_set_sos_filter(BalsaIndex *bindex, const gchar *sos_filter,
                           LibBalsaCondition *flag_filter)
{
    LibBalsaMailbox * mailbox;

    g_return_if_fail(BALSA_IS_INDEX(bindex));
    mailbox = bindex->mailbox_node->mailbox;

    g_free(bindex->sos_filter);
    bindex->sos_filter = g_strdup(sos_filter);

    if(sos_filter && sos_filter[0] != '\0') {
        LibBalsaCondition *name = 
            libbalsa_condition_new_bool_ptr
            (FALSE, CONDITION_OR,
             libbalsa_condition_new_string
             (FALSE, CONDITION_MATCH_SUBJECT, g_strdup(sos_filter), NULL),
             libbalsa_condition_new_string
             (FALSE, CONDITION_MATCH_FROM, g_strdup(sos_filter), NULL));
        
        if(flag_filter)
            flag_filter = libbalsa_condition_new_bool_ptr
                (FALSE, CONDITION_AND, name, flag_filter);
        else 
            flag_filter = name;
    }

    libbalsa_mailbox_set_view_filter(mailbox, flag_filter, TRUE);

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
    for (list = mailbox->message_list; list; list = list->next) {
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
            if (bndx_find_message(index, &path, NULL, message)) {
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
balsa_index_transfer(BalsaIndex *index, GArray * msgnos,
                     LibBalsaMailbox * to_mailbox, gboolean copy)
{
    gboolean success;
    LibBalsaMailbox *from_mailbox;

    if (index->selected->len == 0)
        return;

    from_mailbox = index->mailbox_node->mailbox;
    success = copy ?
        libbalsa_mailbox_messages_copy(from_mailbox, msgnos, to_mailbox) :
        libbalsa_mailbox_messages_move(from_mailbox, msgnos, to_mailbox);

    if (!success) {
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  index->selected->len > 1 
			  ? _("Failed to copy messages to mailbox \"%s\".")
			  : _("Failed to copy message to mailbox \"%s\"."),
			  to_mailbox->name);
	return;
    }

    balsa_mblist_set_status_bar(from_mailbox);

    if (from_mailbox == balsa_app.trash && !copy)
        enable_empty_trash(balsa_app.main_window, TRASH_CHECK);
    else if (to_mailbox == balsa_app.trash)
        enable_empty_trash(balsa_app.main_window, TRASH_FULL);
    balsa_information(LIBBALSA_INFORMATION_MESSAGE,
                      copy ? _("Copied to \"%s\".") 
                      : _("Moved to \"%s\"."), to_mailbox->name);
    if (!copy)
	/* Note when message was flagged as deleted, for use in
	 * auto-expunge. */
	time(&index->mailbox_node->last_use);
}

/* General helpers. */
static void
bndx_expand_to_row(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreePath *tmp = gtk_tree_path_copy(path);
    gint i, j;

    while (gtk_tree_path_up(tmp) && gtk_tree_path_get_depth(tmp) > 0
	   && !gtk_tree_view_row_expanded(GTK_TREE_VIEW(index), tmp));
    /* Now we go from the deepest unexpanded ancestor up to full path */

    if ((i = gtk_tree_path_get_depth(tmp))
	< (j = gtk_tree_path_get_depth(path) - 1)) {
	gint *indices = gtk_tree_path_get_indices(path);

	do {
	    gtk_tree_path_append_index(tmp, indices[i]);
	    gtk_tree_view_expand_row(GTK_TREE_VIEW(index), tmp, FALSE);
	} while (++i < j);
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
    return message && message->msgno > 0
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

/* Expunge deleted messages. */
void
balsa_index_expunge(BalsaIndex * index)
{
    LibBalsaMailbox *mailbox;
    GtkTreeSelection *selection;

    g_return_if_fail(index != NULL);

    mailbox = index->mailbox_node->mailbox;
    if (mailbox->readonly)
	return;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
    g_signal_handler_block(selection, index->selection_changed_id);
    if (!libbalsa_mailbox_sync_storage(mailbox, TRUE))
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Committing mailbox %s failed."),
			  mailbox->name);
    g_signal_handler_unblock(selection, index->selection_changed_id);
    g_signal_emit_by_name(G_OBJECT(selection), "changed");
}

/* Message window */
static guint
bndx_next_msgno(BalsaIndex * index, guint current_msgno,
	        LibBalsaMailboxSearchIter * search_iter,
                BndxSearchDirection direction, BndxSearchViewable viewable,
                BndxSearchWrap wrap)
{
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    GtkTreeModel *model = GTK_TREE_MODEL(mailbox);
    GtkTreeIter iter;
    guint msgno = 0;
    guint stop_msgno;

    if (!libbalsa_mailbox_msgno_find(mailbox, current_msgno, NULL, &iter))
        return msgno;

    stop_msgno = 0;
    if (wrap == BNDX_SEARCH_WRAP_YES)
        stop_msgno = current_msgno;
    if (bndx_search_iter(index, search_iter, &iter, direction, viewable,
                         stop_msgno))
        gtk_tree_model_get(model, &iter, LB_MBOX_MSGNO_COL, &msgno, -1);

    return msgno;
}

guint
balsa_index_next_msgno(BalsaIndex * index, guint current_msgno)
{
    return bndx_next_msgno(index, current_msgno, index->search_iter,
                           BNDX_SEARCH_DIRECTION_NEXT,
                           BNDX_SEARCH_VIEWABLE_ONLY, BNDX_SEARCH_WRAP_NO);
}

guint
balsa_index_previous_msgno(BalsaIndex * index, guint current_msgno)
{
    return bndx_next_msgno(index, current_msgno, index->search_iter,
                           BNDX_SEARCH_DIRECTION_PREV,
                           BNDX_SEARCH_VIEWABLE_ONLY, BNDX_SEARCH_WRAP_NO);
}

static guint
bndx_next_msgno_with_flag(BalsaIndex * index, guint current_msgno,
                          LibBalsaMessageFlag flag)
{
    LibBalsaCondition *condition;
    LibBalsaMailboxSearchIter *search_iter;
    guint msgno;

    condition = libbalsa_condition_new_flag_enum(FALSE, flag);
    condition =
        libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND, condition,
                                        libbalsa_condition_clone
                                        (&cond_undeleted));
    search_iter = libbalsa_mailbox_search_iter_new(condition);
    libbalsa_condition_free(condition);

    msgno =
        bndx_next_msgno(index, current_msgno, search_iter,
		        BNDX_SEARCH_DIRECTION_NEXT,
                        BNDX_SEARCH_VIEWABLE_ANY, BNDX_SEARCH_WRAP_YES);
    libbalsa_mailbox_search_iter_free(search_iter);

    return msgno;
}

guint
balsa_index_next_unread_msgno(BalsaIndex * index, guint current_msgno)
{
    return bndx_next_msgno_with_flag(index, current_msgno,
                                     LIBBALSA_MESSAGE_FLAG_NEW);
}

guint
balsa_index_next_flagged_msgno(BalsaIndex * index, guint current_msgno)
{
    return bndx_next_msgno_with_flag(index, current_msgno,
                                     LIBBALSA_MESSAGE_FLAG_FLAGGED);
}
