/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 2021 Albrecht Dreß <albrecht.dress@arcor.de>
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

/*
 * Filtering of external resources referenced in HTML messages
 *
 * The Webkit extension expects a user message "load_ext" from the main process indicating if external resources shall be loaded.
 * If not, all images are redirected to a non-existing image, whilst other resources (e.g. fonts etc.) are simply ignored.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef HAVE_HTML_WIDGET

#if defined(GTK_DISABLE_DEPRECATED)
#define GtkAction GAction
#include <webkit2/webkit-web-extension.h>
#undef GtkAction
#else  /* defined(GTK_DISABLE_DEPRECATED) */
#include <webkit2/webkit-web-extension.h>
#endif /* defined(GTK_DISABLE_DEPRECATED) */
#include <string.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "html"


#define LOAD_EXT_KEY		"load-ext"


G_MODULE_EXPORT void webkit_web_extension_initialize(WebKitWebExtension *extension);


static gboolean
lbhf_chk_send_request(WebKitWebPage                   *web_page,
					  WebKitURIRequest                *request,
					  WebKitURIResponse G_GNUC_UNUSED *redirected_response,
					  gpointer G_GNUC_UNUSED           user_data)
{
	const gchar *uri = webkit_uri_request_get_uri(request);
	gboolean result;	/* note: TRUE to skip this request, FALSE to process it */

	result = (strcmp(uri, "about:blank") != 0) && (strncmp(uri, "cid:", 4UL) != 0);
	if (result) {
		gboolean *load_val;

		load_val = g_object_get_data(G_OBJECT(web_page), LOAD_EXT_KEY);
		if (load_val == NULL) {
			g_warning("[HTML filter] %s: no policy for loading external resource %s", __func__, uri);
		} else if (*load_val) {
			g_debug("[HTML filter] %s: accept %s", __func__, uri);
			result = FALSE;
		} else {
			webkit_uri_request_set_uri(request, "about:blank");
			g_debug("[HTML filter] %s: request for uri %s blocked", __func__, uri);
			result = FALSE;
		}
	}
	return result;
}


static gboolean
lbhf_page_user_message_cb(WebKitWebPage          *web_page,
						  WebKitUserMessage      *message,
						  gpointer G_GNUC_UNUSED  user_data)
{
	gboolean result;

	if (strcmp(webkit_user_message_get_name(message), "load_ext") == 0) {
		GVariant *data;
		gboolean *load_val;

		data = webkit_user_message_get_parameters(message);
		load_val = g_new(gboolean, 1U);
		*load_val = g_variant_get_boolean(data);
		g_debug("[HTML filter] %s: page %p: load externals = %d", __func__, web_page, *load_val);
		g_object_set_data_full(G_OBJECT(web_page), LOAD_EXT_KEY, load_val, g_free);
		result = TRUE;
	} else {
		g_debug("[HTML filter] %s: unexpected message '%s'", __func__, webkit_user_message_get_name(message));
		result = FALSE;
	}
	return result;
}


static void
lbhf_page_created_callback(WebKitWebExtension G_GNUC_UNUSED *extension,
						   WebKitWebPage                    *web_page,
						   gpointer G_GNUC_UNUSED            user_data)
{
	g_debug("[HTML filter] %s: page %p created", __func__, web_page);
	g_signal_connect(web_page, "user-message-received", G_CALLBACK(lbhf_page_user_message_cb), NULL);
	g_signal_connect(web_page, "send-request", G_CALLBACK(lbhf_chk_send_request), NULL);
}


G_MODULE_EXPORT void
webkit_web_extension_initialize(WebKitWebExtension *extension)
{
	static guint main_notified = 0U;

	g_debug("[HTML filter] %s", __func__);
	g_signal_connect(extension, "page-created", G_CALLBACK(lbhf_page_created_callback), NULL);
	if (g_atomic_int_or(&main_notified, 1U) == 0U) {
		WebKitUserMessage *message;

		/* report the main Balsa process that the HTML filter extension has been found */
		message = webkit_user_message_new("balsa-html-filter", NULL);
		webkit_web_extension_send_message_to_context(extension, message, NULL, NULL, NULL);
	}
}

#endif
