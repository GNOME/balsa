/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

/*
 * Support for HTML mail parts.
 *
 * Balsa supports one HTML engine: WebKit 2.
 * The symbol HAVE_HTML_WIDGET is defined if HTML support is requested at
 * configure time, and the requested engine is available.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "html.h"

#include <stdio.h>
#include <string.h>
#include <glib/gi18n.h>

#ifdef HAVE_HTML_WIDGET

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "html"

/*
 * lbh_get_body_content
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

#ifdef HTML2TEXT

/* common function for all Webkit versions */
static void
html2text(gchar ** text, gsize len)
{
    gchar *html2text[] = { HTML2TEXT, NULL, NULL, NULL };
    GFile *html_data;
    GFileIOStream *stream;
    GError *err = NULL;

    html_data = g_file_new_tmp("balsa-conv-XXXXXX.html", &stream, &err);
    if (html_data != NULL) {
        gsize bytes_written;
        GOutputStream *ostream =
            g_io_stream_get_output_stream(G_IO_STREAM(stream));

        if (g_output_stream_write_all(ostream, *text, len,
                                      &bytes_written, NULL, &err)) {
            gchar *result = NULL;
            gint pathidx;

            g_output_stream_flush(ostream, NULL, NULL);
#if defined(HTML2TEXT_UCOPT)
            html2text[1] = "--unicode-snob";
            pathidx = 2;
#else
            pathidx = 1;
#endif
            html2text[pathidx] = g_file_get_path(html_data);
            if (g_spawn_sync(NULL, html2text, NULL,
                             G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL,
                             &result, NULL, NULL, &err)) {
                g_free(*text);
                *text = result;
            }
            g_free(html2text[pathidx]);
        }
        g_output_stream_close(ostream, NULL, NULL);
        g_object_unref(G_OBJECT(stream));
        g_file_delete(html_data, NULL, NULL);
        g_object_unref(G_OBJECT(html_data));
    }
    if (err != NULL) {
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             _("Could not convert HTML part to text: %s"),
                             err ? err->message : "Unknown error");
        g_error_free(err);
    }
}

#else

#define html2text(p, l)

#endif

/* WebKitContextMenuItem uses GtkAction, which is deprecated.
 * We don't use it, but it breaks the git-tree build, so we just mangle
 * it: */
#if defined(GTK_DISABLE_DEPRECATED)
#define GtkAction GAction
#include <webkit2/webkit2.h>
#undef GtkAction
#else  /* defined(GTK_DISABLE_DEPRECATED) */
#include <webkit2/webkit2.h>
#endif /* defined(GTK_DISABLE_DEPRECATED) */

typedef struct {
    LibBalsaMessageBody  *body;
    LibBalsaHtmlCallback  hover_cb;
    LibBalsaHtmlCallback  clicked_cb;
    GtkWidget            *info_bar;
    WebKitWebView        *web_view;
    gchar                *uri;
    LibBalsaHtmlSearchCallback search_cb;
    gpointer                   search_cb_data;
    gchar                    * search_text;
} LibBalsaWebKitInfo;

#define LIBBALSA_HTML_INFO "libbalsa-webkit2-info"

/*
 * Unlike older HTML widgets, webkit2 wants UTF-8 text
 */
static gssize
lbh_get_body_content_utf8(LibBalsaMessageBody  * body,
                          gchar               ** utf8_text)
{
    gchar *text;
    gssize len;
    const gchar *charset;
    gsize utf8_len;

    len = lbh_get_body_content(body, &text);
    if (len < 0)
        return len;

    charset = libbalsa_message_body_charset(body);
    if (charset) {
        *utf8_text = g_convert(text, len, "UTF-8", charset,
                               NULL, &utf8_len, NULL);
        if (*utf8_text) {
            /* Success! */
            g_free(text);
            return utf8_len;
        }
    }

    /* No charset, or g_convert failed; just make sure it's UTF-8 */
    libbalsa_utf8_sanitize(&text, TRUE, NULL);
    *utf8_text = text;

    return strlen(text);
}

/*
 * GDestroyNotify func
 */
static void
lbh_webkit_info_free(LibBalsaWebKitInfo * info)
{
    if (info->uri) {
        g_free(info->uri);
        (*info->hover_cb) (NULL);
    }

    g_free(info->search_text);
    g_free(info);
}

