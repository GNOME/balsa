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
#   include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "balsa-app.h"

#define OK_TO_INCLUDE_FILTER_RUN_H
#include "filter-run.h"
#undef OK_TO_INCLUDE_FILTER_RUN_H

#include "mailbox-filter.h"
#include "save-restore.h"
#include "filter-funcs.h"
#include <glib/gi18n.h>

/* fe_already_open is TRUE when the filters dialog is opened, we use
 * this to prevent incoherency if we have both filters dialog and
 * mailbox filters dialog boxes opened at the same time.
 * FIXME : we can perhaps imagine a way to "refresh" the other dialog
 * boxes when filters have been modified but it'll be complex, and I'm
 * not sure it is worth it.
 *
 * Defined in filter-edit-dialog.c
 */

extern gboolean fe_already_open;

/* BalsaFilterRunDialog signals */

enum {
    REFRESH,
    LAST_SIGNAL,
};

static gint balsa_filter_run_dialog_signals[LAST_SIGNAL];

GList *fr_dialogs_opened = NULL;

/* BalsaFilterRunDialog methods */

static void balsa_filter_run_dispose(GObject *object);

static void
populate_available_filters_list(GtkTreeView *filter_list,
                                GSList      *mailbox_filters)
{
    LibBalsaFilter *fil;
    GSList *source, *lst;
    GtkTreeModel *model = gtk_tree_view_get_model(filter_list);
    GtkTreeIter iter;

    for (source = balsa_app.filters; source; source = source->next) {
        fil = (LibBalsaFilter *)source->data;
        /* We look for each filter in the mailbox list */
        for (lst = mailbox_filters;
             lst && fil != ((LibBalsaMailboxFilter *)lst->data)->actual_filter;
             lst = lst->next) {
        }
        /* If it's not in mailbox list we can add it to available filters */
        if (!lst) {
            gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               NAME_COLUMN, fil->name,
                               DATA_COLUMN, fil, -1);
        }
    }
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(filter_list);
        gtk_tree_selection_select_iter(selection, &iter);
    }
}


/* Set the toggle button corresponding to the when type */

static void
populate_selected_filters_list(GtkTreeView *filter_list,
                               GSList      *filters_list)
{
    LibBalsaMailboxFilter *fil, *mf;
    GtkTreeModel *model = gtk_tree_view_get_model(filter_list);
    GtkTreeIter iter;

    for (; filters_list; filters_list = filters_list->next) {
        mf  = g_new(LibBalsaMailboxFilter, 1);
        fil = (LibBalsaMailboxFilter *) filters_list->data;
        *mf = *fil;
        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           NAME_COLUMN, fil->actual_filter->name,
                           DATA_COLUMN, mf,
                           INCOMING_COLUMN,
                           FILTER_WHEN_CHKFLAG(fil, FILTER_WHEN_INCOMING),
                           CLOSING_COLUMN,
                           FILTER_WHEN_CHKFLAG(fil, FILTER_WHEN_CLOSING),
                           -1);
    }
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(filter_list);
        gtk_tree_selection_select_iter(selection, &iter);
    }
}


G_DEFINE_TYPE(BalsaFilterRunDialog, balsa_filter_run_dialog, GTK_TYPE_DIALOG)

static void
balsa_filter_run_dialog_class_init(BalsaFilterRunDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    /* FIXME : this is the future implementation of a signal
     * that will be able to tell each filter-run dialog box
     * that filters have changed and that they have to refresh their content.
     * We'll see that later if it's worth the pain
     */
    balsa_filter_run_dialog_signals[REFRESH] =
        g_signal_new("refresh",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     0,
                     NULL, NULL,
		     NULL,
                     G_TYPE_NONE, 1, G_TYPE_OBJECT);

    object_class->dispose = balsa_filter_run_dispose;
}


static GtkWidget *
balsa_filter_run_dialog_new(LibBalsaMailbox *mbox,
                            GtkWindow       *parent)
{
    BalsaFilterRunDialog *p;
    gchar *dialog_title;
    gboolean use_headerbar;
    GSList *filters;

    g_return_val_if_fail(mbox, NULL);

    use_headerbar = (libbalsa_dialog_flags() & GTK_DIALOG_USE_HEADER_BAR) != 0;
    p = g_object_new(BALSA_TYPE_FILTER_RUN_DIALOG,
                     "transient-for", parent,
                     "use-header-bar", use_headerbar,
                     NULL);

    /* We set the dialog title */
    p->mbox = mbox;
    libbalsa_mailbox_open(p->mbox, NULL);
    libbalsa_mailbox_prepare_threading(p->mbox, 0);
    dialog_title = g_strconcat(_("Filters of Mailbox: "),
                               libbalsa_mailbox_get_name(p->mbox), NULL);
    gtk_window_set_title(GTK_WINDOW(p), dialog_title);
    gtk_window_set_role(GTK_WINDOW(p), "filter-run");
    g_free(dialog_title);

    /* Load associated filters if needed */
    if (libbalsa_mailbox_get_filters(p->mbox) == NULL)
        config_mailbox_filters_load(p->mbox);

    /* Populate the lists */
    filters = libbalsa_mailbox_get_filters(mbox);
    populate_available_filters_list(p->available_filters, filters);
    populate_selected_filters_list(p->selected_filters, filters);

    return GTK_WIDGET(p);
}


