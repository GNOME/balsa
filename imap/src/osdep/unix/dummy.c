/*
 * Program:	Dummy routines
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	9 May 1991
 * Last Edited:	4 June 1998
 *
 * Copyright 1998 by the University of Washington
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
#include <pwd.h>
#include <sys/stat.h>
#include "dummy.h"
#include "misc.h"
#include "mx.h"			/* highly unfortunate */

/* Dummy routines */


/* Driver dispatch used by MAIL */

DRIVER dummydriver = {
  "dummy",			/* driver name */
  DR_LOCAL|DR_MAIL,		/* driver flags */
  (DRIVER *) NIL,		/* next driver */
  dummy_valid,			/* mailbox is valid for us */
  dummy_parameters,		/* manipulate parameters */
  dummy_scan,			/* scan mailboxes */
  dummy_list,			/* list mailboxes */
  dummy_lsub,			/* list subscribed mailboxes */
  NIL,				/* subscribe to mailbox */
  NIL,				/* unsubscribe from mailbox */
  dummy_create,			/* create mailbox */
  dummy_delete,			/* delete mailbox */
  dummy_rename,			/* rename mailbox */
  NIL,				/* status of mailbox */
  dummy_open,			/* open mailbox */
  dummy_close,			/* close mailbox */
  NIL,				/* fetch message "fast" attributes */
  NIL,				/* fetch message flags */
  NIL,				/* fetch overview */
  NIL,				/* fetch message structure */
  NIL,				/* fetch header */
  NIL,				/* fetch text */
  NIL,				/* fetch message data */
  NIL,				/* unique identifier */
  NIL,				/* message number from UID */
  NIL,				/* modify flags */
  NIL,				/* per-message modify flags */
  NIL,				/* search for message based on criteria */
  NIL,				/* sort messages */
  NIL,				/* thread messages */
  dummy_ping,			/* ping mailbox to see if still alive */
  dummy_check,			/* check for new messages */
  dummy_expunge,		/* expunge deleted messages */
  dummy_copy,			/* copy messages to another mailbox */
  dummy_append,			/* append string message to mailbox */
  NIL				/* garbage collect stream */
};

				/* prototype stream */
MAILSTREAM dummyproto = {&dummydriver};

/* Dummy validate mailbox
 * Accepts: mailbox name
 * Returns: our driver if name is valid, NIL otherwise
 */

DRIVER *dummy_valid (char *name)
{
  char *s,tmp[MAILTMPLEN];
  struct stat sbuf;
				/* must be valid local mailbox */
  if (name && *name && (*name != '{') && (s = mailboxfile (tmp,name))) {
				/* indeterminate clearbox INBOX */
    if (!*s) return &dummydriver;
    else if (!stat (s,&sbuf)) switch (sbuf.st_mode & S_IFMT) {
    case S_IFREG:
    case S_IFDIR:
      return &dummydriver;
    }
    else if (((name[0] == 'I') || (name[0] == 'i')) &&
	     ((name[1] == 'N') || (name[1] == 'n')) &&
	     ((name[2] == 'B') || (name[2] == 'b')) &&
	     ((name[3] == 'O') || (name[3] == 'o')) &&
	     ((name[4] == 'X') || (name[4] == 'x')) && !name[5])
      return &dummydriver;	/* blackbox INBOX does not exist yet */
  }
  return NIL;
}


/* Dummy manipulate driver parameters
 * Accepts: function code
 *	    function-dependent value
 * Returns: function-dependent return value
 */

void *dummy_parameters (long function,void *value)
{
  return NIL;
}

/* Dummy scan mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 *	    string to scan
 */

