/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of
 * the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this
 * library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef NET_CLIENT_SMTP_H_
#define NET_CLIENT_SMTP_H_


#include "net-client.h"


G_BEGIN_DECLS


#define NET_CLIENT_SMTP_TYPE                            (net_client_smtp_get_type())
#define NET_CLIENT_SMTP(obj)                            (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                                                                                    NET_CLIENT_SMTP_TYPE, \
                                                                                    NetClientSmtp))
#define NET_IS_CLIENT_SMTP(obj)                         (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
                                                                                    NET_CLIENT_SMTP_TYPE))
#define NET_CLIENT_SMTP_CLASS(klass)            (G_TYPE_CHECK_CLASS_CAST((klass), \
                                                                         NET_CLIENT_SMTP_TYPE, \
                                                                         NetClientSmtpClass))
#define NET_IS_CLIENT_SMTP_CLASS(klass)         (G_TYPE_CHECK_CLASS_TYPE((klass), \
                                                                         NET_CLIENT_SMTP_TYPE))
#define NET_CLIENT_SMTP_GET_CLASS(obj)          (G_TYPE_INSTANCE_GET_CLASS((obj), \
                                                                           NET_CLIENT_SMTP_TYPE, \
                                                                           NetClientSmtpClass))

#define NET_CLIENT_SMTP_ERROR_QUARK                     (g_quark_from_static_string( \
                                                             "net-client-smtp"))


typedef struct _NetClientSmtp NetClientSmtp;
typedef struct _NetClientSmtpClass NetClientSmtpClass;
typedef struct _NetClientSmtpPrivate NetClientSmtpPrivate;
typedef struct _NetClientSmtpMessage NetClientSmtpMessage;
typedef enum _NetClientSmtpDsnMode NetClientSmtpDsnMode;


/** @brief SMTP-specific error codes */
enum _NetClientSmtpError {
    NET_CLIENT_ERROR_SMTP_PROTOCOL = 1,                 /**< A bad server reply has been
                                                           received. */
    NET_CLIENT_ERROR_SMTP_TRANSIENT,                    /**< The server replied with a transient
                                                           error code (code 4yz). */
    NET_CLIENT_ERROR_SMTP_PERMANENT,                    /**< The server replied with a permanent
                                                           error code (code 5yz). */
    NET_CLIENT_ERROR_SMTP_NO_AUTH,              /**< The server offers no suitable
                                                   authentication mechanism. */
    NET_CLIENT_ERROR_SMTP_NO_STARTTLS                   /**< The server does not support
                                                           STARTTLS. */
};


/** @name SMTP authentication methods
 * @{
 */
/** RFC 4616 "PLAIN" authentication method. */
#define NET_CLIENT_SMTP_AUTH_PLAIN                      0x01U
/** "LOGIN" authentication method. */
#define NET_CLIENT_SMTP_AUTH_LOGIN                      0x02U
/** RFC 2195 "CRAM-MD5" authentication method. */
#define NET_CLIENT_SMTP_AUTH_CRAM_MD5           0x04U
/** RFC xxxx "CRAM-SHA1" authentication method. */
#define NET_CLIENT_SMTP_AUTH_CRAM_SHA1          0x08U
/** RFC 4752 "GSSAPI" authentication method. */
#define NET_CLIENT_SMTP_AUTH_GSSAPI                     0x10U
/** Mask of all safe authentication methods, i.e. all methods which do not send the cleartext
   password. */
#define NET_CLIENT_SMTP_AUTH_SAFE                       \
    (NET_CLIENT_SMTP_AUTH_CRAM_MD5 + NET_CLIENT_SMTP_AUTH_CRAM_SHA1 + \
     NET_CLIENT_SMTP_AUTH_GSSAPI)
/** Mask of all authentication methods. */
#define NET_CLIENT_SMTP_AUTH_ALL                        \
    (NET_CLIENT_SMTP_AUTH_PLAIN + NET_CLIENT_SMTP_AUTH_LOGIN + NET_CLIENT_SMTP_AUTH_SAFE)
/** Mask of all authentication methods which do not require a password. */
#define NET_CLIENT_SMTP_AUTH_NO_PWD                     NET_CLIENT_SMTP_AUTH_GSSAPI
/** @} */


struct _NetClientSmtp {
    NetClient parent;
    NetClientSmtpPrivate *priv;
};


struct _NetClientSmtpClass {
    NetClientClass parent;
};


/** @brief Delivery Status Notification mode
 *
 * See <a href="https://tools.ietf.org/html/rfc3461">RFC 3461</a> for a description of Delivery
 * Status Notifications (DSNs).  The
 * DSN mode is the logical OR of these options.
 */
