/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include "balsa-app.h"
#include "filter-edit.h"
#include "filter-funcs.h"
#include "message.h"
#include <glib/gi18n.h>

#define FILTER_EDIT_ENTRY_MAX_LENGTH 256
#define FILTER_EDIT_PADDING 6

/* List of filter_run_dialog_box opened : this is !=NULL when
 * filter_run_dialog boxes are opened We test that because you don't
 * want to be able to modify filters that are being used in other
 * dialog boxes FIXME : we can perhaps imagine a way to "refresh" the
 * other dialog boxes when filters have been modified but it'll be
 * complex, and I'm not sure it is worth it
 *
 * Defined in filter-run.dialog.c
 */

extern GList * fr_dialogs_opened;

/* The dialog widget (we need it to be able to close dialog on error) */

GtkWidget * fe_window;

GtkTreeView * fe_filters_list;

gboolean fe_already_open=FALSE;

/* containers for radiobuttons */
GtkWidget *fe_op_codes_option_menu;

/* Name field */
GtkWidget *fe_name_label;
GtkWidget *fe_name_entry;

/* widget for the conditions */
GtkTreeView *fe_conditions_list;

/* List of strings in the combo of user headers name */
GList * fe_user_headers_list;

/* notification field */
GtkWidget *fe_sound_button;
GtkWidget *fe_sound_entry;
GtkWidget *fe_popup_button;
GtkWidget *fe_popup_entry;

/* action field */
GtkWidget *fe_action_option_menu;

/* Mailboxes option menu */
GtkWidget * fe_mailboxes;

/* Select colors */
GtkWidget * fe_color_buttons;
GtkWidget * fe_foreground_set;
GtkWidget * fe_foreground;
GtkWidget * fe_background_set;
GtkWidget * fe_background;

GtkWidget* fe_right_page;

/* Different buttons that need to be greyed or ungreyed */
GtkWidget *fe_new_button, *fe_delete_button;
GtkWidget *fe_apply_button, *fe_revert_button;
GtkWidget * fe_condition_delete_button,* fe_condition_edit_button;

/* ******************************** */

option_list fe_search_type[] = {
    {N_("Simple"), CONDITION_STRING},
    {N_("Regular Expression"), CONDITION_REGEX},
    {N_("Date interval"), CONDITION_DATE},
    {N_("Flag condition"), CONDITION_FLAG}
};

option_list fe_actions[] = {
    {N_("Copy to folder:"), FILTER_COPY},
    {N_("Move to folder:"), FILTER_MOVE},
    {N_("Colorize"), FILTER_COLOR},
    {N_("Print on printer:"), FILTER_PRINT},
    {N_("Run program:"), FILTER_RUN},
    {N_("Send to Trash"), FILTER_TRASH}
};

option_list fe_op_codes[] = {
    {N_("OR"), FILTER_OP_OR},
    {N_("AND"), FILTER_OP_AND}
};

/* ******************************** */
void
fe_enable_right_page(gboolean enabled)
{
    gtk_widget_set_sensitive(fe_right_page, enabled);
}

/*
 * fe_build_option_menu()
 *
 * takes an option_list and builds an OptionMenu from it
 *
 * Arguments:
 *    option_list[] options - array of options
 *    gint num - number of options
 *    GCallback func - callback function
 *    gpointer cb_data - user-data for func
 *
 * Returns:
 *    GtkOptionMenu - the menu created
 */

static void
fe_combo_box_info_free(struct fe_combo_box_info * info)
{
    g_slist_free(info->values);
    g_free(info);
}

GtkWidget *
fe_build_option_menu(option_list options[], gint num, GCallback func,
                     gpointer cb_data)
{
    GtkWidget *combo_box;
    struct fe_combo_box_info *info;
    int i;

    if (num < 1)
	return (NULL);

    combo_box = gtk_combo_box_text_new();
    info = g_new(struct fe_combo_box_info, 1);
    info->values = NULL;

    for (i = 0; i < num; i++) {
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box),
                                       _(options[i].text));
	info->values =
	    g_slist_append(info->values, GINT_TO_POINTER(options[i].value));
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), 0);
    if (func)
	g_signal_connect(G_OBJECT(combo_box), "changed", func, cb_data);
    g_object_set_data_full(G_OBJECT(combo_box), BALSA_FE_COMBO_BOX_INFO,
                           info, (GDestroyNotify) fe_combo_box_info_free);

    return combo_box;
}				/* end fe_build_option_menu */

