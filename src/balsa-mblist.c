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

#include "config.h"

#include <gnome.h>
/* #include <gtk/gtkfeatures.h> */
#include <string.h>
#if BALSA_MAJOR < 2
#include <gdk/gdkprivate.h>
#else
#include <gdk/gdkfont.h>
#endif                          /* BALSA_MAJOR < 2 */

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "libbalsa.h"
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

static GtkTargetEntry bmbl_drop_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

static GtkTreeViewClass *parent_class = NULL;

/* class methods */
static void bmbl_class_init(BalsaMBListClass * klass);
static void bmbl_destroy(GtkObject * obj);
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
static void bmbl_init(BalsaMBList * mblist);
static GtkTreeStore *bmbl_get_store(void);
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
static void bmbl_child_toggled_cb(GtkTreeModel * model, GtkTreePath * path,
                                  GtkTreeIter * iter, gpointer data);
static gint bmbl_row_compare(GtkTreeModel * model,
                             GtkTreeIter * iter1,
                             GtkTreeIter * iter2, gpointer data);
static gboolean bmbl_button_press_cb(GtkWidget * widget,
                                     GdkEventButton * event,
                                     gpointer data);
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
				    GtkTreeStore * store);
/* helpers */
static gboolean bmbl_find_all_unread_mboxes_func(GtkTreeModel * model,
                                                 GtkTreePath * path,
                                                 GtkTreeIter * iter,
                                                 gpointer data);
static void bmbl_real_disconnect_mbnode_signals(BalsaMailboxNode * mbnode,
					      GtkTreeModel * model);
static gboolean bmbl_disconnect_mailbox_signals(GtkTreeModel * model,
                                                GtkTreePath * path,
                                                GtkTreeIter * iter,
                                                gpointer data);
static void bmbl_store_add_gnode(GtkTreeStore * store,
                                 GtkTreeIter * parent, GNode * gnode);
static gboolean bmbl_store_add_mbnode(GtkTreeStore * store,
                                      GtkTreeIter * iter,
                                      BalsaMailboxNode * mbnode);
static gboolean bmbl_prerecurs_fn(GtkTreeModel * model,
                                  GtkTreeIter * iter,
                                  GtkTreeModelForeachFunc func,
                                  gpointer data);
static gboolean bmbl_prerecursive(GtkTreeModel * model,
                                  GtkTreeIter * iter,
                                  GtkTreeModelForeachFunc func,
                                  gpointer data);
static gboolean bmbl_have_new_func(GtkTreeModel * model,
                                   GtkTreePath * path,
                                   GtkTreeIter * iter, gpointer data);
static void bmbl_folder_style(GtkTreeModel * model, GtkTreeIter * iter);
static gboolean bmbl_find_data_func(GtkTreeModel * model,
                                    GtkTreePath * path,
                                    GtkTreeIter * iter, gpointer data);
static void bmbl_mbnode_tab_style(BalsaMailboxNode * mbnode, gint unread);
static void bmbl_node_style(GtkTreeModel * model, GtkTreeIter * iter,
			    gint total_messages);
static gint bmbl_core_mailbox(LibBalsaMailbox * mailbox);
static void bmbl_do_popup(GtkTreeView * tree_view, GtkTreePath * path,
                          GdkEventButton * event);
static void bmbl_expand_to_row(BalsaMBList * mblist, GtkTreePath * path);
/* end of prototypes */

/* class methods */

GtkType
balsa_mblist_get_type(void)
{
    static GtkType mblist_type = 0;

    if (!mblist_type) {
	static const GTypeInfo mblist_info = {
	    sizeof(BalsaMBListClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) bmbl_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(BalsaMBList),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) bmbl_init
	};

	mblist_type =
            g_type_register_static(GTK_TYPE_TREE_VIEW,
	                           "BalsaMBList",
                                   &mblist_info, 0);
    }

    return mblist_type;
}


static void
bmbl_class_init(BalsaMBListClass * klass)
{
    GObjectClass *o_class;
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;

    parent_class = g_type_class_peek_parent(klass);

    o_class = (GObjectClass *) klass;
    object_class = (GtkObjectClass *) klass;
    widget_class = (GtkWidgetClass *) klass;

    /* GObject signals */
    o_class->set_property = bmbl_set_property;
    o_class->get_property = bmbl_get_property;

    /* GtkObject signals */
    object_class->destroy = bmbl_destroy;

    /* GtkWidget signals */
    widget_class->drag_motion = bmbl_drag_motion;
    widget_class->popup_menu = bmbl_popup_menu;

    /* Properties */
    g_object_class_install_property(o_class, PROP_SHOW_CONTENT_INFO,
                                    g_param_spec_boolean
                                    ("show_content_info", NULL, NULL,
                                     FALSE, G_PARAM_READWRITE));
}

static void
bmbl_destroy(GtkObject * obj)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(obj);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    BalsaMBList *mblist = BALSA_MBLIST(obj);

    if (mblist->toggled_handler_id) {
        g_signal_handler_disconnect(G_OBJECT(model),
                                    mblist->toggled_handler_id);
        mblist->toggled_handler_id = 0;
    }

    /* chain up ... */
    GTK_OBJECT_CLASS(parent_class)->destroy(obj);
}

static void
bmbl_set_property(GObject * object, guint prop_id,
                          const GValue * value, GParamSpec * pspec)
{
    BalsaMBList *mblist = BALSA_MBLIST(object);
    GtkTreeView *tree_view = GTK_TREE_VIEW(object);
    gboolean display_info;
    GtkTreeViewColumn *column;

    switch (prop_id) {
    case PROP_SHOW_CONTENT_INFO:
        display_info = g_value_get_boolean(value);
        mblist->display_info = display_info;
        column = gtk_tree_view_get_column(tree_view, 1);
        gtk_tree_view_column_set_visible(column, display_info);
        column = gtk_tree_view_get_column(tree_view, 2);
        gtk_tree_view_column_set_visible(column, display_info);
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
        GTK_WIDGET_CLASS(parent_class)->drag_motion(mblist, context, x, y,
                                                    time);

    gtk_tree_view_get_drag_dest_row(tree_view, &path, NULL);
    if (!path)
        return FALSE;

    can_drop = bmbl_selection_func(selection, model, path, FALSE, NULL);
    gtk_tree_view_set_drag_dest_row(tree_view, can_drop ? path : NULL,
                                    GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
    gtk_tree_path_free(path);

    gdk_drag_status(context,
                    (context->actions ==
                     GDK_ACTION_COPY) ? GDK_ACTION_COPY :
                    GDK_ACTION_MOVE, time);

    return (ret_val && can_drop);
}

/*
 * Set up the mail box list, including the tree's appearance and the
 * callbacks
 */
static void
bmbl_init(BalsaMBList * mblist)
{
    GtkTreeStore *store = bmbl_get_store();
    GtkTreeView *tree_view = GTK_TREE_VIEW(mblist);
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(store));

    /* Mailbox icon and name go in first column. */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Mailbox"));
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "pixbuf", ICON_COLUMN,
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
 

    /* Message counts are right-justified, each in a column centered
     * under its heading. */
    /* Unread message count column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Unread"));
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
    gtk_tree_view_column_set_visible(column, mblist->display_info);
    gtk_tree_view_append_column(tree_view, column);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BMBL_TREE_COLUMN_UNREAD);


    /* Total message count column */
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Total"));
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
    gtk_tree_view_column_set_visible(column, mblist->display_info);
    gtk_tree_view_append_column(tree_view, column);
    gtk_tree_view_column_set_sort_column_id(column,
                                            BMBL_TREE_COLUMN_TOTAL);

    /* arrange for non-mailbox nodes to be non-selectable */
    gtk_tree_selection_set_select_function(gtk_tree_view_get_selection
                                           (tree_view),
                                           bmbl_selection_func, NULL,
                                           NULL);

    g_signal_connect_after(G_OBJECT(tree_view), "row-expanded",
                           G_CALLBACK(bmbl_tree_expand), NULL);
    g_signal_connect(G_OBJECT(tree_view), "row-collapsed",
                     G_CALLBACK(bmbl_tree_collapse), NULL);
    mblist->toggled_handler_id =
        g_signal_connect(G_OBJECT(store), "row-has-child-toggled",
                         G_CALLBACK(bmbl_child_toggled_cb),
                         tree_view);

    g_object_set(G_OBJECT(mblist),
                 "show_content_info",
                 balsa_app.mblist_show_mb_content_info,
                 NULL);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store),
                                    BMBL_TREE_COLUMN_NAME,
                                    bmbl_row_compare,
                                    GINT_TO_POINTER(BMBL_TREE_COLUMN_NAME),
                                    NULL);
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
    /* Default is ascending sort by name */
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
					 BMBL_TREE_COLUMN_NAME,
					 GTK_SORT_ASCENDING);
}

