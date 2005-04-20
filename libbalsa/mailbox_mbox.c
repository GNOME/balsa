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

#define _XOPEN_SOURCE 500

#include <gmime/gmime-stream-fs.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

/* we include time because pthread.h may require it when compiled with c89 */
#include <time.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
/* for mx_lock_file and mx_unlock_file */
#include "mailbackend.h"
#include "i18n.h"

struct message_info {
    off_t start;
    off_t status;		/* Offset of the "Status:" header. */
    off_t x_status;		/* Offset of the "X-Status:" header. */
    off_t mime_version;		/* Offset of the "MIME-Version:" header. */
    off_t end;
    char *from;
    LibBalsaMessage *message; /* registers only referenced messages
			       * to avoid having two objects refering 
			       * to a single physical message. */
    LibBalsaMessageFlag flags;
    LibBalsaMessageFlag orig_flags;
};

static LibBalsaMailboxLocalClass *parent_class = NULL;

static void libbalsa_mailbox_mbox_class_init(LibBalsaMailboxMboxClass *klass);
static void libbalsa_mailbox_mbox_init(LibBalsaMailboxMbox * mailbox);

static GMimeStream *libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *
							     mailbox,
							     LibBalsaMessage *
							     message);
static void libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox);

static gboolean libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox,
					   GError **err);
static void libbalsa_mailbox_mbox_close_mailbox(LibBalsaMailbox * mailbox,
                                                gboolean expunge);
static void libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mbox_sync(LibBalsaMailbox * mailbox,
                                           gboolean expunge);
static LibBalsaMessage *libbalsa_mailbox_mbox_get_message(
						  LibBalsaMailbox * mailbox,
						  guint msgno);
static gboolean
libbalsa_mailbox_mbox_fetch_message_structure(LibBalsaMailbox * mailbox,
                                              LibBalsaMessage * message,
                                              LibBalsaFetchFlag flags);
static int libbalsa_mailbox_mbox_add_message(LibBalsaMailbox * mailbox,
                                             LibBalsaMessage *message,
                                             GError **err);
static gboolean
libbalsa_mailbox_mbox_messages_change_flags(LibBalsaMailbox * mailbox,
                                            GArray * msgnos,
                                            LibBalsaMessageFlag set,
                                            LibBalsaMessageFlag clear);
static gboolean
libbalsa_mailbox_mbox_msgno_has_flags(LibBalsaMailbox * mailbox,
                                      guint msgno,
                                      LibBalsaMessageFlag set,
                                      LibBalsaMessageFlag unset);
static guint
libbalsa_mailbox_mbox_total_messages(LibBalsaMailbox * mailbox);

struct _LibBalsaMailboxMboxClass {
    LibBalsaMailboxLocalClass klass;
};

struct _LibBalsaMailboxMbox {
    LibBalsaMailboxLocal parent;

    GArray* messages_info;
    GMimeStream *gmime_stream;
    gint size;
    time_t mtime;
#ifdef BALSA_USE_THREADS
    /* For locking the GMimeStream: */
    pthread_t thread_id;
    guint lock;
#endif /* BALSA_USE_THREADS */
};

GType libbalsa_mailbox_mbox_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxMboxClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_mbox_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxMbox),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_mbox_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX_LOCAL,
	                           "LibBalsaMailboxMbox",
                                   &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_mbox_class_init(LibBalsaMailboxMboxClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_mbox_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_mbox_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_mbox_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_mbox_sync;
    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_mbox_close_mailbox;
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_mbox_get_message;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_mbox_fetch_message_structure;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_mbox_add_message;
    libbalsa_mailbox_class->messages_change_flags =
	libbalsa_mailbox_mbox_messages_change_flags;
    libbalsa_mailbox_class->msgno_has_flags =
	libbalsa_mailbox_mbox_msgno_has_flags;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_mbox_total_messages;

    libbalsa_mailbox_local_class->remove_files = 
	libbalsa_mailbox_mbox_remove_files;

    libbalsa_mailbox_local_class->load_message =
        libbalsa_mailbox_mbox_get_message;
}



static void
libbalsa_mailbox_mbox_init(LibBalsaMailboxMbox * mbox)
{
#ifdef BALSA_USE_THREADS
    mbox->thread_id = 0;
    mbox->lock = 0;
#endif /* BALSA_USE_THREADS */
}

gint
libbalsa_mailbox_mbox_create(const gchar * path, gboolean create)
{
    gint exists; 
    GType magic_type;
    gint fd;
    
    g_return_val_if_fail( path != NULL, -1);

    exists = access(path, F_OK);
    if ( exists == 0 ) {
	/* File exists. Check if it is an mbox... */
	
	magic_type = libbalsa_mailbox_type_from_path(path);
	if ( magic_type != LIBBALSA_TYPE_MAILBOX_MBOX ) {
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				 _("Mailbox %s does not appear to be an Mbox mailbox."), path);
	    return(-1);
	}
    } else {
	if(!create)
	    return(-1);

	if ((fd = creat(path, S_IRUSR | S_IWUSR)) == -1) {
	    g_warning("An error:\n%s\n occured while trying to "
		      "create the mailbox \"%s\"\n",
		      strerror(errno), path);
	    return -1;
	} else {
	    close(fd);
	}
    }
    return(0);
}

GObject *
libbalsa_mailbox_mbox_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MBOX, NULL);
    mailbox->is_directory = FALSE;
	
    mailbox->url = g_strconcat("file://", path, NULL);
    if( libbalsa_mailbox_mbox_create(path, create) < 0 ) {
	g_object_unref(G_OBJECT(mailbox));
	return NULL;
    }
    
    return G_OBJECT(mailbox);
}

