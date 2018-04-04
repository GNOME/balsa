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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-mime-widget-text.h"

#include <string.h>
#include <stdlib.h>
#include "balsa-app.h"
#include "html.h"
#include <glib/gi18n.h>
#include "quote-color.h"
#include "sendmsg-window.h"
#include "store-address.h"
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-callbacks.h"
#include "balsa-cite-bar.h"

#if HAVE_GTKSOURCEVIEW
#include <gtksourceview/gtksource.h>
#endif


static GtkWidget * create_text_widget(const char * content_type);
static void bm_modify_font_from_string(GtkWidget * widget, const char *font);
static GtkTextTag * quote_tag(GtkTextBuffer * buffer, gint level, gint margin);
static gboolean fix_text_widget(GtkWidget *widget, gpointer data);
static void text_view_populate_popup(GtkWidget *widget, GtkMenu *menu,
                                     gpointer user_data);

#ifdef HAVE_HTML_WIDGET
static BalsaMimeWidget *bm_widget_new_html(BalsaMessage * bm,
                                           LibBalsaMessageBody *
                                           mime_body);
#endif
static BalsaMimeWidget * bm_widget_new_vcard(BalsaMessage * bm,
                                             LibBalsaMessageBody * mime_body,
                                             gchar * ptr, size_t len);

/* URL related stuff */
typedef struct _message_url_t {
    GtkTextMark *end_mark;
    gint start, end;             /* pos in the buffer */
    gchar *url;                  /* the link */
} message_url_t;


/* citation bars */
typedef struct {
    gint start_offs;
    gint end_offs;
    GtkTextIter start_iter;
    GtkTextIter end_iter;
    gint y_pos;
    gint height;
    guint depth;
    GtkWidget * bar;
} cite_bar_t;

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


static void store_button_coords(GtkGestureMultiPress *multi_press,
                                gint                  n_press,
                                gdouble               x,
                                gdouble               y,
                                gpointer              user_data);
static void check_over_url(BalsaMimeWidgetText * mwt,
                           message_url_t       * url);
static void pointer_over_url(GtkWidget * widget, message_url_t * url, gboolean set);
static void prepare_url_offsets(GtkTextBuffer * buffer, GList * url_list);
static void url_found_cb(GtkTextBuffer * buffer, GtkTextIter * iter,
                         const gchar * buf, guint len, gpointer data);
static void check_call_url(GtkGestureMultiPress *multi_press,
                           gint                  n_press,
                           gdouble               x,
                           gdouble               y,
                           gpointer              user_data);
static message_url_t * find_url(BalsaMimeWidgetText *mwt,
                                gint                 x,
                                gint                 y);
static void handle_url(const gchar* url);
static void free_url(message_url_t * url);
static void bm_widget_on_url(const gchar *url);
static void phrase_highlight(GtkTextBuffer * buffer, const gchar * id,
			     gunichar tag_char, const gchar * property,
			     gint value);
static gboolean draw_cite_bars(GtkWidget * widget,
                               cairo_t   * cr,
                               gpointer    user_data);
static gchar *check_text_encoding(BalsaMessage * bm, gchar *text_buf);
static void fill_text_buf_cited(BalsaMimeWidgetText *mwt,
                                GtkWidget           *widget,
                                const gchar         *text_body,
                                gboolean             is_flowed,
                                gboolean             is_plain);


#define PHRASE_HIGHLIGHT_ON    1
#define PHRASE_HIGHLIGHT_OFF   2

#define BALSA_MIME_WIDGET_NEW_TEXT_NOTIFIED \
    "balsa-mime-widget-text-new-notified"

#define BALSA_LEFT_MARGIN   2
#define BALSA_RIGHT_MARGIN 15

/*
 * Class definition
 */

struct _BalsaMimeWidgetTextClass {
    BalsaMimeWidgetClass parent_class;
};

struct _BalsaMimeWidgetText {
    BalsaMimeWidget      parent_instance;

    GtkWidget           *text_widget;
    GList               *url_list;
    message_url_t       *current_url;
    LibBalsaMessageBody *mime_body;
    GtkGesture          *gesture;
    GtkEventController  *controller;
    GList               *cite_bar_list;
    gint                 cite_bar_dimension;
    gint                 phrase_hl;
};

G_DEFINE_TYPE(BalsaMimeWidgetText, balsa_mime_widget_text, BALSA_TYPE_MIME_WIDGET)

static void
balsa_mime_widget_text_dispose(GObject * object) {
    BalsaMimeWidgetText *mwt;

    mwt = BALSA_MIME_WIDGET_TEXT(object);

    g_clear_object(&mwt->gesture);
    g_clear_object(&mwt->controller);

    G_OBJECT_CLASS(balsa_mime_widget_text_parent_class)->dispose(object);
}

static void
balsa_mime_widget_text_finalize(GObject * object) {
    BalsaMimeWidgetText *mwt;

    mwt = BALSA_MIME_WIDGET_TEXT(object);
    g_list_free_full(mwt->url_list, (GDestroyNotify) free_url);
    g_list_free_full(mwt->cite_bar_list, (GDestroyNotify) g_free);

    G_OBJECT_CLASS(balsa_mime_widget_text_parent_class)->finalize(object);
}

static void
balsa_mime_widget_text_class_init(BalsaMimeWidgetTextClass * klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    object_class->dispose  = balsa_mime_widget_text_dispose;
    object_class->finalize = balsa_mime_widget_text_finalize;
}

static void
balsa_mime_widget_text_init(BalsaMimeWidgetText * self)
{
}

/*
 * End of class definition
 */

/*
 * Callbacks
 */

static void
mwt_controller_motion_cb(GtkEventControllerMotion * motion,
                         gdouble                    x,
                         gdouble                    y,
                         gpointer                   user_data)
{
    BalsaMimeWidgetText *mwt = user_data;
    message_url_t *url;

    url = find_url(mwt, (gint) x, (gint) y);
    check_over_url(mwt, url);
}

