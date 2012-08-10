/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2009 Stuart Parmenter and others,
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <fcntl.h>

#include "libbalsa.h"
#include "libbalsa-vfs.h"
#include "misc.h"
#include <glib/gi18n.h>

LibBalsaMessageBody *
libbalsa_message_body_new(LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;

    body = g_new0(LibBalsaMessageBody, 1);

    body->message = message;
    body->buffer = NULL;
    body->html_buffer = NULL;
    body->embhdrs = NULL;
    body->content_type = NULL;
    body->filename = NULL;
    body->file_uri = NULL;
    body->temp_filename = NULL;
    body->charset = NULL;

#ifdef HAVE_GPGME
    body->was_encrypted = FALSE;
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
    g_free(body->html_buffer);
    libbalsa_message_headers_destroy(body->embhdrs);
    g_free(body->content_type);
    g_free(body->filename);
    if (body->file_uri)
        g_object_unref(body->file_uri);
    if (body->temp_filename) {
	unlink(body->temp_filename);
        g_free(body->temp_filename);
    }

    g_free(body->charset);

#ifdef HAVE_GPGME
    if (body->sig_info)
	g_object_unref(G_OBJECT(body->sig_info));
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
	ehdr->subject =
	    g_mime_utils_header_decode_text(subj);
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
    GMimeContentType *type;

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

    g_free(body->content_type);
    body->content_type = g_mime_content_type_to_string(type);
}

static LibBalsaMessageBody **
libbalsa_message_body_set_message_part(LibBalsaMessageBody * body,
				       LibBalsaMessageBody ** next_part)
{
    GMimeMessagePart *message_part;
    GMimeMessage *embedded_message;

    message_part = GMIME_MESSAGE_PART(body->mime_part);
    embedded_message = g_mime_message_part_get_message(message_part);

    if (embedded_message) {
        libbalsa_message_headers_destroy(body->embhdrs);
        body->embhdrs =
            libbalsa_message_body_extract_embedded_headers
            (embedded_message);
        if (!*next_part)
            *next_part = libbalsa_message_body_new(body->message);
        libbalsa_message_body_set_mime_body(*next_part,
                                            embedded_message->mime_part);
    }

    return *next_part ? &(*next_part)->next : next_part;
}

