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
#include "mutt_regex.h"






#include "mx.h"
#include "init.h"
#include "mailbox.h"

#include <pwd.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/utsname.h>
#include <errno.h>
#include <sys/wait.h>


#define toggle_quadoption(opt) QuadOptions ^= (1 << (2 * opt))

void set_quadoption (int opt, int flag)
{
  QuadOptions &= ~(0x3 << (2 * opt)); /* first clear the bits */
  QuadOptions |= (flag & 0x3) << (2 * opt); /* now set them */
}

int quadoption (int opt)
{
  return ((QuadOptions >> (opt * 2)) & 0x3);
}

int query_quadoption (int opt, const char *prompt)
{
  int v = quadoption (opt);

  switch (v)
  {
    case M_YES:
    case M_NO:
      return (v);

    default:
      v = mutt_yesorno (prompt, (v == M_ASKYES));
#ifndef LIBMUTT
      CLEARLINE (LINES - 1);
#endif
      return (v);
  }

  /* not reached */
}
#ifndef LIBMUTT
/* given the variable ``s'', return the index into the rc_vars array which
   matches, or -1 if the variable is not found.  */
int mutt_option_index (char *s)
{
  int i;

  for (i = 0; MuttVars[i].option; i++)
    if (strcmp (s, MuttVars[i].option) == 0)
      return (MuttVars[i].type == DT_SYN ?  mutt_option_index ((char *) MuttVars[i].data) : i);
  return (-1);
}
#endif
static void add_char (BUFFER *buf, char ch)
{
  size_t offset;

  if (buf->dptr >= buf->data + buf->dsize)
  {
    offset = buf->dptr - buf->data;
    buf->dsize += 4;
    safe_realloc ((void **) &buf->data, buf->dsize);
    buf->dptr = buf->data + offset;
  }
  *buf->dptr++ = ch;
}

static void add_str (BUFFER *buf, const char *s)
{
  size_t slen = strlen (s);
  size_t offset;

  if (buf->dptr + slen > buf->data + buf->dsize)
  {
    offset = buf->dptr - buf->data;
    buf->dsize += slen;
    safe_realloc ((void **) &buf->data, buf->dsize);
    buf->dptr = buf->data + offset;
  }
  memcpy (buf->dptr, s, slen);
  buf->dptr += slen;
}

