/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

/* #include <radlib.h>  */
/* FreeBSD 4.1-STABLE FreeBSD 4.1-STABLE */
#include <arpa/inet.h>
#include <netdb.h>

#include <fcntl.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include <sys/socket.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

typedef struct _MessageQueueItem MessageQueueItem;

struct _MessageQueueItem {
    LibBalsaMessage *orig;
    HEADER *message;
    MessageQueueItem *next_message;
    gchar *fcc;
    char tempfile[_POSIX_PATH_MAX];
    enum {MQI_WAITING, MQI_FAILED,MQI_SENT} status;
};

typedef struct _SendMessageInfo SendMessageInfo;

struct _SendMessageInfo{
    LibBalsaMailbox* outbox;
    char* server;
    int port;
};

/* Variables storing the state of the sending thread.
 * These variables are protected in MT by send_messages_lock mutex */
static MessageQueueItem *message_queue;
static int total_messages_left;
/* end of state variables section */


static MessageQueueItem *
msg_queue_item_new(LibBalsaMessage * message)
{
    MessageQueueItem *mqi;

    mqi = g_new(MessageQueueItem, 1);
    mqi->orig = message;
    mqi->message = mutt_new_header();
    mqi->next_message = NULL;
    mqi->fcc = g_strdup(message->fcc_mailbox);
    mqi->status = MQI_WAITING;
    mqi->tempfile[0] = '\0';

    return mqi;
}

static void
msg_queue_item_destroy(MessageQueueItem * mqi)
{
    if (*mqi->tempfile)
	unlink(mqi->tempfile);
    if (mqi->message)
	mutt_free_header(&mqi->message);
    if (mqi->fcc)
	g_free(mqi->fcc);
    g_free(mqi);
}

static SendMessageInfo *
send_message_info_new(LibBalsaMailbox* outbox, const gchar* smtp_server, 
		      gint smtp_port)
{
    SendMessageInfo *smi;

    smi=g_new(SendMessageInfo,1);
    smi->server = g_strdup(smtp_server);
    smi->port = smtp_port;
    smi->outbox = outbox;

    return smi;
}

static void
send_message_info_destroy(SendMessageInfo *smi)
{
    g_free(smi->server);
    g_free(smi);
}



static guint balsa_send_message_real(SendMessageInfo* info);
static void encode_descriptions(BODY * b);
static int libbalsa_smtp_send(const char *server, const int port);
static int libbalsa_smtp_protocol(int s, char *tempfile, HEADER * msg);
static gboolean libbalsa_create_msg(LibBalsaMessage * message,
				    HEADER * msg, char *tempfile,
				    gint encoding, int queu);
gchar **libbalsa_lookup_mime_type(const gchar * path);

#ifdef BALSA_USE_THREADS
void balsa_send_thread(MessageQueueItem * first_message);
#endif


/* from mutt's send.c */
static void
encode_descriptions(BODY * b)
{
    BODY *t;
    char tmp[LONG_STRING];

    for (t = b; t; t = t->next) {
	if (t->description) {
	    libbalsa_lock_mutt();
	    rfc2047_encode_string(tmp, sizeof(tmp),
				  (unsigned char *) t->description);
	    safe_free((void **) &t->description);
	    t->description = safe_strdup(tmp);
	    libbalsa_unlock_mutt();
	}
	if (t->parts)
	    encode_descriptions(t->parts);
    }
}

static BODY *
add_mutt_body_plain(const gchar * charset, gint encoding_style)
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

    body->encoding = encoding_style;
    body->parameter = mutt_new_parameter();
    body->parameter->attribute = g_strdup("charset");
    body->parameter->value = g_strdup(charset);
    body->parameter->next = NULL;

    mutt_mktemp(buffer);
    body->filename = g_strdup(buffer);
    mutt_update_encoding(body);

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

