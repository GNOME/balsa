/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017 - 2020
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

#ifndef NET_CLIENT_UTILS_H_
#define NET_CLIENT_UTILS_H_


#include "config.h"
#include <gio/gio.h>
#include "net-client.h"


G_BEGIN_DECLS


#if defined(HAVE_GSSAPI)

typedef struct _NetClientGssCtx NetClientGssCtx;

#endif		/* HAVE_GSSAPI */


typedef struct _NetClientProbeResult NetClientProbeResult;

/** @brief Server probe results */
struct _NetClientProbeResult {
	guint16 port;							/**< The port where the server listens. */
	NetClientCryptMode crypt_mode;			/**< The encryption mode the server supports for the returned port. */
	NetClientAuthMode auth_mode;			/**< The authentication modes the server supports for the port and encryption mode. */
};


/** @brief Check is a host is resolvable and reachable
 *
 * @param host host name or IP address
 * @param error filled with error information on error
 * @return TRUE if the passed host name is resolvable and reachable
 */
gboolean net_client_host_reachable(const gchar *host, GError **error);


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
	G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;


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
 * This helper function calculates the base64-encoded SASL AUTH PLAIN authentication string from the user name and the password
 * according to <a href="https://tools.ietf.org/html/rfc4616">RFC 4616</a>.  The caller shall free the returned string when it is
 * not needed any more.
 */
gchar *net_client_auth_plain_calc(const gchar *user, const gchar *passwd)
	G_GNUC_MALLOC G_GNUC_WARN_UNUSED_RESULT;


/** @brief Safely free an authentication string
 *
 * @param str string
 *
 * This function can be used to safely free an authentication string.  It overwrites the passed string with random characters, and
 * then frees it.
 */
void net_client_free_authstr(gchar *str);


/** @brief Create a token for anonymous authentication
 *
 * @return a newly allocated string containing the base64-encoded authentication
 *
 * This helper function calculates a base64-encoded SASL AUTH ANONYMOUS authentication token which is used as trace information by
 * the server.  As recommended by RFC 4505, the token does not contain personal data.  The returned value is the encoded SHA256 hex
 * hash of the string <c>user-name\@host-name:time</c> (time is the creation time stamp as returned by time()).  This token will be
 * unique, but makes it impossible for the server to extract user and host.  However, the client @em may store to token if linking
 * server to client operations is required, e.g. for debugging purposes.
 *
 * The caller shall free the returned string when it is not needed any more.
 */
gchar *net_client_auth_anonymous_token(void)
	G_GNUC_WARN_UNUSED_RESULT;


/** \brief Return the host part of a host:port string
 *
 * @param[in] host_and_port a string containing a host name, optionally followed by a colon and a port number
 * @return a newly allocated string containing the host part only
 */
gchar *net_client_host_only(const gchar *host_and_port)
	G_GNUC_WARN_UNUSED_RESULT;


#if defined(HAVE_GSSAPI)

/** @brief Create a GSSAPI authentication context
 *
 * @param service service name (<i>smtp</i>, <i>imap</i> or <i>pop</i>)
 * @param host full-qualified host name of the machine providing the service
 * @param user user name
 * @param error filled with error information on error
 * @return a newly allocated and initialised GSSAPI context on success, or NULL on error
 *
 * Create a new authentication context for Kerberos v5 SASL AUTH GSSAPI based authentication for the passed service and host
 * according to <a href="https://tools.ietf.org/html/rfc4752">RFC 4752</a>.  The returned context shall be used for the
 * authentication process and must be freed by calling net_client_gss_ctx_free().
 *
 * @note The host may optionally contain a port definition (e.g. <tt>smtp.mydom.org:25</tt>) which will be omitted.
 * @sa net_client_gss_auth_step(), net_client_gss_auth_finish()
 */
NetClientGssCtx *net_client_gss_ctx_new(const gchar *service, const gchar *host, const gchar *user, GError **error)
	G_GNUC_WARN_UNUSED_RESULT;


/** @brief Perform a GSSAPI authentication step
 *
 * @param gss_ctx GSSAPI authentication context
 * @param in_token base64-encoded input token, or NULL for the initial authentication step
 * @param out_token filled with the base64-encoded output token on success
 * @param error filled with error information on error
 * @return 0 if an additional step has to be performed, 1 if net_client_gss_auth_finish() shall be called, or -1 on error
 *
 * Initially, the function shall be called with a NULL input token.  The resulting output token shall be sent to the remote server
 * to obtain a new input token until the function returns 1.  Then, net_client_gss_auth_finish() shall be called to finish the
 * authentication process.
 */
gint net_client_gss_auth_step(NetClientGssCtx *gss_ctx, const gchar *in_token, gchar **out_token, GError **error)
	G_GNUC_WARN_UNUSED_RESULT;


/** @brief Finish the GSSAPI authentication
 *
 * @param gss_ctx GSSAPI authentication context
 * @param in_token base64-encoded input token, received in the final net_client_gss_auth_step()
 * @param error filled with error information on error
 * @return the base64-encoded final authentication token on success, or NULL on error
 *
 * Create the final token which has to be sent to the remote server to finalise the GSSAPI authentication process.
 */
/*lint -ecall(9076,net_client_gss_auth_finish)		suppress false-positive re. [const] NetClientGssCtx* (FlexeLint 9.00L bug) */
gchar *net_client_gss_auth_finish(const NetClientGssCtx *gss_ctx, const gchar *in_token, GError **error)
	G_GNUC_WARN_UNUSED_RESULT;


/** @brief Free a GSSAPI authentication context
 *
 * @param gss_ctx GSSAPI authentication context
 *
 * Free all resources in the passed GSSAPI authentication context by net_client_gss_ctx_new() and the context itself.
 */
void net_client_gss_ctx_free(NetClientGssCtx *gss_ctx);

#endif		/* HAVE_GSSAPI */


/** @file
 *
 * This module implements probing and authentication-related helper functions for the network client library.
 */


G_END_DECLS


#endif /* NET_CLIENT_UTILS_H_ */