void dummy_scan (MAILSTREAM *stream,char *ref,char *pat,char *contents)
{
  char *s,test[MAILTMPLEN],file[MAILTMPLEN];
  long i;
  if (!pat || !*pat) {		/* empty pattern? */
    if (dummy_canonicalize (test,ref,"*")) {
				/* tie off name at root */
      if (s = strchr (test,'/')) *++s = '\0';
      else test[0] = '\0';
      dummy_listed (stream,'/',test,LATT_NOSELECT,NIL);
    }
  }
				/* get canonical form of name */
  else if (dummy_canonicalize (test,ref,pat)) {
				/* found any wildcards? */
    if (s = strpbrk (test,"%*")) {
				/* yes, copy name up to that point */
      strncpy (file,test,i = s - test);
      file[i] = '\0';		/* tie off */
    }
    else strcpy (file,test);	/* use just that name then */
    if (s = strrchr (file,'/')){/* find directory name */
      *++s = '\0';		/* found, tie off at that point */
      s = file;
    }
				/* silly case */
    else if ((file[0] == '~') || (file[0] == '#')) s = file;
				/* do the work */
    dummy_list_work (stream,s,test,contents,0);
				/* always an INBOX */
    if (pmatch ("INBOX",ucase (test)))
      dummy_listed (stream,NIL,"INBOX",LATT_NOINFERIORS,contents);
  }
}

/* Dummy list mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void dummy_list (MAILSTREAM *stream,char *ref,char *pat)
{
  dummy_scan (stream,ref,pat,NIL);
}


/* Dummy list subscribed mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void dummy_lsub (MAILSTREAM *stream,char *ref,char *pat)
{
  void *sdb = NIL;
  char *s,*t,test[MAILTMPLEN],tmp[MAILTMPLEN];
  int showuppers = pat[strlen (pat) - 1] == '%';
				/* get canonical form of name */
  if (dummy_canonicalize (test,ref,pat) && (s = sm_read (&sdb))) do
    if (*s != '{') {
      if (((s[0] == 'I') || (s[0] == 'i')) &&
	  ((s[1] == 'N') || (s[1] == 'n')) &&
	  ((s[2] == 'B') || (s[2] == 'b')) &&
	  ((s[3] == 'O') || (s[3] == 'o')) &&
	  ((s[4] == 'X') || (s[4] == 'x')) && !s[5] &&
	  pmatch ("INBOX",ucase (strcpy (tmp,test))))
	mm_lsub (stream,NIL,s,LATT_NOINFERIORS);
      else if (pmatch_full (s,test,'/')) mm_lsub (stream,'/',s,NIL);
      else while (showuppers && (t = strrchr (s,'/'))) {
	*t = '\0';		/* tie off the name */
	if (pmatch_full (s,test,'/')) mm_lsub (stream,'/',s,LATT_NOSELECT);
      }
    }
  while (s = sm_read (&sdb));	/* until no more subscriptions */
}

/* Dummy list mailboxes worker routine
 * Accepts: mail stream
 *	    directory name to search
 *	    search pattern
 *	    string to scan
 *	    search level
 */

