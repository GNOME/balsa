/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-index.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "main-window.h"
#include "message-window.h"
#include "sendmsg-window.h"
#include "store-address.h"

#include "filter-funcs.h"
#include "misc.h"
#include <glib/gi18n.h>

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

/* TREE_VIEW_FIXED_HEIGHT enables hight-performance mode of GtkTreeView
 * very useful for large mailboxes (#msg >5000)  */
#define TREE_VIEW_FIXED_HEIGHT 1


/* gtk widget */
static void bndx_class_init(BalsaIndexClass * klass);
static void bndx_instance_init(BalsaIndex * index);
static void bndx_destroy(GObject * obj);
static gboolean bndx_popup_menu(GtkWidget * widget);

/* statics */

/* Manage the tree view */
static gboolean bndx_row_is_viewable(BalsaIndex * index,
                                     GtkTreePath * path);
static void bndx_expand_to_row_and_select(BalsaIndex * index,
                                          GtkTreeIter * iter);
static void bndx_changed_find_row(BalsaIndex * index);

/* mailbox callbacks */
static void bndx_mailbox_changed_cb(LibBalsaMailbox * mailbox,
                                    BalsaIndex      * bindex);

/* GtkTree* callbacks */
static void bndx_selection_changed(GtkTreeSelection * selection,
                                   BalsaIndex * index);
static void bndx_gesture_pressed_cb(GtkGestureMultiPress *multi_press,
                                    gint                  n_press,
                                    gdouble               x,
                                    gdouble               y,
                                    gpointer              user_data);
static void bndx_row_activated(GtkTreeView * tree_view, GtkTreePath * path,
                               GtkTreeViewColumn * column,
                               gpointer user_data);
static void bndx_column_resize(GtkWidget     * widget,
                               GtkAllocation * allocation,
                               gint            baseline,
                               GtkAllocation * clip,
                               gpointer        user_data);
static void bndx_tree_expand_cb(GtkTreeView * tree_view,
                                GtkTreeIter * iter, GtkTreePath * path,
                                gpointer user_data);
static gboolean bndx_test_collapse_row_cb(GtkTreeView * tree_view,
                                          GtkTreeIter * iter,
                                          GtkTreePath * path,
                                          gpointer user_data);
static void bndx_tree_collapse_cb(GtkTreeView * tree_view,
                                  GtkTreeIter * iter, GtkTreePath * path,
                                  gpointer user_data);

/* formerly balsa-index-page stuff */
enum {
    TARGET_MESSAGES
};

static const gchar * index_drag_types[] = {
    "x-application/x-message-list"
};

static void bndx_drag_cb(GtkWidget* widget,
                         GdkDragContext* drag_context,
                         GtkSelectionData* data,
                         guint time,
                         gpointer user_data);

/* Popup menu */
static GtkWidget* bndx_popup_menu_create(BalsaIndex * index);
static void bndx_do_popup(BalsaIndex     * index,
                          const GdkEvent * event);
static GtkWidget *create_stock_menu_item(GtkWidget * menu,
                                         const gchar * label,
                                         GCallback cb, gpointer data);

static void sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data);

/* signals */
enum {
    INDEX_CHANGED,
    LAST_SIGNAL
};

static gint balsa_index_signals[LAST_SIGNAL] = {
    0
};

/* General helpers. */
static void bndx_expand_to_row(BalsaIndex * index, GtkTreePath * path);
static void bndx_select_row(BalsaIndex * index, GtkTreePath * path);

/* Other callbacks. */
static void bndx_store_address(gpointer data);

static GtkTreeViewClass *parent_class = NULL;

/* Class type. */
GType
balsa_index_get_type(void)
{
    static GType balsa_index_type = 0;

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
    GObjectClass *object_class;
    GtkWidgetClass *widget_class;

    object_class = (GObjectClass *) klass;
    widget_class = (GtkWidgetClass *) klass;

    parent_class = g_type_class_peek_parent(klass);

    balsa_index_signals[INDEX_CHANGED] =
        g_signal_new("index-changed",
                     G_TYPE_FROM_CLASS(object_class),
		     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaIndexClass,
                                     index_changed),
                     NULL, NULL,
		     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);

    object_class->dispose = bndx_destroy;
    widget_class->popup_menu = bndx_popup_menu;
    klass->index_changed = NULL;
}

/* Object class destroy method. */
static void
bndx_mbnode_weak_notify(gpointer data, GObject *where_the_object_was)
{
    BalsaIndex *bindex = data;
    bindex->mailbox_node = NULL;
    gtk_widget_destroy(GTK_WIDGET(bindex));
}

static void
bndx_destroy(GObject * obj)
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
	}

	if (index->mailbox_node) {
            g_object_weak_unref(G_OBJECT(index->mailbox_node),
                                (GWeakNotify) bndx_mbnode_weak_notify,
                                index);
            index->mailbox_node = NULL;
        }
    }

    g_clear_pointer(&index->search_iter, libbalsa_mailbox_search_iter_unref);
    g_clear_pointer(&index->filter_string, g_free);

    g_clear_object(&index->popup_menu);
    g_clear_object(&index->gesture);

    if (G_OBJECT_CLASS(parent_class)->dispose != NULL)
        G_OBJECT_CLASS(parent_class)->dispose(obj);
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
    if(sortable)
        gtk_tree_view_column_set_sort_column_id(column, typeid);

    gtk_tree_view_column_set_alignment(column, 0.5);

#if defined(TREE_VIEW_FIXED_HEIGHT)
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
#endif
}

/* Width of a string in pixels for the default font. */
static gint
bndx_string_width(const gchar * text)
{
    GtkWidget *label;
    gint natural_width;

    label = gtk_label_new(NULL);
    gtk_label_set_markup((GtkLabel *) label, text);
    gtk_widget_measure(label, GTK_ORIENTATION_HORIZONTAL,
                       300, NULL, &natural_width, NULL, NULL);
    g_object_ref_sink(label);
    g_object_unref(label);

    return natural_width;
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
    GdkContentFormats *formats;
    GtkGesture *gesture;

#if defined(TREE_VIEW_FIXED_HEIGHT)
    gtk_tree_view_set_fixed_height_mode(tree_view, TRUE);
#endif

    /* Index column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        ("#", renderer,
         "text",           LB_MBOX_MSGNO_COL,
         "foreground",     LB_MBOX_FOREGROUND_COL,
         "foreground-set", LB_MBOX_FOREGROUND_SET_COL,
         "background",     LB_MBOX_BACKGROUND_COL,
         "background-set", LB_MBOX_BACKGROUND_SET_COL,
         NULL);
    g_object_set(renderer, "xalign", 1.0, NULL);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_MSGNO_COL);
    gtk_tree_view_append_column(tree_view, column);

    /* Status icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes
        ("S", renderer,
         "pixbuf",              LB_MBOX_MARKED_COL,
         "cell-background",     LB_MBOX_BACKGROUND_COL,
         "cell-background-set", LB_MBOX_BACKGROUND_SET_COL,
         NULL);
    bi_apply_other_column_settings(column, FALSE, 0);
    gtk_tree_view_append_column(tree_view, column);

    /* Attachment icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes
        ("A", renderer,
         "pixbuf",              LB_MBOX_ATTACH_COL,
         "cell-background",     LB_MBOX_BACKGROUND_COL,
         "cell-background-set", LB_MBOX_BACKGROUND_SET_COL,
         NULL);
    bi_apply_other_column_settings(column, FALSE, 0);
    gtk_tree_view_append_column(tree_view, column);

    /* From/To column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        (_("From"), renderer,
         "text",           LB_MBOX_FROM_COL,
         "weight",         LB_MBOX_WEIGHT_COL,
         "style",          LB_MBOX_STYLE_COL,
         "foreground",     LB_MBOX_FOREGROUND_COL,
         "foreground-set", LB_MBOX_FOREGROUND_SET_COL,
         "background",     LB_MBOX_BACKGROUND_COL,
         "background-set", LB_MBOX_BACKGROUND_SET_COL,
         NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_FROM_COL);
    gtk_tree_view_append_column(tree_view, column);

    /* Subject column--contains tree expanders */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        (_("Subject"), renderer,
         "text",           LB_MBOX_SUBJECT_COL,
         "weight",         LB_MBOX_WEIGHT_COL,
         "style",          LB_MBOX_STYLE_COL,
         "foreground",     LB_MBOX_FOREGROUND_COL,
         "foreground-set", LB_MBOX_FOREGROUND_SET_COL,
         "background",     LB_MBOX_BACKGROUND_COL,
         "background-set", LB_MBOX_BACKGROUND_SET_COL,
         NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_SUBJECT_COL);
    gtk_tree_view_append_column(tree_view, column);
    gtk_tree_view_set_expander_column(tree_view, column);

    /* Date column */
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        (_("Date"), renderer,
         "text",           LB_MBOX_DATE_COL,
         "weight",         LB_MBOX_WEIGHT_COL,
         "style",          LB_MBOX_STYLE_COL,
         "foreground",     LB_MBOX_FOREGROUND_COL,
         "foreground-set", LB_MBOX_FOREGROUND_SET_COL,
         "background",     LB_MBOX_BACKGROUND_COL,
         "background-set", LB_MBOX_BACKGROUND_SET_COL,
         NULL);
    gtk_tree_view_column_set_resizable(column, TRUE);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_DATE_COL);
    gtk_tree_view_append_column(tree_view, column);

    /* Size column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Size"));
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    /* get a better guess: */
    gtk_cell_renderer_set_fixed_size(renderer,
                                     bndx_string_width("<b>99.9M</b>"),
                                     -1);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes
        (column, renderer,
         "text",           LB_MBOX_SIZE_COL,
         "weight",         LB_MBOX_WEIGHT_COL,
         "style",          LB_MBOX_STYLE_COL,
         "foreground",     LB_MBOX_FOREGROUND_COL,
         "foreground-set", LB_MBOX_FOREGROUND_SET_COL,
         "background",     LB_MBOX_BACKGROUND_COL,
         "background-set", LB_MBOX_BACKGROUND_SET_COL,
         NULL);
    bi_apply_other_column_settings(column, TRUE, LB_MBOX_SIZE_COL);
    gtk_tree_view_append_column(tree_view, column);

    /* Initialize some other members */
    index->mailbox_node = NULL;
    index->popup_menu = bndx_popup_menu_create(index);
    g_object_ref_sink(index->popup_menu);

    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    /* handle select row signals to display message in the window
     * preview pane */
    index->selection_changed_id =
        g_signal_connect(selection, "changed",
                         G_CALLBACK(bndx_selection_changed), index);

    /* we want to handle button presses to pop up context menus if
     * necessary */
    gesture = gtk_gesture_multi_press_new(GTK_WIDGET(index));
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_signal_connect(gesture, "pressed",
		     G_CALLBACK(bndx_gesture_pressed_cb), NULL);
    /* We need to claim the event sequence before GtkTreeView gets it,
     * so we jump in at the capture phase: */
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture),
                                               GTK_PHASE_CAPTURE);
    index->gesture = gesture;

    g_signal_connect(tree_view, "row-activated",
		     G_CALLBACK(bndx_row_activated), NULL);

    /* catch thread expand events */
    index->row_expanded_id =
        g_signal_connect_after(tree_view, "row-expanded",
                               G_CALLBACK(bndx_tree_expand_cb), NULL);
    g_signal_connect(tree_view, "test-collapse-row",
                     G_CALLBACK(bndx_test_collapse_row_cb), NULL);
    index->row_collapsed_id =
        g_signal_connect_after(tree_view, "row-collapsed",
                               G_CALLBACK(bndx_tree_collapse_cb), NULL);

    /* We want to catch column resize attempts to store the new value */
    g_signal_connect_after(tree_view, "size-allocate",
                           G_CALLBACK(bndx_column_resize),
                           NULL);
    gtk_tree_view_set_enable_search(tree_view, FALSE);

    formats = gdk_content_formats_new(index_drag_types, G_N_ELEMENTS(index_drag_types));
    gtk_drag_source_set(GTK_WIDGET (index),
                        GDK_BUTTON1_MASK | GDK_SHIFT_MASK | GDK_CONTROL_MASK,
                        formats,
                        GDK_ACTION_DEFAULT | GDK_ACTION_COPY |
                        GDK_ACTION_MOVE);
    gdk_content_formats_unref(formats);

    g_signal_connect(index, "drag-data-get",
                     G_CALLBACK(bndx_drag_cb), NULL);

    balsa_index_set_column_widths(index);
    gtk_widget_show (GTK_WIDGET(index));
}

