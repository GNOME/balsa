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
 * Callbacks for the filter edit dialog
 */

#include "config.h"

#include <time.h>
#include <gnome.h>

#include <string.h>
#include "filter.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include "filter-edit.h"
#include "filter-file.h"
#include "balsa-app.h"
#include "save-restore.h"

/* Defined in filter-edit-dialog.c*/
extern option_list fe_search_type[4];
extern GtkWidget * build_option_menu(option_list options[], gint num, GtkSignalFunc func);

/* The dialog widget (we need it to be able to close dialog on error) */

extern GnomeDialog * fe_window;
    
extern GtkCList * fe_filters_list;
    
extern gboolean fe_already_open;
    
/* The type notebook */
GtkWidget *fe_type_notebook;
    
/* containers for radiobuttons */
GtkWidget *fe_search_option_menu;
extern GtkWidget *fe_op_codes_option_menu;
    
/* Name field */
extern GtkWidget *fe_name_label;
extern GtkWidget *fe_name_entry;
    
/* widgets for the matching fields */
GtkWidget *fe_match_frame;
GtkWidget *fe_matching_fields_body;
GtkWidget *fe_matching_fields_to;
GtkWidget *fe_matching_fields_from;
GtkWidget *fe_matching_fields_subject;
GtkWidget *fe_matching_fields_cc;

/* widget for the conditions */
extern GtkCList *fe_conditions_list;
    
/* widgets for the type notebook simple page */
GtkWidget *fe_type_simple_label;
GtkWidget *fe_type_simple_entry;

/* widgets for the type notebook date page */
GtkWidget *fe_type_date_label;
GtkWidget *fe_type_date_low_entry,*fe_type_date_high_entry;

/* widgets for the type notebook regex page */
GtkCList *fe_type_regex_list;
GtkWidget *fe_type_regex_label;
GtkWidget *fe_type_regex_entry;

/* widgets for the type notebook condition flag page */
GtkWidget *fe_type_flag_label;
GtkWidget *fe_type_flag_buttons[4];

/* widgets for the Action page */

/* notification field */
extern GtkWidget *fe_sound_button;
extern GtkWidget *fe_sound_entry;
extern GtkWidget *fe_popup_button;
extern GtkWidget *fe_popup_entry;

/* action field */
extern GtkWidget *fe_action_option_menu;
extern GtkWidget *fe_action_entry;

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

/* condition_has_changed allows us to be smart enough not to make the whole process
 * of building a new condition when editing condition has not leaded to any modification
 */

static gboolean condition_has_changed;

/* Keep track of negation state */

static gboolean condition_not;

static GnomeDialog * condition_dialog=NULL;

static gboolean is_new_condition;

/*
 * Struct used to keep track of filter name changes (we must update config files accordingly)
 */

typedef struct _filters_names_rec {
    gchar * old_name;
    gchar * new_name;
} filters_names_rec;

/*
 * List containing all filters names changes
 */

static GList * filters_names_changes=NULL;

/*
 * List containing current names of new filters
 * This is necessary to have coherency in filters_names_changes
 */
static GList * new_filters_names=NULL;

/*
 * unique_filter_name()
 *
 * Checks the name of a filter being added to see if it is unique.
 *
 * Arguments:
 *    gchar *name - the preferred choice for a name
 *    current_row - the row to exclude from the search (-1 means we don't exclude any row)
 * Returns:
 *    0 if it exists another filter of name "name", else returns 1
 */

static gint
unique_filter_name(gchar * name,gint current_row)
{
    gchar *row_text;
    guint len;
    gint row = 0;

    if (!name || name[0] == '\0')
	return (0);

    len = strlen(name);

    for(row=0;row<fe_filters_list->rows;row++) {
	gtk_clist_get_text(fe_filters_list, row, 0, &row_text);
	if (len == strlen(row_text) &&
	    row!=current_row &&
	    strncmp(name, row_text, len) == 0)
	    return (0);
     }

    return 1;
}				/* end unique_filter_name() */

/**************** Conditions *************************/


/* Callback function to fill the regex entry with the selected regex */

void fe_regexs_select_row(GtkWidget * widget, gint row, gint column,
			  GdkEventButton * bevent, gpointer data)
{
    gchar *str;
    
    gtk_clist_get_text(fe_type_regex_list,row,0,&str);
    gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry),str);
}

/*
 * fe_typesmenu_cb()
 *
 * Handles toggling of the type checkbuttons.
 * When they are toggled, the notebook page must change
 */
static void
fe_typesmenu_cb(GtkWidget* widget, gpointer data)
{
    ConditionMatchType type=GPOINTER_TO_INT(data);

    condition_has_changed=TRUE;
    gtk_notebook_set_page(GTK_NOTEBOOK(fe_type_notebook),type-1);
    /* For certain types (date, flag)
     * match fields have no meaning so we disable them
     */
    gtk_widget_set_sensitive(fe_match_frame,(type!=CONDITION_DATE && type!=CONDITION_FLAG));
}			/* end fe_typesmenu_cb() */

/* fe_update_type_simple_label() writes the message of type simple condition
 */
static void 
fe_update_type_simple_label()
{
    const static gchar normal_string[] = 
      N_("One of the specified fields contains:");
    const static gchar negate_string[]=
      N_("None of the specified fields contains:");
    
    if (!fe_conditions_list->selection) return;

    gtk_label_set_text(GTK_LABEL(fe_type_simple_label),condition_not ? _(negate_string) : _(normal_string));
}                      /* end fe_update_type_simple_label */

/* fe_update_type_regex_label() writes the message of type regex condition
 */
static void fe_update_type_regex_label()
{
    static gchar normal_string[]=N_("One of the regular expressions matches");
    static gchar negate_string[]=N_("None of the regular expressions matches");
    
    if (!fe_conditions_list->selection) return;
    
    gtk_label_set_text(GTK_LABEL(fe_type_regex_label),condition_not ? _(negate_string) : _(normal_string));
}                      /* end fe_update_type_regex_label */

/* fe_update_type_date_label() writes the message of type date condition
 */
static void fe_update_type_date_label()
{
    static gchar normal_string[]=N_("Match when date is in the interval:");
    static gchar negate_string[]=N_("Match when date is outside the interval:");
    
    if (!fe_conditions_list->selection) return;
    
    gtk_label_set_text(GTK_LABEL(fe_type_date_label),condition_not ? _(negate_string) : _(normal_string));
}                      /* end fe_update_type_date_label */

/* fe_update_type_flag_label() writes the message of type flag condition
 */
