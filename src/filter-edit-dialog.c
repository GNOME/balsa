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

#include "config.h"

#include <gnome.h>
#include "balsa-app.h"
#include "filter-edit.h"
#include "filter-funcs.h"
#include "message.h"

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

GnomeDialog * fe_window;

GtkCList * fe_filters_list;

gboolean fe_already_open=FALSE;

/* containers for radiobuttons */
GtkWidget *fe_op_codes_option_menu;

/* Name field */
GtkWidget *fe_name_label;
GtkWidget *fe_name_entry;

/* widget for the conditions */
GtkCList *fe_conditions_list;

/* notification field */
GtkWidget *fe_sound_button;
GtkWidget *fe_sound_entry;
GtkWidget *fe_popup_button;
GtkWidget *fe_popup_entry;

/* action field */
GtkWidget *fe_action_option_menu;

/* Mailboxes option menu */
GtkWidget * fe_mailboxes;

GtkWidget* fe_right_page;

/* Different buttons that need to be greyed or ungreyed */
GtkWidget * fe_delete_button,* fe_apply_button,* fe_revert_button;
GtkWidget * fe_condition_delete_button,* fe_condition_edit_button;

/* ******************************** */

option_list fe_search_type[] = {
    {N_("Simple"), CONDITION_SIMPLE, NULL},
    {N_("Regular Expression"), CONDITION_REGEX, NULL},
    {N_("Date interval"),CONDITION_DATE,NULL},
    {N_("Flag condition"),CONDITION_FLAG,NULL}
};

option_list fe_actions[] = {
    {N_("Copy to folder:"), FILTER_COPY, NULL},
    {N_("Move to folder:"), FILTER_MOVE, NULL},
    {N_("Print on printer:"), FILTER_PRINT, NULL},
    {N_("Run program:"), FILTER_RUN, NULL},
    {N_("Send to Trash"), FILTER_TRASH, NULL}
};

option_list fe_op_codes[] = {
    {N_("OR"), FILTER_OP_OR, NULL},
    {N_("AND"), FILTER_OP_AND, NULL}
};

/* ******************************** */
void
fe_enable_right_page(gboolean enabled)
{
    gtk_widget_set_sensitive(fe_right_page, enabled);
}

GtkWidget *
build_option_menu(option_list options[], gint num, GtkSignalFunc func);

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

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

GtkWidget *
build_option_menu(option_list options[], gint num, GtkSignalFunc func)
{
    GtkWidget *option_menu;
    GtkWidget *menu;
    int i;

    if (num < 1)
	return (NULL);

    menu = gtk_menu_new();

    for (i = 0; i < num; i++) {
	options[i].widget =
	    gtk_menu_item_new_with_label(_(options[i].text));
	gtk_object_set_data(GTK_OBJECT(options[i].widget), "value",
			    GINT_TO_POINTER(options[i].value));

	gtk_menu_append(GTK_MENU(menu), options[i].widget);
	if (func)
	    gtk_signal_connect(GTK_OBJECT(options[i].widget),
			       "activate",
			       func, GINT_TO_POINTER(options[i].value));
	gtk_widget_show(options[i].widget);
    }

    option_menu = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu), 0);

    return (option_menu);
}				/* end build_option_menu */

/* Free filters associated with clist row */
void
fe_free_associated_filters(void)
{
    gint row;

    for (row=0;row<fe_filters_list->rows;row++)
	libbalsa_filter_free((LibBalsaFilter*)
                             gtk_clist_get_row_data(fe_filters_list,row),
                             GINT_TO_POINTER(TRUE));
}

void
fe_free_associated_conditions(void)
{
    gint row;

    for (row=0; row<fe_conditions_list->rows; row++)
	libbalsa_condition_free((LibBalsaCondition *)
                                gtk_clist_get_row_data(fe_conditions_list,row));
}

static void
fe_clist_unselect_row(GtkWidget * widget, gint row, gint column, 
                      GdkEventButton *event, gpointer data)
{
    /* unselecting the only row means it is about to be deleted */
    printf("unselect_row\n"); 
    if(fe_filters_list->rows<=1) 
        fe_enable_right_page(FALSE);
}

/*
 * build_left_side()
 *
 * Builds the left side of the dialog
 */
