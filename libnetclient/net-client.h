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

#ifndef NET_CLIENT_H_
#define NET_CLIENT_H_


#include <stdarg.h>
#include <glib-object.h>
#include <gio/gio.h>


G_BEGIN_DECLS


#define NET_CLIENT_TYPE						(net_client_get_type())
#define NET_CLIENT(obj)						(G_TYPE_CHECK_INSTANCE_CAST((obj), NET_CLIENT_TYPE, NetClient))
#define NET_IS_CLIENT(obj)					(G_TYPE_CHECK_INSTANCE_TYPE((obj), NET_CLIENT_TYPE))
#define NET_CLIENT_CLASS(klass)				(G_TYPE_CHECK_CLASS_CAST((klass), NET_CLIENT_TYPE, NetClientClass))
#define NET_IS_CLIENT_CLASS(klass)			(G_TYPE_CHECK_CLASS_TYPE((klass), NET_CLIENT_TYPE))
#define NET_CLIENT_GET_CLASS(obj)			(G_TYPE_INSTANCE_GET_CLASS((obj), NET_CLIENT_TYPE, NetClientClass))

#define NET_CLIENT_ERROR_QUARK				(g_quark_from_static_string("net-client"))


typedef struct _NetClient NetClient;
typedef struct _NetClientClass NetClientClass;
typedef struct _NetClientPrivate NetClientPrivate;
typedef enum _NetClientError NetClientError;
typedef enum _NetClientCryptMode NetClientCryptMode;


struct _NetClient {
    GObject parent;
    NetClientPrivate *priv;
};


struct _NetClientClass {
    GObjectClass parent;
};


/** @brief Encryption mode */
enum _NetClientCryptMode {
	NET_CLIENT_CRYPT_ENCRYPTED = 1,			/**< TLS encryption @em before starting the protocol required (e.g. SMTPS). */
	NET_CLIENT_CRYPT_STARTTLS,				/**< Protocol-specific STARTTLS encryption required. */
	NET_CLIENT_CRYPT_STARTTLS_OPT,			/**< Optional protocol-specific STARTTLS encryption, proceed unencrypted on fail. */
	NET_CLIENT_CRYPT_NONE					/**< Unencrypted connection. */
};


/** @brief Error codes */
enum _NetClientError {
	NET_CLIENT_ERROR_CONNECTED = 1,			/**< The client is already connected. */
	NET_CLIENT_ERROR_NOT_CONNECTED,			/**< The client is not connected. */
	NET_CLIENT_ERROR_CONNECTION_LOST,		/**< The connection is lost. */
	NET_CLIENT_ERROR_TLS_ACTIVE,			/**< TLS is already active for the connection. */
	NET_CLIENT_ERROR_LINE_TOO_LONG,			/**< The line is too long. */
	NET_CLIENT_ERROR_GNUTLS					/**< A GnuTLS error occurred. */
};


GType net_client_get_type(void)
	G_GNUC_CONST;


/** @brief Create a new network client
 *
 * @param host_and_port remote host and port or service, separated by a colon, which shall be connected
 * @param default_port default remote port if host_and_port does not contain a port
 * @param max_line_len maximum line length supported by the underlying protocol
 * @return the net network client object
 *
 * Create a new network client object with the passed parameters.  Call <tt>g_object_unref()</tt> on it to shut down the connection
 * and to free all resources of it.
 */
NetClient *net_client_new(const gchar *host_and_port, guint16 default_port, gsize max_line_len);


/** @brief Configure a network client
 *
 * @param client network client
 * @param host_and_port remote host and port or service, separated by a colon, which shall be connected
 * @param default_port default remote port if host_and_port does not contain a port
 * @param max_line_len maximum line length supported by the underlying protocol
 * @param error filled with error information on error
 * @return TRUE is the connection was successful, FALSE on error
 *
 * Set the remote host and port and the maximum line length to the passed parameters, replacing the previous values set by calling
 * net_client_new().
 */
gboolean net_client_configure(NetClient *client, const gchar *host_and_port, guint16 default_port, gsize max_line_len,
							  GError **error);


/** @brief Get the target host of a network client
 *
 * @param client network client
 * @return the currently set host on success, or NULL on error
 *
 * @note The function returns the value of @em host_and_port set by net_client_new() or net_client_configure().
 */
const gchar *net_client_get_host(NetClient *client);


/** @brief Connect a network client
 *
 * @param client network client
 * @param error filled with error information on error
 * @return TRUE is the connection was successful, FALSE on error
 *
 * Try to connect the remote host and TCP port.
 */
gboolean net_client_connect(NetClient *client, GError **error);


/** @brief Check if a network client is connected
 *
 * @param client network client
 * @return TRUE if the passed network client is connected, FALSE if not
 */
gboolean net_client_is_encrypted(NetClient *client);


/** @brief Load a certificate and private key from PEM data
 *
 * @param client network client
 * @param pem_data NUL-terminated PEM data buffer
 * @param error filled with error information on error
 * @return TRUE is the certificate and private key were loaded, FALSE on error
 *
 * Load a client certificate and private key form the passed PEM data.  If the private key is encrypted, the signal @ref cert-pass
 * is emitted.
 *
 * Use this function (or net_client_set_cert_from_file()) if the remote server requires a client certificate.
 */
gboolean net_client_set_cert_from_pem(NetClient *client, const gchar *pem_data, GError **error);


/** @brief Load a certificate and private key from a PEM file
 *
 * @param client network client
 * @param pem_path path name of the file containing the PEM certificate and private key
 * @param error filled with error information on error
 * @return TRUE is the certificate and private key were loaded, FALSE on error
 *
 * Load a client certificate and private key form the passed PEM file.  If the private key is encrypted, the signal @ref cert-pass
 * is emitted.
 *
 * Use this function (or net_client_set_cert_from_pem()) if the remote server requires a client certificate.
 */
