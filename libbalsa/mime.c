/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"

#include <stdio.h>
#include <sys/utsname.h>
#include <string.h>
#include <time.h>
#include <gnome.h>
#include <obstack.h>

#include "mailbackend.h"

#include "balsa-app.h"
#include "mailbox.h"
#include "misc.h"

#include "mime.h"


#define  obstack_chunk_alloc g_malloc
#define  obstack_chunk_free  g_free

static gchar tmp_file_name[PATH_MAX + 1];
#define obstack_append_string(o, s)   obstack_grow(o, s, strlen(s))


static void
text2html (gchar * buff, struct obstack *html_buff)
{
  gint i = 0, len = strlen (buff);

  for (i = 0; i < len; i++)
    {
      if (buff[i] == '\r' && buff[i + 1] == '\n' &&
	  buff[i + 2] == '\r' && buff[i + 3] == '\n')
	{
	  obstack_append_string (html_buff, "</tt></p>\n<p><tt>\n");
	  i += 3;
	}
      else if (buff[i] == '\r' && buff[i + 1] == '\n')
	{
	  obstack_append_string (html_buff, "<br>\n");
	  i++;
	}
      else if (buff[i] == '\n' && buff[i + 1] == '\r')
	{
	  obstack_append_string (html_buff, "<br>\n");
	  i++;
	}
      else if (buff[i] == '\n' && buff[i + 1] == '\n')
	{
	  obstack_append_string (html_buff, "</tt></p>\n<p><tt>\n");
	  i++;
	}
      else if (buff[i] == '\n')
	{
	  obstack_append_string (html_buff, "<br>\n");
	}
      else if (buff[i] == '\r')
	{
	  obstack_append_string (html_buff, "<br>\n");
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ' && buff[i + 2] == ' ' && buff[i + 3] == ' ')
	{
	  obstack_append_string (html_buff, "&nbsp; &nbsp; ");
	  i += 3;
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ' && buff[i + 2] == ' ')
	{
	  obstack_append_string (html_buff, "&nbsp; &nbsp;");
	  i += 2;
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ')
	{
	  obstack_append_string (html_buff, "&nbsp; ");
	  i++;
	}
      else
	switch (buff[i])
	  {
	    /* for single spaces (not multiple (look above)) do *not*
	     * replace with a &nbsp; or lines will not wrap! bad
	     * thing(tm)
	     */
	  case '\t':
	    obstack_append_string (html_buff, "&nbsp; &nbsp; &nbsp; &nbsp; ");
	    break;
	  case ' ':
	    obstack_append_string (html_buff, " ");
	    break;
	  case '<':
	    obstack_append_string (html_buff, "&lt;");
	    break;
	  case '>':
	    obstack_append_string (html_buff, "&gt;");
	    break;
	  case '"':
	    obstack_append_string (html_buff, "&quot;");
	    break;
	  case '&':
	    obstack_append_string (html_buff, "&amp;");
	    break;
/* 
 * Weird stuff, but stuff that should be taken care of too
 * I might be missing something, lemme know?
 */
	  case '©':
	    obstack_append_string (html_buff, "&copy;");
	    break;
	  case '®':
	    obstack_append_string (html_buff, "&reg;");
	    break;
	  case 'à':
	    obstack_append_string (html_buff, "&agrave;");
	    break;
	  case 'À':
	    obstack_append_string (html_buff, "&Agrave;");
	    break;
	  case 'â':
	    obstack_append_string (html_buff, "&acirc;");
	    break;
	  case 'ä':
	    obstack_append_string (html_buff, "&auml;");
	    break;
	  case 'Ä':
	    obstack_append_string (html_buff, "&Auml;");
	    break;
	  case 'Â':
	    obstack_append_string (html_buff, "&Acirc;");
	    break;
	  case 'å':
	    obstack_append_string (html_buff, "&aring;");
	    break;
	  case 'Å':
	    obstack_append_string (html_buff, "&Aring;");
	    break;
	  case 'æ':
	    obstack_append_string (html_buff, "&aelig;");
	    break;
	  case 'Æ':
	    obstack_append_string (html_buff, "&AElig;");
	    break;
	  case 'ç':
	    obstack_append_string (html_buff, "&ccedil;");
	    break;
	  case 'Ç':
	    obstack_append_string (html_buff, "&Ccedil;");
	    break;
	  case 'é':
	    obstack_append_string (html_buff, "&eacute;");
	    break;
	  case 'É':
	    obstack_append_string (html_buff, "&Eacute;");
	    break;
	  case 'è':
	    obstack_append_string (html_buff, "&egrave;");
	    break;
	  case 'È':
	    obstack_append_string (html_buff, "&Egrave;");
	    break;
	  case 'ê':
	    obstack_append_string (html_buff, "&ecirc;");
	    break;
	  case 'Ê':
	    obstack_append_string (html_buff, "&Ecirc;");
	    break;
	  case 'ë':
	    obstack_append_string (html_buff, "&euml;");
	    break;
	  case 'Ë':
	    obstack_append_string (html_buff, "&Euml;");
	    break;
	  case 'ï':
	    obstack_append_string (html_buff, "&iuml;");
	    break;
	  case 'Ï':
	    obstack_append_string (html_buff, "&Iuml;");
	    break;
	  case 'ô':
	    obstack_append_string (html_buff, "&ocirc;");
	    break;
	  case 'Ô':
	    obstack_append_string (html_buff, "&Ocirc;");
	    break;
	  case 'ö':
	    obstack_append_string (html_buff, "&ouml;");
	    break;
	  case 'Ö':
	    obstack_append_string (html_buff, "&Ouml;");
	    break;
	  case 'ø':
	    obstack_append_string (html_buff, "&oslash;");
	    break;
	  case 'Ø':
	    obstack_append_string (html_buff, "&Oslash;");
	    break;
	  case 'ß':
	    obstack_append_string (html_buff, "&szlig;");
	    break;
	  case 'ù':
	    obstack_append_string (html_buff, "&ugrave;");
	    break;
	  case 'Ù':
	    obstack_append_string (html_buff, "&Ugrave;");
	    break;
	  case 'û':
	    obstack_append_string (html_buff, "&ucirc;");
	    break;
	  case 'Û':
	    obstack_append_string (html_buff, "&Ucirc;");
	    break;
	  case 'ü':
	    obstack_append_string (html_buff, "&uuml;");
	    break;
	  case 'Ü':
	    obstack_append_string (html_buff, "&Uuml;");
	    break;

	  default:
	    obstack_1grow (html_buff, buff[i]);
	    break;
	  }
    }

}


void
other2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  STATE s;
  gchar *ptr;
  size_t alloced;

  fseek (fp, bdy->offset, 0);
  s.fpin = fp;
  mutt_mktemp (tmp_file_name);

  s.fpout = fopen (tmp_file_name, "r+");
  mutt_decode_attachment (bdy, &s);
  fflush (s.fpout);
  alloced = readfile (s.fpout, &ptr);
  if (ptr)
    ptr[alloced - 1] = '\0';
  text2html (ptr, bfr);
  g_free (ptr);
  fclose (s.fpout);
  unlink (tmp_file_name);
}

void
audio2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  obstack_append_string (bfr, "<font size=+1>AUDIO<font size=-1>");
}


void
application2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  gchar link_bfr[128];
  PARAMETER *bdy_parameter = bdy->parameter;

  obstack_append_string (bfr, "<table boundary=\"0\"> <tr><td bgcolor=#dddddd> You received an encoded file of type <tt>application/");
  obstack_append_string (bfr, bdy->subtype);
  obstack_append_string (bfr, "</tt><BR>");
  obstack_append_string (bfr, "<P>The parameters of this message are:<BR>");

  while (bdy_parameter)
    {
      obstack_append_string (bfr, bdy_parameter->attribute);
      obstack_append_string (bfr, "=>");
      obstack_append_string (bfr, bdy_parameter->value);
      obstack_append_string (bfr, "<BR>");
      bdy_parameter = bdy_parameter->next;
    }
  snprintf (link_bfr, 128, "<A HREF=memory://%p BODY> APPLICATION</A></td></tr></table>");
  obstack_append_string (bfr, link_bfr);
}

