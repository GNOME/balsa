/* -*- C -*-
 * filter-edit.h
 *
 * Variables and definitions for the filter edit dialog
 */

#include <glib.h>
#include <gtk/gtk.h>
#include "pixmaps/enabled.xpm"

/*
 * fe = filter edit
 */

/*
 * I know some of these don't need to be in the header.
 * I'll fix that later.
 */


typedef struct _option_list
{
    gchar *text;
    GtkWidget *widget;
} option_list;


static option_list fe_run_on[] =
{
    { "Inbound", NULL },
    { "Outbound", NULL },
    { "Pre-send", NULL },
    { "Demand", NULL }
};

static option_list fe_process_when[] =
{
    { "Matches", NULL },
    { "Doesn't Match", NULL },
    { "Never", NULL }
};

static option_list fe_search_type[] =
{
    { "Simple", NULL },
    { "Regular Expression", NULL },
    { "External Command", NULL}
};

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
void fe_clist_button_event_press(GtkWidget *widget,
				 GdkEventButton *bevent,
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
GtkWidget *fe_type_notebook;

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

/* match page widgets */

/* containers for radiobuttons */
GtkWidget *fe_type_frame;
GtkWidget *fe_type_box;
GtkWidget *fe_search_option_menu;
/* type notebook radiobuttons */
GtkWidget *fe_simple;
GtkWidget *fe_regex;
GtkWidget *fe_exec;
/* their callback */
void fe_checkbutton_toggled(GtkWidget *widget,
			    gpointer data);

/* Name field */
GtkWidget *fe_name_label;
GtkWidget *fe_name_entry;

/* when field */
GtkWidget *fe_when_frame;
GtkWidget *fe_when_box;
GtkWidget *fe_when_option_menu;

/* group field (inbound, outbound, etc) */
GtkWidget *fe_group_frame;
GtkWidget *fe_group_box;
GtkWidget *fe_group_option_menu;

/* widgets for the type notebook simple page */
GtkWidget *fe_type_simple_frame;
GtkWidget *fe_type_simple_table;
GtkWidget *fe_type_simple_all;
GtkWidget *fe_type_simple_body;
GtkWidget *fe_type_simple_header;
GtkWidget *fe_type_simple_to;
GtkWidget *fe_type_simple_from;
GtkWidget *fe_type_simple_subject;
GtkWidget *fe_type_simple_label;
GtkWidget *fe_type_simple_entry;
/* And callback */
void fe_type_simple_toggled(GtkWidget *widget,
		     gpointer data);

/* widgets for the type notebook regex page */
GtkWidget *fe_type_regex_scroll;
GtkWidget *fe_type_regex_list;
GtkWidget *fe_type_regex_box;
GtkWidget *fe_type_regex_add;
GtkWidget *fe_type_regex_remove;
GtkWidget *fe_type_regex_entry;
/* callbacks */
void fe_add_pressed(GtkWidget *widget,
		    gpointer data);
void fe_remove_pressed(GtkWidget *widget,
		       gpointer data);

/* Entry for the type notebook exec page */
GtkWidget *fe_type_exec_label;
GtkWidget *fe_type_exec_entry;

/* widgets for the Action page */

/* notification field */
GtkWidget *fe_notification_frame;
GtkWidget *fe_notification_table;
GtkWidget *fe_sound_button;
GtkWidget *fe_sound_entry;
GtkWidget *fe_sound_browse;
GtkWidget *fe_popup_button;
GtkWidget *fe_popup_entry;
/* callback for browse */
void fe_sound_browse_clicked(GtkWidget *widget,
			     gpointer throwaway);
/* action field */
GtkWidget *fe_action_frame;
GtkWidget *fe_action_table;
GtkWidget *fe_copy_button;
GtkWidget *fe_copy_entry;
GtkWidget *fe_move_button;
GtkWidget *fe_move_entry;
GtkWidget *fe_move_label;
GtkWidget *fe_print_button;
GtkWidget *fe_print_entry;
GtkWidget *fe_run_button;
GtkWidget *fe_run_entry;
GtkWidget *fe_delete_button;
GtkWidget *fe_delete_label;

/* disposition field */
GtkWidget *fe_disp_frame;
GtkWidget *fe_disp_box;
GtkWidget *fe_disp_place;
GtkWidget *fe_disp_continue;
GtkWidget *fe_disp_stop;

