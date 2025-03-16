/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
#include "gmime-part-rfc2440.h"
#include "libbalsa-gpgme.h"
#include "html.h"

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

    body->was_encrypted = FALSE;
    body->sig_info = NULL;

    body->next = NULL;
    body->parts = NULL;

    body->mime_part = NULL;

    return body;
}

#ifdef HAVE_HTML_WIDGET
static void
body_weak_notify(gpointer  data,
                 GObject  *key)
{
    LibBalsaMessageBody *body = data;

    g_hash_table_remove(body->selection_table, key);
}

static void
selection_table_foreach(gpointer key,
                        gpointer value,
                        gpointer user_data)
{
    g_object_weak_unref(key, body_weak_notify, user_data);
}
#endif /* HAVE_HTML_WIDGET */

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

    if (body->sig_info)
	g_object_unref(body->sig_info);
    if (body->dkim != NULL) {
	g_object_unref(body->dkim);
    }
    libbalsa_message_body_free(body->next);
    libbalsa_message_body_free(body->parts);

    if (body->mime_part)
	g_object_unref(body->mime_part);

#ifdef HAVE_HTML_WIDGET
    if (body->selection_table != NULL) {
        g_hash_table_foreach(body->selection_table, selection_table_foreach, body);
        g_hash_table_destroy(body->selection_table);
    }
#endif  /* HAVE_HTML_WIDGET */

    g_free(body);
}


static LibBalsaMessageHeaders *
libbalsa_message_body_extract_embedded_headers(GMimeMessage* msg)
{
    LibBalsaMessageHeaders *ehdr;
    const char *subj;
    GDateTime *datetime;

    ehdr = g_new0(LibBalsaMessageHeaders, 1);

    libbalsa_message_headers_from_gmime(ehdr, msg);
    ehdr->user_hdrs = libbalsa_message_user_hdrs_from_gmime(msg);

    subj = g_mime_message_get_subject(msg);
    if (subj) {
	ehdr->subject =
	    g_mime_utils_header_decode_text(libbalsa_parser_options(), subj);
	libbalsa_utf8_sanitize(&ehdr->subject, TRUE, NULL);
    } else
	ehdr->subject = g_strdup(_("(No subject)"));

    datetime = g_mime_message_get_date(msg);
    if (datetime != NULL)
        ehdr->date = g_date_time_to_unix(datetime);

    return ehdr;
}

/* Create a LibBalsaMessageBody with structure matching the GMimeObject;
 * the body may already have the necessary parts, so we check before
 * allocating new ones; it may have too many, so we free any that are no
 * longer needed.
 */

/* First some helpers. */

/* Set body->filename, if possible.
 *
 * If it is not NULL, GMime documents that the returned string will be in UTF-8.
 */
static void
libbalsa_message_body_set_filename(LibBalsaMessageBody * body)
{
    gchar *access_type;
    gchar *filename = NULL;

    access_type = libbalsa_message_body_get_parameter(body, "access-type");

    /* In either case, a UTF-8 string: */
    if (access_type != NULL && g_ascii_strcasecmp(access_type, "URL") == 0)
        filename = libbalsa_message_body_get_parameter(body, "URL");
    else if (GMIME_IS_PART(body->mime_part))
	filename = g_strdup(g_mime_part_get_filename((GMimePart *) body->mime_part));

    g_free(access_type);

    g_free(body->filename);
    body->filename = filename;
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
    body->content_type = g_mime_content_type_get_mime_type(type);
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
        if (!*next_part) {
            *next_part = libbalsa_message_body_new(body->message);
            (*next_part)->parent = body;
        }
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
	if (!*next_part) {
	    *next_part = libbalsa_message_body_new(body->message);
	    (*next_part)->parent = body;
	}
	libbalsa_message_body_set_mime_body(*next_part, part);
	next_part = &(*next_part)->next;
    }

    return next_part;
}