/*
 * Remove a GObject reference; if it was the last reference (and the
 * GObject has now been finalized), clear the location and return TRUE.
 */
static gboolean
bndx_clear_if_last_ref(gpointer data)
{
    GObject **object = data;

    g_object_add_weak_pointer(G_OBJECT(*object), data);
    g_object_unref(*object);
    if (*object) {
        g_object_remove_weak_pointer(*object, data);
        return FALSE;
    }
    return TRUE;
}

/* Callbacks used by bndx_instance_init. */

/*
 * bndx_selection_changed
 *
 * Callback for the selection "changed" signal.
 *
 * Do nothing if index->current_msgno is still selected;
 * otherwise, display the last (in tree order) selected message.
 */

/* idle callback: */
static gboolean
bndx_selection_changed_idle(BalsaIndex * index)
{
    LibBalsaMailbox *mailbox;
    guint msgno;
    GtkTreeSelection *selection;

    if (bndx_clear_if_last_ref(&index))
        return FALSE;
    index->has_selection_changed_idle = FALSE;

    if (!index->mailbox_node)
        return FALSE;
    mailbox = index->mailbox_node->mailbox;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(index));

    /* Save next_msgno, because changing flags may zero it. */
    msgno = index->next_msgno;
    if (index->current_msgno) {
        /* The current message has been deselected. */
        g_signal_handler_block(selection, index->selection_changed_id);
        libbalsa_mailbox_msgno_change_flags(mailbox, index->current_msgno,
                                            0,
                                            LIBBALSA_MESSAGE_FLAG_SELECTED);
        g_signal_handler_unblock(selection, index->selection_changed_id);
    }

    if (msgno) {
        GtkTreePath *path;

        if (!libbalsa_mailbox_msgno_find(mailbox, msgno, &path, NULL))
            msgno = 0;
        else {
            if (!gtk_tree_selection_path_is_selected(selection, path)) {
                bndx_expand_to_row(index, path);
                bndx_select_row(index, path);
            }
            gtk_tree_path_free(path);

            index->current_message_is_deleted =
                libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                                 LIBBALSA_MESSAGE_FLAG_DELETED,
                                                 0);
            libbalsa_mailbox_msgno_change_flags(mailbox, msgno,
                                                LIBBALSA_MESSAGE_FLAG_SELECTED,
                                                0);
        }
    }

    index->current_msgno = msgno;
    bndx_changed_find_row(index);

    return FALSE;
}

/* GtkTreeSelectionForeachFunc: */
static void
bndx_selection_changed_func(GtkTreeModel * model, GtkTreePath * path,
                            GtkTreeIter * iter, guint * msgno)
{
    if (!*msgno)
        gtk_tree_model_get(model, iter, LB_MBOX_MSGNO_COL, msgno, -1);
}

/* the signal handler: */
static void
bndx_selection_changed(GtkTreeSelection * selection, BalsaIndex * index)
{
    index->next_msgno = 0;
    gtk_tree_selection_selected_foreach(selection,
                                        (GtkTreeSelectionForeachFunc)
                                        bndx_selection_changed_func,
                                        &index->next_msgno);

    if (index->current_msgno) {
        GtkTreePath *path;

        if (libbalsa_mailbox_msgno_find(index->mailbox_node->mailbox,
                                        index->current_msgno,
                                        &path, NULL)) {
            gboolean update_preview = TRUE;

            if (gtk_tree_selection_path_is_selected(selection, path)) {
                /* The current message is still selected; we leave the
                 * preview unchanged. */
                update_preview = FALSE;
            } else if (index->collapsing
                       && !bndx_row_is_viewable(index, path)) {
                /* The current message was deselected because its thread
                 * was collapsed; we leave the preview unchanged, and to
                 * avoid confusion, we unselect all messages. */
                g_signal_handler_block(selection,
                                       index->selection_changed_id);
                gtk_tree_selection_unselect_all(selection);
                g_signal_handler_unblock(selection,
                                         index->selection_changed_id);
                update_preview = FALSE;
            }
            gtk_tree_path_free(path);

            if (!update_preview)
                return;
        }
    }

    if (!index->has_selection_changed_idle) {
        index->has_selection_changed_idle = TRUE;
        g_idle_add((GSourceFunc) bndx_selection_changed_idle,
                   g_object_ref(index));
    }
}

static void
bndx_gesture_pressed_cb(GtkGestureMultiPress *multi_press,
                        gint                  n_press,
                        gdouble               x,
                        gdouble               y,
                        gpointer              user_data)
{
    GtkGesture *gesture;
    GdkEventSequence *sequence;
    const GdkEvent *event;
    BalsaIndex *index;
    GtkTreeView *tree_view;
    gint bx;
    gint by;
    GtkTreePath *path;

    gesture = GTK_GESTURE(multi_press);
    sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(multi_press));
    event = gtk_gesture_get_last_event(gesture, sequence);
    g_return_if_fail(event != NULL);
    if (!gdk_event_triggers_context_menu(event))
        return;

    gtk_gesture_set_sequence_state(gesture, sequence, GTK_EVENT_SEQUENCE_CLAIMED);

    index = BALSA_INDEX(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    tree_view = GTK_TREE_VIEW(index);
    gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, (gint) x, (gint) y,
                                                      &bx, &by);

    /* pop up the context menu:
     * - if the clicked-on message is already selected, don't change
     *   the selection;
     * - if it isn't, select it (cancelling any previous selection)
     * - then create and show the menu */
    if (gtk_tree_view_get_path_at_pos(tree_view, bx, by, &path, NULL, NULL, NULL)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(tree_view);

        if (!gtk_tree_selection_path_is_selected(selection, path))
            bndx_select_row(index, path);
        gtk_tree_path_free(path);
    }

    bndx_do_popup(index, event);
}

