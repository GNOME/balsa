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
}
	

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
}