static LibBalsaMessageBody **
libbalsa_message_body_set_multipart(LibBalsaMessageBody * body,
				    LibBalsaMessageBody ** next_part)
{
    GMimeMultipart *multipart = GMIME_MULTIPART(body->mime_part);
    GMimeObject *part;
    int count, i;
    
    count = g_mime_multipart_get_count (multipart);
    for (i = 0; i < count; i++) {
	part = g_mime_multipart_get_part (multipart, i);
	if (!*next_part)
	    *next_part = libbalsa_message_body_new(body->message);
	libbalsa_message_body_set_mime_body(*next_part, part);
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
    GMimeContentType *type;
    gchar *res = NULL;

    g_return_val_if_fail(body != NULL, NULL);

    if (body->mime_part) {
	type = g_mime_object_get_content_type(body->mime_part);
	res = g_strdup(g_mime_content_type_get_parameter(type, param));
    } else if (body->content_type) {
	type = g_mime_content_type_new_from_string(body->content_type);
	res = g_strdup(g_mime_content_type_get_parameter(type, param));
	g_object_unref(type);
    }

    return res;
}

static gchar *
libbalsa_message_body_get_cid(LibBalsaMessageBody * body)
{
    const gchar *content_id = body->mime_part ?
        g_mime_object_get_content_id(body->mime_part) : body->content_id;

    if (content_id) {
        if (content_id[0] == '<'
            && content_id[strlen(content_id) - 1] == '>')
            return g_strndup(content_id + 1, strlen(content_id) - 2);
        return g_strdup(content_id);
    }

    return NULL;
}

/* libbalsa_message_body_save_temporary:
   check if body has already its copy in temporary file and if not,
   allocates a temporary file name and saves the body there.
*/
gboolean
libbalsa_message_body_save_temporary(LibBalsaMessageBody * body, GError **err)
{
    if (!body) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_ACCESS_ERROR,
                    "NULL message body");
        return FALSE;
    }

    if (body->temp_filename == NULL) {
        gchar *filename;
        gint fd = -1;
        GMimeStream *tmp_stream;

        filename = body->filename ?
            g_strdup(body->filename) : libbalsa_message_body_get_cid(body);

        if (!filename)
	    fd = g_file_open_tmp("balsa-body-XXXXXX",
                                 &body->temp_filename, err);
        else {
            const gchar *tempdir =
                libbalsa_message_get_tempdir(body->message);

            if (!tempdir) {
                g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                            LIBBALSA_MAILBOX_TEMPDIR_ERROR,
                            "Failed to create temporary directory");
            } else {
                body->temp_filename =
                    g_build_filename(tempdir, filename, NULL);
                fd = open(body->temp_filename,
                          O_WRONLY | O_EXCL | O_CREAT,
                          S_IRUSR);
            }
            g_free(filename);
        }

        if (fd < 0) {
            if (err && !*err)
                g_set_error(err, LIBBALSA_ERROR_QUARK, 1,
                            "Failed to create temporary file");
            return FALSE;
        }

        if ((tmp_stream = g_mime_stream_fs_new(fd)) != NULL)
            return libbalsa_message_body_save_stream(body, tmp_stream,
                                                     FALSE, err);
        else {
            g_set_error(err, LIBBALSA_ERROR_QUARK, 1,
                        _("Failed to create output stream"));
            close(fd);
            return FALSE;
        }
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
	    return libbalsa_message_body_save(body, body->temp_filename,
                                              S_IRUSR,
                                              FALSE, err);
    }
}

/* libbalsa_message_body_save:
   NOTE: has to use libbalsa_safe_open to set the file access privileges
   to safe.
*/
gboolean
libbalsa_message_body_save(LibBalsaMessageBody * body,
			   const gchar * filename, mode_t mode,
                           gboolean filter_crlf,
                           GError **err)
{
    int fd;
    int flags = O_CREAT | O_EXCL | O_WRONLY;
    GMimeStream *out_stream;

#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif

    if ((fd = libbalsa_safe_open(filename, flags, mode, err)) < 0)
	return FALSE;

    if ((out_stream = g_mime_stream_fs_new(fd)) != NULL)
        return libbalsa_message_body_save_stream(body, out_stream,
                                                 filter_crlf, err);

    /* could not create stream */
    g_set_error(err, LIBBALSA_ERROR_QUARK, 1,
                _("Failed to create output stream"));
    close(fd);
    return FALSE;
}


gboolean
libbalsa_message_body_save_vfs(LibBalsaMessageBody * body,
                               LibbalsaVfs * dest, mode_t mode,
                               gboolean filter_crlf,
                               GError **err)
{
    GMimeStream * out_stream;
    
    if (!(out_stream = libbalsa_vfs_create_stream(dest, mode, TRUE, err)))
        return FALSE;

    return libbalsa_message_body_save_stream(body, out_stream, filter_crlf, err);
}

static GMimeStream *
libbalsa_message_body_stream_add_filter(GMimeStream * stream,
                                        GMimeFilter * filter)
{
    if (!GMIME_IS_STREAM_FILTER(stream)) {
        GMimeStream *filtered_stream =
            g_mime_stream_filter_new(stream);
        g_object_unref(stream);
        stream = filtered_stream;
    }

    g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream), filter);
    g_object_unref(filter);

    return stream;
}

