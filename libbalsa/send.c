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

#include "mime.h"

/* This is temporary */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

//#include <sys/stat.h>
#include <fcntl.h>

#ifdef BALSA_USE_THREADS
#include <threads.h>

typedef struct
{
  HEADER *message;
  void *next_message;
  Mailbox *fcc;
  char tempfile[_POSIX_PATH_MAX];
} MessageQueueItem;

MessageQueueItem *last_message;
#endif

/* prototype this so that this file doesn't whine.  this function isn't in
 * mutt any longer, so we had to provide it inside mutt for libmutt :-)
 */
int mutt_send_message (HEADER * msg);
guint balsa_send_message_real(HEADER *msg, char *tempfile, Mailbox *fcc );
static void encode_descriptions (BODY * b);
int balsa_smtp_send (HEADER *msg, char *file, char *server);
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

gboolean
balsa_send_message (Message * message)
{
  HEADER *msg;
  char tempfile[_POSIX_PATH_MAX];
#ifdef BALSA_USE_THREADS
  MessageQueueItem *new_message;
#endif

  msg = mutt_new_header ();

  balsa_create_msg (message, msg, tempfile, 0);

#ifdef BALSA_USE_THREADS
  new_message = malloc( sizeof( MessageQueueItem ) );

  new_message->message = msg;  

  new_message->fcc = message->fcc_mailbox;

  strncpy( new_message->tempfile, tempfile, sizeof(new_message->tempfile) );

  pthread_mutex_lock( &send_messages_lock );
  if( sending_mail )
    {
      /* add to the queue of messages waiting to be sent */
      last_message->next_message = (void *) new_message;
      last_message = new_message;
      new_message->next_message = NULL;
      pthread_mutex_unlock( &send_messages_lock );
    } else {
      /* start queue of messages to send and initiate thread */
      last_message = new_message;
      new_message->next_message = NULL;
      /* initiate threads */
      sending_mail = TRUE;
      pthread_create( &send_mail,
  		NULL,
  		(void *) &balsa_send_thread,
		new_message );
      pthread_mutex_unlock( &send_messages_lock );
    }
#else  /*non-threaded code */

  balsa_send_message_real( msg, tempfile, message->fcc_mailbox );

#endif

  /* loop through messages in outbox -- moved up from smtp */
#if 0 
  GList *lista;
  Message *message;
  HEADER *queu_msg;
  char file[_POSIX_PATH_MAX];


  /* We do messages in queu now */
   
  balsa_mailbox_open (balsa_app.outbox);
  
  if (balsa_app.outbox->message_list != NULL)
      lista = balsa_app.outbox->message_list ;
  else
      lista = NULL ;
  
  while (lista != NULL)
  {
 
    message = LIBBALSA_MESSAGE(lista->data); 
    queu_msg = mutt_new_header ();
  
    balsa_create_msg (message, queu_msg, file, 1);
  
  /* If something happen we do not delete the message */
    
    if ((balsa_smtp_protocol ( s, file, queu_msg)) == 0 )
    {
       snprintf (buffer, 512, "RSET\r\n", server);
       write (s, buffer, strlen (buffer));
       if (smtp_answer (s) == 0)
          return -1;
    }
    else
    {
       fprintf (stderr, "Mensaje borrado\n");
       message_delete(message);
    }
  
    lista = lista->next ;  
  }
    
  balsa_mailbox_close (balsa_app.outbox);
#endif

  return TRUE;
}

