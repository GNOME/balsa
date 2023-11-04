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
 *
 * Relevant standards used in this module:
 * * RFC 4918: HTTP Extensions for Web Distributed Authoring and Versioning (WebDAV)
 * * RFC 5397: WebDAV Current Principal Extension
 * * RFC 6578: Collection Synchronization for Web Distributed Authoring and Versioning (WebDAV)
 * * RFC 6764: Locating Services for Calendaring Extensions to WebDAV (CalDAV) and vCard Extensions to WebDAV (CardDAV)
 * * RFC 8144: Use of the Prefer Header Field in Web Distributed Authoring and Versioning (WebDAV)
 * * https://github.com/apple/ccs-calendarserver/blob/master/doc/Extensions/caldav-ctag.txt: CTag in CalDAV
 */

#ifndef _LIBBALSA_WEBDAV_H
#define _LIBBALSA_WEBDAV_H


#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif						/* HAVE_CONFIG_H */


#if defined(HAVE_WEBDAV)


#include <glib-object.h>
#include <libxml/xpath.h>
#include <libsoup/soup.h>


G_BEGIN_DECLS


#define WEBDAV_ERROR_QUARK							(g_quark_from_static_string("libbalsa-webdav"))

/** @brief XPath fragments
 *
 * Some XPath fragments, used to extract information from various PROPFIND (RFC 4918, sect. 9.1) responses.
 * @note Every request always includes the namespace 'a="DAV:"' (RFC 4918).
 */
#define WEBDAV_MULTISTATUS_XP						"/a:multistatus/a:response/a:propstat"
#define WEBDAV_OK_XP								"a:status[contains(text(),' 200 ')]"
#define WEBDAV_XP_SUB_CTAG							"a:propstat/a:prop/c:getctag/text()"
#define WEBDAV_XP_SUB_SYNCTOK						"a:propstat/a:prop/a:sync-token/text()"
#define WEBDAV_XP_SUB_REPSET						"a:propstat/a:prop/a:supported-report-set/a:supported-report/a:report/*"
#define WEBDAV_XP_SUB_HREF							"a:href/text()"
#define WEBDAV_XP_SUB_DISPNAME						"a:propstat/a:prop/a:displayname/text()"

/** Namespace for CalendarServer extensions */
#define WEBDAV_NS_CALSERVER							"http://calendarserver.org/ns/"

#define LIBBALSA_WEBDAV_TYPE						(libbalsa_webdav_get_type())
G_DECLARE_DERIVABLE_TYPE(LibBalsaWebdav, libbalsa_webdav, LIBBALSA, WEBDAV, SoupSession)


struct _LibBalsaWebdavClass {
	SoupSessionClass parent;
};


/** @brief Callback function prototype for evaluating the response of a PROPFIND request (RFC 4918, sect. 9.1)
 *
 * @param[in] nodes set of matching XML nodes
 * @param[in] xpath_ctx current XPath context
 * @param[in] user_data user data pointer
 * @return an application-dependent linked list of items
 */
typedef GList *(*libbalsa_webdav_eval_func)(xmlNodeSetPtr nodes, xmlXPathContextPtr xpath_ctx, gpointer user_data);


/** @brief WebDAV server synchronisation support */
typedef enum {
	LIBBALSA_WEBDAV_SYNC_NONE,			/**< Server does not support synchronisation. */
	LIBBALSA_WEBDAV_SYNC_TOKEN,			/**< Server supports synchronisation token (RFC 6578, sect. 4). */
	LIBBALSA_WEBDAV_SYNC_CTAG			/**< Server supports "getctag" (CTag in CalDAV). */
} libbalsa_webdav_sync_t;


/** @brief WebDAV resource item */
typedef struct {
	gchar *name;						/**< WebDAV resource display name (RFC 4918, sect. 15.2), @em MAY be <c>NULL</c>. */
	gchar *href;						/**< WebDAV resource URI, never <c>NULL</c>. */
	guint flags;						/**< Resource capability flags, type dependent. */
} libbalsa_webdav_resource_t;


/** @brief WebDAV PROPFIND (RFC 4918, sect. 9.1) request data */
typedef struct {
	gchar *item;						/**< The XML items which shall be requested. */
	gchar *depth;						/**< Request DEPTH header value (RFC 4918, sect. 10.2). */
	gchar *xpath;						/**< XPath to extract from the response. */
	gchar *ns_list[5];					/**< <c>NULL</c>-terminated list of (identifier, namespace) pairs containing additional
										 * namespace definitions in the request ("a", "DAV:" is added automatically). */
	libbalsa_webdav_eval_func eval_fn;	/**< Callback function for evaluating the matching XPath items from the response. */
} libbalsa_webdav_propfind_t;


