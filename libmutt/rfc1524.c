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
 * rfc1524 defines a format for the Multimedia Mail Configuration, which
 * is the standard mailcap file format under Unix which specifies what 
 * external programs should be used to view/compose/edit multimedia files
 * based on content type.
 *
 * This file contains various functions for implementing a fair subset of 
 * rfc1524.
 */

#include "mutt.h"
#include "rfc1524.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>

/* The command semantics include the following:
 * %s is the filename that contains the mail body data
 * %t is the content type, like text/plain
 * %{parameter} is replaced by the parameter value from the content-type field
 * \% is %
 * Unsupported rfc1524 parameters: these would probably require some doing
 * by mutt, and can probably just be done by piping the message to metamail
 * %n is the integer number of sub-parts in the multipart
 * %F is "content-type filename" repeated for each sub-part
 *
 * In addition, this function returns a 0 if the command works on a file,
 * and 1 if the command works on a pipe.
 */
int rfc1524_expand_command (BODY *a, char *filename, char *_type,
    char *command, int clen)
{
  int x=0,y=0;
  int needspipe = MUTT_TRUE;
  char buf[LONG_STRING];
  char type[LONG_STRING];
  
  strfcpy (type, _type, sizeof (type));
  
  if (option (OPTMAILCAPSANITIZE))
    mutt_sanitize_filename (type, 0);

  while (command[x] && x<clen && y<sizeof(buf)) 
  {
    if (command[x] == '\\') {
      x++;
      buf[y++] = command[x++];
    }
    else if (command[x] == '%') 
    {
      x++;
      if (command[x] == '{') 
      {
	char param[STRING];
	char pvalue[LONG_STRING];
	int z = 0;
	char *ret = NULL;
	char *pv;

	x++;
	while (command[x] && command[x] != '}' && z<sizeof(param))
	  param[z++] = command[x++];
	param[z] = '\0';
	dprint(2,(debugfile,"Parameter: %s  Returns: %s\n",param,ret));
	pv = mutt_get_parameter (param, a->parameter);
	strfcpy (pvalue, NONULL(pv), sizeof (pvalue));
	if (option (OPTMAILCAPSANITIZE)) 
	  mutt_sanitize_filename (pvalue, 0);
	ret = mutt_quote_filename (pvalue);
	dprint(2,(debugfile,"Parameter: %s  Returns: %s\n",param,ret));
	z = 0;
	while (ret && ret[z] && y<sizeof(buf))
	  buf[y++] = ret[z++];
	FREE(&ret);
      }
      else if (command[x] == 's' && filename != NULL)
      {
	char *fn = mutt_quote_filename(filename);
	int i;
	
	for(i = 0; fn[i] && y < sizeof(buf); i++)
	  buf[y++] = fn[i];
	
	FREE(&fn);
	needspipe = MUTT_FALSE;
      }
      else if (command[x] == 't')
      {
	char *t = mutt_quote_filename (type);
	char *s;
	
	for (s = t; *s && y < sizeof (buf);)
	  buf[y++] = *s++;
	FREE (&t);
      }
      x++;
    }
    else
      buf[y++] = command[x++];
  }
  buf[y] = '\0';
  strfcpy (command, buf, clen);

  return needspipe;
}

/* NUL terminates a rfc 1524 field,
 * returns start of next field or NULL */
static char *get_field (char *s)
{
  char *ch;

  if (!s)
    return NULL;

  while ((ch = strpbrk (s, ";\\")) != NULL)
  {
    if (*ch == '\\')
    {
      s = ch + 1;
      if (*s)
	s++;
    }
    else
    {
      *ch++ = 0;
      SKIPWS (ch);
      break;
    }
  }
  mutt_remove_trailing_ws (s);
  return ch;
}

static int get_field_text (char *field, char **entry,
			   char *type, char *filename, int line)
{
  field = mutt_skip_whitespace (field);
  if (*field == '=')
  {
    if (entry)
    {
      field++;
      field = mutt_skip_whitespace (field);
      safe_free ((void **) entry);
      *entry = safe_strdup (field);
    }
    return 1;
  }
  else 
  {
    mutt_error ("Improperly formated entry for type %s in \"%s\" line %d",
		type, filename, line);
    return 0;
  }
}