guint
balsa_send_message_real(HEADER *msg, char *tempfile, Mailbox *fcc )
{
  int i;

  if (balsa_app.smtp) 
     i = balsa_smtp_send (msg,tempfile,balsa_app.smtp_server);
  else	        
     i = mutt_invoke_sendmail (msg->env->to, msg->env->cc, msg->env->bcc,
			       tempfile,(msg->content->encoding == ENC8BIT));
  
  if (i==-1)
  {
    mutt_write_fcc (MAILBOX_LOCAL (balsa_app.outbox)->path, msg, NULL, 1);

    if (balsa_app.outbox->open_ref > 0)
	mailbox_check_new_sent(balsa_app.outbox);

    mutt_free_header (&msg);

 /* Since we didn´t send the mail we don´t save it at send_mailbox */
    return TRUE;
  }

  if ((balsa_app.sentbox->type == MAILBOX_MAILDIR ||
       balsa_app.sentbox->type == MAILBOX_MH ||
       balsa_app.sentbox->type == MAILBOX_MBOX) &&
       fcc != NULL) 
    {
      mutt_write_fcc (MAILBOX_LOCAL (fcc)->path, msg, NULL, 0);

      if (fcc->open_ref > 0)
	mailbox_check_new_sent( fcc );
    }
  mutt_free_header (&msg);

  return TRUE;
}

#ifdef BALSA_USE_THREADS
void balsa_send_thread(MessageQueueItem *first_message)
{
  MessageQueueItem *current_message, *next_message;

  pthread_mutex_lock( &send_messages_lock );
  current_message = first_message;

  do
    {
      pthread_mutex_unlock( &send_messages_lock );

      balsa_send_message_real( current_message->message, 
			       current_message->tempfile, 
			       current_message->fcc ); 

      pthread_mutex_lock( &send_messages_lock );
      next_message = (MessageQueueItem *) current_message->next_message;
      free( current_message );
      current_message = next_message;
    } while( current_message );

  sending_mail = FALSE;
  last_message = NULL;
  pthread_mutex_unlock( &send_messages_lock );
  pthread_exit(0); 
}
#endif

gboolean
balsa_postpone_message (Message * message)
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

  tmp = address_to_gchar (message->reply_to);
  msg->env->reply_to = rfc822_parse_adrlist (msg->env->reply_to, tmp);
  g_free (tmp);

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

  mutt_write_fcc (MAILBOX_LOCAL (balsa_app.draftbox)->path, msg, NULL, 1);
  if (balsa_app.draftbox->open_ref > 0)
    mailbox_check_new_messages (balsa_app.draftbox);
  mutt_free_header (&msg);

  return TRUE;
}

/* In smtp_answer the check the answer and give back 1 if is a positive answer or a 0 if it is negative, 
 * in that case we need to copy the message to be send later. The error will be shown to the user from 
 * this funtion */	 
   
