/* -*-mode:c; c-style:k&r; c-basic-offset:4; indent-tab-mode: nil; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#define _XOPEN_SOURCE /* glibc2 needs this */
/* yellow dog has crappy libc and needs pthread.h to be included here */
#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#endif
#include <time.h>
#include <gnome.h>

#include <string.h>
#include "filter.h"
#include "filter-funcs.h"
#include "filter-edit.h"
#include "filter-file.h"
#include "balsa-app.h"
#include "save-restore.h"
#include "mailbox-filter.h"

/* Defined in filter-edit-dialog.c*/
extern option_list fe_search_type[4];
extern GList * fe_user_headers_list;

static void fe_add_pressed(GtkWidget * widget, gpointer throwaway);
static void fe_remove_pressed(GtkWidget * widget, gpointer throwaway);
static void fe_regexs_selection_changed(GtkTreeSelection * selection,
                                        gpointer user_data);
static void fe_free_associated_filters(void);
static void fe_free_associated_conditions(void);
static GtkWidget *fe_date_sample(void);

/* The dialog widget (we need it to be able to close dialog on error) */

extern GtkWidget * fe_window;
    
extern GtkTreeView *fe_filters_list;
    
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
/* Combo list for user headers and check button*/
#if GTK_CHECK_VERSION(2, 4, 0)
GtkWidget * fe_user_header;
#else
GtkCombo * fe_user_header;
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
GtkWidget * fe_matching_fields_us_head;

/* widget for the conditions */
extern GtkTreeView *fe_conditions_list;
    
/* widgets for the type notebook simple page */
GtkWidget *fe_type_simple_label;
GtkWidget *fe_type_simple_entry;

/* widgets for the type notebook date page */
GtkWidget *fe_type_date_label;
GtkWidget *fe_type_date_low_entry,*fe_type_date_high_entry;

/* widgets for the type notebook regex page */
GtkTreeView *fe_type_regex_list;
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

/* Mailboxes option menu */
extern GtkWidget * fe_mailboxes;

/* Different buttons that need to be greyed or ungreyed */
extern GtkWidget * fe_new_button;
extern GtkWidget * fe_delete_button;
extern GtkWidget * fe_apply_button;
extern GtkWidget * fe_revert_button;
extern GtkWidget * fe_condition_delete_button;
extern GtkWidget * fe_condition_edit_button;
GtkWidget * fe_regex_remove_button;

/* condition_has_changed allows us to be smart enough not to make the
 * whole process of building a new condition when editing condition
 * has not leaded to any modification */

static gboolean condition_has_changed;

/* Keep track of negation state */

static gboolean condition_not;


static gboolean is_new_condition;

/*
 * Struct used to keep track of filter name changes (we must update
 * config files accordingly)
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

/* Free filters associated with list row */
static void
fe_free_associated_filters(void)
{
    gboolean valid;
    GtkTreeModel *model =
        gtk_tree_view_get_model(fe_filters_list);
    GtkTreeIter iter;

    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaFilter *fil;

        gtk_tree_model_get(model, &iter, 1, &fil, -1);
        libbalsa_filter_free(fil, GINT_TO_POINTER(TRUE));
    }
}

static void
fe_free_associated_conditions(void)
{
    gboolean valid;
    GtkTreeModel *model = 
        gtk_tree_view_get_model(fe_conditions_list);
    GtkTreeIter iter;

    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaCondition *cond;

        gtk_tree_model_get(model, &iter, 1, &cond, -1);
	libbalsa_condition_free(cond);
    }
}

/*
 * unique_filter_name()
 *
 * Checks the name of a filter being added to see if it is unique, apart
 * from any selected filter.
 *
 * Arguments:
 *    gchar *name - the preferred choice for a name
 * Returns:
 *    FALSE if it exists another filter of name "name", else returns
 *    TRUE
 */

static gboolean
unique_filter_name(const gchar * name)
{
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gchar *row_text;
    gboolean valid;

    if (!name || name[0] == '\0')
        return FALSE;

    model = gtk_tree_view_get_model(fe_filters_list);
    selection = gtk_tree_view_get_selection(fe_filters_list);
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
	gboolean matches;

        gtk_tree_model_get(model, &iter, 0, &row_text, -1);
	matches = strcmp(name, row_text) == 0;
	g_free(row_text);
        if (matches
            && !gtk_tree_selection_iter_is_selected(selection, &iter))
            return FALSE;
    }

    return TRUE;
}                               /* end unique_filter_name() */

/**************** Conditions *************************/

/* Callback function to fill the regex entry with the selected regex */

static void
fe_regexs_selection_changed(GtkTreeSelection *selection,
                            gpointer user_data)
{
    gboolean selected;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *str;

    selected = gtk_tree_selection_get_selected(selection, &model, &iter);
    if (selected) {
        gtk_tree_model_get(model, &iter, 0, &str, -1);
        gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry),str);
        g_free(str);
    }
    else gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry),"");
    gtk_widget_set_sensitive(fe_regex_remove_button, selected);
}

#if GTK_CHECK_VERSION(2, 4, 0)
/* Helper. */
static gint
fe_combo_box_get_value(GtkWidget * combo_box)
{
    struct fe_combo_box_info *info =
        g_object_get_data(G_OBJECT(combo_box), BALSA_FE_COMBO_BOX_INFO);
    /* Retrieve the selected item in the menu */
    gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box));

    return GPOINTER_TO_INT(g_slist_nth_data(info->values, active));
}

#endif /* GTK_CHECK_VERSION(2, 4, 0) */
/*
 * fe_typesmenu_cb()
 *
 * Handles toggling of the type checkbuttons.
 * When they are toggled, the notebook page must change
 */
static void
fe_typesmenu_cb(GtkWidget* widget, gpointer data)
{
#if GTK_CHECK_VERSION(2, 4, 0)
    ConditionMatchType type =
	(ConditionMatchType) fe_combo_box_get_value(widget);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    ConditionMatchType type=GPOINTER_TO_INT(data);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

    condition_has_changed=TRUE;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(fe_type_notebook),
                                  type - 1);
    /* For certain types (date, flag)
     * match fields have no meaning so we disable them
     */
    gtk_widget_set_sensitive(fe_match_frame,
                             (type!=CONDITION_DATE && type!=CONDITION_FLAG));
}                       /* end fe_typesmenu_cb() */

typedef struct {
    const gchar * normal_str, *negate_str;
} LabelDescs;
static const LabelDescs simple_label = 
{ N_("One of the specified fields contains:"),
  N_("None of the specified fields contains:") };
static const LabelDescs regex_label =
{ N_("One of the regular expressions matches"),
  N_("None of the regular expressions matches") };
static const LabelDescs date_label = 
{ N_("Match when date is in the interval:"),
  N_("Match when date is outside the interval:") };
static const LabelDescs flags_label = 
{ N_("Match when one of these flags is set:"),
  N_("Match when none of these flags is set:") };

static void 
fe_update_label(GtkWidget* label, const LabelDescs* labels)
{    
    gtk_label_set_text(GTK_LABEL(label),
                       condition_not ? 
                       _(labels->negate_str) : _(labels->normal_str));
}                      /* end fe_update_label */

static void
update_condition_list_label(void)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_conditions_list);
    GtkTreeModel *model;
    GtkTreeIter iter;
    LibBalsaCondition *cond;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 1, &cond, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                       0, _(fe_search_type[cond->type - 1].text), -1);
}                      /* end fe_update_condition_list_label */

