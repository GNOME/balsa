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
#include "rfc2047.h"
#include "mx.h"
#include "mime.h"
#include "mailbox.h"
#include "copy.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sysexits.h>






#define DISPOSITION(X) X==DISPATTACH?"attachment":"inline"

const char MimeSpecials[] = "@.,;<>[]\\\"()?/=";

char B64Chars[64] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
  'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
  't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', '+', '/'
};

static char MsgIdPfx = 'A';

static void transform_to_7bit (BODY *a, FILE *fpin);

static void encode_quoted (FILE * fin, FILE *fout, int istext)
{
  int c, linelen = 0;
  char line[77], savechar;

  while ((c = fgetc (fin)) != EOF)
  {
    /* Escape lines that begin with "the message separator". */
    if (linelen == 5 && !strncmp ("From ", line, 5))
    {
      strfcpy (line, "=46rom ", sizeof (line));
      linelen = 7;
    }
    else if (linelen == 1 && line[0] == '.')
    {
      strfcpy (line, "=2E", sizeof (line));
      linelen = 3;
    }

    /* Wrap the line if needed. */
    if (linelen == 76 && ((istext && c != '\n') || !istext))
    {
      /* If the last character is "quoted", then be sure to move all three
       * characters to the next line.  Otherwise, just move the last
       * character...
       */
      if (line[linelen-3] == '=')
      {
        line[linelen-3] = 0;
        fputs (line, fout);      
        fputs ("=\n", fout);
        line[linelen] = 0;
        line[0] = '=';
        line[1] = line[linelen-2];
        line[2] = line[linelen-1];
        linelen = 3;
      }
      else
      {
        savechar = line[linelen-1];
        line[linelen-1] = '=';
        line[linelen] = 0;
        fputs (line, fout);
        fputc ('\n', fout);
        line[0] = savechar;
        linelen = 1;
      }
    }

    if (c == '\n' && istext)
    {
      /* Check to make sure there is no trailing space on this line. */
      if (line[linelen-1] == ' ' || line[linelen-1] == '\t')
      {
        if (linelen < 74)
	{
          sprintf (line+linelen-1, "=%2.2X", line[linelen-1]);
          fputs (line, fout);
        }
        else
	{
          int savechar = line[linelen-1];

          line[linelen-1] = '=';
          line[linelen] = 0;
          fputs (line, fout);
          fprintf (fout, "\n=%2.2X", savechar);
        }
      }
      else
      {
        line[linelen] = 0;
        fputs (line, fout);
      }
      fputc ('\n', fout);
      linelen = 0;
    }
    else if (c != 9 && (c < 32 || c > 126 || c == '='))
    {
      /* Check to make sure there is enough room for the quoted character.
       * If not, wrap to the next line.
       */
      if (linelen > 73)
      {
        line[linelen++] = '=';
        line[linelen] = 0;
        fputs (line, fout);
        fputc ('\n', fout);
        linelen = 0;
      }
      sprintf (line+linelen,"=%2.2X", c);
      linelen += 3;
    }
    else
    {
      /* Don't worry about wrapping the line here.  That will happen during
       * the next iteration when I'll also know what the next character is.
       */
      line[linelen++] = c;
    }
  }

  /* Take care of anything left in the buffer */
  if (linelen > 0)
  {
    if (line[linelen-1] == ' ' || line[linelen-1] == '\t')
    {
      /* take care of trailing whitespace */
      if (linelen < 74)
        sprintf (line+linelen-1, "=%2.2X", line[linelen-1]);
      else
      {
        savechar = line[linelen-1];
        line[linelen-1] = '=';
        line[linelen] = 0;
        fputs (line, fout);
        fputc ('\n', fout);
        sprintf (line, "=%2.2X", savechar);
      }
    }
    else
      line[linelen] = 0;
    fputs (line, fout);
  }
}

static void encode_base64 (FILE * fin, FILE *fout, int istext)
{
  int c1, c2, c3, ch;
  int insert_newline = 0;
  int linelen = 0;

  FOREVER
  {
    if (istext)
    {
      if (insert_newline)
      {
        c1 = '\n';
        insert_newline = 0;
  
        c2 = fgetc(fin);
        if (c2 == '\n')
	{
          c2 = '\r';
          c3 = '\n';
        }
        else
	{
          c3 = fgetc(fin);
          if (c3 == '\n')
	  {
            c3 = '\r';
            insert_newline = 1;
          }
        }
      }
      else
      {
        c1 = fgetc(fin);
        if (c1 == '\n')
	{
          c1 = '\r';
          c2 = '\n';
          c3 = fgetc(fin);
          if (c3 == '\n')
	  {
            c3 = '\r';
            insert_newline = 1;
          }
        }
        else
	{
          c2 = fgetc(fin);
          if (c2 == '\n')
	  {
            c2 = '\r';
            c3 = '\n';
          }
          else
	  {
            c3 = fgetc(fin);
            if (c3 == '\n')
	    {
              c3 = '\r';
              insert_newline = 1;
            }
          }
        }
      }
    }
    else /* !istext */
    {
      if ((c1 = fgetc(fin)) == EOF)
        break;
      c2 = fgetc(fin);
      c3 = fgetc(fin);
    }

    if (linelen + 4 >= 76)
    {
      fputc('\n', fout);
      linelen = 0;
    }

    ch = c1 >> 2;
    fputc (B64Chars[ch], fout);

    if (c2 != EOF)
    {
      ch = ((c1 & 0x3) << 4) | (c2 >> 4);
      fputc (B64Chars[ch], fout);
    }
    else
    {
      ch = (c1 & 0x3) << 4;
      fputc (B64Chars[ch], fout);
      fputs("==", fout);
      break;
    }

    if (c3 != EOF)
    {
      ch = ((c2 & 0xf) << 2) | (c3 >> 6);
      fputc (B64Chars[ch], fout);
    }
    else
    {
      ch = (c2 & 0xf) << 2;
      fputc(B64Chars[ch], fout);
      fputc('=', fout);
      break;
    }

    ch = c3 & 0x3f;
    fputc(B64Chars[ch], fout);

    linelen += 4;
  }

  fputc('\n', fout);
}