/* libbalsa_message_queue:
   places given message in the outbox. If fcc message field is set, saves
   it to fcc mailbox as well.
*/
void
libbalsa_message_queue(LibBalsaMessage * message, LibBalsaMailbox * outbox,
		       LibBalsaMailbox * fccbox, gint encoding)
{
    MessageQueueItem *mqi;

    g_return_if_fail(message);

    mqi = msg_queue_item_new(message);
    set_option(OPTWRITEBCC);
    if (libbalsa_create_msg(message, mqi->message,
			    mqi->tempfile, encoding, 0)) {
	libbalsa_lock_mutt();
	mutt_write_fcc(LIBBALSA_MAILBOX_LOCAL(outbox)->path,
		       mqi->message, NULL, 0, NULL);
	libbalsa_unlock_mutt();
	if (fccbox && LIBBALSA_IS_MAILBOX_LOCAL(fccbox)) {
	    libbalsa_lock_mutt();
	    mutt_write_fcc(LIBBALSA_MAILBOX_LOCAL(fccbox)->path,
			   mqi->message, NULL, 0, NULL);
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
gboolean
libbalsa_message_send(LibBalsaMessage* message, LibBalsaMailbox* outbox,
		      LibBalsaMailbox* fccbox, gint encoding, 
		      gchar* smtp_server, gint smtp_port)
{

    if (message != NULL)
	libbalsa_message_queue(message, outbox, fccbox, encoding);
    return libbalsa_process_queue(outbox, encoding, smtp_server, smtp_port);
}

/* libbalsa_process_queue:
   treats given mailbox as a set of messages to send. Loads them up and
   launches sending thread/routine.
   NOTE that we do not close outbox after reading. send_real/thread message 
   handler does that.
*/
gboolean 
libbalsa_process_queue(LibBalsaMailbox* outbox, gint encoding, 
		       gchar* smtp_server, gint smtp_port)
{
    MessageQueueItem *mqi, *new_message;
    SendMessageInfo *send_message_info;
    GList *lista;
    LibBalsaMessage *queu;
    /* We do messages in queue now only if where are not sending them already */

#ifdef BALSA_USE_THREADS
    gboolean start_thread;
    GtkWidget *send_dialog_source = NULL;

    pthread_mutex_lock(&send_messages_lock);
    if (sending_mail == FALSE) {
	/* We create here the progress bar */
	send_dialog = gnome_dialog_new("Sending Mail...", "Hide", NULL);
	gtk_window_set_wmclass(GTK_WINDOW(send_dialog), "send_dialog", "Balsa");
	gnome_dialog_set_close(GNOME_DIALOG(send_dialog), TRUE);
	send_dialog_source = gtk_label_new("Sending Mail....");
	gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(send_dialog)->vbox),
			   send_dialog_source, FALSE, FALSE, 0);

	send_progress_message = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(send_dialog)->vbox),
			   send_progress_message, FALSE, FALSE, 0);

	send_dialog_bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(send_dialog)->vbox),
			   send_dialog_bar, FALSE, FALSE, 0);

	gtk_widget_show_all(send_dialog);
	/* Progress bar done */
#endif
	libbalsa_mailbox_open(outbox, FALSE);
	lista = outbox->message_list;
	
	mqi = message_queue;
	while (lista != NULL) {
	    queu = LIBBALSA_MESSAGE(lista->data);
	    
	    new_message = msg_queue_item_new(queu);
	    if (!libbalsa_create_msg(queu, new_message->message,
				     new_message->tempfile, encoding, 1)) {
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
#ifdef BALSA_USE_THREADS
    }
    
    start_thread = !sending_mail;
    sending_mail = TRUE;

    if (start_thread) {

	send_message_info=send_message_info_new(outbox, smtp_server, smtp_port);

	pthread_create(&send_mail, NULL,
		       (void *) &balsa_send_message_real, send_message_info);
	/* Detach so we don't need to pthread_join
	 * This means that all resources will be
	 * reclaimed as soon as the thread exits
	 */
	pthread_detach(send_mail);
    }
    pthread_mutex_unlock(&send_messages_lock);
#else				/*non-threaded code */
    
    send_message_info=send_message_info_new(outbox, smtp_server, smtp_port);
    balsa_send_message_real(send_message_info);
#endif
    return TRUE;
}