gboolean net_client_set_cert_from_file(NetClient *client, const gchar *pem_path, GError **error);


/** @brief Start encryption
 *
 * @param client network client
 * @param error filled with error information on error
 * @return TRUE if the connection is now TLS encrypted, FALSE on error
 *
 * Try to negotiate TLS encryption.  If the remote server presents an untrusted certificate, the signal @ref cert-check is emitted.
 */
gboolean net_client_start_tls(NetClient *client, GError **error);


/** @brief Read a CRLF-terminated line from a network client
 *
 * @param client network client
 * @param recv_line filled with the response buffer on success, may be NULL to discard the line read
 * @param error filled with error information on error
 * @return TRUE is the read operation was successful, FALSE on error
 *
 * Read a CRLF-terminated line from the remote server and return it in the passed buffer.  The terminating CRLF is always stripped.
 *
 * @note The caller must free the returned buffer when it is not needed any more.
 */
gboolean net_client_read_line(NetClient *client, gchar **recv_line, GError **error);


/** @brief Write data to a network client
 *
 * @param client network client
 * @param buffer data buffer
 * @param count number of bytes in the data buffer
 * @param error filled with error information on error
 * @return TRUE is the send operation was successful, FALSE on error
 *
 * Send the complete data buffer to the remote server.  The caller must ensure that the allowed line length is not exceeded, and
 * that the lines are CRLF-terminated.
 */
gboolean net_client_write_buffer(NetClient *client, const gchar *buffer, gsize count, GError **error);


/** @brief Write a line to a network client
 *
 * @param client network client
 * @param format printf-like format string
 * @param args argument list for the format
 * @param error filled with error information on error
 * @return TRUE is the send operation was successful, FALSE on error
 *
 * Format a line according to the passed arguments, append CRLF, and send it to the remote server.
 */
gboolean net_client_vwrite_line(NetClient *client, const gchar *format, va_list args, GError **error);


/** @brief Write a line with a variable argument list to a network client
 *
 * @param client network client
 * @param format printf-like format string
 * @param error filled with error information on error
 * @param ... additional arguments according to the format string
 * @return TRUE is the send operation was successful, FALSE on error
 *
 * Format a line according to the passed arguments, append CRLF, and send it to the remote server.
 */
gboolean net_client_write_line(NetClient *client, const gchar *format, GError **error, ...)
	G_GNUC_PRINTF(2, 4);


/** @brief Execute a command-response sequence with a network client
 *
 * @param client network client
 * @param response filled with the response buffer on success, may be NULL to discard the response
 * @param request_fmt printf-like request format string
 * @param error filled with error information on error
 * @param ... additional arguments according to the format string
 * @return TRUE is the operation was successful, FALSE on error
 *
 * This convenience function calls net_client_vwrite_line() to write a one-line command to the remote server, and then
 * net_client_read_line() to read the response.  The terminating CRLF of the response is always stripped.
 *
 * @note The caller must free the returned buffer when it is not needed any more.
 */
gboolean net_client_execute(NetClient *client, gchar **response, const gchar *request_fmt, GError **error, ...)
	G_GNUC_PRINTF(3, 5);


/** @brief Set the connection time-out
 *
 * @param client network client
 * @param timeout_secs time-out in seconds
 * @return TRUE on success, FALSE on error
 *
 * @note The default timeout is 180 seconds (3 minutes).
 */
gboolean net_client_set_timeout(NetClient *client, guint timeout_secs);


/** 
 * @mainpage
 *
 * This library provides an implementation of CRLF-terminated line-based client protocols built on top of GIO.  It provides a base
 * module (see file net-client.h), containing the line-based IO methods, and on top of that a SMTP (RFC5321) client class (see
 * file net-client-smtp.h).  The file net-client-utils.h contains some helper functions for authentication.
 *
 * \author Written by Albrecht Dreß mailto:albrecht.dress@arcor.de
 * \copyright Copyright &copy; Albrecht Dreß 2017<br/>
 * This library is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as
 * published bythe Free Software Foundation, either version 3 of the License, or (at your option) any later version.<br/>
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.<br/>
 * You should have received a copy of the GNU General Public License along with this library.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * @file
 *
 * This module implements the base class for a simple network client communication on the basis of CRLF-terminated lines.
 *
 * @section signals Signals
 *
 * The following signals are implemented:
 *
 * - @anchor cert-pass cert-pass
 *   @code gchar *cert_pass(NetClient *client, GByteArray *peer_cert_der, gpointer user_data) @endcode The client certificate used
 *   for the connection has a password-protected key.  The certificate in DER format is passed to the signal handler, and shall
 *   return a newly allocated string containing the password.  The string is wiped and freed when it is not needed any more.
 * - @anchor cert-check cert-check
 *   @code gboolean check_cert(NetClient *client, GTlsCertificate *peer_cert, GTlsCertificateFlags errors, gpointer user_data)
 *   @endcode The server certificate is not trusted.  The received certificate and the errors which occurred during the check are
 *   passed to the signal handler.  The handler shall return TRUE to accept the certificate, or FALSE to reject it.
 * - @anchor auth auth
 *   @code gchar **get_auth(NetClient *client, gpointer user_data) @endcode Authentication is required by the remote server.  The
 *   signal handler shall return a NULL-terminated array of strings, containing the user name in the first and the password in the
 *   second element.  The strings are wiped and freed when they are not needed any more.  Return NULL if no authentication is
 *   required.
 */


G_END_DECLS


#endif /* NET_CLIENT_H_ */