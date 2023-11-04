/* LibBalsaCarddav - Carddav class for Balsa
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


#include <glib/gi18n.h>
#include "information.h"
#include "rfc6350.h"
#include "libbalsa-carddav.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "webdav"


struct _LibBalsaCarddav {
	LibBalsaWebdav parent;

	/* configuration items */
	guint refresh_seconds;				/**< The time in seconds after which the server shall be re-checked for updates. */
	gboolean force_mget;				/**< Use the @em addressbook-multiget report even if the server claims to support
										 * to support @em addressbook-query - needed to work around broken servers. */

	/* WebDAV worker thread data */
	gint worker_active;					/**< Indicates if the worker thread is active. */
	GMutex mutex;						/**< Data access mutex. */
	gchar *last_error;					/**< Last error message. */

	/* status items */
	gboolean init_done;					/**< Indicates if the initial server request has been run. */
	libbalsa_webdav_sync_t sync_mode;	/**< Synchronisation mode supported by the server. */
	gchar *displayname;					/**< Display name of the remote address book. */
	gchar *sync_value;					/**< Synchronisation value (token or ctag, according to the mode, @c NULL for none). */
	gint64 valid_until;					/**< Monotonic time when the next update check shall be run. */
	gboolean can_query;					/**< Server supports "addressbook-query" report (RFC 6352, sect. 8.6). */
	gboolean can_mget;					/**< Server supports "addressbook-multiget" report (RFC 6352, sect. 8.7). */
	gboolean can_vcard4;				/**< Server supports VCARD version 4 (RFC 6352, sect. 6.2.2). */
	GList *vcards;						/**< List of @ref LibBalsaAddress items loaded from the server. */
};


G_DEFINE_TYPE(LibBalsaCarddav, libbalsa_carddav, LIBBALSA_WEBDAV_TYPE)


/** @brief CardDAV namespace */
#define NS_CARDDAV			"urn:ietf:params:xml:ns:carddav"

/* XPath fragments */
#define XPATH_IS_AB			"a:prop/a:resourcetype/b:addressbook"
#define XPATH_VCARD_VER		"a:propstat/a:prop/b:supported-address-data/b:address-data-type[@content-type=\"text/vcard\"]/@version"
#define XPATH_SUB_VCARD		"a:propstat/a:prop/b:address-data/text()"
#define XPATH_NO_COLL		"a:prop/a:resourcetype[not(a:collection)]"
#define RESOURCETYPE		"<a:resourcetype/>"

/** @brief VCard properties used to create a LibBalsaAddress
 *
 * See RFC 6352, sect. 10.4.2.  It appears some (most?) CardDAV servers ignore this specification and always return the complete
 * VCard, though.
 */
#define VCARD_ELEMENTS					\
	"<b:address-data>"					\
		"<b:prop name=\"FN\"/>"			\
		"<b:prop name=\"N\"/>"			\
		"<b:prop name=\"NICKNAME\"/>"	\
		"<b:prop name=\"ORG\"/>"		\
		"<b:prop name=\"EMAIL\"/>"		\
	"</b:address-data>"


static void libbalsa_carddav_finalise(GObject *object);

static gpointer libbalsa_carddav_init_thread(gpointer data);
static gpointer libbalsa_carddav_update_thread(gpointer data);
static GList *carddav_ab_props(xmlNodeSetPtr nodes,
							   xmlXPathContextPtr xpath_ctx,
							   gpointer user_data)
	G_GNUC_WARN_UNUSED_RESULT;
static void libbalsa_carddav_thread_error(LibBalsaCarddav *carddav,
										  const gchar     *message);
static gboolean libbalsa_carddav_notify_error(gpointer user_data);
static gboolean libbalsa_carddav_update(LibBalsaCarddav *carddav);
static GList *libbalsa_carddav_run_query(LibBalsaCarddav *carddav,
										 GError         **error)
	G_GNUC_WARN_UNUSED_RESULT;
static GList *libbalsa_carddav_run_multiget(LibBalsaCarddav *carddav,
											GError         **error)
	G_GNUC_WARN_UNUSED_RESULT;
static GList *carddav_extract_vcard_data(xmlNodeSetPtr      nodes,
										 xmlXPathContextPtr xpath_ctx,
										 gpointer           user_data)
	G_GNUC_WARN_UNUSED_RESULT;