#ifdef BALSA_USE_THREADS
/* 
 * Paranoia lock: must be held while repositioning mbox->gmime_stream,
 * to ensure that we can seek and read or write without interruption.
 */
static pthread_cond_t mbox_cond = PTHREAD_COND_INITIALIZER;

static void
lbm_mbox_mime_stream_lock(LibBalsaMailboxMbox * mbox)
{
    pthread_t thread_id;

    pthread_mutex_lock(&mailbox_lock);

    thread_id = pthread_self();
    if (mbox->thread_id != thread_id) {
	while (mbox->lock)
	    pthread_cond_wait(&mbox_cond, &mailbox_lock);
	mbox->thread_id = thread_id;
    }

    mbox->lock++;

    pthread_mutex_unlock(&mailbox_lock);
}

static void
lbm_mbox_mime_stream_unlock(LibBalsaMailboxMbox * mbox)
{
    pthread_mutex_lock(&mailbox_lock);

    if (!--mbox->lock)
        pthread_cond_broadcast(&mbox_cond);
    /* No need to clear mbox->thread_id. */

    pthread_mutex_unlock(&mailbox_lock);
}
#else /* BALSA_USE_THREADS */
#define lbm_mbox_mime_stream_lock(m)
#define lbm_mbox_mime_stream_unlock(m)
#endif /* BALSA_USE_THREADS */

/* Helper: seek to offset, and return TRUE if the seek succeeds and a
 * message begins there. */
static gboolean
lbm_mbox_stream_seek_to_message(GMimeStream * stream, off_t offset)
{
    char buffer[5];
    gboolean retval;

    retval = g_mime_stream_seek(stream, offset, GMIME_STREAM_SEEK_SET) >= 0
        && g_mime_stream_read(stream, buffer, sizeof(buffer)) >= 0
        && strncmp("From ", buffer, 5) == 0;

    g_mime_stream_seek(stream, offset, GMIME_STREAM_SEEK_SET);

    return retval;
}

static gboolean
lbm_mbox_seek_to_message(LibBalsaMailboxMbox * mbox, off_t offset)
{
    gboolean retval;
    
    lbm_mbox_mime_stream_lock(mbox);
    retval = lbm_mbox_stream_seek_to_message(mbox->gmime_stream, offset);
    lbm_mbox_mime_stream_unlock(mbox);

    return retval;
}

static GMimeStream *
libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *mailbox,
                                         LibBalsaMessage *message)
{
	GMimeStream *stream = NULL;
	struct message_info *msg_info;
	LibBalsaMailboxMbox *mbox;

	g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox), NULL);
	g_return_val_if_fail (LIBBALSA_IS_MESSAGE(message), NULL);

	mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

	msg_info = &g_array_index(mbox->messages_info,
				  struct message_info, message->msgno - 1);
	if (!lbm_mbox_seek_to_message(mbox, msg_info->start))
	    return NULL;
	stream = g_mime_stream_substream(mbox->gmime_stream,
					 msg_info->start
					 + strlen(msg_info->from) + 1,
					 msg_info->end);

	return stream;
}

static void
libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox)
{
    g_return_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox));

    if ( unlink(libbalsa_mailbox_local_get_path(mailbox)) == -1 )
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
			     _("Could not remove %s:\n%s"), 
			     libbalsa_mailbox_local_get_path(mailbox), 
			     strerror(errno));
}

static int mbox_lock(LibBalsaMailbox * mailbox, GMimeStream *stream)
{
    const gchar *path = libbalsa_mailbox_local_get_path(mailbox);
    int fd;
    if (stream)
	fd = GMIME_STREAM_FS(stream)->fd;
    else
	fd = GMIME_STREAM_FS(LIBBALSA_MAILBOX_MBOX(mailbox)->gmime_stream)->fd;
    return libbalsa_lock_file(path, fd, FALSE, TRUE, 1);
}

static void mbox_unlock(LibBalsaMailbox * mailbox, GMimeStream *stream)
{
    const gchar *path = libbalsa_mailbox_local_get_path(mailbox);
    int fd;
    if (stream)
	fd = GMIME_STREAM_FS(stream)->fd;
    else
	fd = GMIME_STREAM_FS(LIBBALSA_MAILBOX_MBOX(mailbox)->gmime_stream)->fd;
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
                   const char *value, off_t offset,
                   struct message_info **msg_info_p)
{
    struct message_info *msg_info = *msg_info_p;

    if (g_ascii_strcasecmp(header, "Status") == 0 && msg_info->status < 0)
        msg_info->status = offset;
    else if (g_ascii_strcasecmp(header, "X-Status") == 0
             && msg_info->x_status < 0)
        msg_info->x_status = offset;
    else if (g_ascii_strcasecmp(header, "MIME-Version") == 0
             && msg_info->mime_version < 0)
        msg_info->mime_version = offset;
}

static LibBalsaMessage *lbm_mbox_message_new(GMimeMessage * mime_message,
					     struct message_info
					     *msg_info);
