/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"

#define _BSD_SOURCE    1
#define _ISOC99_SOURCE 1

#include <fcntl.h>
#include <errno.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include <gnome.h>
#include <string.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "send.h"
#include "misc.h"
#include "information.h"

#if ENABLE_ESMTP
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#endif

typedef struct _MessageQueueItem MessageQueueItem;

struct _MessageQueueItem {
    LibBalsaMessage *orig;
    GMimeStream *stream;
#if !ENABLE_ESMTP
    MessageQueueItem *next_message;
#endif
    char tempfile[_POSIX_PATH_MAX];
#if !ENABLE_ESMTP
    enum {MQI_WAITING, MQI_FAILED,MQI_SENT} status;
#else
    long message_size;
    long sent;
    long acc;
    long update;
    int refcount;
#endif
};

typedef struct _SendMessageInfo SendMessageInfo;

struct _SendMessageInfo{
    LibBalsaMailbox* outbox;
#if ENABLE_ESMTP
    /* [BCS] - The smtp_session_t structure holds all the information
       needed to transfer the message to the SMTP server.  This structure
       is opaque to the application. */
    smtp_session_t session;
#endif
    gboolean debug;
};

/* Variables storing the state of the sending thread.
 * These variables are protected in MT by send_messages_lock mutex */
#if ENABLE_ESMTP
#else
static MessageQueueItem *message_queue;
#endif
static int sending_threads = 0; /* how many sending threads are active? */
/* end of state variables section */

gboolean
libbalsa_is_sending_mail(void)
{
    return sending_threads>0;
}

/* libbalsa_wait_for_sending_thread:
   wait for the sending thread but not longer than max_time seconds.
   -1 means wait indefinetely (almost).
*/
void
libbalsa_wait_for_sending_thread(gint max_time)
{
    gint sleep_time = 0;
#define DOZE_LENGTH (20*1000)
    static const struct timespec req = { 0, DOZE_LENGTH*1000 };/*nanoseconds*/

    if(max_time<0) max_time = G_MAXINT;
    else max_time *= 1000000; /* convert to microseconds */
    while(sending_threads>0 && sleep_time<max_time) {
        while(gtk_events_pending())
            gtk_main_iteration_do(FALSE);
        nanosleep(&req, NULL);
        sleep_time += DOZE_LENGTH;
    }
}


static MessageQueueItem *
msg_queue_item_new(LibBalsaMessage * message)
{
    MessageQueueItem *mqi;

    mqi = g_new(MessageQueueItem, 1);
    mqi->orig = message;
#if !ENABLE_ESMTP
    mqi->next_message = NULL;
    mqi->status = MQI_WAITING;
#endif
    mqi->stream=NULL;
    mqi->tempfile[0] = '\0';
    return mqi;
}

static void
msg_queue_item_destroy(MessageQueueItem * mqi)
{
    if (*mqi->tempfile)
	unlink(mqi->tempfile);
    if (mqi->stream)
	g_mime_stream_unref(mqi->stream);
    g_free(mqi);
}

#if ENABLE_ESMTP
static SendMessageInfo *
send_message_info_new(LibBalsaMailbox* outbox, smtp_session_t session,
                      gboolean debug)
{
    SendMessageInfo *smi;

    smi=g_new(SendMessageInfo,1);
    smi->session = session;
    smi->outbox = outbox;
    smi->debug = debug;

    return smi;
}
#else
static SendMessageInfo *
send_message_info_new(LibBalsaMailbox* outbox, gboolean debug)
{
    SendMessageInfo *smi;

    smi=g_new(SendMessageInfo,1);
    smi->outbox = outbox;
    smi->debug = debug;

    return smi;
}
#endif

static void
send_message_info_destroy(SendMessageInfo *smi)
{
    g_free(smi);
}


#if HAVE_GPGME
static gint
libbalsa_create_rfc2440_buffer(LibBalsaMessageBody *body, GMimePart *mime_part);
#endif

static guint balsa_send_message_real(SendMessageInfo* info);
static LibBalsaMsgCreateResult
libbalsa_message_create_mime_message(LibBalsaMessage* message, gint encoding,
				     gboolean flow, gboolean postponing);
static LibBalsaMsgCreateResult libbalsa_create_msg(LibBalsaMessage * message,
				    gint encoding, gboolean flow);
static LibBalsaMsgCreateResult
libbalsa_fill_msg_queue_item_from_queu(LibBalsaMessage * message,
				 MessageQueueItem *mqi);

#ifdef BALSA_USE_THREADS
void balsa_send_thread(MessageQueueItem * first_message);

GtkWidget *send_progress_message = NULL;
GtkWidget *send_dialog = NULL;
GtkWidget *send_dialog_bar = NULL;

static void
send_dialog_response_cb(GtkWidget* w, gint response)
{
    if(response == GTK_RESPONSE_CLOSE)
	gtk_widget_destroy(w);
}

static void
send_dialog_destroy_cb(GtkWidget* w)
{
    send_dialog = NULL;
    send_progress_message = NULL;
    send_dialog_bar = NULL;
}
/* ensure_send_progress_dialog:
   ensures that there is send_dialog available.
*/
static void
ensure_send_progress_dialog()
{
    GtkWidget* label;
    if(send_dialog) return;

    send_dialog = gtk_dialog_new_with_buttons(_("Sending Mail..."), 
                                              NULL,
                                              GTK_DIALOG_DESTROY_WITH_PARENT,
                                              _("_Hide"), 
                                              GTK_RESPONSE_CLOSE,
                                              NULL);
    gtk_window_set_wmclass(GTK_WINDOW(send_dialog), "send_dialog", "Balsa");
    label = gtk_label_new(_("Sending Mail..."));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(send_dialog)->vbox),
		       label, FALSE, FALSE, 0);

    send_progress_message = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(send_dialog)->vbox),
		       send_progress_message, FALSE, FALSE, 0);

    send_dialog_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(send_dialog)->vbox),
		       send_dialog_bar, FALSE, FALSE, 0);

    gtk_widget_show_all(send_dialog);
    g_signal_connect(G_OBJECT(send_dialog), "response", 
		     G_CALLBACK(send_dialog_response_cb), NULL);
    g_signal_connect(G_OBJECT(send_dialog), "destroy", 
		     G_CALLBACK(send_dialog_destroy_cb), NULL);
    /* Progress bar done */
}

/* define commands for locking and unlocking: it makes deadlock debugging
 * easier. */
#define send_lock()   pthread_mutex_lock(&send_messages_lock); 
#define send_unlock() pthread_mutex_unlock(&send_messages_lock);

#else
#define ensure_send_progress_dialog()
#define send_lock()   
#define send_unlock() 
#endif