/*
 * Callback for the "mouse-target-changed" signal
 */
static void
lbh_mouse_target_changed_cb(WebKitWebView       * web_view,
                            WebKitHitTestResult * hit_test_result,
                            guint                 modifiers,
                            gpointer              data)
{
    LibBalsaWebKitInfo *info = data;
    const gchar *uri;

    uri = webkit_hit_test_result_get_link_uri(hit_test_result);

    if (g_strcmp0(uri, info->uri) == 0)
        /* No change */
        return;

    if (info->uri != NULL) {
        g_free(info->uri);
        info->uri = NULL;
        (*info->hover_cb) (NULL);
    }

    if (uri != NULL) {
        info->uri = g_strdup(uri);
        (*info->hover_cb) (uri);
    }
}

/*
 * Callback for the "decide-policy" signal
 *
 * First, handlers for the three types of decision
 */

static void
lbh_navigation_policy_decision(WebKitPolicyDecision * decision,
                               gpointer               data)
{
    LibBalsaWebKitInfo *info = data;
    WebKitNavigationPolicyDecision *navigation_decision;
    WebKitNavigationAction *navigation_action;
    WebKitNavigationType navigation_type;
    WebKitURIRequest *request;
    const gchar *uri;

    navigation_decision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    navigation_action =
        webkit_navigation_policy_decision_get_navigation_action
        (navigation_decision);
    navigation_type =
        webkit_navigation_action_get_navigation_type(navigation_action);
    request = webkit_navigation_action_get_request(navigation_action);
    uri = webkit_uri_request_get_uri(request);

    switch (navigation_type) {
    case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED:
        g_debug("%s clicked %s", __func__, uri);
        (*info->clicked_cb) (uri);
    default:
        if (g_ascii_strcasecmp(uri, "about:blank") != 0) {
            g_debug("%s uri %s, type %d, ignored", __func__, uri, navigation_type);
        	webkit_policy_decision_ignore(decision);
        } else {
        	g_debug("%s uri %s, type %d loaded", __func__, uri, navigation_type);
        }
    }
}

static void
lbh_new_window_policy_decision(WebKitPolicyDecision * decision,
                               gpointer               data)
{
    WebKitNavigationPolicyDecision *navigation_decision;
    WebKitNavigationAction *navigation_action;
    LibBalsaWebKitInfo *info = data;
    WebKitURIRequest *request;

    navigation_decision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    navigation_action =
         webkit_navigation_policy_decision_get_navigation_action
            (navigation_decision);
    switch (webkit_navigation_action_get_navigation_type
            (navigation_action)) {
    case WEBKIT_NAVIGATION_TYPE_LINK_CLICKED:
        request = webkit_navigation_action_get_request(navigation_action);
        g_debug("%s clicked %s", __func__,
                webkit_uri_request_get_uri(request));
        (*info->clicked_cb) (webkit_uri_request_get_uri(request));
    default:
        g_debug("%s type %d, ignored", __func__,
                webkit_navigation_action_get_navigation_type
                (navigation_action));

        webkit_policy_decision_ignore(decision);
    }
}

static void
lbh_response_policy_decision(WebKitPolicyDecision * decision,
                             gpointer               data)
{
    g_debug("%s uri %s, ignored", __func__,
            webkit_uri_request_get_uri
            (webkit_response_policy_decision_get_request
             (WEBKIT_RESPONSE_POLICY_DECISION(decision))));
    webkit_policy_decision_ignore(decision);
}

static gboolean
lbh_decide_policy_cb(WebKitWebView           * web_view,
                     WebKitPolicyDecision    * decision,
                     WebKitPolicyDecisionType  decision_type,
                     gpointer                  data)
{
    switch (decision_type) {
    case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION:
        lbh_navigation_policy_decision(decision, data);
        break;
    case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION:
        lbh_new_window_policy_decision(decision, data);
        break;
    case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
        lbh_response_policy_decision(decision, data);
        break;
    default:
        /* Making no decision results in
         * webkit_policy_decision_use(). */
        return FALSE;
    }

    return TRUE;
}

/*
 * Show the GtkInfoBar for asking about downloading images
 *
 * First two signal callbacks
 */

