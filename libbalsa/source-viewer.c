/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-2016 Stuart Parmenter and others, see AUTHORS file.
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

/* this is simple window that reads text from given file and shows 
   in in a GtkText widget.
*/

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <stdio.h>

#include "application-helpers.h"
#include "misc.h"
#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
#include "geometry-manager.h"
#include <glib/gi18n.h>

typedef struct {
    LibBalsaMessage *msg;
    GtkWidget *text;
    GtkWidget *window;
    gboolean *escape_specials;
} LibBalsaSourceViewerInfo;

static void
lsv_close_activated(GSimpleAction * action,
                    GVariant      * parameter,
                    gpointer        user_data)
{
    gtk_widget_destroy(GTK_WIDGET(user_data));
}

static void
lsv_copy_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        user_data)
{
    LibBalsaSourceViewerInfo *lsvi =
        g_object_get_data(G_OBJECT(user_data), "lsvi");
    GtkTextView *text = GTK_TEXT_VIEW(lsvi->text);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text);
    GdkDisplay *display;
    GtkClipboard *clipboard;

    display = gtk_widget_get_display(GTK_WIDGET(text));
    clipboard = gtk_clipboard_get_for_display(display, GDK_NONE);

    gtk_text_buffer_copy_clipboard(buffer, clipboard);
}

static void
lsv_select_activated(GSimpleAction * action,
                     GVariant      * parameter,
                     gpointer        user_data)
{
    LibBalsaSourceViewerInfo *lsvi =
        g_object_get_data(G_OBJECT(user_data), "lsvi");
    GtkTextView *text = GTK_TEXT_VIEW(lsvi->text);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text);
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_move_mark_by_name(buffer, "insert", &start);
    gtk_text_buffer_move_mark_by_name(buffer, "selection_bound", &end);
}

static void
lsv_show_message(const char *message, LibBalsaSourceViewerInfo * lsvi,
                 gboolean escape)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    gchar *tmp;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lsvi->text));
    gtk_text_buffer_set_text(buffer, "", 0);

    if (escape)
	tmp = g_strescape(message, "\n“");
    else {
	tmp = g_strdup(message);
	libbalsa_utf8_sanitize(&tmp, FALSE, NULL);
    }
    if (tmp) {
	gtk_text_buffer_insert_at_cursor(buffer, tmp, -1);
	g_free(tmp);
    }

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
}

static void
lsv_escape_change_state(GSimpleAction * action,
                        GVariant      * state,
                        gpointer        user_data)
{
    LibBalsaSourceViewerInfo *lsvi =
        g_object_get_data(G_OBJECT(user_data), "lsvi");
    LibBalsaMessage *msg = lsvi->msg;
    LibBalsaMailbox *mailbox;
    GMimeStream *msg_stream;
    GMimeStream *mem_stream;
    char *raw_message;

    mailbox = libbalsa_message_get_mailbox(msg);
    if (mailbox == NULL) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Mailbox closed"));
	return;
    }
    msg_stream =
        libbalsa_mailbox_get_message_stream(mailbox,
                                            libbalsa_message_get_msgno(msg),
                                            TRUE);
    if (msg_stream == NULL)
	return;

    mem_stream = g_mime_stream_mem_new();
    libbalsa_mailbox_lock_store(mailbox);
    g_mime_stream_write_to_stream(msg_stream, mem_stream);
    libbalsa_mailbox_unlock_store(mailbox);
    g_mime_stream_write(mem_stream, "", 1); /* close string */
    raw_message = (char *) GMIME_STREAM_MEM(mem_stream)->buffer->data;

    *(lsvi->escape_specials) = g_variant_get_boolean(state);
    lsv_show_message(raw_message, lsvi, *(lsvi->escape_specials));

    g_object_unref(msg_stream);
    g_object_unref(mem_stream);

    g_simple_action_set_state(action, state);
}

static GActionEntry entries[] = {
    {"close",  lsv_close_activated},
    {"copy",   lsv_copy_activated},
    {"select", lsv_select_activated},
    {"escape", NULL, NULL, "false", lsv_escape_change_state}
};

static void
lsv_window_destroy_notify(LibBalsaSourceViewerInfo * lsvi)
{
    g_object_unref(lsvi->msg);
    g_free(lsvi);
}

/* libbalsa_show_message_source:
   pops up a window containing the source of the message msg.
*/

#define BALSA_SOURCE_VIEWER "balsa-source-viewer"

void
libbalsa_show_message_source(GtkApplication  * application,
                             LibBalsaMessage * msg,
                             const gchar     * font,
			     gboolean        * escape_specials)
{
    GtkWidget *text;
    gchar *css;
    GtkCssProvider *css_provider;
    GtkWidget *vbox, *interior;
    GtkWidget *window;
    const gchar resource_path[] = "/org/desktop/Balsa/source-viewer.ui";
    GtkWidget *menu_bar;
    GError *err = NULL;
    LibBalsaSourceViewerInfo *lsvi;
    GAction *escape_action;

    g_return_if_fail(msg != NULL);
    g_return_if_fail(MAILBOX_OPEN(libbalsa_message_get_mailbox(msg)));

    text = gtk_text_view_new();

    gtk_widget_set_name(text, BALSA_SOURCE_VIEWER);
    css = libbalsa_font_string_to_css(font, BALSA_SOURCE_VIEWER);

    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
    g_free(css);

    gtk_style_context_add_provider(gtk_widget_get_style_context(text) ,
                                   GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);

    interior = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(interior),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(interior), GTK_WIDGET(text));

    window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), _("Message Source"));
    gtk_window_set_role(GTK_WINDOW(window), "message-source");
    geometry_manager_attach(GTK_WINDOW(window), "SourceView");

    menu_bar = libbalsa_window_get_menu_bar(GTK_APPLICATION_WINDOW(window),
                                            entries,
                                            G_N_ELEMENTS(entries),
                                            resource_path, &err, window);
    if (!menu_bar) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Error adding from %s: %s\n"), resource_path,
                             err->message);
        g_error_free(err);
        return;
    }

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    libbalsa_set_vmargins(menu_bar, 1);
    gtk_container_add(GTK_CONTAINER(vbox), menu_bar);

    libbalsa_set_margins(interior, 2);
    gtk_widget_set_vexpand(interior, TRUE);
    gtk_container_add(GTK_CONTAINER(vbox), interior);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    lsvi = g_new(LibBalsaSourceViewerInfo, 1);
    lsvi->msg = g_object_ref(msg);
    lsvi->text = text;
    lsvi->window = window;
    lsvi->escape_specials = escape_specials;
    g_object_set_data_full(G_OBJECT(window), "lsvi", lsvi,
                           (GDestroyNotify) lsv_window_destroy_notify);

    gtk_widget_show_all(window);

    escape_action = g_action_map_lookup_action(G_ACTION_MAP(window), "escape");
    lsv_escape_change_state(G_SIMPLE_ACTION(escape_action),
                            g_variant_new_boolean(*escape_specials),
                            window);
}
