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

#include "mutt.h"
#include "buffy.h"
#include "mailbox.h"
#include "mx.h"

#ifdef USE_IMAP
#include "imap.h"
#endif

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <utime.h>
#include <ctype.h>
#include <unistd.h>

#include <stdio.h>

static time_t BuffyTime = 0;	/* last time we started checking for mail */
time_t BuffyDoneTime = 0;	/* last time we knew for sure how much mail there was. */
static short BuffyCount = 0;	/* how many boxes with new mail */
static short BuffyNotify = 0;	/* # of unnotified new boxes */

#ifdef BUFFY_SIZE

/* Find the last message in the file. * upon success return 0. * If no
 * message found - return -1 */
int fseek_last_message (FILE * f)
{
  long int pos;
  char buffer[BUFSIZ + 9];	/* 7 for "\n\nFrom " */
  int bytes_read;
  int i;			/* Index into `buffer' for scanning.  */

  memset (buffer, 0, sizeof(buffer));
  fseek (f, 0, SEEK_END);
  pos = ftell (f);

  /* Set `bytes_read' to the size of the last, probably partial, buffer; 0 <
   * `bytes_read' <= `BUFSIZ'.  */
  bytes_read = pos % BUFSIZ;
  if (bytes_read == 0)
    bytes_read = BUFSIZ;
  /* Make `pos' a multiple of `BUFSIZ' (0 if the file is short), so that all
   * reads will be on block boundaries, which might increase efficiency.  */
  while ((pos -= bytes_read) >= 0)
  {
    /* we save in the buffer at the end the first 7 chars from the last read */
    strncpy (buffer + BUFSIZ, buffer, 5+2); /* 2 == 2 * mutt_strlen(CRLF) */
    fseek (f, pos, SEEK_SET);
    bytes_read = fread (buffer, sizeof (char), bytes_read, f);
    if (bytes_read == -1)
      return -1;
    for (i = bytes_read; --i >= 0;)
      if (!mutt_strncmp (buffer + i, "\n\nFrom ", mutt_strlen ("\n\nFrom ")))
      {				/* found it - go to the beginning of the From */
	fseek (f, pos + i + 2, SEEK_SET);
	return 0;
      }
    bytes_read = BUFSIZ;
  }

  /* here we are at the beginning of the file */
  if (!mutt_strncmp ("From ", buffer, 5))
  {
    fseek (f, 0, 0);
    return (0);
  }

  return (-1);
}

/* Return 1 if the last message is new */
int test_last_status_new (FILE * f)
{
  HEADER *hdr;
  int result = 0;

  if (fseek_last_message (f) == -1)
    return (0);

  hdr = mutt_new_header ();
  mutt_read_rfc822_header (f, hdr, 0);
  if (!(hdr->read || hdr->old))
    result = 1;
  mutt_free_header (&hdr);

  return result;
}

int test_new_folder (const char *path)
{
  FILE *f;
  int rc = 0;
  int typ;

  typ = mx_get_magic (path);

  if (typ != M_MBOX && typ != M_MMDF && typ != M_KENDRA)
    return 0;

  f = fopen (path, "rb");
  rc = test_last_status_new (f);
  fclose (f);

  return rc;
}

BUFFY *mutt_find_mailbox (const char *path)
{
  BUFFY *tmp = NULL;
  struct stat sb;
  struct stat tmp_sb;
  
  if (stat (path,&sb) != 0)
    return NULL;

  for (tmp = Incoming; tmp; tmp = tmp->next)
  {
    if (stat (tmp->path,&tmp_sb) ==0 && 
	sb.st_dev == tmp_sb.st_dev && sb.st_ino == tmp_sb.st_ino)
      break;
  }
  return tmp;
}

void mutt_update_mailbox (BUFFY * b)
{
  struct stat sb;

  if (!b)
    return;

  if (stat (b->path, &sb) == 0)
    b->size = (long) sb.st_size;
  else
    b->size = 0;
  return;
}
#endif

/* buffy_add_mailbox:
   adds given mailbox to the mailboxes to be checked periodically,
   user and passwd are relevant only for IMAP mailboxes.
   IMAP path is of standard form '{server:port}path'
*/
BUFFY *
buffy_add_mailbox(const char *path, const char *user, const char *passwd)
{
    BUFFY **tmp;
    /* simple check to avoid duplicates */
    for (tmp = &Incoming; *tmp; tmp = &((*tmp)->next))
    {
      if (mutt_strcmp (path, (*tmp)->path) == 0)
	break;
    }

    if (!*tmp)
    {
      *tmp = (BUFFY *) safe_calloc (1, sizeof (BUFFY));
      (*tmp)->path   = safe_strdup (path);
      (*tmp)->user   = user   ? safe_strdup (user)   : NULL;
      (*tmp)->passwd = passwd ? safe_strdup (passwd) : NULL;
      (*tmp)->next = NULL;
      /* it is tempting to set magic right here */
      (*tmp)->magic = 0;
      
    }

    (*tmp)->new = 0;
    (*tmp)->notified = 1;
    (*tmp)->newly_created = 0;

    return *tmp;
}

