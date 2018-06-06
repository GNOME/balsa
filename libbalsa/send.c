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

#include <math.h>
#include <stdlib.h>
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
#include "identity.h"

#include "libbalsa-progress.h"

#ifdef HAVE_GPGME
#include "libbalsa-gpgme.h"
#endif

#include <glib/gi18n.h>

typedef struct _MessageQueueItem MessageQueueItem;
typedef struct _SendMessageInfo SendMessageInfo;

struct _MessageQueueItem {
	SendMessageInfo *smsg_info;
    LibBalsaMessage *orig;
    GMimeStream *stream;
    NetClientSmtpMessage *smtp_msg;
};

struct _SendMessageInfo {
	LibBalsaSmtpServer *smtp_server;
    LibBalsaMailbox *outbox;
    NetClientSmtp *session;
    LibBalsaFccboxFinder finder;
    GList *items;               /* of MessageQueueItem */
    gboolean no_dialog;
    gchar *progress_id;
    gint64 total_size;
    gint64 total_sent;
    gint last_report;
    guint msg_count;
    guint curr_msg;
};


typedef struct _SendQueueInfo SendQueueInfo;

struct _SendQueueInfo {
	LibBalsaMailbox      *outbox;
	LibBalsaFccboxFinder  finder;
	GtkWindow            *parent;
};


static GMutex send_messages_lock;
static gint sending_threads = 0; /* how many sending threads are active, access via g_atomic_* */
static GSourceFunc auto_send_cb = NULL;
static gboolean send_mail_auto = FALSE;
static guint send_mail_time = 0U;
static guint send_mail_timer_id = 0U;
static gint retrigger_send = 0;		/* # of messages added to outbox while the smtp server was locked, access via g_atomic_* */

static ProgressDialog send_progress_dialog;


/* end of state variables section */

/* Stop the the auto-send timer, and start it if start and send_mail_auto are TRUE, and a callback is defined.  In order to catch
 * weird cases (e.g. user unflags or undeletes a message in the outbox) we ignore if the outbox is actually empty or does not
 * contain any ready-to-send messages, and start the timer anyway.  The overhead of the callback just doing nothing should be
 * insignificant. */
static void
update_send_timer(gboolean start)
{
    if (send_mail_timer_id != 0U) {
        g_source_remove(send_mail_timer_id);
        send_mail_timer_id = 0U;
    }

    if (start && send_mail_auto && (auto_send_cb != NULL)) {
    	send_mail_timer_id = g_timeout_add_seconds(send_mail_time, auto_send_cb, NULL);
    }
}

void
libbalsa_auto_send_init(GSourceFunc auto_send_handler)
{
	auto_send_cb = auto_send_handler;
	update_send_timer(FALSE);
}

void
libbalsa_auto_send_config(gboolean enable,
						  guint    timeout_minutes)
{
	send_mail_auto = enable;
	send_mail_time = timeout_minutes * 60U;
	update_send_timer(enable);
}


gboolean
libbalsa_is_sending_mail(void)
{
    return g_atomic_int_get(&sending_threads) > 0;
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
    while ((g_atomic_int_get(&sending_threads) > 0) && (sleep_time < max_time)) {
        while (gtk_events_pending()) {
            gtk_main_iteration_do(FALSE);
        }
        nanosleep(&req, NULL);
        sleep_time += DOZE_LENGTH;
    }
}


