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

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "main-window.h"
#include "message-window.h"
#include "sendmsg-window.h"
#include "toolbar-factory.h"
#include "toolbar-prefs.h"

#if !GTK_CHECK_VERSION(2, 14, 0)
#ifdef HAVE_GNOME
#include <gnome.h>
#endif
#endif                          /* GTK_CHECK_VERSION(2, 14, 0) */

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define OLD_BALSA_COMPATIBILITY_TRANSLATION
#ifdef OLD_BALSA_COMPATIBILITY_TRANSLATION
#include <gnome.h> /* for GNOME_STOCK_* pixmaps */
#endif

/* Enumeration for GtkTreeModel columns. */
enum {
    TP_TEXT_COLUMN,
    TP_ICON_COLUMN,
    TP_ITEM_COLUMN,
    TP_N_COLUMNS
};

static GtkWidget *customize_widget;

/* Structure associated with each notebook page. */
typedef struct ToolbarPage_ ToolbarPage;
struct ToolbarPage_ {
    BalsaToolbarModel *model;
    GtkWidget *available;
    GtkWidget *current;
    GtkWidget *toolbar;
    GtkWidget *add_button;
    GtkWidget *remove_button;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    GtkWidget *standard_button;
};

/* Callbacks. */
static void tp_dialog_response_cb(GtkDialog * dialog, gint response,
                                  gpointer data);
static void add_button_cb(GtkWidget *, ToolbarPage * page);
static void remove_button_cb(GtkWidget *, ToolbarPage * page);
static void back_button_cb(GtkWidget *, ToolbarPage * page);
static void forward_button_cb(GtkWidget *, ToolbarPage * page);
static void wrap_toggled_cb(GtkWidget * widget, GtkNotebook * notebook);
static void available_selection_changed_cb(GtkTreeSelection * selection,
                                           ToolbarPage * page);
static void current_selection_changed_cb(GtkTreeSelection * selection,
                                         ToolbarPage * page);
static void available_row_activated_cb(GtkTreeView * treeview,
                                       GtkTreeIter * arg1,
                                       GtkTreePath * arg2,
                                       ToolbarPage * page);
static void current_row_activated_cb(GtkTreeView * treeview,
                                     GtkTreeIter * arg1,
                                     GtkTreePath * arg2,
                                     ToolbarPage * page);

/* Helpers. */
static GtkWidget *create_toolbar_page(BalsaToolbarModel * model,
                                      GtkUIManager * ui_manager);
static GtkWidget *tp_list_new(void);
static gboolean tp_list_iter_is_first(GtkWidget * list, GtkTreeIter * iter);
static gboolean tp_list_iter_is_last(GtkWidget * list, GtkTreeIter * iter);
static void tp_page_refresh_available(ToolbarPage * page);
static void tp_page_refresh_current(ToolbarPage * page);
static void tp_page_refresh_preview(ToolbarPage * page);
static void tp_page_swap_rows(ToolbarPage * page, gboolean forward);
static void tp_page_add_selected(ToolbarPage * page);
static void tp_page_remove_selected(ToolbarPage * page);
static void tp_store_set(GtkListStore * store, GtkTreeIter * iter,
                         gint item);
static void replace_nl_with_space(char* str);

/* Public methods. */

/* create the toolbar-customization dialog
 */
