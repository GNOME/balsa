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

/* gtk widget */
static void balsa_index_class_init(BalsaIndexClass * klass);
static void balsa_index_init(BalsaIndex * index);
static void balsa_index_close_and_destroy(GtkObject * obj);

static gint date_compare(LibBalsaMessage * m1, LibBalsaMessage * m2);
static gint numeric_compare(LibBalsaMessage * m1, LibBalsaMessage * m2);
static gint size_compare(LibBalsaMessage * m1, LibBalsaMessage * m2);
static void bndx_column_click(GtkTreeViewColumn * column, gpointer data);

/* statics */
static void bndx_set_sort_order(BalsaIndex * index, int col_id, 
				GtkSortType order);
/* adds a new message */
static void balsa_index_add(BalsaIndex * index, LibBalsaMessage * message);
/* retrieve the selection */
static void bndx_set_col_images(BalsaIndex * index, GtkTreeIter * iter,
                                LibBalsaMessage * message);
static gboolean bndx_set_style(BalsaIndex * index, GtkTreePath * path);
static gboolean bndx_set_style_recursive(BalsaIndex * index,
                                         GtkTreePath * path);
static void bndx_set_parent_style(BalsaIndex * index, GtkTreePath * path);
static void bndx_check_visibility(BalsaIndex * index);
static void bndx_scroll_to_row(BalsaIndex * index, GtkTreePath * path);
static void bndx_find_row(BalsaIndex * index, GtkTreeIter * prev,
                          GtkTreeIter * next, LibBalsaMessageFlag flag,
                          gint op, GSList * conditions, GList * exclude);
static void bndx_expand_to_row_and_select(BalsaIndex * index,
                                          GtkTreeIter * iter);
static void balsa_index_transfer_messages(BalsaIndex * index,
                                          LibBalsaMailbox * mailbox);
static void bndx_set_current_message(BalsaIndex * index,
                                     LibBalsaMessage * message);
static void bndx_changed(BalsaIndex * index);
static void bndx_messages_remove(BalsaIndex * index, GList * messages);

/* mailbox callbacks */
static void mailbox_message_changed_status_cb(LibBalsaMailbox * mb,
					      LibBalsaMessage * message,
					      BalsaIndex * index);
static void mailbox_message_new_cb(BalsaIndex * index,
				   LibBalsaMessage * message);
static void mailbox_messages_new_cb(BalsaIndex * index, GList* messages);
static void mailbox_message_delete_cb(BalsaIndex * index, 
				      LibBalsaMessage * message);
static void mailbox_messages_delete_cb(BalsaIndex * index, 
				       GList  * message);

/* GtkTree* callbacks */
static void bndx_selection_changed(GtkTreeSelection * selection,
                                   gpointer data);
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
static void hide_deleted(BalsaIndex * index, gboolean hide);
static gboolean bndx_row_is_viewable(GtkTreeView * tree_view,
                                     GtkTreePath * path);
static gint bndx_row_compare(GtkTreeModel * model, GtkTreeIter * iter1,
                             GtkTreeIter * iter2, gpointer data);

/* formerly balsa-index-page stuff */
enum {
    TARGET_MESSAGES
};

static GtkTargetEntry index_drag_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

static gboolean idle_handler_cb(GtkWidget * widget);
static void bndx_drag_cb(GtkWidget* widget,
                                GdkDragContext* drag_context,
                                GtkSelectionData* data,
                                guint info,
                                guint time,
                                gpointer user_data);
static GtkWidget* bndx_popup_menu_create(BalsaIndex * index);
static GtkWidget* bndx_popup_menu_prepare(BalsaIndex * index);
static GtkWidget *create_stock_menu_item(GtkWidget * menu,
                                         const gchar * type,
                                         const gchar * label,
                                         GtkSignalFunc cb, gpointer data);

/* static void index_button_press_cb(GtkWidget * widget, */
/* 				  GdkEventButton * event, gpointer data); */

static void sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data);

/* signals */
enum {
    INDEX_CHANGED,
    LAST_SIGNAL
};

/* marshallers */
typedef void (*BalsaIndexSignal1) (GtkObject * object,
				   LibBalsaMessage * message,
				   GdkEventButton * bevent, gpointer data);

static gint balsa_index_signals[LAST_SIGNAL] = {
    0
};

static GtkTreeViewClass *parent_class = NULL;

GtkType
balsa_index_get_type(void)
{
    static GtkType balsa_index_type = 0;

    if (!balsa_index_type) {
        static const GTypeInfo balsa_index_info = {
            sizeof(BalsaIndexClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) balsa_index_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(BalsaIndex),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) balsa_index_init
        };

        balsa_index_type =
            g_type_register_static(GTK_TYPE_TREE_VIEW,
                                   "BalsaIndex", &balsa_index_info, 0);
    }

    return balsa_index_type;
}


static void
balsa_index_class_init(BalsaIndexClass * klass)
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

    object_class->destroy = balsa_index_close_and_destroy;
    klass->index_changed = NULL;
}

static void
balsa_index_init(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeStore *tree_store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    
    /* status
     * priority
     * attachments
     */
    static gchar *titles[] = {
	"#",
	"S",
	"A",
        N_("From"),
        N_("Subject"),
        N_("Date"),
        N_("Size")
    };

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

    /* Index column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _(titles[0]));
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", BNDX_INDEX_COLUMN,
                                        NULL);
    gtk_tree_view_column_set_clickable(column, TRUE);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click),
                     index);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree_store),
                                    0, bndx_row_compare,
                                    GINT_TO_POINTER(0), NULL);
    gtk_tree_view_append_column(tree_view, column);

    /* Status icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column =
        gtk_tree_view_column_new_with_attributes(_(titles[1]),
                                                 renderer,
                                                 "pixbuf", BNDX_STATUS_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_append_column(tree_view, column);

    /* Attachment icon column */
    renderer = gtk_cell_renderer_pixbuf_new();
    column =
        gtk_tree_view_column_new_with_attributes(_(titles[2]),
                                                 renderer,
                                                 "pixbuf", BNDX_ATTACH_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_append_column(tree_view, column);
    
    /* From/To column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_(titles[3]),
                                                 renderer,
                                                 "text", BNDX_FROM_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_append_column(tree_view, column);

    /* Subject column--contains tree expanders */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_(titles[4]),
                                                 renderer,
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
    gtk_tree_view_append_column(tree_view, column);
    gtk_tree_view_set_expander_column(tree_view, column);

    /* Date column */
    renderer = gtk_cell_renderer_text_new();
    column = 
        gtk_tree_view_column_new_with_attributes(_(titles[5]),
                                                 renderer,
                                                 "text", BNDX_DATE_COLUMN,
                                                 NULL);
    gtk_tree_view_column_set_alignment(column, 0.5);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_clickable(column, TRUE);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click),
                     index);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree_store),
                                    5, bndx_row_compare,
                                    GINT_TO_POINTER(5), NULL);
    gtk_tree_view_append_column(tree_view, column);

    /* Size column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _(titles[6]));
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", BNDX_SIZE_COLUMN,
                                        NULL);
    gtk_tree_view_column_set_clickable(column, TRUE);
    g_signal_connect(G_OBJECT(column), "clicked",
                     G_CALLBACK(bndx_column_click),
                     index);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(tree_store),
                                    6, bndx_row_compare,
                                    GINT_TO_POINTER(6), NULL);
    gtk_tree_view_append_column(tree_view, column);

    /* Initialize some other members */
    index->mailbox_node = NULL;
    index->popup_menu = bndx_popup_menu_create(index);
    
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    /* Set default sorting behaviour */
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(tree_store),
                                         5, GTK_SORT_ASCENDING);

    /* handle select row signals to display message in the window
     * preview pane */
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
    g_signal_connect_after(tree_view, "row-expanded",
                           G_CALLBACK(bndx_tree_expand_cb), NULL);
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
    GTK_OBJECT_UNSET_FLAGS (index, GTK_CAN_FOCUS);
    gtk_widget_show_all (GTK_WIDGET(index));
    gtk_widget_ref (GTK_WIDGET(index));
}

