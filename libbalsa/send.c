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

#include "../src/balsa-app.h"
#include "mailbox.h"
#include "misc.h"
#include "mailbackend.h"
#include "send.h"

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

/* prototype this so that this file doesn't whine.  this function isn't in
 * mutt any longer, so we had to provide it inside mutt for libmutt :-)
 */
int mutt_send_message (HEADER * msg);
static void encode_descriptions (BODY * b);
int balsa_smtp_send (HEADER * msg);

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
  BODY *last, *newbdy;
  gchar *tmp;
  GList *list;
  int i=0;

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

/* FIXME */
  if (balsa_app.smtp)
    i=balsa_smtp_send (msg);
  else
    mutt_send_message (msg);
  
  if (i==-1)
     balsa_postpone_message (message);
	     
  if ((balsa_app.sentbox->type == MAILBOX_MAILDIR ||
       balsa_app.sentbox->type == MAILBOX_MH ||
       balsa_app.sentbox->type == MAILBOX_MBOX) &&
       message->fcc_mailbox != NULL) {
    mutt_write_fcc (MAILBOX_LOCAL (message->fcc_mailbox)->path, msg, NULL, 0);
    if (message->fcc_mailbox->open_ref > 0)
        mailbox_check_new_messages (message->fcc_mailbox);
  }
#if 0
  switch (balsa_app.outbox->type)
    {
    case MAILBOX_MAILDIR:
    case MAILBOX_MH:
    case MAILBOX_MBOX:
      /*
         send_message (msg, MAILBOX_LOCAL(balsa_app.outbox)->path);
       */
      mutt_send_message (msg);
      break;
    case MAILBOX_IMAP:
      mutt_send_message (msg);
      break;
    default:
      break;
    }
#endif

  mutt_free_header (&msg);

  return TRUE;
}

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

/* In smtp_answer the check the answer and give back 1 if is a positive answer or a 0 if it is negative, it that case we need to copy the message to be send later. The error will be shown to the user from this funtion */	 
   
static int smtp_answer (int fd)
  {
  char *tmp, buffer[512]; /* Maximum allowed by RFC */
  char code[4]; /* we use the 3 number code to check the answer */
  int bytes = 0;
  
  tmp=&buffer; 
		  
  bytes=read (fd, tmp, sizeof(buffer));	  
   
 
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

/* This funtion uses the same arguments as mutt_send_message. 
 * I copy part of the mutt_send_message funtion instead of making the call to
 * this funtion in the last minute, because in this way we don't have to 
 * worry about updating libmutt */


int balsa_smtp_send (HEADER * msg)
{

  struct sockaddr_in sin;
  struct hostent *he;
  char message[1000], buffer[525]; /* Maximum allow by RFC */
  int fp,left,len,n,s,SmtpPort=25;
  char *tmp, *tmpbuffer, tempfile[_POSIX_PATH_MAX];
  FILE *tempfp;
  ADDRESS *address;
  
/* This comes from mutt_send_message */ 

  /* Write out the message in MIME form. */
  mutt_mktemp (tempfile);
  if ((tempfp = safe_fopen (tempfile, "w")) == NULL)
      return (-1);

  mutt_write_rfc822_header (tempfp, msg->env, msg->content, 0);
  fputc ('\n', tempfp); /* tie off the header. */

  if ((mutt_write_mime_body (msg->content, tempfp) == -1))
      {
      fclose(tempfp);
      unlink (tempfile);
      return (-1);
      }
  fputc ('\n', tempfp); /* tie off the body. */

  if (fclose (tempfp) != 0)
     {
     mutt_perror (tempfile);
     unlink (tempfile);
     return (-1);
     }
/* Now we have the body in a temporary file */

/* Here we make the conecction */  
  s = socket (AF_INET, SOCK_STREAM, IPPROTO_IP);

  memset ((char *) &sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (SmtpPort);

  if ((n = inet_addr (NONULL(balsa_app.smtp_server))) == -1)
  {
    /* Must be a DNS name */
    if ((he = gethostbyname (NONULL(balsa_app.smtp_server))) == NULL)
    {
      fprintf(stderr,"Error: Could not find address for host %s.", balsa_app.smtp_server);
      //mutt_error ("Could not find address for host %s.", balsa_app.smtp_server);
      return -1;
    }
    memcpy ((void *)&sin.sin_addr, *(he->h_addr_list), he->h_length);
  }
  else
    memcpy ((void *)&sin.sin_addr, (void *)&n, sizeof(n));
  
  //mutt_message ("Connecting to %s", inet_ntoa (sin.sin_addr));

  if (connect (s, (struct sockaddr *) &sin, sizeof (struct sockaddr_in)) == -1)
  {
    fprintf(stderr,"Error connecting");
    //mutt_perror ("connect");
    return -1;
  }
  
/* Here we have to receive whatever is the initial salutation of the smtp server, since now we are not going to make use of the server being esmtp, we don´t care about this salutation */
 
  if (smtp_answer (s) == 0)
    return -1;

/* Here I just follow the RFC */
	  
  snprintf (buffer, 512, "HELO %s\r\n", balsa_app.smtp_server);
  write (s, buffer, strlen (buffer));
	  
  if (smtp_answer (s) == 0)
    return -1;

  snprintf (buffer, 512,"MAIL FROM:%s\r\n", msg->env->from->mailbox);
  write (s, buffer, strlen (buffer));
  if (smtp_answer (s) == 0)
    return -1;

  address=msg->env->to;
  while (address!=NULL)
  {	  
      snprintf (buffer, 512,"RCPT TO: %s\r\n", address->mailbox);
      write (s, buffer, strlen (buffer));
      address=address->next;
    /* We check for a positive answer */    
      if (smtp_answer (s) == 0)
         return -1;
    
     /*  let's go to the next address */
      
  }

  address=msg->env->cc;
  while (address!=NULL)
  {
      snprintf (buffer, sizeof(buffer),"RCPT TO: %s\r\n", address->mailbox);
      write (s, buffer, strlen (buffer));
      address=address->next;

      if (smtp_answer (s) == 0)
          return -1;
   }

  address=msg->env->bcc;
  while (address!=NULL)
  {       
      snprintf (buffer, sizeof(buffer),"RCPT TO:%s\r\n", address->mailbox);
      write (s, buffer, strlen (buffer));
      address=address->next;

      if (smtp_answer (s) == 0)
          return -1;
   }


  /* Now we are ready to send the message */

  snprintf (buffer, 512,"DATA\r\n");
  write (s, buffer, strlen (buffer));
  if (smtp_answer (s) == 0)
    return -1;

  if ((fp=open(tempfile,O_RDONLY))==-1)
    return -1;

  while ((left=read (fp, message, sizeof(message)))!=0)
  {
    tmpbuffer=&message;
    while ((tmp=strstr(tmpbuffer,"\n"))!=NULL)
    {
	    len=strcspn(tmpbuffer,"\n");
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
    return -1;

/* We close the conection */
  
  snprintf (buffer, sizeof(buffer),"QUIT\r\n");
  write (s, buffer, strlen (buffer));

  close (s);
  close (fp);
  unlink (tempfile);
  return 1;
}  