static GtkTreeView *
selected_filters_new(BalsaFilterRunDialog *p)
{
    GtkListStore *list_store;
    GtkTreeView *view;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    list_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_POINTER,
                                    G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
    view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store)));
    g_object_unref(list_store);

    renderer = gtk_cell_renderer_text_new();
    column   =
        gtk_tree_view_column_new_with_attributes(_("Name"), renderer, "text",
                                                 0, NULL);
    gtk_tree_view_append_column(view, column);

    renderer = gtk_cell_renderer_toggle_new();
    g_object_set_data(G_OBJECT(renderer), BALSA_FILTER_KEY,
                      GINT_TO_POINTER(INCOMING_COLUMN));
    g_signal_connect(renderer, "toggled",
                     G_CALLBACK(selected_list_toggled), p);
    column = gtk_tree_view_column_new_with_attributes(_("On reception"),
                                                      renderer,
                                                      "active",
                                                      INCOMING_COLUMN,
                                                      NULL);
    gtk_tree_view_append_column(view, column);

    renderer = gtk_cell_renderer_toggle_new();
    g_object_set_data(G_OBJECT(renderer), BALSA_FILTER_KEY,
                      GINT_TO_POINTER(CLOSING_COLUMN));
    g_signal_connect(renderer, "toggled",
                     G_CALLBACK(selected_list_toggled), p);
    column = gtk_tree_view_column_new_with_attributes(_("On exit"),
                                                      renderer,
                                                      "active",
                                                      CLOSING_COLUMN,
                                                      NULL);
    gtk_tree_view_append_column(view, column);

    return view;
}


/*
 * Callbacks for the selection "changed" signal of the available and
 * selected lists
 */
static void
available_list_selection_changed(GtkTreeSelection *selection,
                                 gpointer          user_data)
{
    BalsaFilterRunDialog *p = user_data;
    gboolean selected;

    selected = gtk_tree_selection_count_selected_rows(selection) > 0;
    gtk_widget_set_sensitive(p->apply_selected_button, selected);
    gtk_widget_set_sensitive(p->add_button, selected);
}


static void
selected_list_selection_changed(GtkTreeSelection *selection,
                                gpointer          user_data)
{
    BalsaFilterRunDialog *p = user_data;
    gboolean selected;

    selected = gtk_tree_selection_count_selected_rows(selection) > 0;
    gtk_widget_set_sensitive(p->apply_now_button, selected);
    gtk_widget_set_sensitive(p->remove_button, selected);
    gtk_widget_set_sensitive(p->move_up_button, selected);
    gtk_widget_set_sensitive(p->move_down_button, selected);
}


static
void
balsa_filter_run_dialog_init(BalsaFilterRunDialog *p)
{
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(p));
    GtkWidget *bbox, *hbox, *vbox;
    GtkTreeSelection *selection;
    GtkWidget *button;
    GtkWidget *sw;
    GtkSizeGroup *size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);