static GtkWidget *
build_left_side(void)
{
    GtkWidget *vbox, *bbox, *button;
    GtkWidget *pixmap;

    GtkWidget *sw;

    static gchar *titles[] = { N_("Name") };

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

    /* the clist */
    gtk_widget_push_visual(gdk_rgb_get_visual());
    gtk_widget_push_colormap(gdk_rgb_get_cmap());

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

#ifdef ENABLE_NLS
    titles[0] = _(titles[0]);
#endif
    fe_filters_list = GTK_CLIST(gtk_clist_new_with_titles(1, titles));

    gtk_widget_pop_colormap();
    gtk_widget_pop_visual();

    gtk_clist_set_selection_mode(fe_filters_list, GTK_SELECTION_BROWSE);
    gtk_clist_set_row_height(fe_filters_list, 0);
    gtk_clist_column_titles_passive(fe_filters_list);
    gtk_clist_set_sort_column(fe_filters_list,0);
    gtk_clist_set_sort_type(fe_filters_list,GTK_SORT_ASCENDING);
    gtk_clist_set_auto_sort(fe_filters_list,TRUE);
    gtk_signal_connect(GTK_OBJECT(fe_filters_list), "select_row",
		       GTK_SIGNAL_FUNC(fe_clist_select_row), NULL);
    gtk_signal_connect(GTK_OBJECT(fe_filters_list), "unselect_row",
		       GTK_SIGNAL_FUNC(fe_clist_unselect_row), NULL);

    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(fe_filters_list));

    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 2);

    /* new and delete buttons */
    bbox = gtk_hbutton_box_new();
    gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 2);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_SPREAD);
    gtk_button_box_set_child_size(GTK_BUTTON_BOX(bbox),
				  BALSA_BUTTON_WIDTH / 2,
				  BALSA_BUTTON_HEIGHT / 2);

    gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 2);

    /* new button */
    pixmap = gnome_stock_new_with_icon(GNOME_STOCK_MENU_NEW);
    button = gnome_pixmap_button(pixmap, _("New"));
    gtk_signal_connect(GTK_OBJECT(button), "clicked",
		       GTK_SIGNAL_FUNC(fe_new_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), button);
    /* delete button */
    pixmap = gnome_stock_new_with_icon(GNOME_STOCK_MENU_TRASH);
    fe_delete_button = gnome_pixmap_button(pixmap, _("Delete"));
    gtk_signal_connect(GTK_OBJECT(fe_delete_button), "clicked",
		       GTK_SIGNAL_FUNC(fe_delete_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), fe_delete_button);
    gtk_widget_set_sensitive(fe_delete_button,FALSE);

    return vbox;
}				/* end build_left_side() */

/* Used to populate mailboxes option menu */

static void
add_mailbox_to_option_menu(GtkCTree * ctree,GtkCTreeNode *node,gpointer menu)
{
    GtkWidget * item;
    BalsaMailboxNode *mbnode = gtk_ctree_node_get_row_data(ctree, node);

    if (mbnode->mailbox) {
	/* OK this node is a mailbox */
	item = gtk_menu_item_new_with_label(mbnode->mailbox->name);
	gtk_object_set_data(GTK_OBJECT(item), "mailbox",
			    mbnode->mailbox);
	
	gtk_menu_append(GTK_MENU(menu), item);
	gtk_widget_show(item);
    }
}
/*
 * build_match_page()
 *
 * Builds the "Match" page of the main notebook
 */
