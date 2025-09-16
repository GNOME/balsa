/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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


#include <gmime/gmime-stream-fs.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
/* for mx_lock_file and mx_unlock_file */
#include "mailbackend.h"
#include "mime-stream-shared.h"
#include "missing.h"

#include <glib/gi18n.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mbox-mbox"


struct message_info {
    LibBalsaMailboxLocalMessageInfo local_info;
    LibBalsaMessageFlag orig_flags;     /* Has only real flags */
    off_t start;
    off_t status;		/* Offset of the "Status:" header. */
    off_t x_status;		/* Offset of the "X-Status:" header. */
    off_t mime_version;		/* Offset of the "MIME-Version:" header. */
    off_t end;
    size_t from_len;
};

#define REAL_FLAGS(flags) ((flags) & LIBBALSA_MESSAGE_FLAGS_REAL)
#define FLAGS_REALLY_DIFFER(orig_flags, flags) \
    ((((orig_flags) ^ (flags)) & LIBBALSA_MESSAGE_FLAGS_REAL) != 0)

static void libbalsa_mailbox_mbox_dispose(GObject * object);

static GMimeStream *libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *
							     mailbox,
							     guint msgno,
							     gboolean peek);
static gint lbm_mbox_check_files(const gchar * path, gboolean create);
static void libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox);

static gboolean libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox,
					   GError **err);
static void libbalsa_mailbox_mbox_close_mailbox(LibBalsaMailbox * mailbox,
                                                gboolean expunge);
static void libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mbox_sync(LibBalsaMailbox * mailbox,
                                           gboolean expunge);

/* LibBalsaMailboxLocal class methods */
static LibBalsaMailboxLocalMessageInfo
    *lbm_mbox_get_info(LibBalsaMailboxLocal * local, guint msgno);
static LibBalsaMailboxLocalAddMessageFunc lbm_mbox_add_message;

static gboolean
libbalsa_mailbox_mbox_fetch_message_structure(LibBalsaMailbox * mailbox,
                                              LibBalsaMessage * message,
                                              LibBalsaFetchFlag flags);
static guint
libbalsa_mailbox_mbox_total_messages(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_mbox_lock_store(LibBalsaMailbox * mailbox,
                                             gboolean lock);

struct _LibBalsaMailboxMbox {
    LibBalsaMailboxLocal parent;

    GPtrArray *msgno_2_msg_info;
    GMimeStream *gmime_stream;
    off_t size;
    gboolean messages_info_changed;
};

G_DEFINE_TYPE(LibBalsaMailboxMbox,
              libbalsa_mailbox_mbox,
              LIBBALSA_TYPE_MAILBOX_LOCAL)

static void
libbalsa_mailbox_mbox_class_init(LibBalsaMailboxMboxClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_mbox_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_mbox_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_mbox_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_mbox_sync;
    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_mbox_close_mailbox;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_mbox_fetch_message_structure;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_mbox_total_messages;
    libbalsa_mailbox_class->lock_store = libbalsa_mailbox_mbox_lock_store;

    libbalsa_mailbox_local_class->check_files  = lbm_mbox_check_files;
    libbalsa_mailbox_local_class->remove_files =
	libbalsa_mailbox_mbox_remove_files;

    libbalsa_mailbox_local_class->get_info = lbm_mbox_get_info;
    libbalsa_mailbox_local_class->add_message = lbm_mbox_add_message;
    object_class->dispose = libbalsa_mailbox_mbox_dispose;
}



static void
libbalsa_mailbox_mbox_init(LibBalsaMailboxMbox * mbox)
{
}

static void
libbalsa_mailbox_mbox_dispose(GObject * object)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);

    if (MAILBOX_OPEN(mailbox))
	libbalsa_mailbox_mbox_close_mailbox(mailbox, FALSE);
    G_OBJECT_CLASS(libbalsa_mailbox_mbox_parent_class)->dispose(object);
}

static gint
lbm_mbox_check_files(const gchar * path, gboolean create)
{
    if (access(path, F_OK) == 0) {
        /* File exists. Check if it is an mbox... */
        if (libbalsa_mailbox_type_from_path(path) !=
            LIBBALSA_TYPE_MAILBOX_MBOX) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Mailbox %s does not appear to be an mbox mailbox."),
                                 path);
            return -1;
        }
    } else if (create) {
        gint fd;

        if ((fd = creat(path, S_IRUSR | S_IWUSR)) == -1) {
            g_warning("error “%s” occurred while trying to "
                      "create the mailbox “%s”",
					  g_strerror(errno), path);
            return -1;
        } else
            close(fd);
    } else
        return -1;

    return 0;
}

LibBalsaMailbox *
libbalsa_mailbox_mbox_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MBOX, NULL);

    if (libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL(mailbox),
                                        path, create) != 0) {
	g_object_unref(mailbox);
	return NULL;
    }

    return mailbox;
}

/* Helper: seek to offset, and return TRUE if the seek succeeds and a
 * message begins there. */
static gboolean
lbm_mbox_stream_seek_to_message(GMimeStream * stream, off_t offset)
{
    char buffer[5];
    ssize_t nread = 0;
    gboolean retval;

    retval = g_mime_stream_seek(stream, offset, GMIME_STREAM_SEEK_SET) >= 0
        && (nread = g_mime_stream_read(stream, buffer, sizeof buffer))
        == sizeof buffer
        && strncmp("From ", buffer, 5) == 0;
    if (!retval) {
        if (nread == sizeof buffer)
            --nread;
        buffer[nread] = 0;
        g_debug("%s at %ld failed: read %ld chars, saw “%s”", __func__,
                (long) offset, (long) nread, buffer);
    }

    g_mime_stream_seek(stream, offset, GMIME_STREAM_SEEK_SET);

    return retval;
}

static gboolean
lbm_mbox_seek_to_message(LibBalsaMailboxMbox * mbox, off_t offset)
{
    gboolean retval;
    
    libbalsa_mime_stream_shared_lock(mbox->gmime_stream);
    retval = lbm_mbox_stream_seek_to_message(mbox->gmime_stream, offset);
    libbalsa_mime_stream_shared_unlock(mbox->gmime_stream);

    return retval;
}

static struct message_info *
message_info_from_msgno(LibBalsaMailboxMbox * mbox, guint msgno)
{
    g_assert(msgno > 0 && msgno <= mbox->msgno_2_msg_info->len);

    return (struct message_info *) g_ptr_array_index(mbox->
                                                     msgno_2_msg_info,
                                                     msgno - 1);
}

static GMimeStream *
libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox * mailbox,
                                         guint msgno, gboolean peek)
{
    LibBalsaMailboxMbox *mbox;
    struct message_info *msg_info;

    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    msg_info = message_info_from_msgno(mbox, msgno);

    if (!msg_info || !lbm_mbox_seek_to_message(mbox, msg_info->start))
        return NULL;

    return g_mime_stream_substream(mbox->gmime_stream,
                                   msg_info->start + msg_info->from_len,
                                   msg_info->end);
}

static void
libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox)
{
    if ( unlink(libbalsa_mailbox_local_get_path(mailbox)) == -1 )
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
			     _("Could not remove %s: %s"), 
			     libbalsa_mailbox_local_get_path(mailbox), 
				 g_strerror(errno));
    LIBBALSA_MAILBOX_LOCAL_CLASS(libbalsa_mailbox_mbox_parent_class)->remove_files(mailbox);
}

static int mbox_lock(LibBalsaMailbox * mailbox, GMimeStream *stream)
{
    const gchar *path =
        libbalsa_mailbox_local_get_path(LIBBALSA_MAILBOX_LOCAL(mailbox));
    int fd = GMIME_STREAM_FS(stream)->fd;
    return libbalsa_lock_file(path, fd, FALSE, TRUE, 1);
}

static void mbox_unlock(LibBalsaMailbox * mailbox, GMimeStream *stream)
{
    const gchar *path =
        libbalsa_mailbox_local_get_path(LIBBALSA_MAILBOX_LOCAL(mailbox));
    int fd = GMIME_STREAM_FS(stream)->fd;
    libbalsa_unlock_file(path, fd, 1);
}