static void
bndx_row_activated(GtkTreeView * tree_view, GtkTreePath * path,
                   GtkTreeViewColumn * column, gpointer user_data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    guint msgno;
    LibBalsaMailbox *mailbox;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, LB_MBOX_MSGNO_COL, &msgno, -1);
    g_return_if_fail(msgno > 0);

    mailbox = LIBBALSA_MAILBOX(model);
    /* activate a message means open a message window,
     * unless we're in the draftbox, in which case it means open
     * a sendmsg window */
    if (mailbox == balsa_app.draftbox) {
        /* the simplest way to get a sendmsg window would be:
         * balsa_message_continue(widget, (gpointer) index);
         *
         * instead we'll just use the guts of
         * balsa_message_continue: */
        BalsaSendmsg *sm =
            sendmsg_window_continue(mailbox, msgno);
        if (sm)
            g_signal_connect(G_OBJECT(sm->window), "destroy",
                             G_CALLBACK(sendmsg_window_destroy_cb), NULL);
    } else
        message_window_new(mailbox, msgno);
}

static gboolean
bndx_find_current_msgno(BalsaIndex * bindex,
                        GtkTreePath ** path , GtkTreeIter * iter)
{
    return bindex->current_msgno > 0
        && libbalsa_mailbox_msgno_find(bindex->mailbox_node->mailbox,
                                       bindex->current_msgno, path, iter);
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

    /* If current message has become viewable, reselect it. */
    if (bndx_find_current_msgno(index, &current_path, NULL)) {
        if (!gtk_tree_selection_path_is_selected(selection, current_path)
            && bndx_row_is_viewable(index, current_path)) {
            gtk_tree_selection_select_path(selection, current_path);
            gtk_tree_view_set_cursor(tree_view, current_path, NULL, FALSE);
            gtk_tree_view_scroll_to_cell(tree_view, current_path,
                                         NULL, FALSE, 0, 0);
        }
        gtk_tree_path_free(current_path);
    }
    bndx_changed_find_row(index);
}

/* bndx_test_collapse_row_cb:
 * callback on "test-collapse-row" events
 * just set collapsing, and return FALSE to allow the row (thread) to be
 * collapsed. */
static gboolean
bndx_test_collapse_row_cb(GtkTreeView * tree_view, GtkTreeIter * iter,
                          GtkTreePath * path, gpointer user_data)
{
    BalsaIndex *index = BALSA_INDEX(tree_view);
    index->collapsing = TRUE;
    return FALSE;
}

/* callback on collapse events;
 * clear collapsing;
 * the next message may have become invisible, so we must check whether
 * a next message still exists. */
static void
bndx_tree_collapse_cb(GtkTreeView * tree_view, GtkTreeIter * iter,
                      GtkTreePath * path, gpointer user_data)
{
    BalsaIndex *index = BALSA_INDEX(tree_view);
    index->collapsing = FALSE;
    bndx_changed_find_row(index);
}

/* When a column is resized, store the new size for later use */
static void
bndx_column_resize(GtkWidget * widget, GtkAllocation * allocation,
                   gint baseline, GtkAllocation * clip,
                   gpointer user_data)
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
 * This is the drag_data_get callback for the index widgets.
 * Currently supports DND only within the application.
 */
static void
bndx_drag_cb(GtkWidget        * widget,
             GdkDragContext   * drag_context,
             GtkSelectionData * data,
             guint              time,
             gpointer           user_data)
{
    BalsaIndex *index;

    g_return_if_fail(widget != NULL);

    index = BALSA_INDEX(widget);

    if (gtk_tree_selection_count_selected_rows
        (gtk_tree_view_get_selection(GTK_TREE_VIEW(index))) > 0)
        gtk_selection_data_set(data, gtk_selection_data_get_target(data),
                               8, (const guchar *) &index,
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
 * balsa_index_scroll_on_open() moves to the first unread message in
 * the index, or the last message if none is unread, and selects
 * it. Since this routine is usually called from a thread, we have to
 * take care and and make sure the row is selected from the main
 * thread only. And we also check whether the mailbox hasn't been
 * destroyed by now, of course.
 */

static gboolean
bndx_scroll_on_open_idle(BalsaIndex *index)
{
    LibBalsaMailbox *mailbox;
    guint msgno;
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreePath *path;
    gpointer view_on_open;

    if (bndx_clear_if_last_ref(&index))
        return FALSE;

    balsa_index_update_tree(index, balsa_app.expand_tree);
    mailbox = index->mailbox_node->mailbox;
    if ((msgno = libbalsa_mailbox_get_first_unread(mailbox))) {
	libbalsa_mailbox_set_first_unread(mailbox, 0);
        if(!libbalsa_mailbox_msgno_find(mailbox, msgno, &path, NULL))
            return FALSE; /* Oops! */
    } else {
        /* we want to scroll to the last one in current order. The
           alternative which is to scroll to the most recently
           delivered does not feel natural when other sorting order is
           used */
        int total = gtk_tree_model_iter_n_children
            (GTK_TREE_MODEL(mailbox), NULL);
        if(total == 0)
            return FALSE;
        path = gtk_tree_path_new_from_indices(total - 1, -1);
    }

    bndx_expand_to_row(index, path);
    gtk_tree_view_scroll_to_cell(tree_view, path, NULL, FALSE, 0, 0);

    view_on_open =
        g_object_get_data(G_OBJECT(mailbox), BALSA_INDEX_VIEW_ON_OPEN);
    g_object_set_data(G_OBJECT(mailbox), BALSA_INDEX_VIEW_ON_OPEN, NULL);

    if (gtk_tree_view_get_model(tree_view)) {
        if ((view_on_open && GPOINTER_TO_INT(view_on_open))
            || balsa_app.view_message_on_open)
            bndx_select_row(index, path);
        else {
            GtkTreeSelection *selection;
            gulong changed_id = index->selection_changed_id;

            selection = gtk_tree_view_get_selection(tree_view);
            g_signal_handler_block(selection, changed_id);
            gtk_tree_view_set_cursor(tree_view, path, NULL, FALSE);
            gtk_tree_selection_unselect_all(selection);
            g_signal_handler_unblock(selection, changed_id);
        }
    }
    gtk_tree_path_free(path);

    return FALSE;
}

void
balsa_index_scroll_on_open(BalsaIndex * bindex)
{
    /* Scroll in an idle handler, because the mailbox is perhaps being
     * opened in its own idle handler. */
    g_idle_add((GSourceFunc) bndx_scroll_on_open_idle,
               g_object_ref(bindex));
}

static LibBalsaCondition *cond_undeleted;

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
    if (libbalsa_mailbox_msgno_find(info->mailbox, info->msgno,
                                    &path, NULL)) {
        bndx_expand_to_row(info->index, path);
        gtk_tree_path_free(path);
    }
    g_object_unref(info->mailbox);
    g_object_unref(info->index);
    g_free(info);
    return FALSE;
}

static void
bndx_mailbox_row_inserted_cb(LibBalsaMailbox * mailbox, GtkTreePath * path,
                             GtkTreeIter * iter, BalsaIndex * index)
{
    guint msgno;

    if (libbalsa_mailbox_get_state(mailbox) != LB_MAILBOX_STATE_OPEN)
        return;

    gtk_tree_model_get(GTK_TREE_MODEL(mailbox), iter,
                       LB_MBOX_MSGNO_COL, &msgno, -1);

    if (balsa_app.expand_tree
#ifdef BALSA_EXPAND_TO_NEW_UNREAD_MESSAGE
        || (balsa_app.expand_to_new_unread
            && libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                                LIBBALSA_MESSAGE_FLAG_UNREAD,
                                                0))
#endif /* BALSA_EXPAND_TO_NEW_UNREAD_MESSAGE */
        )
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
}

static void
bndx_mailbox_message_expunged_cb(LibBalsaMailbox * mailbox, guint msgno,
                                 BalsaIndex * bindex)
{
    if (bindex->current_msgno == msgno)
        bindex->current_msgno = 0;
    else if (bindex->current_msgno > msgno)
        --bindex->current_msgno;

    if (bindex->next_msgno == msgno)
        bindex->next_msgno = 0;
    else if (bindex->next_msgno > msgno)
        --bindex->next_msgno;
}

/* balsa_index_load_mailbox_node:
 *
 * mbnode->mailbox is already open
 */

