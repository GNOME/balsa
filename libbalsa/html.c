/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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
 * Support for HTML mail parts.
 *
 * Balsa supports both GtkHtml-2 and GtkHTML-3. Code in configure.in
 * defines the symbol HAVE_GTKHTML if either is detected, and
 * HAVE_GTKHTML3 if GtkHTML-3 is detected. This file contains all code
 * that depends on which widget is being used. Elsewhere, HTML support
 * code should be conditional on HAVE_GTKHTML, but neither HAVE_GTKHTML2
 * nor HAVE_GTKHTML3 should be referenced outside this file.
 *
 * As of this writing (2003-07), GtkHtml-2 has the more elegant design,
 * with separate concepts of document and view, but GtkHTML-3 has a far
 * more complete API.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>

#include "html.h"

#ifdef HAVE_GTKHTML

/* We need the declaration of LibBalsaMessage, but including "message.h"
 * directly gets into some kind of circular #include problem, whereas
 * including it indirectly through "libbalsa.h" doesn't! */
#include "libbalsa.h"

/* Forward reference. */
static gboolean libbalsa_html_url_requested(GtkWidget * html,
					    const gchar * url,
					    gpointer stream,
					    LibBalsaMessage * msg);

# ifdef HAVE_GTKHTML3

/* Code for GtkHTML-3 */

#  include <gtkhtml/gtkhtml.h>
#  include <gtkhtml/gtkhtml-stream.h>

/* Callback for exporting an HTML part as text/plain. */
static gboolean
libbalsa_html_receiver_fn(gpointer engine, const gchar * data, size_t len,
			  GString * export_string)
{
    g_string_append(export_string, data);
    return TRUE;
}

/* Widget-dependent helper. */
static void
libbalsa_html_write_mime_stream(GtkHTMLStream * stream,
                                GMimeStream * mime_stream)
{
    gint i;
    char buf[4096];

    while ((i = g_mime_stream_read(mime_stream, buf, sizeof(buf))) > 0)
	gtk_html_stream_write(stream, buf, i);
    gtk_html_stream_close(stream, GTK_HTML_STREAM_OK);
}

/* Helper for creating a new GtkHTML widget:
 * text			the HTML source;
 * len			length of text;
 * export_string 	if we want the text exported as text/plain, a
 * 			GString to receive it; otherwise NULL;
 * message 		the LibBalsaMessage from which to extract any
 *			HTML objects (by url); ignored if NULL;
 * link_clicked_cb	callback for the "link-clicked" signal; ignored
 *			if NULL.
 */
static GtkWidget *
lbh_new(const gchar * text, size_t len, GString * export_string,
	gpointer message, GCallback link_clicked_cb)
{
    GtkWidget *html;
    GtkHTMLStream *stream;

    html = gtk_html_new();
    if (message)
	g_signal_connect(G_OBJECT(html), "url-requested",
			 G_CALLBACK(libbalsa_html_url_requested), message);
    if (link_clicked_cb)
	g_signal_connect(G_OBJECT(html), "link-clicked",
			 link_clicked_cb, NULL);

    stream = gtk_html_begin(GTK_HTML(html));
    if (len > 0)
	gtk_html_write(GTK_HTML(html), stream, text, len);
    if (export_string)
	gtk_html_export(GTK_HTML(html), "text/plain",
			(GtkHTMLSaveReceiverFn) libbalsa_html_receiver_fn,
			export_string);
    gtk_html_end(GTK_HTML(html), stream, GTK_HTML_STREAM_OK);
    if (export_string)
	gtk_html_export(GTK_HTML(html), "text/plain",
			(GtkHTMLSaveReceiverFn) libbalsa_html_receiver_fn,
			export_string);

    gtk_html_set_editable(GTK_HTML(html), FALSE);
    gtk_html_allow_selection(GTK_HTML(html), TRUE);

    return html;
}

/* Create a new HtmlView widget:
 * text			the HTML source;
 * len			length of text;
 * message 		the LibBalsaMessage from which to extract any
 *			HTML objects (by url); ignored if NULL;
 * link_clicked_cb	callback for the "link-clicked" signal; ignored
 *			if NULL.
 */
GtkWidget *
libbalsa_html_new(const gchar * text, size_t len,
		  gpointer message, GCallback link_clicked_cb)
{
    return lbh_new(text, len, NULL, message, link_clicked_cb);
}

/* Use an HtmlView widget to convert html text to a (null-terminated) string:
 * text			the HTML source;
 * len			length of text;
 * frees and reallocates the string.
 */
void
libbalsa_html_to_string(gchar ** text, size_t len)
{
    GtkWidget *html;
    GString *str;

    str = g_string_new(NULL);	/* We want only the text, in str. */
    html = lbh_new(*text, len, str, NULL, NULL);
    gtk_widget_destroy(html);

    g_free(*text);
    *text = g_string_free(str, FALSE);
}