static void
handle_successful_send(MessageQueueItem *mqi)
{
    if (mqi->orig->mailbox)
	libbalsa_message_delete(mqi->orig);
    mqi->status = MQI_SENT;
}

/* get_msg2send: 
   returns first waiting message on the message_queue.
*/
static MessageQueueItem* get_msg2send()
{
    MessageQueueItem* res = message_queue;
#ifdef BALSA_USE_THREADS
    pthread_mutex_lock(&send_messages_lock);
#endif
    while(res && res->status != MQI_WAITING)
	res = res->next_message;

#ifdef BALSA_USE_THREADS
    pthread_mutex_unlock(&send_messages_lock);
#endif
    return res;
}

/* balsa_send_message_real:
   does the acutal message sending. 
   This function may be called as a thread and should therefore do
   proper gdk_threads_{enter/leave} stuff around GTK,libbalsa or
   libmutt calls. Also, structure info should be freed before exiting.
*/


static guint balsa_send_message_real(SendMessageInfo* info) {
    MessageQueueItem *mqi, *next_message;
    int i;
#ifdef BALSA_USE_THREADS
    SendThreadMessage *threadmsg;
    pthread_mutex_lock(&send_messages_lock);
    if (!message_queue) {
	sending_mail = FALSE;
	pthread_mutex_unlock(&send_messages_lock);
	MSGSENDTHREAD(threadmsg, MSGSENDTHREADFINISHED, "", NULL, NULL, 0);
	send_message_info_destroy(info);	
	return TRUE;
    }
    pthread_mutex_unlock(&send_messages_lock);
#else
    if(!message_queue){
	send_message_info_destroy(info);	
	return TRUE;
    }	
#endif

    if (!info->server) {	
	while ( (mqi = get_msg2send()) != NULL) {
	    libbalsa_lock_mutt();
	    i = mutt_invoke_sendmail(mqi->message->env->to,
				     mqi->message->env->cc,
				     mqi->message->env->bcc,
				     mqi->tempfile,
				     (mqi->message->content->encoding
				      == ENC8BIT));
	    libbalsa_unlock_mutt();
	    mqi->status = (i==0?MQI_SENT : MQI_FAILED);
	    total_messages_left--; /* whatever the status is, one less to do*/
	}
    } else {
	i = libbalsa_smtp_send(info->server, info->port);
	
/* We give a message to the user because an error has ocurred in the protocol
 * A mistyped address? A server not allowing relay? We can pop a window to ask
 */
	if (i == -2)
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				 _("SMTP protocol error. Cannot relay.\n"
				   "Your message is left in Outbox."));
    }
    /* We give back all the resources used and delete the sent messages */
    
#ifdef BALSA_USE_THREADS
    pthread_mutex_lock(&send_messages_lock);
#endif
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
    libbalsa_mailbox_commit_changes(info->outbox);
    gdk_threads_leave();

    message_queue = NULL;
    total_messages_left = 0;
#ifdef BALSA_USE_THREADS
    sending_mail = FALSE;
    pthread_mutex_unlock(&send_messages_lock);
    MSGSENDTHREAD(threadmsg, MSGSENDTHREADFINISHED, "", NULL, NULL, 0);
    pthread_exit(0);
#endif
    send_message_info_destroy(info);	
    return TRUE;
}