void
customize_dialog_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *notebook;
    GtkWidget *child;
    GtkWidget *option_frame;
    GtkWidget *option_box;
    GtkWidget *wrap_button;
    GtkWidget *active_window = data;
    BalsaToolbarModel *model;
    BalsaToolbarType   type;
    GtkUIManager * ui_manager;

    /* There can only be one */
    if (customize_widget) {
        gdk_window_raise(customize_widget->window);
        return;
    }

    customize_widget =
        gtk_dialog_new_with_buttons(_("Customize Toolbars"),
                                    GTK_WINDOW(active_window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                    GTK_STOCK_HELP,  GTK_RESPONSE_HELP,
                                    NULL);
    g_object_add_weak_pointer(G_OBJECT(customize_widget),
                              (gpointer) & customize_widget);
    g_signal_connect(G_OBJECT(customize_widget), "response",
                     G_CALLBACK(tp_dialog_response_cb), NULL);

    notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(customize_widget)->vbox),
                      notebook);

    gtk_window_set_wmclass(GTK_WINDOW(customize_widget), "customize",
                           "Balsa");
    gtk_window_set_default_size(GTK_WINDOW(customize_widget), 600, 440);

    /* The order of pages must be consistent with the BalsaToolbarType
     * enum. */
    model = balsa_window_get_toolbar_model();
    ui_manager = balsa_window_ui_manager_new(NULL);
    child = create_toolbar_page(model, ui_manager);
    g_object_unref(ui_manager);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child,
                             gtk_label_new(_("Main window")));

    model = sendmsg_window_get_toolbar_model();
    ui_manager = sendmsg_window_ui_manager_new(NULL);
    child = create_toolbar_page(model, ui_manager);
    g_object_unref(ui_manager);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child,
                             gtk_label_new(_("Compose window")));

    model = message_window_get_toolbar_model();
    ui_manager = message_window_ui_manager_new(NULL);
    child = create_toolbar_page(model, ui_manager);
    g_object_unref(ui_manager);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), child,
                             gtk_label_new(_("Message window")));

    option_frame = gtk_frame_new(_("Toolbar options"));
    gtk_container_set_border_width(GTK_CONTAINER(option_frame), 6);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(customize_widget)->vbox),
                       option_frame, FALSE, FALSE, 0);

    option_box = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(option_box), 6);
    gtk_container_add(GTK_CONTAINER(option_frame), option_box);
    
    wrap_button =
        gtk_check_button_new_with_mnemonic(_("_Wrap button labels"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap_button),
                                 balsa_app.toolbar_wrap_button_text);
    g_signal_connect(G_OBJECT(wrap_button), "toggled",
                     G_CALLBACK(wrap_toggled_cb), notebook);
    gtk_box_pack_start(GTK_BOX(option_box), wrap_button, FALSE, FALSE, 0);

    gtk_widget_show_all(customize_widget);

    /* Now that the pages are shown, we can switch to the page
     * corresponding to the toolbar that the user clicked on. */
    type =
        GPOINTER_TO_INT(g_object_get_data
                        (G_OBJECT(widget), BALSA_TOOLBAR_MODEL_TYPE));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), type);
}

/* get_toolbar_button_index:
   id - button id
   returns -1 on failure.
*/
int
get_toolbar_button_index(const char *id)
{
#ifdef OLD_BALSA_COMPATIBILITY_TRANSLATION
    static const struct {
        gchar *new;
        gchar *old;
    } button_converter[] = {
        { BALSA_PIXMAP_ATTACHMENT,   GNOME_STOCK_ATTACH },
        { BALSA_PIXMAP_COMPOSE,      GNOME_STOCK_MAIL_NEW },
        { BALSA_PIXMAP_CONTINUE,     GNOME_STOCK_MAIL },
        { BALSA_PIXMAP_RECEIVE,      GNOME_STOCK_MAIL_RCV },
        { BALSA_PIXMAP_REPLY,        GNOME_STOCK_MAIL_RPL },
        { BALSA_PIXMAP_REPLY_ALL,    "reply_to_all" },
        { BALSA_PIXMAP_REPLY_GROUP,  "reply_to_group" },
        { BALSA_PIXMAP_FORWARD,      GNOME_STOCK_MAIL_FWD },
        { BALSA_PIXMAP_NEXT,         GTK_STOCK_GO_FORWARD },
        { BALSA_PIXMAP_PREVIOUS,     GTK_STOCK_GO_BACK },
	{ GTK_STOCK_PRINT,           BALSA_OLD_PIXMAP_PRINT },
	{ GTK_STOCK_SAVE,            BALSA_OLD_PIXMAP_SAVE },
        { BALSA_PIXMAP_SEND,         GNOME_STOCK_MAIL_SND },
	{ GTK_STOCK_DELETE,          BALSA_OLD_PIXMAP_TRASH },
        { BALSA_PIXMAP_TRASH_EMPTY,  "empty_trash" },
        { BALSA_PIXMAP_NEXT_UNREAD,  "next_unread" },
        { BALSA_PIXMAP_NEXT_FLAGGED, "next_flagged" },
        { BALSA_PIXMAP_SHOW_HEADERS, "show_all_headers" },
        { BALSA_PIXMAP_SHOW_PREVIEW, "show_preview" },
        { BALSA_PIXMAP_MARKED_NEW,   "flag_unread" },
        { BALSA_PIXMAP_MARK_ALL,     "mark_all" },
        { BALSA_PIXMAP_IDENTITY,     "identity" },
	{ GTK_STOCK_CANCEL,          BALSA_OLD_PIXMAP_CLOSE_MBOX },
        { NULL, NULL }
    };
#endif
    int i;

    g_return_val_if_fail(id, -1);

    for(i=0; i<toolbar_button_count; i++) {
	if(!strcmp(id, toolbar_buttons[i].pixmap_id))
	    return i;
    }
#ifdef OLD_BALSA_COMPATIBILITY_TRANSLATION
    /* you have got a second chance.... */
    
    for(i=0; button_converter[i].new; i++) {
        if(!strcmp(id, button_converter[i].old)) {
            int j;
            for(j=0; j<toolbar_button_count; j++) {
                if(!strcmp(button_converter[i].new,
                           toolbar_buttons[j].pixmap_id))
                    return j;
            }
            return -1;
        }
    }
#endif
    return -1;
}