GtkWidget *
balsa_index_new(void)
{
    BalsaIndex* index = g_object_new(BALSA_TYPE_INDEX, NULL);

    return GTK_WIDGET(index);
}


static gint
date_compare(LibBalsaMessage * m1, LibBalsaMessage * m2)
{
    g_return_val_if_fail(m1 && m2, 0);

    return m2->date - m1->date;
}


static gint
numeric_compare(LibBalsaMessage * m1, LibBalsaMessage * m2)
{
    glong t1, t2;

    g_return_val_if_fail(m1 && m2, 0);

    t1 = LIBBALSA_MESSAGE_GET_NO(m1);
    t2 = LIBBALSA_MESSAGE_GET_NO(m2);

    return t2-t1;
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

    return t2-t1;
}

static void
bndx_column_click(GtkTreeViewColumn * column, gpointer data)
{
    GtkSortType order = GTK_SORT_ASCENDING;
    GtkTreeView *tree_view = GTK_TREE_VIEW(data);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(model);
    GList *columns = gtk_tree_view_get_columns(tree_view);
    gint col_id = g_list_index(columns, column);
    gint current_col_id;

    g_list_free(columns);
    if (gtk_tree_sortable_get_sort_column_id(sortable,
                                             &current_col_id, &order)) {
        GtkTreeViewColumn *current_column =
            gtk_tree_view_get_column(tree_view, current_col_id);

        gtk_tree_view_column_set_sort_indicator(current_column, FALSE);
        if (current_col_id == col_id)
            order = (order == GTK_SORT_DESCENDING ?
                    GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);
    }

    BALSA_INDEX(tree_view)->mailbox_node->sort_field = col_id;
    BALSA_INDEX(tree_view)->mailbox_node->sort_type  = order;

    gtk_tree_sortable_set_sort_column_id(sortable, col_id, order);
    gtk_tree_view_column_set_sort_indicator(column, TRUE);
    gtk_tree_view_column_set_sort_order(column, order);
    bndx_changed(BALSA_INDEX(tree_view));
}

static void
bndx_expand_to_row(GtkTreeView * tree_view, GtkTreePath * path)
{
    GtkTreePath *tmp = gtk_tree_path_copy(path);

    if (gtk_tree_path_up(tmp) && gtk_tree_path_get_depth(tmp) > 0) {
        bndx_expand_to_row(tree_view, tmp);
        gtk_tree_view_expand_row(tree_view, tmp, FALSE);
    }

    gtk_tree_path_free(tmp);
}

/* 
 * This is an idle handler. Be sure to use gdk_threads_{enter/leave}
 * 
 * Description: moves to the first unread message in the index, and
 * selects it.
 */
static gboolean
moveto_handler(BalsaIndex * index)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model;
    gint rows;
    GtkTreeIter iter;
    
    gdk_threads_enter();

    if (!GTK_WIDGET_VISIBLE(GTK_WIDGET(index))) {
        gdk_threads_leave();
	return TRUE;
    }

    model = gtk_tree_view_get_model(tree_view);
    rows = gtk_tree_model_iter_n_children(model, NULL);
    if (rows <= 0) {
        gdk_threads_leave();
        return FALSE;
    }

    bndx_find_row(index, NULL, &iter, LIBBALSA_MESSAGE_FLAG_NEW,
                  FILTER_NOOP, NULL, NULL);
    if (!iter.stamp)
        gtk_tree_model_iter_nth_child(model, &iter, NULL, rows - 1);

    if (balsa_app.view_message_on_open)
        bndx_expand_to_row_and_select(index, &iter);
    else {
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);

        bndx_expand_to_row(tree_view, path);
        bndx_scroll_to_row(index, path);
        gtk_tree_path_free(path);
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
    successp = libbalsa_mailbox_open(mailbox);
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
    }

    /*
     * set the new mailbox
     */
    index->mailbox_node = mbnode;
    /*
     * rename "from" column to "to" for outgoing mail
     */
    if (mailbox == balsa_app.sentbox ||
	mailbox == balsa_app.draftbox || mailbox == balsa_app.outbox) {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(tree_view, 3);

        gtk_tree_view_column_set_title(column, _("To"));
    }

    g_signal_connect(G_OBJECT(mailbox), "message-status-changed",
		     G_CALLBACK(mailbox_message_changed_status_cb),
		     (gpointer) index);
    g_signal_connect_swapped(G_OBJECT(mailbox), "message-new",
		       G_CALLBACK(mailbox_message_new_cb),
		       (gpointer) index);
    g_signal_connect_swapped(G_OBJECT(mailbox), "messages-new",
		       G_CALLBACK(mailbox_messages_new_cb),
		       (gpointer) index);
    g_signal_connect_swapped(G_OBJECT(mailbox), "message-delete",
		       G_CALLBACK(mailbox_message_delete_cb),
		       (gpointer) index);
    g_signal_connect_swapped(G_OBJECT(mailbox), "messages-delete",
		       G_CALLBACK(mailbox_messages_delete_cb),
		       (gpointer) index);

    /* do threading */
    bndx_set_sort_order(index, mbnode->sort_field, mbnode->sort_type);
    /* FIXME: this is an ugly way of doing it:
       override default mbost threading type with the global balsa
       default setting
    */
    balsa_index_set_threading_type(index, balsa_app.threading_type);

    gtk_idle_add((GtkFunction) moveto_handler, index);

    return FALSE;
}


static void
balsa_index_add(BalsaIndex * index, LibBalsaMessage * message)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    gchar buff1[32];
    gchar *text[7];
    gchar *name_str=NULL;
    GList *list;
    LibBalsaAddress *addy = NULL;
    LibBalsaMailbox* mailbox;
    gboolean append_dots;
    GtkTreePath *path;
    

    g_return_if_fail(index != NULL);
    g_return_if_fail(message != NULL);

    if (balsa_app.hide_deleted 
        && message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
        return;

    mailbox = index->mailbox_node->mailbox;
    
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

    text[3] = append_dots ? g_strconcat(name_str, ",...", NULL) : name_str;
    text[4] = (gchar*)LIBBALSA_MESSAGE_GET_SUBJECT(message);
    text[5] =
	libbalsa_message_date_to_gchar(message, balsa_app.date_string);
    text[6] =
	libbalsa_message_size_to_gchar(message, balsa_app.line_length);

    gtk_tree_store_insert_before(GTK_TREE_STORE(model), &iter, NULL, NULL);
    gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
                       BNDX_MESSAGE_COLUMN, message,
                       BNDX_INDEX_COLUMN, text[0],
                       BNDX_FROM_COLUMN, text[3],
                       BNDX_SUBJECT_COLUMN, text[4],
                       BNDX_DATE_COLUMN, text[5],
                       BNDX_SIZE_COLUMN, text[6],
                       BNDX_COLOR_COLUMN, NULL,
                       BNDX_WEIGHT_COLUMN, PANGO_WEIGHT_NORMAL,
                       -1);

    if(append_dots) g_free(text[3]);
    g_free(text[5]);
    g_free(text[6]);

    bndx_set_col_images(index, &iter, message);
    path = gtk_tree_model_get_path(model, &iter);
    bndx_set_parent_style(index, path);
    gtk_tree_path_free(path);
}

