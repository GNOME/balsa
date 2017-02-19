/* -*-mode:c; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
#   include "config.h"
#endif                          /* HAVE_CONFIG_H */

#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 199309L
#include "send.h"

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <string.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "server.h"
#include "misc.h"
#include "missing.h"
#include "information.h"

#include "net-client-smtp.h"
#include "gmime-filter-header.h"
#include "smtp-server.h"

#include <glib/gi18n.h>

typedef struct _MessageQueueItem MessageQueueItem;

struct _MessageQueueItem {
    LibBalsaMessage *orig;
    GMimeStream *stream;
    LibBalsaFccboxFinder finder;
    NetClientSmtpMessage *smtp_msg;
    gint64 message_size;
    gint64 sent;
    gint64 acc;
    gint64 update;
};

typedef struct _SendMessageInfo SendMessageInfo;

struct _SendMessageInfo {
    LibBalsaMailbox *outbox;
    NetClientSmtp *session;
    gchar *mta_name;
    GList *items;               /* of MessageQueueItem */
    gboolean debug;
};

static int sending_threads = 0; /* how many sending threads are active? */
/* end of state variables section */

gboolean
libbalsa_is_sending_mail(void)
{
    return sending_threads > 0;
}


/* libbalsa_wait_for_sending_thread:
   wait for the sending thread but not longer than max_time seconds.
   -1 means wait indefinetely (almost).
 */
void
libbalsa_wait_for_sending_thread(gint max_time)
{
    gint sleep_time = 0;
#define DOZE_LENGTH (20 * 1000)
    static const struct timespec req = {
        0, DOZE_LENGTH * 1000
    };   /*nanoseconds*/

    if (max_time < 0) {
        max_time = G_MAXINT;
    } else {
        max_time *= 1000000;  /* convert to microseconds */
    }
    while (sending_threads > 0 && sleep_time < max_time) {
        while (gtk_events_pending()) {
            gtk_main_iteration_do(FALSE);
        }
        nanosleep(&req, NULL);
        sleep_time += DOZE_LENGTH;
    }
}


static MessageQueueItem *
msg_queue_item_new(LibBalsaFccboxFinder finder)
{
    MessageQueueItem *mqi;

    mqi = g_new0(MessageQueueItem, 1U);
    mqi->finder = finder;
    return mqi;
}


static void
msg_queue_item_destroy(MessageQueueItem *mqi)
{
    if (mqi->smtp_msg != NULL) {
        net_client_smtp_msg_free(mqi->smtp_msg);
    }
    if (mqi->stream != NULL) {
        g_object_unref(G_OBJECT(mqi->stream));
    }
    if (mqi->orig != NULL) {
        g_object_unref(G_OBJECT(mqi->orig));
    }
    g_free(mqi);
}


static SendMessageInfo *
send_message_info_new(LibBalsaMailbox *outbox,
                      NetClientSmtp   *session,
                      const gchar     *name)
{
    SendMessageInfo *smi;

    smi = g_new0(SendMessageInfo, 1);
    smi->session = session;
    smi->outbox = outbox;
    smi->mta_name = g_strdup(name);
    return smi;
}


static void
send_message_info_destroy(SendMessageInfo *smi)
{
    if (smi->session != NULL) {
        g_object_unref(G_OBJECT(smi->session));
    }
    if (smi->items != NULL) {
        g_list_free(smi->items);
    }
    g_free(smi->mta_name);
    g_free(smi);
}


#if HAVE_GPGME
static LibBalsaMsgCreateResult libbalsa_create_rfc2440_buffer(LibBalsaMessage *message,
                                                              GMimePart       *mime_part,
                                                              GtkWindow       *parent,
                                                              GError         **error);
static LibBalsaMsgCreateResult do_multipart_crypto(LibBalsaMessage *message,
                                                   GMimeObject    **mime_root,
                                                   GtkWindow       *parent,
                                                   GError         **error);

#endif

static gboolean balsa_send_message_real(SendMessageInfo *info);
static LibBalsaMsgCreateResult libbalsa_message_create_mime_message(LibBalsaMessage *message,
                                                                    gboolean         flow,
                                                                    gboolean         postponing,
                                                                    GError         **error);
static LibBalsaMsgCreateResult libbalsa_create_msg(LibBalsaMessage *message,
                                                   gboolean         flow,
                                                   GError         **error);
static LibBalsaMsgCreateResult libbalsa_fill_msg_queue_item_from_queu(LibBalsaMessage  *message,
                                                                      MessageQueueItem *mqi);

GtkWidget *send_progress_message = NULL;
GtkWidget *send_dialog = NULL;
GtkWidget *send_dialog_bar = NULL;

static void
send_dialog_response_cb(GtkWidget *w,
                        gint       response)
{
    if (response == GTK_RESPONSE_CLOSE) {
        gtk_widget_destroy(w);
    }
}


static void
send_dialog_destroy_cb(GtkWidget *w)
{
    send_dialog = NULL;
    send_progress_message = NULL;
    send_dialog_bar = NULL;
}


/* ensure_send_progress_dialog:
   ensures that there is send_dialog available.
 */
static void
ensure_send_progress_dialog(GtkWindow *parent)
{
    GtkWidget *label;
    GtkBox *content_box;

    if (send_dialog != NULL) {
        return;
    }

    send_dialog = gtk_dialog_new_with_buttons(_("Sending Mail…"),
                                              parent,
                                              GTK_DIALOG_DESTROY_WITH_PARENT |
                                              libbalsa_dialog_flags(),
                                              _("_Hide"), GTK_RESPONSE_CLOSE,
                                              NULL);
    gtk_window_set_role(GTK_WINDOW(send_dialog), "send_dialog");
    label = gtk_label_new(_("Sending Mail…"));
    content_box =
        GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(send_dialog)));
    gtk_box_pack_start(content_box, label, FALSE, FALSE, 0);

    send_progress_message = gtk_label_new("");
    gtk_box_pack_start(content_box, send_progress_message, FALSE, FALSE, 0);

    send_dialog_bar = gtk_progress_bar_new();
    gtk_box_pack_start(content_box, send_dialog_bar, FALSE, FALSE, 0);
    gtk_window_set_default_size(GTK_WINDOW(send_dialog), 250, 100);
    gtk_widget_show_all(send_dialog);
    g_signal_connect(G_OBJECT(send_dialog), "response",
                     G_CALLBACK(send_dialog_response_cb), NULL);
    g_signal_connect(G_OBJECT(send_dialog), "destroy",
                     G_CALLBACK(send_dialog_destroy_cb), NULL);
    /* Progress bar done */
}


static void
lbs_set_content(GMimePart *mime_part,
                gchar     *content)
{
    GMimeStream *stream;
    GMimeDataWrapper *wrapper;

    stream = g_mime_stream_mem_new();
    g_mime_stream_write(stream, content, strlen(content));

    wrapper =
        g_mime_data_wrapper_new_with_stream(stream,
                                            GMIME_CONTENT_ENCODING_DEFAULT);
    g_object_unref(stream);

    g_mime_part_set_content_object(mime_part, wrapper);
    g_object_unref(wrapper);
}


#ifdef HAVE_GPGME
static GMimeObject *
add_mime_body_plain(LibBalsaMessageBody     *body,
                    gboolean                 flow,
                    gboolean                 postpone,
                    guint                    use_gpg_mode,
                    LibBalsaMsgCreateResult *crypt_res,
                    GError                 **error)