int mutt_extract_token (BUFFER *dest, BUFFER *tok, int flags)
{
  char		ch;
  char		qc = 0; /* quote char */
  char		*pc;

  /* reset the destination pointer to the beginning of the buffer */
  dest->dptr = dest->data;

  SKIPWS (tok->dptr);
  while ((ch = *tok->dptr))
  {
    if (!qc)
    {
      if ((ISSPACE (ch) && !(flags & M_TOKEN_SPACE)) ||
	  (ch == '#' && !(flags & M_TOKEN_COMMENT)) ||
	  (ch == '=' && (flags & M_TOKEN_EQUAL)) ||
	  (ch == ';' && !(flags & M_TOKEN_SEMICOLON)) ||
	  ((flags & M_TOKEN_PATTERN) && strchr ("~!|", ch)))
	break;
    }

    tok->dptr++;

    if (ch == qc)
      qc = 0; /* end of quote */
    else if (!qc && (ch == '\'' || ch == '"') && !(flags & M_TOKEN_QUOTE))
      qc = ch;
    else if (ch == '\\' && qc != '\'')
    {
      switch (ch = *tok->dptr++)
      {
	case 'c':
	case 'C':
	  add_char (dest, (toupper (*tok->dptr) - '@') & 0x7f);
	  tok->dptr++;
	  break;
	case 'r':
	  add_char (dest, '\r');
	  break;
	case 'n':
	  add_char (dest, '\n');
	  break;
	case 't':
	  add_char (dest, '\t');
	  break;
	case 'f':
	  add_char (dest, '\f');
	  break;
	case 'e':
	  add_char (dest, '\033');
	  break;
	default:
	  if (isdigit ((unsigned char) ch) &&
	      isdigit ((unsigned char) *tok->dptr) &&
	      isdigit ((unsigned char) *(tok->dptr + 1)))
	  {

	    add_char (dest, (ch << 6) + (*tok->dptr << 3) + *(tok->dptr + 1) - 3504);
	    tok->dptr += 2;
	  }
	  else
	    add_char (dest, ch);
      }
    }
    else if (ch == '^' && (flags & M_TOKEN_CONDENSE))
    {
      ch = *tok->dptr++;
      if (ch == '^')
	add_char (dest, ch);
      else if (ch == '[')
	add_char (dest, '\033');
      else if (isalpha ((unsigned char) ch))
	add_char (dest, toupper (ch) - '@');
      else
      {
	add_char (dest, '^');
	add_char (dest, ch);
      }
    }
    else if (ch == '`' && (!qc || qc == '"'))
    {
      FILE	*fp;
      pid_t	pid;
      char	*cmd, *ptr;
      size_t	expnlen;
      BUFFER	expn;
      int	line = 0;

      pc = tok->dptr;
      do {
	if ((pc = strpbrk (pc, "\\`")))
	{
	  /* skip any quoted chars */
	  if (*pc == '\\')
	    pc += 2;
	}
      } while (pc && *pc != '`');
      if (!pc)
      {
	dprint (1, (debugfile, "mutt_get_token: mismatched backtics\n"));
	return (-1);
      }
      cmd = mutt_substrdup (tok->dptr, pc);
      if ((pid = mutt_create_filter (cmd, NULL, &fp, NULL)) < 0)
      {
	dprint (1, (debugfile, "mutt_get_token: unable to fork command: %s", cmd));
	return (-1);
      }
      FREE (&cmd);

      tok->dptr = pc + 1;

      /* read line */
      memset (&expn, 0, sizeof (expn));
      expn.data = mutt_read_line (NULL, &expn.dsize, fp, &line);
      fclose (fp);
      mutt_wait_filter (pid);

      /* if we got output, make a new string consiting of the shell ouptput
	 plus whatever else was left on the original line */
      if (expn.data)
      {
	expnlen = strlen (expn.data);
	tok->dsize = expnlen + strlen (tok->dptr) + 1;
	ptr = safe_malloc (tok->dsize);
	memcpy (ptr, expn.data, expnlen);
	strcpy (ptr + expnlen, tok->dptr);
	if (tok->destroy)
	  FREE (&tok->data);
	tok->data = ptr;
	tok->dptr = ptr;
	tok->destroy = 1; /* mark that the caller should destroy this data */
	ptr = NULL;
	FREE (&expn.data);
      }
    }
    else if (ch == '$' && (!qc || qc == '"') && (*tok->dptr == '{' || isalpha ((unsigned char) *tok->dptr)))
    {
      char *env, *var;

      if (*tok->dptr == '{')
      {
	tok->dptr++;
	if ((pc = strchr (tok->dptr, '}')))
	{
	  var = mutt_substrdup (tok->dptr, pc);
	  tok->dptr = pc + 1;
	}
      }
      else
      {
	for (pc = tok->dptr; isalpha ((unsigned char) *pc) || *pc == '_'; pc++)
	  ;
	var = mutt_substrdup (tok->dptr, pc);
	tok->dptr = pc;
      }
      if ((env = getenv (var)))
	add_str (dest, env);
      FREE (&var);
    }
    else
      add_char (dest, ch);
  }
  add_char (dest, 0); /* terminate the string */
  SKIPWS (tok->dptr);
  return 0;
}
#ifndef LIBMUTT
void mutt_add_to_list (LIST **list, BUFFER *inp)
{
  LIST *t, *last = NULL;
  BUFFER buf;

  memset (&buf, 0, sizeof (buf));
  do
  {
    mutt_extract_token (&buf, inp, 0);

    /* check to make sure the item is not already on this list */
    for (last = *list; last; last = last->next)
    {
      if (strcasecmp (buf.data, last->data) == 0)
      {
	/* already on the list, so just ignore it */
	last = NULL;
	break;
      }
      if (!last->next)
	break;
    }

    if (!*list || last)
    {
      t = (LIST *) safe_calloc (1, sizeof (LIST));
      t->data = buf.data;
      memset (&buf, 0, sizeof (buf));
      if (last)
      {
	last->next = t;
	last = last->next;
      }
      else
	*list = last = t;
    }
  }
  while (MoreArgs (inp));
  FREE (&buf.data);
}

static void remove_from_list (LIST **l, BUFFER *inp)
{
  LIST *p, *last = NULL;
  BUFFER buf;

  memset (&buf, 0, sizeof (buf));
  do
  {
    mutt_extract_token (&buf, inp, 0);

    if (strcmp ("*", buf.data) == 0)
      mutt_free_list (l);    /* ``unCMD *'' means delete all current entries */
    else
    {
      p = *l;
      last = NULL;
      while (p)
      {
	if (strcasecmp (buf.data, p->data) == 0)
	{
	  safe_free ((void **) &p->data);
	  if (last)
	    last->next = p->next;
	  else
	    (*l) = p->next;
	  safe_free ((void **) &p);
	}
	else
	{
	  last = p;
	  p = p->next;
	}
      }
    }
  }
  while (MoreArgs (inp));
  FREE (&buf.data);
}

static int parse_unignore (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  mutt_add_to_list (&UnIgnore, s);
  remove_from_list (&Ignore, s);
  return 0;
}

static int parse_ignore (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  mutt_add_to_list (&Ignore, s);
  remove_from_list (&UnIgnore, s);
  return 0;
}

static int parse_list (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  mutt_add_to_list ((LIST **) data, s);
  return 0;
}

static int parse_unlist (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  remove_from_list ((LIST **) data, s);
  return 0;
}

static int parse_unalias (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  ALIAS *tmp, *last = NULL;

  do
  {
    mutt_extract_token (buf, s, 0);

    tmp = Aliases;
    for (tmp = Aliases; tmp; tmp = tmp->next)
    {
      if (strcasecmp (buf->data, tmp->name) == 0)
      {
	if (last)
	  last->next = tmp->next;
	else
	  Aliases = tmp->next;
	tmp->next = NULL;
	mutt_free_alias (&tmp);
	break;
      }
      last = tmp;
    }
  }
  while (MoreArgs (s));
  return 0;
}