static void
libbalsa_message_body_set_text_rfc822headers(LibBalsaMessageBody *body)
{
    LibBalsaMailbox *mailbox;
    GMimeStream *headers;

    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);
    headers = libbalsa_message_body_get_stream(body, NULL);

    if (headers != NULL) {
	GMimeMessage *dummy_msg;
	GMimeParser *parser;

	g_mime_stream_reset(headers);
	parser = g_mime_parser_new_with_stream(headers);
        g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);
	g_object_unref(headers);
	dummy_msg = g_mime_parser_construct_message(parser, libbalsa_parser_options());
	g_object_unref(parser);

	body->embhdrs = libbalsa_message_body_extract_embedded_headers(dummy_msg);
	g_object_unref(dummy_msg);
    }

    libbalsa_mailbox_unlock_store(mailbox);
}

static void
libbalsa_message_body_set_parts(LibBalsaMessageBody * body)
{
    LibBalsaMessageBody **next_part = &body->parts;

    if (GMIME_IS_MESSAGE_PART(body->mime_part))
	next_part = libbalsa_message_body_set_message_part(body, next_part);
    else if (GMIME_IS_MULTIPART(body->mime_part))
	next_part = libbalsa_message_body_set_multipart(body, next_part);
    else {
	gchar *mime_type;

	mime_type = libbalsa_message_body_get_mime_type(body);
	if (strcmp(mime_type, "text/rfc822-headers") == 0)
	    libbalsa_message_body_set_text_rfc822headers(body);
	g_free(mime_type);
    }

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

/* Get a content-type parameter.
 *
 * GMime documents that if the parameter is set, the
 * returned string will be in UTF-8.
 */

gchar *
libbalsa_message_body_get_parameter(const LibBalsaMessageBody * body,
				    const gchar * param)
{
    GMimeContentType *type;
    gchar *res = NULL;

    g_return_val_if_fail(body != NULL, NULL);

    if (body->mime_part) {
	type = g_mime_object_get_content_type(body->mime_part);
	res = g_strdup(g_mime_content_type_get_parameter(type, param));
    } else if (body->content_type) {
	type = g_mime_content_type_parse(libbalsa_parser_options(), body->content_type);
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
        gboolean retval;

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
                gchar *basename;

                basename = g_path_get_basename(filename);
                body->temp_filename =
                    g_build_filename(tempdir, basename, NULL);
                g_free(basename);

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

        retval = libbalsa_message_body_save_fs(body, fd, FALSE, NULL, err);
        close(fd);

        return retval;
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
*/
gboolean
libbalsa_message_body_save(LibBalsaMessageBody * body,
			   const gchar * filename, mode_t mode,
                           gboolean filter_crlf,
                           GError **err)
{
    int fd;
    int flags = O_CREAT | O_EXCL | O_WRONLY;
    gboolean retval;

    if ((fd = open(filename, flags, mode)) < 0) {
        int errsv = errno;
        g_set_error(err, LIBBALSA_ERROR_QUARK, errsv,
                    _("Cannot open %s: %s"), filename, g_strerror(errsv));
	return FALSE;
    }

    retval = libbalsa_message_body_save_fs(body, fd, filter_crlf, NULL, err);
    close(fd);

    return retval;
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

    wrapper = g_mime_part_get_content(GMIME_PART(body->mime_part));
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

    /* convert text bodies but HTML - WebKit 2 does conversion on its own. */
    if (libbalsa_message_body_type(body) == LIBBALSA_MESSAGE_BODY_TYPE_TEXT
        && strcmp(mime_type = libbalsa_message_body_get_mime_type(body),
                  "text/html") != 0
        && (charset = libbalsa_message_body_charset(body)) != NULL
        && g_ascii_strcasecmp(charset, "unknown-8bit") != 0) {
        GMimeStream *stream_null;
        GMimeStream *stream_filter;
        GMimeFilter *filter_windows;
        LibBalsaMailbox *mailbox;


        stream_null = g_mime_stream_null_new();
        stream_filter = g_mime_stream_filter_new(stream_null);
        g_object_unref(stream_null);

        filter_windows = g_mime_filter_windows_new(charset);
        g_mime_stream_filter_add(GMIME_STREAM_FILTER(stream_filter),
                                 filter_windows);

        mailbox = libbalsa_message_get_mailbox(body->message);
        libbalsa_mailbox_lock_store(mailbox);
        g_mime_stream_reset(stream);
        g_mime_stream_write_to_stream(stream, stream_filter);
        libbalsa_mailbox_unlock_store(mailbox);
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
    LibBalsaMailbox *mailbox;
    ssize_t bytes_written;
    GMimeMessage *msg = g_mime_message_part_get_message
        (GMIME_MESSAGE_PART(body->mime_part));

    stream = g_mime_stream_mem_new();
    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);
    bytes_written =
        g_mime_object_write_to_stream(GMIME_OBJECT(msg), NULL, stream);
    libbalsa_mailbox_unlock_store(mailbox);
    g_debug("Written %ld bytes of embedded message",
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

    if (libbalsa_message_get_mailbox(body->message) == NULL) {
        if (err != NULL && *err == NULL) {
            g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_ACCESS_ERROR,
                        "Internal error in get_stream");
        }
        return NULL;
    }

    if (!libbalsa_mailbox_get_message_part(body->message, body, err)
        || body->mime_part == NULL) {
        if (err != NULL && *err == NULL) {
            g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_ACCESS_ERROR,
                        "Cannot get stream for part");
        }
        return NULL;
    }

    if (!(GMIME_IS_PART(body->mime_part)
          || GMIME_IS_MESSAGE_PART(body->mime_part))) {
        if (err != NULL && *err == NULL) {
            g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_ACCESS_ERROR,
                        "Cannot get stream for part of type %s",
                        g_type_name(G_TYPE_FROM_INSTANCE
                                    (body->mime_part)));
        }
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
    LibBalsaMailbox *mailbox;
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

    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);
    g_mime_stream_reset(stream);
    len = g_mime_stream_write_to_stream(stream, stream_mem);
    libbalsa_mailbox_unlock_store(mailbox);
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
    LibBalsaMailbox *mailbox;
    gchar *mime_type;
    GdkPixbufLoader *loader;
    GdkPixbuf *pixbuf = NULL;

    stream = libbalsa_message_body_get_stream(body, err);
    if (!stream)
        return pixbuf;

    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);
    g_mime_stream_reset(stream);

    mime_type = libbalsa_message_body_get_mime_type(body);
    loader = gdk_pixbuf_loader_new_with_mime_type(mime_type, err);

#define ENABLE_WORKAROUND_FOR_IE_NON_IANA_MIME_TYPE TRUE
#if ENABLE_WORKAROUND_FOR_IE_NON_IANA_MIME_TYPE
    if (loader == NULL
        && (strcmp(mime_type, "image/pjpeg") == 0 ||
            strcmp(mime_type, "image/jpg") == 0)) {
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

    libbalsa_mailbox_unlock_store(mailbox);
    g_object_unref(stream);

    return pixbuf;
}

gboolean
libbalsa_message_body_save_stream(LibBalsaMessageBody * body,
                                  GMimeStream * dest, gboolean filter_crlf,
                                  ssize_t             *bytes_written,
                                  GError ** err)
{
    GMimeStream *stream;
    LibBalsaMailbox *mailbox;
    ssize_t len;

    stream = libbalsa_message_body_get_stream(body, err);
    if (!body->mime_part)
        return FALSE;
    g_clear_error(err);

    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);

    if (stream) {
        g_mime_stream_reset(stream);

        if (filter_crlf) {
            GMimeFilter *filter = g_mime_filter_dos2unix_new(FALSE);

            stream =
                libbalsa_message_body_stream_add_filter(stream, filter);
        }

        len = g_mime_stream_write_to_stream(stream, dest);
        g_object_unref(stream);
    } else
        /* body->mime_part is neither a GMimePart nor a GMimeMessagePart. */
        len = g_mime_object_write_to_stream(body->mime_part, NULL, dest);

    libbalsa_mailbox_unlock_store(mailbox);
    g_object_unref(dest);

    if (len < 0) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_ACCESS_ERROR,
                    "Write error in save_stream");
    } else {
        if (bytes_written != NULL)
            *bytes_written = len;
    }

    return len >= 0;
}