static void
message2HEADER(LibBalsaMessage * message, HEADER * hdr) {
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

    libbalsa_unlock_mutt();

    tmp = libbalsa_address_to_gchar(message->from, 0);

    libbalsa_lock_mutt();
    hdr->env->from = rfc822_parse_adrlist(hdr->env->from, tmp);
    libbalsa_unlock_mutt();

    g_free(tmp);

    if (message->reply_to) {
	tmp = libbalsa_address_to_gchar(message->reply_to, 0);

	libbalsa_lock_mutt();
	hdr->env->reply_to =
	    rfc822_parse_adrlist(hdr->env->reply_to, tmp);
	libbalsa_unlock_mutt();

	g_free(tmp);
    }

    if (message->dispnotify_to) {
	tmp = libbalsa_address_to_gchar(message->dispnotify_to, 0);

	libbalsa_lock_mutt();
	hdr->env->dispnotify_to =
	    rfc822_parse_adrlist(hdr->env->dispnotify_to, tmp);
	libbalsa_unlock_mutt();

	g_free(tmp);
    }

    hdr->env->subject = g_strdup(message->subject);

    /* This continuous lock/unlock business is because 
     * we can't call libbalsa API funcs with the 
     * mutt lock held. grr 
     */
    tmp = libbalsa_make_string_from_list(message->to_list);
    libbalsa_lock_mutt();
    hdr->env->to = rfc822_parse_adrlist(hdr->env->to, tmp);
    libbalsa_unlock_mutt();
    g_free(tmp);

    tmp = libbalsa_make_string_from_list(message->cc_list);
    libbalsa_lock_mutt();
    hdr->env->cc = rfc822_parse_adrlist(hdr->env->cc, tmp);
    libbalsa_unlock_mutt();
    g_free(tmp);

    tmp = libbalsa_make_string_from_list(message->bcc_list);
    libbalsa_lock_mutt();
    hdr->env->bcc = rfc822_parse_adrlist(hdr->env->bcc, tmp);
    libbalsa_unlock_mutt();
    g_free(tmp);

}

gboolean
libbalsa_message_postpone(LibBalsaMessage * message,
			  LibBalsaMailbox * draftbox,
			  LibBalsaMessage * reply_message,
			  gchar * fcc, gint encoding) {
    HEADER *msg;
    BODY *last, *newbdy;
    gchar *tmp;
    LibBalsaMessageBody *body;

    libbalsa_lock_mutt();
    msg = mutt_new_header();
    libbalsa_unlock_mutt();

    message2HEADER(message, msg);

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
	} else if (body->buffer) {
	    newbdy = add_mutt_body_plain(body->charset, encoding);
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

    libbalsa_lock_mutt();
    if (msg->content) {
	if (msg->content->next)
	    msg->content = mutt_make_multipart(msg->content);
    }

    mutt_prepare_envelope(msg->env);
    encode_descriptions(msg->content);

    if (reply_message != NULL)
	/* Just saves the message ID, mailbox type and mailbox name. We could
	     * search all mailboxes for the ID but that would not be too fast. We
	     * could also add more stuff ID like path, server, ... without this
	     * if you change the name of the mailbox the flag will not be set. */
	tmp = g_strdup_printf("%s\r%s",
			      reply_message->message_id,
			      reply_message->mailbox->name);
    else
	tmp = NULL;

    mutt_write_fcc(LIBBALSA_MAILBOX_LOCAL(draftbox)->path,
		   msg, tmp, 1, fcc);
    g_free(tmp);
    mutt_free_header(&msg);
    libbalsa_unlock_mutt();

    if (draftbox->open_ref > 0)
	libbalsa_mailbox_check(draftbox);

    return TRUE;
}

/* In smtp_answer the check the answer and give back 1 if is a positive answer
 * or a 0 if it is negative, in that case we need to copy the message to be 
 * send later. The error will be shown to the user from this funtion */

static int smtp_answer(int fd) {
    char buffer[512];	/* Maximum allowed by RFC */
    char code[4];	/* we use the 3 number code to check the answer */

    read(fd, buffer, sizeof(buffer));

    /* now we check the posible answers */
    code[3] = 0;
    strncpy(code, buffer, 3);

    /* I have to check all posible positive code in RFC. Maybe it can be use an
     * if sentence */

    switch (atoi(code)) {
    case 354:
    case 220:
    case 250:
    case 251:
	return 1;
    default:
	fprintf(stderr, "%s", buffer);
	return 0;
    }
}

/* This funtion recives as arguments the message (to use headers),
 * a file were the message is in MIME format (converted using libmutt)
 * and the server (so it will be easier to add support for multiple 
 * personalityes) and it returns 1 if everything is ok or 0 if something
 * went wrong. */
