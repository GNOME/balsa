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

#include <string.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <gnutls/x509.h>
#include "net-client-utils.h"
#include "net-client.h"


/*
 * Stacking of the streams:
 * Input channel:
 *   GSocketClient *sock
 *      GSocketConnection *plain_conn
 *         GIOStream *tls_conn -- optional
 *            GInputStream *comp_istream -- optional
 *               GDataInputStream *istream
 *
 * Output channel:
 *   GSocketClient *sock
 *      GSocketConnection *plain_conn
 *         GIOStream *tls_conn -- optional
 *            GOutputStream *ostream -- optionally compressed
 */
typedef struct _NetClientPrivate NetClientPrivate;

struct _NetClientPrivate {
	gchar *host_and_port;
	guint16 default_port;
	gsize max_line_len;

	GSocketClient *sock;
	GSocketConnectable *remote_address;
	GSocketConnection *plain_conn;
	GIOStream *tls_conn;
	GDataInputStream *istream;
	GOutputStream *ostream;
	GTlsCertificate *certificate;

	GZlibCompressor *comp;
	GZlibDecompressor *decomp;
	GInputStream *comp_istream;
};


static guint signals[3];


G_DEFINE_TYPE_WITH_PRIVATE(NetClient, net_client, G_TYPE_OBJECT)


static void net_client_finalise(GObject *object);
static gboolean cert_accept_cb(GTlsConnection *conn, GTlsCertificate *peer_cert, GTlsCertificateFlags errors, gpointer user_data);


NetClient *
net_client_new(const gchar *host_and_port, guint16 default_port, gsize max_line_len)
{
	NetClient *client;
	NetClientPrivate *priv;

	g_return_val_if_fail(host_and_port != NULL, NULL);

	client = NET_CLIENT(g_object_new(NET_CLIENT_TYPE, NULL));
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	priv = net_client_get_instance_private(client);

	if (priv->sock == NULL) {
		g_object_unref(client);
		client = NULL;
	} else {
		priv->host_and_port = g_strdup(host_and_port);
		priv->default_port = default_port;
		priv->max_line_len = max_line_len;
	}

	return client;
}


gboolean
net_client_configure(NetClient *client, const gchar *host_and_port, guint16 default_port, gsize max_line_len, GError **error)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result;

	g_return_val_if_fail(NET_IS_CLIENT(client) && (host_and_port != NULL), FALSE);

	if (priv->plain_conn != NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_CONNECTED, _("network client is already connected"));
		result = FALSE;
	} else {
		g_free(priv->host_and_port);
		priv->host_and_port = g_strdup(host_and_port);
		priv->default_port = default_port;
		priv->max_line_len = max_line_len;
		result = TRUE;
	}
	return result;
}


const gchar *
net_client_get_host(NetClient *client)
{
	const gchar *result;

	if (NET_IS_CLIENT(client)) {
		const NetClientPrivate *priv;

		/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
		priv = net_client_get_instance_private(client);
		result = priv->host_and_port;
	} else {
		result = NULL;
	}
	return result;
}


gboolean
net_client_connect(NetClient *client, GError **error)
{
	gboolean result = FALSE;
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(client);

	g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

	if (priv->plain_conn != NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_CONNECTED, _("network client is already connected"));
	} else {
		priv->remote_address = g_network_address_parse(priv->host_and_port, priv->default_port, error);
		if (priv->remote_address != NULL) {
			priv->plain_conn = g_socket_client_connect(priv->sock, priv->remote_address, NULL, error);
			if (priv->plain_conn != NULL) {
				g_debug("connected to %s", priv->host_and_port);
				priv->istream = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(priv->plain_conn)));
				g_data_input_stream_set_newline_type(priv->istream, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
				priv->ostream = g_io_stream_get_output_stream(G_IO_STREAM(priv->plain_conn));
				result = TRUE;
			} else {
				g_object_unref(priv->remote_address);
				priv->remote_address = NULL;
			}
		}
	}

	return result;
}


