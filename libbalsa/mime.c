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

#include "mime.h"


static gchar tmp_file_name[PATH_MAX + 1];


GString *
content2reply (Message * message,
	       gchar *reply_prefix_str)    /* arp */
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
      BODY *b;
      body = (Body *) body_list->data;

      b = body->mutt_body;
      while (b)
	{
	  switch (body->mutt_body->type)
	    {
	    case TYPEMULTIPART:
	      {
		b = b->parts;
		continue;
	      }
	    case TYPETEXT:
	      {
		STATE s;
		fseek (msg_stream, body->mutt_body->offset, 0);
		s.fpin = msg_stream;
		mutt_mktemp (tmp_file_name);
		s.prefix = reply_prefix_str; /* arp */
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
	  b = b->next;
	}
      body_list = g_list_next (body_list);
    }
  fclose (msg_stream);
  return reply;
}
