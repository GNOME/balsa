/*
 * Program:	News routines
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	4 September 1991
 * Last Edited:	29 December 1997
 *
 * Copyright 1997 by the University of Washington
 *
 *  Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appears in all copies and that both the
 * above copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the University of Washington not be
 * used in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  This software is made
 * available "as is", and
 * THE UNIVERSITY OF WASHINGTON DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED,
 * WITH REGARD TO THIS SOFTWARE, INCLUDING WITHOUT LIMITATION ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, AND IN
 * NO EVENT SHALL THE UNIVERSITY OF WASHINGTON BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, TORT
 * (INCLUDING NEGLIGENCE) OR STRICT LIABILITY, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
extern int errno;		/* just in case */
#include "mail.h"
#include "osdep.h"
#include <sys/stat.h>
#include <sys/time.h>
#include "news.h"
#include "misc.h"
#include "newsrc.h"

/* News routines */


/* Driver dispatch used by MAIL */

DRIVER newsdriver = {
  "news",			/* driver name */
				/* driver flags */
  DR_NEWS|DR_READONLY|DR_NOFAST|DR_NAMESPACE,
  (DRIVER *) NIL,		/* next driver */
  news_valid,			/* mailbox is valid for us */
  news_parameters,		/* manipulate parameters */
  news_scan,			/* scan mailboxes */
  news_list,			/* find mailboxes */
  news_lsub,			/* find subscribed mailboxes */
  news_subscribe,		/* subscribe to mailbox */
  news_unsubscribe,		/* unsubscribe from mailbox */
  news_create,			/* create mailbox */
  news_delete,			/* delete mailbox */
  news_rename,			/* rename mailbox */
  NIL,				/* status of mailbox */
  news_open,			/* open mailbox */
  news_close,			/* close mailbox */
  news_fast,			/* fetch message "fast" attributes */
  NIL,				/* fetch message flags */
  NIL,				/* fetch overview */
  NIL,				/* fetch message envelopes */
  news_header,			/* fetch message header */
  news_text,			/* fetch message body */
  NIL,				/* fetch partial message text */
  NIL,				/* unique identifier */
  NIL,				/* message number */
  NIL,				/* modify flags */
  news_flagmsg,			/* per-message modify flags */
  NIL,				/* search for message based on criteria */
  NIL,				/* sort messages */
  NIL,				/* thread messages */
  news_ping,			/* ping mailbox to see if still alive */
  news_check,			/* check for new messages */
  news_expunge,			/* expunge deleted messages */
  news_copy,			/* copy messages to another mailbox */
  news_append,			/* append string message to mailbox */
  NIL				/* garbage collect stream */
};

				/* prototype stream */
MAILSTREAM newsproto = {&newsdriver};

/* News validate mailbox
 * Accepts: mailbox name
 * Returns: our driver if name is valid, NIL otherwise
 */

DRIVER *news_valid (char *name)
{
  int fd;
  char *s,*t,*u;
  char tmp[MAILTMPLEN];
  struct stat sbuf;
  if ((name[0] == '#') && (name[1] == 'n') && (name[2] == 'e') &&
      (name[3] == 'w') && (name[4] == 's') && (name[5] == '.') &&
      !strchr (name,'/') &&
      !stat ((char *) mail_parameters (NIL,GET_NEWSSPOOL,NIL),&sbuf) &&
      ((fd = open ((char *) mail_parameters (NIL,GET_NEWSACTIVE,NIL),O_RDONLY,
		   NIL)) >= 0)) {
    fstat (fd,&sbuf);		/* get size of active file */
				/* slurp in active file */
    read (fd,t = s = (char *) fs_get (sbuf.st_size+1),sbuf.st_size);
    s[sbuf.st_size] = '\0';	/* tie off file */
    close (fd);			/* flush file */
    while (*t && (u = strchr (t,' '))) {
      *u++ = '\0';		/* tie off at end of name */
      if (!strcmp (name+6,t)) return &newsdriver;
      t = 1 + strchr (u,'\n');	/* next line */
    }
    fs_give ((void **) &s);	/* flush data */
  }
  return NIL;			/* return status */
}

