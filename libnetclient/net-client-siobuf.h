/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2018 - 2020
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

#ifndef NET_CLIENT_SIOBUF_H_
#define NET_CLIENT_SIOBUF_H_


#include "net-client.h"


G_BEGIN_DECLS


#define NET_CLIENT_SIOBUF_TYPE				(net_client_siobuf_get_type())
G_DECLARE_FINAL_TYPE(NetClientSioBuf, net_client_siobuf, NET, CLIENT_SIOBUF, NetClient)


#define NET_CLIENT_SIOBUF_ERROR_QUARK		(g_quark_from_static_string("net-client-siobuf"))


/** @brief Create a new SIOBUF network client
 *
 * @param host host name or IP address to connect
 * @param port port number to connect
 * @return the SIOBUF network client object
 */
NetClientSioBuf *net_client_siobuf_new(const gchar *host, guint16 port);


/** @brief Read a number of bytes from a SIOBUF network client object
 *
 * @param client SIOBUF network client object
 * @param buffer destination buffer
 * @param count number of bytes which shall be read
 * @param error filled with error information on error
 * @return the number of bytes actually read, or -1 if nothing could be read
 *
 * Read a number of bytes, including the CRLF line terminations if applicable, from the remote server.  Note that the error location
 * may be filled on a short read (i. e. when the number of bytes read is smaller than the requested count).
 */
gint net_client_siobuf_read(NetClientSioBuf *client, void *buffer, gsize count, GError **error);


/** @brief Read a character from a SIOBUF network client object
 *
 * @param client SIOBUF network client object
 * @param error filled with error information on error
 * @return the next character, or -1 if reading more data from the remote server failed
 *
 * Read the next character from the remote server.  This includes the terminating CR and LF characters of each line.
 */
gint net_client_siobuf_getc(NetClientSioBuf *client, GError **error);


/** @brief Read a character from a SIOBUF network client object
 *
 * @param client SIOBUF network client object
 * @return 0 on success, or ä1 on error
 *
 * Put back the last character read from the remote server.  The function fails if no data is available or if the start of the
 * internal buffer has been reached.
 */
gint net_client_siobuf_ungetc(NetClientSioBuf *client);


/** @brief Read a buffer from a SIOBUF network client object
 *
 * @param client SIOBUF network client object
 * @param buffer destination buffer
 * @param buflen number of characters which shall be read
 * @param error filled with error information on error
 * @return the passed buffer on success, or NULL on error
 *
 * Fill the passed buffer with data from the remote server until either the end of the line is reached, or the buffer is full.  The
 * CRLF termination sequence is included in the buffer.  The buffer is always NUL-terminated.
 */
gchar *net_client_siobuf_gets(NetClientSioBuf *client, gchar *buffer, gsize buflen, GError **error);


/** @brief Read a line from a SIOBUF network client object
 *
 * @param client SIOBUF network client object
 * @param error filled with error information on error
 * @return a line of data, excluding the terminating CRLF on success, or NULL on error
 *
 * Return a newly allocated buffer, containing data from the remote server, but excluding the terminating CRLF sequence.  If the
 * read buffer contains only 1 or 2 characters (i. e. the terminating CRLF or only LF), the function returns an empty string.  The
 * internal read buffer is empty after calling this function.
 *
 * @note The caller must free the returned buffer when it is not needed any more.
 */
gchar *net_client_siobuf_get_line(NetClientSioBuf *client, GError **error)
	G_GNUC_WARN_UNUSED_RESULT;


/** @brief Empty the SIOBUF network client object read buffer
 *
 * @param client SIOBUF network client object
 * @param error filled with error information on error
 * @return '\n' on success, or -1 on error
 *
 * Discard all pending data in the read buffer.  If the read buffer is empty, the function reads the next line and discards it.
 */
gint net_client_siobuf_discard_line(NetClientSioBuf *client, GError **error);


/** @brief Write data to the SIOBUF output buffer
 *
 * @param client SIOBUF network client object
 * @param buffer data buffer which shall be written
 * @param count number of bytes which shall be written
 *
 * Append the the passed data to the client's internal write buffer.  Call net_client_siobuf_flush() to actually send the data to
 * the remote server.
 */
void net_client_siobuf_write(NetClientSioBuf *client, const void *buffer, gsize count);


/** @brief Print to the SIOBUF output buffer
 *
 * @param client SIOBUF network client object
 * @param format printf-like format string
 * @param ... additional arguments according to the format string
 *
 * Format a string according to the passed arguments, and append the the passed data to the client's internal write buffer.  Call
 * net_client_siobuf_flush() to actually send the data to the remote server.
 */
void net_client_siobuf_printf(NetClientSioBuf *client, const gchar *format, ...)
	G_GNUC_PRINTF(2, 3);


/** @brief Send buffered SIOBUF output data
 *
 * @param client SIOBUF network client object
 * @param error filled with error information on error
 * @return TRUE is the send operation was successful, FALSE on error
 *
 * Transmit the client's internal write buffer, followed by a CRLF sequence, to the remote server and clear it.
 */
gboolean net_client_siobuf_flush(NetClientSioBuf *client, GError **error);


/** @file
 *
 * This module implements a glue layer client class for Balsa's imap implementation.  In addition to the base class, it implements
 * an internal input line buffer which provides reading single characters and lines, reading an exact amount of bytes, and buffered
 * write operations.
 */

#endif /* NET_CLIENT_SIOBUF_H_ */