static GList *carddav_extract_vcard_href(xmlNodeSetPtr      nodes,
										 xmlXPathContextPtr xpath_ctx,
										 gpointer           user_data)
	G_GNUC_WARN_UNUSED_RESULT;


/* documentation: see header file */
LibBalsaCarddav *
libbalsa_carddav_new(const gchar *uri, const gchar *username, const gchar *password, guint refresh_secs, gboolean force_multiget)
{
	LibBalsaCarddav *carddav;
	GThread *worker;

	g_return_val_if_fail((uri != NULL) && (username != NULL), NULL);

	g_debug("%s: create for uri %s", __func__, uri);
	carddav = LIBBALSA_CARDDAV(g_object_new(LIBBALSA_CARDDAV_TYPE, NULL));
	carddav->refresh_seconds = refresh_secs;
	carddav->force_mget = force_multiget;
	libbalsa_webdav_setup(LIBBALSA_WEBDAV(carddav), uri, username, password);
	g_atomic_int_set(&carddav->worker_active, 1);
	worker = g_thread_new("carddav_init", libbalsa_carddav_init_thread, carddav);
	g_thread_unref(worker);			/* detach thread */
	return carddav;
}


/* documentation: see header file */
const GList *
libbalsa_carddav_lock_addrlist(LibBalsaCarddav *carddav)
{
	GList *result = NULL;

	g_return_val_if_fail(LIBBALSA_IS_CARDDAV(carddav), NULL);

	g_mutex_lock(&carddav->mutex);
	if (!carddav->init_done) {
		if (g_atomic_int_compare_and_exchange(&carddav->worker_active, 0, 1)) {
			GThread *worker;

			worker = g_thread_new("carddav_init", libbalsa_carddav_init_thread, carddav);
			g_thread_unref(worker);			/* detach thread */
		}
	} else {
		gint64 now;

		now = g_get_monotonic_time();
		if (now > carddav->valid_until) {
			if (g_atomic_int_compare_and_exchange(&carddav->worker_active, 0, 1)) {
				GThread *worker;

				worker = g_thread_new("carddav_update", libbalsa_carddav_update_thread, carddav);
				g_thread_unref(worker);			/* detach thread */
			}
		}
		result = carddav->vcards;
	}
	return result;
}


/* documentation: see header file */
void
libbalsa_carddav_unlock_addrlist(LibBalsaCarddav *carddav)
{
	g_return_if_fail(LIBBALSA_IS_CARDDAV(carddav));

	g_mutex_unlock(&carddav->mutex);
}


/* documentation: see header file */
gchar *
libbalsa_carddav_get_last_error(LibBalsaCarddav *carddav)
{
	gchar *retval;

	g_return_val_if_fail(LIBBALSA_IS_CARDDAV(carddav), NULL);

	g_mutex_lock(&carddav->mutex);
	retval = carddav->last_error;
	carddav->last_error = NULL;
	g_mutex_unlock(&carddav->mutex);
	return retval;
}


/* documentation: see header file */
gboolean
libbalsa_carddav_add_address(LibBalsaCarddav *carddav, LibBalsaAddress *address, GError **error)
{
	gchar *vcard;
	gchar *sha1;
	gchar *name;
	gchar *new_etag;
	gboolean result = FALSE;

	g_return_val_if_fail(LIBBALSA_IS_CARDDAV(carddav) && LIBBALSA_IS_ADDRESS(address), FALSE);

	vcard = rfc6350_from_address(address, carddav->can_vcard4, TRUE);
	sha1 = g_compute_checksum_for_string(G_CHECKSUM_SHA1, vcard, -1);
	name = g_strconcat("balsa-", sha1, ".vcf", NULL);
	g_free(sha1);
	/* make sure no other background thread is running */
	while (!g_atomic_int_compare_and_exchange(&carddav->worker_active, 0, 1)) {
		g_usleep(1000);
	}
	new_etag = libbalsa_webdav_put(LIBBALSA_WEBDAV(carddav), vcard, "text/vcard", name, NULL, error);
	g_free(name);
	g_free(vcard);
	if (new_etag != NULL) {
		GThread *worker;

		/* force reloading the address book */
		worker = g_thread_new("carddav_update", libbalsa_carddav_update_thread, carddav);
		g_thread_unref(worker);			/* detach thread */

		g_debug("%s: added vcard, etag '%s'", __func__, new_etag);
		g_free(new_etag);
		result = TRUE;
	} else {
		g_atomic_int_set(&carddav->worker_active, 0);
		libbalsa_webdav_disconnect(LIBBALSA_WEBDAV(carddav));
	}
	return result;
}