void
net_client_shutdown(NetClient *client)
{
	if (NET_IS_CLIENT(client)) {
		NetClientPrivate *priv;

		/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
		priv = net_client_get_instance_private(client);

		/* Note: we must unref the GDataInputStream, but the GOutputStream only if compression is active! */
		if (priv->comp != NULL) {
			/* Note: for some strange reason, GIO decides to send a 0x03 0x00 sequence when closing a compressed connection, before
			 * sending the usual FIN, ACK TCP reply packet.  As the remote server does not expect the former (the connection has
			 * already been closed on its side), it replies with with a RST TCP packet.  Unref'ing client->priv->ostream and
			 * client->priv->comp /after/ all other components of the connection fixes the issue for unencrypted connections, but
			 * throws a critical error for TLS.  Observed with gio 2.48.2 and 2.50.3, no idea how it can be fixed.
			 * See also https://bugzilla.gnome.org/show_bug.cgi?id=795985. */
			if (priv->ostream != NULL) {
				g_object_unref(priv->ostream);
			}
			g_object_unref(priv->comp);
		}
		if (priv->decomp != NULL) {
			g_object_unref(priv->decomp);
			priv->decomp = NULL;
		}
		if (priv->comp_istream!= NULL) {
			g_object_unref(priv->comp_istream);
			priv->comp_istream = NULL;
		}
		if (priv->istream != NULL) {
			g_object_unref(priv->istream);
			priv->istream = NULL;
		}
		if (priv->tls_conn != NULL) {
			g_object_unref(priv->tls_conn);
			priv->tls_conn = NULL;
		}
		if (priv->plain_conn != NULL) {
			g_object_unref(priv->plain_conn);
			priv->plain_conn = NULL;
		}
		if (priv->remote_address != NULL) {
			g_object_unref(priv->remote_address);
			priv->remote_address = NULL;
		}
	}
}


gboolean
net_client_is_connected(NetClient *client)
{
	gboolean result;

	if (NET_IS_CLIENT(client)) {
		const NetClientPrivate *priv;

		/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
		priv = net_client_get_instance_private(client);
		result = (priv->plain_conn != NULL);
	} else {
		result = FALSE;
	}

	return result;
}


gboolean
net_client_is_encrypted(NetClient *client)
{
	gboolean result;

	if (net_client_is_connected(client)) {
		const NetClientPrivate *priv;

		/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
		priv = net_client_get_instance_private(client);
		result = (priv->tls_conn != NULL);
	} else {
		result = FALSE;
	}

	return result;
}


gboolean
net_client_read_line(NetClient *client, gchar **recv_line, GError **error)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	const NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result = FALSE;

	g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

	if (priv->istream == NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_NOT_CONNECTED, _("network client is not connected"));
	} else {
		gchar *line_buf;
		gsize length;
		GError *read_err = NULL;

		line_buf = g_data_input_stream_read_line(priv->istream, &length, NULL, &read_err);
		if (line_buf != NULL) {
			/* check that the protocol-specific maximum line length is not exceeded */
			if ((priv->max_line_len > 0U) && (length > priv->max_line_len)) {
				g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_LINE_TOO_LONG,
					_("reply length %lu exceeds the maximum allowed length %lu"),
					(unsigned long) length, (unsigned long) priv->max_line_len);
				g_free(line_buf);
			} else {
				g_debug("[%s] R '%s'", priv->host_and_port, line_buf);
				result = TRUE;
				if (recv_line != NULL) {
					*recv_line = line_buf;
				} else {
					g_free(line_buf);
				}
			}
		} else {
			if (read_err != NULL) {
				g_propagate_error(error, read_err);
			} else {
				g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_CONNECTION_LOST, _("connection lost"));
			}
		}
	}

	return result;
}


gboolean
net_client_write_buffer(NetClient *client, const gchar *buffer, gsize count, GError **error)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	const NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result;

	g_return_val_if_fail(NET_IS_CLIENT(client) && (buffer != NULL) && (count > 0UL), FALSE);

	if (priv->ostream == NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_NOT_CONNECTED, _("network client is not connected"));
		result = FALSE;
	} else {
		gsize bytes_written;

		if ((count >= 2U) && (buffer[count - 1U] == '\n')) {
			g_debug("[%s] W '%.*s'", priv->host_and_port, (int) count - 2, buffer);
		} else {
			g_debug("[%s] W '%.*s'", priv->host_and_port, (int) count, buffer);
		}
		result = g_output_stream_write_all(priv->ostream, buffer, count, &bytes_written, NULL, error);
		if (result) {
			result = g_output_stream_flush(priv->ostream, NULL, error);
		}
	}

	return result;
}