/*
 * build_left_side()
 *
 * Builds the left side of the dialog
 */
static GtkWidget *
build_left_side(void)
{
    GtkWidget *vbox, *bbox;

    GtkWidget *sw;

    /*
       /--------\
       | /---\  |
       | |   |  |
       | |   |  |
       | |   |  |
       | \---/  |
       |        |
       | -- --  |
       | -- --  |
       \--------/
     */
    vbox = gtk_vbox_new(FALSE, 2);

    /* the list */
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    fe_filters_list =
        libbalsa_filter_list_new(TRUE, _("Name"), GTK_SELECTION_BROWSE,
                                 G_CALLBACK
                                 (fe_filters_list_selection_changed),
                                 TRUE);

    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(fe_filters_list));

    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 2);

    /* new and delete buttons */
    bbox = gtk_hbutton_box_new();
    gtk_box_set_spacing(GTK_BOX(bbox), 2);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);

    gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 2);

    /* new button */
    fe_new_button = balsa_stock_button_with_label(GTK_STOCK_NEW, _("_New"));
    g_signal_connect(G_OBJECT(fe_new_button), "clicked",
		     G_CALLBACK(fe_new_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), fe_new_button);
    /* delete button */
    fe_delete_button =
        balsa_stock_button_with_label(GTK_STOCK_DELETE, _("_Delete"));
    g_signal_connect(G_OBJECT(fe_delete_button), "clicked",
		     G_CALLBACK(fe_delete_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), fe_delete_button);
    gtk_widget_set_sensitive(fe_delete_button,FALSE);

    return vbox;
}				/* end build_left_side() */

/*
 * build_match_page()
 *
 * Builds the "Match" page of the main notebook
 */