/* GMimeParserHeaderRegexFunc callback; save the header's offset if it's
 * "Status", "X-Status", or "MIME-Version".  Save only the first one, to
 * avoid headers in encapsulated messages.
 * 
 * If a message has no status headers but an encapsulated message does,
 * we may save the offset of the encapsulated header; we check for that
 * later.
 * 
 * We use the offset of the "MIME-Version" header as the location to
 * insert status headers, if necessary.
 */
static void
lbm_mbox_header_cb(GMimeParser * parser, const char *header,
                   const char *value, gint64 offset,
                   gpointer user_data)
{
    struct message_info *msg_info = *(struct message_info **) user_data;

    if (g_ascii_strcasecmp(header, "Status") == 0 && msg_info->status < 0)
        msg_info->status = offset;
    else if (g_ascii_strcasecmp(header, "X-Status") == 0
             && msg_info->x_status < 0)
        msg_info->x_status = offset;
    else if (g_ascii_strcasecmp(header, "MIME-Version") == 0
             && msg_info->mime_version < 0)
        msg_info->mime_version = offset;
}

static gchar *
lbm_mbox_get_cache_filename(LibBalsaMailboxMbox * mbox)
{
    gchar *encoded_path;
    gchar *filename;
    gchar *basename;

    encoded_path =
        libbalsa_urlencode(libbalsa_mailbox_local_get_path
                           (LIBBALSA_MAILBOX_LOCAL(mbox)));
    basename = g_strconcat("mbox", encoded_path, NULL);
    g_free(encoded_path);
    filename =
        g_build_filename(g_get_user_state_dir(), "balsa", basename, NULL);
    g_free(basename);

    return filename;
}

static void
lbm_mbox_save(LibBalsaMailboxMbox * mbox)
{
    gchar *filename;
#if !defined(__APPLE__)
    GError *err = NULL;
#endif                          /* !defined(__APPLE__) */

    if (!mbox->messages_info_changed)
        return;

    mbox->messages_info_changed = FALSE;

    filename = lbm_mbox_get_cache_filename(mbox);

    if (mbox->msgno_2_msg_info->len > 0) {
        GArray *messages_info =
            g_array_sized_new(FALSE, FALSE, sizeof(struct message_info), 
                              mbox->msgno_2_msg_info->len);
        guint msgno;
#if defined(__APPLE__)
        gchar *template;
        gint fd;
#endif                          /* !defined(__APPLE__) */

        for (msgno = 1; msgno <= mbox->msgno_2_msg_info->len; msgno++) {
            struct message_info *msg_info =
                message_info_from_msgno(mbox, msgno);
            g_array_append_val(messages_info, *msg_info);
        }

#if !defined(__APPLE__)
        if (!g_file_set_contents(filename, messages_info->data,
                                 messages_info->len
                                 * sizeof(struct message_info), &err)) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Could not write file %s: %s"),
                                 filename, err->message);
            g_error_free(err);
        }
#else                           /* !defined(__APPLE__) */
        template = g_strconcat(filename, ":XXXXXX", NULL);
        fd = g_mkstemp(template);
        if (fd < 0 || write(fd, messages_info->data,
                            messages_info->len *
                            sizeof(struct message_info)) <
            (ssize_t) messages_info->len) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Failed to create temporary file "
                                   "“%s”: %s"), template,
                                 g_strerror(errno));
            g_free(template);
            g_free(filename);
            g_array_free(messages_info, TRUE);
            return;
        }
        if (close(fd) != 0
            || (unlink(filename) != 0 && errno != ENOENT)
            || libbalsa_safe_rename(template, filename) != 0)
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Failed to save cache file “%s”: %s. "
                                   "New version saved as “%s”"),
                                 filename, g_strerror(errno), template);
        g_free(template);
#endif                          /* !defined(__APPLE__) */
        g_array_free(messages_info, TRUE);
    } else if (unlink(filename) < 0)
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Could not unlink file %s: %s"),
                             filename, g_strerror(errno));

    g_free(filename);
    g_debug("%s:    %s    saved %d messages", __func__,
            libbalsa_mailbox_get_name(LIBBALSA_MAILBOX(mbox)),
            mbox->msgno_2_msg_info->len);
}

static LibBalsaMessage *lbm_mbox_message_new(GMimeMessage * mime_message,
					     struct message_info
					     *msg_info);
static void
parse_mailbox(LibBalsaMailboxMbox * mbox)
{
    GMimeParser *gmime_parser;
    struct message_info msg_info;
    struct message_info * msg_info_p = &msg_info;
    unsigned msgno = mbox->msgno_2_msg_info->len;

    gmime_parser = g_mime_parser_new_with_stream(mbox->gmime_stream);
    g_mime_parser_set_format(gmime_parser, GMIME_FORMAT_MBOX);
    g_mime_parser_set_header_regex(gmime_parser,
                                   "^Status|^X-Status|^MIME-Version",
				   lbm_mbox_header_cb, &msg_info_p);

    msg_info.local_info.message = NULL;
    msg_info.local_info.loaded  = FALSE;
    while (!g_mime_parser_eos(gmime_parser)) {
	GMimeMessage *mime_message;
        LibBalsaMessage *msg;
        gchar *from;
        off_t offset;

        msg_info.status = msg_info.x_status = msg_info.mime_version = -1;
        mime_message   = g_mime_parser_construct_message(gmime_parser, libbalsa_parser_options());
        if (mime_message == NULL) {
            /* Skip to the next message, if any */
            GMimeStream *mbox_stream;

            mbox_stream = mbox->gmime_stream;
            while (!g_mime_stream_eos(mbox_stream)) {
                gchar c;

                while (g_mime_stream_read(mbox_stream, &c, 1) == 1)
                    if (c == '\n')
                        break;

                if (lbm_mbox_stream_seek_to_message(mbox_stream,
                                                    g_mime_stream_tell(mbox_stream)))
                    break;
            }
            g_mime_parser_init_with_stream(gmime_parser, mbox_stream);
            continue;
        }
        msg_info.start = g_mime_parser_get_mbox_marker_offset(gmime_parser);
        msg_info.end   = g_mime_parser_tell(gmime_parser);
        if (msg_info.end <= msg_info.start
            || !(from = g_mime_parser_get_mbox_marker(gmime_parser))) {
	    g_object_unref(mime_message);
            continue;
	}
        msg_info.from_len = strlen(from) + 1;
        g_free(from);

	/* Make sure we don't have offsets for any encapsulated headers. */
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_message),
                                      "Status"))
	    msg_info.status = -1;
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_message),
                                      "X-Status"))
	    msg_info.x_status = -1;
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_message),
                                      "MIME-Version"))
	    msg_info.mime_version = -1;

        msg = lbm_mbox_message_new(mime_message, &msg_info);
        g_object_unref(mime_message);
        if (!msg)
            continue;

        msg_info.local_info.flags = msg_info.orig_flags;
        g_ptr_array_add(mbox->msgno_2_msg_info, g_memdup2(&msg_info, sizeof(msg_info)));
        mbox->messages_info_changed = TRUE;

        libbalsa_message_set_flags(msg, msg_info.orig_flags);
        libbalsa_message_set_length(msg, msg_info.end - (msg_info.start + msg_info.from_len));
        libbalsa_message_set_mailbox(msg, LIBBALSA_MAILBOX(mbox));
        libbalsa_message_set_msgno(msg, ++msgno);
	/* We must drop the mime-stream lock to call
         * libbalsa_mailbox_local_cache_message, which calls
	 * libbalsa_mailbox_cache_message(), as it may grab the
	 * gdk lock to emit gtk signals; we save and restore the current
	 * stream position, in case someone changes it while we're not
	 * holding the lock. */
        offset = g_mime_stream_tell(mbox->gmime_stream);
        libbalsa_mime_stream_shared_unlock(mbox->gmime_stream);
        libbalsa_mailbox_cache_message(LIBBALSA_MAILBOX(mbox), msgno, msg);
        libbalsa_mime_stream_shared_lock(mbox->gmime_stream);
        g_mime_stream_seek(mbox->gmime_stream, offset, GMIME_STREAM_SEEK_SET);

        g_object_unref(msg);
    }

    g_object_unref(gmime_parser);
    lbm_mbox_save(mbox);
}

