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
/*
 * Rather crude POP3 support.
 */

#include "md5.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/poll.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <gnome.h>
#include "mutt.h"
#include "libmutt/mailbox.h"
#include "libbalsa.h"
#include "pop3.h"

gint PopDebug = 0;
#define BALSA_TEST_POP3

const gchar*
pop_get_errstr(PopStatus status)
{
    static gchar* errmsgs[] = {
	"",
	N_("connection error"),
	N_("POP command exection failed"),
	N_("Could not write the message"),
	N_("Could not run the delivery program (procmail)"),
	N_("Could not open mailbox for spooling"),
	N_("Could not append message"),
	N_("Host not found"),
	N_("Connection refused"),
	N_("Authentication failed")};

    if(status<POP_OK || status > POP_AUTH_FAILED)
	status = POP_OK;

    return _(errmsgs[status]);
}


#ifdef BALSA_TEST_POP3
#define DM( fmt, args... ) G_STMT_START { if(PopDebug) { fprintf(stderr, fmt, ##args ); fprintf(stderr, "\n" );} } G_STMT_END
#else
#define DM( fmt, args... ) 
#endif

static int
safe_read_char(int fd, char *ch)
{
    int retval = -1;

    retval = read (fd, ch, 1);
    if(retval != 1) {
	struct pollfd popsock;
	
	popsock.fd = fd;
	popsock.events = POLLIN | POLLERR | POLLHUP;

	if(poll( &popsock, 1, 30000 ) < 0) {
	    g_warning("poll error on safe_read_char");
	    return(-1);
	}
	if( (popsock.revents & (POLLERR | POLLHUP)) != 0 )
	    return(-1);

	if( (popsock.revents & POLLIN) != 0 ) {
	    retval = read (fd, ch, 1);
	} else 
	    retval = 0;
    }
    
    return retval;
}

static int getLine (int fd, char *s, int len)
{
    char ch;
    int bytes = 0;
    
    while (safe_read_char (fd, &ch) > 0) {
	s[bytes++] = ch;
	if (ch == '\n') {
	    s[bytes] = 0;
	    DM("POP3 S: \"%s\" (%d bytes)\n", s, bytes);
	    return bytes;
	}
      /* make sure not to overwrite the buffer */
	if (bytes == len - 1) {
	    s[bytes] = 0;
	    DM("POP3 S: \"%s\" (%d bytes)\n", s, bytes);
	    return bytes;
	}
    }
    s[bytes] = 0;
    return -1;
}

/* getApopStamp:
   Get the Server Timestamp for APOP authentication -kabir 
   return TRUE on success.
*/

static gboolean
getApopStamp (char *buff, char *stamp) {
    char *start;
    char *finish;
    size_t len;
    
    start = strchr(buff, '<');
    finish = strchr(buff, '>');
    
    if( buff && stamp && start && finish ) {
        len = strlen(start) - strlen(finish) + 1;
        strncpy( stamp, start, len );
        return TRUE;
    }
    else return FALSE;
}

/* Compute the authentication hash to send to the server - kabir */

static void
computeAuthHash(char *stamp, char *hash, const char *passwd) {
    MD5_CTX mdContext;
    register unsigned char *dp;
    register char *cp;
    unsigned char *ep;
    unsigned char digest[16];
    
    MD5Init(&mdContext);
    MD5Update(&mdContext, (unsigned char *)stamp, strlen(stamp));
    MD5Update(&mdContext, (unsigned char *)passwd, strlen(passwd));
    MD5Final(digest, &mdContext);
    
    cp = hash;
    dp = digest;
    for(ep = dp + sizeof(digest)/sizeof(digest[0]); dp < ep; cp += 2) {
	(void) sprintf (cp, "%02x", *dp);
	dp++;
    }
    
    *cp = '\0';
}

