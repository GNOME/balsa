/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
 * Copyright (C) 1999-2000 Thomas Roessler <roessler@guug.de>
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
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#include "mutt.h"
#include "mutt_curses.h"
#include "mime.h"
#include "mailbox.h"
#include "mx.h"
#include "url.h"

#ifndef LIBMUTT
#include "reldate.h"
#endif

#ifdef USE_IMAP
#include "imap.h"
#endif

#ifdef HAVE_PGP
#include "pgp.h"
#endif

#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <utime.h>

BODY *mutt_new_body (void)
{
  BODY *p = (BODY *) safe_calloc (1, sizeof (BODY));
    
  p->disposition = DISPATTACH;
  p->use_disp = 1;
  return (p);
}


/* Modified by blong to accept a "suggestion" for file name.  If
 * that file exists, then construct one with unique name but 
 * keep any extension.  This might fail, I guess.
 * Renamed to mutt_adv_mktemp so I only have to change where it's
 * called, and not all possible cases.
 */
void mutt_adv_mktemp (char *s, size_t l)
{
  char buf[_POSIX_PATH_MAX];
  char tmp[_POSIX_PATH_MAX];
  char *period;
  size_t sl;
  struct stat sb;
  
  strfcpy (buf, NONULL (Tempdir), sizeof (buf));
  mutt_expand_path (buf, sizeof (buf));
  if (s[0] == '\0')
  {
#ifndef LIBMUTT
    snprintf (s, l, "%s/muttXXXXXX", buf);
#else
#define mktemp(x) mkstemp(x)
    snprintf (s, l, "%s/balsaXXXXXX", buf);
#endif
    mktemp (s);
  }
  else
  {
    strfcpy (tmp, s, sizeof (tmp));
    mutt_sanitize_filename (tmp, 1);
    snprintf (s, l, "%s/%s", buf, tmp);
    if (lstat (s, &sb) == -1 && errno == ENOENT)
      return;
    if ((period = strrchr (tmp, '.')) != NULL)
      *period = 0;
    snprintf (s, l, "%s/%s.XXXXXX", buf, tmp);
    mktemp (s);
    if (period != NULL)
    {
      *period = '.';
      sl = mutt_strlen(s);
      strfcpy(s + sl, period, l - sl);
    }
  }
}

#ifndef LIBMUTT
/* create a send-mode duplicate from a receive-mode body */

int mutt_copy_body (FILE *fp, BODY **tgt, BODY *src)
{
  char tmp[_POSIX_PATH_MAX];
  BODY *b;

  PARAMETER *par, **ppar;
  
  short use_disp;

  if (src->filename)
  {
    use_disp = 1;
    strfcpy (tmp, src->filename, sizeof (tmp));
  }
  else
  {
    use_disp = 0;
    tmp[0] = '\0';
  }

  mutt_adv_mktemp (tmp, sizeof (tmp));
  if (mutt_save_attachment (fp, src, tmp, 0, NULL) == -1)
    return -1;
      
  *tgt = mutt_new_body ();
  b = *tgt;

  memcpy (b, src, sizeof (BODY));
  b->parts = NULL;
  b->next  = NULL;

  b->filename = safe_strdup (tmp);
  b->use_disp = use_disp;
  b->unlink = 1;

  if (mutt_is_text_type (b->type, b->subtype))
    b->noconv = 1;

  b->xtype = safe_strdup (b->xtype);
  b->subtype = safe_strdup (b->subtype);
  b->form_name = safe_strdup (b->form_name);
  b->filename = safe_strdup (b->filename);
  b->d_filename = safe_strdup (b->d_filename);
  b->description = safe_strdup (b->description);

  /* 
   * we don't seem to need the HEADER structure currently.
   * XXX - this may change in the future
   */

  if (b->hdr) b->hdr = NULL;
  
  /* copy parameters */
  for (par = b->parameter, ppar = &b->parameter; par; ppar = &(*ppar)->next, par = par->next)
  {
    *ppar = mutt_new_parameter ();
    (*ppar)->attribute = safe_strdup (par->attribute);
    (*ppar)->value = safe_strdup (par->value);
  }

  mutt_stamp_attachment (b);
  
  return 0;
}
#else
/*
 * kept the old _copy_body function. it does exactly what we want, 
 * as oposed to the new one which is meant to be used in a totally
 * diferent way -- chbm
 */