static GMimeStream *
libbalsa_message_body_get_part_stream(LibBalsaMessageBody * body,
                                      GError ** err)
{
    GMimeStream *stream;
    GMimeDataWrapper *wrapper;
    GMimeContentEncoding encoding;
    GMimeFilter *filter;
    gchar *mime_type = NULL;
    const gchar *charset;

    wrapper = g_mime_part_get_content_object(GMIME_PART(body->mime_part));
    if (!wrapper) {
        /* part is incomplete. */
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_ACCESS_ERROR,
                    "Internal error in get_stream");
        return NULL;
    }

    stream = g_object_ref(g_mime_data_wrapper_get_stream(wrapper));
    encoding = g_mime_data_wrapper_get_encoding(wrapper);

    switch (encoding) {
    case GMIME_CONTENT_ENCODING_BASE64:
    case GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE:
    case GMIME_CONTENT_ENCODING_UUENCODE:
        filter = g_mime_filter_basic_new(encoding, FALSE);
        stream = libbalsa_message_body_stream_add_filter(stream, filter);
        break;
    default:
        break;
    }

    /* convert text bodies but HTML - gtkhtml does conversion on its own. */
    if (libbalsa_message_body_type(body) == LIBBALSA_MESSAGE_BODY_TYPE_TEXT
        && strcmp(mime_type = libbalsa_message_body_get_mime_type(body),
                  "text/html") != 0
        && (charset = libbalsa_message_body_charset(body)) != NULL
        && g_ascii_strcasecmp(charset, "unknown-8bit") != 0) {
        GMimeStream *stream_null;
        GMimeStream *stream_filter;
        GMimeFilter *filter_windows;

        stream_null = g_mime_stream_null_new();
        stream_filter = g_mime_stream_filter_new(stream_null);
        g_object_unref(stream_null);

        filter_windows = g_mime_filter_windows_new(charset);
        g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter),
                                 filter_windows);

        libbalsa_mailbox_lock_store(body->message->mailbox);
        g_mime_stream_reset(stream);
        g_mime_stream_write_to_stream(stream, stream_filter);
        libbalsa_mailbox_unlock_store(body->message->mailbox);
        g_object_unref(stream_filter);

        charset = g_mime_filter_windows_real_charset(GMIME_FILTER_WINDOWS
                                                     (filter_windows));
        if ((filter = g_mime_filter_charset_new(charset, "UTF-8"))) {
            stream =
                libbalsa_message_body_stream_add_filter(stream, filter);
            g_free(body->charset);
            body->charset = g_strdup(charset);
        }
        g_object_unref(filter_windows);
    }

    g_free(mime_type);

    return stream;
}

static GMimeStream *
libbalsa_message_body_get_message_part_stream(LibBalsaMessageBody * body,
                                              GError ** err)
{
    GMimeStream *stream;
    ssize_t bytes_written;
    GMimeMessage *msg = g_mime_message_part_get_message
        (GMIME_MESSAGE_PART(body->mime_part));

    stream = g_mime_stream_mem_new();
    libbalsa_mailbox_lock_store(body->message->mailbox);
    bytes_written =
        g_mime_object_write_to_stream(GMIME_OBJECT(msg), stream);
    libbalsa_mailbox_unlock_store(body->message->mailbox);
    printf("Written %ld bytes of embedded message\n",
           (long) bytes_written);

    if (bytes_written < 0) {
        g_object_unref(stream);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_ACCESS_ERROR,
                    _("Could not read embedded message"));
        return NULL;
    }

    g_mime_stream_reset(stream);
    return stream;
}

GMimeStream *
libbalsa_message_body_get_stream(LibBalsaMessageBody * body, GError **err)
{
    g_return_val_if_fail(body != NULL, NULL);
    g_return_val_if_fail(body->message != NULL, NULL);

    if (!body->message->mailbox) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_ACCESS_ERROR,
                    "Internal error in get_stream");
        return NULL;
    }

    if (!libbalsa_mailbox_get_message_part(body->message, body, err)
        || !(GMIME_IS_PART(body->mime_part)
             || GMIME_IS_MESSAGE_PART(body->mime_part))) {
        if (err && !*err)
            g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_ACCESS_ERROR,
                        "Cannot get stream for part of type %s",
                        g_type_name(G_TYPE_FROM_INSTANCE
                                    (body->mime_part)));
        return NULL;
    }

    /* We handle "real" parts and embedded rfc822 messages
       differently. There is probably a way to unify if we use
       GMimeObject common denominator.  */
    if (GMIME_IS_MESSAGE_PART(body->mime_part))
        return libbalsa_message_body_get_message_part_stream(body, err);

    return libbalsa_message_body_get_part_stream(body, err);
}