static int libbalsa_smtp_protocol(int s, char *tempfile,
				  HEADER * msg) {
    char message[500], buffer[525];	/* Maximum allow by RFC */
    int fp, left, len;
    char *tmp, *tmpbuffer;
    ADDRESS *address;
#ifdef BALSA_USE_THREADS
    int total, sent = 0;
    struct stat st;
    float percent = 0;
    SendThreadMessage *progress_message;
    char send_message[100];
#endif

    snprintf(buffer, 512, "MAIL FROM:<%s>\r\n", msg->env->from->mailbox);
    write(s, buffer, strlen(buffer));

    if (smtp_answer(s) == 0) {
	fprintf(stderr, "%s", buffer);
	return 0;
    }

    address = msg->env->to;
    while (address != NULL) {
	snprintf(buffer, 512, "RCPT TO:<%s>\r\n", address->mailbox);
	write(s, buffer, strlen(buffer));
	address = address->next;
	/* We check for a positive answer */

	if (smtp_answer(s) == 0) {
	    fprintf(stderr, "%s", buffer);
	    return 0;
	}

	/*  let's go to the next address */

    }

    address = msg->env->cc;
    while (address != NULL) {
	snprintf(buffer, sizeof(buffer), "RCPT TO:<%s>\r\n",
		 address->mailbox);
	write(s, buffer, strlen(buffer));
	address = address->next;

	if (smtp_answer(s) == 0) {
	    fprintf(stderr, "%s", buffer);
	    return 0;
	}
    }

    address = msg->env->bcc;
    while (address != NULL) {
	snprintf(buffer, sizeof(buffer), "RCPT TO:<%s>\r\n",
		 address->mailbox);
	write(s, buffer, strlen(buffer));
	address = address->next;

	if (smtp_answer(s) == 0) {
	    fprintf(stderr, "%s", buffer);
	    return 0;
	}
    }


    /* Now we are ready to send the message */

#ifdef BALSA_USE_THREADS
    sprintf(send_message, "Messages to be sent: %d ", total_messages_left);
    MSGSENDTHREAD(progress_message, MSGSENDTHREADPROGRESS,
		  send_message, NULL, NULL, 0);
#endif

    snprintf(buffer, 512, "DATA\r\n");
    write(s, buffer, strlen(buffer));
    if (smtp_answer(s) == 0) {
	fprintf(stderr, "%s", buffer);
	return 0;
    }

    if ((fp = open(tempfile, O_RDONLY)) == -1)
	return -1;

#ifdef BALSA_USE_THREADS
    lstat(tempfile, &st);

    total = (int) st.st_size;
#endif

    while ((left = (int) read(fp, message, 499)) > 0) {
	message[left] = '\0';
	tmpbuffer = message;
	while ((tmp = strstr(tmpbuffer, "\n")) != NULL) {
	    if (strncmp(tmpbuffer, ".\n", 2) == 0)
		write(s, ".", 1);	/* make sure dots won't hurt */
	    len = (int) strcspn(tmpbuffer, "\n");

	    if (*(tmp - 1) != '\r') {
		write(s, tmpbuffer, len);
		write(s, "\r\n", 2);
	    } else {
		write(s, tmpbuffer, len);
		write(s, "\n", 1);
	    }
#ifdef BALSA_USE_THREADS
	    sent += len;
#endif
	    tmpbuffer = tmp + 1;
	    left = left - (len + 1);
	    if (left <= 0)
		break;
	}

	write(s, tmpbuffer, left);

#ifdef BALSA_USE_THREADS
	sent += left;
	percent = ((float) sent) / (float) total;
	MSGSENDTHREAD(progress_message, MSGSENDTHREADPROGRESS, "",
		      NULL, NULL, percent);
#endif
    }

    snprintf(buffer, sizeof(buffer), "\r\n.\r\n");
    write(s, buffer, strlen(buffer));

    if (smtp_answer(s) == 0) {
	fprintf(stderr, "%s", buffer);
	return 0;
    }

    close(fp);

    return 1;
}