static GtkWidget *
build_match_page()
{
    GtkWidget *page, *button;
    GtkWidget *label, *scroll;
    GtkWidget *box = NULL;

    /* The notebook page */
    page = gtk_table_new(10, 15, FALSE);

    /* The name entry */

    fe_name_label = gtk_label_new_with_mnemonic(_("_Filter name:"));
    gtk_table_attach(GTK_TABLE(page),
		     fe_name_label,
		     0, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_name_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(fe_name_entry),
                             FILTER_EDIT_ENTRY_MAX_LENGTH);
    gtk_table_attach(GTK_TABLE(page),
		     fe_name_entry,
		     2, 10, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    gtk_label_set_mnemonic_widget(GTK_LABEL(fe_name_label), fe_name_entry);
    g_signal_connect(fe_name_entry, "changed",
                     G_CALLBACK(fe_action_changed), NULL);

    /* The filter op-code : "OR" or "AND" all the conditions */ 

    label = gtk_label_new(_("Operation between conditions"));
    gtk_table_attach(GTK_TABLE(page),
		     label,
		     0, 5, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    box = gtk_vbox_new(FALSE, 2);
    gtk_table_attach(GTK_TABLE(page),
		     box,
		     5, 10, 1, 3,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    fe_op_codes_option_menu = fe_build_option_menu(fe_op_codes,
						ELEMENTS(fe_op_codes),
						NULL, NULL);
    g_signal_connect(fe_op_codes_option_menu, "changed",
                     G_CALLBACK(fe_action_changed), NULL);
    gtk_box_pack_start(GTK_BOX(box), fe_op_codes_option_menu, FALSE, FALSE,
		       2);

    /* list of conditions defining how this filter matches */

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_table_attach(GTK_TABLE(page),
		     scroll,
		     0, 5, 4, 8,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, 2, 2);

    fe_conditions_list =
        libbalsa_filter_list_new(TRUE, NULL, GTK_SELECTION_BROWSE, NULL,
                                 FALSE);
    g_signal_connect(G_OBJECT(fe_conditions_list), "row-activated",
                     G_CALLBACK(fe_conditions_row_activated), NULL);

    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(fe_conditions_list));

    box = gtk_hbox_new(TRUE, 5);
    gtk_table_attach(GTK_TABLE(page),
		     box,
		     0, 5, 8, 9,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    fe_condition_edit_button = gtk_button_new_with_mnemonic(_("_Edit"));
    gtk_widget_set_sensitive(fe_condition_edit_button,FALSE);
    gtk_box_pack_start(GTK_BOX(box), fe_condition_edit_button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(fe_condition_edit_button), "clicked",
                     G_CALLBACK(fe_edit_condition), GINT_TO_POINTER(0));
    button = gtk_button_new_with_mnemonic(_("Ne_w"));
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
                     G_CALLBACK(fe_edit_condition), GINT_TO_POINTER(1));
    fe_condition_delete_button = gtk_button_new_with_mnemonic(_("_Remove"));
    gtk_widget_set_sensitive(fe_condition_delete_button,FALSE);
    gtk_box_pack_start(GTK_BOX(box), fe_condition_delete_button, TRUE, 
                       TRUE, 0);
    g_signal_connect(G_OBJECT(fe_condition_delete_button), "clicked",
		     G_CALLBACK(fe_condition_remove_pressed), NULL);

    return page;
}				/* end build_match_page() */


/*
 * build_action_page()
 *
 * Builds the "Action" page of the main notebook
 */

static GtkWidget *
fe_make_color_buttons(void)
{
    GtkWidget *table_widget = gtk_table_new(2, 2, FALSE);
    GtkTable  *table = GTK_TABLE(table_widget);
    GdkColor color;

    fe_foreground_set = gtk_check_button_new_with_mnemonic(_("Foreground"));
    gtk_table_attach_defaults(table, fe_foreground_set, 0, 1, 0, 1);
    gdk_color_parse("black", &color);
    fe_foreground = gtk_color_button_new_with_color(&color);
    gtk_widget_set_sensitive(fe_foreground, FALSE);
    gtk_table_attach_defaults(table, fe_foreground, 1, 2, 0, 1);
    g_signal_connect(fe_foreground_set, "toggled",
                     G_CALLBACK(fe_color_check_toggled), fe_foreground);
    g_signal_connect(fe_foreground, "color-set",
                     G_CALLBACK(fe_color_set), NULL);

    fe_background_set = gtk_check_button_new_with_mnemonic(_("Background"));
    gtk_table_attach_defaults(table, fe_background_set, 0, 1, 1, 2);
    gdk_color_parse("white", &color);
    fe_background = gtk_color_button_new_with_color(&color);
    gtk_widget_set_sensitive(fe_background, FALSE);
    gtk_table_attach_defaults(table, fe_background, 1, 2, 1, 2);
    g_signal_connect(fe_background_set, "toggled",
                     G_CALLBACK(fe_color_check_toggled), fe_background);
    g_signal_connect(fe_background, "color-set",
                     G_CALLBACK(fe_color_set), NULL);

    return table_widget;
}

static GtkWidget *
build_action_page(GtkWindow * window)
{
    GtkWidget *page, *frame, *table;
    GtkWidget *box;
    GtkWidget *dialog;

    page = gtk_vbox_new(TRUE, 5);

    /* The notification area */

    frame = gtk_frame_new(_("Notification:"));
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);

    table = gtk_table_new(3, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);

    /* Notification buttons */
    fe_sound_button = gtk_check_button_new_with_label(_("Play sound:"));
    gtk_table_attach(GTK_TABLE(table), fe_sound_button,
		     0, 1, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    dialog =
        gtk_file_chooser_dialog_new(_("Use Sound..."), NULL,
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_alternative_button_order(GTK_DIALOG(dialog),
                                            GTK_RESPONSE_ACCEPT,
                                            GTK_RESPONSE_CANCEL, -1);
    fe_sound_entry = gtk_file_chooser_button_new_with_dialog(dialog);
    gtk_table_attach(GTK_TABLE(table), fe_sound_entry, 1, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    /* fe_sound_entry is initially sensitive, so to be consistent 
     * we must make fe_sound_button active */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_sound_button), TRUE);
    g_signal_connect(G_OBJECT(fe_sound_button), "toggled",
                     G_CALLBACK(fe_button_toggled), fe_sound_entry);
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(fe_sound_response), NULL);

    fe_popup_button = gtk_check_button_new_with_label(_("Popup text:"));
    gtk_table_attach(GTK_TABLE(table),
		     fe_popup_button,
		     0, 1, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_popup_entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(fe_popup_entry), 
                             FILTER_EDIT_ENTRY_MAX_LENGTH);
    gtk_table_attach(GTK_TABLE(table),
		     fe_popup_entry,
		     1, 2, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    /* fe_popup_entry is initially sensitive, so to be consistent 
     * we must make fe_popup_button active */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_popup_button), TRUE);
    g_signal_connect(G_OBJECT(fe_popup_button), "toggled",
                     G_CALLBACK(fe_button_toggled), fe_popup_entry);
    g_signal_connect(G_OBJECT(fe_popup_entry), "changed",
                     G_CALLBACK(fe_action_changed), NULL);

    /* The action area */
    frame = gtk_frame_new(_("Action to perform:"));
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 2);

    box = gtk_vbox_new(TRUE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_container_add(GTK_CONTAINER(frame), box);

    fe_action_option_menu =
        fe_build_option_menu(fe_actions, ELEMENTS(fe_actions),
                             G_CALLBACK(fe_action_selected), NULL);
    gtk_box_pack_start(GTK_BOX(box), fe_action_option_menu,
                       TRUE, FALSE, 1);

    /* FIXME : we use the global mru folder list, perhaps we should use
       our own. We'll see this later, for now let's make something usable
       the old way was way too ugly and even unusable for users with zillions
       of mailboxes. Yes there are ;-)!
    */
    fe_mailboxes = balsa_mblist_mru_option_menu(window,
						&balsa_app.folder_mru);
    g_signal_connect(G_OBJECT(fe_mailboxes), "changed",
                     G_CALLBACK(fe_action_changed), NULL);
    gtk_box_pack_start(GTK_BOX(box), fe_mailboxes, TRUE, FALSE, 1);

    fe_color_buttons = fe_make_color_buttons();
    gtk_box_pack_start(GTK_BOX(box), fe_color_buttons, TRUE, FALSE, 1);
    return page;
}				/* end build_action_page() */