/*
 * bmbl_get_store
 *
 * Return balsa_app.mblist_tree_store, setting it up if this is the
 * first time.
 */
static GtkTreeStore *
bmbl_get_store(void)
{
    if (!balsa_app.mblist_tree_store)
        balsa_app.mblist_tree_store =
            gtk_tree_store_new(N_COLUMNS,
                               G_TYPE_POINTER,    /* MBNODE_COLUMN */
                               GDK_TYPE_PIXBUF,   /* ICON_COLUMN   */
                               G_TYPE_STRING,     /* NAME_COLUMN   */
                               PANGO_TYPE_WEIGHT, /* WEIGHT_COLUMN */
                               PANGO_TYPE_STYLE,  /* STYLE_COLUMN */
                               G_TYPE_STRING,     /* UNREAD_COLUMN */
                               G_TYPE_STRING      /* TOTAL_COLUMN  */
            );

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

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);

    /* If the node is selected, allow it to be deselected, whether or
     * not it has a mailbox (if it doesn't, it shouldn't have been
     * selected in the first place, but you never know...). */
    return (path_currently_selected || (mbnode && mbnode->mailbox));
}

GtkWidget *
balsa_mblist_new()
{
    BalsaMBList *new;

    new = g_object_new(balsa_mblist_get_type(), NULL);
    
    return GTK_WIDGET(new);
}

/* callbacks */

/* bmbl_tree_expand
 * bmbl_tree_collapse
 *
 * These are callbacks that sets the expanded flag on the mailbox node, we use
 * this to save whether the folder was expanded or collapsed between
 * sessions. 
 * */
static void
bmbl_tree_expand(GtkTreeView * tree_view, GtkTreeIter * iter,
                    GtkTreePath * path, gpointer data)
{
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    BalsaMailboxNode *mbnode;
    GtkTreeIter child_iter;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    mbnode->expanded = TRUE;
    balsa_mailbox_node_scan_children(mbnode);

    if (!mbnode->mailbox)
        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           ICON_COLUMN,   
                           gtk_widget_render_icon
                           (GTK_WIDGET(balsa_app.main_window),
                            BALSA_PIXMAP_MBOX_DIR_OPEN,
                            GTK_ICON_SIZE_MENU, NULL),
                           -1);

    if (gtk_tree_model_iter_children(model, &child_iter, iter)) {
        do {
            gtk_tree_model_get(model, &child_iter,
                               MBNODE_COLUMN, &mbnode, -1);
            if (mbnode && mbnode->mailbox)
                mbnode->mailbox->view->exposed = TRUE;
        } while (gtk_tree_model_iter_next(model, &child_iter));
    }
}

