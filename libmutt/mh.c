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
 * This file contains routines specific to MH and ``maildir'' style mailboxes
 */

#include "mutt.h"
#include "mx.h"
#include "mailbox.h"
#include "copy.h"

#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

void mh_parse_message (CONTEXT *ctx,
		       const char *subdir,
		       const char *fname,
		       int *count,
		       int isOld)
{
  char path[_POSIX_PATH_MAX];
  char *p;
  FILE *f;
  HEADER *h;
  struct stat st;

  if (subdir)
    snprintf (path, sizeof (path), "%s/%s/%s", ctx->path, subdir, fname);
  else
    snprintf (path, sizeof (path), "%s/%s", ctx->path, fname);

  if ((f = fopen (path, "r")) != NULL)
  {
    (*count)++;

    if (!ctx->quiet && ReadInc && ((*count % ReadInc) == 0 || *count == 1))
      mutt_message ("Reading %s... %d", ctx->path, *count);

    if (ctx->msgcount == ctx->hdrmax)
      mx_alloc_memory (ctx);

    h = ctx->hdrs[ctx->msgcount] = mutt_new_header ();

    if (subdir)
    {
      snprintf (path, sizeof (path), "%s/%s", subdir, fname);
      h->path = safe_strdup (path);
    }
    else
      h->path = safe_strdup (fname);

    h->env = mutt_read_rfc822_header (f, h);

    fstat (fileno (f), &st);
    fclose (f);

    if (!h->received)
      h->received = h->date_sent;

    if (h->content->length <= 0)
      h->content->length = st.st_size - h->content->offset;

    /* index doesn't have a whole lot of meaning for MH and maildir mailboxes,
     * but this is used to find the current message after a resort in 
     * the `index' event loop.
     */
    h->index = ctx->msgcount;

    if (ctx->magic == M_MAILDIR)
    {
      /* maildir stores its flags in the filename, so ignore the flags in
       * the header of the message
       */

      h->old = isOld;

      if ((p = strchr (h->path, ':')) != NULL && strncmp (p + 1, "2,", 2) == 0)
      {
	p += 3;
	while (*p)
	{
	  switch (*p)
	  {
	    case 'F':

	      h->flagged = 1;
	      break;

	    case 'S': /* seen */

	      h->read = 1;
	      break;

	    case 'R': /* replied */

	      h->replied = 1;
	      break;
	  }
	  p++;
	}
      }
    }

    /* set flags and update context info */
    mx_update_context (ctx);
  }
}

/* Ignore the garbage files.  A valid MH message consists of only
 * digits.  Deleted message get moved to a filename with a comma before
 * it.
 */
int mh_valid_message (const char *s)
{
  for (; *s ; s++)
  {
    if (!isdigit (*s))
      return 0;
  }
  return 1;
}

/* Read a MH/maildir style mailbox.
 *
 * args:
 *	ctx [IN/OUT]	context for this mailbox
 *	subdir [IN]	NULL for MH mailboxes, otherwise the subdir of the
 *			maildir mailbox to read from
 */
int mh_read_dir (CONTEXT *ctx, const char *subdir)
{
  DIR *dirp;
  struct dirent *de;
  char buf[_POSIX_PATH_MAX];
  int isOld = 0;
  int count = 0;
  struct stat st;
  
  if (subdir)
  {
    snprintf (buf, sizeof (buf), "%s/%s", ctx->path, subdir);
    isOld = (strcmp ("cur", subdir) == 0) && option (OPTMARKOLD);
  }
  else
    strfcpy (buf, ctx->path, sizeof (buf));

  if (stat (buf, &st) == -1)
    return (-1);

  if ((dirp = opendir (buf)) == NULL)
    return (-1);

  if (!subdir || (subdir && strcmp (subdir, "new") == 0))
    ctx->mtime = st.st_mtime;

  while ((de = readdir (dirp)) != NULL)
  {
    if (ctx->magic == M_MH)
    {
      if (!mh_valid_message (de->d_name))
	continue;
    }
    else if (*de->d_name == '.')
    {
      /* Skip files that begin with a dot.  This currently isn't documented
       * anywhere, but it was a suggestion from the author of QMail on the
       * mailing list.
       */
      continue;
    }

    mh_parse_message (ctx, subdir, de->d_name, &count, isOld);
  }

  closedir (dirp);
  return 0;
}