static void
mwt_controller_leave_cb(GtkEventControllerMotion * motion,
                        gdouble                    x,
                        gdouble                    y,
                        gpointer                   user_data)
{
    BalsaMimeWidgetText *mwt = user_data;

    check_over_url(mwt, NULL);
}

BalsaMimeWidget *
balsa_mime_widget_new_text(BalsaMessage * bm, LibBalsaMessageBody * mime_body,
			   const gchar * content_type, gpointer data)
{
    LibBalsaHTMLType html_type;
    gchar *ptr = NULL;
    ssize_t alloced;
    BalsaMimeWidgetText *mwt;
    BalsaMimeWidget *mw;
    GtkTextBuffer *buffer;
    GError *err = NULL;
    gboolean is_text_plain;
    GtkWidget *widget;


    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    /* handle HTML if possible */
    html_type = libbalsa_html_type(content_type);
    if (html_type) {
        BalsaMimeWidget *html_widget = NULL;

#ifdef HAVE_HTML_WIDGET
        html_widget = bm_widget_new_html(bm, mime_body);
#endif
        return html_widget;
    }

    is_text_plain = !g_ascii_strcasecmp(content_type, "text/plain");
    alloced = libbalsa_message_body_get_content(mime_body, &ptr, &err);
    if (alloced < 0) {
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Could not save a text part: %s"),
                          err ? err->message : "Unknown error");
        g_clear_error(&err);
        return NULL;
    }

    if(g_ascii_strcasecmp(content_type, "text/x-vcard") == 0 ||
       g_ascii_strcasecmp(content_type, "text/directory") == 0) {
        mw = bm_widget_new_vcard(bm, mime_body, ptr, alloced);
        if (mw) {
            g_free(ptr);
            return mw;
        }
        /* else it was not a vCard with at least one address; we'll just
         * show it as if it were text/plain. */
    }

    /* verify/fix text encoding */
    ptr = check_text_encoding(bm, ptr);

    /* create the mime object and the text/source view widget */
    mwt = g_object_new(BALSA_TYPE_MIME_WIDGET_TEXT, NULL);
    mwt->text_widget = widget = create_text_widget(content_type);

    /* configure text or source view */
    gtk_text_view_set_editable(GTK_TEXT_VIEW(widget), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(widget),  BALSA_LEFT_MARGIN);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(widget), BALSA_RIGHT_MARGIN);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(widget), GTK_WRAP_WORD_CHAR);

    /* set the message font */
    if (!balsa_app.use_system_fonts)
        bm_modify_font_from_string(widget, balsa_app.message_font);

    if (libbalsa_message_body_is_flowed(mime_body)) {
	/* Parse, but don't wrap. */
	gboolean delsp = libbalsa_message_body_is_delsp(mime_body);
	ptr = libbalsa_wrap_rfc2646(ptr, G_MAXINT, FALSE, TRUE, delsp);
    } else if (bm->wrap_text
#if HAVE_GTKSOURCEVIEW
	       && !GTK_SOURCE_IS_VIEW(widget)
#endif
	       )
	libbalsa_wrap_string(ptr, balsa_app.browse_wrap_length);

    g_signal_connect(G_OBJECT(widget), "key_press_event",
		     G_CALLBACK(balsa_mime_widget_key_press_event),
		     (gpointer) bm);

    mwt->mime_body = mime_body;
    g_signal_connect(G_OBJECT(widget), "populate-popup",
		     G_CALLBACK(text_view_populate_popup), mwt);

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));

    fill_text_buf_cited(mwt, widget, ptr,
                        libbalsa_message_body_is_flowed(mime_body),
                        is_text_plain);

    prepare_url_offsets(buffer, mwt->url_list);
    g_signal_connect_after(G_OBJECT(widget), "realize",
			   G_CALLBACK(fix_text_widget), mwt->url_list);
    if (mwt->url_list != NULL) {
        mwt->gesture = gtk_gesture_multi_press_new(widget);
        g_signal_connect(mwt->gesture, "pressed",
                         G_CALLBACK(store_button_coords), NULL);
	g_signal_connect(mwt->gesture, "released",
			 G_CALLBACK(check_call_url), mwt);

        mwt->controller = gtk_event_controller_motion_new(widget);
        g_signal_connect(mwt->controller, "motion",
                         G_CALLBACK(mwt_controller_motion_cb), mwt);
        g_signal_connect(mwt->controller, "leave",
                         G_CALLBACK(mwt_controller_leave_cb), mwt);
    }

    if (is_text_plain) {
	/* plain-text highlighting */
        mwt->phrase_hl = PHRASE_HIGHLIGHT_ON;
	phrase_highlight(buffer, "hp-bold", '*', "weight", PANGO_WEIGHT_BOLD);
	phrase_highlight(buffer, "hp-underline", '_', "underline", PANGO_UNDERLINE_SINGLE);
	phrase_highlight(buffer, "hp-italic", '/', "style", PANGO_STYLE_ITALIC);
    }

    g_free(ptr);

    mw = (BalsaMimeWidget *) mwt;
    balsa_mime_widget_set_widget(mw, widget);

    return mw;
}


/* -- local functions -- */
static GtkWidget *
create_text_widget(const char * content_type)
{
#if HAVE_GTKSOURCEVIEW
    static GtkSourceLanguageManager * lm = NULL;
    static const gchar * const * lm_ids = NULL;
    GtkWidget * widget = NULL;
    gint n;

    /* we use or own highlighting for text/plain */
    if (!g_ascii_strcasecmp(content_type, "text/plain"))
	return gtk_text_view_new();
    
    /* try to initialise the source language manager and return a "simple"
     * text view if this fails */
    if (!lm)
	lm = gtk_source_language_manager_get_default();
    if (lm && !lm_ids)
	lm_ids = gtk_source_language_manager_get_language_ids(lm);
    if (!lm_ids)
	return gtk_text_view_new();
    
    /* search for a language supporting our mime type */
    for (n = 0; !widget && lm_ids[n]; n++) {
	GtkSourceLanguage * src_lang =
	    gtk_source_language_manager_get_language(lm, lm_ids[n]);
	gchar ** mime_types;

	if (src_lang &&
	    (mime_types = gtk_source_language_get_mime_types(src_lang))) {
	    gint k;

	    for (k = 0;
		 mime_types[k] && g_ascii_strcasecmp(mime_types[k], content_type);
		 k++);
	    if (mime_types[k]) {
		GtkSourceBuffer * buffer =
		    gtk_source_buffer_new_with_language(src_lang);
		
		gtk_source_buffer_set_highlight_syntax(buffer, TRUE);
		widget = gtk_source_view_new_with_buffer(buffer);
		g_object_unref(buffer);
	    }
	    g_strfreev(mime_types);
	}
    }
    
    /* fall back to the simple text view if the mime type is not supported */
    return widget ? widget : gtk_text_view_new();
#else /* no GtkSourceview */
    return gtk_text_view_new();
#endif
}