#else
static GMimeObject *
add_mime_body_plain(LibBalsaMessageBody * body, gboolean flow, gboolean postpone)
#endif
{
    GMimePart *mime_part;
    const gchar *charset;
#ifdef HAVE_GPGME
    GtkWindow *parent = g_object_get_data(G_OBJECT(body->message), "parent-window");
#endif

    g_return_val_if_fail(body, NULL);

    charset = body->charset;

    if (body->content_type != NULL) {
        /* Use the suplied mime type */
        gchar *type, *subtype;

        /* FIXME: test sending with different mime types */
        g_message("path active");
        type = g_strdup (body->content_type);
        if ((subtype = strchr (type, '/')) != NULL) {
            *subtype++ = 0;
            mime_part = g_mime_part_new_with_type(type, subtype);
        } else {
            mime_part = g_mime_part_new_with_type("text", "plain");
        }
        g_free (type);
    } else {
        mime_part = g_mime_part_new_with_type("text", "plain");
    }

    g_mime_object_set_disposition(GMIME_OBJECT(mime_part), GMIME_DISPOSITION_INLINE);
    g_mime_part_set_content_encoding(mime_part, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE);
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part), "charset",
                                             charset != NULL ? charset : "us-ascii");
    if (flow) {
        g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part),
                                                 "DelSp", "Yes");
        g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part),
                                                 "Format", "Flowed");
    }

    if ((charset != NULL) &&
        (g_ascii_strcasecmp(charset, "UTF-8") != 0) &&
        (g_ascii_strcasecmp(charset, "UTF8") != 0)) {
        GMimeStream *stream, *filter_stream;
        GMimeFilter *filter;
        GMimeDataWrapper *wrapper;

        stream = g_mime_stream_mem_new();
        filter_stream = g_mime_stream_filter_new(stream);
        filter = g_mime_filter_charset_new("UTF-8", charset);
        g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
        g_object_unref(G_OBJECT(filter));

        g_mime_stream_write(filter_stream, body->buffer, strlen(body->buffer));
        g_object_unref(filter_stream);

        wrapper =
            g_mime_data_wrapper_new_with_stream(stream,
                                                GMIME_CONTENT_ENCODING_DEFAULT);
        g_object_unref(stream);

        g_mime_part_set_content_object(mime_part, wrapper);
        g_object_unref(G_OBJECT(wrapper));
    } else {
        lbs_set_content(mime_part, body->buffer);
    }

#ifdef HAVE_GPGME
    /* rfc 2440 sign/encrypt if requested */
    if (use_gpg_mode != 0) {
        *crypt_res =
            libbalsa_create_rfc2440_buffer(body->message,
                                           GMIME_PART(mime_part),
                                           parent, error);

        if (*crypt_res != LIBBALSA_MESSAGE_CREATE_OK) {
            g_object_unref(G_OBJECT(mime_part));
            return NULL;
        }
    }
#endif

    /* if requested, add a text/html version in a multipart/alternative */
    if (body->html_buffer && !postpone) {
        GMimeMultipart *mpa = g_mime_multipart_new_with_subtype("alternative");

        g_mime_multipart_add(mpa, GMIME_OBJECT(mime_part));
        g_object_unref(G_OBJECT(mime_part));

        mime_part = g_mime_part_new_with_type("text", "html");
        g_mime_multipart_add(mpa, GMIME_OBJECT(mime_part));
        g_object_unref(G_OBJECT(mime_part));
        g_mime_object_set_disposition(GMIME_OBJECT(mime_part), GMIME_DISPOSITION_INLINE);
        g_mime_part_set_content_encoding(mime_part, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE);
        g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part),
                                                 "charset", "UTF-8");
        lbs_set_content(mime_part, body->html_buffer);

#ifdef HAVE_GPGME
        if ((use_gpg_mode != 0) &&
            ((use_gpg_mode & LIBBALSA_PROTECT_MODE) != LIBBALSA_PROTECT_SIGN)) {
            *crypt_res =
                libbalsa_create_rfc2440_buffer(body->message,
                                               GMIME_PART(mime_part),
                                               parent, error);

            if (*crypt_res != LIBBALSA_MESSAGE_CREATE_OK) {
                g_object_unref(G_OBJECT(mpa));
                return NULL;
            }
        }
#endif

        return GMIME_OBJECT(mpa);
    } else {
        return GMIME_OBJECT(mime_part);
    }
}

#if 0
/* you never know when you will need this one... */
static void
dump_queue(const char *msg)
{
    MessageQueueItem *mqi = message_queue;
    printf("dumping message queue at %s:\n", msg);
    while (mqi) {
        printf("item: %p\n", mqi);
        mqi = mqi->next_message;
    }
}


#endif

/* libbalsa_message_queue:
   places given message in the outbox.
 */
static void libbalsa_set_message_id(GMimeMessage *mime_message);

LibBalsaMsgCreateResult
libbalsa_message_queue(LibBalsaMessage    *message,
                       LibBalsaMailbox    *outbox,
                       LibBalsaMailbox    *fccbox,
                       LibBalsaSmtpServer *smtp_server,
                       gboolean            flow,
                       GError            **error)
{
    LibBalsaMsgCreateResult result;
    guint big_message;
    gboolean rc;

    g_assert(error != NULL);
    g_return_val_if_fail(message, LIBBALSA_MESSAGE_CREATE_ERROR);

    if ((result = libbalsa_create_msg(message, flow, error)) !=
        LIBBALSA_MESSAGE_CREATE_OK) {
        return result;
    }

    if (fccbox != NULL) {
        g_mime_object_set_header(GMIME_OBJECT(message->mime_msg), "X-Balsa-Fcc",
                                 fccbox->url);
    }
    g_mime_object_set_header(GMIME_OBJECT(message->mime_msg), "X-Balsa-DSN",
                             message->request_dsn ? "1" : "0");
    g_mime_object_set_header(GMIME_OBJECT(message->mime_msg), "X-Balsa-SmtpServer",
                             libbalsa_smtp_server_get_name(smtp_server));

    big_message = libbalsa_smtp_server_get_big_message(smtp_server);
    if (big_message > 0) {
        GMimeMessage *mime_msg;
        GMimeMessage **mime_msgs;
        size_t nparts;
        guint i;

        mime_msg = message->mime_msg;
        mime_msgs =
            g_mime_message_partial_split_message(mime_msg, big_message,
                                                 &nparts);
        rc = TRUE;
        for (i = 0; i < nparts; ++i) {
            if (nparts > 1) {
                /* RFC 2046, 5.2.2: "...it is specified that entities of
                 * type "message/partial" must always have a content-
                 * transfer-encoding of 7bit (the default)" */
                g_mime_part_set_content_encoding(GMIME_PART
                                                     (mime_msgs[i]->mime_part),
                                                 GMIME_CONTENT_ENCODING_7BIT);
                libbalsa_set_message_id(mime_msgs[i]);
            }
            if (rc) {
                message->mime_msg = mime_msgs[i];
                rc = libbalsa_message_copy(message, outbox, error);
            }
            g_object_unref(mime_msgs[i]);
        }
        g_free(mime_msgs);
        message->mime_msg = mime_msg;
    } else {
        rc = libbalsa_message_copy(message, outbox, error);
    }

    return rc ? LIBBALSA_MESSAGE_CREATE_OK : LIBBALSA_MESSAGE_QUEUE_ERROR;
}


/* libbalsa_message_send:
   send the given messsage (if any, it can be NULL) and all the messages
   in given outbox.
 */