static GMimePart *
add_mime_body_plain(LibBalsaMessageBody *body, gint encoding_style, 
		    gboolean flow)
{
    GMimePart *mime_part;
    const gchar * charset;
  
    g_return_val_if_fail(body, NULL);
    
    charset=body->charset;

    g_return_val_if_fail(charset, NULL);

    if (body->mime_type) {
        /* Use the suplied mime type */
        gchar *type, *subtype;

        type = g_strdup (body->mime_type);
        if ((subtype = strchr (type, '/'))) {
            *subtype++ = 0;
            mime_part = g_mime_part_new_with_type(type, subtype);
        } else {
            mime_part = g_mime_part_new_with_type("text", "plain");
        }
        g_free (type);
    } else {
        mime_part = g_mime_part_new_with_type("text", "plain");
    }

    g_mime_part_set_content_disposition(mime_part, GMIME_DISPOSITION_INLINE);
    g_mime_part_set_encoding(mime_part, encoding_style);
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part),
					     "charset", charset);
    if (flow)
	g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part),
						 "DelSp", "Yes");
        g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part),
						 "Format", "Flowed");

    if (!charset)
	charset="us-ascii";
    if (g_ascii_strcasecmp(charset, "UTF-8")!=0 &&
	g_ascii_strcasecmp(charset, "UTF8")!=0)
    {
	GMimeStream *stream, *filter_stream;
	GMimeFilter *filter;
	GMimeDataWrapper *wrapper;

	stream = g_mime_stream_mem_new();
	filter_stream = g_mime_stream_filter_new_with_stream(stream);
	filter = g_mime_filter_charset_new("UTF-8", charset);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);

	g_mime_stream_write(filter_stream, body->buffer, strlen(body->buffer));

	wrapper = g_mime_data_wrapper_new();
	g_mime_data_wrapper_set_stream(wrapper, stream);
	g_mime_data_wrapper_set_encoding (wrapper, GMIME_PART_ENCODING_DEFAULT);
	g_mime_part_set_content_object(mime_part, wrapper);

	g_object_unref(G_OBJECT(wrapper));
	g_object_unref(G_OBJECT(filter));
	g_mime_stream_unref(filter_stream);
	g_mime_stream_unref(stream);
    } else
	g_mime_part_set_content(mime_part, body->buffer, strlen(body->buffer));

    return mime_part;
}

#if 0
/* you never know when you will need this one... */
static void dump_queue(const char*msg)
{
    MessageQueueItem *mqi = message_queue;
    printf("dumping message queue at %s:\n", msg);
    while(mqi) {
	printf("item: %p\n", mqi);
	mqi = mqi->next_message;
    }
}
#endif

/* write_remote_fcc:
   return -1 on failure, 0 on success.
*/
#if NOT_USED
static int
write_remote_fcc(LibBalsaMailbox* fccbox, HEADER* m_msg)
{
    LibBalsaServer* server = LIBBALSA_MAILBOX_REMOTE(fccbox)->server;
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(fccbox), -1);
    if(MAILBOX_CLOSED(fccbox)) {
        /* We cannot use LIBBALSA_REMOTE_MAILBOX_SERVER() here because */
        /* it will lock up when NO IMAP mailbox has been accessed since */
        /* balsa was started. This should be safe because we have already */
        /* established that fccbox is in fact an IMAP mailbox */
        if(server == (LibBalsaServer *)NULL) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
                                 _("Unable to open sentbox - could not get IMAP server information"));
            return -1;
        }
        if (!(server->passwd && *server->passwd) &&
            !(server->passwd = libbalsa_server_get_password(server, fccbox))) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
                                 "Unable to open sentbox - could not get passwords for server");
            return -1;
        }
    }
  
    /* Passwords are guaranteed to be set now */
    
    return mutt_write_fcc(LIBBALSA_MAILBOX(fccbox)->url,
                          m_msg, NULL, 0, NULL);
}
#endif

/* libbalsa_message_queue:
   places given message in the outbox. If fcc message field is set, saves
   it to fcc mailbox as well.
*/
LibBalsaMsgCreateResult
libbalsa_message_queue(LibBalsaMessage * message, LibBalsaMailbox * outbox,
		       LibBalsaMailbox * fccbox, gint encoding,
		       gboolean flow)
{
    LibBalsaMsgCreateResult result;

    g_return_val_if_fail(message, LIBBALSA_MESSAGE_CREATE_ERROR);

    if ((result = libbalsa_create_msg(message, encoding, flow)) ==
	LIBBALSA_MESSAGE_CREATE_OK) {
        libbalsa_mailbox_copy_message( message, outbox );
	if (fccbox) {
		if (LIBBALSA_IS_MAILBOX_LOCAL(fccbox) ||
		    LIBBALSA_IS_MAILBOX_IMAP(fccbox)) {
		    libbalsa_mailbox_copy_message( message, fccbox );

		}
		libbalsa_mailbox_check(fccbox);
	}
	libbalsa_mailbox_check(outbox);
    } 

    return result;
}

/* libbalsa_message_send:
   send the given messsage (if any, it can be NULL) and all the messages
   in given outbox.
*/
#if ENABLE_ESMTP
LibBalsaMsgCreateResult
libbalsa_message_send(LibBalsaMessage * message, LibBalsaMailbox * outbox,
                      LibBalsaMailbox * fccbox, gint encoding,
                      gchar * smtp_server, auth_context_t smtp_authctx,
                      gint tls_mode, gboolean flow, gboolean debug)
{
    LibBalsaMsgCreateResult result = LIBBALSA_MESSAGE_CREATE_OK;

    if (message != NULL)
        result = libbalsa_message_queue(message, outbox, fccbox, encoding,
					flow);
     if (result == LIBBALSA_MESSAGE_CREATE_OK)
	 if (!libbalsa_process_queue(outbox, smtp_server,
				     smtp_authctx, tls_mode, debug))
	     return LIBBALSA_MESSAGE_SEND_ERROR;
 
     return result;
}
#else
LibBalsaMsgCreateResult
libbalsa_message_send(LibBalsaMessage* message, LibBalsaMailbox* outbox,
		      LibBalsaMailbox* fccbox, gint encoding,
		      gboolean flow, gboolean debug)
{
    LibBalsaMsgCreateResult result = LIBBALSA_MESSAGE_CREATE_OK;

    if (message != NULL)
 	result = libbalsa_message_queue(message, outbox, fccbox, encoding,
 					flow);
    if (result == LIBBALSA_MESSAGE_CREATE_OK)
 	if (!libbalsa_process_queue(outbox, debug))
 	    return LIBBALSA_MESSAGE_SEND_ERROR;
 
    return result;
}
#endif


#if ENABLE_ESMTP
/* [BCS] - libESMTP uses a callback function to read the message from the
   application to the SMTP server.
 */
#define BUFLEN	8192

static const char *
libbalsa_message_cb (void **buf, int *len, void *arg)
{
    MessageQueueItem *current_message = arg;
    GMimeStreamMem *mem_stream = GMIME_STREAM_MEM(current_message->stream);
    char *ptr;

    if (len == NULL) {
	g_mime_stream_reset(current_message->stream);
	return NULL;
    }

    *len = g_mime_stream_length(current_message->stream)
	    - g_mime_stream_tell(current_message->stream);
    ptr = mem_stream->buffer->data
	    + g_mime_stream_tell(current_message->stream);
    g_mime_stream_seek(current_message->stream, *len, GMIME_STREAM_SEEK_CUR);

    return ptr;
}

