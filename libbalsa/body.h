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

#ifndef __LIBBALSA_BODY_H__
#define __LIBBALSA_BODY_H__


#include <stdio.h>

#include <glib.h>

typedef enum _LibBalsaMessageBodyType LibBalsaMessageBodyType;

enum _LibBalsaMessageBodyType {
    LIBBALSA_MESSAGE_BODY_TYPE_OTHER,
    LIBBALSA_MESSAGE_BODY_TYPE_AUDIO,
    LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION,
    LIBBALSA_MESSAGE_BODY_TYPE_IMAGE,
    LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE,
    LIBBALSA_MESSAGE_BODY_TYPE_MODEL,
    LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART,
    LIBBALSA_MESSAGE_BODY_TYPE_TEXT,
    LIBBALSA_MESSAGE_BODY_TYPE_VIDEO
};

struct _LibBalsaMessageBody {
    LibBalsaMessage *message;	/* The message of which this is a part */
    gchar *buffer;		/* holds raw data of the MIME part, or NULL */
    gchar *mime_type;           /* the mime type/subtype of buffer, or NULL, if plain */
    MuttBody *mutt_body;	/* pointer to BODY struct of mutt message */
    gchar *filename;		/* holds filename for attachments and such (used mostly for sending) */
    gboolean attach_as_extbody; /* if an attachment shall be appended as external-body (sending) */
    gchar *temp_filename;	/* Holds the filename of a the temporary file where this part is saved */
    gchar *charset;		/* the charset, used for sending, replying. */
    guint disposition;      /* content-disposition */

    LibBalsaMessageBody *next;	/* Next part in the message */
    LibBalsaMessageBody *parts;	/* The parts of a multipart or message/rfc822 message */
};

LibBalsaMessageBody *libbalsa_message_body_new(LibBalsaMessage * message);
void libbalsa_message_body_free(LibBalsaMessageBody * body);

LibBalsaMessageBodyType libbalsa_message_body_type(LibBalsaMessageBody *
						   body);

void libbalsa_message_body_set_mutt_body(LibBalsaMessageBody * body,
					 MuttBody * mutt_body);

gboolean libbalsa_message_body_save(LibBalsaMessageBody * body,
				    gchar * prefixm, gchar * filename);
gboolean libbalsa_message_body_save_temporary(LibBalsaMessageBody * body,
					      gchar * prefix);

gchar *libbalsa_message_body_get_parameter(LibBalsaMessageBody * body,
					   const gchar * param);
gchar *libbalsa_message_body_get_content_type(LibBalsaMessageBody * body);

gboolean libbalsa_message_body_is_multipart(LibBalsaMessageBody * body);
gboolean libbalsa_message_body_is_inline(LibBalsaMessageBody* body);

LibBalsaMessageBody*
libbalsa_message_body_get_by_id(LibBalsaMessageBody* body, const gchar* id);


#endif				/* __LIBBALSA_BODY_H__ */
