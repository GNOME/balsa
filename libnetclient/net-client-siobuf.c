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

#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include "net-client-utils.h"
#include "net-client-siobuf.h"


/*lint -esym(754,_NetClientSioBuf::parent)	required field, not referenced directly */
struct _NetClientSioBuf {
    NetClient parent;

	GString *buffer;		/**< hold a line read from the remote server, including the terminating CRLF */
	gchar *read_ptr;		/**< point to the next char which shall be read in buffer's GString::str. */
	GString *writebuf;		/**< buffer for buffered write functions */
};


/*lint -esym(528,net_client_siobuf_get_instance_private)		auto-generated function, not referenced */
G_DEFINE_TYPE(NetClientSioBuf, net_client_siobuf, NET_CLIENT_TYPE)


static void net_client_siobuf_finalise(GObject *object);
static gboolean net_client_siobuf_fill(NetClientSioBuf *client, GError **error);


NetClientSioBuf *
net_client_siobuf_new(const gchar *host, guint16 port)
{
	NetClientSioBuf *client;

	g_return_val_if_fail(host != NULL, NULL);

	client = NET_CLIENT_SIOBUF(g_object_new(NET_CLIENT_SIOBUF_TYPE, NULL));
	if (!net_client_configure(NET_CLIENT(client), host, port, 0, NULL)) {
		g_assert_not_reached();
	}
	client->buffer = g_string_sized_new(1024U);
	client->read_ptr = NULL;
	client->writebuf = g_string_sized_new(1024U);

	return client;
}


gint
net_client_siobuf_read(NetClientSioBuf *client, void *buffer, gsize count, GError **error)
{
	gboolean fill_res;
	gchar *dest;
	gsize left;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client) && (buffer != NULL) && (count > 0U), -1);

	dest = (gchar *) buffer;	/*lint !e9079	sane pointer conversion (MISRA C:2012 Rule 11.5) */
	left = count;
	fill_res = net_client_siobuf_fill(client, error);
	while (fill_res && (left > 0U)) {
		gsize avail;
		gsize chunk;

		/*lint -e{737,946,947,9029}		allowed exception according to MISRA C:2012 Rules 18.2, 18.3 */
		avail = client->buffer->len - (client->read_ptr - client->buffer->str);
		if (avail > left) {
			chunk = left;
		} else {
			chunk = avail;
		}

		memcpy(dest, client->read_ptr, chunk);
		dest += chunk;
		client->read_ptr += chunk;
		left -= chunk;
		if (left > 0U) {
			fill_res = net_client_siobuf_fill(client, error);
		}
	}

	return (left < count) ? ((gint) count - (gint) left) : -1;
}


gint
net_client_siobuf_getc(NetClientSioBuf *client, GError **error)
{
	gint retval;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), -1);

	if (net_client_siobuf_fill(client, error)) {
		retval = (gint) *client->read_ptr++;
	} else {
		retval = -1;
	}
	return retval;
}


gint
net_client_siobuf_ungetc(NetClientSioBuf *client)
{
	gint retval;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), -1);

	/*lint -e{946}		allowed exception according to MISRA C:2012 Rules 18.2 and 18.3 */
	if ((client->buffer->len != 0U) && (client->read_ptr > client->buffer->str)) {
		client->read_ptr--;
		retval = 0;
	} else {
		retval = -1;
	}
	return retval;
}


gchar *
net_client_siobuf_gets(NetClientSioBuf *client, gchar *buffer, gsize buflen, GError **error)
{
	gchar *result;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client) && (buffer != NULL) && (buflen > 0U), NULL);

	if (net_client_siobuf_fill(client, error)) {
		gsize avail;
		gsize chunk;

		/*lint -e{737,946,947,9029}		allowed exception according to MISRA C:2012 Rules 18.2 and 18.3 */
		avail = client->buffer->len - (client->read_ptr - client->buffer->str);
		if (avail > (buflen - 1U)) {
			chunk = buflen - 1U;
		} else {
			chunk = avail;
		}
		memcpy(buffer, client->read_ptr, chunk);
		client->read_ptr += chunk;
		buffer[chunk] = '\0';
		result = buffer;
	} else {
		result = NULL;
	}

	return result;
}


gchar *
net_client_siobuf_get_line(NetClientSioBuf *client, GError **error)
{
	gchar *result;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), NULL);

	if (net_client_siobuf_fill(client, error)) {
		gsize avail;

		/*lint -e{737,946,947,9029}		allowed exception according to MISRA C:2012 Rules 18.2 and 18.3 */
		avail = client->buffer->len - (client->read_ptr - client->buffer->str);
		if (avail > 2U) {
			result = g_strndup(client->read_ptr, avail - 2U);
		} else {
			result = g_strdup("");
		}
		client->buffer->len = 0U;
	} else {
		result = NULL;
	}

	return result;
}


gint
net_client_siobuf_discard_line(NetClientSioBuf *client, GError **error)
{
	gint result;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), -1);

	if (net_client_siobuf_fill(client, error)) {
		client->buffer->len = 0U;
		result = (gint) '\n';
	} else {
		result = -1;
	}

	return result;
}


void
net_client_siobuf_write(NetClientSioBuf *client, const void *buffer, gsize count)
{
	g_return_if_fail(NET_IS_CLIENT_SIOBUF(client) && (buffer != NULL) && (count > 0U));

	/*lint -e{9079}		sane pointer conversion (MISRA C:2012 Rule 11.5) */
	(void) g_string_append_len(client->writebuf, (const gchar *) buffer, (gssize) count);
}


void
net_client_siobuf_printf(NetClientSioBuf *client, const gchar *format, ...)
{
	va_list args;

	g_return_if_fail(NET_IS_CLIENT_SIOBUF(client) && (format != NULL));

	va_start(args, format);
	g_string_append_vprintf(client->writebuf, format, args);
	va_end(args);
}


gboolean
net_client_siobuf_flush(NetClientSioBuf *client, GError **error)
{
	gboolean result;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), FALSE);

	if (client->writebuf->len > 0U) {
		(void) g_string_append(client->writebuf, "\r\n");
		result = net_client_write_buffer(NET_CLIENT(client), client->writebuf->str, client->writebuf->len, error);
		(void) g_string_truncate(client->writebuf, 0U);
	} else {
		result = FALSE;
	}

	return result;
}


/* == local functions =========================================================================================================== */

static void
net_client_siobuf_class_init(NetClientSioBufClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	gobject_class->finalize = net_client_siobuf_finalise;
}


/*lint -e{715,818} */
static void
net_client_siobuf_init(G_GNUC_UNUSED NetClientSioBuf *self)
{
}


static gboolean
net_client_siobuf_fill(NetClientSioBuf *client, GError **error)
{
	gboolean result;

	if ((client->buffer->len == 0U) || (client->read_ptr == NULL) || (*client->read_ptr == '\0')) {
		gchar *read_buf;

		result = net_client_read_line(NET_CLIENT(client), &read_buf, error);
		if (result) {
			(void) g_string_assign(client->buffer, read_buf);
			(void) g_string_append(client->buffer, "\r\n");
			client->read_ptr = client->buffer->str;
			g_free(read_buf);
		}
	} else {
		result = TRUE;
	}

	return result;
}


static void
net_client_siobuf_finalise(GObject *object)
{
	const NetClientSioBuf *client = NET_CLIENT_SIOBUF(object);
	const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_siobuf_parent_class);

	(void) g_string_free(client->buffer, TRUE);
	(void) g_string_free(client->writebuf, TRUE);
	(*parent_class->finalize)(object);
}