static void
add_recipients(smtp_message_t message, smtp_message_t bcc_message, 
               GList *recipient_list, const char *header)
{
    for(;recipient_list; recipient_list = recipient_list->next) {
        LibBalsaAddress *addy = recipient_list->data;
        const gchar *mailbox;
        int i;
#ifdef LIBESMTP_ADDS_HEADERS
        const gchar *phrase = libbalsa_address_get_phrase(addy);
#endif

        for(i=0; (mailbox = libbalsa_address_get_mailbox(addy, i)); i++) {
            smtp_add_recipient (message, mailbox);
            /* XXX  - this is where to add DSN requests.  It would be
               cool if LibBalsaAddress could contain DSN options
               for a particular recipient. */
#ifdef LIBESMTP_ADDS_HEADERS
            smtp_set_header (message, "To", phrase, mailbox);
            if (bcc_message)
                smtp_set_header (bcc_message, "To", phrase, mailbox);
#endif
        }
    }
}

/* libbalsa_process_queue:
   treats given mailbox as a set of messages to send. Loads them up and
   launches sending thread/routine.
   NOTE that we do not close outbox after reading. send_real/thread message 
   handler does that.
*/
/* This version uses libESMTP. It has slightly different semantics than
   sendmail version so don't get fooled by similar variable names.
 */
gboolean
libbalsa_process_queue(LibBalsaMailbox * outbox, gchar * smtp_server,
                       auth_context_t smtp_authctx, gint tls_mode,
                       gboolean debug)
{
    MessageQueueItem *new_message;
    SendMessageInfo *send_message_info;
    GList *lista, *recip, *bcc_recip;
    LibBalsaMessage *msg;
    smtp_session_t session;
    smtp_message_t message, bcc_message;
    const gchar *phrase, *mailbox, *subject;
    long estimate;

    send_lock();

    libbalsa_mailbox_open(outbox);
    if (!outbox->total_messages) {
	libbalsa_mailbox_close(outbox);
	send_unlock();
	return TRUE;
    }
    /* We create here the progress bar */
    ensure_send_progress_dialog();

    /* Create the libESMTP session.  Loop over the out box and add the
       messages to the session. */

    /* FIXME - check for failure returns in the smtp_xxx() calls */

    session = smtp_create_session ();
    smtp_set_server (session, smtp_server);

    /* Tell libESMTP how to use the SMTP STARTTLS extension.  */
    smtp_starttls_enable (session, tls_mode);

    /* Now tell libESMTP it can use the SMTP AUTH extension.  */
    smtp_auth_set_context (session, smtp_authctx);
 
    /* At present Balsa can't handle one recipient only out of many
       failing.  Make libESMTP require all specified recipients to
       succeed before transferring a message.  */
    smtp_option_require_all_recipients (session, 1);

    for (lista = LIBBALSA_MAILBOX_LOCAL(outbox)->msg_list; 
         lista; lista = lista->next) {
        LibBalsaMsgCreateResult created;

	msg = LIBBALSA_MESSAGE(lista->data);
        if (LIBBALSA_MESSAGE_HAS_FLAG(msg,
                                      (LIBBALSA_MESSAGE_FLAG_FLAGGED |
                                       LIBBALSA_MESSAGE_FLAG_DELETED)))
            continue;

        libbalsa_message_body_ref(msg, TRUE);
	new_message = msg_queue_item_new(msg);
        created = libbalsa_fill_msg_queue_item_from_queu(msg, new_message);
        libbalsa_message_body_unref(msg);

	if (created != LIBBALSA_MESSAGE_CREATE_OK) {
	    msg_queue_item_destroy(new_message);
	} else {
	    GList * messages = g_list_prepend(NULL, msg);

            libbalsa_messages_change_flag(messages,
                                          LIBBALSA_MESSAGE_FLAG_FLAGGED,
                                          TRUE);
	    g_list_free(messages);
	    /*
	       The message needs to be filtered and the newlines converted to
	       \r\n because internally the lines foolishly terminate with the
	       Unix \n despite RFC 2822 calling for \r\n.  Furthermore RFC 822
	       states that bare \n and \r are acceptable in messages and that
	       individually they do not constitute a line termination.  This
	       requirement cannot be reconciled with storing messages with Unix
	       line terminations.  RFC 2822 relieves this situation slightly by
	       prohibiting bare \r and \n.

	       The following code cannot therefore work correctly in all
	       situations.  Furthermore it is very inefficient since it must
	       search for the \n.
	     */
	    {
		GMimeStream *mem_stream;
		GMimeStream *filter_stream;
		GMimeFilter *filter;

		mem_stream = new_message->stream;
		filter_stream =
		    g_mime_stream_filter_new_with_stream(mem_stream);
		filter =
		    g_mime_filter_crlf_new( GMIME_FILTER_CRLF_ENCODE,
					    GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
		g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream),
					 filter);
		g_object_unref(G_OBJECT(filter));
		mem_stream = g_mime_stream_mem_new();
		g_mime_stream_write_to_stream(filter_stream, mem_stream);
		g_mime_stream_unref(filter_stream);
		g_mime_stream_reset(mem_stream);
		if (new_message->stream)
		    g_mime_stream_unref(new_message->stream);
		new_message->stream = mem_stream;
	    }

	    /* If the Bcc: recipient list is present, add a additional
	       copy of the message to the session.  The recipient list
	       for the main copy of the message is generated from the
	       To: and Cc: recipient list and libESMTP is asked to strip
	       the Bcc: header.  The BCC copy of the message recipient
	       list is taken from the Bcc recipient list and the Bcc:
	       header is preserved in the message. */
	    bcc_recip = g_list_first((GList *) msg->headers->bcc_list);
	    if (!bcc_recip)
		bcc_message = NULL;
	    else
		bcc_message = smtp_add_message (session);
            new_message->refcount = bcc_recip ? 2 : 1;

	    /* Add this after the Bcc: copy. */
	    message = smtp_add_message (session);
	    if (bcc_message)
		smtp_set_header_option (message, "Bcc", Hdr_PROHIBIT);

	    smtp_message_set_application_data (message, new_message);
	    smtp_set_messagecb (message, libbalsa_message_cb, new_message);
	    if (bcc_message) {
		smtp_message_set_application_data (bcc_message, new_message);
		smtp_set_messagecb (bcc_message,
				    libbalsa_message_cb, new_message);
	    }

#define LIBESMTP_ADDS_HEADERS
#ifdef LIBESMTP_ADDS_HEADERS
	    /* XXX - The following calls to smtp_set_header() probably
		     aren't necessary since they should already be in the
		     message. */

	    smtp_set_header (message, "Date", &msg->headers->date);
	    if (bcc_message)
		smtp_set_header (bcc_message, "Date", &msg->headers->date);

	    /* RFC 2822 does not require a message to have a subject.
	               I assume this is NULL if not present */
	    subject = LIBBALSA_MESSAGE_GET_SUBJECT(msg);
	    if (subject) {
	    	smtp_set_header (message, "Subject", subject);
		if (bcc_message)
		    smtp_set_header (bcc_message, "Subject", subject);
	    }

	    /* Add the sender info */
            if (msg->headers->from) {
	        phrase = libbalsa_address_get_phrase(msg->headers->from);
	        mailbox = libbalsa_address_get_mailbox(msg->headers->from, 0);
            } else
                phrase = mailbox = "";
	    smtp_set_reverse_path (message, mailbox);
	    smtp_set_header (message, "From", phrase, mailbox);
	    if (bcc_message) {
		smtp_set_reverse_path (bcc_message, mailbox);
	        smtp_set_header (bcc_message, "From", phrase, mailbox);
	    }

	    if (msg->headers->reply_to) {
		phrase = libbalsa_address_get_phrase(msg->headers->reply_to);
		mailbox = libbalsa_address_get_mailbox(msg->headers->reply_to, 0);
		smtp_set_header (message, "Reply-To", phrase, mailbox);
		if (bcc_message)
		    smtp_set_header (bcc_message, "Reply-To", phrase, mailbox);
	    }

	    if (msg->headers->dispnotify_to) {
		phrase = libbalsa_address_get_phrase(msg->headers->dispnotify_to);
		mailbox = libbalsa_address_get_mailbox(msg->headers->dispnotify_to, 0);
		smtp_set_header (message, "Disposition-Notification-To",
				 phrase, mailbox);
		if (bcc_message)
		    smtp_set_header (bcc_message, "Disposition-Notification-To",
		    	             phrase, mailbox);
	    }
#endif

	    /* Now need to add the recipients to the message.  The main
	       copy of the message gets the To and Cc recipient list.
	       The bcc copy gets the Bcc recipients.  */

	    recip = g_list_first(msg->headers->to_list);
            add_recipients(message, bcc_message, recip, "To");
	    recip = g_list_first(msg->headers->cc_list);
            add_recipients(message, bcc_message, recip, "Cc");

            add_recipients(bcc_message, NULL, bcc_recip, "Cc");


	    /* Estimate the size of the message.  This need not be exact
	       but it's better to err on the large side since some message
	       headers may be altered during the transfer. */
	    new_message->message_size = g_mime_stream_length(new_message->stream);

	    if (new_message->message_size > 0) {
		estimate = new_message->message_size;
		estimate += 1024 - (estimate % 1024);
		smtp_size_set_estimate (message, estimate);
		if (bcc_message)
		    smtp_size_set_estimate (bcc_message, estimate);
	    }

	    /* Set up counters for the progress bar.  Update is the byte
	       count when the progress bar should be updated.  This is
	       capped around 5k so that the progress bar moves about once
	       per second on a slow line.  On small messages it is smaller
	       to allow smooth progress of the bar. */
	    new_message->update = new_message->message_size / 20;
	    if (new_message->update < 100)
	        new_message->update = 100;
	    else if (new_message->update > 5 * 1024)
	        new_message->update = 5 * 1024;
	    new_message->sent = 0;
	    new_message->acc = 0;
	}
    }

   /* At this point the session is ready to be sent.  As I've written the
      code, a new smtp session is created for every call here.  Therefore
      a new thread is always required to dispatch it.
    */

    send_message_info=send_message_info_new(outbox, session, debug);

