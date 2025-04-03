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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-mblist.h"

/* #include <gtk/gtkfeatures.h> */
#include <string.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "libbalsa.h"
#include <glib/gi18n.h>
#include "mailbox-node.h"
#include "main-window.h"

/* Column numbers (used for sort_column_id): */
typedef enum {
    BMBL_TREE_COLUMN_NAME = 1,
    BMBL_TREE_COLUMN_UNREAD,
    BMBL_TREE_COLUMN_TOTAL
} BmblTreeColumnId;

/* object arguments */
enum {
    PROP_0,
    PROP_SHOW_CONTENT_INFO
};

/* Drag and Drop stuff */
enum {
    TARGET_MESSAGES
};

/* tree model columns */
enum {
    MBNODE_COLUMN = 0,         /* we use 0 in other code */
    ICON_COLUMN,
    NAME_COLUMN,
    WEIGHT_COLUMN,
    STYLE_COLUMN,
    UNREAD_COLUMN,
    TOTAL_COLUMN,
    N_COLUMNS
};

/* signals */

enum {
    HAS_UNREAD_MAILBOX,
    LAST_SIGNAL
};
static gint balsa_mblist_signals[LAST_SIGNAL] = { 0 };

static GtkTargetEntry bmbl_drop_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

/* class methods */
static void bmbl_set_property(GObject * object, guint prop_id,
                              const GValue * value, GParamSpec * pspec);
static void bmbl_get_property(GObject * object, guint prop_id,
                              GValue * value, GParamSpec * pspec);
static gboolean bmbl_drag_motion(GtkWidget * mblist,
                                 GdkDragContext * context, gint x, gint y,
                                 guint time);
static gboolean bmbl_popup_menu(GtkWidget * widget);
static void bmbl_select_mailbox(GtkTreeSelection * selection,
                                gpointer data);
static gboolean bmbl_selection_func(GtkTreeSelection * selection,
                                    GtkTreeModel * model,
                                    GtkTreePath * path,
                                    gboolean path_currently_selected,
                                    gpointer data);

/* callbacks */
static void bmbl_tree_expand(GtkTreeView * tree_view, GtkTreeIter * iter,
                             GtkTreePath * path, gpointer data);
static void bmbl_tree_collapse(GtkTreeView * tree_view, GtkTreeIter * iter,
                               GtkTreePath * path, gpointer data);
static gint bmbl_row_compare(GtkTreeModel * model,
                             GtkTreeIter * iter1,
                             GtkTreeIter * iter2, gpointer data);
static void bmbl_column_resize(GtkWidget * widget,
                               GtkAllocation * allocation, gpointer data);
static void bmbl_drag_cb(GtkWidget * widget, GdkDragContext * context,
                         gint x, gint y,
                         GtkSelectionData * selection_data, guint info,
                         guint32 time, gpointer data);
static void bmbl_row_activated_cb(GtkTreeView * tree_view,
                                  GtkTreePath * path,
                                  GtkTreeViewColumn * column,
                                  gpointer data);
static void bmbl_mailbox_changed_cb(LibBalsaMailbox * mailbox,
				    gpointer data);
/* helpers */
static gboolean bmbl_find_all_unread_mboxes_func(GtkTreeModel * model,
                                                 GtkTreePath * path,
                                                 GtkTreeIter * iter,
                                                 gpointer data);
static void bmbl_real_disconnect_mbnode_signals(BalsaMailboxNode * mbnode,
					      GtkTreeModel * model);
static gboolean bmbl_store_redraw_mbnode(GtkTreeIter * iter,
					 BalsaMailboxNode * mbnode);
static void bmbl_node_style(GtkTreeModel * model, GtkTreeIter * iter);
static gint bmbl_core_mailbox(LibBalsaMailbox * mailbox);
static void bmbl_do_popup(GtkTreeView    *tree_view,
                          GtkTreePath    *path,
                          const GdkEvent *event);
static void bmbl_expand_to_row(BalsaMBList * mblist, GtkTreePath * path);
/* end of prototypes */

/* class methods */

struct _BalsaMBList {
    GtkTreeView tree_view;

    /* shall the number of messages be displayed ? */
    gboolean display_info;
    /* signal handler id */
    gulong toggled_handler_id;

    /* to set sort order in an idle callback */
    gint  sort_column_id;
    guint sort_idle_id;
};

G_DEFINE_TYPE(BalsaMBList, balsa_mblist, GTK_TYPE_TREE_VIEW)

static void
balsa_mblist_class_init(BalsaMBListClass * klass)
{
    GObjectClass *object_class;
    GtkWidgetClass *widget_class;

    object_class = (GObjectClass *) klass;
    widget_class = (GtkWidgetClass *) klass;

    /* HAS_UNREAD_MAILBOX is emitted when the number of mailboxes with
     * unread mail might have changed. */
    balsa_mblist_signals[HAS_UNREAD_MAILBOX] =
        g_signal_new("has-unread-mailbox",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     0,
                     NULL, NULL,
                     NULL,
                     G_TYPE_NONE,
                     1, G_TYPE_INT);

    /* GObject signals */
    object_class->set_property = bmbl_set_property;
    object_class->get_property = bmbl_get_property;

    /* GtkWidget signals */
    widget_class->drag_motion = bmbl_drag_motion;
    widget_class->popup_menu = bmbl_popup_menu;

    /* Properties */
    g_object_class_install_property(object_class, PROP_SHOW_CONTENT_INFO,
                                    g_param_spec_boolean
                                    ("show_content_info", NULL, NULL,
                                     FALSE, G_PARAM_READWRITE));
}

static void
bmbl_set_property_node_style(GSList * list)
{
    GSList *l;
    GtkTreePath *path;

    for (l = list; l; l = l->next) {
        GtkTreeRowReference *reference = l->data;
        path = gtk_tree_row_reference_get_path(reference);
        if (path) {
            GtkTreeModel *model;
            GtkTreeIter iter;

            model = gtk_tree_row_reference_get_model(reference);
            if (gtk_tree_model_get_iter(model, &iter, path))
                bmbl_node_style(model, &iter);
            gtk_tree_path_free(path);
        }
        gtk_tree_row_reference_free(reference);
    }

    g_slist_free(list);
}

static gboolean
bmbl_set_property_foreach_func(GtkTreeModel * model, GtkTreePath * path,
                               GtkTreeIter * iter, gpointer data)
{
    GSList **list = data;
    *list =
        g_slist_prepend(*list, gtk_tree_row_reference_new(model, path));
    return FALSE;
}

#define BALSA_MBLIST_DISPLAY_INFO "balsa-mblist-display-info"
static void
bmbl_set_property(GObject * object, guint prop_id,
                          const GValue * value, GParamSpec * pspec)
{
    BalsaMBList *mblist = BALSA_MBLIST(object);
    GtkTreeView *tree_view = GTK_TREE_VIEW(object);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    gboolean display_info;
    GtkTreeViewColumn *column;
    GSList *list = NULL;

    switch (prop_id) {
    case PROP_SHOW_CONTENT_INFO:
        display_info = g_value_get_boolean(value);
        mblist->display_info = display_info;
        gtk_tree_view_set_headers_visible(tree_view, display_info);
        column = gtk_tree_view_get_column(tree_view, 0);
        gtk_tree_view_column_set_sizing(column,
                                        display_info ?
                                        GTK_TREE_VIEW_COLUMN_FIXED :
                                        GTK_TREE_VIEW_COLUMN_GROW_ONLY);
        gtk_tree_view_column_set_resizable(column, display_info);
        column = gtk_tree_view_get_column(tree_view, 1);
        gtk_tree_view_column_set_visible(column, display_info);
        column = gtk_tree_view_get_column(tree_view, 2);
        gtk_tree_view_column_set_visible(column, display_info);
        g_object_set_data(G_OBJECT(model), BALSA_MBLIST_DISPLAY_INFO,
                          GINT_TO_POINTER(display_info));
        gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                               bmbl_set_property_foreach_func, &list);
        bmbl_set_property_node_style(list);
        break;

    default:
        break;
    }
}

static void
bmbl_get_property(GObject * object, guint prop_id, GValue * value,
                          GParamSpec * pspec)
{
    BalsaMBList *mblist = BALSA_MBLIST(object);

    switch (prop_id) {
    case PROP_SHOW_CONTENT_INFO:
        g_value_set_boolean(value, mblist->display_info);
        break;
    default:
        break;
    }
}

static gboolean
bmbl_drag_motion(GtkWidget * mblist, GdkDragContext * context, gint x,
                 gint y, guint time)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(mblist);
    GtkTreePath *path;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    gboolean ret_val;
    gboolean can_drop;

    ret_val =
        GTK_WIDGET_CLASS(balsa_mblist_parent_class)->drag_motion(mblist, context, x, y,
                                                    time);

    gtk_tree_view_get_drag_dest_row(tree_view, &path, NULL);
    if (!path)
        return FALSE;

    can_drop = bmbl_selection_func(selection, model, path, FALSE, NULL);
    gtk_tree_view_set_drag_dest_row(tree_view, can_drop ? path : NULL,
                                    GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
    gtk_tree_path_free(path);

    gdk_drag_status(context,
                    (gdk_drag_context_get_actions(context) ==
                     GDK_ACTION_COPY) ? GDK_ACTION_COPY :
                    GDK_ACTION_MOVE, time);

    return (ret_val && can_drop);
}