gboolean
balsa_index_load_mailbox_node(BalsaIndex * index,
                              BalsaMailboxNode* mbnode)
{
    GtkTreeView *tree_view;
    LibBalsaMailbox *mailbox;

    g_return_val_if_fail(BALSA_IS_INDEX(index), TRUE);
    g_return_val_if_fail(index->mailbox_node == NULL, TRUE);
    g_return_val_if_fail(BALSA_IS_MAILBOX_NODE(mbnode), TRUE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mbnode->mailbox), TRUE);

    mailbox = mbnode->mailbox;

    /*
     * set the new mailbox
     */
    index->mailbox_node = mbnode;
    g_object_weak_ref(G_OBJECT(mbnode),
                      (GWeakNotify) bndx_mbnode_weak_notify, index);
    /*
     * rename "from" column to "to" for outgoing mail
     */
    tree_view = GTK_TREE_VIEW(index);
    if (libbalsa_mailbox_get_show(mailbox) == LB_MAILBOX_SHOW_TO) {
        GtkTreeViewColumn *column =
	    gtk_tree_view_get_column(tree_view, LB_MBOX_FROM_COL);
        index->filter_no = 1; /* FIXME: this is hack! */
        gtk_tree_view_column_set_title(column, _("To"));
    }

    g_signal_connect(mailbox, "changed",
                     G_CALLBACK(bndx_mailbox_changed_cb), index);
    g_signal_connect(mailbox, "row-inserted",
                     G_CALLBACK(bndx_mailbox_row_inserted_cb), index);
    g_signal_connect(mailbox, "message-expunged",
                     G_CALLBACK(bndx_mailbox_message_expunged_cb), index);

    /* Set the tree store */
#ifndef GTK2_FETCHES_ONLY_VISIBLE_CELLS
    g_object_set_data(G_OBJECT(mailbox), "tree-view", tree_view);
#endif
    gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(mailbox));

    /* Create a search-iter for SEARCH UNDELETED. */
    if (!cond_undeleted)
        cond_undeleted =
            libbalsa_condition_new_flag_enum(TRUE,
                                             LIBBALSA_MESSAGE_FLAG_DELETED);
    index->search_iter = libbalsa_mailbox_search_iter_new(cond_undeleted);
    /* Note when this mailbox was opened, for use in auto-closing. */
    time(&index->mailbox_node->last_use);

    return FALSE;
}

void
balsa_index_set_width_preference(BalsaIndex *bindex,
                                 BalsaIndexWidthPreference pref)
{
    GtkTreeView *tree_view;
    gboolean visible;

    if (pref == bindex->width_preference)
        return;

    bindex->width_preference = pref;
    switch (pref) {
    case BALSA_INDEX_NARROW: visible = FALSE; break;
    default:
    case BALSA_INDEX_WIDE:   visible = TRUE;  break;
    }

    tree_view = GTK_TREE_VIEW(bindex);
    gtk_tree_view_column_set_visible
        (gtk_tree_view_get_column(tree_view, LB_MBOX_MSGNO_COL), visible);
    gtk_tree_view_column_set_visible
        (gtk_tree_view_get_column(tree_view, LB_MBOX_ATTACH_COL), visible);
    gtk_tree_view_column_set_visible
        (gtk_tree_view_get_column(tree_view, LB_MBOX_SIZE_COL), visible);
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

    if (!model || !gtk_tree_model_get_iter_first(model, iter))
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

    if (!((index->next_msgno > 0
           && libbalsa_mailbox_msgno_find(index->mailbox_node->mailbox,
                                          index->next_msgno, NULL, &iter))
          || (start == BNDX_SEARCH_START_ANY
              && bndx_find_root(index, &iter))))
        return FALSE;

    stop_msgno = 0;
    if (wrap == BNDX_SEARCH_WRAP_YES && index->next_msgno)
        stop_msgno = index->next_msgno;
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
    /* Make sure we start at the current message: */
    index->next_msgno = index->current_msgno;

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

static gboolean
bndx_select_next_with_flag(BalsaIndex * index, LibBalsaMessageFlag flag)
{
    LibBalsaCondition *cond_flag, *cond_and;
    LibBalsaMailboxSearchIter *search_iter;
    gboolean retval;

    g_assert(BALSA_IS_INDEX(index));

    cond_flag = libbalsa_condition_new_flag_enum(FALSE, flag);
    cond_and =
        libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND, cond_flag,
                                        cond_undeleted);
    libbalsa_condition_unref(cond_flag);

    search_iter = libbalsa_mailbox_search_iter_new(cond_and);
    libbalsa_condition_unref(cond_and);

    retval = bndx_search_iter_and_select(index, search_iter,
                                         BNDX_SEARCH_DIRECTION_NEXT,
                                         BNDX_SEARCH_VIEWABLE_ANY,
                                         BNDX_SEARCH_START_ANY,
                                         BNDX_SEARCH_WRAP_YES);

    libbalsa_mailbox_search_iter_unref(search_iter);

    return retval;
}

gboolean
balsa_index_select_next_unread(BalsaIndex * index)
{
    g_return_val_if_fail(BALSA_IS_INDEX(index), FALSE);

    return bndx_select_next_with_flag(index, LIBBALSA_MESSAGE_FLAG_NEW);
}

void
balsa_index_select_next_flagged(BalsaIndex * index)
{
    g_return_if_fail(BALSA_IS_INDEX(index));

    bndx_select_next_with_flag(index, LIBBALSA_MESSAGE_FLAG_FLAGGED);
}

void
balsa_index_set_next_msgno(BalsaIndex * bindex, guint msgno)
{
    bindex->next_msgno = msgno;
}

guint
balsa_index_get_next_msgno(BalsaIndex * bindex)
{
    return bindex->next_msgno;
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
    gint icon_w = 16;

#if defined(TREE_VIEW_FIXED_HEIGHT)
    /* so that fixed width works properly */
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_MSGNO_COL),
                                         bndx_string_width("00000"));
#endif
    /* I have no idea why we must add 5 pixels to the icon width - otherwise,
       the icon will be clipped... */
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_MARKED_COL),
                                         icon_w + 5);
    gtk_tree_view_column_set_fixed_width(gtk_tree_view_get_column
                                         (tree_view, LB_MBOX_ATTACH_COL),
                                         icon_w + 5);
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

/* bndx_mailbox_changed_cb:
   may be called from a thread. Use idle callback to update the view.
*/

static gboolean
bndx_queue_draw_idle_cb(gpointer bindex)
{
    if (!bndx_clear_if_last_ref(&bindex))
        gtk_widget_queue_draw(bindex);

    return FALSE;
}

static gboolean
bndx_mailbox_changed_idle(BalsaIndex * bindex)
{
    LibBalsaMailbox *mailbox;
    guint msgno;
    GtkTreePath *path;

    if (bndx_clear_if_last_ref(&bindex))
        return FALSE;

    bindex->has_mailbox_changed_idle = FALSE;

    mailbox = bindex->mailbox_node->mailbox;
    if ((msgno = libbalsa_mailbox_get_first_unread(mailbox)) > 0
        && libbalsa_mailbox_msgno_find(mailbox, msgno, &path, NULL)) {
        bndx_expand_to_row(bindex, path);
        gtk_tree_path_free(path);
        libbalsa_mailbox_set_first_unread(mailbox, 0);
    }

    if (bndx_find_current_msgno(bindex, &path, NULL)) {
        /* The thread containing the current message may have been
         * collapsed by rethreading; re-expand it. */
        bndx_expand_to_row(bindex, path);
        gtk_tree_path_free(path);
    }

    bndx_changed_find_row(bindex);

    g_idle_add((GSourceFunc) bndx_queue_draw_idle_cb,
               g_object_ref(bindex));

    return FALSE;
}

static void
bndx_mailbox_changed_cb(LibBalsaMailbox * mailbox, BalsaIndex * bindex)
{
    if (!gtk_widget_get_realized(GTK_WIDGET(bindex)))
        return;

    /* Find the next message to be shown now, not later in the idle
     * callback. */
    if (bindex->current_msgno) {
        if (libbalsa_mailbox_msgno_has_flags(mailbox,
                                             bindex->current_msgno, 0,
                                             LIBBALSA_MESSAGE_FLAG_DELETED))
            bindex->current_message_is_deleted = FALSE;
        else if (!bindex->current_message_is_deleted)
            bndx_select_next_threaded(bindex);
    }

    if (bindex->has_mailbox_changed_idle)
        return;

    bindex->has_mailbox_changed_idle = TRUE;
    g_idle_add((GSourceFunc) bndx_mailbox_changed_idle,
               g_object_ref(bindex));
}

static void
bndx_selected_msgnos_func(GtkTreeModel * model, GtkTreePath * path,
                          GtkTreeIter * iter, GArray * msgnos)
{
    guint msgno;

    gtk_tree_model_get(model, iter, LB_MBOX_MSGNO_COL, &msgno, -1);
    g_array_append_val(msgnos, msgno);
}

GArray *
balsa_index_selected_msgnos_new(BalsaIndex * index)
{
    GArray *msgnos = g_array_new(FALSE, FALSE, sizeof(guint));
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);

    gtk_tree_selection_selected_foreach(selection,
                                        (GtkTreeSelectionForeachFunc)
                                        bndx_selected_msgnos_func, msgnos);
    libbalsa_mailbox_register_msgnos(index->mailbox_node->mailbox, msgnos);
    return msgnos;
}

void
balsa_index_selected_msgnos_free(BalsaIndex * index, GArray * msgnos)
{
    libbalsa_mailbox_unregister_msgnos(index->mailbox_node->mailbox,
                                       msgnos);
    g_array_free(msgnos, TRUE);
}