int mutt_write_mime_header (BODY *a, FILE *f)
{
  PARAMETER *p;
  char buffer[STRING];
  char *t;
  char *fn;
  int len;
  int tmplen;
  
  fprintf (f, "Content-Type: %s/%s", TYPE (a->type), a->subtype);

  if (a->parameter)
  {
    len = 25 + strlen (a->subtype); /* approximate len. of content-type */

    p = a->parameter;
    while (p)
    {
      fputc (';', f);

      buffer[0] = 0;
      rfc822_cat (buffer, sizeof (buffer), p->value, MimeSpecials);

      tmplen = strlen (buffer) + strlen (p->attribute) + 1;

      if (len + tmplen + 2 > 76)
      {
	fputs ("\n\t", f);
	len = tmplen + 8;
      }
      else
      {
	fputc (' ', f);
	len += tmplen + 1;
      }

      fprintf (f, "%s=%s", p->attribute, buffer);

      p = p->next;
    }
  }

  fputc ('\n', f);

  if (a->encoding != ENC7BIT)
    fprintf(f, "Content-Transfer-Encoding: %s\n", ENCODING (a->encoding));

  if (a->description)
    fprintf(f, "Content-Description: %s\n", a->description);

  if (a->use_disp && (a->disposition == DISPATTACH || a->filename || a->d_filename))
  {
    fprintf (f, "Content-Disposition: %s", DISPOSITION (a->disposition));

    if(!(fn = a->d_filename))
      fn = a->filename;
    
    if (fn)
    {
      /* Strip off the leading path... */
      if ((t = strrchr (fn, '/')))
	t++;
      else
	t = fn;
      
      buffer[0] = 0;
      rfc822_cat (buffer, sizeof (buffer), t, MimeSpecials);
      fprintf (f, "; filename=%s", buffer);
    }

    fputc ('\n', f);
  }

  /* Do NOT add the terminator here!!! */
  return (ferror (f) ? -1 : 0);
}

int mutt_write_mime_body (BODY *a, FILE *f)
{
  char *p, boundary[SHORT_STRING];
  FILE *fpin;
  BODY *t;

  if (a->type == TYPEMULTIPART)
  {
    /* First, find the boundary to use */
    if (!(p = mutt_get_parameter ("boundary", a->parameter)))
    {
      dprint (1, (debugfile, "mutt_write_mime_body(): no boundary parameter found!\n"));
      return (-1);
    }
    strfcpy (boundary, p, sizeof (boundary));

    for (t = a->parts; t ; t = t->next)
    {
      fprintf (f, "\n--%s\n", boundary);
      if (mutt_write_mime_header (t, f) == -1)
	return -1;
      fputc ('\n', f);
      if (mutt_write_mime_body (t, f) == -1)
	return -1;
    }
    fprintf (f, "\n--%s--\n", boundary);
    return (ferror (f) ? -1 : 0);
  }






  if ((fpin = fopen (a->filename, "r")) == NULL)
  {
    dprint(1,(debugfile, "write_mime_body: %s no longer exists!\n",a->filename));
    return -1;
  }

  if (a->encoding == ENCQUOTEDPRINTABLE)
    encode_quoted (fpin, f, mutt_is_text_type (a->type, a->subtype));
  else if (a->encoding == ENCBASE64)
    encode_base64 (fpin, f, mutt_is_text_type (a->type, a->subtype));
  else
    mutt_copy_stream (fpin, f);
  fclose (fpin);

  return (ferror (f) ? -1 : 0);
}

#define BOUNDARYLEN 16
char *mutt_generate_boundary (void)
{
  char *rs = (char *)safe_malloc (BOUNDARYLEN + 1);
  char *p = rs;
  int i;

  rs[BOUNDARYLEN] = 0;
  for (i=0;i<BOUNDARYLEN;i++) *p++ = B64Chars[LRAND() % sizeof (B64Chars)];
  *p = 0;
  return (rs);
}

/* analyze the contents of a file to determine which MIME encoding to use */
static CONTENT *mutt_get_content_info (const char *fname)
{
  CONTENT *info;
  FILE *fp;
  int ch, from=0, whitespace=0, dot=0, linelen=0;

  if ((fp = fopen (fname, "r")) == NULL)
  {
    dprint (1, (debugfile, "mutt_get_content_info: %s: %s (errno %d).\n",
		fname, strerror (errno), errno));
    return (NULL);
  }

  info = safe_calloc (1, sizeof (CONTENT));
  while ((ch = fgetc (fp)) != EOF)
  {
    linelen++;
    if (ch == '\n')
    {
      if (whitespace) info->space = 1;
      if (dot) info->dot = 1;
      if (linelen > info->linemax) info->linemax = linelen;
      whitespace = 0;
      linelen = 0;
      dot = 0;
    }
    else if (ch == '\r')
    {
      if ((ch = fgetc (fp)) == EOF)
      {
        info->binary = 1;
        break;
      }
      else if (ch != '\n')
      {
        info->binary = 1;
	ungetc (ch, fp);
	continue;
      }
      else
      {
        if (whitespace) info->space = 1;
	if (dot) info->dot = 1;
        if (linelen > info->linemax) info->linemax = linelen;
        whitespace = 0;
	dot = 0;
        linelen = 0;
      }
    }
    else if (ch & 0x80)
      info->hibin++;
    else if (ch == '\t' || ch == '\f')
    {
      info->ascii++;
      whitespace++;
    }
    else if (ch < 32 || ch == 127)
      info->lobin++;
    else
    {
      if (linelen == 1)
      {
        if (ch == 'F')
          from = 1;
        else
          from = 0;
        if (ch == '.')
          dot = 1;
        else
          dot = 0;
      }
      else if (from)
      {
        if (linelen == 2 && ch != 'r') from = 0;
        else if (linelen == 3 && ch != 'o') from = 0;
        else if (linelen == 4 && ch != 'm') from = 0;
        else if (linelen == 5)
	{
          if (ch == ' ') info->from = 1;
          from = 0;
        }
      }
      if (ch == ' ') whitespace++;
      info->ascii++;
    }
    if (linelen > 1) dot = 0;
    if (ch != ' ' && ch != '\t') whitespace = 0;
  }
  fclose (fp);
  return (info);
}