static gboolean lbs_process_queue(LibBalsaMailbox     *outbox,
                                  LibBalsaFccboxFinder finder,
                                  LibBalsaSmtpServer  *smtp_server,
                                  gboolean             debug,
                                  GtkWindow           *parent);

LibBalsaMsgCreateResult
libbalsa_message_send(LibBalsaMessage     *message,
                      LibBalsaMailbox     *outbox,
                      LibBalsaMailbox     *fccbox,
                      LibBalsaFccboxFinder finder,
                      LibBalsaSmtpServer  *smtp_server,
                      GtkWindow           *parent,
                      gboolean             flow,
                      gboolean             debug,
                      GError             **error)
{
    LibBalsaMsgCreateResult result = LIBBALSA_MESSAGE_CREATE_OK;

    g_return_val_if_fail(smtp_server != NULL,
                         LIBBALSA_MESSAGE_SERVER_ERROR);

    if (message != NULL) {
        result = libbalsa_message_queue(message, outbox, fccbox,
                                        smtp_server, flow, error);
    }

    if ((result == LIBBALSA_MESSAGE_CREATE_OK)
        && !lbs_process_queue(outbox, finder, smtp_server, debug, parent)) {
        return LIBBALSA_MESSAGE_SEND_ERROR;
    }

    return result;
}


static void
add_recipients(NetClientSmtpMessage *message,
               InternetAddressList  *recipient_list,
               gboolean              request_dsn)
{
    if (recipient_list != NULL) {
        const InternetAddress *ia;
        NetClientSmtpDsnMode dsn_mode;
        int i;

        /* XXX  - It would be cool if LibBalsaAddress could contain DSN options
           for a particular recipient.  For the time being, just use a switch */
        if (request_dsn) {
            dsn_mode = NET_CLIENT_SMTP_DSN_SUCCESS + NET_CLIENT_SMTP_DSN_FAILURE +
                NET_CLIENT_SMTP_DSN_DELAY;
        } else {
            dsn_mode = NET_CLIENT_SMTP_DSN_NEVER;
        }

        for (i = 0; i < internet_address_list_length(recipient_list); i++) {
            ia = internet_address_list_get_address(recipient_list, i);

            if (INTERNET_ADDRESS_IS_MAILBOX(ia)) {
                net_client_smtp_msg_add_recipient(message,
                                                  INTERNET_ADDRESS_MAILBOX(ia)->addr,
                                                  dsn_mode);
            } else {
                add_recipients(message, INTERNET_ADDRESS_GROUP(ia)->members, request_dsn);
            }
        }
    }
}


static gssize
send_message_data_cb(gchar   *buffer,
                     gsize    count,
                     gpointer user_data,
                     GError **error)
{
    ssize_t read_res;
    MessageQueueItem *mqi = (MessageQueueItem *) user_data;

    read_res = g_mime_stream_read(mqi->stream, buffer, count);
    if ((mqi->message_size > 0) && (read_res > 0)) {
        mqi->acc += read_res;
        if (mqi->acc >= mqi->update) {
            float percent;
            SendThreadMessage *threadmsg;

            mqi->sent += mqi->acc;
            mqi->acc = 0;
            percent = (float) mqi->sent / (float) mqi->message_size;
            if (percent > 1.0F) {
                percent = 1.0F;
            }
            MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, "", NULL, NULL, percent);
        }
    }
    return read_res;
}


static gboolean
check_cert(NetClient           *client,
           GTlsCertificate     *peer_cert,
           GTlsCertificateFlags errors,
           gpointer             user_data)
{
    GByteArray *cert_der = NULL;
    gboolean result = FALSE;

    /* FIXME - this a hack, simulating the (OpenSSL based) input for libbalsa_is_cert_known().
        If we switch completely to
     * (GnuTLS based) GTlsCertificate/GTlsClientConnection, we can omit this... */
    g_debug("%s: %p %p %u %p", __func__, client, peer_cert, errors, user_data);

    /* create a OpenSSL X509 object from the certificate's DER data */
    g_object_get(G_OBJECT(peer_cert), "certificate", &cert_der, NULL);
    if (cert_der != NULL) {
        X509 *ossl_cert;
        const unsigned char *der_p;

        der_p = (const unsigned char *) cert_der->data;
        ossl_cert = d2i_X509(NULL, &der_p, cert_der->len);
        g_byte_array_unref(cert_der);

        if (ossl_cert != NULL) {
            long vfy_result;

            /* convert the GIO error flags into OpenSSL error flags */
            if ((errors & G_TLS_CERTIFICATE_UNKNOWN_CA) == G_TLS_CERTIFICATE_UNKNOWN_CA) {
                vfy_result = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT;
            } else if ((errors & G_TLS_CERTIFICATE_BAD_IDENTITY) ==
                       G_TLS_CERTIFICATE_BAD_IDENTITY) {
                vfy_result = X509_V_ERR_SUBJECT_ISSUER_MISMATCH;
            } else if ((errors & G_TLS_CERTIFICATE_NOT_ACTIVATED) ==
                       G_TLS_CERTIFICATE_NOT_ACTIVATED) {
                vfy_result = X509_V_ERR_CERT_NOT_YET_VALID;
            } else if ((errors & G_TLS_CERTIFICATE_EXPIRED) == G_TLS_CERTIFICATE_EXPIRED) {
                vfy_result = X509_V_ERR_CERT_HAS_EXPIRED;
            } else if ((errors & G_TLS_CERTIFICATE_REVOKED) == G_TLS_CERTIFICATE_REVOKED) {
                vfy_result = X509_V_ERR_CERT_REVOKED;
            } else {
                vfy_result = X509_V_ERR_APPLICATION_VERIFICATION;
            }

            result = libbalsa_is_cert_known(ossl_cert, vfy_result);
            X509_free(ossl_cert);
        }
    }

    return result;
}


static gchar **
get_auth(NetClient *client,
         gpointer   user_data)
{
    LibBalsaServer *server = LIBBALSA_SERVER(user_data);
    gchar **result = NULL;

    g_debug("%s: %p %p: encrypted = %d", __func__, client, user_data,
            net_client_is_encrypted(client));
    if (server->try_anonymous == 0U) {
        result = g_new0(gchar *, 3U);
        result[0] = g_strdup(server->user);
        if ((server->passwd != NULL) && (server->passwd[0] != '\0')) {
            result[1] = g_strdup(server->passwd);
        } else {
            result[1] = libbalsa_server_get_password(server, NULL);
        }
    }
    return result;
}


static gchar *
get_cert_pass(NetClient        *client,
			  const GByteArray *cert_der,
			  gpointer          user_data)
{
	/* FIXME - we just return the passphrase from the config, but we may also want to show a dialogue here... */
	return g_strdup(libbalsa_smtp_server_get_cert_passphrase(LIBBALSA_SMTP_SERVER(user_data)));
}


/* libbalsa_process_queue:
   treats given mailbox as a set of messages to send. Loads them up and
   launches sending thread/routine.
   NOTE that we do not close outbox after reading. send_real/thread message
   handler does that.
 */
