/* LibBalsaWebdav - Webdav base class for Balsa
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@posteo.de> 2023
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see
 * <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif						/* HAVE_CONFIG_H */


#if defined(HAVE_WEBDAV)

#include <libxml/parser.h>
#include <libxml/xpathInternals.h>
#include <glib/gi18n.h>
#include "net-client-utils.h"
#include "libbalsa-webdav.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#  define G_LOG_DOMAIN "webdav"
#endif


#define XML_HDR				"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"


typedef struct _LibBalsaWebdavPrivate LibBalsaWebdavPrivate;

struct _LibBalsaWebdavPrivate {
	gchar *uri;
	gchar *username;
	gchar *password;
};


G_DEFINE_TYPE_WITH_PRIVATE(LibBalsaWebdav, libbalsa_webdav, SOUP_TYPE_SESSION)


static void libbalsa_webdav_finalise(GObject *object);
static xmlDocPtr webdav_request(LibBalsaWebdav *webdav,
								const gchar    *path,
								const gchar    *method,
								const gchar    *depth,
								const gchar    *req_xml,
								GError        **error)
	G_GNUC_WARN_UNUSED_RESULT;

#if SOUP_CHECK_VERSION(3, 0, 0)

static gboolean do_soup_auth(SoupMessage *msg,
							 SoupAuth    *auth,
							 gboolean     retrying,
							 gpointer     user_data);
static void do_soup_msg_restarted(SoupMessage *msg,
								  gpointer user_data);

#else				/* libsoup 2.4 */

static void do_soup_auth(SoupSession *session,
						 SoupMessage *msg,
						 SoupAuth    *auth,
						 gboolean     retrying,
						 gpointer     user_data);

#endif				/* libsoup 2.4 vs. 3 */

static GList *get_text_string(xmlNodeSetPtr      nodes,
							  xmlXPathContextPtr xpath_ctx,
							  gpointer           user_data)
	G_GNUC_WARN_UNUSED_RESULT;
static GList *extract_resources(xmlNodeSetPtr      nodes,
								xmlXPathContextPtr xpath_ctx,
								gpointer           user_data)
	G_GNUC_WARN_UNUSED_RESULT;
static gint cmp_webdav_resource(const libbalsa_webdav_resource_t *a,
								const libbalsa_webdav_resource_t *b);
static GList *ensure_absolute_resource_uri(const gchar *host,
										   const gchar *path,
										   GList       *list)
	G_GNUC_WARN_UNUSED_RESULT;
static xmlXPathContextPtr init_xpath_ctx(xmlDocPtr      doc,
										 gchar * const *ns_list,
										 GError       **error)
G_GNUC_WARN_UNUSED_RESULT;


LibBalsaWebdav *
libbalsa_webdav_new(void)
{
	return LIBBALSA_WEBDAV(g_object_new(LIBBALSA_WEBDAV_TYPE, NULL));
}


/* documentation: see header file */
void
libbalsa_webdav_setup(LibBalsaWebdav *webdav, const gchar *uri, const gchar *username, const gchar *password)
{
	LibBalsaWebdavPrivate *priv;

	g_return_if_fail(LIBBALSA_IS_WEBDAV(webdav) && (uri != NULL) && (strncmp(uri, "https://", 8UL) == 0) && (username != NULL));
	priv = libbalsa_webdav_get_instance_private(webdav);
	g_free(priv->uri);
	priv->uri = g_strdup(uri);
	g_free(priv->username);
	priv->username = g_strdup(username);
	if (priv->password != NULL) {
		memset(priv->password, 0, strlen(priv->password));
		g_free(priv->password);
	}
	priv->password = g_strdup(password);
}


