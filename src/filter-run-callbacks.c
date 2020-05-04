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

/*
 * Callbacks for the filter run dialog
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <string.h>

#include "mailbox-filter.h"
#include "filter-funcs.h"

#define OK_TO_INCLUDE_FILTER_RUN_H
#include "filter-run.h"
#undef OK_TO_INCLUDE_FILTER_RUN_H

#include "balsa-app.h"
#include "save-restore.h"

#include <glib/gi18n.h>	/* Must come after balsa-app.h. */

/* Global vars */

extern GList * fr_dialogs_opened;

/* FIXME : we should single out invalid filters in the list (eg with colors) */
/* FIXME : implement error reporting */

void
fr_clean_associated_mailbox_filters(GtkTreeView * filter_list)
{
    GtkTreeModel *model = gtk_tree_view_get_model(filter_list);
    GtkTreeIter iter;
    gboolean valid;

    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaMailboxFilter *fil;

        gtk_tree_model_get(model, &iter, DATA_COLUMN, &fil, -1);
        g_free(fil);
    }
}

static GSList *
build_selected_filters_list(GtkTreeView * filter_list, gboolean to_run)
{
    GtkTreeModel *model = gtk_tree_view_get_model(filter_list);
    GtkTreeIter iter;
    gboolean valid;
    GSList *filters = NULL;

    /* Construct list of selected filters */
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaMailboxFilter *fil;

        gtk_tree_model_get(model, &iter, DATA_COLUMN, &fil, -1);
        filters =
            g_slist_prepend(filters,
                            to_run ? (gpointer) fil->actual_filter
                                   : (gpointer) fil);
    }

    return g_slist_reverse(filters);
}

/*
 * Run the selected filters on the mailbox
 * Returns TRUE if OK, or FALSE if errors occurred
 */
static gboolean
run_filters_on_mailbox(GSList * filters, LibBalsaMailbox * mbox)
{
    guint sent_to_trash;

    if (!filters_prepare_to_run(filters))
	return FALSE;

    sent_to_trash = 0;
    for (; filters; filters = filters->next) {
	LibBalsaFilter *filter = filters->data;
	LibBalsaMailboxSearchIter *search_iter;
	guint msgno;
	GArray *messages;

	if (!filter->condition)
	    continue;
	search_iter = libbalsa_mailbox_search_iter_new(filter->condition);

	/* Build a list of matching messages;
	 * - to use the existing search_iter methods, we go repeatedly
	 *   to the mailbox;
	 * - for the local mailboxes, that may be reasonable;
	 * - for imap, it can surely be improved--the backend caches the
	 * search results (in the search-iter), so we don't go
	 * repeatedly to the server, but each test is a hash-table
	 * lookup; the question is how to design an api that handles
	 * cleanly both this search and mailbox searching.
	 */
	messages = g_array_new(FALSE, FALSE, sizeof(guint));
	for (msgno = 1; msgno <= libbalsa_mailbox_total_messages(mbox);
	     msgno++)
	    if (libbalsa_mailbox_message_match(mbox, msgno, search_iter))
		g_array_append_val(messages, msgno);
	libbalsa_mailbox_search_iter_unref(search_iter);

	libbalsa_mailbox_register_msgnos(mbox, messages);
	sent_to_trash +=
	    libbalsa_filter_mailbox_messages(filter, mbox, messages);
	libbalsa_mailbox_unregister_msgnos(mbox, messages);
	g_array_free(messages, TRUE);
    }

    if (sent_to_trash)
	enable_empty_trash(balsa_app.main_window, TRASH_FULL);

    return TRUE;
}

/* "destroy" callback */

void fr_destroy_window_cb(GtkWidget * widget,gpointer throwaway)
{
    /* We pull out the destructed dialog from the list of opened dialogs */
    fr_dialogs_opened=g_list_remove(fr_dialogs_opened,widget);
}

static
void save_filters(BalsaFilterRunDialog * p)
{
    if (p->filters_modified) {
        libbalsa_mailbox_set_filters(p->mbox,
                                     build_selected_filters_list(p->selected_filters, FALSE));
	config_mailbox_filters_save(p->mbox);
	p->filters_modified=FALSE;
    }
}