struct BndxFindInfo {
    LibBalsaMessage *message;
    GtkTreePath **path;
    GtkTreeIter *iter;
    gboolean valid;
};

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
 * bndx_find_func is a callback.
 */
static gboolean
bndx_find_func(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,
              gpointer data)
{
    struct BndxFindInfo *bfi = data;
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    if (message == bfi->message) {
        if (bfi->path)
            *bfi->path = gtk_tree_path_copy(path);
        if (bfi->iter)
            *bfi->iter = *iter;
        bfi->valid = TRUE;
    }

    return bfi->valid;
}

static gboolean
bndx_find_message(BalsaIndex * index, GtkTreePath ** path,
                  GtkTreeIter * iter, LibBalsaMessage *message)
{
    GtkTreeModel *model;
    struct BndxFindInfo bfi;

    if (!message)
        return FALSE;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    bfi.message = message;
    bfi.path = path;
    bfi.iter = iter;
    bfi.valid = FALSE;
    gtk_tree_model_foreach(model, bndx_find_func, &bfi);

    return bfi.valid;
}

static void
bndx_messages_remove(BalsaIndex * index, GList * messages)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GList *children = NULL;
    GList *list;
    LibBalsaMessage *next_message;

    g_return_if_fail(index != NULL);

    if (index->mailbox_node->mailbox == NULL)
        return;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));

    if (index->current_message &&
        !g_list_find(messages, index->current_message))
        next_message = index->current_message;
    else {
        bndx_find_row(index, NULL, &iter, 0, FILTER_NOOP, NULL, messages);
        if (iter.stamp)
            gtk_tree_model_get(model, &iter,
                               BNDX_MESSAGE_COLUMN, &next_message,
                               -1);
        else {
            gtk_tree_store_clear(GTK_TREE_STORE(model));
            return;
        }
    }

    for (list = messages; list; list = g_list_next(list)) {
        if (bndx_find_message(index, NULL, &iter, list->data)) {
            GtkTreeIter child_iter;

            /* make a list of children that need to be moved up */
            if (gtk_tree_model_iter_children(model, &child_iter, &iter)) {
                LibBalsaMessage *message;

                do {
                    gtk_tree_model_get(model, &child_iter, 
                                       BNDX_MESSAGE_COLUMN, &message,
                                       -1);
                    if (!g_list_find(messages, message))
                        children = g_list_prepend(children, message);
                } while (gtk_tree_model_iter_next(model, &child_iter));
            }
        } else
            /* a NULL message will be ignored */
            list->data = NULL;
    }

    /* move the children to the top level */
    for (list = children; list; list = g_list_next(list)) {
        GtkTreePath *path;
        if (bndx_find_message(index, &path, NULL, list->data)) {
            balsa_index_move_subtree(model, path, NULL, NULL);
            gtk_tree_path_free(path);
        }
    }
    g_list_free(children);

    /* remove the messages */
    for (list = messages; list; list = g_list_next(list))
        if (bndx_find_message(index, NULL, &iter, list->data))
            gtk_tree_store_remove(GTK_TREE_STORE(model), &iter);

    /* select next message */
    if (bndx_find_message(index, NULL, &iter, next_message)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
        gtk_tree_selection_unselect_all(selection);
        gtk_tree_selection_select_iter(selection, &iter);
    }
}


/* bndx_select_row ()
 * 
 * Takes care of the actual selection, unselecting other messages and
 * making sure the selected row is within bounds and made visible.
 * */
static void
bndx_select_row(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreeView *tree_view;
    GtkTreeSelection *selection;

    g_return_if_fail(index != NULL);
    g_return_if_fail(BALSA_IS_INDEX(index));

    tree_view = GTK_TREE_VIEW(index);
    selection = gtk_tree_view_get_selection(tree_view);
    gtk_tree_selection_unselect_all(selection);
    gtk_tree_selection_select_path(selection, path);

    bndx_scroll_to_row(index, path);
}

static void
bndx_scroll_to_row(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);

    /* We'd like to scroll just far enough to make the row visible,
     * but that's not yet implemented in GtkTreeView. */
    gtk_tree_view_scroll_to_cell(tree_view, path, NULL, TRUE, 0.5, 0);
}

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
 */

struct BndxScanInfo {
    GtkTreeView *tree_view;     /* the tree                     */
    LibBalsaMessage *message;   /* current message              */
    GHashTable *matching;       /* matching messages            */
    LibBalsaMessageFlag flag;   /* look only for matching nodes */
    GList *exclude;             /* messages excluded from scan  */
    /* we can use GtkTreeIter::stamp to decide validity         */
    GtkTreeIter first;          /* returns first matching       */
    GtkTreeIter prev;           /* returns last matching
                                   before selection             */
    GtkTreeIter next;           /* returns first matching
                                   after selection              */
    GtkTreeIter last;           /* returns last matching        */
};

/* balsa_index_scan_node:
 * callback for pre-recursive search for previous and next
 * after search:
 * - b->prev is last viewable message before current, NULL if none,
 *     NULL if no current message
 * - b->next is first viewable message after current, NULL if none,
 *     first message if no current message
 */
static gboolean
bndx_find_row_func(GtkTreeModel *model, GtkTreePath *path,
                   GtkTreeIter *iter, gpointer data)
{
    struct BndxScanInfo *b = data;
    LibBalsaMessage *message;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);

    if (message == b->message) {
        b->prev = b->last;
        b->next.stamp = 0;
        return FALSE;
    }

    if (b->flag) {
        /* looking for flagged messages */
        if (!(b->flag & message->flags))
            return FALSE;
    } else if (b->matching) {
        /* looking for messages that match some conditions */
        if (!g_hash_table_lookup(b->matching, message))
            return FALSE;
    } else if (b->exclude) {
        /* looking for messages not in the excluded list */
        if (g_list_find(b->exclude, message))
            return FALSE;
    } else {
        /* if there are no flags, no conditions, and no excluded list,
         * we want any viewable message */
        if (!bndx_row_is_viewable(b->tree_view, path))
            return FALSE;
    }

    if (!b->first.stamp)
        /* first matching message */
        b->first = *iter;
    if (!b->next.stamp)
        /* first message, or first after current */
        b->next = *iter;
    /* last becomes prev, which we always want to be viewable */
    if (bndx_row_is_viewable(b->tree_view, path))
        b->last = *iter;

    return FALSE;
}

/* bndx_expand_to_row_and_select:
 * make sure it's viewable, then pass it to bndx_select_row
 * no-op if it's NULL
 */
static void
bndx_expand_to_row_and_select(BalsaIndex * index, GtkTreeIter * iter)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreePath *path;

    if (!iter->stamp)
        return;
    path = gtk_tree_model_get_path(model, iter);
    bndx_expand_to_row(tree_view, path);
    bndx_select_row(index, path);
    gtk_tree_path_free(path);
}

