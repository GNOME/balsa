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

#ifndef NET_CLIENT_POP_H_
#define NET_CLIENT_POP_H_


#include "net-client.h"


G_BEGIN_DECLS


#define NET_CLIENT_POP_TYPE					(net_client_pop_get_type())
#define NET_CLIENT_POP(obj)					(G_TYPE_CHECK_INSTANCE_CAST((obj), NET_CLIENT_POP_TYPE, NetClientPop))
#define NET_IS_CLIENT_POP(obj)				(G_TYPE_CHECK_INSTANCE_TYPE((obj), NET_CLIENT_POP_TYPE))
#define NET_CLIENT_POP_CLASS(klass)			(G_TYPE_CHECK_CLASS_CAST((klass), NET_CLIENT_POP_TYPE, NetClientPopClass))
#define NET_IS_CLIENT_POP_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE((klass), NET_CLIENT_POP_TYPE))
#define NET_CLIENT_POP_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS((obj), NET_CLIENT_POP_TYPE, NetClientPopClass))

#define NET_CLIENT_POP_ERROR_QUARK			(g_quark_from_static_string("net-client-pop"))


typedef struct _NetClientPop NetClientPop;
typedef struct _NetClientPopClass NetClientPopClass;
typedef struct _NetClientPopPrivate NetClientPopPrivate;
typedef struct _NetClientPopMessage NetClientPopMessage;
typedef struct _NetClientPopMessageInfo NetClientPopMessageInfo;


/** @brief POP-specific error codes */
enum _NetClientPopError {
	NET_CLIENT_ERROR_POP_PROTOCOL = 1,		/**< A bad server reply has been received. */
	NET_CLIENT_ERROR_POP_SERVER_ERR,   		/**< The server replied with an error. */
	NET_CLIENT_ERROR_POP_NO_AUTH,      		/**< The server offers no suitable authentication mechanism. */
	NET_CLIENT_ERROR_POP_NO_STARTTLS		/**< The server does not support STARTTLS. */
};


/** @name POP authentication methods
 *
 * Note that the availability of these authentication methods depends upon the result of the CAPABILITY list.  According to RFC
 * 1939, Section 4, at least either APOP or USER/PASS @em must be supported.
 * @{
 */
/** RFC 1939 "USER" and "PASS" authentication method. */
#define NET_CLIENT_POP_AUTH_USER_PASS		0x01U
/** RFC 1939 "APOP" authentication method. */
#define NET_CLIENT_POP_AUTH_APOP			0x02U
/** RFC 5034 SASL "LOGIN" authentication method. */
#define NET_CLIENT_POP_AUTH_LOGIN			0x04U
/** RFC 5034 SASL "PLAIN" authentication method. */
#define NET_CLIENT_POP_AUTH_PLAIN			0x08U
/** RFC 5034 SASL "CRAM-MD5" authentication method. */
#define NET_CLIENT_POP_AUTH_CRAM_MD5		0x10U
/** RFC 5034 SASL "CRAM-SHA1" authentication method. */
#define NET_CLIENT_POP_AUTH_CRAM_SHA1		0x20U
/** RFC 4752 "GSSAPI" authentication method. */
#define NET_CLIENT_POP_AUTH_GSSAPI			0x40U
/** Mask of all safe authentication methods, i.e. all methods which do not send the cleartext password. */
#define NET_CLIENT_POP_AUTH_SAFE			\
	(NET_CLIENT_POP_AUTH_APOP + NET_CLIENT_POP_AUTH_CRAM_MD5 + NET_CLIENT_POP_AUTH_CRAM_SHA1 + NET_CLIENT_POP_AUTH_GSSAPI)
/** Mask of all authentication methods. */
#define NET_CLIENT_POP_AUTH_ALL				\
	(NET_CLIENT_POP_AUTH_USER_PASS + NET_CLIENT_POP_AUTH_PLAIN + NET_CLIENT_POP_AUTH_LOGIN + NET_CLIENT_POP_AUTH_SAFE)
/** Mask of all authentication methods which do not require a password. */
#define NET_CLIENT_POP_AUTH_NO_PWD			NET_CLIENT_POP_AUTH_GSSAPI
/** @} */


struct _NetClientPop {
    NetClient parent;
    NetClientPopPrivate *priv;
};


struct _NetClientPopClass {
	NetClientClass parent;
};