/** @brief WebDAV REPORT request data */
typedef struct {
	gchar *name;						/**< The name of the report including the namespace (note: not an XML item!). */
	gchar *body;						/**< XML body of the report. */
	gchar *depth;						/**< Request DEPTH header value (RFC 4918, sect. 10.2). */
	gchar *xpath;						/**< XPath to extract from the response. */
	gchar *ns_list[5];					/**< <c>NULL</c>-terminated list of (identifier, namespace) pairs containing additional
										 * namespace definitions in the request ("a", "DAV:" is added automatically). */
	libbalsa_webdav_eval_func eval_fn;	/**< Callback function for evaluating the matching XPath items from the response. */
} libbalsa_webdav_report_t;


/** @brief Create a new LibBalsaWebdav object
 *
 * @return LibBalsaWebdav object, not yet linked to a remote URI
 */
LibBalsaWebdav *libbalsa_webdav_new(void);

/** @brief Configure a LibBalsaWebdav object
 *
 * @param[in] webdav LibBalsaWebdav object
 * @param[in] uri full URI of the WebDAV resource
 * @param[in] username user name used for authentication
 * @param[in] password password used for authentication
 */
void libbalsa_webdav_setup(LibBalsaWebdav *webdav,
						   const gchar    *uri,
						   const gchar    *username,
						   const gchar    *password);

/** @brief Get WebDAV resources
 *
 * @param[in] webdav LibBalsaWebdav object
 * @param[in] path request path (see below)
 * @param[in] homeset homeset which shall be requested (e.g. "addressbook-home-set")
 * @param[in] resource relevant resource name returned by the server (e.g. "addressbook")
 * @param[in] namespace namespace for @em item and @em resource
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return a list of @ref libbalsa_webdav_resource_t items on success, @c NULL on error or if not matching resource has been found
 *
 * The request path is either @c NULL (use the URI configured by calling libbalsa_webdav_setup()), a relative path, or a full URI.
 *
 * The function performs a request for the @c current-user-principal (RFC 5397), the home set specified by @em item and for all
 * @em resource resources in all home sets.  The returned list contains the absolute URI and the display name (RFC 4918, sect. 15.2)
 * of all matching resources.
 *
 * @note The caller @em SHALL free the returned list by calling libbalsa_webdav_resource_free() on every element.
 */