static void
bndx_view_source(gpointer data)
{
    BalsaIndex *index = BALSA_INDEX(data);
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    guint i;
    GArray *selected = balsa_index_selected_msgnos_new(index);

    for (i = 0; i < selected->len; i++) {
        guint msgno = g_array_index(selected, guint, i);
        LibBalsaMessage *message =
            libbalsa_mailbox_get_message(mailbox, msgno);

        if (!message)
            continue;
	libbalsa_show_message_source(balsa_app.application,
                                     message, balsa_app.message_font,
				     &balsa_app.source_escape_specials,
                                     &balsa_app.source_width,
                                     &balsa_app.source_height);
        g_object_unref(message);
    }
    balsa_index_selected_msgnos_free(index, selected);
}

static void
bndx_store_address(gpointer data)
{
    GList *messages = balsa_index_selected_list(BALSA_INDEX(data));

    balsa_store_address_from_messages(messages);
    g_list_free_full(messages, g_object_unref);
}

static void
balsa_index_selected_list_func(GtkTreeModel * model, GtkTreePath * path,
                        GtkTreeIter * iter, gpointer data)
{
    GList **list = data;
    guint msgno;
    LibBalsaMessage *message;

    gtk_tree_model_get(model, iter, LB_MBOX_MSGNO_COL, &msgno, -1);
    message = libbalsa_mailbox_get_message(LIBBALSA_MAILBOX(model), msgno);
    if (!message)
        return;
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

    return list;
}

/*
 * bndx_compose_foreach: create a compose window for each selected
 * message
 */
static void
bndx_compose_foreach(BalsaIndex * index, SendType send_type)
{
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    GArray *selected = balsa_index_selected_msgnos_new(index);
    guint i;

    for (i = 0; i < selected->len; i++) {
        guint msgno = g_array_index(selected, guint, i);
        BalsaSendmsg *sm;
        switch(send_type) {
        case SEND_REPLY:
        case SEND_REPLY_ALL:
        case SEND_REPLY_GROUP:
            sm = sendmsg_window_reply(mailbox, msgno, send_type);
            break;
        case SEND_CONTINUE:
            sm = sendmsg_window_continue(mailbox, msgno);
            break;
        default:
            g_assert_not_reached();
            sm = NULL; /** silence invalid warnings */
        }
        if (sm)
            g_signal_connect(G_OBJECT(sm->window), "destroy",
                             G_CALLBACK(sendmsg_window_destroy_cb), NULL);
    }
    balsa_index_selected_msgnos_free(index, selected);
}

/*
 * Public `reply' methods
 */
void
balsa_message_reply(gpointer user_data)
{
    bndx_compose_foreach(BALSA_INDEX (user_data), SEND_REPLY);
}

void
balsa_message_replytoall(gpointer user_data)
{
    bndx_compose_foreach(BALSA_INDEX (user_data), SEND_REPLY_ALL);
}

void
balsa_message_replytogroup(gpointer user_data)
{
    bndx_compose_foreach(BALSA_INDEX (user_data), SEND_REPLY_GROUP);
}

void
balsa_message_continue(gpointer user_data)
{
    bndx_compose_foreach(BALSA_INDEX (user_data), SEND_CONTINUE);
}

/*
 * bndx_compose_from_list: create a single compose window for the
 * selected messages
 */
static void
bndx_compose_from_list(BalsaIndex * index, SendType send_type)
{
    GArray *selected = balsa_index_selected_msgnos_new(index);
    BalsaSendmsg *sm =
        sendmsg_window_new_from_list(index->mailbox_node->mailbox,
                                     selected, send_type);

    balsa_index_selected_msgnos_free(index, selected);
    g_signal_connect(G_OBJECT(sm->window), "destroy",
                     G_CALLBACK(sendmsg_window_destroy_cb), NULL);
}

/*
 * Public forwarding methods
 */
void
balsa_message_forward_attached(gpointer user_data)
{
    bndx_compose_from_list(BALSA_INDEX(user_data), SEND_FORWARD_ATTACH);
}

void
balsa_message_forward_inline(gpointer user_data)
{
    bndx_compose_from_list(BALSA_INDEX(user_data), SEND_FORWARD_INLINE);
}

void
balsa_message_forward_default(gpointer user_data)
{
    bndx_compose_from_list(BALSA_INDEX(user_data),
                           balsa_app.forward_attached ?
                           SEND_FORWARD_ATTACH : SEND_FORWARD_INLINE);
}

/*
 * bndx_do_delete: helper for message delete methods
 */
static void
bndx_do_delete(BalsaIndex* index, gboolean move_to_trash)
{
    BalsaIndex *trash = balsa_find_index_by_mailbox(balsa_app.trash);
    GArray *selected = balsa_index_selected_msgnos_new(index);
    GArray *messages;
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    guint i;

    messages = g_array_new(FALSE, FALSE, sizeof(guint));
    for (i = 0; i < selected->len; i++) {
        guint msgno = g_array_index(selected, guint, i);
	if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno, 0,
                                             LIBBALSA_MESSAGE_FLAG_DELETED))
            g_array_append_val(messages, msgno);
    }

    if (messages->len) {
	if (move_to_trash && (index != trash)) {
            GError *err = NULL;
            if(!libbalsa_mailbox_messages_move(mailbox, messages,
                                               balsa_app.trash, &err)) {
                balsa_information_parented(GTK_WINDOW(balsa_app.main_window),
                                           LIBBALSA_INFORMATION_ERROR,
                                           _("Move to Trash failed: %s"),
                                           err ? err->message : "?");
                g_clear_error(&err);
            }
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
    balsa_index_selected_msgnos_free(index, selected);
}

/*
 * Public message delete methods
 */
void
balsa_message_move_to_trash(gpointer user_data)
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

        if (index && BALSA_INDEX(index)->mailbox_node
            && BALSA_INDEX(index)->mailbox_node->mailbox == mailbox)
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
    GArray *selected = balsa_index_selected_msgnos_new(index);
    guint i;

    /* First see if we should set given flag or unset */
    for (i = 0; i < selected->len; i++) {
        guint msgno = g_array_index(selected, guint, i);
        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno, 0, flag)) {
	    is_all_flagged = FALSE;
	    break;
	}
    }

    libbalsa_mailbox_messages_change_flags(mailbox, selected,
                                           is_all_flagged ? 0 : flag,
                                           is_all_flagged ? flag : 0);
    balsa_index_selected_msgnos_free(index, selected);

    if (flag == LIBBALSA_MESSAGE_FLAG_DELETED)
	/* Note when deleted flag was changed, for use in
	 * auto-expunge. */
	time(&index->mailbox_node->last_use);
}

static void
bi_toggle_deleted_cb(gpointer user_data, GtkWidget * widget)
{
    BalsaIndex *index;
    GArray *selected;

    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX(user_data);
    balsa_index_toggle_flag(index, LIBBALSA_MESSAGE_FLAG_DELETED);

    selected = balsa_index_selected_msgnos_new(index);
    if (widget == index->undelete_item && selected->len > 0) {
	LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
        guint msgno = g_array_index(selected, guint, 0);
        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                             LIBBALSA_MESSAGE_FLAG_DELETED,
					     0))
	    /* Oops! */
	    balsa_index_toggle_flag(index, LIBBALSA_MESSAGE_FLAG_DELETED);
    }
    balsa_index_selected_msgnos_free(index, selected);
}

/* This function toggles the FLAGGED attribute of a list of messages
 */
static void
bi_toggle_flagged_cb(gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_index_toggle_flag(BALSA_INDEX(user_data),
                            LIBBALSA_MESSAGE_FLAG_FLAGGED);
}

static void
bi_toggle_new_cb(gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_index_toggle_flag(BALSA_INDEX(user_data),
                            LIBBALSA_MESSAGE_FLAG_NEW);
}


static void
mru_menu_cb(const gchar * url, BalsaIndex * index)
{
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);

    g_return_if_fail(mailbox != NULL);

    if (index->mailbox_node->mailbox != mailbox) {
        GArray *selected = balsa_index_selected_msgnos_new(index);
        balsa_index_transfer(index, selected, mailbox, FALSE);
        balsa_index_selected_msgnos_free(index, selected);
    }
}

/*
 * bndx_popup_menu_create: create the popup menu at init time
 */