/*
 * Set up the mail box list, including the tree's appearance and the
 * callbacks
 */
static void
balsa_mblist_init(BalsaMBList * mblist)
{
    GtkTreeStore *store = balsa_mblist_get_store();
    GtkTreeView *tree_view = GTK_TREE_VIEW(mblist);
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(store));
    g_object_unref(store);

    /* Mailbox icon and name go in first column. */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Mailbox"));
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "icon-name", ICON_COLUMN,
                                        NULL);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", NAME_COLUMN,
                                        "weight", WEIGHT_COLUMN,
                                        "style", STYLE_COLUMN,
                                        NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column,
                                         balsa_app.mblist_name_width);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(tree_view, column);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BMBL_TREE_COLUMN_NAME);
     /* set sort column id will make the column clickable - disable that! */
    gtk_tree_view_column_set_clickable(column, FALSE);

    /* Message counts are right-justified, each in a column centered
     * under its heading. */
    /* Unread message count column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "U");
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", UNREAD_COLUMN,
                                        NULL);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column,
                                         balsa_app.mblist_newmsg_width);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(tree_view, column);
#ifdef SORTING_MAILBOX_LIST_IS_USEFUL
    gtk_tree_view_column_set_sort_column_id(column,
                                            BMBL_TREE_COLUMN_UNREAD);
#endif

    /* Total message count column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "T");
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", TOTAL_COLUMN,
                                        NULL);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(column,
                                         balsa_app.mblist_totalmsg_width);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(tree_view, column);
#ifdef SORTING_MAILBOX_LIST_IS_USEFUL
    gtk_tree_view_column_set_sort_column_id(column,
                                            BMBL_TREE_COLUMN_TOTAL);
#endif
    /* arrange for non-mailbox nodes to be non-selectable */
    gtk_tree_selection_set_select_function(gtk_tree_view_get_selection
                                           (tree_view),
                                           bmbl_selection_func, NULL,
                                           NULL);

    g_signal_connect_after(tree_view, "row-expanded",
                           G_CALLBACK(bmbl_tree_expand), NULL);
    g_signal_connect(tree_view, "row-collapsed",
                     G_CALLBACK(bmbl_tree_collapse), NULL);

    g_object_set(mblist,
                 "show_content_info",
                 balsa_app.mblist_show_mb_content_info,
                 NULL);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store),
                                    BMBL_TREE_COLUMN_NAME,
                                    bmbl_row_compare,
                                    GINT_TO_POINTER(BMBL_TREE_COLUMN_NAME),
                                    NULL);
#ifdef SORTING_MAILBOX_LIST_IS_USEFUL
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store),
                                    BMBL_TREE_COLUMN_UNREAD,
                                    bmbl_row_compare,
                                    GINT_TO_POINTER(BMBL_TREE_COLUMN_UNREAD),
                                    NULL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store),
                                    BMBL_TREE_COLUMN_TOTAL,
                                    bmbl_row_compare,
                                    GINT_TO_POINTER(BMBL_TREE_COLUMN_TOTAL),
                                    NULL);
#endif
    /* Default is ascending sort by name */
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
					 BMBL_TREE_COLUMN_NAME,
					 GTK_SORT_ASCENDING);
}

/*
 * balsa_mblist_get_store
 *
 * Return balsa_app.mblist_tree_store, setting it up if this is the
 * first time.
 */
GtkTreeStore *
balsa_mblist_get_store(void)
{
    if (balsa_app.mblist_tree_store)
	g_object_ref(balsa_app.mblist_tree_store);
    else {
        balsa_app.mblist_tree_store =
            gtk_tree_store_new(N_COLUMNS,
                               G_TYPE_OBJECT,     /* MBNODE_COLUMN */
                               G_TYPE_STRING,     /* ICON_COLUMN   */
                               G_TYPE_STRING,     /* NAME_COLUMN   */
                               PANGO_TYPE_WEIGHT, /* WEIGHT_COLUMN */
                               PANGO_TYPE_STYLE,  /* STYLE_COLUMN */
                               G_TYPE_STRING,     /* UNREAD_COLUMN */
                               G_TYPE_STRING      /* TOTAL_COLUMN  */
            );
        g_object_add_weak_pointer(G_OBJECT(balsa_app.mblist_tree_store),
                                  (gpointer *) & balsa_app.
                                  mblist_tree_store);
    }

    return balsa_app.mblist_tree_store;
}

/*
 * bmbl_selection_func
 *
 * Used to filter whether or not a row may be selected.
 */
static gboolean
bmbl_selection_func(GtkTreeSelection * selection, GtkTreeModel * model,
                      GtkTreePath * path, gboolean path_currently_selected,
                      gpointer data)
{
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;
    gboolean retval;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);

    /* If the node is selected, allow it to be deselected, whether or
     * not it has a mailbox (if it doesn't, it shouldn't have been
     * selected in the first place, but you never know...). */
    retval = (path_currently_selected ||
              (mbnode != NULL &&
               balsa_mailbox_node_get_mailbox(mbnode) != NULL));

    g_object_unref(mbnode);

    return retval;
}

GtkWidget *
balsa_mblist_new()
{
    BalsaMBList *new;

    new = g_object_new(balsa_mblist_get_type(), NULL);

    return GTK_WIDGET(new);
}

/* callbacks */

/* "row-expanded" */

static void
bmbl_tree_expand(GtkTreeView * tree_view, GtkTreeIter * iter,
                    GtkTreePath * path, gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    BalsaMailboxNode *mbnode;
    GtkTreeIter child_iter;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    balsa_mailbox_node_scan_children(mbnode);

    if (balsa_mailbox_node_get_mailbox(mbnode) == NULL)
        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           ICON_COLUMN,
                           balsa_icon_id(BALSA_PIXMAP_MBOX_DIR_OPEN),
                           -1);
    g_object_unref(mbnode);

    if (gtk_tree_model_iter_children(model, &child_iter, iter)) {
	GtkWidget *current_index =
	    balsa_window_find_current_index(balsa_app.main_window);
	LibBalsaMailbox *current_mailbox =
	    current_index != NULL ?
	    balsa_index_get_mailbox(BALSA_INDEX(current_index)):
	    NULL;
	gboolean first_mailbox = TRUE;

        do {
            LibBalsaMailbox *mailbox;

            gtk_tree_model_get(model, &child_iter,
                               MBNODE_COLUMN, &mbnode, -1);
            if (mbnode != NULL &&
                (mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL) {
		/* Mark only one mailbox as exposed. */
		if (first_mailbox) {
		    libbalsa_mailbox_set_exposed(mailbox, TRUE);
		    first_mailbox = FALSE;
		} else
		    libbalsa_mailbox_set_exposed(mailbox, FALSE);
		if (mailbox == current_mailbox) {
		    GtkTreeSelection *selection =
			gtk_tree_view_get_selection(tree_view);
		    g_signal_handlers_block_by_func(selection,
						    bmbl_select_mailbox,
						    NULL);
		    gtk_tree_selection_select_iter(selection, &child_iter);
		    g_signal_handlers_unblock_by_func(selection,
						      bmbl_select_mailbox,
						      NULL);
		}
	    }
	    g_object_unref(mbnode);
        } while (gtk_tree_model_iter_next(model, &child_iter));
    }
}

/* "row-collapsed" */
static void
bmbl_tree_collapse_helper(GtkTreeModel * model, GtkTreeIter * iter)
{
    GtkTreeIter child_iter;

    if (gtk_tree_model_iter_children(model, &child_iter, iter)) {
        do {
            BalsaMailboxNode *mbnode;
            LibBalsaMailbox *mailbox;

            gtk_tree_model_get(model, &child_iter,
                               MBNODE_COLUMN, &mbnode, -1);
            if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL)
		libbalsa_mailbox_set_exposed(mailbox, FALSE);
	    g_object_unref(mbnode);
            bmbl_tree_collapse_helper(model, &child_iter);
        } while (gtk_tree_model_iter_next(model, &child_iter));
    }
}

static void
bmbl_tree_collapse(GtkTreeView * tree_view, GtkTreeIter * iter,
                      GtkTreePath * path, gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);

    if (balsa_mailbox_node_get_mailbox(mbnode) == NULL)
        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           ICON_COLUMN,
                           balsa_icon_id(BALSA_PIXMAP_MBOX_DIR_CLOSED),
                           -1);
    g_object_unref(mbnode);

    bmbl_tree_collapse_helper(model, iter);
}

/* bmbl_row_compare
 *
 * This function determines the sorting order of the list, depending
 * on what column is selected.  The first column sorts by name, with
 * exception given to the five "core" mailboxes (Inbox, Draftbox,
 * Sentbox, Outbox, Trash).  The second sorts by number of unread
 * messages, and the third by total number of messages.
 * */
