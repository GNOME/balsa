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

#include "libbalsa.h"
#include "libbalsa_private.h"

#include <stdio.h>
#include <gnome.h>


static void close_cb(GtkWidget* w, gpointer data);
static void copy_cb(GtkWidget * w, gpointer data);
static void select_all_cb(GtkWidget* w, gpointer data);

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

static GnomeUIInfo main_menu[] = {
#define SOURCE_FILE_MENU 2
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
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


static void
libbalsa_show_file(FILE* f, long length)
{
    GtkWidget*window, *interior;
    GtkWidget* text;
    GtkTextBuffer* buffer;
    char buf[1024];

    window = gnome_app_new("balsa", _("Message Source"));
    gtk_window_set_wmclass(GTK_WINDOW(window), "message-source", "Balsa");
    buffer = gtk_text_buffer_new (NULL);
    text   = gtk_text_view_new_with_buffer(buffer);
    g_object_set_data(G_OBJECT(window), "text", text);
    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, window);
    
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
    interior = gtk_scrolled_window_new(GTK_TEXT_VIEW(text)->hadjustment,
				       GTK_TEXT_VIEW(text)->vadjustment);
    gtk_container_add(GTK_CONTAINER(interior), GTK_WIDGET(text));

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(interior),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gnome_app_set_contents(GNOME_APP(window), interior);

    if(length<0) length = 5*1024*1024; /* random limit for the file size
					* not likely to be used */
    while(length>0 && fgets(buf, sizeof(buf), f)) {
	gint linelen = strlen(buf);
        gchar *tmp;

        if (linelen > length)
            buf[length] = '\0';
        tmp = g_strescape(buf, "\n\"");
        if (tmp) {
	    gtk_text_buffer_insert_at_cursor(buffer, tmp, -1);
            g_free(tmp);
        }
	length -= linelen;
    }

    gtk_window_set_default_size(GTK_WINDOW(window), 500, 400);
    gtk_widget_show_all(window);
}

/* libbalsa_show_message_source:
   pops up a window containing the source of the message msg.
*/
void
libbalsa_show_message_source(LibBalsaMessage* msg)
{
    FILE *f;
    HEADER* hdr;
    long length;
    g_return_if_fail(msg);
    g_return_if_fail(!CLIENT_CONTEXT_CLOSED(msg->mailbox));
    g_return_if_fail(msg->header);

    hdr = msg->header;
    f = libbalsa_mailbox_get_message_stream(msg->mailbox, msg);
    fseek(f, hdr->offset, 0);
    length = (hdr->content->offset- hdr->offset) + hdr->content->length;
    libbalsa_show_file(f, length);
    fclose(f);
}

#if 0
/* testing program */
int main(int argc, char* argv[])
{
    int i, shown = 0;
    gtk_init(&argc, &argv);

    for(i=1; i<argc; i++) {
	FILE* f = fopen(argv[i], "r");
	if(f) {
	    show_file(f);
	    fclose(f);
	    shown = 1;
	}
    }
    if(shown) gtk_main();
    else fprintf(stderr, "No sensible args passed.\n");

    return !shown;
}
#endif
