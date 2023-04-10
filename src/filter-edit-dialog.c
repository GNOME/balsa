/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
	g_signal_connect(combo_box, "changed", func, cb_data);
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
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

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

    gtk_widget_set_vexpand(sw, TRUE);
    gtk_widget_set_valign(sw, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(sw, 2);
    gtk_container_add(GTK_CONTAINER(vbox), sw);

    /* new and delete buttons */
    bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    gtk_container_add(GTK_CONTAINER(vbox), bbox);

    /* new button */
    /* Translators: button "New" filter */
    fe_new_button = libbalsa_add_mnemonic_button_to_box(C_("filter", "_New"), bbox, GTK_ALIGN_CENTER);
    g_signal_connect(fe_new_button, "clicked",
		     G_CALLBACK(fe_new_pressed), NULL);
    /* delete button */
    fe_delete_button = libbalsa_add_mnemonic_button_to_box(("_Delete"), bbox, GTK_ALIGN_CENTER);
    g_signal_connect(fe_delete_button, "clicked",
		     G_CALLBACK(fe_delete_pressed), NULL);
    gtk_widget_set_sensitive(fe_delete_button, FALSE);

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
    page = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(page), HIG_PADDING);
    gtk_grid_set_column_spacing(GTK_GRID(page), HIG_PADDING);
    libbalsa_set_margins(page, HIG_PADDING);

    /* The name entry */

    fe_name_label = gtk_label_new_with_mnemonic(_("_Filter name:"));
    gtk_widget_set_halign(fe_name_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(page), fe_name_label, 0, 0, 1, 1);
    fe_name_entry = gtk_entry_new();
    gtk_widget_set_hexpand(fe_name_entry, TRUE);
    gtk_entry_set_max_length(GTK_ENTRY(fe_name_entry),
                             FILTER_EDIT_ENTRY_MAX_LENGTH);
    gtk_grid_attach(GTK_GRID(page), fe_name_entry, 1, 0, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(fe_name_label), fe_name_entry);
    g_signal_connect(fe_name_entry, "changed",
                     G_CALLBACK(fe_action_changed), NULL);

    /* The filter op-code : "OR" or "AND" all the conditions */

    label = gtk_label_new(_("Operation between conditions:"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(page), label, 0, 1, 1, 1);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_grid_attach(GTK_GRID(page), box, 1, 1, 1, 1);

    fe_op_codes_option_menu = fe_build_option_menu(fe_op_codes,
						G_N_ELEMENTS(fe_op_codes),
						NULL, NULL);
    g_signal_connect(fe_op_codes_option_menu, "changed",
                     G_CALLBACK(fe_action_changed), NULL);
    libbalsa_set_vmargins(fe_op_codes_option_menu, 2);
    gtk_container_add(GTK_CONTAINER(box), fe_op_codes_option_menu);

    /* list of conditions defining how this filter matches */

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_grid_attach(GTK_GRID(page), scroll, 0, 2, 2, 1);

    fe_conditions_list =
        libbalsa_filter_list_new(TRUE, NULL, GTK_SELECTION_BROWSE, NULL,
                                 FALSE);
    g_signal_connect(fe_conditions_list, "row-activated",
                     G_CALLBACK(fe_conditions_row_activated), NULL);

    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(fe_conditions_list));

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_grid_attach(GTK_GRID(page), box, 0, 3, 2, 1);

    fe_condition_edit_button = libbalsa_add_mnemonic_button_to_box(_("_Edit"), box, GTK_ALIGN_START);
    gtk_widget_set_sensitive(fe_condition_edit_button,FALSE);
    g_signal_connect(fe_condition_edit_button, "clicked",
                     G_CALLBACK(fe_edit_condition), GINT_TO_POINTER(0));
    /* Translators: button "New" filter match */
    button = libbalsa_add_mnemonic_button_to_box(C_("filter match", "Ne_w"), box, GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked",
                     G_CALLBACK(fe_edit_condition), GINT_TO_POINTER(1));
    fe_condition_delete_button = libbalsa_add_mnemonic_button_to_box(_("_Remove"), box, GTK_ALIGN_END);
    gtk_widget_set_sensitive(fe_condition_delete_button,FALSE);
    g_signal_connect(fe_condition_delete_button, "clicked",
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
    GtkWidget *grid_widget;
    GtkGrid *grid;
    GdkRGBA rgba;

    grid_widget = gtk_grid_new();
    grid = GTK_GRID(grid_widget);
    gtk_grid_set_row_spacing(grid, HIG_PADDING);
    gtk_grid_set_column_spacing(grid, HIG_PADDING);

    fe_foreground_set = gtk_check_button_new_with_mnemonic(_("Foreground"));
    gtk_grid_attach(grid, fe_foreground_set, 0, 0, 1, 1);
    gdk_rgba_parse(&rgba, "black");
    fe_foreground = gtk_color_button_new_with_rgba(&rgba);
    gtk_widget_set_sensitive(fe_foreground, FALSE);
    gtk_grid_attach(grid, fe_foreground, 1, 0, 1, 1);
    g_signal_connect(fe_foreground_set, "toggled",
                     G_CALLBACK(fe_color_check_toggled), fe_foreground);
    g_signal_connect(fe_foreground, "color-set",
                     G_CALLBACK(fe_color_set), NULL);

    fe_background_set = gtk_check_button_new_with_mnemonic(_("Background"));
    gtk_grid_attach(grid, fe_background_set, 0, 1, 1, 1);
    gdk_rgba_parse(&rgba, "white");
    fe_background = gtk_color_button_new_with_rgba(&rgba);
    gtk_widget_set_sensitive(fe_background, FALSE);
    gtk_grid_attach(grid, fe_background, 1, 1, 1, 1);
    g_signal_connect(fe_background_set, "toggled",
                     G_CALLBACK(fe_color_check_toggled), fe_background);
    g_signal_connect(fe_background, "color-set",
                     G_CALLBACK(fe_color_set), NULL);

    return grid_widget;
}