static int parse_alias (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  ALIAS *tmp = Aliases;
  ALIAS *last = NULL;
  char *p;
  size_t len;

  if ((p = strpbrk (s->dptr, " \t")) == NULL)
  {
    strfcpy (err->data, "alias: no address", err->dsize);
    return (-1);
  }

  len = p - s->dptr;

  /* check to see if an alias with this name already exists */
  for (; tmp; tmp = tmp->next)
  {
    if (!strncasecmp (tmp->name, s->dptr, len) && *(tmp->name + len) == 0)
      break;
    last = tmp;
  }

  if (!tmp)
  {
    /* create a new alias */
    tmp = (ALIAS *) safe_calloc (1, sizeof (ALIAS));
    tmp->name = safe_malloc (len + 1);
    memcpy (tmp->name, s->dptr, len);
    tmp->name[len] = 0;
  }
  else
  {
    /* override the previous value */
    rfc822_free_address (&tmp->addr);
  }
  s->dptr = p;

  mutt_extract_token (buf, s, M_TOKEN_QUOTE | M_TOKEN_SPACE);
  tmp->addr = mutt_parse_adrlist (tmp->addr, buf->data);
  if (last)
    last->next = tmp;
  else
    Aliases = tmp;
  return 0;
}

static int
parse_unmy_hdr (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  LIST *last = NULL;
  LIST *tmp = UserHeader;
  LIST *ptr;
  size_t l;

  do
  {
    mutt_extract_token (buf, s, 0);
    if (strcmp ("*", buf->data) == 0)
      mutt_free_list (&UserHeader);
    else
    {
      tmp = UserHeader;
      last = NULL;

      l = strlen (buf->data);
      if (buf->data[l - 1] == ':')
	l--;

      while (tmp)
      {
	if (strncasecmp (buf->data, tmp->data, l) == 0 && tmp->data[l] == ':')
	{
	  ptr = tmp;
	  if (last)
	    last->next = tmp->next;
	  else
	    UserHeader = tmp->next;
	  tmp = tmp->next;
	  ptr->next = NULL;
	  mutt_free_list (&ptr);
	}
	else
	{
	  last = tmp;
	  tmp = tmp->next;
	}
      }
    }
  }
  while (MoreArgs (s));
  return 0;
}
#endif
static int parse_my_hdr (BUFFER *buf, BUFFER *s, unsigned long data, BUFFER *err)
{
  LIST *tmp;
  size_t keylen;
  char *p;

  mutt_extract_token (buf, s, M_TOKEN_SPACE | M_TOKEN_QUOTE);
  if ((p = strpbrk (buf->data, ": \t")) == NULL || *p != ':')
  {
    strfcpy (err->data, "invalid header field", err->dsize);
    return (-1);
  }
  keylen = p - buf->data + 1;
  p++;
  SKIPWS (p);
  if (!*p)
  {
    snprintf (err->data, err->dsize, "ignoring empty header field: %s", buf->data);
    return (-1);
  }

  if (UserHeader)
  {
    for (tmp = UserHeader; ; tmp = tmp->next)
    {
      /* see if there is already a field by this name */
      if (strncasecmp (buf->data, tmp->data, keylen) == 0)
      {
	/* replace the old value */
	safe_free ((void **) &tmp->data);
	tmp->data = buf->data;
	memset (buf, 0, sizeof (BUFFER));
	return 0;
      }
      if (!tmp->next)
	break;
    }
    tmp->next = mutt_new_list ();
    tmp = tmp->next;
  }
  else
  {
    tmp = mutt_new_list ();
    UserHeader = tmp;
  }
  tmp->data = buf->data;
  memset (buf, 0, sizeof (BUFFER));
  return 0;
}

