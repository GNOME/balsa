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
 * This file contains functions to display inormational messages 
 * received from libbalsa
 */

#include "config.h"

#include <gnome.h>

#include "libbalsa.h"
#include "balsa-app.h"
#include "information-dialog.h"

static void balsa_information_list(LibBalsaInformationType type,
				   char *msg);
static void balsa_information_dialog(LibBalsaInformationType type,
				     char *msg);
static void balsa_information_stderr(LibBalsaInformationType type,
				     char *msg);

/* Handle button clicks in the warning window */
static void
balsa_information_list_response_cb(GtkDialog * dialog, gint response,
                                   gpointer data)
{
    GtkWidget *list = data;
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));

    switch (response) {
    case GTK_RESPONSE_APPLY:
	gtk_list_store_clear(GTK_LIST_STORE(model));
	break;
    default:
	gtk_widget_destroy(GTK_WIDGET(dialog));
	break;
    }
}

void
balsa_information(LibBalsaInformationType type, const char *fmt, ...)
{
    BalsaInformationShow show;
    gchar *msg;
    va_list ap;

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    switch (type) {
    case LIBBALSA_INFORMATION_MESSAGE:
	show = balsa_app.information_message;
	break;
    case LIBBALSA_INFORMATION_WARNING:
	show = balsa_app.warning_message;
	break;
    case LIBBALSA_INFORMATION_ERROR:
	show = balsa_app.error_message;
	break;
    case LIBBALSA_INFORMATION_DEBUG:
	show = balsa_app.debug_message;
	break;
    case LIBBALSA_INFORMATION_FATAL:
    default:
	show = balsa_app.fatal_message;
	break;
    }

    switch (show) {
    case BALSA_INFORMATION_SHOW_NONE:
	break;
    case BALSA_INFORMATION_SHOW_DIALOG:
	balsa_information_dialog(type, msg);
	break;
    case BALSA_INFORMATION_SHOW_LIST:
	balsa_information_list(type, msg);
	break;
    case BALSA_INFORMATION_SHOW_STDERR:
	balsa_information_stderr(type, msg);
	break;
    }

    g_free(msg);

    if (type == LIBBALSA_INFORMATION_FATAL)
	gtk_main_quit();


}

/*
 * Pops up an error dialog
 */
static void
balsa_information_dialog(LibBalsaInformationType type, char *msg)
{
    GtkMessageType message_type;
    GtkWidget *messagebox;

    switch (type) {
    case LIBBALSA_INFORMATION_MESSAGE:
        message_type = GTK_MESSAGE_INFO;
        break;
    case LIBBALSA_INFORMATION_WARNING:
        message_type = GTK_MESSAGE_WARNING;
        break;
    case LIBBALSA_INFORMATION_ERROR:
        message_type = GTK_MESSAGE_ERROR;
        break;
    case LIBBALSA_INFORMATION_DEBUG:
        message_type = GTK_MESSAGE_INFO;
        break;
    case LIBBALSA_INFORMATION_FATAL:
        message_type = GTK_MESSAGE_ERROR;
        break;
    default:
        message_type = GTK_MESSAGE_INFO;
        break;
    }

    messagebox =
        gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               message_type, GTK_BUTTONS_CLOSE, msg);

    gtk_dialog_run(GTK_DIALOG(messagebox));
    gtk_widget_destroy(messagebox);
}

/* 
 * make the list widget
 */
static GtkWidget *
balsa_information_list_new(void)
{
    GtkListStore *list_store;
    GtkTreeView *view;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    list_store = gtk_list_store_new(1, G_TYPE_STRING);
    view = GTK_TREE_VIEW(gtk_tree_view_new_with_model
                         (GTK_TREE_MODEL(list_store)));
    g_object_unref(list_store);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                      "text", 0, NULL);
    gtk_tree_view_append_column(view, column);
    gtk_tree_view_set_headers_visible(view, FALSE);

    return GTK_WIDGET(view);
}

/*
 * Pops up a dialog containing a list of warnings.
 *
 * This is because their can be many warnings (eg while you are away) and popping up 
 * hundreds of windows is ugly.
 */
static void
balsa_information_list(LibBalsaInformationType type, char *msg)
{
    static GtkWidget *information_list = NULL;
    gchar *outstr;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreePath *path;

    outstr = msg;
    /* this may break UNICODE strings */
    for(;*msg; msg++)
	if(*msg == '\n') *msg= ' ';

    if (information_list == NULL) {
	GtkWidget *information_dialog;
	GtkWidget *scrolled_window;

	information_dialog =
	    gtk_dialog_new_with_buttons(_("Information - Balsa"), 
                                        GTK_WINDOW(balsa_app.main_window),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_CLEAR, GTK_RESPONSE_APPLY,
                                        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                        NULL);
	/* Default is to close */
	gtk_dialog_set_default_response(GTK_DIALOG(information_dialog), 
                                        GTK_RESPONSE_CLOSE);
	gtk_dialog_set_has_separator(GTK_DIALOG(information_dialog), FALSE);

	/* Reset the policy gtk_dialog_new makes itself non-resizable */
	gtk_window_set_resizable(GTK_WINDOW(information_dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(information_dialog), 350, 200);
	gtk_container_set_border_width(GTK_CONTAINER(GTK_WINDOW(
				       information_dialog)), 6);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(information_dialog)->vbox), 12);
	gtk_window_set_wmclass(GTK_WINDOW(information_dialog),
			       "Information", "Balsa");

        g_object_add_weak_pointer(G_OBJECT(information_dialog),
                                  (gpointer) &information_list);

	/* A scrolled window for the list. */
	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW
				       (scrolled_window),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(information_dialog)->vbox),
			   scrolled_window, TRUE, TRUE, 1);
	gtk_container_set_border_width(GTK_CONTAINER(GTK_SCROLLED_WINDOW(
				       scrolled_window)), 6);
	gtk_widget_show(scrolled_window);

	/* The list itself */
	information_list = balsa_information_list_new();
	gtk_container_add(GTK_CONTAINER(scrolled_window),
			  information_list);
        g_signal_connect(G_OBJECT(information_dialog), "response",
                         G_CALLBACK(balsa_information_list_response_cb),
                         information_list);

	gtk_widget_show(information_list);

	gtk_widget_show(information_dialog);
    }

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(information_list));
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, outstr, -1);
    path = gtk_tree_model_get_path(model, &iter);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(information_list),
                                 path, NULL, TRUE, 1, 0);
    gtk_tree_path_free(path);

    /* FIXME: Colour hilight the list */

    gnome_appbar_set_status(balsa_app.appbar, outstr);

}

static void 
balsa_information_stderr(LibBalsaInformationType type, char *msg)
{
    switch (type) {
    case LIBBALSA_INFORMATION_WARNING:
	fprintf(stderr, _("WARNING: "));
	break;
    case LIBBALSA_INFORMATION_ERROR:
	fprintf(stderr, _("ERROR: "));
	break;
    case LIBBALSA_INFORMATION_FATAL:
	fprintf(stderr, _("FATAL: "));
	break;
    default:
	break;
    }
    fprintf(stderr, "%s\n", msg);
}