#ifdef BALSA_USE_THREADS
    sending_threads++;
    pthread_create(&send_mail, NULL,
		   (void *) &balsa_send_message_real, send_message_info);
    /* Detach so we don't need to pthread_join
     * This means that all resources will be
     * reclaimed as soon as the thread exits
     */
    pthread_detach(send_mail);
    send_unlock();
#else				/*non-threaded code */
    balsa_send_message_real(send_message_info);
#endif
    return TRUE;
}

static void
disp_recipient_status(smtp_recipient_t recipient,
                      const char *mailbox, void *arg)
{
  const smtp_status_t *status = smtp_recipient_status (recipient);

  if(status->code != 0) {
      libbalsa_information(
                           LIBBALSA_INFORMATION_WARNING, 
                           _("Could not send the message to %s:\n"
                             "%d: %s\n"
                             "Message left in your outbox.\n"), 
                           mailbox, status->code, status->text);
      (*(int*)arg)++;
  }
}

static void
handle_successful_send (smtp_message_t message, void *be_verbose)
{
    MessageQueueItem *mqi;
    const smtp_status_t *status;
    GList * messages;

    send_lock();
    /* Get the app data and decrement the reference count.  Only delete
       structures if refcount reaches zero */
    mqi = smtp_message_get_application_data (message);
    if (mqi != NULL)
      mqi->refcount--;

    status = smtp_message_transfer_status (message);
    if (status->code / 100 == 2) {
	if (mqi != NULL && mqi->orig != NULL && mqi->refcount <= 0) {
	    if (mqi->orig->mailbox) {
		messages = g_list_prepend(NULL, mqi->orig);
		libbalsa_messages_change_flag(messages,
                                              LIBBALSA_MESSAGE_FLAG_DELETED,
                                              TRUE);
		g_list_free(messages);
	    }
	}
    } else {
	messages = g_list_prepend(NULL, mqi->orig);
        libbalsa_messages_change_flag(messages, LIBBALSA_MESSAGE_FLAG_FLAGGED,
                                      FALSE);
	g_list_free(messages);
	/* XXX - Show the poor user the status codes and message. */
        if(*(gboolean*)be_verbose) {
            int cnt = 0;
            status = smtp_reverse_path_status(message);
            if(status->code != 250) {
                libbalsa_information(
                                     LIBBALSA_INFORMATION_ERROR, 
                                     _("Relaying refused:\n"
                                       "%d: %s\n"
                                       "Message left in your outbox.\n"), 
                                     status->code, status->text);
                cnt++;
            }
            smtp_enumerate_recipients (message, disp_recipient_status, &cnt);
            if(cnt==0) { /* other error */
            libbalsa_information(
           LIBBALSA_INFORMATION_WARNING,
           _("Message submission problem, placing it into your outbox.\n"
             "System will attempt to resubmit the message until you delete it."));

            }
        }
    }
    if (mqi != NULL && mqi->refcount <= 0)
        msg_queue_item_destroy(mqi);
    send_unlock();
}

