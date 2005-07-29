/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#include <string.h>
#include "config.h"
#include "balsa-app.h"
#include "html.h"
#include "i18n.h"
#include "quote-color.h"
#include "sendmsg-window.h"
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "balsa-mime-widget-text.h"


static void bm_modify_font_from_string(GtkWidget * widget, const char *font);
static GtkTextTag * quote_tag(GtkTextBuffer * buffer, gint level);
static gboolean fix_text_widget(GtkWidget *widget, gpointer data);
static void text_view_populate_popup(GtkTextView *textview, GtkMenu *menu,
				     LibBalsaMessageBody * mime_body);

#ifdef HAVE_GTKHTML
static BalsaMimeWidget * bm_widget_new_html(BalsaMessage * bm, LibBalsaMessageBody * mime_body, gchar * ptr, size_t len);
#endif

/* URL related stuff */
typedef struct _message_url_t {
    GtkTextMark *end_mark;
    gint start, end;             /* pos in the buffer */
    gchar *url;                  /* the link */
} message_url_t;

/* store the coordinates at which the button was pressed */
static gint stored_x = -1, stored_y = -1;
static GdkModifierType stored_mask = -1;
#define STORED_MASK_BITS (  GDK_SHIFT_MASK   \
                          | GDK_CONTROL_MASK \
                          | GDK_MOD1_MASK    \
                          | GDK_MOD2_MASK    \
                          | GDK_MOD3_MASK    \
                          | GDK_MOD4_MASK    \
                          | GDK_MOD5_MASK    )

/* the cursors which are displayed over URL's and normal message text */
static GdkCursor *url_cursor_normal = NULL;
static GdkCursor *url_cursor_over_url = NULL;


static gboolean store_button_coords(GtkWidget * widget, GdkEventButton * event, gpointer data);
static gboolean check_over_url(GtkWidget * widget, GdkEventMotion * event, GList * url_list);
static void pointer_over_url(GtkWidget * widget, message_url_t * url, gboolean set);
static void prepare_url_offsets(GtkTextBuffer * buffer, GList * url_list);
static void url_found_cb(GtkTextBuffer * buffer, GtkTextIter * iter, const gchar * buf, gpointer data);
static gboolean check_call_url(GtkWidget * widget, GdkEventButton * event, GList * url_list);
static message_url_t * find_url(GtkWidget * widget, gint x, gint y, GList * url_list);
static void handle_url(const message_url_t* url);
static void free_url_list(GList * url_list);
static void balsa_gtk_html_on_url(GtkWidget *html, const gchar *url);