gssize
libbalsa_message_body_get_content(LibBalsaMessageBody * body, gchar ** buf,
                                  GError **err)
{
    GMimeStream *stream, *stream_mem;
    GByteArray *array;
    gssize len;

    g_return_val_if_fail(body != NULL, -1);
    g_return_val_if_fail(body->message != NULL, -1);
    g_return_val_if_fail(buf != NULL, -1);

    *buf = NULL;
    stream = libbalsa_message_body_get_stream(body, err);
    if (!stream)
        return -1;

    array = g_byte_array_new();
    stream_mem = g_mime_stream_mem_new_with_byte_array(array);
    g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(stream_mem), FALSE);

    libbalsa_mailbox_lock_store(body->message->mailbox);
    g_mime_stream_reset(stream);
    len = g_mime_stream_write_to_stream(stream, stream_mem);
    libbalsa_mailbox_unlock_store(body->message->mailbox);
    g_object_unref(stream);
    g_object_unref(stream_mem);

    if (len >= 0) {
	guint8 zero = 0;
        len = array->len;
	/* NULL-terminate, in case it is used as a string. */
	g_byte_array_append(array, &zero, 1);
        *buf = (gchar *) g_byte_array_free(array, FALSE);
    } else {
        g_byte_array_free(array, TRUE);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_ACCESS_ERROR,
                    "Write error in get_content");
    }

    return len;
}

GdkPixbuf *
libbalsa_message_body_get_pixbuf(LibBalsaMessageBody * body, GError ** err)
{
    GMimeStream *stream;
    gchar *mime_type;
    GdkPixbufLoader *loader;
    GdkPixbuf *pixbuf = NULL;

    stream = libbalsa_message_body_get_stream(body, err);
    if (!stream)
        return pixbuf;

    libbalsa_mailbox_lock_store(body->message->mailbox);
    g_mime_stream_reset(stream);

    mime_type = libbalsa_message_body_get_mime_type(body);
    loader = gdk_pixbuf_loader_new_with_mime_type(mime_type, err);

#define ENABLE_WORKAROUND_FOR_IE_NON_IANA_MIME_TYPE TRUE
#if ENABLE_WORKAROUND_FOR_IE_NON_IANA_MIME_TYPE
    if (!loader
        && (!g_ascii_strcasecmp(mime_type, "image/pjpeg")
            || !g_ascii_strcasecmp(mime_type, "image/jpg"))) {
        g_clear_error(err);
        loader = gdk_pixbuf_loader_new_with_mime_type("image/jpeg", err);
    }
#endif                          /* ENABLE_WORKAROUND_FOR_IE_NON_IANA_MIME_TYPE */

    g_free(mime_type);

    if (loader) {
        gssize count;
        gchar buf[4096];

        while ((count = g_mime_stream_read(stream, buf, sizeof(buf))) > 0)
            if (!gdk_pixbuf_loader_write(loader, (guchar *) buf, count, err))
                break;

        if (!*err && gdk_pixbuf_loader_close(loader, err)) {
            pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
            pixbuf = gdk_pixbuf_apply_embedded_orientation(pixbuf);
        }

        g_object_unref(loader);
    }

    libbalsa_mailbox_unlock_store(body->message->mailbox);
    g_object_unref(stream);

    return pixbuf;
}