/* read a maildir style mailbox */
int maildir_read_dir (CONTEXT *ctx)
{
  /* maildir looks sort of like MH, except that there are two subdirectories
   * of the main folder path from which to read messages
   */
  if (mh_read_dir (ctx, "new") == -1 || mh_read_dir (ctx, "cur") == -1)
    return (-1);

  return 0;
}

/* Open a new (unique) message in a maildir mailbox.  In order to avoid the
 * need for locks, the filename is generated in such a way that it is unique,
 * even over NFS: <time>.<pid>_<count>.<hostname>.  The _<count> part is
 * optional, but required for programs like Mutt which do not change PID for
 * each message that is created in the mailbox (otherwise you could end up
 * creating only a single file per second).
 */
void maildir_create_filename (const char *path, HEADER *hdr, char *msg, char *full)
{
  char subdir[_POSIX_PATH_MAX];
  char suffix[16];
  struct stat sb;

  /* the maildir format stores the status flags in the filename */
  suffix[0] = 0;

  if (hdr && (hdr->flagged || hdr->replied || hdr->read))
  {
    sprintf (suffix, ":2,%s%s%s",
	     hdr->flagged ? "F" : "",
	     hdr->replied ? "R" : "",
	     hdr->read ? "S" : "");
  }

  if (hdr && (hdr->read || hdr->old))
    strfcpy (subdir, "cur", sizeof (subdir));
  else
    strfcpy (subdir, "new", sizeof (subdir));

  FOREVER
  {
    snprintf (msg, _POSIX_PATH_MAX, "%s/%ld.%d_%d.%s%s",
	      subdir, time (NULL), getpid (), Counter++, Hostname, suffix);
    snprintf (full, _POSIX_PATH_MAX, "%s/%s", path, msg);
    if (stat (full, &sb) == -1 && errno == ENOENT) return;
  }
}

static int maildir_sync_message (CONTEXT *ctx, int msgno)
{
  HEADER *h = ctx->hdrs[msgno];
  char newpath[_POSIX_PATH_MAX];
  char fullpath[_POSIX_PATH_MAX];
  char oldpath[_POSIX_PATH_MAX];
  char *p;

  /* decide which subdir this message belongs in */
  strfcpy (newpath, (h->read || h->old) ? "cur" : "new", sizeof (newpath));
  strcat (newpath, "/");

  if ((p = strchr (h->path, '/')) == NULL)
  {
    dprint (1, (debugfile, "maildir_sync_message: %s: unable to find subdir!\n",
		h->path));
    return (-1);
  }
  p++;
  strcat (newpath, p);

  /* kill the previous flags */
  if ((p = strchr (newpath, ':')) != NULL) *p = 0;

  if (h->replied || h->read || h->flagged)
  {
    strcat (newpath, ":2,");
    if (h->flagged) strcat (newpath, "F");
    if (h->replied) strcat (newpath, "R");
    if (h->read) strcat (newpath, "S");
  }

  snprintf (fullpath, sizeof (fullpath), "%s/%s", ctx->path, newpath);
  snprintf (oldpath, sizeof (oldpath), "%s/%s", ctx->path, h->path);

  if (strcmp (fullpath, oldpath) == 0)
  {
    /* message hasn't really changed */
    return 0;
  }

  if (rename (oldpath, fullpath) != 0)
  {
    mutt_perror ("rename");
    return (-1);
  }
  safe_free ((void **)&h->path);
  h->path = safe_strdup (newpath);
  return (0);
}

/* save changes to a message to disk */
static int mh_sync_message (CONTEXT *ctx, int msgno)
{
  HEADER *h = ctx->hdrs[msgno];
  FILE *f;
  FILE *d;
  MESSAGE *msg;
  int rc = -1;
  char oldpath[_POSIX_PATH_MAX];
  char newpath[_POSIX_PATH_MAX];
  long loc = 0;
  int chflags = CH_UPDATE;
  
  snprintf (oldpath, sizeof (oldpath), "%s/%s", ctx->path, h->path);
  
  mutt_mktemp (newpath);
  if ((f = safe_fopen (newpath, "w")) == NULL)
  {
    dprint (1, (debugfile, "mh_sync_message: %s: %s (errno %d).\n",
		newpath, strerror (errno), errno));
    return (-1);
  }

  if ((msg = mx_open_message (ctx, msgno)) != NULL)
  {
    mutt_copy_header (msg->fp, h, f, chflags, NULL);
    loc = ftell (msg->fp);
    rc = mutt_copy_bytes (msg->fp, f, h->content->length);
    mx_close_message (&msg);
  }

  if (rc == 0)
  {
    /* replace the original version of the message with the new one */
    if ((f = freopen (newpath, "r", f)) != NULL)
    {
      unlink (newpath);
      if ((d = mx_open_file_lock (oldpath, "w")) != NULL)
      {
	mutt_copy_stream (f, d);
	mx_unlock_file (oldpath, fileno (d));
	fclose (d);
      }
      else
      {
	fclose (f);
	return (-1);
      }
      fclose (f);
    }
    else
    {
      mutt_perror (newpath);
      return (-1);
    }

    /*
     * if the status of this message changed, the offsets for the body parts
     * will be wrong, so free up the memory.  This is ok since it will get
     * parsed again the next time the user tries to view it.
     */
    h->content->offset = loc;
    mutt_free_body (&h->content->parts);
  }
  else
  {
    fclose (f);
    unlink (newpath);
  }

  return (rc);
}

