/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

/*
 * filter-run.h
 *
 * Variables and definitions for the filter edit dialog
 */

#ifndef __FILTER_RUN_H__
#define __FILTER_RUN_H__

#include <gnome.h>

#include "filter.h"
#include "mailbox.h"

/*
 * We define a new gtk type BalsaFilterRunDialog, inheriting from GnomeDialog
 * each object contains the whole set of data needed for managing the dialog box
 * that way there is no global variables (but the list of 
 * fr = filter run
 */

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */


#define BALSA_TYPE_FILTER_RUN_DIALOG          balsa_filter_run_dialog_get_type()
#define BALSA_FILTER_RUN_DIALOG(obj)          GTK_CHECK_CAST(obj, BALSA_TYPE_FILTER_RUN_DIALOG, BalsaFilterRunDialog)
#define BALSA_FILTER_RUN_DIALOG_CLASS(klass)  GTK_CHECK_CLASS_CAST(klass, BALSA_TYPE_FILTER_RUN_DIALOG, BalsaFilterRunDialogClass)
#define BALSA_IS_FILTER_RUN_DIALOG(obj)       GTK_CHECK_TYPE(obj, BALSA_TYPE_FILTER_RUN_DIALOG)


typedef struct _BalsaFilterRunDialog BalsaFilterRunDialog;
typedef struct _BalsaFilterRunDialogClass BalsaFilterRunDialogClass;

struct _BalsaFilterRunDialog {
    GnomeDialog parent;

    /* GUI members */
    GtkCList * available_filters,* selected_filters;
    gboolean filters_modified;

    /* Mailbox the filters of which are edited */
    LibBalsaMailbox * mbox;
};

struct _BalsaFilterRunDialogClass {
	GnomeDialogClass parent_class;

	void (*refresh) (BalsaFilterRunDialog * fr,GSList * filters_changing,gpointer throwaway);
};

guint balsa_filter_run_dialog_get_type(void);

GtkWidget *balsa_filter_run_dialog_new(LibBalsaMailbox * mbox);

void fr_clean_associated_mailbox_filters(GtkCList * clist);

void fr_destroy_window_cb(GtkWidget * widget,gpointer throwaway);

/* Dialog box button callbacks */
void fr_dialog_button_clicked(GtkWidget * widget, gint button,
			      gpointer data);
/* 
 *Callbacks for left/right buttons
 */
void fr_add_pressed(GtkWidget * widget, gpointer data);
void fr_remove_pressed(GtkWidget * widget, gpointer data);

/* 
 *Callbacks for up/down buttons
 */
void fr_up_pressed(GtkWidget * widget, gpointer data);
void fr_down_pressed(GtkWidget * widget, gpointer data);

/*
 * Callback for clists (to handle double-click)
 */
void available_list_select_row_cb(GtkWidget *widget, gint row, gint column,
				  GdkEventButton *event, gpointer data);

void selected_list_select_row_cb(GtkWidget *widget,gint row,gint column,
				 GdkEventButton *event, gpointer data);
gboolean selected_list_select_row_event_cb(GtkWidget * widget,
                                           GdkEventButton * event,
                                           gpointer data);

#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif  /* __FILTER_RUN_H__ */