/* documentation: see header file */
GList *
libbalsa_webdav_get_resource(LibBalsaWebdav *webdav, const gchar *path, const gchar *homeset, const gchar *resource,
							 const gchar *namespace, GError **error)
{
	static const libbalsa_webdav_propfind_t get_principals = {
		.item = "<a:current-user-principal/>",
		.depth = "0",
		.xpath = WEBDAV_MULTISTATUS_XP "[" WEBDAV_OK_XP "]/a:prop/a:current-user-principal/a:href/text()",
		.ns_list = { NULL },
		.eval_fn = libbalsa_webdav_xp_get_strlist
	};
	LibBalsaWebdavPrivate *priv;
	GList *principals;
	GList *p;
	GError *local_err = NULL;
	GList *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav) && (homeset != NULL) && (resource != NULL) && (namespace != NULL), NULL);
	priv = libbalsa_webdav_get_instance_private(webdav);

	/* find the principals */
	principals = libbalsa_webdav_propfind(webdav, path, &get_principals, NULL, &local_err);
	for (p = principals; (local_err == NULL) && (p != NULL); p = p->next) {
		libbalsa_webdav_propfind_t get_homeset = {
			.depth = "0",
			.ns_list = { "b", (gchar *) namespace, NULL},
			.eval_fn = libbalsa_webdav_xp_get_strlist
		};
		libbalsa_webdav_propfind_t get_resource = {
			.item = "<a:resourcetype/><a:displayname/>",
			.depth = "1",
			.ns_list = { "b", (gchar *) namespace, NULL},
			.eval_fn = extract_resources
		};
		GList *homesets;
		GList *q;

		g_debug("%s: checking principal '%s'", __func__, (const gchar *) p->data);
		/* get the homesets */
		get_homeset.item = g_strdup_printf("<b:%s/>", homeset);
		get_homeset.xpath = g_strdup_printf("%s[%s]/a:prop/b:%s/a:href/text()", WEBDAV_MULTISTATUS_XP, WEBDAV_OK_XP, homeset);
		get_resource.xpath =
			g_strdup_printf("%s[%s and a:prop/a:resourcetype/b:%s]/..", WEBDAV_MULTISTATUS_XP, WEBDAV_OK_XP, resource);
		homesets = libbalsa_webdav_propfind(webdav,(const gchar *) p->data, &get_homeset, NULL, &local_err);
		for (q = homesets; (local_err == NULL) && (q != NULL); q = q->next) {
			const gchar *this_path = (const gchar *) q->data;
			GList *res_list;

			/* extract resources */
			g_debug("%s:   checking homeset '%s'", __func__, this_path);
			res_list = libbalsa_webdav_propfind(webdav, this_path, &get_resource, NULL, &local_err);

			/* ensure absolute URI's */
			result = g_list_concat(result, ensure_absolute_resource_uri(priv->uri, this_path, res_list));
		}
		g_free(get_homeset.item);
		g_free(get_homeset.xpath);
		g_free(get_resource.xpath);
		g_list_free_full(homesets, g_free);
	}
	g_list_free_full(principals, g_free);

	if (local_err != NULL) {
		g_list_free_full(result, (GDestroyNotify) libbalsa_webdav_resource_free);
		result = NULL;
		g_propagate_error(error, local_err);
	}
	return result;
}


/* documentation: see header file */
GList *
libbalsa_webdav_propfind(LibBalsaWebdav *webdav, const gchar *path, const libbalsa_webdav_propfind_t *task, gpointer user_data,
						 GError **error)
{
	GString *req_xml;
	int n;
	xmlDocPtr doc;
	GList *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav) && (task != NULL) && (task->eval_fn != NULL) && (task->item != NULL) &&
		(task->xpath != NULL), NULL);

	/* construct the request XML */
	req_xml = g_string_new(XML_HDR "<a:propfind xmlns:a=\"DAV:\"");
	for (n = 0; task->ns_list[n] != NULL; n += 2) {
		g_string_append_printf(req_xml, " xmlns:%s=\"%s\"", task->ns_list[n], task->ns_list[n + 1]);
	}
	g_string_append_printf(req_xml, "><a:prop>%s</a:prop></a:propfind>", task->item);

	/* run the request */
	doc = webdav_request(webdav, path, "PROPFIND", task->depth, req_xml->str, error);
	g_string_free(req_xml, TRUE);

	/* evaluate XML result */
	if (doc != NULL) {
		xmlXPathContextPtr xpath_ctx;

		xpath_ctx = init_xpath_ctx(doc, task->ns_list, error);
		if (xpath_ctx != NULL) {
			xmlXPathObjectPtr xpath_obj;

			xpath_obj = xmlXPathEvalExpression((const xmlChar *) task->xpath, xpath_ctx);
			if (xpath_obj != NULL) {
				if (!xmlXPathNodeSetIsEmpty(xpath_obj->nodesetval)) {
					result = task->eval_fn(xpath_obj->nodesetval, xpath_ctx, user_data);
				}
				xmlXPathFreeObject(xpath_obj);
			} else {
				g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("failed to evaluate XPath expression"));
			}

			xmlXPathFreeContext(xpath_ctx);
		}
		xmlFreeDoc(doc);
	}

	return result;
}


