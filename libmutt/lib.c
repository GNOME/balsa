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
#include "mutt_curses.h"
#include "mime.h"

#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <sys/stat.h>
#include <fcntl.h>

BODY *mutt_new_body (void)
{
  BODY *p = (BODY *) safe_calloc (1, sizeof (BODY));
    
  p->disposition = DISPATTACH;
  p->use_disp = 1;
  return (p);
}

BODY* mutt_dup_body (BODY *b)
{
  BODY *bn;

  bn = mutt_new_body();
  memcpy(bn, b, sizeof (BODY));
  return bn;
}

void mutt_free_body (BODY **p)
{
  BODY *a = *p, *b;

  while (a)
  {
    b = a;
    a = a->next; 

    if (b->parameter)
      mutt_free_parameter (&b->parameter);
    if (b->unlink && b->filename)
      unlink (b->filename);
    safe_free ((void **) &b->filename);
    safe_free ((void **) &b->content);
    safe_free ((void **) &b->subtype);
    safe_free ((void **) &b->description);
    safe_free ((void **) &b->form_name);

    if (b->hdr)
    {
      /* Don't free twice (b->hdr->content = b->parts) */
      b->hdr->content = NULL;
      mutt_free_header(&b->hdr);
    }

    if (b->parts)
      mutt_free_body (&b->parts);

    safe_free ((void **) &b);
  }

  *p = 0;
}

void mutt_free_parameter (PARAMETER **p)
{
  PARAMETER *t = *p;
  PARAMETER *o;

  while (t)
  {
    safe_free ((void **) &t->attribute);
    safe_free ((void **) &t->value);
    o = t;
    t = t->next;
    safe_free ((void **) &o);
  }
  *p = 0;
}

LIST *mutt_add_list (LIST *head, const char *data)
{
  LIST *tmp;

  for (tmp = head; tmp && tmp->next; tmp = tmp->next)
    ;
  if (tmp)
  {
    tmp->next = safe_malloc (sizeof (LIST));
    tmp = tmp->next;
  }
  else
    head = tmp = safe_malloc (sizeof (LIST));

  tmp->data = safe_strdup (data);
  tmp->next = NULL;
  return head;
}

void mutt_free_list (LIST **list)
{
  LIST *p;
  
  if (!list) return;
  while (*list)
  {
    p = *list;
    *list = (*list)->next;
    safe_free ((void **) &p->data);
    safe_free ((void **) &p);
  }
}

HEADER *mutt_dup_header(HEADER *h)
{
  HEADER *hnew;

  hnew = mutt_new_header();
  memcpy(hnew, h, sizeof (HEADER));
  return hnew;
}

void mutt_free_header (HEADER **h)
{
  mutt_free_envelope (&(*h)->env);
  mutt_free_body (&(*h)->content);
  safe_free ((void **) &(*h)->tree);
  safe_free ((void **) &(*h)->path);
  safe_free ((void **) h);
}

/* returns true if the header contained in "s" is in list "t" */
int mutt_matches_ignore (const char *s, LIST *t)
{
  for (; t; t = t->next)
  {
    if (!strncasecmp (s, t->data, strlen (t->data)) || *t->data == '*')
      return 1;
  }
  return 0;
}

/* prepend the path part of *path to *link */
void mutt_expand_link (char *newpath, const char *path, const char *link)
{
  const char *lb = NULL;
  size_t len;

  /* link is full path */
  if (*link == '/')
  {
    strfcpy (newpath, link, _POSIX_PATH_MAX);
    return;
  }

  if ((lb = strrchr (path, '/')) == NULL)
  {
    /* no path in link */
    strfcpy (newpath, link, _POSIX_PATH_MAX);
    return;
  }

  len = lb - path + 1;
  memcpy (newpath, path, len);
  strfcpy (newpath + len, link, _POSIX_PATH_MAX - len);
}

char *mutt_expand_path (char *s, size_t slen)
{
  char p[_POSIX_PATH_MAX] = "";
  char *q = NULL;

  if (*s == '~')
  {
    if (*(s + 1) == '/' || *(s + 1) == 0)
      snprintf (p, sizeof (p), "%s%s", Homedir, s + 1);
    else
    {
      struct passwd *pw;

      q = strchr (s + 1, '/');
      if (q)
	*q = 0;
      if ((pw = getpwnam (s + 1)))
	snprintf (p, sizeof (p), "%s/%s", pw->pw_dir, q ? q + 1 : "");
      else
      {
	/* user not found! */
	if (q)
	  *q = '/';
	return (NULL);
      }
    }
  }
  else if (*s == '=' || *s == '+')
    snprintf (p, sizeof (p), "%s/%s", Maildir, s + 1);
  else
  {
    if (*s == '>')
      q = Inbox;
    else if (*s == '<')
      q = Outbox;
    else if (*s == '!')
      q = Spoolfile;
    else if (*s == '-')
      q = LastFolder;
    else
      return s;

    if (!*q)
      return s;
    snprintf (p, sizeof (p), "%s%s", q, s + 1);
  }
  if (*p)
    strfcpy (s, p, slen); /* replace the string with the expanded version. */
  return (s);
}