GList *libbalsa_webdav_get_resource(LibBalsaWebdav *webdav,
									const gchar    *path,
									const gchar    *homeset,
									const gchar    *resource,
									const gchar    *namespace,
									GError        **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Run a WebDAV PROPFIND request
 *
 * @param[in] webdav LibBalsaWebdav object
 * @param[in] path request path (see below)
 * @param[in] task PROPFIND request data
 * @param[in] user_data user data, passed to the evaluation callback @ref libbalsa_webdav_propfind_t::eval_fn
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return a list of items allocated by the evaluation callback on success, @c NULL on error or if not data has been found
 *
 * The request path is either @c NULL (use the URI configured by calling libbalsa_webdav_setup()), a relative path, or a full URI.
 */
GList *libbalsa_webdav_propfind(LibBalsaWebdav                   *webdav,
								const gchar                      *path,
								const libbalsa_webdav_propfind_t *task,
								gpointer                          user_data,
								GError                          **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Run a WebDAV REPORT request
 *
 * @param[in] webdav LibBalsaWebdav object
 * @param[in] path request path (see below)
 * @param[in] task REPORT request data
 * @param[in] user_data user data, passed to the evaluation callback @ref libbalsa_webdav_propfind_t::eval_fn
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return a list of items allocated by the evaluation callback on success, @c NULL on error or if not data has been found
 *
 * The request path is either @c NULL (use the URI configured by calling libbalsa_webdav_setup()), a relative path, or a full URI.
 */
GList *libbalsa_webdav_report(LibBalsaWebdav                 *webdav,
							  const gchar                    *path,
							  const libbalsa_webdav_report_t *task,
							  gpointer                        user_data,
							  GError                        **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Run a WebDAV PUT request
 *
 * @param[in] webdav LibBalsaWebdav object
 * @param[in] data the data which shall be written, excluding the terminating NUL character
 * @param[in] mime_type the MIME type of the data
 * @param[in] name the name on the remote server, appended to the URI configured by calling libbalsa_webdav_setup()
 * @param[in] etag an ETag value to modify an existing resource, or @c NULL to create a new one
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return the ETag value returned by the remote server on success, or @c NULL on error
 *
 * Create or modify a remote resource, returning the ETag value of it.  Note that some broken servers do not return an ETag on
 * success.  In this case, the function returns an empty string.
 */
gchar *libbalsa_webdav_put(LibBalsaWebdav *webdav,
						   const gchar    *data,
						   const gchar    *mime_type,
						   const gchar    *name,
						   const gchar    *etag,
						   GError        **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Read the current CTag
 *
 * @param[in] webdav LibBalsaWebdav object
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return a newly allocated string containing the current CTag, @c NULL on error or if the remote server does not support it
 */
gchar *libbalsa_webdav_get_ctag(LibBalsaWebdav *webdav,
								GError        **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Read the current RFC 6578 synchronisation token
 *
 * @param[in] webdav LibBalsaWebdav object
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return a newly allocated string containing the current token, @c NULL on error or if the remote server does not support it
 */
gchar *libbalsa_webdav_get_sync_token(LibBalsaWebdav *webdav,
									  GError        **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Get the URI of the WebDAV resource
 *
 * @param[in] webdav LibBalsaWebdav object
 * @return the full URI of the WebDAV resource
 */
const gchar *libbalsa_webdav_get_uri(LibBalsaWebdav *webdav);

/** @brief Force a disconnect of the underlying HTTPS session
 *
 * @param[in] webdav LibBalsaWebdav object
 */
void libbalsa_webdav_disconnect(LibBalsaWebdav *webdav);

/** @brief Free a WebDAV resource item
 *
 * @param[in] resource item which shall be freed, may be @c NULL
 */
void libbalsa_webdav_resource_free(libbalsa_webdav_resource_t *resource);

/** @brief Run a DNS query to identify a WebDAV server
 *
 * @param[in] domain the domain to check
 * @param[in] service the service the domain shall offer
 * @param[out] path filled with the initial "context path" on success
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return a newly allocated https URI supporting the requested service on success, or @c NULL on error or if no DNS data is
 *         available
 *
 * Run a DNS query for a @c SRV record _service._tcp.domain (RFC 6764) and return it as https URI.  If a @c TXT query returns an
 * initial "context path" it is also returned.  Note that RFC 6764 sect. 4 states that if no such @c TXT record is returned, the
 * ".well-known" URI approach shall be used.  However, at least some broken services (e.g. icloud.com) fail in this case, so the
 * function does not return a path if no appropriate @c TXT record exists.
 */
gchar *libbalsa_webdav_lookup_srv(const gchar *domain,
								  const gchar *service,
								  gchar      **path,
								  GError     **error)
	G_GNUC_WARN_UNUSED_RESULT;


/* Helper functions for evaluating XML node sets */

/** @brief Extract XML text nodes
 *
 * @param[in] nodes XML node set
 * @param[in] xpath_ctx current XPath context, unused
 * @param[in] user_data user data, unused
 * @return a distinct list of newly allocated strings, containing the content of all XML text nodes in @em nodes
 */
GList *libbalsa_webdav_xp_get_strlist(xmlNodeSetPtr      nodes,
									  xmlXPathContextPtr xpath_ctx,
									  gpointer           user_data)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Extract a string using XPath from a XML node
 *
 * @param[in] node XML node
 * @param[in] sub_expr XPath expression relative to the XML node
 * @param[in] xpath_ctx current XPath context
 * @return the extracted string on success, @c NULL if the passed XPath did not match
 *
 * Extract a string from a XML text node, selected using a XPath expression applied to a XML node.
 */
gchar *libbalsa_webdav_xp_string_from_sub(xmlNodePtr         node,
										  const xmlChar     *sub_expr,
										  xmlXPathContextPtr xpath_ctx)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Peek the text content of an XML attribute node
 *
 * @param[in] node XML attribute node
 * @return the text content of the attribute node on success, @c NULL on error
 */
static inline const gchar *
libbalsa_webdav_peek_xml_attr(xmlNodePtr node)
{
	if ((node->type == XML_ATTRIBUTE_NODE) && (node->children != NULL) && (node->children->type == XML_TEXT_NODE)) {
		return (const gchar *) node->children->content;
	} else {
		return NULL;
	}
}


G_END_DECLS


#endif		/* defined(HAVE_WEBDAV) */


#endif  /* _LIBBALSA_WEBDAV_H */
