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
#include "config.h"

#include "libbalsa.h"
#include "misc.h"

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
    const gchar *const_res;
    size_t allocated;
    GString *reply = NULL;
    const GMimeContentType *type;
    GMimeStream *stream, *filter_stream;
    const char *charset;

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
	type=g_mime_object_get_content_type(body->mime_part);
	/* don't return text/html stuff... */
	if (ignore_html && g_mime_content_type_is_type(type, "*", "html"))
	    break;
	stream = g_mime_stream_mem_new();
	filter_stream = g_mime_stream_filter_new_with_stream(stream);
	charset = libbalsa_message_body_charset(body);
	if (!charset)
	    charset="us-ascii";
	if (g_ascii_strcasecmp(charset, "unknown-8bit")) {
	    GMimeFilter *filter =
		g_mime_filter_charset_new(charset, "UTF-8");
	    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream),
				     filter);
	    g_object_unref(filter);
	}
	const_res=g_mime_part_get_content(GMIME_PART(body->mime_part),
					  &allocated);
	if (!allocated || g_mime_stream_write(filter_stream, (char*)const_res,
					      allocated) == -1 ||
	    g_mime_stream_flush(filter_stream) == -1) {
	    res = g_strdup("");
	} else {
	    g_mime_stream_write(stream, "", 1);
	    res = GMIME_STREAM_MEM(stream)->buffer->data;
	    GMIME_STREAM_MEM(stream)->owner = FALSE;
	}
	g_mime_stream_unref(filter_stream);
	g_mime_stream_unref(stream);

#ifdef HAVE_GPGME
	/* if this is a RFC 2440 signed part, strip the signature status */
	if (body->sig_info) {
	    gchar *p = 
		g_strrstr(res, _("\n--\nThis is an OpenPGP signed message part:\n"));
	    
	    if (p)
		*p = '\0';
	}
#endif

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
	    res = g_string_append(res, s->str);
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

    return reply;
}