static ConditionMatchType
get_condition_type(void)
{
#if GTK_CHECK_VERSION(2, 4, 0)
    /* Set the type associated with the selected item */
    return (ConditionMatchType)
        fe_combo_box_get_value(fe_search_option_menu);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    GtkWidget * menu;

    /* Retrieve the selected item in the search type menu */
    menu=gtk_menu_get_active(GTK_MENU(gtk_option_menu_get_menu(GTK_OPTION_MENU(fe_search_option_menu))));
    /* Set the type associated with the selected item */
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu),"value"));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
}

/* fe_negate_condition :handle pressing on the "Contain/Does not
   Contain"... buttons */
static void
fe_negate_condition(GtkWidget * widget, gpointer data)
{
    condition_not = !condition_not;
    switch (get_condition_type()) {
    case CONDITION_STRING: 
        fe_update_label(fe_type_simple_label, &simple_label); break;
    case CONDITION_REGEX:  
        fe_update_label(fe_type_regex_label,  &regex_label);  break;
    case CONDITION_DATE:   
        fe_update_label(fe_type_date_label,   &date_label);   break;
    case CONDITION_FLAG:   
        fe_update_label(fe_type_flag_label,   &flags_label);
    case CONDITION_NONE:
    case CONDITION_AND:
    case CONDITION_OR:
        /* to avoid warnings */
	break;
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
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_body),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_to),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_from),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_subject),active);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_cc),active);
}                       /* end fe_match_fields_buttons_cb */

static void
fe_match_field_user_header_cb(GtkWidget * widget)
{
#if GTK_CHECK_VERSION(2, 4, 0)
    GtkToggleButton *button =
        GTK_TOGGLE_BUTTON(fe_matching_fields_us_head);
    gtk_widget_set_sensitive(fe_user_header,
                             gtk_toggle_button_get_active(button));
#else
    gtk_widget_set_sensitive(GTK_WIDGET(fe_user_header),
			     gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_matching_fields_us_head)));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    condition_has_changed=TRUE;
}

void
fe_add_new_user_header(const gchar * str)
{
    GList *lst;
#if GTK_CHECK_VERSION(2, 4, 0)
    gint i, active;
#endif                          /* GTK_CHECK_VERSION(2, 4, 0) */

    for (lst = fe_user_headers_list; lst; lst = g_list_next(lst))
        if (g_ascii_strcasecmp(str, (gchar *) lst->data) == 0)
            return;

    /* It's a new string, add it */
#if GTK_CHECK_VERSION(2, 4, 0)
    /* First clear the combo box... */
    for (i = g_list_length(fe_user_headers_list); --i >= 0;)
        gtk_combo_box_remove_text(GTK_COMBO_BOX(fe_user_header), i);

#endif                          /* GTK_CHECK_VERSION(2, 4, 0) */
    fe_user_headers_list =
        g_list_insert_sorted(fe_user_headers_list, g_strdup(str),
                             (GCompareFunc) g_ascii_strcasecmp);
#if GTK_CHECK_VERSION(2, 4, 0)

    /* ...then remake it with the new string... */
    active = -1;
    for (lst = fe_user_headers_list, i = 0; lst; lst = lst->next, i++) {
        gtk_combo_box_append_text(GTK_COMBO_BOX(fe_user_header),
                                  lst->data);
        if (!g_ascii_strcasecmp(str, lst->data))
            active = i;
    }

    /* ...and make the new entry active. */
    gtk_combo_box_set_active(GTK_COMBO_BOX(fe_user_header), active);
#endif                          /* GTK_CHECK_VERSION(2, 4, 0) */
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
    gchar * str,* p;
    const gchar *c_str;
    gint row, col;
    struct tm date;
    GtkTreeModel *model =
        gtk_tree_view_get_model(fe_type_regex_list);
    GtkTreeIter iter;


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
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_matching_fields_us_head))) {
	    CONDITION_SETMATCH(new_cnd,CONDITION_MATCH_US_HEAD);
#if GTK_CHECK_VERSION(2, 4, 0)
            str =
                gtk_editable_get_chars(GTK_EDITABLE
                                       (GTK_BIN(fe_user_header)->child), 0,
                                       -1);
#else
            str = gtk_editable_get_chars(GTK_EDITABLE(fe_user_header->entry),
                                         0, -1);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
	    if (!str[0]) {
                balsa_information(LIBBALSA_INFORMATION_ERROR,
                                  _("You must specify the name of the "
                                    "user header to match on"));
		return FALSE;
	    }
#if GTK_CHECK_VERSION(2, 4, 0)
            fe_add_new_user_header(str);
#else
	    fe_add_new_user_header(str);
	    /* This piece of code replaces the combo list
	       by a new one that contains the new string the user has entered
	       it seems that we must reset the text to the correct string,
	    */
	    gtk_combo_set_popdown_strings(fe_user_header,fe_user_headers_list);
	    gtk_entry_set_text(GTK_ENTRY(fe_user_header->entry),str);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
	    g_free(str);
	}
        else if (new_cnd->match.string.fields==CONDITION_EMPTY) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("You must specify at least one "
                                "field for matching"));
            return FALSE;
        }
    }
    switch (new_cnd->type) {
    case CONDITION_STRING:
        c_str = gtk_entry_get_text(GTK_ENTRY(fe_type_simple_entry));
        if (!c_str || c_str[0]=='\0') {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("You must provide a string"));
            return FALSE;
        }
        break;
    case CONDITION_REGEX:
        if (!gtk_tree_model_get_iter_first(model, &iter)) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("You must provide at least one "
                                "regular expression"));
            return FALSE;
        }
        break;
    case CONDITION_DATE:
        c_str = gtk_entry_get_text(GTK_ENTRY(fe_type_date_low_entry));
        if (c_str && c_str[0]!='\0') {
            (void) strptime("00:00:00","%T",&date);
            p=(gchar *)strptime(c_str,"%x",&date);
            if (!p || *p!='\0') {
                balsa_information(LIBBALSA_INFORMATION_ERROR,
                                  _("Low date is incorrect"));
                return FALSE;
            }
            new_cnd->match.date.date_low=mktime(&date);
        }
        c_str = gtk_entry_get_text(GTK_ENTRY(fe_type_date_high_entry));
        if (c_str && c_str[0]!='\0') {
            (void) strptime("23:59:59","%T",&date);
            p=(gchar *)strptime(c_str,"%x",&date);
            if (!p || *p!='\0') {
                balsa_information(LIBBALSA_INFORMATION_ERROR,
                                  _("High date is incorrect"));
                return FALSE;
            }
            new_cnd->match.date.date_high=mktime(&date);
        }
        if (new_cnd->match.date.date_low >
            new_cnd->match.date.date_high) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Low date is greater than high date"));
            return FALSE;
        }
        break;
    case CONDITION_FLAG:
    case CONDITION_NONE:
    case CONDITION_AND:
    case CONDITION_OR:
        /* to avoid warnings */
	break;
    }

    /* Sanity checks OK, retrieve datas from widgets */

    new_cnd->negate = condition_not;
    if (CONDITION_CHKMATCH(new_cnd,CONDITION_MATCH_US_HEAD))
#if GTK_CHECK_VERSION(2, 4, 0)
        new_cnd->match.string.user_header =
            gtk_editable_get_chars(GTK_EDITABLE
                                   (GTK_BIN(fe_user_header)->child), 0,
                                   -1);