static GtkWidget *
build_match_page()
{
    GtkWidget *page,*table,*button;
    GtkWidget *frame,*label,*scroll;
    GtkWidget *box = NULL;

    /* The notebook page */
    page = gtk_table_new(10, 15, FALSE);

    /* The name entry */

    fe_name_label = gtk_label_new(_("Filter name:"));
    gtk_table_attach(GTK_TABLE(page),
		     fe_name_label,
		     0, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_name_entry = gtk_entry_new_with_max_length(256);
    gtk_table_attach(GTK_TABLE(page),
		     fe_name_entry,
		     2, 10, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

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

    fe_op_codes_option_menu = build_option_menu(fe_op_codes,
						ELEMENTS(fe_op_codes),
						NULL);
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
    fe_conditions_list = GTK_CLIST(gtk_clist_new(1));

    gtk_clist_set_selection_mode(fe_conditions_list,GTK_SELECTION_BROWSE);
    gtk_clist_set_row_height(fe_conditions_list, 0);
    gtk_clist_set_reorderable(fe_conditions_list, FALSE);
    gtk_clist_set_use_drag_icons(fe_conditions_list, FALSE);

    gtk_signal_connect(GTK_OBJECT(fe_conditions_list), "select_row",
		       GTK_SIGNAL_FUNC(fe_conditions_select_row), NULL);

    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll),
					  GTK_WIDGET(fe_conditions_list));

    box = gtk_hbox_new(TRUE, 5);
    gtk_table_attach(GTK_TABLE(page),
		     box,
		     0, 5, 8, 9,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    fe_condition_edit_button = gtk_button_new_with_label(_("Edit"));
    gtk_widget_set_sensitive(fe_condition_edit_button,FALSE);
    gtk_box_pack_start(GTK_BOX(box), fe_condition_edit_button, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(fe_condition_edit_button),
		       "clicked", GTK_SIGNAL_FUNC(fe_edit_condition), 
                       GINT_TO_POINTER(0));
    button = gtk_button_new_with_label(_("New"));
    gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(button),
		       "clicked", GTK_SIGNAL_FUNC(fe_edit_condition), 
                       GINT_TO_POINTER(1));
    fe_condition_delete_button = gtk_button_new_with_label(_("Remove"));
    gtk_widget_set_sensitive(fe_condition_delete_button,FALSE);
    gtk_box_pack_start(GTK_BOX(box), fe_condition_delete_button, TRUE, 
                       TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(fe_condition_delete_button),
		       "clicked",
		       GTK_SIGNAL_FUNC(fe_condition_remove_pressed), NULL);

    return page;
}				/* end build_match_page() */


/*
 * build_action_page()
 *
 * Builds the "Action" page of the main notebook
 */