/*
 * Does the widget support zoom?
 */
gboolean
libbalsa_html_can_zoom(GtkWidget * widget)
{
    return GTK_IS_HTML(widget);
}

/*
 * Zoom the widget.
 */
void
libbalsa_html_zoom(GtkWidget * widget, gint in_out)
{
    switch (in_out) {
    case +1:
	gtk_html_zoom_in(GTK_HTML(widget));
	break;
    case -1:
	gtk_html_zoom_out(GTK_HTML(widget));
	break;
    case 0:
	gtk_html_zoom_reset(GTK_HTML(widget));
	break;
    default:
	break;
    }
}

/*
 * Does the widget support selecting text?
 */
gboolean
libbalsa_html_can_select(GtkWidget * widget)
{
    return GTK_IS_HTML(widget);
}

/*
 * Select all the text.
 */
void
libbalsa_html_select_all(GtkWidget * widget)
{
    gtk_html_select_all(GTK_HTML(widget));
}

/*
 * Copy selected text to the clipboard.
 */
void
libbalsa_html_copy(GtkWidget * widget)
{
    gtk_html_copy(GTK_HTML(widget));
}

/*
 * GtkHTML printing suport.
 */

#ifdef HAVE_GNOME
gboolean
libbalsa_html_can_print(void)
{
    return TRUE;
}
/*
 * Print the page(s) in the widget with a header and a footer.
 */
void
libbalsa_html_print(GtkWidget * widget,
		    GnomePrintContext * print_context,
		    gdouble header_height, gdouble footer_height,
		    LibBalsaHTMLPrintCallback header_print,
		    LibBalsaHTMLPrintCallback footer_print,
		    gpointer user_data)
{
    gtk_html_print_with_header_footer(GTK_HTML(widget), print_context,
				      header_height, footer_height,
				      (GtkHTMLPrintCallback) header_print,
				      (GtkHTMLPrintCallback) footer_print,
				      user_data);
}

/*
 * Return the number of pages that will be printed.
 */
gint
libbalsa_html_print_get_pages_num(GtkWidget * widget,
				  GnomePrintContext * print_context,
				  gdouble header_height,
				  gdouble footer_height)
{
    return gtk_html_print_get_pages_num(GTK_HTML(widget), print_context,
					header_height, footer_height);
}
#else /* HAVE_GNOME */
gboolean
libbalsa_html_can_print(void)
{
    return FALSE;
}

#endif /* HAVE_GNOME */
# else				/* HAVE_GTKHTML3 */

/* Code for GtkHtml-2 */

#  include <libgtkhtml/gtkhtml.h>

/* Widget-dependent helper. */
static void
libbalsa_html_write_mime_stream(HtmlStream * stream, 
                                GMimeStream * mime_stream)
{
    gint i;
    char buf[4096];

    while ((i = g_mime_stream_read(mime_stream, buf, sizeof(buf))) > 0)
	html_stream_write(stream, buf, i);
    html_stream_close(stream);
}

/* Create a new HtmlView widget:
 * text			the HTML source;
 * len			length of text;
 * message 		the LibBalsaMessage from which to extract any
 *			HTML objects (by url); ignored if NULL;
 * link_clicked_cb	callback for the "link-clicked" signal; ignored
 *			if NULL.
 */

GtkWidget *
libbalsa_html_new(const gchar * text, size_t len,
		  gpointer message, GCallback link_clicked_cb)
{
    GtkWidget *html;
    HtmlDocument *document;

    document = html_document_new();
    if (message)
	g_signal_connect(G_OBJECT(document), "request-url",
			 G_CALLBACK(libbalsa_html_url_requested), message);
    if (link_clicked_cb)
	g_signal_connect(G_OBJECT(document), "link-clicked",
			 link_clicked_cb, NULL);

    /* We need to first set_document and then do *_stream() operations
     * or gtkhtml2 will crash. */
    html = html_view_new();
    html_view_set_document(HTML_VIEW(html), document);

    html_document_open_stream(document, "text/html");
    html_document_write_stream(document, text, len);
    html_document_close_stream(document);

    return html;
}

void
libbalsa_html_to_string(gchar ** text, size_t len)
{
    return; /* this widget does not support conversion to a string. The
             * string won't be altered. Other alternative would be to set
             * it to an empty string. */
}

/*
 * Does the widget support zoom?
 */
gboolean
libbalsa_html_can_zoom(GtkWidget * widget)
{
    return HTML_IS_VIEW(widget);
}

/*
 * Zoom the widget.
 */