BalsaMimeWidget *
balsa_mime_widget_new_text(BalsaMessage * bm, LibBalsaMessageBody * mime_body,
			   const gchar * content_type, gpointer data)
{
    LibBalsaHTMLType html_type;
    gchar *ptr = NULL;
    size_t alloced;
    BalsaMimeWidget *mw;
    GtkTextBuffer *buffer;
    regex_t rex;
    GList *url_list = NULL;
    const gchar *target_cs;


    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    alloced = libbalsa_message_body_get_content(mime_body, &ptr);
    if (!ptr)
        return NULL;

    /* handle HTML if possible */
    html_type = libbalsa_html_type(content_type);
    if (html_type) {
#ifdef HAVE_GTKHTML
	alloced = libbalsa_html_filter(html_type, &ptr, alloced);
	/* Force vertical scrollbar while we render the html, otherwise
	 * the widget will make itself too wide to accept one, forcing
	 * otherwise unnecessary horizontal scrolling. */
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(bm->scroll),
                                       GTK_POLICY_AUTOMATIC,
                                       GTK_POLICY_ALWAYS);
        return bm_widget_new_html(bm, mime_body, ptr, alloced);
#else
        return NULL;
#endif
    }

    /* prepare a text part */
    if (!libbalsa_utf8_sanitize(&ptr, balsa_app.convert_unknown_8bit,
				&target_cs)) {
	gchar *from =
	    balsa_message_sender_to_gchar(bm->message->headers->from, 0);
	gchar *subject =
	    g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(bm->message));
        
	libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit, 
			       NULL);
	libbalsa_information
	    (LIBBALSA_INFORMATION_MESSAGE,
	     _("The message sent by %s with subject \"%s\" contains 8-bit "
	       "characters, but no header describing the used codeset "
	       "(converted to %s)"),
	     from, subject,
	     target_cs ? target_cs : "\"?\"");
	g_free(subject);
	g_free(from);
    }

    if (libbalsa_message_body_is_flowed(mime_body)) {
	/* Parse, but don't wrap. */
	gboolean delsp = libbalsa_message_body_is_delsp(mime_body);
	ptr = libbalsa_wrap_rfc2646(ptr, G_MAXINT, FALSE, TRUE, delsp);
    } else if (bm->wrap_text)
	libbalsa_wrap_string(ptr, balsa_app.browse_wrap_length);

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    mw->widget = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(mw->widget), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(mw->widget), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(mw->widget), 15);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(mw->widget), GTK_WRAP_WORD);

    /* set the message font */
    bm_modify_font_from_string(mw->widget, balsa_app.message_font);

    g_signal_connect(G_OBJECT(mw->widget), "key_press_event",
		     G_CALLBACK(balsa_mime_widget_key_press_event),
		     (gpointer) bm);
    g_signal_connect(G_OBJECT(mw->widget), "populate-popup",
		     G_CALLBACK(text_view_populate_popup),
		     (gpointer)mime_body);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(mw->widget));
    gtk_text_buffer_create_tag(buffer, "soft", NULL, NULL);
    allocate_quote_colors(GTK_WIDGET(bm), balsa_app.quoted_color,
			  0, MAX_QUOTED_COLOR - 1);
    if (regcomp(&rex, balsa_app.quote_regex, REG_EXTENDED) != 0) {
	g_warning
	    ("part_info_init_mimetext: quote regex compilation failed.");
	gtk_text_buffer_insert_at_cursor(buffer, ptr, -1);
    } else {
	gchar *line_start;
	LibBalsaUrlInsertInfo url_info;

	gtk_text_buffer_create_tag(buffer, "url",
				   "foreground-gdk",
				   &balsa_app.url_color, NULL);
	gtk_text_buffer_create_tag(buffer, "emphasize", 
				   "foreground", "red",
				   "underline", PANGO_UNDERLINE_SINGLE,
				   NULL);
	url_info.callback = url_found_cb;
	url_info.callback_data = &url_list;
	url_info.ml_url = NULL;
	url_info.ml_url_buffer = NULL;

	line_start = ptr;
	while (line_start) {
	    gchar *newline, *this_line;
	    gint quote_level;
	    GtkTextTag *tag;
	    int len;

	    newline = strchr(line_start, '\n');
	    if (newline)
		this_line = g_strndup(line_start, newline - line_start);
	    else
		this_line = g_strdup(line_start);
	    quote_level = is_a_quote(this_line, &rex);
	    tag = quote_tag(buffer, quote_level);
	    len = strlen(this_line);
	    if (len > 0 && this_line[len - 1] == '\r')
		this_line[len - 1] = '\0';
	    /* tag is NULL if the line isn't quoted, but it causes
	     * no harm */
	    if (!libbalsa_insert_with_url(buffer, this_line, line_start,
					  tag, &url_info))
		gtk_text_buffer_insert_at_cursor(buffer, "\n", 1);
	    
	    g_free(this_line);
	    line_start = newline ? newline + 1 : NULL;
	}
	regfree(&rex);
    }

    if (libbalsa_message_body_is_flowed(mime_body))
	libbalsa_wrap_view(GTK_TEXT_VIEW(mw->widget),
			   balsa_app.browse_wrap_length);
    prepare_url_offsets(buffer, url_list);
    g_signal_connect_after(G_OBJECT(mw->widget), "realize",
			   G_CALLBACK(fix_text_widget), url_list);
    if (url_list) {
	g_signal_connect(G_OBJECT(mw->widget), "button_press_event",
			 G_CALLBACK(store_button_coords), NULL);
	g_signal_connect(G_OBJECT(mw->widget), "button_release_event",
			 G_CALLBACK(check_call_url), url_list);
	g_signal_connect(G_OBJECT(mw->widget), "motion-notify-event",
			 G_CALLBACK(check_over_url), url_list);
	g_signal_connect(G_OBJECT(mw->widget), "leave-notify-event",
			 G_CALLBACK(check_over_url), url_list);
	g_object_set_data_full(G_OBJECT(mw->widget), "url-list", url_list,
			       (GDestroyNotify)free_url_list);
    }

    /* size allocation may not be correct, so we'll check back later */
    balsa_mime_widget_schedule_resize(mw->widget);
    
    g_free(ptr);

    return mw;
}