void dummy_list_work (MAILSTREAM *stream,char *dir,char *pat,char *contents,
		      long level)
{
  DIR *dp;
  struct direct *d;
  struct stat sbuf;
  char tmp[MAILTMPLEN];
				/* punt if bogus name */
  if (!mailboxdir (tmp,dir,NIL)) return;
  if (dp = opendir (tmp)) {	/* do nothing if can't open directory */
				/* list it if not at top-level */
    if (!level && dir && pmatch_full (dir,pat,'/'))
      dummy_listed (stream,'/',dir,LATT_NOSELECT,contents);
				/* scan directory, ignore . and .. */
    if (!dir || dir[strlen (dir) - 1] == '/') while (d = readdir (dp))
      if ((d->d_name[0] != '.') ||
	  (d->d_name[1] && (((d->d_name[1] != '.') || d->d_name[2]) &&
			    strcmp (d->d_name+1,MXINDEXNAME+3)))) {
				/* see if name is useful */
	if (dir) sprintf (tmp,"%s%s",dir,d->d_name);
	else strcpy (tmp,d->d_name);
				/* make sure useful and can get info */
	if ((pmatch_full (tmp,pat,'/') ||
	     pmatch_full (strcat (tmp,"/"),pat,'/') || dmatch (tmp,pat,'/')) &&
	    mailboxdir (tmp,dir,d->d_name) && tmp[0] && !stat (tmp,&sbuf)) {
				/* now make name we'd return */
	  if (dir) sprintf (tmp,"%s%s",dir,d->d_name);
	  else strcpy (tmp,d->d_name);
				/* only interested in file type */
	  switch (sbuf.st_mode &= S_IFMT) {
	  case S_IFDIR:		/* directory? */
	    if (pmatch_full (tmp,pat,'/')) {
	      if (!dummy_listed (stream,'/',tmp,LATT_NOSELECT,contents)) break;
	      strcat (tmp,"/");	/* set up for dmatch call */
	    }
				/* try again with trailing / */
	    else if (pmatch_full (strcat (tmp,"/"),pat,'/') &&
		     !dummy_listed (stream,'/',tmp,LATT_NOSELECT,contents))
	      break;
	    if (dmatch (tmp,pat,'/') &&
		(level < (long) mail_parameters (NIL,GET_LISTMAXLEVEL,NIL)))
	      dummy_list_work (stream,tmp,pat,contents,level+1);
	    break;
	  case S_IFREG:		/* ordinary name */
	    /* Must use ctime for systems that don't update mtime properly */
	    if (pmatch_full (tmp,pat,'/') &&
		!(((tmp[0] == 'I') || (tmp[0] == 'i')) &&
		  ((tmp[1] == 'N') || (tmp[1] == 'n')) &&
		  ((tmp[2] == 'B') || (tmp[2] == 'b')) &&
		  ((tmp[3] == 'O') || (tmp[3] == 'o')) &&
		  ((tmp[4] == 'X') || (tmp[4] == 'x')) && !tmp[5]))
	      dummy_listed (stream,'/',tmp,LATT_NOINFERIORS +
			    ((sbuf.st_size && (sbuf.st_atime<=sbuf.st_ctime)) ?
			     ((sbuf.st_atime<sbuf.st_ctime) ? LATT_MARKED : 0)
			     : LATT_UNMARKED),contents);
	    break;
	  }
	}
      }
    closedir (dp);		/* all done, flush directory */
  }
}

/* Mailbox found
 * Accepts: hierarchy delimiter
 *	    mailbox name
 *	    attributes
 *	    contents to search before calling mm_list()
 * Returns: NIL if should abort hierarchy search, else T
 */

#define BUFSIZE 4*MAILTMPLEN

long dummy_listed (MAILSTREAM *stream,char delimiter,char *name,
		   long attributes,char *contents)
{
  struct stat sbuf;
  int fd;
  long csiz,ssiz,bsiz;
  char *buf,tmp[MAILTMPLEN];
  DRIVER *d;
				/* don't \NoSelect if have a driver for it */
  if ((attributes & LATT_NOSELECT) && (d = mail_valid (NIL,name,NIL)) &&
      (d != &dummydriver)) attributes &= ~LATT_NOSELECT;
  if (contents) {		/* want to search contents? */
				/* forget it if can't select or open */
    if ((attributes & LATT_NOSELECT) || !(csiz = strlen (contents)) ||
	stat (dummy_file (tmp,name),&sbuf) || (csiz > sbuf.st_size) ||
	((fd = open (tmp,O_RDONLY,NIL)) < 0)) return T;
				/* get buffer including slop */    
    buf = (char *) fs_get (BUFSIZE + (ssiz = 4 * ((csiz / 4) + 1)) + 1);
    memset (buf,'\0',ssiz);	/* no slop area the first time */
    while (sbuf.st_size) {	/* until end of file */
      read (fd,buf+ssiz,bsiz = min (sbuf.st_size,BUFSIZE));
      if (search ((unsigned char *) buf,bsiz+ssiz,
		  (unsigned char *) contents,csiz)) break;
      memcpy (buf,buf+BUFSIZE,ssiz);
      sbuf.st_size -= bsiz;	/* note that we read that much */
    }
    fs_give ((void **) &buf);	/* flush buffer */
    close (fd);			/* finished with file */
    if (!sbuf.st_size) return T;/* not found */
  }
				/* notify main program */
  mm_list (stream,delimiter,name,attributes);
  return T;
}