static gint
bmbl_row_compare(GtkTreeModel * model, GtkTreeIter * iter1,
                 GtkTreeIter * iter2, gpointer data)
{
    BmblTreeColumnId sort_column = GPOINTER_TO_INT(data);
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *m1 = NULL;
    LibBalsaMailbox *m2 = NULL;
    gchar *name1, *name2;
    gint core1;
    gint core2;
    gint ret_val = 0;

    gtk_tree_model_get(model, iter1,
                       MBNODE_COLUMN, &mbnode, NAME_COLUMN, &name1, -1);
    m1 = balsa_mailbox_node_get_mailbox(mbnode);
    g_object_unref(mbnode);

    gtk_tree_model_get(model, iter2,
                       MBNODE_COLUMN, &mbnode, NAME_COLUMN, &name2, -1);
    m2 = balsa_mailbox_node_get_mailbox(mbnode);
    g_object_unref(mbnode);

    switch (sort_column) {
    case BMBL_TREE_COLUMN_NAME:
        /* compare using names, potentially mailboxnodes */
        core1 = bmbl_core_mailbox(m1);
        core2 = bmbl_core_mailbox(m2);
        ret_val = ((core1 || core2) ? core2 - core1
                   : g_ascii_strcasecmp(name1, name2));
        break;

    case BMBL_TREE_COLUMN_UNREAD:
        ret_val = (libbalsa_mailbox_get_unread(m1)
                   - libbalsa_mailbox_get_unread(m2));
        break;

    case BMBL_TREE_COLUMN_TOTAL:
        ret_val = (libbalsa_mailbox_get_total(m1)
                   - libbalsa_mailbox_get_total(m2));
        break;
    }

    g_free(name1);
    g_free(name2);
    return ret_val;
}

/* bmbl_gesture_pressed_cb:
   handle mouse button press events that occur on mailboxes
   (clicking on folders is passed to GtkTreeView and may trigger expand events
*/
static void
bmbl_gesture_pressed_cb(GtkGestureMultiPress *multi_press_gesture,
                        gint                  n_press,
                        gdouble               x,
                        gdouble               y,
                        gpointer              user_data)
{
    GtkTreeView *tree_view = user_data;
    GtkGesture *gesture;
    GdkEventSequence *sequence;
    const GdkEvent *event;
    gint bx;
    gint by;
    GtkTreePath *path;

    gesture  = GTK_GESTURE(multi_press_gesture);
    sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(multi_press_gesture));
    event    = gtk_gesture_get_last_event(gesture, sequence);

    if (!gdk_event_triggers_context_menu(event) ||
        gdk_event_get_window(event) != gtk_tree_view_get_bin_window(tree_view))
        return;

    gtk_gesture_set_sequence_state(gesture, sequence, GTK_EVENT_SEQUENCE_CLAIMED);

    gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, (gint) x, (gint) y,
                                                      &bx, &by);

    if (!gtk_tree_view_get_path_at_pos(tree_view, bx, by,
                                       &path, NULL, NULL, NULL))
        path = NULL;

    bmbl_do_popup(tree_view, path, event);
    /* bmbl_do_popup frees path */
}

/* bmbl_popup_menu:
 * default handler for the "popup-menu" signal, which is issued when the
 * user hits shift-F10
 */
static gboolean
bmbl_popup_menu(GtkWidget * widget)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath *path;

    gtk_tree_view_get_cursor(tree_view, &path, NULL);
    bmbl_do_popup(tree_view, path, NULL);
    /* bmbl_do_popup frees path */
    return TRUE;
}

/* bmbl_do_popup:
 * do the popup, and free the path
 */
static void
bmbl_do_popup(GtkTreeView    *tree_view,
              GtkTreePath    *path,
              const GdkEvent *event)
{
    BalsaMailboxNode *mbnode = NULL;
    GtkWidget *menu;

    if (path) {
        GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
        GtkTreeIter iter;

        if (gtk_tree_model_get_iter(model, &iter, path))
            gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
        gtk_tree_path_free(path);
    }

    menu = balsa_mailbox_node_get_context_menu(mbnode);
    g_object_ref(menu);
    g_object_ref_sink(menu);
    if (event)
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
    else
        gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(tree_view),
                                 GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                 NULL);
    g_object_unref(menu);

    if (mbnode)
	g_object_unref(mbnode);
}

/* bmbl_column_resize [MBG]
 *
 * clist: The clist (in this case ctree), that is having it's columns resized.
 * column: The column being resized
 * size:  The new size of the column
 * data:  The data passed on to the callback when it was connected (NULL)
 *
 * Description: This callback assigns the new column widths to the balsa_app,
 * so they can be saved and restored between sessions.
 * */
static void
bmbl_column_resize(GtkWidget * widget,
                           GtkAllocation * allocation, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    gint name_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 0));
    gint newmsg_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 1));
    gint totalmsg_width =
        gtk_tree_view_column_get_width(gtk_tree_view_get_column
                                       (tree_view, 2));

    if (name_width > 0 && newmsg_width > 0 && totalmsg_width > 0) {
        balsa_app.mblist_name_width = name_width;
        balsa_app.mblist_newmsg_width = newmsg_width;
        balsa_app.mblist_totalmsg_width = totalmsg_width;
    }
}

/* bmbl_drag_cb
 *
 * Description: This is the drag_data_received signal handler for the
 * BalsaMBList.  It retrieves the source BalsaIndex and transfers the
 * index's selected messages to the target
 * mailbox.  Depending on what key is held down when the message(s)
 * are dropped they are either copied or moved.  The default action is
 * to copy.
 * */
static void
bmbl_drag_cb(GtkWidget * widget, GdkDragContext * context,
             gint x, gint y, GtkSelectionData * selection_data,
             guint info, guint time, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    gboolean dnd_ok = FALSE;

	if ((selection_data != NULL) && (gtk_selection_data_get_data(selection_data) != NULL)) {
		BalsaIndex *orig_index;
		GArray *selected;

		orig_index = *(BalsaIndex **) gtk_selection_data_get_data(selection_data);
		selected = balsa_index_selected_msgnos_new(orig_index);

		/* it is actually possible to drag from GtkTreeView when no rows
		 * are selected: Disable preview for that. */
		if (selected->len > 0U) {
			LibBalsaMailbox *orig_mailbox;
			GtkTreePath *path;
			GtkTreeIter iter;

			orig_mailbox = balsa_index_get_mailbox(orig_index);

			/* find the node and mailbox */

			/* we should be able to use:
			 * gtk_tree_view_get_drag_dest_row(tree_view, &path, NULL);
			 * but it sets path to NULL for some reason, so we'll go down to a
			 * lower level. */
			if (gtk_tree_view_get_dest_row_at_pos(tree_view, x, y, &path, NULL)) {
				BalsaMailboxNode *mbnode;
				LibBalsaMailbox *mailbox;

				gtk_tree_model_get_iter(model, &iter, path);
				gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
				mailbox = balsa_mailbox_node_get_mailbox(mbnode);
				g_object_unref(mbnode);

				/* cannot transfer to the originating mailbox */
				if ((mailbox != NULL) && (mailbox != orig_mailbox)) {
					balsa_index_transfer(orig_index, selected, mailbox,
							gdk_drag_context_get_selected_action(context) != GDK_ACTION_MOVE);
					dnd_ok = TRUE;
				}
				gtk_tree_path_free(path);
			}
			if (balsa_find_iter_by_data(&iter, orig_mailbox)) {
				gtk_tree_selection_select_iter(selection, &iter);
			}
		}
		balsa_index_selected_msgnos_free(orig_index, selected);
	}
	gtk_drag_finish(context, dnd_ok, FALSE, time);
}

/* bmbl_select_mailbox
 *
 * This function is called when the user clicks on the mailbox list,
 * to open the mailbox. It's also called if the user uses the keyboard
 * to focus on the mailbox, in which case we don't open the mailbox.
 */
static void
bmbl_select_mailbox(GtkTreeSelection * selection, gpointer data)
{
    GdkEvent *event = gtk_get_current_event();
    GtkTreeView *tree_view =
        gtk_tree_selection_get_tree_view(selection);
    GtkTreeModel *model =
        gtk_tree_view_get_model(tree_view);
    GtkTreePath *path;
    guint button;
    gdouble x_win, y_win;

    if (event == NULL) {
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(selection, NULL, &iter)) {
	    BalsaMailboxNode *mbnode;
	    LibBalsaMailbox *mailbox;
	    gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
	    mailbox = balsa_mailbox_node_get_mailbox(mbnode);
	    g_object_unref(mbnode);
	    if (MAILBOX_OPEN(mailbox))
		/* Opening a mailbox under program control. */
		return;
	}
	/* Not opening a mailbox--must be the initial selection of the
	 * first mailbox in the list, so we'll unselect it again. */
	g_signal_handlers_block_by_func(selection, bmbl_select_mailbox, NULL);
	gtk_tree_selection_unselect_all(selection);
	g_signal_handlers_unblock_by_func(selection, bmbl_select_mailbox, NULL);

        return;
    }

    if (gdk_event_get_event_type(event) != GDK_BUTTON_PRESS
            /* keyboard navigation */
        || !gdk_event_get_button(event, &button) || button != 1
            /* soft select */
        || gdk_event_get_window(event) != gtk_tree_view_get_bin_window(tree_view)
            /* click on a different widget */ ) {
        gdk_event_free(event);

        return;
    }

    if (!gdk_event_get_coords(event, &x_win, &y_win) ||
        !gtk_tree_view_get_path_at_pos(tree_view, (gint) x_win, (gint) y_win, &path,
                                       NULL, NULL, NULL)) {
        /* GtkTreeView selects the first node in the tree when the
         * widget first gets the focus, whether it's a keyboard event or
         * a button event. If it's a button event, but no mailbox was
         * clicked, we'll just undo that selection and return. */
	g_signal_handlers_block_by_func(selection, bmbl_select_mailbox, NULL);
        gtk_tree_selection_unselect_all(selection);
	g_signal_handlers_unblock_by_func(selection, bmbl_select_mailbox, NULL);
        gdk_event_free(event);
        return;
    }

    if (gtk_tree_selection_path_is_selected(selection, path)) {
        GtkTreeIter iter;
        BalsaMailboxNode *mbnode;
        LibBalsaMailbox *mailbox;

        gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);

        if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL)
            balsa_mblist_open_mailbox(mailbox);
	g_object_unref(mbnode);
    }
    gtk_tree_path_free(path);
    gdk_event_free(event);
}