/* documentation: see header file */
GList *
libbalsa_carddav_list(const gchar *domain_or_uri, const gchar *username, const gchar *password, GError **error)
{
	gchar *list_uri;
	gchar *path;
	GList *result = NULL;

	g_return_val_if_fail((domain_or_uri != NULL) && (username != NULL), NULL);

	/* check if the passed argument is not a https uri, i.e. we should run a DNS query */
	if (strncmp(domain_or_uri, "https://", 8UL) != 0) {
		path = NULL;
		list_uri = libbalsa_webdav_lookup_srv(domain_or_uri, "carddavs", &path, error);
		g_debug("%s: DNS result for '%s': '%s', path '%s'", __func__, domain_or_uri, list_uri, path);
	} else {
		const gchar *slash;

		slash = strchr(&domain_or_uri[8], (int) '/');
		if ((slash == NULL) || (slash[1] == '\0')) {
			list_uri = g_strdup(domain_or_uri);
			path = g_strdup("/.well-known/carddav");
		} else {
			list_uri = g_strndup(domain_or_uri, slash - domain_or_uri);
			path = g_strdup(slash);
		}
	}

	if (list_uri != NULL) {
		LibBalsaWebdav *webdav;

		g_debug("%s: uri '%s'", __func__, list_uri);
		webdav = libbalsa_webdav_new();
		libbalsa_webdav_setup(webdav, list_uri, username, password);
		result = libbalsa_webdav_get_resource(webdav, path, "addressbook-home-set", "addressbook", NS_CARDDAV, error);
		g_object_unref(webdav);
	}
	g_free(list_uri);
	g_free(path);
	return result;
}


/* == local functions =========================================================================================================== */

static void
libbalsa_carddav_class_init(LibBalsaCarddavClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	gobject_class->finalize = libbalsa_carddav_finalise;
}


static void
libbalsa_carddav_init(LibBalsaCarddav *self)
{
	g_mutex_init(&self->mutex);
}


static void
libbalsa_carddav_finalise(GObject *object)
{
	LibBalsaCarddav *carddav = LIBBALSA_CARDDAV(object);
	const GObjectClass *parent_class = G_OBJECT_CLASS(libbalsa_carddav_parent_class);

	while (g_atomic_int_get(&carddav->worker_active) != 0) {
		g_usleep(1000);
	}
	g_mutex_clear(&carddav->mutex);
	g_free(carddav->displayname);
	g_free(carddav->sync_value);
	g_free(carddav->last_error);
	g_list_free_full(carddav->vcards, g_object_unref);
	(*parent_class->finalize)(object);
}


/** @brief Initialise the CardDAV connection
 *
 * @param[in] data thread data, cast'ed to LibBalsaCarddav *
 * @return always @c NULL
 *
 * This function shall be run as a detached thread.  It reads the base capabilities of the remote server (see carddav_ab_props()),
 * and on success calls libbalsa_carddav_update() to read the list of addresses.  Finally, the connection is terminated by calling
 * libbalsa_webdav_disconnect().
 *
 * If any error occurs, the
 */
static gpointer
libbalsa_carddav_init_thread(gpointer data)
{
	static const libbalsa_webdav_propfind_t get_props = {
		.item = "<a:resourcetype/><a:displayname/><a:supported-report-set/><a:sync-token/><b:supported-address-data/><c:getctag/>",
		.depth = "0",
		.xpath = WEBDAV_MULTISTATUS_XP "[" WEBDAV_OK_XP " and " XPATH_IS_AB "]/..",
		.ns_list = { "b", NS_CARDDAV, "c", WEBDAV_NS_CALSERVER, NULL },
		.eval_fn = carddav_ab_props
	};
	LibBalsaCarddav *carddav = LIBBALSA_CARDDAV(data);
	GError *error = NULL;

	g_assert(LIBBALSA_IS_CARDDAV(data));
	g_debug("%s: started for uri %s", __func__, libbalsa_webdav_get_uri(LIBBALSA_WEBDAV(carddav)));
	if (GPOINTER_TO_INT(libbalsa_webdav_propfind(LIBBALSA_WEBDAV(carddav), NULL, &get_props, carddav, &error)) == 0) {
		/* error may be NULL if carddav_ab_props fails */
		if (error != NULL) {
			libbalsa_carddav_thread_error(carddav, error->message);
			g_error_free(error);
		}
	} else {
		libbalsa_carddav_update(carddav);
	}
	libbalsa_webdav_disconnect(LIBBALSA_WEBDAV(carddav));
	g_debug("%s: done for uri %s", __func__, libbalsa_webdav_get_uri(LIBBALSA_WEBDAV(carddav)));
	g_atomic_int_set(&carddav->worker_active, 0);
	return NULL;
}