static int
parse_sort (short *val, const char *s, const struct mapping_t *map, BUFFER *err)
{
  int i, flags = 0;

  if (strncmp ("reverse-", s, 8) == 0)
  {
    s += 8;
    flags = SORT_REVERSE;
  }
  
  if (strncmp ("last-", s, 5) == 0)
  {
    s += 5;
    flags |= SORT_LAST;
  }

  if ((i = mutt_getvaluebyname (s, map)) == -1)
  {
    snprintf (err->data, err->dsize, "%s: unknown sorting method", s);
    return (-1);
  }

  *val = i | flags;

  return 0;
}
#ifndef LIBMUTT
static void mutt_restore_default (struct option_t *p)
{
  switch (p->type & DT_MASK)
  {
    case DT_STR:
      if (p->init)
      {
	FREE (p->data);
	*((char **) p->data) = safe_strdup ((char *) p->init);
      }
      break;
    case DT_PATH:
      if (p->init)
      {
	char path[_POSIX_PATH_MAX];

	FREE (p->data);
	strfcpy (path, (char *) p->init, sizeof (path));
	mutt_expand_path (path, sizeof (path));
	*((char **) p->data) = safe_strdup (path);
      }
      break;
    case DT_BOOL:
      if (p->init)
	set_option (p->data);
      else
	unset_option (p->data);
      break;
    case DT_QUAD:
      set_quadoption (p->data, p->init);
      break;
    case DT_NUM:
    case DT_SORT:
    case DT_MAGIC:
      *((short *) p->data) = p->init;
      break;
    case DT_RX:
      {
	REGEXP *pp = (REGEXP *) p->data;
	FREE (&pp->pattern);
	if (p->init)
	{
	  if (pp->rx)
	    regfree (pp->rx);
	  else
	    pp->rx = safe_calloc (1, sizeof (regex_t));
	  pp->pattern = safe_strdup ((char *) p->init);
	  if (REGCOMP (pp->rx, pp->pattern, mutt_which_case (pp->pattern)) != 0)
	  {
	    fprintf (stderr, "mutt_restore_default: error in regexp: %s\n",
		     pp->pattern);
	  }
	}
      }
      break;
  }
  if (p->flags & R_INDEX)
    set_option (OPTFORCEREDRAWINDEX);
  if (p->flags & R_PAGER)
    set_option (OPTFORCEREDRAWPAGER);
  if (p->flags & R_RESORT_SUB)
    set_option (OPTSORTSUBTHREADS);
  if (p->flags & R_RESORT)
    set_option (OPTNEEDRESORT);
}