#else
	new_cnd->match.string.user_header =
            g_strdup(gtk_entry_get_text(GTK_ENTRY(fe_user_header->entry)));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    /* Set the type specific fields of the condition */
    switch (new_cnd->type) {
    case CONDITION_STRING:
        new_cnd->match.string.string =
            g_strdup(gtk_entry_get_text(GTK_ENTRY(fe_type_simple_entry)));
        break;

    case CONDITION_REGEX:
#if 0
        FIXME;
        do {
            gchar* str;
            gtk_tree_model_get(model, &iter, 0, &str, -1);
            new_reg = libbalsa_condition_regex_new();
            libbalsa_condition_regex_set(new_reg, str);
            libbalsa_condition_prepend_regex(new_cnd, new_reg);
        } while (gtk_tree_model_iter_next(model, &iter));
#endif
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
    case CONDITION_AND: /*FIXME: verify this! */
    case CONDITION_OR:
        /* To avoid warnings :) */
	break;
    }
    return TRUE;
}                        /* condition_validate*/

static void
clear_condition_widgets()
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(fe_type_regex_list);

    gtk_entry_set_text(GTK_ENTRY(fe_type_simple_entry),"");
    gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry),"");          
    gtk_list_store_clear(GTK_LIST_STORE(model));
    gtk_entry_set_text(GTK_ENTRY(fe_type_date_low_entry),"");
    gtk_entry_set_text(GTK_ENTRY(fe_type_date_high_entry),"");
}

/* set_button_sensitivities:
 *
 * We want the `apply' and `revert' buttons to be sensitive only when
 * some changes have been made to the filter's condition(s). To lessen
 * the chance that the user loses the changes, we desensitize the
 * dialog's `OK' button. The filter `new' button also discards changes
 * to the current filter, so we desensitize that, also. However, the
 * `delete' button is still sensitive, so the user can always simply
 * trash the filter.
 */
static void
set_button_sensitivities(gboolean sensitive)
{
    gtk_widget_set_sensitive(fe_apply_button, sensitive);
    gtk_widget_set_sensitive(fe_revert_button, sensitive);
    gtk_widget_set_sensitive(fe_new_button, !sensitive);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(fe_window),
                                      GTK_RESPONSE_OK, !sensitive);
    gtk_widget_set_sensitive(GTK_WIDGET(fe_filters_list), !sensitive);
}

/* fill_condition_widget : Fill all widget according to condition data
   fields
*/

static void
fill_condition_widgets(LibBalsaCondition* cnd)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(fe_type_regex_list);
    gchar str[20];
    struct tm date;
    gint row,col;
    gboolean andmask;
    static gchar xformat[] = "%x"; /* to suppress error in strftime */
    
    condition_not=cnd->negate;
    /* Clear all widgets */
    if (cnd->type!=CONDITION_STRING)
        gtk_entry_set_text(GTK_ENTRY(fe_type_simple_entry),"");

    if (cnd->type!=CONDITION_REGEX)
        gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry),"");      

    gtk_list_store_clear(GTK_LIST_STORE(model));

    gtk_notebook_set_current_page(GTK_NOTEBOOK(fe_type_notebook),
                                  cnd->type - 1);

#if GTK_CHECK_VERSION(2, 4, 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(fe_search_option_menu),
                             cnd->type - 1);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_option_menu_set_history(GTK_OPTION_MENU(fe_search_option_menu), 
                                cnd->type-1);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

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
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_matching_fields_us_head),
                                 CONDITION_CHKMATCH(cnd,CONDITION_MATCH_US_HEAD) && andmask);
    if (CONDITION_CHKMATCH(cnd,CONDITION_MATCH_US_HEAD) && andmask) {
#if GTK_CHECK_VERSION(2, 4, 0)
	gchar *tmp = cnd->match.string.user_header;
	if (tmp && *tmp)
	    fe_add_new_user_header(tmp);
	gtk_widget_set_sensitive(fe_user_header,TRUE);
#else
	gtk_widget_set_sensitive(GTK_WIDGET(fe_user_header),TRUE);
	gtk_entry_set_text(GTK_ENTRY(fe_user_header->entry),
                           cnd->match.string.user_header 
                           ? cnd->match.string.user_header : "");
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    }
    else {
#if GTK_CHECK_VERSION(2, 4, 0)
	gtk_widget_set_sensitive(fe_user_header,FALSE);
	gtk_entry_set_text(GTK_ENTRY(GTK_BIN(fe_user_header)->child),"");
#else
	gtk_widget_set_sensitive(GTK_WIDGET(fe_user_header),FALSE);
	gtk_entry_set_text(GTK_ENTRY(fe_user_header->entry),"");
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    }	
    /* Next update type specific fields */
    switch (cnd->type) {
    case CONDITION_STRING:
        gtk_entry_set_text(GTK_ENTRY(fe_type_simple_entry),
                           cnd->match.string.string 
                           ? cnd->match.string.string : "");
        fe_update_label(fe_type_simple_label, &simple_label);
        break;
    case CONDITION_REGEX:
#if 0
        FIXME;
        for (regex = cnd->match.regexs; regex; regex = g_slist_next(regex)) {
            gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0,
                               libbalsa_condition_regex_get
                               ((LibBalsaConditionRegex *) regex->data),
                               -1);
        }
        if (cnd->match.regexs) {
            /* initially, select first regex */
            gtk_tree_selection_select_iter(selection, &iter);
        } else
            gtk_widget_set_sensitive(fe_regex_remove_button, FALSE);
        fe_update_label(fe_type_regex_label, &regex_label);
#endif
        break;
    case CONDITION_DATE:
        if (cnd->match.date.date_low==0) str[0]='\0';
        else {
            localtime_r(&cnd->match.date.date_low, &date);
            strftime(str, sizeof(str), xformat, &date);
        }
        gtk_entry_set_text(GTK_ENTRY(fe_type_date_low_entry),str);
        if (cnd->match.date.date_high==0) str[0]='\0';
        else {
            localtime_r(&cnd->match.date.date_high, &date);
            strftime(str,sizeof(str), xformat, &date);
        }
        gtk_entry_set_text(GTK_ENTRY(fe_type_date_high_entry),str);
        fe_update_label(fe_type_date_label, &date_label);
        break;
    case CONDITION_FLAG:
        for (row=0;row<2;row++)
            for (col=0;col<2;col++)
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_type_flag_buttons[row*2+col]),
                                             cnd->match.flags & (1 << (row*2+col+1)));
        fe_update_label(fe_type_flag_label,   &flags_label);
        break;
    case CONDITION_NONE:
    case CONDITION_AND:
    case CONDITION_OR:
        /* To avoid warnings :), we should never get there */
	break;
    }
#if GTK_CHECK_VERSION(2, 4, 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(fe_search_option_menu),
                             cnd->type - 1);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_menu_set_active(GTK_MENU(gtk_option_menu_get_menu(GTK_OPTION_MENU(fe_search_option_menu))),cnd->type-1);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
}            /* end fill_condition_widget */