/** @brief Message information
 *
 * This structure is returned in a GList by net_client_pop_list() and contains information about on message in the remote mailbox.
 */
struct _NetClientPopMessageInfo {
	guint id;					/**< Message ID in the remote mailbox. */
	gsize size;					/**< Size of the message in bytes. */
	gchar *uid;					/**< Message UID, or NULL if it was not requested or the remote server does not support the UIDL
	 	 	 	 	 	 	 	 * command. */
};


/** @brief POP3 Message Read Callback Function
 *
 * The user-provided callback function to receive a message from the remote POP3 server:
 * - @em buffer - the next NUL-terminated chunk of data, always guaranteed to consist of complete, LF terminated lines, or NULL
 *   when the @em count is <= 0 (see below)
 * - @em count - indicates the number of bytes in the buffer (> 0), the end of the message (== 0), or that the download is
 *   terminated due to an error condition (-1)
 * - @em lines - number of lines in the buffer, valid only if @em count > 0
 * - @em info - information for the current message
 * - @em user_data - user data pointer
 * - @em error - shall be filled with error information if an error occurs in the callback; this location is actually the @em error
 *   parameter passed to net_client_pop_retr() with the exception of a call when the @em count is -1 when this parameter is always
 *   NULL
 * - return value: TRUE if the message download shall proceed, or FALSE to terminate it because an error occurred in the callback.
 *   In the latter case, the callback function should set @em error appropriately.
 *
 * The message retrieved from the remote POP3 server is passed as "raw" data.  The line endings are always LF (i.e. @em not CRLF),
 * and byte-stuffed termination '.' characters have been unstuffed.  If the data passed to the callback function shall be fed into
 * <a href="http://spruce.sourceforge.net/gmime/">GMime</a>, it is thus @em not necessary to run it through a GMimeFilterCRLF
 * filter.
 *
 * The download of every message is terminated by calling the callback with a @em count of 0.  If the callback returns FALSE for a
 * count >= 0, it is called again for the same message with count == -1 before the download is terminated.  The return value of the
 * callback called with count == -1 is ignored.
 */
typedef gboolean (*NetClientPopMsgCb)(const gchar *buffer, gssize count, gsize lines, const NetClientPopMessageInfo *info,
									  gpointer user_data, GError **error);


GType net_client_pop_get_type(void)
	G_GNUC_CONST;


/** @brief Create a new POP network client
 *
 * @param host host name or IP address to connect
 * @param port port number to connect
 * @param crypt_mode encryption mode
 * @param use_pipelining whether POP3 PIPELINING shall be used if supported by the remote server
 * @return the POP network client object
 */
NetClientPop *net_client_pop_new(const gchar *host, guint16 port, NetClientCryptMode crypt_mode, gboolean use_pipelining);


/** @brief Set allowed POP AUTH methods
 *
 * @param client POP network client object
 * @param encrypted set allowed methods for encrypted or unencrypted connections
 * @param allow_auth mask of allowed authentication methods
 * @return TRUE on success or FALSE on error
 *
 * Set the allowed authentication methods for the passed connection.  The default is @ref NET_CLIENT_POP_AUTH_ALL for both encrypted
 * and unencrypted connections.
 *
 * @note Call this function @em before calling net_client_pop_connect().
 */
gboolean net_client_pop_allow_auth(NetClientPop *client, gboolean encrypted, guint allow_auth);


/** @brief Connect a POP network client
 *
 * @param client POP network client object
 * @param greeting filled with the greeting of the POP server on success, may be NULL to ignore
 * @param error filled with error information if the connection fails
 * @return TRUE on success or FALSE if the connection failed
 *
 * Connect the remote POP server, initialise the encryption if requested, and emit the @ref auth signal to request authentication
 * information.  Simply ignore the signal for an unauthenticated connection.
 *
 * The function will try only @em one authentication method supported by the server and enabled for the current encryption state
 * (see net_client_pop_allow_auth() and \ref NET_CLIENT_POP_AUTH_ALL etc.).  The priority is, from highest to lowest, GSSAPI (if
 * configured), CRAM-SHA1, CRAM-MD5, APOP, PLAIN, USER/PASS or LOGIN.
 *
 * In order to shut down a successfully established connection, just call <tt>g_object_unref()</tt> on the POP network client
 * object.
 *
 * @note The caller must free the returned greeting when it is not needed any more.
 */