/* documentation: see header file */
GList *
libbalsa_webdav_report(LibBalsaWebdav *webdav, const gchar *path, const libbalsa_webdav_report_t *task, gpointer user_data,
					   GError **error)
{
	GString *req_xml;
	int n;
	xmlDocPtr doc;
	GList *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav) && (task != NULL) && (task->eval_fn != NULL) && (task->name != NULL) &&
		(task->body != NULL) && (task->xpath != NULL), NULL);

	/* construct the request XML */
	req_xml = g_string_new(XML_HDR);
	g_string_append_printf(req_xml, "<%s xmlns:a=\"DAV:\"", task->name);
	for (n = 0; task->ns_list[n] != NULL; n += 2) {
		g_string_append_printf(req_xml, " xmlns:%s=\"%s\"", task->ns_list[n], task->ns_list[n + 1]);
	}
	g_string_append_c(req_xml, '>');
	g_string_append_printf(req_xml, "%s</%s>", task->body, task->name);

	/* run the request */
	doc = webdav_request(webdav, path, "REPORT", task->depth, req_xml->str, error);
	g_string_free(req_xml, TRUE);

	/* evaluate XML result */
	if (doc != NULL) {
		xmlXPathContextPtr xpath_ctx;

		xpath_ctx = init_xpath_ctx(doc, task->ns_list, error);
		if (xpath_ctx != NULL) {
			xmlXPathObjectPtr xpath_obj;

			xpath_obj = xmlXPathEvalExpression((const xmlChar *) task->xpath, xpath_ctx);
			if (xpath_obj != NULL) {
				if (!xmlXPathNodeSetIsEmpty(xpath_obj->nodesetval)) {
					result = task->eval_fn(xpath_obj->nodesetval, xpath_ctx, user_data);
				}
				xmlXPathFreeObject(xpath_obj);
			} else {
				g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("failed to evaluate XPath expression"));
			}

			xmlXPathFreeContext(xpath_ctx);
		}
		xmlFreeDoc(doc);
	}

	return result;
}


#if SOUP_CHECK_VERSION(3, 0, 0)

/* documentation: see header file */
gchar *
libbalsa_webdav_put(LibBalsaWebdav *webdav, const gchar *data, const gchar *mime_type, const gchar *name, const gchar *etag,
					GError **error)
{
	LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(webdav);
	gchar *full_uri;
	SoupMessage *msg;
	GBytes *buffer;
	SoupMessageHeaders *msg_headers;
	GBytes *reply;
	SoupStatus status;
	GError *local_err = NULL;
	gchar *new_etag = NULL;

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav) && (data != NULL) && (mime_type != NULL) && (name != NULL), NULL);

	if (strncmp(name, "https://", 8UL) == 0) {
		full_uri = g_strdup(name);
	} else if (g_str_has_suffix(priv->uri, "/")) {
		full_uri = g_strconcat(priv->uri, name, NULL);
	} else {
		full_uri = g_strconcat(priv->uri, "/", name, NULL);
	}
	msg = soup_message_new("PUT", full_uri);
	g_free(full_uri);

	buffer = g_bytes_new_static(data, strlen(data));
	soup_message_set_request_body_from_bytes(msg, mime_type, buffer);
	g_signal_connect(msg, "authenticate", G_CALLBACK(do_soup_auth), webdav);
	g_signal_connect(msg, "restarted", G_CALLBACK(do_soup_msg_restarted), buffer);
	msg_headers = soup_message_get_request_headers(msg);
	if (etag != NULL) {
		soup_message_headers_append(msg_headers, "If-Match", etag);
	} else {
		soup_message_headers_append(msg_headers, "If-None-Match", "*");
	}
	reply = soup_session_send_and_read(SOUP_SESSION(webdav), msg, NULL, &local_err);
	status = soup_message_get_status(msg);
	/* response code must be 201 (CREATED) */
	if (status != SOUP_STATUS_CREATED) {
		if (local_err != NULL) {
			g_propagate_error(error, local_err);
		} else {
			/* Translators: #1 HTTP (RFC 2616) status code, #2 HTTP status message*/
			g_set_error(error, WEBDAV_ERROR_QUARK, (int) status, _("WebDAV response %d: %s"), (int) status,
				soup_message_get_reason_phrase(msg));
		}
	} else {
		const gchar *etag_val;

		msg_headers = soup_message_get_response_headers(msg);
		etag_val = soup_message_headers_get_one(msg_headers, "ETag");
		/* some broken servers do not return a new etag as required by the RFC's... */
		new_etag = g_strdup((etag_val != NULL) ? etag_val : "");
	}
	g_object_unref(msg);
	g_bytes_unref(reply);
	g_bytes_unref(buffer);

	return new_etag;
}

#else				/* libsoup 2.4 */