/* -- local functions -- */
static void
bm_modify_font_from_string(GtkWidget * widget, const char *font)
{
    PangoFontDescription *desc =
        pango_font_description_from_string(balsa_app.message_font);
    gtk_widget_modify_font(widget, desc);
    pango_font_description_free(desc);
}

/* quote_tag:
 * lookup the GtkTextTag for coloring quoted lines of a given level;
 * create the tag if it isn't found.
 *
 * returns NULL if the level is 0 (unquoted)
 */
static GtkTextTag *
quote_tag(GtkTextBuffer * buffer, gint level)
{
    GtkTextTag *tag = NULL;

    if (level > 0) {
        GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
        gchar *name;

        /* Modulus the quote level by the max,
         * ie, always have "1 <= quote level <= MAX"
         * this allows cycling through the possible
         * quote colors over again as the quote level
         * grows arbitrarily deep. */
        level = (level - 1) % MAX_QUOTED_COLOR;
        name = g_strdup_printf("quote-%d", level);
        tag = gtk_text_tag_table_lookup(table, name);

        if (!tag) {
            tag =
                gtk_text_buffer_create_tag(buffer, name, "foreground-gdk",
                                           &balsa_app.quoted_color[level],
                                           NULL);
            /* Set a low priority, so we can set both quote color and
             * URL color, and URL color will take precedence. */
            gtk_text_tag_set_priority(tag, 0);
        }
        g_free(name);
    }

    return tag;
}

/* set the gtk_text widget's cursor to a vertical bar
   fix event mask so that pointer motions are reported (if necessary) */
static gboolean
fix_text_widget(GtkWidget *widget, gpointer data)
{
    GdkWindow *w =
        gtk_text_view_get_window(GTK_TEXT_VIEW(widget),
                                 GTK_TEXT_WINDOW_TEXT);
    
    if (data)
        gdk_window_set_events(w,
                              gdk_window_get_events(w) |
                              GDK_POINTER_MOTION_MASK |
                              GDK_LEAVE_NOTIFY_MASK);
    if (!url_cursor_normal || !url_cursor_over_url) {
        url_cursor_normal = gdk_cursor_new(GDK_XTERM);
        url_cursor_over_url = gdk_cursor_new(GDK_HAND2);
    }
    gdk_window_set_cursor(w, url_cursor_normal);
    return FALSE;
}

static void
gtk_widget_destroy_insensitive(GtkWidget * widget)
{
    if (!GTK_WIDGET_SENSITIVE(widget) ||
	GTK_IS_SEPARATOR_MENU_ITEM(widget))
	gtk_widget_destroy(widget);
}