static PopStatus
pop_connect(int *s, const gchar *host, gint port)
{
#ifdef HAVE_GETADDRINFO
/* --- IPv4/6 --- */

  /* "65536\0" */
  char portstr[6];
  struct addrinfo hints;
  struct addrinfo* res;
  struct addrinfo* cur;
  int sa_size, rc;

  /* we accept v4 or v6 STREAM sockets */
  memset (&hints, 0, sizeof (hints));

  hints.ai_family = ( 1/*option (OPTUSEIPV6) */) ?  AF_UNSPEC : AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  snprintf (portstr, sizeof (portstr), "%d", port);
  if(getaddrinfo (host, portstr, &hints, &res))
      return POP_HOST_NOT_FOUND;

  for(cur = res; cur != NULL; cur = cur->ai_next) {
      *s = socket (cur->ai_family, cur->ai_socktype, cur->ai_protocol);
      fcntl(*s, F_SETFD,FD_CLOEXEC);
      if(*s<0) continue;

      if (cur->ai_addr->sa_family == AF_INET)
	  sa_size = sizeof (struct sockaddr_in);
      else if (cur->ai_addr->sa_family == AF_INET6)
	  sa_size = sizeof (struct sockaddr_in6);
      else {
	  libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			       _("Unknown POP socket family: %d"), 
			       cur->ai_addr->sa_family);
	  return POP_CONNECT_FAILED;
      }
      if((rc = connect(*s, cur->ai_addr, sa_size)) == 0) break;
      close(*s);   
  }
  freeaddrinfo (res);
  if(cur == NULL)
      return POP_CONNECT_FAILED;
#else
  /* IPv4 only. Actually, we should never allow this code to compile
     because gethostbyname() is not reentrant.  One should use
     gethostbyname_r() instead but is it worth it? getaddrinfo is more
     universal. You can override this warning by configuring balsa
     with --disable-more-warnings. Or --disable-threads. Or
     provide a patch.
  */
#ifdef _REENTRANT
#warning "getaddrinfo() is not available, using a thread unsafe code."
#endif
    struct sockaddr_in sin;
    gint32 n;
    struct hostent *he;
    
    *s = socket (AF_INET, SOCK_STREAM, IPPROTO_IP);
    
    memset ((char *) &sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons (port);
    
    if ((n = inet_addr (host)) == -1) {
	/* Must be a DNS name */
	if ((he = gethostbyname (host)) == NULL)
	    return POP_HOST_NOT_FOUND;
	memcpy ((void *)&sin.sin_addr, *(he->h_addr_list), he->h_length);
    }
    else
	memcpy ((void *)&sin.sin_addr, (void *)&n, sizeof(n));
    
    if (connect(*s, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) == -1)
	return POP_CONNECT_FAILED;
#endif

    fcntl(*s, F_SETFL, O_NONBLOCK);

    return POP_OK;
}

static PopStatus
pop_auth(int s, const gchar * pop_user,  const gchar * pop_pass,
	 gboolean use_apop)
{
    char stamp[2048];
    char authHash[BUFSIZ];
    char buffer[2048];
    
    if (getLine (s, buffer, sizeof (buffer)) == -1) return POP_CONN_ERR;
    if (strncmp (buffer, "+OK", 3) != 0)            return POP_COMMAND_ERR;
    
    DM("pop_auth %s %s %d", pop_user, "(password hidden)", use_apop);
    /* handle apop secret transmission, if needed -kabir */
    if(use_apop) {
	memset( stamp, '\0', sizeof(stamp) );
	if( !getApopStamp(buffer, stamp) ) return POP_AUTH_FAILED;
	computeAuthHash(stamp, authHash, pop_pass);
	snprintf(buffer, sizeof(buffer), "apop %s %s\r\n", pop_user, authHash);
	DM("POP3 C: '%s'", buffer);
	write (s, buffer, strlen (buffer));
    } else {
	
	snprintf (buffer, sizeof(buffer), "user %s\r\n", pop_user);
	DM("POP3 C: '%s'", buffer);
	write (s, buffer, strlen (buffer));
	
	if (getLine (s, buffer, sizeof (buffer)) == -1) return POP_CONN_ERR;
	if (strncmp (buffer, "+OK", 3) != 0)            return POP_COMMAND_ERR;
	
	snprintf (buffer, sizeof(buffer), "pass %s\r\n", pop_pass);
	DM("POP3 C: 'pass (password hidden)'");
	write (s, buffer, strlen (buffer));
    } 
  
    if (getLine (s, buffer, sizeof (buffer)) == -1) return POP_CONN_ERR;
    if (strncmp (buffer, "+OK", 3) != 0)            return POP_AUTH_FAILED;
    return POP_OK;
}