/* bndx_find_row:
 * common search code--look for next or previous, with or without flag
 */
static void
bndx_find_row(BalsaIndex * index, GtkTreeIter * prev,
              GtkTreeIter * next, LibBalsaMessageFlag flag, gint op,
              GSList * conditions, GList * exclude)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    struct BndxScanInfo bi;

    g_return_if_fail(index != NULL);

    bi.tree_view = tree_view;
    bi.message = index->current_message;
    bi.flag = flag;
    bi.matching =
        conditions ? libbalsa_mailbox_get_matching(index->mailbox_node->
                                                   mailbox, op,
                                                   conditions) : NULL;
    bi.exclude = exclude;
    bi.prev.stamp = bi.next.stamp = bi.first.stamp = bi.last.stamp = 0;
    gtk_tree_model_foreach(model, bndx_find_row_func, &bi);
    if (bi.matching)
        g_hash_table_destroy(bi.matching);

    if (prev)
        *prev = bi.prev;
    if (next) {
        *next = bi.next;
        if (!bi.next.stamp) {
            /* not valid--look for another message */
            if (flag != 0)
                /* looking for new or flagged messages, cycle back to
                 * first matching */
                *next = bi.first;
            else if (exclude != NULL)
                /* for next message when deleting or moving, fall back to
                 * previous */
                *next = bi.prev;
        }
    }
}

void
balsa_index_select_next(BalsaIndex * index)
{
    GtkTreeIter iter;

    bndx_find_row(index, NULL, &iter, 0, FILTER_NOOP, NULL, NULL);
    bndx_expand_to_row_and_select(index, &iter);
}

void
balsa_index_select_previous(BalsaIndex * index)
{
    GtkTreeIter iter;

    bndx_find_row(index, &iter, NULL, 0, FILTER_NOOP, NULL, NULL);
    bndx_expand_to_row_and_select(index, &iter);
}

void
balsa_index_select_next_unread(BalsaIndex * index)
{
    GtkTreeIter iter;

    bndx_find_row(index, NULL, &iter, LIBBALSA_MESSAGE_FLAG_NEW,
                  FILTER_NOOP, NULL, NULL);
    bndx_expand_to_row_and_select(index, &iter);
}

void
balsa_index_select_next_flagged(BalsaIndex * index)
{
    GtkTreeIter iter;

    bndx_find_row(index, NULL, &iter, LIBBALSA_MESSAGE_FLAG_FLAGGED,
                  FILTER_NOOP, NULL, NULL);
    bndx_expand_to_row_and_select(index, &iter);
}

void
balsa_index_find(BalsaIndex * index, gint op, GSList * conditions,
                 gboolean previous)
{
    GtkTreeIter prev;
    GtkTreeIter next;

    bndx_find_row(index, &prev, &next, 0, op, conditions, NULL);
    bndx_expand_to_row_and_select(index, previous ? &prev : &next);
}

/* balsa_index_redraw_current redraws currently selected message,
   called when for example the message wrapping was switched on/off,
   the message canvas width has changed etc.
   FIXME: find a simpler way to do it.
*/
void
balsa_index_redraw_current(BalsaIndex * index)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(index));
    GtkTreeIter iter;

    if (bndx_find_message(index, NULL, &iter, index->current_message)) {
        gtk_tree_selection_unselect_iter(selection, &iter);
        gtk_tree_selection_select_iter(selection, &iter);
    }
}

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
        { LIBBALSA_MESSAGE_FLAG_REPLIED, BALSA_PIXMAP_INFO_REPLIED },
        { LIBBALSA_MESSAGE_FLAG_NEW,     BALSA_PIXMAP_INFO_NEW } };

    for(tmp=0; tmp<ELEMENTS(flags) && !(message->flags & flags[tmp].mask);
        tmp++);

    if (tmp < ELEMENTS(flags))
        status_pixbuf =
            gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
                                   flags[tmp].icon_name,
                                   GTK_ICON_SIZE_MENU, NULL);
    /* Alternatively, we could show an READ icon:
     * gtk_ctree_node_set_pixmap(ctree, node, 1,
     * balsa_icon_get_pixmap(BALSA_PIXMAP_INFO_READ),
     * balsa_icon_get_bitmap(BALSA_PIXMAP_INFO_READ)); 
     */

    if (libbalsa_message_has_attachment(message))
        attach_pixbuf =
            gtk_widget_render_icon(GTK_WIDGET(balsa_app.main_window),
                                   BALSA_PIXMAP_INFO_ATTACHMENT,
                                   GTK_ICON_SIZE_MENU, NULL);

    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                       BNDX_STATUS_COLUMN, status_pixbuf,
                       BNDX_ATTACH_COLUMN, attach_pixbuf,
                       -1);
}

static gboolean
thread_has_unread(GtkTreeView * tree_view, GtkTreePath * path)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreePath *child_path;
    GtkTreeIter iter;
    gboolean ret_val = FALSE;

    child_path = gtk_tree_path_copy(path);
    gtk_tree_path_down(child_path);

    while (gtk_tree_model_get_iter(model, &iter, child_path)) {
        LibBalsaMessage *message;

        gtk_tree_model_get(model, &iter, BNDX_MESSAGE_COLUMN, &message, -1);

        if ((message && message->flags & LIBBALSA_MESSAGE_FLAG_NEW) ||
            thread_has_unread(tree_view, child_path)) {
            ret_val = TRUE;
            break;
        }

        gtk_tree_path_next(child_path);
    }

    gtk_tree_path_free(child_path);
    return ret_val;
}


static gboolean
bndx_set_style(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeStore *store = GTK_TREE_STORE(model);
    GtkTreeIter iter;

    /* FIXME: Improve style handling;
              - Consider storing styles locally, with or config setting
	        separate from the "mailbox" one.  */
    
    if (!gtk_tree_model_get_iter(model, &iter, path))
        return FALSE;

    if (!gtk_tree_view_row_expanded(tree_view, path)
        && thread_has_unread(tree_view, path)) {
        gtk_tree_store_set(store, &iter,
                           BNDX_COLOR_COLUMN, &balsa_app.mblist_unread_color,
                           BNDX_WEIGHT_COLUMN, PANGO_WEIGHT_BOLD,
                           -1);
    } else
        gtk_tree_store_set(store, &iter,
                           BNDX_COLOR_COLUMN, NULL,
                           BNDX_WEIGHT_COLUMN, PANGO_WEIGHT_NORMAL,
                           -1);

    return TRUE;
}

static gboolean
bndx_set_style_recursive(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreePath *child_path;

    if (gtk_tree_path_get_depth(path) > 0 && !bndx_set_style(index, path))
        return FALSE;

    child_path = gtk_tree_path_copy(path);
    gtk_tree_path_down(child_path);

    while (bndx_set_style_recursive(index, child_path))
        gtk_tree_path_next(child_path);

    gtk_tree_path_free(child_path);
    return TRUE;
}


static void
bndx_set_parent_style(BalsaIndex * index, GtkTreePath * path)
{
    GtkTreePath *parent_path;

    parent_path = gtk_tree_path_copy(path);

    while (gtk_tree_path_up(parent_path)
           && gtk_tree_path_get_depth(parent_path) > 0)
	bndx_set_style(index, parent_path);

    gtk_tree_path_free(parent_path);
}

/* GtkTree* callbacks */

/*
 * bndx_selection_changed
 *
 * Callback for the selection "changed" signal.
 *
 * Display the last (in tree order) selected message.
 */