gboolean
libbalsa_message_body_save_gio(LibBalsaMessageBody *body,
                               GFile               *dest_file,
                               gboolean             filter_crlf,
                               ssize_t             *bytes_written,
                               GError             **err)
{
    GMimeStream *dest;

    g_return_val_if_fail(body != NULL, FALSE);
    g_return_val_if_fail(G_IS_FILE(dest_file), FALSE);

    dest = g_mime_stream_gio_new(dest_file); /* Never NULL */

    /* Caller owns the reference to dest_file: */
    g_mime_stream_gio_set_owner(GMIME_STREAM_GIO(dest), FALSE);

    return libbalsa_message_body_save_stream(body, dest /* takes ownership */,
                                             filter_crlf, bytes_written, err);
}

gboolean
libbalsa_message_body_save_fs(LibBalsaMessageBody *body,
                              int                  fd,
                              gboolean             filter_crlf,
                              ssize_t             *bytes_written,
                              GError             **err)
{
    GMimeStream *dest;

    g_return_val_if_fail(body != NULL, FALSE);
    g_return_val_if_fail(fd >= 0, FALSE);

    dest = g_mime_stream_fs_new(fd); /* Never NULL */

    /* Caller owns the file descriptor: */
    g_mime_stream_fs_set_owner(GMIME_STREAM_FS(dest), FALSE);

    return libbalsa_message_body_save_stream(body, dest /* takes ownership */,
                                             filter_crlf, bytes_written, err);
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
    if (strcmp(content_type, "text/plain") == 0) {
	gchar *format =
	    libbalsa_message_body_get_parameter(body, "format");

	if (format != NULL) {
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

/** @brief Find the root object of a multipart/related
 *
 * @param body multipart/related body
 * @return the root part
 *
 * A multipart/related (typical case: a HTML with inlined images) @em may have a "start" Content-Type parameter, indicating the
 * Content-ID of the part containing the "root" of the compound object.  If the parameter is missing, the root object is the very
 * first child of the container (see RFC 2387, Sect. 3.2).
 *
 * RFC 2387, Sect. 3.1 defines that the multipart/related @em must have a "type" parameter, giving the Content-Type of the root
 * object, but in practice it is often omitted.
 */
LibBalsaMessageBody *
libbalsa_message_body_mp_related_root(LibBalsaMessageBody *body)
{
	gchar *conttype;
	LibBalsaMessageBody *root_body = NULL;

    /* note: checking for a non-NULL body and for the proper content-type is somewhat paranoid... */
    g_return_val_if_fail(body != NULL, NULL);

    conttype = libbalsa_message_body_get_mime_type(body);
    if (strcmp(conttype, "multipart/related") == 0) {
		gchar *start_cont_id;

		/* get the "start" parameter, and identify matching child */
		start_cont_id = libbalsa_message_body_get_parameter(body, "start");
		if ((start_cont_id != NULL) && (body->parts != NULL)) {
			root_body = libbalsa_message_body_get_by_id(body->parts, start_cont_id);
		}
		g_free(start_cont_id);

		/* fall back to first child if either the parameter is missing or if the child cannot be identified */
		if (root_body == NULL) {
			root_body = body->parts;
		}
    }
    g_free(conttype);
    return root_body;
}


#ifdef HAVE_HTML_WIDGET

/** @brief Find the multipart/alternative parent of a body
 *
 * @param[in] body message body
 * @return the body's multipart/alternative parent, NULL if it is not the child of such a part
 *
 * Return the multipart/alternative parent of the passed body, which @em must be either the direct parent, or if the body's direct
 * parent is a multipart/related the direct parent of the latter.
 */
static inline LibBalsaMessageBody *
find_mp_alt_parent(const LibBalsaMessageBody *body)
{
	LibBalsaMessageBody *mp_alt = NULL;

	mp_alt = body->parent;
	if ((mp_alt != NULL) && (mp_alt->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART)) {
		gchar *conttype;

		conttype = libbalsa_message_body_get_mime_type(mp_alt);
		if (strcmp(conttype, "multipart/related") == 0) {
			g_free(conttype);
			mp_alt = mp_alt->parent;
			if (mp_alt != NULL) {
				conttype = libbalsa_message_body_get_mime_type(mp_alt);
			} else {
				conttype = NULL;
			}
		}

		if ((conttype != NULL) && strcmp(conttype, "multipart/alternative") != 0) {
			mp_alt = NULL;
		}
		g_free(conttype);
	}

	return mp_alt;
}

/** @brief Set if a multipart/alternative HTML or plain part is selected
 *
 * @param[in] body message body
 *
 * Iff the passed body is the child of a multipart/alternative set the LibBalsaMessageBody::html_selected property of the latter if
 * the body's content type is a HTML type.
 *
 * @sa find_mp_alt_parent(), libbalsa_html_type()
 */
void
libbalsa_message_body_set_mp_alt_selection(LibBalsaMessageBody *body, gpointer key)
{
	LibBalsaMessageBody *mp_alt_body;

	if ((body != NULL) && (body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_TEXT)) {
		mp_alt_body = find_mp_alt_parent(body);
		if (mp_alt_body != NULL) {
			gchar *conttype;
                        LibBalsaMpAltSelection selection;

			conttype = libbalsa_message_body_get_mime_type(body);
			if (libbalsa_html_type(conttype) != LIBBALSA_HTML_TYPE_NONE) {
				selection = LIBBALSA_MP_ALT_HTML;
			} else {
				selection = LIBBALSA_MP_ALT_PLAIN;
			}
			g_free(conttype);

                        /* Remember the most recent selection: */
                        mp_alt_body->mp_alt_selection = selection;

                        if (mp_alt_body->selection_table == NULL)
                            mp_alt_body->selection_table = g_hash_table_new(NULL, NULL);

                        /* Remember the most recent selection for this key: */
                        if (g_hash_table_insert(mp_alt_body->selection_table, key,
                                                GINT_TO_POINTER(selection))) {
                            g_object_weak_ref(key, body_weak_notify, mp_alt_body);
                        }
		}
	}
}

static inline gboolean body_is_type(const LibBalsaMessageBody *body,
                                    const gchar               *type,
                                    const gchar               *sub_type);

/** @brief Check if a multipart/alternative HTML or plain part is selected
 *
 * @param[in] body message body
 * @return which part of a multipart/alternative is selected, @ref LIBBALSA_MP_ALT_AUTO if @em body id not part of a
 *         multipart/alternative or if the selection shall be done automatically
 * @sa find_mp_alt_parent()
 */
LibBalsaMpAltSelection
libbalsa_message_body_get_mp_alt_selection(LibBalsaMessageBody *body, gpointer key)
{
	LibBalsaMpAltSelection selection;

	g_return_val_if_fail(body != NULL, LIBBALSA_MP_ALT_AUTO);

	if (!body_is_type(body, "multipart", "alternative"))
            body = find_mp_alt_parent(body);

	if (body == NULL) {
            selection = LIBBALSA_MP_ALT_AUTO;
        } else {
            if (body->selection_table != NULL && g_hash_table_contains(body->selection_table, key)) {
                /* The part is currently being viewed, so return the
                 * selection that was used to view it: */
		selection = GPOINTER_TO_INT(g_hash_table_lookup(body->selection_table, key));
            } else {
                /* The part is not currently being viewed, so return the
                 * most recent selection: */
                selection = body->mp_alt_selection;
            }
        }

	return selection;
}

#endif /*HAVE_HTML_WIDGET*/


/** Basic requirements for a multipart crypto: protocol is not NULL, exactly two body parts */
#define MP_CRYPT_STRUCTURE(part, protocol)										\
	(((protocol) != NULL) && ((body)->parts != NULL) && 						\
	 ((body)->parts->next != NULL) && ((body)->parts->next->next == NULL))

/** Check if protocol is application/subtype, and that part has the same MIME type */
#define IS_PROT_PART(part, protocol, subtype)									\
	((g_ascii_strcasecmp("application/" subtype, protocol) == 0) &&				\
	 body_is_type(part, "application", subtype))

static inline gboolean
body_is_type(const LibBalsaMessageBody *body,
			 const gchar               *type,
			 const gchar               *sub_type)
{
	gboolean retval;
	GMimeContentType *content_type;

	if (body->mime_part) {
		content_type = g_mime_object_get_content_type(body->mime_part);
		retval = g_mime_content_type_is_type(content_type, type, sub_type);
	} else {
		content_type = g_mime_content_type_parse(libbalsa_parser_options(), body->content_type);
		retval = g_mime_content_type_is_type(content_type, type, sub_type);
		g_object_unref(content_type);
	}

	return retval;
}

/** @brief Get the basic protection mode of a body
 *
 * @param body message body
 * @return a bit mask indicating the protection state of the body
 *
 * Check the structure and parameters of the passed body, and return a bit mask indicating if it is signed or encrypted, and which
 * protocol is used.  @ref LIBBALSA_PROTECT_ERROR indicates obvious errors.  The bit mask does @em not include the validity of a
 * signature (see libbalsa_message_body_signature_state() for this purpose).
 */
guint
libbalsa_message_body_protect_mode(const LibBalsaMessageBody * body)
{
	guint result;

	g_return_val_if_fail(body != NULL, 0);
	g_return_val_if_fail(body->content_type != NULL, 0);

	if (body_is_type(body, "multipart", "signed")) {
		/* multipart/signed (PGP/MIME, S/MIME) must have a protocol and a micalg parameter */
		gchar *protocol = libbalsa_message_body_get_parameter(body, "protocol");
		gchar *micalg = libbalsa_message_body_get_parameter(body, "micalg");

		result = LIBBALSA_PROTECT_SIGN;
		if (MP_CRYPT_STRUCTURE(body, protocol)) {
			if (IS_PROT_PART(body->parts->next, protocol, "pkcs7-signature") ||
				IS_PROT_PART(body->parts->next, protocol, "x-pkcs7-signature")) {
				result |= LIBBALSA_PROTECT_SMIME;
				if (micalg == NULL) {
					result |= LIBBALSA_PROTECT_ERROR;
				}
			} else if (IS_PROT_PART(body->parts->next, protocol, "pgp-signature")) {
				result |= LIBBALSA_PROTECT_RFC3156;
				if ((micalg == NULL) || (g_ascii_strncasecmp("pgp-", micalg, 4) != 0)) {
					result |= LIBBALSA_PROTECT_ERROR;
				}
			} else {
				result |= LIBBALSA_PROTECT_ERROR;
			}
		} else {
			result |= LIBBALSA_PROTECT_ERROR;
		}
		g_free(micalg);
		g_free(protocol);
	} else if (body_is_type(body, "multipart", "encrypted")) {
		/* multipart/signed (PGP/MIME, S/MIME) must have a protocol parameter */
		gchar *protocol = libbalsa_message_body_get_parameter(body, "protocol");

		result = LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_RFC3156;
		if (!MP_CRYPT_STRUCTURE(body, protocol) ||
			!IS_PROT_PART(body->parts, protocol, "pgp-encrypted") ||
			!body_is_type(body->parts->next, "application", "octet-stream")) {
			result |= LIBBALSA_PROTECT_ERROR;
		}
		g_free(protocol);
	} else if (body_is_type(body, "application", "pkcs7-mime") ||
			   body_is_type(body, "application", "x-pkcs7-mime")) {
		/* multipart/pkcs7-mime (S/MIME) must have a smime-type parameter */
		gchar *smime_type = libbalsa_message_body_get_parameter(body, "smime-type");

		result = LIBBALSA_PROTECT_SMIME;
		if ((g_ascii_strcasecmp("enveloped-data", smime_type) == 0) ||
			(g_ascii_strcasecmp("signed-data", smime_type) == 0)) {
			result |= LIBBALSA_PROTECT_ENCRYPT;
		} else {
			result |= LIBBALSA_PROTECT_ERROR;
		}
		g_free(smime_type);
	} else {
		result = LIBBALSA_PROTECT_NONE;
	}

	return result;
}

/** @brief Get the cryptographic signature state of a body
 *
 * @param body message body
 * @return a value indicating the cryptographic signature state of the body
 */
guint
libbalsa_message_body_signature_state(const LibBalsaMessageBody *body)
{
	guint state;

	if ((body == NULL) || (body->sig_info == NULL) ||
		(g_mime_gpgme_sigstat_status(body->sig_info) == GPG_ERR_NOT_SIGNED) ||
		(g_mime_gpgme_sigstat_status(body->sig_info) == GPG_ERR_CANCELED)) {
		state = LIBBALSA_PROTECT_NONE;
	} else if (g_mime_gpgme_sigstat_status(body->sig_info) != GPG_ERR_NO_ERROR) {
		state = LIBBALSA_PROTECT_SIGN_BAD;
	} else if ((g_mime_gpgme_sigstat_summary(body->sig_info) & GPGME_SIGSUM_VALID) == GPGME_SIGSUM_VALID) {
		state = LIBBALSA_PROTECT_SIGN_GOOD;
	} else {
		state = LIBBALSA_PROTECT_SIGN_NOTRUST;
	}

	return state;
}

/** \brief Check if a body is a multipart/signed with a valid signature
 *
 * \param body message body (part)
 * \return TRUE if the body is a valid multipart/singed, fFALSE if not
 *
 * The body is a valid multipart/signed if it has the proper content type, exactly two sub-parts, of which the second has a
 * \ref GMimeGpgmeSigstat which is not \ref GPG_ERR_NOT_SIGNED.  This applies to both OpenPGP (RFC 3156) and to S/MIME (RFC 8551)
 * multipart/signed parts.
 */
gboolean
libbalsa_message_body_multipart_signed(const LibBalsaMessageBody *body)
{
	return (body != NULL) &&
			(body->content_type != NULL) &&
			(g_ascii_strcasecmp(body->content_type, "multipart/signed") == 0) &&
			(body->parts != NULL) &&					/* must have children */
			(body->parts->next != NULL) &&				/* must have *two* child parts... */
			(body->parts->next->next == NULL) &&		/* ...but not more */
			(body->parts->next->sig_info != NULL) &&
			(g_mime_gpgme_sigstat_status(body->parts->next->sig_info) != GPG_ERR_NOT_SIGNED);
}


/** \brief Check if a body is signed inline with a valid signature
 *
 * \param body message body (part)
 * \return TRUE if the body is an inlined singed part, FALSE if not
 *
 * The body is a valid inlined signed part if has a \ref GMimeGpgmeSigstat which is not \ref GPG_ERR_NOT_SIGNED.  This applies to
 * - RFC 4880 message parts,
 * - OpenPGP combined signed and encrypted parts (RFC 3156 sect. 6.2),
 * - S/MIME application/pkcs7-mime "SignedData" parts (RFC 8551 sect. 3.5.2).
 */
gboolean
libbalsa_message_body_inline_signed(const LibBalsaMessageBody *body)
{
	return (body != NULL) &&
			(body->sig_info != NULL) &&
			(g_mime_gpgme_sigstat_status(body->sig_info) != GPG_ERR_NOT_SIGNED);
}


/** \brief Check if a body contains any crypto content
 *
 * \param body message body
 * \return TRUE if any crypto content has been found
 *
 * Return if the passed body chain or any of its children contains any crypto content which may be either decrypted or rechecked.
 * This includes PGP/MIME (RFC 3156) and S/MIME (RFC 8551) parts as well as RFC 4880 signed or encrypted text/... bodies.
 */
gboolean
libbalsa_message_body_has_crypto_content(const LibBalsaMessageBody *body)
{
	gboolean result;

	/* check if we have a signature info */
	result = (body->sig_info != NULL);

	/* check for signed or encrypted content-type */
	if (!result && (body->content_type != NULL)) {
		if ((g_ascii_strcasecmp(body->content_type, "multipart/signed") == 0) ||
			(g_ascii_strcasecmp(body->content_type, "multipart/encrypted") == 0) ||
			(g_ascii_strcasecmp(body->content_type, "application/pkcs7-mime") == 0) ||
			(g_ascii_strcasecmp(body->content_type, "application/x-pkcs7-mime") == 0)) {
			result = TRUE;
		}
	}

	/* check if a text/... body is RFC 4880 signed or encrypted */
	if (!result && (body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_TEXT) && (GMIME_IS_PART(body->mime_part)) &&
		(g_mime_part_check_rfc2440(GMIME_PART(body->mime_part)) != GMIME_PART_RFC2440_NONE)) {
		result = TRUE;
	}

	/* check children and parts */
	if (!result && (body->parts != NULL)) {
		result = libbalsa_message_body_has_crypto_content(body->parts);
	}
	if (!result && (body->next != NULL)) {
		result = libbalsa_message_body_has_crypto_content(body->next);
	}

	return result;
}