static void fe_update_type_flag_label()
{
    static gchar normal_string[]=N_("Match when one of these flags is set:");
    static gchar negate_string[]=N_("Match when none of these flags is set:");
    
    if (!fe_conditions_list->selection) return;
    
    gtk_label_set_text(GTK_LABEL(fe_type_flag_label),condition_not ? _(negate_string) : _(normal_string));
}                      /* end fe_update_type_date_label */

static void
update_condition_list_label(void)
{
    GList * selected;
    LibBalsaCondition* cond;
    selected=fe_conditions_list->selection;
    if (!selected) return;

    cond= (LibBalsaCondition*)
      gtk_clist_get_row_data(fe_conditions_list,
                             GPOINTER_TO_INT(selected->data));
    gtk_clist_set_text(fe_conditions_list,GPOINTER_TO_INT(selected->data),0,
		       _(fe_search_type[cond->type-1].text));
}                      /* end fe_update_condition_list_label */

static ConditionMatchType
get_condition_type(void)
{
    GtkWidget * menu;

    /* Retrieve the selected item in the search type menu */
    menu=gtk_menu_get_active(GTK_MENU(gtk_option_menu_get_menu(GTK_OPTION_MENU(fe_search_option_menu))));
    /* Set the type associated with the selected item */
    return GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(menu),"value"));
}

/* fe_negate_condition :handle pressing on the "Contain/Does not Contain"... buttons */
static void
fe_negate_condition(GtkWidget * widget, gpointer data)
{
    GList * selected;

    selected=fe_conditions_list->selection;
    if (!selected) return;

    condition_not = !condition_not;
    switch (get_condition_type()) {
    case CONDITION_SIMPLE:
	fe_update_type_simple_label();
	break;
    case CONDITION_REGEX:
	fe_update_type_regex_label();
	break;
    case CONDITION_DATE:
	fe_update_type_date_label();
	break;
    case CONDITION_FLAG:
	fe_update_type_flag_label();
    case CONDITION_NONE:
	/* to avoid warnings */
    }
    condition_has_changed=TRUE;
}                      /* end fe_negate_condition */

/* Callback to say that the condition is changing
 */
static void
fe_condition_changed_cb(GtkWidget * widget,gpointer throwaway)
{
    condition_has_changed=TRUE;
}

/*
 *  void fe_match_fields_buttons_cb(GtkWidget * widget, gpointer data)
 *
 * Callback for the "All"/"All headers" buttons for match fields selection
 */
static void
fe_match_fields_buttons_cb(GtkWidget * widget, gpointer data)
{
    gboolean active=GPOINTER_TO_INT(data)!=3;  /* 3== uncheck all buttons */

    condition_has_changed=TRUE;
    if (!active || GPOINTER_TO_INT(data)==1) /* 1== check all buttons */
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_body),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_to),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_from),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_subject),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_cc),active);
}			/* end fe_match_fields_buttons_cb */

/* FIXME : to insure consistency and keep it simple I use a modal dialog box for condition edition/creation
 * but I have to avoid libbalsa_information (this is not modal and does not get the focus because mine is modal
 * so you end up with the small info box floating around and insensitive), so I use this function
 * This should be corrected
 */
static void
condition_error(const gchar * str)
{
    GnomeDialog * err_dia;

    err_dia=GNOME_DIALOG(gnome_warning_dialog(str));
    gtk_window_set_position(GTK_WINDOW(err_dia), GTK_WIN_POS_CENTER);
    gnome_dialog_run(err_dia);
}

/* conditon_validate is responsible of validating
 * the changes to the current condition, according to the widgets
 * Performs sanity check on the widgets 
 * holding datas of the condition being edited
 * eg : you must provide at least one regex for regex condition,
 * a non empty match string for simple condition...
 * returns FALSE if condition is not valid, else returns TRUE
 */
static gboolean
condition_validate(LibBalsaCondition* new_cnd)
{
    LibBalsaConditionRegex * new_reg;
    gchar * str,* p;
    gint match,row,col;
    struct tm date;

    /* Sanity checks, prevent "empty" condition */

    new_cnd->type=get_condition_type();
    /* Retrieve matching fields only if they are meaningful 
       for the condition type */
    if (new_cnd->type!=CONDITION_DATE && new_cnd->type!=CONDITION_FLAG) {
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_matching_fields_body)))
	    CONDITION_SETMATCH(new_cnd,CONDITION_MATCH_BODY);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_matching_fields_to)))
	    CONDITION_SETMATCH(new_cnd,CONDITION_MATCH_TO);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_matching_fields_subject)))
	    CONDITION_SETMATCH(new_cnd,CONDITION_MATCH_SUBJECT);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_matching_fields_from)))
	    CONDITION_SETMATCH(new_cnd,CONDITION_MATCH_FROM);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_matching_fields_cc)))
	    CONDITION_SETMATCH(new_cnd,CONDITION_MATCH_CC);
	if (new_cnd->match_fields==CONDITION_EMPTY) {
	    /* balsa_information(LIBBALSA_INFORMATION_ERROR,_("You must specify at least one field for matching"));*/
	    condition_error(_("You must specify at least one field for matching"));
	    return FALSE;
	}
    }
    switch (new_cnd->type) {
    case CONDITION_SIMPLE:
	str=gtk_entry_get_text(GTK_ENTRY(fe_type_simple_entry));
	if (!str || str[0]=='\0') {
	    /*balsa_information(LIBBALSA_INFORMATION_ERROR,_("You must provide a string"));*/
	    condition_error(_("You must provide a string"));
	    return FALSE;
	}
	break;
    case CONDITION_REGEX:
	if (!fe_type_regex_list->rows) {
	    /*balsa_information(LIBBALSA_INFORMATION_ERROR,_("You must provide at least one regular expression"));*/
	    condition_error(_("You must provide at least one regular expression"));
	    return FALSE;
	}
	break;
    case CONDITION_DATE:
	str=gtk_entry_get_text(GTK_ENTRY(fe_type_date_low_entry));
	if (str && str[0]!='\0') {
	    (void) strptime("00:00:00","%T",&date);
	    p=(gchar *)strptime(str,"%x",&date);
	    if (!p || *p!='\0') {
		/*balsa_information(LIBBALSA_INFORMATION_ERROR,_("Low date is incorrect"));*/
		condition_error(_("Low date is incorrect"));
		return FALSE;
	    }
	    new_cnd->match.interval.date_low=mktime(&date);
	}
	str=gtk_entry_get_text(GTK_ENTRY(fe_type_date_high_entry));
	if (str && str[0]!='\0') {
	    (void) strptime("23:59:59","%T",&date);
	    p=(gchar *)strptime(str,"%x",&date);
	    if (!p || *p!='\0') {
		/*balsa_information(LIBBALSA_INFORMATION_ERROR,_("High date is incorrect"));*/
		condition_error(_("High date is incorrect"));
		return FALSE;
	    }
	    new_cnd->match.interval.date_high=mktime(&date);
	}
	if (new_cnd->match.interval.date_low>new_cnd->match.interval.date_high) {
	    /*balsa_information(LIBBALSA_INFORMATION_ERROR,_("Low date is greater than high date"));*/
	    condition_error(_("Low date is greater than high date"));
	    return FALSE;
	}
	break;
    case CONDITION_FLAG:
    case CONDITION_NONE:
	/* to avoid warnings */
    }

    /* Sanity checks OK, retrieve datas from widgets */

    new_cnd->condition_not=condition_not;

    /* Set the type specific fields of the condition */
    switch (new_cnd->type) {
    case CONDITION_SIMPLE:
	new_cnd->match.string=g_strdup(gtk_entry_get_text(GTK_ENTRY(fe_type_simple_entry)));
	break;

    case CONDITION_REGEX:
	for (row=0; row<fe_type_regex_list->rows; row++) {
          new_reg = libbalsa_condition_regex_new();
          gtk_clist_get_text(fe_type_regex_list,row,0,&str);
          new_reg->string = g_strdup(str);
          new_cnd->match.regexs=g_slist_prepend(new_cnd->match.regexs,new_reg);
	}
	new_cnd->match.regexs=g_slist_reverse(new_cnd->match.regexs);
	break;

    case CONDITION_DATE:
	break;

    case CONDITION_FLAG:
	new_cnd->match.flags=0;
	for (row=0;row<2;row++)
	    for (col=0;col<2;col++)
		new_cnd->match.flags|=
		    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_type_flag_buttons[row*2+col])) ? 1 << (row*2+col+1): 0;

    case CONDITION_NONE:
	/* To avoid warnings :) */
    }
    return TRUE;
}                        /* condition_validate*/