static void
text_view_populate_popup(GtkTextView *textview, GtkMenu *menu,
                         LibBalsaMessageBody * mime_body)
{
    GtkWidget *menu_item;

    gtk_widget_hide_all(GTK_WIDGET(menu));

    gtk_container_foreach(GTK_CONTAINER(menu),
                          (GtkCallback)gtk_widget_destroy_insensitive, NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
			  gtk_separator_menu_item_new ());
    libbalsa_fill_vfs_menu_by_content_type(menu, "text/plain",
					   G_CALLBACK (balsa_mime_widget_ctx_menu_vfs_cb),
					   (gpointer)mime_body);

    menu_item = gtk_menu_item_new_with_label (_("Save..."));
    g_signal_connect (G_OBJECT (menu_item), "activate",
                      G_CALLBACK (balsa_mime_widget_ctx_menu_save), (gpointer)mime_body);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    gtk_widget_show_all(GTK_WIDGET(menu));
}


/* -- URL related stuff -- */

static gboolean
store_button_coords(GtkWidget * widget, GdkEventButton * event,
                    gpointer data)
{
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        GdkWindow *window =
            gtk_text_view_get_window(GTK_TEXT_VIEW(widget),
                                     GTK_TEXT_WINDOW_TEXT);

        gdk_window_get_pointer(window, &stored_x, &stored_y, &stored_mask);

        /* compare only shift, ctrl, and mod1-mod5 */
        stored_mask &= STORED_MASK_BITS;
    }
    return FALSE;
}

/* check if we are over an url and change the cursor in this case */
static gboolean
check_over_url(GtkWidget * widget, GdkEventMotion * event,
               GList * url_list)
{
    gint x, y;
    GdkModifierType mask;
    static gboolean was_over_url = FALSE;
    static message_url_t *current_url = NULL;
    GdkWindow *window;
    message_url_t *url;

    window = gtk_text_view_get_window(GTK_TEXT_VIEW(widget),
                                      GTK_TEXT_WINDOW_TEXT);
    if (event->type == GDK_LEAVE_NOTIFY)
        url = NULL;
    else {
        /* FIXME: why can't we just use
         * x = event->x;
         * y = event->y;
         * ??? */
        gdk_window_get_pointer(window, &x, &y, &mask);
        url = find_url(widget, x, y, url_list);
    }

    if (url) {
        if (!url_cursor_normal || !url_cursor_over_url) {
            url_cursor_normal = gdk_cursor_new(GDK_LEFT_PTR);
            url_cursor_over_url = gdk_cursor_new(GDK_HAND2);
        }
        if (!was_over_url) {
            gdk_window_set_cursor(window, url_cursor_over_url);
            was_over_url = TRUE;
        }
        if (url != current_url) {
            pointer_over_url(widget, current_url, FALSE);
            pointer_over_url(widget, url, TRUE);
        }
    } else if (was_over_url) {
        gdk_window_set_cursor(window, url_cursor_normal);
        pointer_over_url(widget, current_url, FALSE);
        was_over_url = FALSE;
    }

    current_url = url;
    return FALSE;
}

/* pointer_over_url:
 * change style of a url and set/clear the status bar.
 */
static void
pointer_over_url(GtkWidget * widget, message_url_t * url, gboolean set)
{
    GtkTextBuffer *buffer;
    GtkTextTagTable *table;
    GtkTextTag *tag;
    GtkTextIter start, end;

    if (!url)
        return;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    table = gtk_text_buffer_get_tag_table(buffer);
    tag = gtk_text_tag_table_lookup(table, "emphasize");

    gtk_text_buffer_get_iter_at_offset(buffer, &start, url->start);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, url->end);
    
    if (set) {
        gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
        balsa_gtk_html_on_url(NULL, url->url);
    } else {
        gtk_text_buffer_remove_tag(buffer, tag, &start, &end);
        balsa_gtk_html_on_url(NULL, NULL);
    }
}

/* After wrapping the buffer, populate the start and end offsets for
 * each url. */
static void
prepare_url_offsets(GtkTextBuffer * buffer, GList * url_list)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");

    for (; url_list; url_list = g_list_next(url_list)) {
        message_url_t *url = url_list->data;
        GtkTextIter iter;

        gtk_text_buffer_get_iter_at_mark(buffer, &iter, url->end_mark);
        url->end = gtk_text_iter_get_offset(&iter);
#ifdef BUG_102711_FIXED
        gtk_text_iter_backward_to_tag_toggle(&iter, url_tag);
#else
        while (gtk_text_iter_backward_char(&iter))
            if (gtk_text_iter_begins_tag(&iter, url_tag))
                break;
#endif                          /* BUG_102711_FIXED */
        url->start = gtk_text_iter_get_offset(&iter);
    }
}