static GtkWidget *
bndx_popup_menu_create(BalsaIndex * index)
{
    static const struct {       /* this is a invariable part of */
        const char *icon, *label;       /* the context message menu.    */
        GCallback func;
    } entries[] = {
        {
        BALSA_PIXMAP_REPLY, N_("_Reply…"),
                G_CALLBACK(balsa_message_reply)}, {
        BALSA_PIXMAP_REPLY_ALL, N_("Reply To _All…"),
                G_CALLBACK(balsa_message_replytoall)}, {
        BALSA_PIXMAP_REPLY_GROUP, N_("Reply To _Group…"),
                G_CALLBACK(balsa_message_replytogroup)}, {
        BALSA_PIXMAP_FORWARD, N_("_Forward Attached…"),
                G_CALLBACK(balsa_message_forward_attached)}, {
        BALSA_PIXMAP_FORWARD, N_("Forward _Inline…"),
                G_CALLBACK(balsa_message_forward_inline)}, {
        NULL,                 N_("_Pipe through…"),
                G_CALLBACK(balsa_index_pipe)}, {
        BALSA_PIXMAP_BOOK_RED, N_("_Store Address…"),
                G_CALLBACK(bndx_store_address)}};
    GtkWidget *menu, *menuitem, *submenu;
    unsigned i;

    menu = gtk_menu_new();

    for (i = 0; i < G_N_ELEMENTS(entries); i++)
        create_stock_menu_item(menu, _(entries[i].label),
                               entries[i].func, index);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());
    index->delete_item =
        create_stock_menu_item(menu, _("_Delete"),
                               G_CALLBACK(bi_toggle_deleted_cb),
                               index);
    index->undelete_item =
        create_stock_menu_item(menu, _("_Undelete"),
                               G_CALLBACK(bi_toggle_deleted_cb),
                               index);
    index->move_to_trash_item =
        create_stock_menu_item(menu, _("Move To _Trash"),
                               G_CALLBACK
                               (balsa_message_move_to_trash), index);

    menuitem = gtk_menu_item_new_with_mnemonic(_("T_oggle"));
    index->toggle_item = menuitem;
    submenu = gtk_menu_new();
    create_stock_menu_item(submenu, _("_Flagged"),
                           G_CALLBACK(bi_toggle_flagged_cb),
                           index);
    create_stock_menu_item(submenu, _("_Unread"),
                           G_CALLBACK(bi_toggle_new_cb),
                           index);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_mnemonic(_("_Move to"));
    index->move_to_item = menuitem;
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);


    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());
    create_stock_menu_item(menu, _("_View Source"),
                           G_CALLBACK(bndx_view_source),
                           index);

    return menu;
}

/* bndx_do_popup: common code for the popup menu;
 * set sensitivity of menuitems on the popup
 * menu, and populate the mru submenu
 */

/* If the menu is popped up in response to a keystroke, center it
 * below the headers of the tree-view.
 */

static void
bndx_set_sensitive_func(GtkWidget * item, gpointer sensitive)
{
    gtk_widget_set_sensitive(item, GPOINTER_TO_INT(sensitive));
}

static void
bndx_do_popup(BalsaIndex * index, const GdkEvent * event)
{
    GtkWidget *menu = index->popup_menu;
    GtkWidget *submenu;
    LibBalsaMailbox* mailbox;
    gboolean any;
    gboolean readonly;
    gboolean any_deleted = FALSE;
    gboolean any_not_deleted = FALSE;
    GArray *selected = balsa_index_selected_msgnos_new(index);
    guint i;

    BALSA_DEBUG();

    mailbox = index->mailbox_node->mailbox;
    for (i = 0; i < selected->len; i++) {
        guint msgno = g_array_index(selected, guint, i);
        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                             LIBBALSA_MESSAGE_FLAG_DELETED,
					     0))
            any_deleted = TRUE;
        else
            any_not_deleted = TRUE;
    }
    any = selected->len > 0;
    balsa_index_selected_msgnos_free(index, selected);

    gtk_container_foreach(GTK_CONTAINER(menu), bndx_set_sensitive_func,
                          GINT_TO_POINTER(any));

    mailbox = index->mailbox_node->mailbox;
    readonly = libbalsa_mailbox_get_readonly(mailbox);
    gtk_widget_set_sensitive(index->delete_item,
                             any_not_deleted && !readonly);
    gtk_widget_set_sensitive(index->undelete_item,
                             any_deleted && !readonly);
    gtk_widget_set_sensitive(index->move_to_trash_item,
                             any && mailbox != balsa_app.trash
                             && !readonly);
    gtk_widget_set_sensitive(index->toggle_item,
                             any && !readonly);
    gtk_widget_set_sensitive(index->move_to_item,
                             any && !readonly);

    submenu =
        balsa_mblist_mru_menu(GTK_WINDOW
                              (gtk_widget_get_toplevel(GTK_WIDGET(index))),
                              &balsa_app.folder_mru,
                              G_CALLBACK(mru_menu_cb), index);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(index->move_to_item),
                              submenu);

    if (event != NULL) {
        gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
    } else {
        gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(index),
                                 GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                 NULL);
    }
}

static GtkWidget *
create_stock_menu_item(GtkWidget * menu, const gchar * label, GCallback cb,
		       gpointer data)
{
    GtkWidget *menuitem = gtk_menu_item_new_with_mnemonic(label);

    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(cb), data);

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
    } else {
        g_signal_handler_block(index, index->row_collapsed_id);
        gtk_tree_view_collapse_all(tree_view);
        g_signal_handler_unblock(index, index->row_collapsed_id);
    }

    /* Re-expand msg_node's thread; cf. Remarks */
    /* expand_to_row is redundant in the expand_all case, but the
     * overhead is slight
     * select is needed in both cases, as a previous collapse could have
     * deselected the current message */
    if (bndx_find_current_msgno(index, NULL, &iter))
        bndx_expand_to_row_and_select(index, &iter);
    else
        balsa_index_ensure_visible(index);

    bndx_changed_find_row(index);
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

    if (thtype != LB_MAILBOX_THREADING_FLAT
        && !libbalsa_mailbox_prepare_threading(mailbox, 0))
        return;
    libbalsa_mailbox_set_threading_type(mailbox, thtype);

    libbalsa_mailbox_set_threading(mailbox, thtype);
    balsa_index_update_tree(index, balsa_app.expand_tree);
}

void
balsa_index_set_view_filter(BalsaIndex * bindex, int filter_no,
                            const gchar * filter_string,
                            LibBalsaCondition * filter)
{
    LibBalsaMailbox *mailbox;

    g_return_if_fail(BALSA_IS_INDEX(bindex));
    mailbox = bindex->mailbox_node->mailbox;

    g_free(bindex->filter_string);
    bindex->filter_no = filter_no;
    bindex->filter_string = g_strdup(filter_string);
    if (libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE))
        balsa_index_ensure_visible(bindex);
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
    GError *e = NULL;
    const gchar *to_mailbox_name;

    if (msgnos->len == 0)
        return;

    from_mailbox = index->mailbox_node->mailbox;
    success = copy ?
        libbalsa_mailbox_messages_copy(from_mailbox, msgnos, to_mailbox, &e) :
        libbalsa_mailbox_messages_move(from_mailbox, msgnos, to_mailbox, &e);

    to_mailbox_name = libbalsa_mailbox_get_name(to_mailbox);
    if (!success) {
	balsa_information
            (LIBBALSA_INFORMATION_WARNING,
             ngettext("Failed to copy %d message to mailbox “%s”: %s",
                      "Failed to copy %d messages to mailbox “%s”: %s",
                      msgnos->len),
             msgnos->len, to_mailbox_name, e ? e->message : "?");
	return;
    }

    if (from_mailbox == balsa_app.trash && !copy)
        enable_empty_trash(balsa_app.main_window, TRASH_CHECK);
    else if (to_mailbox == balsa_app.trash)
        enable_empty_trash(balsa_app.main_window, TRASH_FULL);
    balsa_information(LIBBALSA_INFORMATION_MESSAGE,
                      copy ? _("Copied to “%s”.")
                      : _("Moved to “%s”."), to_mailbox_name);
    if (!copy)
	/* Note when message was flagged as deleted, for use in
	 * auto-expunge. */
	time(&index->mailbox_node->last_use);
}

