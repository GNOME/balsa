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

#include <fcntl.h>
#include <errno.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include <gnome.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "send.h"
#include "mailbackend.h"
#include "mailbox_imap.h"
#include "misc.h"
#include "information.h"

#if ENABLE_ESMTP
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#endif

typedef struct _MessageQueueItem MessageQueueItem;

struct _MessageQueueItem {
    LibBalsaMessage *orig;
    HEADER *message;
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
};

/* Variables storing the state of the sending thread.
 * These variables are protected in MT by send_messages_lock mutex */
#if ENABLE_ESMTP
#else
static MessageQueueItem *message_queue;
#endif
static int total_messages_left;
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
    static const int DOZE_LENGTH = 20*1000; /* microseconds */

    if(max_time<0) max_time = G_MAXINT;
    else max_time *= 1000000; /* convert to microseconds */
    while(sending_threads>0 && sleep_time<max_time) {
        while(gtk_events_pending())
            gtk_main_iteration_do(FALSE);
        usleep(DOZE_LENGTH);
        sleep_time += DOZE_LENGTH;
    }
}


static MessageQueueItem *
msg_queue_item_new(LibBalsaMessage * message)
{
    MessageQueueItem *mqi;

    mqi = g_new(MessageQueueItem, 1);
    mqi->orig = message;
    libbalsa_lock_mutt();
    mqi->message = mutt_new_header();
    libbalsa_unlock_mutt();
#if !ENABLE_ESMTP
    mqi->next_message = NULL;
    mqi->status = MQI_WAITING;
#endif
    mqi->tempfile[0] = '\0';
    return mqi;
}

static void
msg_queue_item_destroy(MessageQueueItem * mqi)
{
    if (*mqi->tempfile)
	unlink(mqi->tempfile);
    libbalsa_lock_mutt();
    if (mqi->message)
	mutt_free_header(&mqi->message);
    libbalsa_unlock_mutt();
    g_free(mqi);
}

#if ENABLE_ESMTP
static SendMessageInfo *
send_message_info_new(LibBalsaMailbox* outbox, smtp_session_t session)
{
    SendMessageInfo *smi;

    smi=g_new(SendMessageInfo,1);
    smi->session = session;
    smi->outbox = outbox;

    return smi;
}
#else
static SendMessageInfo *
send_message_info_new(LibBalsaMailbox* outbox)
{
    SendMessageInfo *smi;

    smi=g_new(SendMessageInfo,1);
    smi->outbox = outbox;

    return smi;
}
#endif

static void
send_message_info_destroy(SendMessageInfo *smi)
{
    g_free(smi);
}



static guint balsa_send_message_real(SendMessageInfo* info);
static void encode_descriptions(BODY * b);
static gboolean libbalsa_create_msg(LibBalsaMessage * message,
				    HEADER * msg, char *tempfile,
				    gint encoding, gboolean flow,
				    int queu);

#ifdef BALSA_USE_THREADS
void balsa_send_thread(MessageQueueItem * first_message);

// GtkWidget *send_progress = NULL;
GtkWidget *send_progress_message = NULL;
GtkWidget *send_dialog = NULL;
GtkWidget *send_dialog_bar = NULL;

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

    send_dialog = gnome_dialog_new(_("Sending Mail..."), _("Hide"), NULL);
    gtk_window_set_wmclass(GTK_WINDOW(send_dialog), "send_dialog", "Balsa");
    gnome_dialog_set_close(GNOME_DIALOG(send_dialog), TRUE);
    label = gtk_label_new(_("Sending Mail..."));
    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(send_dialog)->vbox),
		       label, FALSE, FALSE, 0);

    send_progress_message = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(send_dialog)->vbox),
		       send_progress_message, FALSE, FALSE, 0);

    send_dialog_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(send_dialog)->vbox),
		       send_dialog_bar, FALSE, FALSE, 0);

    gtk_widget_show_all(send_dialog);
    gtk_signal_connect(GTK_OBJECT(send_dialog), "destroy", 
		       send_dialog_destroy_cb, NULL);
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


/* from mutt's send.c */
static void 
encode_descriptions (BODY *b)
{
    BODY *t;
    
    for (t = b; t; t = t->next)
	{
	    if (t->description)
		{
		    rfc2047_encode_string (&t->description);
		}
	    if (t->parts)
		encode_descriptions (t->parts);
	}
}