static int rfc1524_mailcap_parse (BODY *a,
				  char *filename,
				  char *type, 
				  rfc1524_entry *entry,
				  int opt)
{
  FILE *fp;
  char *buf = NULL;
  size_t buflen;
  char *ch;
  char *field;
  int found = MUTT_FALSE;
  int copiousoutput;
  int composecommand;
  int editcommand;
  int printcommand;
  int btlen;
  int line = 0;

  /* rfc1524 mailcap file is of the format:
   * base/type; command; extradefs
   * type can be * for matching all
   * base with no /type is an implicit wild
   * command contains a %s for the filename to pass, default to pipe on stdin
   * extradefs are of the form:
   *  def1="definition"; def2="define \;";
   * line wraps with a \ at the end of the line
   * # for comments
   */

  /* find length of basetype */
  if ((ch = strchr (type, '/')) == NULL)
    return MUTT_FALSE;
  btlen = ch - type;

  if ((fp = fopen (filename, "r")) != NULL)
  {
    while (!found && (buf = mutt_read_line (buf, &buflen, fp, &line)) != NULL)
    {
      /* ignore comments */
      if (*buf == '#')
	continue;
      dprint (2, (debugfile, "mailcap entry: %s\n", buf));

      /* check type */
      ch = get_field (buf);
      if (mutt_strcasecmp (buf, type) &&
	  (mutt_strncasecmp (buf, type, btlen) ||
	   (buf[btlen] != 0 &&			/* implicit wild */
	    mutt_strcmp (buf + btlen, "/*"))))	/* wildsubtype */
	continue;

      /* next field is the viewcommand */
      field = ch;
      ch = get_field (ch);
      if (entry)
	entry->command = safe_strdup (field);

      /* parse the optional fields */
      found = MUTT_TRUE;
      copiousoutput = MUTT_FALSE;
      composecommand = MUTT_FALSE;
      editcommand = MUTT_FALSE;
      printcommand = MUTT_FALSE;

      while (ch)
      {
	field = ch;
	ch = get_field (ch);
	dprint (2, (debugfile, "field: %s\n", field));

	if (!mutt_strcasecmp (field, "needsterminal"))
	{
	  if (entry)
	    entry->needsterminal = MUTT_TRUE;
	}
	else if (!mutt_strcasecmp (field, "copiousoutput"))
	{
	  copiousoutput = MUTT_TRUE;
	  if (entry)
	    entry->copiousoutput = MUTT_TRUE;
	}
	else if (!mutt_strncasecmp (field, "composetyped", 12))
	{
	  /* this compare most occur before compose to match correctly */
	  if (get_field_text (field + 12, entry ? &entry->composetypecommand : NULL,
			      type, filename, line))
	    composecommand = MUTT_TRUE;
	}
	else if (!mutt_strncasecmp (field, "compose", 7))
	{
	  if (get_field_text (field + 7, entry ? &entry->composecommand : NULL,
			      type, filename, line))
	    composecommand = MUTT_TRUE;
	}
	else if (!mutt_strncasecmp (field, "print", 5))
	{
	  if (get_field_text (field + 5, entry ? &entry->printcommand : NULL,
			      type, filename, line))
	    printcommand = MUTT_TRUE;
	}
	else if (!mutt_strncasecmp (field, "edit", 4))
	{
	  if (get_field_text (field + 4, entry ? &entry->editcommand : NULL,
			      type, filename, line))
	    editcommand = MUTT_TRUE;
	}
	else if (!mutt_strncasecmp (field, "nametemplate", 12))
	{
	  get_field_text (field + 12, entry ? &entry->nametemplate : NULL,
			  type, filename, line);
	}
	else if (!mutt_strncasecmp (field, "x-convert", 9))
	{
	  get_field_text (field + 9, entry ? &entry->convert : NULL,
			  type, filename, line);
	}
	else if (!mutt_strncasecmp (field, "test", 4))
	{
	  /* 
	   * This routine executes the given test command to determine
	   * if this is the right entry.
	   */
	  char *test_command = NULL;
	  size_t len;

	  if (get_field_text (field + 4, &test_command, type, filename, line)
	      && test_command)
	  {
	    len = mutt_strlen (test_command) + STRING;
	    safe_realloc ((void **) &test_command, len);
	    rfc1524_expand_command (a, NULL, type, test_command, len);
	    if (mutt_system (test_command))
	    {
	      /* a non-zero exit code means test failed */
	      found = MUTT_FALSE;
	    }
	    FREE (&test_command);
	  }
	}
      } /* while (ch) */

      if (opt == M_AUTOVIEW)
      {
	if (!copiousoutput)
	  found = MUTT_FALSE;
      }
      else if (opt == M_COMPOSE)
      {
	if (!composecommand)
	  found = MUTT_FALSE;
      }
      else if (opt == M_EDIT)
      {
	if (!editcommand)
	  found = MUTT_FALSE;
      }
      else if (opt == M_PRINT)
      {
	if (!printcommand)
	  found = MUTT_FALSE;
      }
      
      if (!found)
      {
	/* reset */
	if (entry)
	{
	  safe_free ((void **) &entry->command);
	  safe_free ((void **) &entry->composecommand);
	  safe_free ((void **) &entry->composetypecommand);
	  safe_free ((void **) &entry->editcommand);
	  safe_free ((void **) &entry->printcommand);
	  safe_free ((void **) &entry->nametemplate);
	  safe_free ((void **) &entry->convert);
	  entry->needsterminal = 0;
	  entry->copiousoutput = 0;
	}
      }
    } /* while (!found && (buf = mutt_read_line ())) */
    fclose (fp);
  } /* if ((fp = fopen ())) */
  safe_free ((void **) &buf);
  return found;
}