static void
lbm_mbox_restore(LibBalsaMailboxMbox * mbox)
{
    gchar *filename;
    struct stat st;
    gchar *contents;
    gsize length;
    off_t end;
    struct message_info *msg_info;
    GMimeStream *mbox_stream;

    filename = lbm_mbox_get_cache_filename(mbox);
    if (stat(filename, &st) < 0 
        || st.st_mtime < libbalsa_mailbox_get_mtime(LIBBALSA_MAILBOX(mbox))
        || !g_file_get_contents(filename, &contents, &length, NULL)) {
        /* No cache file, stale cache, or read error. */
        g_free(filename);
        return;
    }

    g_free(filename);

    if (length < sizeof(struct message_info)) {
        /* Error: file always contains at least one record. */
        g_free(contents);
        return;
    }

    g_debug("%s: %s file has %zd messages", __func__,
            libbalsa_mailbox_get_name(LIBBALSA_MAILBOX(mbox)),
            length / sizeof(struct message_info));

    msg_info = (struct message_info *) contents;

    if (msg_info->start != 0) {
        /* Error: first message should start at 0. */
        g_free(contents);
        return;
    }

    end = 0;
    do {
        msg_info->local_info.message = NULL;
        msg_info->local_info.loaded  = FALSE;
        if (msg_info->start != end)
            /* Error: this message doesn't start at the end of the
             * previous one. */
            break;
        end = msg_info->end;
        if (msg_info->from_len < 6
            || (off_t) (msg_info->start + msg_info->from_len) >= end
            || end > mbox->size)
            /* Error: various. */
            break;
        if (end < mbox->size
            && !lbm_mbox_seek_to_message(mbox, msg_info->end))
            /* Error: no message following this one. */
            break;
        g_ptr_array_add(mbox->msgno_2_msg_info, g_memdup2(msg_info, sizeof *msg_info));
    } while (++msg_info < (struct message_info *) (contents + length));

    g_debug("%s: %s restored %zd messages", __func__,
            libbalsa_mailbox_get_name(LIBBALSA_MAILBOX(mbox)),
            msg_info - (struct message_info *) contents);

    mbox_stream = mbox->gmime_stream;
    libbalsa_mime_stream_shared_lock(mbox_stream);
    /* Position the stream for parsing; msg_info is pointing either one
     * message beyond the end of the array, or at the offending message,
     * so we just seek to the end of the previous message. */
    g_mime_stream_seek(mbox_stream,
                       msg_info > (struct message_info *) contents ?
                       (--msg_info)->end : 0,
                       GMIME_STREAM_SEEK_SET);

    /* GMimeParser seems to have issues with a file that has no From_
     * line, so we'll step forward until we find one. */
    while (!g_mime_stream_eos(mbox_stream)) {
        gchar c;

        if (lbm_mbox_stream_seek_to_message(mbox_stream,
                                            g_mime_stream_tell(mbox_stream)))
            break;

        while (g_mime_stream_read(mbox_stream, &c, 1) == 1)
            if (c == '\n')
                break;
    }
    libbalsa_mime_stream_shared_unlock(mbox_stream);

    g_free(contents);
}

static void
free_message_info(struct message_info *msg_info)
{
    LibBalsaMessage *message;

    if ((message = msg_info->local_info.message) != NULL) {
        libbalsa_message_set_mailbox(message, NULL);
        libbalsa_message_set_msgno(message, 0);
        g_object_remove_weak_pointer(G_OBJECT(message),
                                     (gpointer *) &msg_info->local_info.message);
        msg_info->local_info.message = NULL;
    }

    g_free(msg_info);
}

static gboolean
libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox, GError **err)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    struct stat st;
    const gchar* path;
    gboolean readonly;
    int fd;
    GMimeStream *gmime_stream;
    time_t t0;

    path = libbalsa_mailbox_local_get_path(LIBBALSA_MAILBOX_LOCAL(mailbox));

    if (stat(path, &st) == -1) {
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Mailbox does not exist."));
	return FALSE;
    }

    readonly = access (path, W_OK);
    libbalsa_mailbox_set_readonly(mailbox, readonly);
    fd = open(path, readonly ? O_RDONLY : O_RDWR);
    if (fd == -1) {
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Cannot open mailbox."));
	return FALSE;
    }
    gmime_stream = libbalsa_mime_stream_shared_new(fd);

    libbalsa_mime_stream_shared_lock(gmime_stream);
    if (st.st_size > 0
        && !lbm_mbox_stream_seek_to_message(gmime_stream, 0)) {
        libbalsa_mime_stream_shared_unlock(gmime_stream);
        g_object_unref(gmime_stream);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_OPEN_ERROR,
                    _("Mailbox is not in mbox format."));
        return FALSE;
    }

    if (mbox_lock(mailbox, gmime_stream)) {
        libbalsa_mime_stream_shared_unlock(gmime_stream);
	g_object_unref(gmime_stream);
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Cannot lock mailbox."));
	return FALSE;
    }

    mbox->size = st.st_size;
    g_debug("%s %s set size from stat %ld", __func__, libbalsa_mailbox_get_name(mailbox),
            (long) mbox->size);
    libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);
    mbox->gmime_stream = gmime_stream;

    mbox->msgno_2_msg_info =
        g_ptr_array_new_with_free_func((GDestroyNotify) free_message_info);

    libbalsa_mailbox_clear_unread_messages(mailbox);
    time(&t0);

    if (st.st_size > 0) {
        lbm_mbox_restore(mbox);
        parse_mailbox(mbox);
    }

    mbox_unlock(mailbox, gmime_stream);
    libbalsa_mime_stream_shared_unlock(gmime_stream);
    g_debug("%s: Opening %s Refcount: %u",
	    __func__, libbalsa_mailbox_get_name(mailbox),
		libbalsa_mailbox_get_open_ref(mailbox));
    return TRUE;
}

/* Check for new mail in a closed mbox, using a crude parser. */
/*
 * Lightweight replacement for GMimeStreamBuffer
 */
typedef struct {
    ssize_t start;
    ssize_t end;
    gchar buf[1024];
    GMimeStream *stream;
} LbmMboxStreamBuffer;

static off_t
lbm_mbox_seek(LbmMboxStreamBuffer * buffer, off_t offset)
{
    if (offset >= 0) {
        buffer->start =
            buffer->end - (g_mime_stream_tell(buffer->stream) - offset);

        if (buffer->start < 0 || buffer->start > buffer->end) {
            offset =
                g_mime_stream_seek(buffer->stream, offset,
                                   GMIME_STREAM_SEEK_SET);
            buffer->start = buffer->end = 0;
        }
    }

    return offset;
}

static guint
lbm_mbox_readln(LbmMboxStreamBuffer * buffer, GByteArray * line)
{
    gchar *p, *q, *r;

    g_byte_array_set_size(line, 0);

    do {
        if (buffer->start >= buffer->end) {
            buffer->start = 0;
            buffer->end = g_mime_stream_read(buffer->stream, buffer->buf,
                                             sizeof buffer->buf);
            if (buffer->end < 0) {
                g_warning("%s: Read error", __func__);
                break;
            }
            if (buffer->end == 0)
                break;
        }

        p = q = buffer->buf + buffer->start;
        r = buffer->buf + buffer->end;
        while (p < r && *p++ != '\n')
            /* Nothing */;

        g_byte_array_append(line, (guint8 *) q, p - q);
        buffer->start += p - q;
    } while (*--p != '\n');

    return line->len;
}

/*
 * Look for an unread, undeleted message using the cache file.
 */