static void
bmbl_tree_collapse_helper(GtkTreeModel * model, GtkTreeIter * iter)
{
    GtkTreeIter child_iter;

    if (gtk_tree_model_iter_children(model, &child_iter, iter)) {
        do {
            BalsaMailboxNode *mbnode;

            gtk_tree_model_get(model, &child_iter,
                               MBNODE_COLUMN, &mbnode, -1);
            if (mbnode->mailbox)
                mbnode->mailbox->view->exposed = FALSE;
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
    mbnode->expanded = FALSE;

    if (!mbnode->mailbox)
        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           ICON_COLUMN,   
                           gtk_widget_render_icon
                           (GTK_WIDGET(balsa_app.main_window),
                            BALSA_PIXMAP_MBOX_DIR_CLOSED,
                            GTK_ICON_SIZE_MENU, NULL),
                           -1);

    bmbl_tree_collapse_helper(model, iter);
}

/*
 * bmbl_child_toggled_cb: callback for the
 * "row-has-child-toggled" signal.
 *
 * If the mbnode is supposed to be expanded, and the row now has a
 * child, we can actually expand it.
 */
static void
bmbl_child_toggled_cb(GtkTreeModel * model, GtkTreePath * path,
                      GtkTreeIter * iter, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(data);
    BalsaMailboxNode *mbnode = NULL;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    if (mbnode && mbnode->expanded
        && !gtk_tree_view_row_expanded(tree_view, path)) {
        gtk_tree_view_expand_row(tree_view, path, FALSE);
    }
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
    LibBalsaMailbox *m1 = NULL;
    LibBalsaMailbox *m2 = NULL;
    BalsaMailboxNode *mbnode1;
    BalsaMailboxNode *mbnode2;
    gchar *name1, *name2;
    gint core1;
    gint core2;
    gint ret_val = 0;

    gtk_tree_model_get(model, iter1,
                       MBNODE_COLUMN, &mbnode1, NAME_COLUMN, &name1, -1);
    gtk_tree_model_get(model, iter2,
                       MBNODE_COLUMN, &mbnode2, NAME_COLUMN, &name2, -1);

    m1 = mbnode1->mailbox;
    m2 = mbnode2->mailbox;

    switch (sort_column) {
    case BMBL_TREE_COLUMN_NAME:
        /* compare using names, potentially mailboxnodes */
        core1 = bmbl_core_mailbox(m1);
        core2 = bmbl_core_mailbox(m2);
        ret_val = ((core1 || core2) ? core2 - core1
                   : g_ascii_strcasecmp(name1, name2));
        break;

    case BMBL_TREE_COLUMN_UNREAD:
        ret_val = ((m1 ? m1->unread_messages : 0)
                   - (m2 ? m2->unread_messages : 0));
        break;

    case BMBL_TREE_COLUMN_TOTAL:
        ret_val = ((m1 ? libbalsa_mailbox_total_messages(m1) : 0)
                   - (m2 ? libbalsa_mailbox_total_messages(m2) : 0));
        break;
    }

    g_free(name1);
    g_free(name2);
    return ret_val;
}

/* bmbl_button_press_cb:
   handle mouse button press events that occur on mailboxes
   (clicking on folders is passed to GtkTreeView and may trigger expand events
*/
static gboolean
bmbl_button_press_cb(GtkWidget * widget, GdkEventButton * event,
                     gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreePath *path;

    if (event->type != GDK_BUTTON_PRESS || event->button != 3
        || event->window != gtk_tree_view_get_bin_window(tree_view))
        return FALSE;

    if (!gtk_tree_view_get_path_at_pos(tree_view, event->x, event->y,
                                       &path, NULL, NULL, NULL))
        path = NULL;
    bmbl_do_popup(tree_view, path, event);
    /* bmbl_do_popup frees path */

    return TRUE;
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
bmbl_do_popup(GtkTreeView * tree_view, GtkTreePath * path,
              GdkEventButton * event)
{
    BalsaMailboxNode *mbnode = NULL;
    gint event_button;
    guint event_time;

    if (path) {
        GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
        GtkTreeIter iter;

        if (gtk_tree_model_get_iter(model, &iter, path))
            gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
        gtk_tree_path_free(path);
    }

    if (event) {
        event_button = event->button;
        event_time = event->time;
    } else {
        event_button = 0;
        event_time = gtk_get_current_event_time();
    }
    gtk_menu_popup(GTK_MENU(balsa_mailbox_node_get_context_menu(mbnode)),
                   NULL, NULL, NULL, NULL, event_button, event_time);
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
 * Description: This is the drag_data_recieved signal handler for the
 * BalsaMBList.  It retrieves a list of messages terminated by a NULL
 * pointer, converts it to a GList, then transfers it to the selected
 * mailbox.  Depending on what key is held down when the message(s)
 * are dropped they are either copied or moved.  The default action is
 * to copy.
 * */
static void
bmbl_drag_cb(GtkWidget * widget, GdkDragContext * context,
             gint x, gint y, GtkSelectionData * selection_data,
             guint info, guint32 time, gpointer data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreePath *path;
    GtkTreeIter iter;
    LibBalsaMailbox *mailbox;
    LibBalsaMailbox *orig_mailbox;
    GList *messages = NULL;
    BalsaMailboxNode *mbnode;
    LibBalsaMessage **message;



    /* convert pointer array to GList */
    for (message = (LibBalsaMessage **) selection_data->data; *message;
         message++)
        messages = g_list_prepend(messages, *message);
    g_return_if_fail(messages);

    orig_mailbox = ((LibBalsaMessage *) messages->data)->mailbox;

    /* find the node and mailbox */

    /* we should be able to use:
     * gtk_tree_view_get_drag_dest_row(tree_view, &path, NULL);
     * but it sets path to NULL for some reason, so we'll go down to a
     * lower level. */
    if (gtk_tree_view_get_dest_row_at_pos(tree_view,
                                          x, y, &path, NULL)) {
        gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
        mailbox = mbnode->mailbox;

        /* cannot transfer to the originating mailbox */
        if (mailbox != NULL && mailbox != orig_mailbox)
            balsa_index_transfer(balsa_find_index_by_mailbox(orig_mailbox),
                                 messages, mailbox,
                                 context->action != GDK_ACTION_MOVE);
        gtk_tree_path_free(path);
    }

    if (bmbl_prerecursive(model, &iter, bmbl_find_data_func, orig_mailbox)) {
        gtk_tree_selection_select_iter(selection, &iter);
    }

    g_list_free(messages);
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
    GtkTreeIter iter;
    GtkTreeView *tree_view =
        gtk_tree_selection_get_tree_view(selection);
    GtkTreeModel *model =
        gtk_tree_view_get_model(tree_view);
    GtkTreePath *path;

    if (!event)
        return;
    if (event->type != GDK_BUTTON_PRESS
            /* keyboard navigation */
        || event->button.button != 1
            /* soft select */ ) {
        gdk_event_free(event);
        return;
    }

    if (!gtk_tree_view_get_path_at_pos(tree_view, event->button.x,
                                       event->button.y, &path,
                                       NULL, NULL, NULL)) {
        /* GtkTreeView selects the first node in the tree when the
         * widget first gets the focus, whether it's a keyboard event or
         * a button event. If it's a button event, but no mailbox was
         * clicked, we'll just undo that selection and return. */
        gtk_tree_selection_unselect_all(selection);
        gdk_event_free(event);
        return;
    }
    
    if (gtk_tree_selection_path_is_selected(selection, path)) {
        BalsaMailboxNode *mbnode;

        gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
        g_return_if_fail(mbnode != NULL);

        if (mbnode->mailbox)
            balsa_mblist_open_mailbox(mbnode->mailbox);
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

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode != NULL);

    if (mbnode->mailbox)
        balsa_mblist_open_mailbox(mbnode->mailbox);
}

/* Mailbox status changed callbacks: update the UI in an idle handler.
 */

struct update_mbox_data {
    LibBalsaMailbox *mailbox;
    GtkTreeStore *store;
    guint total_messages;
};
static void bmbl_update_mailbox(GtkTreeStore * store,
				LibBalsaMailbox * mailbox,
				gint total_messages);
static gboolean
update_mailbox_idle(struct update_mbox_data*umd)
{
    gdk_threads_enter();
    if (umd->store) {
        g_object_remove_weak_pointer(G_OBJECT(umd->store),
                                     (gpointer) &umd->store);
        bmbl_update_mailbox(umd->store, umd->mailbox, umd->total_messages);
    }
    gdk_threads_leave();
    g_free(umd);
    return FALSE;
}

static void
bmbl_update_mailbox_idle_add(LibBalsaMailbox * mailbox,
			     GtkTreeStore * store)
{
    struct update_mbox_data *umd;
    g_return_if_fail(mailbox);
    g_return_if_fail(store);

    umd = g_new(struct update_mbox_data,1);
    umd->mailbox = mailbox; umd->store = store;
    umd->total_messages = libbalsa_mailbox_total_messages(mailbox);
    g_object_add_weak_pointer(G_OBJECT(store), (gpointer) &umd->store);
    g_idle_add((GSourceFunc)update_mailbox_idle, umd);
}

static void
bmbl_mailbox_changed_cb(LibBalsaMailbox * mailbox, GtkTreeStore * store)
{
    bmbl_update_mailbox_idle_add(mailbox, store);
}

/* public methods */

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
   find all nodes and translate them to mailbox list 
*/
static gboolean
bmbl_find_all_unread_mboxes_func(GtkTreeModel * model,
                                   GtkTreePath * path, GtkTreeIter * iter,
                                   gpointer data)
{
    GList **r = data;
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    if (mbnode->mailbox && mbnode->mailbox->has_unread_messages)
        *r = g_list_prepend(*r, mbnode->mailbox);

    return FALSE;
}

GList *
balsa_mblist_find_all_unread_mboxes(void)
{
    GList *res = NULL;
    GtkTreeModel *model = GTK_TREE_MODEL(balsa_app.mblist);

    gtk_tree_model_foreach(model, bmbl_find_all_unread_mboxes_func, &res);

    return res;
}

/* mblist_open_mailbox
 * 
 * Description: This checks to see if the mailbox is already on a different
 * mailbox page, or if a new page needs to be created and the mailbox
 * parsed.
 */
void
balsa_mblist_open_mailbox(LibBalsaMailbox * mailbox)
{
    int i;
    GNode *gnode;
    GtkWidget *index;
    BalsaMailboxNode *mbnode;

    gdk_threads_leave();
    balsa_mailbox_nodes_lock(FALSE);
    gnode = balsa_find_mailbox(balsa_app.mailbox_nodes, mailbox);
    if (gnode)
        mbnode = gnode->data;
    else {
        mbnode = NULL;
        g_warning(_("Failed to find mailbox"));
    }
    balsa_mailbox_nodes_unlock(FALSE);
    gdk_threads_enter();
    if (!gnode)
        return;

    index = balsa_window_find_current_index(balsa_app.main_window);

    /* If we currently have a page open, update the time last visited */
    if (index) {
	g_get_current_time(&BALSA_INDEX(index)->last_use);
    }
    
    i = balsa_find_notebook_page_num(mailbox);
    if (i != -1) {
	gtk_notebook_set_current_page(GTK_NOTEBOOK(balsa_app.notebook), i);
        index = balsa_window_find_current_index(balsa_app.main_window);
	g_get_current_time(&BALSA_INDEX(index)->last_use);
        balsa_index_set_column_widths(BALSA_INDEX(index));
    } else { /* page with mailbox not found, open it */
	balsa_window_open_mbnode(balsa_app.main_window, mbnode);

	if (balsa_app.mblist->display_info)
	    balsa_mblist_update_mailbox(balsa_app.mblist_tree_store,
                                        mailbox);
    }
    
    balsa_mblist_have_new(balsa_app.mblist_tree_store);
    balsa_mblist_set_status_bar(mailbox);
}

void
balsa_mblist_close_mailbox(LibBalsaMailbox * mailbox)
{
    GNode *gnode;
    BalsaMailboxNode *mbnode;
    
    gdk_threads_leave();
    balsa_mailbox_nodes_lock(FALSE);
    gnode = balsa_find_mailbox(balsa_app.mailbox_nodes,mailbox);
    if (gnode)
        mbnode = gnode->data;
    else {
        mbnode = NULL;
        g_warning(_("Failed to find mailbox"));
    }
    balsa_mailbox_nodes_unlock(FALSE);
    gdk_threads_enter();
    if (!gnode)
        return;

    balsa_window_close_mbnode(balsa_app.main_window, mbnode);
}

/* balsa_mblist_default_signal_bindings:
   connect signals useful for the left-hand side mailbox tree
   but useless for the transfer menu.
*/
void
balsa_mblist_default_signal_bindings(BalsaMBList * mblist)
{
    GtkTreeSelection *selection;

    g_signal_connect(G_OBJECT(mblist), "button_press_event",
                     G_CALLBACK(bmbl_button_press_cb), NULL);
    g_signal_connect_after(G_OBJECT(mblist), "size-allocate",
                           G_CALLBACK(bmbl_column_resize), NULL);
    gtk_tree_view_enable_model_drag_dest(GTK_TREE_VIEW(mblist),
                                         bmbl_drop_types,
                                         ELEMENTS(bmbl_drop_types),
                                         GDK_ACTION_DEFAULT |
                                         GDK_ACTION_COPY |
                                         GDK_ACTION_MOVE);
    g_signal_connect(G_OBJECT(mblist), "drag-data-received",
                     G_CALLBACK(bmbl_drag_cb), NULL);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(mblist));
    g_signal_connect(G_OBJECT(selection), "changed",
                     G_CALLBACK(bmbl_select_mailbox), NULL);
    g_signal_connect(G_OBJECT(mblist), "row-activated",
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
    if (mbnode->mailbox)
        g_signal_handlers_disconnect_by_func(G_OBJECT(mbnode->mailbox),
                                             G_CALLBACK
                                             (bmbl_mailbox_changed_cb),
                                             model);
}

static gboolean
bmbl_disconnect_mailbox_signals(GtkTreeModel *model,
				GtkTreePath *path,
				GtkTreeIter *iter,
				gpointer data)
{
    BalsaMailboxNode *mbnode;
    
    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    bmbl_real_disconnect_mbnode_signals(mbnode, model);
    return FALSE;
}

/* balsa_mblist_repopulate 
 *
 * mblist:  the BalsaMBList that needs recreating.
 *
 * Description: Called whenever a new mailbox is added to the mailbox
 * list, it clears the GtkTreeStore, and draws all the entire tree from
 * scratch.
 * */
void
balsa_mblist_repopulate(GtkTreeStore * store)
{
    g_return_if_fail(GTK_IS_TREE_STORE(store));

    gtk_tree_model_foreach(GTK_TREE_MODEL(store),
			   bmbl_disconnect_mailbox_signals,
			   NULL); 

    gtk_tree_store_clear(store);

    if (balsa_app.mailbox_nodes) {
        balsa_mailbox_nodes_lock(FALSE);
        bmbl_store_add_gnode(store, NULL, balsa_app.mailbox_nodes->children);
        balsa_mailbox_nodes_unlock(FALSE);
    }
    balsa_mblist_have_new(store);
}

/*
 * bmbl_store_add_gnode
 *
 * Make a new row in the GtkTreeStore for gnode and its siblings,
 * and recursively for their children.
 */
static void
bmbl_store_add_gnode(GtkTreeStore * store, GtkTreeIter * parent,
                     GNode * gnode)
{
    GtkTreeIter iter;

    while (gnode) {
        BalsaMailboxNode *mbnode = BALSA_MAILBOX_NODE(gnode->data);

        gtk_tree_store_append(store, &iter, parent);
        bmbl_store_add_mbnode(store, &iter, mbnode);
        bmbl_store_add_gnode(store, &iter, gnode->children);
        gnode = gnode->next;
    }
}

/* bmbl_store_add_mbnode
 * 
 * adds BalsaMailboxNodes to the mailbox list, choosing proper icon for them.
 * returns FALSE on failure (wrong parameters passed).
 * */
static gboolean
bmbl_store_add_mbnode(GtkTreeStore * store, GtkTreeIter * iter,
		      BalsaMailboxNode * mbnode)
{
    const gchar *in;
    gchar *name;

    g_return_val_if_fail(mbnode, FALSE);

    if (mbnode->mailbox) {
        LibBalsaMailbox *mailbox = mbnode->mailbox;

	if (LIBBALSA_IS_MAILBOX_POP3(mailbox)) {
	    g_assert_not_reached();
            in = NULL;
            name = NULL;
        } else {
	    if(mailbox == balsa_app.draftbox)
		in = BALSA_PIXMAP_MBOX_DRAFT;
	    else if(mailbox == balsa_app.inbox)
		in = BALSA_PIXMAP_MBOX_IN;
	    else if(mailbox == balsa_app.outbox)
		in = BALSA_PIXMAP_MBOX_OUT;
	    else if(mailbox == balsa_app.sentbox)
		in = BALSA_PIXMAP_MBOX_SENT;
	    else if(mailbox == balsa_app.trash)
		in = BALSA_PIXMAP_MBOX_TRASH;
	    else
		in = (libbalsa_mailbox_total_messages(mailbox) > 0)
		? BALSA_PIXMAP_MBOX_TRAY_FULL
                : BALSA_PIXMAP_MBOX_TRAY_EMPTY;

            name = g_strdup(mailbox->name);

            /* Make sure the show column is set before showing the
             * mailbox in the list. */
            if (mailbox->view->show == LB_MAILBOX_SHOW_UNSET)
                mailbox->view->show = ((   mailbox == balsa_app.sentbox
                                        || mailbox == balsa_app.draftbox
                                        || mailbox == balsa_app.outbox)
                                       ? LB_MAILBOX_SHOW_TO
                                       : LB_MAILBOX_SHOW_FROM);
	}
	g_signal_connect(G_OBJECT(mailbox), "changed",
			 G_CALLBACK(bmbl_mailbox_changed_cb), store);
    } else {
	/* new directory, but not a mailbox */
        in = mbnode->expanded ? BALSA_PIXMAP_MBOX_DIR_OPEN :
            BALSA_PIXMAP_MBOX_DIR_CLOSED;
        name = g_path_get_basename(mbnode->name);
    }

    gtk_tree_store_set(store, iter,
                       MBNODE_COLUMN, mbnode,
                       ICON_COLUMN,   
                       gtk_widget_render_icon
                           (GTK_WIDGET(balsa_app.main_window), in,
                            GTK_ICON_SIZE_MENU, NULL),
                       NAME_COLUMN,   name,
                       WEIGHT_COLUMN, PANGO_WEIGHT_NORMAL,
                       STYLE_COLUMN, PANGO_STYLE_NORMAL,
                       UNREAD_COLUMN, "",
                       TOTAL_COLUMN,  "",
                       -1);
    g_free(name);
    if (mbnode->mailbox)
	bmbl_node_style(GTK_TREE_MODEL(store), iter, -1);    
    return TRUE;
}

/*
 * bmbl_prerecurs_fn, bmbl_prerecursive
 *
 * Prerecursive traverse of a GtkTreeModel, calling a
 * GtkTreeModelForeachFunc on each node (with a NULL path argument).
 * Traverse terminates if func returns TRUE, and
 * bmbl_prerecursive returns TRUE with iter valid for the
 * terminating node. Returns FALSE if func always returned FALSE, in
 * which case iter is valid for the last node visited, and FALSE if the
 * model is empty, in which case iter isn't valid for anything.
 */
static gboolean
bmbl_prerecurs_fn(GtkTreeModel * model, GtkTreeIter * iter,
                          GtkTreeModelForeachFunc func,
                          gpointer data)
{
    GtkTreeIter child;

    do {
        /* descend to children first */
        if (gtk_tree_model_iter_children(model, &child, iter)
            && bmbl_prerecurs_fn(model, &child, func, data)) {
                *iter = child;
                return TRUE;
            }

        if (func(model, NULL, iter, data))
            return TRUE;
    } while (gtk_tree_model_iter_next(model, iter));

    return FALSE;
}

static gboolean
bmbl_prerecursive(GtkTreeModel * model, GtkTreeIter * iter,
                          GtkTreeModelForeachFunc func,
                          gpointer data)
{
    return (gtk_tree_model_get_iter_first(model, iter)
            && bmbl_prerecurs_fn(model, iter, func, data));
}

/* balsa_mblist_have_new [MBG]
 * 
 * mblist:  The BalsaMBList that you want to check
 * 
 * Description: This function is a wrapper for the recursive call
 * neccessary to check which mailboxes have unread messages, and
 * change the style of them in the mailbox list.
 * Since the mailbox checking can take some time, we implement it as an 
 * idle function.
 * */
static gboolean
bmbl_have_new_func(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
    bmbl_folder_style(model, iter);
    return FALSE;
}

void
balsa_mblist_have_new(GtkTreeStore *store)
{
    GtkTreeIter iter;
    g_return_if_fail(store != NULL);

    /* Update the folder styles based on the mailbox styles */
    bmbl_prerecursive(GTK_TREE_MODEL(store), &iter,
                          bmbl_have_new_func, NULL);
}

/* bmbl_folder_style [MBG]
 * 
 * Description: This is meant to be called as a post recursive
 * function after the mailbox list has been updated style wise to
 * reflect the presence of new messages.  This function will attempt
 * to change the style of any folders to appropriately reflect that of
 * it's sub mailboxes (i.e. if it has one mailbox with unread messages
 * it will be shown as bold.
 * */
static void
bmbl_folder_style(GtkTreeModel * model, GtkTreeIter * iter)
{
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;
    gboolean show_unread;
    gboolean has_unread_child;
    static guint32 has_unread = 0; /*FIXME: is this the right initial value?*/
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    gint depth = gtk_tree_path_get_depth(path);

    gtk_tree_path_free(path);
    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    mailbox = mbnode->mailbox;

    /* ignore special mailboxes */
    if (mailbox == balsa_app.sentbox || mailbox == balsa_app.outbox
        || mailbox == balsa_app.draftbox || mailbox == balsa_app.trash)
        return;

    /* If we're on a mailbox, it must be shown as unread if it or
     * any children have unread mail */
    /* check what this node should look like:
     * first, does it have unread mail? */
    show_unread = mailbox ? mailbox->has_unread_messages : FALSE;

    has_unread_child = has_unread & (1 << (depth + 1));
    if (has_unread_child) {
        /* some child has unread mail */
        show_unread = TRUE;
        has_unread &= ~(1 << (depth + 1));
    }

    if (show_unread) {
        /* propagate up to next level */
        has_unread |= 1 << (depth);
        /* set as unread, even if it already was */
        mbnode->style |= MBNODE_STYLE_NEW_MAIL;
        if (has_unread_child)
	    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
			       STYLE_COLUMN, PANGO_STYLE_OBLIQUE, -1);
    } else if (mbnode->style & MBNODE_STYLE_NEW_MAIL) {
        /* reset to the vanilla style */
        mbnode->style &= ~MBNODE_STYLE_NEW_MAIL;
        gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                           WEIGHT_COLUMN, PANGO_WEIGHT_NORMAL,
                           STYLE_COLUMN, PANGO_STYLE_NORMAL,
                           -1);
    }
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
static gboolean
bmbl_find_data_func(GtkTreeModel * model, GtkTreePath * path,
                         GtkTreeIter * iter, gpointer data)
{
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);

    if (BALSA_IS_MAILBOX_NODE(data))
        return mbnode == data;
    else if (LIBBALSA_IS_MAILBOX(data))
        return (mbnode && mbnode->mailbox == data);

    return FALSE;
}

static void
bmbl_update_mailbox(GtkTreeStore * store, LibBalsaMailbox * mailbox,
		    gint total_messages)
{
    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreeIter iter;
    GtkWidget *bindex;

    /* try and find the mailbox */
    if (!bmbl_prerecursive(model, &iter,
                                   bmbl_find_data_func, mailbox))
        return;

    bmbl_node_style(model, &iter, total_messages);

    /* Do the folder styles as well */
    balsa_mblist_have_new(GTK_TREE_STORE(model));

    bindex = balsa_window_find_current_index(balsa_app.main_window);
    if (!bindex || mailbox != BALSA_INDEX(bindex)->mailbox_node->mailbox)
        return;

    balsa_mblist_set_status_bar(mailbox);
}

void
balsa_mblist_update_mailbox(GtkTreeStore * store,
			    LibBalsaMailbox * mailbox)
{
    bmbl_update_mailbox(store, mailbox, -1);
}

/* bmbl_mbnode_tab_style: find the label widget by recursively searching
 * containers, and set its style.
 *
 * bmbl_mbnode_tab_foreach is the recursive helper.
 */
static void
bmbl_mbnode_tab_foreach(GtkWidget * widget, gpointer data)
{
    if (GTK_IS_CONTAINER(widget))
	gtk_container_foreach((GtkContainer *) widget,
			      bmbl_mbnode_tab_foreach, data);
    else if (GTK_IS_LABEL(widget)) {
	gint unread = GPOINTER_TO_INT(data);
	gchar *str = unread ?
	    g_strconcat("<b>", gtk_label_get_text(GTK_LABEL(widget)),
			"</b>", NULL) :
	    g_strdup(gtk_label_get_text(GTK_LABEL(widget)));
	gtk_label_set_markup(GTK_LABEL(widget), str);
	g_free(str);
    }
}

static void
bmbl_mbnode_tab_style(BalsaMailboxNode * mbnode, gint unread)
{
    BalsaIndex *index;
    GtkWidget *label;

    index = balsa_find_index_by_mailbox(mbnode->mailbox);
    if (index == NULL)
	return;

    label = gtk_notebook_get_tab_label(GTK_NOTEBOOK
				       (balsa_app.main_window->notebook),
				       gtk_widget_get_parent(GTK_WIDGET
							     (index)));

    bmbl_mbnode_tab_foreach(label, GINT_TO_POINTER(unread));
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
bmbl_node_style(GtkTreeModel * model, GtkTreeIter * iter, gint total_messages)
{
    BalsaMailboxNode * mbnode;
    LibBalsaMailbox *mailbox;
    const gchar *icon;
    gchar *text;

    gtk_tree_model_get(model, iter, MBNODE_COLUMN, &mbnode, -1);
    mailbox = mbnode->mailbox;

    if (!(mailbox == balsa_app.sentbox || mailbox == balsa_app.outbox ||
          mailbox == balsa_app.draftbox || mailbox == balsa_app.trash)) {
        if (mailbox->has_unread_messages) {

            /* set the style of the unread maibox list, even if it's already 
             * set... in case the user has changed the colour or font since the
             * last style update */
            icon = BALSA_PIXMAP_MBOX_TRAY_FULL;
            gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                               ICON_COLUMN,
                               gtk_widget_render_icon
                                   (GTK_WIDGET(balsa_app.main_window),
                                    BALSA_PIXMAP_MBOX_TRAY_FULL,
                                    GTK_ICON_SIZE_MENU, NULL),
                               WEIGHT_COLUMN, PANGO_WEIGHT_BOLD, -1);

            /* update the notebook label */
            bmbl_mbnode_tab_style(mbnode, 1);

            mbnode->style |= MBNODE_STYLE_NEW_MAIL;

            /* If we have a count of the unread messages, and we are showing
             * columns, put the number in the unread column */
            if (mailbox->unread_messages > 0) {
                text = g_strdup_printf("%ld", mailbox->unread_messages);
                gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                                   UNREAD_COLUMN, text, -1);
                g_free(text);

                mbnode->style |= MBNODE_STYLE_UNREAD_MESSAGES;
            } else
                gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                                   UNREAD_COLUMN, "", -1);
        } else {
            /* If the clist entry currently has the unread messages icon, set
             * it back, otherwise we can ignore this. */
            if (mbnode->style & MBNODE_STYLE_NEW_MAIL) {
                if (mailbox == balsa_app.inbox)
                    icon = BALSA_PIXMAP_MBOX_IN;
                else
                    icon = BALSA_PIXMAP_MBOX_TRAY_EMPTY;

                gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                                   ICON_COLUMN,
                                   gtk_widget_render_icon
                                       (GTK_WIDGET(balsa_app.main_window),
                                        icon, GTK_ICON_SIZE_MENU, NULL),
                                   WEIGHT_COLUMN, PANGO_WEIGHT_NORMAL,
                                   STYLE_COLUMN, PANGO_STYLE_NORMAL, -1);
                bmbl_mbnode_tab_style(mbnode, 0);

                mbnode->style &= ~MBNODE_STYLE_NEW_MAIL;
            }

            /* If we're showing unread column info, get rid of whatever's
             * there Also set the flag */
            gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                               UNREAD_COLUMN, "0", -1);
            mbnode->style &= ~MBNODE_STYLE_UNREAD_MESSAGES;
        }
    }
    /* We only want to do this if the mailbox is open, otherwise leave
     * the message numbers untouched in the display */
    if (total_messages >= 0 || MAILBOX_OPEN(mailbox)) {
	if (total_messages < 0)
	    total_messages = libbalsa_mailbox_total_messages(mailbox);
        if (total_messages > 0) {
            text =
		g_strdup_printf("%d", total_messages);
            gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                               TOTAL_COLUMN, text, -1);
            g_free(text);

            mbnode->style |= MBNODE_STYLE_TOTAL_MESSAGES;
        } else {
            gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                               TOTAL_COLUMN, "0", -1);
            mbnode->style &= ~MBNODE_STYLE_TOTAL_MESSAGES;
        }
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

    if (bmbl_prerecursive(model, &iter,
                                  bmbl_find_data_func, mailbox)) {
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
balsa_mblist_remove_mailbox_node(GtkTreeStore * store,
                           BalsaMailboxNode* mbnode)
{
    GtkTreeIter iter;

    g_return_val_if_fail(store, FALSE);
    g_return_val_if_fail(mbnode, FALSE);

    if (bmbl_prerecursive(GTK_TREE_MODEL(store), &iter,
                                  bmbl_find_data_func, mbnode)) {
	bmbl_real_disconnect_mbnode_signals(mbnode, GTK_TREE_MODEL(store));
        gtk_tree_store_remove(store, &iter);

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
    GCallback user_func;
    gpointer user_data;
    gchar *url;
    GCallback setup_cb;
};
typedef struct _BalsaMBListMRUEntry BalsaMBListMRUEntry;

/* The callback that's passed in must fit this prototype, although it's
 * cast as a GCallback */
typedef void (*MRUCallback) (gchar * url, gpointer user_data);
/* Callback used internally for letting the option menu know that the
 * option menu needs to be set up */
typedef void (*MRUSetup) (gpointer user_data);

/* Forward references */
static GtkWidget *bmbl_mru_menu(GtkWindow * window, GList ** url_list,
                                GCallback user_func, gpointer user_data,
                                gboolean allow_empty, GCallback setup_cb);
static BalsaMBListMRUEntry *bmbl_mru_new(GList ** url_list,
                                         GCallback user_func,
                                         gpointer user_data,
                                         gchar * url);
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
 * user_func:   called when an item is selected, with the url and
 *              the user_data as arguments;
 * user_data:   passed to the user_func callback.
 *
 * Returns a pointer to a GtkMenu.
 *
 * Takes a list of urls and creates a menu with an entry for each one
 * that resolves to a mailbox, labeled with the mailbox name, with a
 * last entry that pops up the whole mailbox tree. When an item is
 * clicked, user_func is called with the url and user_data as
 * arguments, and the url_list is updated.
 */
GtkWidget *
balsa_mblist_mru_menu(GtkWindow * window, GList ** url_list,
                      GCallback user_func, gpointer user_data)
{
    g_return_val_if_fail(url_list != NULL, NULL);
    return bmbl_mru_menu(window, url_list, user_func, user_data, FALSE,
                         NULL);
}

/*
 * bmbl_mru_menu:
 *
 * window, url_list, user_func, user_data:
 *              as for balsa_mblist_mru_menu;
 * allow_empty: if TRUE, a list with an empty url
 *              will be allowed into the menu;
 * setup_cb:    called when the tree has been displayed, to allow the
 *              display to be reset.
 *
 * Returns the GtkMenu.
 */
static GtkWidget *
bmbl_mru_menu(GtkWindow * window, GList ** url_list,
              GCallback user_func, gpointer user_data,
              gboolean allow_empty, GCallback setup_cb)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;
    GList *list;
    BalsaMBListMRUEntry *mru;

    for (list = *url_list; list; list = g_list_next(list)) {
        gchar *url = list->data;
        LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);

        if (mailbox || (allow_empty && !*url)) {
            mru = bmbl_mru_new(url_list, user_func, user_data, url);
            item =
                gtk_menu_item_new_with_label(mailbox ? mailbox->name : "");
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            g_signal_connect_data(item, "activate",
                                  G_CALLBACK(bmbl_mru_activate_cb), mru,
                                  (GClosureNotify) bmbl_mru_free,
                                  (GConnectFlags) 0);
        }
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());

    mru = bmbl_mru_new(url_list, user_func, user_data, NULL);
    mru->window = window;
    mru->setup_cb = setup_cb;
    item = gtk_menu_item_new_with_mnemonic(_("_Other..."));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect_data(item, "activate",
                          G_CALLBACK(bmbl_mru_show_tree), mru,
                          (GClosureNotify) g_free, (GConnectFlags) 0);

    gtk_widget_show_all(menu);

    return menu;
}