/*
 * Builds the two lists of filters (available, and selected ones) of the dialog
 */


    /*
       /-----------------\
     | /---\  | /---\  |
     | |   | -> |   |  |
     | |   |  | |   |  |
     | |   | <- |   |  |
     | \---/  | \---/  |
     \-----------------/
     */

    gtk_dialog_add_buttons(GTK_DIALOG(p),
                           _("_OK"), GTK_RESPONSE_OK,
                           _("_Cancel"), GTK_RESPONSE_CANCEL,
                           _("_Help"), GTK_RESPONSE_HELP,
                           NULL);

    g_signal_connect(p, "response",
                     G_CALLBACK(fr_dialog_response), NULL);
    g_signal_connect(p, "destroy",
                     G_CALLBACK(fr_destroy_window_cb), NULL);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);

    p->available_filters =
        libbalsa_filter_list_new(TRUE, _("Name"), GTK_SELECTION_MULTIPLE,
                                 NULL, TRUE);
    g_signal_connect(p->available_filters, "row-activated",
                     G_CALLBACK(available_list_activated), p);

    selection = gtk_tree_view_get_selection(p->available_filters);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(available_list_selection_changed), p);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(p->available_filters));

    gtk_widget_set_vexpand(sw, TRUE);
    gtk_widget_set_valign(sw, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), sw);

    /* To keep a consistent look, make a button box for a single button. */
    bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_margin_top(bbox, 2);
    gtk_widget_set_margin_bottom(bbox, 2);
    gtk_container_add(GTK_CONTAINER(vbox), bbox);

    /* "Apply selected" button */
    p->apply_selected_button = button =
            libbalsa_add_mnemonic_button_to_box(_("_Apply Selected"), bbox, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(button, FALSE);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(fr_apply_selected_pressed), p);

    /* Buttons between the 2 lists */
    bbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    /* Right/Add button */
    p->add_button = button =
        gtk_button_new_from_icon_name("go-next-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button,
                                _("Add selected filter to mailbox"));
    gtk_widget_set_sensitive(button, FALSE);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(fr_add_pressed), p);
    gtk_widget_set_vexpand(button, TRUE);
    gtk_widget_set_valign(button, GTK_ALIGN_END);
    gtk_container_add(GTK_CONTAINER(bbox), button);
    /* Left/Remove button */
    p->remove_button = button =
        gtk_button_new_from_icon_name("go-previous-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(button,
                                _("Remove selected filter from mailbox"));
    gtk_widget_set_sensitive(button, FALSE);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(fr_remove_pressed), p);
    gtk_widget_set_vexpand(button, TRUE);
    gtk_widget_set_valign(button, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    gtk_widget_set_margin_start(bbox, 6);
    gtk_widget_set_margin_end(bbox, 6);
    gtk_container_add(GTK_CONTAINER(hbox), bbox);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gtk_widget_set_hexpand(vbox, TRUE);
    gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox),vbox);

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    p->selected_filters = selected_filters_new(p);
    g_signal_connect(p->selected_filters, "row-activated",
                     G_CALLBACK(selected_list_activated), p);

    selection = gtk_tree_view_get_selection(p->selected_filters);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(selected_list_selection_changed), p);

    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(p->selected_filters));

    gtk_widget_set_vexpand(sw, TRUE);
    gtk_widget_set_valign(sw, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), sw);

    /* up down arrow buttons */
    bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    gtk_widget_set_margin_top(bbox, 2);
    gtk_widget_set_margin_bottom(bbox, 2);
    gtk_container_add(GTK_CONTAINER(vbox), bbox);

    /* up button */
    p->move_up_button = button =
        gtk_button_new_from_icon_name("go-up-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_sensitive(button, FALSE);
    gtk_widget_set_tooltip_text(button,
                                _("Move selected filter up"));
    g_signal_connect(button, "clicked",
		     G_CALLBACK(fr_up_pressed), p);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(size_group, button);
    gtk_container_add(GTK_CONTAINER(bbox), button);
    /* down button */
    p->move_down_button = button =
        gtk_button_new_from_icon_name("go-down-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_sensitive(button, FALSE);
    gtk_widget_set_tooltip_text(button,
                                _("Move selected filter down"));
    g_signal_connect(button, "clicked",
		     G_CALLBACK(fr_down_pressed), p);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(size_group, button);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    p->apply_now_button = button =
            gtk_button_new_with_mnemonic(_("A_pply Now!"));
    gtk_widget_set_sensitive(button, FALSE);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(fr_apply_now_pressed), p);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(size_group, button);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    g_object_unref(size_group);

    p->filters_modified = FALSE;
}


/* balsa_filter_run_dispose:
 */
static void
balsa_filter_run_dispose(GObject *object)
{
    BalsaFilterRunDialog *bfrd = BALSA_FILTER_RUN_DIALOG(object);

    if (bfrd->mbox != NULL) {
        libbalsa_mailbox_close(bfrd->mbox, balsa_app.expunge_on_close);
        bfrd->mbox = NULL;
    }

    G_OBJECT_CLASS(balsa_filter_run_dialog_parent_class)->dispose(object);
}


/* filter_run_dialog(LibBalsaMailbox *mbox)
 * params:
 *   mbox - the mailbox concerned by edition/running filters
 */

void
filters_run_dialog(LibBalsaMailbox *mbox,
                   GtkWindow       *parent)
{
    GList *lst;
    GtkWidget *p;

    if (fe_already_open) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("The filters dialog is opened, close it "
                            "before you can run filters on any mailbox"));
        return;
    }
    /* We look for an existing dialog box for this mailbox */
    for (lst = fr_dialogs_opened; lst; lst = g_list_next(lst)) {
        BalsaFilterRunDialog *dialog = lst->data;
        if (strcmp(libbalsa_mailbox_get_url(dialog->mbox),
                   libbalsa_mailbox_get_url(mbox)) == 0)
            break;
    }
    if (lst) {
	/* If there was yet a dialog box for this mailbox, we raise it */
	gtk_window_present_with_time(GTK_WINDOW(lst->data),
                                     gtk_get_current_event_time());
	return;
    }

    p = balsa_filter_run_dialog_new(mbox, parent);
    if (!p) return;

    gtk_window_set_default_size(GTK_WINDOW(p), 500, 250);
    fr_dialogs_opened = g_list_prepend(fr_dialogs_opened, p);

    gtk_widget_show_all(p);
}