#define BALSA_MESSAGE_TEXT_HEADER "balsa-message-text-header"

static void
bm_modify_font_from_string(GtkWidget * widget, const char *font)
{
    gchar *css;
    GtkCssProvider *css_provider;

    gtk_widget_set_name(widget, BALSA_MESSAGE_TEXT_HEADER);
    css = libbalsa_font_string_to_css(font, BALSA_MESSAGE_TEXT_HEADER);

    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, css, -1);
    g_free(css);

    gtk_style_context_add_provider(gtk_widget_get_style_context(widget) ,
                                   GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);
}

/* quote_tag:
 * lookup the GtkTextTag for coloring quoted lines of a given level;
 * create the tag if it isn't found.
 *
 * returns NULL if the level is 0 (unquoted)
 */
static GtkTextTag *
quote_tag(GtkTextBuffer * buffer, gint level, gint margin)
{
    GtkTextTag *tag = NULL;

    if (level > 0) {
        GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
        gchar *name;
	gint q_level;

        /* Modulus the quote level by the max,
         * ie, always have "1 <= quote level <= MAX"
         * this allows cycling through the possible
         * quote colors over again as the quote level
         * grows arbitrarily deep. */
        q_level = (level - 1) % MAX_QUOTED_COLOR;
        name = g_strdup_printf("quote-%d", level);
        tag = gtk_text_tag_table_lookup(table, name);

        if (!tag) {
            GdkRGBA *rgba;

            rgba = &balsa_app.quoted_color[q_level];
            tag =
                gtk_text_buffer_create_tag(buffer, name,
                                           "foreground-rgba", rgba,
					   "left-margin", BALSA_LEFT_MARGIN
                                           + margin * level,
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
    if (data != NULL) {
        GdkSurface *surface = gtk_widget_get_surface(widget);

        gdk_surface_set_events(surface,
                               gdk_surface_get_events(surface) |
                               GDK_POINTER_MOTION_MASK |
                               GDK_LEAVE_NOTIFY_MASK);
    }

    if (!url_cursor_normal || !url_cursor_over_url) {
        url_cursor_normal =
            gdk_cursor_new_from_name("text", NULL);
        url_cursor_over_url =
            gdk_cursor_new_from_name("pointer", NULL);
    }
    gtk_widget_set_cursor(widget, url_cursor_normal);

    /*
     * The first time we click in a text part, the text widget grabs the
     * focus, and that leads the container that holds it to scroll to
     * its focus child, namely the text widget. That moves the text
     * under the pointer, so the click is received somewhere other than
     * was intended. Grabbing the focus on realization seems to avert
     * the scrolling.
     */
    gtk_widget_grab_focus(widget);

    return FALSE;
}

static void
gtk_widget_destroy_insensitive(GtkWidget * widget)
{
    if (!gtk_widget_get_sensitive(widget) ||
	GTK_IS_SEPARATOR_MENU_ITEM(widget))
	gtk_widget_destroy(widget);
}

static void
structured_phrases_toggle(GtkCheckMenuItem *checkmenuitem,
			  gpointer          user_data)
{
    BalsaMimeWidgetText *mwt = user_data;
    GtkTextView * text_view;
    GtkTextTagTable * table;
    GtkTextTag * tag;
    gboolean new_hl;

    text_view = GTK_TEXT_VIEW(mwt->text_widget);
    table = gtk_text_buffer_get_tag_table(gtk_text_view_get_buffer(text_view));
    new_hl = gtk_check_menu_item_get_active(checkmenuitem);
    if (!table || mwt->phrase_hl == 0 ||
	(mwt->phrase_hl == PHRASE_HIGHLIGHT_ON && new_hl) ||
	(mwt->phrase_hl == PHRASE_HIGHLIGHT_OFF && !new_hl))
	return;

    if ((tag = gtk_text_tag_table_lookup(table, "hp-bold")))
	g_object_set(G_OBJECT(tag), "weight",
		     new_hl ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
		     NULL);
    if ((tag = gtk_text_tag_table_lookup(table, "hp-underline")))
	g_object_set(G_OBJECT(tag), "underline",
		     new_hl ? PANGO_UNDERLINE_SINGLE : PANGO_UNDERLINE_NONE,
		     NULL);
    if ((tag = gtk_text_tag_table_lookup(table, "hp-italic")))
	g_object_set(G_OBJECT(tag), "style",
		     new_hl ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL,
		     NULL);

    mwt->phrase_hl = new_hl ? PHRASE_HIGHLIGHT_ON : PHRASE_HIGHLIGHT_OFF;
}

static void
url_copy_cb(GtkWidget * menu_item, message_url_t * uri)
{
    GdkClipboard *clipboard;

    clipboard = gtk_widget_get_clipboard(menu_item);
    gdk_clipboard_set_text(clipboard, uri->url);
}

static void
url_open_cb(GtkWidget * menu_item, message_url_t * uri)
{
    handle_url(uri->url);
}

static void
url_send_cb(GtkWidget * menu_item, message_url_t * uri)
{
    BalsaSendmsg * newmsg;

    newmsg = sendmsg_window_compose();
    sendmsg_window_set_field(newmsg, "body", uri->url);
}

static gboolean
text_view_url_popup(GtkWidget *widget, GtkMenu *menu, message_url_t *url)
{
    GtkWidget *menu_item;

    /* check if we are over an url */
    if (url == NULL)
	return FALSE;

    /* build a popup to copy or open the URL */
    gtk_container_foreach(GTK_CONTAINER(menu),
                          (GtkCallback)gtk_widget_destroy, NULL);

    menu_item = gtk_menu_item_new_with_label (_("Copy link"));
    g_signal_connect (G_OBJECT (menu_item), "activate",
                      G_CALLBACK (url_copy_cb), (gpointer)url);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    menu_item = gtk_menu_item_new_with_label (_("Open link"));
    g_signal_connect (G_OBJECT (menu_item), "activate",
                      G_CALLBACK (url_open_cb), (gpointer)url);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    menu_item = gtk_menu_item_new_with_label (_("Send link…"));
    g_signal_connect (G_OBJECT (menu_item), "activate",
                      G_CALLBACK (url_send_cb), (gpointer)url);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    return TRUE;
}

static void
text_view_populate_popup(GtkWidget *widget, GtkMenu *menu,
                         gpointer user_data)
{
    BalsaMimeWidgetText *mwt = user_data;
    GtkWidget *menu_item;

    gtk_widget_hide(GTK_WIDGET(menu));
    gtk_container_foreach(GTK_CONTAINER(menu),
                          (GtkCallback) gtk_widget_hide, NULL);
    if (text_view_url_popup(widget, menu, mwt->current_url))
	return;

    gtk_container_foreach(GTK_CONTAINER(menu),
                          (GtkCallback)gtk_widget_destroy_insensitive, NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
			  gtk_separator_menu_item_new ());
    libbalsa_vfs_fill_menu_by_content_type(menu, "text/plain",
					   G_CALLBACK (balsa_mime_widget_ctx_menu_cb),
					   (gpointer)mwt->mime_body);

    menu_item = gtk_menu_item_new_with_label (_("Save…"));
    g_signal_connect (G_OBJECT (menu_item), "activate",
                      G_CALLBACK (balsa_mime_widget_ctx_menu_save), mwt->mime_body);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    if (mwt->phrase_hl != 0) {
	gtk_menu_shell_append(GTK_MENU_SHELL(menu),
			      gtk_separator_menu_item_new ());
	menu_item = gtk_check_menu_item_new_with_label (_("Highlight structured phrases"));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menu_item),
                                        mwt->phrase_hl == PHRASE_HIGHLIGHT_ON);
	g_signal_connect (G_OBJECT (menu_item), "toggled",
			  G_CALLBACK (structured_phrases_toggle), mwt);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
    }
}


/* -- URL related stuff -- */

static void
store_button_coords(GtkGestureMultiPress *multi_press,
                    gint                  n_press,
                    gdouble               x,
                    gdouble               y,
                    gpointer              user_data)
{
    GtkGesture *gesture;
    const GdkEvent *event;
    GdkModifierType state;

    gesture = GTK_GESTURE(multi_press);
    event = gtk_gesture_get_last_event(gesture, gtk_gesture_get_last_updated_sequence(gesture));
    g_return_if_fail(event != NULL);

    stored_x = (gint) x;
    stored_y = (gint) y;

    if (gdk_event_get_state(event, &state)) {
        /* compare only shift, ctrl, and mod1-mod5 */
        state &= STORED_MASK_BITS;
        stored_mask = state;
    }
}

/* check if we are over an url and change the cursor in this case */
static void
check_over_url(BalsaMimeWidgetText * mwt,
               message_url_t       * url)
{
    static gboolean was_over_url = FALSE;

    if (url) {
        if (!url_cursor_normal || !url_cursor_over_url) {
            url_cursor_normal =
                gdk_cursor_new_from_name("default", NULL);
            url_cursor_over_url =
                gdk_cursor_new_from_name("pointer", NULL);
        }
        if (!was_over_url) {
            gtk_widget_set_cursor(mwt->text_widget, url_cursor_over_url);
            was_over_url = TRUE;
        }
        if (url != mwt->current_url) {
            pointer_over_url(mwt->text_widget, mwt->current_url, FALSE);
            pointer_over_url(mwt->text_widget, url, TRUE);
        }
    } else if (was_over_url) {
        gtk_widget_set_cursor(mwt->text_widget, url_cursor_normal);
        pointer_over_url(mwt->text_widget, mwt->current_url, FALSE);
        was_over_url = FALSE;
    }

    mwt->current_url = url;
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
        bm_widget_on_url(url->url);
    } else {
        gtk_text_buffer_remove_tag(buffer, tag, &start, &end);
        bm_widget_on_url(NULL);
    }
}