/* General helpers. */
static void
bndx_expand_to_row(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreePath *tmp;
    gint i, j;

    if (!gtk_widget_get_realized(GTK_WIDGET(index)))
        return;

    tmp = gtk_tree_path_copy(path);
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

    if (bndx_find_current_msgno(index, NULL, &iter)) {
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

/* Make the actual selection,
 * making sure the selected row is within bounds and made visible.
 */
static void
bndx_select_row(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);

    gtk_tree_view_set_cursor(tree_view, path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(tree_view, path, NULL, FALSE, 0, 0);
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
    gboolean rc;

    g_return_if_fail(index != NULL);

    mailbox = index->mailbox_node->mailbox;
    if (libbalsa_mailbox_get_readonly(mailbox))
	return;

    rc = libbalsa_mailbox_sync_storage(mailbox, TRUE);
    if (!rc)
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Committing mailbox %s failed."),
			  libbalsa_mailbox_get_name(mailbox));
}

/* Message window */
static guint
bndx_next_msgno(BalsaIndex * index, guint current_msgno,
                LibBalsaMailboxSearchIter * search_iter,
                BndxSearchDirection direction)
{
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    GtkTreeModel *model = GTK_TREE_MODEL(mailbox);
    GtkTreeIter iter;
    guint msgno = 0;

    if (!(current_msgno > 0
          && libbalsa_mailbox_msgno_find(mailbox, current_msgno, NULL,
                                         &iter)))
        return 0;

    if (bndx_search_iter(index, search_iter, &iter, direction,
                         BNDX_SEARCH_VIEWABLE_ONLY, 0))
        gtk_tree_model_get(model, &iter, LB_MBOX_MSGNO_COL, &msgno, -1);

    return msgno;
}

guint
balsa_index_next_msgno(BalsaIndex * index, guint current_msgno)
{
    return bndx_next_msgno(index, current_msgno, index->search_iter,
                           BNDX_SEARCH_DIRECTION_NEXT);
}

guint
balsa_index_previous_msgno(BalsaIndex * index, guint current_msgno)
{
    return bndx_next_msgno(index, current_msgno, index->search_iter,
                           BNDX_SEARCH_DIRECTION_PREV);
}

/* Functions and data structures for asynchronous message piping via
   external commands. */
/** PipeData stores the context of a message currently sent via a pipe
    to an external command. */
struct PipeData {
    GDestroyNotify destroy_notify;
    gpointer notify_arg;
    gchar *message;
    ssize_t message_length;
    ssize_t chars_written;
    GIOChannel *in_channel;
    GIOChannel *out_channel;
    GIOChannel *err_channel;
    guint in_source;
    guint out_source;
    guint err_source;
    int out_closed:1;
    int err_closed:1;
};

static void
pipe_data_destroy(struct PipeData* pipe)
{
    if(pipe->destroy_notify)
	pipe->destroy_notify(pipe->notify_arg);
    g_free(pipe->message);
    if(pipe->in_channel)
	g_io_channel_unref(pipe->in_channel);
    g_io_channel_unref(pipe->out_channel);
    g_io_channel_unref(pipe->err_channel);
    if(pipe->in_source)
	g_source_remove(pipe->in_source);
    g_source_remove(pipe->out_source);
    g_source_remove(pipe->err_source);
    g_free(pipe);
}

static gboolean
pipe_in_watch(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    struct PipeData *pipe = (struct PipeData*)data;
    GIOStatus status;
    GError *error = NULL;
    gsize chars_written;
    gboolean rc;

    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0) {
        if ((condition & G_IO_HUP) != 0) {
            fprintf(stderr, "pipe_in_watch: broken pipe. Aborts writing.\n");
        }
        if ((condition & G_IO_ERR) != 0) {
            fprintf(stderr, "pipe_in_watch encountered error. Aborts writing.\n");
        }
	pipe_data_destroy(pipe);
        return FALSE;
    }

    if( (condition & G_IO_OUT) == G_IO_OUT) {
	status =
	    g_io_channel_write_chars(channel,
				     pipe->message + pipe->chars_written,
				     pipe->message_length-pipe->chars_written,
				     &chars_written,
				     &error);

	switch(status) {
	case G_IO_STATUS_ERROR:
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Cannot process the message: %s"),
				 error->message);
	    g_clear_error(&error);
	    pipe_data_destroy(pipe);
	    return FALSE;
	case G_IO_STATUS_NORMAL:
	    pipe->chars_written += chars_written;
	    break;
	case G_IO_STATUS_EOF:
	    printf("pipe_in::write_chars receieved premature EOF %s\n",
		   error ? error->message : "unknown");
	    pipe_data_destroy(pipe);
	    return FALSE;
	case G_IO_STATUS_AGAIN:
	    printf("pipe_in::write_chars again?\n");
	    break;
	}
    }

    rc = pipe->message_length > pipe->chars_written;
    if(!rc) {
	g_io_channel_unref(pipe->in_channel); pipe->in_channel = NULL;
	g_source_remove(pipe->in_source);     pipe->in_source = 0;
    }
    return rc;
}

static gboolean
pipe_out_watch(GIOChannel *channel, GIOCondition condition, gpointer data)
{
    struct PipeData *pipe = (struct PipeData*)data;
    gsize bytes_read;
    GIOStatus status;
    GError *error = NULL;
    gchar *s;

    if ( (condition & G_IO_IN) == G_IO_IN) {
        char buf[2048];

	status =
	    g_io_channel_read_chars(channel, buf, sizeof(buf), &bytes_read,
				    &error);
	switch(status) {
	case G_IO_STATUS_ERROR:
	    pipe_data_destroy(pipe);
	    fprintf(stderr, "Reading characters from pipe failed: %s\n",
		    error ? error->message : "unknown");
	    g_clear_error(&error);
	    return FALSE;
	case G_IO_STATUS_NORMAL:
	    s = g_strndup(buf, bytes_read > 128 ? 128 : bytes_read);
	    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, "%s", s);
	    g_free(s);
	    /* if(fwrite(buf, 1, bytes_read, stdout) != bytes_read); */
	    break;
	case G_IO_STATUS_EOF:
	    printf("pipe_out got EOF\n");
	    pipe_data_destroy(pipe);
	    g_clear_error(&error);
	    return FALSE;
	case G_IO_STATUS_AGAIN: break;
	}
    }

    if ( condition == G_IO_HUP) {
	if(channel == pipe->out_channel)
	    pipe->out_closed = 1;
	else
	    pipe->err_closed = 1;
	if(pipe->out_closed && pipe->err_closed)
	    pipe_data_destroy((struct PipeData*)data);
    }

    if ( (condition & G_IO_ERR) == G_IO_ERR) {
	fprintf(stderr,
		"pipe_out_watch encountered error…\n");
	pipe_data_destroy(pipe);
	return FALSE;
    }
    return TRUE;
}

/** BndxPipeQueue represents the context of message pipe
    processing. Several messages from specified mailbox can be sent
    via the pipe. For each message, a separate context as stored in
    PipeData structure is created. */

struct BndxPipeQueue {
    LibBalsaMailbox *mailbox;
    GArray *msgnos;
    gchar *pipe_cmd;
};

static void
bndx_pipe_queue_last(struct BndxPipeQueue *queue)
{
    gchar **argv;
    struct PipeData *pipe;
    GMimeStream *stream = NULL;
    gboolean spawned;
    gssize chars_read;
    GError *error = NULL;
    int std_input;
    int std_output;
    int std_error;
    guint msgno = 0;

    while(queue->msgnos->len>0){
	msgno = g_array_index(queue->msgnos, guint, queue->msgnos->len-1);
	stream =
	    libbalsa_mailbox_get_message_stream(queue->mailbox, msgno, TRUE);
	g_array_remove_index(queue->msgnos, queue->msgnos->len-1);
	if(stream)
	    break;
	if(!stream) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Cannot access message %u to pass to %s"),
				 msgno, queue->pipe_cmd);
	}
    }

    if(queue->msgnos->len == 0 && !stream) {
	printf("Piping finished. Destroying the context.\n");
	libbalsa_mailbox_unregister_msgnos(queue->mailbox, queue->msgnos);
	libbalsa_mailbox_close(queue->mailbox, FALSE);
	g_array_free(queue->msgnos, TRUE);
	g_free(queue->pipe_cmd);
	g_free(queue);
	return;
    }

    pipe = g_new0(struct PipeData, 1);
    pipe->destroy_notify = (GDestroyNotify)bndx_pipe_queue_last;
    pipe->notify_arg = queue;

    pipe->message_length = g_mime_stream_length(stream);
    pipe->message = g_malloc(pipe->message_length);
    libbalsa_mailbox_lock_store(queue->mailbox);
    chars_read = g_mime_stream_read(stream, pipe->message,
				    pipe->message_length);
    libbalsa_mailbox_unlock_store(queue->mailbox);
    if(chars_read != pipe->message_length) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Cannot read message %u to pass to %s"),
				 msgno, queue->pipe_cmd);
	bndx_pipe_queue_last(queue);
	g_free(pipe);
	return;
    }

    argv = g_new(gchar *, 4);
    argv[0] = g_strdup("/bin/sh");
    argv[1] = g_strdup("-c");
    argv[2] = g_strdup(queue->pipe_cmd);
    argv[3] = NULL;
    spawned =
	g_spawn_async_with_pipes(NULL, argv, NULL,
				 0, NULL, NULL,
				 NULL,
				 &std_input, &std_output,
				 &std_error,
				 &error);
    g_strfreev(argv);
    if(spawned) {
	pipe->in_channel = g_io_channel_unix_new(std_input);
	g_io_channel_set_flags(pipe->in_channel, G_IO_FLAG_NONBLOCK, NULL);
	pipe->in_source = g_io_add_watch(pipe->in_channel, G_IO_OUT | G_IO_HUP,
					 pipe_in_watch, pipe);
	pipe->out_channel = g_io_channel_unix_new(std_output);
	g_io_channel_set_flags(pipe->out_channel, G_IO_FLAG_NONBLOCK, NULL);
	pipe->out_source = g_io_add_watch(pipe->out_channel, G_IO_IN|G_IO_HUP,
					  pipe_out_watch, pipe);
	pipe->err_channel = g_io_channel_unix_new(std_error);
	g_io_channel_set_flags(pipe->err_channel, G_IO_FLAG_NONBLOCK, NULL);
	pipe->err_source = g_io_add_watch(pipe->err_channel, G_IO_IN | G_IO_HUP,
					  pipe_out_watch, pipe);

	g_io_channel_set_encoding(pipe->in_channel, NULL, NULL);
	g_io_channel_set_encoding(pipe->out_channel, NULL, NULL);
	g_io_channel_set_encoding(pipe->err_channel, NULL, NULL);
	g_io_channel_set_close_on_unref(pipe->in_channel, TRUE);
	g_io_channel_set_close_on_unref(pipe->out_channel, TRUE);
	g_io_channel_set_close_on_unref(pipe->err_channel, TRUE);
    } else {
	printf("Could not spawn pipe %s : %s\n", queue->pipe_cmd,
	       error ? error->message : "unknown");
	g_clear_error(&error);
	g_free(pipe);
    }
}