/** @brief Update the list of CardDAV addresses
 *
 * @param[in] data thread data, cast'ed to LibBalsaCarddav *
 * @return always @c NULL
 *
 * This function shall be run as a detached thread.  Depending upon the synchronisation mode LibBalsaCarddav::sync_mode supported by
 * the remote server, it first reads the sync token or CTag value updates the internal cached list of CardDAV addresses iff it has
 * changed by calling libbalsa_carddav_update().  If the server does not support sync tokens or CTags, the list is always updated.
 *
 * Finally, the connection is terminated by calling libbalsa_webdav_disconnect().
 */
static gpointer
libbalsa_carddav_update_thread(gpointer data)
{
	LibBalsaCarddav *carddav = LIBBALSA_CARDDAV(data);
	libbalsa_webdav_sync_t sync_mode;
	gchar *sync_value = NULL;
	gboolean need_reload = FALSE;

	g_assert(LIBBALSA_IS_CARDDAV(data));
	g_debug("%s: started", __func__);

	g_mutex_lock(&carddav->mutex);
	sync_mode = carddav->sync_mode;
	g_mutex_unlock(&carddav->mutex);

	if (sync_mode == LIBBALSA_WEBDAV_SYNC_NONE) {
		need_reload = TRUE;
	} else {
		GError *error = NULL;

		if (sync_mode == LIBBALSA_WEBDAV_SYNC_TOKEN) {
			sync_value = libbalsa_webdav_get_sync_token(LIBBALSA_WEBDAV(carddav), &error);
		} else {
			sync_value = libbalsa_webdav_get_ctag(LIBBALSA_WEBDAV(carddav), &error);
		}

		if (sync_value == NULL) {
			libbalsa_carddav_thread_error(carddav, error->message);
			g_error_free(error);
		} else {
			g_mutex_lock(&carddav->mutex);
			need_reload = strcmp(carddav->sync_value, sync_value) != 0;
			g_debug("%s: mode=%d '%s'->'%s': reload=%d", __func__, sync_mode, carddav->sync_value, sync_value, need_reload);
			g_mutex_unlock(&carddav->mutex);
		}
	}

	if (need_reload) {
		if (libbalsa_carddav_update(carddav)) {
			g_free(carddav->sync_value);
			carddav->sync_value = sync_value;
			sync_value = NULL;
		}
	} else {
		g_mutex_lock(&carddav->mutex);
		carddav->valid_until = g_get_monotonic_time() + carddav->refresh_seconds * 1000000;
		g_mutex_unlock(&carddav->mutex);
	}

	g_free(sync_value);
	libbalsa_webdav_disconnect(LIBBALSA_WEBDAV(carddav));
	g_debug("%s: done", __func__);
	g_atomic_int_set(&carddav->worker_active, 0);
	return NULL;
}


/** @brief Extract the properties of a CardDAV address book
 *
 * @param[in] nodes XML node set
 * @param[in] xpath_ctx current XPath context
 * @param[in] user_data user data, cast'ed to LibBalsaCarddav *
 * @return @c TRUE on success, or @c FALSE on error, both cast'ed to a pointer
 */
