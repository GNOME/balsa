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

static GtkWidget *information_list = NULL;

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
    switch (response) {
    case GTK_RESPONSE_APPLY:
	gtk_clist_clear(GTK_CLIST(information_list));
	break;
    case GTK_RESPONSE_CLOSE:
	gtk_object_destroy(GTK_OBJECT(dialog)); /* FIXME: should not do it */
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
    GtkWidget *messagebox;

    messagebox =
	gtk_message_dialog_new(GTK_WINDOW(balsa_app.main_window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_INFO,
                               GTK_BUTTONS_CLOSE,msg);

    gtk_window_set_position(GTK_WINDOW(messagebox), GTK_WIN_POS_CENTER);
    gtk_window_set_wmclass(GTK_WINDOW(messagebox), "info-dialog", "Balsa");
    gtk_dialog_run(GTK_DIALOG(messagebox));
    gtk_widget_destroy(messagebox);
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
    gchar *outstr[1];
    gint new_row;

    outstr[0] = msg;
    /* this may break UNICODE strings */
    for(;*msg; msg++)
	if(*msg == '\n') *msg= ' ';

    if (information_list == NULL) {
	GtkWidget *information_dialog;
	GtkWidget *scrolled_window;

	information_dialog =
	    gtk_dialog_new_with_buttons(_("Balsa Information"), 
                                        GTK_WINDOW(balsa_app.main_window),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        _("Clear"), GTK_RESPONSE_APPLY,
                                        GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                        NULL);
	/* Default is to close */
	gtk_dialog_set_default_response(GTK_DIALOG(information_dialog), 
                                        GTK_RESPONSE_CLOSE);

	/* Reset the policy gtk_dialog_new makes itself non-resizable */
	gtk_window_set_policy(GTK_WINDOW(information_dialog), TRUE, TRUE,
                              FALSE);
	gtk_window_set_default_size(GTK_WINDOW(information_dialog), 350,
				    200);
	gtk_window_set_wmclass(GTK_WINDOW(information_dialog),
			       "Information", "Balsa");

	gtk_signal_connect(GTK_OBJECT(information_dialog), "response",
			   GTK_SIGNAL_FUNC
			   (balsa_information_list_response_cb), NULL);

	/* A scrolled window for the list. */
	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW
				       (scrolled_window),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(information_dialog)->vbox),
			   scrolled_window, TRUE, TRUE, 1);
	gtk_widget_show(scrolled_window);

	/* The list itself */
	information_list = gtk_clist_new(1);
	gtk_signal_connect(GTK_OBJECT(information_list), "destroy",
			   GTK_SIGNAL_FUNC(gtk_widget_destroyed),
                           &information_list);
	gtk_clist_set_reorderable(GTK_CLIST(information_list), FALSE);
	gtk_container_add(GTK_CONTAINER(scrolled_window),
			  information_list);
	gtk_clist_set_column_auto_resize(GTK_CLIST(information_list), 0,
					 TRUE);
	gtk_widget_show(information_list);

	gtk_widget_show(information_dialog);
    }

    new_row = gtk_clist_append(GTK_CLIST(information_list), outstr);
    gtk_clist_moveto(GTK_CLIST(information_list), new_row, 0, 0.0, 0.0);

    /* FIXME: Colour hilight the list */

    gnome_appbar_set_status(balsa_app.appbar, outstr[0]);

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