void
image2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  obstack_append_string (bfr, "<font size+1>IMAGE<font size=-1>");
}

void
message2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  obstack_append_string (bfr, "<font size=+1>MESSAGE<font size=-1>");
}

void
multipart2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  obstack_append_string (bfr, "<font size=+1>MULTIPART<font size=-1>");
}


void
video2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  obstack_append_string (bfr, "<font size=+1>VIDEO<font size=-1>");
}

void
mimetext2html (BODY * bdy, FILE * fp, struct obstack *bfr)
{
  STATE s;
  gchar *ptr = 0;
  size_t alloced;

  fseek (fp, bdy->offset, 0);
  s.fpin = fp;
  mutt_mktemp (tmp_file_name);
  s.prefix = 0;
  s.fpout = fopen (tmp_file_name, "w+");
  mutt_decode_attachment (bdy, &s);
  fflush (s.fpout);
  alloced = readfile (s.fpout, &ptr);
  if (ptr)
    {
      ptr[alloced - 1] = '\0';
      if (strcmp (bdy->subtype, "html") == 0)
	{
	  obstack_append_string (bfr, ptr);
	  g_free (ptr);
	  unlink (tmp_file_name);
	  return;
	}
      obstack_append_string (bfr, "<P><TT>");
      text2html (ptr, bfr);
      g_free (ptr);
    }
  fclose (s.fpout);
  unlink (tmp_file_name);
  return;
}


