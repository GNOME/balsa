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
#include <gnome.h>

#include "misc.h"
#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"



static void close_cb(GtkWidget* w, gpointer data);
static void copy_cb(GtkWidget * w, gpointer data);
static void select_all_cb(GtkWidget* w, gpointer data);
static void lsv_escape_cb(GtkWidget * widget, gpointer data);

static GnomeUIInfo file_menu[] = {
#define MENU_FILE_INCLUDE_POS 1
    GNOMEUIINFO_MENU_CLOSE_ITEM(close_cb, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo edit_menu[] = {
#define MENU_EDIT_INCLUDE_POS 0
    GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
    {GNOME_APP_UI_ITEM, N_("_Select Text"),
     N_("Select entire mail"),
     select_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'A', GDK_CONTROL_MASK, NULL
    },
    GNOMEUIINFO_END
};

static GnomeUIInfo view_menu[] = {
#define MENU_VIEW_ESCAPE_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("_Escape Special Characters"),
                           N_("Escape special and non-ASCII characters"),
                           lsv_escape_cb, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] = {
#define SOURCE_FILE_MENU 2
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
    GNOMEUIINFO_MENU_VIEW_TREE(view_menu),
    GNOMEUIINFO_END
};


static void 
select_all_cb(GtkWidget * w, gpointer data)
{
    GtkTextView *text = g_object_get_data(G_OBJECT(data), "text");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text);
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_move_mark_by_name(buffer, "insert", &start);
    gtk_text_buffer_move_mark_by_name(buffer, "selection_bound", &end);
}

static void
copy_cb(GtkWidget * w, gpointer data)
{
    GtkTextView *text = g_object_get_data(G_OBJECT(data), "text");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text);
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_NONE);

    gtk_text_buffer_copy_clipboard(buffer, clipboard);
}

static void
close_cb(GtkWidget* w, gpointer data)
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
lsv_show_file(FILE * f, long length, LibBalsaSourceViewerInfo * lsvi,
                   gboolean escape)
{
    GtkTextBuffer *buffer;
    char buf[1024];
    GtkTextIter start;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lsvi->text));
    gtk_text_buffer_set_text(buffer, "", 0);

    if (length < 0)
        length = 5 * 1024 * 1024;       /* random limit for the file size
                                         * not likely to be used */
    while (length > 0 && fgets(buf, sizeof(buf), f)) {
        gint linelen = strlen(buf);
        gchar *tmp;

        if (linelen > length)
            buf[length] = '\0';
        if (escape)
            tmp = g_strescape(buf, "\n\"");
        else {
            tmp = g_strdup(buf);
            libbalsa_utf8_sanitize(&tmp, FALSE, (LibBalsaCodeset) 0, NULL);
        }
        if (tmp) {
            gtk_text_buffer_insert_at_cursor(buffer, tmp, -1);
            g_free(tmp);
        }
        length -= linelen;
    }

    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
}

static void
lsv_escape_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaSourceViewerInfo *lsvi =
        g_object_get_data(G_OBJECT(data), "lsvi");
    LibBalsaMessage *msg = lsvi->msg;
    HEADER *hdr;
    FILE *f;
    long length;

    hdr = msg->header;
    f = libbalsa_mailbox_get_message_stream(msg->mailbox, msg);
    fseek(f, hdr->offset, 0);
    length = (hdr->content->offset - hdr->offset) + hdr->content->length;
    *(lsvi->escape_specials) = GTK_CHECK_MENU_ITEM(widget)->active;
    lsv_show_file(f, length, lsvi, *(lsvi->escape_specials));
    fclose(f);
}

static void
lsv_msg_weak_ref_notify(LibBalsaSourceViewerInfo * lsvi)
{
    lsvi->msg = NULL;
    gtk_widget_destroy(lsvi->window);
}

static void
lsv_window_destroy_notify(LibBalsaSourceViewerInfo * lsvi)
{
    if (lsvi->msg)
        g_object_weak_unref(G_OBJECT(lsvi->msg),
                            (GWeakNotify) lsv_msg_weak_ref_notify, lsvi);
    g_free(lsvi);
}

/* libbalsa_show_message_source:
   pops up a window containing the source of the message msg.
*/
void
libbalsa_show_message_source(LibBalsaMessage * msg, const gchar * font,
                             gboolean* escape_specials)
{
    GtkWidget *text;
    GtkWidget *interior;
    GtkWidget *window;
    LibBalsaSourceViewerInfo *lsvi;

    g_return_if_fail(msg);
    g_return_if_fail(!CLIENT_CONTEXT_CLOSED(msg->mailbox));
    g_return_if_fail(msg->header);

    text = gtk_text_view_new();
    gtk_widget_modify_font(text, pango_font_description_from_string(font));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);

    interior = gtk_scrolled_window_new(GTK_TEXT_VIEW(text)->hadjustment,
                                       GTK_TEXT_VIEW(text)->vadjustment);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(interior),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(interior), GTK_WIDGET(text));

    window = gnome_app_new("balsa", _("Message Source"));
    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, window);
    gtk_window_set_wmclass(GTK_WINDOW(window), "message-source", "Balsa");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 400);
    gnome_app_set_contents(GNOME_APP(window), interior);

    lsvi = g_new(LibBalsaSourceViewerInfo, 1);
    lsvi->msg = msg;
    g_object_weak_ref(G_OBJECT(msg),
                      (GWeakNotify) lsv_msg_weak_ref_notify, lsvi);
    lsvi->text = text;
    lsvi->window = window;
    lsvi->escape_specials = escape_specials;
    g_object_set_data_full(G_OBJECT(window), "lsvi", lsvi,
                           (GDestroyNotify) lsv_window_destroy_notify);

    gtk_widget_show_all(window);
    if(*escape_specials) 
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                       (view_menu[MENU_VIEW_ESCAPE_POS].
                                        widget), *escape_specials);
    else
        lsv_escape_cb(view_menu[MENU_VIEW_ESCAPE_POS].widget, window);
}


#if 0
/* testing program */
int
main(int argc, char *argv[])
{
    int i, shown = 0;
    gtk_init(&argc, &argv);

    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (f) {
            show_file(f);
            fclose(f);
            shown = 1;
        }
    }
    if (shown)
        gtk_main();
    else
        fprintf(stderr, "No sensible args passed.\n");

    return !shown;
}
#endif