/* News manipulate driver parameters
 * Accepts: function code
 *	    function-dependent value
 * Returns: function-dependent return value
 */

void *news_parameters (long function,void *value)
{
  return NIL;
}


/* News scan mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 *	    string to scan
 */

void news_scan (MAILSTREAM *stream,char *ref,char *pat,char *contents)
{
  char tmp[MAILTMPLEN];
  if (news_canonicalize (ref,pat,tmp))
    mm_log ("Scan not valid for news mailboxes",WARN);
}

/* News find list of newsgroups
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void news_list (MAILSTREAM *stream,char *ref,char *pat)
{
  int fd;
  int i;
  char *s,*t,*u,*lcl,pattern[MAILTMPLEN],name[MAILTMPLEN];
  struct stat sbuf;
  if (news_canonicalize (ref,pat,pattern) &&
      !stat ((char *) mail_parameters (NIL,GET_NEWSSPOOL,NIL),&sbuf) &&
      ((fd = open ((char *) mail_parameters (NIL,GET_NEWSACTIVE,NIL),O_RDONLY,
		   NIL)) >= 0)) {
    fstat (fd,&sbuf);		/* get file size and read data */
    read (fd,s = (char *) fs_get (sbuf.st_size + 1),sbuf.st_size);
    close (fd);			/* close file */
    s[sbuf.st_size] = '\0';	/* tie off string */
				/* point after prefix */
    lcl = strcpy (name,"#news.") + 6;
    i = strlen (pattern);	/* length of pattern */
    if (pattern[--i] != '%') i = 0;
    if (t = strtok (s,"\n")) do if (u = strchr (t,' ')) {
      *u = '\0';		/* tie off at end of name */
      strcpy (lcl,t);		/* make full form of name */
      if (pmatch_full (name,pattern,'.')) mm_list (stream,'.',name,NIL);
      else if (i && (u = strchr (name + i,'.'))) {
	*u = '\0';		/* tie off at delimiter, see if matches */
	if (pmatch_full (name,pattern,'.'))
	  mm_list (stream,'.',name,LATT_NOSELECT);
      }
    } while (t = strtok (NIL,"\n"));
    fs_give ((void **) &s);
  }
}

/* News find list of subscribed newsgroups
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void news_lsub (MAILSTREAM *stream,char *ref,char *pat)
{
  char pattern[MAILTMPLEN];
				/* return data from newsrc */
  if (news_canonicalize (ref,pat,pattern)) newsrc_lsub (stream,pattern);
}


/* News canonicalize newsgroup name
 * Accepts: reference
 *	    pattern
 *	    returned single pattern
 * Returns: T on success, NIL on failure
 */

long news_canonicalize (char *ref,char *pat,char *pattern)
{
  if (ref && *ref) {		/* have a reference */
    strcpy (pattern,ref);	/* copy reference to pattern */
				/* # overrides mailbox field in reference */
    if (*pat == '#') strcpy (pattern,pat);
				/* pattern starts, reference ends, with . */
    else if ((*pat == '.') && (pattern[strlen (pattern) - 1] == '.'))
      strcat (pattern,pat + 1);	/* append, omitting one of the period */
    else strcat (pattern,pat);	/* anything else is just appended */
  }
  else strcpy (pattern,pat);	/* just have basic name */
  return ((pattern[0] == '#') && (pattern[1] == 'n') && (pattern[2] == 'e') &&
	  (pattern[3] == 'w') && (pattern[4] == 's') && (pattern[5] == '.') &&
	  !strchr (pattern,'/')) ? T : NIL;
}

/* News subscribe to mailbox
 * Accepts: mail stream
 *	    mailbox to add to subscription list
 * Returns: T on success, NIL on failure
 */

