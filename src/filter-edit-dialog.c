/* -*- C -*-
 * filter-edit-dialog.c
 *
 * GTK code for balsa's filter edit dialog
 */


#include "filter.h"
#include "filter-edit.h"
#include <gnome.h>


/*
 * build_option_menu()
 *
 * takes an option_list and builds an OptionMenu from it
 *
 * Arguments:
 *    option_list[] options - array of options
 *    gint num - number of options
 *    GtkSignalFunc func - callback function (will get data of i)
 *
 * Returns:
 *    GtkOptionMenu - the menu created
 */
GtkWidget *build_option_menu(option_list options[],
		       gint num,
		       GtkSignalFunc func)
{
    GtkWidget *option_menu;
    GtkWidget *menu;
    GSList *group;
    int i;

    if (num < 1)
	return(NULL);

    menu = gtk_menu_new();
    group = NULL;

    for (i = 0; i < num; i++)
    {
	options[i].widget = gtk_radio_menu_item_new_with_label(
	    group,
	    options[i].text);
	group = gtk_radio_menu_item_group(
	    GTK_RADIO_MENU_ITEM(options[i].widget));
	gtk_menu_append(GTK_MENU(menu), options[i].widget);
	if (func)
	{
	    gtk_signal_connect(GTK_OBJECT(options[i].widget),
			       "toggled",
			       func,
			       (gpointer)i);
	}
	gtk_widget_show(options[i].widget);
    }

    option_menu = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu), 0);

    return(option_menu);
} /* end build_option_menu */


/*
 * build_base_dialog()
 *
 * Builds the framework of the dialog and nothing more
 */
void build_base_dialog()
{
    fe_dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(fe_dialog),
			 "Edit Filters...");

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
} /* end build_dialog() */


/*
 * build_left_side()
 *
 * Builds the left side of the dialog
 */
void build_left_side()
{
    static gchar *titles[] =
    {
	"Enabled",
	"Name",
    };

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
} /* end build_left_side() */


/*
 * build_type_notebook()
 *
 * builds the "Searc Type" notebook on the "Action" page
 */
void build_type_notebook()
{
    /* The notebook */

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
} /* end build_type_notebook() */


/*
 * build_match_page()
 *
 * Builds the "Match" page of the main notebook
 */
void build_match_page()
{
    /* The notebook page */

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


    /* The "Process when:" option menu */

    fe_when_frame = gtk_frame_new("Process when:");
    gtk_frame_set_label_align(GTK_FRAME(fe_when_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_when_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_when_frame,
		     5, 10, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_when_frame);
    fe_when_box = gtk_vbox_new(TRUE, 5);
    gtk_container_add(GTK_CONTAINER(fe_when_frame),
		      fe_when_box);
    gtk_widget_show(fe_when_box);

    fe_when_option_menu = build_option_menu(fe_process_when,
					    3,
					    NULL);
    gtk_box_pack_start(GTK_BOX(fe_when_box),
		       fe_when_option_menu,
		       TRUE,
		       FALSE,
		       5);
    gtk_widget_show(fe_when_option_menu);


    /* The "Run on:" option menu */

    fe_group_frame = gtk_frame_new("Run on:");
    gtk_frame_set_label_align(GTK_FRAME(fe_group_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_group_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_group_frame,
		     0, 5, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_group_frame);
    fe_group_box = gtk_vbox_new(TRUE, 5);
    gtk_container_add(GTK_CONTAINER(fe_group_frame),
		      fe_group_box);
    gtk_widget_show(fe_group_box);

    fe_group_option_menu = build_option_menu(fe_run_on,
					     4,
					     NULL);
    gtk_box_pack_start(GTK_BOX(fe_group_box),
 		       fe_group_option_menu,
		       TRUE,
		       FALSE,
		       5);
    gtk_widget_show(fe_group_option_menu);

    /* the type notebook's option menu */

    fe_type_frame = gtk_frame_new("Search type:");
    gtk_frame_set_label_align(GTK_FRAME(fe_type_frame),
			      GTK_POS_LEFT,
			      GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(fe_type_frame),
			      GTK_SHADOW_ETCHED_IN);
    gtk_container_border_width(GTK_CONTAINER(fe_type_frame), 5);
    gtk_table_attach(GTK_TABLE(fe_notebook_match_page),
		     fe_type_frame,
		     0, 10, 5, 6,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_SHRINK,
		     5, 5);
    gtk_widget_show(fe_type_frame);
    fe_type_box = gtk_hbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(fe_type_frame),
		      fe_type_box);
    gtk_widget_show(fe_type_box);

    fe_search_option_menu = build_option_menu(fe_search_type,
					      3,
					      GTK_SIGNAL_FUNC(fe_checkbutton_toggled));
    gtk_box_pack_start(GTK_BOX(fe_type_box),
		       fe_search_option_menu,
		       FALSE,
		       FALSE,
		       5);
    gtk_widget_show(fe_search_option_menu);

    build_type_notebook();
} /* end build_match_page() */


/*
 * build_action_page()
 *
 * Builds the "Action" page of the main notebook
 */
void build_action_page()
{
    /* The notebook page */

    fe_notebook_action_page = gtk_table_new(10, 10, FALSE);
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
    fe_action_table = gtk_table_new(5, 6, FALSE);
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
} /* end build_action_page() */


/*
 * build_right_side()
 *
 * Builds the right side of the dialog
 */
void build_right_side()
{
    /* The apply/revert buttons */
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

    /* the main notebook */

    fe_notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(fe_notebook), GTK_POS_TOP);
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_notebook,
		     7, 20, 0, 19,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     10, 10);
    gtk_widget_show(fe_notebook);

    build_match_page();

    build_action_page();
} /* end build_right_side() */


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
    build_base_dialog();

    build_left_side();


    /* The separator */
 
    fe_vseparator = gtk_vseparator_new();
    gtk_table_attach(GTK_TABLE(fe_table),
		     fe_vseparator,
		     6, 7, 0, 20,
		     GTK_FILL | GTK_SHRINK,
		     GTK_FILL | GTK_SHRINK,
		     0, 0);
    gtk_widget_show(fe_vseparator);

    build_right_side();

    gtk_widget_show(fe_dialog);
}