BODY *libmutt_new_body (void)
{
  BODY *p = (BODY *) safe_calloc (1, sizeof (BODY));
    
  p->disposition = DISPATTACH;
  p->use_disp = 1;
  return (p);
}

BODY *libmutt_dup_body (BODY *b)
{
  BODY *bn;

  bn = libmutt_new_body();
  memcpy(bn, b, sizeof (BODY));
  return bn;
}

BODY *
libmutt_copy_body(BODY * b, HEADER * hdr)
{
    PARAMETER *p;
    BODY * body = libmutt_dup_body(b);

    if(b->xtype)
        body->xtype = safe_strdup(b->xtype);
    if(b->subtype)
        body->subtype = safe_strdup(b->subtype);

    p = b->parameter; body->parameter = NULL;
    while(p) {
        mutt_set_parameter(p->attribute, p->value, &body->parameter);
        p = p->next;
    }

    if(b->description)
        body->description = safe_strdup(b->description);
    if(b->form_name)
        body->form_name = safe_strdup(b->form_name);

    if(b->filename)
        body->filename = safe_strdup(b->filename);

    if(b->d_filename)
        body->d_filename = safe_strdup(b->d_filename);

    if(b->content) {
        body->content = safe_calloc (1, sizeof (CONTENT));
        memcpy(body->content, b->content, sizeof (CONTENT));
    }
    if(b->next)
        body->next = libmutt_copy_body(b->next, hdr);

    if(b->parts)
        body->parts = libmutt_copy_body(b->parts, hdr);
    if(b->hdr) 
        fprintf(stderr,"FIXME: Don't know how to copy it (pawels)\n");
    body->hdr = hdr;

    return body;
}
#endif


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
    safe_free ((void **) &b->xtype);
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
  if(!h || !*h) return;
  mutt_free_envelope (&(*h)->env);
  mutt_free_body (&(*h)->content);
  safe_free ((void **) &(*h)->tree);
  safe_free ((void **) &(*h)->path);
#ifdef MIXMASTER
  mutt_free_list (&(*h)->chain);
#endif
#if defined USE_POP || defined USE_IMAP
  safe_free ((void**) &(*h)->data);
#endif
  safe_free ((void **) h);
}