long news_subscribe (MAILSTREAM *stream,char *mailbox)
{
  return news_valid (mailbox) ? newsrc_update (stream,mailbox+6,':') : NIL;
}


/* NEWS unsubscribe to mailbox
 * Accepts: mail stream
 *	    mailbox to delete from subscription list
 * Returns: T on success, NIL on failure
 */

long news_unsubscribe (MAILSTREAM *stream,char *mailbox)
{
  return news_valid (mailbox) ? newsrc_update (stream,mailbox+6,'!') : NIL;
}

/* News create mailbox
 * Accepts: mail stream
 *	    mailbox name to create
 * Returns: T on success, NIL on failure
 */

long news_create (MAILSTREAM *stream,char *mailbox)
{
  return NIL;			/* never valid for News */
}


/* News delete mailbox
 *	    mailbox name to delete
 * Returns: T on success, NIL on failure
 */

long news_delete (MAILSTREAM *stream,char *mailbox)
{
  return NIL;			/* never valid for News */
}


/* News rename mailbox
 * Accepts: mail stream
 *	    old mailbox name
 *	    new mailbox name
 * Returns: T on success, NIL on failure
 */

long news_rename (MAILSTREAM *stream,char *old,char *newname)
{
  return NIL;			/* never valid for News */
}

/* News open
 * Accepts: stream to open
 * Returns: stream on success, NIL on failure
 */

MAILSTREAM *news_open (MAILSTREAM *stream)
{
  long i,nmsgs,j,k;
  char c = NIL,*s,*t,tmp[MAILTMPLEN];
  struct direct **names;
  				/* return prototype for OP_PROTOTYPE call */
  if (!stream) return &newsproto;
  if (LOCAL) {			/* close old file if stream being recycled */
    news_close (stream,NIL);	/* dump and save the changes */
    stream->dtb = &newsdriver;	/* reattach this driver */
    mail_free_cache (stream);	/* clean up cache */
    stream->uid_last = 0;	/* default UID validity */
    stream->uid_validity = time (0);
  }
				/* build directory name */
  sprintf (s = tmp,"%s/%s",(char *) mail_parameters (NIL,GET_NEWSSPOOL,NIL),
	   stream->mailbox + 6);
  while (s = strchr (s,'.')) *s = '/';
				/* scan directory */
  if ((nmsgs = scandir (tmp,&names,news_select,news_numsort)) >= 0) {
    mail_exists (stream,nmsgs);	/* notify upper level that messages exist */
    stream->local = fs_get (sizeof (NEWSLOCAL));
    LOCAL->dirty = NIL;		/* no update to .newsrc needed yet */
    LOCAL->dir = cpystr (tmp);	/* copy directory name for later */
				/* make temporary buffer */
    LOCAL->buf = (char *) fs_get ((LOCAL->buflen = MAXMESSAGESIZE) + 1);
    LOCAL->name = cpystr (stream->mailbox + 6);
    for (i = 0; i < nmsgs; ++i) {
      stream->uid_last = mail_elt (stream,i+1)->private.uid =
	atoi (names[i]->d_name);
      fs_give ((void **) &names[i]);
    }
    fs_give ((void **) &names);	/* free directory */
    stream->sequence++;		/* bump sequence number */
    stream->rdonly = stream->perm_deleted = T;
				/* UIDs are always valid */
    stream->uid_validity = 0xbeefface;
				/* read .newsrc entries */
    mail_recent (stream,newsrc_read (LOCAL->name,stream));
				/* notify if empty newsgroup */
    if (!(stream->nmsgs || stream->silent)) {
      sprintf (tmp,"Newsgroup %s is empty",LOCAL->name);
      mm_log (tmp,WARN);
    }
  }
  else mm_log ("Unable to scan newsgroup spool directory",ERROR);
  return LOCAL ? stream : NIL;	/* if stream is alive, return to caller */
}