/*
 * build_right_side()
 *
 * Builds the right side of the dialog
 */
static GtkWidget *
build_right_side(GtkWindow * window)
{
    GtkWidget *rightside;
    GtkWidget *notebook, *page;
    GtkWidget *bbox;

    rightside = gtk_vbox_new(FALSE, 0);

    /* the main notebook */
    notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(rightside), notebook, TRUE, TRUE, 0);

    page = build_match_page();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     page, gtk_label_new(_("Match")));
    page = build_action_page(window);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     page, gtk_label_new(_("Action")));

    /* button box */
    bbox = gtk_hbutton_box_new();
    gtk_box_pack_start(GTK_BOX(rightside), bbox, FALSE, FALSE, 0);

    fe_apply_button = gtk_button_new_from_stock(GTK_STOCK_APPLY);
    g_signal_connect(G_OBJECT(fe_apply_button), "clicked",
		     G_CALLBACK(fe_apply_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), fe_apply_button);

    fe_revert_button =
        balsa_stock_button_with_label(GTK_STOCK_UNDO, _("Revert"));
    g_signal_connect(G_OBJECT(fe_revert_button), "clicked",
		     G_CALLBACK(fe_revert_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), fe_revert_button);
    gtk_widget_set_sensitive(fe_apply_button,FALSE);
    gtk_widget_set_sensitive(fe_revert_button,FALSE);

    return rightside;
}				/* end build_right_side() */

/* Helper */
static void
fe_collect_user_headers(LibBalsaCondition * condition)
{
    g_return_if_fail(condition != NULL);

    switch (condition->type) {
    case CONDITION_STRING:
        if (CONDITION_CHKMATCH(condition, CONDITION_MATCH_US_HEAD)) {
	    gchar *user_header = condition->match.string.user_header;
	    if (user_header && *user_header)
		fe_add_new_user_header(user_header);
	}
        break;
    case CONDITION_AND:
    case CONDITION_OR:
        fe_collect_user_headers(condition->match.andor.left);
        fe_collect_user_headers(condition->match.andor.right);
    default:
        break;
    }
}

/*
 * filters_edit_dialog()
 *
 * Returns immediately, but fires off the filter edit dialog.
 */