static gboolean
lbs_process_queue(LibBalsaMailbox     *outbox,
                  LibBalsaFccboxFinder finder,
                  LibBalsaSmtpServer  *smtp_server,
                  gboolean             debug,
                  GtkWindow           *parent)
{
    LibBalsaServer *server = LIBBALSA_SERVER(smtp_server);
    SendMessageInfo *send_message_info;
    NetClientSmtp *session;
    guint msgno;

    g_mutex_lock(&send_messages_lock);

    if (!libbalsa_mailbox_open(outbox, NULL)) {
        g_mutex_unlock(&send_messages_lock);
        return FALSE;
    }

    /* create the SMTP session */
    if (server->security == NET_CLIENT_CRYPT_ENCRYPTED) {
        session = net_client_smtp_new(server->host, 465U, server->security);
    } else {
        // FIXME - submission (587) is the standard, but most isp's use 25...
        session = net_client_smtp_new(server->host, 587U, server->security);
    }

    /* load client certificate if configured */
    if (libbalsa_smtp_server_require_client_cert(smtp_server)) {
        const gchar *client_cert = libbalsa_smtp_server_get_cert_file(smtp_server);
    	GError *error = NULL;

    	g_signal_connect(G_OBJECT(session), "cert-pass", G_CALLBACK(get_cert_pass), smtp_server);
    	if (!net_client_set_cert_from_file(NET_CLIENT(session), client_cert, &error)) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                 _("Cannot load certificate file %s: %s"),
								 client_cert, error->message);
            g_error_free(error);
            g_mutex_unlock(&send_messages_lock);
    		return FALSE;
    	}
    }

    /* connect signals */
    g_signal_connect(G_OBJECT(session), "cert-check", G_CALLBACK(check_cert), session);
    g_signal_connect(G_OBJECT(session), "auth", G_CALLBACK(get_auth), smtp_server);

    send_message_info =
        send_message_info_new(outbox, session, libbalsa_smtp_server_get_name(smtp_server));

    for (msgno = libbalsa_mailbox_total_messages(outbox); msgno > 0U; msgno--) {
        MessageQueueItem *new_message;
        LibBalsaMessage *msg;
        const gchar *smtp_server_name;
        LibBalsaMsgCreateResult created;

        /* Skip this message if it either FLAGGED or DELETED: */
        if (!libbalsa_mailbox_msgno_has_flags(outbox, msgno, 0,
                                              (LIBBALSA_MESSAGE_FLAG_FLAGGED |
                                               LIBBALSA_MESSAGE_FLAG_DELETED))) {
            continue;
        }

        msg = libbalsa_mailbox_get_message(outbox, msgno);
        if (!msg) {       /* error? */
            continue;
        }
        libbalsa_message_body_ref(msg, TRUE, TRUE);
        smtp_server_name = libbalsa_message_get_user_header(msg, "X-Balsa-SmtpServer");
        if (!smtp_server_name) {
            smtp_server_name = libbalsa_smtp_server_get_name(NULL);
        }
        if (strcmp(smtp_server_name, libbalsa_smtp_server_get_name(smtp_server)) != 0) {
            libbalsa_message_body_unref(msg);
            g_object_unref(msg);
            continue;
        }
        msg->request_dsn = (atoi(libbalsa_message_get_user_header(msg, "X-Balsa-DSN")) != 0);

        new_message = msg_queue_item_new(finder);
        created = libbalsa_fill_msg_queue_item_from_queu(msg, new_message);
        libbalsa_message_body_unref(msg);

        if (created != LIBBALSA_MESSAGE_CREATE_OK) {
            msg_queue_item_destroy(new_message);
        } else {
            const InternetAddress *ia;
            const gchar *mailbox;

            libbalsa_message_change_flags(msg, LIBBALSA_MESSAGE_FLAG_FLAGGED, 0);

            send_message_info->items = g_list_prepend(send_message_info->items, new_message);
            new_message->smtp_msg = net_client_smtp_msg_new(send_message_data_cb, new_message);

            if (msg->request_dsn) {
                net_client_smtp_msg_set_dsn_opts(new_message->smtp_msg, msg->message_id, FALSE);
            }

            /* Add the sender info */
            if (msg->headers->from &&
                (ia = internet_address_list_get_address(msg->headers->from, 0))) {
                while (ia != NULL && INTERNET_ADDRESS_IS_GROUP(ia)) {
                    ia = internet_address_list_get_address (INTERNET_ADDRESS_GROUP(
                                                                ia)->members, 0);
                }
                mailbox = ia ? INTERNET_ADDRESS_MAILBOX(ia)->addr : "";
            } else {
                mailbox = "";
            }
            net_client_smtp_msg_set_sender(new_message->smtp_msg, mailbox);

            /* Now need to add the recipients to the message. */
            add_recipients(new_message->smtp_msg, msg->headers->to_list, msg->request_dsn);
            add_recipients(new_message->smtp_msg, msg->headers->cc_list, msg->request_dsn);
            add_recipients(new_message->smtp_msg, msg->headers->bcc_list, msg->request_dsn);

            /* Estimate the size of the message.  This need not be exact but it's better to err
               on the large side since some
             * message headers may be altered during the transfer. */
            new_message->message_size = g_mime_stream_length(new_message->stream);

            /* Set up counters for the progress bar.  Update is the byte count when the progress
               bar should be updated.  This is
             * capped around 5k so that the progress bar moves about once per second on a slow
             * line.  On small messages it is
             * smaller to allow smooth progress of the bar. */
            new_message->update = new_message->message_size / 20;
            if (new_message->update < 100) {
                new_message->update = 100;
            } else if (new_message->update > 5 * 1024) {
                new_message->update = 5 * 1024;
            }
            new_message->sent = 0;
            new_message->acc = 0;
        }
        g_object_unref(msg);
    }

    /* launch the thread for sending the messages only if we collected any */
    if (send_message_info->items != NULL) {
        GThread *send_mail;

        ensure_send_progress_dialog(parent);
        sending_threads++;
        send_mail = g_thread_new("balsa_send_message_real",
                                 (GThreadFunc) balsa_send_message_real,
                                 send_message_info);
        g_thread_unref(send_mail);
    } else {
        send_message_info_destroy(send_message_info);
    }

    g_mutex_unlock(&send_messages_lock);
    return TRUE;
}


gboolean
libbalsa_process_queue(LibBalsaMailbox     *outbox,
                       LibBalsaFccboxFinder finder,
                       GSList              *smtp_servers,
                       GtkWindow           *parent,
                       gboolean             debug)
{
    for (; smtp_servers; smtp_servers = smtp_servers->next) {
        LibBalsaSmtpServer *smtp_server =
            LIBBALSA_SMTP_SERVER(smtp_servers->data);
        if (!lbs_process_queue(outbox, finder, smtp_server, debug, parent)) {
            return FALSE;
        }
    }

    return TRUE;
}


/* balsa_send_message_real:
   does the actual message sending.
   This function may be called as a thread and should therefore do
   proper gdk_threads_{enter/leave} stuff around GTK or libbalsa calls.
   Also, structure info should be freed before exiting.
 */

static gboolean
balsa_send_message_real_idle_cb(LibBalsaMailbox *outbox)
{
    libbalsa_mailbox_close(outbox, TRUE);
    g_object_unref(outbox);

    return FALSE;
}