static MessageQueueItem *
msg_queue_item_new(SendMessageInfo *smi)
{
    MessageQueueItem *mqi;

    mqi = g_new0(MessageQueueItem, 1U);
    mqi->smsg_info = smi;
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
send_message_info_new(LibBalsaSmtpServer   *smtp_server,
					  LibBalsaMailbox      *outbox,
					  LibBalsaFccboxFinder  finder,
                      NetClientSmtp        *session)
{
    SendMessageInfo *smi;

    smi = g_new0(SendMessageInfo, 1);
    smi->session = session;
    smi->outbox = g_object_ref(outbox);
    smi->finder = finder;
    smi->smtp_server = g_object_ref(smtp_server);
    smi->progress_id = g_strdup_printf(_("SMTP server %s"), libbalsa_smtp_server_get_name(smtp_server));
    return smi;
}


static void
send_message_info_destroy(SendMessageInfo *smi)
{
	if (smi->outbox != NULL) {
		g_object_unref(G_OBJECT(smi->outbox));
	}
    if (smi->session != NULL) {
        g_object_unref(G_OBJECT(smi->session));
    }
    if (smi->items != NULL) {
        g_list_free_full(smi->items, (GDestroyNotify) msg_queue_item_destroy);
    }
    if (smi->progress_id != NULL) {
    	g_free(smi->progress_id);
    }
    g_object_unref(smi->smtp_server);
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

static GMimePart *lb_create_pubkey_part(LibBalsaMessage  *message,
				      	  	  	  	  	GtkWindow        *parent,
										GError          **error);
#endif

static gpointer balsa_send_message_real(SendMessageInfo *info);
static LibBalsaMsgCreateResult libbalsa_message_create_mime_message(LibBalsaMessage *message,
                                                                    gboolean         flow,
                                                                    gboolean         postponing,
                                                                    GError         **error);
static LibBalsaMsgCreateResult libbalsa_create_msg(LibBalsaMessage *message,
                                                   gboolean         flow,
                                                   GError         **error);
static LibBalsaMsgCreateResult libbalsa_fill_msg_queue_item_from_queu(LibBalsaMessage  *message,
                                                                      MessageQueueItem *mqi);

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
        /* Use the supplied mime type */
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

static LibBalsaMsgCreateResult
lbs_message_queue_real(LibBalsaMessage    *message,
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


LibBalsaMsgCreateResult
libbalsa_message_queue(LibBalsaMessage    *message,
                       LibBalsaMailbox    *outbox,
                       LibBalsaMailbox    *fccbox,
                       LibBalsaSmtpServer *smtp_server,
                       gboolean            flow,
                       GError            **error)
{
	LibBalsaMsgCreateResult result;

    update_send_timer(FALSE);

	result = lbs_message_queue_real(message, outbox, fccbox, smtp_server, flow, error);

    if (g_atomic_int_get(&sending_threads) == 0) {
    	update_send_timer(TRUE);
    }

	return result;
}


/* libbalsa_message_send:
   send the given messsage (if any, it can be NULL) and all the messages
   in given outbox.
 */
static void lbs_process_queue(LibBalsaMailbox     *outbox,
							  LibBalsaFccboxFinder finder,
							  LibBalsaSmtpServer  *smtp_server,
							  GtkWindow           *parent);
static gboolean lbs_process_queue_real(LibBalsaSmtpServer *smtp_server,
								   	   SendQueueInfo      *send_info);


LibBalsaMsgCreateResult
libbalsa_message_send(LibBalsaMessage     *message,
                      LibBalsaMailbox     *outbox,
                      LibBalsaMailbox     *fccbox,
                      LibBalsaFccboxFinder finder,
                      LibBalsaSmtpServer  *smtp_server,
					  gboolean			   show_progress,
                      GtkWindow           *parent,
                      gboolean             flow,
                      GError             **error)
{
    LibBalsaMsgCreateResult result = LIBBALSA_MESSAGE_CREATE_OK;

    g_return_val_if_fail(smtp_server != NULL,
                         LIBBALSA_MESSAGE_SERVER_ERROR);

    if (message != NULL) {
        update_send_timer(FALSE);

        result = lbs_message_queue_real(message, outbox, fccbox,
                                        smtp_server, flow, error);

        if (result == LIBBALSA_MESSAGE_CREATE_OK) {
        	if (libbalsa_smtp_server_trylock(smtp_server)) {
        		lbs_process_queue(outbox, finder, smtp_server, show_progress ? parent : NULL);
        	} else {
        		g_atomic_int_inc(&retrigger_send);
        	}
        } else if (g_atomic_int_get(&sending_threads) == 0) {
        	update_send_timer(TRUE);
        } else {
        	/* nothing to do */
        }
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
	SendMessageInfo *smi = mqi->smsg_info;

    read_res = g_mime_stream_read(mqi->stream, buffer, count);
    if (!smi->no_dialog && (smi->total_size > 0) && (read_res > 0)) {
        gdouble fraction;
        gint ipercent;

    	smi->total_sent += read_res;
    	fraction = (gdouble) smi->total_sent / (gdouble) smi->total_size;
    	g_debug("%s: s=%lu t=%lu %g", __func__, (unsigned long) smi->total_sent, (unsigned long) smi->total_size, fraction);
    	if (fraction > 1.0) {
            fraction = 1.0;
        }
    	ipercent = (gint) (100.0 * (fraction + 0.5));
    	if (!smi->no_dialog && (ipercent > smi->last_report)) {
    		libbalsa_progress_dialog_update(&send_progress_dialog, smi->progress_id, FALSE, fraction,
    			_("Message %u of %u"), smi->curr_msg, smi->msg_count);
    		smi->last_report = ipercent;
        }
    }
    return read_res;
}


static void
lbs_check_reachable_cb(GObject  *object,
					   gboolean  can_reach,
					   gpointer  cb_data)
{
    LibBalsaSmtpServer *smtp_server = LIBBALSA_SMTP_SERVER(object);
	SendQueueInfo *send_info = (SendQueueInfo *) cb_data;
	gboolean thread_started = FALSE;

	if (can_reach) {
		thread_started = lbs_process_queue_real(smtp_server, send_info);
	} else {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Cannot reach SMTP server %s (%s), any queued message will remain in %s."),
							 libbalsa_smtp_server_get_name(smtp_server),
							 LIBBALSA_SERVER(smtp_server)->host,
							 send_info->outbox->name);
	}

	if (!thread_started) {
		/* if the thread has been started, it will take care of unlocking the server and re-starting the timer */
		libbalsa_smtp_server_unlock(smtp_server);
		update_send_timer(TRUE);
	}

	g_object_unref(send_info->outbox);
	if (send_info->parent != NULL) {
		g_object_unref(send_info->parent);
	}
	g_free(send_info);
}


/* note: the following function is called with the passed smtp server being locked
 * parent != NULL indicates that the progress dialogue shall be shown */
static void
lbs_process_queue(LibBalsaMailbox      *outbox,
    			  LibBalsaFccboxFinder  finder,
				  LibBalsaSmtpServer   *smtp_server,
				  GtkWindow            *parent)
{
	SendQueueInfo *send_info;

	send_info = g_new(SendQueueInfo, 1U);
	send_info->outbox = g_object_ref(outbox);
	send_info->finder = finder;
	if (parent != NULL) {
		send_info->parent = g_object_ref(parent);
	} else {
		send_info->parent = NULL;
	}
	libbalsa_server_test_can_reach(LIBBALSA_SERVER(smtp_server), lbs_check_reachable_cb, send_info);
}


static void
lbs_process_queue_msg(guint 		   msgno,
					  SendMessageInfo *send_message_info)
{
	MessageQueueItem* new_message;
	LibBalsaMessage* msg;
	const gchar* smtp_server_name;
	LibBalsaMsgCreateResult created;
        const gchar *dsn_header;

	/* Skip this message if it either FLAGGED or DELETED: */
	if (!libbalsa_mailbox_msgno_has_flags(send_message_info->outbox, msgno, 0,
		(LIBBALSA_MESSAGE_FLAG_FLAGGED | LIBBALSA_MESSAGE_FLAG_DELETED))) {
		return;
	}

	msg = libbalsa_mailbox_get_message(send_message_info->outbox, msgno);
	if (!msg) {
		/* error? */
		return;
	}

	/* check the smtp server */
	libbalsa_message_body_ref(msg, TRUE, TRUE);
	smtp_server_name = libbalsa_message_get_user_header(msg, "X-Balsa-SmtpServer");
	if (!smtp_server_name) {
		smtp_server_name = libbalsa_smtp_server_get_name(NULL);
	}
	if (strcmp(smtp_server_name, libbalsa_smtp_server_get_name(send_message_info->smtp_server)) != 0) {
		libbalsa_message_body_unref(msg);
		g_object_unref(msg);
		return;
	}

        dsn_header = libbalsa_message_get_user_header(msg, "X-Balsa-DSN");
        msg->request_dsn = dsn_header != NULL ? atoi(dsn_header) != 0 : FALSE;
	new_message = msg_queue_item_new(send_message_info);
	created = libbalsa_fill_msg_queue_item_from_queu(msg, new_message);
	libbalsa_message_body_unref(msg);

	if (created != LIBBALSA_MESSAGE_CREATE_OK) {
		msg_queue_item_destroy(new_message);
	} else {
		const InternetAddress* ia;
		const gchar* mailbox;

		libbalsa_message_change_flags(msg, LIBBALSA_MESSAGE_FLAG_FLAGGED, 0);
		send_message_info->items = g_list_prepend(send_message_info->items, new_message);
		new_message->smtp_msg = net_client_smtp_msg_new(send_message_data_cb, new_message);
		if (msg->request_dsn) {
			net_client_smtp_msg_set_dsn_opts(new_message->smtp_msg, msg->message_id, FALSE);
		}

		/* Add the sender info */
		if (msg->headers->from && (ia = internet_address_list_get_address(msg->headers->from, 0))) {
			while (ia != NULL && INTERNET_ADDRESS_IS_GROUP(ia)) {
				ia = internet_address_list_get_address(INTERNET_ADDRESS_GROUP(
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
		 * on the large side since some message headers may be altered during the transfer. */
		send_message_info->total_size += g_mime_stream_length(new_message->stream);
		send_message_info->msg_count++;
	}
	g_object_unref(msg);
}


static NetClientSmtp *
lbs_process_queue_init_session(LibBalsaServer* server)
{
	NetClientSmtp* session;

	if (server->security == NET_CLIENT_CRYPT_ENCRYPTED) {
		session = net_client_smtp_new(server->host, 465U, server->security);
	} else {
		// FIXME - submission (587) is the standard, but most isp's use 25...
		session = net_client_smtp_new(server->host, 587U, server->security);
	}

	/* load client certificate if configured */
	if (server->client_cert) {
		GError* error = NULL;

		g_signal_connect(G_OBJECT(session), "cert-pass", G_CALLBACK(libbalsa_server_get_cert_pass), server);
		if (!net_client_set_cert_from_file(NET_CLIENT(session), server->cert_file, &error)) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot load certificate file %s: %s"), server->cert_file,
				error->message);
			g_error_free(error);
			g_object_unref(session);
			session = NULL;
		}
	} else {
		/* connect signals */
		g_signal_connect(G_OBJECT(session), "cert-check", G_CALLBACK(libbalsa_server_check_cert), session);
		g_signal_connect(G_OBJECT(session), "auth", G_CALLBACK(libbalsa_server_get_auth), server);
	}

	return session;
}


/* libbalsa_process_queue:
   treats given mailbox as a set of messages to send. Loads them up and
   launches sending thread/routine.
   NOTE that we do not close outbox after reading. send_real/thread message
   handler does that.
   Returns if the thread has been launched

   Note: the function is always called with the respective SMTP server being locked, so unlock if we don't start the sending
         thread
 */
static gboolean
lbs_process_queue_real(LibBalsaSmtpServer *smtp_server, SendQueueInfo *send_info)
{
	gboolean thread_started = FALSE;

    g_mutex_lock(&send_messages_lock);

    if (libbalsa_mailbox_open(send_info->outbox, NULL)) {
    	NetClientSmtp *session;

    	/* create the SMTP session */
    	session = lbs_process_queue_init_session(LIBBALSA_SERVER(smtp_server));
    	if (session != NULL) {
        	SendMessageInfo *send_message_info;
        	guint msgno;

    		send_message_info = send_message_info_new(smtp_server, send_info->outbox, send_info->finder, session);

    		for (msgno = libbalsa_mailbox_total_messages(send_info->outbox); msgno > 0U; msgno--) {
    			lbs_process_queue_msg(msgno, send_message_info);
    		}

    		/* launch the thread for sending the messages only if we collected any */
    		if (send_message_info->items != NULL) {
    			GThread *send_mail;

    			if (send_info->parent != NULL) {
    				libbalsa_progress_dialog_ensure(&send_progress_dialog, _("Sending Mail"), send_info->parent,
    					send_message_info->progress_id);
    				send_message_info->no_dialog = FALSE;
    			} else {
    				send_message_info->no_dialog = TRUE;
    			}
    			g_atomic_int_inc(&sending_threads);
    			send_mail = g_thread_new("balsa_send_message_real", (GThreadFunc) balsa_send_message_real, send_message_info);
    			g_thread_unref(send_mail);
    			thread_started = TRUE;
    		} else {
    			send_message_info_destroy(send_message_info);
    		}
    	}

        if (!thread_started) {
			libbalsa_mailbox_close(send_info->outbox, TRUE);
        }
    }

    g_mutex_unlock(&send_messages_lock);

    return thread_started;
}


void
libbalsa_process_queue(LibBalsaMailbox     *outbox,
                       LibBalsaFccboxFinder finder,
                       GSList              *smtp_servers,
					   gboolean				show_progress,
                       GtkWindow           *parent)
{
	if (libbalsa_mailbox_open(outbox, NULL)) {
		guint msgno;
		guint pending = 0U;

		update_send_timer(FALSE);

		for (msgno = libbalsa_mailbox_total_messages(outbox); msgno > 0U; msgno--) {
			if (libbalsa_mailbox_msgno_has_flags(outbox, msgno, 0,
				(LIBBALSA_MESSAGE_FLAG_FLAGGED | LIBBALSA_MESSAGE_FLAG_DELETED))) {
				pending++;
			}
		}

		if (pending > 0U) {
			for (; smtp_servers; smtp_servers = smtp_servers->next) {
				LibBalsaSmtpServer *smtp_server = LIBBALSA_SMTP_SERVER(smtp_servers->data);

				if (libbalsa_smtp_server_trylock(smtp_server)) {
					lbs_process_queue(outbox, finder, smtp_server, show_progress ? parent : NULL);
				}
			}
		} else {
			update_send_timer(TRUE);
			g_message("%s: no messages pending", __func__);
		}

		libbalsa_mailbox_close(outbox, FALSE);
	}
}


/* balsa_send_message_real:
   does the actual message sending.
   This function may be called as a thread.
   Also, structure info should be freed before exiting.
 */

static gboolean
balsa_send_message_real_idle_cb(LibBalsaMailbox *outbox)
{
	if (g_atomic_int_and(&retrigger_send, 0U) != 0U) {
		g_idle_add(auto_send_cb, GINT_TO_POINTER(1));
	} else {
	    update_send_timer(TRUE);
	}

    libbalsa_mailbox_close(outbox, TRUE);
    g_object_unref(outbox);

    return FALSE;
}

#define ERROR_IS_TRANSIENT(error) \
    (g_error_matches((error), NET_CLIENT_ERROR_QUARK, NET_CLIENT_ERROR_CONNECTION_LOST) || \
     g_error_matches((error), NET_CLIENT_SMTP_ERROR_QUARK, NET_CLIENT_ERROR_SMTP_TRANSIENT))


static inline void
balsa_send_message_success(MessageQueueItem *mqi,
						   SendMessageInfo  *info)
{
	/* sending message successful */
	if ((mqi->orig != NULL) && (mqi->orig->mailbox != NULL)) {
		gboolean remove = TRUE;
		const gchar *fccurl = libbalsa_message_get_user_header(mqi->orig, "X-Balsa-Fcc");

		if (fccurl != NULL) {
			LibBalsaMailbox *fccbox = info->finder(fccurl);
			GError *err = NULL;

			if (!info->no_dialog) {
				libbalsa_progress_dialog_update(&send_progress_dialog, info->progress_id, FALSE, NAN,
					_("Save message in %s…"), fccbox->name);
			}

			libbalsa_message_change_flags(mqi->orig, 0, LIBBALSA_MESSAGE_FLAG_NEW | LIBBALSA_MESSAGE_FLAG_FLAGGED);
			libbalsa_mailbox_sync_storage(mqi->orig->mailbox, FALSE);
			remove = libbalsa_message_copy(mqi->orig, fccbox, &err);
			if (!remove) {
				libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Saving sent message to %s failed: %s"), fccbox->name,
					err ? err->message : "?");
				g_clear_error(&err);
			}
		}

		/* If copy failed, mark the message again as flagged - otherwise it will get
		 * resent again. And again, and again... */
		libbalsa_message_change_flags(mqi->orig, remove ? LIBBALSA_MESSAGE_FLAG_DELETED : LIBBALSA_MESSAGE_FLAG_FLAGGED, 0);
	}
}

static inline void
balsa_send_message_error(MessageQueueItem *mqi,
						 GError           *error)
{
	/* sending message failed */
	if ((mqi->orig != NULL) && (mqi->orig->mailbox != NULL)) {
		if (ERROR_IS_TRANSIENT(error)) {
			/* Mark it as:
			 * - neither flagged nor deleted, so it can be resent later
			 *   without changing flags. */
			libbalsa_message_change_flags(mqi->orig, 0, LIBBALSA_MESSAGE_FLAG_FLAGGED | LIBBALSA_MESSAGE_FLAG_DELETED);
		} else {
			/* Mark it as:
			 * - flagged, so it will not be sent again until the error is fixed
			 *   and the user manually clears the flag;
			 * - undeleted, in case it was already deleted. */
			libbalsa_message_change_flags(mqi->orig, LIBBALSA_MESSAGE_FLAG_FLAGGED, LIBBALSA_MESSAGE_FLAG_DELETED);
		}
	}
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Sending message failed: %s\nMessage left in your outbox."),
		error->message);
}

static gpointer
balsa_send_message_real(SendMessageInfo *info)
{
    gboolean result;
    GError *error = NULL;
    gchar *greeting = NULL;

    g_debug("%s: starting", __func__);

    /* connect the SMTP server */
    if (!info->no_dialog) {
		libbalsa_progress_dialog_update(&send_progress_dialog, info->progress_id, FALSE, INFINITY,
			_("Connecting %s…"), net_client_get_host(NET_CLIENT(info->session)));
    }
    result = net_client_smtp_connect(info->session, &greeting, &error);
    g_debug("%s: connect = %d [%p]: '%s'", __func__, result, info->items, greeting);
    g_free(greeting);
    if (result) {
        GList *this_msg;

        if (!info->no_dialog) {
    		libbalsa_progress_dialog_update(&send_progress_dialog, info->progress_id, FALSE, 0.0,
    			_("Connected to %s"), net_client_get_host(NET_CLIENT(info->session)));
        }

        for (this_msg = info->items; this_msg != NULL; this_msg = this_msg->next) {
            MessageQueueItem *mqi = (MessageQueueItem *) this_msg->data;
            gboolean send_res;

            info->curr_msg++;
            g_debug("%s: %u/%u mqi = %p", __func__, info->msg_count, info->curr_msg, mqi);
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
				balsa_send_message_success(mqi, info);
            } else {
                /* sending message failed */
				balsa_send_message_error(mqi, error);
                g_clear_error(&error);
                result = FALSE;
            }

            /* free data */
            g_mutex_unlock(&send_messages_lock);
        }
    } else {
        if (ERROR_IS_TRANSIENT(error)) {
            GList *this_msg;

            /* Mark all messages as neither flagged nor deleted, so they can be resent later
             * without changing flags. */
            for (this_msg = info->items; this_msg != NULL; this_msg = this_msg->next) {
                MessageQueueItem *mqi = (MessageQueueItem *) this_msg->data;

                if ((mqi->orig != NULL) && (mqi->orig->mailbox != NULL)) {
                    libbalsa_message_change_flags(mqi->orig,
                                                  0,
                                                  LIBBALSA_MESSAGE_FLAG_FLAGGED |
                                                  LIBBALSA_MESSAGE_FLAG_DELETED);
                }
            }
        }
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                             _("Connecting SMTP server %s (%s) failed: %s"),
                             libbalsa_smtp_server_get_name(info->smtp_server),
                             net_client_get_host(NET_CLIENT(info->session)),
                             error->message);
        g_error_free(error);
    }

    /* close outbox in an idle callback, as it might affect the display */
    g_idle_add((GSourceFunc) balsa_send_message_real_idle_cb, g_object_ref(info->outbox));

    /* finalise the SMTP session (which may be slow) */
    g_object_unref(G_OBJECT(info->session));
    info->session = NULL;

    /* clean up */
    if (!info->no_dialog) {
		libbalsa_progress_dialog_update(&send_progress_dialog, info->progress_id, TRUE, 1.0, _("Finished"));
    } else if (result) {
    	libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
    		ngettext("Transmitted %u message to %s", "Transmitted %u messages to %s", info->msg_count),
			info->msg_count, libbalsa_smtp_server_get_name(info->smtp_server));
    } else {
    	/* no dialogue and error: information already displayed, nothing to do */
    }
	libbalsa_smtp_server_unlock(info->smtp_server);
    send_message_info_destroy(info);

    (void) g_atomic_int_dec_and_test(&sending_threads);

    return NULL;
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
    gboolean attach_pubkey = FALSE;
#ifdef HAVE_GPGME
    GtkWindow *parent = g_object_get_data(G_OBJECT(message), "parent-window");
#endif

#ifdef HAVE_GPGME
    /* attach the public key only if we send the message, not if we just postpone it */
    if (!postponing && message->att_pubkey && ((message->gpg_mode & LIBBALSA_PROTECT_PROTOCOL) != 0)) {
    	attach_pubkey = TRUE;
    }
#endif

    body = message->body_list;
    if ((body != NULL) && ((body->next != NULL) || attach_pubkey)) {
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
                GMimeMessage *mime_msg;
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
                mime_msg = g_mime_parser_construct_message(parser);
                g_object_unref(parser);
                mime_part =
                    GMIME_OBJECT(g_mime_message_part_new_with_message
                                     (mime_type[1], mime_msg));
                g_object_unref(mime_msg);
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
    if (attach_pubkey) {
    	GMimePart *pubkey_part;

    	pubkey_part = lb_create_pubkey_part(message, parent, error);
    	if (pubkey_part == NULL) {
            if (mime_root != NULL) {
                g_object_unref(G_OBJECT(mime_root));
            }
            return LIBBALSA_MESSAGE_CREATE_ERROR;
    	}
        if (mime_root != NULL) {
            g_mime_multipart_add(GMIME_MULTIPART(mime_root), GMIME_OBJECT(pubkey_part));
            g_object_unref(G_OBJECT(pubkey_part));
        } else {
            mime_root = GMIME_OBJECT(pubkey_part);
        }
    }
#endif

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
/*
 * If the identity contains a forced key ID for the passed protocol, return the key ID.  Otherwise, return the email address of the
 * "From:" address list to let GpeME automagically select the proper key.
 */
static const gchar *
lb_send_from(LibBalsaMessage  *message,
			 gpgme_protocol_t  protocol)
{
	const gchar *from_id;

	if ((protocol == GPGME_PROTOCOL_OpenPGP) &&
		(message->ident->force_gpg_key_id != NULL) &&
		(message->ident->force_gpg_key_id[0] != '\0')) {
		from_id = message->ident->force_gpg_key_id;
	} else if ((protocol == GPGME_PROTOCOL_CMS) &&
		(message->ident->force_smime_key_id != NULL) &&
		(message->ident->force_smime_key_id[0] != '\0')) {
		from_id = message->ident->force_smime_key_id;
	} else {
		InternetAddress *ia = internet_address_list_get_address(message->headers->from, 0);

		while (INTERNET_ADDRESS_IS_GROUP(ia)) {
			ia = internet_address_list_get_address(((InternetAddressGroup *) ia)->members, 0);
		}
		from_id = ((InternetAddressMailbox *) ia)->addr;
	}

    return from_id;
}


static GMimePart *
lb_create_pubkey_part(LibBalsaMessage  *message,
				      GtkWindow        *parent,
				      GError          **error)
{
	const gchar *key_id;
	gchar *keybuf;
	GMimePart *mime_part = NULL;

	key_id = lb_send_from(message, GPGME_PROTOCOL_OpenPGP);
	keybuf = libbalsa_gpgme_get_pubkey(GPGME_PROTOCOL_OpenPGP, key_id, parent, error);
	if (keybuf != NULL) {
	    GMimeStream *stream;
	    GMimeDataWrapper *wrapper;
	    gchar *filename;

	    mime_part = g_mime_part_new_with_type("application", "pgp-keys");
	    filename = g_strconcat(key_id, ".asc", NULL);
		g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part), "name", filename);
		g_mime_object_set_disposition(GMIME_OBJECT(mime_part), GMIME_DISPOSITION_ATTACHMENT);
	    g_mime_object_set_content_disposition_parameter(GMIME_OBJECT(mime_part), "filename", filename);
		g_free(filename);
	    g_mime_part_set_content_encoding(mime_part, GMIME_CONTENT_ENCODING_7BIT);
	    stream = g_mime_stream_mem_new();
	    g_mime_stream_write(stream, keybuf, strlen(keybuf));
	    g_free(keybuf);
	    wrapper = g_mime_data_wrapper_new();
	    g_mime_data_wrapper_set_stream(wrapper, stream);
	    g_object_unref(stream);
	    g_mime_part_set_content_object(mime_part, wrapper);
	    g_object_unref(wrapper);
	}

	return mime_part;
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
                                           lb_send_from(message, GPGME_PROTOCOL_OpenPGP),
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
                                              lb_send_from(message, GPGME_PROTOCOL_OpenPGP),
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
    } else if (message->gpg_mode & LIBBALSA_PROTECT_SMIMEV3) {
        protocol = GPGME_PROTOCOL_CMS;
    } else if (message->gpg_mode & LIBBALSA_PROTECT_OPENPGP) {
        return LIBBALSA_MESSAGE_CREATE_OK;  /* already done... */
    } else {
        return LIBBALSA_MESSAGE_ENCRYPT_ERROR;  /* hmmm.... */
    }
    always_trust = (message->gpg_mode & LIBBALSA_PROTECT_ALWAYS_TRUST) != 0;
    /* sign and/or encrypt */
    switch (message->gpg_mode & LIBBALSA_PROTECT_MODE) {
    case LIBBALSA_PROTECT_SIGN:       /* sign message */
        if (!libbalsa_sign_mime_object(mime_root,
                                       lb_send_from(message, protocol),
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
                                    g_strdup(lb_send_from(message, protocol)));
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
                                                  lb_send_from(message, protocol),
                                                  encrypt_for, protocol,
                                                  always_trust, parent,
                                                  error);
        } else {
            success =
                libbalsa_encrypt_mime_object(mime_root, encrypt_for,
                                             protocol, always_trust,
                                             parent, error);
        }
        g_list_free_full(encrypt_for, (GDestroyNotify) g_free);

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