/* After wrapping the buffer, populate the start and end offsets for
 * each url. */
static void
prepare_url_offsets(GtkTextBuffer * buffer, GList * url_list)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *url_tag = gtk_text_tag_table_lookup(table, "url");

    for (; url_list != NULL; url_list = url_list->next) {
        message_url_t *url = url_list->data;
        GtkTextIter iter;

        gtk_text_buffer_get_iter_at_mark(buffer, &iter, url->end_mark);
        url->end = gtk_text_iter_get_offset(&iter);
#ifdef BUG_102711_FIXED
        gtk_text_iter_backward_to_tag_toggle(&iter, url_tag);
#else
        while (gtk_text_iter_backward_char(&iter)) {
            if (gtk_text_iter_starts_tag(&iter, url_tag))
                break;
        }
#endif                          /* BUG_102711_FIXED */
        url->start = gtk_text_iter_get_offset(&iter);
    }
}

static void
url_found_cb(GtkTextBuffer * buffer, GtkTextIter * iter,
             const gchar * buf, guint len, gpointer data)
{
    GList **url_list = data;
    message_url_t *url_found;

    url_found = g_new(message_url_t, 1);
    url_found->end_mark =
        gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    url_found->url = g_strndup(buf, len);       /* gets freed later... */
    *url_list = g_list_prepend(*url_list, url_found);
}

/* if the mouse button was released over an URL, and the mouse hasn't
 * moved since the button was pressed, try to call the URL */
