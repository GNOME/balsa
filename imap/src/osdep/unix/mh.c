/*
 * Program:	MH mail routines
 *
 * Author(s):	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	23 February 1992
 * Last Edited:	3 June 1998
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
#include <sys/time.h>
#include "mh.h"
#include "misc.h"
#include "dummy.h"

/* MH mail routines */


/* Driver dispatch used by MAIL */

DRIVER mhdriver = {
  "mh",				/* driver name */
				/* driver flags */
  DR_MAIL|DR_LOCAL|DR_NOFAST|DR_NAMESPACE,
  (DRIVER *) NIL,		/* next driver */
  mh_valid,			/* mailbox is valid for us */
  mh_parameters,		/* manipulate parameters */
  mh_scan,			/* scan mailboxes */
  mh_list,			/* find mailboxes */
  mh_lsub,			/* find subscribed mailboxes */
  mh_subscribe,			/* subscribe to mailbox */
  mh_unsubscribe,		/* unsubscribe from mailbox */
  mh_create,			/* create mailbox */
  mh_delete,			/* delete mailbox */
  mh_rename,			/* rename mailbox */
  NIL,				/* status of mailbox */
  mh_open,			/* open mailbox */
  mh_close,			/* close mailbox */
  mh_fast,			/* fetch message "fast" attributes */
  NIL,				/* fetch message flags */
  NIL,				/* fetch overview */
  NIL,				/* fetch message envelopes */
  mh_header,			/* fetch message header */
  mh_text,			/* fetch message body */
  NIL,				/* fetch partial message text */
  NIL,				/* unique identifier */
  NIL,				/* message number */
  NIL,				/* modify flags */
  NIL,				/* per-message modify flags */
  NIL,				/* search for message based on criteria */
  NIL,				/* sort messages */
  NIL,				/* thread messages */
  mh_ping,			/* ping mailbox to see if still alive */
  mh_check,			/* check for new messages */
  mh_expunge,			/* expunge deleted messages */
  mh_copy,			/* copy messages to another mailbox */
  mh_append,			/* append string message to mailbox */
  NIL				/* garbage collect stream */
};

				/* prototype stream */
MAILSTREAM mhproto = {&mhdriver};


/* MH mail validate mailbox
 * Accepts: mailbox name
 * Returns: our driver if name is valid, NIL otherwise
 */

DRIVER *mh_valid (char *name)
{
  char tmp[MAILTMPLEN];
  return mh_isvalid (name,tmp,T) ? &mhdriver : NIL;
}

/* MH mail test for valid mailbox
 * Accepts: mailbox name
 *	    temporary buffer to use
 *	    syntax only test flag
 * Returns: T if valid, NIL otherwise
 */

static char *mh_profile = NIL;	/* holds MH profile */
static char *mh_path = NIL;	/* holds MH path name */
static long mh_once = 0;	/* already through this code */

int mh_isvalid (char *name,char *tmp,long synonly)
{
  struct stat sbuf;
  if (!mh_path) {		/* have MH path yet? */
    char *s,*s1,*t,*v;
    int fd;
    if (mh_once++) return NIL;	/* only do this code once */
    if (!mh_profile) {		/* have MH profile? */
      sprintf (tmp,"%s/%s",myhomedir (),MHPROFILE);
      mh_profile = cpystr (tmp);
    }
    if ((fd = open (tmp,O_RDONLY,NIL)) < 0) return NIL;
    fstat (fd,&sbuf);		/* yes, get size and read file */
    read (fd,(s1 = t = (char *) fs_get (sbuf.st_size + 1)),sbuf.st_size);
    close (fd);			/* don't need the file any more */
    t[sbuf.st_size] = '\0';	/* tie it off */
				/* parse profile file */
    while (*(s = t) && (t = strchr (s,'\n'))) {
      *t++ = '\0';		/* tie off line */
				/* found space in line? */
      if (v = strpbrk (s," \t")) {
	*v = '\0';		/* tie off, is keyword "Path:"? */
	if (!strcmp (lcase (s),"path:")) {
				/* skip whitespace */
	  while (*++v == ' ' || *v == '\t');
	  if (*v == '/') s = v;	/* absolute path? */
	  else sprintf (s = tmp,"%s/%s",myhomedir (),v);
	  mh_path = cpystr (s);	/* copy name */
	  break;		/* don't need to look at rest of file */
	}
      }
    }
    fs_give ((void **) &s1);	/* flush profile text */
    if (!mh_path) {		/* default path if not in the profile */
      sprintf (tmp,"%s/%s",myhomedir (),MHPATH);
      mh_path = cpystr (tmp);
    }
  }

				/* name must be #MHINBOX or #mh/... */
  if (strcmp (ucase (strcpy (tmp,name)),"#MHINBOX") &&
      !(tmp[0] == '#' && tmp[1] == 'M' && tmp[2] == 'H' && tmp[3] == '/')) {
    errno = EINVAL;		/* bogus name */
    return NIL;
  }
				/* all done if syntax only check */
  if (synonly && tmp[0] == '#') return T;
  errno = NIL;			/* zap error */
				/* validate name as directory */
  return ((stat (mh_file (tmp,name),&sbuf) == 0) &&
	  (sbuf.st_mode & S_IFMT) == S_IFDIR);
}


