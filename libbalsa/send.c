/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
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

#include <arpa/inet.h>
#include <netdb.h>

#include <fcntl.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include <sys/socket.h>

/* FIXME: This balsa dependency must go... */
#include "../src/balsa-app.h"

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

typedef struct _MessageQueueItem        MessageQueueItem;

struct _MessageQueueItem
{
	LibBalsaMessage *orig;
	HEADER *message;
	MessageQueueItem *next_message;
	gchar *fcc;
	char tempfile[_POSIX_PATH_MAX];
	int delete;
};

MessageQueueItem *last_message;
int total_messages_left;

static MessageQueueItem *
msg_queue_item_new(LibBalsaMessage *message) 
{
	MessageQueueItem *mqi;

	mqi = g_new(MessageQueueItem,1);
	mqi->orig = message;
	mqi->message = mutt_new_header ();
	mqi->next_message = NULL;
	mqi->fcc = g_strdup(message->fcc_mailbox);
	mqi->delete = 0;
	mqi->tempfile[0] = '\0';

	return mqi;
}

static void 
msg_queue_item_destroy(MessageQueueItem* mqi) 
{
	if(*mqi->tempfile)
		unlink (mqi->tempfile);
	if(mqi->message)
		mutt_free_header (&mqi->message);
	if(mqi->fcc)
		g_free(mqi->fcc);
	free(mqi);
}

static guint balsa_send_message_real(MessageQueueItem *first_message);
static void encode_descriptions (BODY * b);
static int libbalsa_smtp_send  (MessageQueueItem *first_message, char *server);
static int libbalsa_smtp_protocol (int s, char *tempfile, HEADER *msg);
static gboolean libbalsa_create_msg (LibBalsaMessage * message, HEADER *msg, 
				  char *tempfile, int queu);
gchar** libbalsa_lookup_mime_type (const gchar* path);

#ifdef BALSA_USE_THREADS
void balsa_send_thread(MessageQueueItem *first_message);
#endif


/* from mutt's send.c */
static void
encode_descriptions (BODY * b)
{
	BODY *t;
	char tmp[LONG_STRING];
	
	for (t = b; t; t = t->next) {
		if (t->description) {
			libbalsa_lock_mutt();
			rfc2047_encode_string (tmp, sizeof (tmp), (unsigned char *) t->description);
			safe_free ((void **) &t->description);
			t->description = safe_strdup (tmp);
			libbalsa_unlock_mutt();
		}
		if (t->parts)
			encode_descriptions (t->parts);
	}
}

static BODY *
add_mutt_body_plain (const gchar* charset)
{
	BODY *body;
	gchar buffer[PATH_MAX];

	libbalsa_lock_mutt();
	body = mutt_new_body ();

	body->type = TYPETEXT;
	body->subtype = g_strdup ("plain");
	body->unlink = 1;
	body->use_disp = 0;
  
	body->encoding = balsa_app.encoding_style;
	body->parameter = mutt_new_parameter();
	body->parameter->attribute = g_strdup("charset");
	body->parameter->value = g_strdup(charset ? charset : balsa_app.charset);
	body->parameter->next = NULL;

	mutt_mktemp (buffer);
	body->filename = g_strdup (buffer);
	mutt_update_encoding (body);

	libbalsa_unlock_mutt();

	return body;
}