static void
check_call_url(GtkGestureMultiPress *multi_press,
               gint                  n_press,
               gdouble               x,
               gdouble               y,
               gpointer              user_data)
{
    BalsaMimeWidgetText *mwt = user_data;
    GtkGesture *gesture;
    const GdkEvent *event;
    GdkModifierType state;

    gesture = GTK_GESTURE(multi_press);
    event = gtk_gesture_get_last_event(gesture, gtk_gesture_get_last_updated_sequence(gesture));

    if (event == NULL || !gdk_event_get_state(event, &state)) {
        return;
    }

    /* 2-pixel motion tolerance */
    if (abs(((gint) x) - stored_x) <= 2 && abs(((gint) y) - stored_y) <= 2
        && (state & STORED_MASK_BITS) == stored_mask) {
        message_url_t *url;

        url = find_url(mwt, (gint) x, (gint) y);
        if (url != NULL)
            handle_url(url->url);
    }
}

/* find_url:
 * look in widget at coordinates x, y for a URL in url_list.
 */
static message_url_t *
find_url(BalsaMimeWidgetText * mwt, gint x, gint y)
{
    GtkTextIter iter;
    gint offset;
    GList *url_list;

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(mwt->text_widget),
                                          GTK_TEXT_WINDOW_TEXT,
                                          x, y, &x, &y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(mwt->text_widget), &iter, x, y);
    offset = gtk_text_iter_get_offset(&iter);

    for (url_list = mwt->url_list; url_list != NULL; url_list = url_list->next) {
        message_url_t *url = (message_url_t *) url_list->data;

        if (url->start <= offset && offset < url->end)
            return url;
    }

    return NULL;
}

static gboolean
statusbar_pop(gpointer data)
{
    if (BALSA_IS_WINDOW(balsa_app.main_window)
        && GTK_IS_STATUSBAR(balsa_app.main_window->statusbar)) {
        GtkStatusbar *statusbar;
        guint context_id;

        statusbar = (GtkStatusbar *) balsa_app.main_window->statusbar;
        context_id = gtk_statusbar_get_context_id(statusbar, "BalsaMimeWidget message");
        gtk_statusbar_pop(statusbar, context_id);
    }

    return FALSE;
}

#define SCHEDULE_BAR_REFRESH() \
    g_timeout_add_seconds(5, statusbar_pop, NULL);

static void
handle_url(const gchar * url)
{
    if (!g_ascii_strncasecmp(url, "mailto:", 7)) {
        BalsaSendmsg *snd = sendmsg_window_compose();
        sendmsg_window_process_url(url + 7, sendmsg_window_set_field, snd);
    } else {
        GtkStatusbar *statusbar;
        guint context_id;
        gchar *notice = g_strdup_printf(_("Calling URL %s…"), url);
        GError *err = NULL;

        statusbar = GTK_STATUSBAR(balsa_app.main_window->statusbar);
        context_id =
            gtk_statusbar_get_context_id(statusbar,
                                         "BalsaMimeWidget message");
        gtk_statusbar_push(statusbar, context_id, notice);
        SCHEDULE_BAR_REFRESH();
        g_free(notice);
        gtk_show_uri_on_window(GTK_WINDOW(balsa_app.main_window), url,
                               gtk_get_current_event_time(), &err);

        if (err) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Error showing %s: %s\n"),
                              url, err->message);
            g_error_free(err);
        }
    }
}

static void
free_url(message_url_t * url)
{
    g_free(url->url);
    g_free(url);
}

/* --- Hacker's Jargon highlighting --- */
#define UNICHAR_PREV(p)  g_utf8_get_char(g_utf8_prev_char(p))

static void
phrase_highlight(GtkTextBuffer * buffer, const gchar * id, gunichar tag_char,
		 const gchar * property, gint value)
{
    GtkTextTag *tag = NULL;
    gchar * buf_chars;
    gchar * utf_start;
    GtkTextIter iter_start;
    GtkTextIter iter_end;

    /* get the utf8 buffer */
    gtk_text_buffer_get_start_iter(buffer, &iter_start);
    gtk_text_buffer_get_end_iter(buffer, &iter_end);
    buf_chars = gtk_text_buffer_get_text(buffer, &iter_start, &iter_end, TRUE);
    g_return_if_fail(buf_chars != NULL);

    /* find the tag char in the text and scan the buffer for
       <buffer start or whitespace><tag char><alnum><any text><alnum><tagchar>
       <whitespace, punctuation or buffer end> */
    utf_start = g_utf8_strchr(buf_chars, -1, tag_char);
    while (utf_start) {
	gchar * s_next = g_utf8_next_char(utf_start);

	if ((utf_start == buf_chars || g_unichar_isspace(UNICHAR_PREV(utf_start))) &&
	    *s_next != '\0' && g_unichar_isalnum(g_utf8_get_char(s_next))) {
	    gchar * utf_end;
	    gchar * line_end;
	    gchar * e_next;

	    /* found a proper start sequence - find the end or eject */
	    if (!(utf_end = g_utf8_strchr(s_next, -1, tag_char))) {
                g_free(buf_chars);
		return;
            }
	    line_end = g_utf8_strchr(s_next, -1, '\n');
	    e_next = g_utf8_next_char(utf_end);
	    while (!g_unichar_isalnum(UNICHAR_PREV(utf_end)) ||
		   !(*e_next == '\0' || 
		     g_unichar_isspace(g_utf8_get_char(e_next)) ||
		     g_unichar_ispunct(g_utf8_get_char(e_next)))) {
		if (!(utf_end = g_utf8_strchr(e_next, -1, tag_char))) {
                    g_free(buf_chars);
		    return;
                }
		e_next = g_utf8_next_char(utf_end);
	    }
	    
	    /* insert the tag if there is no line break */
	    if (!line_end || line_end >= e_next) {
		if (!tag)
		    tag = gtk_text_buffer_create_tag(buffer, id, property, value, NULL);
		gtk_text_buffer_get_iter_at_offset(buffer, &iter_start,
						   g_utf8_pointer_to_offset(buf_chars, utf_start));
		gtk_text_buffer_get_iter_at_offset(buffer, &iter_end,
						   g_utf8_pointer_to_offset(buf_chars, e_next));
		gtk_text_buffer_apply_tag(buffer, tag, &iter_start, &iter_end);
		
		/* set the next start properly */
		utf_start = *e_next ? g_utf8_strchr(e_next, -1, tag_char) : NULL;
	    } else
		utf_start = *s_next ? g_utf8_strchr(s_next, -1, tag_char) : NULL;
	} else
	    /* no start sequence, find the next start tag char */
	    utf_start = *s_next ? g_utf8_strchr(s_next, -1, tag_char) : NULL;
    }
    g_free(buf_chars);
}

