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
    GtkTreeView *list;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkWidget *sw;
    LibBalsaFilter *fil;
    GSList *filter_list;

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

    fex_already_open = TRUE;

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

    list =
        libbalsa_filter_list_new(TRUE, _("Name"), GTK_SELECTION_MULTIPLE,
                                 NULL, TRUE);

    gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(list));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(fex_window)->vbox),
                       sw, TRUE, TRUE, 2);

    /* Populate the list of filters */

    model = gtk_tree_view_get_model(list);
    for (filter_list = balsa_app.filters; filter_list;
         filter_list = g_slist_next(filter_list)) {
        fil = (LibBalsaFilter *) filter_list->data;
        gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
        gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, fil->name, 1,
                           fil, -1);
    }
    if (gtk_tree_model_get_iter_first(model, &iter)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(list);
        gtk_tree_selection_select_iter(selection, &iter);
    }

    g_signal_connect(G_OBJECT(fex_window), "response",
                     G_CALLBACK(fex_dialog_response), list);
    g_signal_connect(G_OBJECT(fex_window), "destroy",
		     G_CALLBACK(fex_destroy_window_cb), NULL);

    gtk_widget_show_all(GTK_WIDGET(fex_window));
}
