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

#include <string.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include <gnutls/x509.h>
#include "net-client.h"


struct _NetClientPrivate {
    gchar *host_and_port;
    guint16 default_port;
    gsize max_line_len;

    GSocketClient *sock;
    GSocketConnection *plain_conn;
    GIOStream *tls_conn;
    GDataInputStream *istream;
    GOutputStream *ostream;
    GTlsCertificate *certificate;
};


static guint signals[3];


G_DEFINE_TYPE(NetClient, net_client, G_TYPE_OBJECT)


static void net_client_dispose(GObject *object);
static void net_client_finalise(GObject *object);
static gboolean cert_accept_cb(GTlsConnection      *conn,
                               GTlsCertificate     *peer_cert,
                               GTlsCertificateFlags errors,
                               gpointer             user_data);


NetClient *
net_client_new(const gchar *host_and_port,
               guint16      default_port,
               gsize        max_line_len)
{
    NetClient *client;

    g_return_val_if_fail(host_and_port != NULL, NULL);

    client = NET_CLIENT(g_object_new(NET_CLIENT_TYPE, NULL));

    if (client->priv->sock == NULL) {
        g_object_unref(G_OBJECT(client));
        client = NULL;
    } else {
        client->priv->host_and_port = g_strdup(host_and_port);
        client->priv->default_port = default_port;
        client->priv->max_line_len = max_line_len;
    }

    return client;
}


gboolean
net_client_configure(NetClient   *client,
                     const gchar *host_and_port,
                     guint16      default_port,
                     gsize        max_line_len,
                     GError     **error)
{
    NetClientPrivate *priv;
    gboolean result;

    g_return_val_if_fail(NET_IS_CLIENT(client) && (host_and_port != NULL), FALSE);

    priv = client->priv;
    if (priv->plain_conn != NULL) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_CONNECTED,
                    _("network client is already connected"));
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
net_client_get_host(const NetClient *client)
{
    const gchar *result;

    /*lint -e{9005}		cast'ing away const in the next statement is fine */
    if (NET_IS_CLIENT(client)) {
        result = client->priv->host_and_port;
    } else {
        result = NULL;
    }
    return result;
}


gboolean
net_client_connect(NetClient *client,
                   GError   **error)
{
    gboolean result = FALSE;
    NetClientPrivate *priv;

    g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

    priv = client->priv;
    if (priv->plain_conn != NULL) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_CONNECTED,
                    _("network client is already connected"));
    } else {
        priv->plain_conn = g_socket_client_connect_to_host(priv->sock,
                                                           priv->host_and_port,
                                                           priv->default_port,
                                                           NULL,
                                                           error);
        if (priv->plain_conn != NULL) {
            g_debug("connected to %s", priv->host_and_port);
            priv->istream =
                g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(priv->
                                                                                 plain_conn)));
            g_data_input_stream_set_newline_type(priv->istream,
                                                 G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
            priv->ostream = g_io_stream_get_output_stream(G_IO_STREAM(priv->plain_conn));
            result = TRUE;
        }
    }

    return result;
}


void
net_client_shutdown(const NetClient *client)
{
    if (NET_IS_CLIENT(client)) {
        /* note: we must unref the GDataInputStream, but *not* the GOutputStream! */
        if (client->priv->istream != NULL) {
            g_object_unref(G_OBJECT(client->priv->istream));
            client->priv->istream = NULL;
        }
        if (client->priv->tls_conn != NULL) {
            g_object_unref(G_OBJECT(client->priv->tls_conn));
            client->priv->tls_conn = NULL;
        }
        if (client->priv->plain_conn != NULL) {
            g_object_unref(G_OBJECT(client->priv->plain_conn));
            client->priv->plain_conn = NULL;
        }
    }
}


gboolean
net_client_is_connected(NetClient *client)
{
    gboolean result;

    if (NET_IS_CLIENT(client) && (client->priv->plain_conn != NULL)) {
        result = TRUE;
    } else {
        result = FALSE;
    }

    return result;
}


gboolean
net_client_is_encrypted(NetClient *client)
{
    gboolean result;

    if (net_client_is_connected(client) && (client->priv->tls_conn != NULL)) {
        result = TRUE;
    } else {
        result = FALSE;
    }

    return result;
}