/* The code of returning answer is the following:
 * -1    Error when conecting to the smtp server (this includes until
 *        salutation is over and we are ready to start with smtp protocol)
 * -2    Error during protocol 
 * 1     Everything when perfect */
/* Does not expect the GDK lock to be held */
static int libbalsa_smtp_send(const char *server, const int port) {

    struct sockaddr_in sin;
    struct hostent *he;
    char buffer[525];	/* Maximum allow by RFC */
    int n, s, error = 0;
    MessageQueueItem *current_message;
#ifdef BALSA_USE_THREADS
    SendThreadMessage *finish_message;
#endif

    g_return_val_if_fail(server, -1);
    /* Here we make the conecction */
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    memset((char *) &sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;

    if (port > 0)
	sin.sin_port = htons(port);
    else 	    
	sin.sin_port = htons(25);

    if ((n = inet_addr(server)) == -1) {
	/* Must be a DNS name */
	if ((he = gethostbyname(server)) == NULL) {
	    libbalsa_information(
		LIBBALSA_INFORMATION_WARNING,
		_("Error: Could not find address for host %s."), server);
	    return -1;
	}
	memcpy((void *) &sin.sin_addr, *(he->h_addr_list),
	       he->h_length);
    } else {
	memcpy((void *) &sin.sin_addr, (void *) &n, sizeof(n));
    }

    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
			 _("Connecting to %s"), inet_ntoa(sin.sin_addr));

    if (connect
	(s, (struct sockaddr *) &sin,
	 sizeof(struct sockaddr_in)) == -1) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Error connecting to %s."), server);
	return -1;
    }

    /* Here we have to receive whatever is the initial salutation of the smtp server, 
	 * since now we are not going to make use of the server being esmtp, we don´t care 
	 * about this salutation 
	 */

    if (smtp_answer(s) == 0)
	return -1;

	/* Here I just follow the RFC */

    snprintf(buffer, 512, "HELO %s\r\n", server);
    write(s, buffer, strlen(buffer));

    if (smtp_answer(s) == 0)
	return -2;

    while ( (current_message = get_msg2send())!= NULL) {
	if (libbalsa_smtp_protocol(s, current_message->tempfile,
				   current_message->message) == 0) {
	    current_message->status = MQI_FAILED;
	    error = 1;
	    snprintf(buffer, 512, "RSET %s\r\n", server);
	    write(s, buffer, strlen(buffer));
	    if (smtp_answer(s) == 0)
		return -2;
	} else {
	    current_message->status = MQI_SENT;
	}

#ifdef BALSA_USE_THREADS
	pthread_mutex_lock(&send_messages_lock);
	total_messages_left--;
	pthread_mutex_unlock(&send_messages_lock);
#endif
    }

/* We close the conection */

    snprintf(buffer, sizeof(buffer), "QUIT\r\n");
    write(s, buffer, strlen(buffer));

    close(s);

#ifdef BALSA_USE_THREADS
    MSGSENDTHREAD(finish_message, MSGSENDTHREADFINISHED, "", NULL,
		  NULL, 0);
#endif

    if (error == 1)
	return -2;
    return 1;

}


/* balsa_lookup_mime_type [MBG] 
 *
 * Description: This is a function to use the gnome mime functions to
 * get the type and subtype for later use.  Returns the type, and the
 * subtype in a gchar array, free using g_strfreev()
 * */