static void
clear_condition_widgets()
{
    gtk_entry_set_text(GTK_ENTRY(fe_type_simple_entry),"");
    gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry),"");	    
    gtk_clist_clear(fe_type_regex_list);
    gtk_entry_set_text(GTK_ENTRY(fe_type_date_low_entry),"");
    gtk_entry_set_text(GTK_ENTRY(fe_type_date_high_entry),"");
}

/* fill_condition_widget : Fill all widget according to condition data
   fields
*/

static void
fill_condition_widgets(LibBalsaCondition* cnd)
{
    GSList * regex;
    gchar str[20];
    struct tm * date;
    gint row,col;
    gboolean andmask;
    
    condition_not=cnd->condition_not;
    /* Clear all widgets */
    if (cnd->type!=CONDITION_SIMPLE)
	gtk_entry_set_text(GTK_ENTRY(fe_type_simple_entry),"");

    if (cnd->type!=CONDITION_REGEX)
	gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry),"");	    

    gtk_clist_clear(fe_type_regex_list);

    gtk_notebook_set_page(GTK_NOTEBOOK(fe_type_notebook),
			  cnd->type-1);

    gtk_option_menu_set_history(GTK_OPTION_MENU(fe_search_option_menu), cnd->type-1);

    /* First update matching fields
     * but if type is date or flag, these are meaning less so we disable them */
    andmask = (cnd->type!=CONDITION_FLAG && cnd->type!=CONDITION_DATE);
    gtk_widget_set_sensitive(fe_match_frame,andmask);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_body),
				 CONDITION_CHKMATCH(cnd,CONDITION_MATCH_BODY) && andmask);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_to),
				 CONDITION_CHKMATCH(cnd,CONDITION_MATCH_TO) && andmask);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_from),
				 CONDITION_CHKMATCH(cnd,CONDITION_MATCH_FROM) && andmask);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_subject),
				 CONDITION_CHKMATCH(cnd,CONDITION_MATCH_SUBJECT) && andmask);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_cc),
				 CONDITION_CHKMATCH(cnd,CONDITION_MATCH_CC) && andmask);
    /* Next update type specific fields */
    switch (cnd->type) {
    case CONDITION_SIMPLE:
	gtk_entry_set_text(GTK_ENTRY(fe_type_simple_entry),cnd->match.string==NULL ? "" : cnd->match.string);
	fe_update_type_simple_label();
	break;
    case CONDITION_REGEX:
	for (regex=cnd->match.regexs;regex;regex=g_slist_next(regex))
	    gtk_clist_append(fe_type_regex_list,&(((LibBalsaConditionRegex *)regex->data)->string));
	fe_update_type_regex_label();
	break;
    case CONDITION_DATE:
	if (cnd->match.interval.date_low==0) str[0]='\0';
	else {
	    date=localtime(&cnd->match.interval.date_low);
	    strftime(str,sizeof(str),"%x",date);
	}
	gtk_entry_set_text(GTK_ENTRY(fe_type_date_low_entry),str);
	if (cnd->match.interval.date_high==0) str[0]='\0';
	else {
	    date=localtime(&cnd->match.interval.date_high);
	    strftime(str,sizeof(str),"%x",date);
	}
	gtk_entry_set_text(GTK_ENTRY(fe_type_date_high_entry),str);
	fe_update_type_date_label();
	break;
    case CONDITION_FLAG:
	for (row=0;row<2;row++)
	    for (col=0;col<2;col++)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_type_flag_buttons[row*2+col]),
					     cnd->match.flags & (1 << (row*2+col+1)));
	fe_update_type_flag_label();
	break;
    case CONDITION_NONE:
	/* To avoid warnings :), we should never get there */
    }
    gtk_menu_set_active(GTK_MENU(gtk_option_menu_get_menu(GTK_OPTION_MENU(fe_search_option_menu))),cnd->type-1);
}            /* end fill_condition_widget */

static void
condition_dialog_button_clicked(GtkWidget * dialog, gint button, 
                                gpointer throwaway)
{
    LibBalsaCondition* new_cnd;
    gint row;

    switch (button) {
    case 0:  /* OK button */
	if (condition_has_changed) {
	    new_cnd = libbalsa_condition_new();
            if (!condition_validate(new_cnd))
              return;
            /* No error occured, condition is valid, so change/add it based on is_new_condition
             * and only if something has changed of course
             */
            if (condition_has_changed) {
              if (!is_new_condition) {
                /* We free the old condition*/
                row=GPOINTER_TO_INT(fe_conditions_list->selection->data);
                libbalsa_condition_free((LibBalsaCondition*)
                                        gtk_clist_get_row_data(fe_conditions_list,row));
              }
              else {
                gchar * str[]={""};
                /* It was a new condition, so add it to the list */
                row=gtk_clist_append(fe_conditions_list,str);
                gtk_clist_select_row(fe_conditions_list,row,-1);
              }
              /* Associate the new condition to the row in the clist*/
              gtk_clist_set_row_data(fe_conditions_list,row,new_cnd);
              /* And refresh the conditions list */
              update_condition_list_label();
            }
	}
    case 1:  /* Cancel button */
	/* we only hide it because it is too expensive to destroy and rebuild each time */
	gtk_widget_hide_all(dialog);
	break;
    case 2:  /* Help button */
	/* FIXME */
    }
}