gboolean
libbalsa_message_send (LibBalsaMessage * message)
{
	MessageQueueItem *first_message, *current_message , *new_message;
	GList *lista;
	LibBalsaMessage *queu;
	int message_number = 0;
#ifdef BALSA_USE_THREADS
	GtkWidget *send_dialog_source = NULL;
#endif  

	if (message != NULL ) {
		first_message = msg_queue_item_new(message);
		if(!libbalsa_create_msg (message,first_message->message,
				      first_message->tempfile,0)) {
			msg_queue_item_destroy(first_message);
			return FALSE;
		}
		message_number++;
	} else {
		first_message = NULL;
	}

	current_message = first_message ;
  
	/* We do messages in queue now only if where are not sending them already */

#ifdef BALSA_USE_THREADS

	pthread_mutex_lock( &send_messages_lock );
	if (sending_mail == FALSE ) {
/* We create here the progress bar */
		send_dialog = gnome_dialog_new("Sending Mail...", "Hide", NULL);

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
							      
		gtk_widget_show_all( send_dialog );
/* Progress bar done */
#endif

		last_message = first_message;
		libbalsa_mailbox_open (balsa_app.outbox, FALSE);
		lista = balsa_app.outbox->message_list;
	
		while (lista != NULL) {
			queu = LIBBALSA_MESSAGE(lista->data);

			new_message = msg_queue_item_new(queu);
			if(!libbalsa_create_msg (queu,new_message->message,
					      new_message->tempfile,1)) {
				msg_queue_item_destroy(new_message);
			} else {
				if(current_message)
					current_message->next_message = new_message ;
				else
					first_message = new_message;
				
				current_message = new_message;
				last_message = new_message;
				message_number++;
			}
			lista = lista->next ;  
		}
	 
		libbalsa_mailbox_close (balsa_app.outbox);
	 
#ifdef BALSA_USE_THREADS	
	}

	total_messages_left =  total_messages_left + message_number;

	if( sending_mail == TRUE ) {
		/* add to the queue of messages waiting to be sent */
		last_message->next_message = first_message;
		pthread_mutex_unlock( &send_messages_lock );
	} else {
		/* start queue of messages to send and initiate thread */
		sending_mail = TRUE;
		pthread_mutex_unlock( &send_messages_lock );
		pthread_create( &send_mail,
				NULL,
				(void *) &balsa_send_message_real,
				first_message );
	}
#else  /*non-threaded code */

	balsa_send_message_real( first_message);

#endif

	return TRUE;
}

