/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */ 

/*
 * Rather crude POP3 support.
 */

#include "../config.h"
#include "mutt.h"
#include "mailbox.h"
#include "mx.h"

#ifdef BALSA_USE_THREADS
#include "thread_msgs.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

/* XXXXX */
#define BALSA_TEST_POP3 1

static int getLine (int fd, char *s, int len)
{
  char ch;
  int bytes = 0;

  while (read (fd, &ch, 1) > 0)
  {
    *s++ = ch;
    bytes++;
    if (ch == '\n')
    {
      *s = 0;
      #ifdef BALSA_TEST_POP3
      fprintf( stderr, "POP3 L: \"%s\"\n", (char *) (s - bytes) );
      #endif
      return (bytes);
    }
    /* make sure not to overwrite the buffer */
    if (bytes == len - 1)
    {
      *s = 0;
      #ifdef BALSA_TEST_POP3
      fprintf( stderr, "POP3 L: \"%s\"\n", (char *) (s - bytes) );
      #endif
      return bytes;
    }
  }
  *s = 0;
  return (-1);
}

#ifndef LIBMUTT
static int getPass (void)
{
  if (!PopPass)
  {
    char tmp[SHORT_STRING];
    tmp[0] = '\0';
    if (mutt_get_password ("POP Password: ", tmp, sizeof (tmp)) != 0
	|| *tmp == '\0')
      return 0;
    PopPass = safe_strdup (tmp);
  }
  return 1;
}
#endif

#ifdef BALSA_TEST_POP3
#define G_STMT_START ({
#define G_STMT_END })
#define mutt_error( str, args... ) G_STMT_START { fprintf( stderr, "POP3 E: " ); fprintf( stderr, str, ##args ); fprintf( stderr, "\n" ); mutt_error( str, ##args ); } G_STMT_END
#define mutt_message( str, args... ) G_STMT_START { fprintf( stderr, "POP3 M: " ); fprintf( stderr, str, ##args ); fprintf( stderr, "\n" ); mutt_message( str, ##args ); } G_STMT_END
#define mutt_perror( str ) G_STMT_START { fprintf( stderr, "POP3 P: %s\n", (str) ); mutt_perror( str ); } G_STMT_END
#define write( sock, buffer, size ) G_STMT_START { fprintf( stderr, "POP3 O: \"%s\"\n", buffer ); write( sock, buffer, (size) ); } G_STMT_END
#define DM( fmt, args... ) G_STMT_START { fprintf( stderr, "POP3 D: " ); fprintf( stderr, fmt, ##args ); fprintf( stderr, "\n" ); } G_STMT_END
#else
#define DM( fmt, args... ) 
#endif

