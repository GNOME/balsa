/* Balsa E-Mail Client  
 * Copyright (C) 1997-1999 Stuart Parmenter
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

#include <stdio.h>
#include <string.h>
#include <gnome.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "../src/balsa-app.h"
#include "mailbox.h"
#include "misc.h"
#include "mailbackend.h"
#include "send.h"
#include "libbalsa_private.h"

#include "mime.h"

/* This is temporary */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>

#ifdef BALSA_USE_THREADS
#include <threads.h>
#endif

typedef struct _MessageQueueItem        MessageQueueItem;

struct _MessageQueueItem
{
  Message *orig;
  HEADER *message;
  MessageQueueItem *next_message;
  Mailbox *fcc;
  char tempfile[_POSIX_PATH_MAX];
  int delete;
} ;

MessageQueueItem *last_message;
int total_messages_left;

/* prototype this so that this file doesn't whine.  this function isn't in
 * mutt any longer, so we had to provide it inside mutt for libmutt :-)
 */
int mutt_send_message (HEADER * msg);
guint balsa_send_message_real(MessageQueueItem *first_message);

static void encode_descriptions (BODY * b);
int balsa_smtp_send  (MessageQueueItem *first_message, char *server);
int balsa_smtp_protocol (int s, char *tempfile, HEADER *msg);
gboolean balsa_create_msg (Message * message, HEADER *msg, char *tempfile, int queu);

#ifdef BALSA_USE_THREADS
void balsa_send_thread(MessageQueueItem *first_message);
#endif

BODY *add_mutt_body_plain (void);

/* from mutt's send.c */
static void
encode_descriptions (BODY * b)
{
  BODY *t;
  char tmp[LONG_STRING];

  for (t = b; t; t = t->next)
    {
      if (t->description)
	{
	  rfc2047_encode_string (tmp, sizeof (tmp), (unsigned char *) t->description
	    );
	  safe_free ((void **) &t->description);
	  t->description = safe_strdup (tmp);
	}
      if (t->parts)
	encode_descriptions (t->parts);
    }
}


BODY *
add_mutt_body_plain (void)
{
  BODY *body;
  gchar buffer[PATH_MAX];

  body = mutt_new_body ();

  body->type = TYPETEXT;
  body->subtype = g_strdup ("plain");
  body->unlink = 1;
  body->use_disp = 0;
  
  body->encoding = balsa_app.encoding_style;
  body->parameter = mutt_new_parameter();
  body->parameter->attribute = g_strdup("charset");
  body->parameter->value = g_strdup(balsa_app.charset);
  body->parameter->next = NULL;

  mutt_mktemp (buffer);
  body->filename = g_strdup (buffer);
  mutt_update_encoding (body);

  return body;
}

//  GtkWidget *send_dialog = NULL;

