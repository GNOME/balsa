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
#include "mailbox.h"
#include "mx.h"
#include "copy.h"
#include "rfc2047.h"
#include <string.h>
#include <stdlib.h>

/* Ok, the only reason for not merging this with mutt_copy_header()
 * below is to avoid creating a HEADER structure in message_handler().
 */
int
mutt_copy_hdr (FILE *in, FILE *out, long off_start, long off_end, int flags,
	       const char *prefix)
{
  int from = 0;
  int ignore = 0;
  char buf[STRING]; /* should be long enough to get most fields in one pass */
  char *nl;
  LIST *t;
  char **headers;
  int hdr_count;
  int x;
  int error;

  if (ftell (in) != off_start)
    fseek (in, off_start, 0);

  buf[0] = '\n';
  buf[1] = 0;

  if ((flags & (CH_REORDER | CH_WEED | CH_MIME | CH_DECODE | CH_PREFIX)) == 0)
  {
    /* Without these flags to complicate things
     * we can do a more efficient line to line copying
     */
    while (ftell (in) < off_end)
    {
      nl = strchr (buf, '\n');

      if ((fgets (buf, sizeof (buf), in)) == NULL)
	break;

      /* Is it the begining of a header? */
      if (nl && buf[0] != ' ' && buf[0] != '\t')
      {
	ignore = 1;
	if (!from && strncmp ("From ", buf, 5) == 0)
	{
	  if ((flags & CH_FROM) == 0)
	    continue;
	  from = 1;
	}
	else if (buf[0] == '\n' || (buf[0] == '\r' && buf[1] == '\n'))
	  break; /* end of header */

	if ((flags & (CH_UPDATE | CH_XMIT | CH_NOSTATUS)) &&
	    (strncasecmp ("Status:", buf, 7) == 0 ||
	     strncasecmp ("X-Status:", buf, 9) == 0))
	  continue;
	if ((flags & (CH_UPDATE_LEN | CH_XMIT)) &&
	    (strncasecmp ("Content-Length:", buf, 15) == 0 ||
	     strncasecmp ("Lines:", buf, 6) == 0))
	  continue;
	ignore = 0;
      }

      if (!ignore && fputs (buf, out) == EOF)
	return (-1);
    }
    return 0;
  }

  hdr_count = 1;
  x = 0;
  error = FALSE;

  /* We are going to read and collect the headers in an array
   * so we are able to do re-ordering.
   * First count the number of entries in the array
   */
  if (flags & CH_REORDER)
  {
    for (t = HeaderOrderList; t; t = t->next)
    {
      dprint(1, (debugfile, "Reorder list: %s\n", t->data));
      hdr_count++;
    }
  }

  dprint (1, (debugfile, "WEED is %s\n", (flags & CH_WEED) ? "Set" : "Not"));

  headers = safe_calloc (hdr_count, sizeof (char *));

  /* Read all the headers into the array */
  while (ftell (in) < off_end)
  {
    nl = strchr (buf, '\n');

    /* Read a line */
    if ((fgets (buf, sizeof (buf), in)) == NULL)
      break;

    /* Is it the begining of a header? */
    if (nl && buf[0] != ' ' && buf[0] != '\t')
    {
      ignore = 1;
      if (!from && strncmp ("From ", buf, 5) == 0)
      {
	if ((flags & CH_FROM) == 0)
	  continue;
	from = 1;
      }
      else if (buf[0] == '\n' || (buf[0] == '\r' && buf[1] == '\n'))
	break; /* end of header */

      if ((flags & CH_WEED) && 
	  mutt_matches_ignore (buf, Ignore) &&
	  !mutt_matches_ignore (buf, UnIgnore))
	continue;
      if ((flags & (CH_UPDATE | CH_XMIT | CH_NOSTATUS)) &&
	  (strncasecmp ("Status:", buf, 7) == 0 ||
	   strncasecmp ("X-Status:", buf, 9) == 0))
	continue;
      if ((flags & (CH_UPDATE_LEN | CH_XMIT)) &&
	  (strncasecmp ("Content-Length:", buf, 15) == 0 ||
	   strncasecmp ("Lines:", buf, 6) == 0))
	continue;
      if ((flags & CH_MIME) &&
	  ((strncasecmp ("content-", buf, 8) == 0 &&
	    (strncasecmp ("transfer-encoding:", buf + 8, 18) == 0 ||
	     strncasecmp ("type:", buf + 8, 5) == 0)) ||
	   strncasecmp ("mime-version:", buf, 13) == 0))
	continue;

      /* Find x -- the array entry where this header is to be saved */
      if (flags & CH_REORDER)
      {
	for (t = HeaderOrderList, x = 0 ; (t) ; t = t->next, x++)
	{
	  if (!strncasecmp (buf, t->data, strlen (t->data)))
	  {
	    dprint(2, (debugfile, "Reorder: %s matches %s\n", t->data, 
		       headers[x]));
	    break;
	  }
	}
      }

      ignore = 0;
    } /* If beginning of header */

    if (!ignore)
    {
      /* Save the header in headers[x] */
      if (!headers[x])
	headers[x] = safe_strdup (buf);
      else
      {
	safe_realloc ((void **) &headers[x],
		      strlen (headers[x]) + strlen (buf) + sizeof (char));
	strcat (headers[x], buf);
      }
    }
  } /* while (ftell (in) < off_end) */

  /* Now output the headers in order */
  for (x = 0; x < hdr_count; x++)
  {
    if (headers[x])
    {
      if (flags & CH_DECODE)
	rfc2047_decode (headers[x], headers[x], strlen (headers[x]));

      /* We couldn't do the prefixing when reading because RFC 2047
       * decoding may have concatenated lines.
       */
      if (flags & CH_PREFIX)
      {
	char *ch = headers[x];
	int print_prefix = 1;

	while (*ch)
	{ 
	  if (print_prefix)
	  {
	    if (fputs (prefix, out) == EOF)
	    {
	      error = TRUE;
	      break;
	    }
	    print_prefix = 0;
	  }

	  if (*ch == '\n' && ch[1])
	    print_prefix = 1;

	  if (putc (*ch++, out) == EOF)
	  {
	    error = TRUE;
	    break;
	  }
	}
	if (error)
	  break;
      }
      else
      {      
	if (fputs (headers[x], out) == EOF)
	{
	  error = TRUE;
	  break;
	}
      }
    }
  }

  /* Free in a separate loop to be sure that all headers are freed
   * in case of error. */
  for (x = 0; x < hdr_count; x++)
    safe_free ((void **) &headers[x]);
  safe_free ((void **) &headers);

  if (error)
    return (-1);
  return (0);
}