extern int message_cnt;
static void
parse_mailbox(LibBalsaMailboxMbox * mbox)
{
    GMimeParser *gmime_parser;
    struct message_info msg_info;
    struct message_info * msg_info_p = &msg_info;
    unsigned msgno = 0;

    gmime_parser = g_mime_parser_new_with_stream(mbox->gmime_stream);
    g_mime_parser_set_scan_from(gmime_parser, TRUE);
    g_mime_parser_set_respect_content_length(gmime_parser, TRUE);
    g_mime_parser_set_header_regex(gmime_parser,
                                   "^Status|^X-Status|^MIME-Version",
				   (GMimeParserHeaderRegexFunc)
				   lbm_mbox_header_cb, &msg_info_p);

    msg_info.message = NULL;
    while (!g_mime_parser_eos(gmime_parser)) {
	GMimeMessage *mime_message;
        LibBalsaMessage *msg;

        msg_info.status = msg_info.x_status = msg_info.mime_version = -1;
        mime_message   = g_mime_parser_construct_message(gmime_parser);
        msg_info.start = g_mime_parser_get_from_offset(gmime_parser);
        msg_info.end   = g_mime_parser_tell(gmime_parser);
        if (msg_info.end <= msg_info.start
            || !(msg_info.from = g_mime_parser_get_from(gmime_parser))) {
	    g_object_unref(mime_message);
            continue;
	}

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
        if (!msg) {
            g_free(msg_info.from);
            continue;
        }

        msg_info.flags = msg_info.orig_flags;
        g_array_append_val(mbox->messages_info, msg_info);

        msg->flags = msg_info.orig_flags;
        msg->length = msg_info.end - msg_info.start;
        msg->mailbox = LIBBALSA_MAILBOX(mbox);
        msg->msgno = ++msgno;
        g_ptr_array_add(LIBBALSA_MAILBOX(mbox)->mindex, 
                        libbalsa_mailbox_index_entry_new_from_msg(msg));
        g_object_unref(msg);
    }

    g_object_unref(G_OBJECT(gmime_parser));
    printf("done, msgcnt=%d\n", message_cnt);
}

static void
mbox_msg_unref(gpointer data, GObject *msg)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(data);
    unsigned msgno = LIBBALSA_MESSAGE(msg)->msgno;
    struct message_info *msg_info =
	&g_array_index(mbox->messages_info, struct message_info, msgno - 1);
    msg_info->message = NULL;
}

static void
free_message_info(struct message_info *msg_info)
{
    g_free(msg_info->from);
    msg_info->from = NULL;
    if (msg_info->message) {
	g_object_weak_unref(G_OBJECT(msg_info->message), mbox_msg_unref,
			    msg_info->message->mailbox);
	msg_info->message->mailbox = NULL;
	msg_info->message->msgno   = 0;
	msg_info->message = NULL;
    }
}

static void
free_messages_info(GArray * messages_info)
{
    guint i;

    for (i = 0; i < messages_info->len; i++) {
	struct message_info *msg_info =
	    &g_array_index(messages_info, struct message_info, i);
	free_message_info(msg_info);
    }
    g_array_free(messages_info, TRUE);
}

static gboolean
libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox, GError **err)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    struct stat st;
    const gchar* path;
    int fd;
    GMimeStream *gmime_stream;
    time_t t0;

    path = libbalsa_mailbox_local_get_path(mailbox);

    if (stat(path, &st) == -1) {
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Mailbox does not exist."));
	return FALSE;
    }

    mailbox->readonly = access (path, W_OK);
    fd = open(path, mailbox->readonly ? O_RDONLY : O_RDWR);
    if (fd == -1) {
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Cannot open mailbox."));
	return FALSE;
    }
    gmime_stream = g_mime_stream_fs_new(fd);

    if (st.st_size > 0
        && !lbm_mbox_stream_seek_to_message(gmime_stream, 0)) {
        g_object_unref(gmime_stream);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_OPEN_ERROR,
                    _("Mailbox is not in mbox format."));
        return FALSE;
    }

    if (mbox_lock(mailbox, gmime_stream)) {
	g_object_unref(gmime_stream);
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Cannot lock mailbox."));
	return FALSE;
    }

    mbox->size = st.st_size;
    mbox->mtime = st.st_mtime;
    mbox->gmime_stream = gmime_stream;

    mbox->messages_info =
	g_array_new(FALSE, FALSE, sizeof(struct message_info));

    mailbox->unread_messages = 0;
    time(&t0);
    if (st.st_size != 0)
	parse_mailbox(mbox);
    LIBBALSA_MAILBOX_LOCAL(mailbox)->sync_time = time(NULL) - t0;
    LIBBALSA_MAILBOX_LOCAL(mailbox)->sync_cnt  = 1;

    mbox_unlock(mailbox, NULL);
#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxMbox", mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