/*
 * bmbl_row_activated_cb: callback for the "row-activated" signal
 *
 * This is emitted when focus is on a mailbox, and the user hits the
 * space bar. It's also emitted if the user double-clicks on a mailbox,
 * in which case we've already opened the mailbox. We could detect this
 * by looking at gtk_current_event, but we don't want the extra code.
 */
static void
bmbl_row_activated_cb(GtkTreeView * tree_view, GtkTreePath * path,
                      GtkTreeViewColumn * column, gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode != NULL);

    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL)
        balsa_mblist_open_mailbox(mailbox);
    g_object_unref(mbnode);
}

/* Mailbox status changed callbacks: update the UI in an idle handler.
 */

struct update_mbox_data {
    LibBalsaMailbox *mailbox;
    gboolean notify;
};
static void bmbl_update_mailbox(GtkTreeStore * store,
                                LibBalsaMailbox * mailbox);

G_LOCK_DEFINE_STATIC(mblist_update);

static gboolean
update_mailbox_idle(struct update_mbox_data *umd)
{
    G_LOCK(mblist_update);

    if (umd->mailbox) {
        g_object_remove_weak_pointer(G_OBJECT(umd->mailbox),
                                     (gpointer *) & umd->mailbox);
        g_object_set_data(G_OBJECT(umd->mailbox), "mblist-update", NULL);

        if (balsa_app.mblist_tree_store) {
            gboolean subscribed =
                libbalsa_mailbox_get_subscribe(umd->mailbox) !=
                LB_MAILBOX_SUBSCRIBE_NO;
            bmbl_update_mailbox(balsa_app.mblist_tree_store, umd->mailbox);
            check_new_messages_count(umd->mailbox, umd->notify
                                     && subscribed);

            if (subscribed) {
                if (libbalsa_mailbox_get_unread(umd->mailbox) > 0)
                    g_signal_emit(balsa_app.mblist,
                                  balsa_mblist_signals[HAS_UNREAD_MAILBOX],
                                  0, TRUE);
                else {
                    GList *unread_mailboxes =
                        balsa_mblist_find_all_unread_mboxes(NULL);
                    if (unread_mailboxes)
                        g_list_free(unread_mailboxes);
                    else
                        g_signal_emit(balsa_app.mblist,
                                      balsa_mblist_signals
                                      [HAS_UNREAD_MAILBOX], 0, FALSE);
                }
            }
        }
    }
    g_free(umd);

    G_UNLOCK(mblist_update);

    return FALSE;
}

static void
bmbl_mailbox_changed_cb(LibBalsaMailbox * mailbox, gpointer data)
{
    struct update_mbox_data *umd;
    LibBalsaMailboxState state;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    G_LOCK(mblist_update);

    umd = g_object_get_data(G_OBJECT(mailbox), "mblist-update");

    if (!umd) {
        umd = g_new(struct update_mbox_data, 1);
        g_object_set_data(G_OBJECT(mailbox), "mblist-update", umd);
        umd->mailbox = mailbox;
        g_object_add_weak_pointer(G_OBJECT(mailbox),
                                  (gpointer *) & umd->mailbox);
        g_idle_add((GSourceFunc) update_mailbox_idle, umd);
    }

    state = libbalsa_mailbox_get_state(mailbox);
    umd->notify = (state == LB_MAILBOX_STATE_OPEN
                   || state == LB_MAILBOX_STATE_CLOSED);

    G_UNLOCK(mblist_update);
}

/* public methods */

/* Caller must unref mbnode. */
BalsaMailboxNode *
balsa_mblist_get_selected_node(BalsaMBList * mbl)
{
    GtkTreeSelection *select =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(mbl));
    BalsaMailboxNode *mbnode = NULL;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(select, &model, &iter))
        gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);

    return mbnode;
}

/* mblist_find_all_unread_mboxes:
 * If mailbox == NULL, returns a list of all mailboxes with unread mail.
 * If mailbox != NULL, returns a similar list, but including a NULL
 * marker for the position of mailbox in the list.
 * Trashbox is excluded.
 */
struct bmbl_find_all_unread_mboxes_info {
    LibBalsaMailbox *mailbox;
    GList *list;
};

static gboolean
bmbl_find_all_unread_mboxes_func(GtkTreeModel * model, GtkTreePath * path,
                                 GtkTreeIter * iter, gpointer data)
{
    struct bmbl_find_all_unread_mboxes_info *info = data;
    BalsaMailboxNode *mbnode = NULL;
    LibBalsaMailbox *mailbox;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    if(!mbnode) /* this node has no MBNODE associated at this time */
        return FALSE;
    mailbox = balsa_mailbox_node_get_mailbox(mbnode);
    g_object_unref(mbnode);

    if (mailbox
        && (libbalsa_mailbox_get_subscribe(mailbox) !=
            LB_MAILBOX_SUBSCRIBE_NO)) {
        if (mailbox == info->mailbox)
            info->list = g_list_prepend(info->list, NULL);
        else if (libbalsa_mailbox_get_unread(mailbox) > 0)
            info->list = g_list_prepend(info->list, mailbox);
    }

    return FALSE;
}

GList *
balsa_mblist_find_all_unread_mboxes(LibBalsaMailbox * mailbox)
{
    struct bmbl_find_all_unread_mboxes_info info;

    info.mailbox = mailbox;
    info.list = NULL;

    if (!balsa_app.mblist_tree_store) /* We have no mailboxes, maybe
					 we are about to quit? */
	return NULL;

    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                           bmbl_find_all_unread_mboxes_func, &info);

    return g_list_reverse(info.list);
}

/* mblist_open_mailbox
 *
 * Description: This checks to see if the mailbox is already on a different
 * mailbox page, or if a new page needs to be created and the mailbox
 * parsed.
 */
static void
bmbl_open_mailbox(LibBalsaMailbox * mailbox, gboolean set_current)
{
    int i;
    GtkWidget *bindex;
    BalsaMailboxNode *mbnode;

    mbnode = balsa_find_mailbox(mailbox);
    if (mbnode == NULL) {
        g_warning("Failed to find mailbox");
        return;
    }

    bindex = balsa_window_find_current_index(balsa_app.main_window);

    /* If we currently have a page open, update the time last visited */
    if (bindex != NULL)
        balsa_index_set_last_use_time(BALSA_INDEX(bindex));

    i = balsa_find_notebook_page_num(mailbox);
    if (i != -1) {
        if (set_current) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(balsa_app.notebook),
                                          i);
            bindex = balsa_window_find_current_index(balsa_app.main_window);
            balsa_index_set_last_use_time(BALSA_INDEX(bindex));
            balsa_index_set_column_widths(BALSA_INDEX(bindex));
        }
    } else { /* page with mailbox not found, open it */
        balsa_window_open_mbnode(balsa_app.main_window, mbnode,
                                 set_current);

	if (balsa_app.mblist->display_info)
	    balsa_mblist_update_mailbox(balsa_app.mblist_tree_store,
                                        mailbox);
    }
    g_object_unref(mbnode);
}

void
balsa_mblist_open_mailbox(LibBalsaMailbox * mailbox)
{
    bmbl_open_mailbox(mailbox, TRUE);
}

void
balsa_mblist_open_mailbox_hidden(LibBalsaMailbox * mailbox)
{
    bmbl_open_mailbox(mailbox, FALSE);
}

void
balsa_mblist_close_mailbox(LibBalsaMailbox * mailbox)
{
    BalsaMailboxNode *mbnode;

    mbnode = balsa_find_mailbox(mailbox);
    if (!mbnode)  {
        g_warning("Failed to find mailbox");
        return;
    }

    balsa_window_close_mbnode(balsa_app.main_window, mbnode);
    g_object_unref(mbnode);
}

/* balsa_mblist_close_lru_peer_mbx closes least recently used mailbox
 * on the same server as the one given as the argument: some IMAP
 * servers limit the number of simultaneously open connections. */
struct lru_data {
    GtkTreePath     *ancestor_path;
    BalsaMailboxNode *mbnode;
};