gboolean
net_client_read_line(NetClient *client,
                     gchar    **recv_line,
                     GError   **error)
{
    gboolean result = FALSE;

    g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

    if (client->priv->istream == NULL) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_NOT_CONNECTED,
                    _("network client is not connected"));
    } else {
        gchar *line_buf;
        gsize length;
        GError *read_err = NULL;

        line_buf =
            g_data_input_stream_read_line(client->priv->istream, &length, NULL, &read_err);
        if (line_buf != NULL) {
            /* check that the protocol-specific maximum line length is not exceeded */
            if ((client->priv->max_line_len > 0U) && (length > client->priv->max_line_len)) {
                g_set_error(error,
                            NET_CLIENT_ERROR_QUARK,
                            (gint) NET_CLIENT_ERROR_LINE_TOO_LONG,
                            _("reply length %lu exceeds the maximum allowed length %lu"),
                            (unsigned long) length,
                            (unsigned long) client->priv->max_line_len);
                g_free(line_buf);
            } else {
                g_debug("R '%s'", line_buf);
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
                g_set_error(error,
                            NET_CLIENT_ERROR_QUARK,
                            (gint) NET_CLIENT_ERROR_CONNECTION_LOST,
                            _("connection lost"));
            }
        }
    }

    return result;
}


gboolean
net_client_write_buffer(NetClient   *client,
                        const gchar *buffer,
                        gsize        count,
                        GError     **error)
{
    gboolean result;

    g_return_val_if_fail(NET_IS_CLIENT(client) && (buffer != NULL) && (count > 0UL), FALSE);

    if (client->priv->ostream == NULL) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_NOT_CONNECTED,
                    _("network client is not connected"));
        result = FALSE;
    } else {
        gsize bytes_written;

        if ((count >= 2U) && (buffer[count - 1U] == '\n')) {
            g_debug("W '%.*s'", (int) count - 2, buffer);
        } else {
            g_debug("W '%.*s'", (int) count, buffer);
        }
        result = g_output_stream_write_all(client->priv->ostream,
                                           buffer,
                                           count,
                                           &bytes_written,
                                           NULL,
                                           error);
        if (result) {
            result = g_output_stream_flush(client->priv->ostream, NULL, error);
        }
    }

    return result;
}


gboolean
net_client_vwrite_line(NetClient   *client,
                       const gchar *format,
                       va_list      args,
                       GError     **error)
{
    gboolean result;
    GString *buffer;

    g_return_val_if_fail(NET_IS_CLIENT(client) && (format != NULL), FALSE);

    buffer = g_string_new(NULL);
    g_string_vprintf(buffer, format, args);
    if ((client->priv->max_line_len > 0U) && (buffer->len > client->priv->max_line_len)) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_LINE_TOO_LONG,
                    _("line too long"));
        result = FALSE;
    } else {
        buffer = g_string_append(buffer, "\r\n");
        result = net_client_write_buffer(client, buffer->str, buffer->len, error);
    }
    (void) g_string_free(buffer, TRUE);

    return result;
}


gboolean
net_client_write_line(NetClient   *client,
                      const gchar *format,
                      GError     **error,
                      ...)
{
    va_list args;
    gboolean result;

    va_start(args, error);
    result = net_client_vwrite_line(client, format, args, error);
    va_end(args);

    return result;
}


gboolean
net_client_execute(NetClient   *client,
                   gchar      **response,
                   const gchar *request_fmt,
                   GError     **error,
                   ...)
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


/* Note: I have no idea how I can load a PEM with an encrypted key into a GTlsCertificate using
   g_tls_certificate_new_from_file()
 * or g_tls_certificate_new_from_pem(), as this will always fail with a GnuTLS error "ASN1
 * parser: Error in DER parsing".  There
 * /might/ be a solution with GTlsInteraction and GTlsPassword, though.  Please enlighten me if
 * you know more...
 *
 * This function uses "raw" GnuTLS calls and will emit the cert-pass signal if necessary.
 *
 * See also <https://mail.gnome.org/archives/gtk-list/2016-May/msg00027.html>.
 */
