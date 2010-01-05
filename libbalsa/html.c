/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2009 Stuart Parmenter and others,
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "html.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_GTKHTML

/* We need the declaration of LibBalsaMessage, but including "message.h"
 * directly gets into some kind of circular #include problem, whereas
 * including it indirectly through "libbalsa.h" doesn't! */
#include "libbalsa.h"

# if defined(HAVE_WEBKIT)

/*
 * Experimental support for WebKit.
 */

#include <webkit/webkit.h>

typedef struct {
    WebKitWebView        *web_view;
    WebKitWebFrame       *frame;
#if !WEBKIT_CHECK_VERSION(1, 12, 0)
    GtkAdjustment        *hadj;
    GtkAdjustment        *vadj;
#endif                          /* !WEBKIT_CHECK_VERSION(1, 12, 0) */
    LibBalsaHtmlCallback  hover_cb;
    LibBalsaHtmlCallback  clicked_cb;
    LibBalsaMessage      *message;
} LibBalsaWebKitInfo;

/*
 * Callback for the "hovering-over-link" signal
 *
 * Pass the URI to the handler set by the caller
 */
static void
lbh_hovering_over_link_cb(GtkWidget   * web_view,
                          const gchar * title,
                          const gchar * uri,
                          gpointer      data)
{
    LibBalsaWebKitInfo *info = data;

    (*info->hover_cb)(uri);
}

#if !WEBKIT_CHECK_VERSION(1, 12, 0)
static void
lbh_size_request_cb(GtkWidget      * widget,
                    GtkRequisition * requisition,
                    gpointer         data)
{
    LibBalsaWebKitInfo *info = data;
    gint upper;

    upper = gtk_adjustment_get_upper(info->hadj);
    if (upper > requisition->width)
        requisition->width = upper;
    upper = gtk_adjustment_get_upper(info->vadj);
    if (upper > requisition->height)
        requisition->height = upper;
}
#endif                          /* !WEBKIT_CHECK_VERSION(1, 12, 0) */

/*
 * Callback for the "navigation-policy-decision-requested" signal
 *
 * If the reason is that the user clicked on a link, pass the URI to to
 * the handler set by the caller; slightly complicated by links that
 * are supposed to open in their own windows.
 */
static gboolean
lbh_navigation_policy_decision_requested_cb(WebKitWebView             * web_view,
                                            WebKitWebFrame            * frame,
                                            WebKitNetworkRequest      * request,
                                            WebKitWebNavigationAction * action,
                                            WebKitWebPolicyDecision   * decision,
                                            gpointer                    data)
{
    WebKitWebNavigationReason reason;
    LibBalsaWebKitInfo *info = data;

    g_object_get(action, "reason", &reason, NULL);

    /* First request is for "about:blank"; we must allow it, but first
     * we store the main frame, so we can detect a new frame later. */
    if (!info->frame)
        info->frame = frame;
    else if (web_view == info->web_view
             && frame != info->frame
             && reason == WEBKIT_WEB_NAVIGATION_REASON_OTHER) {
        g_message("%s new frame ignored:\n URI=\"%s\"", __func__, 
                  webkit_network_request_get_uri(request));
        webkit_web_policy_decision_ignore(decision);
        return TRUE;
    }

    if (reason == WEBKIT_WEB_NAVIGATION_REASON_LINK_CLICKED ||
        (web_view != info->web_view
         && reason == WEBKIT_WEB_NAVIGATION_REASON_OTHER)) {
        (*info->clicked_cb) (webkit_network_request_get_uri(request));
        webkit_web_policy_decision_ignore(decision);
        return TRUE;
    }

    return FALSE;
}

/*
 * Callback for the "resource-request-starting" signal
 *
 * Here we get to disallow all requests involving remote servers, and to
 * handle cid: requests by replacing them with file: requests.
 */