static void
lbh_info_bar_response_cb(GtkInfoBar * info_bar,
                         gint response_id, gpointer data)
{
    LibBalsaWebKitInfo *info = data;

    if (response_id == GTK_RESPONSE_OK) {
        gchar *text;

        if (lbh_get_body_content_utf8(info->body, &text) >= 0) {
            WebKitSettings *settings;

            settings = webkit_web_view_get_settings(info->web_view);
            webkit_settings_set_auto_load_images(settings, TRUE);
            webkit_web_view_load_html(info->web_view, text, NULL);
            g_free(text);
        }
    }

    gtk_widget_destroy(info->info_bar);
    info->info_bar = NULL;
}

static void
lbh_info_bar_realize_cb(GtkInfoBar * info_bar)
{
    gtk_info_bar_set_default_response(info_bar, GTK_RESPONSE_CLOSE);
}

static GtkWidget *
lbh_info_bar(LibBalsaWebKitInfo * info)
{
    GtkWidget *info_bar_widget;
    GtkInfoBar *info_bar;
    GtkWidget *label;
    GtkWidget *content_area;
#ifdef GTK_INFO_BAR_WRAPPING_IS_BROKEN
    static const gchar text[] =
                 N_("This message part contains images "
                    "from a remote server.\n"
                    "To protect your privacy, "
                    "Balsa has not downloaded them.\n"
                    "You may choose to download them "
                    "if you trust the server.");
#else                           /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */
    static const gchar text[] =
                 N_("This message part contains images "
                    "from a remote server. "
                    "To protect your privacy, "
                    "Balsa has not downloaded them. "
                    "You may choose to download them "
                    "if you trust the server.");
#endif                          /* GTK_INFO_BAR_WRAPPING_IS_BROKEN */

    info_bar_widget =
        gtk_info_bar_new_with_buttons(_("_Download images"),
                                     GTK_RESPONSE_OK,
                                     _("_Close"), GTK_RESPONSE_CLOSE,
                                     NULL);

    info_bar = GTK_INFO_BAR(info_bar_widget);
    gtk_orientable_set_orientation(GTK_ORIENTABLE
                                   (gtk_info_bar_get_action_area
                                    (info_bar)), GTK_ORIENTATION_VERTICAL);

    label = gtk_label_new(_(text));
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);

    content_area = gtk_info_bar_get_content_area(info_bar);
    gtk_container_add(GTK_CONTAINER(content_area), label);

    g_signal_connect(info_bar, "realize",
                     G_CALLBACK(lbh_info_bar_realize_cb), info);
    g_signal_connect(info_bar, "response",
                     G_CALLBACK(lbh_info_bar_response_cb), info);
    gtk_info_bar_set_message_type(info_bar, GTK_MESSAGE_QUESTION);

    return info_bar_widget;
}

/*
 * Callback for the "resource-load-started" signal
 */
static void
lbh_resource_notify_response_cb(WebKitWebResource * resource,
                                GParamSpec        * pspec,
                                gpointer            data)
{
    LibBalsaWebKitInfo *info = data;
    const gchar *mime_type;
    WebKitURIResponse *response;

    response = webkit_web_resource_get_response(resource);
    mime_type = webkit_uri_response_get_mime_type(response);
    g_debug("%s mime-type %s", __func__, mime_type);
    if (g_ascii_strncasecmp(mime_type, "image/", 6) != 0)
        return;

    if (info->info_bar) {
        g_debug("%s %s destroy info_bar", __func__,
                webkit_web_resource_get_uri(resource));
        /* web_view is loading an image from its cache, so we do not
         * need to ask the user for permission to download */
        gtk_widget_destroy(info->info_bar);
        info->info_bar = NULL;
    } else {
        g_debug("%s %s null info_bar", __func__,
                webkit_web_resource_get_uri(resource));
    }
}

static void
lbh_resource_load_started_cb(WebKitWebView     * web_view,
                             WebKitWebResource * resource,
                             WebKitURIRequest  * request,
                             gpointer            data)
{
    const gchar *uri;

    uri = webkit_uri_request_get_uri(request);
    if (!g_ascii_strcasecmp(uri, "about:blank"))
        return;

    g_signal_connect(resource, "notify::response",
                     G_CALLBACK(lbh_resource_notify_response_cb), data);
}

/*
 * Callback for the "web-process-crashed" signal
 */