/* MH manipulate driver parameters
 * Accepts: function code
 *	    function-dependent value
 * Returns: function-dependent return value
 */

void *mh_parameters (long function,void *value)
{
  switch ((int) function) {
  case SET_MHPROFILE:
    if (mh_profile) fs_give ((void **) &mh_profile);
    mh_profile = cpystr ((char *) value);
    break;
  case GET_MHPROFILE:
    value = (void *) mh_profile;
    break;
  case SET_MHPATH:
    if (mh_path) fs_give ((void **) &mh_path);
    mh_path = cpystr ((char *) value);
    break;
  case GET_MHPATH:
    value = (void *) mh_path;
    break;
  default:
    value = NIL;		/* error case */
    break;
  }
  return NIL;
}

/* MH scan mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 *	    string to scan
 */

void mh_scan (MAILSTREAM *stream,char *ref,char *pat,char *contents)
{
  char tmp[MAILTMPLEN];
  if (mh_canonicalize (tmp,ref,pat))
    mm_log ("Scan not valid for mh mailboxes",WARN);
}

/* MH list mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void mh_list (MAILSTREAM *stream,char *ref,char *pat)
{
  char *s,test[MAILTMPLEN],file[MAILTMPLEN];
  long i = 0;
				/* get canonical form of name */
  if (mh_canonicalize (test,ref,pat)) {
    if (test[3] == '/') {	/* looking down levels? */
				/* yes, found any wildcards? */
      if (s = strpbrk (test,"%*")) {
				/* yes, copy name up to that point */
	strncpy (file,test+4,i = s - (test+4));
	file[i] = '\0';		/* tie off */
      }
      else strcpy (file,test+4);/* use just that name then */
				/* find directory name */
      if (s = strrchr (file,'/')) {
	*s = '\0';		/* found, tie off at that point */
	s = file;
      }
				/* do the work */
      mh_list_work (stream,s,test,0);
    }
				/* always an INBOX */
    if (pmatch ("#MHINBOX",ucase (test)))
      mm_list (stream,NIL,"#MHINBOX",LATT_NOINFERIORS);
  }
}


/* MH list subscribed mailboxes
 * Accepts: mail stream
 *	    reference
 *	    pattern to search
 */

void mh_lsub (MAILSTREAM *stream,char *ref,char *pat)
{
  void *sdb = NIL;
  char *s,test[MAILTMPLEN];
				/* get canonical form of name */
  if (mh_canonicalize (test,ref,pat) && (s = sm_read (&sdb))) {
    do if (pmatch_full (s,test,'/')) mm_lsub (stream,'/',s,NIL);
    while (s = sm_read (&sdb)); /* until no more subscriptions */
  }
}

/* MH list mailboxes worker routine
 * Accepts: mail stream
 *	    directory name to search
 *	    search pattern
 *	    search level
 */