/* Check for new mail in a closed mbox, using a crude parser. */
static gboolean
lbm_mbox_check(LibBalsaMailbox * mailbox, const gchar * path)
{
    int fd;
    GMimeStream *gmime_stream;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;
    gboolean retval = FALSE;
    gboolean eos;

    if (!(fd = open(path, O_RDONLY)))
	return retval;

    gmime_stream = g_mime_stream_fs_new(fd);
    gmime_stream_buffer =
	g_mime_stream_buffer_new(gmime_stream,
				 GMIME_STREAM_BUFFER_BLOCK_READ);
    g_object_unref(gmime_stream);

    if (mbox_lock(mailbox, gmime_stream)) {
	g_object_unref(gmime_stream_buffer);
	return FALSE;
    }

    line = (GByteArray *) g_array_sized_new(TRUE, FALSE, 1, 80);
    do {
	gboolean new_undeleted;
	guint content_length;

	/* Find the next From_ line; if it's inside a message, protected
	 * by an embedded Content-Length header, we may be misled, but a
	 * full GMime parse takes too long. */
	do {
	    line->len = 0;
	    g_mime_stream_buffer_readln(gmime_stream_buffer, line);
	} while (!(eos = g_mime_stream_eos(gmime_stream_buffer))
		 && !libbalsa_str_has_prefix((gchar *) line->data, "From "));
	if (eos)
	    break;

	/* Scan headers. */
	new_undeleted = TRUE;
	content_length = 0;
	do {
	    line->len = 0;
	    g_mime_stream_buffer_readln(gmime_stream_buffer, line);
	    if (g_ascii_strncasecmp((gchar *) line->data,
				    "Status: ", 8) == 0) {
		if (strchr((gchar *) line->data + 8, 'R'))
		    new_undeleted = FALSE;
	    } else if (g_ascii_strncasecmp((gchar *) line->data,
				           "X-Status: ", 10) == 0) {
		if (strchr((gchar *) line->data + 10, 'D'))
		    new_undeleted = FALSE;
	    } else
		if (g_ascii_strncasecmp((gchar *) line->data,
					"Content-Length: ", 16) == 0) {
		content_length = atoi((gchar *) line->data + 16);
	    }
	    /* Blank line ends headers. */
	} while (!(eos = g_mime_stream_eos(gmime_stream_buffer))
		 && line->data[0] != '\n');

	if (new_undeleted) {
	    retval = TRUE;
	    /* One new message is enough. */
	    break;
	}

	if (content_length)
	    /* SEEK_CUR seems to be broken for stream-buffer. */
	    g_mime_stream_seek(gmime_stream_buffer,
			       (g_mime_stream_tell(gmime_stream_buffer)
				+ content_length),
			       GMIME_STREAM_SEEK_SET);
    } while (!eos);

    mbox_unlock(mailbox, gmime_stream);
    g_object_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);

    return retval;
}

/* Called with mailbox locked. */
static void
libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox)
{
    struct stat st;
    const gchar *path;
    LibBalsaMailboxMbox *mbox;
    guint msgno;

    g_assert(LIBBALSA_IS_MAILBOX_MBOX(mailbox));

    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    path = libbalsa_mailbox_local_get_path(mailbox);
    if (mbox->gmime_stream ?
        fstat(GMIME_STREAM_FS(mbox->gmime_stream)->fd, &st) :
        stat(path, &st)) {
	perror(path);
	return;
    }

    if (mbox->mtime == 0) {
	/* First check--just cache the mtime and size. */
	mbox->mtime = st.st_mtime;
	mbox->size = st.st_size;
	return;
    }
    if (st.st_mtime == mbox->mtime && st.st_size == mbox->size)
	return;

    mbox->mtime = st.st_mtime;

    if (!MAILBOX_OPEN(mailbox)) {
	libbalsa_mailbox_set_unread_messages_flag(mailbox,
						  lbm_mbox_check(mailbox,
								 path));
	/* Cache the file size, so we don't check the next time. */
	mbox->size = st.st_size;
	return;
    }

    if (mbox_lock(mailbox, NULL) != 0)
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
    lbm_mbox_mime_stream_lock(mbox);
    while ((msgno = mbox->messages_info->len) > 0) {
	off_t offset;
        struct message_info *msg_info =
            &g_array_index(mbox->messages_info, struct message_info,
                           msgno - 1);
        if (lbm_mbox_seek_to_message(mbox, msg_info->end))
	    /* A message begins at the end of this one, so it must(?) be
	     * in its original position--start parsing here. */
            break;

	/* Back up over this message and try again. */
        if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_NEW)
            && !(msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED))
            --mailbox->unread_messages;

	/* We must drop the mime-stream lock to call
	 * libbalsa_mailbox_local_msgno_removed(), as it will grab the
	 * gdk lock to emit gtk signals; we save and restore the current
	 * stream position, in case someone changes it while we're not
	 * holding the lock. */
	offset = g_mime_stream_tell(mbox->gmime_stream);
	lbm_mbox_mime_stream_unlock(mbox);
        libbalsa_mailbox_local_msgno_removed(mailbox, msgno);
	lbm_mbox_mime_stream_lock(mbox);
        g_mime_stream_seek(mbox->gmime_stream, offset, GMIME_STREAM_SEEK_SET);

        free_message_info(msg_info);
        g_array_remove_index(mbox->messages_info, msgno - 1);
    }
    if(msgno == 0)
        g_mime_stream_seek(mbox->gmime_stream, 0, GMIME_STREAM_SEEK_SET);
    parse_mailbox(mbox);
    mbox->size = g_mime_stream_tell(mbox->gmime_stream);
    lbm_mbox_mime_stream_unlock(mbox);
    mbox_unlock(mailbox, NULL);
    libbalsa_mailbox_local_load_messages(mailbox, msgno);
}

static void
libbalsa_mailbox_mbox_close_mailbox(LibBalsaMailbox * mailbox,
                                    gboolean expunge)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

    if (mbox->messages_info) {
	guint len;

	len = mbox->messages_info->len;
        libbalsa_mailbox_mbox_sync(mailbox, expunge);
	if (mbox->messages_info->len != len)
	    libbalsa_mailbox_changed(mailbox);
	free_messages_info(mbox->messages_info);
	mbox->messages_info = NULL;
    }
    if (mbox->gmime_stream) {
	g_object_unref(mbox->gmime_stream);
	mbox->gmime_stream = NULL;	/* chbm: is this correct? */
    }
    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
        LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox(mailbox,
                                                            expunge);
}