/* Callbacks. */

#define BALSA_KEY_TOOLBAR_PAGE "balsa-toolbar-page"

/* Callback for the wrap_buttons' "toggled" signal. */
static void
wrap_toggled_cb(GtkWidget * widget, GtkNotebook * notebook)
{
    gint i;
    GtkWidget *child;
    ToolbarPage *page;
    
    balsa_app.toolbar_wrap_button_text = GTK_TOGGLE_BUTTON(widget)->active;

    for (i = 0; (child = gtk_notebook_get_nth_page(notebook, i)); i++) {
        page = g_object_get_data(G_OBJECT(child), BALSA_KEY_TOOLBAR_PAGE);
        balsa_toolbar_model_changed(page->model);
    }
}

/* Button callbacks: each makes the appropriate change to the
 * page->current GtkTreeView, then refreshes the page's
 * BalsaToolbarModel and GtkToolbar; add_button_cb and remove_button_cb
 * also refresh the page's available GtkTreeView.
 */
static void
back_button_cb(GtkWidget * widget, ToolbarPage * page)
{
    tp_page_swap_rows(page, FALSE);
}

static void
forward_button_cb(GtkWidget * widget, ToolbarPage * page)
{
    tp_page_swap_rows(page, TRUE);
}

static void
add_button_cb(GtkWidget *widget, ToolbarPage * page)
{
    tp_page_add_selected(page);
}

static void
remove_button_cb(GtkWidget *widget, ToolbarPage * page)
{
    tp_page_remove_selected(page);
}

static void
standard_button_cb(GtkWidget *widget, ToolbarPage * page)
{
    balsa_toolbar_model_clear(page->model);
    gtk_widget_set_sensitive(page->standard_button, FALSE);
    tp_page_refresh_available(page);
    tp_page_refresh_current(page);
    balsa_toolbar_model_changed(page->model);
}

static void
style_button_cb(GtkWidget *widget, ToolbarPage * page)
{
    gboolean handled;

    g_signal_emit_by_name(page->toolbar, "popup-menu", &handled);
}

/* Callback for the "row-activated" signal for the available list. */
static void
available_row_activated_cb(GtkTreeView * treeview, GtkTreeIter * arg1,
                        GtkTreePath * arg2, ToolbarPage * page)
{
    tp_page_add_selected(page);
}

/* Callback for the selection "changed" signal for the available list. */
static void
available_selection_changed_cb(GtkTreeSelection * selection,
                            ToolbarPage * page)
{
    gtk_widget_set_sensitive(page->add_button,
                             gtk_tree_selection_get_selected(selection,
                                                             NULL, NULL));
}

/* Callback for the "row-activated" signal for the current list. */
static void
current_row_activated_cb(GtkTreeView * treeview, GtkTreeIter * arg1,
                      GtkTreePath * arg2, ToolbarPage * page)
{
    tp_page_remove_selected(page);
}

/* Callback for the selection "changed" signal for the destination list. */
static void
current_selection_changed_cb(GtkTreeSelection * selection, ToolbarPage * page)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean remove = FALSE;
    gboolean back = FALSE;
    gboolean forward = FALSE;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        remove = TRUE;
        back =
            !tp_list_iter_is_first(page->current,
                                   &iter);
        forward =
            !tp_list_iter_is_last(page->current,
                                  &iter);
    }
    gtk_widget_set_sensitive(page->remove_button, remove);
    gtk_widget_set_sensitive(page->back_button, back);
    gtk_widget_set_sensitive(page->forward_button, forward);
}