/* build_type_notebook
 * build the notebook containing one page for each condition type (simple, regex, date, flag)
 */
static void build_type_notebook()
{
    GtkWidget *page,*frame;
    GtkWidget *scroll;
    GtkWidget *box;
    GtkWidget *button;
    gint row,col;
    static gchar * flag_names[]={N_("New"),N_("Deleted"),N_("Replied"),N_("Flagged")};

    /* The notebook */

    fe_type_notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(fe_type_notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(fe_type_notebook), FALSE);

    /* The simple page of the notebook */

    page = gtk_table_new(5, 3, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);

    fe_type_simple_label = gtk_label_new(_("One of the specified fields contains:"));
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_simple_label,
		     0, 5, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_type_simple_entry = gtk_entry_new();
    gtk_signal_connect(GTK_OBJECT(fe_type_simple_entry),
		       "changed", GTK_SIGNAL_FUNC(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_simple_entry,
		     0, 5, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    button = gtk_button_new_with_label(_("Contain/Does not contain"));
    gtk_signal_connect(GTK_OBJECT(button),
		       "clicked", GTK_SIGNAL_FUNC(fe_negate_condition), NULL);
    gtk_table_attach(GTK_TABLE(page),
		     button,
		     0, 5, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    
    /* The regex page of the type notebook */

    page = gtk_table_new(5, 6, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);

    fe_type_regex_label = gtk_label_new(_("One of the regular expressions matches"));
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_regex_label,
		     0, 5, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_table_attach(GTK_TABLE(page),
		     scroll,
		     0, 5, 2, 4,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, 2, 2);

    fe_type_regex_list = GTK_CLIST(gtk_clist_new(1));

    gtk_clist_set_selection_mode(fe_type_regex_list, GTK_SELECTION_SINGLE);
    gtk_clist_set_row_height(fe_type_regex_list, 0);
    gtk_clist_set_reorderable(fe_type_regex_list, FALSE);
    gtk_clist_set_use_drag_icons(fe_type_regex_list, FALSE);

    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll),
					  GTK_WIDGET(fe_type_regex_list));
    gtk_signal_connect(GTK_OBJECT(fe_type_regex_list), "select_row",
		       GTK_SIGNAL_FUNC(fe_regexs_select_row), NULL);

    box = gtk_hbox_new(TRUE, 5);
    gtk_table_attach(GTK_TABLE(page),
		     box,
		     0, 5, 4, 5,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    button = gtk_button_new_with_label(_("Add"));
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(button),
		       "clicked", GTK_SIGNAL_FUNC(fe_add_pressed), NULL);
    button = gtk_button_new_with_label(_("Remove"));
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(button),
		       "clicked",
		       GTK_SIGNAL_FUNC(fe_remove_pressed), NULL);
    button = gtk_button_new_with_label(_("One matches/None matches"));
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(button),
		       "clicked", GTK_SIGNAL_FUNC(fe_negate_condition), NULL);

    fe_type_regex_entry = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_regex_entry,
		     0, 5, 5, 6,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);

    /* The date page of the notebook */

    page = gtk_table_new(5, 3, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);

    fe_type_date_label = gtk_label_new(_("Match when date is in the interval:"));
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_date_label,
		     0, 5, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_type_date_low_entry = gtk_entry_new();
    gtk_signal_connect(GTK_OBJECT(fe_type_date_low_entry),
		       "changed", GTK_SIGNAL_FUNC(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_date_low_entry,
		     0, 2, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_type_date_high_entry = gtk_entry_new();
    gtk_signal_connect(GTK_OBJECT(fe_type_date_high_entry),
		       "changed", GTK_SIGNAL_FUNC(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_date_high_entry,
		     3, 5, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    button = gtk_button_new_with_label(_("Inside/outside the date interval"));
    gtk_signal_connect(GTK_OBJECT(button),
		       "clicked", GTK_SIGNAL_FUNC(fe_negate_condition), NULL);
    gtk_table_attach(GTK_TABLE(page),
		     button,
		     0, 5, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    /* The flag page of the notebook */

    page = gtk_table_new(1, 2, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);
    fe_type_flag_label = gtk_label_new(_("Match when one of these flags is set:"));
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_flag_label,
		     0, 1, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    frame = gtk_frame_new(NULL);
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(page),
		     frame,
		     0, 1, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    page = gtk_table_new(2, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), page);

    for (row=0;row<2;row++)
	for (col=0;col<2;col++) {
	    fe_type_flag_buttons[row*2+col] = gtk_check_button_new_with_label(_(flag_names[row*2+col]));
	    gtk_signal_connect(GTK_OBJECT(fe_type_flag_buttons[row*2+col]),
			       "toggled",
			       GTK_SIGNAL_FUNC(fe_condition_changed_cb),
			       NULL);
	    gtk_table_attach(GTK_TABLE(page),
			     fe_type_flag_buttons[row*2+col],
			     col, col+1,row,row+1,
			     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
	}
    button = gtk_button_new_with_label(_("Match when one flag is set/when no flag is set"));
    gtk_signal_connect(GTK_OBJECT(button),
		       "clicked", GTK_SIGNAL_FUNC(fe_negate_condition), NULL);
    gtk_table_attach(GTK_TABLE(page),
		     button,
		     0, 2, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
}				/* end build_type_notebook() */

static
void build_condition_dialog()
{
    GtkWidget * table,* frame,* button,* page,* box;

    page = gtk_table_new(3, 7, FALSE);
    /* builds the toggle buttons to specify fields concerned by the conditions of
     * the filter */
    
    fe_match_frame = gtk_frame_new(_("Match in:"));
    gtk_frame_set_label_align(GTK_FRAME(fe_match_frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_match_frame), GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(page),
		     fe_match_frame,
		     0, 3, 0, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    
    table = gtk_table_new(3, 3, TRUE);
    gtk_container_add(GTK_CONTAINER(fe_match_frame), table);
    
    button = gtk_button_new_with_label(_("All"));
    gtk_table_attach(GTK_TABLE(table),
		     button,
		     0, 1, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2); 
    gtk_signal_connect(GTK_OBJECT(button),"clicked",GTK_SIGNAL_FUNC(fe_match_fields_buttons_cb),GINT_TO_POINTER(1));
    button = gtk_button_new_with_label(_("All headers"));
    gtk_table_attach(GTK_TABLE(table),
		     button,
		     1, 2, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    gtk_signal_connect(GTK_OBJECT(button),"clicked",GTK_SIGNAL_FUNC(fe_match_fields_buttons_cb),GINT_TO_POINTER(2));
    button = gtk_button_new_with_label(_("Clear"));
    gtk_table_attach(GTK_TABLE(table),
		     button,
		     2, 3, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    gtk_signal_connect(GTK_OBJECT(button),"clicked",GTK_SIGNAL_FUNC(fe_match_fields_buttons_cb),GINT_TO_POINTER(3));
    
    fe_matching_fields_body = gtk_check_button_new_with_label(_("Body"));
    gtk_table_attach(GTK_TABLE(table),
		     fe_matching_fields_body,
		     0, 1, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    gtk_signal_connect(GTK_OBJECT(fe_matching_fields_body),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_condition_changed_cb),
		       NULL);
    fe_matching_fields_to = gtk_check_button_new_with_label(_("To:"));
    gtk_table_attach(GTK_TABLE(table),
		     fe_matching_fields_to,
		     1, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    gtk_signal_connect(GTK_OBJECT(fe_matching_fields_to),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_condition_changed_cb),
		       NULL);
    fe_matching_fields_from = gtk_check_button_new_with_label(_("From:"));
    gtk_table_attach(GTK_TABLE(table),
		     fe_matching_fields_from,
		     1, 2, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    gtk_signal_connect(GTK_OBJECT(fe_matching_fields_from),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_condition_changed_cb),
		       NULL);
    fe_matching_fields_subject = gtk_check_button_new_with_label(_("Subject"));
    gtk_table_attach(GTK_TABLE(table),
		     fe_matching_fields_subject,
		     2, 3, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    gtk_signal_connect(GTK_OBJECT(fe_matching_fields_subject),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_condition_changed_cb),
		       NULL);
    fe_matching_fields_cc = gtk_check_button_new_with_label(_("Cc:"));
    gtk_table_attach(GTK_TABLE(table),
		     fe_matching_fields_cc,
		     2, 3, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    gtk_signal_connect(GTK_OBJECT(fe_matching_fields_cc),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_condition_changed_cb),
		       NULL);

    frame = gtk_frame_new(_("Selected condition search type:"));
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
    gtk_table_attach(GTK_TABLE(page),
		     frame,
		     0, 3, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    box = gtk_hbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(frame), box);

    fe_search_option_menu = build_option_menu(fe_search_type,
					      ELEMENTS(fe_search_type),
					      GTK_SIGNAL_FUNC(fe_typesmenu_cb));
    gtk_box_pack_start(GTK_BOX(box), fe_search_option_menu, FALSE, FALSE, 5);

    build_type_notebook();
    gtk_table_attach(GTK_TABLE(page),
		     fe_type_notebook,
		     0, 3, 3, 7,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, 5, 5);
    gtk_box_pack_start(GTK_BOX(condition_dialog->vbox),page,FALSE,FALSE,2);
}

/*
 * fe_edit_condition is the callback to edit command for conditions
 * this is done popping a modal dialog
 * is_new_cnd is 1 if user is creating a new condition, else it is 0
 */

void
fe_edit_condition(GtkWidget * throwaway,gpointer is_new_cnd)
{
    gchar * title;
    LibBalsaCondition* cnd=NULL;
    gint row=-1;

    if (!fe_filters_list->selection || (!is_new_cnd && !fe_conditions_list->selection)) return;

    is_new_condition=GPOINTER_TO_INT(is_new_cnd);
    if (!is_new_condition) {
	row=GPOINTER_TO_INT(fe_conditions_list->selection->data);
	cnd=(LibBalsaCondition*)gtk_clist_get_row_data(fe_conditions_list,row);
	condition_has_changed=FALSE;
    }
    /* This a new condition, we set condition_has_changed to TRUE to force validation and replacement*/
    else condition_has_changed=TRUE;
    /* We construct the dialog box if it wasn't done before */
    if (!condition_dialog) {
	condition_dialog=GNOME_DIALOG(gnome_dialog_new("",
						       GNOME_STOCK_BUTTON_OK,
						       GNOME_STOCK_BUTTON_CANCEL,
						       GNOME_STOCK_BUTTON_HELP, NULL));

	if (!condition_dialog) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Error : not enough memory"));
	    return;
	}

	gtk_signal_connect(GTK_OBJECT(condition_dialog),
			   "clicked", condition_dialog_button_clicked, NULL);
	/* Now we build the dialog*/
	build_condition_dialog();
	/* For now this box is modal */
	gtk_window_set_modal(GTK_WINDOW(condition_dialog),TRUE);

    }
    title = g_strconcat(_("Edit condition for filter: "),
			((LibBalsaFilter*)
                         gtk_clist_get_row_data(fe_filters_list,GPOINTER_TO_INT(fe_filters_list->selection->data)))->name,
			NULL);
    gtk_window_set_title(GTK_WINDOW(condition_dialog),title);
    g_free(title);
    /* We fire the dialog box */
    gtk_widget_show_all(GTK_WIDGET(condition_dialog));
    if (cnd) fill_condition_widgets(cnd);
    else clear_condition_widgets();
}

/* fe_conditions_select_row : update all widget when a condition is selected
 */
void
fe_conditions_select_row(GtkWidget * widget, gint row, gint column,
                         GdkEventButton * bevent, gpointer data)
{
    if (bevent == NULL)
	return;

    if (bevent->type == GDK_2BUTTON_PRESS)
	fe_edit_condition(NULL,GINT_TO_POINTER(0));
}

void
fe_condition_remove_pressed(GtkWidget * widget, gpointer data)
{
    GList *selected;
    gint row;
    
    selected = fe_conditions_list->selection;
    
    if (!selected)
      return;
    row=GPOINTER_TO_INT(selected->data);
    libbalsa_condition_free((LibBalsaCondition*) 
                            gtk_clist_get_row_data(fe_conditions_list,row));
    gtk_clist_remove(fe_conditions_list,row);
}

/**************** Filters ****************************/

/*
 * Function that is called when traversing the tree of mailboxes
 * to replace filters name that have changed in the filters list of mailboxes
 * and to invalidate previously constructed list of filters, to force reloading
 */
static
gboolean update_filters_mailbox(GNode * node,gpointer throwaway)
{
    BalsaMailboxNode *mbnode = (BalsaMailboxNode *) node->data;
    gchar * tmp;

    if (mbnode->mailbox) {
	/* First we free the filters list (which is now obsolete) */
	//g_print("Updating mailbox %s, filters==NULL=%d\n",mbnode->mailbox->url,mbnode->mailbox->filters==NULL);
	g_slist_free(mbnode->mailbox->filters);
	mbnode->mailbox->filters=NULL;
	/* Second we replace old filters name by the new ones
	 * Note : deleted filters are also removed */
	if (!filters_names_changes) 
	    return FALSE;
	tmp=mailbox_filters_section_lookup(mbnode->mailbox->url);
	//g_print("Section is : %s\n",tmp);
	if (tmp) {
	    gchar **filters_names=NULL;
	    gboolean def;
	    guint nb_filters;
	    
	    gnome_config_push_prefix(tmp);
	    gnome_config_get_vector_with_default(MAILBOX_FILTERS_KEY,&nb_filters,&filters_names,&def);
	    if (!def) {
		guint i;
		GList * lst;
		
		for (i=0;i<nb_filters;) {
		    for (lst=filters_names_changes;
			 lst &&
			 strcmp(((filters_names_rec *)lst->data)->old_name,filters_names[i])!=0;
			 lst=g_list_next(lst));

			if (lst) {
			    //g_print("Find %s in changed ones\n",filters_names[i]);
			    g_free(filters_names[i]);
			    if (((filters_names_rec *)lst->data)->new_name) {
				/* Name changing */
				filters_names[i++]=g_strdup(((filters_names_rec *)lst->data)->new_name);
				//g_print("Replaced by %s\n",filters_names[i-1]);			    
			    }
			    else {
				/* Name removing */
				guint j;
				
				//g_print("Removed\n");
				for (j=i;j<nb_filters-1;j++)
				    filters_names[j]=filters_names[j+1];
				filters_names[--nb_filters]=NULL;
			    }
			}
			else i++;
		}
	    }
	    
	    if (nb_filters) {
		gnome_config_set_vector(MAILBOX_FILTERS_KEY,nb_filters,(const gchar **)filters_names);	    
		gnome_config_pop_prefix();
	    }
	    else {
		gnome_config_pop_prefix();
		gnome_config_clean_section(tmp);
	    }
	    g_strfreev(filters_names);
	    g_free(tmp);
	}
    }
    return FALSE;
}

/* Destroy callback : do all needed clean-ups
 * FIXME : be sure no more destruction is needed
 */

void fe_destroy_window_cb(GtkWidget * widget,gpointer throwaway)
{
    GList * lst;
    
    /* We clear the current edited conditions list */
    fe_free_associated_conditions();
    /* Destroy the condition dialog */
    if (condition_dialog) {
	gtk_widget_destroy(GTK_WIDGET(condition_dialog));
	condition_dialog=NULL;
    }
    /* Litte hack : on OK button press we have set the data of row 0 to NULL
     * thus we don't free filters in this case */
    if (fe_filters_list->rows && gtk_clist_get_row_data(fe_filters_list,0))
	fe_free_associated_filters();

    for (lst=filters_names_changes;lst;lst=g_list_next(lst)) {
	/*g_print("Old=%s <> New=%s\n",
		((filters_names_rec *)lst->data)->old_name,
		((filters_names_rec *)lst->data)->new_name);*/
	g_free(((filters_names_rec *)lst->data)->old_name);
	g_free(((filters_names_rec *)lst->data)->new_name);
	g_free((filters_names_rec *)lst->data);
    }

    g_list_free(filters_names_changes);
    filters_names_changes=NULL;

    for (lst=new_filters_names;lst;lst=g_list_next(lst)) {
	//g_print("New filters, actual name : %s\n",(gchar *)lst->data);
	g_free((gchar *)lst->data);
    }

    g_list_free(new_filters_names);
    new_filters_names=NULL;

    fe_already_open=FALSE;
}

/*
 * fe_dialog_button_clicked()
 *
 * Handles the clicking of the main buttons at the 
 * bottom of the dialog.  wooo.
 */
void
fe_dialog_button_clicked(GtkWidget * dialog, gint button, gpointer data)
{
    gint row;
    GList * names_lst;
    
    switch (button) {
    case 0:			/* OK button */
	/* We clear the old filters */
	libbalsa_filter_clear_filters(balsa_app.filters,TRUE);
	balsa_app.filters=NULL;
	
	/* We put the modified filters */
	for (row=0;row<fe_filters_list->rows;row++) {
	    balsa_app.filters=g_slist_prepend(balsa_app.filters,
					      gtk_clist_get_row_data(fe_filters_list,row));
	}

	/* Little hack to tell the clean-up functions not to free filters on OK button press */
	if (fe_filters_list->rows) gtk_clist_set_row_data(fe_filters_list,0,NULL);

	/* Update mailboxes filters */
	g_node_traverse(balsa_app.mailbox_nodes,
			G_LEVEL_ORDER,
			G_TRAVERSE_ALL, 10, update_filters_mailbox, NULL);

	/* Update filters config (remove sections of removed filters) */
	for (names_lst=filters_names_changes;names_lst;names_lst=g_list_next(names_lst))
	    if (!((filters_names_rec *)names_lst->data)->new_name) {
		// g_print("Cleaning section for %s\n",((filters_names_rec *)names_lst->data)->old_name);
		clean_filter_config_section(((filters_names_rec *)names_lst->data)->old_name);
	    }
	
	gnome_dialog_close(GNOME_DIALOG(dialog));
	config_filters_save();
	break;

    case 1:			/* Cancel button */
	gnome_dialog_close(GNOME_DIALOG(dialog));
	break;

    case 2:			/* Help button */
	/* more of something here */

    default:
	/* we should NEVER get here */
	break;
    }
}			/* end fe_dialog_button_clicked */

/*
 * fe_action_selected()
 *
 * Callback for the "Action" option menu
 */ 
void
fe_action_selected(GtkWidget * widget, gpointer data)
{
    gtk_widget_set_sensitive(GTK_WIDGET(fe_action_entry),
			     GPOINTER_TO_INT(data)!=FILTER_TRASH);
}			/* end fe_action_selected() */

/*
 * fe_add_pressed()
 *
 * Callback for the "Add" button for the regex type
 */
void fe_add_pressed(GtkWidget * widget, gpointer throwaway)
{
    gchar *text;

    text = gtk_entry_get_text(GTK_ENTRY(fe_type_regex_entry));
    
    if (!text || text[0] == '\0')
	return;
    
    gtk_clist_append(fe_type_regex_list, &text);
    condition_has_changed=TRUE;
}			/* end fe_add_pressed() */

/*
 * fe_remove_pressed()
 * 
 * Callback for the "remove" button of the regex type
 */
void
fe_remove_pressed(GtkWidget * widget, gpointer throwaway)
{
    GList *selected;
    
    selected = fe_type_regex_list->selection;
    
    if (!selected)
		 return;
    
    gtk_clist_remove(fe_type_regex_list, GPOINTER_TO_INT(selected->data));
    condition_has_changed=TRUE;
}			/* end fe_remove_pressed() */

/************************************************************/
/******** Functions handling filters ************************/
/************************************************************/

/*
 * Add a filter name change in the list
 * if new_name==NULL, it's a deletion
 */
static
void change_filter_name(gchar * old_name,gchar * new_name)
{
    //g_print("Changing from %s to %s\n",old_name,new_name);
    if (!new_name || strcmp(old_name,new_name)!=0) {
	GList * lst;
	filters_names_rec * p=NULL;

	/* First we check if the filter that changes has been created in this session (looking new_filters_names list)
	 * if yes we update new_filters_names, and that's all because we have no reference to
	 * it in any mailbox, because it's new
	 */

	for (lst=new_filters_names;lst && strcmp(old_name,(gchar *) lst->data)!=0;lst=g_list_next(lst));
	if (lst) {	    
	    /* Found it ! Update new_filters_names */
	    //g_print("Catch a new filter change : %s --> %s\n",old_name,new_name);
	    g_free((gchar*)lst->data);
	    if (new_name)
		lst->data=g_strdup(new_name);
	    else {
		//g_print("Collapsed new filter change\n");
		new_filters_names=g_list_remove_link(new_filters_names,lst);
		g_list_free_1(lst);
	    }
	    return;
	}

	for (lst=filters_names_changes;lst;lst=g_list_next(lst))
	    if (((filters_names_rec *)lst->data)->new_name && 
		strcmp(((filters_names_rec *)lst->data)->new_name,old_name)==0) {
		/*g_print("Found previous %s->%s\n",
			((filters_names_rec *)lst->data)->old_name,
			((filters_names_rec *)lst->data)->new_name);*/
		p=(filters_names_rec *)lst->data;
		g_free(p->new_name);
		break;
	    }
	if (!lst) {
	    /* New name change, create record */
	    p=g_new(filters_names_rec,1);
	    p->old_name=g_strdup(old_name);
	    filters_names_changes=g_list_prepend(filters_names_changes,p);
	}
	/* Record exists yet, test if we can collapse it (in case his old_name==new_name)
	 * It's only a small optimization
	 */
	else if (new_name && strcmp(p->old_name,new_name)==0) {
	    //g_print("Collapsed\n");
	    filters_names_changes=g_list_remove_link(filters_names_changes,lst);
	    g_list_free_1(lst);
	    return;
	}

	if (new_name)
	    p->new_name=g_strdup(new_name);
	else p->new_name=NULL;
	/*g_print("Added %s->%s\n",
		p->old_name,
		p->new_name);*/
    }
}

/*
 * fe_new_pressed()
 *
 * Callback for the "new" filter button
 */

void
fe_new_pressed(GtkWidget * widget, gpointer data)
{
    gint new_row;
    LibBalsaFilter* fil;
    gchar * new_item[] = { N_("New filter") };

    if (!unique_filter_name(new_item[0],-1)) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("There is yet a ""New Filter"" in the list, "
                            "rename it first"));
	return;
    }

    fil = libbalsa_filter_new();

    if (filter_errno!=FILTER_NOERR) {
	filter_perror(filter_strerror(filter_errno));
	return;
    }

    new_row = gtk_clist_append(fe_filters_list, new_item);
    
    /* Fill the filter with default values */

    fil->name=g_strdup(new_item[0]);
    fil->conditions_op=FILTER_OP_OR;

    FILTER_SETFLAG(fil,FILTER_COMPILED);
    fil->action=FILTER_MOVE;

    gtk_clist_set_row_data(fe_filters_list,new_row,(gpointer) fil);

    /* Selecting the row will also display the new filter */
    gtk_clist_select_row(fe_filters_list, new_row, -1);

    /* Adds "New Filter" to the list of actual new filters names */
    new_filters_names=g_list_prepend(new_filters_names,g_strdup(new_item[0]));
}			/* end fe_new_pressed() */

/*
 * Helper function to keep track of changed/removed filters
 * if new_name==NULL the filter has been removed
 */

/*
 * fe_delete_pressed()
 *
 * Callback for the "Delete" button
 */
void
fe_delete_pressed(GtkWidget * widget, gpointer data)
{
    gint row;
    LibBalsaFilter *fil;

    if (!fe_filters_list->selection)
	return;
    
    row = GPOINTER_TO_INT(fe_filters_list->selection->data);
    
    fil = (LibBalsaFilter*) gtk_clist_get_row_data(fe_filters_list, row);
    
    g_assert(fil);
    change_filter_name(fil->name,NULL);
    libbalsa_filter_free(fil, NULL);
    
    gtk_clist_remove(fe_filters_list, row);
}			/* end fe_delete_pressed() */

/*
 * fe_apply_pressed()
 *
 * Builds a new filter from the data provided, check for correctness (regex compil...)
 * and sticks it where the selection is in the clist
 * FIXME : Only basic checks are made
 * we can have a filter with unvalid conditions
 */
void
fe_apply_pressed(GtkWidget * widget, gpointer data)
{
    LibBalsaFilter *fil,*old;
    gchar *temp,*action_str;
    GtkWidget * menu;
    gint row,i;
    FilterActionType action;

    if (!fe_filters_list->selection)
	return;

    row = GPOINTER_TO_INT(fe_filters_list->selection->data);
    /* quick check before we malloc */
    temp = gtk_entry_get_text(GTK_ENTRY(fe_name_entry));
    if ((!temp) || (temp[0] == '\0')
	|| (!unique_filter_name(temp,row))) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,_("Invalid filter name\n"));
	return;
    }
    
    action_str=gtk_entry_get_text(GTK_ENTRY(fe_action_entry));

    /* Retrieve the selected item in the action menu */
    menu=gtk_menu_get_active(GTK_MENU(gtk_option_menu_get_menu(GTK_OPTION_MENU(fe_action_option_menu))));
    /* Set the type associated with the selected item */
    action=GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(menu),"value"));
    
    if (action!=FILTER_RUN && action!=FILTER_TRASH) {
	if (!action_str || action_str[0] == '\0' || !mblist_find_mbox_by_name(balsa_app.mblist,action_str)) {
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Invalid mailbox name\n"));
	    return;
	}
    }

    if (!fe_conditions_list->rows) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Your filter must have conditions\n"));
	return;
    }
    /* Construct the new filter according with the data fields */

    fil = libbalsa_filter_new();
    if (filter_errno!=FILTER_NOERR) {
	filter_perror(filter_strerror(filter_errno));
	gnome_dialog_close(fe_window);
	return;
    }

   /* Set name of the filter */

    fil->name=g_strdup(temp);

    /* Retrieve the selected item in the op codes menu */

    menu=gtk_menu_get_active(GTK_MENU(gtk_option_menu_get_menu(GTK_OPTION_MENU(fe_op_codes_option_menu))));

    /* Set the op-codes associated with the selected item */

    fil->conditions_op=GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(menu),"value"));

    /* Retrieve all conditions for that filter */

    FILTER_SETFLAG(fil,FILTER_VALID);

    /* Here I set back FILTER_COMPILED, that way, modified filters with no Regex condition
     * won't have to recalculate regex (that they don't have actually :)
     * but modified filters with regex condition will have their compiled flag unset by filter_append_condition,
     * So that's OK
     */
    FILTER_SETFLAG(fil,FILTER_COMPILED);

    for (i=0; i<fe_conditions_list->rows && filter_errno==FILTER_NOERR; i++) {
      LibBalsaCondition *cond = gtk_clist_get_row_data(fe_conditions_list,i);
      libbalsa_filter_prepend_condition(fil, libbalsa_condition_clone(cond));
    }

    fil->conditions=g_slist_reverse(fil->conditions);

    if (filter_errno!=FILTER_NOERR) {
	filter_perror(filter_strerror(filter_errno));
	gnome_dialog_close(fe_window);
	return;
    }

    /* Set action fields according to dialog data */

    fil->action=action;

    if (fil->action!=FILTER_TRASH)
	fil->action_string=g_strdup(action_str);

    if (GTK_TOGGLE_BUTTON(fe_popup_button)->active) {
	static gchar defstring[19] = N_("Filter has matched");
	gchar *tmpstr;
	
	FILTER_SETFLAG(fil, FILTER_POPUP);
	tmpstr = gtk_entry_get_text(GTK_ENTRY(fe_popup_entry));
	
	fil->popup_text=g_strdup(((!tmpstr)
				  || (tmpstr[0] ==
				      '\0')) ? _(defstring) : tmpstr);
    }
    else {
	g_free(fil->popup_text);
	fil->popup_text=NULL;
    }