static gboolean
balsa_send_message_real(SendMessageInfo *info)
{
    gboolean result;
    GError *error = NULL;
    SendThreadMessage *threadmsg;
    gchar *greeting = NULL;

    g_debug("%s: starting", __func__);

    /* connect the SMTP server */
    result = net_client_smtp_connect(info->session, &greeting, &error);
    g_debug("%s: connect = %d [%p]: '%s'", __func__, result, info->items, greeting);
    if (result) {
        GList *this_msg;
        gchar *msg;

        msg = g_strdup_printf(_("Connected to MTA %s: %s"), info->mta_name, greeting);
        MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, msg, NULL, NULL, 0);
        g_free(msg);
        for (this_msg = info->items; this_msg != NULL; this_msg = this_msg->next) {
            MessageQueueItem *mqi = (MessageQueueItem *) this_msg->data;
            gboolean send_res;

            g_debug("%s: mqi = %p", __func__, mqi);
            /* send the message */
            send_res = net_client_smtp_send_msg(info->session, mqi->smtp_msg, &error);

            g_mutex_lock(&send_messages_lock);
            if ((mqi->orig != NULL) && (mqi->orig->mailbox != NULL)) {
                libbalsa_message_change_flags(mqi->orig, 0, LIBBALSA_MESSAGE_FLAG_FLAGGED);
            } else {
                g_message("mqi: %p mqi->orig: %p mqi->orig->mailbox: %p\n",
                          mqi,
                          mqi ? mqi->orig : NULL,
                          mqi && mqi->orig ? mqi->orig->mailbox : NULL);
            }

            if (send_res) {
                /* sending message successful */
                if ((mqi->orig != NULL) && (mqi->orig->mailbox != NULL)) {
                    gboolean remove = TRUE;
                    const gchar *fccurl = libbalsa_message_get_user_header(mqi->orig,
                                                                           "X-Balsa-Fcc");

                    if (fccurl != NULL) {
                        LibBalsaMailbox *fccbox = mqi->finder(fccurl);
                        GError *err = NULL;

                        libbalsa_message_change_flags(mqi->orig,
                                                      0,
                                                      LIBBALSA_MESSAGE_FLAG_NEW |
                                                      LIBBALSA_MESSAGE_FLAG_FLAGGED);
                        libbalsa_mailbox_sync_storage(mqi->orig->mailbox, FALSE);
                        remove = libbalsa_message_copy(mqi->orig, fccbox, &err);
                        if (!remove) {
                            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                                 _("Saving sent message to %s failed: %s"),
                                                 fccbox->url, err ? err->message : "?");
                            g_clear_error(&err);
                        }
                    }
                    /* If copy failed, mark the message again as flagged - otherwise it will get
                       resent again. And again, and
                     * again... */
                    libbalsa_message_change_flags(mqi->orig,
                                                  remove
                                                  ? LIBBALSA_MESSAGE_FLAG_DELETED
                                                  : LIBBALSA_MESSAGE_FLAG_FLAGGED,
                                                  0);
                }
            } else {
                /* sending message failed - mark it as:
                 *   - flagged, so it will not be sent again until the error is fixed and the
                 * user manually clears the flag;
                 *   - undeleted, in case it was already deleted. */
                if ((mqi->orig != NULL) && (mqi->orig->mailbox != NULL)) {
                    libbalsa_message_change_flags(mqi->orig,
                                                  LIBBALSA_MESSAGE_FLAG_FLAGGED,
                                                  LIBBALSA_MESSAGE_FLAG_DELETED);
                }
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _(
                                         "Sending message failed: %s\nMessage left in your outbox."),
                                     error->message);
                g_clear_error(&error);
            }

            /* free data */
            msg_queue_item_destroy(mqi);
            g_mutex_unlock(&send_messages_lock);
        }
    } else {
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             _("Connecting MTA %s (%s) failed: %s"),
                             info->mta_name,
                             net_client_get_host(NET_CLIENT(info->session)),
                             error->message);
        g_error_free(error);
    }
    g_free(greeting);

    /* close outbox in an idle callback, as it might affect the display */
    g_idle_add((GSourceFunc) balsa_send_message_real_idle_cb, g_object_ref(info->outbox));

    /* clean up */
    send_message_info_destroy(info);

    g_mutex_lock(&send_messages_lock);
    MSGSENDTHREAD(threadmsg, MSGSENDTHREADFINISHED, "", NULL, NULL, 0);
    sending_threads--;
    g_mutex_unlock(&send_messages_lock);

    return result;
}


static void
message_add_references(const LibBalsaMessage *message,
                       GMimeMessage          *msg)
{
    /* If the message has references set, add them to the envelope */
    if (message->references != NULL) {
        GList *list = message->references;
        GString *str = g_string_new(NULL);

        do {
            if (str->len > 0) {
                g_string_append_c(str, ' ');
            }
            g_string_append_printf(str, "<%s>", (gchar *) list->data);
        } while ((list = list->next) != NULL);
        g_mime_object_set_header(GMIME_OBJECT(msg), "References", str->str);
        g_string_free(str, TRUE);
    }

    if (message->in_reply_to != NULL) {
        /* There's no specific header function for In-Reply-To */
        g_mime_object_set_header(GMIME_OBJECT(msg), "In-Reply-To",
                                 message->in_reply_to->data);
    }
}


#ifdef HAVE_GPGME
static GList *
get_mailbox_names(GList               *list,
                  InternetAddressList *address_list)
{
    gint i, len;

    len = internet_address_list_length(address_list);
    for (i = 0; i < len; i++) {
        InternetAddress *ia =
            internet_address_list_get_address(address_list, i);

        if (INTERNET_ADDRESS_IS_MAILBOX(ia)) {
            list = g_list_append(list, g_strdup(((InternetAddressMailbox *) ia)->addr));
        } else {
            list = get_mailbox_names(list, ((InternetAddressGroup *) ia)->members);
        }
    }

    return list;
}


#endif

/* We could have used g_strsplit_set(s, "/;", 3) but it is not
 * available in older glib. */
static gchar **
parse_content_type(const char *content_type)
{
    gchar **ret = g_new0(gchar *, 3);
    char *delim, *slash = strchr(content_type, '/');
    if (!slash) {
        ret[0] = g_strdup(content_type);
        return ret;
    }
    ret[0] = g_strndup(content_type, slash - content_type);
    slash++;
    for (delim = slash; *delim && *delim != ';' && *delim != ' '; delim++) {
    }
    ret[1] = g_strndup(slash, delim - slash);
    return ret;
}


/* get_tz_offset() returns tz offset in RFC 5322 format ([-]hhmm) */
static gint
get_tz_offset(time_t t)
{
    GTimeZone *local_tz;
    gint interval;
    gint32 offset;
    gint hours;

    local_tz = g_time_zone_new_local();
    interval = g_time_zone_find_interval(local_tz, G_TIME_TYPE_UNIVERSAL, t);
    offset = g_time_zone_get_offset(local_tz, interval);
    g_time_zone_unref(local_tz);
    hours = offset / 3600;
    return (hours * 100) + ((offset - (hours * 3600)) / 60);
}


