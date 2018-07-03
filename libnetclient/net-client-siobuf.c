/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2018
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

#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include "net-client-utils.h"
#include "net-client-siobuf.h"


struct _NetClientSioBufPrivate {
	GString *buffer;		/**< hold a line read from the remote server, including the terminating CRLF */
	gchar *read_ptr;		/**< point to the next char which shall be read in buffer's GString::str. */
	GString *writebuf;		/**< buffer for buffered write functions */
};


G_DEFINE_TYPE_WITH_PRIVATE(NetClientSioBuf, net_client_siobuf, NET_CLIENT_TYPE)


static void net_client_siobuf_finalise(GObject *object);
static gboolean net_client_siobuf_fill(NetClientSioBuf *client, GError **error);


NetClientSioBuf *
net_client_siobuf_new(const gchar *host, guint16 port)
{
	NetClientSioBuf *client;

	g_return_val_if_fail(host != NULL, NULL);

	client = NET_CLIENT_SIOBUF(g_object_new(NET_CLIENT_SIOBUF_TYPE, NULL));
	if (client != NULL) {
		if (!net_client_configure(NET_CLIENT(client), host, port, 0, NULL)) {
			g_object_unref(G_OBJECT(client));
			client = NULL;
		} else {
			client->priv->buffer = g_string_sized_new(1024U);
			client->priv->read_ptr = NULL;
			client->priv->writebuf = g_string_sized_new(1024U);
		}
	}

	return client;
}


gint
net_client_siobuf_read(NetClientSioBuf *client, void *buffer, gsize count, GError **error)
{
	NetClientSioBufPrivate *priv;
	gboolean fill_res;
	gchar *dest;
	gsize left;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client) && (buffer != NULL) && (count > 0U), -1);

	priv = client->priv;
	dest = (gchar *) buffer;
	left = count;
	fill_res = net_client_siobuf_fill(client, error);
	while (fill_res && (left > 0U)) {
		gsize avail;
		gsize chunk;

		avail = priv->buffer->len - (priv->read_ptr - priv->buffer->str);
		if (avail > left) {
			chunk = left;
		} else {
			chunk = avail;
		}

		memcpy(dest, priv->read_ptr, chunk);
		dest += chunk;
		priv->read_ptr += chunk;
		left -= chunk;
		if (left > 0U) {
			fill_res = net_client_siobuf_fill(client, error);
		}
	}

	return (left < count) ? (gint) (count - left) : -1;
}


gint
net_client_siobuf_getc(NetClientSioBuf *client, GError **error)
{
	gint retval;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), -1);

	if (net_client_siobuf_fill(client, error)) {
		retval = *client->priv->read_ptr++;
	} else {
		retval = -1;
	}
	return retval;
}


gint
net_client_siobuf_ungetc(NetClientSioBuf *client)
{
	NetClientSioBufPrivate *priv;
	gint retval;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), -1);

	priv = client->priv;
	if ((priv->buffer->len != 0U) && (priv->read_ptr > priv->buffer->str)) {
		priv->read_ptr--;
		retval = 0;
	} else {
		retval = -1;
	}
	return retval;
}


gchar *
net_client_siobuf_gets(NetClientSioBuf *client, gchar *buffer, gsize buflen, GError **error)
{
	NetClientSioBufPrivate *priv;
	gchar *result;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client) && (buffer != NULL) && (buflen > 0U), NULL);

	priv = client->priv;
	if (net_client_siobuf_fill(client, error)) {
		gsize avail;
		gsize chunk;

		avail = priv->buffer->len - (priv->read_ptr - priv->buffer->str);
		if (avail > (buflen - 1U)) {
			chunk = buflen - 1U;
		} else {
			chunk = avail;
		}
		memcpy(buffer, priv->read_ptr, chunk);
		priv->read_ptr += chunk;
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
	NetClientSioBufPrivate *priv;
	gchar *result;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), NULL);

	priv = client->priv;
	if (net_client_siobuf_fill(client, error)) {
		gsize avail;

		avail = priv->buffer->len - (priv->read_ptr - priv->buffer->str);
		if (avail > 2U) {
			result = g_strndup(priv->read_ptr, avail - 2U);
		} else {
			result = g_strdup("");
		}
		priv->buffer->len = 0U;
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
		client->priv->buffer->len = 0U;
		result = '\n';
	} else {
		result = -1;
	}

	return result;
}


void
net_client_siobuf_write(NetClientSioBuf *client, const void *buffer, gsize count)
{
	g_return_if_fail(NET_IS_CLIENT_SIOBUF(client) && (buffer != NULL) && (count > 0U));

	g_string_append_len(client->priv->writebuf, (const gchar *) buffer, count);
}


void
net_client_siobuf_printf(NetClientSioBuf *client, const gchar *format, ...)
{
	va_list args;

	g_return_if_fail(NET_IS_CLIENT_SIOBUF(client) && (format != NULL));

	va_start(args, format);
	g_string_append_vprintf(client->priv->writebuf, format, args);
	va_end(args);
}


gboolean
net_client_siobuf_flush(NetClientSioBuf *client, GError **error)
{
	NetClientSioBufPrivate *priv;
	gboolean result;

	g_return_val_if_fail(NET_IS_CLIENT_SIOBUF(client), FALSE);

	priv = client->priv;
	if (priv->writebuf->len > 0U) {
		g_string_append(priv->writebuf, "\r\n");
		result = net_client_write_buffer(NET_CLIENT(client), priv->writebuf->str, priv->writebuf->len, error);
		g_string_truncate(priv->writebuf, 0U);
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


static void
net_client_siobuf_init(NetClientSioBuf *self)
{
	self->priv = net_client_siobuf_get_instance_private(self);
}


static gboolean
net_client_siobuf_fill(NetClientSioBuf *client, GError **error)
{
	NetClientSioBufPrivate *priv = client->priv;
	gboolean result;

	if ((priv->buffer->len == 0U) || (priv->read_ptr == NULL) || (*priv->read_ptr == '\0')) {
		gchar *read_buf;

		result = net_client_read_line(NET_CLIENT(client), &read_buf, error);
		if (result) {
			g_string_assign(priv->buffer, read_buf);
			g_string_append(priv->buffer, "\r\n");
			priv->read_ptr = priv->buffer->str;
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

	g_string_free(client->priv->buffer, TRUE);
	g_string_free(client->priv->writebuf, TRUE);
	(*parent_class->finalize)(object);
}