/* flags
 	CH_DECODE	RFC2047 header decoding
 	CH_FROM		retain the "From " message separator
 	CH_MIME		ignore MIME fields
 	CH_NONEWLINE	don't output a newline after the header
 	CH_NOSTATUS	ignore the Status: and X-Status:
 	CH_PREFIX	quote header with $indent_str
 	CH_REORDER	output header in order specified by `hdr_order'
  	CH_TXTPLAIN	generate text/plain MIME headers
 	CH_UPDATE	write new Status: and X-Status:
 	CH_UPDATE_LEN	write new Content-Length: and Lines:
 	CH_XMIT		ignore Lines: and Content-Length:
 	CH_WEED		do header weeding

   prefix
   	string to use if CH_PREFIX is set
 */

int
mutt_copy_header (FILE *in, HEADER *h, FILE *out, int flags, const char *prefix)
{
  if (mutt_copy_hdr (in, out, h->offset, h->content->offset, flags, prefix) == -1)
    return (-1);

  if (flags & CH_TXTPLAIN)
  {
    fputs ("Mime-Version: 1.0\n", out);
    fputs ("Content-Type: text/plain\n", out);
    fputs ("Content-Transfer-Encoding: 8bit\n", out);
  }
  
  if (flags & CH_UPDATE)
  {
    if ((flags & CH_NOSTATUS) == 0)
    {
      if (h->old || h->read)
      {
	if (fputs ("Status: ", out) == EOF)
	  return (-1);

	if (h->read)
	{
	  if (fputs ("RO", out) == EOF)
	    return (-1);
	}
	else if (h->old)
	{
	  if (fputc ('O', out) == EOF)
	    return (-1);
	}

	if (fputc ('\n', out) == EOF)
	  return (-1);
      }

      if (h->flagged || h->replied)
      {
	if (fputs ("X-Status: ", out) == EOF)
	  return (-1);

	if (h->replied)
	{
	  if (fputc ('A', out) == EOF)
	    return (-1);
	}

	if (h->flagged)
	{
	  if (fputc ('F', out) == EOF)
	    return (-1);
	}
	
	if (fputc ('\n', out) == EOF)
	  return (-1);
      }
    }
  }

  if (flags & CH_UPDATE_LEN)
  {
    fprintf (out, "Content-Length: %ld\n", h->content->length);
    if (h->lines != 0 || h->content->length == 0)
      fprintf (out, "Lines: %d\n", h->lines);
  }

  if ((flags & CH_NONEWLINE) == 0)
  {
    if (fputc ('\n', out) == EOF) /* add header terminator */
      return (-1);
  }

  return (0);
}