static LibBalsaMsgCreateResult
libbalsa_message_create_mime_message(LibBalsaMessage *message,
                                     gboolean         flow,
                                     gboolean         postponing,
                                     GError         **error)
{
    gchar **mime_type;
    GMimeObject *mime_root = NULL;
    GMimeMessage *mime_message;
    LibBalsaMessageBody *body;
    InternetAddressList *ia_list;
    gchar *tmp;
    GList *list;
#ifdef HAVE_GPGME
    GtkWindow *parent = g_object_get_data(G_OBJECT(message), "parent-window");
#endif

    body = message->body_list;
    if ((body != NULL) && (body->next != NULL)) {
        mime_root = GMIME_OBJECT(g_mime_multipart_new_with_subtype(message->subtype));
    }

    while (body != NULL) {
        GMimeObject *mime_part;
        mime_part = NULL;

        if ((body->file_uri != NULL) || (body->filename != NULL)) {
            if (body->content_type != NULL) {
                mime_type = parse_content_type(body->content_type);
            } else {
                gchar *mt = g_strdup(libbalsa_vfs_get_mime_type(body->file_uri));
                mime_type = g_strsplit(mt, "/", 2);
                g_free(mt);
            }

            if (body->attach_mode == LIBBALSA_ATTACH_AS_EXTBODY) {
                GMimeContentType *content_type =
                    g_mime_content_type_new("message", "external-body");
                mime_part = g_mime_object_new_type("message", "external-body");
                g_mime_object_set_content_type(mime_part, content_type);
                g_mime_part_set_content_encoding(GMIME_PART(mime_part),
                                                 GMIME_CONTENT_ENCODING_7BIT);
                if (body->filename && !strncmp(body->filename, "URL", 3)) {
                    g_mime_object_set_content_type_parameter(mime_part,
                                                             "access-type", "URL");
                    g_mime_object_set_content_type_parameter(mime_part,
                                                             "URL", body->filename + 4);
                } else {
                    g_mime_object_set_content_type_parameter(mime_part,
                                                             "access-type", "local-file");
                    g_mime_object_set_content_type_parameter(mime_part,
                                                             "name",
                                                             libbalsa_vfs_get_uri_utf8(body->
                                                                                       file_uri));
                }
                lbs_set_content(GMIME_PART(mime_part),
                                "Note: this is _not_ the real body!\n");
            } else if (g_ascii_strcasecmp(mime_type[0], "message") == 0) {
                GMimeStream *stream;
                GMimeParser *parser;
                GMimeMessage *mime_message;
                GError *err = NULL;

                stream = libbalsa_vfs_create_stream(body->file_uri, 0, FALSE, &err);
                if (!stream) {
                    if (err != NULL) {
                        gchar *msg =
                            err->message
                            ? g_strdup_printf(_("Cannot read %s: %s"),
                                              libbalsa_vfs_get_uri_utf8(body->file_uri),
                                              err->message)
                            : g_strdup_printf(_("Cannot read %s"),
                                              libbalsa_vfs_get_uri_utf8(body->file_uri));
                        g_set_error(error, err->domain, err->code, "%s", msg);
                        g_clear_error(&err);
                        g_free(msg);
                    }
                    return LIBBALSA_MESSAGE_CREATE_ERROR;
                }
                parser = g_mime_parser_new_with_stream(stream);
                g_object_unref(stream);
                mime_message = g_mime_parser_construct_message(parser);
                g_object_unref(parser);
                mime_part =
                    GMIME_OBJECT(g_mime_message_part_new_with_message
                                     (mime_type[1], mime_message));
                g_object_unref(mime_message);
            } else {
                const gchar *charset = NULL;
                GMimeStream *stream;
                GMimeDataWrapper *content;
                GError *err = NULL;

                if ((g_ascii_strcasecmp(mime_type[0], "text") == 0)
                    && ((charset = body->charset) == NULL)) {
                    charset = libbalsa_vfs_get_charset(body->file_uri);
                    if (charset == NULL) {
                        static const gchar default_type[] =
                            "application/octet-stream";

                        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                             _("Cannot determine character set "
                                               "for text file “%s”; "
                                               "sending as MIME type “%s”"),
                                             libbalsa_vfs_get_uri_utf8(body->file_uri),
                                             default_type);
                        g_strfreev(mime_type);
                        mime_type = g_strsplit(default_type, "/", 2);
                    }
                }

                /* use BASE64 encoding for non-text mime types
                   use 8BIT for message */
                mime_part =
                    GMIME_OBJECT(g_mime_part_new_with_type(mime_type[0],
                                                           mime_type[1]));
                g_mime_object_set_disposition(mime_part,
                                              body->attach_mode == LIBBALSA_ATTACH_AS_INLINE ?
                                              GMIME_DISPOSITION_INLINE : GMIME_DISPOSITION_ATTACHMENT);
                if (g_ascii_strcasecmp(mime_type[0], "text") != 0) {
                    g_mime_part_set_content_encoding(GMIME_PART(mime_part),
                                                     GMIME_CONTENT_ENCODING_BASE64);
                } else {
                    /* is text */
                    g_mime_object_set_content_type_parameter(mime_part, "charset", charset);
                }

                g_mime_part_set_filename(GMIME_PART(mime_part),
                                         libbalsa_vfs_get_basename_utf8(body->file_uri));
                stream = libbalsa_vfs_create_stream(body->file_uri, 0, FALSE, &err);
                if (!stream) {
                    if (err != NULL) {
                        gchar *msg =
                            err->message
                            ? g_strdup_printf(_("Cannot read %s: %s"),
                                              libbalsa_vfs_get_uri_utf8(body->file_uri),
                                              err->message)
                            : g_strdup_printf(_("Cannot read %s"),
                                              libbalsa_vfs_get_uri_utf8(body->file_uri));
                        g_set_error(error, err->domain, err->code, "%s", msg);
                        g_clear_error(&err);
                        g_free(msg);
                    }
                    g_object_unref(G_OBJECT(mime_part));
                    return LIBBALSA_MESSAGE_CREATE_ERROR;
                }
                content = g_mime_data_wrapper_new_with_stream(stream,
                                                              GMIME_CONTENT_ENCODING_DEFAULT);
                g_object_unref(stream);
                g_mime_part_set_content_object(GMIME_PART(mime_part),
                                               content);
                g_object_unref(content);
            }
            g_strfreev(mime_type);
        } else if (body->buffer != NULL) {
#ifdef HAVE_GPGME
            guint use_gpg_mode;
            LibBalsaMsgCreateResult crypt_res = LIBBALSA_MESSAGE_CREATE_OK;

            /* in '2440 mode, touch *only* the first body! */
            if (!postponing && (body == body->message->body_list) &&
                (message->gpg_mode > 0) &&
                ((message->gpg_mode & LIBBALSA_PROTECT_OPENPGP) != 0)) {
                use_gpg_mode = message->gpg_mode;
            } else {
                use_gpg_mode = 0;
            }
            mime_part = add_mime_body_plain(body, flow, postponing, use_gpg_mode,
                                            &crypt_res, error);
            if (!mime_part) {
                if (mime_root != NULL) {
                    g_object_unref(G_OBJECT(mime_root));
                }
                return crypt_res;
            }
#else
            mime_part = add_mime_body_plain(body, flow, postponing);
#endif /* HAVE_GPGME */
        }

        if (mime_root != NULL) {
            g_mime_multipart_add(GMIME_MULTIPART(mime_root),
                                 GMIME_OBJECT(mime_part));
            g_object_unref(G_OBJECT(mime_part));
        } else {
            mime_root = mime_part;
        }

        body = body->next;
    }

#ifdef HAVE_GPGME
    if ((message->body_list != NULL) && !postponing) {
        LibBalsaMsgCreateResult crypt_res =
            do_multipart_crypto(message, &mime_root, parent, error);
        if (crypt_res != LIBBALSA_MESSAGE_CREATE_OK) {
            return crypt_res;
        }
    }