/* Dialog box button callbacks */
void fr_dialog_response(GtkWidget * widget, gint response,
			gpointer throwaway)
{
    BalsaFilterRunDialog * p;
    GError *err = NULL;

    p=BALSA_FILTER_RUN_DIALOG(widget);
    switch (response) {
    case GTK_RESPONSE_OK:       /* OK button */
	save_filters(p);
	break;

    case GTK_RESPONSE_CANCEL:   /* Cancel button */
    case GTK_RESPONSE_NONE:     /* Close window */
	/* We free the mailbox_filter datas, they are useless now */
	fr_clean_associated_mailbox_filters(p->selected_filters);

	break;
    case GTK_RESPONSE_HELP:     /* Help button */
        gtk_show_uri_on_window(GTK_WINDOW(widget),
                               "help:balsa/win-run-filters",
                               gtk_get_current_event_time(), &err);
	if (err) {
	    balsa_information_parented(GTK_WINDOW(widget),
		    LIBBALSA_INFORMATION_WARNING,
		    _("Error displaying run filters help: %s\n"),
		    err->message);
	    g_error_free(err);
	}
	return;

    default:
	/* we should NEVER get here */
	break;
    }
    gtk_widget_destroy(GTK_WIDGET(p));
}


/*
 *Callbacks for apply/left/right buttons
 */

static void
fr_apply_selected_pressed_func(GtkTreeModel * model, GtkTreePath * path,
                               GtkTreeIter * iter, gpointer data)
{
    LibBalsaFilter *fil;
    BalsaFilterRunDialog *p = data;

    gtk_tree_model_get(model, iter, DATA_COLUMN, &fil, -1);
    /* Check for move to self: */
    if (fil->action == FILTER_RUN
        || fil->action == FILTER_TRASH
        || (fil->action == FILTER_COLOR && fil->action_string[0])
        || strcmp(fil->action_string, libbalsa_mailbox_get_url(p->mbox)) != 0)
        p->filters = g_slist_append(p->filters, fil);
}

void
fr_apply_selected_pressed(BalsaFilterRunDialog * p)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(p->available_filters);

    p->filters = NULL;
    gtk_tree_selection_selected_foreach(selection,
                                        fr_apply_selected_pressed_func, p);

    if (!p->filters)
        return;

    if (!run_filters_on_mailbox(p->filters, p->mbox))
        balsa_information_parented(GTK_WINDOW(p),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("Error when applying filters"));
    else
        balsa_information(LIBBALSA_INFORMATION_MESSAGE,
                          _("Filter applied to “%s”."),
                          libbalsa_mailbox_get_name(p->mbox));

    g_slist_free(p->filters);
}

void
fr_apply_now_pressed(BalsaFilterRunDialog* p)
{
    GSList *filters;

    filters = build_selected_filters_list(p->selected_filters, TRUE);

    if (!filters)
        return;

    if (!run_filters_on_mailbox(filters, p->mbox))
        balsa_information_parented(GTK_WINDOW(p),
                                   LIBBALSA_INFORMATION_ERROR,
                                   _("Error when applying filters"));
    else
        balsa_information(LIBBALSA_INFORMATION_MESSAGE,
                          _("Filter applied to “%s”."),
                          libbalsa_mailbox_get_name(p->mbox));
}

static void
fr_add_pressed_func(GtkTreeModel * model, GtkTreePath * path,
                    GtkTreeIter * iter, gpointer data)
{
    BalsaFilterRunDialog *p = data;
    LibBalsaFilter *fil;

    gtk_tree_model_get(model, iter, DATA_COLUMN, &fil, -1);
    if (fil->action == FILTER_RUN
        || fil->action == FILTER_TRASH
        || (fil->action == FILTER_COLOR && fil->action_string[0])
        || strcmp(fil->action_string, libbalsa_mailbox_get_url(p->mbox)) != 0) {
        /* Ok we can add the filter to this mailbox, there is no recursion problem */
        LibBalsaMailboxFilter *mf = g_new(LibBalsaMailboxFilter, 1);
        GtkTreeModel *sel_model =
            gtk_tree_view_get_model(p->selected_filters);
        GtkTreeSelection *sel_selection =
            gtk_tree_view_get_selection(p->selected_filters);
        GtkTreeIter sel_iter;

        mf->actual_filter = fil;
        g_debug("%s adds fil=%p", __func__, fil);
        mf->when = FILTER_WHEN_NEVER;
        gtk_list_store_append(GTK_LIST_STORE(sel_model), &sel_iter);
        gtk_list_store_set(GTK_LIST_STORE(sel_model), &sel_iter,
                           NAME_COLUMN, fil->name,
                           DATA_COLUMN, mf, -1);
        gtk_tree_selection_select_iter(sel_selection, &sel_iter);
        /* Mark for later deletion */
        gtk_list_store_set(GTK_LIST_STORE(model), iter,
                           DATA_COLUMN, NULL, -1);
    } else
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("The destination mailbox of "
                            "the filter “%s” is “%s”.\n"
                            "You can’t associate it with the same "
                            "mailbox (that causes recursion)."),
                          fil->name, libbalsa_mailbox_get_name(p->mbox));

    if (!libbalsa_mailbox_can_match(p->mbox, fil->condition))
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("The filter “%s” is not compatible with "
			    "the mailbox type of “%s”.\n"
			    "This happens for example when you use"
			    " regular expressions match with IMAP mailboxes,"
			    " it is done by a very slow method; if possible, use substring match"
			    " instead."),
			  fil->name, libbalsa_mailbox_get_name(p->mbox));
}

