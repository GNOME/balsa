/* -*- C -*-
 * filter-edit-dialog.c
 *
 * GTK code for balsa's filter edit dialog
 */


#include "filter.h"
#include "filter-edit.h"
#include <gnome.h>




/*
 * filter_edit_dialog()
 *
 * Returns immediately, but fires off the filter edit dialog.
 *
 * Arguments:
 *   GList *filter_list - the list of filters
 */
void filter_edit_dialog(GList *filter_list)
{
    static gchar *titles[] =
    {
	"Enabled",
	"Name",
    };

    fe_dialog = gtk_dialog_new();

    /*
     * The buttons at the bottom
     */
    fe_dialog_ok = gtk_button_new_with_label("OK");
    gtk_signal_connect(GTK_OBJECT(fe_dialog_ok),
		       "clicked",
		       GTK_SIGNAL_FUNC(fe_dialog_button_clicked),
		       (gpointer) 3);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(fe_dialog)->action_area),
		       fe_dialog_ok,
		       FALSE, TRUE, 0);
    gtk_widget_show(fe_dialog_ok);
    fe_dialog_cancel = gtk_button_new_with_label("Cancel");
    gtk_signal_connect(GTK_OBJECT(fe_dialog_cancel),
		       "clicked",
		       GTK_SIGNAL_FUNC(fe_dialog_button_clicked),
		       (gpointer) 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(fe_dialog)->action_area),
		       fe_dialog_cancel,
		       FALSE, TRUE, 0);
    gtk_widget_show(fe_dialog_cancel);
    fe_dialog_help = gtk_button_new_with_label("Help");
    gtk_signal_connect(GTK_OBJECT(fe_dialog_help),
		       "clicked",
		       GTK_SIGNAL_FUNC(fe_dialog_button_clicked),
		       (gpointer) 1);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(fe_dialog)->action_area),
		       fe_dialog_help,
		       FALSE, TRUE, 0);
    gtk_widget_show(fe_dialog_help);

    /*
     * The main table
     */

    fe_table = gtk_table_new(20, 20, FALSE);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(fe_dialog)->vbox),
		       GTK_WIDGET(fe_table),
		       TRUE, TRUE, 0);
    gtk_widget_show(fe_table);

    /*
     * The left side
     */

    fe_box_newdelete = gtk_hbox_new(TRUE, 2);
    fe_new = gtk_button_new_with_label("New");
    gtk_box_pack_start(GTK_BOX(fe_box_newdelete),
		       fe_new,
		       TRUE,
		       TRUE,
		       0);
    gtk_widget_show(fe_new);
    fe_delete = gtk_button_new_with_label("Delete");
    gtk_box_pack_start(GTK_BOX(fe_box_newdelete),
		       fe_delete,
		       TRUE,
		       TRUE,
		       0);
    gtk_widget_show(fe_delete);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_box_newdelete,
		     0, 3, 19, 20,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     10, 10);
    gtk_widget_show(fe_box_newdelete);

    fe_box_updown = gtk_hbox_new(TRUE, 2);
    fe_up = gtk_button_new_with_label("Up");
    gtk_box_pack_start(GTK_BOX(fe_box_updown),
		       fe_up,
		       TRUE,
		       TRUE,
		       0);
    gtk_widget_show(fe_up);
    fe_down = gtk_button_new_with_label("Down");
    gtk_box_pack_start(GTK_BOX(fe_box_updown),
		       fe_down,
		       TRUE,
		       TRUE,
		       0);
    gtk_widget_show(fe_down);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_box_updown,
		     0, 3, 18, 19,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     10, 0);
    gtk_widget_show(fe_box_updown);

    fe_clist = gtk_clist_new_with_titles(2, titles);
    gtk_clist_set_policy(GTK_CLIST(fe_clist),
			 GTK_POLICY_AUTOMATIC,
			 GTK_POLICY_AUTOMATIC);
    gtk_clist_set_selection_mode(GTK_CLIST(fe_clist),
				 GTK_SELECTION_SINGLE);
    gtk_clist_set_column_justification(GTK_CLIST(fe_clist),
				       0,
				       GTK_JUSTIFY_CENTER);
    gtk_clist_set_column_width(GTK_CLIST(fe_clist),
			       0, 16);
    gtk_clist_set_column_width(GTK_CLIST(fe_clist),
			       1,150);
    gtk_clist_set_row_height(GTK_CLIST(fe_clist), 16);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_clist,
		     0, 3, 0, 18,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     10,10);
    gtk_widget_show(fe_clist);

    /*
     * The separator
     */

    fe_vseparator = gtk_vseparator_new();
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_vseparator,
		     3, 4, 0, 20,
		     GTK_FILL | GTK_SHRINK,
		     GTK_FILL | GTK_SHRINK,
		     0, 0);
    gtk_widget_show(fe_vseparator);

    /*
     * The right side
     */

    fe_box_applyrevert = gtk_hbox_new(TRUE, 5);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_box_applyrevert,
		     5, 19, 19, 20,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     10, 10);
    gtk_widget_show(fe_box_applyrevert);

    fe_apply = gtk_button_new_with_label("Apply");
    gtk_box_pack_start(GTK_BOX(fe_box_applyrevert),
		       fe_apply,
		       TRUE,
		       TRUE,
		       10);
    gtk_widget_show(fe_apply);
    fe_revert = gtk_button_new_with_label("Revert");
    gtk_box_pack_start(GTK_BOX(fe_box_applyrevert),
		       fe_revert,
		       TRUE,
		       TRUE,
		       10);
    gtk_widget_show(fe_revert);

    fe_notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(fe_notebook), GTK_POS_TOP);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_notebook,
		     4, 20, 0, 19,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     10, 10);
    gtk_widget_show(fe_notebook);

    /* The match notebook page */

    fe_notebook_match_page = gtk_table_new(10, 10, FALSE);
    fe_match_label = gtk_label_new("Match");
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_notebook),
			     fe_notebook_match_page,
			     fe_match_label);
    gtk_widget_show(fe_notebook_match_page);
    gtk_widget_show(fe_match_label);

    /* The action notebook page */

    fe_notebook_action_page = gtk_table_new(10, 10, FALSE);
    fe_action_label = gtk_label_new("Action");
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_notebook),
			     fe_notebook_action_page,
			     fe_action_label);
    gtk_widget_show(fe_notebook_action_page);
    gtk_widget_show(fe_action_label);

    gtk_widget_show(fe_dialog);
}