static PopStatus
pop_get_stats(int s, gint *first_msg, gint *msgs, gint *tot_bytes, 
	      gchar *last_uid, gchar* prev_last_uid)
{
    gint i, tmp, num_bytes, bytes;
    char buffer[2048];
    char uid[80];

    /* find out how many messages are in the mailbox. */
    DM( "POP3 C: stat" );
    write(s, "stat\r\n", 6);
    
    if (getLine (s, buffer, sizeof (buffer)) == -1) return POP_CONN_ERR;
    if (strncmp (buffer, "+OK", 3) != 0)            return POP_COMMAND_ERR;
    
    if( sscanf (buffer, "+OK %d %d", msgs, &bytes) < 2 ) {
	
    }
    
    *first_msg = 1;
    
    if (*msgs == 0) return POP_OK;
    
/*  
 *  If Messages are left on the server, be sure that we don't retrieve
 *  them a second time.  Do this by checking Unique IDs backward from the
 *  last message, until a previously downloaded message is found.
 */
 
    if (last_uid)  {
	for(i = *msgs; i>0; i--) {
	    snprintf(buffer, sizeof(buffer), "uidl %d\r\n", i);
	    DM( "POP3 C: '%s'", buffer);
	    write(s, buffer, strlen(buffer));
	    
	    if(getLine (s, buffer, sizeof (buffer)) == -1) return POP_CONN_ERR;
	    
	    if (strncmp (buffer, "+OK", 3) != 0) {
	        /* Check if "last" cmd is supported */
	        write (s, "last\r\n", 6);
		if(getLine (s, buffer, sizeof (buffer)) == -1)
		    return POP_CONN_ERR;
		if (strncmp (buffer, "+OK", 3) == 0) {       
		    sscanf (buffer, "+OK %d", first_msg);
		    (*first_msg)++; /* fix the off-by-one msg index  */
		    break;
		} 
		/* none of uidl or last recognised, fail.. */
		return POP_COMMAND_ERR;
	    }
	    sscanf( buffer, "+OK %d %s", &tmp, uid);
	    
	    if(i == *msgs) {
		strcpy(last_uid, uid); /* save uid of the last message */
		if(*prev_last_uid == '\0')
		    break;  /* no previous UID set */
	    }
	    
	    if(*prev_last_uid && strcmp(uid, prev_last_uid) == 0 )  {
		/* 
		 * this message seen, so start w/ next in queue           *
		 * This will be larger than 'msgs' if no new messages     *
		 * forcing the for loop to skip retrieving messages       *
		 */
		*first_msg = i + 1; 
		break;
	    }
	}
    }

    DM("POP3 status summary: messages %d, first_msg %d", *msgs, *first_msg );
    
    /*  Check for the total amount of bytes mail to be received */
    *tot_bytes=0;
    for (i=*first_msg; i <= *msgs ; i++) {
	snprintf(buffer, sizeof(buffer), "list %d\r\n", i);
	DM( "POP3 C: '%s'", buffer);
	write (s, buffer, strlen(buffer));
	if ( getLine (s, buffer, sizeof(buffer)) == -1) return POP_CONN_ERR;
    
	if (sscanf (buffer,"+OK %d %d",&i,&num_bytes) != 2) {
	    DM( "Error on list message %d encountered", i );
	    *tot_bytes=-1;
	    break;
	}
	*tot_bytes += num_bytes;

	DM("POP3 Message info: total %7d, this %6d", *tot_bytes, num_bytes );
    }
    return POP_OK;
}

static PopStatus send_fetch_req(int s, int msgno, char* buffer, size_t bufsz)
{
    snprintf (buffer, bufsz, "retr %d\r\n", msgno);
    DM("POP3 C: '%s'", buffer);
    if(write (s, buffer, strlen (buffer)) == -1) return POP_CONN_ERR;
    if (getLine (s, buffer, bufsz) == -1)        return POP_CONN_ERR;
    if (strncmp (buffer, "+OK", 3) != 0)         return POP_COMMAND_ERR;
    return POP_OK;
}