/* Given a file with path ``s'', see if there is a registered MIME type.
 * returns the major MIME type, and copies the subtype to ``d''.  First look
 * for ~/.mime.types, then look in a system mime.types if we can find one.
 * The longest match is used so that we can match `ps.gz' when `gz' also
 * exists.
 */

static int lookup_mime_type (char *d, const char *s)
{
  FILE *f;
  char *p, *ct,
  buf[LONG_STRING];
  int count;
  int szf, sze, cur_n, cur_sze;

  *d = 0;
  cur_n = TYPEOTHER;
  cur_sze = 0;
  szf = strlen (s);

  for (count = 0 ; count < 2 ; count++)
  {
    /*
     * can't use strtok() because we use it in an inner loop below, so use
     * a switch statement here instead.
     */
    switch (count)
    {
      case 0:
	snprintf (buf, sizeof (buf), "%s/.mime.types", Homedir);
	break;
      case 1:
	strfcpy (buf, SHAREDIR"/mime.types", sizeof (buf));
	break;
      default:
	return (cur_n);
    }

    if ((f = fopen (buf, "r")) != NULL)
    {
      while (fgets (buf, sizeof (buf) - 1, f) != NULL)
      {
	/* weed out any comments */
	if ((p = strchr (buf, '#')))
	  *p = 0;

	/* remove any leading space. */
	ct = buf;
	SKIPWS (ct);

	/* position on the next field in this line */
	if ((p = strpbrk (ct, " \t")) == NULL)
	  continue;
	*p++ = 0;
	SKIPWS (p);

	/* cycle through the file extensions */
	while ((p = strtok (p, " \t\n")))
	{
	  sze = strlen (p);
	  if ((sze > cur_sze) && (szf >= sze) &&
	      strcasecmp (s + szf - sze, p) == 0 &&
	      (szf == sze || s[szf - sze - 1] == '.'))
	  {
	    char *dc;

	    /* get the content-type */

	    if ((p = strchr (ct, '/')) == NULL)
	    {
	      /* malformed line, just skip it. */
	      break;
	    }
	    *p++ = 0;

	    dc = d;
	    while (*p && !ISSPACE (*p))
	      *dc++ = *p++;
	    *dc = 0;

	    cur_n = mutt_check_mime_type (ct);
	    cur_sze = sze;
	  }
	  p = NULL;
	}
      }
      fclose (f);
    }
  }
  return (cur_n);
}

static char *set_text_charset (CONTENT *info)
{
  if (strcasecmp (NONULL (Charset), "us-ascii") == 0)
  {
    if (info->hibin != 0)
      return "unknown-8bit";
  }
  else if (info->hibin == 0)
    return "us-ascii";

  /* if no charset is given, provide a reasonable default */
  return (Charset ? Charset : "us-ascii");
}

void mutt_message_to_7bit (BODY *a, FILE *fp)
{
  char temp[_POSIX_PATH_MAX];
  size_t linelen = 0;
  char *line = NULL;
  FILE *fpin = NULL;
  FILE *fpout = NULL;
  struct stat sb;

  if (!a->filename && fp)
    fpin = fp;
  else if (!a->filename || !(fpin = fopen (a->filename, "r")))
  {
    mutt_error ("Could not open %s", a->filename ? a->filename : "(null)");
    return;
  } 
  else
  {
    a->offset = 0;
    if (stat (a->filename, &sb) == -1)
    {
      mutt_perror ("stat");
      fclose (fpin);
    }
    a->length = sb.st_size;
  }

  mutt_mktemp (temp);
  if (!(fpout = safe_fopen (temp, "w+")))
  {
    mutt_perror ("fopen");
    goto cleanup;
  }

  fseek (fpin, a->offset, 0);
  a->parts = mutt_parse_messageRFC822 (fpin, a);

  transform_to_7bit (a->parts, fpin);
  
  mutt_copy_hdr (fpin, fpout, a->offset, a->offset + a->length, 
		 CH_MIME | CH_NONEWLINE | CH_XMIT, NULL);

  fputs ("Mime-Version: 1.0\n", fpout);
  mutt_write_mime_header (a->parts, fpout);
  fputc ('\n', fpout);
  mutt_write_mime_body (a->parts, fpout);
  
 cleanup:
  safe_free ((void **) &line);
  linelen = 0;

  if (fpin && !fp)
    fclose (fpin);
  if (fpout)
    fclose (fpout);
  else
    return;
  
  a->encoding = ENC7BIT;
  a->d_filename = a->filename;
  if (a->filename && a->unlink)
    unlink (a->filename);
  a->filename = safe_strdup (temp);
  a->unlink = 1;
  if(stat (a->filename, &sb) == -1) 
  {
    mutt_perror ("stat");
    return;
  }
  a->length = sb.st_size;
  mutt_free_body (&a->parts);
}