/* make a copy of a message
 
   fpout	where to write output
   fpin		where to get input
   hdr		header of message being copied
   body		structure of message being copied
   flags
 	M_CM_NOHEADER	don't copy header
 	M_CM_PREFIX	quote header and body
 	M_CM_DECODE	decode message body to text/plain
 	M_CM_DISPLAY	displaying output to the user
   chflags	flags to mutt_copy_header()
 */

int
_mutt_copy_message (FILE *fpout, FILE *fpin, HEADER *hdr, BODY *body,
		    int flags, int chflags)
{
  char prefix[SHORT_STRING];
  STATE s;

  if (flags & M_CM_PREFIX)
    _mutt_make_string (prefix, sizeof (prefix), Prefix, hdr, 0);

  if ((flags & M_CM_NOHEADER) == 0)
  {
    if (flags & M_CM_PREFIX)
      chflags |= CH_PREFIX;
    if (mutt_copy_header (fpin, hdr, fpout, chflags,
			  (chflags & CH_PREFIX) ? prefix : NULL) == -1)
      return -1;
  }

  if (flags & M_CM_DECODE)
  {
    /* now make a text/plain version of the message */
    memset (&s, 0, sizeof (STATE));
    s.fpin = fpin;
    s.fpout = fpout;
    if (flags & M_CM_PREFIX)
      s.prefix = prefix;
    if (flags & M_CM_DISPLAY)
      s.flags |= M_DISPLAY;






    mutt_body_handler (body, &s);
  }
  else
  {
    fseek (fpin, body->offset, 0);
    if (flags & M_CM_PREFIX)
    {
      char buf[LONG_STRING];
      size_t bytes = body->length;

      buf[sizeof (buf) - 1] = 0;
      while (bytes > 0)
      {
	if (fgets (buf, sizeof (buf) - 1, fpin) == NULL)
	  break;
	bytes -= strlen (buf);
	fputs (prefix, fpout);
	fputs (buf, fpout);
      }
    }
    else
    {
      if (mutt_copy_bytes (fpin, fpout, body->length) == -1)
	return -1;
    }
  }
  return 0;
}

int
mutt_copy_message (FILE *fpout, CONTEXT *src, HEADER *hdr, int flags,
		   int chflags)
{
  MESSAGE *msg;
  int r;
  
  if ((msg = mx_open_message (src, hdr->msgno)) == NULL)
    return -1;
  r = _mutt_copy_message (fpout, msg->fp, hdr, hdr->content, flags, chflags);
  mx_close_message (&msg);
  return r;
}

/* appends a copy of the given message to a mailbox
 *
 * dest		destination mailbox
 * fpin		where to get input
 * src		source mailbox
 * hdr		message being copied
 * body		structure of message being copied
 * flags	mutt_copy_message() flags
 * chflags	mutt_copy_header() flags
 */

int
_mutt_append_message (CONTEXT *dest, FILE *fpin, CONTEXT *src, HEADER *hdr,
		      BODY *body, int flags, int chflags)
{
  MESSAGE *msg;
  int r;

  if ((msg = mx_open_new_message (dest, hdr, (src->magic == M_MBOX || src->magic == M_MMDF) ? 0 : M_ADD_FROM)) == NULL)
    return -1;
  if (dest->magic == M_MBOX || dest->magic == M_MMDF)
    chflags |= CH_FROM;
  chflags |= (dest->magic == M_MAILDIR ? CH_NOSTATUS : CH_UPDATE);
  r = _mutt_copy_message (msg->fp, fpin, hdr, body, flags, chflags);
  mx_close_message (&msg);
  return r;
}

int
mutt_append_message (CONTEXT *dest, CONTEXT *src, HEADER *hdr, int cmflags,
		     int chflags)
{
  MESSAGE *msg;
  int r;

  if ((msg = mx_open_message (src, hdr->msgno)) == NULL)
    return -1;
  r = _mutt_append_message (dest, msg->fp, src, hdr, hdr->content, cmflags, chflags);
  mx_close_message (&msg);
  return r;
}