void mh_list_work (MAILSTREAM *stream,char *dir,char *pat,long level)
{
  DIR *dp;
  struct direct *d;
  struct stat sbuf;
  char *cp,*np,curdir[MAILTMPLEN],name[MAILTMPLEN];
				/* build MH name to search */
  if (dir) sprintf (name,"#mh/%s/",dir);
  else strcpy (name,"#mh/");
				/* make directory name, punt if bogus */
  if (!mh_file (curdir,name)) return;
  cp = curdir + strlen (curdir);/* end of directory name */
  np = name + strlen (name);	/* end of MH name */
  if (dp = opendir (curdir)) {	/* open directory */
    while (d = readdir (dp))	/* scan directory, ignore all . names */
      if (d->d_name[0] != '.') {/* build file name */
	strcpy (cp,d->d_name);	/* make directory name */
	if (!stat (curdir,&sbuf) && ((sbuf.st_mode &= S_IFMT) == S_IFDIR)) {
	  strcpy (np,d->d_name);/* make mh name of directory name */
				/* yes, an MH name if full match */
	  if (pmatch_full (name,pat,'/')) mm_list (stream,'/',name,NIL);
				/* check if should recurse */
	  if (dmatch (name,pat,'/') &&
	      (level < (long) mail_parameters (NIL,GET_LISTMAXLEVEL,NIL)))
	    mh_list_work (stream,name+4,pat,level+1);
	}
      }
    closedir (dp);		/* all done, flush directory */
  }
}

/* MH mail subscribe to mailbox
 * Accepts: mail stream
 *	    mailbox to add to subscription list
 * Returns: T on success, NIL on failure
 */

long mh_subscribe (MAILSTREAM *stream,char *mailbox)
{
  char tmp[MAILTMPLEN];
  return sm_subscribe (mailbox);
}


/* MH mail unsubscribe to mailbox
 * Accepts: mail stream
 *	    mailbox to delete from subscription list
 * Returns: T on success, NIL on failure
 */

long mh_unsubscribe (MAILSTREAM *stream,char *mailbox)
{
  char tmp[MAILTMPLEN];
  return sm_unsubscribe (mailbox);
}

/* MH mail create mailbox
 * Accepts: mail stream
 *	    mailbox name to create
 * Returns: T on success, NIL on failure
 */