void
fr_add_pressed(BalsaFilterRunDialog* p)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(p->available_filters);
    GtkTreeModel *model =
        gtk_tree_view_get_model(p->available_filters);
    GtkTreeIter iter;
    gboolean valid;
    gboolean modified = FALSE;

    /* We check possibility of recursion here: we do not allow a filter 
       to be applied on a mailbox if its action rule modify this mailbox
    */
    gtk_tree_selection_selected_foreach(selection, fr_add_pressed_func, p);

    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        LibBalsaFilter *fil;

        gtk_tree_model_get(model, &iter, DATA_COLUMN, &fil, -1);
        if (!fil) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &iter);

            gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
            modified = TRUE;
            valid = gtk_tree_model_get_iter(model, &iter, path);
            if (valid)
                gtk_tree_selection_select_path(selection, path);
            else if (gtk_tree_path_prev(path))
                gtk_tree_selection_select_path(selection, path);
            gtk_tree_path_free(path);
        } else
            valid = gtk_tree_model_iter_next(model, &iter);
    }

    p->filters_modified = modified;
}

static void
fr_remove_pressed_func(GtkTreeModel * model, GtkTreePath * path, 
                       GtkTreeIter * iter, gpointer data)
{
    BalsaFilterRunDialog *p = data;
    LibBalsaMailboxFilter *fil;
    GtkTreeModel *avl_model =
        gtk_tree_view_get_model(p->available_filters);
    GtkTreeIter avl_iter;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(p->available_filters);

    p->filters_modified = TRUE;
    gtk_tree_model_get(model, iter, DATA_COLUMN, &fil, -1);
    /* mark for later deletion */
    gtk_list_store_set(GTK_LIST_STORE(model), iter,
                       DATA_COLUMN, NULL, -1);
    gtk_list_store_append(GTK_LIST_STORE(avl_model), &avl_iter);
    gtk_list_store_set(GTK_LIST_STORE(avl_model), &avl_iter,
                       NAME_COLUMN, fil->actual_filter->name,
                       DATA_COLUMN, fil->actual_filter, -1);
    gtk_tree_selection_unselect_all(selection);
    gtk_tree_selection_select_iter(selection, &avl_iter);
    g_free(fil);
}

void
fr_remove_pressed(BalsaFilterRunDialog * p)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(p->selected_filters);
    gboolean valid;
    GtkTreeModel *model =
        gtk_tree_view_get_model(p->selected_filters);
    GtkTreeIter iter;

    gtk_tree_selection_selected_foreach(selection, fr_remove_pressed_func, p);

    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        LibBalsaMailboxFilter *fil;

        gtk_tree_model_get(model, &iter, DATA_COLUMN, &fil, -1);
        if (!fil) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &iter);

            gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
            valid = gtk_tree_model_get_iter(model, &iter, path);
            if (valid)
                gtk_tree_selection_select_path(selection, path);
            else if (gtk_tree_path_prev(path))
                gtk_tree_selection_select_path(selection, path);
            gtk_tree_path_free(path);
        } else
            valid = gtk_tree_model_iter_next(model, &iter);
    }
}

/* Callbacks for filter lists */

void
available_list_activated(GtkTreeView * treeview, GtkTreePath * path,
                         GtkTreeViewColumn * column, gpointer data)
{
    fr_add_pressed(BALSA_FILTER_RUN_DIALOG(data));
}