static gboolean
get_lru_descendant(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
                   gpointer data)
{
    struct lru_data *dt  = (struct lru_data*)data;
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    if (!gtk_tree_path_is_descendant(path, dt->ancestor_path))
        return FALSE;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);

    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL &&
        libbalsa_mailbox_is_open(mailbox) &&
        (dt->mbnode == NULL ||
         (balsa_mailbox_node_get_last_use_time(mbnode) <
          balsa_mailbox_node_get_last_use_time(dt->mbnode)))) {
        g_set_object(&dt->mbnode, mbnode);
    }

    g_object_unref(mbnode);

    return FALSE;
}

gboolean
balsa_mblist_close_lru_peer_mbx(BalsaMBList * mblist,
                                LibBalsaMailbox *mailbox)
{
    GtkTreeModel *model;
    GtkTreeIter   iter;
    struct lru_data dt;
    g_return_val_if_fail(mailbox, FALSE);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(mblist));

    if(!balsa_find_iter_by_data(&iter, mailbox))
        return FALSE;
    dt.ancestor_path = gtk_tree_model_get_path(model, &iter);
    while(gtk_tree_path_get_depth(dt.ancestor_path)>1)
        gtk_tree_path_up(dt.ancestor_path);

    dt.mbnode = NULL;
    gtk_tree_model_foreach(model, get_lru_descendant, &dt);
    if(dt.mbnode) {
        balsa_window_close_mbnode(balsa_app.main_window, dt.mbnode);
        g_object_unref(dt.mbnode);
    }
    return dt.mbnode != NULL;
}

/* balsa_mblist_default_signal_bindings:
   connect signals useful for the left-hand side mailbox tree
   but useless for the transfer menu.
*/
void
balsa_mblist_default_signal_bindings(BalsaMBList * mblist)
{
    GtkGesture *gesture;
    GtkTreeSelection *selection;

    gesture = gtk_gesture_multi_press_new(GTK_WIDGET(mblist));
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_signal_connect(gesture, "pressed",
                     G_CALLBACK(bmbl_gesture_pressed_cb), mblist);

    g_signal_connect_after(mblist, "size-allocate",
                           G_CALLBACK(bmbl_column_resize), NULL);
    gtk_tree_view_enable_model_drag_dest(GTK_TREE_VIEW(mblist),
                                         bmbl_drop_types,
										 G_N_ELEMENTS(bmbl_drop_types),
                                         GDK_ACTION_DEFAULT |
                                         GDK_ACTION_COPY |
                                         GDK_ACTION_MOVE);
    g_signal_connect(mblist, "drag-data-received",
                     G_CALLBACK(bmbl_drag_cb), NULL);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(mblist));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(bmbl_select_mailbox), NULL);
    g_signal_connect(mblist, "row-activated",
                     G_CALLBACK(bmbl_row_activated_cb), NULL);
}

/* bmbl_disconnect_mailbox_signals
 *
 * Remove the signals we attached to the mailboxes.
 */
static void
bmbl_real_disconnect_mbnode_signals(BalsaMailboxNode * mbnode,
				    GtkTreeModel * model)
{
    LibBalsaMailbox *mailbox;

    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL)
        g_signal_handlers_disconnect_by_func(mailbox,
                                             G_CALLBACK
                                             (bmbl_mailbox_changed_cb),
                                             NULL);
}

/* bmbl_store_redraw_mbnode
 *
 * adds BalsaMailboxNodes to the mailbox list, choosing proper icon for them.
 * returns FALSE on failure (wrong parameters passed).
 * */
static gboolean
bmbl_store_redraw_mbnode(GtkTreeIter * iter, BalsaMailboxNode * mbnode)
{
    LibBalsaMailbox *mailbox;
    const gchar *icon;
    const gchar *name;
    gchar *tmp = NULL;
    gboolean expose = FALSE;

    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL) {
	static guint mailbox_changed_signal = 0;

	if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	    g_assert_not_reached();
            icon = NULL;
            name = NULL;
        } else {
	    if(mailbox == balsa_app.draftbox)
		icon = BALSA_PIXMAP_MBOX_DRAFT;
	    else if(mailbox == balsa_app.inbox)
		icon = BALSA_PIXMAP_MBOX_IN;
	    else if(mailbox == balsa_app.outbox)
		icon = BALSA_PIXMAP_MBOX_OUT;
	    else if(mailbox == balsa_app.sentbox)
		icon = BALSA_PIXMAP_MBOX_SENT;
	    else if(mailbox == balsa_app.trash)
		icon = BALSA_PIXMAP_MBOX_TRASH;
	    else
		icon = (libbalsa_mailbox_total_messages(mailbox) > 0)
		? BALSA_PIXMAP_MBOX_TRAY_FULL
                : BALSA_PIXMAP_MBOX_TRAY_EMPTY;

            name = libbalsa_mailbox_get_name(mailbox);

            /* Make sure the show column is set before showing the
             * mailbox in the list. */
	    if (libbalsa_mailbox_get_show(mailbox) == LB_MAILBOX_SHOW_UNSET)
		libbalsa_mailbox_set_show(mailbox,
					  (mailbox == balsa_app.sentbox
					   || mailbox == balsa_app.draftbox
					   || mailbox == balsa_app.outbox) ?
                                          LB_MAILBOX_SHOW_TO :
                                          LB_MAILBOX_SHOW_FROM);
            /* ...and whether to jump to new mail in this mailbox. */
            if (libbalsa_mailbox_get_subscribe(mailbox) ==
                LB_MAILBOX_SUBSCRIBE_UNSET)
                libbalsa_mailbox_set_subscribe(mailbox,
                                               mailbox ==
                                               balsa_app.trash ?
                                               LB_MAILBOX_SUBSCRIBE_NO :
                                               LB_MAILBOX_SUBSCRIBE_YES);
	}

	if (!mailbox_changed_signal)
	    mailbox_changed_signal =
		g_signal_lookup("changed", LIBBALSA_TYPE_MAILBOX);
	if (!g_signal_has_handler_pending(mailbox,
                                          mailbox_changed_signal, 0, TRUE)) {
	    /* Now we have a mailbox: */
	    g_signal_connect(mailbox, "changed",
			     G_CALLBACK(bmbl_mailbox_changed_cb),
			     NULL);
            if (libbalsa_mailbox_get_unread(mailbox) > 0
                && (libbalsa_mailbox_get_subscribe(mailbox) !=
                    LB_MAILBOX_SUBSCRIBE_NO))
                g_signal_emit(balsa_app.mblist,
                              balsa_mblist_signals[HAS_UNREAD_MAILBOX],
                              0, TRUE);
	    /* If necessary, expand rows to expose this mailbox after
	     * setting its mbnode in the tree-store. */
	    expose = libbalsa_mailbox_get_exposed(mailbox);
	}
    } else {
	/* new directory, but not a mailbox */
	icon = BALSA_PIXMAP_MBOX_DIR_CLOSED;
        name = tmp = g_path_get_basename(balsa_mailbox_node_get_name(mbnode));
    }

    gtk_tree_store_set(balsa_app.mblist_tree_store, iter,
                       MBNODE_COLUMN, mbnode,
                       ICON_COLUMN, balsa_icon_id(icon),
                       NAME_COLUMN,   name,
                       WEIGHT_COLUMN, PANGO_WEIGHT_NORMAL,
                       STYLE_COLUMN, PANGO_STYLE_NORMAL,
                       UNREAD_COLUMN, "",
                       TOTAL_COLUMN,  "",
                       -1);
    g_free(tmp);

    if (mailbox != NULL) {
	GtkTreeModel *model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);
	if (expose) {
	    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
	    bmbl_expand_to_row(balsa_app.mblist, path);
	    gtk_tree_path_free(path);
	}
	bmbl_node_style(model, iter);
    }

    return TRUE;
}

/* balsa_mblist_update_mailbox [MBG]
 *
 * mblist: the mailbox list that contains the mailbox
 * mbnode:  the mailbox node that you wish to update
 *
 * Description: the function looks for the mailbox in the mblist, if
 * it's there it changes the style (and fills the info columns)
 * depending on the mailbox variables unread_messages and
 * total_messages. Selects the mailbox (important when previous mailbox
 * was closed).
 * */
static void
bmbl_update_mailbox(GtkTreeStore * store, LibBalsaMailbox * mailbox)
{
    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreeIter iter;
    GtkWidget *bindex;

    /* try and find the mailbox */
    if (!balsa_find_iter_by_data(&iter, mailbox))
        return;

    bmbl_node_style(model, &iter);

    bindex = balsa_window_find_current_index(balsa_app.main_window);
    if (bindex == NULL ||
        mailbox != balsa_index_get_mailbox(BALSA_INDEX(bindex)))
        return;

    balsa_window_set_statusbar(balsa_app.main_window, mailbox);
}

void
balsa_mblist_update_mailbox(GtkTreeStore * store,
			    LibBalsaMailbox * mailbox)
{
    bmbl_update_mailbox(store, mailbox);
}

/* bmbl_node_style [MBG]
 *
 * model:  The model containing the mailbox
 * iter : the iterator pointing on the mailbox node
 * Description: A function to actually do the changing of the style,
 * and is called by both balsa_mblist_update_mailbox, and
 * balsa_mblist_check_new
 *
 * NOTES: ignore special mailboxes.
 * */