/* News file name selection test
 * Accepts: candidate directory entry
 * Returns: T to use file name, NIL to skip it
 */

int news_select (struct direct *name)
{
  char c;
  char *s = name->d_name;
  while (c = *s++) if (!isdigit (c)) return NIL;
  return T;
}


/* News file name comparision
 * Accepts: first candidate directory entry
 *	    second candidate directory entry
 * Returns: negative if d1 < d2, 0 if d1 == d2, postive if d1 > d2
 */

int news_numsort (const void *d1,const void *d2)
{
  return atoi ((*(struct direct **) d1)->d_name) -
    atoi ((*(struct direct **) d2)->d_name);
}


/* News close
 * Accepts: MAIL stream
 *	    option flags
 */

void news_close (MAILSTREAM *stream,long options)
{
  if (LOCAL) {			/* only if a file is open */
    news_check (stream);	/* dump final checkpoint */
    if (LOCAL->dir) fs_give ((void **) &LOCAL->dir);
				/* free local scratch buffer */
    if (LOCAL->buf) fs_give ((void **) &LOCAL->buf);
    if (LOCAL->name) fs_give ((void **) &LOCAL->name);
				/* nuke the local data */
    fs_give ((void **) &stream->local);
    stream->dtb = NIL;		/* log out the DTB */
  }
}

/* News fetch fast information
 * Accepts: MAIL stream
 *	    sequence
 *	    option flags
 */

void news_fast (MAILSTREAM *stream,char *sequence,long flags)
{
  unsigned long i,j;
				/* ugly and slow */
  if (stream && LOCAL && ((flags & FT_UID) ?
			  mail_uid_sequence (stream,sequence) :
			  mail_sequence (stream,sequence)))
    for (i = 1; i <= stream->nmsgs; i++)
      if (mail_elt (stream,i)->sequence) news_header (stream,i,&j,NIL);
}

/* News fetch message header
 * Accepts: MAIL stream
 *	    message # to fetch
 *	    pointer to returned header text length
 *	    option flags
 * Returns: message header in RFC822 format
 */

char *news_header (MAILSTREAM *stream,unsigned long msgno,
		   unsigned long *length,long flags)
{
  unsigned long i,hdrsize;
  int fd;
  char *t;
  struct stat sbuf;
  struct tm *tm;
  MESSAGECACHE *elt;
  *length = 0;			/* default to empty */
  if (flags & FT_UID) return "";/* UID call "impossible" */
  elt = mail_elt (stream,msgno);/* get elt */
  if (!elt->private.msg.header.text.data) {
				/* build message file name */
    sprintf (LOCAL->buf,"%s/%lu",LOCAL->dir,elt->private.uid);
    if ((fd = open (LOCAL->buf,O_RDONLY,NIL)) < 0) return "";
    fstat (fd,&sbuf);		/* get size of message */
				/* make plausible IMAPish date string */
    tm = gmtime (&sbuf.st_mtime);
    elt->day = tm->tm_mday; elt->month = tm->tm_mon + 1;
    elt->year = tm->tm_year + 1900 - BASEYEAR;
    elt->hours = tm->tm_hour; elt->minutes = tm->tm_min;
    elt->seconds = tm->tm_sec;
    elt->zhours = 0; elt->zminutes = 0;
				/* is buffer big enough? */
    if (sbuf.st_size > LOCAL->buflen) {
      fs_give ((void **) &LOCAL->buf);
      LOCAL->buf = (char *) fs_get ((LOCAL->buflen = sbuf.st_size) + 1);
    }
				/* slurp message */
    read (fd,LOCAL->buf,sbuf.st_size);
				/* tie off file */
    LOCAL->buf[sbuf.st_size] = '\0';
    close (fd);			/* flush message file */
				/* find end of header */
    for (i = 0,t = LOCAL->buf; *t && !(i && (*t == '\n')); i = (*t++ == '\n'));
				/* number of header bytes */
    hdrsize = (*t ? ++t : t) - LOCAL->buf;
    elt->rfc822_size =		/* size of entire message in CRLF form */
      (elt->private.msg.header.text.size =
       strcrlfcpy ((char **) &elt->private.msg.header.text.data,&i,LOCAL->buf,
		   hdrsize)) +
	 (elt->private.msg.text.text.size =
	  strcrlfcpy ((char **) &elt->private.msg.text.text.data,&i,t,
		      sbuf.st_size - hdrsize));
  }
  *length = elt->private.msg.header.text.size;
  return (char *) elt->private.msg.header.text.data;
}