/*
 * bmbl_mru_new:
 *
 * url_list, user_func, user_data:
 *              as for balsa_mblist_mru_menu;
 * url:         url of a mailbox.
 *
 * Returns a newly allocated BalsaMBListMRUEntry structure, initialized
 * with the data.
 */
static BalsaMBListMRUEntry *
bmbl_mru_new(GList ** url_list, GCallback user_func, gpointer user_data,
             gchar * url)
{
    BalsaMBListMRUEntry *mru = g_new(BalsaMBListMRUEntry, 1);

    mru->url_list = url_list;
    mru->user_func = user_func;
    mru->user_data = user_data;
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

    balsa_mblist_mru_add(mru->url_list, mru->url);
    if (mru->user_func)
        ((MRUCallback) mru->user_func) (mru->url, mru->user_data);
}

/*
 * bmbl_mru_show_tree:
 *
 * Callback for the "activate" signal of the last menu item.
 * Pops up a GtkDialog with a BalsaMBList.
 */
static void
bmbl_mru_show_tree(GtkWidget * widget, gpointer data)
{
    BalsaMBListMRUEntry *mru = data;
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(_("Choose destination folder"),
                                    mru->window,
                                    GTK_DIALOG_MODAL,
                                    GTK_STOCK_CANCEL,
                                    GTK_RESPONSE_CANCEL,
                                    NULL);
    GtkWidget *scroll;
    GtkWidget *mblist = balsa_mblist_new();
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(mblist));
    GtkRequisition req;

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    g_signal_connect(selection, "changed",
                     G_CALLBACK(bmbl_mru_selected_cb), data);
    g_signal_connect(mblist, "row-activated",
                     G_CALLBACK(bmbl_mru_activated_cb), data);

    /* Force the mailbox list to be a reasonable size. */
    gtk_widget_size_request(mblist, &req);
    if (req.height > balsa_app.mw_height)
        req.height = balsa_app.mw_height;
    /* For the mailbox list width, we use the one used on the main
     * window. This is the user choice and required because the mblist
     * widget saves the size in balsa_app.mblist_width */
    req.width = balsa_app.mblist_width;
    gtk_widget_set_size_request(GTK_WIDGET(mblist), req.width, req.height);

    gtk_container_add(GTK_CONTAINER(scroll), mblist);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), scroll);
    gtk_widget_show_all(scroll);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (mru->setup_cb)
        ((MRUSetup) mru->setup_cb) (mru->user_data);
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
    GdkEvent *event;
    GtkTreeView *tree_view;
    GtkTreePath *path;
    GtkTreeViewColumn *column;
    GdkRectangle rect;
    GValue expander_size;

    if (!data)
        return;

    event = gtk_get_current_event();
    if (!event)
        return;

    tree_view = gtk_tree_selection_get_tree_view(selection);
    if (event->type != GDK_BUTTON_PRESS ||
        !gtk_tree_view_get_path_at_pos(tree_view, event->button.x,
                                       event->button.y, &path,
                                       &column, NULL, NULL)) {
        gtk_tree_selection_unselect_all(selection);
        gdk_event_free(event);
        return;
    }

    gtk_tree_view_get_cell_area(tree_view, path, column, &rect);
    g_value_init(&expander_size, G_TYPE_INT);
    gtk_widget_style_get_property((GtkWidget *) tree_view, "expander-size",
                                  &expander_size);
    if (event->button.x < rect.x + g_value_get_int(&expander_size))
        gtk_tree_selection_unselect_all(selection);
    else if (gtk_tree_selection_path_is_selected(selection, path)) {
        GtkTreeModel *model;
        GtkTreeIter iter;
        BalsaMailboxNode *mbnode;

        model = gtk_tree_view_get_model(tree_view);
        gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_model_get(model, &iter, 0, &mbnode, -1);
        ((BalsaMBListMRUEntry *) data)->url = g_strdup(mbnode->mailbox->url);
        bmbl_mru_activate_cb(NULL, data);

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
    GtkTreeIter iter;
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, MBNODE_COLUMN, &mbnode, -1);
    g_return_if_fail(mbnode != NULL);

    if (mbnode->mailbox) {
        ((BalsaMBListMRUEntry *) data)->url =
            g_strdup(mbnode->mailbox->url);
        bmbl_mru_activate_cb(NULL, data);

        gtk_dialog_response(GTK_DIALOG
                            (gtk_widget_get_ancestor
                             (GTK_WIDGET(tree_view), GTK_TYPE_DIALOG)),
                            GTK_RESPONSE_OK);
    }
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