static GList *
carddav_ab_props(xmlNodeSetPtr nodes, xmlXPathContextPtr xpath_ctx, gpointer user_data)
{
	LibBalsaCarddav *carddav = LIBBALSA_CARDDAV(user_data);
	GList *result;

	if ((nodes->nodeNr != 1) || (nodes->nodeTab[0]->type != XML_ELEMENT_NODE)) {
		libbalsa_carddav_thread_error(carddav, _("The server returned an invalid reply."));
		g_warning("expect one element node (got %d)", nodes->nodeNr);
		result = GINT_TO_POINTER(FALSE);
	} else {
		xmlNodePtr this_node = nodes->nodeTab[0];
		xmlXPathObjectPtr xpath_sub;
		int n;

		/* lock mutex */
		g_mutex_lock(&carddav->mutex);

		/* clean data in case the object is re-used */
		g_free(carddav->sync_value);
		carddav->can_mget = FALSE;
		carddav->can_query = FALSE;
		carddav->can_vcard4 = FALSE;

		carddav->displayname = libbalsa_webdav_xp_string_from_sub(this_node, (xmlChar *) WEBDAV_XP_SUB_DISPNAME, xpath_ctx);
		/* try to get the sync token and the ctag to determine the best synchronisation mode */
		carddav->sync_value = libbalsa_webdav_xp_string_from_sub(this_node, (xmlChar *) WEBDAV_XP_SUB_SYNCTOK, xpath_ctx);
		if (carddav->sync_value != NULL) {
			carddav->sync_mode = LIBBALSA_WEBDAV_SYNC_TOKEN;
		} else {
			carddav->sync_value = libbalsa_webdav_xp_string_from_sub(this_node, (xmlChar *) WEBDAV_XP_SUB_CTAG, xpath_ctx);
			if (carddav->sync_value != NULL) {
				carddav->sync_mode = LIBBALSA_WEBDAV_SYNC_CTAG;
			} else {
				carddav->sync_mode = LIBBALSA_WEBDAV_SYNC_NONE;
			}
		}
		g_debug("%s: '%s': sync mode %d, value '%s'", __func__, carddav->displayname, carddav->sync_mode, carddav->sync_value);

		/* reports */
		xpath_sub = xmlXPathNodeEval(this_node, (xmlChar *) WEBDAV_XP_SUB_REPSET, xpath_ctx);
		if (xpath_sub != NULL) {
			for (n = 0; n < xmlXPathNodeSetGetLength(xpath_sub->nodesetval); n++) {
				xmlNodePtr subnode = xpath_sub->nodesetval->nodeTab[n];

				if (subnode->type == XML_ELEMENT_NODE) {
					if (strcmp((const gchar *) subnode->name, "addressbook-multiget") == 0) {
						carddav->can_mget = TRUE;
					} else if (strcmp((const gchar *) subnode->name, "addressbook-query") == 0) {
						carddav->can_query = TRUE;
					} else {
						/* other report sets are ignored */
					}
				}
			}
			xmlXPathFreeObject(xpath_sub);
		}

		/* VCARD ver. 4 support */
		xpath_sub = xmlXPathNodeEval(this_node, (xmlChar *) XPATH_VCARD_VER, xpath_ctx);
		if (xpath_sub != NULL) {
			for (n = 0; n < xmlXPathNodeSetGetLength(xpath_sub->nodesetval); n++) {
				if (g_strcmp0(libbalsa_webdav_peek_xml_attr(xpath_sub->nodesetval->nodeTab[n]), "4.0") == 0) {
					carddav->can_vcard4 = TRUE;
				}
			}
			xmlXPathFreeObject(xpath_sub);
		}
		g_debug("%s: supported features: multiget=%d query=%d VCard 4.0=%d", __func__, carddav->can_mget, carddav->can_query,
			carddav->can_vcard4);

		/* common properties */
		carddav->init_done = TRUE;
		carddav->valid_until = g_get_monotonic_time();		/* i.e. update the address list asap */

		/* unlock mutex */
		g_mutex_unlock(&carddav->mutex);

		if (!carddav->can_mget && !carddav->can_query) {
			libbalsa_carddav_thread_error(carddav, _("The server does not support any usable address book report."));
			result = GINT_TO_POINTER(FALSE);
		} else {
			result = GINT_TO_POINTER(TRUE);
		}
	}
	return result;
}


/** @brief Schedule an error notification from a thread
 *
 * @param[in] carddav CardDAV object
 * @param[in] message error message
 *
 * Store the passed error in LibBalsaCarddav::last error and schedule libbalsa_carddav_notify_error() as idle callback to display
 * the passed error message.
 */