static BUFFY*
buffy_mailbox_remove(BUFFY * bf)
{
    BUFFY ** tmp = &Incoming;

    if(!*tmp) return NULL; /* strange error */
    if(*tmp == bf)
	*tmp = (*tmp)->next;
    else {
	while(*tmp && (*tmp)->next != bf) 
	    tmp = &(*tmp)->next;
	if( !*tmp ) return NULL; /* not found again, critical error! */
	(*tmp)->next = bf->next;
    }

    fprintf(stderr,"Removing %s from buffy list\n", bf->path);
    safe_free((void **) &bf->user);
    safe_free((void **) &bf->passwd);
    safe_free((void **) &bf->path);
    safe_free((void **) &bf);
    return (*tmp)->next;
}

int mutt_parse_mailboxes (BUFFER *path, BUFFER *s, unsigned long data, BUFFER *err)
{
  BUFFY *tmp;
  char buf[_POSIX_PATH_MAX];
#ifdef BUFFY_SIZE
  struct stat sb;
#endif /* BUFFY_SIZE */

  while (MoreArgs (s))
  {
    mutt_extract_token (path, s, 0);
    strfcpy (buf, path->data, sizeof (buf));
    mutt_expand_path (buf, sizeof (buf));

    /* Skip empty tokens. */
    if(!*buf) continue;

    tmp = buffy_add_mailbox(buf, NULL, NULL);

#ifdef BUFFY_SIZE
    /* for buffy_size, it is important that if the folder is new (tested by
     * reading it), the size is set to 0 so that later when we check we see
     * that it increased .  without buffy_size we probably don't care.
     */
    if (stat (tmp->path, &sb) == 0 && !test_new_folder (tmp->path))
    {
      /* some systems out there don't have an off_t type */
      tmp->size = (long) sb.st_size;
    }
    else
      tmp->size = 0;
#endif /* BUFFY_SIZE */
  }
  return 0;
}

#ifdef BUFFY_SIZE
/* people use buffy_size on systems where modified time attributes are BADLY
 * broken. Ignore them.
 */
#define STAT_CHECK (sb.st_size > tmp->size)
#else
#define STAT_CHECK (sb.st_mtime > sb.st_atime || (tmp->newly_created && sb.st_ctime == sb.st_mtime && sb.st_ctime == sb.st_atime))
#endif /* BUFFY_SIZE */