/* balsa_mblist_mru_option_menu: create a GtkOptionMenu to manage an MRU
 * list */

/* The info that needs to be passed around */
struct _BalsaMBListMRUOption {
    GtkWindow *window;
    GList **url_list;
    GtkOptionMenu *option_menu;
    gchar *url;
};
typedef struct _BalsaMBListMRUOption BalsaMBListMRUOption;

/* Forward references */
static void bmbl_mru_option_menu_setup(BalsaMBListMRUOption * mro);
static void bmbl_mru_option_menu_init(BalsaMBListMRUOption * mro);
static void bmbl_mru_option_menu_cb(const gchar * url, gpointer data);
static void bmbl_mru_option_menu_destroy_cb(BalsaMBListMRUOption * mro);

/*
 * balsa_mblist_mru_option_menu:
 *
 * window:      parent window for the dialog;
 * url_list:    pointer to a list of urls;
 *
 * Returns a GtkOptionMenu.
 *
 * Takes a list of urls and creates an option menu with an entry for
 * each one that resolves to a mailbox, labeled with the mailbox name,
 * including the special case of an empty url and a NULL mailbox.
 * Adds a last entry that pops up the whole mailbox tree. When an item
 * is clicked, the corresponding url is stored in *url,
 * and the url_list is updated.
 */
