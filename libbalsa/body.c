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

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <fcntl.h>
/* gnome-i18n.h needed for _() */
#include <libgnome/gnome-i18n.h>

#include "libbalsa.h"
#include "misc.h"

LibBalsaMessageBody *
libbalsa_message_body_new(LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;

    body = g_new0(LibBalsaMessageBody, 1);

    body->message = message;
    body->buffer = NULL;
    body->embhdrs = NULL;
    body->mime_type = NULL;
    body->filename = NULL;
    body->temp_filename = NULL;
    body->charset = NULL;

#ifdef HAVE_GPGME
    body->decrypt_file = NULL;
    body->sig_info = NULL;
#endif

    body->next = NULL;
    body->parts = NULL;

    body->mime_part = NULL;

    return body;
}


void
libbalsa_message_body_free(LibBalsaMessageBody * body)
{
    if (body == NULL)
	return;

    g_free(body->buffer);
    libbalsa_message_headers_destroy(body->embhdrs);
    g_free(body->mime_type);
    g_free(body->filename);

    if (body->temp_filename)
	unlink(body->temp_filename);
    g_free(body->temp_filename);

    g_free(body->charset);

#ifdef HAVE_GPGME
    g_free(body->decrypt_file);
    body->sig_info = libbalsa_signature_info_destroy(body->sig_info);
#endif

    libbalsa_message_body_free(body->next);
    libbalsa_message_body_free(body->parts);

    if (body->mime_part)
	g_object_unref(body->mime_part);	
    
    g_free(body);
}


static LibBalsaMessageHeaders *
libbalsa_message_body_extract_embedded_headers(GMimeMessage* msg)
{
    LibBalsaMessageHeaders *ehdr;
    const char *subj;
    int offset;

    ehdr = g_new0(LibBalsaMessageHeaders, 1);

    libbalsa_message_headers_from_gmime(ehdr, msg);
    ehdr->user_hdrs = libbalsa_message_user_hdrs_from_gmime(msg);

    subj = g_mime_message_get_subject(msg);
    if (subj) {
	ehdr->subject = g_mime_utils_8bit_header_decode(subj);
	libbalsa_utf8_sanitize(&ehdr->subject, TRUE, NULL);
    } else 
	ehdr->subject = g_strdup(_("(No subject)"));
    g_mime_message_get_date(msg, &ehdr->date, &offset);

    return ehdr;
}

/* Create a LibBalsaMessageBody with structure matching the GMimeObject;
 * the body may already have the necessary parts, so we check before
 * allocating new ones; it may have too many, so we free any that are no
 * longer needed.
 */

/* First some helpers. */
static void
libbalsa_message_body_set_filename(LibBalsaMessageBody * body)
{
    if (GMIME_IS_PART(body->mime_part)) {
	g_free(body->filename);
	body->filename =
	    g_strdup(g_mime_part_get_filename(GMIME_PART(body->mime_part)));
    }
}

