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


typedef struct _option_list
  {
    gchar *text;
    gint value;
    GtkWidget *widget;
  }
option_list;

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
void clist_button_event_press (GtkWidget * widget,
                                  GdkEventButton * bevent,
                                  gpointer data);

/* button callbacks */
void fe_new_pressed (GtkWidget * widget,
                     gpointer data);
void fe_delete_pressed (GtkWidget * widget,
                        gpointer data);
void fe_up_pressed (GtkWidget * widget,
                    gpointer data);
void fe_down_pressed (GtkWidget * widget,
                      gpointer data);


/*---------------- Right side of hbox ----------------*/

/* The type notebook */
GtkWidget *fe_type_notebook;

/* apply and revert callbacks */
void fe_apply_pressed (GtkWidget * widget,
                       gpointer data);
void fe_revert_pressed (GtkWidget * widget,
                        gpointer data);

/* match page widgets */

/* containers for radiobuttons */
GtkWidget *fe_search_option_menu;
/* search type callback */
void fe_checkbutton_toggled (GtkWidget * widget,
                             gpointer data);

/* Name field */
GtkWidget *fe_name_label;
GtkWidget *fe_name_entry;

/* when field */
GtkWidget *fe_when_option_menu;

/* group field (inbound, outbound, etc) */
GtkWidget *fe_group_option_menu;

/* widgets for the type notebook simple page */
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
GtkWidget *fe_type_regex_list;
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
GtkWidget *fe_sound_button;
GtkWidget *fe_sound_entry;
GtkWidget *fe_popup_button;
GtkWidget *fe_popup_entry;

/* action field */
GtkWidget *fe_action_option_menu;
GtkWidget *fe_action_entry;
/* callback */
void fe_action_selected (GtkWidget * widget,
                         gpointer data);


/* disposition field */
GtkWidget *fe_disp_place;
GtkWidget *fe_disp_continue;
GtkWidget *fe_disp_stop;