/* returns true if the header contained in "s" is in list "t" */
int mutt_matches_ignore (const char *s, LIST *t)
{
  for (; t; t = t->next)
  {
    if (!ascii_strncasecmp (s, t->data, mutt_strlen (t->data)) || *t->data == '*')
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
  return _mutt_expand_path (s, slen, 0);
}

char *_mutt_expand_path (char *s, size_t slen, int rx)
{
  char p[_POSIX_PATH_MAX] = "";
  char q[_POSIX_PATH_MAX] = "";
  char tmp[_POSIX_PATH_MAX];
  char *t;

  char *tail = ""; 

  int recurse = 0;
  
  do 
  {
    recurse = 0;

    switch (*s)
    {
      case '~':
      {
	if (*(s + 1) == '/' || *(s + 1) == 0)
	{
	  strfcpy (p, NONULL(Homedir), sizeof (p));
	  tail = s + 1;
	}
	else
	{
	  struct passwd *pw;
	  if ((t = strchr (s + 1, '/'))) 
	    *t = 0;

	  if ((pw = getpwnam (s + 1)))
	  {
	    strfcpy (p, pw->pw_dir, sizeof (p));
	    if (t)
	    {
	      *t = '/';
	      tail = t;
	    }
	    else
	      tail = "";
	  }
	  else
	  {
	    /* user not found! */
	    if (t)
	      *t = '/';
	    *p = '\0';
	    tail = s;
	  }
	}
      }
      break;
      
      case '=':
      case '+':    
      {
#ifdef USE_IMAP
	/* if folder = {host} or imap[s]://host/: don't append slash */
	if (mx_is_imap (NONULL (Maildir)) &&
	    (Maildir[strlen (Maildir) - 1] == '}' ||
	     Maildir[strlen (Maildir) - 1] == '/'))
	  strfcpy (p, NONULL (Maildir), sizeof (p));
	else
#endif
	  snprintf (p, sizeof (p), "%s/", NONULL (Maildir));
	
	tail = s + 1;
      }
      break;
      
      /* elm compatibility, @ expands alias to user name */
#ifndef LIBMUTT    
      case '@':
      {
	HEADER *h;
	ADDRESS *alias;
	
	if ((alias = mutt_lookup_alias (s + 1)))
	{
	  h = mutt_new_header();
	  h->env = mutt_new_envelope();
	  h->env->from = h->env->to = alias;
	  mutt_default_save (p, sizeof (p), h);
	  h->env->from = h->env->to = NULL;
	  mutt_free_header (&h);
	  /* Avoid infinite recursion if the resulting folder starts with '@' */
	  if (*p != '@')
	    recurse = 1;
	  
	  tail = "";
	}
      }
      break;
#endif      
      case '>':
      {
	strfcpy (p, NONULL(Inbox), sizeof (p));
	tail = s + 1;
      }
      break;
      
      case '<':
      {
	strfcpy (p, NONULL(Outbox), sizeof (p));
	tail = s + 1;
      }
      break;
      
      case '!':
      {
	if (*(s+1) == '!')
	{
	  strfcpy (p, NONULL(LastFolder), sizeof (p));
	  tail = s + 2;
	}
	else 
	{
	  strfcpy (p, NONULL(Spoolfile), sizeof (p));
	  tail = s + 1;
	}
      }
      break;
      
      case '-':
      {
	strfcpy (p, NONULL(LastFolder), sizeof (p));
	tail = s + 1;
      }
      break;
      
      default:
      {
	*p = '\0';
	tail = s;
      }
    }

    if (rx && *p && !recurse)
    {
      mutt_rx_sanitize_string (q, sizeof (q), p);
      snprintf (tmp, sizeof (tmp), "%s%s", q, tail);
    }
    else
      snprintf (tmp, sizeof (tmp), "%s%s", p, tail);
    
    strfcpy (s, tmp, slen);
  }
  while (recurse);

#ifdef USE_IMAP
  /* Rewrite IMAP path in canonical form - aids in string comparisons of
   * folders. May possibly fail, in which case s should be the same. */
  if (mx_is_imap (s))
    imap_expand_path (s, slen);
#endif

  return (s);
}

/* Extract the real name from /etc/passwd's GECOS field.
 * When set, honor the regular expression in GecosMask,
 * otherwise assume that the GECOS field is a 
 * comma-separated list.
 * Replace "&" by a capitalized version of the user's login
 * name.
 */

char *mutt_gecos_name (char *dest, size_t destlen, struct passwd *pw)
{
  regmatch_t pat_match[1];
  size_t pwnl;
  int idx;
  char *p;
  
  if (!pw || !pw->pw_gecos) 
    return NULL;

  memset (dest, 0, destlen);
  
  if (GecosMask.rx)
  {
    if (regexec (GecosMask.rx, pw->pw_gecos, 1, pat_match, 0) == 0)
      strfcpy (dest, pw->pw_gecos + pat_match[0].rm_so, 
	       MIN (pat_match[0].rm_eo - pat_match[0].rm_so + 1, destlen));
  }
  else if ((p = strchr (pw->pw_gecos, ',')))
    strfcpy (dest, pw->pw_gecos, MIN (destlen, p - pw->pw_gecos + 1));
  else
    strfcpy (dest, pw->pw_gecos, destlen);

  pwnl = strlen (pw->pw_name);

  for (idx = 0; dest[idx]; idx++)
  {
    if (dest[idx] == '&')
    {
      memmove (&dest[idx + pwnl], &dest[idx + 1],
	       MAX(destlen - idx - pwnl - 1, 0));
      memcpy (&dest[idx], pw->pw_name, MIN(destlen - idx - 1, pwnl));
      dest[idx] = toupper (dest[idx]);
    }
  }
      
  return dest;
}
  

char *mutt_get_parameter (const char *s, PARAMETER *p)
{
  for (; p; p = p->next)
    if (ascii_strcasecmp (s, p->attribute) == 0)
      return (p->value);

  return NULL;
}

void mutt_set_parameter (const char *attribute, const char *value, PARAMETER **p)
{
  PARAMETER *q;

  if (!value)
  {
    mutt_delete_parameter (attribute, p);
    return;
  }
  
  for(q = *p; q; q = q->next)
  {
    if (ascii_strcasecmp (attribute, q->attribute) == 0)
    {
      mutt_str_replace (&q->value, value);
      return;
    }
  }
  
  q = mutt_new_parameter();
  q->attribute = safe_strdup(attribute);
  q->value = safe_strdup(value);
  q->next = *p;
  *p = q;
}

void mutt_delete_parameter (const char *attribute, PARAMETER **p)
{
  PARAMETER *q;
  
  for (q = *p; q; p = &q->next, q = q->next)
  {
    if (ascii_strcasecmp (attribute, q->attribute) == 0)
    {
      *p = q->next;
      q->next = NULL;
      mutt_free_parameter (&q);
      return;
    }
  }
}

/* returns 1 if Mutt can't display this type of data, 0 otherwise */
int mutt_needs_mailcap (BODY *m)
{
  switch (m->type)
  {
    case TYPETEXT:

      if (!ascii_strcasecmp ("plain", m->subtype) ||
	  !ascii_strcasecmp ("rfc822-headers", m->subtype) ||
	  !ascii_strcasecmp ("enriched", m->subtype))
	return 0;
      break;



#ifdef HAVE_PGP
    case TYPEAPPLICATION:
      if(mutt_is_application_pgp(m))
	return 0;
      break;
#endif /* HAVE_PGP */


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
    if (!ascii_strcasecmp ("delivery-status", s))
      return 1;
  }



#ifdef HAVE_PGP
  if (t == TYPEAPPLICATION)
  {
    if (!ascii_strcasecmp ("pgp-keys", s))
      return 1;
  }
#endif /* HAVE_PGP */



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
  safe_free ((void **) &(*p)->supersedes);
  safe_free ((void **) &(*p)->date);
  mutt_free_list (&(*p)->references);
  mutt_free_list (&(*p)->in_reply_to);
  mutt_free_list (&(*p)->userhdrs);
  safe_free ((void **) p);
}

void mutt_mktemp (char *s)
{
#ifndef LIBMUTT
  snprintf (s, _POSIX_PATH_MAX, "%s/mutt-%s-%d-%d", NONULL (Tempdir), NONULL(Hostname), (int) getpid (), Counter++);
#else
  snprintf (s, _POSIX_PATH_MAX, "%s/balsa-%s-%d-%d", NONULL (Tempdir), NONULL(Hostname), (int) getpid (), Counter++);
#endif
  unlink (s);
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
    safe_free ((void **) &t);
  }
}