#ifdef BALSA_USE_THREADS
static void
libbalsa_smtp_event_cb (smtp_session_t session, int event_no, void *arg, ...)
{
    SendThreadMessage *threadmsg;
    MessageQueueItem *mqi;
    char buf[1024];
    va_list ap;
    const char *mailbox;
    smtp_message_t message;
    smtp_recipient_t recipient;
    const smtp_status_t *status;
    int len;
    float percent;

    va_start (ap, arg);
    switch (event_no) {
    case SMTP_EV_CONNECT:
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS,
		      _("Connected to MTA"),
		      NULL, NULL, 0);
        break;
    case SMTP_EV_MAILSTATUS:
        mailbox = va_arg (ap, const char *);
        message = va_arg (ap, smtp_message_t);
	status = smtp_reverse_path_status (message);
	snprintf (buf, sizeof buf, "%s %d <%s>", _("From:"), status->code, mailbox);
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, buf, NULL, NULL, 0);

	snprintf (buf, sizeof buf, "%s %s: %d %s", _("From"),
	          mailbox, status->code, status->text);
	libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, buf);
        break;
    case SMTP_EV_RCPTSTATUS:
        mailbox = va_arg (ap, const char *);
        recipient = va_arg (ap, smtp_recipient_t);
	status = smtp_recipient_status (recipient);
	snprintf (buf, sizeof buf, "%s %d <%s>", _("To:"), status->code, mailbox);
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, buf, NULL, NULL, 0);

	snprintf (buf, sizeof buf, "%s %s: %d %s", _("To"),
	          mailbox, status->code, status->text);
	libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, buf);
        break;
    case SMTP_EV_MESSAGEDATA:
        message = va_arg (ap, smtp_message_t);
        len = va_arg (ap, int);
        mqi = smtp_message_get_application_data (message);
        if (mqi != NULL && mqi->message_size > 0) {
	    mqi->acc += len;
	    if (mqi->acc >= mqi->update) {
		mqi->sent += mqi->acc;
		mqi->acc = 0;

		percent = (float) mqi->sent / (float) mqi->message_size;
		if(percent>1) percent = 1;
		MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, "", NULL, NULL,
			      percent);
	    }
	}
        break;
    case SMTP_EV_MESSAGESENT:
        message = va_arg (ap, smtp_message_t);
        status = smtp_message_transfer_status (message);
	snprintf (buf, sizeof buf, "%d %s", status->code, status->text);
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, buf, NULL, NULL, 0);
	libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, buf);
        /* Reset 'mqi->sent' for the next message (i.e. bcc copy) */
        mqi = smtp_message_get_application_data (message);
        if (mqi != NULL) {
	    mqi->sent = 0;
	    mqi->acc = 0;
        }
        break;
    case SMTP_EV_DISCONNECT:
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS,
		      _("Disconnected"),
		      NULL, NULL, 0);
        break;
    }
    va_end (ap);
}
#endif

#else /* ESMTP */

/* CHBM: non-esmtp version */

/* libbalsa_process_queue:
   treats given mailbox as a set of messages to send. Loads them up and
   launches sending thread/routine.
   NOTE that we do not close outbox after reading. send_real/thread message 
   handler does that.
*/
gboolean 
libbalsa_process_queue(LibBalsaMailbox* outbox, gboolean debug)
{
    MessageQueueItem *mqi, *new_message;
    SendMessageInfo *send_message_info;
    GList *lista;
    LibBalsaMessage *queu;

    /* We do messages in queue now only if where are not sending them already */

    send_lock();

#ifdef BALSA_USE_THREADS
    if (sending_threads>0) {
	send_unlock();
	return TRUE;
    }
    sending_threads++;
#endif

    ensure_send_progress_dialog();
    libbalsa_mailbox_open(outbox);
    lista = outbox->message_list;
    if (!lista) {
	libbalsa_mailbox_close(outbox);
	sending_threads--;
	send_unlock();
	return TRUE;
    }
	
    mqi = message_queue;
    while (lista != NULL) {
        LibBalsaMsgCreateResult created;

	queu = LIBBALSA_MESSAGE(lista->data);
        if (LIBBALSA_MESSAGE_HAS_FLAG(queu,
                                      (LIBBALSA_MESSAGE_FLAG_FLAGGED |
                                       LIBBALSA_MESSAGE_FLAG_DELETED)))
            continue;

        libbalsa_message_body_ref(queu, TRUE);
	new_message = msg_queue_item_new(queu);
        created = libbalsa_fill_msg_queue_item_from_queu(queu, new_message);
        libbalsa_message_body_unref(queu);
	
	if (created != LIBBALSA_MESSAGE_CREATE_OK) {
	    msg_queue_item_destroy(new_message);
	} else {
	    if (mqi)
		mqi->next_message = new_message;
	    else
		message_queue = new_message;
	    
	    mqi = new_message;
	}
	lista = lista->next;
    }

    send_message_info=send_message_info_new(outbox, debug);
    
#ifdef BALSA_USE_THREADS
    
    pthread_create(&send_mail, NULL,
		   (void *) &balsa_send_message_real, send_message_info);
    /* Detach so we don't need to pthread_join
     * This means that all resources will be
     * reclaimed as soon as the thread exits
     */
    pthread_detach(send_mail);
    
#else				/*non-threaded code */
    
    balsa_send_message_real(send_message_info);
#endif
    send_unlock();
    return TRUE;
}

static void
handle_successful_send(MessageQueueItem *mqi)
{
    if (mqi->orig->mailbox) {
	GList * messages = g_list_prepend(NULL, mqi->orig);

	libbalsa_messages_change_flag(messages, 
                                      LIBBALSA_MESSAGE_FLAG_DELETED,
                                      TRUE);
	g_list_free(messages);
    }
    mqi->status = MQI_SENT;
}

/* get_msg2send: 
   returns first waiting message on the message_queue.
*/
static MessageQueueItem* get_msg2send()
{
    MessageQueueItem* res = message_queue;
    send_lock();

    while(res && res->status != MQI_WAITING)
	res = res->next_message;

    send_unlock();
    return res;
}

#endif /* ESMTP */

#if ENABLE_ESMTP
static void
monitor_cb (const char *buf, int buflen, int writing, void *arg)
{
  FILE *fp = arg;

  if (writing == SMTP_CB_HEADERS)
    {
      fputs ("H: ", fp);
      fwrite (buf, 1, buflen, fp);
      return;
    }

 fputs (writing ? "C: " : "S: ", fp);
 fwrite (buf, 1, buflen, fp);
 if (buf[buflen - 1] != '\n')
   putc ('\n', fp);
}

/* balsa_send_message_real:
   does the actual message sending. 
   This function may be called as a thread and should therefore do
   proper gdk_threads_{enter/leave} stuff around GTK or libbalsa calls.
   Also, structure info should be freed before exiting.
*/

/* [BCS] radically different since it uses the libESMTP interface.
 */