static void
url_found_cb(GtkTextBuffer * buffer, GtkTextIter * iter,
             const gchar * buf, gpointer data)
{
    GList **url_list = data;
    message_url_t *url_found;

    url_found = g_new(message_url_t, 1);
    url_found->end_mark =
        gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    url_found->url = g_strdup(buf);       /* gets freed later... */
    *url_list = g_list_append(*url_list, url_found);
}

/* if the mouse button was released over an URL, and the mouse hasn't
 * moved since the button was pressed, try to call the URL */
static gboolean
check_call_url(GtkWidget * widget, GdkEventButton * event,
               GList * url_list)
{
    gint x, y;
    message_url_t *url;

    if (event->type != GDK_BUTTON_RELEASE || event->button != 1)
        return FALSE;

    x = event->x;
    y = event->y;
    /* 2-pixel motion tolerance */
    if (abs(x - stored_x) <= 2 && abs(y - stored_y) <= 2
        && (event->state & STORED_MASK_BITS) == stored_mask) {
        url = find_url(widget, x, y, url_list);
        if (url)
            handle_url(url);
    }
    return FALSE;
}

/* find_url:
 * look in widget at coordinates x, y for a URL in url_list.
 */
static message_url_t *
find_url(GtkWidget * widget, gint x, gint y, GList * url_list)
{
    GtkTextIter iter;
    gint offset;
    message_url_t *url;

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(widget),
                                          GTK_TEXT_WINDOW_TEXT,
                                          x, y, &x, &y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(widget), &iter, x, y);
    offset = gtk_text_iter_get_offset(&iter);

    for (; url_list; url_list = g_list_next(url_list)) {
        url = (message_url_t *) url_list->data;
        if (url->start <= offset && offset < url->end)
            return url;
    }

    return NULL;
}

static gboolean
status_bar_refresh(gpointer data)
{
    gdk_threads_enter();
    gnome_appbar_refresh(balsa_app.appbar);
    gdk_threads_leave();
    return FALSE;
}
#define SCHEDULE_BAR_REFRESH()  g_timeout_add(5000, status_bar_refresh, NULL);

static void
handle_url(const message_url_t* url)
{
    if (!g_ascii_strncasecmp(url->url, "mailto:", 7)) {
        BalsaSendmsg *snd = 
            sendmsg_window_new(GTK_WIDGET(balsa_app.main_window),
                               NULL, SEND_NORMAL);
        sendmsg_window_process_url(url->url + 7,
                                   sendmsg_window_set_field, snd);      
    } else {
        gchar *notice = g_strdup_printf(_("Calling URL %s..."),
                                        url->url);
        GError *err = NULL;

        gnome_appbar_set_status(balsa_app.appbar, notice);
        SCHEDULE_BAR_REFRESH();
        g_free(notice);
        gnome_url_show(url->url, &err);
        if (err) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                    _("Error showing %s: %s\n"), url->url,
                    err->message);
            g_error_free(err);
        }
    }
}

static void
free_url_list(GList * url_list)
{
    GList *list;

    for (list = url_list; list; list = g_list_next(list)) {
        message_url_t *url_data = (message_url_t *) list->data;

        g_free(url_data->url);
        g_free(url_data);
    }
    g_list_free(url_list);
}

/* --- HTML related functions -- */
static void
balsa_gtk_html_on_url(GtkWidget *html, const gchar *url)
{
    if( url ) {
        gnome_appbar_set_status(balsa_app.appbar, url);
        SCHEDULE_BAR_REFRESH();
    } else 
        gnome_appbar_refresh(balsa_app.appbar);
}