gchar **libbalsa_lookup_mime_type(const gchar * path) {
    gchar **tmp;
    const gchar *mime_type;

	
    mime_type =
	gnome_mime_type_or_default(path, "application/octet-stream");
    tmp = g_strsplit(mime_type, "/", 2);

    return tmp;
}
/* balsa_create_msg:
   copies message to msg.
   PS: seems to be broken when queu == 1 - further execution of
   mutt_free_header(mgs) leads to crash.
*/ static gboolean
libbalsa_create_msg(LibBalsaMessage * message, HEADER * msg, char *tmpfile,
		    gint encoding, int queu) {
    BODY *last, *newbdy;
    FILE *tempfp;
    HEADER *msg_tmp;
    MESSAGE *mensaje;
    LIST *in_reply_to;
    LIST *references;
    LibBalsaMessageBody *body;
    GList *list;
    gchar **mime_type;


    message2HEADER(message, msg);

    /* If the message has references set, add them to he envelope */
    if (message->references != NULL) {
	list = message->references;
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
	 * add it to the user headers */ in_reply_to = mutt_new_list();
	in_reply_to->next = msg->env->userhdrs;
	in_reply_to->data =
	    g_strconcat("In-Reply-To: ", message->in_reply_to, NULL);
	msg->env->userhdrs = in_reply_to;
    }


    if (message->mailbox)
	libbalsa_message_body_ref(message);

    body = message->body_list;

    last = msg->content;
    while (last && last->next)
	last = last->next;

    while (body) {
	FILE *tempfp = NULL;
	newbdy = NULL;

	if (body->filename) {
	    if ((newbdy = mutt_make_file_attach(body->filename)) ==
		NULL) {
		g_warning
		    ("Cannot attach file: %s.\nSending without it.",
		     body->filename);
	    } else {

		/* Do this here because we don't want
		 * to use libmutt's mime types */
		mime_type =
		    libbalsa_lookup_mime_type((const gchar *) body->
					      filename);
		newbdy->type = mutt_check_mime_type(mime_type[0]);
		g_free(newbdy->subtype);
		newbdy->subtype = g_strdup(mime_type[1]);
		g_strfreev(mime_type);
	    }
	} else if (body->buffer) {
	    newbdy = add_mutt_body_plain(body->charset, encoding);
	    tempfp = safe_fopen(newbdy->filename, "w+");
	    fputs(body->buffer, tempfp);
	    fclose(tempfp);
	    tempfp = NULL;
	} else {
	    /* safe_free bug patch: steal it! */
	    msg->content = mutt_copy_body(body->mutt_body, NULL);
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

    if (msg->content) {
	if (msg->content->next)
	    msg->content = mutt_make_multipart(msg->content);
    }

    mutt_prepare_envelope(msg->env);

    encode_descriptions(msg->content);

/* We create the message in MIME format here, we use the same format 
 * for local delivery that for SMTP */
    if (queu == 0) {
	mutt_mktemp(tmpfile);
	if ((tempfp = safe_fopen(tmpfile, "w")) == NULL)
	    return (-1);

	mutt_write_rfc822_header(tempfp, msg->env, msg->content, -1);
	fputc('\n', tempfp);	/* tie off the header. */

	if ((mutt_write_mime_body(msg->content, tempfp) == -1)) {
	    fclose(tempfp);
	    unlink(tmpfile);
	    return (-1);
	}
	fputc('\n', tempfp);	/* tie off the body. */

	if (fclose(tempfp) != 0) {
	    mutt_perror(tmpfile);
	    unlink(tmpfile);
	    return (-1);
	}

    } else {
	/* the message is in the queue */

	msg_tmp =
	    CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];
	mutt_parse_mime_message(CLIENT_CONTEXT(message->mailbox),
				msg_tmp);
	mensaje =
	    mx_open_message(CLIENT_CONTEXT(message->mailbox),
			    msg_tmp->msgno);

	mutt_mktemp(tmpfile);
	if ((tempfp = safe_fopen(tmpfile, "w")) == NULL)
	    return FALSE;

	_mutt_copy_message(tempfp, mensaje->fp, msg_tmp,
			   msg_tmp->content, 0, CH_NOSTATUS);

	if (fclose(tempfp) != 0) {
	    mutt_perror(tmpfile);
	    unlink(tmpfile);
	    if (message->mailbox)
		libbalsa_message_body_unref(message);
	    return FALSE;
	}

    }

    if (message->mailbox)
	libbalsa_message_body_unref(message);

    return TRUE;
}