static guint
balsa_send_message_real(SendMessageInfo* info)
{
    gboolean session_started;
#ifdef BALSA_USE_THREADS
    SendThreadMessage *threadmsg;

    /* The event callback is used to write messages to the the progress
       dialog shown when transferring a message to the SMTP server. 
       This callback is only used in MT build, we do not show any
       feedback in non-MT version.
    */
    smtp_set_eventcb (info->session, libbalsa_smtp_event_cb, NULL);
#endif

    /* Add a protocol monitor when debugging is enabled. */
    if(info->debug)
        smtp_set_monitorcb (info->session, monitor_cb, stderr, 1);

    /* Kick off the connection with the MTA.  When this returns, all
       messages with valid recipients have been sent. */
    if ( !(session_started = smtp_start_session (info->session)) ){
        char buf[256];
        int smtp_err = smtp_errno();
        switch(smtp_err) {
        case -ECONNREFUSED:
            libbalsa_information
                (LIBBALSA_INFORMATION_ERROR,
                 _("SMTP server refused connection.\n"
                   "Balsa by default uses submission service (587).\n"
                   "If you want to submit mail using relay service (25),"
                   "specify it explicitly via: \"host:smtp\".\n"
                   "Message is left in outbox."));
            break;
        case SMTP_ERR_NOTHING_TO_DO: /* silence this one? */
            break;
        default:
            libbalsa_information (LIBBALSA_INFORMATION_ERROR,
                                  _("SMTP server problem (%d): %s\n"
                                    "Message is left in outbox."),
                                  smtp_errno(),
                                  smtp_strerror (smtp_errno (), 
                                                 buf, sizeof buf));
        }
    } 
    /* We give back all the resources used and delete the sent messages */
    /* Quite a bit of status info has been gathered about messages and
       their recipients.  The following will do a libbalsa_message_delete()
       on the messages with a 2xx status recorded against them.  However
       its possible for individual recipients to fail too.  Need a way to
       report it all.  */
    /* Do we really need to grab the gdk lock? If so, we'd have to drop
     * it again before calling libbalsa_messages_change_flag().
     * gdk_threads_enter();
     */
    smtp_enumerate_messages (info->session, handle_successful_send, 
                             &session_started);

    libbalsa_mailbox_close(info->outbox);
    /*
     * gdk_flush();
     * gdk_threads_leave();
     */

    send_lock();
#ifdef BALSA_USE_THREADS
    MSGSENDTHREAD(threadmsg, MSGSENDTHREADFINISHED, "", NULL, NULL, 0);
    sending_threads--;
#endif
        
    send_unlock();
    smtp_destroy_session (info->session);
    send_message_info_destroy(info);	
    return TRUE;
}

#else /* ESMTP */

/* balsa_send_message_real:
   does the actual message sending. 
   This function may be called as a thread and should therefore do
   proper gdk_threads_{enter/leave} stuff around GTK,libbalsa or
   libmutt calls. Also, structure info should be freed before exiting.
*/

static guint
balsa_send_message_real(SendMessageInfo* info)
{
    MessageQueueItem *mqi, *next_message;
#ifdef BALSA_USE_THREADS
    SendThreadMessage *threadmsg;
    send_lock();
    if (!message_queue) {
	sending_threads--;
	send_unlock();
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADFINISHED, "", NULL, NULL, 0);
	send_message_info_destroy(info);	
	return TRUE;
    }
    send_unlock();
#else
    if(!message_queue){
	send_message_info_destroy(info);	
	return TRUE;
    }	
#endif

    while ( (mqi = get_msg2send()) != NULL) {
	GPtrArray *args = g_ptr_array_new();
	LibBalsaMessage *msg = LIBBALSA_MESSAGE(mqi->orig);
	GList *recip;
	LibBalsaAddress *addy;
	const gchar *mailbox;
	gchar *cmd;
	FILE *sendmail;
	GMimeStream *out;

	g_ptr_array_add(args, SENDMAIL);
	g_ptr_array_add(args, "--");
	recip = g_list_first((GList *) msg->headers->to_list);
	while (recip) {
	    addy = recip->data;
	    mailbox = libbalsa_address_get_mailbox(addy, 0);
	    g_ptr_array_add(args, (char*)mailbox);
	    recip = recip->next;
	}
	recip = g_list_first((GList *) msg->headers->cc_list);
	while (recip) {
	    addy = recip->data;
	    mailbox = libbalsa_address_get_mailbox(addy, 0);
	    g_ptr_array_add(args, (char*)mailbox);
	    recip = recip->next;
	}
	recip = g_list_first((GList *) msg->headers->bcc_list);
	while (recip) {
	    addy = recip->data;
	    mailbox = libbalsa_address_get_mailbox(addy, 0);
	    g_ptr_array_add(args, (char*)mailbox);
	    recip = recip->next;
	}
	g_ptr_array_add(args, NULL);
	cmd = g_strjoinv(" ", (gchar**)args->pdata);
	g_ptr_array_free(args, FALSE);
	if ( (sendmail=popen(cmd, "w")) == NULL) {
	    /* Error while sending */
	    mqi->status = MQI_FAILED;
	} else {
	    out = g_mime_stream_file_new(sendmail);
	    g_mime_stream_file_set_owner(GMIME_STREAM_FILE(out), FALSE);
	    if (g_mime_stream_write_to_stream(mqi->stream, out) == -1)
		mqi->status = MQI_FAILED;
	    g_mime_stream_unref(out);
	    if (pclose(sendmail) != 0)
		mqi->status = MQI_FAILED;
	    if (mqi->status != MQI_FAILED)
		mqi->status = MQI_SENT;
	}
	g_free(cmd);
    }

    /* We give back all the resources used and delete the sent messages */
    
    send_lock();
    mqi = message_queue;
    
    while (mqi != NULL) {
	if (mqi->status == MQI_SENT) 
	    handle_successful_send(mqi);
	next_message = mqi->next_message;
	msg_queue_item_destroy(mqi);
	mqi = next_message;
    }
    
    gdk_threads_enter();
    libbalsa_mailbox_close(info->outbox);
    gdk_flush();
    gdk_threads_leave();

    message_queue = NULL;
#ifdef BALSA_USE_THREADS
    sending_threads--;
    MSGSENDTHREAD(threadmsg, MSGSENDTHREADFINISHED, "", NULL, NULL, 0);
#endif
    send_message_info_destroy(info);	
    send_unlock();
    return TRUE;
}

#endif /* ESMTP */


static void
message_add_references(LibBalsaMessage * message, GMimeMessage * msg)
{
    /* If the message has references set, add them to the envelope */
    if (message->references != NULL) {
	GList *list = message->references;
	GString *str = g_string_new(NULL);

	do {
	    if (str->len > 0)
		g_string_append_c(str, ' ');
	    g_string_append_printf(str, "<%s>", (gchar *) list->data);
	} while ((list = list->next) != NULL);
	g_mime_message_set_header(msg, "References", str->str);
	g_string_free(str, TRUE);
    }

    if (message->in_reply_to)
	/* There's no specific header function for In-Reply-To */
	g_mime_message_set_header(msg, "In-Reply-To",
				  message->in_reply_to->data);
}

#ifdef HAVE_GPGME
static GList *
get_mailbox_names(GList *list, GList *address_list)
{
    GList *recip;
    LibBalsaAddress *addy; 
    recip = g_list_first(address_list);
    while (recip) {
	char *mailbox;
	addy = recip->data;
	mailbox = (char*)libbalsa_address_get_mailbox(addy, 0);
	list = g_list_append(list, mailbox);
	recip = recip->next;
    }

    return list;
}
#endif