static gboolean
lbm_mbox_check_cache(LibBalsaMailboxMbox * mbox,
                     LbmMboxStreamBuffer * buffer, GByteArray * line)
{
    gchar *filename;
    gboolean tmp;
    gchar *contents;
    gsize length;
    struct message_info *msg_info;
    gboolean retval = FALSE;

    filename = lbm_mbox_get_cache_filename(mbox);
    tmp = g_file_get_contents(filename, &contents, &length, NULL);
    g_free(filename);
    if (!tmp)
        return retval;

    for (msg_info = (struct message_info *) contents;
         msg_info < (struct message_info *) (contents + length);
         msg_info++) {
        if (lbm_mbox_seek(buffer, msg_info->status) >= 0
            && lbm_mbox_readln(buffer, line)) {
            if (g_ascii_strncasecmp((gchar *) line->data,
                                    "Status: ", 8) != 0)
                /* Bad cache. */
                break;
            if (strchr((gchar *) line->data + 8, 'R'))
                /* Message has been read. */
                continue;
        }
        if (lbm_mbox_seek(buffer, msg_info->x_status) >= 0
            && lbm_mbox_readln(buffer, line)) {
            if (g_ascii_strncasecmp((gchar *) line->data,
                                    "X-Status: ", 10) != 0)
                /* Bad cache. */
                break;
            if (strchr((gchar *) line->data + 10, 'D'))
                /* Message has been read. */
                continue;
        }
        /* Message is unread and undeleted. */
        retval = TRUE;
        break;
    }
    if (!retval)
        /* Seek to the end of the last message we checked. */
        lbm_mbox_seek(buffer, msg_info > (struct message_info *) contents ?
                      (msg_info-1)->end : 0);
    g_free(contents); /* msg_info points to contents, cannot free too early */


    return retval;
}

/*
 * Look for an unread, undeleted message in the mbox file, beyond the
 * messages we found in the cache file.
 */
static gboolean
lbm_mbox_check_file(LibBalsaMailboxMbox * mbox,
                    LbmMboxStreamBuffer * buffer, GByteArray * line)
{
    gboolean retval = FALSE;

    do {
        guint content_length = 0;
        guint old_or_deleted = 0;

        /* Find the next From_ line; if it's inside a message, protected
         * by an embedded Content-Length header, we may be misled, but a
         * full GMime parse takes too long. */
        while (lbm_mbox_readln(buffer, line)
               && strncmp((gchar *) line->data, "From ", 5) != 0)
            /* Nothing. */ ;
        if (line->len == 0)
            break;

        /* Scan headers. */
        do {
            /* Blank line ends headers. */
            if (!lbm_mbox_readln(buffer, line)
                || line->data[0] == '\n')
                break;

            line->data[line->len - 1] = '\0';
            if (g_ascii_strncasecmp((gchar *) line->data,
                                    "Status: ", 8) == 0) {
                if (strchr((gchar *) line->data + 8, 'R'))
                    ++old_or_deleted;
            } else if (g_ascii_strncasecmp((gchar *) line->data,
                                           "X-Status: ", 10) == 0) {
                if (strchr((gchar *) line->data + 10, 'D'))
                    ++old_or_deleted;
            } else if (g_ascii_strncasecmp((gchar *) line->data,
                                           "Content-Length: ", 16) == 0)
                content_length = atoi((gchar *) line->data + 16);
        } while (!(old_or_deleted && content_length));

        if (!old_or_deleted) {
            retval = TRUE;
            /* One new message is enough. */
            break;
        }

        if (content_length) {
            /* Seek past the content. */
            off_t remaining;

            buffer->start += content_length;
            remaining = (off_t) buffer->end - (off_t) buffer->start;
            if (remaining < 0) {
                g_mime_stream_seek(buffer->stream, -remaining,
                                   GMIME_STREAM_SEEK_CUR);
                buffer->start = buffer->end = 0;
            }
        }
    } while (line->len > 0);

    return retval;
}

static gboolean
lbm_mbox_check(LibBalsaMailbox * mailbox, const gchar * path)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    int fd;
    gboolean retval = FALSE;
    LbmMboxStreamBuffer buffer = { 0, 0 };
    GByteArray *line;

    if (!(fd = open(path, O_RDONLY)))
        return retval;

    buffer.stream = g_mime_stream_fs_new(fd);

    if (mbox_lock(mailbox, buffer.stream)) {
        g_object_unref(buffer.stream);
        return retval;
    }

    line = g_byte_array_sized_new(80);

    retval = lbm_mbox_check_cache(mbox, &buffer, line);
    if (!retval)
        retval = lbm_mbox_check_file(mbox, &buffer, line);

    g_byte_array_free(line, TRUE);
    mbox_unlock(mailbox, buffer.stream);
    g_object_unref(buffer.stream);

    return retval;
}

/* Called with mailbox locked. */
static void
libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox)
{
    struct stat st;
    const gchar *path;
    LibBalsaMailboxMbox *mbox;
    GMimeStream *mbox_stream;
    guint msgno;
    time_t mtime;
    off_t start;

    g_assert(LIBBALSA_IS_MAILBOX_MBOX(mailbox));

    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    path = libbalsa_mailbox_local_get_path(LIBBALSA_MAILBOX_LOCAL(mailbox));
    if (mbox->gmime_stream ?
        fstat(GMIME_STREAM_FS(mbox->gmime_stream)->fd, &st) :
        stat(path, &st)) {
	perror(path);
	return;
    }

    mtime = libbalsa_mailbox_get_mtime(mailbox);
    if (mtime == 0) {
	/* First check--just cache the mtime and size. */
        libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);
	mbox->size = st.st_size;
        g_debug("%s %s set size from stat %ld", __func__, libbalsa_mailbox_get_name(mailbox),
                (long) mbox->size);
	return;
    }
    if (st.st_mtime == mtime && st.st_size == mbox->size)
	return;

    libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);

    if (!MAILBOX_OPEN(mailbox)) {
	libbalsa_mailbox_set_unread_messages_flag(mailbox,
						  lbm_mbox_check(mailbox,
								 path));
	/* Cache the file size, so we don't check the next time. */
	mbox->size = st.st_size;
        g_debug("%s %s set size from stat %ld", __func__, libbalsa_mailbox_get_name(mailbox),
                (long) mbox->size);
	return;
    }

    mbox_stream = mbox->gmime_stream;
    if (mbox_lock(mailbox, mbox_stream) != 0)
	/* we couldn't lock the mailbox, but nothing serious happened:
	 * probably the new mail arrived: no reason to wait till we can
	 * parse it: we'll get it on the next pass
	 */
	return;

    /* Find a good place to start parsing the mailbox.  If the only
     * change is that message(s) were appended to this file, we should
     * see the message separator at *exactly* what used to be the end of
     * the folder.  If a message was expunged by another MUA, we back up
     * over messages until we find one that is still where we expected
     * to find it, and start parsing at its end.
     * We must lock the mime-stream for the whole process, as
     * parse_mailbox assumes that the stream is positioned at the first
     * message to be parsed.
     */

    libbalsa_mime_stream_shared_lock(mbox_stream);

    /* If Balsa appended a message, it was prefixed with "\nFrom ", so
     * we first check one byte beyond the end of the last message: */
    start = mbox->size + 1;
#if DEBUG_SEEK
    g_print("%s %s looking where to start parsing.\n",
              __func__, mailbox->name);
    if (!lbm_mbox_stream_seek_to_message(mbox_stream, start)) {
        g_print(" did not find a message at offset %ld\n", (long) start);
        --start;
        if (lbm_mbox_stream_seek_to_message(mbox_stream, start))
            g_print(" found a message at offset %ld\n", (long) start);
        else
            g_print(" did not find a message at offset %ld\n", (long) start);
    } else
        g_print(" found a message at offset %ld\n", (long) start);
#else
    if (!lbm_mbox_stream_seek_to_message(mbox_stream, start))
        /* Sometimes we seem to be off by 1: */
        --start;