static void
libbalsa_carddav_thread_error(LibBalsaCarddav *carddav, const gchar *message)
{
	if (message != NULL) {
		gchar *notify_msg;

		g_mutex_lock(&carddav->mutex);
		g_free(carddav->last_error);
		carddav->last_error = g_strdup(message);
		/* Translators: #1 address book name, #2 address book URL, #3 error message */
		notify_msg = g_strdup_printf(_("CardDAV error for address book “%s” (URL %s):\n%s"),
			(carddav->displayname != NULL) ? carddav->displayname : _("unknown"),
			libbalsa_webdav_get_uri(LIBBALSA_WEBDAV(carddav)), message);
		g_mutex_unlock(&carddav->mutex);
		g_idle_add(libbalsa_carddav_notify_error, notify_msg);
	}
}


/** @brief Notify about an error from a thread
 *
 * @param[in] user_data error message, cast'ed to gchar * and free'd by this function
 * @return always @c FALSE
 *
 * This idle callback, scheduled by libbalsa_carddav_thread_error(), actually shows the notification and frees the message.
 */
static gboolean
libbalsa_carddav_notify_error(gpointer user_data)
{
	gchar *message = (gchar *) user_data;

	/* Translators: #1 error message */
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("CardDAV error: %s"), message);
	g_free(message);
	return FALSE;
}


/** @brief Update the cached list of CardDAV addresses
 *
 * @param[in] carddav CardDAV object
 * @return @c TRUE on success, @c FALSE on error
 *
 * Depending upon the address book reports supported by the remote server and the configuration parameter
 * LibBalsaCarddav::force_mget run either an address book query (preferred) or multiget.  On success, the cached list of CardDAV
 * addresses is replaced, and its validity time stamp is updated.
 */
static gboolean
libbalsa_carddav_update(LibBalsaCarddav *carddav)
{
	gboolean query;
	GList *addresses;
	GError *error = NULL;
	gboolean result;

	g_mutex_lock(&carddav->mutex);
	query = carddav->can_query && !carddav->force_mget;
	g_mutex_unlock(&carddav->mutex);

	if (query) {
		addresses = libbalsa_carddav_run_query(carddav, &error);
	} else {
		addresses = libbalsa_carddav_run_multiget(carddav, &error);
	}
	g_debug("%s: loaded %u VCards", __func__, g_list_length(addresses));

	if (error != NULL) {
		libbalsa_carddav_thread_error(carddav, error->message);
		g_error_free(error);
		result = FALSE;
	} else {
		GList *old_addrs;

		g_mutex_lock(&carddav->mutex);
		old_addrs = carddav->vcards;
		carddav->vcards = addresses;
		carddav->valid_until = g_get_monotonic_time() + carddav->refresh_seconds * 1000000;
		g_mutex_unlock(&carddav->mutex);
		g_list_free_full(old_addrs, g_object_unref);
		result = TRUE;
	}

	return result;
}


/** @brief Run an Address Book Query report
 *
 * @param[in] carddav CardDAV object
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return the new list of CardDAV LibBalsaAddress addresses
 * @sa carddav_extract_vcard_data(); RFC 6352, sect. 8.6
 */
static GList *
libbalsa_carddav_run_query(LibBalsaCarddav *carddav, GError **error)
{
	static const libbalsa_webdav_report_t vcard_report = {
		.name = "b:addressbook-query",
		.body = "<a:prop>" VCARD_ELEMENTS "</a:prop><b:filter><b:prop-filter name=\"EMAIL\"/></b:filter>",
		.depth = "1",
		.xpath = WEBDAV_MULTISTATUS_XP "[" WEBDAV_OK_XP "]/..",
		.ns_list = { "b", NS_CARDDAV, NULL },
		.eval_fn = carddav_extract_vcard_data
	};

	return libbalsa_webdav_report(LIBBALSA_WEBDAV(carddav), NULL, &vcard_report, NULL, error);
}


/** @brief Run an Address Book Multiget report
 *
 * @param[in] carddav CardDAV object
 * @param[in,out] error filled with error information on error, may be @c NULL
 * @return the new list of CardDAV LibBalsaAddress addresses
 *
 * Run a @c PROPFIND request to read the href of all address items, and then the multiget report for them (RFC 6352, sect. 8.7)
 *
 * @sa carddav_extract_vcard_href(), carddav_extract_vcard_data()
 */
