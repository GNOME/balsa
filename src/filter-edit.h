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
    gint value;
    GtkWidget *widget;
  }
option_list;


static option_list fe_run_on[] =
{
    { "Inbound", 1, NULL },
    { "Outbound", 2, NULL },
    { "Pre-send", 3, NULL },
    { "Demand", 4, NULL }
};

static option_list fe_process_when[] =
{
    { "Matches", FILTER_MATCHES, NULL },
    { "Doesn't Match", FILTER_NOMATCH, NULL },
    { "Always", FILTER_ALWAYS, NULL }
};

static option_list fe_search_type[] =
{
    { "Simple", FILTER_SIMPLE, NULL },
    { "Regular Expression", FILTER_REGEX, NULL },
    { "External Command", FILTER_EXEC, NULL}
};

static option_list fe_actions[] =
{
    { "Copy to folder:", FILTER_COPY, NULL },
    { "Move to folder:", FILTER_MOVE, NULL },
    { "Print on printer:", FILTER_PRINT, NULL },
    { "Run program:", FILTER_RUN, NULL },
    { "Send to Trash", FILTER_TRASH, NULL}
};

/* and button callbacs */
void fe_dialog_button_clicked (GtkWidget * widget,
                               gint button,
                               gpointer data);



/*---------------- Left side of hbox ----------------*/

/* clist callbacks */
void fe_clist_select_row (GtkWidget * widget,
                          gint row, gint column,
                          GdkEventButton * bevent,
                          gpointer data);
void fe_clist_button_event_press (GtkWidget * widget,
                                  GdkEventButton * bevent,
                                  gpointer data);

/* their callbacks */
void fe_new_pressed (GtkWidget * widget,
                     gpointer data);
void fe_delete_pressed (GtkWidget * widget,
                        gpointer data);
void fe_up_pressed (GtkWidget * widget,
                    gpointer data);
void fe_down_pressed (GtkWidget * widget,
                      gpointer data);


/*---------------- Right side of hbox ----------------*/

/* its pages */
GtkWidget *fe_notebook_match_page;

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
void fe_apply_pressed (GtkWidget * widget,
                       gpointer data);
void fe_revert_pressed (GtkWidget * widget,
                        gpointer data);

/* match page widgets */

/* containers for radiobuttons */
GtkWidget *fe_type_frame;
GtkWidget *fe_type_box;
GtkWidget *fe_search_option_menu;
/* search type callback */
void fe_checkbutton_toggled (GtkWidget * widget,
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
void fe_type_simple_toggled (GtkWidget * widget,
                             gpointer data);

/* widgets for the type notebook regex page */
GtkWidget *fe_type_regex_scroll;
GtkWidget *fe_type_regex_list;
GtkWidget *fe_type_regex_box;
GtkWidget *fe_type_regex_add;
GtkWidget *fe_type_regex_remove;
GtkWidget *fe_type_regex_entry;
/* callbacks */
void fe_add_pressed (GtkWidget * widget,
                     gpointer data);
void fe_remove_pressed (GtkWidget * widget,
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
void fe_sound_browse_clicked (GtkWidget * widget,
                              gpointer throwaway);
/* action field */
GtkWidget *fe_action_frame;
GtkWidget *fe_action_table;
GtkWidget *fe_action_option_menu;
GtkWidget *fe_action_entry;
/* callback */
void fe_action_selected (GtkWidget * widget,
                         gpointer data);

/*GtkWidget *fe_copy_button;
   GtkWidget *fe_copy_entry;
   GtkWidget *fe_move_button;
   GtkWidget *fe_move_entry;
   GtkWidget *fe_move_label;
   GtkWidget *fe_print_button;
   GtkWidget *fe_print_entry;
   GtkWidget *fe_run_button;
   GtkWidget *fe_run_entry;
   GtkWidget *fe_delete_button;
   GtkWidget *fe_delete_label; */

/* disposition field */
GtkWidget *fe_disp_frame;
GtkWidget *fe_disp_box;
GtkWidget *fe_disp_place;
GtkWidget *fe_disp_continue;
GtkWidget *fe_disp_stop;
