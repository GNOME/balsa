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
 * filter-run.h
 *
 * Variables and definitions for the filter edit dialog
 */

#ifndef OK_TO_INCLUDE_FILTER_RUN_H
# error "filter-run.h is private to the filter-run modules"
#endif

#ifndef __FILTER_RUN_H__
#define __FILTER_RUN_H__

#include "filter.h"
#include "mailbox.h"

/*
 * We define a new gtk type BalsaFilterRunDialog, inheriting from
 * GtkDialog each object contains the whole set of data needed for
 * managing the dialog box.
 * In that way there is no global variables (but the list of
 * fr = filter run
 */

G_BEGIN_DECLS


struct _BalsaFilterRunDialog {
    GtkDialog parent;

    /* GUI members */
    GtkTreeView *available_filters, *selected_filters;
    gboolean filters_modified;

    /* Mailbox the filters of which are edited */
    LibBalsaMailbox *mbox;

    /* Temporary list variable */
    GSList *filters;

    /* Buttons */
    GtkWidget *add_button;
    GtkWidget *remove_button;
    GtkWidget *move_up_button;
    GtkWidget *move_down_button;
    GtkWidget *apply_selected_button;
    GtkWidget *apply_now_button;
};

#define BALSA_TYPE_FILTER_RUN_DIALOG balsa_filter_run_dialog_get_type()

G_DECLARE_FINAL_TYPE(BalsaFilterRunDialog,
                     balsa_filter_run_dialog,
                     BALSA,
                     FILTER_RUN_DIALOG,
                     GtkDialog);

enum {
    NAME_COLUMN,
    DATA_COLUMN,
    INCOMING_COLUMN,
    CLOSING_COLUMN,
    N_COLUMNS
};

#define BALSA_FILTER_KEY "balsa-filter-key"

void fr_clean_associated_mailbox_filters(GtkTreeView *filter_list);

void fr_destroy_window_cb(GtkWidget *widget,
                          gpointer   throwaway);

/* Dialog box button callbacks */
void fr_dialog_response(GtkWidget *widget,
                        gint       response,
                        gpointer   data);

/*
 * Callbacks for apply/left/right buttons
 */
void fr_apply_selected_pressed(BalsaFilterRunDialog *dialog);
void fr_apply_now_pressed(BalsaFilterRunDialog *dialog);
void fr_add_pressed(BalsaFilterRunDialog *dialog);
void fr_remove_pressed(BalsaFilterRunDialog *dialog);

/*
 * Callbacks for up/down buttons
 */
void fr_up_pressed(GtkWidget *widget,
                   gpointer   data);
void fr_down_pressed(GtkWidget *widget,
                     gpointer   data);

/*
 * Callback for filter lists
 */
void available_list_activated(GtkTreeView       *treeview,
                              GtkTreePath       *path,
                              GtkTreeViewColumn *column,
                              gpointer           data);
void selected_list_toggled(GtkCellRendererToggle *cellrenderertoggle,
                           const gchar           *path_string,
                           gpointer               data);
void selected_list_activated(GtkTreeView       *treeview,
                             GtkTreePath       *path,
                             GtkTreeViewColumn *column,
                             gpointer           data);

G_END_DECLS

#endif  /* __FILTER_RUN_H__ */