static PopStatus
fetch_single_msg(int s, FILE *msg, int msgno, int first_msg, int msgs, 
		 int *num_bytes, int tot_bytes, ProgressCallback prog_cb) {
    char buffer[2048];
#ifdef BALSA_USE_THREADS
    static const int GUI_PROGRESS_STEP = 2048;
    int last_update_size = 0;
    char threadbuf[160];
    
    sprintf(threadbuf, _("Retrieving Message %d of %d"), msgno, msgs);
    prog_cb ( threadbuf, 0, 0);
#endif

    DM("POP3: fetching message %d", msgno);
    /* Now read the actual message. */
    while(1) {
	char *p;
	int chunk;
	
	if ((chunk = getLine (s, buffer, sizeof (buffer))) == -1) 
	    return POP_CONN_ERR;
#ifdef BALSA_USE_THREADS
	if(last_update_size<*num_bytes) {
	    sprintf(threadbuf,_("Received %d bytes of %d"), 
		    *num_bytes, tot_bytes);
	    prog_cb (threadbuf, *num_bytes, tot_bytes);
	    last_update_size += GUI_PROGRESS_STEP;
	}
#endif
	/* check to see if we got a full line */
	if (buffer[chunk-2] == '\r' && buffer[chunk-1] == '\n') {
	    if (strcmp(".\r\n", buffer) == 0) {
		DM( "Message %d finished", msgno);
		/* end of message */
		break; /* while look */
	    }
	    
	    *num_bytes += chunk;
	    
	    /* change CRLF to just LF */
	    buffer[chunk-2] = '\n';
	    buffer[chunk-1] = 0;
	    chunk--;
	    
	    /* see if the line was byte-stuffed */
	    if (buffer[0] == '.')  {
		p = buffer + 1;
		chunk--;
	    }
	    else
		p = buffer;
	}
	else
	    p = buffer;
	
	/* fwrite(p, 1, chunk, stdout); */
	if(fwrite (p, 1, chunk, msg) != chunk) return POP_WRITE_ERR;
    } /* end of while */
    
    DM("POP3: Message %d retrieved", msgno);
    return POP_OK;
}

static PopStatus
delete_msg(int s, int msgno)
{
    char buffer[256];
	    
    /* delete the message on the server */
    snprintf (buffer, sizeof(buffer), "dele %d\r\n", msgno);
    DM( "POP3 C: '%s'", buffer);
    write (s, buffer, strlen (buffer));
	    
    /* eat the server response */
    if(getLine (s, buffer, sizeof (buffer)) == -1) return POP_CONN_ERR;
    if (strncmp (buffer, "+OK", 3) != 0)           return POP_COMMAND_ERR;
    return POP_OK;
}

static PopStatus reset_server(int s, char* buffer, size_t bufsz)
{
    /* make sure no messages get deleted */
    DM("POP3: rset");
    write (s, "rset\r\n", 6);
    getLine (s, buffer, bufsz); /* snarf the response */
    return POP_OK;
}

static PopStatus
fetch_procmail(int s, gint first_msg, gint msgs, gint tot_bytes,
	       gboolean delete_on_server, const gchar * procmail_path,
	       ProgressCallback prog_cb)
{
    gint i, num_bytes;
    char buffer[2048];
    PopStatus err = POP_OK;
    FILE *msg;
    
    num_bytes=0;  
    for (i = first_msg; i <= msgs; i++) {
	if( (err=send_fetch_req(s, i, buffer, sizeof(buffer))) != POP_OK)
	    break;

	if( (msg=popen(procmail_path, "w")) == NULL) {
	    DM( "Error while creating new message %d", i );
	    return POP_PROCMAIL_ERR;
	}
	
	err = fetch_single_msg(s, msg, i, first_msg, msgs, &num_bytes, 
			       tot_bytes, prog_cb);
	if (pclose (msg) != 0 && err == POP_OK) err = POP_PROCMAIL_ERR;
	
	if (err != POP_OK)  break; /* the 'for' loop */
	if (delete_on_server) delete_msg(s, i); /* ignore errors */
    }
    
    if (err != POP_OK) reset_server(s, buffer, sizeof(buffer));
    return err;
}