/* Dummy create mailbox
 * Accepts: mail stream
 *	    mailbox name to create
 * Returns: T on success, NIL on failure
 */

long dummy_create (MAILSTREAM *stream,char *mailbox)
{
  char tmp[MAILTMPLEN];
  if (strcmp (ucase (strcpy (tmp,mailbox)),"INBOX") && dummy_file(tmp,mailbox))
    return dummy_create_path (stream,tmp);
  sprintf (tmp,"Can't create %s: invalid name",mailbox);
  mm_log (tmp,ERROR);
  return NIL;
}


/* Dummy create path
 * Accepts: mail stream
 *	    path name name to create
 * Returns: T on success, NIL on failure
 */

long dummy_create_path (MAILSTREAM *stream,char *path)
{
  struct stat sbuf;
  char c,*s,tmp[MAILTMPLEN];
  int fd;
  long ret = NIL;
  char *t = strrchr (path,'/');
  int wantdir = t && !t[1];
  if (wantdir) *t = '\0';	/* flush trailing delimiter for directory */
  if (s = strrchr (path,'/')) {	/* found superior to this name? */
    c = *++s;			/* remember first character of inferior */
    *s = '\0';			/* tie off to get just superior */
				/* name doesn't exist, create it */
    if ((stat (path,&sbuf) || ((sbuf.st_mode & S_IFMT) != S_IFDIR)) &&
	!dummy_create_path (stream,path)) return NIL;
    *s = c;			/* restore full name */
  }
  if (wantdir) {		/* want to create directory? */
    ret = !mkdir (path,(int) mail_parameters (NIL,GET_DIRPROTECTION,NIL));
    *t = '/';			/* restore directory delimiter */
  }
				/* create file */
  else if ((fd = open (path,O_WRONLY|O_CREAT|O_EXCL,
		       (int) mail_parameters(NIL,GET_MBXPROTECTION,NIL))) >= 0)
    ret = !close (fd);
  if (!ret) {			/* error? */
    sprintf (tmp,"Can't create mailbox node %s: %s",path,strerror (errno));
    mm_log (tmp,ERROR);
  }
  return ret;			/* return status */
}

/* Dummy delete mailbox
 * Accepts: mail stream
 *	    mailbox name to delete
 * Returns: T on success, NIL on failure
 */

long dummy_delete (MAILSTREAM *stream,char *mailbox)
{
  struct stat sbuf;
  char *s,tmp[MAILTMPLEN];
				/* no trailing / (workaround BSD kernel bug) */
  if ((s = strrchr (dummy_file (tmp,mailbox),'/')) && !s[1]) *s = '\0';
  if (stat (tmp,&sbuf) || ((sbuf.st_mode & S_IFMT) == S_IFDIR) ?
      rmdir (tmp) : unlink (tmp)) {
    sprintf (tmp,"Can't delete mailbox %s: %s",mailbox,strerror (errno));
    mm_log (tmp,ERROR);
    return NIL;
  }
  return T;			/* return success */
}

/* Mail rename mailbox
 * Accepts: mail stream
 *	    old mailbox name
 *	    new mailbox name
 * Returns: T on success, NIL on failure
 */

