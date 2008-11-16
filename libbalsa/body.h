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
#include <sys/stat.h>

#include <glib.h>
#include <gmime/gmime.h>
#include <gdk/gdk.h>

#include "libbalsa-vfs.h"
#include "config.h"

#ifdef HAVE_GPGME
#include "gmime-gpgme-signature.h"
#endif

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

typedef enum _LibBalsaAttachMode LibBalsaAttachMode;

enum _LibBalsaAttachMode {
    LIBBALSA_ATTACH_AS_ATTACHMENT = 1,
    LIBBALSA_ATTACH_AS_INLINE,
    LIBBALSA_ATTACH_AS_EXTBODY
};

#define LIBBALSA_MESSAGE_BODY_SAFE (S_IRUSR | S_IWUSR)
#define LIBBALSA_MESSAGE_BODY_UNSAFE \
    (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
struct _LibBalsaMessageBody {
    LibBalsaMessage *message;	/* The message of which this is a part */
    /* FIXME: remove buffer and buf_len to decrease memory usage. */
    gchar *buffer;		/* holds raw data of the MIME part, or NULL */
    gchar *html_buffer;         /* holds the html representation of the part or NULL */
    ssize_t buflen;             /* size of the block */
    LibBalsaMessageHeaders *embhdrs;  /* headers of a message/rfc822 part */
    LibBalsaMessageBodyType body_type;
    gchar *content_type;        /* value of the Content-Type header of
                                 * the body including mime type with
                                 * optional parameters. NULL, if text/plain. */
    const gchar *content_dsp;	/* content-disposition */ 
    const gchar *content_id;    /* content-id */
    gchar *filename;		/* holds filename for attachments and such (used mostly for sending) */
    LibbalsaVfs * file_uri;     /* file uri for attachments (used for sending) */
    LibBalsaAttachMode attach_mode; /* attachment mode for sending */
    gchar *temp_filename;	/* Holds the filename of a the temporary file where this part is saved */
    gchar *charset;		/* the charset, used for sending, replying. */
    GMimeObject *mime_part;	/* mime body */

#ifdef HAVE_GPGME
    gboolean was_encrypted;
    GMimeGpgmeSigstat* sig_info;  /* info about a pgp signature body */
#endif

    LibBalsaMessageBody *next;	/* Next part in the message */
    LibBalsaMessageBody *parts;	/* The parts of a multipart or message/rfc822 message */
};

LibBalsaMessageBody *libbalsa_message_body_new(LibBalsaMessage * message);
void libbalsa_message_body_free(LibBalsaMessageBody * body);

LibBalsaMessageBodyType libbalsa_message_body_type(LibBalsaMessageBody *
						   body);

void libbalsa_message_body_set_mime_body(LibBalsaMessageBody * body,
					 GMimeObject * mime_part);

GMimeStream *libbalsa_message_body_get_stream(LibBalsaMessageBody * body,
                                              GError **err);
gssize libbalsa_message_body_get_content(LibBalsaMessageBody * body,
                                         gchar ** buf, GError **err);
GdkPixbuf *libbalsa_message_body_get_pixbuf(LibBalsaMessageBody * body,
                                            GError ** err);

gboolean libbalsa_message_body_save_stream(LibBalsaMessageBody * body,
                                           GMimeStream * dest,
                                           gboolean filter_crlf,
                                           GError **err);
gboolean libbalsa_message_body_save(LibBalsaMessageBody * body,
                                    const gchar * filename, mode_t mode,
                                    gboolean filter_crlf, GError **err);
gboolean libbalsa_message_body_save_vfs(LibBalsaMessageBody * body,
                                        LibbalsaVfs * dest,
                                        mode_t mode,
                                        gboolean filter_crlf,
                                        GError **err);
gboolean libbalsa_message_body_save_temporary(LibBalsaMessageBody * body,
                                              GError **err);

gchar *libbalsa_message_body_get_parameter(LibBalsaMessageBody * body,
					   const gchar * param);
gchar *libbalsa_message_body_get_mime_type(LibBalsaMessageBody * body);

gboolean libbalsa_message_body_is_multipart(LibBalsaMessageBody * body);
gboolean libbalsa_message_body_is_inline(LibBalsaMessageBody* body);
gboolean libbalsa_message_body_is_flowed(LibBalsaMessageBody * body);
gboolean libbalsa_message_body_is_delsp(LibBalsaMessageBody * body);

LibBalsaMessageBody *libbalsa_message_body_get_by_id(LibBalsaMessageBody *
                                                     body,
                                                     const gchar * id);

#ifdef HAVE_GPGME
LibBalsaMsgProtectState libbalsa_message_body_protect_state(LibBalsaMessageBody *body);
#endif


#endif				/* __LIBBALSA_BODY_H__ */
