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

/*
 * Author : Emmanuel ALLAUD
 */

/*
 * FIXME : should have a combo box for mailbox name when selecting a move or copy action
 */

#include "config.h"

#include "balsa-app.h"
#include "filter-export.h"

/* To prevent user from silmultaneously edit/export filters */

extern gboolean fe_already_open;
extern GList * fr_dialogs_opened;

gboolean fex_already_open=FALSE;

GtkWidget * fex_window;

/*
 * filters_export_dialog()
 *
 * Returns immediately, but fires off the filter export dialog.
 */
void
filters_export_dialog(void)
{
    static gchar *titles[] = { N_("Name") };
    GtkCList * clist;
    GtkWidget * sw;
    gint row;
    LibBalsaFilter * fil;
    GSList * filter_list;

    if (fr_dialogs_opened) {
	balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("There are opened filter run dialogs, "
                            "close them before you can modify filters."));
	return;
    }
    if (fex_already_open) {
	gdk_window_raise(fex_window->window);
	return;
    }
    
    fex_already_open=TRUE;

    fex_window =
        gtk_dialog_new_with_buttons(_("Balsa Filters Export"),
                                    NULL, 0, /* FIXME */
                                    GTK_STOCK_OK,     GTK_RESPONSE_OK,
                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                    GTK_STOCK_HELP,   GTK_RESPONSE_HELP,
                                    NULL);
    gtk_window_set_wmclass(GTK_WINDOW(fex_window), "filter-export",
                           "Balsa");

    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

#ifdef ENABLE_NLS
    titles[0] = _(titles[0]);
#endif
    clist = GTK_CLIST(gtk_clist_new_with_titles(1, titles));

    gtk_clist_set_selection_mode(clist, GTK_SELECTION_MULTIPLE);
    gtk_clist_set_row_height(clist, 0);
    gtk_clist_column_titles_passive(clist);
    gtk_clist_set_sort_column(clist,0);
    gtk_clist_set_sort_type(clist,GTK_SORT_ASCENDING);
    gtk_clist_set_auto_sort(clist,TRUE);

    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(clist));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(fex_window)->vbox),
                       sw, TRUE, TRUE, 2);

    /* Populate the clist of filters */

    for(filter_list=balsa_app.filters;filter_list;
        filter_list=g_slist_next(filter_list)) {

	fil=(LibBalsaFilter*)filter_list->data;
	row=gtk_clist_append(clist,&(fil->name));
	
	/* We associate the data with the newly appended row */
	gtk_clist_set_row_data(clist,row,(gpointer)fil);
    }

    gtk_widget_set_usize(GTK_WIDGET(clist),-1,200);

    gtk_signal_connect(GTK_OBJECT(fex_window), "response",
                       GTK_SIGNAL_FUNC(fex_dialog_response), clist);
    gtk_signal_connect(GTK_OBJECT(fex_window), "destroy",
		       GTK_SIGNAL_FUNC(fex_destroy_window_cb), NULL);

    gtk_widget_show_all(GTK_WIDGET(fex_window));
}