static BODY *
add_mutt_body_plain(const gchar * charset, gint encoding_style, 
		    gboolean flow)
{
    BODY *body;
    gchar buffer[PATH_MAX];

    g_return_val_if_fail(charset, NULL);
    libbalsa_lock_mutt();
    body = mutt_new_body();

    body->type = TYPETEXT;
    body->subtype = g_strdup("plain");
    body->unlink = 1;
    body->use_disp = 0;
    body->force_charset = 1;
    body->disposition = DISPINLINE;

    body->encoding = encoding_style;

    mutt_set_parameter("charset", charset, &body->parameter);
    if (flow)
	mutt_set_parameter("format", "flowed", &body->parameter);

    mutt_mktemp(buffer);
    body->filename = g_strdup(buffer);
    mutt_update_encoding(body);

    libbalsa_unlock_mutt();

    return body;
}

static BODY *
add_mutt_body_as_extbody(const gchar *filename, const gchar *mime_type)
{
    BODY *body;
    gchar buffer[PATH_MAX];
    FILE *tempfp;

    libbalsa_lock_mutt();
    body = mutt_new_body();

    body->type = TYPEMESSAGE;
    body->subtype = g_strdup("external-body");
    body->unlink = 1;
    body->use_disp = 0;

    body->encoding = ENC7BIT;
    if(!strncmp( filename, "URL", 3 )) {
	mutt_set_parameter("access-type", "URL", &body->parameter);
	mutt_set_parameter("URL", filename + 4, &body->parameter);
    } else {
	mutt_set_parameter("access-type", "local-file", &body->parameter);
	mutt_set_parameter("name", filename, &body->parameter);
    }
    mutt_mktemp(buffer);
    body->filename = g_strdup(buffer);
    tempfp = safe_fopen(body->filename, "w+");
    fprintf(tempfp, "Note: this is _not_ the real body!\n");
    fclose(tempfp);

    libbalsa_unlock_mutt();

    return body;
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
static int
write_remote_fcc(LibBalsaMailbox* fccbox, HEADER* m_msg)
{
    LibBalsaServer* server = LIBBALSA_MAILBOX_REMOTE(fccbox)->server;
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(fccbox), -1);
    if(CLIENT_CONTEXT_CLOSED(fccbox)) {
        /* We cannot use LIBBALSA_REMOTE_MAILBOX_SERVER() here because */
        /* it will lock up when NO IMAP mailbox has been accessed since */
        /* balsa was started. This should be safe because we have already */
        /* established that fccbox is in fact an IMAP mailbox */
        if(server == (LibBalsaServer *)NULL) {
            libbalsa_unlock_mutt();
            libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
                                 _("Unable to open sentbox - could not get IMAP server information"));
            return -1;
        }
        if (!(server->passwd && *server->passwd) &&
            !(server->passwd = libbalsa_server_get_password(server, fccbox))) {
            libbalsa_unlock_mutt();
            libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
                                 "Unable to open sentbox - could not get passwords for server");
            return -1;
        }
        reset_mutt_passwords(server);
    }
  
    /* Passwords are guaranteed to be set now */
    
    return mutt_write_fcc(LIBBALSA_MAILBOX(fccbox)->url,
                          m_msg, NULL, 0, NULL);
}
/* libbalsa_message_queue:
   places given message in the outbox. If fcc message field is set, saves
   it to fcc mailbox as well.
*/
void
libbalsa_message_queue(LibBalsaMessage * message, LibBalsaMailbox * outbox,
		       LibBalsaMailbox * fccbox, gint encoding,
		       gboolean flow)
{
    MessageQueueItem *mqi;

    g_return_if_fail(message);

    mqi = msg_queue_item_new(message);
    set_option(OPTWRITEBCC);
    if (libbalsa_create_msg(message, mqi->message,
			    mqi->tempfile, encoding, flow, 0)) {
	libbalsa_lock_mutt();
	mutt_write_fcc(libbalsa_mailbox_local_get_path(outbox),
		       mqi->message, NULL, 0, NULL);
	libbalsa_unlock_mutt();
	if (fccbox && (LIBBALSA_IS_MAILBOX_LOCAL(fccbox)
		|| LIBBALSA_IS_MAILBOX_IMAP(fccbox))) {
	    libbalsa_lock_mutt();
	    if (LIBBALSA_IS_MAILBOX_LOCAL(fccbox))
	    mutt_write_fcc(libbalsa_mailbox_local_get_path(fccbox),
			   mqi->message, NULL, 0, NULL);
	    else if (LIBBALSA_IS_MAILBOX_IMAP(fccbox))
                write_remote_fcc(fccbox, mqi->message);

	    libbalsa_unlock_mutt();
	    libbalsa_mailbox_check(fccbox);
	}
	libbalsa_mailbox_check(outbox);
    } 
    unset_option(OPTWRITEBCC);
    msg_queue_item_destroy(mqi);
}