static void transform_to_7bit (BODY *a, FILE *fpin)
{
  char buff[_POSIX_PATH_MAX];
  STATE s;
  struct stat sb;

  memset (&s, 0, sizeof (s));
  for (; a; a = a->next)
  {
    if (a->type == TYPEMULTIPART)
    {
      if (a->encoding != ENC7BIT)
	a->encoding = ENC7BIT;

      transform_to_7bit (a->parts, fpin);
    } 
    else if (a->type == TYPEMESSAGE && strcasecmp (a->subtype, "delivery-status"))
    {
      mutt_message_to_7bit (a, fpin);
    }
    else 
    {
      mutt_mktemp (buff);
      if ((s.fpout = safe_fopen (buff, "w")) == NULL) 
      {
	mutt_perror ("fopen");
	return;
      }
      s.fpin = fpin;
      mutt_decode_attachment (a, &s);
      fclose (s.fpout);
      a->d_filename = a->filename;
      a->filename = safe_strdup (buff);
      a->unlink = 1;
      if (stat (a->filename, &sb) == -1) 
      {
	mutt_perror ("stat");
	return;
      }
      a->length = sb.st_size;

      mutt_update_encoding (a);
      if (a->encoding == ENC8BIT)
	a->encoding = ENCQUOTEDPRINTABLE;
      else if(a->encoding == ENCBINARY)
	a->encoding = ENCBASE64;
    }
  }
}

/* determine which Content-Transfer-Encoding to use */
static void mutt_set_encoding (BODY *b, CONTENT *info)
{
  if (b->type == TYPETEXT)
  {
    if (info->lobin)
      b->encoding = ENCQUOTEDPRINTABLE;
    else if (info->hibin)
      b->encoding = option (OPTALLOW8BIT) ? ENC8BIT : ENCQUOTEDPRINTABLE;
    else
      b->encoding = ENC7BIT;
  }
  else if (b->type == TYPEMESSAGE)
  {
    if (info->lobin || info->hibin)
    {
      if (option (OPTALLOW8BIT) && !info->lobin)
	b->encoding = ENC8BIT; 
      else
	mutt_message_to_7bit (b, NULL);
    }
    else
      b->encoding = ENC7BIT;
  }
  else if (info->lobin || info->hibin || info->binary || info->linemax > 990)
  {
    /* Determine which encoding is smaller  */
    if (1.33 * (float)(info->lobin+info->hibin+info->ascii) < 3.0 * (float) (info->lobin + info->hibin) + (float)info->ascii)
      b->encoding = ENCBASE64;
    else
      b->encoding = ENCQUOTEDPRINTABLE;
  }
  else
    b->encoding = ENC7BIT;
}

/* Assumes called from send mode where BODY->filename points to actual file */
void mutt_update_encoding (BODY *a)
{
  CONTENT *info;

  if ((info = mutt_get_content_info (a->filename)) == NULL)
    return;

  mutt_set_encoding (a, info);

  if (a->type == TYPETEXT)
  {
    /* make sure the charset is valid */
    if (a->parameter)
      safe_free ((void **) &a->parameter->value);
    else
    {
      a->parameter = mutt_new_parameter ();
      a->parameter->attribute = safe_strdup ("charset");
    }
    a->parameter->value = safe_strdup (set_text_charset (info));
  }






  safe_free ((void **) &info);
}

BODY *mutt_make_attach (const char *path)
{
  BODY *att;
  CONTENT *info;
  char buf[SHORT_STRING];
  int n;
  
  if ((info = mutt_get_content_info (path)) == NULL)
    return NULL;

  att = mutt_new_body ();
  att->filename = safe_strdup (path);

  /* Attempt to determine the appropriate content-type based on the filename
   * suffix.
   */
  if ((n = lookup_mime_type (buf, path)) != TYPEOTHER)
  {
    att->type = n;
    att->subtype = safe_strdup (buf);
  }

  if (!att->subtype)
  {
    if (info->lobin == 0 || (info->lobin + info->hibin + info->ascii)/ info->lobin >= 10)
    {
      /*
       * Statistically speaking, there should be more than 10% "lobin" 
       * chars if this is really a binary file...
       */
      att->type = TYPETEXT;
      att->subtype = safe_strdup ("plain");
      att->parameter = mutt_new_parameter ();
      att->parameter->attribute = safe_strdup ("charset");
      att->parameter->value = safe_strdup (set_text_charset (info));
    }
    else
    {
      att->type = TYPEAPPLICATION;
      att->subtype = safe_strdup ("octet-stream");
    }
  } 

  mutt_set_encoding (att, info);






  safe_free ((void **) &info);

  return (att);
}

static int get_toplevel_encoding (BODY *a)
{
  int e = ENC7BIT;

  for (; a; a = a->next)
  {
    if (a->encoding == ENCBINARY)
      return (ENCBINARY);
    else if (a->encoding == ENC8BIT)
      e = ENC8BIT;
  }

  return (e);
}

BODY *mutt_make_multipart (BODY *b)
{
  BODY *new;

  new = mutt_new_body ();
  new->type = TYPEMULTIPART;
  new->subtype = safe_strdup ("mixed");
  new->encoding = get_toplevel_encoding (b);
  new->parameter = mutt_new_parameter ();
  new->parameter->attribute = safe_strdup ("boundary");
  new->parameter->value = mutt_generate_boundary ();
  new->use_disp = 0;  
  new->parts = b;

  return new;
}

static char *mutt_make_date (char *s)
{
  time_t t = time (NULL);
  struct tm *l = gmtime(&t);
  int yday = l->tm_yday;
  int tz = l->tm_hour * 60 + l->tm_min;

  l = localtime(&t);
  tz = l->tm_hour * 60 + l->tm_min - tz;
  yday = l->tm_yday - yday;

  if (yday != 0)
    tz += yday * 24 * 60; /* GMT is next or previous day! */

  sprintf (s, "Date: %s, %d %s %d %02d:%02d:%02d %+03d%02d\n",
	   Weekdays[l->tm_wday], l->tm_mday, Months[l->tm_mon], l->tm_year+1900,
	   l->tm_hour, l->tm_min, l->tm_sec, tz/60, abs(tz) % 60);
  return (s);
}

/* wrapper around mutt_write_address() so we can handle very large
   recipient lists without needing a huge temporary buffer in memory */
