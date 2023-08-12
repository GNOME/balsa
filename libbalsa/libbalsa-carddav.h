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
 *
 * Relevant standard used in this module, in addition to those referenced in libbalsa-webdav.h:
 * * RFC 6352: CardDAV: vCard Extensions to Web Distributed Authoring and Versioning (WebDAV)
 */

#ifndef _LIBBALSA_CARDDAV_H_
#define _LIBBALSA_CARDDAV_H_


#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif						/* HAVE_CONFIG_H */


#if defined(HAVE_WEBDAV)


#include "address.h"
#include "libbalsa-webdav.h"


G_BEGIN_DECLS


#define LIBBALSA_CARDDAV_TYPE				(libbalsa_carddav_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaCarddav, libbalsa_carddav, LIBBALSA, CARDDAV, LibBalsaWebdav)


/** @brief Create a new CardDAV object
 *
 * @param[in] uri full URI of the CardDAV address book
 * @param[in] username user name used for authentication
 * @param[in] password password used for authentication
 * @param[in] refresh_secs minimum time in seconds after which the internal cache is refreshed if necessary
 * @param[in] force_multiget force using @em addressbook-multiget even if the server claims to support addressbook-query
 * @return a new CardDAV object
 * @note Some broken servers (e.g. <em>Deutsche Telekom</em> <c>spica.t-online.de</c>) claim to support @em addressbook-query
 *       reports, but do not return anything, whilst @em addressbook-multiget work as expected.
 */
LibBalsaCarddav *libbalsa_carddav_new(const gchar *uri,
									  const gchar *username,
									  const gchar *password,
									  guint        refresh_secs,
									  gboolean     force_multiget)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Access the cached list of CardDAV addresses
 *
 * @param[in] carddav CardDAV object
 * @return the cached list of CardDAV addresses of @ref LibBalsaAddress elements
 *
 * Lock and return the internal cache of CardDAV addresses.  If the refresh time for the cache has been reached, a thread checking
 * for updates is started.  After processing the list, the caller @em MUST call @ref libbalsa_carddav_unlock_addrlist() to release
 * the lock.
 *
 * @note The returned list is owned by the LibBalsaCarddav object and <em>MUST NOT</em> be modified by the caller.
 */
const GList *libbalsa_carddav_lock_addrlist(LibBalsaCarddav *carddav);

/** @brief Drop access the cached list of CardDAV addresses
 *
 * @param[in] carddav CardDAV object
 *
 * Release the lock of the internal cache of CardDAV addresses.  This function @em MUST be called in balance with
 * @ref libbalsa_carddav_lock_addrlist().
 */
void libbalsa_carddav_unlock_addrlist(LibBalsaCarddav *carddav);

/** @brief Get the last error message from running a thread
 *
 * @param[in] carddav CardDAV object
 * @return the last error message, @c NULL of no error occurred
 * @note Reading the last error message clears the internal storage.\n
 *       The caller @em MUST free the returned value.\n
 *       The threads display @em all error messages by calling libbalsa_information().
 */
gchar *libbalsa_carddav_get_last_error(LibBalsaCarddav *carddav)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Add an address to the CardDAV address book
 *
 * @param[in] carddav CardDAV object
 * @param[in] address address which shall be added to the remote CardDAV address book
 * @param[in,out] error filled with error information on error, may be NULL
 * @return @c TRUE on success, @FALSE on error
 */
gboolean libbalsa_carddav_add_address(LibBalsaCarddav *carddav,
									  LibBalsaAddress *address,
									  GError         **error);

/** @brief List available CardDAV resources
 *
 * @param[in] domain_or_uri a HTTPS URI of a CardDAV server or a domain to perform a DNS lookup
 * @param[in] username user name used for authentication
 * @param[in] password password used for authentication
 * @param[in,out] error filled with error information on error, may be NULL
 * @return a list of @ref libbalsa_webdav_resource_t items on success
 *
 * List CardDAV resources, depending upon the format of the passed @em domain_or_uri parameter:
 * * a https uri including a path component is used as is;
 * * for a https uri without path use @c .well-known/carddav as path (RFC 6764, sect. 5);
 * * otherwise perform a DNS SRV lookup for @c _carddavs._tcp.<em>domain</em> (RFC 6352, sect. 11).
 *
 * @note The returned list @em MUST be freed by the caller.  Use libbalsa_webdav_resource_free() to free the list items.
 * @sa libbalsa_webdav_lookup_srv()
 */
GList *libbalsa_carddav_list(const gchar *domain_or_uri,
							 const gchar *username,
							 const gchar *password,
							 GError     **error);


G_END_DECLS


#endif		/* defined(HAVE_WEBDAV) */


#endif /* _LIBBALSA_CARDDAV_H_ */