GtkWidget *
balsa_mblist_mru_option_menu(GtkWindow * window, GList ** url_list)
{
    GtkWidget *option_menu;
    BalsaMBListMRUOption *mro;

    g_return_val_if_fail(url_list != NULL, NULL);

    option_menu = gtk_option_menu_new();
    mro = g_new(BalsaMBListMRUOption, 1);

    mro->window = window;
    mro->url_list = url_list;
    mro->option_menu = GTK_OPTION_MENU(option_menu);
    mro->url = NULL;
    bmbl_mru_option_menu_setup(mro);
    bmbl_mru_option_menu_init(mro);
    g_object_set_data_full(G_OBJECT(option_menu), "mro", mro, 
                           (GDestroyNotify) bmbl_mru_option_menu_destroy_cb);

    return option_menu;
}

/*
 * balsa_mblist_mru_option_menu_set
 *
 * option_menu: a GtkOptionMenu created by balsa_mblist_mru_option_menu;
 * url:         URL of a mailbox
 *
 * Adds url to the front of the url_list managed by option_menu, resets
 * option_menu to show the new url, and stores a copy in the mro
 * structure.
 */
void
balsa_mblist_mru_option_menu_set(GtkWidget * option_menu, const gchar *url)
{
    BalsaMBListMRUOption *mro =
        g_object_get_data(G_OBJECT(option_menu), "mro");
    
    balsa_mblist_mru_add(mro->url_list, url);
    bmbl_mru_option_menu_setup(mro);
    bmbl_mru_option_menu_cb(url, mro);
}