static GMimeMessage *
lbm_mbox_get_mime_message(LibBalsaMailbox * mailbox,
			  LibBalsaMessage * message)
{
    GMimeStream *stream;
    GMimeParser *parser;
    GMimeMessage *mime_message;

    stream = libbalsa_mailbox_mbox_get_message_stream(mailbox, message);
    if (!stream)
	return NULL;
    parser = g_mime_parser_new_with_stream(stream);
    g_object_unref(stream);

    mime_message = g_mime_parser_construct_message(parser);
    g_object_unref(parser);

    return mime_message;
}

/* Write one or two newlines to stream. */
static gint
lbm_mbox_newline(GMimeStream * stream)
{
    gint retval;
    gchar buf[1];
    static gchar newlines[] = "\n\n";

    retval = g_mime_stream_seek(stream, -1, GMIME_STREAM_SEEK_CUR);
    if (retval >= 0)
	retval = g_mime_stream_read(stream, buf, 1);
    if (retval == 1)
	retval =
	    g_mime_stream_write(stream, newlines, buf[0] == '\n' ? 1 : 2);

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
    lbm_mbox_status_hdr(msg_info->flags, 0, header);
    g_assert(header->len <= 3);

    if (!lbm_mbox_rewrite_helper(msg_info->status, stream, header,
				 status_buf, sizeof(status_buf),
				 "Status: ")) {
	g_string_free(header, TRUE);
	return FALSE;
    }

    /* Get the flags for the "X-Status" header. */
    g_string_truncate(header, 0);
    lbm_mbox_x_status_hdr(msg_info->flags, 0, header);
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
    msg_info->orig_flags = msg_info->flags & LIBBALSA_MESSAGE_FLAGS_REAL;
    return TRUE;
}

/* Length of the line beginning at offset, including trailing '\n'.
 * Returns -1 if no '\n' found, or if seek to offset fails. */