static GList *
libbalsa_carddav_run_multiget(LibBalsaCarddav *carddav, GError **error)
{
	static const libbalsa_webdav_propfind_t vcard_props = {
		.item = RESOURCETYPE,
		.depth = "1",
		.xpath = WEBDAV_MULTISTATUS_XP "[" WEBDAV_OK_XP " and " XPATH_NO_COLL "]/..",
		.ns_list = { "b", NS_CARDDAV, NULL },
		.eval_fn = carddav_extract_vcard_href
	};
	GList *vcard_list = NULL;
	GError *local_err = NULL;

	vcard_list = libbalsa_webdav_propfind(LIBBALSA_WEBDAV(carddav), NULL, &vcard_props, NULL, &local_err);
	if (local_err == NULL) {
		libbalsa_webdav_report_t vcard_report = {
			.name = "b:addressbook-multiget",
			.body = NULL,
			.depth = "1",
			.xpath = WEBDAV_MULTISTATUS_XP "[" WEBDAV_OK_XP "]/..",
			.ns_list = { "b", NS_CARDDAV, NULL },
			.eval_fn = carddav_extract_vcard_data
		};
		GString *report_xml;
		GList *p;

		report_xml = g_string_new("<a:prop>" VCARD_ELEMENTS "</a:prop>");
		for (p = vcard_list; p != NULL; p = p->next) {
			g_string_append_printf(report_xml, "<a:href>%s</a:href>", (gchar *) p->data);
			g_free(p->data);
		}
		g_list_free(vcard_list);
		vcard_report.body = report_xml->str;

		vcard_list = libbalsa_webdav_report(LIBBALSA_WEBDAV(carddav), NULL, &vcard_report, NULL, &local_err);
		g_string_free(report_xml, TRUE);
	}
	if (local_err != NULL) {
		g_propagate_error(error, local_err);
	}

	return vcard_list;
}


/** @brief Extract VCards
 *
 * @param[in] nodes XML node set
 * @param[in] xpath_ctx current XPath context
 * @param[in] user_data user data, unused
 * @return a newly allocated list of LibBalsaAddress elements
 */
static GList *
carddav_extract_vcard_data(xmlNodeSetPtr nodes, xmlXPathContextPtr xpath_ctx, gpointer G_GNUC_UNUSED user_data)
{
	GList *result = NULL;
	int n;

	for (n = 0; n < nodes->nodeNr; n++) {
		xmlNodePtr this_node = nodes->nodeTab[n];

		if (nodes->nodeTab[n]->type == XML_ELEMENT_NODE) {
			gchar *vcard;
			LibBalsaAddress *address;

			vcard = libbalsa_webdav_xp_string_from_sub(this_node, (xmlChar *) XPATH_SUB_VCARD, xpath_ctx);
			address = libbalsa_address_new_from_vcard(vcard, "utf-8");
			g_free(vcard);
			if (address != NULL) {
				g_debug("%s: VCard data for '%s'", __func__, libbalsa_address_get_full_name(address));
				result = g_list_prepend(result, address);
			}
		}
	}
	return g_list_reverse(result);
}


/** @brief Extract address book item href properties
 *
 * @param[in] nodes XML node set
 * @param[in] xpath_ctx current XPath context
 * @param[in] user_data user data, unused
 * @return a newly allocated list of gchar * elements containing the extracted href properties
 */
static GList *
carddav_extract_vcard_href(xmlNodeSetPtr nodes, xmlXPathContextPtr xpath_ctx, gpointer G_GNUC_UNUSED user_data)
{
	GList *result = NULL;
	int n;

	for (n = 0; n < nodes->nodeNr; n++) {
		xmlNodePtr this_node = nodes->nodeTab[n];

		if (nodes->nodeTab[n]->type == XML_ELEMENT_NODE) {
			gchar *href;

			href = libbalsa_webdav_xp_string_from_sub(this_node, (xmlChar *) WEBDAV_XP_SUB_HREF, xpath_ctx);
			if (href != NULL) {
				g_debug("%s: found VCard href: '%s'", __func__, href);
				result = g_list_prepend(result, href);
			}
		}
	}
	return g_list_reverse(result);
}

#endif		/* defined(HAVE_WEBDAV) */