void *safe_calloc (size_t nmemb, size_t size)
{
  void *p;

  if (!nmemb || !size)
    return NULL;
  if (!(p = calloc (nmemb, size)))
  {
    mutt_error ("Out of memory");
    sleep (1);
    mutt_exit (1);
  }
  return p;
}

void *safe_malloc (unsigned int siz)
{
  void *p;

  if (siz == 0)
    return 0;
  if ((p = (void *) malloc (siz)) == 0)
  {
    mutt_error ("Out of memory!");
    sleep (1);
    mutt_exit (1);
  }
  return (p);
}

void safe_realloc (void **p, size_t siz)
{
  void *r;

  if (siz == 0)
  {
    if (*p)
    {
      free (*p);
      *p = NULL;
    }
    return;
  }

  if (*p)
    r = (void *) realloc (*p, siz);
  else
  {
    /* realloc(NULL, nbytes) doesn't seem to work under SunOS 4.1.x */
    r = (void *) malloc (siz);
  }

  if (!r)
  {
    mutt_error ("Out of memory!");
    sleep (1);
    mutt_exit (1);
  }

  *p = r;
}

void safe_free (void **p)
{
  if (*p)
  {
    free (*p);
    *p = 0;
  }
}

char *safe_strdup (const char *s)
{
  char *p;
  size_t l;

  if (!s || !*s) return 0;
  l = strlen (s) + 1;
  p = (char *)safe_malloc (l);
  memcpy (p, s, l);
  return (p);
}

char *mutt_skip_whitespace (char *p)
{
  SKIPWS (p);
  return p;
}

int mutt_copy_bytes (FILE *in, FILE *out, size_t size)
{
  char buf[2048];
  size_t chunk;

  while (size > 0)
  {
    chunk = (size > sizeof (buf)) ? sizeof (buf) : size;
    if ((chunk = fread (buf, 1, chunk, in)) < 1)
      break;
    if (fwrite (buf, 1, chunk, out) != chunk)
    {
      dprint (1, (debugfile, "mutt_copy_bytes(): fwrite() returned short byte count\n"));
      return (-1);
    }
    size -= chunk;
  }

  return 0;
}

char *mutt_get_parameter (const char *s, PARAMETER *p)
{
  for (; p; p = p->next)
    if (strcasecmp (s, p->attribute) == 0)
      return (p->value);

  return NULL;
}

/* returns 1 if Mutt can't display this type of data, 0 otherwise */
int mutt_needs_mailcap (BODY *m)
{
  switch (m->type)
  {
    case TYPETEXT:

      if (!strcasecmp ("plain", m->subtype) ||
	  !strcasecmp ("rfc822-headers", m->subtype) ||
	  !strcasecmp ("enriched", m->subtype))
	return 0;
      break;


    case TYPEMULTIPART:
    case TYPEMESSAGE:

      return 0;
  }

  return 1;
}

int mutt_is_text_type (int t, char *s)
{
  if (t == TYPETEXT)
    return 1;

  if (t == TYPEMESSAGE)
  {
    if (!strcasecmp ("delivery-status", s))
      return 1;
  }


  return 0;
}

void mutt_free_envelope (ENVELOPE **p)
{
  if (!*p) return;
  rfc822_free_address (&(*p)->return_path);
  rfc822_free_address (&(*p)->to);
  rfc822_free_address (&(*p)->cc);
  rfc822_free_address (&(*p)->bcc);
  rfc822_free_address (&(*p)->sender);
  rfc822_free_address (&(*p)->from);
  rfc822_free_address (&(*p)->reply_to);
  rfc822_free_address (&(*p)->mail_followup_to);
  safe_free ((void **) &(*p)->subject);
  safe_free ((void **) &(*p)->message_id);
  mutt_free_list (&(*p)->references);
  mutt_free_list (&(*p)->userhdrs);
  safe_free ((void **) p);
}