void
libbalsa_html_zoom(GtkWidget * widget, gint in_out)
{
    switch (in_out) {
    case +1:
	html_view_zoom_in(HTML_VIEW(widget));
	break;
    case -1:
	html_view_zoom_out(HTML_VIEW(widget));
	break;
    case 0:
	html_view_zoom_reset(HTML_VIEW(widget));
	break;
    default:
	break;
    }
}

/*
 * HtmlView doesn't support selecting text.
 */
gboolean
libbalsa_html_can_select(GtkWidget * widget)
{
    return FALSE;
}

/*
 * Do nothing.
 */
void
libbalsa_html_select_all(GtkWidget * widget)
{
}

/*
 * Do nothing.
 */
void
libbalsa_html_copy(GtkWidget * widget)
{
}

/*
 * HtmlView doesn't support printing.
 */
gboolean
libbalsa_html_can_print(void)
{
    return FALSE;
}

#ifdef HAVE_GNOME
/*
 * Do nothing.
 */
void
libbalsa_html_print(GtkWidget * widget,
		    GnomePrintContext * print_context,
		    gdouble header_height, gdouble footer_height,
		    LibBalsaHTMLPrintCallback header_print,
		    LibBalsaHTMLPrintCallback footer_print,
		    gpointer user_data)
{
}

/*
 * Return nothing.
 */
gint
libbalsa_html_print_get_pages_num(GtkWidget * widget,
				  GnomePrintContext * print_context,
				  gdouble header_height,
				  gdouble footer_height)
{
    return 0;
}
#endif /* HAVE_GNOME */

# endif				/* HAVE_GTKHTML3 */

/* Common code for both widgets. */

static gboolean
libbalsa_html_url_requested(GtkWidget * html, const gchar * url,
			    gpointer stream, LibBalsaMessage * msg)
{
    GMimeStream *mime_stream;

    if (strncmp(url, "cid:", 4)) {
	printf("non-local URL request ignored: %s\n", url);
	return FALSE;
    }
    if ((mime_stream =
         libbalsa_message_get_part_by_id(msg, url + 4)) == NULL) {
	gchar *s = g_strconcat("<", url + 4, ">", NULL);

	if (s == NULL)
	    return FALSE;

	mime_stream = libbalsa_message_get_part_by_id(msg, s);
	g_free(s);
	if (mime_stream == NULL)
	    return FALSE;
    }

    libbalsa_html_write_mime_stream(stream, mime_stream);

    g_object_unref(mime_stream);

    return TRUE;
}

/* Filter text/enriched or text/richtext to text/html, if we have GMime
 * >= 2.1.0; free and reallocate the text. */
guint
libbalsa_html_filter(LibBalsaHTMLType html_type, gchar ** text, guint len)
{
    guint retval = len;
    GMimeStream *stream;
    GByteArray *array;
    GMimeStream *filter_stream;
    GMimeFilter *filter;
    guint32 flags;
    guint8 zero = 0;

    switch (html_type) {
    case LIBBALSA_HTML_TYPE_ENRICHED:
	flags = 0;
	break;
    case LIBBALSA_HTML_TYPE_RICHTEXT:
	flags = GMIME_FILTER_ENRICHED_IS_RICHTEXT;
	break;
    case LIBBALSA_HTML_TYPE_HTML:
    default:
	return retval;
    }

    stream = g_mime_stream_mem_new();
    array = g_byte_array_new();
    g_mime_stream_mem_set_byte_array(GMIME_STREAM_MEM(stream), array);

    filter_stream = g_mime_stream_filter_new_with_stream(stream);
    g_object_unref(stream);

    filter = g_mime_filter_enriched_new(flags);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
    g_object_unref(filter);

    g_mime_stream_write(filter_stream, *text, len);
    g_object_unref(filter_stream);

    g_byte_array_append(array, &zero, 1);

    retval = array->len;
    g_free(*text);
    *text = (gchar *) g_byte_array_free(array, FALSE);
    return retval;
}

LibBalsaHTMLType
libbalsa_html_type(const gchar * mime_type)
{
    if (!strcmp(mime_type, "text/html"))
	return LIBBALSA_HTML_TYPE_HTML;
    if (!strcmp(mime_type, "text/enriched"))
	return LIBBALSA_HTML_TYPE_ENRICHED;
    if (!strcmp(mime_type, "text/richtext"))
	return LIBBALSA_HTML_TYPE_RICHTEXT;
    return LIBBALSA_HTML_TYPE_NONE;
}

#else				/* HAVE_GTKHTML */

LibBalsaHTMLType
libbalsa_html_type(const gchar * mime_type)
{
    if (!strcmp(mime_type, "text/html"))
	return LIBBALSA_HTML_TYPE_HTML;
    return LIBBALSA_HTML_TYPE_NONE;
}

#endif				/* HAVE_GTKHTML */
