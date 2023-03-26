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
 * This file contains functions to display informational messages
 * received from libbalsa
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "information-dialog.h"

#include <string.h>

#include <glib/gi18n.h>
#include "balsa-app.h"

static void balsa_information_bar(GtkWindow *parent,
                                  LibBalsaInformationType type,
                                  const char *msg);
static void balsa_information_list(GtkWindow *parent,
                                   LibBalsaInformationType type,
				   const char *msg);
static void balsa_information_dialog(GtkWindow *parent,
                                     LibBalsaInformationType type,
				     const char *msg);
static void balsa_information_stderr(LibBalsaInformationType type,
				     const char *msg);

/* Handle button clicks in the warning window */
static void
balsa_information_list_response_cb(GtkWidget * dialog, gint response,
                                   GtkTextView * view)
{
    switch (response) {
    case GTK_RESPONSE_APPLY:
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(view), "", 0);
	break;
    default:
	gtk_widget_destroy(dialog);
	break;
    }
}

void
balsa_information_real(GtkWindow *parent, LibBalsaInformationType type,
                       const char *msg)
{
    BalsaInformationShow show;
    gchar *show_msg;

    if (!balsa_app.main_window)
        return;

    show_msg = g_strdup(msg);
    libbalsa_utf8_sanitize(&show_msg, balsa_app.convert_unknown_8bit, NULL);
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
	balsa_information_dialog(parent, type, show_msg);
	break;
    case BALSA_INFORMATION_SHOW_LIST:
	balsa_information_list(parent, type, show_msg);
	break;
    case BALSA_INFORMATION_SHOW_BAR:
	balsa_information_bar(parent, type, show_msg);
	break;
    case BALSA_INFORMATION_SHOW_STDERR:
	balsa_information_stderr(type, show_msg);
	break;
    }
    g_free(show_msg);

    if (type == LIBBALSA_INFORMATION_FATAL)
	gtk_main_quit();
}

void
balsa_information(LibBalsaInformationType type, const char *fmt, ...)
{
    gchar *msg;
    va_list ap;

    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    balsa_information_real(GTK_WINDOW(balsa_app.main_window), type, msg);
    g_free(msg);
}

void
balsa_information_parented(GtkWindow *parent, LibBalsaInformationType type,
                           const char *fmt, ...)
{
    gchar *msg;
    va_list ap;

    if(!parent)
        parent = GTK_WINDOW(balsa_app.main_window);
    va_start(ap, fmt);
    msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    balsa_information_real(parent, type, msg);
    g_free(msg);
}

/*
 * Pops up an error dialog
 */
static void
balsa_information_dialog(GtkWindow *parent, LibBalsaInformationType type,
                         const char *msg)
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

    /* Use explicit format so that message_dialog does not try to interpret
     * the message string. */
    messagebox =
        gtk_message_dialog_new(GTK_WINDOW(parent),
                               GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
							   message_type, GTK_BUTTONS_CLOSE,
                               "%s", msg);

    gtk_dialog_run(GTK_DIALOG(messagebox));
    gtk_widget_destroy(messagebox);
}

/* 
 * make the list widget
 */
static GtkWidget *
balsa_information_list_new(void)
{
    GtkTextView *view;

    view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(view, FALSE);
    gtk_text_view_set_left_margin(view, 2);
    gtk_text_view_set_indent(view, -12);
    gtk_text_view_set_right_margin(view, 2);
    gtk_text_view_set_wrap_mode(view, GTK_WRAP_WORD_CHAR);

    return GTK_WIDGET(view);
}

/*
 * Pops up a dialog containing a list of warnings.
 *
 * This is because their can be many warnings (eg while you are away) and popping up 
 * hundreds of windows is ugly.
 */