/*
 * balsa_mblist_mru_option_menu_get
 *
 * option_menu: a GtkOptionMenu created by balsa_mblist_mru_option_menu.
 *
 * Returns the address of the current URL.
 *
 * Note that the url is held in the mro structure, and is freed when the
 * widget is destroyed. The calling code must make its own copy of the
 * string if it is needed more than temporarily.
 */
const gchar *
balsa_mblist_mru_option_menu_get(GtkWidget * option_menu)
{
    BalsaMBListMRUOption *mro =
        g_object_get_data(G_OBJECT(option_menu), "mro");

    return mro->url;
}

/*
 * bmbl_mru_option_menu_setup:
 *
 * mro:         pointer to a BalsaMBListMRUOption structure.
 *
 * Creates the menu for the option menu, and sets it. Called by
 * balsa_mblist_mru_option_menu, and also passed to bmbl_mru_menu
 * as the setup_cb.
 */
static void
bmbl_mru_option_menu_setup(BalsaMBListMRUOption * mro)
{
    GtkOptionMenu *option_menu = mro->option_menu;
    GtkWidget *menu = bmbl_mru_menu(mro->window, mro->url_list,
                                    G_CALLBACK(bmbl_mru_option_menu_cb),
                                    mro, TRUE,
                                    G_CALLBACK
                                    (bmbl_mru_option_menu_setup));

    gtk_option_menu_set_menu(option_menu, menu);
}

