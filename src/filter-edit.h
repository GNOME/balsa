/* -*- C -*-
 * filter-edit.h
 *
 * Variables and definitions for the filter edit dialog
 */

#include <glib.h>
#include <gtk/gtk.h>

/*
 * fe = filter edit
 */

/*
 * I know some of these don't need to be in the header.
 * I'll fix that later.
 */

/* Dialog window */
GtkWidget *fe_dialog;
/* and buttons */
GtkWidget *fe_dialog_ok;
GtkWidget *fe_dialog_cancel;
GtkWidget *fe_dialog_help;
/* and button callbacs */
void fe_dialog_button_clicked(GtkWidget *widget,
			      gpointer data);

/* main table */
GtkWidget *fe_table;


/*
 * Left side of hbox
 */

/* clist */
GtkWidget *fe_clist;
/* its callbacks */
void fe_clist_select_row(GtkWidget *widget,
			 gint row, gint column,
			 GdkEventButton *bevent,
			 gpointer data);
void fe_clist_button_event_press(GtkCList *clist,
				 GdkEventButton *event,
				 gpointer data);

/* boxes for clist control buttons */
GtkWidget *fe_box_newdelete;
GtkWidget *fe_box_updown;

/* clist control buttons */
GtkWidget *fe_new;
GtkWidget *fe_delete;
GtkWidget *fe_up;
GtkWidget *fe_down;
/* their callbacks */
void fe_new_pressed(GtkWidget *widget,
		    gpointer data);
void fe_delete_pressed(GtkWidget *widget,
		       gpointer data);
void fe_up_pressed(GtkWidget *widget,
		   gpointer data);
void fe_down_pressed(GtkWidget *widget,
		     gpointer data);

/*
 * Separator
 */
GtkWidget *fe_vseparator;

/*
 * Right side of hbox
 */

/* main notebook */
GtkWidget *fe_notebook;

/* its pages */
GtkWidget *fe_notebook_match_page;
GtkWidget *fe_notebook_action_page;
/* their labels */
GtkWidget *fe_match_label;
GtkWidget *fe_action_label;

/* type notebook */
GtkNotebook *fe_type_notebook;

/* its pages */
GtkWidget *fe_type_notebook_simple_page;
GtkWidget *fe_type_notebook_regex_page;
GtkWidget *fe_type_notebook_exec_page;

/* box for notebook control buttons */
GtkWidget *fe_box_applyrevert;

/* notebook control buttons */
GtkWidget *fe_apply;
GtkWidget *fe_revert;
/* their callbacks */
void fe_apply_pressed(GtkWidget *widget,
		      gpointer data);
void fe_revert_pressed(GtkWidget *widget,
		       gpointer data);

/* type notebook checkbuttons */
GtkWidget *fe_simple;
GtkWidget *fe_regex;
GtkWidget *fe_exec;
/* state variable (which toggle is selected)*/
gint fe_type_state;
/* their callback */
void fe_checkbutton_toggled(GtkWidget *widget,
			    gpointer data);
