/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
extern gboolean fex_already_open;

void 
fex_destroy_window_cb(GtkWidget * widget,gpointer throwaway)
{
    fex_already_open=FALSE;
}

void fex_dialog_response(GtkWidget * dialog, gint response, gpointer data)
{
    GtkCList * clist;
    GList * selected;
    LibBalsaFilter * fil;
    gchar * str;

    if (response == GTK_RESPONSE_OK) { /* OK Button */
	clist=GTK_CLIST(data);
	for (selected=clist->selection;
             selected; selected=g_list_next(selected)) {
	    fil=(LibBalsaFilter*)
                gtk_clist_get_row_data(clist,GPOINTER_TO_INT(selected->data));
	    str=g_strdup_printf("%s.siv",fil->name);
	    if (!libbalsa_filter_export_sieve(fil,str))
		balsa_information(LIBBALSA_INFORMATION_ERROR,
                                  _("Unable to export filter %s,"
                                    "an error occured."),
                                  fil->name);
	    g_free(str);
	}
    }
    gtk_widget_destroy(dialog);
}