gboolean
net_client_vwrite_line(NetClient *client, const gchar *format, va_list args, GError **error)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	const NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result;
	GString *buffer;

	g_return_val_if_fail(NET_IS_CLIENT(client) && (format != NULL), FALSE);

	buffer = g_string_new(NULL);
	g_string_vprintf(buffer, format, args);
	if ((priv->max_line_len > 0U) && (buffer->len > priv->max_line_len)) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_LINE_TOO_LONG, _("line too long"));
		result = FALSE;
	} else {
		buffer = g_string_append(buffer, "\r\n");
		result = net_client_write_buffer(client, buffer->str, buffer->len, error);
	}
	(void) g_string_free(buffer, TRUE);

	return result;
}


gboolean
net_client_write_line(NetClient *client, const gchar *format, GError **error, ...)
{
	va_list args;
	gboolean result;

	va_start(args, error);
	result = net_client_vwrite_line(client, format, args, error);
	va_end(args);

	return result;
}


gboolean
net_client_execute(NetClient *client, gchar **response, const gchar *request_fmt, GError **error, ...)
{
	va_list args;
	gboolean result;

	va_start(args, error);
	result = net_client_vwrite_line(client, request_fmt, args, error);
	va_end(args);
	if (result) {
		result = net_client_read_line(client, response, error);
	}

	return result;
}


/* Note: I have no idea how I can load a PEM with an encrypted key into a GTlsCertificate using g_tls_certificate_new_from_file()
 * or g_tls_certificate_new_from_pem(), as this will always fail with a GnuTLS error "ASN1 parser: Error in DER parsing".  There
 * /might/ be a solution with GTlsInteraction and GTlsPassword, though.  Please enlighten me if you know more...
 *
 * This function uses "raw" GnuTLS calls and will emit the cert-pass signal if necessary.
 *
 * See also <https://mail.gnome.org/archives/gtk-list/2016-May/msg00027.html>.
 */
gboolean
net_client_set_cert_from_pem(NetClient *client, const gchar *pem_data, GError **error)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result = FALSE;
	gnutls_x509_crt_t cert;
	int res;

	g_return_val_if_fail(NET_IS_CLIENT(client) && (pem_data != NULL), FALSE);

	/* always free any existing certificate */
	if (priv->certificate != NULL) {
		g_object_unref(priv->certificate);
		priv->certificate = NULL;
	}

	/* load the certificate */
	res = gnutls_x509_crt_init(&cert);
	if (res != GNUTLS_E_SUCCESS) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GNUTLS, _("error initializing certificate: %s"),
			gnutls_strerror(res));
	} else {
		gnutls_datum_t data;

		/*lint -e9005	cast'ing away the const is safe as gnutls treats data as const */
		data.data = (unsigned char *) pem_data;
		data.size = strlen(pem_data);
		res = gnutls_x509_crt_import(cert, &data, GNUTLS_X509_FMT_PEM);
		if (res != GNUTLS_E_SUCCESS) {
			g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GNUTLS, _("error loading certificate: %s"),
				gnutls_strerror(res));
		} else {
			gnutls_x509_privkey_t key;

			/* try to load the key, emit cert-pass signal if gnutls says the key is encrypted */
			res = gnutls_x509_privkey_init(&key);
			if (res != GNUTLS_E_SUCCESS) {
				g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GNUTLS, _("error initializing key: %s"),
					gnutls_strerror(res));
			} else {
				res = gnutls_x509_privkey_import2(key, &data, GNUTLS_X509_FMT_PEM, NULL, 0);
				if (res == GNUTLS_E_DECRYPTION_FAILED) {
					size_t dn_size;
					gchar *dn_str;

					/* determine dn string buffer size requirements */
					dn_size = 0U;
					(void) gnutls_x509_crt_get_dn(cert, NULL, &dn_size);
					dn_str = g_malloc0(dn_size + 1U);		/*lint !e9079 (MISRA C:2012 Rule 11.5) */

					res = gnutls_x509_crt_get_dn(cert, dn_str, &dn_size);
					if (res == GNUTLS_E_SUCCESS) {
						gchar *key_pass = NULL;

						if (!g_utf8_validate(dn_str, -1, NULL)) {
							gchar *buf;

							buf = g_locale_to_utf8(dn_str, -1, NULL, NULL, NULL);
							g_free(dn_str);
							dn_str = buf;
						}
						g_debug("[%s] emit 'cert-pass' signal", priv->host_and_port);
						g_signal_emit(client, signals[2], 0, dn_str, &key_pass);
						if (key_pass != NULL) {
							res = gnutls_x509_privkey_import2(key, &data, GNUTLS_X509_FMT_PEM, key_pass, 0);
							net_client_free_authstr(key_pass);
						}
					}
					g_free(dn_str);
				}

				/* on success, set the certificate using the unencrypted key */
				if (res == GNUTLS_E_SUCCESS) {
					gchar *pem_buf;
					size_t key_buf_size = 0;
					size_t crt_buf_size = 0;

					/* determine buffer size requirements */
					(void) gnutls_x509_privkey_export(key, GNUTLS_X509_FMT_PEM, NULL, &key_buf_size);
					(void) gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, NULL, &crt_buf_size);
					pem_buf = g_malloc(key_buf_size + crt_buf_size);		/*lint !e9079 (MISRA C:2012 Rule 11.5) */
					res = gnutls_x509_privkey_export(key, GNUTLS_X509_FMT_PEM, pem_buf, &key_buf_size);
					if (res == GNUTLS_E_SUCCESS) {
						res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, &pem_buf[key_buf_size], &crt_buf_size);
					}

					if (res == GNUTLS_E_SUCCESS) {
						priv->certificate = g_tls_certificate_new_from_pem(pem_buf, -1, error);
						if (priv->certificate != NULL) {
							result = TRUE;
						}
					}
					g_free(pem_buf);
				}
				gnutls_x509_privkey_deinit(key);

				if (res != GNUTLS_E_SUCCESS) {
					g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_CERT_KEY_PASS, _("error loading key: %s"),
						gnutls_strerror(res));
				}
			}
		}
		gnutls_x509_crt_deinit(cert);
	}

	return result;
}