static void
bndx_selection_changed_func(GtkTreeModel * model, GtkTreePath * path,
                            GtkTreeIter * iter, gpointer data)
{
    LibBalsaMessage **message = data;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, message, -1);
}

static void
bndx_selection_changed(GtkTreeSelection * selection, gpointer data)
{
    BalsaIndex *index = BALSA_INDEX(data);
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
    LibBalsaMessage *message = NULL;

    gtk_tree_selection_selected_foreach(selection,
                                        bndx_selection_changed_func,
                                        &message);

    /* we don't clear the current message if the tree contains any
     * messages */
    if ((message || gtk_tree_model_iter_n_children(model, NULL) == 0)
        && message != index->current_message)
        bndx_set_current_message(index, message);
}

static gboolean
bndx_button_event_press_cb(GtkWidget * widget, GdkEventButton * event,
                           gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath *path;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    BalsaIndex *index = BALSA_INDEX(widget);

    g_return_val_if_fail(event, FALSE);
    if (event->button != 3)
        return FALSE;

    /* pop up the context menu:
     * - if the clicked-on message is already selected, don't change
     *   the selection;
     * - if it isn't, select it (cancelling any previous selection)
     * - then create and show the menu */
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree_view),
                                      event->x, event->y, &path,
                                      NULL, NULL, NULL)) {
        if (!gtk_tree_selection_path_is_selected(selection, path))
            bndx_select_row(index, path);
        gtk_tree_path_free(path);
    }

    gtk_menu_popup(GTK_MENU(bndx_popup_menu_prepare(index)),
                   NULL, NULL, NULL, NULL,
                   event->button, event->time);

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
            sendmsg_window_new(GTK_WIDGET(balsa_app.main_window),
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

    /* if current message has become viewable, reselect it */
    if (bndx_find_message(index, &current_path, NULL,
                          index->current_message)) {
        if (!gtk_tree_selection_path_is_selected(selection, current_path)
            && bndx_row_is_viewable(tree_view, current_path)) {
            g_signal_handlers_block_by_func(selection,
                                            G_CALLBACK
                                            (bndx_selection_changed),
                                            index);
            gtk_tree_selection_select_path(selection, current_path);
            g_signal_handlers_unblock_by_func(selection,
                                              G_CALLBACK
                                              (bndx_selection_changed),
                                              index);
        } else {
            gtk_tree_path_free(current_path);
            current_path = NULL;
        }
    }

    /* scroll to current message if it became viewable, or to child of
     * expanded row otherwise */
    if (!current_path) {
        current_path = gtk_tree_path_copy(path);
        gtk_tree_path_down(current_path);
    }
    bndx_scroll_to_row(index, current_path);
    gtk_tree_path_free(current_path);

    bndx_set_style(BALSA_INDEX(tree_view), path);
}

/* bndx_tree_collapse_cb:
 * callback on collapse events
 * set/reset unread style, as appropriate */
