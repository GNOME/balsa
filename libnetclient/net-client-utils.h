/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef NET_CLIENT_UTILS_H_
#define NET_CLIENT_UTILS_H_


#include <gio/gio.h>


G_BEGIN_DECLS


/** @brief Calculate a CRAM authentication string
 *
 * @param base64_challenge base64-encoded challenge sent by the server
 * @param chksum_type checksum type
 * @param user user name
 * @param passwd password
 * @return a newly allocated string containing the base64-encoded authentication
 *
 * This helper function calculates the the base64-encoded authentication string from the server challenge, the user name and the
 * password according to the standards <a href="https://tools.ietf.org/html/rfc2104">RFC 2104 (MD5, SHA1)</a> and
 * <a href="https://tools.ietf.org/html/rfc4868">RFC 4868 (SHA256, SHA512)</a>.  The caller shall free the returned string when it
 * is not needed any more.
 *
 * \sa <a href="https://tools.ietf.org/html/rfc2195">RFC 2195</a>.
 */
gchar *net_client_cram_calc(const gchar *base64_challenge, GChecksumType chksum_type, const gchar *user, const gchar *passwd)
	G_GNUC_MALLOC;


/** @brief Get the checksum type as string
 *
 * @param chksum_type checksum type
 * @return a string representation of the checksum type
 */
const gchar *net_client_chksum_to_str(GChecksumType chksum_type);


/** @brief Calculate a SASL AUTH PLAIN authentication string
 *
 * @param user user name
 * @param passwd password
 * @return a newly allocated string containing the base64-encoded authentication
 *
 * This helper function calculates the the base64-encoded SASL AUTH PLAIN authentication string from the user name and the password
 * according to the <a href="https://tools.ietf.org/html/rfc4616">RFC 4616</a>.  The caller shall free the returned string when it
 * is not needed any more.
 */
gchar *net_client_auth_plain_calc(const gchar *user, const gchar *passwd)
	G_GNUC_MALLOC;


/** @file
 *
 * This module implements authentication-related helper functions for the network client library.
 */


G_END_DECLS


#endif /* NET_CLIENT_UTILS_H_ */