static gboolean
lbh_web_process_crashed_cb(WebKitWebView * web_view,
                           gpointer        data)
{
    g_debug("%s", __func__);
    return FALSE;
}

/*
 * WebKitURISchemeRequestCallback for "cid:" URIs
 */
static void
lbh_cid_cb(WebKitURISchemeRequest * request,
           gpointer                 data)
{
    LibBalsaWebKitInfo *info = *(LibBalsaWebKitInfo **) data;
    const gchar *path;
    LibBalsaMessageBody *body;

    path = webkit_uri_scheme_request_get_path(request);
    g_debug("%s path %s", __func__, path);

    if ((body =
         libbalsa_message_get_part_by_id(info->body->message, path))) {
        gchar *content;
        gssize len;

        len = libbalsa_message_body_get_content(body, &content, NULL);
        if (len > 0) {
            GInputStream *stream;
            gchar *mime_type;

            stream =
                g_memory_input_stream_new_from_data(content, len, g_free);
            mime_type = libbalsa_message_body_get_mime_type(body);
            webkit_uri_scheme_request_finish(request, stream, len,
                                             mime_type);
            g_object_unref(stream);
            g_free(mime_type);
        }
    }
}

/*
 * Callback for the "context-menu" signal
 */
static gboolean
lbh_context_menu_cb(WebKitWebView       * web_view,
                    WebKitContextMenu   * context_menu,
                    GdkEvent            * event,
                    WebKitHitTestResult * hit_test_result,
                    gpointer              data)
{
    GtkWidget *parent;
    gboolean retval;

    parent = gtk_widget_get_parent(GTK_WIDGET(web_view));
    /* The signal is asynchronous, so gtk_get_current_event() gets NULL;
     * we pass the event to the popup-menu handler: */
    g_object_set_data(G_OBJECT(parent), LIBBALSA_HTML_POPUP_EVENT, event);

    g_signal_emit_by_name(parent, "popup-menu", &retval);

    g_object_set_data(G_OBJECT(parent), LIBBALSA_HTML_POPUP_EVENT, NULL);

    return retval;
}

/* Create a new WebKitWebView widget:
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
    GtkWidget *widget;
    GtkWidget *vbox;
    WebKitWebView *web_view;
    static LibBalsaWebKitInfo *info;
    static gboolean have_registered_cid = FALSE;
    WebKitSettings *settings;
    static const gchar cid_regex[] =
        "<[^>]*src\\s*=\\s*['\"]?\\s*cid:";
    static const gchar src_regex[] =
        "<[^>]*src\\s*=\\s*['\"]?\\s*[^c][^i][^d][^:]";

    len = lbh_get_body_content_utf8(body, &text);
    if (len < 0)
        return NULL;

    info = g_new(LibBalsaWebKitInfo, 1);
    info->body            = body;
    info->hover_cb        = hover_cb;
    info->clicked_cb      = clicked_cb;
    info->info_bar        = NULL;
    info->uri             = NULL;
    info->search_text     = NULL;

    widget = webkit_web_view_new();
    /* WebkitWebView is uncontrollably scrollable, so if we don't set a
     * minimum size it may be just a few pixels high. */
    gtk_widget_set_size_request(widget, -1, 200);

    info->web_view = web_view = WEBKIT_WEB_VIEW(widget);
    g_object_set_data_full(G_OBJECT(web_view), LIBBALSA_HTML_INFO, info,
                           (GDestroyNotify) lbh_webkit_info_free);

    if (!have_registered_cid) {
        WebKitWebContext *context;
        /* Apparently, WebKitWebContext is static, and does not like to
         * have the scheme registered many times (13? 15?), after which
         * the web process crashes and does not get respawned.
         * We register it once with the address of a static pointer to
         * LibBalsaWebKitInfo. */

        context = webkit_web_view_get_context(web_view);
        webkit_web_context_register_uri_scheme(context, "cid", lbh_cid_cb,
                                               &info, NULL);
        have_registered_cid = TRUE;
        g_debug("%s registered cid: scheme", __func__);
    }

    settings = webkit_web_view_get_settings(web_view);
    webkit_settings_set_enable_plugins(settings, FALSE);
    webkit_settings_set_enable_javascript(settings, FALSE);
	webkit_settings_set_enable_java(settings, FALSE);
	webkit_settings_set_enable_hyperlink_auditing(settings, TRUE);
    webkit_settings_set_auto_load_images
        (settings,
         g_regex_match_simple(cid_regex, text, G_REGEX_CASELESS, 0));

    g_signal_connect(web_view, "mouse-target-changed",
                     G_CALLBACK(lbh_mouse_target_changed_cb), info);
    g_signal_connect(web_view, "decide-policy",
                     G_CALLBACK(lbh_decide_policy_cb), info);
    g_signal_connect(web_view, "resource-load-started",
                     G_CALLBACK(lbh_resource_load_started_cb), info);
    g_signal_connect(web_view, "web-process-crashed",
                     G_CALLBACK(lbh_web_process_crashed_cb), info);
    g_signal_connect(web_view, "context-menu",
                     G_CALLBACK(lbh_context_menu_cb), info);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(vbox), "libbalsa-html-web-view", web_view);
    gtk_box_pack_end(GTK_BOX(vbox), widget, TRUE, TRUE, 0);

    /* Simple check for possible resource requests: */
    if (g_regex_match_simple(src_regex, text, G_REGEX_CASELESS, 0)) {
        info->info_bar = lbh_info_bar(info);
        gtk_box_pack_start(GTK_BOX(vbox), info->info_bar, FALSE, FALSE, 0);
        g_debug("%s shows info_bar", __func__);
    }

    webkit_web_view_load_html(web_view, text, NULL);
    g_free(text);

    return vbox;
}