static void
lbh_resource_request_starting_cb(WebKitWebView         * web_view,
                                 WebKitWebFrame        * frame,
                                 WebKitWebResource     * resource,
                                 WebKitNetworkRequest  * request,
                                 WebKitNetworkResponse * response,
                                 gpointer                data)
{
    const gchar *uri = webkit_network_request_get_uri(request);

    if (g_ascii_strncasecmp(uri, "cid:", 4)) {
        /* Not a "cid:" request: disable loading. */
        webkit_network_request_set_uri(request, "about:blank");
    } else {
        LibBalsaWebKitInfo *info = data;
        LibBalsaMessageBody *body;

        /* Replace "cid:" request with a "file:" request. */
        if ((body =
             libbalsa_message_get_part_by_id(info->message, uri + 4))
            && libbalsa_message_body_save_temporary(body, NULL)) {
            gchar *file_uri =
                g_strconcat("file://", body->temp_filename, NULL);
            webkit_network_request_set_uri(request, file_uri);
            g_free(file_uri);
        }
    }
}

/*
 * To handle a clicked link that is supposed to open in a new window
 * (specifically, has the attribute 'target="_blank"'), we must allow
 * WebKit to open one, but we destroy it after it has emitted the
 * "navigation-policy-decision-requested" signal.
 */

/*
 * Allow the new window only if it is needed because a link was clicked.
 */
static gboolean
lbh_new_window_policy_decision_requested_cb(WebKitWebView             * web_view,
                                            WebKitWebFrame            * frame,
                                            WebKitNetworkRequest      * request,
                                            WebKitWebNavigationAction * action,
                                            WebKitWebPolicyDecision   * decision,
                                            gpointer                    data)
{
    WebKitWebNavigationReason reason;

    g_object_get(action, "reason", &reason, NULL);

    if (reason == WEBKIT_WEB_NAVIGATION_REASON_LINK_CLICKED)
        return FALSE;

    webkit_web_policy_decision_ignore(decision);
    return TRUE;
}

/*
 * Idle handler to actually unref it.
 */
static gboolean
lbh_web_view_ready_idle(WebKitWebView * web_view)
{
    g_object_unref(g_object_ref_sink(web_view));

    return FALSE;
}

/*
 * Window is ready, now we destroy it.
 */
static gboolean
lbh_web_view_ready_cb(WebKitWebView * web_view,
                      gpointer        data)
{
    g_idle_add((GSourceFunc) lbh_web_view_ready_idle, web_view);

    return TRUE;
}

/*
 * WebKit wants a new window.
 */
static WebKitWebView *
lbh_create_web_view_cb(WebKitWebView  * web_view,
                       WebKitWebFrame * frame,
                       gpointer         data)
{
    GtkWidget *widget;

    widget = webkit_web_view_new();
    g_signal_connect(widget, "navigation-policy-decision-requested",
                     G_CALLBACK
                     (lbh_navigation_policy_decision_requested_cb), data);
    g_signal_connect(widget, "web-view-ready", G_CALLBACK(lbh_web_view_ready_cb),
                     NULL);

    return WEBKIT_WEB_VIEW(widget);
}

/* Create a new WebKitWebView widget:
 * text			the HTML source;
 * len			length of text;
 * charset		ignored;
 * message 		the LibBalsaMessage from which to extract any
 *			HTML objects (by url); ignored if NULL;
 * hover_cb             callback for link-hover signal;
 * clicked_cb	        callback for the "link-clicked" signal; ignored
 *			if NULL.
 */