gboolean
balsa_send_message (Message * message)
{
  MessageQueueItem *first_message, *current_message , *new_message;
  GList *lista;
  Message *queu;
  int message_number = 0;
#ifdef BALSA_USE_THREADS
  GtkWidget *send_dialog_source = NULL;
#endif  
//  GtkWidget *send_dialog_message = NULL;

fprintf(stderr,"Comienzo la funcion\n");

  if (message != NULL )
  {
      first_message = malloc( sizeof( MessageQueueItem ) );
      first_message->orig = message;
      first_message->next_message = NULL;
      first_message->delete = 0;
      first_message->message = mutt_new_header ();
      balsa_create_msg (message,first_message->message,
		            first_message->tempfile,0);
      first_message->fcc = message->fcc_mailbox;
      
      message_number++;
  }
  else
      first_message = NULL;

  current_message = first_message ;
  

  /* We do messages in queu now only if where are not sending them already */


#ifdef BALSA_USE_THREADS

  pthread_mutex_lock( &send_messages_lock );
  
  if (sending_mail == FALSE )
  {

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
         balsa_mailbox_open (balsa_app.outbox);
  
         if (balsa_app.outbox->message_list != NULL)
             lista = balsa_app.outbox->message_list ;
         else
             lista = NULL ;
  
         while (lista != NULL)
         {
 
                queu = LIBBALSA_MESSAGE(lista->data);

                new_message = malloc( sizeof( MessageQueueItem ) );
                new_message->message = mutt_new_header ();
	        new_message->delete = 0;
	 
	        balsa_create_msg (queu,new_message->message,
				      new_message->tempfile,1);

	        new_message->fcc = queu->fcc_mailbox;
	        new_message->orig = queu;
	        new_message->next_message = NULL ;

	        if(current_message)
		      current_message->next_message = new_message ;
	        else
		      first_message = new_message;
	 
	        current_message = new_message;
	        last_message = new_message;
	 
	        lista = lista->next ;  
		message_number++;
	 }
	 
	 balsa_mailbox_close (balsa_app.outbox);
	 
#ifdef BALSA_USE_THREADS	
   }

      pthread_mutex_unlock( &send_messages_lock );
#endif


  
#ifdef BALSA_USE_THREADS
  
  
  pthread_mutex_lock( &send_messages_lock );
  total_messages_left =  total_messages_left + message_number;
    
  if( sending_mail == TRUE )
    {
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

guint
balsa_send_message_real(MessageQueueItem *first_message)
{
#ifdef BALSA_USE_THREADS
  SendThreadMessage *threadmsg, *delete_message ;
#endif
  MessageQueueItem *current_message, *next_message;
  int i;

  if( !first_message )
    return TRUE;
  
  if (balsa_app.smtp) 
  {
      i = balsa_smtp_send (first_message,balsa_app.smtp_server);
  
#ifdef BALSA_USE_THREADS

      if (i == -1)
      {
            pthread_mutex_lock( &send_messages_lock );
            sending_mail = FALSE;
	    total_messages_left = 0;
            pthread_mutex_unlock( &send_messages_lock );
            
	    MSGSENDTHREAD (threadmsg, MSGSENDTHREADFINISHED,"",NULL,NULL,0); 
	    
	    
      }
#endif
  
  }
  else
  {	 

#ifdef BALSA_USE_THREADS

        pthread_mutex_lock( &send_messages_lock );
        sending_mail = FALSE;
        total_messages_left = 0;
        pthread_mutex_unlock( &send_messages_lock );
#endif
		
     i = mutt_invoke_sendmail (first_message->message->env->to, 
		   first_message->message->env->cc, 
		   first_message->message->env->bcc, first_message->tempfile,
                   (first_message->message->content->encoding == ENC8BIT));
 
     if (i != 0 )
     {
            mutt_write_fcc (MAILBOX_LOCAL (balsa_app.outbox)->path, 
			    first_message->message, NULL, 1, NULL);

            if (balsa_app.outbox->open_ref > 0)
            {
	          mailbox_check_new_messages(balsa_app.outbox);
#ifdef BALSA_USE_THREADS
	          MSGSENDTHREAD(threadmsg, MSGSENDTHREADLOAD, 
				  "Load Sent/Outbox", NULL, 
				  balsa_app.outbox,0 );
#endif
             }

     }
     else
     {	     
     	if ((balsa_app.sentbox->type == MAILBOX_MAILDIR ||
                balsa_app.sentbox->type == MAILBOX_MH ||
             	balsa_app.sentbox->type == MAILBOX_MBOX) &&
                first_message->fcc != NULL) 
     	{
            mutt_write_fcc (MAILBOX_LOCAL (first_message->fcc)->path, 
			    first_message->message, NULL, 0, NULL);

            if (first_message->fcc->open_ref > 0)
	    {
	          mailbox_check_new_messages( first_message->fcc );
#ifdef BALSA_USE_THREADS
	          MSGSENDTHREAD(threadmsg, MSGSENDTHREADLOAD, 
			  "Load Sent/Outbox", NULL, first_message->fcc,0 );
#endif
	    }

         }

     }
     
     unlink (first_message->tempfile);
     mutt_free_header (&first_message->message);
     free( first_message );

#ifdef BALSA_USE_THREADS

       MSGSENDTHREAD (threadmsg, MSGSENDTHREADFINISHED,"",NULL,NULL,0);

       pthread_exit(0);
#endif
     
     return TRUE;

  }

  /* We give a message to the user because an error has ocurred in the protocol
   * A mistyped address? A server not allowing relay? We can pop a window to ask */
       
   if (i==-2)
        fprintf(stderr,"Something has happened in the SMTP protocol\n");
       
//   return TRUE;

  /* We give back all the resources used and delete the messages send*/
            
    current_message = first_message;
    
    while (current_message != NULL)
    {
	   
         if (current_message->delete == 1)
         {
	         if ((balsa_app.sentbox->type == MAILBOX_MAILDIR ||
	                 balsa_app.sentbox->type == MAILBOX_MH ||
	                 balsa_app.sentbox->type == MAILBOX_MBOX) )
			 

		       	mutt_write_fcc (
				MAILBOX_LOCAL (balsa_app.sentbox)->path,
				current_message->message, NULL, 0, NULL);
	       

#ifdef BALSA_USE_THREADS
		 MSGSENDTHREAD(delete_message, MSGSENDTHREADDELETE," ",
		                         current_message->orig, NULL, 0);
#endif
		 
	 }
	 else
         {
		 if(current_message->orig->mailbox == NULL)
	               mutt_write_fcc (
			       MAILBOX_LOCAL (balsa_app.outbox)->path,
			       current_message->message, NULL, 0, NULL);
	 }
	
	unlink (current_message->tempfile); 
        next_message = current_message->next_message;
        mutt_free_header (&current_message->message);
        
	current_message = next_message;
    }
    
#ifdef BALSA_USE_THREADS
    MSGSENDTHREAD(delete_message, MSGSENDTHREADDELETE, "LAST",
                             NULL, NULL, 0);
    
    pthread_exit(0); 
  
#endif

   return TRUE;
}



gboolean
balsa_postpone_message (Message * message, Message * reply_message, 
                        gchar * fcc)
{
  HEADER *msg;
  BODY *last, *newbdy;
  gchar *tmp;
  GList *list;

  msg = mutt_new_header ();

  if (!msg->env)
    msg->env = mutt_new_envelope ();

  msg->env->userhdrs = mutt_new_list ();
  {
    LIST *sptr = UserHeader;
    LIST *dptr = msg->env->userhdrs;
    LIST *delptr = 0;
    while (sptr)
      {
	dptr->data = g_strdup (sptr->data);
	sptr = sptr->next;
	delptr = dptr;
	dptr->next = mutt_new_list ();
	dptr = dptr->next;
      }
    g_free (delptr->next);
    delptr->next = 0;
  }

  tmp = address_to_gchar (message->from);
  msg->env->from = rfc822_parse_adrlist (msg->env->from, tmp);
  g_free (tmp);

  if(message->reply_to) {
     tmp = address_to_gchar (message->reply_to);
     msg->env->reply_to = rfc822_parse_adrlist (msg->env->reply_to, tmp);
     g_free (tmp);
  }

  msg->env->subject = g_strdup (message->subject);

  msg->env->to = rfc822_parse_adrlist (msg->env->to, make_string_from_list (message->to_list));
  msg->env->cc = rfc822_parse_adrlist (msg->env->cc, make_string_from_list (message->cc_list));
  msg->env->bcc = rfc822_parse_adrlist (msg->env->bcc, make_string_from_list (message->bcc_list));

  list = message->body_list;

  last = msg->content;
  while (last && last->next)
    last = last->next;

  while (list)
    {
      FILE *tempfp = NULL;
      Body *body;
      newbdy = NULL;

      body = list->data;

      if (body->filename)
	newbdy = mutt_make_file_attach (body->filename);

      else if (body->buffer)
	{
	  newbdy = add_mutt_body_plain ();
	  tempfp = safe_fopen (newbdy->filename, "w+");
	  fputs (body->buffer, tempfp);
	  fclose (tempfp);
	  tempfp = NULL;
	}

      if (newbdy)
	{
	  if (last)
	    last->next = newbdy;
	  else
	    msg->content = newbdy;

	  last = newbdy;
	}

      list = list->next;
    }

  if (msg->content)
    {
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
    tmp = g_strdup_printf ("%s\r%d\r%s",
                           reply_message->message_id,
                           reply_message->mailbox->type,
                           reply_message->mailbox->name);
  else
    tmp = NULL;
  mutt_write_fcc (MAILBOX_LOCAL (balsa_app.draftbox)->path, msg, tmp, 1, fcc);

  if (balsa_app.draftbox->open_ref > 0)
    mailbox_check_new_messages (balsa_app.draftbox);
  mutt_free_header (&msg);

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

  switch (atoi(code)) 
    {
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

int balsa_smtp_protocol (int s, char *tempfile, HEADER *msg)
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
  if (smtp_answer (s) == 0)
  {
    fprintf(stderr,"%s",buffer);
    return 0;
  }

  address = msg->env->to;
  while (address != NULL)
  {	  
      snprintf (buffer, 512,"RCPT TO:<%s>\r\n", address->mailbox);
      write (s, buffer, strlen (buffer));
      address=address->next;
    /* We check for a positive answer */
        
      if (smtp_answer (s) == 0)
      {
	 fprintf(stderr,"%s",buffer);
	 return 0;
      }
    
     /*  let's go to the next address */
      
  }

  address = msg->env->cc;
  while (address != NULL)
  {
      snprintf (buffer, sizeof(buffer),"RCPT TO:<%s>\r\n", address->mailbox);
      write (s, buffer, strlen (buffer));
      address=address->next;

      if (smtp_answer (s) == 0)
      {
	 fprintf(stderr,"%s",buffer);
	 return 0;
      }
   }

  address = msg->env->bcc;
  while (address != NULL)
  {       
      snprintf (buffer, sizeof(buffer),"RCPT TO:<%s>\r\n", address->mailbox);
      write (s, buffer, strlen (buffer));
      address=address->next;

      if (smtp_answer (s) == 0)
      {
	 fprintf(stderr,"%s",buffer);
	 return 0;
      }
   }


  /* Now we are ready to send the message */

#ifdef BALSA_USE_THREADS
  
  sprintf (send_message, "Messages to be send: %d ", total_messages_left);
  MSGSENDTHREAD(progress_message, MSGSENDTHREADPROGRESS, send_message,
	                      NULL, NULL, 0);
#endif
  
  snprintf (buffer, 512,"DATA\r\n");
  write (s, buffer, strlen (buffer));
  if (smtp_answer (s) == 0)
  {
     fprintf(stderr,"%s",buffer);	    
     return 0;
  }

  if ((fp=open(tempfile,O_RDONLY))==-1)
    return -1;
  
#ifdef BALSA_USE_THREADS 
  lstat (tempfile, &st);
   
  total = (int)st.st_size;
#endif  

  while ((left = (int)read(fp, message, 499)) > 0)
  {
    message[left] = '\0';
    tmpbuffer = message;
    while ((tmp = strstr(tmpbuffer,"\n"))!=NULL)
    {
	    len = (int)strcspn(tmpbuffer,"\n");
	    
	    if (*(tmp-1)!='\r')
            {
		write (s,tmpbuffer,len);
		write (s, "\r\n",2);
#ifdef BALSA_USE_THREADS
		send = send + len + 2 ;
	    
		percent = (float)send/total ;
/*	        MSGSENDTHREAD(progress_message, MSGSENDTHREADPROGRESS, "",	                         NULL, NULL, percent);
*/			
#endif
	    }
	    else
	    {
		write (s,tmpbuffer,len);
		write (s, "\n",1);
#ifdef BALSA_USE_THREADS
		send = send + len + 1;

		percent = (float)send/total ;
/*		MSGSENDTHREAD(progress_message, MSGSENDTHREADPROGRESS, "",	                         NULL, NULL, percent);
*/			
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

  if (smtp_answer (s) == 0)
  {
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

int balsa_smtp_send (MessageQueueItem *first_message, char *server)
{

  struct sockaddr_in sin;
  struct hostent *he;
  char buffer[525]; /* Maximum allow by RFC */
  int n, s, error = 0, SmtpPort=25;
  MessageQueueItem *current_message;
#ifdef BALSA_USE_THREADS
  SendThreadMessage *error_message, *finish_message;
  char error_msg[256];
/*    char msgbuf[160]; */
/*    MailThreadMessage *threadmsg; */

/*    sprintf( msgbuf, "SMTP: Hola cara de bola"); */
/*    MSGMAILTHREAD( threadmsg, MSGMAILTHREAD_SOURCE, msgbuf ); */

#endif
 

/* Here we make the conecction */  
  s = socket (AF_INET, SOCK_STREAM, IPPROTO_IP);

  memset ((char *) &sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (SmtpPort);

  if ((n = inet_addr (NONULL(server))) == -1)
  {
    /* Must be a DNS name */
    if ((he = gethostbyname (NONULL(balsa_app.smtp_server))) == NULL)
    {
#ifdef BALSA_USE_THREADS
     sprintf(error_msg,"Error: Could not find address for host %s.",server);
     MSGSENDTHREAD(error_message, MSGSENDTHREADERROR,error_msg,NULL,NULL,0); 
#else
     fprintf(stderr,"Error: Could not find address for host %s.\n", server);
#endif
      return -1;
    }
    memcpy ((void *)&sin.sin_addr, *(he->h_addr_list), he->h_length);
  }
  else
    memcpy ((void *)&sin.sin_addr, (void *)&n, sizeof(n));
  
  mutt_message ("Connecting to %s", inet_ntoa (sin.sin_addr));

  if (connect (s, (struct sockaddr *) &sin, sizeof (struct sockaddr_in)) == -1)
  {
#ifdef BALSA_USE_THREADS
     sprintf(error_msg,"Error connecting to %s.",server);
     MSGSENDTHREAD(error_message, MSGSENDTHREADERROR,error_msg,NULL,NULL,0);
#else
    fprintf(stderr,"Error connecting to host\n\n");
#endif
    return -1;
  }
  
/* Here we have to receive whatever is the initial salutation of the smtp server, since now we are not going to make use of the server being esmtp, we don´t care about this salutation */
 
  if (smtp_answer (s) == 0)
    return -1;

/* Here I just follow the RFC */
	  
  snprintf (buffer, 512, "HELO %s\r\n", server);
  write (s, buffer, strlen (buffer));
	  
  if (smtp_answer (s) == 0)
    return -2;

  current_message = first_message;
  while (current_message != NULL)
  {
       if ((balsa_smtp_protocol ( s, current_message->tempfile, 
				       current_message->message)) == 0 )
       {
           error = 1;
           snprintf (buffer, 512, "RSET %s\r\n", server);
           write (s, buffer, strlen (buffer));
           if (smtp_answer (s) == 0)
                return -2;
       }
       else
	   current_message->delete = 1;
       
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


gboolean balsa_create_msg (Message *message, HEADER *msg, char *tmpfile, int queu)
{
  BODY *last, *newbdy;
  gchar *tmp;
  GList *list;
  FILE *tempfp;
  HEADER *msg_tmp;
  MESSAGE *mensaje;
  LIST *in_reply_to;
				
  if (!msg->env)
    msg->env = mutt_new_envelope ();

  msg->env->userhdrs = mutt_new_list ();
  {
    LIST *sptr = UserHeader;
    LIST *dptr = msg->env->userhdrs;
    LIST *delptr = 0;

    while (sptr)
      {
	dptr->data = g_strdup (sptr->data);
	sptr = sptr->next;
	delptr = dptr;
	dptr->next = mutt_new_list ();
	dptr = dptr->next;
      }
    g_free (delptr->next);
    delptr->next = 0;
  }

  tmp = address_to_gchar (message->from);
  msg->env->from = rfc822_parse_adrlist (msg->env->from, tmp);
  g_free (tmp);

  if(message->reply_to) {
     tmp = address_to_gchar (message->reply_to);
     msg->env->reply_to = rfc822_parse_adrlist (msg->env->reply_to, tmp);
     g_free (tmp);
  }

  msg->env->subject = g_strdup (message->subject);

  msg->env->to = rfc822_parse_adrlist (msg->env->to, make_string_from_list (message->to_list));
  msg->env->cc = rfc822_parse_adrlist (msg->env->cc, make_string_from_list (message->cc_list));
  msg->env->bcc = rfc822_parse_adrlist (msg->env->bcc, make_string_from_list (message->bcc_list));

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
  

  if ((list = message->body_list) == NULL )
  {
     message_body_ref (message);
     list = message->body_list;
  }
  
  last = msg->content;
  while (last && last->next)
     last = last->next;

  while (list)
    {
      FILE *tempfp = NULL;
      Body *body;
      newbdy = NULL;

      body = list->data;

      if (body->filename)
	newbdy = mutt_make_file_attach (body->filename);

      else if (body->buffer)
	{
	  newbdy = add_mutt_body_plain ();
	  tempfp = safe_fopen (newbdy->filename, "w+");
	  fputs (body->buffer, tempfp);
	  fclose (tempfp);
	  tempfp = NULL;
	}
      else
        msg->content = body->mutt_body ;

      if (newbdy)
	{
	  if (last)
	    last->next = newbdy;
	  else
	    msg->content = newbdy;

	  last = newbdy;
	}

      list = list->next;
    }

  if (msg->content)
    {
      if (msg->content->next)
	msg->content = mutt_make_multipart (msg->content);
    }

  mutt_prepare_envelope (msg->env);

  encode_descriptions (msg->content);

/* We create the message in MIME format here, we use the same format 
 * for local delivery that for SMTP */
  if (queu == 0)
  {
	mutt_mktemp (tmpfile);
	if ((tempfp = safe_fopen (tmpfile, "w")) == NULL)
 	     return (-1);

	mutt_write_rfc822_header (tempfp, msg->env, msg->content, 0);
	fputc ('\n', tempfp); /* tie off the header. */

	if ((mutt_write_mime_body (msg->content, tempfp) == -1))
	{
 	     fclose(tempfp);
 	     unlink (tmpfile);
  	     return (-1);
   	}
  	fputc ('\n', tempfp); /* tie off the body. */

  	if (fclose (tempfp) != 0)
   	{
    	     mutt_perror (tmpfile);
             unlink (tmpfile);
             return (-1);
        }

  }
  else
  {
   
       msg_tmp = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];  
       mutt_parse_mime_message (CLIENT_CONTEXT (message->mailbox), msg_tmp);
       mensaje = mx_open_message(CLIENT_CONTEXT (message->mailbox),msg_tmp->msgno);
	       
       mutt_mktemp (tmpfile);
       if ((tempfp = safe_fopen (tmpfile, "w")) == NULL)
          return (-1);
       _mutt_copy_message (tempfp, mensaje->fp , msg_tmp, msg_tmp->content ,0 ,0);
		       

       if (fclose (tempfp) != 0)
       {
           mutt_perror (tmpfile);
           unlink (tmpfile);
           return (-1);
       }

  }	 

 return TRUE;
        
}



