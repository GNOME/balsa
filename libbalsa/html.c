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
#include <glib/gi18n.h>

#ifdef HAVE_GTKHTML

/*
 * Used by all HTML widgets
 *
 * returns -1 on error
 * otherwise, caller must g_free buf
 */
static gssize
lbh_get_body_content(LibBalsaMessageBody * body, gchar ** buf)
{
    gssize len;
    GError *err = NULL;
    LibBalsaHTMLType html_type;
    gchar *content_type;

    len = libbalsa_message_body_get_content(body, buf, &err);
    if (len < 0) {
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             _("Could not get an HTML part: %s"),
                             err ? err->message : "Unknown error");
        g_error_free(err);
        return len;
    }

    content_type = libbalsa_message_body_get_mime_type(body);
    html_type = libbalsa_html_type(content_type);
    g_free(content_type);

    return libbalsa_html_filter(html_type, buf, len);
}

# if defined(HAVE_WEBKIT)

/*
 * Experimental support for WebKit.
 */

#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>

typedef struct {
    LibBalsaMessageBody  *body;
    LibBalsaHtmlCallback  hover_cb;
    LibBalsaHtmlCallback  clicked_cb;
    WebKitWebFrame       *frame;
#if GTK_CHECK_VERSION(2, 18, 0)
    gboolean              download_images;
    GtkWidget            *info_bar_widget;
#endif /* GTK_CHECK_VERSION(2, 18, 0) */
    WebKitWebView        *web_view;
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
    LibBalsaWebKitInfo *info = data;

    if (!g_ascii_strcasecmp(uri, "about:blank"))
        return;

    if (g_ascii_strncasecmp(uri, "cid:", 4)) {
        /* Not a "cid:" request: disable loading. */
#if GTK_CHECK_VERSION(2, 18, 0)
        static GHashTable *cache = NULL;

        if (!cache)
            cache = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, NULL);

        if (!g_hash_table_lookup(cache, uri)) {
            if (info->download_images) {
                g_hash_table_insert(cache, g_strdup(uri),
                                    GINT_TO_POINTER(TRUE));
            } else {
                webkit_network_request_set_uri(request, "about:blank");
                gtk_widget_show_all(info->info_bar_widget);
                gtk_info_bar_set_default_response(GTK_INFO_BAR
                                                  (info->info_bar_widget),
                                                  GTK_RESPONSE_CLOSE);
            }
        }
#else  /* GTK_CHECK_VERSION(2, 18, 0) */
        webkit_network_request_set_uri(request, "about:blank");
#endif /* GTK_CHECK_VERSION(2, 18, 0) */
    } else {
        LibBalsaMessageBody *body;

        /* Replace "cid:" request with a "file:" request. */
        if ((body =
             libbalsa_message_get_part_by_id(info->body->message, uri + 4))
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

#if GTK_CHECK_VERSION(2, 18, 0)
/*
 * Make the GtkInfoBar for asking about downloading images
 */

static void
lbh_info_bar_response_cb(GtkInfoBar * info_bar,
                         gint response_id, gpointer user_data)
{
    LibBalsaWebKitInfo *info = user_data;

    if (response_id == GTK_RESPONSE_OK) {
        gchar *text;

        if (lbh_get_body_content(info->body, &text) >= 0) {
            info->download_images = TRUE;
            webkit_web_view_reload_bypass_cache(info->web_view);
            webkit_web_view_load_string(info->web_view, text, "text/html",
                                        libbalsa_message_body_charset
                                        (info->body), NULL);
            g_free(text);
        }
    }

    gtk_widget_destroy(GTK_WIDGET(info_bar));
}

static void
lbh_info_bar_realize_cb(GtkWidget * info_bar,
                        GtkWidget * text_view)
{
    GtkStyle *style = gtk_style_copy(gtk_widget_get_style(info_bar));

    style->base[GTK_STATE_NORMAL] = style->bg[GTK_STATE_NORMAL];
    gtk_widget_set_style(text_view, style);
    g_object_unref(style);
    gtk_widget_hide(info_bar);
}

static GtkWidget *
lbh_info_bar_widget(GtkWidget * widget, LibBalsaWebKitInfo * info)
{
    GtkWidget *info_bar_widget;
    GtkInfoBar *info_bar;
    GtkWidget *text_view_widget;
    GtkTextView *text_view;
    GtkWidget *content_area;
    gchar *text = _("This message part contains images "
                    "from a remote server. "
                    "To protect your privacy, "
                    "Balsa has not downloaded them. "
                    "You may choose to download them "
                    "if you trust the server.");

    info_bar_widget =
        gtk_info_bar_new_with_buttons(_("_Download images"), GTK_RESPONSE_OK,
                                      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                      NULL);

    info_bar = GTK_INFO_BAR(info_bar_widget);
    gtk_info_bar_set_message_type(info_bar, GTK_MESSAGE_QUESTION);
    g_signal_connect(info_bar, "response",
                     G_CALLBACK(lbh_info_bar_response_cb), info);

    text_view_widget = gtk_text_view_new();
    g_signal_connect(info_bar_widget, "realize",
                     G_CALLBACK(lbh_info_bar_realize_cb), text_view_widget);

    content_area = gtk_info_bar_get_content_area(info_bar);
    gtk_container_add(GTK_CONTAINER(content_area), text_view_widget);

    text_view = GTK_TEXT_VIEW(text_view_widget);
    gtk_text_view_set_wrap_mode(text_view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(text_view, FALSE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(text_view),
                             text, -1);

    return info_bar_widget;
}
#endif /* GTK_CHECK_VERSION(2, 18, 0) */

/* Create a new WebKitWebView widget:
 * text			the HTML source;
 * len			length of text;
 * body 		LibBalsaMessageBody that belongs to the
 *                      LibBalsaMessage from which to extract any
 *			HTML objects (by url);
 * hover_cb             callback for link-hover signal;
 * clicked_cb	        callback for the "link-clicked" signal;
 */

GtkWidget *
libbalsa_html_new(LibBalsaMessageBody * body,
                  LibBalsaHtmlCallback  hover_cb,
                  LibBalsaHtmlCallback  clicked_cb)
{
    gchar *text;
    gssize len;
    GtkWidget *vbox;
    GtkWidget *widget;
    WebKitWebView *web_view;
    LibBalsaWebKitInfo *info;

    len = lbh_get_body_content(body, &text);
    if (len < 0)
        return NULL;

    vbox = gtk_vbox_new(FALSE, 0);

    widget = webkit_web_view_new();

    info = g_new(LibBalsaWebKitInfo, 1);
    info->body            = body;
    info->hover_cb        = hover_cb;
    info->clicked_cb      = clicked_cb;
    info->frame           = NULL;
#if GTK_CHECK_VERSION(2, 18, 0)
    info->download_images = FALSE;
    info->info_bar_widget = lbh_info_bar_widget(widget, info);

    gtk_box_pack_start(GTK_BOX(vbox), info->info_bar_widget,
                       FALSE, FALSE, 0);
#endif /* GTK_CHECK_VERSION(2, 18, 0) */
    gtk_box_pack_start(GTK_BOX(vbox), widget, TRUE, TRUE, 0);

    info->web_view = web_view = WEBKIT_WEB_VIEW(widget);
    g_object_set_data(G_OBJECT(vbox), "libbalsa-html-web-view", web_view);
    g_object_set_data_full(G_OBJECT(web_view), "libbalsa-html-info", info,
                           g_free);

    g_object_set(webkit_web_view_get_settings(web_view),
                 "enable-scripts",   FALSE,
                 "enable-plugins",   FALSE,
                 NULL);

    g_signal_connect(web_view, "hovering-over-link",
                     G_CALLBACK(lbh_hovering_over_link_cb), info);
    g_signal_connect(web_view, "navigation-policy-decision-requested",
                     G_CALLBACK
                     (lbh_navigation_policy_decision_requested_cb), info);
    g_signal_connect(web_view, "resource-request-starting",
                     G_CALLBACK(lbh_resource_request_starting_cb), info);
    g_signal_connect(web_view, "new-window-policy-decision-requested",
                     G_CALLBACK(lbh_new_window_policy_decision_requested_cb),
                     info);
    g_signal_connect(web_view, "create-web-view",
                     G_CALLBACK(lbh_create_web_view_cb), info);

    g_signal_connect(web_view, "notify::progress",
                     G_CALLBACK(gtk_widget_queue_resize), NULL);

    webkit_web_view_load_string(web_view, text, "text/html",
                                libbalsa_message_body_charset(body), NULL);
    g_free(text);

    return vbox;
}

void
libbalsa_html_to_string(gchar ** text, size_t len)
{
    return; /* this widget does not support conversion to a string. The
             * string won't be altered. Other alternative would be to set
             * it to an empty string. */
}

static gboolean
lbh_get_web_view(GtkWidget * widget, WebKitWebView ** web_view)
{
    *web_view =
        g_object_get_data(G_OBJECT(widget), "libbalsa-html-web-view");

    return *web_view && WEBKIT_IS_WEB_VIEW(*web_view);
}

/*
 * Does the widget support zoom?
 */
gboolean
libbalsa_html_can_zoom(GtkWidget * widget)
{
    WebKitWebView *web_view;

    return lbh_get_web_view(widget, &web_view);
}

/*
 * Zoom the widget.
 */
void
libbalsa_html_zoom(GtkWidget * widget, gint in_out)
{
    WebKitWebView *web_view;

    if (lbh_get_web_view(widget, &web_view)) {
        switch (in_out) {
        case +1:
            webkit_web_view_zoom_in(web_view);
            break;
        case -1:
            webkit_web_view_zoom_out(web_view);
            break;
        case 0:
            webkit_web_view_set_zoom_level(web_view, 1.0);
            break;
        default:
            break;
        }
    }
}

/*
 * Does the widget support selecting text?
 */
gboolean
libbalsa_html_can_select(GtkWidget * widget)
{
    WebKitWebView *web_view;

    return lbh_get_web_view(widget, &web_view);
}

/*
 * Select all the text.
 */
void
libbalsa_html_select_all(GtkWidget * widget)
{
    WebKitWebView *web_view;

    if (lbh_get_web_view(widget, &web_view))
        webkit_web_view_select_all(web_view);
}

/*
 * Copy selected text to the clipboard.
 */
void
libbalsa_html_copy(GtkWidget * widget)
{
    WebKitWebView *web_view;

    if (lbh_get_web_view(widget, &web_view))
        webkit_web_view_copy_clipboard(web_view);
}

/*
 * Does the widget support searching text?
 */
gboolean
libbalsa_html_can_search(GtkWidget * widget)
{
    WebKitWebView *web_view;

    return lbh_get_web_view(widget, &web_view);
}

/*
 * JavaScript-based helpers for text search
 */
static JSGlobalContextRef
lbh_js_get_global_context(WebKitWebView * web_view)
{
    WebKitWebFrame *web_frame = webkit_web_view_get_main_frame(web_view);
    return webkit_web_frame_get_global_context(web_frame);
}

static JSValueRef
lbh_js_run_script(JSContextRef  ctx,
                  const gchar * script)
{
    JSStringRef str;
    JSValueRef value;

    str = JSStringCreateWithUTF8CString(script);
    value = JSEvaluateScript(ctx, str, NULL, NULL, 0, NULL);
    JSStringRelease(str);

    return value;
}

static gint
lbh_js_object_get_property(JSContextRef  ctx,
                           JSObjectRef   object,
                           const gchar * property_name)
{
    JSStringRef str  = JSStringCreateWithUTF8CString(property_name);
    JSValueRef value = JSObjectGetProperty(ctx, object, str, NULL);
    JSStringRelease(str);

    return (gint) JSValueToNumber(ctx, value, NULL);
}

/*
 * Search for the text; if text is empty, return TRUE (for consistency
 * with GtkTextIter methods).
 */
gboolean
libbalsa_html_search_text(GtkWidget   * widget,
                          const gchar * text,
                          gboolean      find_forward,
                          gboolean      wrap)
{
    WebKitWebView *web_view;

    if (!lbh_get_web_view(widget, &web_view))
        return FALSE;

    if (!*text) {
        gchar script[] = "window.getSelection().removeAllRanges()";

        lbh_js_run_script(lbh_js_get_global_context(web_view), script);

        return TRUE;
    }

    return webkit_web_view_search_text(web_view, text,
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
    WebKitWebView *web_view;
    JSGlobalContextRef ctx;
    gchar script[] =
        "window.getSelection().getRangeAt(0).getBoundingClientRect()";
    JSValueRef value;

    if (!lbh_get_web_view(widget, &web_view))
        return;

    ctx = lbh_js_get_global_context(web_view);
    value = lbh_js_run_script(ctx, script);

    if (value && JSValueIsObject(ctx, value)) {
        JSObjectRef object = JSValueToObject(ctx, value, NULL);
        gint x, y;

        x = lbh_js_object_get_property(ctx, object, "left");
        y = lbh_js_object_get_property(ctx, object, "top");

        gtk_widget_translate_coordinates(GTK_WIDGET(web_view), widget,
                                         x, y,
                                         &selection_bounds->x,
                                         &selection_bounds->y);

        selection_bounds->width =
            lbh_js_object_get_property(ctx, object, "width");
        selection_bounds->height =
            lbh_js_object_get_property(ctx, object, "height");
    }
}

/*
 * Get the WebKitWebView widget from the container; we need to connect
 * to its "populate-popup" signal.
 */
GtkWidget *
libbalsa_html_popup_menu_widget(GtkWidget * widget)
{
    WebKitWebView *web_view;

    return lbh_get_web_view(widget, &web_view) ?
        GTK_WIDGET(web_view) : NULL;
}

/*
 * Does the widget support printing?
 */
gboolean
libbalsa_html_can_print(GtkWidget * widget)
{
    WebKitWebView *web_view;

    return lbh_get_web_view(widget, &web_view);
}

/*
 * Print the widget's content.
 */
void
libbalsa_html_print(GtkWidget * widget)
{
    WebKitWebView *web_view;

    if (lbh_get_web_view(widget, &web_view)) {
        WebKitWebFrame *frame = webkit_web_view_get_main_frame(web_view);
        webkit_web_frame_print(frame);
    }
}

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
 * body 		LibBalsaMessageBody that belongs to the
 *                      LibBalsaMessage from which to extract any
 *			HTML objects (by url);
 * hover_cb             callback for the "on-url" signal;
 * link_clicked_cb	callback for the "link-clicked" signal;
 */
GtkWidget *
libbalsa_html_new(LibBalsaMessageBody * body,
                  LibBalsaHtmlCallback  hover_cb,
                  LibBalsaHtmlCallback  clicked_cb)
{
    gssize len;
    gchar *text;
    GtkWidget *widget;
    LibBalsaHTMLInfo *info;

    len = lbh_get_body_content(body, &text);
    if (len < 0)
        return NULL;

    widget = lbh_new(text, len, libbalsa_message_body_charset(body), NULL);
    g_free(text);

    info = g_new(LibBalsaHTMLInfo, 1);
    g_object_weak_ref(G_OBJECT(widget), (GWeakNotify) g_free, info);

    info->hover_cb = hover_cb;
    g_signal_connect(widget, "on-url",
                     G_CALLBACK(lbh_hovering_over_link_cb), info);

    info->clicked_cb = clicked_cb;
    g_signal_connect(widget, "link-clicked",
                     G_CALLBACK(lbh_navigation_requested_cb), info);

    g_signal_connect(widget, "url-requested",
                     G_CALLBACK(libbalsa_html_url_requested),
                     body->message);

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

/*
 * Does the widget support printing?
 */
gboolean
libbalsa_html_can_print(GtkWidget * widget)
{
    return GTK_IS_HTML(widget);
}

/*
 * Print the widget's content.
 */
void
libbalsa_html_print(GtkWidget * widget)
{
    GtkPrintOperation *operation = gtk_print_operation_new();
    gtk_html_print_operation_run(GTK_HTML(widget), operation,
                                 GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    g_object_unref(operation);
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
 * body        		LibBalsaMessageBody that belongs to the
 *                      LibBalsaMessage from which to extract any
 *                      HTML objects (by url);
 * hover_cb             callback for the "on-url" signal;
 * link_clicked_cb	callback for the "link-clicked" signal;
 */

GtkWidget *
libbalsa_html_new(LibBalsaMessageBody  * body,
                  LibBalsaHtmlCallback   hover_cb,
                  LibBalsaHtmlCallback   link_clicked_cb)
{
    HtmlDocument *document;
    LibBalsaHTMLInfo *info;
    GtkWidget *html;
    gssize len;
    gchar *text;

    len = lbh_get_body_content(body, &text);
    if (len < 0)
        return NULL;

    document = html_document_new();
    info = g_new(LibBalsaHTMLInfo, 1);
    g_object_weak_ref(G_OBJECT(document), (GWeakNotify) g_free, info);

    g_signal_connect(document, "request-url",
                     G_CALLBACK(libbalsa_html_url_requested),
                     body->message);

    info->clicked_cb = link_clicked_cb;
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
    g_free(text);
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
libbalsa_html_can_print(GtkWidget * widget)
{
    return FALSE;
}

/*
 * Do nothing.
 */
void
libbalsa_html_print(GtkWidget * widget)
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

/*
 * Neither widget implements its own popup widget.
 */
GtkWidget *
libbalsa_html_popup_menu_widget(GtkWidget *widget)
{
    return NULL;
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