rfc1524_entry *rfc1524_new_entry(void)
{
  return (rfc1524_entry *)safe_calloc(1, sizeof(rfc1524_entry));
}

void rfc1524_free_entry(rfc1524_entry **entry)
{
  rfc1524_entry *p = *entry;

  safe_free((void **)&p->command);
  safe_free((void **)&p->testcommand);
  safe_free((void **)&p->composecommand);
  safe_free((void **)&p->composetypecommand);
  safe_free((void **)&p->editcommand);
  safe_free((void **)&p->printcommand);
  safe_free((void **)&p->nametemplate);
  safe_free((void **)entry);
}

/*
 * rfc1524_mailcap_lookup attempts to find the given type in the
 * list of mailcap files.  On success, this returns the entry information
 * in *entry, and returns 1.  On failure (not found), returns 0.
 * If entry == NULL just return 1 if the given type is found.
 */
int rfc1524_mailcap_lookup (BODY *a, char *type, rfc1524_entry *entry, int opt)
{
  char path[_POSIX_PATH_MAX];
  int x;
  int found = MUTT_FALSE;
  char *curr = MailcapPath;

  /* rfc1524 specifies that a path of mailcap files should be searched.
   * joy.  They say 
   * $HOME/.mailcap:/etc/mailcap:/usr/etc/mailcap:/usr/local/etc/mailcap, etc
   * and overriden by the MAILCAPS environment variable, and, just to be nice, 
   * we'll make it specifiable in .muttrc
   */
  if (!curr || !*curr)
  {
    mutt_error ("No mailcap path specified");
    return 0;
  }

  while (!found && *curr)
  {
    x = 0;
    while (*curr && *curr != ':' && x < sizeof (path) - 1)
    {
      path[x++] = *curr;
      curr++;
    }
    if (*curr)
      curr++;

    if (!x)
      continue;
    
    path[x] = '\0';
    mutt_expand_path (path, sizeof (path));

    dprint(2,(debugfile,"Checking mailcap file: %s\n",path));
    found = rfc1524_mailcap_parse (a, path, type, entry, opt);
  }

  if (entry && !found)
    mutt_error ("mailcap entry for type %s not found", type);

  return found;
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
    snprintf (s, l, "%s/muttXXXXXX", buf);
    mkstemp (s);
  }
  else
  {
    strfcpy (tmp, s, sizeof (tmp));
    snprintf (s, l, "%s/%s", buf, tmp);
    if (lstat (s, &sb) == -1 && errno == ENOENT)
      return;
    if ((period = strrchr (tmp, '.')) != NULL)
      *period = 0;
    snprintf (s, l, "%s/%s.XXXXXX", buf, tmp);
    mkstemp (s);
    if (period != NULL)
    {
      *period = '.';
      sl = mutt_strlen(s);
      strfcpy(s + sl, period, l - sl);
    }
  }
}