#endif

    mime_message = g_mime_message_new(TRUE);
    if (mime_root != NULL) {
        GList *param = message->parameters;

        while (param != NULL) {
            gchar **vals = (gchar **)param->data;

            g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_root),
                                                     vals[0], vals[1]);
            param = param->next;
        }
        g_mime_message_set_mime_part(mime_message, mime_root);
        g_object_unref(G_OBJECT(mime_root));
    }
    message_add_references(message, mime_message);

    if (message->headers->from != NULL) {
        tmp = internet_address_list_to_string(message->headers->from,
                                              TRUE);
        if (tmp != NULL) {
            g_mime_message_set_sender(mime_message, tmp);
            g_free(tmp);
        }
    }
    if (message->headers->reply_to != NULL) {
        tmp = internet_address_list_to_string(message->headers->reply_to,
                                              TRUE);
        if (tmp != NULL) {
            g_mime_message_set_reply_to(mime_message, tmp);
            g_free(tmp);
        }
    }

    if (LIBBALSA_MESSAGE_GET_SUBJECT(message)) {
        g_mime_message_set_subject(mime_message,
                                   LIBBALSA_MESSAGE_GET_SUBJECT(message));
    }

    g_mime_message_set_date(mime_message, message->headers->date,
                            get_tz_offset(message->headers->date));

    if ((ia_list = message->headers->to_list)) {
        InternetAddressList *recipients =
            g_mime_message_get_recipients(mime_message,
                                          GMIME_RECIPIENT_TYPE_TO);
        internet_address_list_append(recipients, ia_list);
    }

    if ((ia_list = message->headers->cc_list)) {
        InternetAddressList *recipients =
            g_mime_message_get_recipients(mime_message,
                                          GMIME_RECIPIENT_TYPE_CC);
        internet_address_list_append(recipients, ia_list);
    }

    if ((ia_list = message->headers->bcc_list)) {
        InternetAddressList *recipients =
            g_mime_message_get_recipients(mime_message,
                                          GMIME_RECIPIENT_TYPE_BCC);
        internet_address_list_append(recipients, ia_list);
    }

    if (message->headers->dispnotify_to != NULL) {
        tmp = internet_address_list_to_string(message->headers->dispnotify_to, TRUE);
        if (tmp != NULL) {
            g_mime_object_append_header(GMIME_OBJECT(mime_message),
                                        "Disposition-Notification-To", tmp);
            g_free(tmp);
        }
    }

    for (list = message->headers->user_hdrs; list; list = list->next) {
        gchar **pair = list->data;
        g_strchug(pair[1]);
        g_mime_object_append_header(GMIME_OBJECT(mime_message), pair[0], pair[1]);
#if DEBUG_USER_HEADERS
        printf("adding header '%s:%s'\n", pair[0], pair[1]);
#endif
    }

    tmp = g_strdup_printf("Balsa %s", VERSION);
    g_mime_object_append_header(GMIME_OBJECT(mime_message), "X-Mailer", tmp);
    g_free(tmp);

    message->mime_msg = mime_message;

    return LIBBALSA_MESSAGE_CREATE_OK;
}


/* When we postpone a message in the compose window, we lose track of
 * the message we were replying to.  We *could* save some identifying
 * information in a dummy header, but it could still be hard to track it
 * down: it might have been filed in another mailbox, for instance.  For
 * now, we'll just let it go...
 */
gboolean
libbalsa_message_postpone(LibBalsaMessage *message,
                          LibBalsaMailbox *draftbox,
                          LibBalsaMessage *reply_message,
                          gchar          **extra_headers,
                          gboolean         flow,
                          GError         **error)
{
    if (!message->mime_msg
        && (libbalsa_message_create_mime_message(message, flow,
                                                 TRUE, error) !=
            LIBBALSA_MESSAGE_CREATE_OK)) {
        return FALSE;
    }

    if (extra_headers != NULL) {
        gint i;

        for (i = 0; extra_headers[i] && extra_headers[i + 1]; i += 2) {
            g_mime_object_set_header(GMIME_OBJECT(message->mime_msg), extra_headers[i],
                                     extra_headers[i + 1]);
        }
    }

    return libbalsa_message_copy(message, draftbox, error);
}


static inline gchar
base32_char(guint8 val)
{
    val &= 0x1f;
    if (val <= 25) {
        return val + 'A';
    } else {
        return val + '2' - 26;
    }
}


/* Create a message-id and set it on the mime message.
 */
static void
libbalsa_set_message_id(GMimeMessage *mime_message)
{
    static GMutex mutex;        /* as to make me thread-safe... */
    static GRand *rand = NULL;
    static struct {
        gint64 now_monotonic;
        gdouble randval;
        char user_name[16];
        char host_name[16];
    } id_data;
    GHmac *msg_id_hash;
    guint8 buffer[32];
    gsize buflen;
    gchar *message_id;
    guint8 *src;
    gchar *dst;

    g_mutex_lock(&mutex);
    if (rand == NULL) {
        /* initialise some stuff on first-time use... */
        rand = g_rand_new_with_seed((guint32) time(NULL));
        strncpy(id_data.user_name, g_get_user_name(),
                sizeof(id_data.user_name));
        strncpy(id_data.host_name, g_get_host_name(),
                sizeof(id_data.host_name));
    }

    /* get some randomness... */
    id_data.now_monotonic = g_get_monotonic_time();
    id_data.randval = g_rand_double(rand);

    /* hash the buffer */
    msg_id_hash =
        g_hmac_new(G_CHECKSUM_SHA256, (const guchar *) &id_data,
                   sizeof(id_data));
    buflen = sizeof(buffer);
    g_hmac_get_digest(msg_id_hash, buffer, &buflen);
    g_hmac_unref(msg_id_hash);
    g_mutex_unlock(&mutex);

    /* create a msg id string as base32-encoded string from the first
     * 30 bytes of the hashed result, and separate the groups by '.'
     * or '@' */
    message_id = dst = g_malloc0(54U);  /* = (32 / 5) * 9 */
    src = buffer;
    while (buflen >= 5U) {
        *dst++ = base32_char(src[0] >> 3);
        *dst++ = base32_char((src[0] << 2) + (src[1] >> 6));
        *dst++ = base32_char(src[1] >> 1);
        *dst++ = base32_char((src[1] << 4) + (src[2] >> 4));
        *dst++ = base32_char((src[2] << 1) + (src[3] >> 7));
        *dst++ = base32_char(src[3] >> 2);
        *dst++ = base32_char((src[3] << 3) + (src[4] >> 5));
        *dst++ = base32_char(src[4]);
        src = &src[5];
        buflen -= 5U;
        if (dst == &message_id[(54U / 2U) - 1U]) {
            *dst++ = '@';
        } else if (buflen >= 5U) {
            *dst++ = '.';
        }
    }
    g_mime_message_set_message_id(mime_message, message_id);
    g_free(message_id);
}


/* balsa_create_msg:
   copies message to msg.
 */
static LibBalsaMsgCreateResult
libbalsa_create_msg(LibBalsaMessage *message,
                    gboolean         flow,
                    GError         **error)
{
    if (!message->mime_msg) {
        LibBalsaMsgCreateResult res =
            libbalsa_message_create_mime_message(message, flow,
                                                 FALSE, error);
        if (res != LIBBALSA_MESSAGE_CREATE_OK) {
            return res;
        }
    }

    libbalsa_set_message_id(message->mime_msg);

    return LIBBALSA_MESSAGE_CREATE_OK;
}