/* Callback for the "response" signal of the dialog. */
static void
tp_dialog_response_cb(GtkDialog * dialog, gint response, gpointer data)
{
#if GTK_CHECK_VERSION(2, 14, 0) || HAVE_GNOME
    GError *err = NULL;
#endif

    switch (response) {
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CLOSE:
        gtk_widget_destroy(GTK_WIDGET(dialog));
        break;
    case GTK_RESPONSE_HELP:
#if GTK_CHECK_VERSION(2, 14, 0) || HAVE_GNOME
#if GTK_CHECK_VERSION(2, 14, 0)
        gtk_show_uri(NULL, "ghelp:balsa?toolbar-prefs",
                     gtk_get_current_event_time(), &err);
#else                           /* GTK_CHECK_VERSION(2, 14, 0) */
        gnome_help_display("balsa", "toolbar-prefs", &err);
#endif                          /* GTK_CHECK_VERSION(2, 14, 0) */
        if (err) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
		    _("Error displaying toolbar help: %s\n"),
		    err->message);
            g_error_free(err);
        }
#endif
        break;
    default:
        break;
    }
}

/* Helpers. */

/* Create a page for the main notebook.
 */
static GtkWidget*
create_toolbar_page(BalsaToolbarModel * model, GtkUIManager * ui_manager)
{
    GtkWidget *outer_box;
    GtkWidget *toolbar_frame, *toolbar_scroll;
    GtkWidget *toolbar_ctlbox;
    GtkWidget *lower_ctlbox, *button_box, *move_button_box, *center_button_box;
    GtkWidget *list_frame, *list_scroll;
    GtkWidget *destination_frame, *destination_scroll;
    GtkWidget *style_button;
    ToolbarPage *page;
    GtkTreeSelection *selection;

    page = g_new(ToolbarPage, 1);
    page->model = model;

    /* The "window itself" */
    outer_box=gtk_vbox_new(FALSE, 0);
    g_object_set_data_full(G_OBJECT(outer_box), BALSA_KEY_TOOLBAR_PAGE,
                           page, g_free);

    /* Preview display */
    toolbar_frame=gtk_frame_new(_("Preview"));
    gtk_container_set_border_width(GTK_CONTAINER(toolbar_frame), 5);
    gtk_box_pack_start(GTK_BOX(outer_box), toolbar_frame, FALSE, FALSE, 0);

    toolbar_ctlbox=gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(toolbar_frame), toolbar_ctlbox);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar_ctlbox), 5);

    /* The ui-manager has actions but no ui, so we add an empty toolbar. */
    gtk_ui_manager_add_ui_from_string(ui_manager,
                                      "<ui>"
                                      "  <toolbar name='Toolbar'/>"
                                      "</ui>",
                                      -1, NULL);

    /* The preview is an actual, fully functional toolbar */
    page->toolbar = balsa_toolbar_new(model, ui_manager);
    gtk_widget_set_sensitive(page->toolbar, FALSE);

    /* embedded in a scrolled_window */
    toolbar_scroll=gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(toolbar_scroll),
				   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_NEVER);

    gtk_box_pack_start(GTK_BOX(toolbar_ctlbox), toolbar_scroll,
                       TRUE, TRUE, 0);

    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(toolbar_scroll),
                                          page->toolbar);

    /* Button box */
    button_box = gtk_hbutton_box_new();
    gtk_box_pack_start(GTK_BOX(toolbar_ctlbox), button_box, FALSE, FALSE, 0);

    /* Standard button */
    page->standard_button =
        gtk_button_new_with_mnemonic(_("_Restore toolbar to standard buttons"));
    gtk_container_add(GTK_CONTAINER(button_box), page->standard_button);

    /* Style button */
    style_button = gtk_button_new_with_mnemonic(_("Toolbar _style..."));
    gtk_container_add(GTK_CONTAINER(button_box), style_button);

    /* Done with preview */
	
    /* Box for lower half of window */
    lower_ctlbox=gtk_hbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(lower_ctlbox), 5);

    gtk_box_pack_start(GTK_BOX(outer_box), lower_ctlbox, TRUE, TRUE, 0);

    /* A list to show the available items */
    list_scroll=gtk_scrolled_window_new(NULL, NULL);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scroll),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    list_frame=gtk_frame_new(_("Available buttons"));
    page->available = tp_list_new();

    gtk_box_pack_start(GTK_BOX(lower_ctlbox), list_frame,
		       TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(list_frame), list_scroll);
    gtk_container_add(GTK_CONTAINER(list_scroll), page->available);

    /* Done with available list */

    /* Another list to show the current tools */
    destination_scroll=gtk_scrolled_window_new(NULL, NULL);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(destination_scroll),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	
    destination_frame=gtk_frame_new(_("Current toolbar"));
    page->current = tp_list_new();

    /* Done with destination list */

    /* Button box */
    center_button_box=gtk_vbox_new(TRUE, 0);

    button_box=gtk_vbox_new(FALSE, 0);

    gtk_box_pack_start(GTK_BOX(lower_ctlbox), center_button_box,
		       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(center_button_box), button_box,
		       FALSE, FALSE, 0);

    page->back_button =
        balsa_stock_button_with_label(GTK_STOCK_GO_UP, _("Up"));
    gtk_box_pack_start(GTK_BOX(button_box), page->back_button, FALSE, FALSE, 0);

    move_button_box=gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), move_button_box, FALSE, FALSE, 0);

    page->remove_button =
        balsa_stock_button_with_label(GTK_STOCK_GO_BACK, "-");
    gtk_box_pack_start(GTK_BOX(move_button_box), page->remove_button,
                       FALSE, FALSE, 0);

    page->add_button =
        balsa_stock_button_with_label(GTK_STOCK_GO_FORWARD, "+");
    gtk_box_pack_start(GTK_BOX(move_button_box), page->add_button, FALSE, FALSE, 0);

    page->forward_button =
        balsa_stock_button_with_label(GTK_STOCK_GO_DOWN, _("Down"));
    gtk_box_pack_start(GTK_BOX(button_box), page->forward_button, FALSE, FALSE, 0);

    /* Pack destination list */
    gtk_box_pack_start(GTK_BOX(lower_ctlbox), destination_frame, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(destination_frame), destination_scroll);
    gtk_container_add(GTK_CONTAINER(destination_scroll), page->current);

    /* UI signals */
    g_signal_connect(G_OBJECT(page->available), "row-activated",
                     G_CALLBACK(available_row_activated_cb), page);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->available));
    g_signal_connect(G_OBJECT(selection), "changed",
		       G_CALLBACK(available_selection_changed_cb), page);

    g_signal_connect(G_OBJECT(page->current), "row-activated",
                     G_CALLBACK(current_row_activated_cb), page);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->current));
    g_signal_connect(G_OBJECT(selection), "changed",
		       G_CALLBACK(current_selection_changed_cb), page);

    g_signal_connect(G_OBJECT(page->add_button), "clicked",
		       G_CALLBACK(add_button_cb), page);
    g_signal_connect(G_OBJECT(page->remove_button), "clicked",
		       G_CALLBACK(remove_button_cb), page);
    g_signal_connect(G_OBJECT(page->forward_button), "clicked",
		       G_CALLBACK(forward_button_cb), page);
    g_signal_connect(G_OBJECT(page->back_button), "clicked",
		       G_CALLBACK(back_button_cb), page);

    g_signal_connect(G_OBJECT(page->standard_button), "clicked",
		       G_CALLBACK(standard_button_cb), page);
    g_signal_connect(G_OBJECT(style_button), "clicked",
		       G_CALLBACK(style_button_cb), page);

    gtk_widget_set_sensitive(page->add_button, FALSE);
    gtk_widget_set_sensitive(page->remove_button, FALSE);
    gtk_widget_set_sensitive(page->back_button, FALSE);
    gtk_widget_set_sensitive(page->forward_button, FALSE);
    gtk_widget_set_sensitive(page->standard_button,
                             !balsa_toolbar_model_is_standard(model));

    tp_page_refresh_available(page);
    tp_page_refresh_current(page);

    return outer_box;
}