static void
condition_dialog_response(GtkWidget * dialog, gint response,
                          gpointer throwaway)
{
    LibBalsaCondition *new_cnd;
    GError *err;

    switch (response) {
    case GTK_RESPONSE_OK:       /* OK button */
        if (condition_has_changed) {
            GtkTreeSelection *selection;
            GtkTreeModel *model;
            GtkTreeIter iter;

            new_cnd = libbalsa_condition_new();
            if (!condition_validate(new_cnd)) {
                g_free(new_cnd);
                return;
            }

            /* No error occured, condition is valid, so change/add it
             * based on is_new_condition and only if something has
             * changed of course */
            selection = gtk_tree_view_get_selection(fe_conditions_list);
            model = gtk_tree_view_get_model(fe_conditions_list);

            if (!is_new_condition) {
                /* We free the old condition */
                if (gtk_tree_selection_get_selected(selection, NULL,
                                                    &iter)) {
                    LibBalsaCondition *cond;

                    gtk_tree_model_get(model, &iter, 1, &cond, -1);
                    libbalsa_condition_free(cond);
                }
            } else {
                /* It was a new condition, so add it to the list */
                gtk_list_store_append(GTK_LIST_STORE(model), &iter);
                gtk_tree_selection_select_iter(selection, &iter);
                /* We make the buttons sensitive */
                gtk_widget_set_sensitive(fe_condition_delete_button, TRUE);
                gtk_widget_set_sensitive(fe_condition_edit_button, TRUE);
                gtk_widget_set_sensitive(fe_op_codes_option_menu,
                                         gtk_tree_model_iter_n_children
                                         (model, NULL) > 1);
            }
            /* Associate the new condition to the row in the list */
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, 1, new_cnd,
                               -1);
            /* And refresh the conditions list */
            update_condition_list_label();

            set_button_sensitivities(TRUE);
        }
    case GTK_RESPONSE_CANCEL:   /* Cancel button */
    case GTK_RESPONSE_NONE:     /* Window close */
        /* we only hide it because it is too expensive to destroy and
           rebuild each time */
        gtk_widget_hide(dialog);
        break;
    case GTK_RESPONSE_HELP:     /* Help button */
	err = NULL;
	gnome_help_display("balsa", "win-condition", &err);
	if (err) {
	    balsa_information_parented(GTK_WINDOW(dialog),
		    LIBBALSA_INFORMATION_WARNING,
		    _("Error displaying condition help: %s\n"),
		    err->message);
	    g_error_free(err);
	}
	break;
    }
    gtk_widget_set_sensitive(fe_window, TRUE);
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
    static gchar * flag_names[]=
        {N_("Unread"), N_("Deleted"), N_("Replied"), N_("Flagged")};

    /* The notebook */

    fe_type_notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(fe_type_notebook), FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(fe_type_notebook), FALSE);

    /* The simple page of the notebook */

    page = gtk_table_new(5, 3, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);

    fe_type_simple_label = 
	gtk_label_new_with_mnemonic(_("One of the specified f_ields contains"));
    gtk_table_attach(GTK_TABLE(page),
                     fe_type_simple_label,
                     0, 5, 0, 1,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_type_simple_entry = gtk_entry_new();
    g_signal_connect(G_OBJECT(fe_type_simple_entry), "changed",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(page),
                     fe_type_simple_entry,
                     0, 5, 1, 2,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    gtk_label_set_mnemonic_widget(GTK_LABEL(fe_type_simple_label),
				  fe_type_simple_entry);

    button = gtk_button_new_with_mnemonic(_("Contain/Does _Not Contain"));
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_negate_condition), NULL);
    gtk_table_attach(GTK_TABLE(page),
                     button,
                     0, 5, 2, 3,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    
    /* The regex page of the type notebook */

    page = gtk_table_new(5, 6, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);

    fe_type_regex_label = 
        gtk_label_new_with_mnemonic(_("_One of the regular expressions matches"));
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

    fe_type_regex_list =
        libbalsa_filter_list_new(FALSE, NULL, GTK_SELECTION_BROWSE,
                                 G_CALLBACK(fe_regexs_selection_changed),
                                 FALSE);

    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(fe_type_regex_list));

    box = gtk_hbox_new(TRUE, 5);
    gtk_table_attach(GTK_TABLE(page),
                     box,
                     0, 5, 4, 5,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    button = gtk_button_new_with_mnemonic(_("A_dd"));
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_add_pressed), NULL);
    fe_regex_remove_button = gtk_button_new_with_mnemonic(_("_Remove"));
    gtk_box_pack_start(GTK_BOX(box), fe_regex_remove_button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(fe_regex_remove_button), "clicked",
                     G_CALLBACK(fe_remove_pressed), NULL);
    button = gtk_button_new_with_mnemonic(_("One _Matches/None Matches"));
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_negate_condition), NULL);

    fe_type_regex_entry = gtk_entry_new();
    gtk_table_attach(GTK_TABLE(page),
                     fe_type_regex_entry,
                     0, 5, 5, 6,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);

    /* The date page of the notebook */

    page = gtk_table_new(5, 3, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);

    fe_type_date_label = 
        gtk_label_new(_("Match when date is in the interval:"));
    gtk_table_attach(GTK_TABLE(page),
                     fe_type_date_label,
                     0, 5, 0, 1,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_type_date_low_entry = gtk_entry_new();
    g_signal_connect(G_OBJECT(fe_type_date_low_entry), "changed",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(page),
                     fe_type_date_low_entry,
                     0, 2, 1, 2,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_type_date_high_entry = gtk_entry_new();
    g_signal_connect(G_OBJECT(fe_type_date_high_entry), "changed",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(page),
                     fe_type_date_high_entry,
                     3, 5, 1, 2,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    button = gtk_button_new_with_label(_("Inside/outside the date interval"));
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_negate_condition), NULL);
    gtk_table_attach(GTK_TABLE(page),
                     button,
                     0, 5, 2, 3,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    gtk_table_attach(GTK_TABLE(page),
                     fe_date_sample(),
                     0, 5, 3, 4,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    /* The flag page of the notebook */

    page = gtk_table_new(1, 2, FALSE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook), page, NULL);
    fe_type_flag_label =
        gtk_label_new(_("Match when one of these flags is set:"));
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
            fe_type_flag_buttons[row*2+col] = 
                gtk_check_button_new_with_label(_(flag_names[row*2+col]));
            g_signal_connect(G_OBJECT(fe_type_flag_buttons[row*2+col]),
                             "toggled",
                             G_CALLBACK(fe_condition_changed_cb), NULL);
            gtk_table_attach(GTK_TABLE(page),
                             fe_type_flag_buttons[row*2+col],
                             col, col+1,row,row+1,
                             GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 
                             2, 2);
        }
    button = gtk_button_new_with_label(_("Match when one flag is set/when no flag is set"));
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_negate_condition), NULL);
    gtk_table_attach(GTK_TABLE(page),
                     button,
                     0, 2, 2, 3,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
}                               /* end build_type_notebook() */