gboolean
net_client_set_cert_from_pem(NetClient   *client,
                             const gchar *pem_data,
                             GError     **error)
{
    gboolean result = FALSE;
    gnutls_x509_crt_t cert;
    int res;

    g_return_val_if_fail(NET_IS_CLIENT(client) && (pem_data != NULL), FALSE);

    /* always free any existing certificate */
    if (client->priv->certificate != NULL) {
        g_object_unref(G_OBJECT(client->priv->certificate));
        client->priv->certificate = NULL;
    }

    /* load the certificate */
    res = gnutls_x509_crt_init(&cert);
    if (res != GNUTLS_E_SUCCESS) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GNUTLS,
                    _("error initializing certificate: %s"),
                    gnutls_strerror(res));
    } else {
        gnutls_datum_t data;

        /*lint -e9005	cast'ing away the const is safe as gnutls treats data as const */
        data.data = (unsigned char *) pem_data;
        data.size = strlen(pem_data);
        res = gnutls_x509_crt_import(cert, &data, GNUTLS_X509_FMT_PEM);
        if (res != GNUTLS_E_SUCCESS) {
            g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GNUTLS,
                        _("error loading certificate: %s"),
                        gnutls_strerror(res));
        } else {
            gnutls_x509_privkey_t key;

            /* try to load the key, emit cert-pass signal if gnutls says the key is encrypted */
            res = gnutls_x509_privkey_init(&key);
            if (res != GNUTLS_E_SUCCESS) {
                g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_GNUTLS,
                            _("error initializing key: %s"),
                            gnutls_strerror(res));
            } else {
                res = gnutls_x509_privkey_import2(key, &data, GNUTLS_X509_FMT_PEM, NULL, 0);
                if (res == GNUTLS_E_DECRYPTION_FAILED) {
                    size_t der_size;
                    guint8 *der_data;

                    /* determine cert buffer size requirements */
                    der_size = 0U;
                    (void) gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_DER, NULL, &der_size);
                    der_data = g_malloc(der_size);                              /*lint !e9079
                                                                                   (MISRA C:2012
                                                                                   Rule 11.5) */

                    res =
                        gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_DER, der_data, &der_size);
                    if (res == GNUTLS_E_SUCCESS) {
                        GByteArray *cert_der;
                        gchar *key_pass = NULL;

                        cert_der = g_byte_array_new_take(der_data, der_size);
                        g_debug("emit 'cert-pass' signal for client %p", client);
                        g_signal_emit(client, signals[2], 0, cert_der, &key_pass);
                        g_byte_array_unref(cert_der);
                        if (key_pass != NULL) {
                            res = gnutls_x509_privkey_import2(key,
                                                              &data,
                                                              GNUTLS_X509_FMT_PEM,
                                                              key_pass,
                                                              0);
                            memset(key_pass, 0, strlen(key_pass));
                            g_free(key_pass);
                        }
                    }
                }

                /* on success, set the certificate using the unencrypted key */
                if (res == GNUTLS_E_SUCCESS) {
                    gchar *pem_buf;
                    size_t key_buf_size = 0;
                    size_t crt_buf_size = 0;

                    /* determine buffer size requirements */
                    (void) gnutls_x509_privkey_export(key,
                                                      GNUTLS_X509_FMT_PEM,
                                                      NULL,
                                                      &key_buf_size);
                    (void) gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, NULL,
                                                  &crt_buf_size);
                    pem_buf = g_malloc(key_buf_size + crt_buf_size);                                    /*lint
                                                                                                           !e9079
                                                                                                           (MISRA
                                                                                                           C:2012
                                                                                                           Rule
                                                                                                           11.5)
                                                                                                         */
                    res = gnutls_x509_privkey_export(key,
                                                     GNUTLS_X509_FMT_PEM,
                                                     pem_buf,
                                                     &key_buf_size);
                    if (res == GNUTLS_E_SUCCESS) {
                        res =
                            gnutls_x509_crt_export(cert,
                                                   GNUTLS_X509_FMT_PEM,
                                                   &pem_buf[key_buf_size],
                                                   &crt_buf_size);
                    }

                    if (res == GNUTLS_E_SUCCESS) {
                        client->priv->certificate = g_tls_certificate_new_from_pem(pem_buf,
                                                                                   -1,
                                                                                   error);
                        if (client->priv->certificate != NULL) {
                            result = TRUE;
                        }
                    }
                    g_free(pem_buf);
                }
                gnutls_x509_privkey_deinit(key);

                if (res != GNUTLS_E_SUCCESS) {
                    g_set_error(error,
                                NET_CLIENT_ERROR_QUARK,
                                (gint) NET_CLIENT_ERROR_GNUTLS,
                                _("error loading key: %s"),
                                gnutls_strerror(res));
                }
            }
        }
        gnutls_x509_crt_deinit(cert);
    }

    return result;
}


gboolean
net_client_set_cert_from_file(NetClient   *client,
                              const gchar *pem_path,
                              GError     **error)
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
    g_object_unref(G_OBJECT(pem_file));
    return result;
}