gboolean
net_client_set_cert_from_file(NetClient *client, const gchar *pem_path, GError **error)
{
	gboolean result;
	GFile *pem_file;
	gchar *pem_buf;

	g_return_val_if_fail(pem_path != NULL, FALSE);

	pem_file = g_file_new_for_path(pem_path);
	result = g_file_load_contents(pem_file, NULL, &pem_buf, NULL, NULL, error);
	if (result) {
		result = net_client_set_cert_from_pem(client, pem_buf, error);
		g_free(pem_buf);
	}
	g_object_unref(pem_file);
	return result;
}


gboolean
net_client_start_tls(NetClient *client, GError **error)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result = FALSE;

	g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

	if (priv->plain_conn == NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_NOT_CONNECTED, _("not connected"));
	} else if (priv->tls_conn != NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_TLS_ACTIVE, _("connection is already encrypted"));
	} else {
		priv->tls_conn = g_tls_client_connection_new(G_IO_STREAM(priv->plain_conn), priv->remote_address, error);
		if (priv->tls_conn != NULL) {
			if (priv->certificate != NULL) {
				g_tls_connection_set_certificate(G_TLS_CONNECTION(priv->tls_conn), priv->certificate);
			}
			(void) g_signal_connect(priv->tls_conn, "accept-certificate", G_CALLBACK(cert_accept_cb), client);
			result = g_tls_connection_handshake(G_TLS_CONNECTION(priv->tls_conn), NULL, error);
			if (result) {
				g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(priv->istream), FALSE);
				g_object_unref(priv->istream);		/* unref the plain connection's stream */
				priv->istream = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(priv->tls_conn)));
				g_data_input_stream_set_newline_type(priv->istream, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
				priv->ostream = g_io_stream_get_output_stream(G_IO_STREAM(priv->tls_conn));
				g_debug("[%s] connection is encrypted", priv->host_and_port);
			} else {
				g_object_unref(priv->tls_conn);
				priv->tls_conn = NULL;
			}
		}
	}

	return result;
}