static
void build_condition_dialog(GtkWidget * condition_dialog)
{
    GtkWidget * table,* label,* button,* page,* box;
#if GTK_CHECK_VERSION(2, 4, 0)
    GList *list;
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

    page = gtk_table_new(7, 2, FALSE);
    /* builds the toggle buttons to specify fields concerned by the conditions of
     * the filter */
    
    fe_match_frame = gtk_frame_new(_("Match In"));
    gtk_frame_set_label_align(GTK_FRAME(fe_match_frame), 
                              GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_match_frame), GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(page),
                     fe_match_frame,
                     0, 2, 0, 2,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    
    table = gtk_table_new(5, 2, TRUE);
    gtk_container_add(GTK_CONTAINER(fe_match_frame), table);
    
    button = gtk_button_new_with_mnemonic(_("_All"));
    gtk_table_attach(GTK_TABLE(table),
                     button,
                     0, 1, 4, 5,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2); 
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_match_fields_buttons_cb),
                     GINT_TO_POINTER(1));
    button = gtk_button_new_with_mnemonic(_("C_lear"));
    gtk_table_attach(GTK_TABLE(table),
                     button,
                     1, 2, 4, 5,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_match_fields_buttons_cb),
                     GINT_TO_POINTER(3));
    
    fe_matching_fields_body = gtk_check_button_new_with_mnemonic(_("_Body"));
    gtk_table_attach(GTK_TABLE(table),
                     fe_matching_fields_body,
                     0, 1, 0, 1,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    g_signal_connect(G_OBJECT(fe_matching_fields_body), "toggled",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    fe_matching_fields_to = gtk_check_button_new_with_mnemonic(_("_To:"));
    gtk_table_attach(GTK_TABLE(table),
                     fe_matching_fields_to,
                     0, 1, 1, 2,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    g_signal_connect(G_OBJECT(fe_matching_fields_to), "toggled",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    fe_matching_fields_from = gtk_check_button_new_with_mnemonic(_("_From:"));
    gtk_table_attach(GTK_TABLE(table),
                     fe_matching_fields_from,
                     1, 2, 1, 2,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    g_signal_connect(G_OBJECT(fe_matching_fields_from), "toggled",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    fe_matching_fields_subject = 
	gtk_check_button_new_with_mnemonic(_("_Subject"));
    gtk_table_attach(GTK_TABLE(table),
                     fe_matching_fields_subject,
                     0, 1, 2, 3,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    g_signal_connect(G_OBJECT(fe_matching_fields_subject), "toggled",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    fe_matching_fields_cc = gtk_check_button_new_with_mnemonic(_("_Cc:"));
    gtk_table_attach(GTK_TABLE(table),
                     fe_matching_fields_cc,
                     1, 2, 2, 3,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    g_signal_connect(G_OBJECT(fe_matching_fields_cc), "toggled",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    fe_matching_fields_us_head = gtk_check_button_new_with_mnemonic(_("_User header:"));
    gtk_table_attach(GTK_TABLE(table),
                     fe_matching_fields_us_head,
                     0, 1, 3, 4,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    g_signal_connect(G_OBJECT(fe_matching_fields_us_head), "toggled",
                     G_CALLBACK(fe_match_field_user_header_cb), NULL);
#if GTK_CHECK_VERSION(2, 4, 0)
    fe_user_header = gtk_combo_box_entry_new_text();
    for (list = fe_user_headers_list; list; list = list->next)
        gtk_combo_box_append_text(GTK_COMBO_BOX(fe_user_header),
                                  list->data);
    g_signal_connect(G_OBJECT(fe_user_header), "changed",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(table),
                     fe_user_header,
                     1, 2, 3, 4,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
#else
    fe_user_header = GTK_COMBO(gtk_combo_new());
    gtk_combo_set_value_in_list(fe_user_header,FALSE,FALSE);
    gtk_combo_set_case_sensitive(fe_user_header,FALSE);
    gtk_combo_set_popdown_strings(fe_user_header,fe_user_headers_list);
    g_signal_connect(G_OBJECT(fe_user_header->entry), "changed",
                     G_CALLBACK(fe_condition_changed_cb), NULL);
    gtk_table_attach(GTK_TABLE(table),
                     GTK_WIDGET(fe_user_header),
                     1, 2, 3, 4,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    
    box = gtk_hbox_new(FALSE, 5);
    gtk_table_attach(GTK_TABLE(page),
                     box,
                     0, 2, 2, 3,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    label = gtk_label_new_with_mnemonic(_("Search T_ype:"));
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 5);
    fe_search_option_menu = 
        fe_build_option_menu(fe_search_type, ELEMENTS(fe_search_type),
                             G_CALLBACK(fe_typesmenu_cb));
    gtk_box_pack_start(GTK_BOX(box), fe_search_option_menu, FALSE, FALSE, 5);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), fe_search_option_menu);

    build_type_notebook();
    gtk_table_attach(GTK_TABLE(page),
                     fe_type_notebook,
                     0, 2, 3, 7,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, 5, 5);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(condition_dialog)->vbox),page,FALSE,FALSE,2);
}

/*
 * fe_edit_condition is the callback to edit command for conditions
 * this is done popping a dialog
 * is_new_cnd is 1 if user is creating a new condition, else it is 0
 */

void
fe_edit_condition(GtkWidget * throwaway,gpointer is_new_cnd)
{
    static GtkWidget * condition_dialog;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_filters_list);
    gboolean filter_selected =
        gtk_tree_selection_get_selected(selection, &model, &iter);
    gchar * title;
    LibBalsaCondition *cnd = NULL;
    LibBalsaFilter *fil;

    is_new_condition=GPOINTER_TO_INT(is_new_cnd);

    if (!is_new_condition) {
        GtkTreeSelection *cond_selection =
            gtk_tree_view_get_selection(fe_conditions_list);
        GtkTreeModel *cond_model;
        GtkTreeIter cond_iter;

        if (!filter_selected
            || !gtk_tree_selection_get_selected(cond_selection,
                                                &cond_model, &cond_iter))
            return;

        gtk_tree_model_get(cond_model, &cond_iter, 1, &cnd, -1);
        condition_has_changed = FALSE;
    } else
    /* This a new condition, we set condition_has_changed to TRUE to force validation and replacement*/
        condition_has_changed = TRUE;
    /* We construct the dialog box if it wasn't done before */
    if (!condition_dialog) {
        condition_dialog=
            gtk_dialog_new_with_buttons("",
                                        GTK_WINDOW(fe_window),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_OK, GTK_RESPONSE_OK,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                                        NULL);
        g_object_add_weak_pointer(G_OBJECT(condition_dialog),
                                  (gpointer) &condition_dialog);

        g_signal_connect(G_OBJECT(condition_dialog), "response",
                         G_CALLBACK(condition_dialog_response), NULL);
        /* Now we build the dialog*/
        build_condition_dialog(condition_dialog);
    }
    gtk_tree_model_get(model, &iter, 1, &fil, -1);
    title = g_strconcat(_("Edit condition for filter: "), fil->name, NULL);
    gtk_window_set_title(GTK_WINDOW(condition_dialog),title);
    g_free(title);
    /* We fire the dialog box */
    gtk_widget_set_sensitive(fe_window, FALSE);
    gtk_widget_show_all(condition_dialog);
    if (cnd) fill_condition_widgets(cnd);
    else clear_condition_widgets();
}

/* fe_conditions_row_activated : update all widget when a condition
 * is activated
 */
void
fe_conditions_row_activated(GtkTreeView * treeview, GtkTreePath * path,
                            GtkTreeViewColumn * column, gpointer data)

{
    fe_edit_condition(NULL,GINT_TO_POINTER(0));
}

void
fe_condition_remove_pressed(GtkWidget * widget, gpointer data)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_conditions_list);
    GtkTreeModel *model;
    GtkTreeIter iter;
    LibBalsaCondition *cond;
    GtkTreePath *path;
    
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 1, &cond, -1);
    libbalsa_condition_free(cond);
    path = gtk_tree_model_get_path(model, &iter);
    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);

    /* select the next condition if there is one, and the previous if
     * there isn't... */
    if (gtk_tree_model_get_iter(model, &iter, path)
        || gtk_tree_path_prev(path)) {
        gtk_tree_selection_select_path(selection, path);
        if (gtk_tree_model_iter_n_children(model, NULL) == 1)
            gtk_widget_set_sensitive(fe_op_codes_option_menu, FALSE);
    } else {
        /* ...or reset the buttons if the list is empty. */
        gtk_widget_set_sensitive(fe_condition_delete_button,FALSE);
        gtk_widget_set_sensitive(fe_condition_edit_button,FALSE);
    }
    gtk_tree_path_free(path);
    set_button_sensitivities(TRUE);
}

/**************** Filters ****************************/

/*
 * Function that is called when traversing the tree of mailboxes
 * to replace filters name that have changed in the filters list of mailboxes
 * and to invalidate previously constructed list of filters, to force reloading
 */
static gboolean
update_filters_mailbox(GtkTreeModel * model, GtkTreePath * path,
		       GtkTreeIter * iter, gpointer data)
{
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;
    gchar *tmp;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    mailbox = mbnode->mailbox;
    g_object_unref(mbnode);
    if (!mailbox)
	return FALSE;

    /* First we free the filters list (which is now obsolete) */
    g_slist_foreach(mailbox->filters, (GFunc) g_free, NULL);
    g_slist_free(mailbox->filters);
    mailbox->filters = NULL;
    /* Second we replace old filters name by the new ones
     * Note : deleted filters are also removed */
    if (!filters_names_changes)
	return FALSE;
    tmp = mailbox_filters_section_lookup(mailbox->url ?
					 mailbox->url : mailbox->name);
    if (tmp) {
	gchar **filters_names = NULL;
	gboolean def;
	guint nb_filters;

	gnome_config_push_prefix(tmp);
	gnome_config_get_vector_with_default(MAILBOX_FILTERS_KEY,
					     &nb_filters, &filters_names,
					     &def);
	if (!def) {
	    guint i;
	    GList *lst;

	    for (i = 0; i < nb_filters;) {
		for (lst = filters_names_changes;
		     lst &&
		     strcmp(((filters_names_rec *) lst->data)->old_name,
			    filters_names[i]) != 0;
		     lst = g_list_next(lst));

		if (lst) {
		    g_free(filters_names[i]);
		    if (((filters_names_rec *) lst->data)->new_name) {
			/* Name changing */
			filters_names[i++] =
			    g_strdup(((filters_names_rec *) lst->data)->
				     new_name);
		    } else {
			/* Name removing */
			guint j;

			for (j = i; j < nb_filters - 1; j++)
			    filters_names[j] = filters_names[j + 1];
			/* We put NULL to be sure that
			 * g_strfreev does not free already
			 * freed memory. */
			filters_names[--nb_filters] = NULL;
		    }
		} else
		    i++;
	    }
	}

	if (nb_filters) {
	    gnome_config_set_vector(MAILBOX_FILTERS_KEY, nb_filters,
				    (const gchar **) filters_names);
	    gnome_config_pop_prefix();
	} else {
	    gnome_config_pop_prefix();
	    gnome_config_clean_section(tmp);
	}
	g_strfreev(filters_names);
	g_free(tmp);
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

    /* On OK button press we have set "balsa-ok-button"
     * thus we don't free filters in this case */
    if (g_object_get_data(G_OBJECT(fe_filters_list), "balsa-ok-button"))
        g_object_set_data(G_OBJECT(fe_filters_list), "balsa-ok-button",
                          NULL);
    else
        fe_free_associated_filters();

    for (lst=filters_names_changes;lst;lst=g_list_next(lst)) {
        g_free(((filters_names_rec *)lst->data)->old_name);
        g_free(((filters_names_rec *)lst->data)->new_name);
        g_free((filters_names_rec *)lst->data);
    }

    g_list_free(filters_names_changes);
    filters_names_changes=NULL;

    for (lst=new_filters_names;lst;lst=g_list_next(lst)) {
        g_free((gchar *)lst->data);
    }

    g_list_free(new_filters_names);
    new_filters_names=NULL;

    /* free all strings in fe_user_headers_list */
    g_list_foreach(fe_user_headers_list,(GFunc)g_free,NULL);
    g_list_free(fe_user_headers_list);
    fe_user_headers_list = NULL;

    fe_already_open=FALSE;
}

/*
 * fe_dialog_response()
 *
 * Handles the clicking of the main buttons at the 
 * bottom of the dialog.  wooo.
 */
void
fe_dialog_response(GtkWidget * dialog, gint response, gpointer data)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(fe_filters_list);
    GtkTreeIter iter;
    gboolean valid;
    GError *err;
    
    switch (response) {
    case GTK_RESPONSE_OK:       /* OK button */
        /* We clear the old filters */
        libbalsa_filter_clear_filters(balsa_app.filters,TRUE);
        balsa_app.filters=NULL;
        
        /* We put the modified filters */
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
            LibBalsaFilter *fil;

            gtk_tree_model_get(model, &iter, 1, &fil, -1);
            balsa_app.filters = g_slist_prepend(balsa_app.filters, fil);
        }

        /* Tell the clean-up functions not to free
           filters on OK button press */
        g_object_set_data(G_OBJECT(fe_filters_list), "balsa-ok-button",
                          GINT_TO_POINTER(TRUE));

        /* Update mailboxes filters */
	gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			       (GtkTreeModelForeachFunc)
			       update_filters_mailbox, NULL);

        config_filters_save();

    case GTK_RESPONSE_CANCEL:   /* Cancel button */
    case GTK_RESPONSE_NONE:     /* Window close */
        gtk_widget_destroy(dialog);
        break;

    case GTK_RESPONSE_HELP:     /* Help button */
	err = NULL;
	gnome_help_display("balsa", "win-filters", &err);
	if (err) {
	    balsa_information_parented(GTK_WINDOW(dialog),
		    LIBBALSA_INFORMATION_WARNING,
		    _("Error displaying filter help: %s\n"),
		    err->message);
	    g_error_free(err);
	}
	break;

    default:
        /* we should NEVER get here */
        break;
    }
}                       /* end fe_dialog_response */

/*
 * fe_action_selected()
 *
 * Callback for the "Action" option menu
 */ 
void
fe_action_selected(GtkWidget * widget, gpointer data)
{
#if GTK_CHECK_VERSION(2, 4, 0)
    FilterActionType type =
        (FilterActionType) fe_combo_box_get_value(widget);
    gtk_widget_set_sensitive(GTK_WIDGET(fe_mailboxes),
                             type != FILTER_TRASH);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_widget_set_sensitive(GTK_WIDGET(fe_mailboxes),
                             GPOINTER_TO_INT(data)!=FILTER_TRASH);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    set_button_sensitivities(TRUE);
}                       /* end fe_action_selected() */

/*
 * fe_button_toggled()
 *
 * Callback for the "toggled" signal of the action buttons.
 */
void
fe_button_toggled(GtkWidget * widget, gpointer data)
{
    GtkToggleButton *button = GTK_TOGGLE_BUTTON(widget);
    gboolean active = gtk_toggle_button_get_active(button);

    if (GTK_IS_CONTAINER(data)) {
        GList *list;
        for (list = gtk_container_get_children(GTK_CONTAINER(data));
             list; list = g_list_next(list)) 
            gtk_widget_set_sensitive(GTK_WIDGET(list->data), active);
    } else
        gtk_widget_set_sensitive(GTK_WIDGET(data), active);
    set_button_sensitivities(TRUE);
}

/*
 * fe_action_changed() 
 *
 * Callback for various signals of the other action items.
 */
void
fe_action_changed(GtkWidget * widget, gpointer data)
{
    set_button_sensitivities(TRUE);
}

/*
 * fe_add_pressed()
 *
 * Callback for the "Add" button for the regex type
 */
static void
fe_add_pressed(GtkWidget * widget, gpointer throwaway)
{
    const gchar *text;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeSelection *selection;

    text = gtk_entry_get_text(GTK_ENTRY(fe_type_regex_entry));
    
    if (!text || text[0] == '\0')
        return;
    
    model = gtk_tree_view_get_model(fe_type_regex_list);
    selection = gtk_tree_view_get_selection(fe_type_regex_list);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, text, -1);
    gtk_tree_selection_select_iter(selection, &iter);
    condition_has_changed=TRUE;
}                       /* end fe_add_pressed() */