/* Refresh the page's available GtkTreeView.
 */
static void
tp_page_refresh_available(ToolbarPage * page)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(page->available));
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    GHashTable *legal = balsa_toolbar_model_get_legal(page->model);
    GSList *current = balsa_toolbar_model_get_current(page->model);
    int item;

    /* save currently selected path, or point path to the first row */
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        path = gtk_tree_model_get_path(model, &iter);
    else {
        path = gtk_tree_path_new();
        gtk_tree_path_down(path);
    }
    gtk_list_store_clear(GTK_LIST_STORE(model));

    for (item = 0; item < toolbar_button_count; item++) {
        if (item > 0
            && (!g_hash_table_lookup(legal,
                                     toolbar_buttons[item].pixmap_id)
                || g_slist_find_custom(current,
                                       toolbar_buttons[item].pixmap_id,
                                       (GCompareFunc) strcmp)))
            continue;

        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        tp_store_set(GTK_LIST_STORE(model), &iter, item);
    }

    if (gtk_tree_model_get_iter(model, &iter, path)
        || gtk_tree_path_prev(path)) {
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->available),
                                     path, NULL, FALSE, 0, 0);
    }
    gtk_tree_path_free(path);
}

/* Refresh the page's current GtkTreeView.
 */
static void
tp_page_refresh_current(ToolbarPage * page)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(page->current));
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    int item;
    GSList *list;

    /* save currently selected path, or point path to the first row */
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        path = gtk_tree_model_get_path(model, &iter);
    else {
        path = gtk_tree_path_new();
        gtk_tree_path_down(path);
    }
    gtk_list_store_clear(GTK_LIST_STORE(model));

    for (list = balsa_toolbar_model_get_current(page->model); list;
         list = g_slist_next(list)) {
        item = get_toolbar_button_index(list->data);
        if (item < 0)
            continue;

        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        tp_store_set(GTK_LIST_STORE(model), &iter, item);
    }

    if (gtk_tree_model_get_iter(model, &iter, path) 
        || gtk_tree_path_prev(path)) {
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->current),
                                     path, NULL, FALSE, 0, 0);
    }
    gtk_tree_path_free(path);
}