static GtkWidget *
build_action_page()
{
    GtkWidget *page, *frame, *table;
    GtkWidget *box = NULL;
    GtkWidget *menu,* item;
    GtkCTreeNode * node;
    gint i;

    page = gtk_vbox_new(TRUE, 5);

    /* The notification area */

    frame = gtk_frame_new(_("Notification:"));
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 2);

    table = gtk_table_new(2, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);

    /* Notification buttons */
    fe_sound_button = gtk_check_button_new_with_label(_("Play sound:"));
    gtk_table_attach(GTK_TABLE(table), fe_sound_button,
		     0, 1, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    fe_sound_entry =
	gnome_file_entry_new("filter_sounds", _("Use Sound..."));
    gtk_table_attach(GTK_TABLE(table), fe_sound_entry, 1, 2, 0, 1,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    fe_popup_button = gtk_check_button_new_with_label(_("Popup text:"));
    gtk_table_attach(GTK_TABLE(table),
		     fe_popup_button,
		     0, 1, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    fe_popup_entry = gtk_entry_new_with_max_length(255);
    gtk_table_attach(GTK_TABLE(table),
		     fe_popup_entry,
		     1, 2, 1, 2,
		     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);

    /* The action area */
    frame = gtk_frame_new(_("Action to perform:"));
    gtk_frame_set_label_align(GTK_FRAME(frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 2);

    box = gtk_vbox_new(TRUE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_container_add(GTK_CONTAINER(frame), box);

    fe_action_option_menu = build_option_menu(fe_actions,
					      ELEMENTS(fe_actions),
					      GTK_SIGNAL_FUNC
					      (fe_action_selected));
    gtk_box_pack_start(GTK_BOX(box), fe_action_option_menu, TRUE, FALSE,
		       1);

    /* We populate the option menu with mailboxes name
     * FIXME : This is done once for all, but user could
     * remove or add mailboxes behind us : we should connect
     * ourselves to signals to refresh the options in these cases
     */

    menu = gtk_menu_new();
    for(node=gtk_ctree_node_nth(GTK_CTREE(balsa_app.mblist), 0);
	node; 
	node=GTK_CTREE_NODE_NEXT(node))
	gtk_ctree_pre_recursive(GTK_CTREE(balsa_app.mblist), 
				node,
				add_mailbox_to_option_menu,
				menu);
    fe_mailboxes = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(fe_mailboxes), menu);
    gtk_option_menu_set_history(GTK_OPTION_MENU(fe_mailboxes), 0);
    gtk_box_pack_start(GTK_BOX(box), fe_mailboxes, TRUE, FALSE, 1);

    return page;
}				/* end build_action_page() */


/*
 * build_right_side()
 *
 * Builds the right side of the dialog
 */
static GtkWidget *
build_right_side(void)
{
    GtkWidget *rightside;
    GtkWidget *notebook, *page;
    GtkWidget *bbox, *pixmap;

    rightside = gtk_vbox_new(FALSE, 0);

    /* the main notebook */
    notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(rightside), notebook, FALSE, FALSE, 0);

    page = build_match_page();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     page, gtk_label_new(_("Match")));
    page = build_action_page();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
			     page, gtk_label_new(_("Action")));

    /* button box */
    bbox = gtk_hbutton_box_new();
    gtk_box_pack_start(GTK_BOX(rightside), bbox, FALSE, FALSE, 0);

    fe_apply_button = gnome_stock_button(GNOME_STOCK_BUTTON_APPLY);
    gtk_signal_connect(GTK_OBJECT(fe_apply_button),
		       "clicked",
		       GTK_SIGNAL_FUNC(fe_apply_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), fe_apply_button);

    pixmap = gnome_stock_new_with_icon(GNOME_STOCK_MENU_UNDO);
    fe_revert_button = gnome_pixmap_button(pixmap, _("Revert"));
    gtk_signal_connect(GTK_OBJECT(fe_revert_button),
		       "clicked",
		       GTK_SIGNAL_FUNC(fe_revert_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(bbox), fe_revert_button);
    gtk_widget_set_sensitive(fe_apply_button,FALSE);
    gtk_widget_set_sensitive(fe_revert_button,FALSE);

    return rightside;
}				/* end build_right_side() */


/*
 * filters_edit_dialog()
 *
 * Returns immediately, but fires off the filter edit dialog.
 */
void
filters_edit_dialog(void)
{
    GtkWidget *hbox;
    GtkWidget *piece;
    GtkWidget *sep;
    gint row;
    LibBalsaFilter * cpfil,* fil;
    GSList * cnds,* filter_list;

    if (fr_dialogs_opened) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("A filter run dialog is open."
                            "Close it before you can modify filters."));
	return;
    }
    if (fe_already_open) {
	gdk_window_raise(GTK_WIDGET(fe_window)->window);
	return;
    }
    
    fe_already_open=TRUE;

    piece = build_left_side();

    fe_window = GNOME_DIALOG(gnome_dialog_new(_("Balsa Filters"),
					      GNOME_STOCK_BUTTON_OK,
					      GNOME_STOCK_BUTTON_CANCEL,
					      GNOME_STOCK_BUTTON_HELP, NULL));

    gtk_signal_connect(GTK_OBJECT(fe_window),
		       "clicked", fe_dialog_button_clicked, NULL);
    gtk_signal_connect(GTK_OBJECT(fe_window), "destroy",
		       GTK_SIGNAL_FUNC(fe_destroy_window_cb), NULL);
    /* main hbox */
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fe_window->vbox),
		       hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), piece, FALSE, FALSE, 2);

    sep = gtk_vseparator_new();
    gtk_box_pack_start(GTK_BOX(hbox), sep, FALSE, FALSE, 2);

    fe_right_page = piece = build_right_side();
    gtk_widget_set_sensitive(fe_right_page, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), piece, FALSE, FALSE, 2);

    /* Populate the clist of filters */

    for(filter_list=balsa_app.filters; 
        filter_list; filter_list=g_slist_next(filter_list)) {

	fil=(LibBalsaFilter*)filter_list->data;
	/* Make a copy of the current filter */
	cpfil=libbalsa_filter_new();
	
	cpfil->name=g_strdup(fil->name);
	cpfil->flags=fil->flags;
	if (fil->sound) cpfil->sound=g_strdup(fil->sound);
	if (fil->popup_text) cpfil->popup_text=g_strdup(fil->popup_text);
	cpfil->conditions_op=fil->conditions_op;
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
      	for (cnds=fil->conditions; cnds; cnds=g_slist_next(cnds)) {
            LibBalsaCondition *c = (LibBalsaCondition*)cnds->data;
	    cpfil->conditions = 
                g_slist_prepend(cpfil->conditions,libbalsa_condition_clone(c));
        }
	cpfil->conditions=g_slist_reverse(cpfil->conditions);

	cpfil->action=fil->action;
	if (fil->action_string) 
            cpfil->action_string=g_strdup(fil->action_string);	

	row=gtk_clist_append(fe_filters_list,&(cpfil->name));
	
	/* We associate the data with the newly appended row */
	gtk_clist_set_row_data(fe_filters_list,row,(gpointer)cpfil);
    }

    if (filter_errno!=FILTER_NOERR) {
	filter_perror(filter_strerror(filter_errno));
	gnome_dialog_close(fe_window);
	return;
    }

    gtk_widget_show_all(GTK_WIDGET(fe_window));
    if (fe_filters_list->rows)
	gtk_clist_select_row(fe_filters_list,0,-1);
}