/* libbalsa_message_send:
   send the given messsage (if any, it can be NULL) and all the messages
   in given outbox.
*/
#if ENABLE_ESMTP
gboolean
libbalsa_message_send(LibBalsaMessage* message, LibBalsaMailbox* outbox,
		      LibBalsaMailbox* fccbox, gint encoding,
		      gchar* smtp_server, auth_context_t smtp_authctx,
		      gint tls_mode, gboolean flow)
{
    if (message != NULL)
	libbalsa_message_queue(message, outbox, fccbox, encoding,
			       flow);
    return libbalsa_process_queue(outbox, encoding, smtp_server,
				  smtp_authctx, tls_mode, flow);
}
#else
gboolean
libbalsa_message_send(LibBalsaMessage* message, LibBalsaMailbox* outbox,
		      LibBalsaMailbox* fccbox, gint encoding,
		      gboolean flow)
{
    if (message != NULL)
	libbalsa_message_queue(message, outbox, fccbox, encoding,
			       flow);
    return libbalsa_process_queue(outbox, encoding, flow);
}
#endif


#if ENABLE_ESMTP
/* [BCS] - libESMTP uses a callback function to read the message from the
   application to the SMTP server.
 */
#define BUFLEN  8192

static const char *
libbalsa_message_cb (void **buf, int *len, void *arg)
{
    MessageQueueItem *current_message = arg;
    struct ctx { FILE *fp; char buf[BUFLEN - sizeof (FILE *)]; } *ctx;
    size_t octets;

    if (*buf == NULL)
      *buf = calloc (sizeof (struct ctx), 1);
    ctx = (struct ctx *) *buf;

    if (len == NULL) {
	if (ctx->fp == NULL)
	    ctx->fp = fopen (current_message->tempfile, "r");
	else
	    rewind (ctx->fp);
	return NULL;
    }

    /* The message needs to be read a line at a time and the newlines
       converted to \r\n because libmutt foolishly terminates lines in
       the temporary file with the Unix \n despite RFC 2822 calling for
       \r\n.  Furthermore RFC 822 states that bare \n and \r are acceptable
       in messages and that individually they do not constitute a line
       termination.  This requirement cannot be reconciled with storing
       messages with Unix line terminations.  RFC 2822 relieves this
       situation slightly by prohibiting bare \r and \n.

       The following code cannot therefore work correctly in all
       situations.  Furthermore it is very inefficient since it must
       search for the \n.
     */
    if (ctx->fp == NULL) {
        octets = 0;
    } else if (fgets (ctx->buf, sizeof ctx->buf - 1, ctx->fp) == NULL) {
	fclose (ctx->fp);
	ctx->fp = NULL;
        octets = 0;
    } else {
	char *p = strchr (ctx->buf, '\0');

	if (p[-1] == '\n' && p[-2] != '\r') {
	    strcpy (p - 1, "\r\n");
	    p++;
	}
	octets = p - ctx->buf;
    }
    *len = octets;
    return ctx->buf;
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
libbalsa_process_queue(LibBalsaMailbox* outbox, gint encoding, 
		       gchar* smtp_server, auth_context_t smtp_authctx,
		       gint tls_mode, gboolean flow)
{
    MessageQueueItem *new_message;
    SendMessageInfo *send_message_info;
    GList *lista, *recip, *bcc_recip;
    LibBalsaMessage *msg;
    LibBalsaAddress *addy; 
    smtp_session_t session;
    smtp_message_t message, bcc_message;
    smtp_recipient_t recipient;
    const gchar *phrase, *mailbox, *subject;
    struct stat st;
    long estimate;

    send_lock();

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

    libbalsa_mailbox_open(outbox);
    for (lista = outbox->message_list; lista; lista = lista->next) {
	msg = LIBBALSA_MESSAGE(lista->data);

	if (msg->flags & (LIBBALSA_MESSAGE_FLAG_FLAGGED |
                          LIBBALSA_MESSAGE_FLAG_DELETED))
            continue;
	new_message = msg_queue_item_new(msg);
	if (!libbalsa_create_msg(msg, new_message->message,
				 new_message->tempfile, encoding, 
				 flow, 1)) {
	    msg_queue_item_destroy(new_message);
	} else {
	    GList * messages = g_list_prepend(NULL, msg);

	    total_messages_left++;
            libbalsa_messages_flag(messages, TRUE);
	    g_list_free(messages);
	    /* If the Bcc: recipient list is present, add a additional
	       copy of the message to the session.  The recipient list
	       for the main copy of the message is generated from the
	       To: and Cc: recipient list and libESMTP is asked to strip
	       the Bcc: header.  The BCC copy of the message recipient
	       list is taken from the Bcc recipient list and the Bcc:
	       header is preserved in the message. */
	    bcc_recip = g_list_first((GList *) msg->bcc_list);
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

	    smtp_set_header (message, "Date", &msg->date);
	    if (bcc_message)
		smtp_set_header (bcc_message, "Date", &msg->date);

	    /* RFC 2822 does not require a message to have a subject.
	               I assume this is NULL if not present */
	    subject = LIBBALSA_MESSAGE_GET_SUBJECT(msg);
	    if (subject) {
	    	smtp_set_header (message, "Subject", subject);
		if (bcc_message)
		    smtp_set_header (bcc_message, "Subject", subject);
	    }

	    /* Add the sender info */
	    phrase = libbalsa_address_get_phrase(msg->from);
	    mailbox = libbalsa_address_get_mailbox(msg->from, 0);
	    smtp_set_reverse_path (message, mailbox);
	    smtp_set_header (message, "From", phrase, mailbox);
	    if (bcc_message) {
		smtp_set_reverse_path (bcc_message, mailbox);
	        smtp_set_header (bcc_message, "From", phrase, mailbox);
	    }

	    if (msg->reply_to) {
		phrase = libbalsa_address_get_phrase(msg->reply_to);
		mailbox = libbalsa_address_get_mailbox(msg->reply_to, 0);
		smtp_set_header (message, "Reply-To", phrase, mailbox);
		if (bcc_message)
		    smtp_set_header (bcc_message, "Reply-To", phrase, mailbox);
	    }

	    if (msg->dispnotify_to) {
		phrase = libbalsa_address_get_phrase(msg->dispnotify_to);
		mailbox = libbalsa_address_get_mailbox(msg->dispnotify_to, 0);
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

	    recip = g_list_first((GList *) msg->to_list);
	    while (recip) {
        	addy = recip->data;
		phrase = libbalsa_address_get_phrase(addy);
		mailbox = libbalsa_address_get_mailbox(addy, 0);
		recipient = smtp_add_recipient (message, mailbox);
		/* XXX  - this is where to add DSN requests.  It would be
			  cool if LibBalsaAddress could contain DSN options
			  for a particular recipient. */
#ifdef LIBESMTP_ADDS_HEADERS
		smtp_set_header (message, "To", phrase, mailbox);
		if (bcc_message)
		    smtp_set_header (bcc_message, "To", phrase, mailbox);
#endif
	    	recip = recip->next;
	    }

	    recip = g_list_first((GList *) msg->cc_list);
	    while (recip) {
        	addy = recip->data;
		phrase = libbalsa_address_get_phrase(addy);
		mailbox = libbalsa_address_get_mailbox(addy, 0);
		recipient = smtp_add_recipient (message, mailbox);
		/* XXX  -  DSN */
#ifdef LIBESMTP_ADDS_HEADERS
		smtp_set_header (message, "Cc", phrase, mailbox);
		if (bcc_message)
		    smtp_set_header (bcc_message, "Cc", phrase, mailbox);
#endif
	    	recip = recip->next;
	    }

	    while (bcc_recip) {
        	addy = bcc_recip->data;
		phrase = libbalsa_address_get_phrase(addy);
		mailbox = libbalsa_address_get_mailbox(addy, 0);
		recipient = smtp_add_recipient (bcc_message, mailbox);
		/* XXX  -  DSN */
#ifdef LIBESMTP_ADDS_HEADERS
		smtp_set_header (message, "Bcc", phrase, mailbox);
#endif
	    	bcc_recip = bcc_recip->next;
	    }

	    /* Estimate the size of the message.  This need not be exact
	       but it's better to err on the large side since some message
	       headers may be altered during the transfer. */
	    new_message->message_size = 0;
	    if (stat (new_message->tempfile, &st) == 0) {
		new_message->message_size = st.st_size;
	    }

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

    send_message_info=send_message_info_new(outbox, session);

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
		libbalsa_messages_delete(messages, TRUE);
		g_list_free(messages);
	    }
	}
    } else {
	messages = g_list_prepend(NULL, mqi->orig);
        libbalsa_messages_flag(messages, FALSE);
	g_list_free(messages);
	/* XXX - Show the poor user the status codes and message. */
        if(*(gboolean*)be_verbose)
            libbalsa_information(
	    LIBBALSA_INFORMATION_WARNING, 
	    _("Message submission problem, placing it into your outbox.\n" 
	      "System will attempt to resubmit the message until you delete it."));
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
	snprintf (buf, sizeof buf, _("From: %d <%s>"), status->code, mailbox);
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, buf, NULL, NULL, 0);

	snprintf (buf, sizeof buf, _("From %s: %d %s"),
	          mailbox, status->code, status->text);
	libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, buf);
        break;
    case SMTP_EV_RCPTSTATUS:
        mailbox = va_arg (ap, const char *);
        recipient = va_arg (ap, smtp_recipient_t);
	status = smtp_recipient_status (recipient);
	snprintf (buf, sizeof buf, _("To: %d <%s>"), status->code, mailbox);
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADPROGRESS, buf, NULL, NULL, 0);

	snprintf (buf, sizeof buf, _("To %s: %d %s"),
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
	snprintf (buf, sizeof buf, _("%d %s"),
		  status->code, status->text);
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
libbalsa_process_queue(LibBalsaMailbox* outbox, gint encoding, 
		       gboolean flow)
{
    MessageQueueItem *mqi, *new_message;
    SendMessageInfo *send_message_info;
    GList *lista;
    LibBalsaMessage *queu;
    gboolean start_thread;

    /* We do messages in queue now only if where are not sending them already */

    send_lock();

    ensure_send_progress_dialog();
    if (sending_threads==0) {
	libbalsa_mailbox_open(outbox);
	lista = outbox->message_list;
	
	mqi = message_queue;
	while (lista != NULL) {
	    queu = LIBBALSA_MESSAGE(lista->data);
	    
	    new_message = msg_queue_item_new(queu);
	    if (!libbalsa_create_msg(queu, new_message->message,
				     new_message->tempfile, 
				     encoding, flow, 1)) {
		msg_queue_item_destroy(new_message);
	    } else {
		if (mqi)
		    mqi->next_message = new_message;
		else
		    message_queue = new_message;

		mqi = new_message;
		total_messages_left++;
	    }
	    lista = lista->next;
	}
    }
#ifdef BALSA_USE_THREADS
    
    start_thread = sending_threads==0;

    if (start_thread) {
        sending_threads++;
	send_message_info=send_message_info_new(outbox);

	pthread_create(&send_mail, NULL,
		       (void *) &balsa_send_message_real, send_message_info);
	/* Detach so we don't need to pthread_join
	 * This means that all resources will be
	 * reclaimed as soon as the thread exits
	 */
	pthread_detach(send_mail);
    }
#else				/*non-threaded code */
    
    send_message_info=send_message_info_new(outbox);
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

	libbalsa_messages_delete(messages, TRUE);
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

#ifdef DEBUG
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
#endif

/* balsa_send_message_real:
   does the actual message sending. 
   This function may be called as a thread and should therefore do
   proper gdk_threads_{enter/leave} stuff around GTK,libbalsa or
   libmutt calls. Also, structure info should be freed before exiting.
*/

/* [BCS] radically different since it uses the libESMTP interface.
 */
static guint
balsa_send_message_real(SendMessageInfo* info) {
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
#ifdef DEBUG
    /* Add a protocol monitor when compiled with DEBUG.  This is somewhat
       unsatisfactory, it would be better handled at run time with an
       option in the preferences dialogue.  I don't know how to access
       app level options within libbalsa however without breaking the
       current modularity of the code. */
    smtp_set_monitorcb (info->session, monitor_cb, stderr, 1);
#endif

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
            /* case SMTP_ERR_NOTHING_TO_DO: silence this one?
               break; */
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
    gdk_threads_enter();
    smtp_enumerate_messages (info->session, handle_successful_send, 
                             &session_started);

    libbalsa_mailbox_close(info->outbox);
    gdk_threads_leave();

    send_lock();
    total_messages_left = 0;
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

static guint balsa_send_message_real(SendMessageInfo* info) {
    MessageQueueItem *mqi, *next_message;
    int i;
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
	libbalsa_lock_mutt();
	i = mutt_invoke_sendmail(mqi->message->env->from,
                                 mqi->message->env->to,
				 mqi->message->env->cc,
				 mqi->message->env->bcc,
				 mqi->tempfile,
				 (mqi->message->content->encoding
				  == ENC8BIT));
	libbalsa_unlock_mutt();
	mqi->status = (i==0?MQI_SENT : MQI_FAILED);
	total_messages_left--; /* whatever the status is, one less to do*/
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
    gdk_threads_leave();

    message_queue = NULL;
    total_messages_left = 0;
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
message2HEADER(LibBalsaMessage * message, HEADER * hdr) {
    LIST *tmp_hdr;
    GList *list;
    gchar *tmp;

    libbalsa_lock_mutt();

    if (!hdr->env)
	hdr->env = mutt_new_envelope();

    hdr->env->userhdrs = mutt_new_list();

    {
	LIST *sptr = UserHeader;
	LIST *dptr = hdr->env->userhdrs;
	LIST *delptr = 0;

	while (sptr) {
	    dptr->data = g_strdup(sptr->data);
	    sptr = sptr->next;
	    delptr = dptr;
	    dptr->next = mutt_new_list();
	    dptr = dptr->next;
	} safe_free((void **) &delptr->next);
    }

    tmp = libbalsa_address_to_gchar_p(message->from, 0);

    hdr->env->from = rfc822_parse_adrlist(hdr->env->from, tmp);
    g_free(tmp);

    if (message->reply_to) {
	tmp = libbalsa_address_to_gchar_p(message->reply_to, 0);

	hdr->env->reply_to =
	    rfc822_parse_adrlist(hdr->env->reply_to, tmp);

	g_free(tmp);
    }

    if (message->dispnotify_to) {
	tmp = libbalsa_address_to_gchar_p(message->dispnotify_to, 0);

	hdr->env->dispnotify_to =
	    rfc822_parse_adrlist(hdr->env->dispnotify_to, tmp);

	g_free(tmp);
    }

    hdr->env->subject = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));

    tmp = libbalsa_make_string_from_list_p(message->to_list);
    hdr->env->to = rfc822_parse_adrlist(hdr->env->to, tmp);
    g_free(tmp);

    tmp = libbalsa_make_string_from_list_p(message->cc_list);
    hdr->env->cc = rfc822_parse_adrlist(hdr->env->cc, tmp);
    g_free(tmp);

    tmp = libbalsa_make_string_from_list_p(message->bcc_list);
    hdr->env->bcc = rfc822_parse_adrlist(hdr->env->bcc, tmp);
    g_free(tmp);

    for (list = message->user_headers; list; list = g_list_next(list)) {
        tmp_hdr = mutt_new_list();
        tmp_hdr->next = hdr->env->userhdrs;
        tmp_hdr->data = g_strjoinv(": ", list->data);
        hdr->env->userhdrs = tmp_hdr;
    }

    libbalsa_unlock_mutt();
}

static void
message_add_references(LibBalsaMessage* message, HEADER* msg) 
{
    LIST* in_reply_to;
    LIST* references;
    GList* list;

    /* If the message has references set, add them to he envelope */
    if (message->references != NULL) {
        list = message->references;
        libbalsa_lock_mutt();
        msg->env->references = mutt_new_list();
	references = msg->env->references;
	references->data = g_strdup(list->data);
	list = list->next;

	while (list != NULL) {
	    references->next = mutt_new_list();
	    references = references->next;
	    references->data = g_strdup(list->data);
	    references->next = NULL;
	    list = list->next;
	}
        

	/* There's no specific header for In-Reply-To, just
	 * add it to the user headers */ 
        in_reply_to = mutt_new_list();
	in_reply_to->next = msg->env->userhdrs;
	in_reply_to->data =
	    g_strconcat("In-Reply-To: ", message->in_reply_to, NULL);
	msg->env->userhdrs = in_reply_to;
        libbalsa_unlock_mutt();
    }
}


gboolean
libbalsa_message_postpone(LibBalsaMessage * message,
			  LibBalsaMailbox * draftbox,
			  LibBalsaMessage * reply_message,
			  gchar * fcc, gint encoding,
			  gboolean flow) {
    HEADER *msg;
    BODY *last, *newbdy;
    gchar *tmp;
    LibBalsaMessageBody *body;
    int thereturn; 

    libbalsa_lock_mutt();
    msg = mutt_new_header();
    libbalsa_unlock_mutt();

    message2HEADER(message, msg);
    message_add_references(message, msg);

    body = message->body_list;

    last = msg->content;
    while (last && last->next)
	last = last->next;

    while (body) {
	FILE *tempfp = NULL;
	newbdy = NULL;

	if (body->filename) {
	    libbalsa_lock_mutt();
	    newbdy = mutt_make_file_attach(body->filename);
	    libbalsa_unlock_mutt();
	    if (!newbdy) {
		g_warning("Cannot attach file: %s.\nPostponing without it.",
		     body->filename);
	    } else {
		gchar **mime_type;

		/* Do this here because we don't want
		 * to use libmutt's mime types */
		if (!body->mime_type) {
                    gchar* mt = libbalsa_lookup_mime_type(body->filename);
		    mime_type = g_strsplit(mt,"/", 2);
                    g_free(mt);
                } else
		    mime_type = g_strsplit(body->mime_type, "/", 2);
		/* use BASE64 encoding for non-text mime types 
		   use 8BIT for message */
		libbalsa_lock_mutt();
		newbdy->disposition = DISPATTACH;
		if(!strcasecmp(mime_type[0],"message") && 
		   !strcasecmp(mime_type[1],"rfc822")) {
		    newbdy->encoding = ENC8BIT;
		    newbdy->disposition = DISPINLINE;
		} else if(strcasecmp(mime_type[0],"text") != 0)
		    newbdy->encoding = ENCBASE64;
		newbdy->type = mutt_check_mime_type(mime_type[0]);
		g_free(newbdy->subtype);
		newbdy->subtype = g_strdup(mime_type[1]);
		libbalsa_unlock_mutt();
		g_strfreev(mime_type);
	    }
	} else if (body->buffer) {
	    newbdy = add_mutt_body_plain(body->charset, encoding, flow);
	    newbdy->disposition = DISPINLINE;
	    if (body->mime_type) {
		/* change the type and subtype within the mutt body */
		gchar *type, *subtype;
		
		type = g_strdup (body->mime_type);
		if ((subtype = strchr (type, '/'))) {
		    *subtype++ = 0;
		    libbalsa_lock_mutt();
		    newbdy->type = mutt_check_mime_type (type);
		    libbalsa_unlock_mutt();
		    newbdy->subtype = g_strdup(subtype);
		}
		g_free (type);
	    }
	    tempfp = safe_fopen(newbdy->filename, "w+");
	    fputs(body->buffer, tempfp);
	    fclose(tempfp);
	    tempfp = NULL;
	}

	if (newbdy) {
	    if (last)
		last->next = newbdy;
	    else
		msg->content = newbdy;

	    last = newbdy;
	}
	body = body->next;
    }

    { /* scope */
	gchar *cset = g_strdup(libbalsa_message_charset(message));
	
	libbalsa_lock_mutt();
	mutt_set_charset(cset);
	if (msg->content) {
	    if (msg->content->next)
		msg->content = mutt_make_multipart(msg->content);
	}
	mutt_prepare_envelope(msg->env, FALSE);
	encode_descriptions(msg->content);
	mutt_set_charset(NULL);
	g_free(cset);
	libbalsa_unlock_mutt();
    }	
	
    if ((reply_message != NULL) && (reply_message->mailbox != NULL))
	/* Just saves the message ID, mailbox type and mailbox name. We could
	 * search all mailboxes for the ID but that would not be too fast. We
	 * could also add more stuff ID like path, server, ... without this
	 * if you change the name of the mailbox the flag will not be set. 
	 CHBM: unbreak this in 1.3 - use URL */
	tmp = g_strdup_printf("%s\r%s",
			      reply_message->message_id,
			      reply_message->mailbox->name);
    else
	tmp = NULL;

    libbalsa_lock_mutt();
    if (LIBBALSA_IS_MAILBOX_LOCAL(draftbox)) {
	thereturn = mutt_write_fcc(libbalsa_mailbox_local_get_path(draftbox),
				   msg, tmp, 1, fcc);
    } else if (LIBBALSA_IS_MAILBOX_IMAP(draftbox)) {
        thereturn = write_remote_fcc(draftbox, msg);
    } else 
	thereturn = -1;
    g_free(tmp);
    mutt_free_header(&msg);
    libbalsa_unlock_mutt();

    if (draftbox->open_ref > 0)
	libbalsa_mailbox_check(draftbox);

    return thereturn>=0;
}

/* libbalsa_create_msg:
   copies message to msg.
   PS: seems to be broken when queu == 1 - further execution of
   mutt_free_header(mgs) leads to crash.
*/ 
static gboolean
libbalsa_create_msg(LibBalsaMessage * message, HEADER * msg, char *tmpfile,
		    gint encoding, gboolean flow, int queu) {
    BODY *last, *newbdy;
    FILE *tempfp;
    HEADER *msg_tmp;
    MESSAGE *mensaje;
    LibBalsaMessageBody *body;
    gchar **mime_type;
    gboolean res = TRUE;

    
    message2HEADER(message, msg);
    message_add_references(message, msg);

    if (message->mailbox)
	libbalsa_message_body_ref(message, TRUE);

    body = message->body_list;

    last = msg->content;
    while (last && last->next)
	last = last->next;

    while (body) {
	FILE *tempfp = NULL;
	newbdy = NULL;

	if (body->filename) {
	    if (body->attach_as_extbody) {
                if(body->mime_type)
                    newbdy = add_mutt_body_as_extbody(body->filename, 
                                                      body->mime_type);
                else {
                    gchar* mt = libbalsa_lookup_mime_type(body->filename);
                    newbdy = add_mutt_body_as_extbody(body->filename, mt);
                    g_free(mt);
                }
	    } else {
		libbalsa_lock_mutt();
		newbdy = mutt_make_file_attach(body->filename);
		libbalsa_unlock_mutt();
		if (!newbdy) {
		    g_warning
			("Cannot attach file: %s.\nSending without it.",
			 body->filename);
		} else {
		    
		    /* Do this here because we don't want
		     * to use libmutt's mime types */
		    if (!body->mime_type) {
                        gchar* mt = libbalsa_lookup_mime_type(body->filename);
			mime_type = g_strsplit(mt, "/", 2);
                        g_free(mt);
                    } else
			mime_type = g_strsplit(body->mime_type, "/", 2);
		    /* use BASE64 encoding for non-text mime types 
		       use 8BIT for message */
		    libbalsa_lock_mutt();
		    newbdy->disposition = DISPATTACH;
		    if(!strcasecmp(mime_type[0],"message") && 
		       !strcasecmp(mime_type[1],"rfc822")) {
			newbdy->encoding = ENC8BIT;
			newbdy->disposition = DISPINLINE;
		    } else if(strcasecmp(mime_type[0],"text") != 0)
			newbdy->encoding = ENCBASE64;
		    newbdy->type = mutt_check_mime_type(mime_type[0]);
		    g_free(newbdy->subtype);
		    newbdy->subtype = g_strdup(mime_type[1]);
		    libbalsa_unlock_mutt();
		    g_strfreev(mime_type);
		}
	    }
	} else if (body->buffer) {
	    newbdy = add_mutt_body_plain(body->charset, encoding, flow);
	    if (body->mime_type) {
		/* change the type and subtype within the mutt body */
		gchar *type, *subtype;
		
		type = g_strdup (body->mime_type);
		if ((subtype = strchr (type, '/'))) {
		    *subtype++ = 0;
		    libbalsa_lock_mutt();
		    newbdy->type = mutt_check_mime_type (type);
		    g_free(newbdy->subtype);
		    newbdy->subtype = g_strdup(subtype);
		    libbalsa_unlock_mutt();
		}
		g_free (type);
	    }
	    tempfp = safe_fopen(newbdy->filename, "w+");
	    fputs(body->buffer, tempfp);
	    fclose(tempfp);
	    tempfp = NULL;
	} else {
	    /* safe_free bug patch: steal it! */
            libbalsa_lock_mutt();
            msg->content = libmutt_copy_body(body->mutt_body, NULL);
            libbalsa_unlock_mutt();
	}

	if (newbdy) {
	    if (last)
		last->next = newbdy;
	    else
		msg->content = newbdy;

	    last = newbdy;
	}

	body = body->next;
    }
    { /* scope */
	gchar *cset = g_strdup(libbalsa_message_charset(message));

	libbalsa_lock_mutt();
	mutt_set_charset(cset);
	if (msg->content && msg->content->next)
	    msg->content = mutt_make_multipart(msg->content);
	mutt_prepare_envelope(msg->env, TRUE);
	encode_descriptions(msg->content);
	mutt_set_charset(NULL);
	g_free(cset);
	libbalsa_unlock_mutt();
    }
/* We create the message in MIME format here, we use the same format 
 * for local delivery and for SMTP */
    if (queu == 0) {
        libbalsa_lock_mutt();
	mutt_mktemp(tmpfile);
	if ((tempfp = safe_fopen(tmpfile, "w")) == NULL) {
            libbalsa_unlock_mutt();
            return FALSE;
        }

	mutt_write_rfc822_header(tempfp, msg->env, msg->content, -1, 0);
	fputc('\n', tempfp);	/* tie off the header. */

	if ((mutt_write_mime_body(msg->content, tempfp) == -1)) {
	    fclose(tempfp);
	    unlink(tmpfile);
            libbalsa_unlock_mutt();
            return FALSE;
	}
        libbalsa_unlock_mutt();
	fputc('\n', tempfp);	/* tie off the body. */
	if (fclose(tempfp) != 0) {
	    mutt_perror(tmpfile);
            unlink(tmpfile);
            return FALSE;
	}

    } else { /* the message is in the queue */
	msg_tmp = message->header;
        libbalsa_lock_mutt();
	mutt_parse_mime_message(CLIENT_CONTEXT(message->mailbox),
				msg_tmp);
	mensaje =
	    mx_open_message(CLIENT_CONTEXT(message->mailbox),
			    msg_tmp->msgno);

	mutt_mktemp(tmpfile);
	if ((tempfp = safe_fopen(tmpfile, "w")) == NULL) {
            libbalsa_unlock_mutt();
            return FALSE;
        }
	_mutt_copy_message(tempfp, mensaje->fp, msg_tmp,
			   msg_tmp->content, 0, CH_NOSTATUS);

	if (fclose(tempfp) != 0) {
	    mutt_perror(tmpfile);
	    unlink(tmpfile);
            res = FALSE;
	}
        libbalsa_unlock_mutt();
    }    

    if (message->mailbox)
	libbalsa_message_body_unref(message);

    return res;
}