gboolean
net_client_start_tls(NetClient *client,
                     GError   **error)
{
    gboolean result = FALSE;

    g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

    if (client->priv->plain_conn == NULL) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_NOT_CONNECTED,
                    _("not connected"));
    } else if (client->priv->tls_conn != NULL) {
        g_set_error(error, NET_CLIENT_ERROR_QUARK, (gint) NET_CLIENT_ERROR_TLS_ACTIVE,
                    _("connection is already encrypted"));
    } else {
        client->priv->tls_conn =
            g_tls_client_connection_new(G_IO_STREAM(client->priv->plain_conn), NULL, error);
        if (client->priv->tls_conn != NULL) {
            if (client->priv->certificate != NULL) {
                g_tls_connection_set_certificate(G_TLS_CONNECTION(
                                                     client->priv->tls_conn),
                                                 client->priv->certificate);
            }
            (void) g_signal_connect(G_OBJECT(
                                        client->priv->tls_conn), "accept-certificate", G_CALLBACK(
                                        cert_accept_cb), client);
            result = g_tls_connection_handshake(G_TLS_CONNECTION(
                                                    client->priv->tls_conn), NULL, error);
            if (result) {
                g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(client->priv->
                                                                                  istream),
                                                            FALSE);
                g_object_unref(G_OBJECT(client->priv->istream));                                /*
                                                                                                   unref
                                                                                                   the
                                                                                                   plain
                                                                                                   connection's
                                                                                                   stream
                                                                                                 */
                client->priv->istream =
                    g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(client->
                                                                                     priv->
                                                                                     tls_conn)));
                g_data_input_stream_set_newline_type(client->priv->istream,
                                                     G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
                client->priv->ostream =
                    g_io_stream_get_output_stream(G_IO_STREAM(client->priv->tls_conn));
                g_debug("connection is encrypted");
            } else {
                g_object_unref(G_OBJECT(client->priv->tls_conn));
                client->priv->tls_conn = NULL;
            }
        }
    }

    return result;
}


gboolean
net_client_set_timeout(NetClient *client,
                       guint      timeout_secs)
{
    g_return_val_if_fail(NET_IS_CLIENT(client), FALSE);

    g_socket_client_set_timeout(client->priv->sock, timeout_secs);
    return TRUE;
}


/* == local functions
   ===========================================================================================================
 */

static void
net_client_class_init(NetClientClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(NetClientPrivate));
    gobject_class->dispose = net_client_dispose;
    gobject_class->finalize = net_client_finalise;
    signals[0] = g_signal_new("cert-check",
                              NET_CLIENT_TYPE,
                              G_SIGNAL_RUN_LAST,
                              0U,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_BOOLEAN,
                              2U,
                              G_TYPE_TLS_CERTIFICATE,
                              G_TYPE_TLS_CERTIFICATE_FLAGS);
    signals[1] = g_signal_new("auth",
                              NET_CLIENT_TYPE,
                              G_SIGNAL_RUN_LAST,
                              0U,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_STRV,
                              1U,
                              G_TYPE_BOOLEAN);
    signals[2] = g_signal_new("cert-pass",
                              NET_CLIENT_TYPE,
                              G_SIGNAL_RUN_LAST,
                              0U,
                              NULL,
                              NULL,
                              NULL,
                              G_TYPE_STRING,
                              1U,
                              G_TYPE_BYTE_ARRAY);
}


static void
net_client_init(NetClient *self)
{
    NetClientPrivate *priv;

    self->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NET_CLIENT_TYPE, NetClientPrivate);
    priv->sock = g_socket_client_new();
    if (priv->sock != NULL) {
        g_socket_client_set_timeout(priv->sock, 180U);
    }
}


static void
net_client_dispose(GObject *object)
{
    const NetClient *client = NET_CLIENT(object);
    NetClientPrivate *priv = client->priv;
    const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_parent_class);

    net_client_shutdown(client);
    g_clear_object(&priv->sock);
    g_clear_object(&priv->certificate);
    g_debug("disposed connection to %s", priv->host_and_port);

    (*parent_class->dispose)(object);
}


static void
net_client_finalise(GObject *object)
{
    const NetClient *client = NET_CLIENT(object);
    NetClientPrivate *priv = client->priv;
    const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_parent_class);

    g_debug("finalised connection to %s", priv->host_and_port);
    g_free(priv->host_and_port);

    (*parent_class->finalize)(object);
}


static gboolean
cert_accept_cb(G_GNUC_UNUSED GTlsConnection *conn,
               GTlsCertificate              *peer_cert,
               GTlsCertificateFlags          errors,
               gpointer                      user_data)
{
    NetClient *client = NET_CLIENT(user_data);
    gboolean result;

    g_debug("emit 'cert-check' signal for client %p", client);
    g_signal_emit(client, signals[0], 0, peer_cert, errors, &result);
    return result;
}