gboolean net_client_pop_connect(NetClientPop *client, gchar **greeting, GError **error);


/** @brief Get the status of a POP3 mailbox
 *
 * @param client POP network client object
 * @param msg_count filled with the number of messages available in the mailbox, may be NULL to ignore the value
 * @param mbox_size filled with the total mailbox size in bytes, may be NULL to ignore the value
 * @param error filled with error information if the connection fails
 * @return TRUE on success or FALSE if the command failed
 *
 * Run the POP3 STAT command to retrieve the mailbox status.
 */
gboolean net_client_pop_stat(NetClientPop *client, gsize *msg_count, gsize *mbox_size, GError **error);


/** @brief List the messages in the POP3 mailbox
 *
 * @param client POP network client object
 * @param msg_list filled with a list of @ref NetClientPopMessageInfo items
 * @param with_uid TRUE to include the UID's of the messages in the returned list
 * @param error filled with error information if the connection fails
 * @return TRUE on success or FALSE if the command failed
 *
 * Run the LIST command and fill the passed list with the message identifier and message size for all messages available in the
 * mailbox.  If the parameter @em with_uid is TRUE, also run the UIDL command and include the UID's reported by the remote server in
 * the returned list.
 *
 * The caller shall free the items in the returned list by calling net_client_pop_msg_info_free() on them.
 *
 * @note The UID's can be added only if the remote server reports in its @em CAPABILITY list that the @em UIDL command is supported.
 */
gboolean net_client_pop_list(NetClientPop *client, GList **msg_list, gboolean with_uid, GError **error);


/** @brief Load messages from the POP3 mailbox
 *
 * @param client POP network client object
 * @param msg_list list of @ref NetClientPopMessageInfo items which shall be read from the server
 * @param callback callback function which shall be called to process the downloaded message data
 * @param user_data user data pointer passed to the callback function
 * @param error filled with error information if the connection fails
 * @return TRUE on success or FALSE if the command failed
 *
 * Load all messages in the passed list from the remote server, passing them through the specified callback function.  The function
 * takes advantage of the RFC 2449 @em PIPELINING capability if supported by the remote server.
 */
gboolean net_client_pop_retr(NetClientPop *client, GList *msg_list, NetClientPopMsgCb callback, gpointer user_data, GError **error);


/** @brief Delete messages from the POP3 mailbox
 *
 * @param client POP network client object
 * @param msg_list list of @ref NetClientPopMessageInfo items which shall be deleted from the server
 * @param error filled with error information if the connection fails
 * @return TRUE on success or FALSE if the command failed
 *
 * Delete all messages in the passed list from the remote server.  The function takes advantage of the RFC 2449 @em PIPELINING
 * capability if supported by the remote server.
 */
gboolean net_client_pop_dele(NetClientPop *client, GList *msg_list, GError **error);


/** @brief Free POP3 message item information
 *
 * @param info POP3 message item information as returned by net_client_pop_list()
 *
 * Free the data of a POP3 message item information.
 */
void net_client_pop_msg_info_free(NetClientPopMessageInfo *info);


/** @file
 *
 * This module implements a POP3 client class conforming with <a href="https://tools.ietf.org/html/rfc1939">RFC 1939</a>.
 *
 * The following features are supported:
 * - the <i>STAT</i>, <i>LIST</i>, <i>RETR</i> and <i>DELE</i> commands as defined in RFC 1939;
 * - support for <i>PIPELINING</i> and <i>UIDL</i> as defined by <a href="https://tools.ietf.org/html/rfc2449">RFC 2449</a>;
 * - <i>STLS</i> encryption as defined by <a href="https://tools.ietf.org/html/rfc2595">RFC 2595</a>;
 * - authentication using <i>APOP</i>, <i>USER/PASS</i> (both RFC 1939) or the SASL methods <i>PLAIN</i>, <i>LOGIN</i>,
 *   <i>CRAM-MD5</i>, <i>CRAM-SHA1</i> or <i>GSSAPI</i> (see <a href="https://tools.ietf.org/html/rfc4752">RFC 4752</a> and
 *   <a href="https://tools.ietf.org/html/rfc5034">RFC 5034</a>), depending upon the capabilities reported by the server.  Note the
 *   <i>GSSAPI</i> is available only if configured with gssapi support.
 */


G_END_DECLS


#endif /* NET_CLIENT_POP_H_ */