void
selected_list_toggled(GtkCellRendererToggle * renderer,
                      const gchar * path_string, gpointer data)
{
    gint column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(renderer),
                                                    BALSA_FILTER_KEY));
    BalsaFilterRunDialog *p = data;
    GtkTreeModel *model = gtk_tree_view_get_model(p->selected_filters);
    GtkTreeIter iter;
    LibBalsaMailboxFilter *fil;
    gboolean active;
    gint mask;

    gtk_tree_model_get_iter_from_string(model, &iter, path_string);
    gtk_tree_model_get(model, &iter, DATA_COLUMN, &fil,
                       column, &active, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, column, !active, -1);

    switch (column) {
        case INCOMING_COLUMN:
            mask = FILTER_WHEN_INCOMING;
            break;
        case CLOSING_COLUMN:
            mask = FILTER_WHEN_CLOSING;
            break;
        default:
            mask = FILTER_WHEN_NEVER;
            break;
    }
    if (active)
        FILTER_WHEN_CLRFLAG(fil, mask);
    else
        FILTER_WHEN_SETFLAG(fil, mask);

    p->filters_modified = TRUE;
}

void
selected_list_activated(GtkTreeView * treeview, GtkTreePath * path,
                        GtkTreeViewColumn * column, gpointer data)
{
    fr_remove_pressed(BALSA_FILTER_RUN_DIALOG(data));
}

/* 
 *Callbacks for up/down buttons
 */

/*
 * helper
 */
static void
fr_swap(GtkTreeModel * model, GtkTreeIter * iter1, GtkTreeIter * iter2,
        GtkTreeSelection * selection, GtkTreePath * path,
        BalsaFilterRunDialog * p)
{
    LibBalsaMailboxFilter *fil1, *fil2;
    gboolean incoming1, closing1;
    gboolean incoming2, closing2;

    gtk_tree_model_get(model, iter1,
                       DATA_COLUMN, &fil1,
                       INCOMING_COLUMN, &incoming1,
                       CLOSING_COLUMN, &closing1, -1);
    gtk_tree_model_get(model, iter2,
                       DATA_COLUMN, &fil2,
                       INCOMING_COLUMN, &incoming2,
                       CLOSING_COLUMN, &closing2, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter1,
                       NAME_COLUMN, fil2->actual_filter->name,
                       DATA_COLUMN, fil2,
                       INCOMING_COLUMN, incoming2,
                       CLOSING_COLUMN, closing2, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter2,
                       NAME_COLUMN, fil1->actual_filter->name,
                       DATA_COLUMN, fil1,
                       INCOMING_COLUMN, incoming1,
                       CLOSING_COLUMN, closing1, -1);

    gtk_tree_selection_select_path(selection, path);
    gtk_tree_view_scroll_to_cell(p->selected_filters, path, NULL,
                                 FALSE, 0, 0);
    p->filters_modified = TRUE;
}

/*
 * fr_down_pressed()
 *
 * Callback for the "Down" button
 */
void
fr_down_pressed(GtkWidget * widget, gpointer data)
{
    BalsaFilterRunDialog *p = data;
    GtkTreeModel *model;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(p->selected_filters);
    GtkTreeIter iter1, iter2;

    if (gtk_tree_selection_get_selected(selection, &model, &iter1)) {
        iter2 = iter1;
        if (gtk_tree_model_iter_next(model, &iter2)) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &iter2);
            fr_swap(model, &iter1, &iter2, selection, path, p);
            gtk_tree_path_free(path);
        }
    }
}                               /* end fe_down_pressed */

/*
 * fr_up_pressed()
 *
 * Callback for the "Up" button
 */
void
fr_up_pressed(GtkWidget * widget, gpointer data)
{
    BalsaFilterRunDialog *p = data;
    GtkTreeModel *model;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(p->selected_filters);
    GtkTreeIter iter1, iter2;

    if (gtk_tree_selection_get_selected(selection, &model, &iter1)) {
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter1);
        if (gtk_tree_path_prev(path)) {
            gtk_tree_model_get_iter(model, &iter2, path);
            fr_swap(model, &iter1, &iter2, selection, path, p);
        }
        gtk_tree_path_free(path);
    }
}                               /* end fe_up_pressed */