#ifdef HAVE_LIBESD
    if (GTK_TOGGLE_BUTTON(fe_sound_button)->active) {
	gchar *tmpstr;
	
	FILTER_SETFLAG(fil, FILTER_SOUND);
	tmpstr = gtk_entry_get_text(GTK_ENTRY(fe_sound_entry));
	if ((!tmpstr) || (tmpstr[0] == '\0')) {
	    libbalsa_filter_free(fil, GINT_TO_POINTER(TRUE));
	    /* FIXME error_dialog("You must provide a sound to play") */
	    return;
	}
	fil->popup_sound(tmpstr);
    }
    else {
	g_free(fil->sound);
	fil->sound=NULL;
    }
#endif
    /* New filter is OK, we replace the old one */
    old=(LibBalsaFilter*)gtk_clist_get_row_data(fe_filters_list,row);
    change_filter_name(old->name, fil->name);
    libbalsa_filter_free(old,GINT_TO_POINTER(TRUE));
    gtk_clist_remove(fe_filters_list, row);
    row=gtk_clist_append(fe_filters_list,&(fil->name));
    gtk_clist_set_row_data(fe_filters_list,row,(gpointer)fil);
    gtk_clist_select_row(fe_filters_list,row,-1);
}			/* end fe_apply_pressed */