static LibBalsaMsgCreateResult
libbalsa_message_create_mime_message(LibBalsaMessage* message, gint encoding,
				     gboolean flow, gboolean postponing)
{
    gchar **mime_type;
    GMimeObject *mime_root = NULL;
    GMimeMessage *mime_message;
    LibBalsaMessageBody *body;
    gchar *tmp;
    GList *list;
#ifdef HAVE_GPGME
    gboolean can_create_rfc3156 = message->body_list != NULL;
    if (postponing)
	can_create_rfc3156 = FALSE;
#endif

    body = message->body_list;
    if (body && body->next)
	mime_root=GMIME_OBJECT(g_mime_multipart_new_with_subtype(message->subtype));

    while (body) {
	GMimePart *mime_part;
	mime_part=NULL;

	if (body->filename) {
	    if (body->attach_as_extbody) {
		GMimeObject *mime_obj;
		mime_part=g_mime_part_new_with_type("message", "external-body");
		g_mime_part_set_encoding(mime_part, GMIME_PART_ENCODING_7BIT);
		mime_obj = GMIME_OBJECT(mime_part);
		if(!strncmp( body->filename, "URL", 3 )) {
		    g_mime_object_set_content_type_parameter(mime_obj,
					     "access-type", "URL");
		    g_mime_object_set_content_type_parameter(mime_obj,
					     "URL", body->filename + 4);
		} else {
		    g_mime_object_set_content_type_parameter(mime_obj,
					     "access-type", "local-file");
		    g_mime_object_set_content_type_parameter(mime_obj,
					     "name", body->filename);
		}
		g_mime_part_set_content(mime_part,
					"Note: this is _not_ the real body!\n",
					34);
	    } else {
		GMimeStream *stream;
		GMimePartEncodingType encoding;
		GMimeDataWrapper *content;
		int fd;

		if (!body->mime_type) {
		    gchar* mt = libbalsa_lookup_mime_type(body->filename);
		    mime_type = g_strsplit(mt,"/", 2);
		    g_free(mt);
		} else
		    mime_type = g_strsplit(body->mime_type, "/", 2);
		/* use BASE64 encoding for non-text mime types 
		   use 8BIT for message */
		mime_part=g_mime_part_new_with_type(mime_type[0], mime_type[1]);
		g_mime_part_set_content_disposition(mime_part, GMIME_DISPOSITION_ATTACHMENT);
		if(!strcasecmp(mime_type[0],"message") && 
		   !strcasecmp(mime_type[1],"rfc822")) {
		    g_mime_part_set_encoding(mime_part, GMIME_PART_ENCODING_8BIT);
		    g_mime_part_set_content_disposition(mime_part, GMIME_DISPOSITION_INLINE);
		} else if(strcasecmp(mime_type[0],"text") != 0)
		{
		    g_mime_part_set_encoding(mime_part, GMIME_PART_ENCODING_BASE64);
		} else {
		    /* is text, force unknown-8bit here. FIXME! PLEASE! */
		    g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_part), "charset", "unknown-8bit");
		}
		g_strfreev(mime_type);

		g_mime_part_set_filename(mime_part, body->filename);
		encoding = g_mime_part_get_encoding(mime_part);
		fd = open(body->filename, O_RDONLY);
		stream = g_mime_stream_fs_new(fd);
		content = g_mime_data_wrapper_new_with_stream(stream, encoding);
		g_mime_stream_unref(stream);
		g_mime_part_set_content_object(mime_part, content);
		g_object_unref(content);
	    }
	} else if (body->buffer) {
#ifdef HAVE_GPGME
	    /* force quoted printable encoding if only signing is requested */
	    mime_part = add_mime_body_plain(body,
		((message->gpg_mode &
		  (LIBBALSA_GPG_SIGN | LIBBALSA_GPG_ENCRYPT)
		  ) == LIBBALSA_GPG_SIGN) ? GMIME_PART_ENCODING_QUOTEDPRINTABLE :
					    encoding, flow);
#ifdef HAVE_GPGME
	    /* in '2440 mode, touch *only* the first body! */
	    if (!postponing && body == body->message->body_list &&
		message->gpg_mode > 0 &&
		(message->gpg_mode & LIBBALSA_GPG_RFC2440) != 0) {
		gint result = 
		    libbalsa_create_rfc2440_buffer(body, mime_part);

		if (result != LIBBALSA_MESSAGE_CREATE_OK) {
		    g_object_unref(G_OBJECT(mime_part));
		    g_object_unref(G_OBJECT(mime_root));
		    return LIBBALSA_MESSAGE_CREATE_ERROR;
		}
	    }
#endif
#else
	    mime_part = add_mime_body_plain(body, encoding, flow);
#endif
	}

	if (mime_root) {
	    g_mime_multipart_add_part(GMIME_MULTIPART(mime_root),
				      GMIME_OBJECT(mime_part));
	    g_object_unref(G_OBJECT(mime_part));
	} else {
	    mime_root = GMIME_OBJECT(mime_part);
	}

	body = body->next;
    }

#ifdef HAVE_GPGME
    if (can_create_rfc3156 && message->gpg_mode > 0 &&
	(message->gpg_mode & LIBBALSA_GPG_RFC2440) == 0) {
	switch (message->gpg_mode)
	    {
	    case LIBBALSA_GPG_SIGN:   /* sign message */
		{
		    if (libbalsa_sign_mime_object(&mime_root,
					   message->headers->from->address_list->data,
					   NULL)) {
		    } else {
			/* FIXME: do we have to free any resources here??? */
			return LIBBALSA_MESSAGE_SIGN_ERROR;
		    }
		    break;
		}
	    case LIBBALSA_GPG_ENCRYPT:
	    case LIBBALSA_GPG_ENCRYPT | LIBBALSA_GPG_SIGN:
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
					message->headers->from->address_list->data);
		    if (message->headers->bcc_list)
			libbalsa_information(LIBBALSA_INFORMATION_WARNING,
					     _("This message will not be encrpyted for the BCC: recipient(s)."));

		    if (message->gpg_mode & LIBBALSA_GPG_SIGN)
			success = 
			    libbalsa_sign_encrypt_mime_object(&mime_root,
					   message->headers->from->address_list->data,
					   encrypt_for,
					   NULL);
		    else
			success = 
			    libbalsa_encrypt_mime_object(&mime_root,
							 encrypt_for);
		    g_list_free(encrypt_for);
			
		    if (!success) {
			/* FIXME: do we have to free any resources here??? */
			return LIBBALSA_MESSAGE_ENCRYPT_ERROR;
		    }
		    break;
		}
	    default:
		g_error("illegal gpg_mode %d (" __FILE__ " line %d)",
		    message->gpg_mode, __LINE__);
	    }
    }