void mutt_write_address_list (ADDRESS *adr, FILE *fp, int linelen)
{
  ADDRESS *tmp;
  char buf[LONG_STRING];
  int count = 0;
  int len;

  while (adr)
  {
    tmp = adr->next;
    adr->next = NULL;
    buf[0] = 0;
    rfc822_write_address (buf, sizeof (buf), adr);
    len = strlen (buf);
    if (count && linelen + len > 74)
    {
      if (count)
      {
	fputs ("\n\t", fp);
	linelen = len + 8; /* tab is usually about 8 spaces... */
      }
    }
    else
    {
      if (count && adr->mailbox)
      {
	fputc (' ', fp);
	linelen++;
      }
      linelen += len;
    }
    fputs (buf, fp);
    adr->next = tmp;
    if (!adr->group && adr->next && adr->next->mailbox)
    {
      linelen++;
      fputc (',', fp);
    }
    adr = adr->next;
    count++;
  }
  fputc ('\n', fp);
}

/* arbitrary number of elements to grow the array by */
#define REF_INC 16

#define TrimRef 10

/* need to write the list in reverse because they are stored in reverse order
 * when parsed to speed up threading
 */
static void write_references (LIST *r, FILE *f)
{
  LIST **ref = NULL;
  int refcnt = 0, refmax = 0;

  for ( ; (TrimRef == 0 || refcnt < TrimRef) && r ; r = r->next)
  {
    if (refcnt == refmax)
      safe_realloc ((void **) &ref, (refmax += REF_INC) * sizeof (LIST *));
    ref[refcnt++] = r;
  }

  while (refcnt-- > 0)
  {
    fputc (' ', f);
    fputs (ref[refcnt]->data, f);
  }

  safe_free ((void **) &ref);
}

/* Note: all RFC2047 encoding should be done outside of this routine, except
 * for the "real name."  This will allow this routine to be used more than
 * once, if necessary.
 *
 * mode == 1  => "lite" mode (used for edit_hdrs)
 * mode == 0  => normal mode.  write full header + MIME headers
 * mode == -1 => write just the envelope info (used for postponing messages)
 */

int mutt_write_rfc822_header (FILE *fp, ENVELOPE *env, BODY *attach, int mode)
{
  char buffer[LONG_STRING];
  LIST *tmp = env->userhdrs;

  if (mode == 0)
  {
    if (env->message_id)
      fprintf (fp, "Message-ID: %s\n", env->message_id);
    fputs (mutt_make_date (buffer), fp);
  }

  /* OPTUSEFROM is not consulted here so that we can still write a From:
   * field if the user sets it with the `my_hdr' command
   */
  if (env->from)
  {
    buffer[0] = 0;
    rfc822_write_address (buffer, sizeof (buffer), env->from);
    fprintf (fp, "From: %s\n", buffer);
  }

  if (env->to)
  {
    fputs ("To: ", fp);
    mutt_write_address_list (env->to, fp, 4);
  }
  else if (mode > 0)
    fputs ("To: \n", fp);

  if (env->cc)
  {
    fputs ("Cc: ", fp);
    mutt_write_address_list (env->cc, fp, 4);
  }
  else if (mode > 0)
    fputs ("Cc: \n", fp);

  if (env->bcc)
  {
    fputs ("Bcc: ", fp);
    mutt_write_address_list (env->bcc, fp, 5);
  }
  else if (mode > 0)
    fputs ("Bcc: \n", fp);

  if (env->subject)
    fprintf (fp, "Subject: %s\n", env->subject);
  else if (mode == 1)
    fputs ("Subject: \n", fp);

  if (env->reply_to)
  {
    fputs ("Reply-To: ", fp);
    mutt_write_address_list (env->reply_to, fp, 10);
  }
  else if (mode > 0)
    fputs ("Reply-To: \n", fp);

  if (env->mail_followup_to)
  {
    fputs ("Mail-Followup-To: ", fp);
    mutt_write_address_list (env->mail_followup_to, fp, 18);
  }

  if (mode <= 0)
  {
    if (env->references)
    {
      fputs ("References:", fp);
      write_references (env->references, fp);
      fputc('\n', fp);
    }

    /* Add the MIME headers */
    fputs ("Mime-Version: 1.0\n", fp);
    mutt_write_mime_header (attach, fp);
  }

#ifndef NO_XMAILER
  if (mode == 0)
  {
    /* Add a vanity header */
    fprintf (fp, "X-Mailer: Mutt %s\n", VERSION);
  }
#endif

  /* Add any user defined headers */
  for (; tmp; tmp = tmp->next)
  {
    fputs (tmp->data, fp);
    fputc ('\n', fp);
  }

  return (ferror (fp) == 0 ? 0 : -1);
}

static void encode_headers (LIST *h)
{
  char tmp[LONG_STRING];
  char *p;
  size_t len;

  for (; h; h = h->next)
  {
    if ((p = strchr (h->data, ':')))
    {
      *p++ = 0;
      SKIPWS (p);
      snprintf (tmp, sizeof (tmp), "%s: ", h->data);
      len = strlen (tmp);
      rfc2047_encode_string (tmp + len, sizeof (tmp) - len, (unsigned char *) p);
      safe_free ((void **) &h->data);
      h->data = safe_strdup (tmp);
    }
  }
}

/* rfc2047 encode the content-descriptions */
static void encode_descriptions (BODY *b)
{
  BODY *t;
  char tmp[LONG_STRING];

  for (t = b; t; t = t->next)
  {
    if (t->description)
    {
      rfc2047_encode_string (tmp, sizeof (tmp), (unsigned char *) t->description);
      safe_free ((void **) &t->description);
      t->description = safe_strdup (tmp);
    }
    if (t->parts)
      encode_descriptions (t->parts);
  }
}

char *mutt_gen_msgid (void)
{
  char buf[SHORT_STRING];
  time_t now;
  struct tm *tm;

  now = time (NULL);
  tm = localtime (&now);
  snprintf (buf, sizeof (buf), "<%d%02d%02d%02d%02d%02d.%c%d@%s>",
	    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour,
	    tm->tm_min, tm->tm_sec, MsgIdPfx, getpid (),
	    Fqdn[0] != '@' ? Fqdn : Hostname);
  MsgIdPfx = (MsgIdPfx == 'Z') ? 'A' : MsgIdPfx + 1;
  return (safe_strdup (buf));
}