/* balsa_send_message_real:
   does the acutal message sending. Suffers from severe memory leaks.
   One of them: local MDA mode, more than one messages to send (some of
   them loaded from outbox). Only first one will be sent and memory released.
   FIXME ...

   This function may be called as a thread and should therefore do
   proper gdk_threads_{enter/leave} stuff around GTK,libbalsa or
   libmutt calls
*/
static guint
balsa_send_message_real(MessageQueueItem *first_message)
{
#ifdef BALSA_USE_THREADS
	SendThreadMessage *threadmsg, *delete_message ;
#endif
	MessageQueueItem *current_message, *next_message;
	LibBalsaMailbox *save_box;
	int i;

	if( !first_message ) {
#ifdef BALSA_USE_THREADS
		sending_mail = FALSE;
#endif
		return TRUE;
	}

	/* the local MDA code is so simple that we handle it in short-circuit
	   fashion.
	*/
	if (!balsa_app.smtp) {	 
#ifdef BALSA_USE_THREADS
		pthread_mutex_lock( &send_messages_lock );
		sending_mail = FALSE;
		total_messages_left = 0;
		pthread_mutex_unlock( &send_messages_lock );
#endif

#ifdef BALSA_USE_THREADS
		gdk_threads_enter();
#endif
		
		libbalsa_lock_mutt();
		i = mutt_invoke_sendmail (first_message->message->env->to, 
					  first_message->message->env->cc, 
					  first_message->message->env->bcc, 
					  first_message->tempfile,
					  (first_message->message->content->encoding == ENC8BIT));
		libbalsa_unlock_mutt();

		if (i != 0 ) 
			save_box = balsa_app.outbox;
		else
			save_box = balsa_find_mbox_by_name(first_message->fcc);
    
		if(save_box && LIBBALSA_IS_MAILBOX_LOCAL(save_box) ) {
			libbalsa_lock_mutt();
			mutt_write_fcc (LIBBALSA_MAILBOX_LOCAL (save_box)->path, 
					first_message->message, NULL, 1, NULL);
			libbalsa_unlock_mutt();

			if (save_box->open_ref > 0) {
				libbalsa_mailbox_check(save_box);
    
			}
		}
		msg_queue_item_destroy( first_message );

#ifdef BALSA_USE_THREADS
		gdk_threads_leave();
#endif
    
#ifdef BALSA_USE_THREADS
		MSGSENDTHREAD (threadmsg, MSGSENDTHREADFINISHED,"",NULL,NULL,0);
		pthread_exit(0);
#endif

    
		return TRUE;
	}
  
	/* The hell of SMTP only code follows below... */
	/* libbalsa_smtp_send doesn't expect the GDK lock to be held */
	i = libbalsa_smtp_send (first_message,balsa_app.smtp_server);

#ifdef BALSA_USE_THREADS
	if (i == -1) {
		pthread_mutex_lock( &send_messages_lock );
		sending_mail = FALSE;
		total_messages_left = 0;
		pthread_mutex_unlock( &send_messages_lock );
    
		MSGSENDTHREAD (threadmsg, MSGSENDTHREADFINISHED,"",NULL,NULL,0); 
	}
#endif
  
	/* We give a message to the user because an error has ocurred in the protocol
	 * A mistyped address? A server not allowing relay? We can pop a window to ask */
  
	if (i==-2)
		fprintf(stderr,"SMTP protocol error (wrong address, relaying denied)\n");
  
	/* We give back all the resources used and delete the sent messages */
  
#ifndef BALSA_USE_THREADS
	libbalsa_mailbox_open(balsa_app.outbox, FALSE);
#endif
	current_message = first_message;
  
	while (current_message != NULL) {
		if (current_message->delete == 1) {
#ifdef BALSA_USE_THREADS
			gdk_threads_enter();
#endif
			save_box = balsa_find_mbox_by_name(current_message->fcc);

#ifdef BALSA_USE_THREADS
			gdk_threads_leave();
#endif 

			if( save_box && LIBBALSA_IS_MAILBOX_LOCAL(save_box) ) {
				libbalsa_lock_mutt();
				mutt_write_fcc (LIBBALSA_MAILBOX_LOCAL (save_box)->path,
						current_message->message, NULL, 0, NULL);
				libbalsa_unlock_mutt();
			}

#ifdef BALSA_USE_THREADS
			MSGSENDTHREAD(delete_message, MSGSENDTHREADDELETE," ",
				      current_message->orig, NULL, 0);
#else
			if(current_message->orig->mailbox)
				libbalsa_message_delete (current_message->orig);
#endif
		} else {
			if(current_message->orig->mailbox == NULL) {
				libbalsa_lock_mutt();
				mutt_write_fcc (
					LIBBALSA_MAILBOX_LOCAL (balsa_app.outbox)->path,
					current_message->message, NULL, 0, NULL);
				libbalsa_unlock_mutt();
			}
		}
		
		next_message = current_message->next_message;
		msg_queue_item_destroy(current_message);
		current_message = next_message;
	}
  
#ifdef BALSA_USE_THREADS
	MSGSENDTHREAD(delete_message, MSGSENDTHREADDELETE, "LAST",
		      NULL, NULL, 0);
	gdk_threads_enter();
	libbalsa_mailbox_close(balsa_app.outbox);
	gdk_threads_leave();
	pthread_exit(0); 
#endif
  
	return TRUE;
}

static void
message2HEADER(LibBalsaMessage * message, HEADER * hdr)
{
	gchar * tmp;

	libbalsa_lock_mutt();

	if (!hdr->env)
		hdr->env = mutt_new_envelope ();
    
	hdr->env->userhdrs = mutt_new_list ();

	{
		LIST *sptr = UserHeader;
		LIST *dptr = hdr->env->userhdrs;
		LIST *delptr = 0;

		while (sptr) {
			dptr->data = g_strdup (sptr->data);
			sptr = sptr->next;
			delptr = dptr;
			dptr->next = mutt_new_list ();
			dptr = dptr->next;
		}
		g_free (delptr->next);
		delptr->next = 0;
	}

	libbalsa_unlock_mutt();

	tmp = libbalsa_address_to_gchar (message->from);

	libbalsa_lock_mutt();
	hdr->env->from = rfc822_parse_adrlist (hdr->env->from, tmp);
	libbalsa_unlock_mutt();

	g_free (tmp);
    
	if(message->reply_to) {
		tmp = libbalsa_address_to_gchar (message->reply_to);

		libbalsa_lock_mutt();
		hdr->env->reply_to = rfc822_parse_adrlist (hdr->env->reply_to, tmp);
		libbalsa_unlock_mutt();

		g_free (tmp);
	}

	hdr->env->subject = g_strdup (message->subject);

	/* This continuous lock/unlock business is because 
	 * we can't call libbalsa API funcs with the 
	 * mutt lock held. grr 
	 */
	tmp = libbalsa_make_string_from_list (message->to_list);
	libbalsa_lock_mutt();
	hdr->env->to = rfc822_parse_adrlist (hdr->env->to, tmp);
	libbalsa_unlock_mutt();
	g_free(tmp);

	tmp = libbalsa_make_string_from_list (message->cc_list);
	libbalsa_lock_mutt();
	hdr->env->cc = rfc822_parse_adrlist (hdr->env->cc, tmp);
	libbalsa_unlock_mutt();
	g_free(tmp);

	tmp = libbalsa_make_string_from_list (message->bcc_list);
	libbalsa_lock_mutt();
	hdr->env->bcc = rfc822_parse_adrlist (hdr->env->bcc, tmp);
	libbalsa_unlock_mutt();
	g_free(tmp);
    
}