/* This routine will create a _temporary_ filename matching the
 * name template given if this needs to be done.
 * 
 * Please note that only the last path element of the
 * template and/or the old file name will be used for the
 * comparison and the temporary file name.
 * 
 * Returns 0 if oldfile is fine as is.
 * Returns 1 if newfile specified
 */

static void strnfcpy(char *d, char *s, size_t siz, size_t len)
{
  if(len > siz)
    len = siz - 1;
  strfcpy(d, s, len);
}

int rfc1524_expand_filename (char *nametemplate,
			     char *oldfile, 
			     char *newfile,
			     size_t nflen)
{
  int i, j, k, ps, r;
  char *s;
  short lmatch = 0, rmatch = 0; 
  char left[_POSIX_PATH_MAX];
  char right[_POSIX_PATH_MAX];
  
  newfile[0] = 0;

  /* first, ignore leading path components.
   */
  
  if (nametemplate && (s = strrchr (nametemplate, '/')))
    nametemplate = s + 1;

  if (oldfile && (s = strrchr (oldfile, '/')))
    oldfile = s + 1;
    
  if (!nametemplate)
  {
    if (oldfile)
      strfcpy (newfile, oldfile, nflen);
  }
  else if (!oldfile)
  {
    snprintf (newfile, nflen, nametemplate, "mutt");
  }
  else /* oldfile && nametemplate */
  {

    /* first, compare everything left from the "%s" 
     * (if there is one).
     */
    
    lmatch = 1; ps = 0;
    for(i = 0; nametemplate[i]; i++)
    {
      if(nametemplate[i] == '%' && nametemplate[i+1] == 's')
      { 
	ps = 1;
	break;
      }

      /* note that the following will _not_ read beyond oldfile's end. */

      if(lmatch && nametemplate[i] != oldfile[i])
	lmatch = 0;
    }

    if(ps)
    {
      
      /* If we had a "%s", check the rest. */
      
      /* now, for the right part: compare everything right from 
       * the "%s" to the final part of oldfile.
       * 
       * The logic here is as follows:
       * 
       * - We start reading from the end.
       * - There must be a match _right_ from the "%s",
       *   thus the i + 2.  
       * - If there was a left hand match, this stuff
       *   must not be counted again.  That's done by the
       *   condition (j >= (lmatch ? i : 0)).
       */
      
      rmatch = 1;

      for(r = 0, j = mutt_strlen(oldfile) - 1, k = mutt_strlen(nametemplate) - 1 ;
	  j >= (lmatch ? i : 0) && k >= i + 2;
	  j--, k--)
      {
	if(nametemplate[k] != oldfile[j])
	{
	  rmatch = 0;
	  break;
	}
      }
      
      /* Now, check if we had a full match. */
      
      if(k >= i + 2)
	rmatch = 0;
      
      if(lmatch) *left = 0;
      else strnfcpy(left, nametemplate, sizeof(left), i);
      
      if(rmatch) *right = 0;
      else strfcpy(right, nametemplate + i + 2, sizeof(right));
      
      snprintf(newfile, nflen, "%s%s%s", left, oldfile, right);
    }
    else
    {
      /* no "%s" in the name template. */
      strfcpy(newfile, nametemplate, nflen);
    }
  }
  
  mutt_adv_mktemp(newfile, nflen);

  if(rmatch && lmatch)
    return 0;
  else 
    return 1;
  
}

/* If rfc1524_expand_command() is used on a recv'd message, then
 * the filename doesn't exist yet, but if its used while sending a message,
 * then we need to rename the existing file.
 *
 * This function returns 0 on successful move, 1 on old file doesn't exist,
 * 2 on new file already exists, and 3 on other failure.
 */

/* note on access(2) use: No dangling symlink problems here due to
 * safe_fopen().
 */

int mutt_rename_file (char *oldfile, char *newfile)
{
  FILE *ofp, *nfp;

  if (access (oldfile, F_OK) != 0)
    return 1;
  if (access (newfile, F_OK) == 0)
    return 2;
  if ((ofp = fopen (oldfile,"r")) == NULL)
    return 3;
  if ((nfp = safe_fopen (newfile,"w")) == NULL)
  {
    fclose(ofp);
    return 3;
  }
  mutt_copy_stream (ofp,nfp);
  fclose (nfp);
  fclose (ofp);
  mutt_unlink (oldfile);
  return 0;
}