static void
bmbl_node_style(GtkTreeModel * model, GtkTreeIter * iter)
{
    BalsaMailboxNode * mbnode;
    LibBalsaMailbox *mailbox;
    gint unread_messages;
    gint total_messages;
    GtkTreeIter parent;
    gboolean has_unread_child;
    gchar *text_unread = NULL;
    gchar *text_total = NULL;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    if (mbnode == NULL ||
        (mailbox = balsa_mailbox_node_get_mailbox(mbnode)) == NULL)
        return;

    unread_messages = libbalsa_mailbox_get_unread(mailbox);
    total_messages = libbalsa_mailbox_get_total(mailbox);

    /* SHOW UNREAD for special mailboxes? */
    if (!(mailbox == balsa_app.sentbox || mailbox == balsa_app.outbox ||
          mailbox == balsa_app.draftbox || mailbox == balsa_app.trash)) {
        const gchar *icon;
        const gchar *name;
        const gchar *mailbox_name;
        gchar *tmp = NULL;
        PangoWeight weight;

        /* Set the style appropriate for unread_messages; we do this
         * even if the state hasn't changed, because we might be
         * rerendering after hiding or showing the info columns. */
        mailbox_name = libbalsa_mailbox_get_name(mailbox);
        if (unread_messages > 0) {
            gboolean display_info;

            icon = BALSA_PIXMAP_MBOX_TRAY_FULL;

            display_info = GPOINTER_TO_INT(g_object_get_data
                                           (G_OBJECT(model),
                                            BALSA_MBLIST_DISPLAY_INFO));
            name = (!display_info && total_messages >= 0) ?
                (tmp = g_strdup_printf("%s (%d)", mailbox_name,
                                      unread_messages))
                : mailbox_name;

            weight = PANGO_WEIGHT_BOLD;
            balsa_mailbox_node_change_style(mbnode, MBNODE_STYLE_NEW_MAIL, 0);
        } else {
            icon = (mailbox == balsa_app.inbox) ?
                BALSA_PIXMAP_MBOX_IN : BALSA_PIXMAP_MBOX_TRAY_EMPTY;
            name = mailbox_name;
            weight = PANGO_WEIGHT_NORMAL;
            balsa_mailbox_node_change_style(mbnode, 0, MBNODE_STYLE_NEW_MAIL);
        }

        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           ICON_COLUMN, balsa_icon_id(icon),
                           NAME_COLUMN, name,
                           WEIGHT_COLUMN, weight,
                           -1);
        g_free(tmp);

    }
    g_object_unref(mbnode);

    if (total_messages >= 0) {
        /* Both counts are valid. */
        text_unread = g_strdup_printf("%d", unread_messages);
        text_total  = g_strdup_printf("%d", total_messages);
    } else if (unread_messages == 0)
        /* Total is unknown, and unread is unknown unless it's 0. */
        text_unread = g_strdup("0");
    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                       UNREAD_COLUMN, text_unread,
                       TOTAL_COLUMN, text_total,
                       -1);
    g_free(text_unread);
    g_free(text_total);

    /* Do the folder styles as well */
    has_unread_child = libbalsa_mailbox_get_unread(mailbox) > 0;
    while (gtk_tree_model_iter_parent(model, &parent, iter)) {
	*iter = parent;
	gtk_tree_model_get(model, &parent, MBNODE_COLUMN, &mbnode, -1);
	if (!has_unread_child) {
	    /* Check all the children of this parent. */
	    GtkTreeIter child;
	    /* We know it has at least one child. */
	    gtk_tree_model_iter_children(model, &child, &parent);
	    do {
		BalsaMailboxNode *mn;
		gtk_tree_model_get(model, &child, MBNODE_COLUMN, &mn, -1);
		if ((balsa_mailbox_node_get_style(mn) &
                     (MBNODE_STYLE_NEW_MAIL | MBNODE_STYLE_UNREAD_CHILD)) != 0)
		    has_unread_child = TRUE;
		g_object_unref(mn);
	    } while (!has_unread_child && gtk_tree_model_iter_next(model, &child));
	}
	if (has_unread_child) {
	    balsa_mailbox_node_change_style(mbnode, MBNODE_STYLE_UNREAD_CHILD, 0);
	    gtk_tree_store_set(GTK_TREE_STORE(model), &parent,
			       STYLE_COLUMN, PANGO_STYLE_OBLIQUE, -1);
	} else {
	    balsa_mailbox_node_change_style(mbnode, 0, MBNODE_STYLE_UNREAD_CHILD);
	    gtk_tree_store_set(GTK_TREE_STORE(model), &parent,
			       STYLE_COLUMN, PANGO_STYLE_NORMAL, -1);
	}
	g_object_unref(mbnode);
    }
}

/* bmbl_core_mailbox
 *
 * Simple function, if the mailbox is one of the five "core" mailboxes
 * (i.e. Inbox, Sentbox...) it returns an integer representing it's
 * place in the desired heirarchy in the mblist.  If the mailbox is
 * not a core mailbox it returns zero.
 * */
static gint
bmbl_core_mailbox(LibBalsaMailbox* mailbox)
{
    static const int num_core_mailboxes = 5;
    LibBalsaMailbox* core_mailbox[] = {
        balsa_app.inbox,
        balsa_app.sentbox,
        balsa_app.draftbox,
        balsa_app.outbox,
        balsa_app.trash
    };
    gint i = 0;

    for (i = 0; i < num_core_mailboxes; ++i) {
        if (mailbox == core_mailbox[i]) {
            /* we want to return as if from a base-1 array */
            return num_core_mailboxes - i + 1;
        }
    }

    /* if we couldn't find the mailbox, return 0 */
    return 0;
}

gboolean
balsa_mblist_focus_mailbox(BalsaMBList * mblist, LibBalsaMailbox * mailbox)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeSelection *selection;

    g_return_val_if_fail(mblist, FALSE);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(mblist));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(mblist));

    if (mailbox && balsa_find_iter_by_data(&iter, mailbox)) {
        if (!gtk_tree_selection_iter_is_selected(selection, &iter)) {
            /* moving selection to the respective mailbox.
               this is neccessary when the previous mailbox was closed and
               redundant if the mailboxes were switched (notebook_switch_page)
               or the mailbox is checked for the new mail arrival
             */
            GtkTreePath *path;

            path = gtk_tree_model_get_path(model, &iter);
            bmbl_expand_to_row(mblist, path);
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(mblist), path, NULL,
                                         FALSE, 0, 0);
            gtk_tree_path_free(path);
        }
        return TRUE;
    } else {
        gtk_tree_selection_unselect_all(selection);
        return FALSE;
    }
}

/* mblist_remove_mailbox_node:
   remove give mailbox node from the mailbox tree.
   Return TRUE (or equivalent) on success, FALSE on failure.
*/

gboolean
balsa_mblist_mailbox_node_remove(BalsaMailboxNode* mbnode)
{
    GtkTreeIter iter;

    g_return_val_if_fail(mbnode, FALSE);

    if (balsa_find_iter_by_data(&iter, mbnode)) {
	bmbl_real_disconnect_mbnode_signals(mbnode,
					    GTK_TREE_MODEL
					    (balsa_app.mblist_tree_store));
        gtk_tree_store_remove(balsa_app.mblist_tree_store, &iter);

        return TRUE;
    }

    return FALSE;
}

/* balsa_mblist_mru_menu:
 * a menu showing a list of recently used mailboxes, with an option for
 * selecting one from the whole mailbox tree
 */

/* The info that needs to be passed around */
struct _BalsaMBListMRUEntry {
    GtkWindow *window;
    GList **url_list;
    GTask *task;
    gchar *url;
};
typedef struct _BalsaMBListMRUEntry BalsaMBListMRUEntry;

/* Forward references */
static BalsaMBListMRUEntry *bmbl_mru_new(GtkWindow  *window,
                                         GList     **url_list,
                                         GTask      *task,
                                         gchar      *url);
static void bmbl_mru_free(BalsaMBListMRUEntry * mru);
static void bmbl_mru_activate_cb(GtkWidget * widget, gpointer data);
static void bmbl_mru_show_tree(GtkWidget * widget, gpointer data);
static void bmbl_mru_selected_cb(GtkTreeSelection * selection,
                                 gpointer data);
static void bmbl_mru_activated_cb(GtkTreeView * tree_view,
                                  GtkTreePath * path,
                                  GtkTreeViewColumn * column,
                                  gpointer data);

/*
 * balsa_mblist_mru_menu:
 *
 * window:      parent window for the `Other...' dialog;
 * url_list:    pointer to a list of urls;
 * callback:    called when an item is selected; must call
 *              balsa_mblist_mru_menu_finish() to retrieve the selectd
 *              mailbox;
 * user_data:   passed to the callback.
 *
 * Returns a pointer to a GtkMenu.
 *
 * Takes a list of urls and creates a menu with an entry for each one
 * that resolves to a mailbox, labeled with the mailbox name, with a
 * last entry that pops up the whole mailbox tree. When an item is
 * clicked, the url_list is updated and callback is called.
 */