/*
 * bmbl_mru_option_menu_cb:
 *
 * Callback passed to bmbl_mru_menu.
 */
static void
bmbl_mru_option_menu_cb(const gchar * url, gpointer data)
{
    BalsaMBListMRUOption *mro = (BalsaMBListMRUOption *) data;

    g_free(mro->url);
    mro->url = g_strdup(url);
}

/*
 * bmbl_mru_option_menu_destroy_cb:
 *
 * Free the allocated BalsaMBListMRUOption structure.
 */
static void
bmbl_mru_option_menu_destroy_cb(BalsaMBListMRUOption * mro)
{
    g_free(mro->url);
    g_free(mro);
}

/* Initialize mro->url by activating the top menu item on
 * mro->option_menu.
 */
static void
bmbl_mru_option_menu_init(BalsaMBListMRUOption * mro)
{
    GtkWidget *menu =
	    gtk_option_menu_get_menu(GTK_OPTION_MENU(mro->option_menu));
    GList *children = GTK_MENU_SHELL(menu)->children;
    if (g_list_length(children) > 1) {
        GtkMenuItem *item = children->data;
        gtk_menu_item_activate(item);
    }
}

void
balsa_mblist_set_status_bar(LibBalsaMailbox * mailbox)
{
    gchar *desc =
        g_strdup_printf(_("Shown mailbox: %s with %d messages, %ld new"),
			mailbox->name,
			libbalsa_mailbox_total_messages(mailbox),
                        mailbox->unread_messages);

    gnome_appbar_set_default(balsa_app.appbar, desc);
    g_free(desc);
}

static void
bmbl_expand_to_row(BalsaMBList * mblist, GtkTreePath * path)
{
    GtkTreePath *tmp = gtk_tree_path_copy(path);

    if (gtk_tree_path_up(tmp) && gtk_tree_path_get_depth(tmp) > 0
        && !gtk_tree_view_row_expanded(GTK_TREE_VIEW(mblist), tmp)) {
        bmbl_expand_to_row(mblist, tmp);
        gtk_tree_view_expand_row(GTK_TREE_VIEW(mblist), tmp, FALSE);
    }

    gtk_tree_path_free(tmp);
}
