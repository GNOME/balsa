/* -*- C -*-
 * filter-edit-callbacks.c
 *
 * Callbacks for the filter edit dialog
 */


#include "filter.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include "filter-edit.h"


/*
 * fe_dialog_button_clicked()
 *
 * Handles the clicking of the main buttons at the 
 * bottom of the dialog.  wooo.
 */
void fe_dialog_button_clicked(GtkWidget *widget,
			      gpointer data)
{
    switch ((int)data)
    {
    case 1: /* Cancel button */
	g_print("cancel\n");
	break;

    case 2: /* Help button */
	g_print("help\n");
	/* something here */
	break;

    case 3: /* OK button */
	g_print("ok\n");
	/* more of something here */

    default:
	/* we should NEVER get here */
    }

    /* destroy the dialog */
    gtk_widget_destroy(fe_dialog);
    /* more destruction is needed.  But that is for later */
} /* end fe_dialog_button_clicked */
	

/*
 * fe_checkbutton_toggled()
 *
 * Handles toggling of the type checkbuttons.
 * When they are toggled, the notebook page must change
 */
void fe_checkbutton_toggled(GtkWidget *widget,
			    gpointer data)
{
    if (GTK_CHECK_MENU_ITEM(widget)->active) 
    {
	gtk_notebook_set_page(GTK_NOTEBOOK(fe_type_notebook),
			      (gint)data);
    }
} /* end fe_checkbutton_toggled() */


/*
 * fe_add_pressed()
 *
 * Callback for the "Add" button for the regex type
 */
void fe_add_pressed(GtkWidget *widget,
		    gpointer throwaway)
{
    gchar *text;
    GList *list;
    GtkWidget *list_item;

    list = NULL;

    text = gtk_entry_get_text(GTK_ENTRY(fe_type_regex_entry));

    if ((text == NULL) || (strlen(text) == 0))
	return;
    list_item = gtk_list_item_new_with_label(text);
    gtk_widget_show(list_item);
    list = g_list_append(list,
			 list_item);
    gtk_list_append_items(GTK_LIST(fe_type_regex_list),
			  list);
    gtk_entry_set_text(GTK_ENTRY(fe_type_regex_entry), "");
} /* end fe_add_pressed() */


/*
 * fe_remove_pressed()
 * 
 * Callback for the "remove" button of the regex type
 */
void fe_remove_pressed(GtkWidget *widget,
		       gpointer throwaway)
{
    GList *selected;

    selected = (GList *)GTK_LIST(fe_type_regex_list)->selection;

    if (! selected)
	return;

    gtk_list_remove_items(GTK_LIST(fe_type_regex_list),
			  selected);
} /* end fe_remove_pressed() */


/*
 * fe_type_simple_toggled()
 *
 * Callback for the checkbuttons in the "Simple" type
 */
void fe_type_simple_toggled(GtkWidget *widget,
			    gpointer data)
{
    if (GTK_TOGGLE_BUTTON(widget)->active)
    {
	switch((gint)data)
	{
	case 1: /* ALL */
	    gtk_toggle_button_set_state(
		GTK_TOGGLE_BUTTON(fe_type_simple_header),
		FALSE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_header),
		FALSE);
	    gtk_toggle_button_set_state(
		GTK_TOGGLE_BUTTON(fe_type_simple_body),
		FALSE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_body),
		FALSE);
 
	case 2: /* header */
	    gtk_toggle_button_set_state(
		GTK_TOGGLE_BUTTON(fe_type_simple_to),
		FALSE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_to),
		FALSE);
	    gtk_toggle_button_set_state(
		GTK_TOGGLE_BUTTON(fe_type_simple_from),
		FALSE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_from),
		FALSE);
	    gtk_toggle_button_set_state(
		GTK_TOGGLE_BUTTON(fe_type_simple_subject),
		FALSE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_subject),
		FALSE);
 
	default:
	    break;
	}
    }
    else
    {
	switch((gint)data)
	{
	case 1: /* ALL */
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_header),
		TRUE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_body),
		TRUE);

	case 2: /* header */
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_to),
		TRUE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_from),
		TRUE);
	    gtk_widget_set_sensitive(
		GTK_WIDGET(fe_type_simple_subject),
		TRUE);

	default:
	    break;
	}
    }
} /* end fe_type_simple_toggled() */