static LibBalsaMsgCreateResult
libbalsa_fill_msg_queue_item_from_queu(LibBalsaMessage  *message,
                                       MessageQueueItem *mqi)
{
    GMimeStream *msg_stream;
    LibBalsaMsgCreateResult result = LIBBALSA_MESSAGE_CREATE_ERROR;

    mqi->orig = message;
    if (message->mime_msg != NULL) {
        msg_stream = g_mime_stream_mem_new();
        libbalsa_mailbox_lock_store(message->mailbox);
        g_mime_object_write_to_stream(GMIME_OBJECT(message->mime_msg), msg_stream);
        libbalsa_mailbox_unlock_store(message->mailbox);
        g_mime_stream_reset(msg_stream);
    } else {
        msg_stream = libbalsa_message_stream(message);
    }

    if (msg_stream != NULL) {
        GMimeStream *filter_stream;
        GMimeFilter *filter;

        filter_stream = g_mime_stream_filter_new(msg_stream);

        /* filter out unwanted headers */
        filter = g_mime_filter_header_new();
        g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
        g_object_unref(G_OBJECT(filter));

        /* add CRLF, encode dot */
        filter = g_mime_filter_crlf_new(TRUE, TRUE);
        g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
        g_object_unref(G_OBJECT(filter));

        /* write to a new stream */
        mqi->stream = g_mime_stream_mem_new();
        g_mime_stream_write_to_stream(filter_stream, mqi->stream);
        g_object_unref(G_OBJECT(filter_stream));
        g_mime_stream_reset(mqi->stream);
        g_object_unref(G_OBJECT(msg_stream));

        g_object_ref(G_OBJECT(mqi->orig));
        result = LIBBALSA_MESSAGE_CREATE_OK;
    }

    return result;
}


#ifdef HAVE_GPGME
static const gchar *
lb_send_from(LibBalsaMessage *message)
{
    InternetAddress *ia =
        internet_address_list_get_address(message->headers->from, 0);

    if (message->force_key_id != NULL) {
        return message->force_key_id;
    }

    while (INTERNET_ADDRESS_IS_GROUP(ia)) {
        ia = internet_address_list_get_address(((InternetAddressGroup *)
                                                ia)->members, 0);
    }

    return ((InternetAddressMailbox *) ia)->addr;
}


static LibBalsaMsgCreateResult
libbalsa_create_rfc2440_buffer(LibBalsaMessage *message,
                               GMimePart       *mime_part,
                               GtkWindow       *parent,
                               GError         **error)
{
    gint mode = message->gpg_mode;
    gboolean always_trust = (mode & LIBBALSA_PROTECT_ALWAYS_TRUST) != 0;

    switch (mode & LIBBALSA_PROTECT_MODE) {
    case LIBBALSA_PROTECT_SIGN:       /* sign only */
        if (!libbalsa_rfc2440_sign_encrypt(mime_part,
                                           lb_send_from(message),
                                           NULL, FALSE,
                                           parent, error)) {
            return LIBBALSA_MESSAGE_SIGN_ERROR;
        }
        break;

    case LIBBALSA_PROTECT_ENCRYPT:
    case LIBBALSA_PROTECT_SIGN | LIBBALSA_PROTECT_ENCRYPT:
    {
        GList *encrypt_for = NULL;
        gboolean result;

        /* build a list containing the addresses of all to:, cc:
           and the from: address. Note: don't add bcc: addresses
           as they would be visible in the encrypted block. */
        encrypt_for = get_mailbox_names(encrypt_for,
                                        message->headers->to_list);
        encrypt_for = get_mailbox_names(encrypt_for,
                                        message->headers->cc_list);
        encrypt_for = get_mailbox_names(encrypt_for,
                                        message->headers->from);
        if (message->headers->bcc_list != NULL) {
            libbalsa_information
                (LIBBALSA_INFORMATION_WARNING,
                ngettext("This message will not be encrypted "
                         "for the BCC: recipient.",
                         "This message will not be encrypted "
                         "for the BCC: recipients.",
                         internet_address_list_length
                             (message->headers->bcc_list)));
        }

        if (mode & LIBBALSA_PROTECT_SIGN) {
            result =
                libbalsa_rfc2440_sign_encrypt(mime_part,
                                              lb_send_from(message),
                                              encrypt_for,
                                              always_trust,
                                              parent, error);
        } else {
            result =
                libbalsa_rfc2440_sign_encrypt(mime_part,
                                              NULL,
                                              encrypt_for,
                                              always_trust,
                                              parent, error);
        }
        g_list_foreach(encrypt_for, (GFunc) g_free, NULL);
        g_list_free(encrypt_for);
        if (!result) {
            return LIBBALSA_MESSAGE_ENCRYPT_ERROR;
        }
    }
    break;

    default:
        g_assert_not_reached();
    }

    return LIBBALSA_MESSAGE_CREATE_OK;
}


/* handle rfc2633 and rfc3156 signing and/or encryption of a message */
static LibBalsaMsgCreateResult
do_multipart_crypto(LibBalsaMessage *message,
                    GMimeObject    **mime_root,
                    GtkWindow       *parent,
                    GError         **error)
{
    gpgme_protocol_t protocol;
    gboolean always_trust;

    /* check if we shall do any protection */
    if (!(message->gpg_mode & LIBBALSA_PROTECT_MODE)) {
        return LIBBALSA_MESSAGE_CREATE_OK;
    }

    /* check which protocol should be used */
    if (message->gpg_mode & LIBBALSA_PROTECT_RFC3156) {
        protocol = GPGME_PROTOCOL_OpenPGP;
    }
#   ifdef HAVE_SMIME
    else if (message->gpg_mode & LIBBALSA_PROTECT_SMIMEV3) {
        protocol = GPGME_PROTOCOL_CMS;
    }
#   endif
    else if (message->gpg_mode & LIBBALSA_PROTECT_OPENPGP) {
        return LIBBALSA_MESSAGE_CREATE_OK;  /* already done... */
    } else {
        return LIBBALSA_MESSAGE_ENCRYPT_ERROR;  /* hmmm.... */

    }
    always_trust = (message->gpg_mode & LIBBALSA_PROTECT_ALWAYS_TRUST) != 0;
    /* sign and/or encrypt */
    switch (message->gpg_mode & LIBBALSA_PROTECT_MODE) {
    case LIBBALSA_PROTECT_SIGN:       /* sign message */
        if (!libbalsa_sign_mime_object(mime_root,
                                       lb_send_from(message),
                                       protocol, parent, error)) {
            return LIBBALSA_MESSAGE_SIGN_ERROR;
        }
        break;

    case LIBBALSA_PROTECT_ENCRYPT:
    case LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_SIGN:
    {
        GList *encrypt_for = NULL;
        gboolean success;

        /* build a list containing the addresses of all to:, cc:
           and the from: address. Note: don't add bcc: addresses
           as they would be visible in the encrypted block. */
        encrypt_for = get_mailbox_names(encrypt_for,
                                        message->headers->to_list);
        encrypt_for = get_mailbox_names(encrypt_for,
                                        message->headers->cc_list);
        encrypt_for = g_list_append(encrypt_for,
                                    g_strdup(lb_send_from(message)));
        if (message->headers->bcc_list
            && (internet_address_list_length(message->headers->
                                             bcc_list) > 0)) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _(
                                     "This message will not be encrypted for the BCC: recipient(s)."));
        }

        if (message->gpg_mode & LIBBALSA_PROTECT_SIGN) {
            success =
                libbalsa_sign_encrypt_mime_object(mime_root,
                                                  lb_send_from(message),
                                                  encrypt_for, protocol,
                                                  always_trust, parent,
                                                  error);
        } else {
            success =
                libbalsa_encrypt_mime_object(mime_root, encrypt_for,
                                             protocol, always_trust,
                                             parent, error);
        }
        g_list_free(encrypt_for);

        if (!success) {
            return LIBBALSA_MESSAGE_ENCRYPT_ERROR;
        }
        break;
    }

    default:
        g_error("illegal gpg_mode %d (" __FILE__ " line %d)",
                message->gpg_mode, __LINE__);
    }

    return LIBBALSA_MESSAGE_CREATE_OK;
}


#endif
