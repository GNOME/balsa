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
#include "mutt_regex.h"
#include "pattern.h"

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

typedef struct hook
{
  int type;		/* hook type */
  REGEXP rx;		/* regular expression */
  char * command;	/* filename, command or pattern to execute */
  pattern_t *pattern;	/* used for fcc,save,send-hook */
  struct hook *next;
} HOOK;

static HOOK *Hooks = NULL;

int mutt_parse_hook (const char *s, unsigned long data, char *err, size_t errlen)
{
  HOOK *ptr;
  char expn[LONG_STRING];
  char command[LONG_STRING];
  char pattern[STRING];
  int type = data;
  regex_t *rx = NULL;
  pattern_t *pat = NULL;
  int rc, not = 0;

  if (*s == '!')
  {
    s++;
    SKIPWS (s);
    not = 1;
  }

  s = mutt_extract_token (pattern, sizeof (pattern), s, expn, sizeof (expn), 0);

  if (!s)
  {
    strfcpy (err, "too few arguments", errlen);
    return (-1);
  }

  s = mutt_extract_token (command, sizeof (command), s, expn, sizeof (expn),
			  (type & (M_FOLDERHOOK | M_SENDHOOK)) ?  M_SPACE : 0);

  if (!*command)
  {
    strfcpy (err, "too few arguments", errlen);
    return (-1);
  }

  if (s)
  {
    strfcpy (err, "too many arguments", errlen);
    return (-1);
  }

  if (type & (M_FOLDERHOOK | M_MBOXHOOK))
    mutt_expand_path (pattern, sizeof (pattern));
  if (type & (M_MBOXHOOK | M_SAVEHOOK | M_FCCHOOK))
    mutt_expand_path (command, sizeof (command));

  /* check to make sure that a matching hook doesn't already exist */
  for (ptr = Hooks; ptr; ptr = ptr->next)
  {
    if (ptr->type == type && ptr->rx.not == not &&
	!strcmp (pattern, ptr->rx.pattern) &&
	!strcmp (command, ptr->command))
      return 0; /* skip it */
    if (!ptr->next)
      break;
  }

  if (type & (M_SENDHOOK | M_SAVEHOOK | M_FCCHOOK))
  {
    mutt_check_simple (pattern, sizeof (pattern), DefaultHook);
    if ((pat = mutt_pattern_comp (pattern, 0, err, errlen)) == NULL)
    {
      return (-1);
    }
  }
  else
  {
    rx = safe_malloc (sizeof (regex_t));
    if ((rc = REGCOMP (rx, pattern, 0)) != 0)
    {
      regerror (rc, rx, err, errlen);
      regfree (rx);
      safe_free ((void **) &rx);
      return (-1);
    }
  }

  if (ptr)
  {
    ptr->next = safe_calloc (1, sizeof (HOOK));
    ptr = ptr->next;
  }
  else
    Hooks = ptr = safe_calloc (1, sizeof (HOOK));

  ptr->type = type;
  ptr->command = safe_strdup (command);
  ptr->pattern = pat;
  ptr->rx.pattern = safe_strdup (pattern);
  ptr->rx.rx = rx;
  ptr->rx.not = not;
  return 0;
}

void mutt_folder_hook (char *path)
{
  HOOK *tmp = Hooks;
  char err[SHORT_STRING];

  for (; tmp; tmp = tmp->next)
    if (tmp->type & M_FOLDERHOOK)
    {
      if ((regexec (tmp->rx.rx, path, 0, NULL, 0) == 0) ^ tmp->rx.not)
      {
	if (mutt_parse_rc_line (tmp->command, err, sizeof (err)) == -1)
	{
	  mutt_error ("%s", err);
	  sleep (1);	/* pause a moment to let the user see the error */
	  return;
	}
      }
    }
}

char *mutt_find_hook (int type, const char *pat)
{
  HOOK *tmp = Hooks;

  for (; tmp; tmp = tmp->next)
    if (tmp->type & type)
    {
      if (regexec (tmp->rx.rx, pat, 0, NULL, 0) == 0)
	return (tmp->command);
    }
  return (NULL);
}

void mutt_send_hook (HEADER *hdr)
{
  char err[SHORT_STRING];
  HOOK *hook;

  for (hook = Hooks; hook; hook = hook->next)
    if (hook->type & M_SENDHOOK)
      if ((mutt_pattern_exec (hook->pattern, 0, NULL, hdr) > 0) ^ hook->rx.not)
	if (mutt_parse_rc_line (hook->command, err, sizeof (err)) != 0)
	{
	  mutt_error ("%s", err);
	  sleep (1);
	  return;
	}
}

static int mutt_addr_hook (char *path,
			   size_t pathlen,
			   int type,
			   CONTEXT *ctx,
			   HEADER *hdr)
{
  HOOK *hook;
  
  /* determine if a matching hook exists */
  for (hook = Hooks; hook; hook = hook->next)
    if (hook->type & type)
      if ((mutt_pattern_exec (hook->pattern, 0, ctx, hdr) > 0) ^ hook->rx.not)
      {
	strfcpy (path, hook->command, pathlen);
	return 0;
      }

  return -1;
}

void mutt_default_save (char *path, size_t pathlen, HEADER *hdr)
{
  char tmp[_POSIX_PATH_MAX];
  ADDRESS *adr;
  ENVELOPE *env = hdr->env;
  
  if (mutt_addr_hook (path, pathlen, M_SAVEHOOK, Context, hdr) == 0)
    return;

  /* check to see if this is a mailing list */
  for (adr = env->to; adr; adr = adr->next)
    if (mutt_is_mail_list (adr))
      break;

  /* check the CC: list */
  if (!adr)
  {
    for (adr = env->cc; adr; adr = adr->next)
      if (mutt_is_mail_list (adr))
	break;
  }

  /* pick default based upon sender */
  if (!adr)
  {
    int fromMe = mutt_addr_is_user (env->from);

    if (!fromMe && env->reply_to && env->reply_to->mailbox)
      adr = env->reply_to;
    else if (!fromMe && env->from && env->from->mailbox)
      adr = env->from;
    else if (env->to && env->to->mailbox)
      adr = env->to;
    else if (env->cc && env->cc->mailbox)
      adr = env->cc;
  }

  mutt_safe_path (tmp, sizeof (tmp), adr);
  snprintf (path, pathlen, "=%s", tmp);
}

void mutt_select_fcc (char *path, size_t pathlen, HEADER *hdr)
{
  ADDRESS *adr;
  char buf[_POSIX_PATH_MAX];
  ENVELOPE *env = hdr->env;

  if (mutt_addr_hook (path, pathlen, M_FCCHOOK, NULL, hdr) != 0)
  {
    if ((option (OPTSAVENAME) || option (OPTFORCENAME)) &&
	(env->to || env->cc || env->bcc))
    {
      adr = env->to ? env->to : (env->cc ? env->cc : env->bcc);
      mutt_safe_path (buf, sizeof (buf), adr);
      snprintf (path, pathlen, "%s/%s", Maildir, buf);
      if (!option (OPTFORCENAME) && access (path, W_OK) != 0)
	strfcpy (path, Outbox, pathlen);
    }
    else
      strfcpy (path, Outbox, pathlen);
  }
  mutt_pretty_mailbox (path);
}