enum _NetClientSmtpDsnMode {
    NET_CLIENT_SMTP_DSN_NEVER = 0,                      /**< Never request a DSN (do not combine
                                                           with other options). */
    NET_CLIENT_SMTP_DSN_SUCCESS = 1,                    /**< Request a DSN on successful
                                                           delivery. */
    NET_CLIENT_SMTP_DSN_FAILURE = 2,                    /**< Request a DSN on delivery failure.
                                                         */
    NET_CLIENT_SMTP_DSN_DELAY = 4                       /**< Request a DSN if delivery of a
                                                           message has been delayed. */
};


/** @brief SMTP Message Transmission Callback Function
 *
 * The user-provided callback function to send a message to the remote SMTP server:
 * - @em buffer - shall be filled with the next chunk of data
 * - @em count - maximum number of bytes which may be written to @em buffer
 * - @em user_data - user data pointer (_NetClientSmtpMessage::user_data)
 * - @em error - shall be filled with error information if an error occurs in the callback
 * - return value: a value > 0 indicating the number of bytes written to @em buffer, or 0 to
 * indicate that all data has been
 *   transferred, or a value < 0 to indicate an error in the callback function.
 *
 * @note The callback function is responsible for properly formatting the message body according
 * to
 *       <a href="https://tools.ietf.org/html/rfc5321">RFC 5321</a>, <a
 * href="https://tools.ietf.org/html/rfc5322">RFC 5322</a> and
 *       further relevant standards, e.g. by using <a
 * href="http://spruce.sourceforge.net/gmime/">GMime</a>.
 */
typedef gssize (*NetClientSmtpSendCb)(gchar   *buffer,
                                      gsize    count,
                                      gpointer user_data,
                                      GError **error);


GType net_client_smtp_get_type(void)
G_GNUC_CONST;


/** @brief Create a new SMTP network client
 *
 * @param host host name or IP address to connect
 * @param port port number to connect
 * @param crypt_mode encryption mode
 * @return the SMTP network client object
 */
NetClientSmtp *net_client_smtp_new(const gchar       *host,
                                   guint16            port,
                                   NetClientCryptMode crypt_mode);


/** @brief Set allowed SMTP AUTH methods
 *
 * @param client SMTP network client object
 * @param encrypted set allowed methods for encrypted or unencrypted connections
 * @param allow_auth mask of allowed authentication methods
 * @return TRUE on success or FALSE on error
 *
 * Set the allowed authentication methods for the passed connection.  The default is @ref
 * NET_CLIENT_SMTP_AUTH_ALL for encrypted and
 * @ref NET_CLIENT_SMTP_AUTH_SAFE for unencrypted connections, respectively.
 *
 * @note Call this function @em before calling net_client_smtp_connect().
 */
gboolean net_client_smtp_allow_auth(NetClientSmtp *client,
                                    gboolean       encrypted,
                                    guint          allow_auth);


/** @brief Connect a SMTP network client
 *
 * @param client SMTP network client object
 * @param greeting filled with the greeting of the SMTP server on success, may be NULL to ignore
 * @param error filled with error information if the connection fails
 * @return TRUE on success or FALSE if the connection failed
 *
 * Connect the remote SMTP server, initialise the encryption if requested, and emit the @ref
 * auth signal to request authentication
 * information.  Simply ignore the signal for an unauthenticated connection.
 *
 * The function will try only @em one authentication method supported by the server and enabled
 * for the current encryption state
 * (see net_client_smtp_allow_auth() and \ref NET_CLIENT_SMTP_AUTH_ALL etc.).  The priority is,
 * from highest to lowest, GSSAPI (if
 * configured), CRAM-SHA1, CRAM-MD5, PLAIN or LOGIN.
 *
 * In order to shut down a successfully established connection, just call
 *<tt>g_object_unref()</tt> on the SMTP network client
 * object.
 *
 * @note The caller must free the returned greeting when it is not needed any more.
 */
gboolean net_client_smtp_connect(NetClientSmtp *client,
                                 gchar        **greeting,
                                 GError       **error);


/** @brief Check if the SMTP network client supports Delivery Status Notifications
 *
 * @param client connected SMTP network client object
 * @return TRUE is DSN's are supported, FALSE if not
 *
 * Return if the connected SMTP server announced support for Delivery Status Notifications
 *(DSNs) according to
 * <a href="https://tools.ietf.org/html/rfc3461">RFC 3461</a>.
 */
gboolean net_client_smtp_can_dsn(NetClientSmtp *client);