/* collapse the pathname using ~ or = when possible */
void mutt_pretty_mailbox (char *s)
{
  char *p = s, *q = s;
  size_t len;
  url_scheme_t scheme;

  scheme = url_check_scheme (s);

#ifdef USE_IMAP
  if (scheme == U_IMAP || scheme == U_IMAPS)
  {
    imap_pretty_mailbox (s);
    return;
  }
#endif

  /* if s is an url, only collapse path component */
  if (scheme != U_UNKNOWN)
  {
    p = strchr(s, ':')+1;
    if (!strncmp (p, "//", 2))
      q = strchr (p+2, '/');
    if (!q)
      q = strchr (p, '\0');
    p = q;
  }
  
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

  if (mutt_strncmp (s, Maildir, (len = mutt_strlen (Maildir))) == 0 &&
      s[len] == '/')
  {
    *s++ = '=';
    memmove (s, s + len, mutt_strlen (s + len) + 1);
  }
  else if (mutt_strncmp (s, Homedir, (len = mutt_strlen (Homedir))) == 0 &&
	   s[len] == '/')
  {
    *s++ = '~';
    memmove (s, s + len - 1, mutt_strlen (s + len - 1) + 1);
  }
}

void mutt_pretty_size (char *s, size_t len, long n)
{
  if (n == 0)
    strfcpy (s, "0K", len);
  else if (n < 10189) /* 0.1K - 9.9K */
    snprintf (s, len, "%3.1fK", (n < 103) ? 0.1 : n / 1024.0);
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

void mutt_expand_file_fmt (char *dest, size_t destlen, const char *fmt, const char *src)
{
  char tmp[LONG_STRING];
  
  mutt_quote_filename (tmp, sizeof (tmp), src);
  mutt_expand_fmt (dest, destlen, fmt, tmp);
}

void mutt_expand_fmt (char *dest, size_t destlen, const char *fmt, const char *src)
{
  const char *p = fmt;
  const char *last = p;
  size_t len;
  size_t slen;
  int found = 0;

  slen = mutt_strlen (src);
  
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
    snprintf (dest, destlen, "%s %s", fmt, src);
  
}

#ifndef LIBMUTT
/* return 0 on success, -1 on error */
int mutt_check_overwrite (const char *attname, const char *path,
				char *fname, size_t flen, int *append) 
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
    if (mutt_yesorno (_("File is a directory, save under it?"), 1) != M_YES) 
      return (-1);
    if (!attname || !attname[0])
    {
      tmp[0] = 0;
      if (mutt_get_field (_("File under directory: "), tmp, sizeof (tmp),
				      M_FILE | M_CLEAR) != 0 || !tmp[0])
	return (-1);
      snprintf (fname, flen, "%s/%s", path, tmp);
    }
    else
      snprintf (fname, flen, "%s/%s", path, attname);
  }
  
  if (*append == 0 && access (fname, F_OK) == 0)
  {
    switch (mutt_multi_choice
	    (_("File exists, (o)verwrite, (a)ppend, or (c)ancel?"), _("oac")))
    {
      case -1: /* abort */
      case 3:  /* cancel */
	return -1;

      case 2: /* append */
        *append = M_SAVE_APPEND;
        break;
      case 1: /* overwrite */
        *append = M_SAVE_OVERWRITE;
        break;
    }
  }
  return 0;
}
#endif