static gint
lbm_mbox_line_len(LibBalsaMailboxMbox * mbox, off_t offset)
{
    GMimeStream *stream = mbox->gmime_stream;
    gint retval = -1;

    lbm_mbox_mime_stream_lock(mbox);
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
    lbm_mbox_mime_stream_unlock(mbox);

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
    lbm_mbox_mime_stream_lock(mbox);
    retval = g_mime_stream_write_to_stream(substream, dest) == end - start;
    lbm_mbox_mime_stream_unlock(mbox);
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
    if (mbox->messages_info->len == 0)
	return TRUE;
    mbox_stream = mbox->gmime_stream;

    path = libbalsa_mailbox_local_get_path(mailbox);

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
    messages = mbox->messages_info->len;
    first = -1;
    for (i = j = 0; i < messages; i++)
    {
	msg_info = &g_array_index(mbox->messages_info,
		       struct message_info, i);
	if (mailbox->state == LB_MAILBOX_STATE_CLOSING)
	    msg_info->flags &= ~LIBBALSA_MESSAGE_FLAG_RECENT;
	if (expunge && (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED))
	    break;
	if (first < 0 && (msg_info->status < 0 || msg_info->x_status < 0))
	    first = i;
        if ((msg_info->orig_flags ^ msg_info->flags)
            & LIBBALSA_MESSAGE_FLAGS_REAL) {
	    gboolean can_rewrite_in_place;

	    lbm_mbox_mime_stream_lock(mbox);
	    can_rewrite_in_place =
		lbm_mbox_rewrite_in_place(msg_info, mbox_stream);
	    lbm_mbox_mime_stream_unlock(mbox);
	    if (!can_rewrite_in_place)
		break;
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
	    g_warning("can't flush mailbox stream\n");
	if (fstat(GMIME_STREAM_FS(mbox_stream)->fd, &st))
	    g_warning("can't stat \"%s\"", path);
	else
	    mbox->mtime = st.st_mtime;
	mbox_unlock(mailbox, mbox_stream);
	return TRUE;
    }

    /* save the index of the first changed/deleted message */
    if (first < 0)
	first = i; 
    /* where to start overwriting */
    offset =
	g_array_index(mbox->messages_info, struct message_info, first).start;

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
	guint status_len, x_status_len;

	msg_info = &g_array_index(mbox->messages_info,
				  struct message_info, i);
	if (expunge && (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED))
	    continue;

	if (msg_info->status >= 0) {
	    status_len = lbm_mbox_line_len(mbox, msg_info->status);
	    if (status_len < 0)
		break;
	} else {
            msg_info->status = msg_info->mime_version >= 0 ?
		msg_info->mime_version :
                (off_t) (msg_info->start + strlen(msg_info->from) + 1);
	    
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
		|| !lbm_mbox_write_status_hdr(temp_stream, msg_info->flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->status + status_len,
					 msg_info->x_status, temp_stream)
		|| !lbm_mbox_write_x_status_hdr(temp_stream,
						msg_info->flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->x_status +
					 x_status_len, msg_info->end,
					 temp_stream))
		break;
	} else {
	    if (!lbm_mbox_copy_stream(mbox, msg_info->start,
				      msg_info->x_status, temp_stream)
		|| !lbm_mbox_write_x_status_hdr(temp_stream,
						msg_info->flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->x_status +
					 x_status_len,
					 msg_info->status, temp_stream)
		|| !lbm_mbox_write_status_hdr(temp_stream, msg_info->flags)
		|| !lbm_mbox_copy_stream(mbox,
					 msg_info->status + status_len,
					 msg_info->end, temp_stream))
		break;
	}
    }

    if (i < messages) {
	/* We broke on an error. */
	g_warning("error making temporary copy\n");
	g_object_unref(temp_stream);
	unlink(tempfile);
	g_free(tempfile);
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }

    g_mime_stream_set_bounds(mbox_stream, 0, -1);
    if (g_mime_stream_flush(temp_stream) == -1)
    {
	g_warning("can't flush temporary copy\n");
	g_object_unref(temp_stream);
	unlink(tempfile);
	g_free(tempfile);
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }

    save_failed = TRUE;
    lbm_mbox_mime_stream_lock(mbox);
    if (g_mime_stream_reset(temp_stream) == -1) {
        g_warning("mbox_sync: can't rewind temporary copy.\n");
    } else if (!lbm_mbox_seek_to_message(mbox, offset))
        g_warning("mbox_sync: message not in expected position.\n");
    else if (g_mime_stream_write_to_stream(temp_stream, mbox_stream) != -1) {
        save_failed = FALSE;
        mbox->size = g_mime_stream_tell(mbox_stream);
        ftruncate(GMIME_STREAM_FS(mbox_stream)->fd, mbox->size);
    }
    lbm_mbox_mime_stream_unlock(mbox);
    g_object_unref(temp_stream);
    mbox_unlock(mailbox, mbox_stream);
    if (g_mime_stream_flush(mbox_stream) == -1 || save_failed)
    {
	/*
	 * error occured while writing the mailbox back,
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

    if (mailbox->state == LB_MAILBOX_STATE_CLOSING) {
	/* Just shorten the msg_info array. */
	for (j = first; j < mbox->messages_info->len; ) {
	    msg_info = &g_array_index(mbox->messages_info,
				      struct message_info, j);
	    if (expunge &&
		(msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
		free_message_info(msg_info);
		g_array_remove_index(mbox->messages_info, j);
                libbalsa_mailbox_index_entry_free(g_ptr_array_index
                                                  (mailbox->mindex, j));
		g_ptr_array_remove_index(mailbox->mindex, j);
	    } else
		j++;
	}
	return TRUE;
    }

    /* update the rewritten messages */
    lbm_mbox_mime_stream_lock(mbox);
    if (g_mime_stream_seek(mbox_stream, offset, GMIME_STREAM_SEEK_SET)
	== -1) {
	g_warning("Can't update message info");
	lbm_mbox_mime_stream_unlock(mbox);
	return FALSE;
    }
    gmime_parser = g_mime_parser_new_with_stream(mbox_stream);
    g_mime_parser_set_scan_from(gmime_parser, TRUE);
    g_mime_parser_set_respect_content_length(gmime_parser, TRUE);
    g_mime_parser_set_header_regex(gmime_parser,
                                   "^Status|^X-Status|^MIME-Version",
				   (GMimeParserHeaderRegexFunc)
				   lbm_mbox_header_cb, &msg_info);
    for (j = first; j < mbox->messages_info->len; ) {
	GMimeMessage *mime_msg;

	msg_info =
	    &g_array_index(mbox->messages_info, struct message_info, j);
	if (expunge && (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	    /* We must drop the mime-stream lock to call
	     * libbalsa_mailbox_local_msgno_removed(), as it will grab
	     * the gdk lock to emit gtk signals; we save and restore the
	     * current file position, in case someone changes it while
	     * we're not holding the lock. */
	    offset = g_mime_stream_tell(mbox_stream);
	    lbm_mbox_mime_stream_unlock(mbox);
	    libbalsa_mailbox_local_msgno_removed(mailbox, j + 1);
	    lbm_mbox_mime_stream_lock(mbox);
	    g_mime_stream_seek(mbox_stream, offset, GMIME_STREAM_SEEK_SET);
	    free_message_info(msg_info);
	    g_array_remove_index(mbox->messages_info, j);
	    continue;
	}
	if (msg_info->message)
	    msg_info->message->msgno = j + 1;
	j++;

	msg_info->start = g_mime_parser_tell(gmime_parser);
	msg_info->status = msg_info->x_status = msg_info->mime_version = -1;
	mime_msg = g_mime_parser_construct_message(gmime_parser);

	/* Make sure we don't have offsets for any encapsulated headers. */
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_msg), "Status"))
	    msg_info->status = -1;
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_msg), "X-Status"))
	    msg_info->x_status = -1;
	if (!g_mime_object_get_header(GMIME_OBJECT(mime_msg), "MIME-Version"))
	    msg_info->mime_version = -1;

	g_free(msg_info->from);
	msg_info->from = g_mime_parser_get_from(gmime_parser);
	msg_info->end = g_mime_parser_tell(gmime_parser);
	msg_info->orig_flags = msg_info->flags & LIBBALSA_MESSAGE_FLAGS_REAL;
	g_assert(mime_msg != NULL);
	g_assert(mime_msg->mime_part != NULL);
	if (!msg_info->message || !msg_info->message->mime_msg)
	    g_object_unref(mime_msg);
	else {
	    g_object_unref(msg_info->message->mime_msg);
	    msg_info->message->mime_msg = mime_msg;
	    /*
	     * reinit the message parts info
	     */
	    libbalsa_message_body_set_mime_body(msg_info->message->body_list,
						mime_msg->mime_part);
	}
    }
    lbm_mbox_mime_stream_unlock(mbox);
    mbox->messages_info->len = j;
    g_object_unref(G_OBJECT(gmime_parser));

    return TRUE;
}