/* --- citation bar stuff --- */

static void
draw_cite_bar_real(cite_bar_t * bar, BalsaMimeWidgetText * mwt)
{
    GtkTextView * view;
    GtkTextBuffer * buffer;
    gint dimension;
    GdkRectangle location;
    gint x_pos;
    gint y_pos;
    gint height;

    view = GTK_TEXT_VIEW(mwt->text_widget);
    buffer = gtk_text_view_get_buffer(view);
    dimension = mwt->cite_bar_dimension;

    /* initialise iters if we don't have the widget yet */
    if (!bar->bar) {
        gtk_text_buffer_get_iter_at_offset(buffer,
                                           &bar->start_iter,
                                           bar->start_offs);
        gtk_text_buffer_get_iter_at_offset(buffer,
                                           &bar->end_iter,
                                           bar->end_offs);
    }

    /* get the locations */
    gtk_text_view_get_iter_location(view, &bar->start_iter, &location);
    gtk_text_view_buffer_to_surface_coords(view, GTK_TEXT_WINDOW_TEXT,
                                           location.x, location.y,
                                           &x_pos, &y_pos);
    gtk_text_view_get_iter_location(view, &bar->end_iter, &location);
    gtk_text_view_buffer_to_surface_coords(view, GTK_TEXT_WINDOW_TEXT,
                                           location.x, location.y,
                                           &x_pos, &height);
    height -= y_pos;

    /* add a new widget if necessary */
    if (!bar->bar) {
#define BALSA_MESSAGE_CITE_BAR "balsa-message-cite-bar"
        gchar *color;
        gchar *css;
        GtkCssProvider *css_provider;

        bar->bar = balsa_cite_bar_new(height, bar->depth, dimension);
        gtk_widget_set_name(bar->bar, BALSA_MESSAGE_CITE_BAR);

        color =
            gdk_rgba_to_string(&balsa_app.quoted_color[(bar->depth - 1)
                                                       % MAX_QUOTED_COLOR]);
        css = g_strconcat("#" BALSA_MESSAGE_CITE_BAR " {color:", color, "}", NULL);
        g_free(color);

        css_provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css_provider, css, -1);
        g_free(css);

        gtk_style_context_add_provider(gtk_widget_get_style_context(bar->bar) ,
                                       GTK_STYLE_PROVIDER(css_provider),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css_provider);

        gtk_text_view_add_child_in_window(view, bar->bar,
                                          GTK_TEXT_WINDOW_TEXT, 0, y_pos);
    } else if (bar->y_pos != y_pos || bar->height != height) {
        /* shift/resize existing widget */
        balsa_cite_bar_resize(BALSA_CITE_BAR(bar->bar), height);
        gtk_text_view_move_child(view, bar->bar, 0, y_pos);
    }

    /* remember current values */
    bar->y_pos = y_pos;
    bar->height = height;
}


static gboolean
draw_cite_bars(GtkWidget * widget,
               cairo_t   * cr,
               gpointer    user_data)
{
    BalsaMimeWidgetText *mwt = user_data;

    g_list_foreach(mwt->cite_bar_list, (GFunc) draw_cite_bar_real, mwt);

    return FALSE;
}


/* --- HTML related functions -- */
static void
bm_widget_on_url(const gchar *url)
{
    GtkStatusbar *statusbar;
    guint context_id;

    statusbar = GTK_STATUSBAR(balsa_app.main_window->statusbar);
    context_id = gtk_statusbar_get_context_id(statusbar, "BalsaMimeWidget URL");

    if( url ) {
        gtk_statusbar_push(statusbar, context_id, url);
        SCHEDULE_BAR_REFRESH();
    } else 
        gtk_statusbar_pop(statusbar, context_id);
}

#ifdef HAVE_HTML_WIDGET
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

static void
bm_select_all_cb(GtkWidget * html)
{
    libbalsa_html_select_all(html);
}

static void
bmwt_populate_popup_menu(BalsaMessage * bm,
                         GtkWidget    * html,
                         GtkMenu      * menu)
{
    GtkWidget *menuitem;
    gpointer mime_body = g_object_get_data(G_OBJECT(html), "mime-body");

    menuitem = gtk_menu_item_new_with_label(_("Zoom In"));
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(bm_zoom_in), bm);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_label(_("Zoom Out"));
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(bm_zoom_out), bm);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_label(_("Zoom 100%"));
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(bm_zoom_reset), bm);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    if (libbalsa_html_can_select(html)) {
        menuitem = gtk_menu_item_new_with_mnemonic(_("Select _All"));
        g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                                 G_CALLBACK(bm_select_all_cb), html);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

        menuitem = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    }

    libbalsa_vfs_fill_menu_by_content_type(GTK_MENU(menu), "text/html",
                                           G_CALLBACK
                                           (balsa_mime_widget_ctx_menu_cb),
                                           mime_body);

    menuitem = gtk_menu_item_new_with_label(_("Save…"));
    g_signal_connect(G_OBJECT(menuitem), "activate",
                     G_CALLBACK(balsa_mime_widget_ctx_menu_save),
                     mime_body);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    menuitem = gtk_menu_item_new_with_label(_("Print…"));
    g_signal_connect_swapped(G_OBJECT(menuitem), "activate",
                             G_CALLBACK(libbalsa_html_print), html);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_set_sensitive(menuitem, libbalsa_html_can_print(html));
}