/* Refresh a page after its destination TreeView has changed.
 */
static void
tp_page_refresh_preview(ToolbarPage * page)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(page->current));
    GtkTreeIter iter;
    gboolean valid;
    
    balsa_toolbar_model_clear(page->model);
    for (valid = gtk_tree_model_get_iter_first(model, &iter);
         valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        gint item;

        gtk_tree_model_get(model, &iter, TP_ITEM_COLUMN, &item, -1);
        if (item >= 0 && item < toolbar_button_count)
            balsa_toolbar_model_insert_icon(page->model, 
                                            toolbar_buttons[item].pixmap_id,
                                            -1);
    }
}

/* Create a GtkTreeView for an icon list.
 */
static GtkWidget *
tp_list_new(void)
{
    GtkListStore *list_store;
    GtkTreeView *view;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    list_store = gtk_list_store_new(TP_N_COLUMNS,
                                    G_TYPE_STRING,   /* TP_TEXT_COLUMN */
                                    GDK_TYPE_PIXBUF, /* TP_ICON_COLUMN */
                                    G_TYPE_INT       /* TP_ITEM_COLUMN */
                                    );
    view = GTK_TREE_VIEW(gtk_tree_view_new_with_model
                         (GTK_TREE_MODEL(list_store)));
    g_object_unref(list_store);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(NULL, renderer, "text",
                                                 TP_TEXT_COLUMN, NULL);
    gtk_tree_view_append_column(view, column);

    renderer = gtk_cell_renderer_pixbuf_new();
    column =
        gtk_tree_view_column_new_with_attributes(NULL, renderer, "pixbuf",
                                                 TP_ICON_COLUMN, NULL);
    gtk_tree_view_append_column(view, column);

    gtk_tree_view_set_headers_visible(view, FALSE);
    return GTK_WIDGET(view);
}

/* Test whether the iter addresses the first row.
 */
static gboolean 
tp_list_iter_is_first(GtkWidget * list, GtkTreeIter * iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    GtkTreePath *path = gtk_tree_model_get_path(model, iter);
    gboolean ret_val = !gtk_tree_path_prev(path);
    gtk_tree_path_free(path);
    return ret_val;
}

/* Test whether the iter addresses the last row.
 */
static gboolean 
tp_list_iter_is_last(GtkWidget * list, GtkTreeIter * iter)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    GtkTreeIter tmp_iter = *iter;
    return !gtk_tree_model_iter_next(model, &tmp_iter);
}

/* Swap the currently selected row in page->current
 * with the next or previous row.
 */