void
part2html (BODY * bdy, FILE * fp, struct obstack *html_bfr)
{

  switch (bdy->type)
    {
    case TYPEOTHER:
      other2html (bdy, fp, html_bfr);
      break;
    case TYPEAUDIO:
      audio2html (bdy, fp, html_bfr);
      break;
    case TYPEAPPLICATION:
      application2html (bdy, fp, html_bfr);
      break;
    case TYPEIMAGE:
      image2html (bdy, fp, html_bfr);
      break;
    case TYPEMESSAGE:
      message2html (bdy, fp, html_bfr);
      break;
    case TYPEMULTIPART:
      multipart2html (bdy, fp, html_bfr);
      break;
    case TYPETEXT:
      mimetext2html (bdy, fp, html_bfr);
      break;
    case TYPEVIDEO:
      video2html (bdy, fp, html_bfr);
      break;
    }
}

void
header2row (gchar * name, gchar * data, struct obstack *html_bfr)
{
  obstack_append_string (html_bfr, "<TR>");

  obstack_append_string (html_bfr, "<TH ALIGN=LEFT WIDTH=\"10\%\">");
  obstack_append_string (html_bfr, name);
  obstack_append_string (html_bfr, "</TH>");
  obstack_append_string (html_bfr, "<TD ALIGN=LEFT WIDTH=\"90\%\">");
  text2html (data, html_bfr);
  obstack_append_string (html_bfr, "</TD>");

  obstack_append_string (html_bfr, "</TR>\n");
}

void
headers2html (Message * message, struct obstack *html_bfr)
{
  gchar tbuff[1024];

  obstack_append_string (html_bfr, "<DIV ALIGN=LEFT>\n");
  obstack_append_string (html_bfr, "<TABLE BORDER=0 WIDTH=\"100\%\">\n");

  if (message->date)
    header2row ("Date:", message->date, html_bfr);

  if (message->to_list)
    header2row ("To:", make_string_from_list (message->to_list), html_bfr);

  if (message->cc_list)
    header2row ("cc:", make_string_from_list (message->cc_list), html_bfr);

  if (message->from)
    {
      if (message->from->personal)
	snprintf (tbuff, 1024, "%s <%s>",
		  message->from->personal,
		  message->from->mailbox);
      else
	snprintf (tbuff, 1024, "%s", message->from->mailbox);

      header2row ("From:", tbuff, html_bfr);
    }

  if (message->subject)
    header2row ("Subject:", message->subject, html_bfr);

  obstack_append_string (html_bfr, "</TABLE>\n");
  obstack_append_string (html_bfr, "</DIV>\n");
}