static PopStatus
fetch_direct(int s, gint first_msg, gint msgs, gint tot_bytes,
	     gboolean delete_on_server, const gchar *spoolfile,
	     ProgressCallback prog_cb)
{
    gint i, num_bytes;
    char buffer[2048];
    PopStatus err = POP_OK;
    CONTEXT ctx, *nctx;
    MESSAGE *msg = NULL;
    
    g_return_val_if_fail(spoolfile, POP_OK);

    gdk_threads_enter(); libbalsa_lock_mutt();
    nctx = mx_open_mailbox (spoolfile, M_APPEND, &ctx);
    libbalsa_unlock_mutt(); gdk_threads_leave(); 
    if(nctx == NULL) return POP_OPEN_ERR;

    num_bytes=0;  
    for (i = first_msg; i <= msgs; i++) {
	if( (err=send_fetch_req(s, i, buffer, sizeof(buffer))) != POP_OK)
	    break;
	gdk_threads_enter(); libbalsa_lock_mutt();
	msg = mx_open_new_message (&ctx, NULL, M_ADD_FROM);
	libbalsa_unlock_mutt(); gdk_threads_leave(); 
	if (msg == NULL)  {
	    DM("POP3: Error while creating new message %d", i );
	    err = POP_MSG_APPEND;
	    break;
	}

	err = fetch_single_msg(s, msg->fp, i, first_msg, msgs, &num_bytes,
			       tot_bytes, prog_cb);
	if(err != POP_OK) break;
	gdk_threads_enter(); libbalsa_lock_mutt();
	if (mx_commit_message (msg, &ctx) != 0) err = POP_WRITE_ERR;
	mx_close_message (&msg);
	libbalsa_unlock_mutt(); gdk_threads_leave(); 
	
	if (err) break;
	if (delete_on_server) delete_msg(s, i); /* ignore errors */
    }
    if (err != POP_OK) reset_server(s, buffer, sizeof(buffer));
    gdk_threads_enter(); libbalsa_lock_mutt();
    mx_close_mailbox (&ctx, NULL);
    libbalsa_unlock_mutt(); gdk_threads_leave();
    return err;
}

/* fetch_pop_mail:
   loads the mail down from a POP server.
   Can handle connection timeouts by using poll()
*/
typedef PopStatus
(*ProcessCallback)(int s, gint first_msg, gint msgs, gint tot_bytes,
		   gboolean delete_on_server, const gchar * procmail_path,
		   ProgressCallback prog_cb);

static PopStatus
fetch_pop_mail (const gchar *pop_host, const gchar *pop_user,
		const gchar *pop_pass, gint pop_port,
		gboolean use_apop, gboolean delete_on_server,
		gchar * last_uid, ProgressCallback prog_cb,
		ProcessCallback proccb, const gchar *data)
{
    char buffer[2048];
    char uid[80];
    int s, msgs, bytes, first_msg;
    PopStatus status = POP_OK;

    g_return_val_if_fail(pop_host, POP_HOST_NOT_FOUND);
    g_return_val_if_fail(pop_user, POP_AUTH_FAILED);


    DM("POP3 - begin connection");
    status = pop_connect(&s, pop_host, pop_port);
    DM("POP3 Connected; hello");

    if(status == POP_OK)
	status = pop_auth(s, pop_user, pop_pass, use_apop);

    DM("POP3 authentication %s successful", (status ==POP_OK ? "" : "NOT"));
    if(status == POP_OK)
	status = pop_get_stats(s, &first_msg, &msgs, &bytes,  
			       delete_on_server ? NULL : uid, last_uid);
    DM("POP3 status after get stats: %d", status);
    if(status == POP_OK)
	status = proccb(s, first_msg, msgs, bytes, delete_on_server, 
			data, prog_cb);
    
    if(status != POP_CONN_ERR) {
	DM("POP3 C: quit");
	/* exit gracefully */
	write (s, "quit\r\n", 6);
	getLine (s, buffer, sizeof (buffer)); /* snarf the response */
	if(status == POP_OK)
	    strcpy(last_uid, uid);/* FIXME: overflow error on hideous reply? */
    }
    close (s);

    return status;
}

/* the only exported functions below */
PopStatus
libbalsa_fetch_pop_mail_direct (LibBalsaMailboxPop3* mailbox,
				const gchar * spoolfile, 
				ProgressCallback prog_cb, gchar* uid)
{
    LibBalsaServer *s;
    g_return_val_if_fail(mailbox, POP_OK);
    
    s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    return fetch_pop_mail(s->host, s->user,s->passwd, s->port,
			  mailbox->use_apop, mailbox->delete_from_server,
			  uid, prog_cb, fetch_direct, spoolfile);
}
PopStatus
libbalsa_fetch_pop_mail_filter (LibBalsaMailboxPop3* mailbox, 
				ProgressCallback prog_cb, gchar* uid)
{
    LibBalsaServer *s;
    g_return_val_if_fail(mailbox, POP_OK);
    
    s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    return fetch_pop_mail(s->host, s->user,s->passwd, s->port,
			  mailbox->use_apop, mailbox->delete_from_server,
			  uid, prog_cb, fetch_procmail, "procmail -f -");
}