/*
 * fe_remove_pressed()
 * 
 * Callback for the "remove" button of the regex type
 */
static void
fe_remove_pressed(GtkWidget * widget, gpointer throwaway)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_type_regex_list);
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    
    path = gtk_tree_model_get_path(model, &iter);
    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
    /* select the next regex if there is one, or the previous regex if
     * there isn't. */
    if (gtk_tree_model_get_iter(model, &iter, path)
        || gtk_tree_path_prev(path))
        gtk_tree_selection_select_path(selection, path);
    gtk_tree_path_free(path);
    condition_has_changed=TRUE;
}                       /* end fe_remove_pressed() */

/************************************************************/
/******** Functions handling filters ************************/
/************************************************************/

/*
 * Add a filter name change in the list
 * if new_name==NULL, it's a deletion
 */
static void
change_filter_name(gchar * old_name,gchar * new_name)
{
    if (!new_name || strcmp(old_name,new_name)!=0) {
        GList * lst;
        filters_names_rec * p=NULL;

        /* First we check if the filter that changes has been created
         * in this session (looking new_filters_names list) if yes we
         * update new_filters_names, and that's all because we have no
         * reference to it in any mailbox, because it's new
         */

        for (lst=new_filters_names;
             lst && strcmp(old_name,(gchar*)lst->data)!=0;
             lst=g_list_next(lst));
        if (lst) {          
            /* Found it ! Update new_filters_names */
            g_free(lst->data);
            if (new_name)
                lst->data=g_strdup(new_name);
            else
                new_filters_names=g_list_remove(new_filters_names,lst->data);
            return;
        }
	
	/* Now we check if there already exists a change : any name -> old_name
          if yes we must change it to : any name -> new_name
          else we create a new record
	*/
	for (lst=filters_names_changes;lst;lst=g_list_next(lst))
            if (((filters_names_rec *)lst->data)->new_name && 
                strcmp(((filters_names_rec *)lst->data)->new_name,old_name)==0) {
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
        /* Record exists yet, test if we can collapse it (in case his
         * old_name==new_name) It's only a small optimization
         */
        else if (new_name && strcmp(p->old_name,new_name)==0) {
	    g_free(p->old_name);
	    g_free(p);
            filters_names_changes=
                g_list_remove(filters_names_changes,lst->data);
            return;
        }

        if (new_name)
            p->new_name=g_strdup(new_name);
        else p->new_name=NULL;
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
    const static char FLT_NAME_TEMPLATE[] = N_("New filter");
    gint filter_number;
    LibBalsaFilter* fil;
    guint len = strlen(_(FLT_NAME_TEMPLATE))+4;
    gchar *new_item = g_new(gchar,len);
    GtkTreeModel *model =
        gtk_tree_view_get_model(fe_filters_list);
    GtkTreeIter iter;
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_filters_list);

    /* Put a number behind 'New filter' */
    gtk_tree_selection_unselect_all(selection);
    for(filter_number=0; filter_number<1000; filter_number++){
        if(filter_number == 0)
            strcpy(new_item, _(FLT_NAME_TEMPLATE));
        else
            g_snprintf(new_item, 
                       len, "%s%d",
                       _(FLT_NAME_TEMPLATE), filter_number);
        if (unique_filter_name(new_item)) break;
    }

    fil = libbalsa_filter_new();

    if (filter_errno!=FILTER_NOERR) {
        filter_perror(filter_strerror(filter_errno));
        return;
    }

    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, new_item,
                       1, fil, -1);

    /* Fill the filter with default values */

    fil->name=g_strdup(new_item);

    FILTER_SETFLAG(fil,FILTER_COMPILED);
    fil->action=FILTER_MOVE;

    /* Selecting the row will also display the new filter */
    gtk_tree_selection_select_iter(selection, &iter);

    /* Adds "New Filter" to the list of actual new filters names */
    new_filters_names=g_list_prepend(new_filters_names,g_strdup(new_item));
    gtk_widget_grab_focus(fe_name_label);
}                       /* end fe_new_pressed() */