/* documentation: see header file */
gchar *
libbalsa_webdav_put(LibBalsaWebdav *webdav, const gchar *data, const gchar *mime_type, const gchar *name, const gchar *etag,
					GError **error)
{
	LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(webdav);
	gchar *full_uri;
	SoupMessage *msg;
	guint result;
	gchar *new_etag = NULL;

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav) && (data != NULL) && (mime_type != NULL) && (name != NULL), NULL);

	if (strncmp(name, "https://", 8UL) == 0) {
		full_uri = g_strdup(name);
	} else if (g_str_has_suffix(priv->uri, "/")) {
		full_uri = g_strconcat(priv->uri, name, NULL);
	} else {
		full_uri = g_strconcat(priv->uri, "/", name, NULL);
	}
	msg = soup_message_new("PUT", full_uri);
	g_free(full_uri);

	soup_message_set_request(msg, mime_type, SOUP_MEMORY_STATIC, data, strlen(data));
	if (etag != NULL) {
		soup_message_headers_append(msg->request_headers, "If-Match", etag);
	} else {
		soup_message_headers_append(msg->request_headers, "If-None-Match", "*");
	}
	result = soup_session_send_message(SOUP_SESSION(webdav), msg);
	/* response code must be 201 (CREATED) */
	if ((result != SOUP_STATUS_CREATED) || (msg->status_code != SOUP_STATUS_CREATED)) {
		/* Translators: #1 HTTP (RFC 2616) status code, #2 HTTP status message*/
		g_set_error(error, WEBDAV_ERROR_QUARK, result, _("WebDAV response %d: %s"), msg->status_code, msg->reason_phrase);
	} else {
		const gchar *etag_val = soup_message_headers_get_one(msg->response_headers, "ETag");

		/* some broken servers do not return a new etag as required by the RFC's... */
		new_etag = g_strdup((etag_val != NULL) ? etag_val : "");
	}
	g_object_unref(msg);

	return new_etag;
}

#endif				/* libsoup 2.4 vs. 3 */


/* documentation: see header file */
gchar *
libbalsa_webdav_get_ctag(LibBalsaWebdav *webdav, GError **error)
{
	static const libbalsa_webdav_propfind_t get_ctag = {
		.item = "<c:getctag/>",
		.depth = "0",
		.xpath = WEBDAV_MULTISTATUS_XP "[" WEBDAV_OK_XP "]/a:prop/c:getctag/text()",
		.ns_list = { "c", WEBDAV_NS_CALSERVER, NULL },
		.eval_fn = get_text_string
	};
	gchar *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav), NULL);
	g_list_free(libbalsa_webdav_propfind(webdav, NULL, &get_ctag, &result, error));
	return result;
}


/* documentation: see header file */
gchar *
libbalsa_webdav_get_sync_token(LibBalsaWebdav *webdav, GError **error)
{
	static const libbalsa_webdav_propfind_t get_synctok = {
		.item = "<a:sync-token/>",
		.depth = "0",
		.xpath = WEBDAV_MULTISTATUS_XP "[" WEBDAV_OK_XP "]/a:prop/a:sync-token/text()",
		.ns_list = { NULL },
		.eval_fn = get_text_string
	};
	gchar *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav), NULL);
	g_list_free(libbalsa_webdav_propfind(webdav, NULL, &get_synctok, &result, error));
	return result;
}


/* documentation: see header file */
const gchar *
libbalsa_webdav_get_uri(LibBalsaWebdav *webdav)
{
	LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(webdav);

	g_return_val_if_fail(LIBBALSA_IS_WEBDAV(webdav), NULL);
	return priv->uri;
}


/* documentation: see header file */
void
libbalsa_webdav_disconnect(LibBalsaWebdav *webdav)
{
	g_return_if_fail(LIBBALSA_IS_WEBDAV(webdav));

	soup_session_abort(SOUP_SESSION(webdav));
}


/* documentation: see header file */
void
libbalsa_webdav_resource_free(libbalsa_webdav_resource_t *resource)
{
	if (resource != NULL) {
		g_free(resource->href);
		g_free(resource->name);
		g_free(resource);
	}
}


/* documentation: see header file */
GList *
libbalsa_webdav_xp_get_strlist(xmlNodeSetPtr nodes, xmlXPathContextPtr G_GNUC_UNUSED xpath_ctx, gpointer G_GNUC_UNUSED user_data)
{
	GList *result = NULL;
	int n;

	for (n = 0; n < nodes->nodeNr; n++) {
		if (nodes->nodeTab[n]->type == XML_TEXT_NODE) {
			if (g_list_find_custom(result, nodes->nodeTab[n]->content, (GCompareFunc) strcmp) == NULL) {
				result = g_list_prepend(result, g_strdup((const gchar *) nodes->nodeTab[n]->content));
			}
		}
	}
	return g_list_reverse(result);
}