#endif

    while ((msgno = mbox->msgno_2_msg_info->len) > 0) {
	off_t offset;
        struct message_info *msg_info;

        if (lbm_mbox_stream_seek_to_message(mbox_stream, start))
	    /* A message begins here, so it must(?) be
	     * the first new message--start parsing here. */
            break;

        g_debug(" backing up over message %d", msgno);
	/* Back up over this message and try again. */
        msg_info = message_info_from_msgno(mbox, msgno);
        start = msg_info->start;

        if ((msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_NEW)
            && !(msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_DELETED))
            libbalsa_mailbox_add_to_unread_messages(mailbox, -1);

	/* We must drop the mime-stream lock to call
	 * libbalsa_mailbox_local_msgno_removed(), as it will grab the
	 * gdk lock to emit gtk signals; we save and restore the current
	 * stream position, in case someone changes it while we're not
	 * holding the lock. */
        offset = g_mime_stream_tell(mbox_stream);
        libbalsa_mime_stream_shared_unlock(mbox_stream);
        libbalsa_mailbox_local_msgno_removed(mailbox, msgno);
        libbalsa_mime_stream_shared_lock(mbox_stream);
        g_mime_stream_seek(mbox_stream, offset, GMIME_STREAM_SEEK_SET);

        g_ptr_array_remove_index(mbox->msgno_2_msg_info, msgno - 1);
        mbox->messages_info_changed = TRUE;
    }
    if(msgno == 0)
        g_mime_stream_seek(mbox_stream, 0, GMIME_STREAM_SEEK_SET);
    g_debug("%s: start parsing at msgno %d of %d", __func__, msgno,
            mbox->msgno_2_msg_info->len);
    parse_mailbox(mbox);
    mbox->size = g_mime_stream_tell(mbox_stream);
    g_debug("%s %s set size from tell %ld", __func__, libbalsa_mailbox_get_name(mailbox),
            (long) mbox->size);
    libbalsa_mime_stream_shared_unlock(mbox_stream);
    mbox_unlock(mailbox, mbox_stream);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mbox_parent_class)->check != NULL)
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mbox_parent_class)->check(mailbox);
}

static void
libbalsa_mailbox_mbox_close_mailbox(LibBalsaMailbox * mailbox,
                                    gboolean expunge)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    guint len;

    if (mbox->msgno_2_msg_info == NULL)
        return;

    len = mbox->msgno_2_msg_info->len;
    libbalsa_mailbox_mbox_sync(mailbox, expunge);
    if (mbox->msgno_2_msg_info->len != len)
        libbalsa_mailbox_changed(mailbox);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mbox_parent_class)->close_mailbox)
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mbox_parent_class)->close_mailbox(mailbox,
                                                            expunge);

    /* Now it's safe to close the stream and free the message info. */
    if (mbox->gmime_stream) {
        g_object_unref(mbox->gmime_stream);
        mbox->gmime_stream = NULL;
    }

    g_ptr_array_free(mbox->msgno_2_msg_info, TRUE);
    mbox->msgno_2_msg_info = NULL;
}

static GMimeMessage *
lbm_mbox_get_mime_message(LibBalsaMailbox * mailbox,
			  guint msgno)
{
    GMimeStream *stream;
    GMimeParser *parser;
    GMimeMessage *mime_message;

    stream = libbalsa_mailbox_mbox_get_message_stream(mailbox, msgno, TRUE);
    if (!stream)
	return NULL;
    libbalsa_mime_stream_shared_lock(stream);
    parser = g_mime_parser_new_with_stream(stream);
    g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);

    mime_message = g_mime_parser_construct_message(parser, libbalsa_parser_options());
    g_object_unref(parser);
    libbalsa_mime_stream_shared_unlock(stream);
    g_object_unref(stream);

    return mime_message;
}

/* Write one or two newlines to stream. */
static gint
lbm_mbox_newline(GMimeStream * stream)
{
    gint retval;
    static const gchar newlines[] = "\n\n";

    if (g_mime_stream_seek(stream, -1, GMIME_STREAM_SEEK_CUR) < 0) {
    	retval = -1;
    } else {
        gchar buf;

    	retval = g_mime_stream_read(stream, &buf, 1);
    	if (retval == 1) {
    		retval =
    			g_mime_stream_write(stream, newlines, buf == '\n' ? 1 : 2);
    	}
    }

    return retval;
}

/* Store the message status flags in str, padded with spaces to a minimum
 * length of len.
 */
static void
lbm_mbox_status_hdr(LibBalsaMessageFlag flags, guint len, GString * str)
{
    if ((flags & LIBBALSA_MESSAGE_FLAG_NEW) == 0)
	g_string_append_c(str, 'R');
    if ((flags & LIBBALSA_MESSAGE_FLAG_RECENT) == 0)
	g_string_append_c(str, 'O');
    while (str->len < len)
	g_string_append_c(str, ' ');
}

/* Store the message x-status flags in str, padded with spaces to a
 * minimum length of len.
 */
static void
lbm_mbox_x_status_hdr(LibBalsaMessageFlag flags, guint len, GString * str)
{
    if ((flags & LIBBALSA_MESSAGE_FLAG_REPLIED) != 0)
	g_string_append_c(str, 'A');
    if ((flags & LIBBALSA_MESSAGE_FLAG_FLAGGED) != 0)
	g_string_append_c(str, 'F');
    if ((flags & LIBBALSA_MESSAGE_FLAG_DELETED) != 0)
	g_string_append_c(str, 'D');
    while (str->len < len)
	g_string_append_c(str, ' ');
}

/* Helper for lbm_mbox_rewrite_in_place.
 *
 * offset:	the offset of a header in the mbox file;
 * stream:	mbox stream;
 * header:	a GString containing flags to be stored as the value of
 * 		the header;
 * buf:		buffer to hold the whole text of the header;
 * len:		buffer length;
 * start:	offset into buf for storing the flags.
 *
 * Returns TRUE if the rewrite can be carried out in place,
 * 	   FALSE otherwise.
 *
 * If TRUE, on return, buf contains the new header text.
 */
static gboolean
lbm_mbox_rewrite_helper(off_t offset, GMimeStream * stream,
			GString * header, gchar * buf, guint len,
			const gchar * name)
{
    guint name_len = strlen(name);
    guint i;

    g_assert(name_len < len);

    if (offset < 0)
	/* No existing header, so we can rewrite in place only if no
	 * flags need to be set. */
	return header->len == 0;

    if (g_mime_stream_seek(stream, offset, GMIME_STREAM_SEEK_SET) < 0
	|| g_mime_stream_read(stream, buf, len) < (gint) len
	|| g_ascii_strncasecmp(buf, name, name_len) != 0)
	return FALSE;

    /* Copy the flags into the buffer. */
    for (i = 0; i < header->len; i++) {
	if (buf[name_len + i] == '\n')
	    /* The original header is too short to hold all the flags. */
	    return FALSE;
	buf[name_len + i] = header->str[i];
    }

    /* Fill with spaces to the end of the original header line. */
    while (buf[name_len + i] != '\n') {
	if (name_len + i >= len - 1)
	    /* Hit the end of the buffer before finding the end of the
	     * line--the header must have some extra garbage. */
	    return FALSE;
	buf[name_len + i++] = ' ';
    }

    return TRUE;
}

/* Rewrite message status headers in place, if possible.
 *
 * msg_info:	struct message_info for the message;
 * stream:	mbox stream--must be locked by caller.
 *
 * Returns TRUE if it was possible to rewrite in place,
 * 	   FALSE otherwise.
 */
static gboolean
lbm_mbox_rewrite_in_place(struct message_info *msg_info,
			  GMimeStream * stream)
{
    GString *header;
    gchar status_buf[12];	/* "Status: XXX\n" */
    gchar x_status_buf[14];	/* "X-Status: XXX\n" */

    /* Get the flags for the "Status" header. */
    header = g_string_new(NULL);
    lbm_mbox_status_hdr(msg_info->local_info.flags, 0, header);
    g_assert(header->len <= 3);

    if (!lbm_mbox_rewrite_helper(msg_info->status, stream, header,
				 status_buf, sizeof(status_buf),
				 "Status: ")) {
	g_string_free(header, TRUE);
	return FALSE;
    }

    /* Get the flags for the "X-Status" header. */
    g_string_truncate(header, 0);
    lbm_mbox_x_status_hdr(msg_info->local_info.flags, 0, header);
    g_assert(header->len <= 3);

    if (!lbm_mbox_rewrite_helper(msg_info->x_status, stream, header,
				 x_status_buf, sizeof(x_status_buf),
				 "X-Status: ")) {
	g_string_free(header, TRUE);
	return FALSE;
    }

    g_string_free(header, TRUE);

    /* Both headers are OK to rewrite, if they exist. */
    if (msg_info->status >= 0) {
	g_mime_stream_seek(stream, msg_info->status, GMIME_STREAM_SEEK_SET);
	g_mime_stream_write(stream, status_buf, sizeof(status_buf));
    }
    if (msg_info->x_status >= 0) {
	g_mime_stream_seek(stream, msg_info->x_status, GMIME_STREAM_SEEK_SET);
	g_mime_stream_write(stream, x_status_buf, sizeof(x_status_buf));
    }
    msg_info->orig_flags = REAL_FLAGS(msg_info->local_info.flags);
    return TRUE;
}