#ifdef LIBMUTT
int mutt_buffy_check (int force, int imap_check_test(const char*))
#else
int mutt_buffy_check (int force)
#endif
{
  BUFFY *tmp;
  struct stat sb;
  struct dirent *de;
  DIR *dirp;
  char path[_POSIX_PATH_MAX];
  struct stat contex_sb;
  time_t t;
#ifdef USE_IMAP
  static time_t last_imap_check = 0;
  int do_imap_check = 1;

  if (ImapCheckTimeout)
  {
    time_t now = time (NULL);
    if (!force && (now - last_imap_check < ImapCheckTimeout))
      do_imap_check = 0;
    else
      last_imap_check = now;
  }
#endif

  /* fastest return if there are no mailboxes */
  if (!Incoming)
    return 0;
  t = time (NULL);
  if (!force && t - BuffyTime < BuffyTimeout)
    return BuffyCount;
 
  BuffyTime = t;
  BuffyCount = 0;
  BuffyNotify = 0;

#ifdef USE_IMAP
  if (!Context || Context->magic != M_IMAP)
#endif
  /* check device ID and serial number instead of comparing paths */
  if (!Context || !Context->path || stat (Context->path, &contex_sb) != 0)
  {
    contex_sb.st_dev=0;
    contex_sb.st_ino=0;
  }
  
  for (tmp = Incoming; tmp; tmp = tmp->next)
  {
#ifdef USE_IMAP
    if (mx_is_imap (tmp->path))
      tmp->magic = M_IMAP;
    else
#endif
    {
      tmp->new = 0;

      if (stat (tmp->path, &sb) != 0 ||
        (!tmp->magic && (tmp->magic = mx_get_magic (tmp->path)) <= 0))
      {
        /* if the mailbox still doesn't exist, set the newly created flag to
         * be ready for when it does.
         */
        tmp->newly_created = 1;
        tmp->magic = 0;
#ifdef BUFFY_SIZE
        tmp->size = 0;
#endif
        continue;
      }
    }

    if (!Context || !Context->path || 
#ifdef USE_IMAP
        /* Poll current IMAP folder like any other */
        tmp->magic == M_IMAP ||
#endif
	sb.st_dev != contex_sb.st_dev || sb.st_ino != contex_sb.st_ino)
    {
      switch (tmp->magic)
      {
      case M_MBOX:
      case M_MMDF:
      case M_KENDRA:

	if (STAT_CHECK)
	{
	  BuffyCount++;
	  tmp->new = 1;
	}
#ifdef BUFFY_SIZE
	else
	{
	  /* some other program has deleted mail from the folder */
	  tmp->size = (long) sb.st_size;
	}
#endif
	if (tmp->newly_created &&
	    (sb.st_ctime != sb.st_mtime || sb.st_ctime != sb.st_atime))
	  tmp->newly_created = 0;

	break;

      case M_MAILDIR:
	snprintf (path, sizeof (path), "%s/new", tmp->path);
	if ((dirp = opendir (path)) == NULL)
	{
	  tmp->magic = 0;
	  break;
	}
	while ((de = readdir (dirp)) != NULL)
	{
	  char *p;
	  if (*de->d_name != '.' && 
	      (!(p = strstr (de->d_name, ":2,")) || !strchr (p + 3, 'T')))
	  {
	    /* one new message is enough */
	    BuffyCount++;
	    tmp->new = 1;
	    break;
	  }
	}
	closedir (dirp);
	break;
	
      case M_MH:
	if ((tmp->new = mh_buffy (tmp->path)) > 0)
	  BuffyCount++;
	break;

#ifdef USE_IMAP
      case M_IMAP:
        /* poll on do_imap_check, else return cached value */
        if (do_imap_check)
        {
	  char * old_user = ImapUser, *old_passwd = ImapPass;
	  int res;
          tmp->new = 0;
	  
	  ImapUser = tmp->user;
	  ImapPass = tmp->passwd;
#ifdef LIBMUTT
          if ( (res=imap_mailbox_check (tmp->path, 1, imap_check_test))>0 )
#else
          if ( (res=imap_mailbox_check (tmp->path, 1))>0 )
#endif
          {
            BuffyCount++;
            tmp->new = 1;
          } else if(res<0)
	      /* failed (authorization?), remove it from list not
		 to try the same wrong password over and over again */
	      tmp = buffy_mailbox_remove(tmp);
	  ImapUser = old_user, ImapPass = old_passwd;
        }
        else
          if (tmp->new)
            BuffyCount++;

	break;
#endif
      }
    }
#ifdef BUFFY_SIZE
    else if (Context && Context->path)
      tmp->size = (long) sb.st_size;	/* update the size */
#endif
    /* tmp could be set to NULL by buffy_mailbox_remove */
    if(!tmp) break;

    if (!tmp->new)
      tmp->notified = 0;
    else if (!tmp->notified)
      BuffyNotify++;
  }
  BuffyDoneTime = BuffyTime;
  return (BuffyCount);
}

#ifndef LIBMUTT
int mutt_buffy_notify (void)
{
  BUFFY *tmp;
  char path[_POSIX_PATH_MAX];

  if (mutt_buffy_check (0) && BuffyNotify)
  {
    for (tmp = Incoming; tmp; tmp = tmp->next)
    {
      if (tmp->new && !tmp->notified)
      {
	strfcpy (path, tmp->path, sizeof (path));
	mutt_pretty_mailbox (path);
	mutt_message ("New mail in %s.", path);
	tmp->notified = 1;
	BuffyNotify--;
	return (1);
      }
    }
    /* there were no mailboxes needing to be notified, so clean up since 
     * BuffyNotify has somehow gottten out of sync
     */
    BuffyNotify = 0;
  }
  return (0);
}

/* 
 * mutt_buffy() -- incoming folders completion routine
 *
 * given a folder name, this routine gives the next incoming folder with new
 * new mail.
 */
void mutt_buffy (char *s)
{
  int count;
  BUFFY *tmp = Incoming;

  mutt_expand_path (s, _POSIX_PATH_MAX);
  switch (mutt_buffy_check (0))
  {
  case 0:

    s = '\0';
    break;

  case 1:

    while (tmp && !tmp->new)
      tmp = tmp->next;
    if (!tmp)
    {
      s = '\0';
      mutt_buffy_check (1); /* buffy was wrong - resync things */
      break;
    }
    strcpy (s, tmp->path);
    mutt_pretty_mailbox (s);
    break;

  default:
    
    count = 0;
    while (count < 3)
    {
      if (mutt_strcmp (s, tmp->path) == 0)
	count++;
      else if (count && tmp->new)
	break;
      tmp = tmp->next;
      if (!tmp)
      {
	tmp = Incoming;
	count++;
      }
    }
    if (count >= 3)
    {
      s = '\0';
      mutt_buffy_check (1); /* buffy was wrong - resync things */
      break;
    }
    strcpy (s, tmp->path);
    mutt_pretty_mailbox (s);
    break;
  }
}
#endif