/* documentation: see header file */
gchar *
libbalsa_webdav_xp_string_from_sub(xmlNodePtr node, const xmlChar *sub_expr, xmlXPathContextPtr xpath_ctx)
{
	xmlXPathObjectPtr xpath_sub;
	gchar *result = NULL;

	xpath_sub = xmlXPathNodeEval(node, sub_expr, xpath_ctx);
	if (xpath_sub != NULL) {
		if ((xmlXPathNodeSetGetLength(xpath_sub->nodesetval) > 0) && (xpath_sub->nodesetval->nodeTab[0]->type == XML_TEXT_NODE)) {
			result = g_strdup((const gchar *) xpath_sub->nodesetval->nodeTab[0]->content);
		}
		xmlXPathFreeObject(xpath_sub);
	}
	return result;
}


/* documentation: see header file */
gchar *
libbalsa_webdav_lookup_srv(const gchar *domain, const gchar *service, gchar **path, GError **error)
{
	GResolver *resolver;
	GList *res;
	gchar *result = NULL;

	resolver = g_resolver_get_default();
	res = g_resolver_lookup_service(resolver, service, "tcp", domain, NULL, error);
	if (res != NULL) {
		GSrvTarget *target = (GSrvTarget *) res->data;
		const gchar *host;
		guint16 port;

		host = g_srv_target_get_hostname(target);
		port = g_srv_target_get_port(target);
		if ((host != NULL) && (host[0] != '\0') && (port > 0U)) {
			if (port == 443U) {
				result = g_strconcat("https://", host, NULL);
			} else {
				result = g_strdup_printf("https://%s:%hu", host, port);
			}
		}
		g_resolver_free_targets(res);

		/* check for an initial "context path" */
		if (result != NULL) {
			gchar *srv_val;
			gchar *ctx_path = NULL;

			srv_val = g_strdup_printf("_%s._tcp.%s", service, domain);
			res = g_resolver_lookup_records(resolver, srv_val, G_RESOLVER_RECORD_TXT, NULL, NULL);
			if (res != NULL) {
				GList *p;

				for (p = res; p != NULL; p = p->next) {
					GVariant *value = (GVariant *) p->data;
					GVariantIter *iter;
					const gchar *txt_item;

					g_variant_get(value, "(as)", &iter);
					if (g_variant_iter_next(iter, "&s", &txt_item)) {
						if (strncmp(txt_item, "path=", 5U) == 0) {
							g_free(ctx_path);
							ctx_path = g_strdup(&txt_item[5]);
						}
					}
					g_variant_iter_free(iter);
				}
				g_list_free_full(res, (GDestroyNotify) g_variant_unref);
			}
			*path = ctx_path;
			g_free(srv_val);
		}
	}
	g_object_unref(resolver);

	return result;
}


/* == local functions =========================================================================================================== */

static void
libbalsa_webdav_class_init(LibBalsaWebdavClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	gobject_class->finalize = libbalsa_webdav_finalise;
}


static void
libbalsa_webdav_init(LibBalsaWebdav *self)
{
#if SOUP_CHECK_VERSION(3, 0, 0)
	soup_session_set_user_agent(SOUP_SESSION(self), "balsa/" BALSA_VERSION " ");
	soup_session_set_timeout(SOUP_SESSION(self), 30U);
#else
	g_object_set(self, SOUP_SESSION_USER_AGENT, "balsa/" BALSA_VERSION " ", SOUP_SESSION_TIMEOUT, 30U, NULL);
	g_signal_connect(self, "authenticate", G_CALLBACK(do_soup_auth), self);
#endif		/* libsoup 2.4/3 */
}


static void
libbalsa_webdav_finalise(GObject *object)
{
	LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(LIBBALSA_WEBDAV(object));
	const GObjectClass *parent_class = G_OBJECT_CLASS(libbalsa_webdav_parent_class);

	/* make sure any session is terminated before freeing user name and password */
	soup_session_abort(SOUP_SESSION(object));
	g_free(priv->uri);
	g_free(priv->username);
	if (priv->password != NULL) {
		memset(priv->password, 0, strlen(priv->password));
		g_free(priv->password);
	}
	(*parent_class->finalize)(object);
}


#if SOUP_CHECK_VERSION(3, 0, 0)

/** @brief Authenticate the Soup session
 *
 * @param[in] auth Soup authentication object
 * @param[in] retrying FALSE for the first authentication attempt only
 * @param[in] user_data LibBalsaWebdav object
 * @return always @c FALSE
 *
 * Iff @em retrying is @c FALSE authenticate by calling soup_auth_authenticate().  Otherwise, the function is a no-op causing the
 * triggering request to fail.
 */