static RETSIGTYPE alarm_handler (int sig)
{
  Signals |= S_ALARM;
}

/* invoke sendmail in a subshell
   path	(in)		path to program to execute
   args	(in)		arguments to pass to program
   msg (in)		temp file containing message to send
   tempfile (out)	if sendmail is put in the background, this points
   			to the temporary file containing the stdout of the
			child process */
static int
send_msg (const char *path, char **args, const char *msg, char **tempfile)
{
  int fd, st, w = 0, err = 0;
  pid_t pid;
  struct sigaction act, oldint, oldquit, oldalrm;

  memset (&act, 0, sizeof (struct sigaction));
  sigemptyset (&(act.sa_mask));
  act.sa_handler = SIG_IGN;
  sigaction (SIGINT, &act, &oldint);
  sigaction (SIGQUIT, &act, &oldquit);

  if (SendmailWait)
  {
    char tmp[_POSIX_PATH_MAX];

    mutt_mktemp (tmp);
    *tempfile = safe_strdup (tmp);
  }

  if ((pid = fork ()) == 0)
  {
    /* reset signals for the child */
    act.sa_handler = SIG_DFL;
    /* we need SA_RESTART for the open() below */
#ifdef SA_RESTART
    act.sa_flags = SA_NOCLDSTOP | SA_RESTART;
#else
    act.sa_flags = SA_NOCLDSTOP;
#endif
    sigaction (SIGCHLD, &act, NULL);
    act.sa_flags = 0;
    sigaction (SIGINT, &act, NULL);
    sigaction (SIGQUIT, &act, NULL);

    /* if it is possible that we will deliver in the background, then we have
       to detach the child from this process group or it will die when the
       parent process exists, causing the mail to not get delivered.  The
       problem here is that any error messages will get lost... */
    if (SendmailWait)
      setsid ();
    if ((pid = fork ()) == 0)
    {
      fd = open (msg, O_RDONLY, 0);
      if (fd < 0)
	_exit (127);
      dup2 (fd, 0);
      close (fd);
      if (SendmailWait)
      {
	/* write stdout to a tempfile */
	w = open (*tempfile, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (w < 0)
	  _exit (errno);
	dup2 (w, 1);
	close (w);
      }
      sigaction (SIGCHLD, &act, NULL);
      execv (path, args);
      unlink (msg);
      _exit (127);
    }
    else if (pid == -1)
    {
      unlink (msg);
      _exit (errno);
    }

    if (waitpid (pid, &st, 0) > 0)
    {
      st = WIFEXITED (st) ? WEXITSTATUS (st) : 127;
      if (st == (EX_OK & 0xff) && SendmailWait)
	unlink (*tempfile); /* no longer needed */
    }
    else
    {
      st = 127;
      if (SendmailWait)
	unlink (*tempfile);
    }
    unlink (msg);
    _exit (st);
  }
  /* SendmailWait > 0: SIGALRM will interrupt wait() after alrm seconds
     SendmailWait = 0: wait forever
     SendmailWait < 0: don't wait */
  if (SendmailWait > 0)
  {
    Signals &= ~S_ALARM;
    act.sa_handler = alarm_handler;
#ifdef SA_INTERRUPT
    /* need to make sure the waitpid() is interrupted on SIGALRM */
    act.sa_flags = SA_INTERRUPT;
#else
    act.sa_flags = 0;
#endif
    sigaction (SIGALRM, &act, &oldalrm);
    alarm (SendmailWait);
  }
  if (SendmailWait >= 0)
  {
    w = waitpid (pid, &st, 0);
    err = errno; /* save error since it might be clobbered by another
		    system call before we check the value */
    dprint (1, (debugfile, "send_msg(): waitpid returned %d (%s)\n", w,
	    (w < 0 ? strerror (errno) : "OK")));
  }
  if (SendmailWait > 0)
  {
    alarm (0);
    sigaction (SIGALRM, &oldalrm, NULL);
  }
  if (SendmailWait < 0 || ((Signals & S_ALARM) && w < 0 && err == EINTR))
  {
    /* add to list of children pids to reap */
    mutt_add_child_pid (pid);
    /* since there is no way for the user to be notified of error in this case,
       remove the temp file now */
    unlink (*tempfile);
    FREE (tempfile);
#ifdef DEBUG
    if (Signals & S_ALARM)
      dprint (1, (debugfile, "send_msg(): received SIGALRM\n"));
    dprint (1, (debugfile, "send_msg(): putting sendmail in the background\n"));
#endif
    st = EX_OK & 0xff;
  }
  else if (w < 0)
  {
    /* if err==EINTR, alarm interrupt, child status unknown,
       otherwise, there was an error invoking the child */
    st = (err == EINTR) ? (EX_OK & 0xff) : 127;
  }
  else
  {
#ifdef DEBUG
    if (WIFEXITED (st))
    {
      dprint (1, (debugfile, "send_msg(): child exited %d\n", WEXITSTATUS (st)));
    }
    else
    {
      dprint (1, (debugfile, "send_msg(): child did not exit\n"));
    }
#endif /* DEBUG */
    st = WIFEXITED (st) ? WEXITSTATUS (st) : -1; /* return child status */
  }
  sigaction (SIGINT, &oldint, NULL);
  sigaction (SIGQUIT, &oldquit, NULL);
  /* restore errno so that the caller can build an error message */
  errno = err;
  return (st);
}

static char **
add_args (char **args, size_t *argslen, size_t *argsmax, ADDRESS *addr)
{
  for (; addr; addr = addr->next)
  {
    /* weed out group mailboxes, since those are for display only */
    if (addr->mailbox && !addr->group)
    {
      if (*argslen == *argsmax)
	safe_realloc ((void **) &args, (*argsmax += 5) * sizeof (char *));
      args[(*argslen)++] = addr->mailbox;
    }
  }
  return (args);
}

static char **
add_option (char **args, size_t *argslen, size_t *argsmax, char *s)
{
  if (*argslen == *argsmax)
    safe_realloc ((void **) &args, (*argsmax += 5) * sizeof (char *));
  args[(*argslen)++] = s;
  return (args);
}

static int
invoke_sendmail (ADDRESS *to, ADDRESS *cc, ADDRESS *bcc, /* recips */
		 const char *msg, /* file containing message */
		 int eightbit) /* message contains 8bit chars */
{
  char *ps = NULL, *path = NULL, *s = safe_strdup (Sendmail), *childout = NULL;
  char **args = NULL;
  size_t argslen = 0, argsmax = 0;
  int i;

  ps = s;
  i = 0;
  while ((ps = strtok (ps, " ")))
  {
    if (argslen == argsmax)
      safe_realloc ((void **) &args, sizeof (char *) * (argsmax += 5));

    if (i)
      args[argslen++] = ps;
    else
    {
      path = safe_strdup (ps);
      ps = strrchr (ps, '/');
      if (ps)
	ps++;
      else
	ps = path;
      args[argslen++] = ps;
    }
    ps = NULL;
    i++;
  }
  if (eightbit && option (OPTUSE8BITMIME))
    args = add_option (args, &argslen, &argsmax, "-B8BITMIME");
  if (DsnNotify)
  {
    args = add_option (args, &argslen, &argsmax, "-N");
    args = add_option (args, &argslen, &argsmax, DsnNotify);
  }
  if (DsnReturn)
  {
    args = add_option (args, &argslen, &argsmax, "-R");
    args = add_option (args, &argslen, &argsmax, DsnReturn);
  }
  args = add_args (args, &argslen, &argsmax, to);
  args = add_args (args, &argslen, &argsmax, cc);
  args = add_args (args, &argslen, &argsmax, bcc);

  if (argslen == argsmax)
    safe_realloc ((void **) &args, sizeof (char *) * (++argsmax));
  
  args[argslen++] = NULL;

  if (!option (OPTNOCURSES))
    endwin ();
  if ((i = send_msg (path, args, msg, &childout)) != (EX_OK & 0xff))
  {
    char *e = strerror (errno);

    fprintf (stderr, "Error sending message, child exited %d (%s).\n", i, NONULL (e));
    if (childout)
      fprintf (stderr, "Saved output of child process to %s.\n", childout);
    if (!option (OPTNOCURSES))
    {
      mutt_any_key_to_continue (NULL);
      mutt_error ("Error sending message.");
    }
  }
  FREE (&childout);
  FREE (&path);
  FREE (&s);
  FREE (&args);

  return (i);
}

/* appends string 'b' to string 'a', and returns the pointer to the new
   string. */
char *mutt_append_string (char *a, const char *b)
{
  size_t la = strlen (a);
  safe_realloc ((void **) &a, la + strlen (b) + 1);
  strcpy (a + la, b);
  return (a);
}

/* returns 1 if char `c' needs to be quoted to protect from shell
   interpretation when executing commands in a subshell */
#define INVALID_CHAR(c) (!isalnum ((unsigned char)c) && !strchr ("@.+-_,:", c))

/* returns 1 if string `s' contains characters which could cause problems
   when used on a command line to execute a command */
int mutt_needs_quote (const char *s)
{
  while (*s)
  {
    if (INVALID_CHAR (*s))
      return 1;
    s++;
  }
  return 0;
}

/* Quote a string to prevent shell escapes when this string is used on the
   command line to send mail. */
char *mutt_quote_string (const char *s)
{
  char *r, *pr;
  size_t rlen;

  rlen = strlen (s) + 3;
  pr = r = malloc (rlen);
  *pr++ = '"';
  while (*s)
  {
    if (INVALID_CHAR (*s))
    {
      size_t o = pr - r;
      safe_realloc ((void **) &r, ++rlen);
      pr = r + o;
      *pr++ = '\\';
    }
    *pr++ = *s++;
  }
  *pr++ = '"';
  *pr = 0;
  return (r);
}

int mutt_send_message (HEADER *msg, const char *fcc)
{
  char tempfile[_POSIX_PATH_MAX], buffer[STRING];
  FILE *tempfp;
  int i;

  /* Take care of 8-bit => 7-bit conversion. */
  rfc2047_encode_adrlist (msg->env->to);
  rfc2047_encode_adrlist (msg->env->cc);
  rfc2047_encode_adrlist (msg->env->from);
  rfc2047_encode_adrlist (msg->env->mail_followup_to);
  if (msg->env->subject)
  {
    rfc2047_encode_string (buffer, sizeof (buffer) - 1,
			   (unsigned char *) msg->env->subject);
    safe_free ((void **) &msg->env->subject);
    msg->env->subject = safe_strdup (buffer);
  }
  encode_headers (msg->env->userhdrs);
  encode_descriptions (msg->content);

  if (!msg->env->message_id)
    msg->env->message_id = mutt_gen_msgid ();

  /* Write out the message in MIME form. */
  mutt_mktemp (tempfile);
  if ((tempfp = safe_fopen (tempfile, "w")) == NULL)
    return (-1);

  mutt_write_rfc822_header (tempfp, msg->env, msg->content, 0);
  fputc ('\n', tempfp); /* tie off the header. */

  mutt_write_mime_body (msg->content, tempfp);
  if (fclose (tempfp) != 0)
  {
    mutt_perror (tempfile);
    unlink (tempfile);
    return (-1);
  }

  /* save a copy of the message, if necessary. */
  if (*fcc && strcmp ("/dev/null", fcc) != 0)
  {
    BODY *tmpbody = msg->content;

    /* check to see if the user wants copies of all attachments */
    if (msg->content->type == TYPEMULTIPART &&
	strcmp (msg->content->subtype, "encrypted") &&
	strcmp (msg->content->subtype, "signed") &&
	!option (OPTFCCATTACH))
      msg->content = msg->content->parts;

    mutt_write_fcc (fcc, msg, NULL, 0);
    msg->content = tmpbody;
  }

  i = invoke_sendmail (msg->env->to, msg->env->cc, msg->env->bcc,
		       tempfile,
		       (msg->content->encoding == ENC8BIT));
  return (i ? -1 : 0);
}

void mutt_bounce_message (HEADER *h, ADDRESS *to)
{
  int i;
  FILE *f;
  char date[SHORT_STRING], tempfile[_POSIX_PATH_MAX];
  MESSAGE *msg;

  if (!h)
  {
    for (i=0; i<Context->msgcount; i++)
      if (Context->hdrs[i]->tagged)
	mutt_bounce_message (Context->hdrs[i], to);
    return;
  }

  if ((msg = mx_open_message (Context, h->msgno)) != NULL)
  {
    mutt_mktemp (tempfile);
    if ((f = safe_fopen (tempfile, "w")) != NULL)
    {
      fseek (msg->fp, h->offset, 0);
      mutt_copy_header (msg->fp, h, f, CH_XMIT | CH_NONEWLINE, NULL);
      fprintf (f, "Resent-From: %s", Username);
      if (Fqdn[0] != '@')
	fprintf (f, "@%s", Fqdn);
      fprintf (f, "\nResent-%s", mutt_make_date (date));
      fputs ("Resent-To: ", f);
      mutt_write_address_list (to, f, 11);
      fputc ('\n', f);
      mutt_copy_bytes (msg->fp, f, h->content->length);
      fclose (f);

      invoke_sendmail (to, NULL, NULL, tempfile, h->content->encoding == ENC8BIT);
    }
    mx_close_message (&msg);
  }
}

/* given a list of addresses, return a list of unique addresses */
ADDRESS *mutt_remove_duplicates (ADDRESS *addr)
{
  ADDRESS *top = NULL;
  ADDRESS *tmp;
  
  if ((top = addr) == NULL)
    return (NULL);
  addr = addr->next;
  top->next = NULL;
  while (addr)
  {
    tmp = top;
    do {
      if (addr->mailbox && tmp->mailbox &&
	  !strcasecmp (addr->mailbox, tmp->mailbox))
      {
	/* duplicate address, just ignore it */
	tmp = addr;
	addr = addr->next;
	tmp->next = NULL;
	rfc822_free_address (&tmp);
      }
      else if (!tmp->next)
      {
	/* unique address.  add it to the list */
	tmp->next = addr;
	addr = addr->next;
	tmp = tmp->next;
	tmp->next = NULL;
	tmp = NULL; /* so we exit the loop */
      }
      else
	tmp = tmp->next;
    } while (tmp);
  }

  return (top);
}

int mutt_write_fcc (const char *path, HEADER *hdr, const char *msgid, int post)
{
  CONTEXT f;
  MESSAGE *msg;
  char tempfile[_POSIX_PATH_MAX];
  FILE *tempfp = NULL;
  int r;

  if (mx_open_mailbox (path, M_APPEND | M_QUIET, &f) == NULL)
  {
    dprint (1, (debugfile, "mutt_write_fcc(): unable to open mailbox %s in append-mode, aborting.\n",
		path));
    return (-1);
  }

  /* We need to add a Content-Length field to avoid problems where a line in
   * the message body begins with "From "   
   */
  if (f.magic == M_MMDF || f.magic == M_MBOX)
  {
    mutt_mktemp (tempfile);
    if ((tempfp = safe_fopen (tempfile, "w+")) == NULL)
    {
      mutt_perror (tempfile);
      mx_close_mailbox (&f);
      return (-1);
    }
  }

  hdr->read = 1; /* make sure to put it in the `cur' directory (maildir) */
  if ((msg = mx_open_new_message (&f, hdr, M_ADD_FROM)) == NULL)
  {
    mx_close_mailbox (&f);
    return (-1);
  }

  mutt_write_rfc822_header (msg->fp, hdr->env, hdr->content, (post ? -1 : 0));

  /* (postponment) if this was a reply of some sort, <msgid> contians the
   * Message-ID: of message replied to.  Save it using a special X-Mutt-
   * header so it can be picked up if the message is recalled at a later
   * point in time.  This will allow the message to be marked as replied if
   * the same mailbox is still open.
   */
  if (post && msgid)
    fprintf (msg->fp, "X-Mutt-References: %s\n", msgid);
  fprintf (msg->fp, "Status: RO\n");






  if (tempfp)
  {
    char sasha[LONG_STRING];
    int lines = 0;

    mutt_write_mime_body (hdr->content, tempfp);

    /* make sure the last line ends with a newline.  Emacs doesn't ensure
     * this will happen, and it can cause problems parsing the mailbox   
     * later.
     */
    fseek (tempfp, -1, 2);
    if (fgetc (tempfp) != '\n')
    {
      fseek (tempfp, 0, 2);
      fputc ('\n', tempfp);
    }

    fflush (tempfp);
    if (ferror (tempfp))
    {
      dprint (1, (debugfile, "mutt_write_fcc(): %s: write failed.\n", tempfile));
      fclose (tempfp);
      unlink (tempfile);
      mx_close_message (&msg);
      mx_close_mailbox (&f);
      return -1;
    }

    /* count the number of lines */
    rewind (tempfp);
    while (fgets (sasha, sizeof (sasha), tempfp) != NULL)
      lines++;
    fprintf (msg->fp, "Content-Length: %ld\n", (long) ftell (tempfp));
    fprintf (msg->fp, "Lines: %d\n\n", lines);

    /* copy the body and clean up */
    rewind (tempfp);
    r = mutt_copy_stream (tempfp, msg->fp);
    if (fclose (tempfp) != 0)
      r = -1;
    /* if there was an error, leave the temp version */
    if (!r)
      unlink (tempfile);
  }
  else
  {
    fputc ('\n', msg->fp); /* finish off the header */
    r = mutt_write_mime_body (hdr->content, msg->fp);
  }

  mx_close_message (&msg);
  mx_close_mailbox (&f);

  return r;
}
