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
		     0, 6, 19, 20,
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
		     0, 6, 18, 19,
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
    gtk_clist_set_row_height(GTK_CLIST(fe_clist), 16);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_clist,
		     0, 6, 0, 18,
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
		     6, 7, 0, 20,
		     GTK_FILL | GTK_SHRINK,
		     GTK_FILL | GTK_SHRINK,
		     0, 0);
    gtk_widget_show(fe_vseparator);

    /*
     * The right side
     */

    fe_box_applyrevert = gtk_hbox_new(TRUE, 0);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_box_applyrevert,
		     8, 19, 19, 20,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     10, 10);
    gtk_widget_show(fe_box_applyrevert);

    fe_apply = gtk_button_new_with_label("Apply");
    gtk_box_pack_start(GTK_BOX(fe_box_applyrevert),
		       fe_apply,
		       TRUE,
		       TRUE,
		       5);
    gtk_widget_show(fe_apply);
    fe_revert = gtk_button_new_with_label("Revert");
    gtk_box_pack_start(GTK_BOX(fe_box_applyrevert),
		       fe_revert,
		       TRUE,
		       TRUE,
		       5);
    gtk_widget_show(fe_revert);

    fe_notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(fe_notebook), GTK_POS_TOP);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_notebook,
		     7, 20, 0, 19,
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

    /* The name entry */
    
    fe_name_label = gtk_label_new("Filter name:");
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_name_label,
		     0, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_name_label);
    fe_name_entry = gtk_entry_new_with_max_length(256);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_name_entry,
		     2, 10, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_name_entry);

    /* The when stuff */
    
    fe_when_frame = gtk_frame_new("Process when:");
    gtk_frame_set_label_align(GTK_FRAME(fe_when_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_when_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_when_frame,
		     5, 10, 1, 4,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_when_frame);
    fe_when_box = gtk_vbox_new(TRUE, 5);
    gtk_container_add(GTK_CONTAINER(fe_when_frame),
		      fe_when_box);
    gtk_widget_show(fe_when_box);
    fe_when_match = gtk_radio_button_new_with_label(NULL,
						    "Matches");
    gtk_box_pack_start(GTK_BOX(fe_when_box),
		       fe_when_match,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_when_match);
    fe_when_nomatch = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_when_match)),
	"Doesn't Match");
    gtk_box_pack_start(GTK_BOX(fe_when_box),
		       fe_when_nomatch,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_when_nomatch);
    fe_when_always = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_when_nomatch)),
	"Doesn't Match");
    gtk_box_pack_start(GTK_BOX(fe_when_box),
		       fe_when_always,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_when_always);

    /* The groups */

    fe_group_frame = gtk_frame_new("Run on:");
    gtk_frame_set_label_align(GTK_FRAME(fe_group_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_group_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_group_frame,
		     0, 5, 1, 5,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_group_frame);
    fe_group_box = gtk_vbox_new(TRUE, 5);
    gtk_container_add(GTK_CONTAINER(fe_group_frame),
		      fe_group_box);
    gtk_widget_show(fe_group_box);
    fe_group_inbound = gtk_radio_button_new_with_label(NULL,
						       "Inbound");
    gtk_box_pack_start(GTK_BOX(fe_group_box),
		       fe_group_inbound,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_group_inbound);
    fe_group_outbound = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_group_inbound)),
			       "Outbound");
    gtk_box_pack_start(GTK_BOX(fe_group_box),
		       fe_group_outbound,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_group_outbound);
    fe_group_presend = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_group_outbound)),
			       "Pre-send");
    gtk_box_pack_start(GTK_BOX(fe_group_box),
		       fe_group_presend,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_group_presend);
    fe_group_demand = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_group_presend)),
			       "On demand");
    gtk_box_pack_start(GTK_BOX(fe_group_box),
		       fe_group_demand,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_group_demand);

    /* the type notebook's radio buttons */

    fe_type_frame = gtk_frame_new("Search type:");
    gtk_frame_set_label_align(GTK_FRAME(fe_type_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_type_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_type_frame,
		     0, 10, 5, 6,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_type_frame);
    fe_type_box = gtk_hbox_new(TRUE, 5);
    gtk_container_add(GTK_CONTAINER(fe_type_frame),
		      fe_type_box);
    gtk_widget_show(fe_type_box);
    fe_simple = gtk_radio_button_new_with_label(NULL,
						"Simple");
    gtk_signal_connect(GTK_OBJECT(fe_simple),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_checkbutton_toggled),
		       (gpointer)0);
    gtk_box_pack_start(GTK_BOX(fe_type_box),
		       fe_simple,
		       TRUE,
		       TRUE,
		       5);
    gtk_widget_show(fe_simple);
    fe_regex = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_simple)),
			       "Regular Expression");
    gtk_signal_connect(GTK_OBJECT(fe_regex),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_checkbutton_toggled),
		       (gpointer)1);
    gtk_box_pack_start(GTK_BOX(fe_type_box),
		       fe_regex,
		       TRUE,
		       TRUE,
		       5);
    gtk_widget_show(fe_regex);
    fe_exec = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_regex)),
			       "External Command");
    gtk_signal_connect(GTK_OBJECT(fe_exec),
		       "toggled",
		       GTK_SIGNAL_FUNC(fe_checkbutton_toggled),
		       (gpointer)2);
    gtk_box_pack_start(GTK_BOX(fe_type_box),
		       fe_exec,
		       TRUE,
		       TRUE,
		       5);
    gtk_widget_show(fe_exec);
    

    /* The type notebook */

    fe_type_notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(fe_type_notebook),
			       FALSE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(fe_type_notebook),
				 FALSE);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_type_notebook,
		     0, 10, 6, 10,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     5, 5);
    gtk_widget_show(fe_type_notebook);
    
    /* The simple page of the type notebook */

    fe_type_notebook_simple_page = gtk_table_new(5, 5, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook),
			     fe_type_notebook_simple_page,
			     NULL);
    gtk_widget_show(fe_type_notebook_simple_page);

    fe_type_simple_frame = gtk_frame_new("Match in:");
    gtk_frame_set_label_align(GTK_FRAME(fe_type_simple_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_type_simple_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_type_notebook_simple_page),
		     fe_type_simple_frame,
		     0, 5, 0, 4,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_type_simple_frame);
    fe_type_simple_table = gtk_table_new(3, 3, TRUE);
    gtk_container_add(GTK_CONTAINER(fe_type_simple_frame),
		      fe_type_simple_table);
    gtk_widget_show(fe_type_simple_table);

    fe_type_simple_all = gtk_check_button_new_with_label("All");
    gtk_table_attach(GTK_TABLE(fe_type_simple_table),
		     fe_type_simple_all,
		     0, 1, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_simple_all);
    fe_type_simple_header = gtk_check_button_new_with_label("Header");
    gtk_table_attach(GTK_TABLE(fe_type_simple_table),
		     fe_type_simple_header,
		     1, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_simple_header);
    fe_type_simple_body = gtk_check_button_new_with_label("Body");
    gtk_table_attach(GTK_TABLE(fe_type_simple_table),
		     fe_type_simple_body,
		     1, 2, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_simple_body);
    fe_type_simple_to = gtk_check_button_new_with_label("To:");
    gtk_table_attach(GTK_TABLE(fe_type_simple_table),
		     fe_type_simple_to,
		     2, 3, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_simple_to);
    fe_type_simple_from = gtk_check_button_new_with_label("From:");
    gtk_table_attach(GTK_TABLE(fe_type_simple_table),
		     fe_type_simple_from,
		     2, 3, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_simple_from);
    fe_type_simple_subject = gtk_check_button_new_with_label("Subject");
    gtk_table_attach(GTK_TABLE(fe_type_simple_table),
		     fe_type_simple_subject,
		     2, 3, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_simple_subject);

    fe_type_simple_label = gtk_label_new("Match string:");
    gtk_table_attach(GTK_TABLE(fe_type_notebook_simple_page),
		     fe_type_simple_label,
		     0, 1, 4, 5,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_type_simple_label);
    fe_type_simple_entry = gtk_entry_new_with_max_length(1023);
    gtk_table_attach(GTK_TABLE(fe_type_notebook_simple_page),
		     fe_type_simple_entry,
		     1, 5, 4, 5,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_type_simple_entry);

    /* The regex page of the type notebook */

    fe_type_notebook_regex_page = gtk_table_new(5, 5, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook),
			     fe_type_notebook_regex_page,
			     NULL);
    gtk_widget_show(fe_type_notebook_regex_page);

    fe_type_regex_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(fe_type_regex_scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_table_attach(GTK_TABLE(fe_type_notebook_regex_page),
		     fe_type_regex_scroll,
		     0, 5, 0, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     2, 2);
    gtk_widget_show(fe_type_regex_scroll);
    fe_type_regex_list = gtk_list_new();
    gtk_list_set_selection_mode(GTK_LIST(fe_type_regex_list),
				GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(fe_type_regex_scroll),
		      fe_type_regex_list);
    gtk_widget_show(fe_type_regex_list);

    fe_type_regex_box = gtk_hbox_new(TRUE, 5);
    gtk_table_attach(GTK_TABLE(fe_type_notebook_regex_page),
		     fe_type_regex_box,
		     0, 5, 3, 4,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_regex_box);
    fe_type_regex_add = gtk_button_new_with_label("Add");
    gtk_box_pack_start(GTK_BOX(fe_type_regex_box),
		       fe_type_regex_add,
		       TRUE,
		       TRUE,
		       0);
    gtk_widget_show(fe_type_regex_add);
    fe_type_regex_remove = gtk_button_new_with_label("Remove");
    gtk_box_pack_start(GTK_BOX(fe_type_regex_box),
		       fe_type_regex_remove,
		       TRUE,
		       TRUE,
		       0);
    gtk_widget_show(fe_type_regex_remove);

    fe_type_regex_entry = gtk_entry_new_with_max_length(1023);
    gtk_table_attach(GTK_TABLE(fe_type_notebook_regex_page),
		     fe_type_regex_entry,
		     0, 5, 4, 5,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     2, 2);
    gtk_widget_show(fe_type_regex_entry);

    /* The exec page of the type notebook */

    fe_type_notebook_exec_page = gtk_table_new(5, 5, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_type_notebook),
			     fe_type_notebook_exec_page,
			     NULL);
    gtk_widget_show(fe_type_notebook_exec_page);
    
    fe_type_exec_label = gtk_label_new("Command:");
    gtk_table_attach(GTK_TABLE(fe_type_notebook_exec_page),
		     fe_type_exec_label,
		     0, 1, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_type_exec_label);
    fe_type_exec_entry = gtk_entry_new_with_max_length(1023);
    gtk_table_attach(GTK_TABLE(fe_type_notebook_exec_page),
		     fe_type_exec_entry,
		     1, 5, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_type_exec_entry);
    

    /* The action notebook page */

    fe_notebook_action_page = gtk_table_new(10, 10, TRUE);
    fe_action_label = gtk_label_new("Action");
    gtk_notebook_append_page(GTK_NOTEBOOK(fe_notebook),
			     fe_notebook_action_page,
			     fe_action_label);
    gtk_widget_show(fe_notebook_action_page);
    gtk_widget_show(fe_action_label);

    /* The action area */
    
    fe_action_frame = gtk_frame_new("Action to perform:");
    gtk_frame_set_label_align(GTK_FRAME(fe_action_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_action_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_notebook_action_page),
		     fe_action_frame,
		     0, 10, 0, 7,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     5, 5);
    gtk_widget_show(fe_action_frame);
    fe_action_table = gtk_table_new(5, 6, TRUE);
    gtk_container_add(GTK_CONTAINER(fe_action_frame),
		      fe_action_table);
    gtk_widget_show(fe_action_table);

    fe_copy_button = gtk_radio_button_new_with_label(NULL,
						     "Copy to folder:");
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_copy_button,
		     0, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_copy_button);
    fe_copy_entry = gtk_entry_new_with_max_length(255);
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_copy_entry,
		     2, 5, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_copy_entry);
    fe_move_button = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_copy_button)),
			       "Move to folder:");
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_move_button,
		     0, 2, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_move_button);
    fe_move_entry = gtk_entry_new_with_max_length(1023);
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_move_entry,
		     2, 5, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_move_entry);
    fe_move_label = gtk_label_new(
	"(\"Move\" implies \"Do not place/leave\" below)");
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_move_label,
		     2, 5, 2, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_move_label);
    fe_print_button = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_move_button)),
			       "Print on printer:");
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_print_button,
		     0, 2, 3, 4,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_print_button);
    fe_print_entry = gtk_entry_new_with_max_length(255);
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_print_entry,
		     2, 5, 3, 4,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_print_entry);
    fe_run_button = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_print_button)),
			       "Run program:");
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_run_button,
		     0, 2, 4, 5,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_run_button);
    fe_run_entry = gtk_entry_new_with_max_length(1023);
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_run_entry,
		     2, 5, 4, 5,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_run_entry);
    fe_delete_button = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_run_button)),
			       "Send to Trash");
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_delete_button,
		     0, 2, 5, 6,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_delete_button);
    fe_delete_label = gtk_label_new(
	"(Implies \"Stop filtering\" below)");
    gtk_table_attach(GTK_TABLE(fe_action_table),
		     fe_delete_label,
		     2, 5, 5, 6,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_delete_label);

    /* The disposition area */

    fe_disp_frame = gtk_frame_new("Disposition");
    gtk_frame_set_label_align(GTK_FRAME(fe_disp_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_disp_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_notebook_action_page),
		     fe_disp_frame,
		     0, 10, 7, 10,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_disp_frame);
    fe_disp_box = gtk_vbox_new(TRUE, 5);
    gtk_container_add(GTK_CONTAINER(fe_disp_frame),
		      fe_disp_box);
    gtk_widget_show(fe_disp_box);

    fe_disp_place 
	= gtk_radio_button_new_with_label(NULL,
					  "Place/leave in default folder");
    gtk_box_pack_start(GTK_BOX(fe_disp_box),
		       fe_disp_place,
		       TRUE,
		       FALSE,
		       2);
    gtk_widget_show(fe_disp_place);
    fe_disp_continue = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_disp_place)),
	"Do not place/leave in default folder");
    gtk_box_pack_start(GTK_BOX(fe_disp_box),
		       fe_disp_continue,
		       TRUE,
		       FALSE,
		       2);
    gtk_widget_show(fe_disp_continue);
    fe_disp_stop = gtk_radio_button_new_with_label(
	gtk_radio_button_group(GTK_RADIO_BUTTON(fe_disp_continue)),
	"Stop filtering here");
    gtk_box_pack_start(GTK_BOX(fe_disp_box),
		       fe_disp_stop,
		       TRUE,
		       FALSE,
		       2);
    gtk_widget_show(fe_disp_stop);
    

    gtk_widget_show(fe_dialog);
}