static gboolean
do_soup_auth(SoupMessage *msg, SoupAuth *auth, gboolean retrying, gpointer user_data)
{
	LibBalsaWebdav *webdav = LIBBALSA_WEBDAV(user_data);

	if (!retrying) {
		LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(webdav);

		soup_auth_authenticate(auth, priv->username, priv->password);
	}
	return FALSE;
}


/** @brief Soup message restart callback
 *
 * @param[in] msg triggering Soup message
 * @param[in] user_data Soup message payload (actually GBytes *)
 */
static void
do_soup_msg_restarted(SoupMessage *msg, gpointer user_data)
{
	soup_message_set_request_body_from_bytes(msg, NULL, user_data);
}


/** @brief Run a Webdav request
 *
 * @param[in] session Webdav soup session
 * @param[in] host host or full request path
 * @param[in] path request path, @c NULL iff host contains the full path, the relative path if it starts with `/`, or the full path
 *            otherwise
 * @param[in] method request method
 * @param[in] depth value of the @c Depth header, @c NULL to omit the header
 * @param[in] req_xml request XML document
 * @param[in,out] error return location for an error, may be @c NULL
 * @return parsed XML reply document upon success, @c NULL on error
 */
static xmlDocPtr
webdav_request(LibBalsaWebdav *webdav, const gchar *path, const gchar *method, const gchar *depth, const gchar *req_xml,
			   GError **error)
{
	LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(webdav);
	SoupMessage *msg;
	GBytes *buffer;
	SoupMessageHeaders *msg_headers;
	GBytes *reply;
	SoupStatus status;
	GError *local_err = NULL;
	xmlDocPtr doc = NULL;

	if (path != NULL) {
		if (path[0] == '/') {
			gchar *uri;

			uri = g_strconcat(priv->uri, path, NULL);
			msg = soup_message_new(method, uri);
			g_free(uri);
		} else {
			msg = soup_message_new(method, path);
		}
	} else {
		msg = soup_message_new(method, priv->uri);
	}

	buffer = g_bytes_new_static(req_xml, strlen(req_xml));
	soup_message_set_request_body_from_bytes(msg, "application/xml", buffer);
	g_signal_connect(msg, "restarted", G_CALLBACK(do_soup_msg_restarted), buffer);
	g_signal_connect(msg, "authenticate", G_CALLBACK(do_soup_auth), webdav);
	msg_headers = soup_message_get_request_headers(msg);
	if (depth != NULL) {
		soup_message_headers_append(msg_headers, "Depth", depth);
	}
	soup_message_headers_append(msg_headers, "Prefer", "return=minimal");		/* RFC 8144 */
	reply = soup_session_send_and_read(SOUP_SESSION(webdav), msg, NULL, &local_err);
	status = soup_message_get_status(msg);
	/* multistatus response code must be 207 (RFC 4918, sect. 13) */
	if (status != SOUP_STATUS_MULTI_STATUS) {
		if (local_err != NULL) {
			g_propagate_error(error, local_err);
		} else {
			/* Translators: #1 HTTP (RFC 2616) status code, #2 HTTP status message*/
			g_set_error(error, WEBDAV_ERROR_QUARK, (int) status, _("WebDAV response %d: %s"), (int) status,
				soup_message_get_reason_phrase(msg));
		}
	} else if (reply == NULL) {
		g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("WebDAV: empty response"));
	} else {
		const gchar *reply_buf;
		gsize reply_len;

		reply_buf = g_bytes_get_data(reply, &reply_len);
		if ((reply_buf == NULL) || (reply_len == 0UL)) {
			g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("WebDAV: empty response"));
		} else {
			doc = xmlReadMemory(reply_buf, reply_len, NULL, NULL, 0);
			if (doc == NULL) {
				g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("cannot parse the WebDAV server response as XML"));
			}
		}
	}
	g_object_unref(msg);
	g_bytes_unref(reply);
	g_bytes_unref(buffer);

	return doc;
}

#else				/* libsoup 2.4 */

/** @brief Authenticate the Soup session
 *
 * @param[in] session Soup session, unused
 * @param[in] msg triggering Soup message, unused
 * @param[in] auth Soup authentication object
 * @param[in] retrying FALSE for the first authentication attempt only
 * @param[in] user_data LibBalsaWebdav object
 *
 * Iff @em retrying is @c FALSE authenticate by calling soup_auth_authenticate().  Otherwise, the function is a no-op causing the
 * triggering request to fail.
 */
