/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Joel Becker and Stuart Parmenter
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
#include "filter.h"
#include "filter-edit.h"

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

static GtkWidget *
build_option_menu (option_list options[],
		   gint num,
		   GtkSignalFunc func)
{
    GtkWidget *option_menu;
    GtkWidget *menu;
    GSList *group;
    int i;

    if (num < 1)
        return (NULL);

    menu = gtk_menu_new ();
    group = NULL;

    for (i = 0; i < num; i++)
    {
        options[i].widget = gtk_radio_menu_item_new_with_label (group, options[i].text);
        gtk_object_set_data(GTK_OBJECT(options[i].widget),
                            "value",
                            GINT_TO_POINTER(options[i].value));

        group = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (options[0].widget));
        gtk_menu_append (GTK_MENU (menu), options[i].widget);
        if (func)
            gtk_signal_connect (GTK_OBJECT (options[i].widget),
                                "toggled",
                                func,
                                GINT_TO_POINTER (options[i].value));
        gtk_widget_show (options[i].widget);
    }

    option_menu = gtk_option_menu_new ();
    gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
    gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), 0);

    return (option_menu);
}				/* end build_option_menu */


/*
 * build_left_side()
 *
 * Builds the left side of the dialog
 */
static GtkWidget *
build_left_side (GtkWidget **clist_ptr)
{
    GtkWidget *vbox, *bbox, *button;
    GtkWidget *pixmap;

    GtkWidget *sw, *clist;

    static gchar *titles[] =
    {
        N_("On"),
        N_("Name"),
    };

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
    vbox = gtk_vbox_new (FALSE, 2);

    /* the clist */
    gtk_widget_push_visual (gdk_imlib_get_visual ());
    gtk_widget_push_colormap (gdk_imlib_get_colormap ());

    sw = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);

    clist = gtk_clist_new_with_titles (2, titles);

    gtk_widget_pop_colormap ();
    gtk_widget_pop_visual ();

    gtk_clist_set_selection_mode (GTK_CLIST (clist), GTK_SELECTION_SINGLE);
    gtk_clist_set_column_justification (GTK_CLIST (clist),
                                        0, GTK_JUSTIFY_CENTER);
    gtk_clist_set_column_width (GTK_CLIST (clist), 0, 16);
    gtk_clist_set_row_height (GTK_CLIST (clist), 16);
    gtk_clist_set_reorderable (GTK_CLIST (clist), TRUE);
    gtk_clist_set_use_drag_icons (GTK_CLIST (clist), FALSE);

    gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (clist));
    gtk_signal_connect(GTK_OBJECT(clist),
                       "button_press_event",
                       GTK_SIGNAL_FUNC(clist_button_event_press),
                       NULL);

    gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 2);



    /* up down arrow buttons */
    bbox = gtk_hbutton_box_new ();
    gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 2);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
    gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
                                   BALSA_BUTTON_WIDTH/2, BALSA_BUTTON_HEIGHT/2);

    gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 2);

    /* up button */
    pixmap = gnome_stock_new_with_icon (GNOME_STOCK_MENU_UP);
    button = gnome_pixmap_button (pixmap, _("Up"));
    gtk_signal_connect (GTK_OBJECT (button), "clicked",
                        GTK_SIGNAL_FUNC (fe_up_pressed), clist);
    gtk_container_add (GTK_CONTAINER (bbox), button);
    /* down button */
    pixmap = gnome_stock_new_with_icon (GNOME_STOCK_MENU_DOWN);
    button = gnome_pixmap_button (pixmap, _("Down"));
    gtk_signal_connect (GTK_OBJECT (button), "clicked",
                        GTK_SIGNAL_FUNC (fe_down_pressed), clist);
    gtk_container_add (GTK_CONTAINER (bbox), button);


    /* new and delete buttons */
    bbox = gtk_hbutton_box_new ();
    gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 2);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
    gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
                                   BALSA_BUTTON_WIDTH/2, BALSA_BUTTON_HEIGHT/2);

    gtk_box_pack_start (GTK_BOX (vbox), bbox, FALSE, FALSE, 2);

    /* new button */
    pixmap = gnome_stock_new_with_icon (GNOME_STOCK_MENU_NEW);
    button = gnome_pixmap_button (pixmap, _("New"));
    gtk_signal_connect (GTK_OBJECT (button), "clicked",
                        GTK_SIGNAL_FUNC (fe_new_pressed), clist);
    gtk_container_add (GTK_CONTAINER (bbox), button);
    /* delete button */
    pixmap = gnome_stock_new_with_icon (GNOME_STOCK_MENU_TRASH);
    button = gnome_pixmap_button (pixmap, _("Delete"));
    gtk_signal_connect (GTK_OBJECT (button), "clicked",
                        GTK_SIGNAL_FUNC (fe_delete_pressed), clist);
    gtk_container_add (GTK_CONTAINER (bbox), button);

    gtk_widget_show_all (vbox);

    *clist_ptr = clist;

    return vbox;
}				/* end build_left_side() */


