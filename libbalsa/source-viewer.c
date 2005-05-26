/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-2001 Stuart Parmenter and others, see AUTHORS file.
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

/* this is simple window that reads text from given file and shows 
   in in a GtkText widget.
*/

#include "config.h"

#include <stdio.h>

#include "misc.h"
#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
#include "mime-stream-shared.h"
#include "i18n.h"

static void close_cb(GtkAction * action, gpointer data);
static void copy_cb(GtkAction * action, gpointer data);
static void select_all_cb(GtkAction * action, gpointer data);
static void lsv_escape_cb(GtkAction * action, gpointer data);

/* Normal items */
static GtkActionEntry entries[] = {
    /* Top level */
    {"FileMenu", NULL, "_File"},
    {"EditMenu", NULL, "_Edit"},
    {"ViewMenu", NULL, "_View"},
    /* Items */
    {"Close", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
     N_("Close the window"), G_CALLBACK(close_cb)},
    {"Copy", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
     N_("Copy text"), G_CALLBACK(copy_cb)},
    {"Select", NULL, N_("_Select Text"), "<control>A",
     N_("Select entire mail"), G_CALLBACK(select_all_cb)},
};

/* Toggle items */
static GtkToggleActionEntry toggle_entries[] = {
    {"Escape", NULL, N_("_Escape Special Characters"), NULL,
     N_("Escape special and non-ASCII characters"),
     G_CALLBACK(lsv_escape_cb), FALSE}
};

static const char *ui_description =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='Close'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Copy'/>"
"      <separator/>"
"      <menuitem action='Select'/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='Escape'/>"
"    </menu>"
"  </menubar>"
"</ui>";

static void 
select_all_cb(GtkAction * action, gpointer data)
{
    GtkTextView *text = g_object_get_data(G_OBJECT(data), "text");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text);
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_move_mark_by_name(buffer, "insert", &start);
    gtk_text_buffer_move_mark_by_name(buffer, "selection_bound", &end);
}

static void
copy_cb(GtkAction * action, gpointer data)
{
    GtkTextView *text = g_object_get_data(G_OBJECT(data), "text");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text);
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_NONE);

    gtk_text_buffer_copy_clipboard(buffer, clipboard);
}

static void
close_cb(GtkAction * action, gpointer data)
{
    gtk_widget_destroy(GTK_WIDGET(data));
}

struct _LibBalsaSourceViewerInfo {
    LibBalsaMessage *msg;
    GtkWidget *text;
    GtkWidget *window;
    gboolean *escape_specials;
};

typedef struct _LibBalsaSourceViewerInfo LibBalsaSourceViewerInfo;

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
	tmp = g_strescape(message, "\n\"");
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
lsv_escape_cb(GtkAction * action, gpointer data)
{
    LibBalsaSourceViewerInfo *lsvi =
        g_object_get_data(G_OBJECT(data), "lsvi");
    LibBalsaMessage *msg = lsvi->msg;
    GMimeStream *msg_stream;
    GMimeStream *mem_stream;
    char *raw_message;

    if (!msg->mailbox) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Mailbox closed"));
	return;
    }
    msg_stream = libbalsa_mailbox_get_message_stream(msg->mailbox, msg->msgno);
    if (msg_stream == NULL)
	return;

    mem_stream = g_mime_stream_mem_new();
    libbalsa_mime_stream_shared_lock(msg_stream);
    g_mime_stream_write_to_stream(msg_stream, mem_stream);
    libbalsa_mime_stream_shared_unlock(msg_stream);
    g_mime_stream_write(mem_stream, "", 1); /* close string */
    raw_message = (char *) GMIME_STREAM_MEM(mem_stream)->buffer->data;

    *(lsvi->escape_specials) =
	gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action));
    lsv_show_message(raw_message, lsvi, *(lsvi->escape_specials));

    g_object_unref(msg_stream);
    g_object_unref(mem_stream);
}

static void
lsv_window_destroy_notify(LibBalsaSourceViewerInfo * lsvi)
{
    g_object_unref(lsvi->msg);
    g_free(lsvi);
}

/* libbalsa_show_message_source:
   pops up a window containing the source of the message msg.
*/

static GtkWidget*
lbsv_app_set_menus(GtkWindow * app, GtkAction ** action)
{
    GtkWidget *window;
    GtkWidget *menubar;
    GtkActionGroup *action_group;
    GtkUIManager *ui_manager;
    GtkAccelGroup *accel_group;
    GError *error;

    window = GTK_WIDGET(app);

    action_group = gtk_action_group_new("MenuActions");
    gtk_action_group_add_actions(action_group, entries,
                                 G_N_ELEMENTS(entries), window);
    gtk_action_group_add_toggle_actions(action_group, toggle_entries,
                                        G_N_ELEMENTS(toggle_entries),
                                        window);

    ui_manager = gtk_ui_manager_new();
    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    accel_group = gtk_ui_manager_get_accel_group(ui_manager);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    error = NULL;
    if (!gtk_ui_manager_add_ui_from_string(ui_manager, ui_description,
                                           -1, &error)) {
        g_message("building menus failed: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    menubar = gtk_ui_manager_get_widget(ui_manager, "/MainMenu");

    *action =
        gtk_ui_manager_get_action(ui_manager, "/MainMenu/ViewMenu/Escape");
    return menubar;
}

void
libbalsa_show_message_source(LibBalsaMessage* msg, const gchar * font,
			     gboolean* escape_specials)
{
    GtkWidget *text;
    PangoFontDescription *desc;
    GtkWidget *vbox, *interior;
    GtkWidget *window;
    GtkAction *escape_action = NULL;
    LibBalsaSourceViewerInfo *lsvi;

    g_return_if_fail(msg);
    g_return_if_fail(MAILBOX_OPEN(msg->mailbox));

    text = gtk_text_view_new();

    desc = pango_font_description_from_string(font);
    gtk_widget_modify_font(text, desc);
    pango_font_description_free(desc);

    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);

    interior = gtk_scrolled_window_new(GTK_TEXT_VIEW(text)->hadjustment,
                                       GTK_TEXT_VIEW(text)->vadjustment);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(interior),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(interior), GTK_WIDGET(text));

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), _("Message Source"));
    g_object_set_data(G_OBJECT(window), "text", text);
    gtk_window_set_wmclass(GTK_WINDOW(window), "message-source", "Balsa");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 400);
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), 
                       lbsv_app_set_menus(GTK_WINDOW(window),
                                          &escape_action), FALSE, TRUE, 1);
    gtk_box_pack_start_defaults(GTK_BOX(vbox), interior);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    lsvi = g_new(LibBalsaSourceViewerInfo, 1);
    lsvi->msg = msg;
    g_object_ref(msg);
    lsvi->text = text;
    lsvi->window = window;
    lsvi->escape_specials = escape_specials;
    g_object_set_data_full(G_OBJECT(window), "lsvi", lsvi,
                           (GDestroyNotify) lsv_window_destroy_notify);

    gtk_widget_show_all(window);
    if (*escape_specials)
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(escape_action),
                                     TRUE);
    else
        lsv_escape_cb(escape_action, window);
}