static LibBalsaMessage*
libbalsa_mailbox_mbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    struct message_info *msg_info;

    msg_info = &g_array_index(mbox->messages_info,
			      struct message_info, msgno-1);
    if(!msg_info->message) {
	GMimeParser *gmime_parser;
	GMimeMessage *mime_message;

	gmime_parser = g_mime_parser_new_with_stream(mbox->gmime_stream);
	g_mime_parser_set_scan_from(gmime_parser, TRUE);
	g_mime_parser_set_respect_content_length(gmime_parser, TRUE);

	lbm_mbox_mime_stream_lock(mbox);
	g_mime_stream_seek(mbox->gmime_stream, msg_info->start,
			   GMIME_STREAM_SEEK_SET);
	mime_message = g_mime_parser_construct_message(gmime_parser);
	lbm_mbox_mime_stream_unlock(mbox);

	msg_info->message = lbm_mbox_message_new(mime_message, msg_info);
	msg_info->message->mailbox = mailbox;
	msg_info->message->msgno   = msgno;
	msg_info->message->flags   =
	    msg_info->flags & LIBBALSA_MESSAGE_FLAGS_REAL;
	g_object_unref(mime_message);
	g_object_unref(gmime_parser);
	g_object_weak_ref(G_OBJECT(msg_info->message), mbox_msg_unref,
			  mbox);
    } else g_object_ref(msg_info->message);
    return msg_info->message;
}

static gboolean
libbalsa_mailbox_mbox_fetch_message_structure(LibBalsaMailbox * mailbox,
					      LibBalsaMessage * message,
					      LibBalsaFetchFlag flags)
{
    if (!message->mime_msg)
	message->mime_msg = lbm_mbox_get_mime_message(mailbox, message);

    return LIBBALSA_MAILBOX_CLASS(parent_class)->
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
    if (mime_message->subject &&
	!strcmp(mime_message->subject,
		"DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA")) {
	return NULL;
    }
#endif

    message = libbalsa_message_new();

    header = g_mime_message_get_header (mime_message, "Status");
    if (header) {
	if (strchr(header, 'R') == NULL) /* not found == not READ */
	    flags |= LIBBALSA_MESSAGE_FLAG_NEW;
	if (strchr(header, 'r') != NULL) /* found == REPLIED */
	    flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
	if (strchr(header, 'O') == NULL) /* not found == RECENT */
	    flags |= LIBBALSA_MESSAGE_FLAG_RECENT;
    } else
	    flags |= LIBBALSA_MESSAGE_FLAG_NEW |  LIBBALSA_MESSAGE_FLAG_RECENT;
    header = g_mime_message_get_header (mime_message, "X-Status");
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
    g_mime_message_set_header(message, "Status", new_header->str);
    g_string_truncate(new_header, 0);
    lbm_mbox_x_status_hdr(flags, 3, new_header);
    g_mime_message_set_header(message, "X-Status", new_header->str);
    g_string_free(new_header, TRUE);
}

/*
 * Encode text parts as quoted-printable.
 */
static void
lbm_mbox_prepare_object(GMimeObject * mime_part)
{
    g_mime_object_remove_header(mime_part, "Content-Length");

    if (GMIME_IS_MULTIPART(mime_part)) {
        if (GMIME_IS_MULTIPART_SIGNED(mime_part)
            || GMIME_IS_MULTIPART_ENCRYPTED(mime_part))
            /* Do not break crypto. */
            return;

        g_mime_multipart_foreach((GMimeMultipart *) mime_part,
                                 (GMimePartFunc) lbm_mbox_prepare_object,
                                 NULL);
    } else if (GMIME_IS_MESSAGE_PART(mime_part))
        lbm_mbox_prepare_object(GMIME_OBJECT
                                (((GMimeMessagePart *) mime_part)->
                                 message));
    else if (GMIME_IS_MESSAGE(mime_part))
        lbm_mbox_prepare_object(((GMimeMessage *) mime_part)->mime_part);
    else {
        GMimePartEncodingType encoding;
        const GMimeContentType *mime_type;

        encoding = g_mime_part_get_encoding(GMIME_PART(mime_part));
        if (encoding == GMIME_PART_ENCODING_BASE64)
            return;

        mime_type = g_mime_object_get_content_type(mime_part);
        if (g_mime_content_type_is_type(mime_type, "text", "plain")) {
            const gchar *format =
                g_mime_content_type_get_parameter(mime_type, "format");
            if (format && !g_ascii_strcasecmp(format, "flowed"))
                /* Format=Flowed text cannot contain From_ lines. */
                return;
        }

        g_mime_part_set_encoding(GMIME_PART(mime_part),
                                 GMIME_PART_ENCODING_QUOTEDPRINTABLE);
    }
}

static GMimeObject *
lbm_mbox_armored_object(GMimeStream * stream)
{
    GMimeParser *parser;
    GMimeObject *object;

    parser = g_mime_parser_new_with_stream(stream);
    object = GMIME_OBJECT(g_mime_parser_construct_message(parser));
    g_object_unref(parser);
    lbm_mbox_prepare_object(object);

    return object;
}

static GMimeStream *
lbm_mbox_armored_stream(GMimeStream * stream)
{
    GMimeStream *fstream;
    GMimeFilter *filter;
    
    fstream = g_mime_stream_filter_new_with_stream(stream);

    filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,
				    GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(fstream), filter);
    g_object_unref(filter);

    filter = g_mime_filter_from_new(GMIME_FILTER_FROM_MODE_ARMOR);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(fstream), filter);
    g_object_unref(filter);

    return fstream;
}