static void
do_soup_auth(SoupSession G_GNUC_UNUSED *session, SoupMessage G_GNUC_UNUSED *msg, SoupAuth *auth, gboolean retrying,
			 gpointer user_data)
{
	LibBalsaWebdav *webdav = LIBBALSA_WEBDAV(user_data);

	if (!retrying) {
		LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(webdav);

		soup_auth_authenticate(auth, priv->username, priv->password);
	}
}


/** @brief Run a Webdav request
 *
 * @param[in] session Webdav soup session
 * @param[in] host host or full request path
 * @param[in] path request path, @c NULL iff host contains the full path, the relative path if it starts with `/`, or the full path
 *            otherwise
 * @param[in] method request method
 * @param[in] depth value of the @c Depth header, @c NULL to omit the header
 * @param[in] req_xml request XML document
 * @param[in,out] error return location for an error, may be @c NULL
 * @return parsed XML reply document upon success, @c NULL on error
 */
static xmlDocPtr
webdav_request(LibBalsaWebdav *webdav, const gchar *path, const gchar *method, const gchar *depth, const gchar *req_xml,
			   GError **error)
{
	LibBalsaWebdavPrivate *priv = libbalsa_webdav_get_instance_private(webdav);
	SoupMessage *msg;
	guint result;
	xmlDocPtr doc = NULL;

	if (path != NULL) {
		if (path[0] == '/') {
			gchar *uri;

			uri = g_strconcat(priv->uri, path, NULL);
			msg = soup_message_new(method, uri);
			g_free(uri);
		} else {
			msg = soup_message_new(method, path);
		}
	} else {
		msg = soup_message_new(method, priv->uri);
	}

	soup_message_set_request(msg, "application/xml", SOUP_MEMORY_STATIC, req_xml, strlen(req_xml));
	if (depth != NULL) {
		soup_message_headers_append(msg->request_headers, "Depth", depth);
	}
	soup_message_headers_append(msg->request_headers, "Prefer", "return=minimal");		/* RFC 8144 */
	result = soup_session_send_message(SOUP_SESSION(webdav), msg);
	/* multistatus response code must be 207 (RFC 4918, sect. 13) */
	if ((result != SOUP_STATUS_MULTI_STATUS) || (msg->status_code != SOUP_STATUS_MULTI_STATUS)) {
		/* Translators: #1 HTTP (RFC 2616) status code, #2 HTTP status message*/
		g_set_error(error, WEBDAV_ERROR_QUARK, result, _("WebDAV response %d: %s"), msg->status_code, msg->reason_phrase);
	} else if ((msg->response_body == NULL) || (msg->response_body->data == NULL) || (msg->response_body->length <= 0)) {
		g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("WebDAV: empty response"));
	} else {
		doc = xmlReadMemory(msg->response_body->data, msg->response_body->length, NULL, NULL, 0);
		if (doc == NULL) {
			g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("cannot parse the WebDAV server response as XML"));
		}
	}
	g_object_unref(msg);

	return doc;
}

#endif				/* libsoup 2.4 vs. 3 */


/** @brief Extract WebDAV resources
 *
 * @param[in] nodes XML node set
 * @param[in] xpath_ctx current XPath context
 * @param[in] user_data user data, unused
 * @return a newly allocated distinct list of @ref libbalsa_webdav_resource_t items
 * @note Callback function for libbalsa_webdav_get_resource()
 */
static GList *
extract_resources(xmlNodeSetPtr nodes, xmlXPathContextPtr xpath_ctx, gpointer G_GNUC_UNUSED user_data)
{
	GList *result = NULL;
	int n;

	for (n = 0; n < nodes->nodeNr; n++) {
		xmlNodePtr this_node = nodes->nodeTab[n];

		if (this_node->type == XML_ELEMENT_NODE) {
			libbalsa_webdav_resource_t *new_item;

			new_item = g_new0(libbalsa_webdav_resource_t, 1U);
			new_item->href = libbalsa_webdav_xp_string_from_sub(this_node, (xmlChar *) WEBDAV_XP_SUB_HREF, xpath_ctx);
			new_item->name = libbalsa_webdav_xp_string_from_sub(this_node, (xmlChar *) WEBDAV_XP_SUB_DISPNAME, xpath_ctx);
			if ((new_item->href != NULL) &&
				(g_list_find_custom(result, new_item, (GCompareFunc) cmp_webdav_resource) == NULL)) {
				result = g_list_prepend(result, new_item);
			} else {
				libbalsa_webdav_resource_free(new_item);
			}
		}
	}
	return g_list_reverse(result);
}