gboolean
libbalsa_message_postpone (LibBalsaMessage * message, 
			   LibBalsaMessage * reply_message, gchar * fcc)
{
	HEADER *msg;
	BODY *last, *newbdy;
	gchar *tmp;
	LibBalsaMessageBody *body;

	libbalsa_lock_mutt();
	msg = mutt_new_header ();
	libbalsa_unlock_mutt();

	message2HEADER(message, msg);

	body = message->body_list;

	last = msg->content;
	while (last && last->next)
		last = last->next;

	while ( body ) {
		FILE *tempfp = NULL;
		newbdy = NULL;

		if (body->filename) {
			libbalsa_lock_mutt();
			newbdy = mutt_make_file_attach (body->filename);
			libbalsa_unlock_mutt();
		} else if (body->buffer) {
			newbdy = add_mutt_body_plain (body->charset);
			tempfp = safe_fopen (newbdy->filename, "w+");
			fputs (body->buffer, tempfp);
			fclose (tempfp);
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
			msg->content = mutt_make_multipart (msg->content);
	}

	mutt_prepare_envelope (msg->env);
	encode_descriptions (msg->content);

	if (reply_message != NULL)
		/* Just saves the message ID, mailbox type and mailbox name. We could
		 * search all mailboxes for the ID but that would not be too fast. We
		 * could also add more stuff ID like path, server, ... without this
		 * if you change the name of the mailbox the flag will not be set. */
		tmp = g_strdup_printf ("%s\r%s",
				       reply_message->message_id,
				       reply_message->mailbox->name);
	else
		tmp = NULL;

	mutt_write_fcc (LIBBALSA_MAILBOX_LOCAL (balsa_app.draftbox)->path,
			msg, tmp, 1, fcc);
	g_free(tmp);
	mutt_free_header (&msg);
	libbalsa_unlock_mutt();

	if (balsa_app.draftbox->open_ref > 0)
		libbalsa_mailbox_check (balsa_app.draftbox);

	return TRUE;
}

/* In smtp_answer the check the answer and give back 1 if is a positive answer
 * or a 0 if it is negative, in that case we need to copy the message to be 
 * send later. The error will be shown to the user from this funtion */	 
   
static int smtp_answer (int fd)
{
	char *tmp, buffer[512]; /* Maximum allowed by RFC */
	char code[4]; /* we use the 3 number code to check the answer */
	int bytes = 0;
  
	tmp = buffer; 
		  
	bytes = (int)read (fd, tmp, sizeof(buffer));	  
    
	/* now we check the posible answers */

	strncpy (code, buffer, 3);

  
/* I have to check all posible positive code in RFC. Maybe it can be use an 
 * if sentence */

	switch (atoi(code)) {
	case 354:
	case 220:
	case 250:
	case 251:
		return 1;
	default:
		fprintf(stderr,"%s",buffer);
		return 0;
	}
}

/* This funtion recives as arguments the message (to use headers),
 * a file were the message is in MIME format (converted using libmutt)
 * and the server (so it will be easier to add support for multiple 
 * personalityes) and it returns 1 if everything is ok or 0 if something
 * when wrong  */

static int libbalsa_smtp_protocol (int s, char *tempfile, HEADER *msg)
{
	char message[500], buffer[525]; /* Maximum allow by RFC */
	int fp,left,len;
	char *tmp, *tmpbuffer;
	ADDRESS *address;
#ifdef BALSA_USE_THREADS  
	int total, send = 0;
	struct stat st;
	float percent=0;
	SendThreadMessage *progress_message;
	char send_message[100];
#endif
 
	snprintf (buffer, 512,"MAIL FROM:<%s>\r\n", msg->env->from->mailbox);
	write (s, buffer, strlen (buffer));

	if (smtp_answer (s) == 0) {
		fprintf(stderr,"%s",buffer);
		return 0;
	}

	address = msg->env->to;
	while (address != NULL) {	  
		snprintf (buffer, 512,"RCPT TO:<%s>\r\n", address->mailbox);
		write (s, buffer, strlen (buffer));
		address=address->next;
		/* We check for a positive answer */
        
		if (smtp_answer (s) == 0) {
			fprintf(stderr,"%s",buffer);
			return 0;
		}
    
		/*  let's go to the next address */
      
	}

	address = msg->env->cc;
	while (address != NULL)	{
		snprintf (buffer, sizeof(buffer),"RCPT TO:<%s>\r\n", address->mailbox);
		write (s, buffer, strlen (buffer));
		address=address->next;

		if (smtp_answer (s) == 0) {
			fprintf(stderr,"%s",buffer);
			return 0;
		}
	}

	address = msg->env->bcc;
	while (address != NULL) {       
		snprintf (buffer, sizeof(buffer),"RCPT TO:<%s>\r\n", address->mailbox);
		write (s, buffer, strlen (buffer));
		address=address->next;

		if (smtp_answer (s) == 0) {
			fprintf(stderr,"%s",buffer);
			return 0;
		}
	}


	/* Now we are ready to send the message */

#ifdef BALSA_USE_THREADS
	sprintf (send_message, "Messages to be sent: %d ", total_messages_left);
	MSGSENDTHREAD(progress_message, MSGSENDTHREADPROGRESS, send_message,
		      NULL, NULL, 0);
#endif
  
	snprintf (buffer, 512,"DATA\r\n");
	write (s, buffer, strlen (buffer));
	if (smtp_answer (s) == 0) {
		fprintf(stderr,"%s",buffer);	    
		return 0;
	}

	if ((fp=open(tempfile,O_RDONLY))==-1)
		return -1;
  
#ifdef BALSA_USE_THREADS 
	lstat (tempfile, &st);
   
	total = (int)st.st_size;
#endif  

	while ((left = (int)read(fp, message, 499)) > 0) {
		message[left] = '\0';
		tmpbuffer = message;
		while ((tmp = strstr(tmpbuffer,"\n"))!=NULL) {
			if(strncmp(tmpbuffer, ".\n", 2) == 0) 
				write(s,".",1); /* make sure dots won't hurt */
			len = (int)strcspn(tmpbuffer,"\n");
	    
			if (*(tmp-1)!='\r') {
				write (s,tmpbuffer,len);
				write (s, "\r\n",2);
#ifdef BALSA_USE_THREADS
				send = send + len + 2 ;
	    
				percent = (float)send/total ;
#endif
			} else {
				write (s,tmpbuffer,len);
				write (s, "\n",1);
#ifdef BALSA_USE_THREADS
				send = send + len + 1;

				percent = (float)send/total ;
#endif		
			}
			tmpbuffer=tmp+1;
			left=left-(len+1);
			if (left<=0)
				break;
		}
    
		write (s, tmpbuffer,left); 
    
#ifdef BALSA_USE_THREADS
		send = send + left;
		percent = (float)send/total ;
    
		MSGSENDTHREAD(progress_message, MSGSENDTHREADPROGRESS, "",
			      NULL, NULL, percent);
#endif
	}
  
	snprintf (buffer, sizeof(buffer),"\r\n.\r\n");
	write (s, buffer, strlen (buffer));

	if (smtp_answer (s) == 0) {
		fprintf(stderr,"%s",buffer);
		return 0;
	}
  
	close (fp);
  
	return 1;
}

/* The code of returning answer is the following:
 * -1    Error when conecting to the smtp server (this includes until
 *        salutation is over and we are ready to start with smtp protocol)
 * -2    Error during protocol 
 * 1     Everything when perfect */
/* Does not expect the GDK lock to be held */
static int libbalsa_smtp_send (MessageQueueItem *first_message, char *server)
{

	struct sockaddr_in sin;
	struct hostent *he;
	char buffer[525]; /* Maximum allow by RFC */
	int n, s, error = 0, SmtpPort=25;
	MessageQueueItem *current_message;
#ifdef BALSA_USE_THREADS
	SendThreadMessage *error_message, *finish_message;
	char error_msg[256];
#endif
 
	/* Here we make the conecction */  
	s = socket (AF_INET, SOCK_STREAM, IPPROTO_IP);

	memset ((char *) &sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons (SmtpPort);

	if ((n = inet_addr (NONULL(server))) == -1) {
		/* Must be a DNS name */
		if ((he = gethostbyname (NONULL(balsa_app.smtp_server))) == NULL) {
#ifdef BALSA_USE_THREADS
			sprintf(error_msg, _("Could not find address for host %s."), server);
			MSGSENDTHREAD(error_message, MSGSENDTHREADERROR,error_msg,NULL,NULL,0); 
#else
			libbalsa_information( LIBBALSA_INFORMATION_WARNING, _("Error: Could not find address for host %s."), server);
#endif
			return -1;
		}
		memcpy ((void *)&sin.sin_addr, *(he->h_addr_list), he->h_length);
	} else {
		memcpy ((void *)&sin.sin_addr, (void *)&n, sizeof(n));
	}

	libbalsa_information ( LIBBALSA_INFORMATION_MESSAGE, "Connecting to %s", inet_ntoa (sin.sin_addr));

	if (connect (s, (struct sockaddr *) &sin, sizeof (struct sockaddr_in)) == -1) {
#ifdef BALSA_USE_THREADS
		sprintf(error_msg,"Error connecting to %s.",server);
		MSGSENDTHREAD(error_message, MSGSENDTHREADERROR,error_msg,NULL,NULL,0);
#else
		fprintf(stderr,"Error connecting to host\n\n");
#endif
		return -1;
	}
  
	/* Here we have to receive whatever is the initial salutation of the smtp server, 
	 * since now we are not going to make use of the server being esmtp, we don´t care 
	 * about this salutation 
	 */
 
	if (smtp_answer (s) == 0)
		return -1;

	/* Here I just follow the RFC */
	  
	snprintf (buffer, 512, "HELO %s\r\n", server);
	write (s, buffer, strlen (buffer));
	  
	if (smtp_answer (s) == 0)
		return -2;

	current_message = first_message;
	while (current_message != NULL) {
		if ((libbalsa_smtp_protocol ( s, current_message->tempfile, current_message->message)) == 0 ) {
			error = 1;
			snprintf (buffer, 512, "RSET %s\r\n", server);
			write (s, buffer, strlen (buffer));
			if (smtp_answer (s) == 0)
				return -2;
		} else {
			current_message->delete = 1;
		}

#ifdef BALSA_USE_THREADS
		pthread_mutex_lock( &send_messages_lock );
       
		total_messages_left =  total_messages_left - 1;  
		if (current_message->next_message == NULL)
			sending_mail = FALSE;
       
		pthread_mutex_unlock( &send_messages_lock );
#endif 
		current_message = current_message->next_message;
	}     

/* We close the conection */
  
	snprintf (buffer, sizeof(buffer),"QUIT\r\n");
	write (s, buffer, strlen (buffer));

	close (s);
  
#ifdef BALSA_USE_THREADS
  
	MSGSENDTHREAD (finish_message, MSGSENDTHREADFINISHED,"",NULL,NULL,0); 
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
gchar** libbalsa_lookup_mime_type (const gchar* path)
{
        gchar** tmp;
        const gchar* mime_type;
        
        mime_type = gnome_mime_type_or_default (path, "application/octet-stream");
        tmp = g_strsplit (mime_type , "/", 2 );

        return tmp;
}

/* balsa_create_msg:
   copies message to msg.
   PS: seems to be broken when queu == 1 - further execution of
   mutt_free_header(mgs) leads to crash.
*/
static gboolean 
libbalsa_create_msg (LibBalsaMessage *message, HEADER *msg, char *tmpfile, int queu)
{
	BODY *last, *newbdy;
	FILE *tempfp;
	HEADER *msg_tmp;
	MESSAGE *mensaje;
	LIST *in_reply_to;
	LibBalsaMessageBody *body;

	gchar** mime_type;
	
	message2HEADER(message, msg);

	/* If the message has references set, add them to he envelope */
	if (message->references != NULL) {        
		msg->env->references = mutt_new_list ();
		msg->env->references->next = NULL;
		msg->env->references->data =  g_strdup (message->references);

		/* There's no specific header for In-Reply-To, just add it to the user
		 * headers */
		in_reply_to = mutt_new_list ();
		in_reply_to->next = msg->env->userhdrs;
		in_reply_to->data = g_strconcat("In-Reply-To: ", message->in_reply_to, NULL);
		msg->env->userhdrs = in_reply_to;
	}
  

	if(message->mailbox)
		libbalsa_message_body_ref (message);

	body = message->body_list;
  
	last = msg->content;
	while (last && last->next)
		last = last->next;
  
	while ( body ) {
		FILE *tempfp = NULL;
		newbdy = NULL;

		if (body->filename) {
			if( (newbdy = mutt_make_file_attach (body->filename)) == NULL) {
				g_warning("Cannot attach file: %s.\nSending without it.", 
					  body->filename);
			} else {

				/* Do this here because we don't want to use libmutt's mime
				 * types */
				mime_type = libbalsa_lookup_mime_type ((const gchar*)body->filename);
				newbdy->type = mutt_check_mime_type (mime_type[0]);
				g_free (newbdy->subtype);
				newbdy->subtype = g_strdup(mime_type[1]);
				g_strfreev (mime_type);
			}
		} else if (body->buffer) {
			newbdy = add_mutt_body_plain (body->charset);
			tempfp = safe_fopen (newbdy->filename, "w+");
			fputs (body->buffer, tempfp);
			fclose (tempfp);
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
			msg->content = mutt_make_multipart (msg->content);
	}

	mutt_prepare_envelope (msg->env);

	encode_descriptions (msg->content);

/* We create the message in MIME format here, we use the same format 
 * for local delivery that for SMTP */
	if (queu == 0) {
		mutt_mktemp (tmpfile);
		if ((tempfp = safe_fopen (tmpfile, "w")) == NULL)
			return (-1);

		mutt_write_rfc822_header (tempfp, msg->env, msg->content, 0);
		fputc ('\n', tempfp); /* tie off the header. */

		if ((mutt_write_mime_body (msg->content, tempfp) == -1)) {
			fclose(tempfp);
			unlink (tmpfile);
			return (-1);
		}
		fputc ('\n', tempfp); /* tie off the body. */

		if (fclose (tempfp) != 0) {
			mutt_perror (tmpfile);
			unlink (tmpfile);
			return (-1);
		}

	} else {
		/* the message is in the queue */

		msg_tmp = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];  
		mutt_parse_mime_message (CLIENT_CONTEXT (message->mailbox), msg_tmp);
		mensaje = mx_open_message(CLIENT_CONTEXT (message->mailbox),
					  msg_tmp->msgno);
	       
		mutt_mktemp (tmpfile);
		if ((tempfp = safe_fopen (tmpfile, "w")) == NULL)
			return FALSE;

		_mutt_copy_message (tempfp, mensaje->fp, msg_tmp, msg_tmp->content,
				    0, 0);

		if (fclose (tempfp) != 0) {
			mutt_perror (tmpfile);
			unlink (tmpfile);
			if(message->mailbox)
				libbalsa_message_body_unref (message);
			return FALSE;
		}

	}	 

	if(message->mailbox)
		libbalsa_message_body_unref (message);

	return TRUE;
}