/* Called with mailbox locked. */
static int
libbalsa_mailbox_mbox_add_message(LibBalsaMailbox * mailbox,
                                  LibBalsaMessage *message, GError **err)
{
    gchar date_string[27];
    gchar *sender;
    gchar *address;
    gchar *brack;
    gchar *from = NULL;
    const char *path;
    int fd;
    GMimeStream *orig;
    GMimeObject *armored_object;
    GMimeStream *armored_dest;
    GMimeStream *dest;
    gint retval = 1;
    off_t orig_length;

    ctime_r(&(message->headers->date), date_string);

    sender = message->headers->from ?
	internet_address_list_to_string(message->headers->from, FALSE) :
	g_strdup("none");
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
    
    path = libbalsa_mailbox_local_get_path(mailbox);
    /* open in read-write mode */
    fd = open(path, O_RDWR);
    if (fd < 0) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("%s: could not open %s."), "MBOX", path);
        return -1;
    }
    
    orig_length = lseek (fd, 0, SEEK_END);
    lseek (fd, 0, SEEK_SET);
    dest = g_mime_stream_fs_new (fd);
    if (!dest) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("%s: could not get new mime stream."),
                    "MBOX");
	g_free(from);
	return -1;
    }
    if (orig_length > 0 && !lbm_mbox_stream_seek_to_message(dest, 0)) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("%s: %s is not in mbox format."),
                    "MBOX", path);
	g_object_unref(dest);
	g_free(from);
	return -1;
    }
    mbox_lock ( mailbox, dest );

    orig = libbalsa_mailbox_get_message_stream(message->mailbox, message);
    if (!orig) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("%s: could not get message stream."),
                    "MBOX");
	mbox_unlock(mailbox, dest);
	g_object_unref(dest);
	g_free(from);
	return -1;
    }

    /* From_ armor */
    armored_object = lbm_mbox_armored_object(orig);
    g_object_unref(orig);
    /* Make sure we have "Status" and "X-Status" headers, so we can
     * update them in place later, if necessary. */
    update_message_status_headers(GMIME_MESSAGE(armored_object),
                                  message->flags |
                                  LIBBALSA_MESSAGE_FLAG_RECENT);
    armored_dest = lbm_mbox_armored_stream(dest);

    g_mime_stream_seek(dest, 0, GMIME_STREAM_SEEK_END);
    if (g_mime_stream_write_string(dest, from) < (gint) strlen(from)
	|| g_mime_object_write_to_stream(armored_object, armored_dest) < 0) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR, _("Data copy error"));
	retval = -1;
    }
    g_free(from);
    g_object_unref(armored_object);
    g_object_unref(armored_dest);

    if (retval > 0)
	retval = lbm_mbox_newline(dest);

    if(retval<0)
        truncate(path, orig_length);
    mbox_unlock (mailbox, dest);
    g_object_unref(dest);

    return retval;
}

static gboolean
libbalsa_mailbox_mbox_messages_change_flags(LibBalsaMailbox * mailbox,
                                            GArray * msgnos,
                                            LibBalsaMessageFlag set,
                                            LibBalsaMessageFlag clear)
{
    guint i;
    guint changed = 0;

    for (i = 0; i < msgnos->len; i++) {
        guint msgno = g_array_index(msgnos, guint, i);
        struct message_info *msg_info =
            &g_array_index(LIBBALSA_MAILBOX_MBOX(mailbox)->messages_info,
                           struct message_info, msgno - 1);
        LibBalsaMessageFlag old_flags = msg_info->flags;
        gboolean was_unread_undeleted, is_unread_undeleted;

        msg_info->flags |= set;
        msg_info->flags &= ~clear;
        if (!((old_flags ^ msg_info->flags) & LIBBALSA_MESSAGE_FLAGS_REAL))
	    /* No real flags changed. */
            continue;
        ++changed;

        if (msg_info->message)
            msg_info->message->flags =
		msg_info->flags & LIBBALSA_MESSAGE_FLAGS_REAL;

        libbalsa_mailbox_index_set_flags(mailbox, msgno, msg_info->flags);
        libbalsa_mailbox_msgno_changed(mailbox, msgno);

        was_unread_undeleted = (old_flags & LIBBALSA_MESSAGE_FLAG_NEW)
            && !(old_flags & LIBBALSA_MESSAGE_FLAG_DELETED);
        is_unread_undeleted = (msg_info->flags & LIBBALSA_MESSAGE_FLAG_NEW)
            && !(msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED);
        mailbox->unread_messages +=
            is_unread_undeleted - was_unread_undeleted;
    }

    if (changed > 0) {
        libbalsa_mailbox_set_unread_messages_flag(mailbox,
                                                  mailbox->unread_messages
                                                  > 0);
        libbalsa_mailbox_local_queue_sync(LIBBALSA_MAILBOX_LOCAL(mailbox));
    }

    return TRUE;
}

static gboolean
libbalsa_mailbox_mbox_msgno_has_flags(LibBalsaMailbox * mailbox,
                                      guint msgno, LibBalsaMessageFlag set,
                                      LibBalsaMessageFlag unset)
{
    struct message_info *msg_info =
        &g_array_index(LIBBALSA_MAILBOX_MBOX(mailbox)->messages_info,
                       struct message_info, msgno - 1);

    return (msg_info->flags & set) == set
        && (msg_info->flags & unset) == 0;
}

static guint
libbalsa_mailbox_mbox_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMbox *mbox = (LibBalsaMailboxMbox *) mailbox;

    return mbox->messages_info ? mbox->messages_info->len : 0;
}