/*
 * build_type_notebook()
 *
 * builds the "Search Type" notebook on the "Match" page
 */
static void 
build_type_notebook ()
{
    GtkWidget *page;
    GtkWidget *frame;
    GtkWidget *table;
    GtkWidget *scroll;
    GtkWidget *box;
    GtkWidget *button;

    /* The notebook */
    fe_type_notebook = gtk_notebook_new ();
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (fe_type_notebook),
                                FALSE);
    gtk_notebook_set_show_border (GTK_NOTEBOOK (fe_type_notebook),
                                  FALSE);
    gtk_widget_show (fe_type_notebook);

  /* The simple page of the type notebook */
    page = gtk_table_new (5, 5, FALSE);
    gtk_notebook_append_page (GTK_NOTEBOOK (fe_type_notebook),
                              page,
                              NULL);
    gtk_widget_show (page);

    frame = gtk_frame_new (_("Match in:"));
    gtk_frame_set_label_align (GTK_FRAME (frame),
                               GTK_POS_LEFT,
                               GTK_POS_TOP);
    gtk_frame_set_shadow_type (GTK_FRAME(frame),
                               GTK_SHADOW_ETCHED_IN);
    gtk_table_attach (GTK_TABLE (page),
                      frame,
                      0, 5, 0, 4,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_FILL | GTK_SHRINK,
                      5, 5);
    gtk_widget_show (frame);
    table = gtk_table_new (3, 3, TRUE);
    gtk_container_add (GTK_CONTAINER (frame),
                       table);
    gtk_widget_show (table);

    fe_type_simple_all = gtk_check_button_new_with_label (_("All"));
    gtk_table_attach (GTK_TABLE (table),
                      fe_type_simple_all,
                      0, 1, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_signal_connect (GTK_OBJECT (fe_type_simple_all),
                        "toggled",
                        GTK_SIGNAL_FUNC (fe_type_simple_toggled),
                        GINT_TO_POINTER (1));
    gtk_widget_show (fe_type_simple_all);
    fe_type_simple_header = gtk_check_button_new_with_label (_("Header"));
    gtk_table_attach (GTK_TABLE (table),
                      fe_type_simple_header,
                      1, 2, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_signal_connect (GTK_OBJECT (fe_type_simple_header),
                        "toggled",
                        GTK_SIGNAL_FUNC (fe_type_simple_toggled),
                        GINT_TO_POINTER (2));
    gtk_widget_show (fe_type_simple_header);
    fe_type_simple_body = gtk_check_button_new_with_label (_("Body"));
    gtk_table_attach (GTK_TABLE (table),
                      fe_type_simple_body,
                      1, 2, 1, 2,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_signal_connect (GTK_OBJECT (fe_type_simple_body),
                        "toggled",
                        GTK_SIGNAL_FUNC (fe_type_simple_toggled),
                        GINT_TO_POINTER (3));
    gtk_widget_show (fe_type_simple_body);
    fe_type_simple_to = gtk_check_button_new_with_label (_("To:"));
    gtk_table_attach (GTK_TABLE (table),
                      fe_type_simple_to,
                      2, 3, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_signal_connect (GTK_OBJECT (fe_type_simple_to),
                        "toggled",
                        GTK_SIGNAL_FUNC (fe_type_simple_toggled),
                        GINT_TO_POINTER (4));
    gtk_widget_show (fe_type_simple_to);
    fe_type_simple_from = gtk_check_button_new_with_label (_("From:"));
    gtk_table_attach (GTK_TABLE (table),
                      fe_type_simple_from,
                      2, 3, 1, 2,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_signal_connect (GTK_OBJECT (fe_type_simple_from),
                        "toggled",
                        GTK_SIGNAL_FUNC (fe_type_simple_toggled),
                        GINT_TO_POINTER (5));
    gtk_widget_show (fe_type_simple_from);
    fe_type_simple_subject = gtk_check_button_new_with_label (_("Subject"));
    gtk_table_attach (GTK_TABLE (table),
                      fe_type_simple_subject,
                      2, 3, 2, 3,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_signal_connect (GTK_OBJECT (fe_type_simple_subject),
                        "toggled",
                        GTK_SIGNAL_FUNC (fe_type_simple_toggled),
                        GINT_TO_POINTER (6));
    gtk_widget_show (fe_type_simple_subject);

    fe_type_simple_label = gtk_label_new (_("Match string:"));
    gtk_table_attach (GTK_TABLE (page),
                      fe_type_simple_label,
                      0, 1, 4, 5,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_type_simple_label);
    fe_type_simple_entry = gtk_entry_new_with_max_length (1023);
    gtk_table_attach (GTK_TABLE (page),
                      fe_type_simple_entry,
                      1, 5, 4, 5,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_type_simple_entry);

  /* The regex page of the type notebook */

    page = gtk_table_new (5, 5, FALSE);
    gtk_notebook_append_page (GTK_NOTEBOOK (fe_type_notebook),
                              page,
                              NULL);
    gtk_widget_show (page);

    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    gtk_table_attach (GTK_TABLE (page),
                      scroll,
                      0, 5, 0, 3,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      2, 2);
    gtk_widget_show (scroll);
    fe_type_regex_list = gtk_list_new ();
    gtk_list_set_selection_mode (GTK_LIST (fe_type_regex_list),
                                 GTK_SELECTION_SINGLE);
    gtk_scrolled_window_add_with_viewport (
        GTK_SCROLLED_WINDOW (scroll),
        fe_type_regex_list);
    gtk_widget_show (fe_type_regex_list);

    box = gtk_hbox_new (TRUE, 5);
    gtk_table_attach (GTK_TABLE (page),
                      box,
                      0, 5, 3, 4,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_widget_show (box);
    button = gtk_button_new_with_label (_("Add"));
    gtk_box_pack_start (GTK_BOX (box),
                        button,
                        TRUE,
                        TRUE,
                        0);
    gtk_signal_connect (GTK_OBJECT (button),
                        "clicked",
                        GTK_SIGNAL_FUNC (fe_add_pressed),
                        NULL);
    gtk_widget_show (button);
    button = gtk_button_new_with_label (_("Remove"));
    gtk_box_pack_start (GTK_BOX (box),
                        button,
                        TRUE,
                        TRUE,
                        0);
    gtk_signal_connect (GTK_OBJECT (button),
                        "clicked",
                        GTK_SIGNAL_FUNC (fe_remove_pressed),
                        NULL);
    gtk_widget_show (button);

    fe_type_regex_entry = gtk_entry_new_with_max_length (1023);
    gtk_table_attach (GTK_TABLE (page),
                      fe_type_regex_entry,
                      0, 5, 4, 5,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      2, 2);
    gtk_widget_show (fe_type_regex_entry);

  /* The exec page of the type notebook */

    page = gtk_table_new (5, 5, FALSE);
    gtk_notebook_append_page (GTK_NOTEBOOK (fe_type_notebook),
                              page,
                              NULL);
    gtk_widget_show (page);

    fe_type_exec_label = gtk_label_new (_("Command:"));
    gtk_table_attach (GTK_TABLE (page),
                      fe_type_exec_label,
                      0, 1, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_type_exec_label);
    fe_type_exec_entry = gtk_entry_new_with_max_length (1023);
    gtk_table_attach (GTK_TABLE (page),
                      fe_type_exec_entry,
                      1, 5, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_type_exec_entry);
}				/* end build_type_notebook() */


/*
 * build_match_page()
 *
 * Builds the "Match" page of the main notebook
 */
static GtkWidget *
build_match_page ()
{
    GtkWidget *page;
    GtkWidget *frame;
    GtkWidget *box = NULL;

    /* The notebook page */
    page = gtk_table_new (10, 10, FALSE);

  /* The name entry */

    fe_name_label = gtk_label_new (_("Filter name:"));
    gtk_table_attach (GTK_TABLE (page),
                      fe_name_label,
                      0, 2, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_name_label);
    fe_name_entry = gtk_entry_new_with_max_length (256);
    gtk_table_attach (GTK_TABLE (page),
                      fe_name_entry,
                      2, 10, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_name_entry);

  /* The "Process when:" option menu */

    frame = gtk_frame_new (_("Process when:"));
    gtk_frame_set_label_align (GTK_FRAME (frame),
                               GTK_POS_LEFT,
                               GTK_POS_TOP);
    gtk_frame_set_shadow_type (GTK_FRAME (frame),
                               GTK_SHADOW_ETCHED_IN);
    gtk_table_attach (GTK_TABLE (page),
                      frame,
                      5, 10, 1, 2,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (frame);
    box = gtk_vbox_new (TRUE, 5);
    gtk_container_add (GTK_CONTAINER (frame),
                       box);
    gtk_widget_show (box);

    fe_when_option_menu = build_option_menu (fe_process_when,
                                             ELEMENTS(fe_process_when),
                                             NULL);
    gtk_box_pack_start (GTK_BOX (box),
                        fe_when_option_menu,
                        TRUE,
                        FALSE,
                        5);
    gtk_widget_show (fe_when_option_menu);


  /* The "Run on:" option menu */

    frame = gtk_frame_new (_("Run on:"));
    gtk_frame_set_label_align (GTK_FRAME (frame),
                               GTK_POS_LEFT,
                               GTK_POS_TOP);
    gtk_frame_set_shadow_type (GTK_FRAME (frame),
                               GTK_SHADOW_ETCHED_IN);
    gtk_table_attach (GTK_TABLE (page),
                      frame,
                      0, 5, 1, 2,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (frame);
    box = gtk_vbox_new (TRUE, 5);
    gtk_container_add (GTK_CONTAINER (frame),
                       box);
    gtk_widget_show (box);

    fe_group_option_menu = build_option_menu (fe_run_on,
                                              ELEMENTS(fe_run_on),
                                              NULL);
    gtk_box_pack_start (GTK_BOX (box),
                        fe_group_option_menu,
                        TRUE,
                        FALSE,
                        5);
    gtk_widget_show (fe_group_option_menu);

  /* the type notebook's option menu */

    frame = gtk_frame_new (_("Search type:"));
    gtk_frame_set_label_align (GTK_FRAME (frame),
                               GTK_POS_LEFT,
                               GTK_POS_TOP);
    gtk_frame_set_shadow_type (GTK_FRAME (frame),
                               GTK_SHADOW_ETCHED_IN);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 5);
    gtk_table_attach (GTK_TABLE (page),
                      frame,
                      0, 10, 5, 6,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (frame);
    box = gtk_hbox_new (FALSE, 5);
    gtk_container_add (GTK_CONTAINER (frame),
                       box);
    gtk_widget_show (box);

    fe_search_option_menu = build_option_menu (fe_search_type,
                                               ELEMENTS(fe_search_type),
                                               GTK_SIGNAL_FUNC (fe_checkbutton_toggled));
    gtk_box_pack_start (GTK_BOX (box),
                        fe_search_option_menu,
                        FALSE,
                        FALSE,
                        5);
    gtk_widget_show (fe_search_option_menu);

    build_type_notebook ();
    gtk_table_attach (GTK_TABLE (page),
                      fe_type_notebook,
                      0, 10, 6, 10,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      5, 5);
    return page;
}				/* end build_match_page() */


/*
 * build_action_page()
 *
 * Builds the "Action" page of the main notebook
 */
static GtkWidget *
build_action_page ()
{
    GtkWidget *page, *frame, *table;

    GtkWidget *box = NULL;
 
    page = gtk_vbox_new (TRUE, 5);

    /* The notification area */

    frame = gtk_frame_new (_("Notification:"));
    gtk_frame_set_label_align (GTK_FRAME (frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start (GTK_BOX (page), frame, FALSE, FALSE, 2);

    table = gtk_table_new (2, 2, FALSE);
    gtk_container_add (GTK_CONTAINER (frame), table);

    /* Notification buttons */
    fe_sound_button = gtk_check_button_new_with_label (_("Play sound:"));
    gtk_table_attach (GTK_TABLE (table), fe_sound_button,
                      0, 1, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show(fe_sound_button);

    fe_sound_entry = gnome_file_entry_new("filter_sounds", _("Use Sound..."));
    gtk_table_attach (GTK_TABLE (table),
                      fe_sound_entry,
                      1, 2, 0, 1,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_sound_entry);

    fe_popup_button = gtk_check_button_new_with_label (_("Popup text:"));
    gtk_table_attach (GTK_TABLE (table),
                      fe_popup_button,
                      0, 1, 1, 2,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_popup_button);
    fe_popup_entry = gtk_entry_new_with_max_length (255);
    gtk_table_attach (GTK_TABLE (table),
                      fe_popup_entry,
                      1, 2, 1, 2,
                      GTK_FILL | GTK_SHRINK | GTK_EXPAND,
                      GTK_SHRINK,
                      5, 5);
    gtk_widget_show (fe_popup_entry);


    /* The action area */
    frame = gtk_frame_new (_("Action to perform:"));
    gtk_frame_set_label_align (GTK_FRAME (frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start (GTK_BOX (page), frame, FALSE, FALSE, 2);

    box = gtk_vbox_new(TRUE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(frame), 3);
    gtk_container_add(GTK_CONTAINER(frame), box);
    gtk_widget_show(box);

    fe_action_option_menu = build_option_menu (fe_actions,
                                               ELEMENTS(fe_actions),
                                               GTK_SIGNAL_FUNC (fe_action_selected));
    gtk_box_pack_start(GTK_BOX(box), fe_action_option_menu, TRUE, FALSE, 1);
    gtk_widget_show (fe_action_option_menu);
    fe_action_entry = gtk_entry_new_with_max_length (1023);
    gtk_box_pack_start(GTK_BOX(box), fe_action_entry, TRUE, FALSE, 1);
    gtk_widget_show (fe_action_entry);

    /* The disposition area */

    frame = gtk_frame_new (_("Disposition:"));
    gtk_frame_set_label_align (GTK_FRAME (frame), GTK_POS_LEFT, GTK_POS_TOP);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
    gtk_box_pack_start (GTK_BOX (page), frame, FALSE, FALSE, 2);

    box = gtk_vbox_new (TRUE, 2);
    gtk_container_add (GTK_CONTAINER (frame), box);

    fe_disp_place = gtk_radio_button_new_with_label (NULL, _("Place/leave in default folder"));
    gtk_box_pack_start (GTK_BOX (box), fe_disp_place, TRUE, FALSE, 1);

    fe_disp_continue = gtk_radio_button_new_with_label (
        gtk_radio_button_group(GTK_RADIO_BUTTON(fe_disp_place)),
        _("Do not place/leave in default folder"));
    gtk_box_pack_start (GTK_BOX (box), fe_disp_continue, TRUE, FALSE, 1);

    fe_disp_stop = gtk_radio_button_new_with_label (
        gtk_radio_button_group(GTK_RADIO_BUTTON(fe_disp_continue)),
        _("Stop filtering here"));
    gtk_box_pack_start (GTK_BOX (box), fe_disp_stop, TRUE, FALSE, 1);

    return page;
}				/* end build_action_page() */


/*
 * build_right_side()
 *
 * Builds the right side of the dialog
 */
static GtkWidget *
build_right_side (GtkWidget *clist)
{
    GtkWidget *rightside;
    GtkWidget *notebook, *page;
    GtkWidget *bbox, *pixmap, *button;

    rightside = gtk_vbox_new(FALSE, 0);
  
    /* the main notebook */
    notebook = gtk_notebook_new ();
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
    gtk_box_pack_start(GTK_BOX(rightside), notebook, FALSE, FALSE, 0);

    page = build_match_page ();
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                              page, gtk_label_new (_("Match")));
    page = build_action_page ();
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                              page, gtk_label_new (_("Action")));

    /* button box */
    bbox = gtk_hbutton_box_new ();
    gtk_box_pack_start(GTK_BOX(rightside), bbox, FALSE, FALSE, 0);

    button = gnome_stock_button(GNOME_STOCK_BUTTON_APPLY);
    gtk_signal_connect(GTK_OBJECT(button),
                       "clicked",
                       GTK_SIGNAL_FUNC(fe_apply_pressed),
                       clist);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    pixmap = gnome_stock_new_with_icon (GNOME_STOCK_MENU_UNDO);
    button = gnome_pixmap_button(pixmap, _("Revert"));
    gtk_signal_connect(GTK_OBJECT(button),
                       "clicked",
                       GTK_SIGNAL_FUNC(fe_revert_pressed),
                       clist);
    gtk_container_add(GTK_CONTAINER(bbox), button);

    return rightside;
}				/* end build_right_side() */


/*
 * filter_edit_dialog()
 *
 * Returns immediately, but fires off the filter edit dialog.
 *
 * Arguments:
 *   GList *filter_list - the list of filters
 */
void 
filter_edit_dialog (GList * filter_list)
{
    GtkWidget *window;
    GtkWidget *hbox;
    GtkWidget *piece;
    GtkWidget *sep;
    GtkWidget *clist;

    window = gnome_dialog_new ( _("Balsa Filters"),
                               GNOME_STOCK_BUTTON_OK,
                               GNOME_STOCK_BUTTON_CANCEL,
                               GNOME_STOCK_BUTTON_HELP, NULL);

    gtk_signal_connect (GTK_OBJECT (window),
                        "clicked",
                        fe_dialog_button_clicked,
                        NULL);
    /* main hbox */
    hbox = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (window)->vbox),
                        hbox, FALSE, FALSE, 0);

    piece = build_left_side (&clist);
    gtk_box_pack_start (GTK_BOX (hbox), piece, FALSE, FALSE, 2);

    sep = gtk_vseparator_new ();
    gtk_box_pack_start (GTK_BOX (hbox), sep, FALSE, FALSE, 2);

    piece = build_right_side (clist);
    gtk_box_pack_start (GTK_BOX (hbox), piece, FALSE, FALSE, 2);

    gtk_widget_show_all (window);
}