#endif
    
    mime_message = g_mime_message_new(TRUE);
    if (mime_root) {
	GList *param = message->parameters;

 	while (param) {
 	    gchar **vals = (gchar **)param->data;
 
	    g_mime_object_set_content_type_parameter(GMIME_OBJECT(mime_root),
						     vals[0], vals[1]);
 	    param = param->next;
 	}
	g_mime_message_set_mime_part(mime_message, mime_root);
	g_object_unref(G_OBJECT(mime_root));
    }
    message_add_references(message, mime_message);

    if (message->headers->from)
	g_mime_message_set_sender(mime_message,
			libbalsa_address_to_gchar(message->headers->from, -1));
    if (message->headers->reply_to)
	g_mime_message_set_reply_to(mime_message,
			libbalsa_address_to_gchar(message->headers->reply_to, -1));

    if (LIBBALSA_MESSAGE_GET_SUBJECT(message))
	g_mime_message_set_subject(mime_message,
				   LIBBALSA_MESSAGE_GET_SUBJECT(message));

    g_mime_message_set_date(mime_message, message->headers->date, 0); /* FIXME: Set GMT offset */

    tmp = libbalsa_make_string_from_list(message->headers->to_list);
    g_mime_message_add_recipients_from_string(mime_message,
					      GMIME_RECIPIENT_TYPE_TO, tmp);
    g_free(tmp);

    tmp = libbalsa_make_string_from_list(message->headers->cc_list);
    g_mime_message_add_recipients_from_string(mime_message,
					      GMIME_RECIPIENT_TYPE_CC, tmp);
    g_free(tmp);

    tmp = libbalsa_make_string_from_list(message->headers->bcc_list);
    g_mime_message_add_recipients_from_string(mime_message,
					      GMIME_RECIPIENT_TYPE_BCC, tmp);
    g_free(tmp);

    if (message->headers->dispnotify_to) {
	g_mime_message_add_header(mime_message, "Disposition-Notification-To",
			libbalsa_address_to_gchar(message->headers->dispnotify_to, 0));
    }

    for (list = message->headers->user_hdrs; list; list = g_list_next(list)) {
	gchar **pair;
	pair = g_strsplit(list->data, ":", 1);
	g_strchug(pair[1]);
	g_mime_message_add_header(mime_message, pair[0], pair[1]);
	g_strfreev(pair);
    }

    g_mime_message_add_header(mime_message, "X-Mailer",
			      g_strdup_printf("Balsa %s", VERSION));

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
libbalsa_message_postpone(LibBalsaMessage * message,
			  LibBalsaMailbox * draftbox,
			  LibBalsaMessage * reply_message,
			  gchar * fcc, gint encoding,
			  gboolean flow)
{
    int thereturn; 

    if (!message->mime_msg
	&& libbalsa_message_create_mime_message(message, encoding, flow,
						TRUE) !=
	LIBBALSA_MESSAGE_CREATE_OK)
	return FALSE;

    if (fcc)
	g_mime_message_set_header(message->mime_msg, "X-Balsa-Fcc", fcc);

    thereturn = libbalsa_mailbox_copy_message( message, draftbox );

    if (MAILBOX_OPEN(draftbox))
	libbalsa_mailbox_check(draftbox);

    return thereturn!=-1;
}


#ifdef HAVE_GPGME
static gint
libbalsa_create_rfc2440_buffer(LibBalsaMessageBody *body, GMimePart *mime_part)
{
    const gchar *content;
    gchar *cbuf, *rbuf = NULL;
    LibBalsaMessage *message = body->message;
    gint mode = message->gpg_mode;
    guint len;

    content = g_mime_part_get_content(mime_part, &len);
    cbuf = g_strndup(content, len);

    g_return_val_if_fail(cbuf != NULL, LIBBALSA_MESSAGE_SIGN_ERROR);
    
    switch (mode & ~LIBBALSA_GPG_RFC2440)
	{
	case LIBBALSA_GPG_SIGN:   /* sign only */
	    rbuf =
		libbalsa_rfc2440_sign_buffer(cbuf, 
					     message->headers->from->address_list->data,
					     NULL);
	    g_free(cbuf);
	    if (!rbuf)
		return LIBBALSA_MESSAGE_SIGN_ERROR;
	    break;
	case LIBBALSA_GPG_ENCRYPT:
	case LIBBALSA_GPG_ENCRYPT | LIBBALSA_GPG_SIGN:
	    {
		GList *encrypt_for = NULL;
			    
		/* build a list containing the addresses of all to:, cc:
		   and the from: address. Note: don't add bcc: addresses
		   as they would be visible in the encrypted block. */
		encrypt_for = get_mailbox_names(encrypt_for, message->headers->to_list);
		encrypt_for = get_mailbox_names(encrypt_for, message->headers->cc_list);
		encrypt_for = g_list_append(encrypt_for,
					    message->headers->from->address_list->data);
		if (message->headers->bcc_list)
		    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
					 _("This message will not be encrpyted for the BCC: recipient(s)."));

		if (mode & LIBBALSA_GPG_SIGN)
		    rbuf = libbalsa_rfc2440_encrypt_buffer(cbuf, 
					     message->headers->from->address_list->data,
					     encrypt_for,
					     NULL);
		else
		    rbuf = libbalsa_rfc2440_encrypt_buffer(cbuf, 
							   NULL,
							   encrypt_for,
							   NULL);
		g_list_free(encrypt_for);
	    }
	    g_free(cbuf);
	    if (!rbuf)
		return LIBBALSA_MESSAGE_ENCRYPT_ERROR;
	    break;
	default:
	    g_free(cbuf);
	    g_error("illegal gpg_mode %d (" __FILE__ " line %d)",
		    mode, __LINE__);
	}

    g_mime_part_set_content(mime_part, rbuf, strlen(rbuf));
    return LIBBALSA_MESSAGE_CREATE_OK;
}
#endif

/* Create a message-id and set it on the mime message.
 */
static void
libbalsa_set_message_id(GMimeMessage * mime_message)
{
    struct utsname utsbuf;
    gchar *host = "localhost";
    gchar *message_id;
#ifdef _GNU_SOURCE
    gchar *fqdn;
    gchar *domain = "localdomain";

    /* In an ideal world, uname() allows us to make a FQDN. */
    if (uname(&utsbuf) == 0) {
	if (*utsbuf.nodename)
	    host = utsbuf.nodename;
	if (*utsbuf.domainname)
	    domain = utsbuf.domainname;
    }
    fqdn = g_strconcat(host, ".", domain, NULL);
    message_id = g_mime_utils_generate_message_id(fqdn);
    g_free(fqdn);
#else				/* _GNU_SOURCE */

    if (uname(&utsbuf) == 0 && *utsbuf.nodename)
	host = utsbuf.nodename;
    message_id = g_mime_utils_generate_message_id(host);
#endif				/* _GNU_SOURCE */

    g_mime_message_set_message_id(mime_message, message_id);
    g_free(message_id);
}

/* balsa_create_msg:
   copies message to msg.
*/ 
static LibBalsaMsgCreateResult
libbalsa_create_msg(LibBalsaMessage * message,
		    gint encoding, gboolean flow)
{
    if (!message->mime_msg) {
	LibBalsaMsgCreateResult res =
	    libbalsa_message_create_mime_message(message, encoding, flow,
						 FALSE);
	if (res != LIBBALSA_MESSAGE_CREATE_OK)
	    return res;
    }

    libbalsa_set_message_id(message->mime_msg);

    return LIBBALSA_MESSAGE_CREATE_OK;
}

static LibBalsaMsgCreateResult
libbalsa_fill_msg_queue_item_from_queu(LibBalsaMessage * message,
				 MessageQueueItem *mqi)
{
    mqi->orig = message;
    mqi->stream = libbalsa_mailbox_get_message_stream(message->mailbox,
						      message);
    if (mqi->stream == NULL)
	return LIBBALSA_MESSAGE_CREATE_ERROR;
  
    return LIBBALSA_MESSAGE_CREATE_OK;
}