#define BALSA_FILTER_PADDING 6
void
filters_edit_dialog(void)
{
    GtkWidget *hbox;
    GtkWidget *piece;
    LibBalsaFilter * cpfil,* fil;
    GSList * filter_list;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (fr_dialogs_opened) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("A filter run dialog is open."
                            "Close it before you can modify filters."));
	return;
    }
    if (fe_already_open) {
	gtk_window_present(GTK_WINDOW(fe_window));
	return;
    }
    
    fe_already_open=TRUE;

    piece = build_left_side();

    fe_window = gtk_dialog_new_with_buttons(_("Balsa Filters"),
                                            NULL, 0, /* FIXME */
                                            GTK_STOCK_OK,
                                            GTK_RESPONSE_OK,
                                            GTK_STOCK_CANCEL,
                                            GTK_RESPONSE_CANCEL,
                                            GTK_STOCK_HELP,
                                            GTK_RESPONSE_HELP,
					    NULL);

    g_signal_connect(G_OBJECT(fe_window), "response",
                     G_CALLBACK(fe_dialog_response), NULL);
    g_signal_connect(G_OBJECT(fe_window), "destroy",
	             G_CALLBACK(fe_destroy_window_cb), NULL);

    gtk_window_set_wmclass(GTK_WINDOW (fe_window), "filter-edit", "Balsa");
    gtk_dialog_set_response_sensitive(GTK_DIALOG(fe_window),
                                      GTK_RESPONSE_OK, FALSE);

    /* main hbox */
    hbox = gtk_hbox_new(FALSE, FILTER_EDIT_PADDING);
    gtk_box_pack_start(GTK_BOX
                       (gtk_dialog_get_content_area(GTK_DIALOG(fe_window))),
                       hbox, TRUE, TRUE, FILTER_EDIT_PADDING);
    gtk_box_pack_start(GTK_BOX(hbox), piece, FALSE, FALSE,
                       FILTER_EDIT_PADDING);

    gtk_box_pack_start(GTK_BOX(hbox), gtk_vseparator_new(),
                       FALSE, FALSE, 0);

    fe_right_page = build_right_side(GTK_WINDOW(fe_window));
    gtk_widget_set_sensitive(fe_right_page, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), fe_right_page, TRUE, TRUE,
                       FILTER_EDIT_PADDING);

    fe_user_headers_list = NULL;

    /* Populate the list of filters */
    model = gtk_tree_view_get_model(fe_filters_list);
    for(filter_list=balsa_app.filters; 
        filter_list; filter_list=g_slist_next(filter_list)) {

	fil=(LibBalsaFilter*)filter_list->data;
	/* Make a copy of the current filter */
	cpfil=libbalsa_filter_new();
	
	cpfil->name=g_strdup(fil->name);
	cpfil->flags=fil->flags;
	if (fil->sound) cpfil->sound=g_strdup(fil->sound);
	if (fil->popup_text) cpfil->popup_text=g_strdup(fil->popup_text);
	/* FIXME: cpfil->conditions_op=fil->conditions_op; */
	cpfil->flags=fil->flags;

	/* We have to unset the "compiled" flag, because we don't copy
	 * the regex_t struc with copy condition (because I have no
	 * idea how to copy that mess) so that if user doesn't modify
	 * a filter with regex conditions they would be marked as
	 * compiled with regex_t struct being unallocated Drawback :
	 * even if user doesn't modify any regex, but press OK button,
	 * we'll recalculate all regex I guess we could be a bit
	 * smarter without too much gymnastic. */

	FILTER_CLRFLAG(cpfil,FILTER_COMPILED);
	/* Copy conditions */
        if (fil->condition)
            /* Copy conditions */
            cpfil->condition = libbalsa_condition_ref(fil->condition);
        else
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Filter \"%s\" has no condition."),
                              fil->name);

	fe_collect_user_headers(fil->condition);

	cpfil->action=fil->action;
	if (fil->action_string) 
            cpfil->action_string=g_strdup(fil->action_string);	

        gtk_list_store_append(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter, 
                           0, cpfil->name, 1, cpfil, -1);
    }

    if (filter_errno!=FILTER_NOERR) {
	filter_perror(filter_strerror(filter_errno));
	gtk_widget_destroy(GTK_WIDGET(fe_window));
	return;
    }

    gtk_widget_show_all(GTK_WIDGET(fe_window));
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(fe_filters_list);
        gtk_tree_selection_select_iter(selection, &iter);
    }
}
