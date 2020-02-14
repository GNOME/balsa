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

/*
 * Author : Emmanuel ALLAUD
 */

/*
 * FIXME : should have a combo box for mailbox name when selecting a move or copy action
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include "balsa-app.h"
#include "filter-export.h"

#include <glib/gi18n.h>	/* Must come after balsa-app.h. */

/* To prevent user from silmultaneously edit/export filters */
extern gboolean fex_already_open;

void 
fex_destroy_window_cb(GtkWidget * widget,gpointer throwaway)
{
    fex_already_open=FALSE;
}

static void
fex_dialog_response_func(GtkTreeModel * model, GtkTreePath * path,
                         GtkTreeIter * iter, gpointer data)
{
    LibBalsaFilter *fil;
    gchar *str;

    gtk_tree_model_get(model, iter, 1, &fil, -1);
    str = g_strdup_printf("%s.siv", fil->name);
    if (!libbalsa_filter_export_sieve(fil, str))
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Unable to export filter %s, "
                            "an error occurred."), fil->name);
    g_free(str);
}

void
fex_dialog_response(GtkWidget * dialog, gint response, gpointer data)
{
    if (response == GTK_RESPONSE_OK) {  /* OK Button */
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection(GTK_TREE_VIEW(data));

        gtk_tree_selection_selected_foreach(selection,
                                            fex_dialog_response_func,
                                            NULL);
    }
    gtk_widget_destroy(dialog);
}
