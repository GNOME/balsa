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
 * filter-edit.h
 *
 * Variables and definitions for the filter edit dialog
 */

#ifndef __FILTER_EDIT_H__
#define __FILTER_EDIT_H__

#include "filter.h"

/*
 * fe = filter edit
 */

/*
 * I know some of these don't need to be in the header.
 * I'll fix that later.
 */

typedef struct _option_list {
    gchar *text;
    gint value;
} option_list;

struct fe_combo_box_info {
    GSList *values;
};
#define BALSA_FE_COMBO_BOX_INFO "balsa-fe-combo-box-info"

/* destroy calback */
void fe_destroy_window_cb(GtkWidget *,gpointer);

/* button callbacks */
void fe_dialog_response(GtkWidget * widget, gint response,
			      gpointer data);

/* helper */
GtkWidget *fe_build_option_menu(option_list options[], gint num,
                                GCallback func, gpointer cb_data);

/*---------------- Left side of hbox ----------------*/

/* list callbacks */
void fe_filters_list_selection_changed(GtkTreeSelection * selection,
                                       gpointer data);

/* button callbacks */
void fe_new_pressed(GtkWidget * widget, gpointer data);
void fe_delete_pressed(GtkWidget * widget, gpointer data);

/*---------------- Right side of hbox ----------------*/


/* apply and revert callbacks */
void fe_apply_pressed(GtkWidget * widget, gpointer data);
void fe_revert_pressed(GtkWidget * widget, gpointer data);

/*op codes callback */
void fe_op_codes_toggled(GtkWidget * widget, gpointer data);

/* Conditions callbacks */
void fe_conditions_row_activated(GtkTreeView * treeview,
                                 GtkTreePath * path,
                                 GtkTreeViewColumn * column,
			         gpointer data);
void fe_edit_condition(GtkWidget * widget, gpointer data);
void fe_condition_remove_pressed(GtkWidget * widget, gpointer data);

/* action callback */
void fe_action_selected(GtkWidget * widget, gpointer data);
void fe_button_toggled(GtkWidget * widget, gpointer data);
void fe_action_changed(GtkWidget * widget, gpointer data);
void fe_enable_right_page(gboolean enabled);

/* Callback for the sound file-chooser-button's dialog. */
void fe_sound_response(GtkDialog * dialog, gint response, gpointer data);

void fe_add_new_user_header(const gchar *);

/* Callbacks for color button signals */
void fe_color_check_toggled(GtkToggleButton * check_button, gpointer data);
void fe_color_set(GtkColorButton * color_button, gpointer data);

#endif /*__FILTER_EDIT_H__ */