void mutt_fetchPopMail (void)
{
  struct sockaddr_in sin;
#if SIZEOF_LONG == 4
  long n;
#else
  int n;
#endif
  struct hostent *he;
  char buffer[2048];
  char msgbuf[SHORT_STRING];
#ifdef BALSA_USE_THREADS
  MailThreadMessage *threadmsg;
  char threadbuf[160];
#endif
  char uid[80], last_uid[80];
  int s, i, last = 0, tmp, total, msgs, bytes, err = 0;
  int num_bytes, tot_bytes;
  int first_msg;

  CONTEXT ctx;
  MESSAGE *msg = NULL;

  DM( "CheckPopMail head" );

  if (!PopHost)
  {
    mutt_error ("POP host is not defined.");
    return;
  }

  if (!PopUser)
  {
    mutt_error ("No POP username is defined.");
    return;
  }

  DM( "Open buffer mailbox" );

  if (mx_open_mailbox (NONULL(Spoolfile), M_APPEND, &ctx) == NULL) {
	  mutt_error( "Cannot open the temporary mailbox to spool POP3 messages: \"%s\"", Spoolfile );
	  return;
  }


#ifndef LIBMUTT   
  if (!getPass ()) return;
#endif

  DM( "Begin connection" );

  s = socket (AF_INET, SOCK_STREAM, IPPROTO_IP);

  memset ((char *) &sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (PopPort);

  if ((n = inet_addr (NONULL(PopHost))) == -1)
  {
    /* Must be a DNS name */
    if ((he = gethostbyname (NONULL(PopHost))) == NULL)
    {
      mutt_error ("Could not find address for host %s.", PopHost);
      mx_fastclose_mailbox (&ctx);
      return;
    }
    memcpy ((void *)&sin.sin_addr, *(he->h_addr_list), he->h_length);
  }
  else
    memcpy ((void *)&sin.sin_addr, (void *)&n, sizeof(n));
  
  mutt_message ("Connecting to %s", inet_ntoa (sin.sin_addr));

  if (connect (s, (struct sockaddr *) &sin, sizeof (struct sockaddr_in)) == -1)
  {
    mutt_perror ("connect");
    return;
  }

  DM( "Connected; hello" );

  if (getLine (s, buffer, sizeof (buffer)) == -1) {
	  mx_fastclose_mailbox (&ctx);
	  goto fail;
  }

  if (mutt_strncmp (buffer, "+OK", 3) != 0)
  {
    mutt_remove_trailing_ws (buffer);
    mutt_error (buffer);
    goto finish;
  }

  DM( "hellod; user" );

  snprintf (buffer, sizeof(buffer), "user %s\r\n", PopUser);
  write (s, buffer, mutt_strlen (buffer));

  if (getLine (s, buffer, sizeof (buffer)) == -1) {
      mx_fastclose_mailbox (&ctx);
      goto fail;
  }

  if (mutt_strncmp (buffer, "+OK", 3) != 0)
  {
    mutt_remove_trailing_ws (buffer);
    mutt_error (buffer);
    goto finish;
  }

  DM( "usered; password" );

  snprintf (buffer, sizeof(buffer), "pass %s\r\n", NONULL(PopPass));
  write (s, buffer, mutt_strlen (buffer));
  
  if (getLine (s, buffer, sizeof (buffer)) == -1) {
	  mx_fastclose_mailbox (&ctx);
	  goto fail;
  }

  if (mutt_strncmp (buffer, "+OK", 3) != 0)
  {
    if(PopPass)
      memset(PopPass, 0, mutt_strlen(PopPass));
    
    safe_free((void **) &PopPass); /* void the given password */
    mutt_remove_trailing_ws (buffer);
    mutt_error (buffer[0] ? buffer : "Server closed connection!");
    goto finish;
  }

  DM( "passworded; stat" );

  /* find out how many messages are in the mailbox. */
  write (s, "stat\r\n", 6);
  
  if (getLine (s, buffer, sizeof (buffer)) == -1) {
	  mx_fastclose_mailbox (&ctx);
	  goto fail;
  }

  if (mutt_strncmp (buffer, "+OK", 3) != 0)
  {
    mutt_remove_trailing_ws (buffer);
    mutt_error (buffer);
    goto finish;
  }
  
  sscanf (buffer, "+OK %d %d", &msgs, &bytes);

  DM( "Stat retrieved" );

  if (msgs == 0)
  {
    mutt_message ("No new mail in POP mailbox.");
    goto finish;
  }

 first_msg = 1;


/*  
 *  If Messages are left on the server, be sure that we don't retrieve
 *  them a second time.  Do this by checking Unique IDs backward from the
 *  last message, until a previously downloaded message is found.
 */
 
 if ( !option (OPTPOPDELETE) )
 {
	 DM( "Checking for seen UIDLs" );

  for( i = msgs; i > 0 ; i--) 
  {
  	snprintf( buffer, sizeof(buffer), "uidl %d\r\n", i);
  	write( s, buffer, strlen(buffer) );

  	getLine (s, buffer, sizeof (buffer));
  	
  	if (strncmp (buffer, "+OK", 3) != 0)
  	{
  		mutt_remove_trailing_ws( buffer);
  		mutt_error (buffer);
  		goto finish; 
          /* I'm following the convention of libmutt, not my coding style */
  	}
  	sscanf( buffer, "+OK %d %s", &tmp, uid );

	if( i == msgs )  
	{
	   strcpy( last_uid, uid ); // save uid of the last message for exit
	   if( PopUID[0] == 0 )
	     break;  /* no previous UID set */
	 }

    if( *PopUID && strcmp( uid, PopUID ) == 0 )  
	{
	  /* 
	   * this message seen, so start w/ next in queue           *
	   * This will be larger than 'msgs' if no new messages     *
	   * forcing the for loop below to skip retrieving messages *
	   */
	    
      first_msg = i + 1; 
      break;
    }

   }
  } 
    	 

  total = msgs - first_msg + 1; /* will be used for display later */
 
  DM( "Status: total %d, messages %d, first_msg %d", total, msgs, first_msg );

  /*  Check for the total amount of bytes mail to be received */
  tot_bytes=0;
  for (i = first_msg ; i <= msgs ; i++)
  {
    snprintf(buffer, sizeof(buffer), "list %d\r\n", i);
    write (s, buffer, strlen(buffer));
    if ( getLine (s, buffer, sizeof(buffer)) == -1) 
    {
	    DM( "Abort while listing message %d", i );
      mx_fastclose_mailbox (&ctx);
      goto fail;
    }
    
    if (sscanf (buffer,"+OK %d %d",&i,&num_bytes) != 2) 
    {
	    DM( "Error on list message %d encountered", i );
      tot_bytes=-1;
      break;
    }
    tot_bytes += num_bytes;

    DM( "Message info: total %d, this %d", tot_bytes, num_bytes );
  }


  num_bytes=0;  
  for (i = first_msg ; i <= msgs ; i++)
  {
	  DM( "Retrieving message %d", i );

    snprintf (buffer, sizeof(buffer), "retr %d\r\n", i);
    write (s, buffer, strlen (buffer));

#ifdef BALSA_USE_THREADS
    sprintf( threadbuf, "Retrieving Message %d of %d", 
	     i - first_msg + 1, total );
    MSGMAILTHREAD( threadmsg, MSGMAILTHREAD_MSGINFO, threadbuf,0,0 );
#endif

    if (getLine (s, buffer, sizeof (buffer)) == -1)
    {
	    DM( "Abort while retrieving message %d", i );
      mx_fastclose_mailbox (&ctx);
      goto fail;
    }

    if (mutt_strncmp (buffer, "+OK", 3) != 0)
    {
	    DM( "Error on retrieve message %d", i );
      mutt_remove_trailing_ws (buffer);
      mutt_error (buffer);
      break;
    }

    if ((msg = mx_open_new_message (&ctx, NULL, M_ADD_FROM)) == NULL)
    {
	    DM( "Error while creating new message %d", i );
      err = 1;
      break;
    }

    /* Now read the actual message. */
    FOREVER
    {
      char *p;
      int chunk;

      DM( "Message reading loop: %d" );

      if ((chunk = getLine (s, buffer, sizeof (buffer))) == -1)
      {
	mutt_error ("Error reading message!");
	err = 1;
	break;
      }
      
#ifdef BALSA_USE_THREADS
      sprintf( threadbuf,"Received %d bytes of %d",num_bytes,tot_bytes);
      MSGMAILTHREAD(threadmsg, MSGMAILTHREAD_PROGRESS, threadbuf, num_bytes,tot_bytes); 
#endif

      /* check to see if we got a full line */
      if (buffer[chunk-2] == '\r' && buffer[chunk-1] == '\n')
      {
	      DM( "Line completed" );

	if (mutt_strcmp(".\r\n", buffer) == 0)
	{
		DM( "Line finished" );
	  /* end of message */
	  break;
	}

	num_bytes += chunk;

	/* change CRLF to just LF */
	buffer[chunk-2] = '\n';
	buffer[chunk-1] = 0;
	chunk--;

	/* see if the line was byte-stuffed */
	if (buffer[0] == '.')
	{
	  p = buffer + 1;
	  chunk--;
	}
	else
	  p = buffer;
      }
      else
	p = buffer;

      DM( "Writing message to fp" );
      fwrite (p, 1, chunk, msg->fp);
    }

    if (mx_commit_message (msg, &ctx) != 0)
    {
      mutt_error ("Error while writing mailbox!");
      err = 1;
    }

    DM( "Message retrieved" );

    mx_close_message (&msg);

    if (err)
      break;

    if (option (OPTPOPDELETE))
    {
	    DM( "Nuking message" );

      /* delete the message on the server */
      snprintf (buffer, sizeof(buffer), "dele %d\r\n", i);
      write (s, buffer, mutt_strlen (buffer));

      /* eat the server response */
      getLine (s, buffer, sizeof (buffer));
      if (mutt_strncmp (buffer, "+OK", 3) != 0)
      {
	err = 1;
	DM( "Error on message delete %d", i );
        mutt_remove_trailing_ws (buffer);
	mutt_error (buffer);
	break;
      }
    }

    mutt_message ("%s [%d messages read]", msgbuf, i);
  }

  if (msg)
  {
	  DM( "commit close message" );
    mx_commit_message (msg, &ctx);
    mx_close_message (&msg);
  }

  DM( "mailbox close" );
  mx_close_mailbox (&ctx, NULL);

  if (err)
  {
	  DM( "Reset server" );
    /* make sure no messages get deleted */
    write (s, "rset\r\n", 6);
    getLine (s, buffer, sizeof (buffer)); /* snarf the response */
  }

finish:

  DM( "Logout from server" );
  /* exit gracefully */
  write (s, "quit\r\n", 6);
  getLine (s, buffer, sizeof (buffer)); /* snarf the response */
  close (s);
  strcpy( PopUID, last_uid );
  return;

  /* not reached */

fail:
  DM( "Abort failure death explosion" );
  mutt_error ("Server closed connection!");
  close (s);
}