void mutt_tabs_to_spaces (char *s)
{
  while (*s)
  {
    if (ISSPACE (*s))
      *s = ' ';
    s++;
  }
}

void mutt_mktemp (char *s)
{
  snprintf (s, _POSIX_PATH_MAX, "%s/mutt-%s-%d-%d", Tempdir, Hostname, (int) getpid (), Counter++);
  unlink (s);
}

/* convert all characters in the string to lowercase */
char *mutt_strlower (char *s)
{
  char *p = s;

  while (*p)
  {
    *p = tolower (*p);
    p++;
  }

  return (s);
}

/* strcmp() allowing NULL pointers */
int mutt_strcmp (const char *s1, const char *s2)
{
  if (s1 != NULL)
  {
    if (s2 != NULL)
      return strcmp (s1, s2);
    else
      return (1);
  }
  else
    return ((s2 == NULL) ? 0 : -1);
}

void mutt_free_alias (ALIAS **p)
{
  ALIAS *t;

  while (*p)
  {
    t = *p;
    *p = (*p)->next;
    safe_free ((void **) &t->name);
    rfc822_free_address (&t->addr);
    free (t);
  }
}

/* collapse the pathname using ~ or = when possible */
void mutt_pretty_mailbox (char *s)
{
  char *p = s, *q = s;
  size_t len;

  /* first attempt to collapse the pathname */
  while (*p)
  {
    if (*p == '/' && p[1] == '/')
    {
      *q++ = '/';
      p += 2;
    }
    else if (p[0] == '/' && p[1] == '.' && p[2] == '/')
    {
      *q++ = '/';
      p += 3;
    }
    else
      *q++ = *p++;
  }
  *q = 0;

  if (strncmp (s, Maildir, (len = strlen (Maildir))) == 0 && s[len] == '/')
  {
    *s++ = '=';
    strcpy (s, s + len);
  }
  else if (strncmp (s, Homedir, (len = strlen (Homedir))) == 0 &&
	   s[len] == '/')
  {
    *s++ = '~';
    strcpy (s, s + len - 1);
  }
}

void mutt_unlink (const char *s)
{
  FILE *f;
  struct stat sb;
  char buf[2048];
  
  if (stat (s, &sb) == 0)
  {
    if ((f = fopen (s, "r+")))
    {
      unlink (s);
      memset (buf, 0, sizeof (buf));
      while (sb.st_size > 0)
      {
	fwrite (buf, 1, sizeof (buf), f);
	sb.st_size -= sizeof (buf);
      }
      fclose (f);
    }
  }
}

int mutt_copy_stream (FILE *fin, FILE *fout)
{
  size_t l;
  char buf[LONG_STRING];

  while ((l = fread (buf, 1, sizeof (buf), fin)) > 0)
  {
    if (fwrite (buf, 1, l, fout) != l)
      return (-1);
  }

  return 0;
}

void mutt_expand_fmt (char *dest, size_t destlen, const char *fmt, const char *src)
{
  const char *p = fmt;
  const char *last = p;
  size_t len;
  size_t slen = strlen (src);
  int found = 0;

  while ((p = strchr (p, '%')) != NULL)
  {
    if (p[1] == 's')
    {
      found++;

      len = (size_t) (p - last);
      if (len)
      {
	if (len > destlen - 1)
	  len = destlen - 1;

	memcpy (dest, last, len);
	dest += len;
	destlen -= len;

	if (destlen <= 0)
	{
	  *dest = 0;
	  break; /* no more space */
	}
      }

      strfcpy (dest, src, destlen);
      if (slen > destlen)
      {
	/* no more room */
	break;
      }
      dest += slen;
      destlen -= slen;

      p += 2;
      last = p;
    }
    else if (p[1] == '%')
      p++;

    p++;
  }

  if (found)
    strfcpy (dest, last, destlen);
  else
    snprintf (dest, destlen, "%s '%s'", fmt, src);
}

/* when opening files for writing, make sure the file doesn't already exist
 * to avoid race conditions.
 */
FILE *safe_fopen (const char *path, const char *mode)
{
  struct stat osb, nsb;

  if (mode[0] == 'w')
  {
    int fd;
    int flags = O_CREAT | O_EXCL;

    if (mode[1] == '+')
      flags |= O_RDWR;
    else
      flags |= O_WRONLY;

    if ((fd = open (path, flags, 0600)) < 0)
      return NULL;

    /* make sure the file is not symlink */
    if (lstat (path, &osb) < 0 || fstat (fd, &nsb) < 0 ||
	osb.st_dev != nsb.st_dev || osb.st_ino != nsb.st_ino ||
	osb.st_rdev != nsb.st_rdev)
    {
      dprint (1, (debugfile, "safe_fopen():%s is a symlink!\n", path));
      close (fd);
      return (NULL);
    }

    return (fdopen (fd, mode));
  }
  else
    return (fopen (path, mode));
}