int mh_sync_mailbox (CONTEXT *ctx)
{
  char path[_POSIX_PATH_MAX], tmp[_POSIX_PATH_MAX];
  int i, rc = 0;
  
  for (i = 0; i < ctx->msgcount; i++)
  {
    if (ctx->hdrs[i]->deleted)
    {
      snprintf (path, sizeof (path), "%s/%s", ctx->path, ctx->hdrs[i]->path);
      if (ctx->magic == M_MAILDIR)
	unlink (path);
      else
      {
	/* MH just moves files out of the way when you delete them */
	snprintf (tmp, sizeof (tmp), "%s/,%s", ctx->path, ctx->hdrs[i]->path);
	unlink (tmp);
	rename (path, tmp);
      }
    }
    else if (ctx->hdrs[i]->changed)
    {
      if (ctx->magic == M_MAILDIR)
	maildir_sync_message (ctx, i);
      else
      {
	/* FOO - seems ok to ignore errors, but might want to warn... */
	mh_sync_message (ctx, i);
      }
    }
  }

  return (rc);
}

/* check for new mail */
int mh_check_mailbox (CONTEXT *ctx, int *index_hint)
{
  DIR *dirp;
  struct dirent *de;
  char buf[_POSIX_PATH_MAX];
  struct stat st;
  LIST *lst = NULL, *tmp = NULL, *prev;
  int i, lng = 0;
  int count = 0;

  /* MH users might not like the behavior of this function because it could
   * take awhile if there are many messages in the mailbox.
   */
  if (!option (OPTCHECKNEW))
    return (0); /* disabled */

  if (ctx->magic == M_MH)
    strfcpy (buf, ctx->path, sizeof (buf));
  else
    snprintf (buf, sizeof (buf), "%s/new", ctx->path);

  if (stat (buf, &st) == -1)
    return (-1);

  if (st.st_mtime == ctx->mtime)
    return (0); /* unchanged */

  if ((dirp = opendir (buf)) == NULL)
    return (-1);

  while ((de = readdir (dirp)) != NULL)
  {
    if (ctx->magic == M_MH)
    {
      if (!mh_valid_message (de->d_name))
	continue;
    }
    else /* maildir */
    {
      if (*de->d_name == '.')
	continue;
    }

    if (tmp)
    {
      tmp->next = mutt_new_list ();
      tmp = tmp->next;
    }
    else
      lst = tmp = mutt_new_list ();
    tmp->data = safe_strdup (de->d_name);
  }
  closedir (dirp);

  ctx->mtime = st.st_mtime; /* save the time we scanned at */

  if (!lst)
    return 0;

  /* if maildir, skip the leading "new/" */
  lng = (ctx->magic == M_MAILDIR) ? 4 : 0;

  for (i = 0; i < ctx->msgcount; i++)
  {
    if (ctx->magic == M_MAILDIR && ctx->hdrs[i]->old)
      continue; /* only look at NEW messages */

    prev = NULL;
    tmp = lst;
    while (tmp)
    {
      if (strcmp (tmp->data, ctx->hdrs[i]->path + lng) == 0)
      {
	if (prev)
	  prev->next = tmp->next;
	else
	  lst = lst->next;
	safe_free ((void **) &tmp->data);
	safe_free ((void **) &tmp);
	break;
      }
      else
      {
	prev = tmp;
	tmp = tmp->next;
      }
    }
  }

  for (tmp = lst; tmp; tmp = lst)
  {
    mh_parse_message (ctx, (ctx->magic == M_MH ? NULL : "new"), tmp->data, &count, 0);

    lst = tmp->next;
    safe_free ((void **) &tmp->data);
    safe_free ((void **) &tmp);
  }

  return (1);
}