void mutt_save_path (char *d, size_t dsize, ADDRESS *a)
{
  if (a && a->mailbox)
  {
    strfcpy (d, a->mailbox, dsize);
    if (!option (OPTSAVEADDRESS))
    {
      char *p;

      if ((p = strpbrk (d, "%@")))
	*p = 0;
    }
    mutt_strlower (d);
  }
  else
    *d = 0;
}

void mutt_safe_path (char *s, size_t l, ADDRESS *a)
{
  char *p;

  mutt_save_path (s, l, a);
  for (p = s; *p; p++)
    if (*p == '/' || ISSPACE (*p) || !IsPrint ((unsigned char) *p))
      *p = '_';
}

#ifndef LIBMUTT
void mutt_FormatString (char *dest,		/* output buffer */
			size_t destlen,		/* output buffer len */
			const char *src,	/* template string */
			format_t *callback,	/* callback for processing */
			unsigned long data,	/* callback data */
			format_flag flags)	/* callback flags */
{
  char prefix[SHORT_STRING], buf[LONG_STRING], *cp, *wptr = dest, ch;
  char ifstring[SHORT_STRING], elsestring[SHORT_STRING];
  size_t wlen, count, len;

  prefix[0] = '\0';
  destlen--; /* save room for the terminal \0 */
  wlen = (flags & M_FORMAT_ARROWCURSOR && option (OPTARROWCURSOR)) ? 3 : 0;
    
  while (*src && wlen < destlen)
  {
    if (*src == '%')
    {
      if (*++src == '%')
      {
	*wptr++ = '%';
	wlen++;
	src++;
	continue;
      }

      if (*src == '?')
      {
	flags |= M_FORMAT_OPTIONAL;
	src++;
      }
      else
      {
	flags &= ~M_FORMAT_OPTIONAL;

	/* eat the format string */
	cp = prefix;
	count = 0;
	while (count < sizeof (prefix) &&
	       (isdigit ((unsigned char) *src) || *src == '.' || *src == '-'))
	{
	  *cp++ = *src++;
	  count++;
	}
	*cp = 0;
      }

      if (!*src)
	break; /* bad format */

      ch = *src++; /* save the character to switch on */

      if (flags & M_FORMAT_OPTIONAL)
      {
        if (*src != '?')
          break; /* bad format */
        src++;

        /* eat the `if' part of the string */
        cp = ifstring;
	count = 0;
        while (count < sizeof (ifstring) && *src && *src != '?' && *src != '&')
	{
          *cp++ = *src++;
	  count++;
	}
        *cp = 0;

	/* eat the `else' part of the string (optional) */
	if (*src == '&')
	  src++; /* skip the & */
	cp = elsestring;
	count = 0;
	while (count < sizeof (elsestring) && *src && *src != '?')
	{
	  *cp++ = *src++;
	  count++;
	}
	*cp = 0;

	if (!*src)
	  break; /* bad format */

        src++; /* move past the trailing `?' */
      }

      /* handle generic cases first */
      if (ch == '>')
      {
	/* right justify to EOL */
	ch = *src++; /* pad char */
	/* calculate space left on line.  if we've already written more data
	   than will fit on the line, ignore the rest of the line */
	count = (COLS < destlen ? COLS : destlen);
	if (count > wlen)
	{
	  count -= wlen; /* how many chars left on this line */
	  mutt_FormatString (buf, sizeof (buf), src, callback, data, flags);
	  len = mutt_strlen (buf);
	  if (count > len)
	  {
	    count -= len; /* how many chars to pad */
	    memset (wptr, ch, count);
	    wptr += count;
	    wlen += count;
	  }
	  if (len + wlen > destlen)
	    len = destlen - wlen;
	  memcpy (wptr, buf, len);
	  wptr += len;
	  wlen += len;
	}
	break; /* skip rest of input */
      }
      else if (ch == '|')
      {
	/* pad to EOL */
	ch = *src++;
	if (destlen > COLS)
	  destlen = COLS;
	if (destlen > wlen)
	{
	  count = destlen - wlen;
	  memset (wptr, ch, count);
	  wptr += count;
	}
	break; /* skip rest of input */
      }
      else
      {
	short tolower =  0;
	
	if (ch == '_')
	{
	  ch = *src++;
	  tolower = 1;
	}
	
	/* use callback function to handle this case */
	src = callback (buf, sizeof (buf), ch, src, prefix, ifstring, elsestring, data, flags);

	if (tolower)
	  mutt_strlower (buf);
	
	if ((len = mutt_strlen (buf)) + wlen > destlen)
	  len = (destlen - wlen > 0) ? (destlen - wlen) : 0;

	memcpy (wptr, buf, len);
	wptr += len;
	wlen += len;
      }
    }
    else if (*src == '\\')
    {
      if (!*++src)
	break;
      switch (*src)
      {
	case 'n':
	  *wptr = '\n';
	  break;
	case 't':
	  *wptr = '\t';
	  break;
	case 'r':
	  *wptr = '\r';
	  break;
	case 'f':
	  *wptr = '\f';
	  break;
	case 'v':
	  *wptr = '\v';
	  break;
	default:
	  *wptr = *src;
	  break;
      }
      src++;
      wptr++;
      wlen++;
    }
    else
    {
      *wptr++ = *src++;
      wlen++;
    }
  }
  *wptr = 0;

#if 0
  if (flags & M_FORMAT_MAKEPRINT)
  {
    /* Make sure that the string is printable by changing all non-printable
       chars to dots, or spaces for non-printable whitespace */
    for (cp = dest ; *cp ; cp++)
      if (!IsPrint (*cp) &&
	  !((flags & M_FORMAT_TREE) && (*cp <= M_TREE_MAX)))
	*cp = isspace ((unsigned char) *cp) ? ' ' : '.';
  }
#endif
}

