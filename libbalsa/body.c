/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
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

#include <string.h>
#include <glib.h>

#include "libbalsa.h"
#include "mailbackend.h"

LibBalsaMessageBody *
libbalsa_message_body_new(LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;

    body = g_new0(LibBalsaMessageBody, 1);

    body->message = message;
    body->buffer = NULL;
    body->mutt_body = NULL;
    body->filename = NULL;
    body->temp_filename = NULL;
    body->charset = NULL;

    body->next = NULL;
    body->parts = NULL;

    return body;
}


void
libbalsa_message_body_free(LibBalsaMessageBody * body)
{
    if (body == NULL)
	return;

    g_free(body->buffer);
    g_free(body->filename);

    if (body->temp_filename)
	unlink(body->temp_filename);
    g_free(body->temp_filename);

    g_free(body->charset);

    libbalsa_message_body_free(body->next);
    libbalsa_message_body_free(body->parts);

    /* FIXME: Need to free MuttBody?? */

    g_free(body);
}

void
libbalsa_message_body_set_mutt_body(LibBalsaMessageBody * body,
				    MuttBody * mutt_body)
{
    g_return_if_fail(body->mutt_body == NULL);

    body->mutt_body = mutt_body;
    body->filename = g_strdup(mutt_body->filename);
    if(body->filename)
	rfc2047_decode(&body->filename);
    if (mutt_body->next) {
	body->next = libbalsa_message_body_new(body->message);
	libbalsa_message_body_set_mutt_body(body->next, mutt_body->next);
    }

    if (mutt_body->parts) {
	body->parts = libbalsa_message_body_new(body->message);
	libbalsa_message_body_set_mutt_body(body->parts, mutt_body->parts);
    }
}

LibBalsaMessageBodyType
libbalsa_message_body_type(LibBalsaMessageBody * body)
{
    switch (body->mutt_body->type) {
    case TYPEOTHER:
	return LIBBALSA_MESSAGE_BODY_TYPE_OTHER;
    case TYPEAUDIO:
	return LIBBALSA_MESSAGE_BODY_TYPE_AUDIO;
    case TYPEAPPLICATION:
	return LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION;
    case TYPEIMAGE:
	return LIBBALSA_MESSAGE_BODY_TYPE_IMAGE;
    case TYPEMESSAGE:
	return LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE;
    case TYPEMODEL:
	return LIBBALSA_MESSAGE_BODY_TYPE_MODEL;
    case TYPEMULTIPART:
	return LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART;
    case TYPETEXT:
	return LIBBALSA_MESSAGE_BODY_TYPE_TEXT;
    case TYPEVIDEO:
	return LIBBALSA_MESSAGE_BODY_TYPE_VIDEO;
    }

    g_assert_not_reached();
    return LIBBALSA_MESSAGE_BODY_TYPE_OTHER;
}

gchar *
libbalsa_message_body_get_parameter(LibBalsaMessageBody * body,
				    const gchar * param)
{
    gchar *res;

    g_return_val_if_fail(body != NULL, NULL);

    libbalsa_lock_mutt();
    res = mutt_get_parameter(param, body->mutt_body->parameter);
    libbalsa_unlock_mutt();

    return g_strdup(res);
}

gboolean
libbalsa_message_body_save_temporary(LibBalsaMessageBody * body,
				     gchar * prefix)
{
    /* FIXME: Role our own mktemp that doesn't need a large array (use g_strdup_printf) */
    if (body->temp_filename == NULL) {
	gchar tmp_file_name[PATH_MAX + 1];

	libbalsa_lock_mutt();
	mutt_mktemp(tmp_file_name);
	libbalsa_unlock_mutt();

	body->temp_filename = g_strdup(tmp_file_name);
    }
    return libbalsa_message_body_save(body, prefix, body->temp_filename);
}

/* libbalsa_message_body_save:
   NOTE: has to use safe_fopen to set the file access privileges to safe.
*/
gboolean
libbalsa_message_body_save(LibBalsaMessageBody * body, gchar * prefix,
			   gchar * filename)
{
    FILE *stream;
    STATE s;

    stream =
	libbalsa_mailbox_get_message_stream(body->message->mailbox,
					    body->message);

    g_return_val_if_fail(stream != NULL, FALSE);

    fseek(stream, body->mutt_body->offset, 0);

    s.fpin = stream;

    s.prefix = prefix;
    s.fpout = safe_fopen(filename, "w");
    if (!s.fpout)
	return FALSE;

    libbalsa_lock_mutt();
    mutt_decode_attachment(body->mutt_body, &s);
    libbalsa_unlock_mutt();

    fflush(s.fpout);
    fclose(s.fpout);
    fclose(s.fpin);

    return TRUE;
}

gchar *
libbalsa_message_body_get_content_type(LibBalsaMessageBody * body)
{
    gchar *res;

    if (body->mutt_body->subtype)
	res =
	    g_strdup_printf("%s/%s", TYPE(body->mutt_body),
			    body->mutt_body->subtype);
    else
	res = g_strdup(TYPE(body->mutt_body));

    return res;
}

gboolean
libbalsa_message_body_is_multipart(LibBalsaMessageBody * body)
{
    return is_multipart(body->mutt_body);
}