/*
 * fe_delete_pressed()
 *
 * Callback for the "Delete" button
 */
void
fe_delete_pressed(GtkWidget * widget, gpointer data)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_filters_list);
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;
    LibBalsaFilter *fil;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    
    gtk_tree_model_get(model, &iter, 1, &fil, -1);
    
    g_assert(fil);
    change_filter_name(fil->name,NULL);
    libbalsa_filter_free(fil, NULL);
    
    path = gtk_tree_model_get_path(model, &iter);
    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
    /* select the next filter if there is one, or the previous filter if
     * there isn't, or... */
    if (gtk_tree_model_get_iter(model, &iter, path)
        || gtk_tree_path_prev(path))
        gtk_tree_selection_select_path(selection, path);
    else {
        /* ...the store is empty: */
        /* We make the filters delete,revert,apply buttons unsensitive */
        gtk_widget_set_sensitive(fe_delete_button,FALSE);
        set_button_sensitivities(FALSE);
        /* We clear all widgets */
        gtk_entry_set_text(GTK_ENTRY(fe_name_entry),"");
        gtk_entry_set_text(GTK_ENTRY(fe_popup_entry),"");
        /*gtk_option_menu_set_history(GTK_OPTION_MENU(fe_mailboxes), 0); */
        gtk_entry_set_text(GTK_ENTRY(gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(fe_sound_entry))),"");
        gtk_list_store_clear(GTK_LIST_STORE
                             (gtk_tree_view_get_model
                              (fe_conditions_list)));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_sound_button),FALSE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_popup_button),FALSE);
    }
    gtk_tree_path_free(path);
}                       /* end fe_delete_pressed() */

/*
 * fe_apply_pressed()
 *
 * Builds a new filter from the data provided, check for correctness (regex compil...)
 * and sticks it where the selection is in the list
 * we can have a filter with unvalid conditions
 */
void
fe_apply_pressed(GtkWidget * widget, gpointer data)
{
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_filters_list);
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeModel *cond_model =
        gtk_tree_view_get_model(fe_conditions_list);
    GtkTreeIter cond_iter;
    LibBalsaFilter *fil,*old;
    const gchar *temp;
#if !GTK_CHECK_VERSION(2, 4, 0)
    GtkWidget * menu;
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    FilterActionType action;
    ConditionMatchType condition_op;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    /* quick check before we malloc */
    temp = gtk_entry_get_text(GTK_ENTRY(fe_name_entry));
    if (!temp || temp[0] == '\0') {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("No filter name specified."));
        return;
    } 
    if(!unique_filter_name(temp)) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Filter \"%s\" already exists."), temp);
        return;
    }
    
#if GTK_CHECK_VERSION(2, 4, 0)
    /* Set the type associated with the selected item */
    action =
        (FilterActionType) fe_combo_box_get_value(fe_action_option_menu);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    /* Retrieve the selected item in the action menu */
    menu = gtk_menu_get_active(GTK_MENU
                               (gtk_option_menu_get_menu
                                (GTK_OPTION_MENU(fe_action_option_menu))));
    /* Set the type associated with the selected item */
    action=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu),"value"));
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    
    if (!gtk_tree_model_get_iter_first(cond_model, &cond_iter)) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Filter must have conditions."));
        return;
    }
    /* Construct the new filter according with the data fields */

    fil = libbalsa_filter_new();
    if (filter_errno!=FILTER_NOERR) {
        filter_perror(filter_strerror(filter_errno));
        gtk_widget_destroy(fe_window);
        return;
    }

   /* Set name of the filter */

    fil->name=g_strdup(temp);

    /* Retrieve the selected item in the op codes menu */