/* This function allows the user to specify a command to read stdout from in
   place of a normal file.  If the last character in the string is a pipe (|),
   then we assume it is a commmand to run instead of a normal file. */
FILE *mutt_open_read (const char *path, pid_t *thepid)
{
  FILE *f;
  int len = mutt_strlen (path);

  if (path[len - 1] == '|')
  {
    /* read from a pipe */

    char *s = safe_strdup (path);

    s[len - 1] = 0;
    mutt_endwin (NULL);
    *thepid = mutt_create_filter (s, NULL, &f, NULL);
    safe_free ((void **) &s);
  }
  else
  {
    f = fopen (path, "r");
    *thepid = -1;
  }
  return (f);
}

/* returns 1 if OK to proceed, 0 to abort */
int mutt_save_confirm (const char *s, struct stat *st)
{
  char tmp[_POSIX_PATH_MAX];
  int ret = 1;
  int magic = 0;

  magic = mx_get_magic (s);

#ifdef USE_POP
  if (magic == M_POP)
  {
    mutt_error _("Can't save message to POP mailbox.");
    return 0;
  }
#endif

  if (stat (s, st) != -1)
  {
    if (magic == -1)
    {
      mutt_error (_("%s is not a mailbox!"), s);
      return 0;
    }

    if (option (OPTCONFIRMAPPEND))
    {
      snprintf (tmp, sizeof (tmp), _("Append messages to %s?"), s);
      if (mutt_yesorno (tmp, 1) < 1)
	ret = 0;
    }
  }
  else
  {
#ifdef USE_IMAP
    if (magic != M_IMAP)
#endif /* execute the block unconditionally if we don't use imap */
    {
      st->st_mtime = 0;
      st->st_atime = 0;

      if (errno == ENOENT)
      {
	if (option (OPTCONFIRMCREATE))
	{
	  snprintf (tmp, sizeof (tmp), _("Create %s?"), s);
	  if (mutt_yesorno (tmp, 1) < 1)
	    ret = 0;
	}
      }
      else
      {
	mutt_perror (s);
	return 0;
      }
    }
  }

  CLEARLINE (LINES-1);
  return (ret);
}
#endif /* LIBMUTT */