gchar *
content2html (Message * message)
{
  GList *body_list;
  Body *body;
  FILE *msg_stream;
  gchar msg_filename[PATH_MAX];
  static struct obstack *html_buffer = 0;
  static gchar *html_buffer_content = (gchar *) - 1;

  if (!html_buffer)
    {
      html_buffer = g_malloc (sizeof (struct obstack));
      obstack_init (html_buffer);
    }
  else
    {
      obstack_free (html_buffer, html_buffer_content);
    }


  obstack_append_string (html_buffer, "<HTML>\n");
  obstack_append_string (html_buffer, "<HEAD><TITLE>Message</TITLE></HEAD>\n");
  obstack_append_string (html_buffer, "<BODY BGCOLOR=\"#FFFFFF\">\n");

  headers2html (message, html_buffer);

  switch (message->mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      {
	snprintf (msg_filename, PATH_MAX, "%s/%s", MAILBOX_LOCAL (message->mailbox)->path, message_pathname (message));
	msg_stream = fopen (msg_filename, "r");
	if (!msg_stream || ferror (msg_stream))
	  {
	    fprintf (stderr, "Open of %s failed. Errno = %d, ",
		     msg_filename, errno);
	    perror (NULL);
	    return 0;
	  }
	break;
      }
    case MAILBOX_IMAP:
      msg_stream = fopen (MAILBOX_IMAP (message->mailbox)->tmp_file_path, "r");
      break;
    default:
      msg_stream = fopen (MAILBOX_LOCAL (message->mailbox)->path, "r");
      break;
    }

  body_list = message->body_list;
  while (body_list)
    {
      body = (Body *) body_list->data;
      part2html (body->mutt_body, msg_stream, html_buffer);
      body_list = g_list_next (body_list);
    }
  obstack_append_string (html_buffer, "</TT></P>\n</BODY></HTML>");
  obstack_1grow (html_buffer, '\0');
  html_buffer_content = obstack_finish (html_buffer);
  return html_buffer_content;
}


GString *
content2reply (Message * message)
{
  GList *body_list;
  Body *body;
  FILE *msg_stream;
  gchar msg_filename[PATH_MAX];
  size_t alloced;
  gchar *ptr = 0;
  GString *reply = 0;

  switch (message->mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      {
	snprintf (msg_filename, PATH_MAX, "%s/%s", MAILBOX_LOCAL (message->mailbox)->path, message_pathname (message));
	msg_stream = fopen (msg_filename, "r");
	if (!msg_stream || ferror (msg_stream))
	  {
	    fprintf (stderr, "Open of %s failed. Errno = %d, ",
		     msg_filename, errno);
	    perror (NULL);
	    return 0;
	  }
	break;
      }
    case MAILBOX_IMAP:
      msg_stream = fopen (MAILBOX_IMAP (message->mailbox)->tmp_file_path, "r");
      break;
    default:
      msg_stream = fopen (MAILBOX_LOCAL (message->mailbox)->path, "r");
      break;
    }

  body_list = message->body_list;
  while (body_list)
    {
      body = (Body *) body_list->data;
      switch (body->mutt_body->type)
	{
	case TYPETEXT:
	  {
	    STATE s;
	    fseek (msg_stream, body->mutt_body->offset, 0);
	    s.fpin = msg_stream;
	    mutt_mktemp (tmp_file_name);
	    s.prefix = "> ";
	    s.fpout = fopen (tmp_file_name, "w+");
	    mutt_decode_attachment (body->mutt_body, &s);
	    fflush (s.fpout);
	    alloced = readfile (s.fpout, &ptr);
	    if (ptr)
	      ptr[alloced - 1] = '\0';
	    if (reply)
	      {
		reply = g_string_append (reply, "\n");
		reply = g_string_append (reply, ptr);
	      }
	    else
	      reply = g_string_new (ptr);
	    fclose (s.fpout);
	    unlink (tmp_file_name);
	  }
	}
      body_list = g_list_next (body_list);
    }
  fclose (msg_stream);
  return reply;
}