/* Length of the line beginning at offset, including trailing '\n'.
 * Returns -1 if no '\n' found, or if seek to offset fails. */
static gint
lbm_mbox_line_len(LibBalsaMailboxMbox * mbox, off_t offset)
{
    GMimeStream *stream = mbox->gmime_stream;
    gint retval = -1;

    libbalsa_mime_stream_shared_lock(mbox->gmime_stream);
    if (g_mime_stream_seek(stream, offset, GMIME_STREAM_SEEK_SET) >= 0) {
        gint old = 0;
        while (retval < 0) {
            gint i, len;
            gchar buf[80];

            len = g_mime_stream_read(stream, buf, sizeof buf);
            if (len <= 0)
                break;

            i = 0;
            do
                if (buf[i++] == '\n') {
                    retval = old + i;
                    break;
                }
            while (i < len);
            old += len;
        }
    }
    libbalsa_mime_stream_shared_unlock(mbox->gmime_stream);

    return retval;
}

static gboolean
lbm_mbox_copy_stream(LibBalsaMailboxMbox * mbox, off_t start, off_t end,
		     GMimeStream * dest)
{
    GMimeStream *substream;
    gboolean retval;

    if (start >= end)
	return TRUE;

    substream = g_mime_stream_substream(mbox->gmime_stream, start, end);
    libbalsa_mime_stream_shared_lock(substream);
    retval = g_mime_stream_write_to_stream(substream, dest) == end - start;
    libbalsa_mime_stream_shared_unlock(substream);
    g_object_unref(substream);

    return retval;
}

/* Write a (X-)Status header to the stream. */
static gboolean
lbm_mbox_write_status_hdr(GMimeStream * stream, LibBalsaMessageFlag flags)
{
    gboolean retval;
    GString *header = g_string_new("Status: ");
    lbm_mbox_status_hdr(flags, header->len + 2, header);
    g_string_append_c(header, '\n');
    retval = g_mime_stream_write(stream, header->str,
				 header->len) == (gint) header->len;
    g_string_free(header, TRUE);
    return retval;
}

static gboolean
lbm_mbox_write_x_status_hdr(GMimeStream * stream, LibBalsaMessageFlag flags)
{
    gboolean retval;
    GString *header = g_string_new("X-Status: ");
    lbm_mbox_x_status_hdr(flags, header->len + 3, header);
    g_string_append_c(header, '\n');
    retval = g_mime_stream_write(stream, header->str,
				 header->len) == (gint) header->len;
    g_string_free(header, TRUE);
    return retval;
}

static void update_message_status_headers(GMimeMessage *message,
					  LibBalsaMessageFlag flags);