GtkWidget *
balsa_mblist_mru_menu(GtkWindow * window, GList ** url_list,
                      GAsyncReadyCallback callback, gpointer user_data)
{
    GTask *task;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;
    GList *list;
    BalsaMBListMRUEntry *mru;

    g_return_val_if_fail(url_list != NULL, NULL);

    task = g_task_new(NULL, NULL, callback, user_data);

    for (list = *url_list; list; list = list->next) {
        gchar *url = list->data;
        LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);

        if (mailbox != NULL) {
            mru = bmbl_mru_new(NULL, url_list, task, url);
            item = gtk_menu_item_new_with_label(libbalsa_mailbox_get_name(mailbox));
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            g_signal_connect_data(item, "activate",
                                  G_CALLBACK(bmbl_mru_activate_cb), mru,
                                  (GClosureNotify) bmbl_mru_free,
                                  (GConnectFlags) 0);
        }
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());

    mru = bmbl_mru_new(window, url_list, task, NULL);
    item = gtk_menu_item_new_with_mnemonic(_("_Other…"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect_data(item, "activate",
                          G_CALLBACK(bmbl_mru_show_tree), mru,
                          (GClosureNotify) bmbl_mru_free, (GConnectFlags) 0);

    gtk_widget_show_all(menu);

    return menu;
}

LibBalsaMailbox *
balsa_mblist_mru_menu_finish(GAsyncResult *result)
{
    GTask *task = G_TASK(result);
    char *url;
    LibBalsaMailbox *mailbox = NULL;

    url = g_task_propagate_pointer(task, NULL);
    if (url != NULL) {
        mailbox = balsa_find_mailbox_by_url(url);
        g_free(url);
    }

    return mailbox;
}


/*
 * bmbl_mru_new:
 *
 * window, url_list: as for balsa_mblist_mru_menu;
 * task:             a GTask constructed from a GAsyncReadyCallback and user_data
 * url:              URL of a mailbox.
 *
 * Returns a newly allocated BalsaMBListMRUEntry structure, initialized
 * with the data.
 */
static BalsaMBListMRUEntry *
bmbl_mru_new(GtkWindow  *window, GList **url_list, GTask *task,
             gchar * url)
{
    BalsaMBListMRUEntry *mru = g_new(BalsaMBListMRUEntry, 1);

    mru->window = window;
    mru->url_list = url_list;
    mru->task = task;
    mru->url = g_strdup(url);

    return mru;
}

static void
bmbl_mru_free(BalsaMBListMRUEntry * mru)
{
    g_free(mru->url);
    g_free(mru);
}

/*
 * bmbl_mru_activate_cb:
 *
 * Callback for the "activate" signal of a menu item.
 */
static void
bmbl_mru_activate_cb(GtkWidget * item, gpointer data)
{
    BalsaMBListMRUEntry *mru = (BalsaMBListMRUEntry *) data;
    GTask *task;
    char *url;

    if (mru->url != NULL)
        balsa_mblist_mru_add(mru->url_list, mru->url);

    /* The GTask callback may destroy the menu, which then
     * frees mru, so we steal its contents. */
    task = mru->task;
    url = mru->url;
    mru->url = NULL;

    g_task_return_pointer(task, url, g_free);
    g_object_unref(task);
}

/*
 * bmbl_mru_show_tree:
 *
 * Callback for the "activate" signal of the last menu item.
 * Pops up a GtkDialog with a BalsaMBList.
 */

/*
 * bmbl_mru_size_allocate_cb:
 *
 * Callback for the dialog's "size-allocate" signal.
 * Remember the width and height.
 */
static void
bmbl_mru_size_allocate_cb(GtkWidget * widget, GdkRectangle * allocation,
                          gpointer user_data)
{
    GdkWindow *gdk_window;
    gboolean maximized;

    gdk_window = gtk_widget_get_window(widget);
    if (gdk_window == NULL)
        return;

    /* Maximizing a GtkDialog may not be possible, but we check anyway. */
    maximized =
        (gdk_window_get_state(gdk_window) &
         (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) != 0;

    if (!maximized)
        gtk_window_get_size(GTK_WINDOW(widget),
                            & balsa_app.mru_tree_width,
                            & balsa_app.mru_tree_height);
}

static void
bmbl_mru_show_tree_response(GtkDialog *self,
                            gint       response_id,
                            gpointer   user_data)
{
    BalsaMBListMRUEntry *mru = user_data;

    gtk_widget_destroy(GTK_WIDGET(self));

    bmbl_mru_activate_cb(NULL, mru);
}

static void
bmbl_mru_show_tree(GtkWidget * widget, gpointer data)
{
    BalsaMBListMRUEntry *mru = data;
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *scroll;
    GtkWidget *mblist;
    GtkTreeSelection *selection;

    mblist = balsa_mblist_new();
    g_signal_connect(mblist, "row-activated",
                     G_CALLBACK(bmbl_mru_activated_cb), data);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(mblist));
    g_signal_connect(selection, "changed",
                     G_CALLBACK(bmbl_mru_selected_cb), data);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), mblist);
    gtk_widget_show_all(scroll);

    dialog =
        gtk_dialog_new_with_buttons(_("Choose destination folder"),
                                    mru->window,
                                    GTK_DIALOG_MODAL |
                                    libbalsa_dialog_flags(),
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_valign(scroll, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(content_area), scroll);

    g_signal_connect(dialog, "size-allocate",
                     G_CALLBACK(bmbl_mru_size_allocate_cb), NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog),
                                balsa_app.mru_tree_width,
                                balsa_app.mru_tree_height);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(bmbl_mru_show_tree_response), mru);
    gtk_widget_show_all(dialog);
}

/*
 * bmbl_mru_selected_cb:
 *
 * Callback for the "changed" signal of the GtkTreeSelection in the
 * BalsaMBList object. This permits one-click selection of a mailbox.
 *
 * Emulates selecting one of the other menu items, and closes the dialog.
 */
static void
bmbl_mru_selected_cb(GtkTreeSelection * selection, gpointer data)
{
    BalsaMBListMRUEntry *mru = data;
    GdkEvent *event;
    GtkTreeView *tree_view;
    gdouble x_win, y_win;
    GtkTreePath *path;

    if (!data)
        return;

    event = gtk_get_current_event();
    if (event == NULL)
        return;

    if (gdk_event_get_event_type(event) != GDK_BUTTON_PRESS ||
        !gdk_event_get_coords(event, &x_win, &y_win)) {
        gdk_event_free(event);
        return;
    }

    tree_view = gtk_tree_selection_get_tree_view(selection);

    if (!gtk_tree_view_get_path_at_pos(tree_view, (gint) x_win, (gint) y_win, &path,
                                       NULL, NULL, NULL)) {
        gtk_tree_selection_unselect_all(selection);
        gdk_event_free(event);
        return;
    }

    if (gtk_tree_selection_path_is_selected(selection, path)) {
        GtkTreeModel *model;
        GtkTreeIter iter;
        BalsaMailboxNode *mbnode;

        model = gtk_tree_view_get_model(tree_view);
        gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_model_get(model, &iter, 0, &mbnode, -1);
        mru->url =
            g_strdup(libbalsa_mailbox_get_url(balsa_mailbox_node_get_mailbox(mbnode)));
	g_object_unref(mbnode);

        gtk_dialog_response(GTK_DIALOG
                            (gtk_widget_get_ancestor
                             (GTK_WIDGET(tree_view), GTK_TYPE_DIALOG)),
                            GTK_RESPONSE_OK);
    }

    gtk_tree_path_free(path);
    gdk_event_free(event);
}

/*
 * bmbl_mru_activated_cb:
 *
 * Callback for the "row-activated" signal of the GtkTreeView in the
 * BalsaMBList object. This permits keyboard selection of a mailbox.
 */
static void
bmbl_mru_activated_cb(GtkTreeView * tree_view, GtkTreePath * path,
                      GtkTreeViewColumn * column, gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    BalsaMBListMRUEntry *mru = data;
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode != NULL);

    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) != NULL) {
        mru->url = g_strdup(libbalsa_mailbox_get_url(mailbox));

        gtk_dialog_response(GTK_DIALOG
                            (gtk_widget_get_ancestor
                             (GTK_WIDGET(tree_view), GTK_TYPE_DIALOG)),
                            GTK_RESPONSE_OK);
    }
    g_object_unref(mbnode);
}

/* balsa_mblist_mru_add/drop:
   Add given folder's url to mailbox-recently-used list.
   Drop it first, so that if it's already there, it moves to the top.
*/
#define FOLDER_MRU_LENGTH 10
void
balsa_mblist_mru_add(GList ** list, const gchar * url)
{
    balsa_mblist_mru_drop(list, url);
    while (g_list_length(*list) >= FOLDER_MRU_LENGTH) {
        GList *tmp = g_list_last(*list);

        g_free(tmp->data);
        *list = g_list_delete_link(*list, tmp);
    }
    *list = g_list_prepend(*list, g_strdup(url));

    if (list != &balsa_app.folder_mru) {
        /* Update the folder MRU list as well */
        balsa_mblist_mru_add(&balsa_app.folder_mru, url);
    }
}

void
balsa_mblist_mru_drop(GList ** list, const gchar * url)
{
    GList *tmp;

    for (tmp = *list; tmp; tmp = g_list_next(tmp)) {
        if (!strcmp((char *) tmp->data, url)) {
            g_free(tmp->data);
            *list = g_list_delete_link(*list, tmp);
            break;
        }
    }
}