/** @brief Send a message to a SMTP network client
 *
 * @param client connected SMTP network client object
 * @param message message data
 * @param error filled with error information if the connection fails
 * @return TRUE on success or FALSE if sending the message failed
 *
 * Send the passed SMTP message to the connected SMTP server.
 */
gboolean net_client_smtp_send_msg(NetClientSmtp              *client,
                                  const NetClientSmtpMessage *message,
                                  GError                    **error);


/** @brief Create a SMTP message
 *
 * @param data_callback callback function called to send the message data
 * @param user_data additional user data passed to the callback function
 * @return a newly create SMTP message
 *
 * Create a message suitable for transmission by calling net_client_smtp_send_msg().  At least
 * one sender and at least one recipient
 * must be added by calling net_client_smtp_msg_set_sender() and
 * net_client_smtp_msg_add_recipient(), respectively.  When the SMTP
 * message is not needed any more, call net_client_smtp_msg_free() to free it.
 */
NetClientSmtpMessage *net_client_smtp_msg_new(NetClientSmtpSendCb data_callback,
                                              gpointer            user_data)
G_GNUC_MALLOC;


/** @brief Set the sender of a SMTP message
 *
 * @param smtp_msg SMTP message returned by net_client_smtp_msg_new()
 * @param rfc5321_sender RFC 5321-compliant sender address
 * @return TRUE on success or FALSE on error
 *
 * Set the sender address ("MAIL FROM" reverse-path, see <a
 * href="https://tools.ietf.org/html/rfc5321">RFC 5321</a>) of the SMTP
 * message.
 */
gboolean net_client_smtp_msg_set_sender(NetClientSmtpMessage *smtp_msg,
                                        const gchar          *rfc5321_sender);


/** @brief Set options for Delivery Status Notifications
 *
 * @param smtp_msg SMTP message returned by net_client_smtp_msg_new()
 * @param envid ENVID parameter to the ESMTP MAIL command, may be NULL
 * @param ret_full return the full message on failure instead of the headers only
 * @return TRUE on success or FALSE on error
 *
 * Set the @em ENVID and @em RET parameters for Delivery Status Notifications (DSN's).  The
 * default is to omit the ENVID and to
 * request headers only.
 */
gboolean net_client_smtp_msg_set_dsn_opts(NetClientSmtpMessage *smtp_msg,
                                          const gchar          *envid,
                                          gboolean              ret_full);


/** @brief Add a recipient to a SMTP message
 *
 * @param smtp_msg SMTP message returned by net_client_smtp_msg_new()
 * @param rfc5321_rcpt RFC 5321-compliant recipient address
 * @param dsn_mode Delivery Status Notification mode for the recipient
 * @return TRUE on success or FALSE on error
 *
 * Add a recipient address ("RCPT TO" forward-path, see <a
 * href="https://tools.ietf.org/html/rfc5321">RFC 5321</a>) to the SMTP
 * message.
 */
gboolean net_client_smtp_msg_add_recipient(NetClientSmtpMessage *smtp_msg,
                                           const gchar          *rfc5321_rcpt,
                                           NetClientSmtpDsnMode  dsn_mode);


/** @brief Free a SMTP message
 *
 * @param smtp_msg SMTP message returned by net_client_smtp_msg_new()
 *
 * Free all resources of the passed SMTP message.
 */
void net_client_smtp_msg_free(NetClientSmtpMessage *smtp_msg);


/** @file
 *
 * This module implements a SMTP client class conforming with <a
 * href="https://tools.ietf.org/html/rfc5321">RFC 5321</a>.
 *
 * The following additional features are supported:
 * - Authentication according to <a href="https://tools.ietf.org/html/rfc4954">RFC 4954</a>,
 * using the methods
 *   - CRAM-MD5 according to <a href="https://tools.ietf.org/html/rfc2195">RFC 2195</a>
 *   - CRAM-SHA1 according to <a href="https://tools.ietf.org/html/rfc_TBD">TBD</a>
 *   - PLAIN according to <a href="https://tools.ietf.org/html/rfc4616">RFC 4616</a>
 *   - LOGIN
 *   - GSSAPI according to <a href="https://tools.ietf.org/html/rfc4752">RFC 4752</a> (if
 * configured with gssapi support)
 * - STARTTLS encryption according to <a href="https://tools.ietf.org/html/rfc3207">RFC 3207</a>
 * - Delivery Status Notifications (DSNs) according to <a
 * href="https://tools.ietf.org/html/rfc3461">RFC 3461</a>
 */


G_END_DECLS


#endif /* NET_CLIENT_SMTP_H_ */