static gboolean
libbalsa_mailbox_mbox_sync(LibBalsaMailbox * mailbox, gboolean expunge)
{
    const gchar *path;
    struct stat st;
    gint messages;
    struct message_info *msg_info;
    off_t offset;
    int first;
    int i;
    guint j;
    GMimeStream *temp_stream;
    GMimeStream *mbox_stream;
    gchar *tempfile;
    GError *error = NULL;
    gboolean save_failed;
    GMimeParser *gmime_parser;
    LibBalsaMailboxMbox *mbox;

    /* FIXME: We should probably lock the mailbox file before checking,
     * and hold the lock while we sync it.  As it stands,
     * libbalsa_mailbox_mbox_check() locks it to do the check, then
     * releases the lock, and we reacquire it here.  Concievably, more
     * mail could have been delivered...
     */
    libbalsa_mailbox_mbox_check(mailbox);
    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    if (mbox->msgno_2_msg_info->len == 0)
	return TRUE;
    mbox_stream = mbox->gmime_stream;

    path = libbalsa_mailbox_local_get_path(LIBBALSA_MAILBOX_LOCAL(mailbox));

    /* lock mailbox file */
    if (mbox_lock(mailbox, mbox_stream) != 0)
	return FALSE;

    /* Check to make sure that the file hasn't changed on disk */
    if (fstat(GMIME_STREAM_FS(mbox_stream)->fd, &st) != 0
        || st.st_size != mbox->size) {
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }

    /* Find where we need to start rewriting the mailbox.  We save a lot
     * of time by only rewriting the mailbox from the point where we
     * really need to.
     * But if we're rewriting it, we start from the first message that's
     * missing either status header, to reduce the chances of multiple
     * rewrites.
     */
    messages = mbox->msgno_2_msg_info->len;
    first = -1;
    for (i = j = 0; i < messages; i++)
    {
	msg_info = message_info_from_msgno(mbox, i + 1);
	if (libbalsa_mailbox_get_state(mailbox) == LB_MAILBOX_STATE_CLOSING)
	    msg_info->local_info.flags &= ~LIBBALSA_MESSAGE_FLAG_RECENT;
	if (expunge && (msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_DELETED))
	    break;
	if (first < 0 && (msg_info->status < 0 || msg_info->x_status < 0))
	    first = i;
        if (FLAGS_REALLY_DIFFER(msg_info->orig_flags,
                                msg_info->local_info.flags)) {
	    gboolean can_rewrite_in_place;

	    libbalsa_mime_stream_shared_lock(mbox_stream);
	    can_rewrite_in_place =
		lbm_mbox_rewrite_in_place(msg_info, mbox_stream);
	    libbalsa_mime_stream_shared_unlock(mbox_stream);
	    if (!can_rewrite_in_place)
		break;
            mbox->messages_info_changed = TRUE;
	    ++j;
	}
    }
    if (i >= messages) {
	if (j > 0) {
	    struct utimbuf utimebuf;
	    /* Restore the previous access/modification times */
	    utimebuf.actime = st.st_atime;
	    utimebuf.modtime = st.st_mtime;
	    utime(path, &utimebuf);
	}
	if (g_mime_stream_flush(mbox_stream) < 0)
	    g_warning("can't flush mailbox stream");
	if (fstat(GMIME_STREAM_FS(mbox_stream)->fd, &st))
	    g_warning("can't stat “%s”", path);
	else
            libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);
        lbm_mbox_save(mbox);
	mbox_unlock(mailbox, mbox_stream);
	return TRUE;
    }

    /* save the index of the first changed/deleted message */
    if (first < 0)
	first = i; 
    /* where to start overwriting */
    offset = message_info_from_msgno(mbox, first + 1)->start;

    /* Create a temporary file to write the new version of the mailbox in. */
    i = g_file_open_tmp("balsa-tmp-mbox-XXXXXX", &tempfile, &error);
    if (i == -1)
    {
	g_warning("Could not create temporary file: %s", error->message);
	g_error_free (error);
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }
    temp_stream = g_mime_stream_fs_new(i);

    for (i = first; i < messages; i++) {
	gint status_len, x_status_len;

	msg_info = message_info_from_msgno(mbox, i + 1);
	if (expunge && (msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_DELETED))
	    continue;

	if (msg_info->status >= 0) {
	    status_len = lbm_mbox_line_len(mbox, msg_info->status);
	    if (status_len < 0)
		break;
	} else {
            msg_info->status = msg_info->mime_version >= 0 ?
		msg_info->mime_version :
                (off_t) (msg_info->start + msg_info->from_len);
	    
	    status_len = 0;
	}

	if (msg_info->x_status >= 0) {
	    x_status_len = lbm_mbox_line_len(mbox, msg_info->x_status);
	    if (x_status_len < 0)
		break;
	} else {
	    msg_info->x_status = msg_info->status;
	    x_status_len = 0;
	}

	if (msg_info->status <= msg_info->x_status) {
	    if (!lbm_mbox_copy_stream(mbox, msg_info->start,
				      msg_info->status, temp_stream)
		|| !lbm_mbox_write_status_hdr(temp_stream, msg_info->local_info.flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->status + status_len,
					 msg_info->x_status, temp_stream)
		|| !lbm_mbox_write_x_status_hdr(temp_stream,
						msg_info->local_info.flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->x_status +
					 x_status_len, msg_info->end,
					 temp_stream))
		break;
	} else {
	    if (!lbm_mbox_copy_stream(mbox, msg_info->start,
				      msg_info->x_status, temp_stream)
		|| !lbm_mbox_write_x_status_hdr(temp_stream,
						msg_info->local_info.flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->x_status +
					 x_status_len,
					 msg_info->status, temp_stream)
		|| !lbm_mbox_write_status_hdr(temp_stream, msg_info->local_info.flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->status + status_len,
					 msg_info->end, temp_stream))
		break;
	}
    }

    if (i < messages) {
	/* We broke on an error. */
	g_warning("error making temporary copy");
	g_object_unref(temp_stream);
	unlink(tempfile);
	g_free(tempfile);
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }

    g_mime_stream_set_bounds(mbox_stream, 0, -1);
    if (g_mime_stream_flush(temp_stream) == -1)
    {
	g_warning("can't flush temporary copy");
	g_object_unref(temp_stream);
	unlink(tempfile);
	g_free(tempfile);
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }

    save_failed = TRUE;
    libbalsa_mime_stream_shared_lock(mbox_stream);
    if (g_mime_stream_reset(temp_stream) == -1) {
        g_warning("mbox_sync: can't rewind temporary copy.");
    } else if (!lbm_mbox_stream_seek_to_message(mbox_stream, offset))
        g_warning("mbox_sync: message not in expected position.");
    else if (g_mime_stream_write_to_stream(temp_stream, mbox_stream) != -1) {
        mbox->size = g_mime_stream_tell(mbox_stream);
        g_debug("%s %s set size from tell %ld", __func__, libbalsa_mailbox_get_name(mailbox),
                (long) mbox->size);
        if (ftruncate(GMIME_STREAM_FS(mbox_stream)->fd, mbox->size) == 0)
            save_failed = FALSE;
    }
    g_object_unref(temp_stream);
    mbox_unlock(mailbox, mbox_stream);
    if (g_mime_stream_flush(mbox_stream) == -1)
        save_failed = TRUE;
    libbalsa_mime_stream_shared_unlock(mbox_stream);
    if (save_failed) {
	/*
	 * error occurred while writing the mailbox back,
	 * so keep the temp copy around
	 */
	char *savefile;
	{
	    gchar *foo = g_path_get_basename(path);
	    savefile = g_strdup_printf ("%s/saved-mbox.%s-%s-%d", g_get_tmp_dir(),
			 g_get_user_name(), foo, getpid ());
	    g_free(foo);
	}
	rename (tempfile, savefile);
	g_warning("Write failed!  Saved partial mailbox to %s", savefile);
	g_free(savefile);
	g_free(tempfile);
	return FALSE;
    }

    {
	struct utimbuf utimebuf;
	/* Restore the previous access/modification times */
	utimebuf.actime = st.st_atime;
	utimebuf.modtime = st.st_mtime;
	utime (path, &utimebuf);
    }

    unlink(tempfile); /* remove partial copy of the mailbox */
    g_free(tempfile);

    if (libbalsa_mailbox_get_state(mailbox) == LB_MAILBOX_STATE_CLOSING) {
	/* Just shorten the msg_info array. */
	for (j = first; j < mbox->msgno_2_msg_info->len; ) {
	    msg_info = message_info_from_msgno(mbox, j + 1);
	    if (expunge &&
		(msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	        libbalsa_mailbox_local_msgno_removed(mailbox, j + 1);
		g_ptr_array_remove_index(mbox->msgno_2_msg_info, j);
                mbox->messages_info_changed = TRUE;
	    } else
		j++;
	}
        lbm_mbox_save(mbox);
	return TRUE;
    }

    /* update the rewritten messages */
    libbalsa_mime_stream_shared_lock(mbox_stream);
    if (g_mime_stream_seek(mbox_stream, offset, GMIME_STREAM_SEEK_SET)
	== -1) {
	g_warning("Can't update message info");
	libbalsa_mime_stream_shared_unlock(mbox_stream);
	return FALSE;
    }
    gmime_parser = g_mime_parser_new_with_stream(mbox_stream);
    g_mime_parser_set_format(gmime_parser, GMIME_FORMAT_MBOX);
    g_mime_parser_set_respect_content_length(gmime_parser, TRUE);
    g_mime_parser_set_header_regex(gmime_parser,
                                   "^Status|^X-Status|^MIME-Version",
				   lbm_mbox_header_cb, &msg_info);
    for (j = first; j < mbox->msgno_2_msg_info->len; ) {
	GMimeMessage *mime_msg;
        gchar *from;

	msg_info = message_info_from_msgno(mbox, j + 1);
	if (expunge && (msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	    /* We must drop the mime-stream lock to call
	     * libbalsa_mailbox_local_msgno_removed(), as it will grab
	     * the gdk lock to emit gtk signals; we save and restore the
	     * current file position, in case someone changes it while
	     * we're not holding the lock. */
	    offset = g_mime_stream_tell(mbox_stream);
	    libbalsa_mime_stream_shared_unlock(mbox_stream);
	    libbalsa_mailbox_local_msgno_removed(mailbox, j + 1);
	    libbalsa_mime_stream_shared_lock(mbox_stream);
	    g_mime_stream_seek(mbox_stream, offset, GMIME_STREAM_SEEK_SET);
	    g_ptr_array_remove_index(mbox->msgno_2_msg_info, j);
            mbox->messages_info_changed = TRUE;
	    continue;
	}
	if (msg_info->local_info.message != NULL)
	    libbalsa_message_set_msgno(msg_info->local_info.message, j + 1);

	msg_info->status = msg_info->x_status = msg_info->mime_version = -1;
	mime_msg = g_mime_parser_construct_message(gmime_parser, libbalsa_parser_options());

        if (mime_msg == NULL) {
            /* Unrecoverable error */
            libbalsa_mime_stream_shared_unlock(mbox_stream);
            g_object_unref(gmime_parser);

            return FALSE;
        }

        msg_info->start = g_mime_parser_get_mbox_marker_offset(gmime_parser);

	/* Make sure we don't have offsets for any encapsulated headers. */
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_msg), "Status"))
	    msg_info->status = -1;
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_msg), "X-Status"))
	    msg_info->x_status = -1;
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_msg), "MIME-Version"))
	    msg_info->mime_version = -1;

	from = g_mime_parser_get_mbox_marker(gmime_parser);
        if (!from) {
            /* Try to recover */
            g_object_unref(mime_msg);
            continue;
        }

        msg_info->from_len = strlen(from) + 1;
        g_free(from);
	msg_info->end = g_mime_parser_tell(gmime_parser);
	msg_info->orig_flags = REAL_FLAGS(msg_info->local_info.flags);
	g_assert(mime_msg->mime_part != NULL);

	if (msg_info->local_info.message != NULL &&
            libbalsa_message_get_mime_message(msg_info->local_info.message) != NULL) {
            libbalsa_message_set_mime_message(msg_info->local_info.message, mime_msg);
	    /*
	     * reinit the message parts info
	     */
	    libbalsa_message_body_set_mime_body(libbalsa_message_get_body_list(msg_info->local_info.message),
						mime_msg->mime_part);
	}

        g_object_unref(mime_msg);

	j++;
    }
    libbalsa_mime_stream_shared_unlock(mbox_stream);
    g_object_unref(gmime_parser);
    lbm_mbox_save(mbox);

    return TRUE;
}

static LibBalsaMailboxLocalMessageInfo *
lbm_mbox_get_info(LibBalsaMailboxLocal * local, guint msgno)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(local);
    struct message_info *msg_info = message_info_from_msgno(mbox, msgno);

    return &msg_info->local_info;
}