/** Initiates the asynchronous process of sending specified messages
    from given mailbox via the provided command. */
static gboolean
bndx_start_pipe_messages_array(LibBalsaMailbox *mailbox,
			       GArray *msgnos,
			       const char *pipe_cmd)
{
    guint i;
    struct BndxPipeQueue *queue;

    if(!libbalsa_mailbox_open(mailbox, NULL))
	return FALSE;

    queue = g_new(struct BndxPipeQueue, 1);
    queue->mailbox = mailbox;
    queue->msgnos = g_array_sized_new(FALSE, FALSE, sizeof(guint), msgnos->len);
    queue->pipe_cmd = g_strdup(pipe_cmd);
    for(i=0; i<msgnos->len; i++)
	g_array_append_val(queue->msgnos,
			   g_array_index(msgnos, guint, msgnos->len-i-1));
    libbalsa_mailbox_register_msgnos(mailbox, queue->msgnos);

    bndx_pipe_queue_last(queue);
    return TRUE;
}

struct bndx_mailbox_info {
    GtkWidget *dialog;
    GtkWidget *entry;
    LibBalsaMailbox *mailbox;
    BalsaIndex *bindex;
    GArray *msgnos;
};

static void
bndx_mailbox_notify(gpointer data)
{
    struct bndx_mailbox_info *info = data;

    gtk_widget_destroy(info->dialog);
    balsa_index_selected_msgnos_free(info->bindex, info->msgnos);
    g_free(info);
}

#define BALSA_INDEX_PIPE_INFO "balsa-index-pipe-info"

static void
bndx_pipe_response(GtkWidget * dialog, gint response,
                   struct bndx_mailbox_info *info)
{
    LibBalsaMailbox *mailbox = info->mailbox;

    g_object_add_weak_pointer(G_OBJECT(mailbox), (gpointer) & mailbox);

    if (response == GTK_RESPONSE_OK) {
        gchar *pipe_cmd;
        GList *active_cmd;

        pipe_cmd =
            gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT
                                               (info->entry));
        active_cmd =
            g_list_find_custom(balsa_app.pipe_cmds, pipe_cmd,
                               (GCompareFunc) strcmp);
        if (!active_cmd)
            balsa_app.pipe_cmds =
                g_list_prepend(balsa_app.pipe_cmds, g_strdup(pipe_cmd));
        else if (active_cmd != balsa_app.pipe_cmds) {
            balsa_app.pipe_cmds =
                g_list_remove_link(balsa_app.pipe_cmds, active_cmd);
            balsa_app.pipe_cmds =
                g_list_concat(active_cmd, balsa_app.pipe_cmds);
        }

	bndx_start_pipe_messages_array(mailbox, info->msgnos, pipe_cmd);
        g_free(pipe_cmd);
    }

    if (!mailbox)
        return;
    g_object_remove_weak_pointer(G_OBJECT(mailbox), (gpointer) & mailbox);

    libbalsa_mailbox_close(mailbox, balsa_app.expunge_on_close);
    g_object_set_data(G_OBJECT(mailbox), BALSA_INDEX_PIPE_INFO, NULL);
}

#define HIG_PADDING 12

void
balsa_index_pipe(BalsaIndex * index)
{
    struct bndx_mailbox_info *info;
    GtkWidget *label, *entry;
    GtkWidget *dialog;
    GtkWidget *vbox;
    GList *list;

    g_return_if_fail(BALSA_IS_INDEX(index));
    g_return_if_fail(BALSA_IS_MAILBOX_NODE(index->mailbox_node));
    g_return_if_fail(LIBBALSA_IS_MAILBOX(index->mailbox_node->mailbox));

    info =
        g_object_get_data(G_OBJECT(index->mailbox_node->mailbox),
                          BALSA_INDEX_PIPE_INFO);
    if (info) {
        gtk_window_present(GTK_WINDOW(info->dialog));
        return;
    }

    if(!libbalsa_mailbox_open(index->mailbox_node->mailbox, NULL))
	return;
    info = g_new(struct bndx_mailbox_info, 1);
    info->bindex = index;
    info->mailbox = index->mailbox_node->mailbox;
    g_object_set_data_full(G_OBJECT(info->mailbox), BALSA_INDEX_PIPE_INFO,
                           info, bndx_mailbox_notify);

    info->msgnos = balsa_index_selected_msgnos_new(index);

    info->dialog = dialog =
        gtk_dialog_new_with_buttons(_("Pipe message through a program"),
                                    GTK_WINDOW(balsa_app.main_window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_Run"), GTK_RESPONSE_OK,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(balsa_app.main_window));
#endif
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    g_object_set(G_OBJECT(vbox), "margin", 5, NULL);
    gtk_box_set_spacing(GTK_BOX(vbox), HIG_PADDING);

    label = gtk_label_new(_("Specify the program to run:"));
    gtk_box_pack_start(GTK_BOX(vbox), label);

    info->entry = entry = gtk_combo_box_text_new_with_entry();
    for (list = balsa_app.pipe_cmds; list; list = list->next)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(entry),
                                       list->data);
    gtk_combo_box_set_active(GTK_COMBO_BOX(entry), 0);
    gtk_box_pack_start(GTK_BOX(vbox), entry);

    gtk_widget_show(label);
    gtk_widget_show(entry);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    g_signal_connect(dialog, "response", G_CALLBACK(bndx_pipe_response),
                     info);
    gtk_widget_show(dialog);
}

/** GtkTreeView can leave no messages showing after changing the view
 * filter, even though the view does contain messages.  We prefer to
 * scroll to either the current message. If this one is unavailable -
 * to the last message in the view, if any. */
void
balsa_index_ensure_visible(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GdkRectangle rect;
    GtkTreePath *path = NULL;

    if (!gtk_widget_get_realized(GTK_WIDGET(tree_view)))
        return;

    if (!bndx_find_current_msgno(index, &path, NULL)) {
        /* Current message not displayed, make sure that something
           else is... */
        gtk_tree_view_get_visible_rect(tree_view, &rect);
        gtk_tree_view_convert_tree_to_widget_coords(tree_view,
                                                    rect.x, rect.y,
                                                    &rect.x, &rect.y);

        if (gtk_tree_view_get_path_at_pos(tree_view, rect.x, rect.y, &path,
                                          NULL, NULL, NULL)) {
            /* We have a message in the view, so we do nothing. */
            gtk_tree_path_free(path);
            path = NULL;
        } else {
            /* Scroll to the last message. */
            GtkTreeModel *model;
            gint n_children;

            model = gtk_tree_view_get_model(tree_view);
            n_children = gtk_tree_model_iter_n_children(model, NULL);

            if (n_children > 0) {
                GtkTreeIter iter;
                gtk_tree_model_iter_nth_child(model, &iter, NULL,
                                              --n_children);
                path = gtk_tree_model_get_path(model, &iter);
            }
        }
    }

    if (path) {
        gtk_tree_view_scroll_to_cell(tree_view, path, NULL, FALSE, 0, 0);
        gtk_tree_path_free(path);
    }
}

void
balsa_index_select_all(BalsaIndex * bindex)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(bindex);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);

    gtk_tree_view_expand_all(tree_view);
    g_signal_handler_block(selection, bindex->selection_changed_id);
    gtk_tree_selection_select_all(selection);
    g_signal_handler_unblock(selection, bindex->selection_changed_id);
    g_signal_emit(bindex, balsa_index_signals[INDEX_CHANGED], 0);
}

gint
balsa_index_count_selected_messages(BalsaIndex * bindex)
{
    g_return_val_if_fail(BALSA_IS_INDEX(bindex), -1);

    return
        gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection
                                               (GTK_TREE_VIEW(bindex)));
}

/* Select all messages in current thread */
void
balsa_index_select_thread(BalsaIndex * bindex)
{
    GtkTreeIter iter;
    GtkTreeModel *model = GTK_TREE_MODEL(bindex->mailbox_node->mailbox);
    GtkTreeIter next_iter;
    GtkTreePath *path;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(bindex));
    gboolean valid;

    if (!bindex->current_msgno
        || !libbalsa_mailbox_msgno_find(bindex->mailbox_node->mailbox,
                                        bindex->current_msgno, NULL,
                                        &iter))
        return;

    while (gtk_tree_model_iter_parent(model, &next_iter, &iter))
        iter = next_iter;

    path = gtk_tree_model_get_path(model, &iter);
    gtk_tree_view_expand_row(GTK_TREE_VIEW(bindex), path, TRUE);
    gtk_tree_path_free(path);

    do {
        gtk_tree_selection_select_iter(selection, &iter);

        valid = gtk_tree_model_iter_children(model, &next_iter, &iter);
        if (valid)
            iter = next_iter;
        else {
            do {
                GtkTreeIter save_iter = iter;

                valid = gtk_tree_model_iter_next(model, &iter);
                if (valid)
                    break;
                valid = gtk_tree_model_iter_parent(model, &iter,
                                                   &save_iter);
            } while (valid);
        }
    } while (valid
             && gtk_tree_model_iter_parent(model, &next_iter, &iter));
}