/* News fetch message text (body only)
 * Accepts: MAIL stream
 *	    message # to fetch
 *	    pointer to returned stringstruct
 *	    option flags
 * Returns: T on success, NIL on failure
 */

long news_text (MAILSTREAM *stream,unsigned long msgno,STRING *bs,long flags)
{
  unsigned long i;
  MESSAGECACHE *elt;
				/* UID call "impossible" */
  if (flags & FT_UID) return NIL;
  elt = mail_elt (stream,msgno);/* get elt */
				/* snarf message if don't have it yet */
  if (!elt->private.msg.text.text.data) {
    news_header (stream,msgno,NIL,flags);
    if (!elt->private.msg.text.text.data) return NIL;
  }
  if (!(flags & FT_PEEK)) {	/* mark as seen */
    mail_elt (stream,msgno)->seen = T;
    mm_flags (stream,msgno);
  }
  if (!elt->private.msg.text.text.data) return NIL;
  INIT (bs,mail_string,elt->private.msg.text.text.data,
	elt->private.msg.text.text.size);
  return T;
}

/* News per-message modify flag
 * Accepts: MAIL stream
 *	    message cache element
 */

void news_flagmsg (MAILSTREAM *stream,MESSAGECACHE *elt)
{
  if (!LOCAL->dirty) {		/* only bother checking if not dirty yet */
    if (elt->valid) {		/* if done, see if deleted changed */
      if (elt->sequence != elt->deleted) LOCAL->dirty = T;
      elt->sequence = T;	/* leave the sequence set */
    }
				/* note current setting of deleted flag */
    else elt->sequence = elt->deleted;
  }
}


/* News ping mailbox
 * Accepts: MAIL stream
 * Returns: T if stream alive, else NIL
 */

long news_ping (MAILSTREAM *stream)
{
  return T;			/* always alive */
}


/* News check mailbox
 * Accepts: MAIL stream
 */

void news_check (MAILSTREAM *stream)
{
				/* never do if no updates */
  if (LOCAL->dirty) newsrc_write (LOCAL->name,stream);
  LOCAL->dirty = NIL;
}


/* News expunge mailbox
 * Accepts: MAIL stream
 */

void news_expunge (MAILSTREAM *stream)
{
  if (!stream->silent) mm_log ("Expunge ignored on news",NIL);
}

/* News copy message(s)
 * Accepts: MAIL stream
 *	    sequence
 *	    destination mailbox
 *	    option flags
 * Returns: T if copy successful, else NIL
 */

long news_copy (MAILSTREAM *stream,char *sequence,char *mailbox,long options)
{
  mailproxycopy_t pc =
    (mailproxycopy_t) mail_parameters (stream,GET_MAILPROXYCOPY,NIL);
  if (pc) return (*pc) (stream,sequence,mailbox,options);
  mm_log ("Copy not valid for News",ERROR);
  return NIL;
}


/* News append message from stringstruct
 * Accepts: MAIL stream
 *	    destination mailbox
 *	    stringstruct of messages to append
 * Returns: T if append successful, else NIL
 */

long news_append (MAILSTREAM *stream,char *mailbox,char *flags,char *date,
		  STRING *message)
{
  mm_log ("Append not valid for News",ERROR);
  return NIL;
}