static gboolean
libbalsa_mailbox_mbox_fetch_message_structure(LibBalsaMailbox * mailbox,
					      LibBalsaMessage * message,
					      LibBalsaFetchFlag flags)
{
    if (libbalsa_message_get_mime_message(message) == NULL) {
        GMimeMessage *mime_msg =
            lbm_mbox_get_mime_message(mailbox, libbalsa_message_get_msgno(message));
        libbalsa_message_set_mime_message(message, mime_msg);
        g_object_unref(mime_msg);
    }

    return LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mbox_parent_class)->
        fetch_message_structure(mailbox, message, flags);
}

/* Create the LibBalsaMessage and call libbalsa_message_init_from_gmime
 * to populate the headers we need for the display:
 *   headers->from
 *   headers->date
 *   headers->to_list
 *   headers->content_type
 *   subj
 *   length
 * and for threading:
 *   message_id
 *   references
 *   in_reply_to
 */
static LibBalsaMessage *
lbm_mbox_message_new(GMimeMessage * mime_message,
		     struct message_info *msg_info)
{
    LibBalsaMessage *message;
    const char *header;
    LibBalsaMessageFlag flags = 0;

#if defined(THIS_HAS_BEEN_TESTED)
    if (!g_strcmp0(mime_message->subject,
                   "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA")) {
	return NULL;
    }
#endif

    message = libbalsa_message_new();

    header = g_mime_object_get_header (GMIME_OBJECT(mime_message), "Status");
    if (header) {
	if (strchr(header, 'R') == NULL) /* not found == not READ */
	    flags |= LIBBALSA_MESSAGE_FLAG_NEW;
	if (strchr(header, 'r') != NULL) /* found == REPLIED */
	    flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
	if (strchr(header, 'O') == NULL) /* not found == RECENT */
	    flags |= LIBBALSA_MESSAGE_FLAG_RECENT;
    } else
	    flags |= LIBBALSA_MESSAGE_FLAG_NEW |  LIBBALSA_MESSAGE_FLAG_RECENT;
    header = g_mime_object_get_header (GMIME_OBJECT(mime_message), "X-Status");
    if (header) {
	if (strchr(header, 'D') != NULL) /* found == DELETED */
	    flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
	if (strchr(header, 'F') != NULL) /* found == FLAGGED */
	    flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
	if (strchr(header, 'A') != NULL) /* found == REPLIED */
	    flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
    }
    msg_info->orig_flags = flags;

    libbalsa_message_init_from_gmime(message, mime_message);

    return message;
}

static void update_message_status_headers(GMimeMessage *message,
					  LibBalsaMessageFlag flags)
{
    GString *new_header = g_string_new(NULL);

    /* Create headers with spaces in place of flags, if necessary, so we
     * can later update them in place. */
    lbm_mbox_status_hdr(flags, 2, new_header);
    g_mime_object_set_header(GMIME_OBJECT(message), "Status", new_header->str, NULL);
    g_string_truncate(new_header, 0);
    lbm_mbox_x_status_hdr(flags, 3, new_header);
    g_mime_object_set_header(GMIME_OBJECT(message), "X-Status", new_header->str, NULL);
    g_string_free(new_header, TRUE);
}


static GMimeObject *
lbm_mbox_armored_object(GMimeStream * stream)
{
    GMimeParser *parser;
    GMimeObject *object;

    parser = g_mime_parser_new_with_stream(stream);
    g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);
    object = GMIME_OBJECT(g_mime_parser_construct_message(parser, libbalsa_parser_options()));
    g_object_unref(parser);
    g_mime_object_encode(object, GMIME_ENCODING_CONSTRAINT_7BIT);

    return object;
}

static GMimeStream *
lbm_mbox_armored_stream(GMimeStream * stream)
{
    GMimeStream *fstream;
    GMimeFilter *filter;
    
    fstream = g_mime_stream_filter_new(stream);

    filter = g_mime_filter_dos2unix_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(fstream), filter);
    g_object_unref(filter);

    filter = g_mime_filter_from_new(GMIME_FILTER_FROM_MODE_ARMOR);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(fstream), filter);
    g_object_unref(filter);

    return fstream;
}

/* Called with mailbox locked. */
static gboolean
lbm_mbox_add_message(LibBalsaMailboxLocal * local,
                     GMimeStream          * stream,
                     LibBalsaMessageFlag    flags,
                     GError              ** err)
{
    LibBalsaMailbox *mailbox = (LibBalsaMailbox *) local;
    LibBalsaMessage *message;
    LibBalsaMessageHeaders *headers;
    gchar date_string[27];
    gchar *sender;
    gchar *address;
    gchar *brack;
    gchar *from = NULL;
    const char *path;
    int fd;
    GMimeObject *armored_object;
    GMimeStream *armored_dest;
    GMimeStream *dest;
    off_t retval;
    off_t orig_length;

    message = libbalsa_message_new();
    libbalsa_message_load_envelope_from_stream(message, stream);

    headers = libbalsa_message_get_headers(message);
    ctime_r(&(headers->date), date_string);

    sender = headers->from != NULL ?
        internet_address_list_to_string(headers->from, NULL, FALSE) :
	g_strdup("none");

    g_object_unref(message);

    if ( (brack = strrchr( sender, '<' )) ) {
        gchar * a = strrchr ( brack , '>' );
        if (a)
            address = g_strndup(brack + 1, a - brack - 1);
        else
            address = g_strdup("none");
	g_free(sender);
    } else {
        address = sender;
    }
    from = g_strdup_printf ("From %s %s", address, date_string );
    g_free(address);
    
    path = libbalsa_mailbox_local_get_path(local);
    /* open in read-write mode */
    fd = open(path, O_RDWR);
    if (fd < 0) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("%s: could not open %s."), "MBOX", path);
        g_free(from);
        return FALSE;
    }
    
    orig_length = lseek (fd, 0, SEEK_END);
    lseek (fd, 0, SEEK_SET);
    dest = g_mime_stream_fs_new (fd);
    if (!dest) {
	g_free(from);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("%s: could not get new MIME stream."),
                    "MBOX");
	return FALSE;
    }
    if (orig_length > 0 && !lbm_mbox_stream_seek_to_message(dest, 0)) {
	g_object_unref(dest);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("%s: %s is not in mbox format."),
                    "MBOX", path);
	g_free(from);
	return FALSE;
    }
    mbox_lock ( mailbox, dest );

    /* From_ armor */
    libbalsa_mime_stream_shared_lock(stream);
    g_mime_stream_reset(stream);
    armored_object = lbm_mbox_armored_object(stream);
    /* Make sure we have "Status" and "X-Status" headers, so we can
     * update them in place later, if necessary. */
    update_message_status_headers(GMIME_MESSAGE(armored_object),
                                  flags | LIBBALSA_MESSAGE_FLAG_RECENT);
    armored_dest = lbm_mbox_armored_stream(dest);

    retval = g_mime_stream_seek(dest, 0, GMIME_STREAM_SEEK_END);
    if (retval > 0)
        retval = lbm_mbox_newline(dest);
    if (retval < 0
        || g_mime_stream_write_string(dest, from) < (gint) strlen(from)
	|| g_mime_object_write_to_stream(armored_object, NULL, armored_dest) < 0) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR, _("Data copy error"));
	retval = -1;
    }
    g_free(from);
    g_object_unref(armored_object);
    libbalsa_mime_stream_shared_unlock(stream);
    g_object_unref(armored_dest);

    if (retval < 0 && truncate(path, orig_length) < 0)
        retval = -2;
    mbox_unlock (mailbox, dest);
    g_object_unref(dest);

    return retval >= 0;
}

static guint
libbalsa_mailbox_mbox_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMbox *mbox = (LibBalsaMailboxMbox *) mailbox;

    return mbox->msgno_2_msg_info ? mbox->msgno_2_msg_info->len : 0;
}

static void
libbalsa_mailbox_mbox_lock_store(LibBalsaMailbox * mailbox, gboolean lock)
{
    LibBalsaMailboxMbox *mbox = (LibBalsaMailboxMbox *) mailbox;
    GMimeStream *stream = mbox->gmime_stream;

    if (lock)
        libbalsa_mime_stream_shared_lock(stream);
    else
        libbalsa_mime_stream_shared_unlock(stream);
}