gboolean
libbalsa_message_body_save_stream(LibBalsaMessageBody * body,
                                  GMimeStream * dest, gboolean filter_crlf,
                                  GError ** err)
{
    GMimeStream *stream;
    ssize_t len;

    stream = libbalsa_message_body_get_stream(body, err);
    if (!body->mime_part)
        return FALSE;
    g_clear_error(err);

    libbalsa_mailbox_lock_store(body->message->mailbox);

    if (stream) {
        g_mime_stream_reset(stream);

        if (filter_crlf) {
            GMimeFilter *filter = g_mime_filter_crlf_new(FALSE, FALSE);
            stream =
                libbalsa_message_body_stream_add_filter(stream, filter);
        }

        len = g_mime_stream_write_to_stream(stream, dest);
        g_object_unref(stream);
    } else
        /* body->mime_part is neither a GMimePart nor a GMimeMessagePart. */
        len = g_mime_object_write_to_stream(body->mime_part, dest);

    libbalsa_mailbox_unlock_store(body->message->mailbox);
    g_object_unref(dest);

    if (len < 0)
        g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_ACCESS_ERROR,
                    "Write error in save_stream");

    return len >= 0;
}

gchar *
libbalsa_message_body_get_mime_type(LibBalsaMessageBody * body)
{
    gchar *res, *tmp;

    g_return_val_if_fail(body != NULL, NULL);

    if (!body->content_type)
	return g_strdup("text/plain");

    tmp = strchr(body->content_type, ';');
    res = g_ascii_strdown(body->content_type,
                          tmp ? tmp-body->content_type : -1);
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

    if (!disposition)
	/* Default disposition is in-line for text/plain, and generally
	 * attachment for other content types.  Default content type is
	 * text/plain except in multipart/digest, where it is
	 * message/rfc822; in either case, we want to in-line the part.
	 */
        return (body->content_type == NULL
                || g_ascii_strcasecmp(body->content_type,
                                      "message/rfc822") == 0
                || g_ascii_strcasecmp(body->content_type,
                                      "text/plain") == 0);

    return g_ascii_strncasecmp(disposition,
                               GMIME_DISPOSITION_INLINE,
                               strlen(GMIME_DISPOSITION_INLINE)) == 0;
}

/* libbalsa_message_body_is_flowed:
 * test whether a message body is format=flowed */
gboolean
libbalsa_message_body_is_flowed(LibBalsaMessageBody * body)
{
    gchar *content_type;
    gboolean flowed = FALSE;

    content_type = libbalsa_message_body_get_mime_type(body);
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
    gchar *cid;

    g_return_val_if_fail(id != NULL, NULL);

    if (!body)
	return NULL;

    if ((cid = libbalsa_message_body_get_cid(body))) {
        gboolean matches = !strcmp(id, cid);
        g_free(cid);
        if (matches)
            return body;
    }

    if ((res = libbalsa_message_body_get_by_id(body->parts, id)) != NULL)
	return res;

    return libbalsa_message_body_get_by_id(body->next, id);
}

#ifdef HAVE_GPGME
LibBalsaMsgProtectState
libbalsa_message_body_protect_state(LibBalsaMessageBody *body)
{
    if (!body || !body->sig_info ||
	body->sig_info->status == GPG_ERR_NOT_SIGNED ||
	body->sig_info->status == GPG_ERR_CANCELED)
	return LIBBALSA_MSG_PROTECT_NONE;

    if (body->sig_info->status == GPG_ERR_NO_ERROR) {
	/* good signature, check if the validity and trust (OpenPGP only) are
	   at least marginal */
	if (body->sig_info->validity >= GPGME_VALIDITY_MARGINAL &&
	    (body->sig_info->protocol == GPGME_PROTOCOL_CMS ||
	     body->sig_info->key->owner_trust >= GPGME_VALIDITY_MARGINAL))
	    return LIBBALSA_MSG_PROTECT_SIGN_GOOD;
	else
	    return LIBBALSA_MSG_PROTECT_SIGN_NOTRUST;
    }
    
    return LIBBALSA_MSG_PROTECT_SIGN_BAD;
}
#endif