static void
libbalsa_message_body_set_types(LibBalsaMessageBody * body)
{
    const GMimeContentType *type;

    type = g_mime_object_get_content_type(body->mime_part);
    if      (g_mime_content_type_is_type(type, "audio", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_AUDIO;
    else if (g_mime_content_type_is_type(type, "application", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION;
    else if (g_mime_content_type_is_type(type, "image", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_IMAGE;
    else if (g_mime_content_type_is_type(type, "message", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE;
    else if (g_mime_content_type_is_type(type, "model", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_MODEL;
    else if (g_mime_content_type_is_type(type, "multipart", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART;
    else if (g_mime_content_type_is_type(type, "text", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_TEXT;
    else if (g_mime_content_type_is_type(type, "video", "*"))
	body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_VIDEO;
    else body->body_type = LIBBALSA_MESSAGE_BODY_TYPE_OTHER;

    g_free(body->mime_type);
    body->mime_type = g_mime_content_type_to_string(type);
}

static LibBalsaMessageBody **
libbalsa_message_body_set_message_part(LibBalsaMessageBody * body,
				       LibBalsaMessageBody ** next_part)
{
    GMimeMessagePart *message_part;
    GMimeMessage *embedded_message;

    message_part = GMIME_MESSAGE_PART(body->mime_part);
    embedded_message = g_mime_message_part_get_message(message_part);

    libbalsa_message_headers_destroy(body->embhdrs);
    body->embhdrs =
	libbalsa_message_body_extract_embedded_headers(embedded_message);
    if (!*next_part)
	*next_part = libbalsa_message_body_new(body->message);
    libbalsa_message_body_set_mime_body(*next_part,
					embedded_message->mime_part);
    next_part = &(*next_part)->next;

    if (GMIME_IS_PART(embedded_message->mime_part))
	/* This part may not have a Content-Disposition header, but
	 * we must treat it as inline. */
	g_mime_part_set_content_disposition(GMIME_PART
					    (embedded_message->mime_part),
					    GMIME_DISPOSITION_INLINE);

    g_object_unref(embedded_message);

    return next_part;
}

static LibBalsaMessageBody **
libbalsa_message_body_set_multipart(LibBalsaMessageBody * body,
				    LibBalsaMessageBody ** next_part)
{
    GList *child;

    for (child = GMIME_MULTIPART(body->mime_part)->subparts; child;
	 child = child->next) {
	if (!*next_part)
	    *next_part = libbalsa_message_body_new(body->message);
	libbalsa_message_body_set_mime_body(*next_part, child->data);
	next_part = &(*next_part)->next;
    }

    return next_part;
}

static void
libbalsa_message_body_set_parts(LibBalsaMessageBody * body)
{
    LibBalsaMessageBody **next_part = &body->parts;

    if (GMIME_IS_MESSAGE_PART(body->mime_part))
	next_part = libbalsa_message_body_set_message_part(body, next_part);
    else if (GMIME_IS_MULTIPART(body->mime_part))
	next_part = libbalsa_message_body_set_multipart(body, next_part);

    /* Free any parts that weren't used; the test isn't strictly
     * necessary, but it should fail unless something really strange has
     * happened, so it's worth including. */
    if (*next_part) {
	libbalsa_message_body_free(*next_part);
	*next_part = NULL;
    }
}

void
libbalsa_message_body_set_mime_body(LibBalsaMessageBody * body,
				    GMimeObject * mime_part)
{
    g_return_if_fail(body != NULL);
    g_return_if_fail(GMIME_IS_OBJECT(mime_part));

    g_object_ref(mime_part);
    if (body->mime_part)
	g_object_unref(body->mime_part);
    body->mime_part = mime_part;

    libbalsa_message_body_set_filename(body);
    libbalsa_message_body_set_types(body);
    libbalsa_message_body_set_parts(body);
}

LibBalsaMessageBodyType
libbalsa_message_body_type(LibBalsaMessageBody * body)
{
    /* FIXME: this could be a virtual function... OR not? */
    return body->body_type;
}

gchar *
libbalsa_message_body_get_parameter(LibBalsaMessageBody * body,
				    const gchar * param)
{
    gchar *res;

    g_return_val_if_fail(body != NULL, NULL);

    if (body->mime_part) {
	const GMimeContentType *type =
	    g_mime_object_get_content_type(body->mime_part);
	res = g_strdup(g_mime_content_type_get_parameter(type, param));
    } else {
	GMimeContentType *type =
	    g_mime_content_type_new_from_string(body->mime_type);
	res = g_strdup(g_mime_content_type_get_parameter(type, param));
	g_mime_content_type_destroy(type);
    }

    return res;
}

/* libbalsa_message_body_save_temporary:
   check if body has already its copy in temporary file and if not,
   allocates a temporary file name and saves the body there.
*/
gboolean
libbalsa_message_body_save_temporary(LibBalsaMessageBody * body)
{
    if (body->temp_filename == NULL) {
	gint count = 100; /* Magic number, same as in g_mkstemp. */

	/* We want a temporary file with a name that ends with the same
	 * set of suffices as body->filename (for the benefit of helpers
	 * that depend on such things); however, g_file_open_tmp() works
	 * only with templates that end with "XXXXXX", so we fake it. */
	do {
	    gint fd;
	    gchar *tmp_file_name;
	    gchar *dotpos = NULL;

	    fd = g_file_open_tmp("balsa-body-XXXXXX", &tmp_file_name,
				 NULL);
	    if (fd < 0)
		return FALSE;
	    close(fd);
	    unlink(tmp_file_name);

	    if (body->filename) {
		gchar *seppos = strrchr(body->filename, G_DIR_SEPARATOR);
		dotpos = strchr(seppos ? seppos : body->filename, '.');
	    }
	    g_free(body->temp_filename);
	    if (dotpos) {
		body->temp_filename =
		    g_strdup_printf("%s%s", tmp_file_name, dotpos);
		g_free(tmp_file_name);
	    } else
		body->temp_filename = tmp_file_name;
	    fd = open(body->temp_filename, O_WRONLY | O_EXCL | O_CREAT, 0600);
	    if (fd >= 0)
		return libbalsa_message_body_save_fd(body, fd);
	} while (errno == EEXIST && --count > 0);

	/* Either we hit a real error, or we used up 100 attempts. */
	return FALSE;
    } else {
	/* the temporary name has been already allocated on previous
	   save_temporary action. We just check if the file is still there.
	*/
	struct stat s;
	if (stat(body->temp_filename, &s) == 0 && 
	    S_ISREG(s.st_mode) && 
	    s.st_uid == getuid())
	    return TRUE;
	else
	    return libbalsa_message_body_save(body, body->temp_filename);
    }
}

/* libbalsa_message_body_save:
   NOTE: has to use libbalsa_safe_open to set the file access privileges
   to safe.
*/
gboolean
libbalsa_message_body_save(LibBalsaMessageBody * body,
			   const gchar * filename)
{
    int fd;
    int flags = O_CREAT | O_EXCL | O_WRONLY;

#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    if ((fd=libbalsa_safe_open(filename, flags)) < 0)
	return FALSE;
    return libbalsa_message_body_save_fd(body, fd);
}

gboolean
libbalsa_message_body_save_fd(LibBalsaMessageBody * body, int fd)
{
    const char *buf;
    ssize_t len;
    GMimeStream *stream, *filter_stream;
    gchar *mime_type = NULL;
    const char *charset;
    GMimeFilter *filter;

    stream = g_mime_stream_fs_new(fd);
    /* convert text bodies but HTML - gtkhtml does conversion on its own. */
    if (libbalsa_message_body_type(body) == LIBBALSA_MESSAGE_BODY_TYPE_TEXT
	&& strcmp(mime_type = libbalsa_message_body_get_content_type(body),
			                  "text/html") != 0
	&& (charset = libbalsa_message_body_charset(body))
	&& g_ascii_strcasecmp(charset, "unknown-8bit") != 0
	&& (filter = g_mime_filter_charset_new(charset, "UTF-8")) != NULL) {
	filter_stream = g_mime_stream_filter_new_with_stream(stream);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
	g_object_unref(filter);
	g_object_unref(stream);
	stream = filter_stream;
    }
    g_free(mime_type);

    buf = libbalsa_mailbox_get_message_part(body->message, body, &len);
    if (len && (g_mime_stream_write(stream, buf, len) == -1
		|| g_mime_stream_flush(stream) == -1)) {
	g_object_unref(stream);
	/* FIXME: unlink??? */
	return FALSE;
    }

    g_object_unref(stream);

    return TRUE;
}

gchar *
libbalsa_message_body_get_content_type(LibBalsaMessageBody * body)
{
    gchar *res;
#ifdef OLD_CODE
    gchar *tmp;
    const GMimeContentType *type=g_mime_object_get_content_type(body->mime_part);
    tmp=g_mime_content_type_to_string(type);

    res = g_ascii_strdown(tmp, -1);
    g_free(tmp);
#else
    res = g_ascii_strdown(body->mime_type, -1);
#endif
    return res;
}

gboolean
libbalsa_message_body_is_multipart(LibBalsaMessageBody * body)
{
    return body->mime_part ?
	GMIME_IS_MULTIPART(body->mime_part) :
	body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART;
}

gboolean
libbalsa_message_body_is_inline(LibBalsaMessageBody * body)
{
    const gchar *disposition;

    g_return_val_if_fail(body->mime_part == NULL ||
			 GMIME_IS_OBJECT(body->mime_part), FALSE);

    if (body->mime_part)
	disposition = g_mime_object_get_header(body->mime_part,
					       "Content-Disposition");
    else
	disposition = body->content_dsp;

    return (disposition
	    && g_ascii_strncasecmp(disposition,
				   GMIME_DISPOSITION_INLINE,
				   strlen(GMIME_DISPOSITION_INLINE)) == 0);
}

/* libbalsa_message_body_is_flowed:
 * test whether a message body is format=flowed */
gboolean
libbalsa_message_body_is_flowed(LibBalsaMessageBody * body)
{
    gchar *content_type;
    gboolean flowed = FALSE;

    content_type = libbalsa_message_body_get_content_type(body);
    if (g_ascii_strcasecmp(content_type, "text/plain") == 0) {
	gchar *format =
	    libbalsa_message_body_get_parameter(body, "format");

	if (format) {
	    flowed = (g_ascii_strcasecmp(format, "flowed") == 0);
	    g_free(format);
	}
    }
    g_free(content_type);

    return flowed;
}

gboolean
libbalsa_message_body_is_delsp(LibBalsaMessageBody * body)
{
    gboolean delsp = FALSE;

    if (libbalsa_message_body_is_flowed(body)) {
	gchar *delsp_param =
	    libbalsa_message_body_get_parameter(body, "delsp");

	if (delsp_param) {
	    delsp = (g_ascii_strcasecmp(delsp_param, "yes") == 0);
	    g_free(delsp_param);
	}
    }

    return delsp;
}
    
LibBalsaMessageBody *
libbalsa_message_body_get_by_id(LibBalsaMessageBody * body,
				const gchar * id)
{
    LibBalsaMessageBody *res;

    g_return_val_if_fail(id != NULL, NULL);

    if (!body)
	return NULL;

    if (body->mime_part) {
	const gchar *bodyid =
	    g_mime_object_get_content_id(body->mime_part);

	if (bodyid && strcmp(id, bodyid) == 0)
	    return body;
    }

    if ((res = libbalsa_message_body_get_by_id(body->parts, id)) != NULL)
	return res;

    return libbalsa_message_body_get_by_id(body->next, id);
}