static gboolean
balsa_gtk_html_popup(GtkWidget * html, BalsaMessage * bm)
{
    GtkWidget *menu;
    const GdkEvent *event;
    GdkEvent *current_event = NULL;

    menu = gtk_menu_new();
    bmwt_populate_popup_menu(bm, html, GTK_MENU(menu));

    /* In WebKit2, the context menu signal is asynchronous, so the
     * GdkEvent is no longer current; instead it is preserved and passed
     * to us: */
    event = g_object_get_data(G_OBJECT(html), LIBBALSA_HTML_POPUP_EVENT);
    if (!event)
        event = current_event = gtk_get_current_event();
    if (event)
        gtk_menu_popup_at_pointer(GTK_MENU(menu),
                                  (GdkEvent *) event);
    else
        gtk_menu_popup_at_widget(GTK_MENU(menu),
                                 GTK_WIDGET(bm),
                                 GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER,
                                 NULL);
    if (current_event)
        gdk_event_free(current_event);

    return TRUE;
}

static void
mwt_gesture_pressed_cb(GtkGestureMultiPress *multi_press,
                       gint                  n_press,
                       gdouble               x,
                       gdouble               y,
                       gpointer              user_data)
{
    GtkGesture *gesture;
    const GdkEvent *event;
    GtkWidget *html;
    BalsaMessage *bm = user_data;

    gesture = GTK_GESTURE(multi_press);
    event = gtk_gesture_get_last_event(gesture, gtk_gesture_get_last_updated_sequence(gesture));
    g_return_if_fail(event != NULL);
    html = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

    if (gdk_event_triggers_context_menu(event))
        balsa_gtk_html_popup(html, bm);
}

static void
bmwt_populate_popup_cb(GtkWidget * widget, GtkMenu * menu, gpointer data)
{
    BalsaMessage *bm =
        g_object_get_data(G_OBJECT(widget), "balsa-message");
    GtkWidget *html = data;

    /* Remove WebKitWebView's items--they're irrelevant and confusing */
    gtk_container_foreach(GTK_CONTAINER(menu),
                          (GtkCallback) gtk_widget_destroy, NULL);
    bmwt_populate_popup_menu(bm, html, menu);
}

static BalsaMimeWidget *
bm_widget_new_html(BalsaMessage * bm, LibBalsaMessageBody * mime_body)
{
    BalsaMimeWidget *mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    GtkWidget *widget;
    GtkWidget *popup_menu;

    widget =
        libbalsa_html_new(mime_body,
                          (LibBalsaHtmlCallback) bm_widget_on_url,
                          (LibBalsaHtmlCallback) handle_url);
    g_object_set_data(G_OBJECT(widget), "mime-body", mime_body);

    g_signal_connect(libbalsa_html_get_view_widget(widget),
                     "key_press_event",
                     G_CALLBACK(balsa_mime_widget_key_press_event), bm);
    if ((popup_menu = libbalsa_html_popup_menu_widget(widget)) != NULL) {
        g_object_set_data(G_OBJECT(popup_menu), "balsa-message", bm);
        g_signal_connect(popup_menu, "populate-popup",
                         G_CALLBACK(bmwt_populate_popup_cb), widget);
    } else {
        BalsaMimeWidgetText *mwt = BALSA_MIME_WIDGET_TEXT(mw);

        mwt->gesture = gtk_gesture_multi_press_new(widget);
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mwt->gesture), 0);
        g_signal_connect(mwt->gesture, "pressed",
                         G_CALLBACK(mwt_gesture_pressed_cb), bm);

        g_signal_connect(widget, "popup-menu",
                         G_CALLBACK(balsa_gtk_html_popup), bm);
    }

    balsa_mime_widget_set_widget(mw, widget);

    return mw;
}
#endif /* defined HAVE_HTML_WIDGET */

#define GRID_ATTACH(g,str,label)                                  \
    if(str) { GtkWidget *lbl;                                     \
        lbl = gtk_label_new(label);                               \
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);                \
        gtk_grid_attach(g, lbl, 0, row, 1, 1);                    \
        lbl = gtk_label_new(str);                                 \
        gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);            \
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);              \
        gtk_grid_attach(g, lbl, 1, row, 1, 1);                    \
        row++;                                                    \
    }

static BalsaMimeWidget *
bm_widget_new_vcard(BalsaMessage *bm, LibBalsaMessageBody *mime_body,
                    gchar *ptr, size_t len)
{
    BalsaMimeWidget *mw;
    LibBalsaAddress *address;
    GtkWidget *widget;
    GtkGrid *grid;
    GtkWidget *w;
    int row = 1;

    address =
        libbalsa_address_new_from_vcard(ptr, mime_body->charset ?
                                        mime_body-> charset : "us-ascii");
    if (address == NULL)
        return NULL;

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    widget = gtk_grid_new();
    g_object_set_data(G_OBJECT(widget), "mime-body", mime_body);
    balsa_mime_widget_set_widget(mw, widget);

    grid = (GtkGrid *) widget;
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 12);

    w = gtk_button_new_with_mnemonic(_("S_tore Address"));
    gtk_grid_attach(grid, w, 0, 0, 2, 1);
    g_signal_connect_swapped(w, "clicked",
                             G_CALLBACK(balsa_store_address), address);
    g_object_weak_ref(G_OBJECT(mw), (GWeakNotify)g_object_unref, address);

    GRID_ATTACH(grid, libbalsa_address_get_full_name(address),    _("Full Name:"));
    GRID_ATTACH(grid, libbalsa_address_get_nick_name(address),    _("Nick Name:"));
    GRID_ATTACH(grid, libbalsa_address_get_first_name(address),   _("First Name:"));
    GRID_ATTACH(grid, libbalsa_address_get_last_name(address),    _("Last Name:"));
    GRID_ATTACH(grid, libbalsa_address_get_organization(address), _("Organization:"));
    GRID_ATTACH(grid, libbalsa_address_get_addr(address),         _("Email Address:"));

    return mw;
}