/* balsa_mblist_mru_option_menu: create a GtkComboBox to manage an MRU
 * list */

/* The info that needs to be passed around */
struct _BalsaMBListMRUOption {
    GtkWindow *window;
    GList **url_list;
    GSList *real_urls;
};
typedef struct _BalsaMBListMRUOption BalsaMBListMRUOption;

/* Helper. */
static void bmbl_mru_combo_box_changed(GtkComboBox * combo_box,
                                       BalsaMBListMRUOption * mro);
static void
bmbl_mru_combo_box_setup(GtkComboBox * combo_box)
{
    BalsaMBListMRUOption *mro =
        g_object_get_data(G_OBJECT(combo_box), "mro");
    GList *list;
    GtkListStore *store;
    GtkTreeIter iter;

    gtk_combo_box_set_active(combo_box, -1);
    store = GTK_LIST_STORE(gtk_combo_box_get_model(combo_box));
    gtk_list_store_clear(store);
    g_slist_free_full(mro->real_urls, g_free);
    mro->real_urls = NULL;

    for (list = *mro->url_list; list; list = list->next) {
        const gchar *url = list->data;
        
        gchar * short_name = balsa_get_short_mailbox_name(url);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           0, short_name,
                           1, FALSE, -1);
        g_free(short_name);
        mro->real_urls = g_slist_append(mro->real_urls, g_strdup(url));
    }

    /* Separator: */
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 1, TRUE, -1);
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
                       0, _("Other…"),
		       1, FALSE, -1);
    gtk_combo_box_set_active(combo_box, 0);
}

/* Callbacks */
static void
bmbl_mru_combo_box_changed_callback(GObject      *source_object,
                                    GAsyncResult *res,
                                    gpointer      data)
{
    GtkComboBox *combo_box = GTK_COMBO_BOX(source_object);

    bmbl_mru_combo_box_setup(combo_box);
}

static void
bmbl_mru_combo_box_changed(GtkComboBox * combo_box,
                           BalsaMBListMRUOption * mro)
{
    GTask *task;
    BalsaMBListMRUEntry *mru;
    const gchar *url;
    gint active = gtk_combo_box_get_active(combo_box);

    if (active < 0)
	return;
    if ((url = g_slist_nth_data(mro->real_urls, active))) {
	/* Move this url to the top. */
	balsa_mblist_mru_add(mro->url_list, url);
        return;
    }

    /* User clicked on "Other..." */
    task = g_task_new(combo_box, NULL, bmbl_mru_combo_box_changed_callback, NULL);
    mru = bmbl_mru_new(mro->window, mro->url_list, task, NULL);

    /* Let the combobox own the mru: */
    g_object_set_data_full(G_OBJECT(combo_box), "bmbl-mru-combo-box-data",
                           mru, (GDestroyNotify) bmbl_mru_free);

    bmbl_mru_show_tree(NULL, mru);
}

static void
bmbl_mru_combo_box_destroy_cb(BalsaMBListMRUOption * mro)
{
    g_slist_free_full(mro->real_urls, g_free);
    g_free(mro);
}

/*
 * balsa_mblist_mru_option_menu:
 *
 * window:      parent window for the dialog;
 * url_list:    pointer to a list of urls;
 *
 * Returns a GtkComboBox.
 *
 * Takes a list of urls and creates a combo-box with an entry for
 * each one that resolves to a mailbox, labeled with the mailbox name,
 * including the special case of an empty url and a NULL mailbox.
 * Adds a last entry that pops up the whole mailbox tree. When an item
 * is clicked, the url_list is updated.
 */
static gboolean
bmbl_mru_separator_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gboolean is_sep;

    gtk_tree_model_get(model, iter, 1, &is_sep, -1);

    return is_sep;
}

GtkWidget *
balsa_mblist_mru_option_menu(GtkWindow * window, GList ** url_list)
{
    GtkWidget *combo_box;
    BalsaMBListMRUOption *mro;
    GtkListStore *store;
    GtkCellRenderer *renderer;

    g_return_val_if_fail(url_list != NULL, NULL);

    store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_BOOLEAN);
    combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo_box), renderer, TRUE);
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_box), renderer,
                                   "text", 0, NULL);
    gtk_combo_box_set_row_separator_func(GTK_COMBO_BOX(combo_box),
                                         bmbl_mru_separator_func, NULL,
                                         NULL);
    mro = g_new(BalsaMBListMRUOption, 1);

    mro->window = window;
    mro->url_list = url_list;
    mro->real_urls = NULL;

    g_object_set_data_full(G_OBJECT(combo_box), "mro", mro,
                           (GDestroyNotify) bmbl_mru_combo_box_destroy_cb);
    bmbl_mru_combo_box_setup(GTK_COMBO_BOX(combo_box));
    g_signal_connect(combo_box, "changed",
                     G_CALLBACK(bmbl_mru_combo_box_changed), mro);

    return combo_box;
}

/*
 * balsa_mblist_mru_option_menu_set
 *
 * combo_box: a GtkComboBox created by balsa_mblist_mru_option_menu;
 * url:       URL of a mailbox
 *
 * Adds url to the front of the url_list managed by combo_box, resets
 * combo_box to show the new url, and stores a copy in the mro
 * structure.
 */
void
balsa_mblist_mru_option_menu_set(GtkWidget * combo_box, const gchar * url)
{
    BalsaMBListMRUOption *mro =
        g_object_get_data(G_OBJECT(combo_box), "mro");

    balsa_mblist_mru_add(mro->url_list, url);
    bmbl_mru_combo_box_setup(GTK_COMBO_BOX(combo_box));
}

/*
 * balsa_mblist_mru_option_menu_get
 *
 * combo_box: a GtkComboBox created by balsa_mblist_mru_option_menu.
 *
 * Returns the address of the current URL.
 *
 * Note that the url is held in the mro structure, and is freed when the
 * widget is destroyed. The calling code must make its own copy of the
 * string if it is needed more than temporarily.
 */
const gchar *
balsa_mblist_mru_option_menu_get(GtkWidget * combo_box)
{
    gint active;
    BalsaMBListMRUOption *mro =
        g_object_get_data(G_OBJECT(combo_box), "mro");

    active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box));

    return g_slist_nth_data(mro->real_urls, active);
}

static void
bmbl_expand_to_row(BalsaMBList * mblist, GtkTreePath * path)
{
    GtkTreePath *tmp = gtk_tree_path_copy(path);

    if (gtk_tree_path_up(tmp) && gtk_tree_path_get_depth(tmp) > 0
        && !gtk_tree_view_row_expanded(GTK_TREE_VIEW(mblist), tmp)) {
	gtk_tree_view_expand_to_path(GTK_TREE_VIEW(mblist), tmp);
    }

    gtk_tree_path_free(tmp);
}

/* Make a new row for mbnode in balsa_app.mblist_tree_store; the row
 * will be a child to the row for root, if we find it, and a top-level
 * row otherwise. */
static gboolean
bmbl_sort_idle(gpointer data)
{
    GtkTreeSortable *sortable = data;

    gtk_tree_sortable_set_sort_column_id(sortable,
                                         balsa_app.mblist->sort_column_id,
                                         GTK_SORT_ASCENDING);
    balsa_app.mblist->sort_idle_id = 0;
    g_object_unref(sortable);

    return FALSE;
}

void
balsa_mblist_mailbox_node_append(BalsaMailboxNode * root,
				 BalsaMailboxNode * mbnode)
{
    GtkTreeModel *model;
    GtkTreeIter parent;
    GtkTreeIter *parent_iter = NULL;
    GtkTreeIter iter;

    model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);

    if (!balsa_app.mblist->sort_idle_id) {
        GtkTreeSortable *sortable = GTK_TREE_SORTABLE(model);
        gtk_tree_sortable_get_sort_column_id
            (sortable, &balsa_app.mblist->sort_column_id, NULL);
        gtk_tree_sortable_set_sort_column_id
            (sortable, GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
                       GTK_SORT_ASCENDING);
        balsa_app.mblist->sort_idle_id =
            g_idle_add(bmbl_sort_idle, g_object_ref(sortable));
    }

    if (root && balsa_find_iter_by_data(&parent, root))
	parent_iter = &parent;
    gtk_tree_store_prepend(balsa_app.mblist_tree_store, &iter, parent_iter);
    bmbl_store_redraw_mbnode(&iter, mbnode);

    if (parent_iter) {
        /* Check whether this node is exposed. */
        GtkTreePath *parent_path =
            gtk_tree_model_get_path(model, parent_iter);
        if (gtk_tree_view_row_expanded(GTK_TREE_VIEW(balsa_app.mblist),
                                       parent_path)) {
            /* Check this node for children. */
            balsa_mailbox_node_append_subtree(mbnode);
        }
        gtk_tree_path_free(parent_path);
    }

    /* The tree-store owns mbnode. */
    g_object_unref(mbnode);
}

/* Rerender a row after its properties have changed. */
void
balsa_mblist_mailbox_node_redraw(BalsaMailboxNode * mbnode)
{
    GtkTreeIter iter;
    if (balsa_find_iter_by_data(&iter, mbnode))
	bmbl_store_redraw_mbnode(&iter, mbnode);
    balsa_window_update_tab(mbnode);
}