static void
balsa_information_list(GtkWindow *parent, LibBalsaInformationType type,
                       const char *msg)
{
    static GtkWidget *information_list = NULL;
    GtkTextBuffer *buffer;
    GtkTextIter iter;

    if (information_list == NULL) {
	GtkWidget *information_dialog;
	GtkWidget *scrolled_window;

	information_dialog =
	    gtk_dialog_new_with_buttons(_("Information — Balsa"), 
                                        parent,
                                        GTK_DIALOG_DESTROY_WITH_PARENT |
                                        libbalsa_dialog_flags(),
                                        _("_Clear"), GTK_RESPONSE_APPLY,
                                        _("Cl_ose"), GTK_RESPONSE_CANCEL,
                                        NULL);
	/* Default is to close */
	gtk_dialog_set_default_response(GTK_DIALOG(information_dialog), 
                                        GTK_RESPONSE_CANCEL);

	/* Reset the policy gtk_dialog_new makes itself non-resizable */
	gtk_window_set_resizable(GTK_WINDOW(information_dialog), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(information_dialog), 350, 200);
	gtk_window_set_role(GTK_WINDOW(information_dialog), "Information");

        g_object_add_weak_pointer(G_OBJECT(information_dialog),
                                  (gpointer *) &information_list);

	/* A scrolled window for the list. */
	scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW
				       (scrolled_window),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
        gtk_widget_set_vexpand(scrolled_window, TRUE);
        gtk_widget_set_valign(scrolled_window, GTK_ALIGN_FILL);
        libbalsa_set_vmargins(scrolled_window, 1);
        gtk_container_add(GTK_CONTAINER
                           (gtk_dialog_get_content_area
                            (GTK_DIALOG(information_dialog))),
                           scrolled_window);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), HIG_PADDING);
	gtk_widget_show(scrolled_window);

	/* The list itself */
	information_list = balsa_information_list_new();
	gtk_container_add(GTK_CONTAINER(scrolled_window),
			  information_list);
        g_signal_connect(information_dialog, "response",
                         G_CALLBACK(balsa_information_list_response_cb),
                         information_list);

	gtk_widget_show_all(information_dialog);
    }

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(information_list));
    gtk_text_buffer_get_end_iter(buffer, &iter);
    gtk_text_buffer_place_cursor(buffer, &iter);
    if (gtk_text_buffer_get_char_count(buffer))
        gtk_text_buffer_insert_at_cursor(buffer, "\n", 1);
    gtk_text_buffer_insert_at_cursor(buffer, msg, -1);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(information_list),
                                 gtk_text_buffer_get_insert(buffer),
                                 0, FALSE, 0, 0);

    if (balsa_app.main_window) {
        gchar *line;
        GtkStatusbar *statusbar;
        guint context_id;

        statusbar = balsa_window_get_statusbar(balsa_app.main_window);
        context_id = gtk_statusbar_get_context_id(statusbar, "Information list");
        gtk_statusbar_pop(statusbar, context_id);

        line = g_strdup(msg);
        g_strdelimit(line, "\r\n", ' ');
        gtk_statusbar_push(statusbar, context_id, line);
        g_free(line);
    }
}

static guint bar_timeout_id = 0;
static gboolean
status_bar_refresh(gpointer data)
{
    if (balsa_app.main_window) {
        GtkStatusbar *statusbar;
        guint context_id;

        statusbar = balsa_window_get_statusbar(balsa_app.main_window);
        context_id = gtk_statusbar_get_context_id(statusbar, "Information bar");
        gtk_statusbar_pop(statusbar, context_id);
    }

    bar_timeout_id = 0;

    return FALSE;
}

static void
balsa_information_bar(GtkWindow *parent, LibBalsaInformationType type,
                      const char *msg)
{
    gchar *line;
    GtkStatusbar *statusbar;
    guint context_id;

    if (!balsa_app.main_window)
        return;

    statusbar = balsa_window_get_statusbar(balsa_app.main_window);
    context_id = gtk_statusbar_get_context_id(statusbar, "Information bar");

    /* First clear any current message. */
    if (bar_timeout_id) {
        gtk_statusbar_pop(statusbar, context_id);
        g_source_remove(bar_timeout_id);
    }

    line = g_strdup(msg);
    g_strdelimit(line, "\r\n", ' ');
    gtk_statusbar_push(statusbar, context_id, line);
    g_free(line);

    bar_timeout_id = g_timeout_add_seconds(4, status_bar_refresh, NULL);
}

static void 
balsa_information_stderr(LibBalsaInformationType type, const char *msg)
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