/* check for a proper text encoding, fix and notify the user if necessary */
static gchar *
check_text_encoding(BalsaMessage * bm, gchar *text_buf)
{
    const gchar *target_cs;

    if (!libbalsa_utf8_sanitize(&text_buf, balsa_app.convert_unknown_8bit,
                                &target_cs)
        && !g_object_get_data(G_OBJECT(bm->message),
                              BALSA_MIME_WIDGET_NEW_TEXT_NOTIFIED)) {
        LibBalsaMessageHeaders *headers = libbalsa_message_get_headers(bm->message);
        gchar *from = balsa_message_sender_to_gchar(headers->from, 0);
        gchar *subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(bm->message));

        libbalsa_utf8_sanitize(&from,    balsa_app.convert_unknown_8bit,
                               NULL);
        libbalsa_utf8_sanitize(&subject, balsa_app.convert_unknown_8bit,
                               NULL);
        libbalsa_information
            (LIBBALSA_INFORMATION_MESSAGE,
             _("The message sent by %s with subject “%s” contains 8-bit "
               "characters, but no header describing the used codeset "
               "(converted to %s)"),
             from, subject,
             target_cs ? target_cs : "“?”");
        g_free(subject);
        g_free(from);
        /* Avoid multiple notifications: */
        g_object_set_data(G_OBJECT(bm->message),
                          BALSA_MIME_WIDGET_NEW_TEXT_NOTIFIED,
                          GUINT_TO_POINTER(TRUE));
    }

    return text_buf;
}


static void
fill_text_buf_cited(BalsaMimeWidgetText *mwt,
                    GtkWidget           *widget,
                    const gchar         *text_body,
                    gboolean             is_flowed,
                    gboolean             is_plain)
{
    GtkWidget *label;
    LibBalsaUrlInsertInfo url_info;
    guint cite_level;
    guint cite_start;
    GtkTextTag *invisible;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    GRegex *rex = NULL;
    GdkRGBA *rgba;

    /* prepare citation regular expression for plain bodies */
    if (is_plain) {
        rex = balsa_quote_regex_new();
    }

    /* Get the width in pixels of a character in the current font.
     * These vary widely in a proportionally spaced font, but digits all
     * have the same width and it matches the previous calculation based
     * on the PangoFontDescription. */
    label = gtk_label_new("0");
    gtk_widget_measure(label, GTK_ORIENTATION_HORIZONTAL,
                       300, NULL, &mwt->cite_bar_dimension, NULL, NULL);
    g_object_ref_sink(label);
    g_object_unref(label);

    rgba = &balsa_app.url_color;
    gtk_text_buffer_create_tag(buffer, "url",
                               "foreground-rgba", rgba, NULL);
    gtk_text_buffer_create_tag(buffer, "emphasize",
                               "foreground", "red",
                               "underline", PANGO_UNDERLINE_SINGLE,
                               NULL);
    if (rex != NULL) {
        invisible = gtk_text_buffer_create_tag(buffer, "hide-cite",
                                               "size-points", (gdouble) 0.01,
                                               NULL);
    } else {
        invisible = NULL;
    }

    mwt->url_list = NULL;
    url_info.callback = url_found_cb;
    url_info.callback_data = &mwt->url_list;
    url_info.buffer_is_flowed = is_flowed;
    url_info.ml_url_buffer = NULL;

    cite_level = 0;
    cite_start = 0;
    while (*text_body) {
        const gchar *line_end;
        GtkTextTag *tag = NULL;
        int len;

        if (!(line_end = strchr(text_body, '\n')))
            line_end = text_body + strlen(text_body);

        if (rex != NULL) {
            guint quote_level;
            guint cite_idx;

            /* get the cite level only for text/plain parts */
            libbalsa_match_regex(text_body, rex, &quote_level,
                                 &cite_idx);

            /* check if the citation level changed */
            if (cite_level != quote_level) {
                if (cite_level > 0) {
                    cite_bar_t * cite_bar = g_new0(cite_bar_t, 1);

                    cite_bar->start_offs = cite_start;
                    cite_bar->end_offs = gtk_text_buffer_get_char_count(buffer);
                    cite_bar->depth = cite_level;
                    mwt->cite_bar_list =
                        g_list_append(mwt->cite_bar_list, cite_bar);
                }
                if (quote_level > 0)
                    cite_start = gtk_text_buffer_get_char_count(buffer);
                cite_level = quote_level;
            }

            /* skip the citation prefix */
            tag = quote_tag(buffer, quote_level, mwt->cite_bar_dimension);
            if (quote_level) {
                GtkTextIter cite_iter;

                gtk_text_buffer_get_iter_at_mark(buffer, &cite_iter,
                                                 gtk_text_buffer_get_insert(buffer));
                gtk_text_buffer_insert_with_tags(buffer, &cite_iter,
                                                 text_body,
                                                 cite_idx,
                                                 tag, invisible, NULL);
                text_body += cite_idx;

                /* append a zero-width space if the remainder of the line is
                 * empty, as otherwise the line is not visible (i.e.
                 * completely 0.01 pts high)... */
                if (text_body == line_end || *text_body == '\r')
                    gtk_text_buffer_insert_at_cursor(buffer, "\xE2\x80\x8B", 3);
            }
        }

        len = line_end - text_body;
        if (len > 0 && text_body[len - 1] == '\r')
            --len;
        /* tag is NULL if the line isn't quoted, but it causes
         * no harm */
        if (!libbalsa_insert_with_url(buffer, text_body, len,
                                      tag, &url_info))
            gtk_text_buffer_insert_at_cursor(buffer, "\n", 1);

        text_body = *line_end ? line_end + 1 : line_end;
    }

    /* add any pending cited part */
    if (cite_level > 0) {
        cite_bar_t * cite_bar = g_new0(cite_bar_t, 1);

        cite_bar->start_offs = cite_start;
        cite_bar->end_offs = gtk_text_buffer_get_char_count(buffer);
        cite_bar->depth = cite_level;
        mwt->cite_bar_list = g_list_append(mwt->cite_bar_list, cite_bar);
    }

    /* add list of citation bars (if any) */
    if (mwt->cite_bar_list != NULL) {
        g_signal_connect_after(widget, "draw", G_CALLBACK(draw_cite_bars), mwt);
    }

    if (rex != NULL)
        g_regex_unref(rex);
}
