/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include <string.h>
#include <libgnome/libgnome.h>
#include "config.h"

#include "libbalsa.h"
#include "misc.h"
#include "html.h"

/* FIXME: The content of this file could go to message.c */

static GString *process_mime_multipart(LibBalsaMessage * message,
                                       LibBalsaMessageBody * body,
				       gchar * reply_prefix_str,
				       gint llen, gboolean ignore_html,
                                       gboolean flow);

/* process_mime_part:
   returns string representation of given message part.
   NOTE: may return NULL(!).
*/

GString *
process_mime_part(LibBalsaMessage * message, LibBalsaMessageBody * body,
		  gchar * reply_prefix_str, gint llen, gboolean ignore_html,
                  gboolean flow)
{
    gchar *res = NULL;
    size_t allocated;
    GString *reply = NULL;
    gchar *mime_type;
    LibBalsaHTMLType html_type;

    switch (libbalsa_message_body_type(body)) {
    case LIBBALSA_MESSAGE_BODY_TYPE_OTHER:
    case LIBBALSA_MESSAGE_BODY_TYPE_AUDIO:
    case LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION:
    case LIBBALSA_MESSAGE_BODY_TYPE_IMAGE:
    case LIBBALSA_MESSAGE_BODY_TYPE_MODEL:
    case LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE:
    case LIBBALSA_MESSAGE_BODY_TYPE_VIDEO:
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART:
        reply = process_mime_multipart(message, body, reply_prefix_str,
                                       llen, ignore_html, flow);
	break;
    case LIBBALSA_MESSAGE_BODY_TYPE_TEXT:
	/* don't return text/html stuff... */
	mime_type = libbalsa_message_body_get_mime_type(body);
	html_type = libbalsa_html_type(mime_type);
	g_free(mime_type);

	if (ignore_html && html_type)
	    break;

	allocated = libbalsa_message_body_get_content(body, &res);
	if (!res)
	    return NULL;

#ifdef HAVE_GTKHTML
	if (html_type) {
	    allocated = libbalsa_html_filter(html_type, &res, allocated);
	    libbalsa_html_to_string(&res, allocated);
	}
#endif /* HAVE_GTKHTML */

	if (llen > 0) {
            if (flow && libbalsa_message_body_is_flowed(body)) {
                /* we're making a `format=flowed' message, and the
                 * message we're quoting was flowed
                 *
                 * we'll assume it's going to the screen */
		gboolean delsp = libbalsa_message_body_is_delsp(body);

		reply =
		    libbalsa_process_text_rfc2646(res, G_MAXINT, FALSE,
						  TRUE,
						  reply_prefix_str != NULL,
						  delsp);
                g_free(res);
                break;
            }
	    if (reply_prefix_str)
		llen -= strlen(reply_prefix_str);
	    libbalsa_wrap_string(res, llen);
	}
        if (reply_prefix_str || flow) {
	    gchar *str, *ptr;
	    /* prepend the prefix to all the lines */

	    reply = g_string_new("");
	    str = res;
	    do {
		ptr = strchr(str, '\n');
		if (ptr)
		    *ptr = '\0';
                if (reply_prefix_str)
		reply = g_string_append(reply, reply_prefix_str);
                if (flow) {
                    gchar *p;
                    /* we're making a `format=flowed' message, but the
                     * message we're quoting was `format=fixed', so we
                     * must make sure all lines are `fixed'--that is,
                     * trim any trailing ' ' characters */
                    for (p = str; *p; ++p);
                    while (*--p == ' ');
                    *++p = '\0';
                }
		reply = g_string_append(reply, str);
		reply = g_string_append_c(reply, '\n');
		str = ptr;
	    } while (str++);
	} else
	    reply = g_string_new(res);
	g_free(res);
	break;
    }
    return reply;
}

static GString *
process_mime_multipart(LibBalsaMessage * message,
                       LibBalsaMessageBody * body,
		       gchar * reply_prefix_str, gint llen,
		       gboolean ignore_html, gboolean flow)
{
    LibBalsaMessageBody *part;
    GString *res = NULL, *s;

    for (part = body->parts; part; part = part->next) {
	s = process_mime_part(message, part, reply_prefix_str, llen,
                          ignore_html, flow);
	if (!s)
	    continue;
	if (res) {
	    if (res->str[res->len - 1] != '\n')
		g_string_append_c(res, '\n');
	    g_string_append_c(res, '\n');
	    g_string_append(res, s->str);
	    g_string_free(s, TRUE);
	} else
	    res = s;
    }
    return res;
}

GString *
content2reply(LibBalsaMessage * message, gchar * reply_prefix_str,
	      gint llen, gboolean ignore_html, gboolean flow)
{
    LibBalsaMessageBody *body;
    GString *reply = NULL, *res;

    libbalsa_message_body_ref(message, FALSE, FALSE);
    body = message->body_list;
    for (body = message->body_list; body; body = body->next) {
	res = process_mime_part(message, body, reply_prefix_str, llen,
                                ignore_html, flow);
	if (!res)
	    continue;
	if (reply) {
	    reply = g_string_append(reply, res->str);
	    g_string_free(res, TRUE);
	} else
	    reply = res;
    }
    libbalsa_message_body_unref(message);

    return reply;
}