static int smtp_answer (int fd)
  {
  char *tmp, buffer[512]; /* Maximum allowed by RFC */
  char code[4]; /* we use the 3 number code to check the answer */
  int bytes = 0;
  
  tmp = buffer; 
		  
  bytes = (int)read (fd, tmp, sizeof(buffer));	  
   
 
  /* now we check the posible answers */

  strncpy (code, buffer, 3);

  
/* I have to check all posible positive code in RFC. Maybe it can be use an if sentence */

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
  char message[1000], buffer[525]; /* Maximum allow by RFC */
  int fp,left,len;
  char *tmp, *tmpbuffer;
  ADDRESS *address;
 
  snprintf (buffer, 512,"MAIL FROM:%s\r\n", msg->env->from->mailbox);
  write (s, buffer, strlen (buffer));
  if (smtp_answer (s) == 0)
  {
    fprintf(stderr,"%s",buffer);
    return 0;
  }

  address = msg->env->to;
  while (address != NULL)
  {	  
      snprintf (buffer, 512,"RCPT TO:%s\r\n", address->mailbox);
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
      snprintf (buffer, sizeof(buffer),"RCPT TO:%s\r\n", address->mailbox);
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
      snprintf (buffer, sizeof(buffer),"RCPT TO:%s\r\n", address->mailbox);
      write (s, buffer, strlen (buffer));
      address=address->next;

      if (smtp_answer (s) == 0)
      {
	 fprintf(stderr,"%s",buffer);
	 return 0;
      }
   }


  /* Now we are ready to send the message */

  snprintf (buffer, 512,"DATA\r\n");
  write (s, buffer, strlen (buffer));
  if (smtp_answer (s) == 0)
  {
     fprintf(stderr,"%s",buffer);	    
     return 0;
  }

  if ((fp=open(tempfile,O_RDONLY))==-1)
    return -1;

  while ((left = (int)read (fp, message, sizeof(message)))!=0)
  {
    tmpbuffer = message;
    while ((tmp = strstr(tmpbuffer,"\n"))!=NULL)
    {
	    len = (int)strcspn(tmpbuffer,"\n");
	    
	    if (*(tmp-1)!='\r')
            {
		write (s,tmpbuffer,len);
		write (s, "\r\n",2);
	    }
	    else
	    {
		write (s,tmpbuffer,len);
		write (s, "\n",1);
	    }
	    tmpbuffer=tmp+1;
	    left=left-(len+1);
	    if (left<=0)
		 break;
    }
    
    write (s, tmpbuffer,left);  
    
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

int balsa_smtp_send (HEADER *msg, char *tempfile, char *server)
{

  struct sockaddr_in sin;
  struct hostent *he;
  char buffer[525]; /* Maximum allow by RFC */
  int n, s, error = 0, SmtpPort=25;
  
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
      fprintf(stderr,"Error: Could not find address for host %s.\n", server);
      //mutt_error ("Could not find address for host %s.", balsa_app.smtp_server);
      return -1;
    }
    memcpy ((void *)&sin.sin_addr, *(he->h_addr_list), he->h_length);
  }
  else
    memcpy ((void *)&sin.sin_addr, (void *)&n, sizeof(n));
  
  mutt_message ("Connecting to %s", inet_ntoa (sin.sin_addr));

  if (connect (s, (struct sockaddr *) &sin, sizeof (struct sockaddr_in)) == -1)
  {
    fprintf(stderr,"Error connecting to host\n\n");
    //mutt_perror ("connect");
    return -1;
  }
  
/* Here we have to receive whatever is the initial salutation of the smtp server, since now we are not going to make use of the server being esmtp, we don´t care about this salutation */
 
  if (smtp_answer (s) == 0)
    return -1;

/* Here I just follow the RFC */
	  
  snprintf (buffer, 512, "HELO %s\r\n", server);
  write (s, buffer, strlen (buffer));
	  
  if (smtp_answer (s) == 0)
    return -1;

  if ((balsa_smtp_protocol ( s, tempfile, msg)) == 0 )
  {
     error = 1;
     snprintf (buffer, 512, "RSET %s\r\n", server);
     write (s, buffer, strlen (buffer));
     if (smtp_answer (s) == 0)
        return -1;
  }
  
  unlink (tempfile);
  
/* We close the conection */
  
  snprintf (buffer, sizeof(buffer),"QUIT\r\n");
  write (s, buffer, strlen (buffer));

  close (s);
  
  if (error == 1)
     return -1;
     
  return 1;
}  


gboolean balsa_create_msg (Message *message, HEADER *msg, char *tmpfile, int queu)
{
  BODY *last, *newbdy;
  gchar *tmp;
  GList *list;
  FILE *tempfp;


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

  tmp = address_to_gchar (message->reply_to);
  msg->env->reply_to = rfc822_parse_adrlist (msg->env->reply_to, tmp);
  g_free (tmp);

  msg->env->subject = g_strdup (message->subject);

  msg->env->to = rfc822_parse_adrlist (msg->env->to, make_string_from_list (message->to_list));
  msg->env->cc = rfc822_parse_adrlist (msg->env->cc, make_string_from_list (message->cc_list));
  msg->env->bcc = rfc822_parse_adrlist (msg->env->bcc, make_string_from_list (message->bcc_list));

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
//	file = &tmpfile;
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
  	switch (message->mailbox->type)
        {
		case MAILBOX_MH:
  		case MAILBOX_MAILDIR:
    		  {
      			  snprintf (tmpfile, PATH_MAX, "%s/%s", MAILBOX_LOCAL (message->mailbox)->path, message_pathname (message));
  //      		  file = tmpfile;
        		  break;
      		  }
                default:
  //                  file = fopen (MAILBOX_LOCAL (message->mailbox)->path, "r");
   	            break;
	}

  }	 

        return TRUE;
        
}