long dummy_rename (MAILSTREAM *stream,char *old,char *newname)
{
  struct stat sbuf;
  char c,*s,tmp[MAILTMPLEN],mbx[MAILTMPLEN];
  long ret = NIL;
  if (!(s = dummy_file (mbx,newname))) {
    sprintf (mbx,"Can't rename %s to %s: invalid name",old,newname);
    mm_log (mbx,ERROR);
    return NIL;
  }
				/* found superior to destination name? */
  if (s = strrchr (s,'/')) {
    c = *++s;			/* remember first character of inferior */
    *s = '\0';			/* tie off to get just superior */
				/* name doesn't exist, create it */
    if ((stat (mbx,&sbuf) || ((sbuf.st_mode & S_IFMT) != S_IFDIR)) &&
	!dummy_create (stream,mbx)) return NIL;
    *s = c;			/* restore full name */
  }
				/* rename of non-ex INBOX creates dest */
  if (!strcmp (ucase (strcpy (tmp,old)),"INBOX") &&
      stat (dummy_file (tmp,old),&sbuf)) return dummy_create (NIL,mbx);
  if (rename (dummy_file (tmp,old),mbx)) {
    sprintf (tmp,"Can't rename mailbox %s to %s: %s",old,newname,
	     strerror (errno));
    mm_log (tmp,ERROR);
    return NIL;
  }
  return T;			/* return success */
}

/* Dummy open
 * Accepts: stream to open
 * Returns: stream on success, NIL on failure
 */

MAILSTREAM *dummy_open (MAILSTREAM *stream)
{
  int fd;
  char err[MAILTMPLEN],tmp[MAILTMPLEN];
  struct stat sbuf;
				/* OP_PROTOTYPE call */
  if (!stream) return &dummyproto;
  err[0] = '\0';		/* no error message yet */
				/* can we open the file? */
  if ((fd = open (dummy_file (tmp,stream->mailbox),O_RDONLY,NIL)) < 0) {
				/* no, error unless INBOX */
    if (strcmp (ucase (strcpy (tmp,stream->mailbox)),"INBOX"))
      sprintf (err,"%s: %s",strerror (errno),stream->mailbox);
  }
  else {			/* file had better be empty then */
    fstat (fd,&sbuf);		/* sniff at its size */
    close (fd);
    if ((sbuf.st_mode & S_IFMT) != S_IFREG)
      sprintf (err,"Can't open %s: not a selectable mailbox",stream->mailbox);
    else if (sbuf.st_size)	/* bogus format if non-empty */
      sprintf (err,"Can't open %s (file %s): not in valid mailbox format",
	       stream->mailbox,tmp);
  }
  if (err[0]) {			/* if an error happened */
    mm_log (err,stream->silent ? WARN : ERROR);
    return NIL;
  }
  else if (!stream->silent) {	/* only if silence not requested */
    mail_exists (stream,0);	/* say there are 0 messages */
    mail_recent (stream,0);	/* and certainly no recent ones! */
    stream->uid_validity = 1;
  }
  return stream;		/* return success */
}


/* Dummy close
 * Accepts: MAIL stream
 *	    options
 */

void dummy_close (MAILSTREAM *stream,long options)
{
				/* return silently */
}

/* Dummy ping mailbox
 * Accepts: MAIL stream
 * Returns: T if stream alive, else NIL
 * No-op for readonly files, since read/writer can expunge it from under us!
 */

long dummy_ping (MAILSTREAM *stream)
{
  char tmp[MAILTMPLEN];
  MAILSTREAM *test = mail_open (NIL,stream->mailbox,OP_PROTOTYPE);
				/* swap streams if looks like a new driver */
  if (test && (test->dtb != stream->dtb))
    test = mail_open (stream,strcpy (tmp,stream->mailbox),NIL);
  return test ? T : NIL;
}


/* Dummy check mailbox
 * Accepts: MAIL stream
 * No-op for readonly files, since read/writer can expunge it from under us!
 */

void dummy_check (MAILSTREAM *stream)
{
  dummy_ping (stream);		/* invoke ping */
}