void state_prefix_putc(char c, STATE *s)
{
  if (s->flags & M_PENDINGPREFIX)
  {
    state_reset_prefix(s);
    if (s->prefix)
      state_puts(s->prefix, s);
  }

  state_putc(c, s);

  if(c == '\n')
    state_set_prefix(s);
}

int state_printf(STATE *s, const char *fmt, ...)
{
  int rv;
  va_list ap;

  va_start (ap, fmt);
  rv = vfprintf(s->fpout, fmt, ap);
  va_end(ap);
  
  return rv;
}

#ifndef LIBMUTT
void state_mark_attach (STATE *s)
{
  if ((s->flags & M_DISPLAY) && !mutt_strcmp (Pager, "builtin"))
    state_puts (AttachmentMarker, s);
}

void state_attach_puts (const char *t, STATE *s)
{
  state_mark_attach (s);
  while (*t)
  {
    state_putc (*t, s);
    if (*t++ == '\n' && *t)
      state_mark_attach (s);
  }
}

void mutt_display_sanitize (char *s)
{
  for (; *s; s++)
  {
    if (!IsPrint (*s))
      *s = '?';
  }
}

/* BALSA: if this function is needed something is wrong */
void mutt_sleep (short s)
{
  if (SleepTime > s)
    sleep (SleepTime);
  else if (s)
    sleep (s);
}
#endif /* LIBMUTT */

void mutt_buffer_addstr (BUFFER* buf, const char* s)
{
  mutt_buffer_add (buf, s, mutt_strlen (s));
}

void mutt_buffer_addch (BUFFER* buf, char c)
{
  mutt_buffer_add (buf, &c, 1);
}

/* dynamically grows a BUFFER to accomodate s, in increments of 128 bytes.
 * Always one byte bigger than necessary for the null terminator, and
 * the buffer is always null-terminated */
void mutt_buffer_add (BUFFER* buf, const char* s, size_t len)
{
  size_t offset;

  if (buf->dptr + len + 1 > buf->data + buf->dsize)
  {
    offset = buf->dptr - buf->data;
    buf->dsize += len < 128 ? 128 : len + 1;
    safe_realloc ((void**) &buf->data, buf->dsize);
    buf->dptr = buf->data + offset;
  }
  memcpy (buf->dptr, s, len);
  buf->dptr += len;
  *(buf->dptr) = '\0';
}

/* Decrease a file's modification time by 1 second */

time_t mutt_decrease_mtime (const char *f, struct stat *st)
{
  struct utimbuf utim;
  struct stat _st;
  time_t mtime;
  
  if (!st)
  {
    if (stat (f, &_st) == -1)
      return -1;
    st = &_st;
  }

  if ((mtime = st->st_mtime) == time (NULL))
  {
    mtime -= 1;
    utim.actime = mtime;
    utim.modtime = mtime;
    utime (f, &utim);
  }
  
  return mtime;
}

#ifndef LIBMUTT 
const char *mutt_make_version (void)
{
  static char vstring[STRING];
  snprintf (vstring, sizeof (vstring), "Mutt %s (%s)",
	    MUTT_VERSION, ReleaseDate);
  return vstring;
}
#endif 
