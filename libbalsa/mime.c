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

#include "mailbackend.h"

#include "balsa-app.h"
#include "mailbox.h"
#include "misc.h"

static char*
content_type2str(int contenttype)
{
  switch (contenttype)
    {
    case TYPEOTHER:
      return "OTHER";
    case TYPEAUDIO:
      return "AUDIO";
    case TYPEAPPLICATION:
      return "APPLICATION";
    case TYPEIMAGE:
      return "IMAGE";
    case TYPEMULTIPART:
      return "MULTIPART";
    case TYPETEXT:
      return "TEXT";
    case TYPEVIDEO:
      return "VIDEO";
    }
}

static gchar tmp_file_name[PATH_MAX+1];
gchar *
text2html (char *buff)
{
  int i = 0, len = strlen (buff);
  gchar *str;
  GString *gs = g_string_new (NULL);

  for (i = 0; i < len; i++)
    {
      if (buff[i] == '\r' && buff[i + 1] == '\n' &&
	  buff[i + 2] == '\r' && buff[i + 3] == '\n')
	{
	  gs = g_string_append (gs, "</tt></p><p><tt>\n");
	  i += 3;
	}
      else if (buff[i] == '\r' && buff[i + 1] == '\n')
	{
	  gs = g_string_append (gs, "<br>\n");
	  i++;
	}
      else if (buff[i] == '\n' && buff[i + 1] == '\r')
	{
	  gs = g_string_append (gs, "<br>\n");
	  i++;
	}
      else if (buff[i] == '\n' && buff[i + 1] == '\n')
	{
	  gs = g_string_append (gs, "</tt></p><p><tt>\n");
	  i++;
	}
      else if (buff[i] == '\n')
	{
	  gs = g_string_append (gs, "<br>\n");
	}
      else if (buff[i] == '\r')
	{
	  gs = g_string_append (gs, "<br>\n");
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ' && buff[i + 2] == ' ' && buff[i + 3] == ' ')
	{
	  gs = g_string_append (gs, "&nbsp; &nbsp; ");
	  i += 3;
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ' && buff[i + 2] == ' ')
	{
	  gs = g_string_append (gs, "&nbsp; &nbsp;");
	  i += 2;
	}
      else if (buff[i] == ' ' && buff[i + 1] == ' ')
	{
	  gs = g_string_append (gs, "&nbsp; ");
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
	    gs = g_string_append (gs, "&nbsp; &nbsp; &nbsp; &nbsp; ");
	    break;
	  case ' ':
	    gs = g_string_append (gs, " ");
	    break;
	  case '<':
	    gs = g_string_append (gs, "&lt;");
	    break;
	  case '>':
	    gs = g_string_append (gs, "&gt;");
	    break;
	  case '"':
	    gs = g_string_append (gs, "&quot;");
	    break;
	  case '&':
	    gs = g_string_append (gs, "&amp;");
	    break;
/* 
 * Weird stuff, but stuff that should be taken care of too
 * I might be missing something, lemme know?
 */
	  case '©':
	    gs = g_string_append (gs, "&copy;");
	    break;
	  case '®':
	    gs = g_string_append (gs, "&reg;");
	    break;
	  case 'à':
	    gs = g_string_append (gs, "&agrave;");
	    break;
	  case 'À':
	    gs = g_string_append (gs, "&Agrave;");
	    break;
	  case 'â':
	    gs = g_string_append (gs, "&acirc;");
	    break;
	  case 'ä':
	    gs = g_string_append (gs, "&auml;");
	    break;
	  case 'Ä':
	    gs = g_string_append (gs, "&Auml;");
	    break;
	  case 'Â':
	    gs = g_string_append (gs, "&Acirc;");
	    break;
	  case 'å':
	    gs = g_string_append (gs, "&aring;");
	    break;
	  case 'Å':
	    gs = g_string_append (gs, "&Aring;");
	    break;
	  case 'æ':
	    gs = g_string_append (gs, "&aelig;");
	    break;
	  case 'Æ':
	    gs = g_string_append (gs, "&AElig;");
	    break;
	  case 'ç':
	    gs = g_string_append (gs, "&ccedil;");
	    break;
	  case 'Ç':
	    gs = g_string_append (gs, "&Ccedil;");
	    break;
	  case 'é':
	    gs = g_string_append (gs, "&eacute;");
	    break;
	  case 'É':
	    gs = g_string_append (gs, "&Eacute;");
	    break;
	  case 'è':
	    gs = g_string_append (gs, "&egrave;");
	    break;
	  case 'È':
	    gs = g_string_append (gs, "&Egrave;");
	    break;
	  case 'ê':
	    gs = g_string_append (gs, "&ecirc;");
	    break;
	  case 'Ê':
	    gs = g_string_append (gs, "&Ecirc;");
	    break;
	  case 'ë':
	    gs = g_string_append (gs, "&euml;");
	    break;
	  case 'Ë':
	    gs = g_string_append (gs, "&Euml;");
	    break;
	  case 'ï':
	    gs = g_string_append (gs, "&iuml;");
	    break;
	  case 'Ï':
	    gs = g_string_append (gs, "&Iuml;");
	    break;
	  case 'ô':
	    gs = g_string_append (gs, "&ocirc;");
	    break;
	  case 'Ô':
	    gs = g_string_append (gs, "&Ocirc;");
	    break;
	  case 'ö':
	    gs = g_string_append (gs, "&ouml;");
	    break;
	  case 'Ö':
	    gs = g_string_append (gs, "&Ouml;");
	    break;
	  case 'ø':
	    gs = g_string_append (gs, "&oslash;");
	    break;
	  case 'Ø':
	    gs = g_string_append (gs, "&Oslash;");
	    break;
	  case 'ß':
	    gs = g_string_append (gs, "&szlig;");
	    break;
	  case 'ù':
	    gs = g_string_append (gs, "&ugrave;");
	    break;
	  case 'Ù':
	    gs = g_string_append (gs, "&Ugrave;");
	    break;
	  case 'û':
	    gs = g_string_append (gs, "&ucirc;");
	    break;
	  case 'Û':
	    gs = g_string_append (gs, "&Ucirc;");
	    break;
	  case 'ü':
	    gs = g_string_append (gs, "&uuml;");
	    break;
	  case 'Ü':
	    gs = g_string_append (gs, "&Uuml;");
	    break;

	  default:
	    gs = g_string_append_c (gs, buff[i]);
	    break;
	  }
    }

  str = gs->str;
  g_string_free (gs, TRUE);
  return str;
}


static gchar*
other2html(BODY* bdy, FILE* fp)
{
  STATE  s;
  char*  ptr;
  size_t alloced;
  gchar* retval;
  
  fseek(fp, bdy->offset, 0);
  s.fpin  = fp;
  mutt_mktemp(tmp_file_name);
  
  s.fpout = fopen(tmp_file_name,"r+");
  mutt_decode_attachment(bdy, &s);
  fflush(s.fpout);
  alloced = readfile(s.fpout, &ptr);
  ptr[alloced - 1] = '\0';
  retval = text2html(ptr);
  free(ptr);
  fclose(s.fpout);
  return retval;
}

static gchar*
audio2html(BODY* bdy, FILE* fp)
{
  return "<font size=+1>AUDIO<font size=-1>";
}


static gchar*
application2html(BODY* bdy, FILE* fp)
{
  return "<font size=+1>APPLICATION<font size=-1>";
}

static gchar*
image2html(BODY* bdy, FILE* fp)
{
  return "<font size+1>IMAGE<font size=-1>";
}

static gchar*
message2html(BODY* bdy, FILE* fp)
{
  return "<font size=+1>MESSAGE<font size=-1>";
}

static gchar*
multipart2html(BODY* bdy, FILE* fp)
{
  return "<font size=+1>MULTIPART<font size=-1>";
}


static gchar*
video2html(BODY* bdy, FILE* fp)
{
  return "<font size=+1>VIDEO<font size=-1>";
}

static gchar*
mimetext2html(BODY* bdy, FILE* fp)
{
  STATE  s;
  char*  ptr = 0;
  size_t alloced;
  gchar* retval = NULL;
  
  fseek(fp, bdy->offset, 0);
  s.fpin  = fp;
  mutt_mktemp(tmp_file_name);
  s.prefix = 0;
  s.fpout = fopen(tmp_file_name,"w+");
  mutt_decode_attachment(bdy, &s);
  fflush(s.fpout);
  alloced = readfile(s.fpout, &ptr);
  if (alloced != 0)
  {
  ptr[alloced - 1] = '\0';
  if (strcmp(bdy->subtype,"html") == 0)
    {
      return ptr;
    }
  retval = text2html(ptr);
  free(ptr);
  }
  fclose(s.fpout);
  return retval;
}

gchar*
part2html(BODY* bdy, FILE* fp, STATE* s)
{
  GString* buf = g_string_new(NULL);
  gchar*   retval;
  
  switch (bdy->type)
    {
    case TYPEOTHER:
      g_string_append(buf, other2html(bdy, fp));
      break;
    case TYPEAUDIO:
      g_string_append(buf, audio2html(bdy, fp));
      break;
    case TYPEAPPLICATION:
      g_string_append(buf, application2html(bdy, fp));
      break;
    case TYPEIMAGE:
      g_string_append(buf, image2html(bdy, fp));
      break;
    case TYPEMESSAGE:
      g_string_append(buf, message2html(bdy, fp));
      break;
    case TYPEMULTIPART:
      g_string_append(buf, multipart2html(bdy, fp));
      break;
    case TYPETEXT:
      g_string_append(buf, mimetext2html(bdy, fp));
      break;
    case TYPEVIDEO:
      g_string_append(buf, video2html(bdy, fp));
      break;
    }
  retval = g_strdup(buf->str);
  g_string_free(buf, TRUE);
  return retval;
}