static void
bndx_tree_collapse_cb(GtkTreeView * tree_view, GtkTreeIter * iter,
                      GtkTreePath * path, gpointer user_data)
{
    bndx_set_style(BALSA_INDEX(tree_view), path);
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

/* bndx_changed_idle:
 *
 * idle handler for signalling main-window that the index has changed,
 * and for setting icons in the tree
 */
static gboolean
bndx_changed_idle(gpointer data)
{
    BalsaIndex *index = data;
    GSList *list;

    index->changed_idle_id = 0;
    gdk_threads_enter();

    /* signal main-window only if we're the current index */
    if ((GtkWidget *) index ==
        balsa_window_find_current_index(BALSA_WINDOW(index->window)))
    {
        GtkTreeModel *model;
        GtkTreeIter prev, next;

        model = gtk_tree_view_get_model(GTK_TREE_VIEW(index));
        bndx_find_row(index, &prev, &next, 0, FILTER_NOOP, NULL, NULL);

        if (prev.stamp)
            gtk_tree_model_get(model, &prev,
                               BNDX_MESSAGE_COLUMN,
                               &index->previous_message,
                               -1);
        else
            index->previous_message = NULL;

        if (next.stamp)
            gtk_tree_model_get(model, &next,
                               BNDX_MESSAGE_COLUMN,
                               &index->next_message,
                               -1);
        else
            index->next_message = NULL;

        g_signal_emit(G_OBJECT(index), balsa_index_signals[INDEX_CHANGED],
                      0);
    }

    /* set icons */
    for (list = index->update_flag_list; list; list = g_slist_next(list))
        balsa_index_update_flag(index, list->data);
    g_slist_free(index->update_flag_list);
    index->update_flag_list = NULL;

    gdk_threads_leave();
    return FALSE;
}

/* bndx_changed:
 *
 * called whenever the index has changed and we need to signal
 * main-window; an idle callback does the actual signalling, to avoid
 * mailbox deadlocks, and to avoid multiple signals.
 */

static void
bndx_changed(BalsaIndex * index)
{
    if (index->changed_idle_id || index->sync_backend_idle_id)
        return;

    index->changed_idle_id = gtk_idle_add(bndx_changed_idle, index);
}

/* bndx_sync_backend_idle:
 *
 * idle handler for calling libbalsa_mailbox_sync_backend.
 */

static gboolean
bndx_sync_backend_idle(gpointer data)
{
    BalsaIndex *index = data;

    index->sync_backend_idle_id = 0;
    gdk_threads_enter();
    if (index && BALSA_IS_INDEX(index)) {
        libbalsa_mailbox_sync_backend(index->mailbox_node->mailbox,
                                      balsa_app.delete_immediately);
    }
    gdk_threads_leave();

    return FALSE;
}

/* Mailbox Callbacks... */
/* mailbox_message_changed_status_cb:
   We must be *extremely* careful here - message might have changed 
   its status because the mailbox was forcibly closed and message
   became invalid. See for example #70807.

*/
static void
mailbox_message_changed_status_cb(LibBalsaMailbox * mb,
                                  LibBalsaMessage * message,
                                  BalsaIndex * index)
{
    if (!libbalsa_mailbox_is_valid(mb))
        return;

    index->update_flag_list =
        g_slist_prepend(index->update_flag_list, message);

    if (!((message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
          && (balsa_app.hide_deleted || balsa_app.delete_immediately))) {
        bndx_changed(index);
        return;
    }

    /* one or more messages must be removed from the index, so we
     * schedule an idle handler to call libbalsa_sync_backend, which
     * will in turn signal the index */
    /* first cancel any index-changed idle */
    if (index->changed_idle_id) {
        gtk_idle_remove(index->changed_idle_id);
        index->changed_idle_id = 0;
    }
    /* schedule the sync-backend idle, if one isn't pending */
    if (!index->sync_backend_idle_id)
        index->sync_backend_idle_id =
            gtk_idle_add(bndx_sync_backend_idle, index);
}

static void
mailbox_message_new_cb(BalsaIndex * index, LibBalsaMessage * message)
{
    GList *list = g_list_prepend(NULL, message);
    mailbox_messages_new_cb(index, list);
    g_list_free_1(list);
}

static void
mailbox_messages_new_cb(BalsaIndex * index, GList * messages)
{
    LibBalsaMessage *message;

    while (messages) {
        message = (LibBalsaMessage *) (messages->data);
        balsa_index_add(index, message);
        messages = g_list_next(messages);
    }

    balsa_index_threading(index);
    balsa_mblist_update_mailbox(balsa_app.mblist_tree_store,
                                index->mailbox_node->mailbox);
    bndx_changed(index);
}

static void
mailbox_message_delete_cb(BalsaIndex * index, LibBalsaMessage * message)
{
    GList *messages = g_list_prepend(NULL, message);

    bndx_messages_remove(index, messages);
    g_list_free_1(messages);
    bndx_check_visibility(index);
}

static void
mailbox_messages_delete_cb(BalsaIndex * index, GList * messages)
{
    bndx_messages_remove(index, messages);
    bndx_check_visibility(index);
}

/* balsa_index_close_and_destroy:
 */
static void
bndx_preview_idle_remove(BalsaIndex * index)
{
    if (index->preview_idle_id) {
        gtk_idle_remove(index->preview_idle_id);
        index->preview_idle_id = 0;
    }
    if (index->preview_message) {
        g_object_unref(index->preview_message);
        index->preview_message = NULL;
    }
}

static void
balsa_index_close_and_destroy(GtkObject * obj)
{
    BalsaIndex *index;
    LibBalsaMailbox* mailbox;

    g_return_if_fail(obj != NULL);
    index = BALSA_INDEX(obj);

    /* remove idle callbacks and attached data */
    bndx_preview_idle_remove(index);
    if (index->sync_backend_idle_id) {
        gtk_idle_remove(index->sync_backend_idle_id);
        index->sync_backend_idle_id = 0;
    }
    if (index->changed_idle_id) {
        gtk_idle_remove(index->changed_idle_id);
        index->changed_idle_id = 0;
    }
    if (index->update_flag_list) {
        g_slist_free(index->update_flag_list);
        index->update_flag_list = NULL;
    }

    /*page->window references our owner */
    if (index->mailbox_node && (mailbox = index->mailbox_node->mailbox) ) {
        g_signal_handlers_disconnect_matched(G_OBJECT(mailbox),
                                             G_SIGNAL_MATCH_DATA,
                                             0, 0, NULL, NULL,
                                             index);
	libbalsa_mailbox_close(mailbox);
	index->mailbox_node = NULL;
    }

    /* destroy the popup menu */
    gtk_widget_destroy(index->popup_menu);
    index->popup_menu = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
        (*GTK_OBJECT_CLASS(parent_class)->destroy) (obj);
}

static void
bndx_view_source_func(GtkTreeModel *model, GtkTreePath *path,
                      GtkTreeIter *iter, gpointer data)
{
    LibBalsaMessage *message = NULL;

    gtk_tree_model_get(model, iter, BNDX_MESSAGE_COLUMN, &message, -1);
    libbalsa_show_message_source(message);
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

    if(messages) {
	if (move_to_trash && (index != trash)) {
	    libbalsa_messages_move(messages, balsa_app.trash);
	    enable_empty_trash(TRASH_FULL);
	} else {
	    libbalsa_messages_delete(messages);
	    if (index == trash)
		enable_empty_trash(TRASH_CHECK);
	}
	g_list_free(messages);

        /* Update the style and message counts in the mailbox list */
        balsa_mblist_update_mailbox(balsa_app.mblist_tree_store,
                                    index->mailbox_node->mailbox);
        /* balsa_index_redraw_current(index); */
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
    GList *list, *l;
    LibBalsaMessage *message;
    BalsaIndex* index;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);

    index = BALSA_INDEX (user_data);

    l = balsa_index_selected_list(index);
    for (list = l; list; list = g_list_next(list)) {
	message = list->data;
	libbalsa_message_delete(message, FALSE);
    }
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
                          void(*cb)(LibBalsaMessage*, gboolean))
{
    GList *list, *l;
    LibBalsaMessage *message;
    int is_all_flagged = TRUE;
    gboolean new_flag;

    /* First see if we should set given flag or unset */
    l = balsa_index_selected_list(index);
    for (list = l; list; list = g_list_next(list)) {
	message = list->data;
	if (!(message->flags & flag)) {
	    is_all_flagged = FALSE;
	    break;
	}
    }
    g_list_free(l);

    /* If they all have the flag set, then unset them. Otherwise, set
     * them all.
     * Note: the callback for `toggle unread' changes the `read' flag,
     * but the callback for `toggle flagged' changes `flagged'
     */

    new_flag =
        (flag ==
         LIBBALSA_MESSAGE_FLAG_NEW ? is_all_flagged : !is_all_flagged);

    l = balsa_index_selected_list(index);
    for (list = l; list; list = g_list_next(list)) {
	message = list->data;
        (*cb) (message, new_flag);
    }
    g_list_free(l);
}

/* This function toggles the FLAGGED attribute of a list of messages
 */
void
balsa_message_toggle_flagged(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_message_toggle_flag(BALSA_INDEX(user_data), 
                              LIBBALSA_MESSAGE_FLAG_FLAGGED,
                              libbalsa_message_flag);
}


/* This function toggles the NEW attribute of a list of messages
 */
void
balsa_message_toggle_new(GtkWidget * widget, gpointer user_data)
{
    g_return_if_fail(user_data != NULL);

    balsa_message_toggle_flag(BALSA_INDEX(user_data), 
                              LIBBALSA_MESSAGE_FLAG_NEW,
                              libbalsa_message_read);
}


/* balsa_index_update_message:
   update preview window to currently selected message of index.
*/
void
balsa_index_update_message(BalsaIndex * index)
{
    guint i;
    GtkWidget *page;

    /* we're called from the notebook-switch-page callback, so we must
     * clear any pending idle callbacks */
    for (i = 0; (page =
                 gtk_notebook_get_nth_page(GTK_NOTEBOOK
                                           (balsa_app.notebook),
                                           i)) != NULL; ++i) {
        GtkWidget *child = gtk_bin_get_child(GTK_BIN(page));
        if (child != (GtkWidget *) index)
            bndx_preview_idle_remove(BALSA_INDEX(child));
    }

    bndx_set_current_message(index, index->current_message);
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
    GtkTreeView *tree_view = NULL;
    

    g_return_if_fail (widget != NULL);
    tree_view = GTK_TREE_VIEW(widget);
    
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


/* idle_handler_cb:
 * This is an idle handler, be sure to call use gdk_threads_{enter/leave}
 * No assumptions should be made about validity of the attached_data.
 */
static gboolean
idle_handler_cb(GtkWidget * widget)
{
    BalsaMessage *bmsg;
    BalsaIndex* index;
    /* gpointer data; */

    if (!widget)
        return FALSE;

    gdk_threads_enter();
    
    index = BALSA_INDEX(widget);
    if (!index->preview_idle_id) {
	gdk_threads_leave();
	return FALSE;
    }

    /* get the preview pane from the index page's BalsaWindow parent */
    bmsg = BALSA_MESSAGE (BALSA_WINDOW (index->window)->preview);

    if (bmsg && BALSA_MESSAGE (bmsg)) {
	if (index->preview_message) {
	    if(!balsa_message_set(BALSA_MESSAGE (bmsg),
                                  index->preview_message))
		balsa_information
		    (LIBBALSA_INFORMATION_ERROR,
		     _("Cannot access the message's body\n"));
	}
	else
	    balsa_message_clear(BALSA_MESSAGE (bmsg));
    }
    
    /* Update the style and message counts in the mailbox list */
    if(index->mailbox_node)
	balsa_mblist_update_mailbox(balsa_app.mblist_tree_store, 
				    index->mailbox_node->mailbox);

    bndx_preview_idle_remove(index);
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
        GNOME_STOCK_BOOK_OPEN, N_("View Source"),
                GTK_SIGNAL_FUNC(bndx_view_source)}, {
        BALSA_PIXMAP_MENU_REPLY, N_("Reply..."),
                GTK_SIGNAL_FUNC(balsa_message_reply)}, {
        BALSA_PIXMAP_MENU_REPLY_ALL, N_("Reply To All..."),
                GTK_SIGNAL_FUNC(balsa_message_replytoall)}, {
        BALSA_PIXMAP_MENU_REPLY_GROUP, N_("Reply To Group..."),
                GTK_SIGNAL_FUNC(balsa_message_replytogroup)}, {
        BALSA_PIXMAP_MENU_FORWARD, N_("Forward Attached..."),
                GTK_SIGNAL_FUNC(balsa_message_forward_attached)}, {
        BALSA_PIXMAP_MENU_FORWARD, N_("Forward Inline..."),
                GTK_SIGNAL_FUNC(balsa_message_forward_inline)}, {
        GNOME_STOCK_BOOK_RED, N_("Store Address..."),
                GTK_SIGNAL_FUNC(balsa_store_address)}};
    GtkWidget *menu, *menuitem, *submenu;
    unsigned i;

    menu = gtk_menu_new();

    for (i = 0; i < ELEMENTS(entries); i++)
        create_stock_menu_item(menu, entries[i].icon, _(entries[i].label),
                               entries[i].func, index);

    index->delete_item =
        create_stock_menu_item(menu, GNOME_STOCK_TRASH,
                               _("Delete"),
                               GTK_SIGNAL_FUNC(balsa_message_delete),
                               index);
    index->undelete_item =
        create_stock_menu_item(menu, GTK_STOCK_UNDELETE,
                               _("Undelete"),
                               GTK_SIGNAL_FUNC(balsa_message_undelete),
                               index);
    index->move_to_trash_item =
        create_stock_menu_item(menu, GNOME_STOCK_TRASH,
                               _("Move To Trash"),
                               GTK_SIGNAL_FUNC
                               (balsa_message_move_to_trash), index);

    menuitem = gtk_menu_item_new_with_label(_("Toggle"));
    submenu = gtk_menu_new();
    create_stock_menu_item(submenu, BALSA_PIXMAP_MENU_FLAGGED,
                           _("Flagged"),
                           GTK_SIGNAL_FUNC(balsa_message_toggle_flagged),
                           index);
    create_stock_menu_item(submenu, BALSA_PIXMAP_MENU_NEW, _("Unread"),
                           GTK_SIGNAL_FUNC(balsa_message_toggle_new),
                           index);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_label(_("Move to"));
    index->move_to_item = menuitem;
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    return menu;
}

/*
 * bndx_popup_menu_prepare: set sensitivity of menuitems on the popup
 * menu, and populate the mru submenu
 */
static GtkWidget *
bndx_popup_menu_prepare(BalsaIndex * index)
{
    GtkWidget *menu = index->popup_menu;
    GtkWidget *submenu;
    LibBalsaMailbox* mailbox;
    GList *list, *l;
    gboolean any;
    gboolean any_deleted = FALSE;
    gboolean any_not_deleted = FALSE;
 
    BALSA_DEBUG();

    l = balsa_index_selected_list(index);
    for (list = l; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;

        if (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
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

    submenu = balsa_mblist_mru_menu(GTK_WINDOW(balsa_app.main_window),
                                    &balsa_app.folder_mru,
                                    G_CALLBACK(mru_menu_cb),
                                    index);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(index->move_to_item),
                              submenu);

    gtk_widget_show_all(menu);

    return menu;
}

static GtkWidget *
create_stock_menu_item(GtkWidget * menu, const gchar * type,
		       const gchar * label, GtkSignalFunc cb,
		       gpointer data)
{
#if BALSA_MAJOR < 2
    GtkWidget *menuitem = gnome_stock_menu_item(type, label);
#else
    GtkWidget *menuitem = gtk_image_menu_item_new_with_label(label);
    GtkWidget *image = gtk_image_new_from_stock(type, GTK_ICON_SIZE_MENU);

    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menuitem), image);
#endif                          /* BALSA_MAJOR < 2 */

    g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(cb), data);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    return menuitem;
}