/* Dummy expunge mailbox
 * Accepts: MAIL stream
 */

void dummy_expunge (MAILSTREAM *stream)
{
				/* return silently */
}

/* Dummy copy message(s)
 * Accepts: MAIL stream
 *	    sequence
 *	    destination mailbox
 *	    options
 * Returns: T if copy successful, else NIL
 */

long dummy_copy (MAILSTREAM *stream,char *sequence,char *mailbox,long options)
{
  if ((options & CP_UID) ? mail_uid_sequence (stream,sequence) :
      mail_sequence (stream,sequence)) fatal ("Impossible dummy_copy");
  return NIL;
}


/* Dummy append message string
 * Accepts: mail stream
 *	    destination mailbox
 *	    optional flags
 *	    optional date
 *	    stringstruct of message to append
 * Returns: T on success, NIL on failure
 */

long dummy_append (MAILSTREAM *stream,char *mailbox,char *flags,char *date,
		   STRING *message)
{
  struct stat sbuf;
  int fd = -1;
  int e;
  char tmp[MAILTMPLEN];
  MAILSTREAM *ts = default_proto (T);
  if ((strcmp (ucase (strcpy (tmp,mailbox)),"INBOX")) &&
	   ((fd = open (dummy_file (tmp,mailbox),O_RDONLY,NIL)) < 0)) {
    if ((e = errno) == ENOENT)	/* failed, was it no such file? */
      mm_notify (stream,"[TRYCREATE] Must create mailbox before append",NIL);
    sprintf (tmp,"%s: %s",strerror (e),mailbox);
    mm_log (tmp,ERROR);		/* pass up error */
    return NIL;			/* always fails */
  }
  if (fd >= 0) {		/* found file? */
    fstat (fd,&sbuf);		/* get its size */
    close (fd);			/* toss out the fd */
    if (sbuf.st_size) ts = NIL;	/* non-empty file? */
  }
  if (ts) return (*ts->dtb->append) (stream,mailbox,flags,date,message);
  sprintf (tmp,"Indeterminate mailbox format: %s",mailbox);
  mm_log (tmp,ERROR);
  return NIL;
}

/* Dummy mail generate file string
 * Accepts: temporary buffer to write into
 *	    mailbox name string
 * Returns: local file string or NIL if failure
 */

char *dummy_file (char *dst,char *name)
{
  char *s = mailboxfile (dst,name);
				/* return our standard inbox */
  return (s && !*s) ? strcpy (dst,sysinbox ()) : s;
}


/* Dummy canonicalize name
 * Accepts: buffer to write name
 *	    reference
 *	    pattern
 * Returns: T if success, NIL if failure
 */

long dummy_canonicalize (char *tmp,char *ref,char *pat)
{
  char *s;
  if (ref) {			/* preliminary reference check */
    if (*ref == '{') return NIL;/* remote reference not allowed */
    else if (!*ref) ref = NIL;	/* treat empty reference as no reference */
  }
  switch (*pat) {
  case '#':			/* namespace name */
    if (mailboxfile (tmp,pat)) strcpy (tmp,pat);
    else return NIL;		/* unknown namespace */
    break;
  case '{':			/* remote names not allowed */
    return NIL;
  case '/':			/* rooted name */
  case '~':			/* home directory name */
    if (!ref || (*ref != '#')) {/* non-namespace reference? */
      strcpy (tmp,pat);		/* yes, ignore */
      break;
    }
				/* fall through */
  default:			/* apply reference for all other names */
    if (!ref) strcpy (tmp,pat);	/* just copy if no namespace */
    else if ((*ref != '#') || mailboxfile (tmp,ref)) {
				/* wants root of name? */
      if (*pat == '/') strcpy (strchr (strcpy (tmp,ref),'/'),pat);
				/* otherwise just append */
      else sprintf (tmp,"%s%s",ref,pat);
    }
    else return NIL;		/* unknown namespace */
  }
  return T;
}