void
libbalsa_html_to_string(gchar ** text, size_t len)
{
    /* this widget does not support conversion to a string. */
    html2text(text, len);
}

/*
 * We may be passed either the WebKitWebView or its container:
 */
static gboolean
lbh_get_web_view(GtkWidget * widget, WebKitWebView ** web_view)
{
    if (!WEBKIT_IS_WEB_VIEW(widget))
        widget =
            g_object_get_data(G_OBJECT(widget), "libbalsa-html-web-view");

    *web_view = (WebKitWebView *) widget;

    return WEBKIT_IS_WEB_VIEW(*web_view);
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
#define LIBBALSA_HTML_ZOOM_FACTOR 1.2
void
libbalsa_html_zoom(GtkWidget * widget, gint in_out)
{
    WebKitWebView *web_view;

    if (lbh_get_web_view(widget, &web_view)) {
        gdouble zoom_level;

        zoom_level = webkit_web_view_get_zoom_level(web_view);

        switch (in_out) {
        case +1:
            zoom_level *= LIBBALSA_HTML_ZOOM_FACTOR;
            break;
        case -1:
            zoom_level /= LIBBALSA_HTML_ZOOM_FACTOR;
            break;
        case 0:
            zoom_level = 1.0;
            break;
        default:
            break;
        }

        webkit_web_view_set_zoom_level(web_view, zoom_level);
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
        webkit_web_view_execute_editing_command
            (web_view, WEBKIT_EDITING_COMMAND_SELECT_ALL);
}

/*
 * Copy selected text to the clipboard.
 */
void
libbalsa_html_copy(GtkWidget * widget)
{
    WebKitWebView *web_view;

    if (lbh_get_web_view(widget, &web_view))
        webkit_web_view_execute_editing_command
            (web_view, WEBKIT_EDITING_COMMAND_COPY);
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
 * Search for the text; if text is empty, call the callback with TRUE
 * (for consistency with GtkTextIter methods).
 */

static void
lbh_search_failed_to_find_text_cb(WebKitFindController * controller,
                                  gpointer               data)
{
    LibBalsaWebKitInfo *info = data;

    (*info->search_cb)(info->search_text, FALSE, info->search_cb_data);
}

static void
lbh_search_found_text_cb(WebKitFindController *controller,
                         guint                 match_count,
                         gpointer              data)
{
    LibBalsaWebKitInfo *info = data;

    (*info->search_cb)(info->search_text, TRUE, info->search_cb_data);
}

static void
lbh_search_init(LibBalsaWebKitInfo       * info,
                WebKitFindController     * controller,
                const gchar              * text,
                gboolean                   find_forward,
                gboolean                   wrap,
                LibBalsaHtmlSearchCallback search_cb,
                gpointer                   cb_data)
{
    guint32 find_options;

    if (!info->search_text) {
        /* First search */
        g_signal_connect(controller, "failed-to-find-text",
                         G_CALLBACK(lbh_search_failed_to_find_text_cb),
                         info);
        g_signal_connect(controller, "found-text",
                         G_CALLBACK(lbh_search_found_text_cb),
                         info);
    }

    g_free(info->search_text);
    info->search_text = g_strdup(text);
    info->search_cb = search_cb;
    info->search_cb_data = cb_data;

    if (!*text) {
        webkit_find_controller_search_finish(controller);
        (*search_cb)(text, TRUE, cb_data);
        return;
    }

    find_options = WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE;
    if (!find_forward)
        find_options |= WEBKIT_FIND_OPTIONS_BACKWARDS;
    if (wrap)
        find_options |= WEBKIT_FIND_OPTIONS_WRAP_AROUND;

    webkit_find_controller_search(controller, text, find_options,
                                  G_MAXUINT);
}

static void
lbh_search_continue(WebKitFindController * controller,
                    const gchar          * text,
                    gboolean               find_forward,
                    gboolean               wrap)
{
    guint32 find_options;
    guint32 orig_find_options;

    orig_find_options = find_options =
        webkit_find_controller_get_options(controller);

    if (!find_forward)
        find_options |= WEBKIT_FIND_OPTIONS_BACKWARDS;
    else
        find_options &= ~WEBKIT_FIND_OPTIONS_BACKWARDS;

    if (wrap)
        find_options |= WEBKIT_FIND_OPTIONS_WRAP_AROUND;
    else
        find_options &= ~WEBKIT_FIND_OPTIONS_WRAP_AROUND;

    if (find_options != orig_find_options) {
        /* No setter for find-options, so we start a new search */
        webkit_find_controller_search(controller, text, find_options,
                                      G_MAXUINT);
    } else {
        /* OK to use next/previous methods */
        if (find_forward)
            webkit_find_controller_search_next(controller);
        else
            webkit_find_controller_search_previous(controller);
    }
}

void
libbalsa_html_search(GtkWidget                * widget,
                     const gchar              * text,
                     gboolean                   find_forward,
                     gboolean                   wrap,
                     LibBalsaHtmlSearchCallback search_cb,
                     gpointer                   cb_data)
{
    WebKitWebView *web_view;
    WebKitFindController *controller;
    LibBalsaWebKitInfo *info;

    if (!lbh_get_web_view(widget, &web_view)) {
        return;
    }

    info = g_object_get_data(G_OBJECT(web_view), LIBBALSA_HTML_INFO);
    controller = webkit_web_view_get_find_controller(web_view);

    if (g_strcmp0(text, info->search_text) != 0) {
        lbh_search_init(info, controller, text, find_forward, wrap,
                        search_cb, cb_data);
    } else {
        lbh_search_continue(controller, text, find_forward, wrap);
    }
}

/*
 * We do not need selection bounds.
 */
gboolean
libbalsa_html_get_selection_bounds(GtkWidget    * widget,
                                   GdkRectangle * selection_bounds)
{
    return FALSE;
}

/*
 * Get the WebKitWebView widget from the container; we need to connect
 * to its "populate-popup" signal.
 */
GtkWidget *
libbalsa_html_popup_menu_widget(GtkWidget * widget)
{
    return NULL;
}

GtkWidget *
libbalsa_html_get_view_widget(GtkWidget * widget)
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
        static GtkPrintSettings *settings = NULL;
        WebKitPrintOperation *print_operation;
        WebKitPrintOperationResponse response;

        if (!settings)
            settings = gtk_print_settings_new();

        print_operation = webkit_print_operation_new(web_view);
        webkit_print_operation_set_print_settings(print_operation,
                                                  settings);
        response =
            webkit_print_operation_run_dialog(print_operation, NULL);
        if (response != WEBKIT_PRINT_OPERATION_RESPONSE_CANCEL) {
            g_object_unref(settings);
            settings =
                webkit_print_operation_get_print_settings(print_operation);
            g_object_ref(settings);
        }
        g_object_unref(print_operation);
    }
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

#else				/* HAVE_HTML_WIDGET */

LibBalsaHTMLType
libbalsa_html_type(const gchar * mime_type)
{
    if (!strcmp(mime_type, "text/html"))
	return LIBBALSA_HTML_TYPE_HTML;
    return LIBBALSA_HTML_TYPE_NONE;
}

#endif				/* HAVE_HTML_WIDGET */