static void
tp_page_swap_rows(ToolbarPage * page, gboolean forward)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(page->current));
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(page->current));
    GtkTreeIter iter;
    GtkTreeIter tmp_iter;
    GtkTreePath *tmp_path;
    gint item, tmp_item;

    selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(page->current));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    if (forward) {
        tmp_iter = iter;
        if (!gtk_tree_model_iter_next(model, &tmp_iter))
            return;
        tmp_path = gtk_tree_model_get_path(model, &tmp_iter);
    } else {
        tmp_path = gtk_tree_model_get_path(model, &iter);
        if (!gtk_tree_path_prev(tmp_path)) {
            gtk_tree_path_free(tmp_path);
            return;
        }
        gtk_tree_model_get_iter(model, &tmp_iter, tmp_path);
    }

    gtk_tree_model_get(model, &iter, TP_ITEM_COLUMN, &item, -1);
    gtk_tree_model_get(model, &tmp_iter, TP_ITEM_COLUMN, &tmp_item, -1);
    tp_store_set(GTK_LIST_STORE(model), &tmp_iter, item);
    tp_store_set(GTK_LIST_STORE(model), &iter, tmp_item);

    gtk_tree_selection_select_path(selection, tmp_path);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->current),
                                 tmp_path, NULL, FALSE, 0, 0);
    gtk_tree_path_free(tmp_path);

    gtk_widget_set_sensitive(page->standard_button, TRUE);
    tp_page_refresh_preview(page);
    balsa_toolbar_model_changed(page->model);
}

/* Add an item to a GtkTreeView's GtkListStore.
 */ 
static void
tp_store_set(GtkListStore * store, GtkTreeIter * iter, gint item)
{
    GdkPixbuf *pixbuf;
    gchar *text;

    text = g_strdup(balsa_toolbar_button_text(item));
    replace_nl_with_space(text);
    pixbuf = (item > 0
              ? gtk_widget_render_icon(customize_widget,
                                       toolbar_buttons[item].pixmap_id,
                                       GTK_ICON_SIZE_LARGE_TOOLBAR,
                                       "Balsa")
              : NULL);
    gtk_list_store_set(store, iter,
                       TP_TEXT_COLUMN, text,
                       TP_ICON_COLUMN, pixbuf,
                       TP_ITEM_COLUMN, item,
                       -1);
    g_free(text);
    if (pixbuf)
        g_object_unref(pixbuf);
}

/* Add an item to the current list.
 */
static void
tp_page_add_selected(ToolbarPage * page)
{
    gint item = 0;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeIter sibling;
    GtkTreePath *path;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->available));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;
    gtk_tree_model_get(model, &iter, TP_ITEM_COLUMN, &item, -1);

    selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(page->current));
    if (gtk_tree_selection_get_selected(selection, &model, &sibling))
        gtk_list_store_insert_before(GTK_LIST_STORE(model), &iter, 
                                     &sibling);
    else
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);

    tp_store_set(GTK_LIST_STORE(model), &iter, item);
	
    path = gtk_tree_model_get_path(model, &iter);
    gtk_tree_selection_select_path(selection, path);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->current), path, NULL,
                                 FALSE, 0, 0);
    gtk_tree_path_free(path);

    gtk_widget_set_sensitive(page->standard_button, TRUE);
    tp_page_refresh_preview(page);
    tp_page_refresh_available(page);
    balsa_toolbar_model_changed(page->model);
}

/* Remove an item from the page's current list.
 */
static void
tp_page_remove_selected(ToolbarPage * page)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(GTK_TREE_VIEW(page->current));
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    path = gtk_tree_model_get_path(model, &iter);

    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);

    if (gtk_tree_model_get_iter(model, &iter, path) 
        || gtk_tree_path_prev(path)) {
        gtk_tree_selection_select_path(selection, path);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->current),
                                     path, NULL, FALSE, 0, 0);
    }
    gtk_tree_path_free(path);

    gtk_widget_set_sensitive(page->standard_button, TRUE);
    tp_page_refresh_preview(page);
    tp_page_refresh_available(page);
    balsa_toolbar_model_changed(page->model);
}

/* Unwrap text.
 */
static void
replace_nl_with_space(char* str)
{
    while(*str) {
	if(*str == '\n')
	    *str=' ';
	str++;
    }
}