static GtkWidget *
build_action_page(GtkWindow * window)
{
    GtkWidget *page, *frame, *grid;
    GtkWidget *box;
    GtkWidget *dialog;

    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, HIG_PADDING);
    gtk_box_set_homogeneous(GTK_BOX(page), TRUE);

    /* The notification area */

    frame = gtk_frame_new(_("Notification:"));
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(page), frame);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_grid_set_column_spacing(GTK_GRID(grid), HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(frame), grid);

    /* Notification buttons */
    fe_sound_button = gtk_check_button_new_with_label(_("Play sound:"));
    gtk_widget_set_hexpand(fe_sound_button, TRUE);
    gtk_grid_attach(GTK_GRID(grid), fe_sound_button, 0, 0, 1, 1);

    dialog =
        gtk_file_chooser_dialog_new(_("Use Sound…"), NULL,
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_Open"),   GTK_RESPONSE_ACCEPT,
                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    GTK_RESPONSE_ACCEPT);
    fe_sound_entry = gtk_file_chooser_button_new_with_dialog(dialog);
    gtk_widget_set_hexpand(fe_sound_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), fe_sound_entry, 1, 0, 1, 1);
    /* fe_sound_entry is initially sensitive, so to be consistent
     * we must make fe_sound_button active */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_sound_button), TRUE);
    g_signal_connect(fe_sound_button, "toggled",
                     G_CALLBACK(fe_button_toggled), fe_sound_entry);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(fe_sound_response), NULL);

    fe_popup_button = gtk_check_button_new_with_label(_("Pop-up text:"));
    gtk_widget_set_hexpand(fe_popup_button, TRUE);
    gtk_grid_attach(GTK_GRID(grid), fe_popup_button, 0, 1, 1, 1);
    fe_popup_entry = gtk_entry_new();
    gtk_widget_set_hexpand(fe_popup_entry, TRUE);
    gtk_entry_set_max_length(GTK_ENTRY(fe_popup_entry), 
                             FILTER_EDIT_ENTRY_MAX_LENGTH);
    gtk_grid_attach(GTK_GRID(grid), fe_popup_entry, 1, 1, 1, 1);
    /* fe_popup_entry is initially sensitive, so to be consistent
     * we must make fe_popup_button active */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fe_popup_button), TRUE);
    g_signal_connect(fe_popup_button, "toggled",
                     G_CALLBACK(fe_button_toggled), fe_popup_entry);
    g_signal_connect(fe_popup_entry, "changed",
                     G_CALLBACK(fe_action_changed), NULL);

    /* The action area */
    frame = gtk_frame_new(_("Action to perform:"));
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(page), frame);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_set_homogeneous(GTK_BOX(box), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_container_add(GTK_CONTAINER(frame), box);

    fe_action_option_menu =
        fe_build_option_menu(fe_actions, G_N_ELEMENTS(fe_actions),
                             G_CALLBACK(fe_action_selected), NULL);
    gtk_widget_set_vexpand(fe_action_option_menu, TRUE);
    libbalsa_set_vmargins(fe_action_option_menu, 1);
    gtk_container_add(GTK_CONTAINER(box), fe_action_option_menu);

    /* FIXME : we use the global mru folder list, perhaps we should use
       our own. We'll see this later, for now let's make something usable
       the old way was way too ugly and even unusable for users with zillions
       of mailboxes. Yes there are ;-)!
    */
    fe_mailboxes = balsa_mblist_mru_option_menu(window,
						&balsa_app.folder_mru);
    g_signal_connect(fe_mailboxes, "changed",
                     G_CALLBACK(fe_action_changed), NULL);
    gtk_widget_set_vexpand(fe_mailboxes, TRUE);
    libbalsa_set_vmargins(fe_mailboxes, 1);
    gtk_container_add(GTK_CONTAINER(box), fe_mailboxes);

    fe_color_buttons = fe_make_color_buttons();
    gtk_widget_set_vexpand(fe_color_buttons, TRUE);
    libbalsa_set_vmargins(fe_color_buttons, 1);
    gtk_container_add(GTK_CONTAINER(box), fe_color_buttons);

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

    rightside = gtk_box_new(GTK_ORIENTATION_VERTICAL, HIG_PADDING);

    /* the main notebook */
    notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_widget_set_valign(notebook, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(rightside), notebook);

    page = build_match_page();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     page, gtk_label_new(_("Match")));
    page = build_action_page(window);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     page, gtk_label_new(_("Action")));

    /* button box */
    bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(rightside), bbox);

    fe_apply_button = libbalsa_add_mnemonic_button_to_box(_("_Apply"), bbox, GTK_ALIGN_START);
    g_signal_connect(fe_apply_button, "clicked",
		     G_CALLBACK(fe_apply_pressed), NULL);

    fe_revert_button = libbalsa_add_mnemonic_button_to_box(_("Re_vert"), bbox, GTK_ALIGN_END);
    g_signal_connect(fe_revert_button, "clicked",
		     G_CALLBACK(fe_revert_pressed), NULL);
    gtk_widget_set_sensitive(fe_apply_button, FALSE);
    gtk_widget_set_sensitive(fe_revert_button, FALSE);

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
void
filters_edit_dialog(GtkWindow * parent)
{
    GtkWidget *content_area;
    GtkWidget *hbox;
    GtkWidget *piece;
    LibBalsaFilter * cpfil,* fil;
    GSList * filter_list;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (fr_dialogs_opened) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("A filter run dialog is open. "
                            "Close it before you can modify filters."));
	return;
    }
    if (fe_already_open) {
	gtk_window_present_with_time(GTK_WINDOW(fe_window),
                                     gtk_get_current_event_time());
	return;
    }
    
    fe_already_open=TRUE;

    piece = build_left_side();

    fe_window = gtk_dialog_new_with_buttons(_("Filters"),
                                            parent,
                                            libbalsa_dialog_flags(),
                                            _("_OK"), GTK_RESPONSE_OK,
                                            _("_Cancel"), GTK_RESPONSE_CANCEL,
                                            _("_Help"), GTK_RESPONSE_HELP,
					    NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(fe_window));

    g_signal_connect(fe_window, "response",
                     G_CALLBACK(fe_dialog_response), NULL);
    g_signal_connect(fe_window, "destroy",
	             G_CALLBACK(fe_destroy_window_cb), NULL);

    gtk_window_set_role(GTK_WINDOW (fe_window), "filter-edit");
    gtk_dialog_set_response_sensitive(GTK_DIALOG(fe_window),
                                      GTK_RESPONSE_OK, FALSE);

    /* main hbox */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, HIG_PADDING);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_widget_set_valign(hbox, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(hbox, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(content_area), hbox);

    gtk_widget_set_hexpand(piece, FALSE);
    libbalsa_set_hmargins(piece, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(hbox), piece);

    gtk_container_add(GTK_CONTAINER(hbox),
                      gtk_separator_new(GTK_ORIENTATION_VERTICAL));

    fe_right_page = build_right_side(GTK_WINDOW(fe_window));
    gtk_widget_set_sensitive(fe_right_page, FALSE);

    gtk_widget_set_hexpand(fe_right_page, TRUE);
    gtk_widget_set_halign(fe_right_page, GTK_ALIGN_FILL);
    libbalsa_set_hmargins(fe_right_page, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(hbox), fe_right_page);

    fe_user_headers_list = NULL;

    /* Populate the list of filters */
    model = gtk_tree_view_get_model(fe_filters_list);
    for (filter_list = balsa_app.filters;
         filter_list != NULL;
         filter_list = filter_list->next) {

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
                              _("Filter “%s” has no condition."),
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