#if GTK_CHECK_VERSION(2, 4, 0)
    /* Set the op-codes associated with the selected item */
    condition_op =
        (FilterOpType) fe_combo_box_get_value(fe_op_codes_option_menu) ==
        FILTER_OP_OR ? CONDITION_OR : CONDITION_AND;
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    menu = gtk_menu_get_active(GTK_MENU
                               (gtk_option_menu_get_menu
                                (GTK_OPTION_MENU
                                 (fe_op_codes_option_menu))));

    /* Set the op-codes associated with the selected item */
    condition_op =
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu), "value")) ==
	FILTER_OP_OR ? CONDITION_OR : CONDITION_AND;
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

    /* Retrieve all conditions for that filter */

    FILTER_SETFLAG(fil,FILTER_VALID);

    /* Here I set back FILTER_COMPILED, that way, modified filters
     * with no Regex condition won't have to recalculate regex (that
     * they don't have actually :) but modified filters with regex
     * condition will have their compiled flag unset by
     * filter_append_condition, So that's OK
     */
    FILTER_SETFLAG(fil,FILTER_COMPILED);
    gtk_tree_model_get_iter_first(cond_model, &cond_iter);
    do {
        LibBalsaCondition *cond;

        gtk_tree_model_get(cond_model, &cond_iter, 1, &cond, -1);
        libbalsa_filter_prepend_condition(fil,
                                          libbalsa_condition_clone(cond),
                                          condition_op);
    } while (gtk_tree_model_iter_next(cond_model, &cond_iter));

    if (filter_errno!=FILTER_NOERR) {
        filter_perror(filter_strerror(filter_errno));
        gtk_widget_destroy(fe_window);
        return;
    }

    /* Set action fields according to dialog data */

    fil->action=action;

    if (fil->action!=FILTER_TRASH)
        fil->action_string=g_strdup(balsa_mblist_mru_option_menu_get(fe_mailboxes));
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_popup_button))) {
        static gchar defstring[] = N_("Filter has matched");
        const gchar *tmpstr;
        
        tmpstr = gtk_entry_get_text(GTK_ENTRY(fe_popup_entry));
        
        fil->popup_text=g_strdup(((!tmpstr)
                                  || (tmpstr[0] ==
                                      '\0')) ? _(defstring) : tmpstr);
    }

#ifdef HAVE_LIBESD
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fe_sound_button))) {
        gchar *tmpstr;
        
        tmpstr = gtk_entry_get_text(GTK_ENTRY(fe_sound_entry));
        if ((!tmpstr) || (tmpstr[0] == '\0')) {
            libbalsa_filter_free(fil, GINT_TO_POINTER(TRUE));
	    balsa_information(LIBBALSA_INFORMATION_ERROR,
			      _("You must provide a sound to play"));
            return;
        }
        fil->sound=g_strdup(tmpstr);
    }
#endif
    /* New filter is OK, we replace the old one */
    gtk_tree_model_get(model, &iter, 1, &old, -1);
    change_filter_name(old->name, fil->name);
    libbalsa_filter_free(old,GINT_TO_POINTER(TRUE));
    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                       0, fil->name, 1, fil, -1);
    gtk_tree_selection_select_iter(selection, &iter);
}                       /* end fe_apply_pressed */


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
    GtkTreeSelection *selection =
        gtk_tree_view_get_selection(fe_filters_list);
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, NULL, &iter))
        return;

    gtk_tree_selection_unselect_all(selection);
    gtk_tree_selection_select_iter(selection, &iter);
}                       /* end fe_revert_pressed() */

/*
 * Callback function handling the selection of a row in the filter list
 * so that we can refresh the notebook page
 */
static void
fill_condition_list(GtkTreeModel *model, LibBalsaCondition *condition,
		    ConditionMatchType type)
{
    GtkTreeIter iter;
    if (!condition)
	return;
    if (condition->type == CONDITION_OR
	|| condition->type == CONDITION_AND)  {
	/* A nested boolean operator must be the same as the top level
	 * operator. */
	if (condition->type != type)
	    /* We'll silently ignore a mismatch. */
	    return;
	fill_condition_list(model, condition->match.andor.left, type);
	fill_condition_list(model, condition->match.andor.right, type);
	return;
    }

    gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                       0, _(fe_search_type[condition->type-1].text),
                       1, libbalsa_condition_clone(condition),
                       -1);
}

void
fe_filters_list_selection_changed(GtkTreeSelection * selection,
                                  gpointer data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    LibBalsaFilter* fil;
    int pos;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        if (gtk_tree_model_iter_n_children(model, NULL) == 0)
            fe_enable_right_page(FALSE);
        return;
    }

    gtk_tree_model_get(model, &iter, 1, &fil, -1);
    
    /* Populate all fields with filter data */
    gtk_entry_set_text(GTK_ENTRY(fe_name_entry),fil->name);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_popup_button),
                                 fil->popup_text!=NULL);
    gtk_entry_set_text(GTK_ENTRY(fe_popup_entry),
                       fil->popup_text!=NULL
                       ? fil->popup_text : "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_sound_button),
                                 fil->sound!=NULL);
    gtk_entry_set_text(GTK_ENTRY(gnome_file_entry_gtk_entry(GNOME_FILE_ENTRY(fe_sound_entry))),
                       fil->sound!=NULL ? fil->sound : "");
    
#if GTK_CHECK_VERSION(2, 4, 0)
    gtk_combo_box_set_active(GTK_COMBO_BOX(fe_action_option_menu),
                             fil->action - 1);
    pos = (fil->condition
           && fil->condition->type == CONDITION_AND) ? 1 : 0;
    gtk_combo_box_set_active(GTK_COMBO_BOX(fe_op_codes_option_menu), pos);
#else /* GTK_CHECK_VERSION(2, 4, 0) */
    gtk_option_menu_set_history(GTK_OPTION_MENU(fe_action_option_menu), 
                                fil->action-1);
    pos = (fil->condition && fil->condition->type == CONDITION_AND)
        ? 1 : 0;
    gtk_option_menu_set_history(GTK_OPTION_MENU(fe_op_codes_option_menu), 
                                pos);
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
    if (fil->action!=FILTER_TRASH && fil->action_string)
        balsa_mblist_mru_option_menu_set(fe_mailboxes,
                                         fil->action_string);
    /* We free the conditions */
    fe_free_associated_conditions();

    /* Clear the conditions list */
    model = gtk_tree_view_get_model(fe_conditions_list);
    gtk_list_store_clear(GTK_LIST_STORE(model));

    /* Populate the conditions list */
    filter_errno=FILTER_NOERR;
    fill_condition_list(model, fil->condition,
			fil->condition ?
			fil->condition->type : CONDITION_OR);
    if (filter_errno!=FILTER_NOERR)
        gtk_widget_destroy(fe_window);

    if (gtk_tree_model_get_iter_first(model, &iter)) {
        selection = gtk_tree_view_get_selection(fe_conditions_list);
        gtk_tree_selection_select_iter(selection, &iter);
        gtk_widget_set_sensitive(fe_condition_delete_button, TRUE);
        gtk_widget_set_sensitive(fe_condition_edit_button, TRUE);
        gtk_widget_set_sensitive(fe_op_codes_option_menu,
                                 gtk_tree_model_iter_n_children(model,
                                                                NULL) > 1);
    }

    /* We set the filters delete,revert,apply buttons sensitivity */
    gtk_widget_set_sensitive(fe_delete_button, TRUE);
    set_button_sensitivities(FALSE);
    fe_enable_right_page(TRUE);
}                      /* end fe_filters_list_selection_changed */

static GtkWidget *
fe_date_sample(void)
{
    struct tm tm;
    gchar fmt[20];
    char xfmt[] = "%x";    /* to avoid gcc whining */
    gchar *tmp;
    GtkWidget *label;

    strptime("2000-12-31", "%Y-%m-%d", &tm);
    strftime(fmt, sizeof fmt, xfmt, &tm);
    tmp = g_strdup_printf(_("(Example: write December 31, 2000, as %s)"),
                          fmt);
    label = gtk_label_new(tmp);
    g_free(tmp);

    return label;
}                               /* end fe_date_sample */