/* return 0 on success, -1 on error */
int mutt_check_overwrite (const char *attname, const char *path,
				char *fname, size_t flen, int flags) 
{
  char tmp[_POSIX_PATH_MAX];
  struct stat st;

  strfcpy (fname, path, flen);
  if (access (fname, F_OK) != 0)
    return 0;
  if (stat (fname, &st) != 0)
    return -1;
  if (S_ISDIR (st.st_mode))
  {
    if (mutt_yesorno ("File is a directory, save under it?", 1) != M_YES) 
      return (-1);
    if (!attname || !attname[0])
    {
      tmp[0] = 0;
      if (mutt_get_field ("File under directory: ", tmp, sizeof (tmp),
				      M_FILE | M_CLEAR) != 0 || !tmp[0])
	return (-1);
      snprintf (fname, flen, "%s/%s", path, tmp);
    }
    else
      snprintf (fname, flen, "%s/%s", path, attname);
  }

  if (flags != M_SAVE_APPEND &&
      access (fname, F_OK) == 0 && 
      mutt_yesorno ("File exists, overwrite?", 0) != 1)
    return (-1);
  
  return 0;
}

void mutt_remove_trailing_ws (char *s)
{
  char *p;

  for (p = s + strlen (s) - 1 ; p >= s && ISSPACE (*p) ; p--)
    *p = 0;
}

void mutt_pretty_size (char *s, size_t len, long n)
{
  if (n == 0)
    strfcpy (s, "0K", len);
  else if (n < 103)
    strfcpy (s, "0.1K", len);
  else if (n < 10189) /* 0.1K - 9.9K */
    snprintf (s, len, "%3.1fK", n / 1024.0);
  else if (n < 1023949) /* 10K - 999K */
  {
    /* 51 is magic which causes 10189/10240 to be rounded up to 10 */
    snprintf (s, len, "%ldK", (n + 51) / 1024);
  }
  else if (n < 10433332) /* 1.0M - 9.9M */
    snprintf (s, len, "%3.1fM", n / 1048576.0);
  else /* 10M+ */
  {
    /* (10433332 + 52428) / 1048576 = 10 */
    snprintf (s, len, "%ldM", (n + 52428) / 1048576);
  }
}

void mutt_safe_path (char *s, size_t l, ADDRESS *a)
{
  char *p;

  if (a && a->mailbox)
  {
    strfcpy (s, a->mailbox, l);
    if (!option (OPTSAVEADDRESS))
    {
    if ((p = strpbrk (s, "%@")))
      *p = 0;
    }
  }
  else
    *s = 0;
  mutt_strlower (s);
  for (p = s; *p; p++)
    if (*p == '/' || ISSPACE (*p) || !IsPrint ((unsigned char) *p))
      *p = '_';
}

/* Read a line from ``fp'' into the dynamically allocated ``s'',
 * increasing ``s'' if necessary. The ending "\n" or "\r\n" is removed.
 * If a line ends with "\", this char and the linefeed is removed,
 * and the next line is read too.
 */
char *mutt_read_line (char *s, size_t *size, FILE *fp, int *line)
{
  size_t offset = 0;
  char *ch;

  if (!s)
  {
    s = safe_malloc (STRING);
    *size = STRING;
  }

  FOREVER
  {
    if (fgets (s + offset, *size - offset, fp) == NULL)
    {
      free (s);
      return NULL;
    }
    if ((ch = strchr (s + offset, '\n')) != NULL)
    {
      (*line)++;
      *ch = 0;
      if (ch > s && *(ch - 1) == '\r')
	*--ch = 0;
      if (ch == s || *(ch - 1) != '\\')
	return s;
      offset = ch - s - 1;
    }
    else
    {
      /* There wasn't room for the line -- increase ``s'' */
      offset = *size - 1; /* overwrite the terminating 0 */
      *size += STRING;
      safe_realloc ((void **) &s, *size);
    }
  }
}

char *
mutt_substrcpy (char *dest, const char *beg, const char *end, size_t destlen)
{
  size_t len;

  len = end - beg;
  if (len > destlen - 1)
    len = destlen - 1;
  memcpy (dest, beg, len);
  dest[len] = 0;
  return dest;
}