static void
balsa_index_transfer_messages(BalsaIndex * index,
                              LibBalsaMailbox * mailbox)
{
    GList *messages;

    g_return_if_fail(index != NULL);
    if (mailbox == NULL)
        return;

    /*Transferring to same mailbox? */
    if (index->mailbox_node->mailbox == mailbox)
	return;

    messages = balsa_index_selected_list(index);
    balsa_index_transfer(index, messages, mailbox, FALSE);
    g_list_free(messages);
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
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreePath *path;
    GtkTreeIter iter;

    g_signal_handlers_block_by_func(GTK_OBJECT(index),
                                    G_CALLBACK(expand ?
                                               bndx_tree_expand_cb :
                                               bndx_tree_collapse_cb),
                                    NULL);
    g_signal_handlers_block_by_func(selection,
                                    G_CALLBACK(bndx_selection_changed),
                                    index);

    if (expand)
        gtk_tree_view_expand_all(tree_view);
    else
        gtk_tree_view_collapse_all(tree_view);

    path = gtk_tree_path_new();
    bndx_set_style_recursive(index, path);     /* chbm */
    gtk_tree_path_free(path);

    if (bndx_find_message(index, &path, &iter, index->current_message)) {
        if (!expand)           /* Re-expand msg_node's thread; cf. Remarks */
            bndx_expand_to_row_and_select(index, &iter);
        else
            bndx_scroll_to_row(index, path);
        gtk_tree_path_free(path);
    }

    g_signal_handlers_unblock_by_func(selection,
                                      G_CALLBACK(bndx_selection_changed),
                                      index);
    g_signal_handlers_unblock_by_func(G_OBJECT(index),
                                      G_CALLBACK(expand ?
                                                 bndx_tree_expand_cb :
                                                 bndx_tree_collapse_cb),
                                      NULL);
}

/* balsa_index_set_threading_type:
   FIXME: balsa_index_threading() requires that the index has been freshly
   recreated. This should not be necessary.
*/
void
balsa_index_set_threading_type(BalsaIndex * index, int thtype)
{
    GList *list;
    LibBalsaMailbox* mailbox = NULL;

    g_return_if_fail (index);
    g_return_if_fail (index->mailbox_node != NULL);
    g_return_if_fail (index->mailbox_node->mailbox != NULL);

    index->threading_type = thtype;
    index->mailbox_node->threading_type = thtype;
    
    gtk_tree_store_clear(GTK_TREE_STORE
                         (gtk_tree_view_get_model(GTK_TREE_VIEW(index))));
    
    mailbox = index->mailbox_node->mailbox;
    for (list = mailbox->message_list; list; list = list->next)
	balsa_index_add(index, list->data);
    /* do threading */
    balsa_index_threading(index);
    /* expand tree if specified in config */
    balsa_index_update_tree(index, balsa_app.expand_tree);

    /* set the menu apriopriately */
    balsa_window_set_threading_menu(thtype);
}