gboolean
net_client_start_compression(NetClient *client, GError **error)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result = FALSE;

	g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

	if (priv->plain_conn == NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_NOT_CONNECTED, _("not connected"));
	} else if (priv->comp != NULL) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_COMP_ACTIVE, _("connection is already compressed"));
	} else {
		priv->comp = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW, -1);
		priv->decomp = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW);

		g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(priv->istream), FALSE);
		g_object_unref(priv->istream);

		if (priv->tls_conn != NULL) {
			priv->comp_istream =
				g_converter_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(priv->tls_conn)),
					G_CONVERTER(priv->decomp));
		} else {
			priv->comp_istream =
				g_converter_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(priv->plain_conn)),
					G_CONVERTER(priv->decomp));
		}
		priv->istream = g_data_input_stream_new(priv->comp_istream);
		g_data_input_stream_set_newline_type(priv->istream, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

		priv->ostream = g_converter_output_stream_new(priv->ostream, G_CONVERTER(priv->comp));
		result = TRUE;
		g_debug("[%s] connection is compressed", priv->host_and_port);
	}

	return result;
}


gboolean
net_client_set_timeout(NetClient *client, guint timeout_secs)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	const NetClientPrivate *priv = net_client_get_instance_private(client);

	g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

	g_socket_client_set_timeout(priv->sock, timeout_secs);
	return TRUE;
}


GSocket *
net_client_get_socket(NetClient *client)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	const NetClientPrivate *priv = net_client_get_instance_private(client);

	g_return_val_if_fail(NET_IS_CLIENT(client) && (priv->plain_conn != NULL), NULL);

	return g_socket_connection_get_socket(priv->plain_conn);
}


gboolean
net_client_can_read(NetClient *client)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	const NetClientPrivate *priv = net_client_get_instance_private(client);

	g_return_val_if_fail(NET_IS_CLIENT(client) && (priv->plain_conn != NULL) && (priv->istream != NULL), FALSE);

	return (g_socket_condition_check(g_socket_connection_get_socket(priv->plain_conn), G_IO_IN) != 0) ||
		(g_buffered_input_stream_get_available(G_BUFFERED_INPUT_STREAM(priv->istream)) > 0U);
}


/* == local functions =========================================================================================================== */

static void
net_client_class_init(NetClientClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	gobject_class->finalize = net_client_finalise;
	signals[0] = g_signal_new("cert-check", NET_CLIENT_TYPE, G_SIGNAL_RUN_LAST, 0U, NULL, NULL, NULL, G_TYPE_BOOLEAN, 2U,
		G_TYPE_TLS_CERTIFICATE, G_TYPE_TLS_CERTIFICATE_FLAGS);
	signals[1] = g_signal_new("auth", NET_CLIENT_TYPE, G_SIGNAL_RUN_LAST, 0U, NULL, NULL, NULL, G_TYPE_STRV, 1U, G_TYPE_INT);
	signals[2] = g_signal_new("cert-pass", NET_CLIENT_TYPE, G_SIGNAL_RUN_LAST, 0U, NULL, NULL, NULL, G_TYPE_STRING, 1U,
		G_TYPE_STRING);
}


static void
net_client_init(NetClient *self)
{
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(self);

	priv->sock = g_socket_client_new();
	if (priv->sock != NULL) {
		g_socket_client_set_timeout(priv->sock, 180U);
	}
}


static void
net_client_finalise(GObject *object)
{
	NetClient *client = NET_CLIENT(object);
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(client);
	const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_parent_class);

	net_client_shutdown(client);
	if (priv->sock != NULL) {
		g_object_unref(priv->sock);
		priv->sock = NULL;
	}
	if (priv->certificate != NULL) {
		g_object_unref(priv->certificate);
		priv->certificate = NULL;
	}
	g_debug("finalised connection to %s", priv->host_and_port);
	g_free(priv->host_and_port);
	(*parent_class->finalize)(object);
}


/*lint -e{715,818} */
static gboolean
cert_accept_cb(G_GNUC_UNUSED GTlsConnection *conn, GTlsCertificate *peer_cert, GTlsCertificateFlags errors, gpointer user_data)
{
	NetClient *client = NET_CLIENT(user_data);
	/*lint -e{9079}		(MISRA C:2012 Rule 11.5) intended use of this function */
	NetClientPrivate *priv = net_client_get_instance_private(client);
	gboolean result;

	g_debug("[%s] emit 'cert-check' signal", priv->host_and_port);
	g_signal_emit(client, signals[0], 0, peer_cert, errors, &result);
	return result;
}