/** @brief Compare two WebDAV resources
 *
 * @param[in] a first WebDAV resource
 * @param[in] b second WebDAV resource
 * @return a value similar to strcmp() indicating the result of the comparison of the two input values
 */
static gint
cmp_webdav_resource(const libbalsa_webdav_resource_t *a, const libbalsa_webdav_resource_t *b)
{
	int res;

	res = strcmp(a->href, b->href);
	if (res == 0) {
		if ((a->name != NULL) && (b->name != NULL)) {
			res = strcmp(a->name, b->name);
		} else if ((a->name != NULL) || (b->name != NULL)) {
			res = 1;
		} else {
			/* both names are NULL -> equal */
		}
	}
	return res;
}


/** @brief Make WebDAV resource URI's absolute
 *
 * @param[in] host https URI of the host
 * @param[in] path base path, either relative to @em host or a full https URI
 * @param[in] list list of @ref libbalsa_webdav_resource_t items
 * @return the list
 *
 * If the passed path starts with a slash '/', use the passed host, or the path as real host otherwise.  Prepend its scheme, user
 * info, host and port components to any relative @ref libbalsa_webdav_resource_t::href in the passed list.  Any list item already
 * starting with @c https:// is not touched.
 */
static GList *
ensure_absolute_resource_uri(const gchar *host, const gchar *path, GList *list)
{
	gchar *real_host;
	gchar *slash;
	GList *p;

	if (path[0] == '/') {
		real_host = g_strdup(host);
	} else {
		real_host = g_strdup(path);
	}
	slash = strchr(&real_host[8], (int) '/');
	if (slash != NULL) {
		slash[0] = '\0';
	}
	for (p = list; p != NULL; p = p->next) {
		libbalsa_webdav_resource_t *this_res = (libbalsa_webdav_resource_t *) p->data;

		if (strncmp(this_res->href, "https://", 8UL) != 0) {
			gchar *new_href;

			new_href = g_strconcat(real_host, this_res->href, NULL);
			g_free(this_res->href);
			this_res->href = new_href;
		}
	}
	g_free(real_host);
	return list;
}


/** @brief Initialise the XPath context for a XML document
 *
 * @param[in] doc XML document
 * @param[in] ns_list <c>NULL</<>-terminated list of prefix and namespace URI items which are added to the XPath context in addition
 *            to @c a=DAV:
 * @param[out] error return location for an error, may be @c NULL
 * @return XPath context on success, @c NULL on error
 */
static xmlXPathContextPtr
init_xpath_ctx(xmlDocPtr doc, gchar * const *ns_list, GError **error)
{
	xmlXPathContextPtr xpath_ctx;

	xpath_ctx = xmlXPathNewContext(doc);
	if (xpath_ctx != NULL) {
		int add_res;
		int n;

		add_res = xmlXPathRegisterNs(xpath_ctx, (const xmlChar *) "a", (const xmlChar *) "DAV:");
		for (n = 0; (add_res == 0) && (ns_list[n] != NULL); n += 2) {
			add_res = xmlXPathRegisterNs(xpath_ctx, (const xmlChar *) ns_list[n], (const xmlChar *) ns_list[n + 1]);
		}
		if (add_res != 0) {
			xmlXPathFreeContext(xpath_ctx);
			xpath_ctx = NULL;
			g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("cannot add namespaces to XPath context"));
		}
	} else {
		g_set_error(error, WEBDAV_ERROR_QUARK, -1, _("cannot create XPath context"));
	}

	return xpath_ctx;
}


/** @brief Extract the content of a XML text node
 *
 * @param[in] nodes XML node set, @em must point to a single XML text node
 * @param[in] xpath_ctx current XPath context, unused
 * @param[in] user_data user data, cast'ed to gchar ** and filled with the extracted content
 * @return a newly allocated distinct list of @ref libbalsa_webdav_resource_t items
 * @note Callback function for libbalsa_webdav_get_ctag() and libbalsa_webdav_get_sync_token()
 */
static GList *
get_text_string(xmlNodeSetPtr nodes, xmlXPathContextPtr G_GNUC_UNUSED xpath_ctx, gpointer user_data)
{
	if ((nodes->nodeNr != 1) || (nodes->nodeTab[0]->type != XML_TEXT_NODE)) {
		g_warning("expect one text node (got %d)", nodes->nodeNr);
	} else {
		gchar **result = (gchar **) user_data;

		*result = g_strdup((const gchar *) nodes->nodeTab[0]->content);
	}
	return NULL;
}


#endif		/* defined(HAVE_WEBDAV) */
