/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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

#include "libbalsa.h"
#include "mailbackend.h"

/* FIXME: The content of this file could go to message.c */

/* FIXME: unnecesary global */
GString *reply;

static void process_mime_multipart (LibBalsaMessage * message, LibBalsaMessageBody * body, gchar *reply_prefix_str);
static void process_mime_part (LibBalsaMessage * message, LibBalsaMessageBody * body, gchar *reply_prefix_str);

static void
process_mime_part (LibBalsaMessage * message, LibBalsaMessageBody * body, gchar *reply_prefix_str)
{
	FILE *part;
	size_t alloced;
	gchar *ptr = 0;

	switch ( libbalsa_message_body_type (body) ) {
	case LIBBALSA_MESSAGE_BODY_TYPE_OTHER:
	case LIBBALSA_MESSAGE_BODY_TYPE_AUDIO:
	case LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION:
	case LIBBALSA_MESSAGE_BODY_TYPE_IMAGE:
	case LIBBALSA_MESSAGE_BODY_TYPE_MODEL:
	case LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE:
	case LIBBALSA_MESSAGE_BODY_TYPE_VIDEO:
		break;
	case LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART:
		process_mime_multipart (message, body, reply_prefix_str);
		break;
	case LIBBALSA_MESSAGE_BODY_TYPE_TEXT:
		libbalsa_message_body_save_temporary(body, reply_prefix_str);
    
		part = fopen (body->temp_filename, "r");
		alloced = libbalsa_readfile (part, &ptr);
    
		if (reply) {
			reply = g_string_append (reply, "\n");
			reply = g_string_append (reply, ptr);
		} else {
			reply = g_string_new (ptr);
		}

		break;
	}
}

static void
process_mime_multipart (LibBalsaMessage * message, LibBalsaMessageBody * body, gchar *reply_prefix_str)
{
	LibBalsaMessageBody *part;

	for (part = body->parts; part; part = part->next) {
		process_mime_part (message, part, reply_prefix_str);
	}
}

GString *
content2reply (LibBalsaMessage * message, gchar *reply_prefix_str)
{
	LibBalsaMessageBody *body;

	reply = 0;

	body = message->body_list;

	while ( body ) {
		process_mime_part (message, body, reply_prefix_str);
		body = body->next;
	}    

	return reply;
}