static int parse_set (BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err)
{
  int idx, query, unset, inv, reset, r = 0;
  char *p, scratch[_POSIX_PATH_MAX];

  while (MoreArgs (s))
  {
    /* reset state variables */
    query = 0;
    unset = data & M_SET_UNSET;
    inv = data & M_SET_INV;
    reset = data & M_SET_RESET;

    if (*s->dptr == '?')
    {
      query = 1;
      s->dptr++;
    }
    else if (strncmp ("no", s->dptr, 2) == 0)
    {
      s->dptr += 2;
      unset = !unset;
    }
    else if (strncmp ("inv", s->dptr, 3) == 0)
    {
      s->dptr += 3;
      inv = !inv;
    }
    else if (*s->dptr == '&')
    {
      reset = 1;
      s->dptr++;
    }

    /* get the variable name */
    mutt_extract_token (tmp, s, M_TOKEN_EQUAL);

    if ((idx = mutt_option_index (tmp->data)) == -1 &&
	!(reset && !strcmp ("all", tmp->data)))
    {
      snprintf (err->data, err->dsize, "%s: unknown variable", tmp->data);
      return (-1);
    }
    SKIPWS (s->dptr);

    if (reset)
    {
      if (query || unset || inv)
      {
	snprintf (err->data, err->dsize, "prefix is illegal with reset");
	return (-1);
      }

      if (s && *s->dptr == '=')
      {
	snprintf (err->data, err->dsize, "value is illegal with reset");
	return (-1);
      }
     
      if (!strcmp ("all", tmp->data))
      {
	for (idx = 0; MuttVars[idx].option; idx++)
	  mutt_restore_default (&MuttVars[idx]);
	return 0;
      }
      else
	mutt_restore_default (&MuttVars[idx]);
    } 
    else if (DTYPE (MuttVars[idx].type) == DT_BOOL)
    { 
      if (s && *s->dptr == '=')
      {
	snprintf (err->data, err->dsize, "%s is a boolean var!", tmp->data);
	return (-1);
      }

      if (query)
      {
	snprintf (err->data, err->dsize, "%s is %sset", tmp->data,
		  option (MuttVars[idx].data) ? "" : "un");
	return 0;
      }

      if (unset)
	unset_option (MuttVars[idx].data);
      else if (inv)
	toggle_option (MuttVars[idx].data);
      else
	set_option (MuttVars[idx].data);
    }
    else if (DTYPE (MuttVars[idx].type) == DT_STR ||
	     DTYPE (MuttVars[idx].type) == DT_PATH)
    {
      if (query || *s->dptr != '=')
      {
	/* user requested the value of this variable */
	snprintf (err->data, err->dsize, "%s=\"%s\"", MuttVars[idx].option,
		  NONULL (*((char **) MuttVars[idx].data)));
	break;
      }

      s->dptr++;

      /* copy the value of the string */
      FREE (MuttVars[idx].data);
      mutt_extract_token (tmp, s, 0);
      if (MuttVars[idx].type == DT_PATH)
      {
	strfcpy (scratch, tmp->data, sizeof (scratch));
	mutt_expand_path (scratch, sizeof (scratch));
	*((char **) MuttVars[idx].data) = safe_strdup (scratch);
      }
      else
      {
	*((char **) MuttVars[idx].data) = safe_strdup (tmp->data);
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_RX)
    {
      REGEXP *ptr = (REGEXP *) MuttVars[idx].data;
      regex_t *rx;
      int e, flags = 0;

      if (query || *s->dptr != '=')
      {
	/* user requested the value of this variable */
	snprintf (err->data, err->dsize, "%s=\"%s\"", MuttVars[idx].option,
		  NONULL (ptr->pattern));
	break;
      }

      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token (tmp, s, 0);

      if (!ptr->pattern || strcmp (ptr->pattern, tmp->data) != 0)
      {
	/* $alternates is case-insensitive,
	   $mask is case-sensitive */
	if (strcmp (MuttVars[idx].option, "alternates") == 0)
	  flags |= REG_ICASE;
	else if (strcmp (MuttVars[idx].option, "mask") != 0)
	  flags |= mutt_which_case (tmp->data);
	
	rx = (regex_t *) safe_malloc (sizeof (regex_t));
	if ((e = REGCOMP (rx, tmp->data, flags)) != 0)
	{
	  regerror (e, rx, err->data, err->dsize);
	  regfree (rx);
	  FREE (&rx);
	  break;
	}

	/* get here only if everything went smootly */
	if (ptr->pattern)
	{
	  FREE (&ptr->pattern);
	  regfree ((regex_t *) ptr->rx);
	  FREE (&ptr->rx);
	}

	ptr->pattern = safe_strdup (tmp->data);
	ptr->rx = rx;

	/* $reply_regexp requires special treatment */
	if (Context && Context->msgcount &&
	    strcmp (MuttVars[idx].option, "reply_regexp") == 0)
	{
	  regmatch_t pmatch[1];
	  int i;
	  
#define CUR_ENV Context->hdrs[i]->env
	  for (i = 0; i < Context->msgcount; i++)
	  {
	    if (CUR_ENV && CUR_ENV->subject)
	    {
	      CUR_ENV->real_subj = (regexec (ReplyRegexp.rx,
				    CUR_ENV->subject, 1, pmatch, 0)) ?
				    CUR_ENV->subject : 
				    CUR_ENV->subject + pmatch[0].rm_eo;
	    }
	  }
#undef CUR_ENV
	}
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_MAGIC)
    {
      if (query || *s->dptr != '=')
      {
	switch (DefaultMagic)
	{
	  case M_MBOX:
	    p = "mbox";
	    break;
	  case M_MMDF:
	    p = "MMDF";
	    break;
	  case M_MH:
	    p = "MH";
	    break;
	  case M_MAILDIR:
	    p = "Maildir";
	    break;
	  default:
	    p = "unknown";
	    break;
	}
	snprintf (err->data, err->dsize, "%s=%s", MuttVars[idx].option, p);
	break;
      }

      s->dptr++;

      /* copy the value of the string */
      mutt_extract_token (tmp, s, 0);
      if (mx_set_magic (tmp->data))
      {
	snprintf (err->data, err->dsize, "%s: invalid mailbox type", tmp->data);
	r = -1;
	break;
      }
    }
    else if (DTYPE(MuttVars[idx].type) == DT_NUM)
    {
      short *ptr = (short *) MuttVars[idx].data;

      if (query || *s->dptr != '=')
      {
	/* user requested the value of this variable */
	snprintf (err->data, err->dsize, "%s=%d", MuttVars[idx].option, *ptr);
	break;
      }

      s->dptr++;

      mutt_extract_token (tmp, s, 0);
      *ptr = (short) atoi (tmp->data);

      /* these ones need a sanity check */
      if (strcmp (MuttVars[idx].option, "history") == 0)
      {
	if (*ptr < 0)
	  *ptr = 0;
	mutt_init_history ();
      }
      else if (strcmp (MuttVars[idx].option, "pager_index_lines") == 0)
      {
	if (*ptr < 0)
	  *ptr = 0;
      }
    }
    else if (DTYPE (MuttVars[idx].type) == DT_QUAD)
    {
      if (query)
      {
	char *vals[] = { "no", "yes", "ask-no", "ask-yes" };

	snprintf (err->data, err->dsize, "%s=%s", MuttVars[idx].option,
		  vals [ quadoption (MuttVars[idx].data) ]);
	break;
      }

      if (*s->dptr == '=')
      {
	s->dptr++;
	mutt_extract_token (tmp, s, 0);
	if (strcasecmp ("yes", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, M_YES);
	else if (strcasecmp ("no", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, M_NO);
	else if (strcasecmp ("ask-yes", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, M_ASKYES);
	else if (strcasecmp ("ask-no", tmp->data) == 0)
	  set_quadoption (MuttVars[idx].data, M_ASKNO);
	else
	{
	  snprintf (err->data, err->dsize, "%s: invalid value.", tmp->data);
	  r = -1;
	  break;
	}
      }
      else
      {
	if (inv)
	  toggle_quadoption (MuttVars[idx].data);
	else if (unset)
	  set_quadoption (MuttVars[idx].data, M_NO);
	else
	  set_quadoption (MuttVars[idx].data, M_YES);
      }
    }
    else if (DTYPE (MuttVars[idx].type) == DT_SORT)
    {
      const struct mapping_t *map;

      switch (MuttVars[idx].type & DT_SUBTYPE_MASK)
      {
	case DT_SORT_ALIAS:
	  map = SortAliasMethods;
	  break;
	case DT_SORT_BROWSER:
	  map = SortBrowserMethods;
	  break;
	default:
	  map = SortMethods;
	  break;
      }

      if (query || *s->dptr != '=')
      {
	p = mutt_getnamebyvalue (*((short *) MuttVars[idx].data) & SORT_MASK, map);

	snprintf (err->data, err->dsize, "%s=%s%s%s", MuttVars[idx].option,
		  (*((short *) MuttVars[idx].data) & SORT_REVERSE) ? "reverse-" : "",
		  (*((short *) MuttVars[idx].data) & SORT_LAST) ? "last-" : "",
		  p);
	return 0;
      }
      s->dptr++;
      mutt_extract_token (tmp, s , 0);

      if (parse_sort ((short *) MuttVars[idx].data, tmp->data, map, err) == -1)
	break;
    }
    else
    {
      snprintf (err->data, err->dsize, "%s: unknown type", MuttVars[idx].option);
      r = -1;
      break;
    }

    if (MuttVars[idx].flags & R_INDEX)
      set_option (OPTFORCEREDRAWINDEX);
    if (MuttVars[idx].flags & R_PAGER)
      set_option (OPTFORCEREDRAWPAGER);
    if (MuttVars[idx].flags & R_RESORT_SUB)
      set_option (OPTSORTSUBTHREADS);
    if (MuttVars[idx].flags & R_RESORT)
      set_option (OPTNEEDRESORT);
  }
  return (r);
}

void mutt_nocurses_error (const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
}

/* reads the specified initialization file.  returns -1 if errors were found
   so that we can pause to let the user know...  */
static int source_rc (const char *rcfile, BUFFER *err)
{
  FILE *f;
  int line = 0, rc = 0;
  BUFFER token;
  char *linebuf = NULL;
  size_t buflen;
  pid_t pid;

  if ((f = mutt_open_read (rcfile, &pid)) == NULL)
  {
    snprintf (err->data, err->dsize, "%s: %s", rcfile, strerror (errno));
    return (-1);
  }

  memset (&token, 0, sizeof (token));
  while ((linebuf = mutt_read_line (linebuf, &buflen, f, &line)) != NULL)
  {
    if (mutt_parse_rc_line (linebuf, &token, err) == -1)
    {
      mutt_error ("Error in %s, line %d: %s", rcfile, line, err->data);
      rc = -1;
    }
  }
  FREE (&token.data);
  safe_free ((void **) &linebuf);
  fclose (f);
  if (pid != -1)
    mutt_wait_filter (pid);
  if (rc)
    snprintf (err->data, err->dsize, "source: errors in %s", rcfile);
  return (rc);
}

static int parse_source (BUFFER *tmp, BUFFER *s, unsigned long data, BUFFER *err)
{
  char path[_POSIX_PATH_MAX];

  if (mutt_extract_token (tmp, s, 0) != 0)
  {
    snprintf (err->data, err->dsize, "source: error at %s", s->dptr);
    return (-1);
  }
  if (MoreArgs (s))
  {
    strfcpy (err->data, "source: too many arguments", err->dsize);
    return (-1);
  }
  strfcpy (path, tmp->data, sizeof (path));
  mutt_expand_path (path, sizeof (path));
  return (source_rc (path, err));
}

/* line		command to execute

   token	scratch buffer to be used by parser.  caller should free
   		token->data when finished.  the reason for this variable is
		to avoid having to allocate and deallocate a lot of memory
		if we are parsing many lines.  the caller can pass in the
		memory to use, which avoids having to create new space for
		every call to this function.

   err		where to write error messages */
int mutt_parse_rc_line (/* const */ char *line, BUFFER *token, BUFFER *err)
{
  int i, r = -1;
  BUFFER expn;

  memset (&expn, 0, sizeof (expn));
  expn.data = expn.dptr = line;
  expn.dsize = strlen (line);

  *err->data = 0;

  SKIPWS (expn.dptr);
  while (*expn.dptr)
  {
    if (*expn.dptr == '#')
      break; /* rest of line is a comment */
    if (*expn.dptr == ';')
    {
      expn.dptr++;
      continue;
    }
    mutt_extract_token (token, &expn, 0);
    for (i = 0; Commands[i].name; i++)
    {
      if (!strcmp (token->data, Commands[i].name))
      {
	if (Commands[i].func (token, &expn, Commands[i].data, err) != 0)
	  goto finish;
        break;
      }
    }
    if (!Commands[i].name)
    {
      snprintf (err->data, err->dsize, "%s: unknown command", token->data);
      goto finish;
    }
  }
  r = 0;
finish:
  if (expn.destroy)
    FREE (&expn.data);
  return (r);
}

/* helper function for completion.  Changes the dest buffer if
   necessary/possible to aid completion.
	dest == completion result gets here.
	src == candidate for completion.
	try == user entered data for completion.
	len == length of dest buffer.
*/
static void candidate (char *dest, char *try, char *src, int len)
{
  int l;

  if (strstr (src, try) == src)
  {
    if (dest[0] == 0)
    {
      strncpy (dest, src, len);
      strncat (dest, " ", len);
    }
    else
    {
      for (l = 0; src[l] && src[l] == dest[l]; l++);
	dest[l] = 0;
    }
  }
}

int mutt_command_complete (char *buffer, size_t len, int pos)
{
  char cmd[STRING];
  char completed[STRING] = { 0 };
  char *pt;
  int num;
  
  if (buffer[0] == 0)
    return 0;
  strncpy (cmd, buffer, pos);
  pt = cmd;
  pt[pos] = 0;
  while (!isspace ((unsigned char) *pt))
    pt++;
  *pt = 0;

  pt = buffer + pos;
  while ((pt > buffer) && !isspace ((unsigned char) *pt))
    pt--;
  if (pt == buffer) /* complete cmd */
  {
    for (num = 0; Commands[num].name; num++)
      candidate (completed, cmd, Commands[num].name, sizeof (completed));

    if (completed[0] == 0)
      return 0;
    strncpy (buffer, completed, len);
  }
  else if (!strncasecmp (cmd, "set", 3)
	   || !strncasecmp (cmd, "unset", 5)
	   || !strncasecmp (cmd, "toggle", 6))
  { 		/* complete variables */
    pt++;
    if (*pt == 0)
      return 0;
    strncpy (cmd, pt, sizeof (cmd));
    for (num = 0; MuttVars[num].option; num++)
      candidate (completed, cmd, MuttVars[num].option, sizeof (completed));
    if (completed[0] == 0)
      return 0;
    strncpy (pt, completed, buffer + len - pt);
  }
  else
    return 0;
  return 1;
}
#endif

char *mutt_getnamebyvalue (int val, const struct mapping_t *map)
{
  int i;

  for (i=0; map[i].name; i++)
    if (map[i].value == val)
      return (map[i].name);
  return NULL;
}

int mutt_getvaluebyname (const char *name, const struct mapping_t *map)
{
  int i;

  for (i = 0; map[i].name; i++)
    if (strcasecmp (map[i].name, name) == 0)
      return (map[i].value);
  return (-1);
}

#ifndef LIBMUTT
#ifdef DEBUG
static void start_debug (void)
{
  time_t t;
  int i;
  char buf[_POSIX_PATH_MAX];
  char buf2[_POSIX_PATH_MAX];

  /* rotate the old debug logs */
  for (i=3; i>=0; i--)
  {
    snprintf (buf, sizeof(buf), "%s/.muttdebug%d", Homedir, i);
    snprintf (buf2, sizeof(buf2), "%s/.muttdebug%d", Homedir, i+1);
    rename (buf, buf2);
  }
  if ((debugfile = safe_fopen(buf, "w")) != NULL)
  {
    t = time (0);
    fprintf (debugfile, "Mutt %s started at %s.\nDebugging at level %d.\n\n",
	     VERSION, asctime (localtime (&t)), debuglevel);
  }
}
#endif

static int mutt_execute_commands (LIST *p)
{
  BUFFER err, token;
  char errstr[SHORT_STRING];

  memset (&err, 0, sizeof (err));
  err.data = errstr;
  err.dsize = sizeof (errstr);
  memset (&token, 0, sizeof (token));
  for (; p; p = p->next)
  {
    if (mutt_parse_rc_line (p->data, &token, &err) != 0)
    {
      fprintf (stderr, "Error in command line: %s\n", err.data);
      FREE (&token.data);
      return (-1);
    }
  }
  FREE (&token.data);
  return 0;
}

void mutt_init (int skip_sys_rc, LIST *commands)
{
  struct passwd *pw;
  struct utsname utsname;
  char *p, buffer[STRING], error[STRING];
  int i, default_rc = 0, need_pause = 0;
  BUFFER err;

  memset (&err, 0, sizeof (err));
  err.data = error;
  err.dsize = sizeof (error);

  /* on one of the systems I use, getcwd() does not return the same prefix
     as is listed in the passwd file */
  if ((p = getenv ("HOME")))
    Homedir = safe_strdup (p);

  /* Get some information about the user */
  if ((pw = getpwuid (getuid ())))
  {
    Username = safe_strdup (pw->pw_name);
    if (!Homedir)
      Homedir = safe_strdup (pw->pw_dir);
    if ((p = strchr (pw->pw_gecos, ',')))
      Realname = mutt_substrdup (pw->pw_gecos, p);
    else
      Realname = safe_strdup (pw->pw_gecos);
    Shell = safe_strdup (pw->pw_shell);
  }
  else 
  {
    if (!Homedir)
    {
      mutt_endwin (NULL);
      fputs ("unable to determine home directory", stderr);
      exit (1);
    }
    if ((p = getenv ("USER")))
      Username = safe_strdup (p);
    else
    {
      mutt_endwin (NULL);
      fputs ("unable to determine user", stderr);
      exit (1);
    }
    Shell = safe_strdup ((p = getenv ("SHELL")) ? p : "/bin/sh");
  }

#ifdef DEBUG
  /* Start up debugging mode if requested */
  if (debuglevel > 0)
    start_debug ();
#endif

  /* And about the host... */
  uname (&utsname);
  /* some systems report the FQDN instead of just the hostname */
  if ((p = strchr (utsname.nodename, '.')))
  {
    Hostname = mutt_substrdup (utsname.nodename, p);
    p++;
    strfcpy (buffer, p, sizeof (buffer)); /* save the domain for below */
  }
  else
    Hostname = safe_strdup (utsname.nodename);

#ifndef DOMAIN
#define DOMAIN buffer
  if (!p && getdnsdomainname (buffer, sizeof (buffer)) == -1)
    Fqdn = safe_strdup ("@");
  else
#endif /* DOMAIN */
  {
# ifdef HIDDEN_HOST
    Fqdn = safe_strdup (DOMAIN);
# else
    Fqdn = safe_malloc (strlen (DOMAIN) + strlen (Hostname) + 2);
    sprintf (Fqdn, "%s.%s", Hostname, DOMAIN);
# endif /* HIDDEN_HOST */
  }

  if ((p = getenv ("MAIL")))
    Spoolfile = safe_strdup (p);
  else
  {
#ifdef HOMESPOOL
    snprintf (buffer, sizeof (buffer), "%s/%s", Homedir, MAILPATH);
#else
    snprintf (buffer, sizeof (buffer), "%s/%s", MAILPATH, Username);
#endif
    Spoolfile = safe_strdup (buffer);
  }

  if ((p = getenv ("MAILCAPS")))
    MailcapPath = safe_strdup (p);
  else
  {
    /* Default search path from RFC1524 */
    MailcapPath = safe_strdup ("~/.mailcap:" SHAREDIR "/mailcap:/etc/mailcap:/usr/etc/mailcap:/usr/local/etc/mailcap");
  }

  Tempdir = safe_strdup ((p = getenv ("TMPDIR")) ? p : "/tmp");

  

  
  

#ifdef USE_POP
  PopUser = safe_strdup (Username);
#endif

  Editor = safe_strdup ((p = getenv ("EDITOR")) ? p : "vi");
  Visual = safe_strdup ((p = getenv ("VISUAL")) ? p : Editor);

  if ((p = getenv ("REPLYTO")) != NULL)
  {
    BUFFER buf, token;

    snprintf (buffer, sizeof (buffer), "Reply-To: %s", p);

    memset (&buf, 0, sizeof (buf));
    buf.data = buf.dptr = buffer;
    buf.dsize = strlen (buffer);

    memset (&token, 0, sizeof (token));
    parse_my_hdr (&token, &buf, 0, &err);
    FREE (&token.data);
  }

  /* Set standard defaults */
  for (i = 0; MuttVars[i].option; i++)
    mutt_restore_default (&MuttVars[i]);

#ifndef LOCALES_HACK
  /* Do we have a locale definition? */
  if (((p = getenv ("LC_ALL")) != NULL && p[0]) ||
      ((p = getenv ("LANG")) != NULL && p[0]) ||
      ((p = getenv ("LC_CTYPE")) != NULL && p[0]))
    set_option (OPTLOCALES);
#endif

  mutt_init_history ();

  if (!Muttrc)
  {
    snprintf (buffer, sizeof (buffer), "%s/.muttrc-%s", Homedir, VERSION);
    if (access (buffer, F_OK) == -1)
      snprintf (buffer, sizeof (buffer), "%s/.muttrc", Homedir);
    default_rc = 1;
    Muttrc = safe_strdup (buffer);
  }
  else
  {
    strfcpy (buffer, Muttrc, sizeof (buffer));
    FREE (&Muttrc);
    mutt_expand_path (buffer, sizeof (buffer));
    Muttrc = safe_strdup (buffer);
  }
  FREE (&AliasFile);
  AliasFile = safe_strdup (Muttrc);

  /* Process the global rc file if it exists and the user hasn't explicity
     requested not to via "-n".  */
  if (!skip_sys_rc)
  {
    snprintf (buffer, sizeof (buffer), "%s/Muttrc-%s", SHAREDIR, VERSION);
    if (access (buffer, F_OK) == -1)
      snprintf (buffer, sizeof (buffer), "%s/Muttrc", SHAREDIR);
    if (access (buffer, F_OK) != -1)
    {
      if (source_rc (buffer, &err) != 0)
      {
	fputs (err.data, stderr);
	fputc ('\n', stderr);
	need_pause = 1;
      }
    }
  }

  /* Read the user's initialization file.  */
  if (access (Muttrc, F_OK) != -1)
  {
    if (!option (OPTNOCURSES))
      endwin ();
    if (source_rc (Muttrc, &err) != 0)
    {
      fputs (err.data, stderr);
      fputc ('\n', stderr);
      need_pause = 1;
    }
  }
  else if (!default_rc)
  {
    /* file specified by -F does not exist */
    snprintf (buffer, sizeof (buffer), "%s: %s", Muttrc, strerror (errno));
    mutt_endwin (buffer);
    exit (1);
  }

  if (mutt_execute_commands (commands) != 0)
    need_pause = 1;

  if (need_pause && !option (OPTNOCURSES))
  {
    if (mutt_any_key_to_continue (NULL) == -1)
      mutt_exit(1);
  }

  set_option (OPTWEED); /* turn weeding on by default */
}
#endif