long mh_create (MAILSTREAM *stream,char *mailbox)
{
  char tmp[MAILTMPLEN];
  if (!(mailbox[0] == '#' && (mailbox[1] == 'm' || mailbox[1] == 'M') &&
	(mailbox[2] == 'h' || mailbox[2] == 'H') && mailbox[3] == '/')) {
    sprintf (tmp,"Can't create mailbox %s: invalid MH-format name",mailbox);
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* must not already exist */
  if (mh_isvalid (mailbox,tmp,NIL)) {
    sprintf (tmp,"Can't create mailbox %s: mailbox already exists",mailbox);
    mm_log (tmp,ERROR);
    return NIL;
  }
  if (!mh_path) return NIL;	/* sorry */
  sprintf (tmp,"%s/%s/",mh_path,mailbox + 4);
				/* try to make it */
  if (!dummy_create_path (stream,tmp)) {
    sprintf (tmp,"Can't create mailbox %s: %s",mailbox,strerror (errno));
    mm_log (tmp,ERROR);
    return NIL;
  }
  return T;			/* return success */
}

/* MH mail delete mailbox
 *	    mailbox name to delete
 * Returns: T on success, NIL on failure
 */

long mh_delete (MAILSTREAM *stream,char *mailbox)
{
  DIR *dirp;
  struct direct *d;
  int i;
  char tmp[MAILTMPLEN];
  if (!(mailbox[0] == '#' && (mailbox[1] == 'm' || mailbox[1] == 'M') &&
	(mailbox[2] == 'h' || mailbox[2] == 'H') && mailbox[3] == '/')) {
    sprintf (tmp,"Can't delete mailbox %s: invalid MH-format name",mailbox);
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* is mailbox valid? */
  if (!mh_isvalid (mailbox,tmp,NIL)){
    sprintf (tmp,"Can't delete mailbox %s: no such mailbox",mailbox);
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* get name of directory */
  i = strlen (mh_file (tmp,mailbox));
  if (dirp = opendir (tmp)) {	/* open directory */
    tmp[i++] = '/';		/* now apply trailing delimiter */
    while (d = readdir (dirp))	/* massacre all numeric or comma files */
      if (mh_select (d) || *d->d_name == ',') {
	strcpy (tmp + i,d->d_name);
	unlink (tmp);		/* sayonara */
      }
    closedir (dirp);		/* flush directory */
  }
				/* try to remove the directory */
  if (rmdir (mh_file (tmp,mailbox))) {
    sprintf (tmp,"Can't delete mailbox %s: %s",mailbox,strerror (errno));
    mm_log (tmp,ERROR);
    return NIL;
  }
  return T;			/* return success */
}

/* MH mail rename mailbox
 * Accepts: MH mail stream
 *	    old mailbox name
 *	    new mailbox name
 * Returns: T on success, NIL on failure
 */

long mh_rename (MAILSTREAM *stream,char *old,char *newname)
{
  char tmp[MAILTMPLEN],tmp1[MAILTMPLEN];
  if (!(old[0] == '#' && (old[1] == 'm' || old[1] == 'M') &&
	(old[2] == 'h' || old[2] == 'H') && old[3] == '/')) {
    sprintf (tmp,"Can't delete mailbox %s: invalid MH-format name",old);
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* old mailbox name must be valid */
  if (!mh_isvalid (old,tmp,NIL)) {
    sprintf (tmp,"Can't rename mailbox %s: no such mailbox",old);
    mm_log (tmp,ERROR);
    return NIL;
  }
  if (!(newname[0] == '#' && (newname[1] == 'm' || newname[1] == 'M') &&
	(newname[2] == 'h' || newname[2] == 'H') && newname[3] == '/')) {
    sprintf (tmp,"Can't rename to mailbox %s: invalid MH-format name",newname);
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* new mailbox name must not be valid */
  if (mh_isvalid (newname,tmp,NIL)) {
    sprintf (tmp,"Can't rename to mailbox %s: destination already exists",
	     newname);
    mm_log (tmp,ERROR);
    return NIL;
  }
				/* try to rename the directory */
  if (rename (mh_file (tmp,old),mh_file (tmp1,newname))) {
    sprintf (tmp,"Can't rename mailbox %s to %s: %s",old,newname,
	     strerror (errno));
    mm_log (tmp,ERROR);
    return NIL;
  }
  return T;			/* return success */
}

/* MH mail open
 * Accepts: stream to open
 * Returns: stream on success, NIL on failure
 */

MAILSTREAM *mh_open (MAILSTREAM *stream)
{
  char tmp[MAILTMPLEN];
  if (!stream) return &mhproto;	/* return prototype for OP_PROTOTYPE call */
  if (LOCAL) {			/* close old file if stream being recycled */
    mh_close (stream,NIL);	/* dump and save the changes */
    stream->dtb = &mhdriver;	/* reattach this driver */
    mail_free_cache (stream);	/* clean up cache */
    stream->uid_last = 0;	/* default UID validity */
    stream->uid_validity = time (0);
  }
  stream->local = fs_get (sizeof (MHLOCAL));
				/* note if an INBOX or not */
  LOCAL->inbox = !strcmp (ucase (strcpy (tmp,stream->mailbox)),"#MHINBOX");
  mh_file (tmp,stream->mailbox);/* get directory name */
  LOCAL->dir = cpystr (tmp);	/* copy directory name for later */
				/* make temporary buffer */
  LOCAL->buf = (char *) fs_get ((LOCAL->buflen = MAXMESSAGESIZE) + 1);
  LOCAL->scantime = 0;		/* not scanned yet */
  stream->sequence++;		/* bump sequence number */
				/* parse mailbox */
  stream->nmsgs = stream->recent = 0;
  if (!mh_ping (stream)) return NIL;
  if (!(stream->nmsgs || stream->silent))
    mm_log ("Mailbox is empty",(long) NIL);
  return stream;		/* return stream to caller */
}

/* MH mail close
 * Accepts: MAIL stream
 *	    close options
 */

void mh_close (MAILSTREAM *stream,long options)
{
  if (LOCAL) {			/* only if a file is open */
    int silent = stream->silent;
    stream->silent = T;		/* note this stream is dying */
    if (options & CL_EXPUNGE) mh_expunge (stream);
    if (LOCAL->dir) fs_give ((void **) &LOCAL->dir);
				/* free local scratch buffer */
    if (LOCAL->buf) fs_give ((void **) &LOCAL->buf);
				/* nuke the local data */
    fs_give ((void **) &stream->local);
    stream->dtb = NIL;		/* log out the DTB */
    stream->silent = silent;	/* reset silent state */
  }
}


/* MH mail fetch fast information
 * Accepts: MAIL stream
 *	    sequence
 *	    option flags
 */

void mh_fast (MAILSTREAM *stream,char *sequence,long flags)
{
  unsigned long i,j;
				/* ugly and slow */
  if (stream && LOCAL && ((flags & FT_UID) ?
			  mail_uid_sequence (stream,sequence) :
			  mail_sequence (stream,sequence)))
    for (i = 1; i <= stream->nmsgs; i++)
      if (mail_elt (stream,i)->sequence) mh_header (stream,i,&j,NIL);
}

/* MH mail fetch message header
 * Accepts: MAIL stream
 *	    message # to fetch
 *	    pointer to returned header text length
 *	    option flags
 * Returns: message header in RFC822 format
 */

char *mh_header (MAILSTREAM *stream,unsigned long msgno,unsigned long *length,
		 long flags)
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

/* MH mail fetch message text (body only)
 * Accepts: MAIL stream
 *	    message # to fetch
 *	    pointer to returned stringstruct
 *	    option flags
 * Returns: T on success, NIL on failure
 */

long mh_text (MAILSTREAM *stream,unsigned long msgno,STRING *bs,long flags)
{
  unsigned long i;
  MESSAGECACHE *elt;
				/* UID call "impossible" */
  if (flags & FT_UID) return NIL;
  elt = mail_elt (stream,msgno);/* get elt */
				/* snarf message if don't have it yet */
  if (!elt->private.msg.text.text.data) {
    mh_header (stream,msgno,&i,flags);
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

/* MH mail ping mailbox
 * Accepts: MAIL stream
 * Returns: T if stream alive, else NIL
 */

long mh_ping (MAILSTREAM *stream)
{
  MAILSTREAM *sysibx = NIL;
  MESSAGECACHE *elt,*selt;
  struct stat sbuf;
  char *s,tmp[MAILTMPLEN];
  int fd;
  unsigned long i,j,r,old;
  long nmsgs = stream->nmsgs;
  long recent = stream->recent;
  int silent = stream->silent;
  if (stat (LOCAL->dir,&sbuf)) { /* directory exists? */
    if (LOCAL->inbox) return T;
    mm_log ("No such mailbox",ERROR);
    return NIL;
  }
  stream->silent = T;		/* don't pass up mm_exists() events yet */
  if (sbuf.st_ctime != LOCAL->scantime) {
    struct direct **names = NIL;
    long nfiles = scandir (LOCAL->dir,&names,mh_select,mh_numsort);
    if (nfiles < 0) nfiles = 0;	/* in case error */
    old = stream->uid_last;
				/* note scanned now */
    LOCAL->scantime = sbuf.st_ctime;
				/* scan directory */
    for (i = 0; i < nfiles; ++i) {
				/* if newly seen, add to list */
      if ((j = atoi (names[i]->d_name)) > old) {
	mail_exists (stream,++nmsgs);
	stream->uid_last = (elt = mail_elt (stream,nmsgs))->private.uid = j;
	elt->valid = T;		/* note valid flags */
	if (old) {		/* other than the first pass? */
	  elt->recent = T;	/* yup, mark as recent */
	  recent++;		/* bump recent count */
	}
	else {			/* see if already read */
	  sprintf (tmp,"%s/%s",LOCAL->dir,names[i]->d_name);
	  stat (tmp,&sbuf);	/* get inode poop */
	  if (sbuf.st_atime > sbuf.st_mtime) elt->seen = T;
	}
      }
      fs_give ((void **) &names[i]);
    }
				/* free directory */
    if (names) fs_give ((void **) &names);
  }

				/* if INBOX, snarf from system INBOX  */
  if (LOCAL->inbox && strcmp (sysinbox (),stream->mailbox)) {
    old = stream->uid_last;
    mm_critical (stream);	/* go critical */
    stat (sysinbox (),&sbuf);	/* see if anything there */
				/* can get sysinbox mailbox? */
    if (sbuf.st_size && (sysibx = mail_open (sysibx,sysinbox (),OP_SILENT))
	&& (!sysibx->rdonly) && (r = sysibx->nmsgs)) {
      for (i = 1; i <= r; ++i) {/* for each message in sysinbox mailbox */
				/* build file name we will use */
	sprintf (LOCAL->buf,"%s/%lu",LOCAL->dir,++old);
				/* snarf message from Berkeley mailbox */
	selt = mail_elt (sysibx,i);
	if (((fd = open (LOCAL->buf,O_WRONLY|O_CREAT|O_EXCL,
			 S_IREAD|S_IWRITE)) >= 0) &&
	    (s = mail_fetchheader_full (sysibx,i,NIL,&j,FT_INTERNAL)) &&
	    (write (fd,s,j) == j) &&
	    (s = mail_fetchtext_full (sysibx,i,&j,FT_INTERNAL|FT_PEEK)) &&
	    (write (fd,s,j) == j) && !fsync (fd) && !close (fd)) {
				/* swell the cache */
	  mail_exists (stream,++nmsgs);
	  stream->uid_last =	/* create new elt, note its file number */
	    (elt = mail_elt (stream,nmsgs))->private.uid = old;
	  recent++;		/* bump recent count */
				/* set up initial flags and date */
	  elt->valid = elt->recent = T;
	  elt->seen = selt->seen;
	  elt->deleted = selt->deleted;
	  elt->flagged = selt->flagged;
	  elt->answered = selt->answered;
	  elt->draft = selt->draft;
	  elt->day = selt->day;elt->month = selt->month;elt->year = selt->year;
	  elt->hours = selt->hours;elt->minutes = selt->minutes;
	  elt->seconds = selt->seconds;
	  elt->zhours = selt->zhours; elt->zminutes = selt->zminutes;
	  mh_setdate (LOCAL->buf,elt);
	}

	else {			/* failed to snarf */
	  if (fd) {		/* did it ever get opened? */
	    mm_log ("Message copy to MH mailbox failed",ERROR);
	    close (fd);		/* close descriptor */
	    unlink (LOCAL->buf);/* flush this file */
	  }
	  else {
	    sprintf (tmp,"Can't add message: %s",strerror (errno));
	    mm_log (tmp,ERROR);
	  }
	  stream->silent = silent;
	  return NIL;		/* note that something is badly wrong */
	}
	sprintf (tmp,"%lu",i);	/* delete it from the sysinbox */
	mail_flag (sysibx,tmp,"\\Deleted",ST_SET);
      }
      stat (LOCAL->dir,&sbuf);	/* update scan time */
      LOCAL->scantime = sbuf.st_ctime;      
      mail_expunge (sysibx);	/* now expunge all those messages */
    }
    if (sysibx) mail_close (sysibx);
    mm_nocritical (stream);	/* release critical */
  }
  stream->silent = silent;	/* can pass up events now */
  mail_exists (stream,nmsgs);	/* notify upper level of mailbox size */
  mail_recent (stream,recent);
  return T;			/* return that we are alive */
}

/* MH mail check mailbox
 * Accepts: MAIL stream
 */

void mh_check (MAILSTREAM *stream)
{
  /* Perhaps in the future this will preserve flags */
  if (mh_ping (stream)) mm_log ("Check completed",(long) NIL);
}


/* MH mail expunge mailbox
 * Accepts: MAIL stream
 */

void mh_expunge (MAILSTREAM *stream)
{
  MESSAGECACHE *elt;
  unsigned long j;
  unsigned long i = 1;
  unsigned long n = 0;
  unsigned long recent = stream->recent;
  mm_critical (stream);		/* go critical */
  while (i <= stream->nmsgs) {	/* for each message */
				/* if deleted, need to trash it */
    if ((elt = mail_elt (stream,i))->deleted) {
      sprintf (LOCAL->buf,"%s/%lu",LOCAL->dir,elt->private.uid);
      if (unlink (LOCAL->buf)) {/* try to delete the message */
	sprintf (LOCAL->buf,"Expunge of message %ld failed, aborted: %s",i,
		 strerror (errno));
	mm_log (LOCAL->buf,(long) NIL);
	break;
      }
      mail_gc_msg (&elt->private.msg,GC_ENV | GC_TEXTS);
      if (elt->recent) --recent;/* if recent, note one less recent message */
      mail_expunged (stream,i);	/* notify upper levels */
      n++;			/* count up one more expunged message */
    }
    else i++;			/* otherwise try next message */
  }
  if (n) {			/* output the news if any expunged */
    sprintf (LOCAL->buf,"Expunged %ld messages",n);
    mm_log (LOCAL->buf,(long) NIL);
  }
  else mm_log ("No messages deleted, so no update needed",(long) NIL);
  mm_nocritical (stream);	/* release critical */
				/* notify upper level of new mailbox size */
  mail_exists (stream,stream->nmsgs);
  mail_recent (stream,recent);
}

/* MH mail copy message(s)
 * Accepts: MAIL stream
 *	    sequence
 *	    destination mailbox
 *	    copy options
 * Returns: T if copy successful, else NIL
 */

long mh_copy (MAILSTREAM *stream,char *sequence,char *mailbox,long options)
{
  STRING st;
  MESSAGECACHE *elt;
  struct stat sbuf;
  int fd;
  unsigned long i,j;
  char *t,flags[MAILTMPLEN],date[MAILTMPLEN];
				/* copy the messages */
  if ((options & CP_UID) ? mail_uid_sequence (stream,sequence) :
      mail_sequence (stream,sequence))
    for (i = 1; i <= stream->nmsgs; i++) 
      if ((elt = mail_elt (stream,i))->sequence) {
	sprintf (LOCAL->buf,"%s/%lu",LOCAL->dir,elt->private.uid);
	if ((fd = open (LOCAL->buf,O_RDONLY,NIL)) < 0) return NIL;
	fstat (fd,&sbuf);	/* get size of message */
	if (!elt->day) {	/* make plausible IMAPish date string */
	  struct tm *tm = gmtime (&sbuf.st_mtime);
	  elt->day = tm->tm_mday; elt->month = tm->tm_mon + 1;
	  elt->year = tm->tm_year + 1900 - BASEYEAR;
	  elt->hours = tm->tm_hour; elt->minutes = tm->tm_min;
	  elt->seconds = tm->tm_sec;
	  elt->zhours = 0; elt->zminutes = 0;
	}
				/* is buffer big enough? */
	if (sbuf.st_size > LOCAL->buflen) {
	  fs_give ((void **) &LOCAL->buf);
	  LOCAL->buf = (char *) fs_get ((LOCAL->buflen = sbuf.st_size) + 1);
	}
				/* slurp message */
	read (fd,LOCAL->buf,sbuf.st_size);
				/* tie off file */
	LOCAL->buf[sbuf.st_size] = '\0';
	close (fd);		/* flush message file */
	INIT (&st,mail_string,(void *) LOCAL->buf,sbuf.st_size);
	flags[0] = '\0';	/* init flag string */
	if (elt->seen) strcat (flags," \\Seen");
	if (elt->deleted) strcat (flags," \\Deleted");
	if (elt->flagged) strcat (flags," \\Flagged");
	if (elt->answered) strcat (flags," \\Answered");
	if (elt->draft) strcat (flags," \\Draft");
	flags[0] = '(';		/* open list */
	strcat (flags,")");	/* close list */
	mail_date (date,elt);	/* generate internal date */
	if (!mail_append_full (stream,mailbox,flags,date,&st)) return NIL;
	if (options & CP_MOVE) elt->deleted = T;
      }
  return T;			/* return success */
}

/* MH mail append message from stringstruct
 * Accepts: MAIL stream
 *	    destination mailbox
 *	    stringstruct of messages to append
 * Returns: T if append successful, else NIL
 */

long mh_append (MAILSTREAM *stream,char *mailbox,char *flags,char *date,
		STRING *message)
{
  struct stat sbuf;
  struct direct **names;
  int fd;
  char c,*s,*t,tmp[MAILTMPLEN];
  MESSAGECACHE elt;
  long i,last,nfiles;
  long size = 0;
  long ret = LONGT;
  unsigned long uf;
  short f = mail_parse_flags (stream ? stream : &mhproto,flags,&uf);
  if (date) {			/* want to preserve date? */
				/* yes, parse date into an elt */
    if (!mail_parse_date (&elt,date)) {
      sprintf (tmp,"Bad date in append: %.80s",date);
      mm_log (tmp,ERROR);
      return NIL;
    }
  }
				/* N.B.: can't use LOCAL->buf for tmp */
				/* make sure valid mailbox */
  if (!mh_isvalid (mailbox,tmp,NIL)) switch (errno) {
  case ENOENT:			/* no such file? */
    if ((mailbox[0] == '#') && ((mailbox[1] == 'M') || (mailbox[1] == 'm')) &&
	((mailbox[2] == 'H') || (mailbox[2] == 'h')) &&
	((mailbox[3] == 'I') || (mailbox[3] == 'i')) &&
	((mailbox[4] == 'N') || (mailbox[4] == 'n')) &&
	((mailbox[5] == 'B') || (mailbox[5] == 'b')) &&
	((mailbox[6] == 'O') || (mailbox[6] == 'o')) &&
	((mailbox[7] == 'X') || (mailbox[7] == 'x')) && !mailbox[8])
      mh_create (NIL,"INBOX");
    else {
      mm_notify (stream,"[TRYCREATE] Must create mailbox before append",NIL);
      return NIL;
    }
				/* falls through */
  case 0:			/* merely empty file? */
    break;
  case EINVAL:
    sprintf (tmp,"Invalid MH-format mailbox name: %.80s",mailbox);
    mm_log (tmp,ERROR);
    return NIL;
  default:
    sprintf (tmp,"Not a MH-format mailbox: %.80s",mailbox);
    mm_log (tmp,ERROR);
    return NIL;
  }
  mh_file (tmp,mailbox);	/* build file name we will use */
  if ((nfiles = scandir (tmp,&names,mh_select,mh_numsort)) > 0) {
				/* largest number */
    last = atoi (names[nfiles-1]->d_name);    
    for (i = 0; i < nfiles; ++i) /* free directory */
      fs_give ((void **) &names[i]);
  }
  else last = 0;		/* no messages here yet */
  if (names) fs_give ((void **) &names);

  sprintf (tmp + strlen (tmp),"/%lu",++last);
  if ((fd = open (tmp,O_WRONLY|O_CREAT|O_EXCL,S_IREAD|S_IWRITE)) < 0) {
    sprintf (tmp,"Can't open append mailbox: %s",strerror (errno));
    mm_log (tmp,ERROR);
    return NIL;
  }
  i = SIZE (message);		/* get size of message */
  s = (char *) fs_get (i + 1);	/* get space for the data */
				/* copy the data w/o CR's */
  while (i--) if ((c = SNX (message)) != '\015') s[size++] = c;
  mm_critical (stream);		/* go critical */
				/* write the data */
  if ((write (fd,s,size) < 0) || fsync (fd)) {
    unlink (tmp);		/* delete mailbox */
    sprintf (tmp,"Message append failed: %s",strerror (errno));
    mm_log (tmp,ERROR);
    ret = NIL;
  }
  close (fd);			/* close the file */
				/* set the date for this message */
  if (date) mh_setdate (tmp,&elt);

  mm_nocritical (stream);	/* release critical */
  fs_give ((void **) &s);	/* flush the buffer */
  return ret;
}

/* Internal routines */


/* MH file name selection test
 * Accepts: candidate directory entry
 * Returns: T to use file name, NIL to skip it
 */

int mh_select (struct direct *name)
{
  char c;
  char *s = name->d_name;
  while (c = *s++) if (!isdigit (c)) return NIL;
  return T;
}


/* MH file name comparision
 * Accepts: first candidate directory entry
 *	    second candidate directory entry
 * Returns: negative if d1 < d2, 0 if d1 == d2, postive if d1 > d2
 */

int mh_numsort (const void *d1,const void *d2)
{
  return atoi ((*(struct direct **) d1)->d_name) -
    atoi ((*(struct direct **) d2)->d_name);
}


/* MH mail build file name
 * Accepts: destination string
 *          source
 * Returns: destination
 */

char *mh_file (char *dst,char *name)
{
  char tmp[MAILTMPLEN];
				/* build composite name */
  sprintf (dst,"%s/%s",mh_path,strcmp (ucase (strcpy (tmp,name)),"#MHINBOX") ?
	   name + 4 : "inbox");
  return dst;
}


/* MH canonicalize name
 * Accepts: buffer to write name
 *	    reference
 *	    pattern
 * Returns: T if success, NIL if failure
 */

long mh_canonicalize (char *pattern,char *ref,char *pat)
{
  char tmp[MAILTMPLEN];
  if (ref && *ref) {		/* have a reference */
    strcpy (pattern,ref);	/* copy reference to pattern */
				/* # overrides mailbox field in reference */
    if (*pat == '#') strcpy (pattern,pat);
				/* pattern starts, reference ends, with / */
    else if ((*pat == '/') && (pattern[strlen (pattern) - 1] == '/'))
      strcat (pattern,pat + 1);	/* append, omitting one of the period */
    else strcat (pattern,pat);	/* anything else is just appended */
  }
  else strcpy (pattern,pat);	/* just have basic name */
  return (mh_isvalid (pattern,tmp,T));
}

/* Set date for message
 * Accepts: file name
 *	    elt containing date
 */

void mh_setdate (char *file,MESSAGECACHE *elt)
{
  time_t tp[2];
  tp[0] = time (0);		/* atime is now */
  tp[1] = mail_longdate (elt);	/* modification time */
  utime (file,tp);		/* set the times */
}