#ifdef HAVE_GTKHTML
static void
bm_zoom_in(BalsaMessage * bm)
{
    balsa_message_zoom(bm, 1);
}

static void
bm_zoom_out(BalsaMessage * bm)
{
    balsa_message_zoom(bm, -1);
}

static void
bm_zoom_reset(BalsaMessage * bm)
{
    balsa_message_zoom(bm, 0);
}

static gboolean
balsa_gtk_html_popup(GtkWidget * html, BalsaMessage * bm)
{
    GtkWidget *menu, *menuitem;
    gpointer mime_body = g_object_get_data(G_OBJECT(html), "mime-body");

    menu = gtk_menu_new();

    menuitem = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_IN, NULL);
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(bm_zoom_in), bm);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_OUT, NULL);
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(bm_zoom_out), bm);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_100, NULL);
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(bm_zoom_reset), bm);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_separator_menu_item_new ();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    libbalsa_fill_vfs_menu_by_content_type(GTK_MENU(menu), "text/html",
					   G_CALLBACK (balsa_mime_widget_ctx_menu_vfs_cb),
					   mime_body);

    menuitem = gtk_menu_item_new_with_label (_("Save..."));
    g_signal_connect (G_OBJECT (menuitem), "activate",
                      G_CALLBACK (balsa_mime_widget_ctx_menu_save), mime_body);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);

    gtk_widget_show_all(menu);
    g_object_ref(menu);
    gtk_object_sink(GTK_OBJECT(menu));
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                   0, gtk_get_current_event_time());
    g_object_unref(menu);
    return TRUE;
}

static gboolean
balsa_gtk_html_button_press_cb(GtkWidget * html, GdkEventButton * event,
                               BalsaMessage * bm)
{
    return ((event->type == GDK_BUTTON_PRESS && event->button == 3)
            ? balsa_gtk_html_popup(html, bm) : FALSE);
}

/* balsa_gtk_html_size_request:
   report the requested size of the HTML widget.
*/
static void
balsa_gtk_html_size_request(GtkWidget * widget,
                            GtkRequisition * requisition, gpointer data)
{
    g_return_if_fail(widget != NULL);
    g_return_if_fail(requisition != NULL);

    requisition->width  = GTK_LAYOUT(widget)->hadjustment->upper;
    requisition->height = GTK_LAYOUT(widget)->vadjustment->upper;
}

static void
balsa_gtk_html_link_clicked(GObject *obj, const gchar *url)
{
    GError *err = NULL;

    gnome_url_show(url, &err);
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                _("Error showing %s: %s\n"), url, err->message);
        g_error_free(err);
    }
}

BalsaMimeWidget *
bm_widget_new_html(BalsaMessage * bm, LibBalsaMessageBody * mime_body, gchar * ptr, size_t len)
{
    BalsaMimeWidget *mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);

    mw->widget =
        libbalsa_html_new(ptr, len,
			  libbalsa_message_body_charset(mime_body),
			  bm->message,
                          G_CALLBACK(balsa_gtk_html_link_clicked));
    g_object_set_data(G_OBJECT(mw->widget), "mime-body", mime_body);

    g_signal_connect(G_OBJECT(mw->widget), "size-request",
                     G_CALLBACK(balsa_gtk_html_size_request), bm);
    g_signal_connect(G_OBJECT(mw->widget), "on-url",
                     G_CALLBACK(balsa_gtk_html_on_url), bm);
    g_signal_connect(G_OBJECT(mw->widget), "button-press-event",
                     G_CALLBACK(balsa_gtk_html_button_press_cb), bm);
    g_signal_connect(G_OBJECT(mw->widget), "key_press_event",
                     G_CALLBACK(balsa_mime_widget_key_press_event), bm);
    g_signal_connect_swapped(G_OBJECT(mw->widget), "popup-menu",
                     G_CALLBACK(balsa_gtk_html_popup), bm);

    return mw;
}
#endif /* defined HAVE_GTKHTML */