/*
 * fe_revert_pressed()
 *
 * Reverts the filter values to the ones stored.
 * It really just select()s the row, letting the callback handle
 * things
 */
void
fe_revert_pressed(GtkWidget * widget, gpointer data)
{
    gint row;

    if (!fe_filters_list->selection) return;
    
    row=GPOINTER_TO_INT(fe_filters_list->selection->data);
    gtk_clist_select_row(fe_filters_list,row,-1);
}			/* end fe_revert_pressed() */

/*
 * Callback function handling the selection of a row in the filter list
 * so that we can refresh the notebook page
 */

void fe_clist_select_row(GtkWidget * widget, gint row, gint column, 
			 GdkEventButton *event, gpointer data)
{
    LibBalsaFilter* fil;
    LibBalsaCondition* cnd;
    GSList *list;
    gint new_row;

    fil=(LibBalsaFilter*)gtk_clist_get_row_data(fe_filters_list,row);
    
    /* FIXME : Should be impossible */
    g_assert(fil!=NULL);
    
    /* Populate all fields with filter data */
    gtk_entry_set_text(GTK_ENTRY(fe_name_entry),fil->name);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_popup_button),
				 FILTER_CHKFLAG(fil,FILTER_POPUP));
    gtk_entry_set_text(GTK_ENTRY(fe_popup_entry),
		       FILTER_CHKFLAG(fil,FILTER_POPUP) ? fil->popup_text : "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_sound_button),
				 FILTER_CHKFLAG(fil,FILTER_SOUND));
    /*    gtk_entry_set_text(GTK_ENTRY(fe_sound_entry),
	  FILTER_CHKFLAG(fil,FILTER_SOUND) ? fil->sound : "");*/

    gtk_option_menu_set_history(GTK_OPTION_MENU(fe_action_option_menu), fil->action-1);
    gtk_option_menu_set_history(GTK_OPTION_MENU(fe_op_codes_option_menu), fil->conditions_op-1);

    if (fil->action!=FILTER_TRASH)
	gtk_entry_set_text(GTK_ENTRY(fe_action_entry),fil->action_string==NULL ? "" : fil->action_string);

    /* We free the conditions */
    fe_free_associated_conditions();

    /* Clear the conditions list */
    gtk_clist_clear(fe_conditions_list);

    /* Populate the conditions list */
    filter_errno=FILTER_NOERR;
    for (list=fil->conditions;
         list && filter_errno==FILTER_NOERR;list=g_slist_next(list)) {
	cnd=(LibBalsaCondition*) list->data;
	new_row=gtk_clist_append(fe_conditions_list,
                                 &(fe_search_type[cnd->type-1].text));
	gtk_clist_set_row_data(fe_conditions_list,new_row,
                               libbalsa_condition_clone(cnd));
    }

    if (filter_errno!=FILTER_NOERR)
	gnome_dialog_close(fe_window);

    if (fe_conditions_list->rows)
	gtk_clist_select_row(fe_conditions_list,0,-1);
}                      /* end fe_clist_select_row */