static void
bndx_set_sort_order(BalsaIndex * index, int col_id, GtkSortType order)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(index);
    GtkTreeSortable *sortable =
        GTK_TREE_SORTABLE(gtk_tree_view_get_model(tree_view));
    GtkTreeViewColumn *column =
        gtk_tree_view_get_column(tree_view, col_id);

    g_return_if_fail(index->mailbox_node);
    g_return_if_fail(col_id >= 0 && col_id <= 6);
    g_return_if_fail(order == GTK_SORT_DESCENDING || 
		     order == GTK_SORT_ASCENDING);

    index->mailbox_node->sort_field = col_id;
    index->mailbox_node->sort_type  = order;

    gtk_tree_sortable_set_sort_column_id(sortable, col_id, order);
    gtk_tree_view_column_set_sort_indicator(column, TRUE);
    gtk_tree_view_column_set_sort_order(column, order);
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

/* idle handler wrappers */

/* bndx_set_current_message:
 * first remove any existing handler, then set a new
 * one
 */
static void
bndx_set_current_message(BalsaIndex * index, LibBalsaMessage * message)
{
    gboolean switch_page = (message == index->current_message);

    index->current_message = message;
    bndx_changed(index);

    if (!balsa_app.previewpane)
        return;

    bndx_preview_idle_remove(index);

    /* update the preview only if the notebook page was switched,
     * or we're the current index */
    if (switch_page
        || (GtkWidget *) index ==
        balsa_window_find_current_index(BALSA_WINDOW(index->window))) {
        index->preview_message = message;
        if (message)
            g_object_ref(message);

        index->preview_idle_id =
            gtk_idle_add((GtkFunction) idle_handler_cb, index);
    }
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
        hide_deleted(BALSA_INDEX(index), hide);
    }
}

/* hide_deleted:
 * hide (or show, if hide is FALSE) deleted messages.
 */
static void
hide_deleted(BalsaIndex * index, gboolean hide)
{
    LibBalsaMailbox *mailbox = index->mailbox_node->mailbox;
    GList *list;
    GList *messages = NULL;

    for (list = mailbox->message_list; list; list = g_list_next(list)) {
        LibBalsaMessage *message = list->data;

        if (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
            messages = g_list_prepend(messages, message);
    }

    if (hide)
        mailbox_messages_delete_cb(index, messages);
    else
        mailbox_messages_new_cb(index, messages);

    g_list_free(messages);
}

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
    balsa_mblist_update_mailbox(balsa_app.mblist_tree_store,
                                to_mailbox);

    if (from_mailbox == balsa_app.trash && !copy)
        enable_empty_trash(TRASH_CHECK);
    else if (to_mailbox == balsa_app.trash)
        enable_empty_trash(TRASH_FULL);
}

static gboolean
bndx_row_is_viewable(GtkTreeView * tree_view, GtkTreePath * path)
{
    GtkTreePath *tmp_path = gtk_tree_path_copy(path);
    gboolean ret_val = TRUE;

    while (gtk_tree_path_up(tmp_path)
           && gtk_tree_path_get_depth(tmp_path) > 0)
        if (!gtk_tree_view_row_expanded(tree_view, tmp_path))
            ret_val = FALSE;

    gtk_tree_path_free(tmp_path);
    return ret_val;
}

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
        case 0:
            return numeric_compare(m1, m2);
        case 5:
            return date_compare(m1, m2);
        case 6:
            return size_compare(m1, m2);
        default:
            return 0;
    }
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

static GNode *
bndx_make_tree(GtkTreeModel * model, GtkTreeIter * iter)
{
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    GtkTreeRowReference *reference =
        gtk_tree_row_reference_new(model, path);
    GNode *node = g_node_new(reference);
    GtkTreeIter child_iter;
    
    if (gtk_tree_model_iter_children(model, &child_iter, iter))
        do
            g_node_prepend(node, bndx_make_tree(model, &child_iter));
        while (gtk_tree_model_iter_next(model, &child_iter));

    gtk_tree_path_free(path);
    return node;
}

static void
bndx_copy_tree(GNode * node, GtkTreeModel * model,
               GtkTreePath * parent_path, GHashTable * ref_table)
{
    GtkTreeRowReference *reference = node->data;
    GtkTreePath *path;
    GNode *child_node;
    LibBalsaMessage *message;
    gchar *index, *from, *subject, *date, *size;
    GdkPixbuf *status, *attach;
    GdkColor *color;
    PangoWeight weight;
    GtkTreeIter iter, parent_iter, new_iter;

    path = gtk_tree_row_reference_get_path(reference);
    gtk_tree_row_reference_free(reference);
    gtk_tree_model_get_iter(model, &iter, path);

    gtk_tree_model_get(model, &iter,
                       BNDX_MESSAGE_COLUMN, &message,
                       BNDX_INDEX_COLUMN, &index,
                       BNDX_STATUS_COLUMN, &status,
                       BNDX_ATTACH_COLUMN, &attach,
                       BNDX_FROM_COLUMN, &from,
                       BNDX_SUBJECT_COLUMN, &subject,
                       BNDX_DATE_COLUMN, &date,
                       BNDX_SIZE_COLUMN, &size,
                       BNDX_COLOR_COLUMN, &color,
                       BNDX_WEIGHT_COLUMN, &weight, -1);

    if (parent_path && gtk_tree_path_get_depth(parent_path) > 0) {
        /* create a new child row of parent_path */
        gtk_tree_model_get_iter(model, &parent_iter, parent_path);
        gtk_tree_store_append(GTK_TREE_STORE(model), &new_iter,
                              &parent_iter);
    } else {
        /* create a new row at the top level, immediately after the
         * top of the thread on which we're working */
        while (gtk_tree_path_get_depth(path) > 1)
            gtk_tree_path_up(path);
        gtk_tree_model_get_iter(model, &parent_iter, path);
        gtk_tree_store_insert_after(GTK_TREE_STORE(model), &new_iter,
                                    NULL, &parent_iter);
    }
    gtk_tree_path_free(path);

    gtk_tree_store_set(GTK_TREE_STORE(model), &new_iter,
                       BNDX_MESSAGE_COLUMN, message,
                       BNDX_INDEX_COLUMN, index,
                       BNDX_STATUS_COLUMN, status,
                       BNDX_ATTACH_COLUMN, attach,
                       BNDX_FROM_COLUMN, from,
                       BNDX_SUBJECT_COLUMN, subject,
                       BNDX_DATE_COLUMN, date,
                       BNDX_SIZE_COLUMN, size,
                       BNDX_COLOR_COLUMN, color,
                       BNDX_WEIGHT_COLUMN, weight, -1);

    path = gtk_tree_model_get_path(model, &new_iter);
    if (ref_table)
        g_hash_table_replace(ref_table, message,
                             gtk_tree_row_reference_new(model, path));

    for (child_node = node->children; child_node;
         child_node = child_node->next)
        bndx_copy_tree(child_node, model, path, ref_table);

    gtk_tree_path_free(path);
}

void
balsa_index_move_subtree(GtkTreeModel * model, GtkTreePath * root, 
                         GtkTreePath * new_parent, GHashTable * ref_table)
{
    GtkTreeIter root_iter;
    GNode *node;

    gtk_tree_model_get_iter(model, &root_iter, root);
    node = bndx_make_tree(model, &root_iter);
    bndx_copy_tree(node, model, new_parent, ref_table);
    g_node_destroy(node);
    gtk_tree_store_remove(GTK_TREE_STORE(model), &root_iter);
}