GtkWidget *
libbalsa_html_new(const gchar        * text,
                  size_t               len,
                  const gchar        * charset,
                  gpointer             msg,
                  LibBalsaHtmlCallback hover_cb,
                  LibBalsaHtmlCallback clicked_cb)
{
    LibBalsaMessage *message = LIBBALSA_MESSAGE(msg);
    GtkWidget *widget;
    WebKitWebView *web_view;
    LibBalsaWebKitInfo *info;

    widget = webkit_web_view_new();

    info = g_new(LibBalsaWebKitInfo, 1);
    info->web_view = web_view = WEBKIT_WEB_VIEW(widget);
    info->frame = NULL;
    g_object_weak_ref(G_OBJECT(web_view), (GWeakNotify) g_free, info);

    g_object_set(webkit_web_view_get_settings(web_view),
                 "enable-scripts",   FALSE,
                 "enable-plugins",   FALSE,
                 NULL);

#if !WEBKIT_CHECK_VERSION(1, 12, 0)
    info->hadj =
        GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
    info->vadj =
        GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
    gtk_widget_set_scroll_adjustments(widget, info->hadj, info->vadj);
    g_object_unref(g_object_ref_sink(info->hadj));
    g_object_unref(g_object_ref_sink(info->vadj));
    g_signal_connect(web_view, "size-request",
                     G_CALLBACK(lbh_size_request_cb), info);

#endif                          /* !WEBKIT_CHECK_VERSION(1, 12, 0) */
    info->hover_cb = hover_cb;
    g_signal_connect(web_view, "hovering-over-link",
                     G_CALLBACK(lbh_hovering_over_link_cb), info);

    info->clicked_cb = clicked_cb;
    g_signal_connect(web_view, "navigation-policy-decision-requested",
                     G_CALLBACK
                     (lbh_navigation_policy_decision_requested_cb), info);
    info->message = message;
    g_signal_connect(web_view, "resource-request-starting",
                     G_CALLBACK(lbh_resource_request_starting_cb), info);
    g_signal_connect(web_view, "new-window-policy-decision-requested",
                     G_CALLBACK(lbh_new_window_policy_decision_requested_cb), info);
    g_signal_connect(web_view, "create-web-view",
                     G_CALLBACK(lbh_create_web_view_cb), info);

    g_signal_connect(web_view, "notify::progress",
                     G_CALLBACK(gtk_widget_queue_resize), NULL);

#if WEBKIT_CHECK_VERSION(1, 1, 18)
    webkit_set_cache_model(WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
#endif                          /* WEBKIT_CHECK_VERSION(1, 1, 18) */

    webkit_web_view_load_string(web_view, text, "text/html", charset,
                                NULL);

    return widget;
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
    return WEBKIT_IS_WEB_VIEW(widget);
}

/*
 * Zoom the widget.
 */
void
libbalsa_html_zoom(GtkWidget * widget, gint in_out)
{
    switch (in_out) {
    case +1:
	webkit_web_view_zoom_in(WEBKIT_WEB_VIEW(widget));
	break;
    case -1:
	webkit_web_view_zoom_out(WEBKIT_WEB_VIEW(widget));
	break;
    case 0:
	webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(widget), 1.0);
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
    return WEBKIT_IS_WEB_VIEW(widget);
}

/*
 * Select all the text.
 */
void
libbalsa_html_select_all(GtkWidget * widget)
{
    webkit_web_view_select_all(WEBKIT_WEB_VIEW(widget));
}

/*
 * Copy selected text to the clipboard.
 */
void
libbalsa_html_copy(GtkWidget * widget)
{
    webkit_web_view_copy_clipboard(WEBKIT_WEB_VIEW(widget));
}

#define WEBKIT_WEB_VIEW_CAN_MANAGE_SELECTION FALSE
#if WEBKIT_WEB_VIEW_CAN_MANAGE_SELECTION
/*
 * Does the widget support searching text?
 */
gboolean
libbalsa_html_can_search(GtkWidget * widget)
{
    return WEBKIT_IS_WEB_VIEW(widget);
}

/*
 * Search for the text; if text is empty, return TRUE (for consistency
 * with GtkTextIter methods).
 */
gboolean
libbalsa_html_search_text(GtkWidget * widget, const gchar * text,
                          gboolean find_forward, gboolean wrap)
{
    if (!*text) {
        webkit_web_view_clear_selection(WEBKIT_WEB_VIEW(widget));
        return TRUE;
    }

    return webkit_web_view_search_text(WEBKIT_WEB_VIEW(widget), text,
                                       FALSE,    /* case-insensitive */
                                       find_forward, wrap);
}

/*
 * Get the rectangle containing the currently selected text, for
 * scrolling.
 */
void
libbalsa_html_get_selection_bounds(GtkWidget    * widget,
                                   GdkRectangle * selection_bounds)
{
    webkit_web_view_get_selection_bounds(WEBKIT_WEB_VIEW(widget),
                                         selection_bounds);
}
#else                           /* WEBKIT_WEB_VIEW_CAN_MANAGE_SELECTION */
gboolean
libbalsa_html_can_search(GtkWidget * widget)
{
    return FALSE;
}

gboolean
libbalsa_html_search_text(GtkWidget * widget, const gchar * text,
                          gboolean find_forward, gboolean wrap)
{
    return FALSE;
}

void
libbalsa_html_get_selection_bounds(GtkWidget    * widget,
                                   GdkRectangle * selection_bounds)
{
}
#endif                          /* WEBKIT_WEB_VIEW_CAN_MANAGE_SELECTION */

# else                          /* defined(HAVE_WEBKIT) */

/* Common code for both GtkHtml widgets. */

/* Forward reference. */
static gboolean libbalsa_html_url_requested(GtkWidget * html,
					    const gchar * url,
					    gpointer stream,
					    LibBalsaMessage * msg);

typedef struct {
    LibBalsaHtmlCallback hover_cb;
    LibBalsaHtmlCallback clicked_cb;
} LibBalsaHTMLInfo;

static void
lbh_navigation_requested_cb(GtkWidget   * widget,
                            const gchar * uri,
                            gpointer      data)
{
    LibBalsaHTMLInfo *info = data;

    if (info->clicked_cb)
        (*info->clicked_cb)(uri);
}

static void
lbh_hovering_over_link_cb(GtkWidget   * widget,
                          const gchar * uri,
                          gpointer      data)
{
    LibBalsaHTMLInfo *info = data;

    if (info->hover_cb)
        (*info->hover_cb) (uri);
}

static void
lbh_size_request_cb(GtkWidget      * widget,
                    GtkRequisition * requisition,
                    gpointer         data)
{
    GtkLayout *layout = GTK_LAYOUT(widget);
    GtkAdjustment *adjustment;

    adjustment = gtk_layout_get_hadjustment(layout);
    requisition->width = gtk_adjustment_get_upper(adjustment);
    adjustment = gtk_layout_get_vadjustment(layout);
    requisition->height = gtk_adjustment_get_upper(adjustment);
}

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
 * charset		source charset, or NULL;
 * export_string 	if we want the text exported as text/plain, a
 * 			GString to receive it; otherwise NULL;
 * message 		the LibBalsaMessage from which to extract any
 *			HTML objects (by url); ignored if NULL;
 * link_clicked_cb	callback for the "link-clicked" signal; ignored
 *			if NULL.
 */
static GtkWidget *
lbh_new(const gchar * text, size_t len,
	const gchar * charset,
        GString * export_string)
{
    GtkWidget *html;
    GtkHTMLStream *stream;

    html = gtk_html_new();
    stream = gtk_html_begin(GTK_HTML(html));
    if (len > 0) {
        if (charset && g_ascii_strcasecmp(charset, "us-ascii") != 0
            && g_ascii_strcasecmp(charset, "utf-8") != 0) {
            const gchar *charset_iconv;
            gchar *s;
            gsize bytes_written;
            GError *error = NULL;

            charset_iconv = g_mime_charset_iconv_name(charset);
            s = g_convert(text, len, "utf-8", charset_iconv, NULL,
                          &bytes_written, &error);
            if (error) {
                g_message("(%s) error converting from %s to utf-8:\n%s\n",
                          __func__, charset_iconv, error->message);
                g_error_free(error);
                gtk_html_write(GTK_HTML(html), stream, text, len);
            } else
                gtk_html_write(GTK_HTML(html), stream, s, bytes_written);
            g_free(s);
        } else
            gtk_html_write(GTK_HTML(html), stream, text, len);
    }
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
 * charset		source charset, or NULL;
 * message 		the LibBalsaMessage from which to extract any
 *			HTML objects (by url); ignored if NULL;
 * link_clicked_cb	callback for the "link-clicked" signal; ignored
 *			if NULL.
 */
GtkWidget *
libbalsa_html_new(const gchar * text, size_t len,
                  const gchar * charset,
                  gpointer message,
                  LibBalsaHtmlCallback hover_cb,
                  LibBalsaHtmlCallback clicked_cb)
{
    GtkWidget *widget;
    LibBalsaHTMLInfo *info;

    widget = lbh_new(text, len, charset, NULL);
    info = g_new(LibBalsaHTMLInfo, 1);
    g_object_weak_ref(G_OBJECT(widget), (GWeakNotify) g_free, info);

    info->hover_cb = hover_cb;
    g_signal_connect(widget, "on-url",
                     G_CALLBACK(lbh_hovering_over_link_cb), info);

    info->clicked_cb = clicked_cb;
    g_signal_connect(widget, "link-clicked",
                     G_CALLBACK(lbh_navigation_requested_cb), info);

    g_signal_connect(widget, "url-requested",
                     G_CALLBACK(libbalsa_html_url_requested), message);

    g_signal_connect(widget, "size-request",
                     G_CALLBACK(lbh_size_request_cb), info);

    return widget;
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
    html = lbh_new(*text, len, NULL, str);
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
 * charset		ignored;
 * message 		the LibBalsaMessage from which to extract any
 *			HTML objects (by url); ignored if NULL;
 * link_clicked_cb	callback for the "link-clicked" signal; ignored
 *			if NULL.
 */

GtkWidget *
libbalsa_html_new(const gchar * text, size_t len,
		  const gchar * charset,
		  gpointer message,
                  LibBalsaHtmlCallback hover_cb,
                  LibBalsaHtmlCallback link_clicked_cb)
{
    GtkWidget *html;
    LibBalsaHTMLInfo *info;
    HtmlDocument *document;

    document = html_document_new();
    info = g_new(LibBalsaHTMLInfo, 1);
    g_object_weak_ref(G_OBJECT(document), (GWeakNotify) g_free, info);

    if (message)
	g_signal_connect(document, "request-url",
			 G_CALLBACK(libbalsa_html_url_requested), message);

    info->clicked_cb = link_clicked_cb;
    if (link_clicked_cb)
        g_signal_connect(document, "link-clicked",
                         G_CALLBACK(lbh_navigation_requested_cb), info);

    /* We need to first set_document and then do *_stream() operations
     * or gtkhtml2 will crash. */
    html = html_view_new();
    html_view_set_document(HTML_VIEW(html), document);

    info->hover_cb = hover_cb;
    g_signal_connect(html, "on-url",
                     G_CALLBACK(lbh_hovering_over_link_cb), info);

    g_signal_connect(html, "size-request",
                     G_CALLBACK(lbh_size_request_cb), info);

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

# endif				/* HAVE_GTKHTML3 */

/* Common code for both widgets. */

static gboolean
libbalsa_html_url_requested(GtkWidget * html, const gchar * url,
			    gpointer stream, LibBalsaMessage * msg)
{
    LibBalsaMessageBody *body;
    GMimeStream *mime_stream;

    if (strncmp(url, "cid:", 4)) {
	/* printf("non-local URL request ignored: %s\n", url); */
	return FALSE;
    }
    if ((body =
         libbalsa_message_get_part_by_id(msg, url + 4)) == NULL) {
	gchar *s = g_strconcat("<", url + 4, ">", NULL);

	if (s == NULL)
	    return FALSE;

	body = libbalsa_message_get_part_by_id(msg, s);
	g_free(s);
	if (body == NULL)
	    return FALSE;
    }
    if (!(mime_stream = libbalsa_message_body_get_stream(body, NULL)))
        return FALSE;

    libbalsa_mailbox_lock_store(msg->mailbox);
    g_mime_stream_reset(mime_stream);
    libbalsa_html_write_mime_stream(stream, mime_stream);
    libbalsa_mailbox_unlock_store(msg->mailbox);

    g_object_unref(mime_stream);

    return TRUE;
}

/*
 * Neither widget supports searching text
 */
gboolean
libbalsa_html_can_search(GtkWidget * widget)
{
    return FALSE;
}

gboolean
libbalsa_html_search_text(GtkWidget * widget, const gchar * text,
                          gboolean find_forward, gboolean wrap)
{
    return FALSE;
}

void
libbalsa_html_get_selection_bounds(GtkWidget    * widget,
                                   GdkRectangle * selection_bounds)
{
}

# endif                         /* defined(HAVE_WEBKIT) */

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

    filter_stream = g_mime_stream_filter_new(stream);
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
