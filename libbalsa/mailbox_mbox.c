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

#include <gnome.h>
#include <gmime/gmime-stream-fs.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <string.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
/* for mx_lock_file and mx_unlock_file */
#include "mailbackend.h"

struct message_info {
    LibBalsaMessage *message;
    off_t start;
    off_t status;		/* Offset of the "Status:" header. */
    off_t x_status;		/* Offset of the "X-Status:" header. */
    off_t end;
    char *from;
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

static gboolean libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_mbox_close_mailbox(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mbox_sync(LibBalsaMailbox * mailbox,
                                           gboolean expunge);
static LibBalsaMessage *libbalsa_mailbox_mbox_get_message(
						  LibBalsaMailbox * mailbox,
						  guint msgno);
static void libbalsa_mailbox_mbox_fetch_message_structure(LibBalsaMailbox *
							  mailbox,
							  LibBalsaMessage *
							  message,
							  LibBalsaFetchFlag
							  flags);
static LibBalsaMessage *libbalsa_mailbox_mbox_load_message(LibBalsaMailbox
							   * mailbox,
							   guint msgno);
static int libbalsa_mailbox_mbox_add_message(LibBalsaMailbox * mailbox,
                                             LibBalsaMessage *message );
static void libbalsa_mailbox_mbox_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear);
static guint libbalsa_mailbox_mbox_total_messages(LibBalsaMailbox *
						  mailbox);


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
    libbalsa_mailbox_class->change_message_flags =
	libbalsa_mailbox_mbox_change_message_flags;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_mbox_total_messages;

    libbalsa_mailbox_local_class->remove_files = 
	libbalsa_mailbox_mbox_remove_files;

    libbalsa_mailbox_local_class->load_message =
        libbalsa_mailbox_mbox_load_message;
}



static void
libbalsa_mailbox_mbox_init(LibBalsaMailboxMbox * mailbox)
{
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
    
    libbalsa_notify_register_mailbox(mailbox);
    
    return G_OBJECT(mailbox);
}

static GMimeStream *
libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *mailbox, LibBalsaMessage *message)
{
	GMimeStream *stream = NULL;
	struct message_info *msg_info;
	LibBalsaMailboxMbox *mbox;

	g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox), NULL);
	g_return_val_if_fail (LIBBALSA_IS_MESSAGE(message), NULL);

	mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

	msg_info = &g_array_index(mbox->messages_info,
				  struct message_info, message->msgno - 1);
	if (!msg_info)
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
 * either "Status" or "X-Status".
 * 
 * We'll save only the first of each that we see, to try to avoid
 * headers on embedded messages.  This isn't guaranteed, as the real
 * message may be missing one of the headers, if it was delivered by
 * someone else--we make sure every message has both, with blank flags
 * if necessary, when delivering our own mail.
 */
static void
lbm_mbox_header_cb(GMimeParser * parser, const char *header,
		   const char *value, off_t offset,
		   struct message_info *msg_info)
{
    if (g_ascii_strcasecmp(header, "status") == 0 && msg_info->status < 0)
	msg_info->status = offset;
    else if (g_ascii_strcasecmp(header, "x-status") == 0
	     && msg_info->x_status < 0)
	msg_info->x_status = offset;
}

static LibBalsaMessage *lbm_mbox_message_new(GMimeMessage * mime_message,
					     struct message_info
					     *msg_info);

static void
parse_mailbox(LibBalsaMailboxMbox * mbox)
{
    GMimeParser *gmime_parser;
    struct message_info msg_info;
    GMimeMessage *mime_message;

    gmime_parser = g_mime_parser_new_with_stream(mbox->gmime_stream);
    g_mime_parser_set_scan_from(gmime_parser, TRUE);
    g_mime_parser_set_respect_content_length(gmime_parser, TRUE);
    g_mime_parser_set_header_regex(gmime_parser,
				   "^(X-)?Status",
				   (GMimeParserHeaderRegexFunc)
				   lbm_mbox_header_cb, &msg_info);

    while (!g_mime_stream_eos(mbox->gmime_stream)) {
	msg_info.status = -1;
	msg_info.x_status  = -1;
	mime_message = g_mime_parser_construct_message(gmime_parser);
	msg_info.start = g_mime_parser_get_from_offset(gmime_parser);
	msg_info.end = g_mime_parser_tell(gmime_parser);
	if (msg_info.end > msg_info.start) {
	    msg_info.from = g_mime_parser_get_from(gmime_parser);
	    msg_info.message =
		lbm_mbox_message_new(mime_message, &msg_info);
	    if (msg_info.message) {
		g_array_append_val(mbox->messages_info, msg_info);
		msg_info.message->msgno = mbox->messages_info->len;
		msg_info.message->mailbox = (LibBalsaMailbox *) mbox;
	    }
	}
	g_mime_object_unref(GMIME_OBJECT(mime_message));
    }

    g_object_unref(G_OBJECT(gmime_parser));
}

static void
free_message_info(struct message_info *msg_info)
{
    g_free(msg_info->from);

    if (msg_info->message) {
	msg_info->message->msgno = 0;
	msg_info->message->mailbox = NULL;
	g_object_unref(msg_info->message);
    }
}

static void
free_messages_info(GArray * messages_info)
{
    guint msgno;

    for (msgno = 1; msgno <= messages_info->len; msgno++) {
	struct message_info *msg_info =
	    &g_array_index(messages_info, struct message_info,
			   msgno - 1);
	free_message_info(msg_info);
    }

    messages_info->len = 0;
}

static gboolean
libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    struct stat st;
    const gchar* path;
    int fd;
    GMimeStream *gmime_stream;

    path = libbalsa_mailbox_local_get_path(mailbox);

    if (stat(path, &st) == -1) {
	return FALSE;
    }

    fd = open(path, O_RDWR);
    if (fd == -1) {
	return FALSE;
    }
    gmime_stream = g_mime_stream_fs_new(fd);

    if (mbox_lock(mailbox, gmime_stream)) {
	g_mime_stream_unref(gmime_stream);
	return FALSE;
    }

    mbox->size = st.st_size;
    mbox->mtime = st.st_mtime;
    mbox->gmime_stream = gmime_stream;

    mbox->messages_info =
	g_array_new(FALSE, FALSE, sizeof(struct message_info));

    if (!mailbox->readonly)
	mailbox->readonly = access (path, W_OK) ? 1 : 0;
    mailbox->unread_messages = 0;
    if (st.st_size != 0)
	parse_mailbox(mbox);
    mbox_unlock(mailbox, NULL);
#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxMbox", mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

/* Called with mailbox locked. */
static void
libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox)
{
    struct stat st;
    gchar buffer[10];
    const gchar *path;
    LibBalsaMailboxMbox *mbox;

    g_assert(LIBBALSA_IS_MAILBOX_MBOX(mailbox));

    if (!MAILBOX_OPEN(mailbox)) {
	if (libbalsa_notify_check_mailbox(mailbox))
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
	return;
    }

    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    path = libbalsa_mailbox_local_get_path(mailbox);
    if (stat(path, &st) == 0) {
	if (st.st_mtime == mbox->mtime && st.st_size == mbox->size)
	    return;
	if (st.st_size == mbox->size) {
	    mbox->mtime = st.st_mtime;
	    return;
	}
	if (mbox_lock(mailbox, NULL) != 0)
	    /* we couldn't lock the mailbox, but nothing serious happened:
	     * probably the new mail arrived: no reason to wait till we can
	     * parse it: we'll get it on the next pass
	     */
	    return;

	/*
	 * Check to make sure that the only change to the mailbox is that 
	 * message(s) were appended to this file.  The heuristic here is
	 * that we should see the message separator at *exactly* what used
	 * to be the end of the folder.
	 */
	if (g_mime_stream_seek(mbox->gmime_stream, mbox->size,
			       GMIME_STREAM_SEEK_SET) == -1)
	    g_message("libbalsa_mailbox_mbox_check: "
		      "g_mime_stream_seek() failed\n");
	if (g_mime_stream_read(mbox->gmime_stream, buffer,
			       sizeof(buffer)) == -1) {
	    g_message("libbalsa_mailbox_mbox_check: "
		      "g_mime_stream_read() failed\n");
	} else {
	    if (strncmp("From ", buffer, 5) == 0) {
		guint last_msgno;

		if (g_mime_stream_seek(mbox->gmime_stream, mbox->size,
				       GMIME_STREAM_SEEK_SET) == -1)
		    g_message("libbalsa_mailbox_mbox_check: "
			      "g_mime_stream_seek() failed\n");
		last_msgno = mbox->messages_info->len;
		parse_mailbox(mbox);
		mbox->size = g_mime_stream_tell(mbox->gmime_stream);
		mbox_unlock(mailbox, NULL);
		libbalsa_mailbox_local_load_messages(mailbox, last_msgno);
		libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);
		return;
	    }
	}
	free_messages_info(mbox->messages_info);
	g_mime_stream_reset(mbox->gmime_stream);
	parse_mailbox(mbox);
	mbox->size = g_mime_stream_tell(mbox->gmime_stream);
	mbox_unlock(mailbox, NULL);
	libbalsa_mailbox_local_load_messages(mailbox, 0);
	libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);
	return;
    }
    /* fatal error */
    g_warning("Mailbox was corrupted!");
}

static gboolean lbm_mbox_sync_real(LibBalsaMailbox * mailbox,
				   gboolean expunge,
				   gboolean closing);

static void
libbalsa_mailbox_mbox_close_mailbox(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

    if (mbox->messages_info) {
	lbm_mbox_sync_real(mailbox, TRUE, TRUE);
	free_messages_info(mbox->messages_info);
	g_array_free(mbox->messages_info, TRUE);
	mbox->messages_info = NULL;
    }
    if (mbox->gmime_stream) {
	g_mime_stream_unref(mbox->gmime_stream);
	mbox->gmime_stream = NULL;	// chbm: is this correct ?
    }
    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
	LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox(mailbox);
}

static GMimeMessage *
lbm_mbox_get_mime_message(LibBalsaMailbox * mailbox,
			  LibBalsaMessage * message)
{
    GMimeStream *stream;
    GMimeParser *parser;
    GMimeMessage *mime_message;

    stream = libbalsa_mailbox_mbox_get_message_stream(mailbox, message);
    parser = g_mime_parser_new_with_stream(stream);
    g_mime_stream_unref(stream);

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
 * stream:	mbox stream.
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
    msg_info->orig_flags = msg_info->flags;
    return TRUE;
}

static void lbm_mbox_armor_part(GMimeObject ** part);
static void update_message_status_headers(GMimeMessage *message,
					  LibBalsaMessageFlag flags);
static gboolean
lbm_mbox_sync_real(LibBalsaMailbox * mailbox,
		   gboolean expunge,
		   gboolean closing)
{
    const gchar *path;
    struct stat st;
    gint messages;
    struct message_info *msg_info;
    off_t offset;
    int first;
    int i;
    int j;
    GMimeStream *temp_stream;
    GMimeStream *mbox_stream;
    gchar *tempfile;
    GError *error = NULL;
    gchar buffer[5];
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
    mbox_stream = mbox->gmime_stream;

    path = libbalsa_mailbox_local_get_path(mailbox);

    /* lock mailbox file */
    if (mbox_lock(mailbox, mbox_stream) != 0)
	return FALSE;

    /* Check to make sure that the file hasn't changed on disk */
    if (stat(path, &st) != 0 || st.st_size != mbox->size)
    {
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }

    /* Find where we need to start rewriting the mailbox.  We save a lot
     * of time by only rewriting the mailbox from the point where we
     * really need to.
     */
    messages = mbox->messages_info->len;
    for (i = 0; i < messages; i++)
    {
	msg_info = &g_array_index(mbox->messages_info,
		       struct message_info, i);
	if ((msg_info->orig_flags != msg_info->flags
	     && !lbm_mbox_rewrite_in_place(msg_info, mbox_stream))
	    || (expunge
		&& (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED)))
	    break;
    }
    if (i >= messages) {
	/* g_message("No modified messages.\n"); */
	mbox_unlock(mailbox, mbox_stream);
	return TRUE;
    }

    /* save the index of the first changed/deleted message */
    first = i; 
    /* where to start overwriting */
    offset = msg_info->start;

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
	msg_info = &g_array_index(mbox->messages_info,
				  struct message_info, i);
	if (expunge && (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED))
	    continue;

	if (msg_info->flags == msg_info->orig_flags) {
	    g_mime_stream_set_bounds(mbox_stream, msg_info->start,
				     msg_info->end);
	    if (g_mime_stream_seek(mbox_stream, msg_info->start,
				   GMIME_STREAM_SEEK_SET) < 0
		|| g_mime_stream_write_to_stream(mbox_stream,
						 temp_stream) <= 0) {
		g_mime_stream_unref(temp_stream);
		g_mime_stream_set_bounds(mbox_stream, 0, -1);
		unlink(tempfile);
		g_free(tempfile);
		mbox_unlock(mailbox, mbox_stream);
		return FALSE;
	    }
	} else {
	    GMimeMessage *mime_msg = msg_info->message->mime_msg;

	    if (mime_msg)
		g_mime_object_ref(GMIME_OBJECT(mime_msg));
	    else {
		mime_msg =
		    lbm_mbox_get_mime_message(mailbox, msg_info->message);
	    }
	    update_message_status_headers(mime_msg, msg_info->flags);

	    lbm_mbox_armor_part(&mime_msg->mime_part);
	    /* write From_ & message */
	    if (!msg_info->from
		|| g_mime_stream_write_string(temp_stream,
					      msg_info->from) == -1
		|| g_mime_stream_write_string(temp_stream, "\n") == -1
		|| g_mime_message_write_to_stream(mime_msg,
						  temp_stream) == -1
		|| lbm_mbox_newline(temp_stream) == -1) {
		g_mime_object_unref(GMIME_OBJECT(mime_msg));
		g_mime_stream_unref(temp_stream);
		unlink(tempfile);
		mbox_unlock(mailbox, mbox_stream);
		return FALSE;
	    }
	    g_mime_object_unref(GMIME_OBJECT(mime_msg));
	}
    }

    g_mime_stream_set_bounds(mbox_stream, 0, -1);
    if (g_mime_stream_flush(temp_stream) == -1)
    {
	g_warning("can't flush temporary copy\n");
	g_mime_stream_unref(temp_stream);
	unlink(tempfile);
	g_free(tempfile);
	mbox_unlock(mailbox, mbox_stream);
	return FALSE;
    }

    save_failed = TRUE;
    if (g_mime_stream_reset(temp_stream) == -1) {
	    g_warning("mbox_sync: can't rewind temporary copy.\n");
    } else
	if (g_mime_stream_seek(mbox_stream, offset,
			       GMIME_STREAM_SEEK_SET) == -1
	    || g_mime_stream_read(mbox_stream, buffer, 5) == -1
	    || strncmp ("From ", buffer, 5) != 0)
	    {
		g_warning("mbox_sync: message not in expected position.\n");
	    } else {
		if (g_mime_stream_seek(mbox_stream, offset,
				       GMIME_STREAM_SEEK_SET) != -1
		    && g_mime_stream_write_to_stream(temp_stream,
						     mbox_stream) != -1)
		    {
			save_failed = FALSE;
			mbox->size = g_mime_stream_tell(mbox_stream);
			ftruncate(GMIME_STREAM_FS(mbox_stream)->fd, 
				  mbox->size);
		    }
	    }
    g_mime_stream_unref(temp_stream);
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

    if (closing)
	return TRUE;

    /* update the rewritten messages */
    if (g_mime_stream_seek(mbox_stream, offset, GMIME_STREAM_SEEK_SET)
	== -1) {
	g_warning("Can't update message info");
	return FALSE;
    }
    gmime_parser = g_mime_parser_new_with_stream(mbox_stream);
    g_mime_parser_set_scan_from(gmime_parser, TRUE);
    g_mime_parser_set_respect_content_length(gmime_parser, TRUE);
    for (i = first, j = first; i < messages; i++) {
	msg_info = &g_array_index(mbox->messages_info,
				  struct message_info, j);
	g_assert(msg_info->message);
	if (!expunge ||
	    (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED) == 0) {
	    GMimeMessage *mime_msg;

	    msg_info->message->msgno = j + 1;
	    msg_info->start = g_mime_parser_tell(gmime_parser);
	    mime_msg = g_mime_parser_construct_message(gmime_parser);
	    g_free(msg_info->from);
	    msg_info->from = g_mime_parser_get_from(gmime_parser);
	    msg_info->end = g_mime_parser_tell(gmime_parser);
	    msg_info->orig_flags = msg_info->flags;
	    g_assert(mime_msg != NULL);
	    g_assert(mime_msg->mime_part != NULL);
	    j++;
	    if (!msg_info->message->mime_msg)
		g_mime_object_unref(GMIME_OBJECT(mime_msg));
	    else {
		g_mime_object_unref(GMIME_OBJECT
				    (msg_info->message->mime_msg));
		msg_info->message->mime_msg = mime_msg;
		/*
		 * reinit the message parts info
		 */
		libbalsa_message_body_set_mime_body(msg_info->message->
						    body_list,
						    mime_msg->mime_part);
	    }
	} else {
	    libbalsa_mailbox_msgno_removed(mailbox, j + 1);
	    free_message_info(msg_info);
	    g_array_remove_index(mbox->messages_info, j);
	}
    }
    g_object_unref(G_OBJECT(gmime_parser));

    return TRUE;
}

static gboolean
libbalsa_mailbox_mbox_sync(LibBalsaMailbox * mailbox, gboolean expunge)
{
    g_assert(LIBBALSA_IS_MAILBOX_MBOX(mailbox));

    return lbm_mbox_sync_real(mailbox, expunge, FALSE);
}

static LibBalsaMessage*
libbalsa_mailbox_mbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;

    msg_info = &g_array_index(LIBBALSA_MAILBOX_MBOX(mailbox)->messages_info,
			      struct message_info, msgno-1);

    return msg_info->message;
}

static void
libbalsa_mailbox_mbox_fetch_message_structure(LibBalsaMailbox * mailbox,
					      LibBalsaMessage * message,
					      LibBalsaFetchFlag flags)
{
    if (!message->mime_msg)
	message->mime_msg = lbm_mbox_get_mime_message(mailbox, message);

    LIBBALSA_MAILBOX_CLASS(parent_class)->fetch_message_structure(mailbox,
								  message,
								  flags);
}

static LibBalsaMessage*
libbalsa_mailbox_mbox_load_message(LibBalsaMailbox *mailbox, guint msgno)
{
    LibBalsaMailboxMbox *mbox;

    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

    return g_array_index(mbox->messages_info,
			      struct message_info, msgno - 1).message;
}

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

#ifdef MESSAGE_COPY_CONTENT
    header =
	g_mime_message_get_header(mime_message, "Content-Length");
    message->length = 0;
    if (header)
	message->length = atoi(header);

    header = g_mime_message_get_header(mime_message, "Lines");
    message->lines_len = 0;
    if (header)
	message->lines_len = atoi(header);
#endif

    header = g_mime_message_get_header (mime_message, "Status");
    if (header) {
	if (strchr(header, 'R') == NULL) /* not found == not READ */
	    flags |= LIBBALSA_MESSAGE_FLAG_NEW;
	if (strchr(header, 'r') != NULL) /* found == REPLIED */
	    flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
	if (strchr(header, 'O') == NULL) /* not found == RECENT */
	    flags |= LIBBALSA_MESSAGE_FLAG_RECENT;
    } else
	    flags |= LIBBALSA_MESSAGE_FLAG_NEW;
    header = g_mime_message_get_header (mime_message, "X-Status");
    if (header) {
	if (strchr(header, 'D') != NULL) /* found == DELETED */
	    flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
	if (strchr(header, 'F') != NULL) /* found == FLAGGED */
	    flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
	if (strchr(header, 'A') != NULL) /* found == REPLIED */
	    flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
    }

    message->flags = msg_info->flags = msg_info->orig_flags = flags;
    libbalsa_message_headers_update(message, mime_message);

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

/* Encode text parts as quoted-printable, and armor any "From " lines.
 */
static void
lbm_mbox_armor_part(GMimeObject ** part)
{
    const GMimeContentType *mime_type;
    GMimePart *mime_part;
    GMimeStream *mem;
    GMimeStream *fstream;
    GMimeFilter *filter;
    GMimeParser *parser;

    /* Content length will change, and we no longer need the header. */
    g_mime_object_remove_header(GMIME_OBJECT(*part), "Content-Length");

    while (GMIME_IS_MESSAGE_PART(*part)) {
	GMimeMessage *message =
	    g_mime_message_part_get_message(GMIME_MESSAGE_PART(*part));
	part = &message->mime_part;
	g_mime_object_unref(GMIME_OBJECT(message));
	g_mime_object_remove_header(GMIME_OBJECT(*part), "Content-Length");
    }

    if (GMIME_IS_MULTIPART(*part)) {
	const GMimeContentType *content_type =
	    g_mime_object_get_content_type(*part);
	GList *subpart;

	if (g_mime_content_type_is_type(content_type, "multipart", "signed"))
	    /* Don't change the coding of its parts. */
	    return;

	for (subpart = GMIME_MULTIPART(*part)->subparts; subpart;
	     subpart = subpart->next)
	    lbm_mbox_armor_part((GMimeObject **) &subpart->data);

	return;
    }

    if (!GMIME_IS_PART(*part))
	return;

    mime_part = GMIME_PART(*part);
    mime_type = g_mime_part_get_content_type(mime_part);
    if (!g_mime_content_type_is_type(mime_type, "text", "*"))
	return;

    g_mime_part_set_encoding(mime_part, GMIME_PART_ENCODING_QUOTEDPRINTABLE);

    mem = g_mime_stream_mem_new();
    g_mime_part_write_to_stream(mime_part, mem);
    g_mime_object_unref(GMIME_OBJECT(mime_part));

    g_mime_stream_reset(mem);
    fstream = g_mime_stream_filter_new_with_stream(mem);
    g_mime_stream_unref(mem);

    filter = g_mime_filter_from_new(GMIME_FILTER_FROM_MODE_ARMOR);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(fstream), filter);
    g_object_unref(filter);

    parser = g_mime_parser_new_with_stream(fstream);
    g_mime_stream_unref(fstream);

    *part = g_mime_parser_construct_part(parser);
    g_object_unref(parser);
}

static GMimeStream *
lbm_mbox_armor_stream(GMimeStream * stream, LibBalsaMessageFlag flags)
{
    GMimeStream *fstream;
    GMimeFilter *filter;
    GMimeParser *parser;
    GMimeMessage *message;
    GMimeStream *mem;

    /* CRLF filter before parsing the message. */
    fstream = g_mime_stream_filter_new_with_stream(stream);
    filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,
				    GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(fstream), filter);
    g_object_unref(filter);

    parser = g_mime_parser_new_with_stream(fstream);
    g_mime_stream_unref(fstream);

    message = g_mime_parser_construct_message(parser);
    g_object_unref(parser);

    lbm_mbox_armor_part(&message->mime_part);

    mem = g_mime_stream_mem_new();
    /* Make sure we have "Status" and "X-Status" headers, so we can
     * update them in place later, if necessary. */
    update_message_status_headers(message, flags);
    g_mime_message_write_to_stream(message, mem);
    g_mime_object_unref(GMIME_OBJECT(message));

    g_mime_stream_reset(mem);
    return mem;
}

/* Called with mailbox locked. */
static int libbalsa_mailbox_mbox_add_message(LibBalsaMailbox * mailbox,
                                             LibBalsaMessage *message )
{
    gchar date_string[27];
    gchar *sender;
    gchar *address;
    gchar *brack;
    gchar *from = NULL;
    const char *path;
    int fd;
    GMimeStream *orig;
    GMimeStream *dest;
    GMimeStream *tmp;
    gint retval = 1;

    ctime_r(&(message->headers->date), date_string);

    sender = message->headers->from ?
	libbalsa_address_to_gchar(message->headers->from, 0) :
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
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
			     _("%s: could not open %s."), "MBOX", path);
        return -1;
    }
    
    lseek (fd, 0, SEEK_END);
    dest = g_mime_stream_fs_new (fd);
    if (!dest) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
			     _("%s: could not get new mime stream."),
			     "MBOX");
	g_free(from);
	return -1;
    }
    mbox_lock ( mailbox, dest );

    tmp = libbalsa_mailbox_get_message_stream( message->mailbox, message );
    if (!tmp) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
			     _("%s: could not get message stream."),
			     "MBOX");
	mbox_unlock(mailbox, dest);
	g_mime_stream_unref(dest);
	g_free(from);
	return -1;
    }

    orig = lbm_mbox_armor_stream(tmp, message->flags);
    g_mime_stream_unref(tmp);

    if (g_mime_stream_write_string(dest, from) < (gint) strlen(from)
	|| g_mime_stream_write_to_stream (orig, dest) < 0) {
        libbalsa_information ( LIBBALSA_INFORMATION_ERROR,
			       _("Error copying message to mailbox %s!\n"
				 "Mailbox might have been corrupted!"),
			       mailbox->name );
	retval = -1;
    }

    if (retval > 0)
	retval = lbm_mbox_newline(dest);
 
    g_mime_stream_unref(orig);
    mbox_unlock (mailbox, dest);
    g_mime_stream_unref(dest);
    g_free(from);

    return retval;
}


static void
libbalsa_mailbox_mbox_change_message_flags(LibBalsaMailbox * mailbox,
					   guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    struct message_info *msg_info;

    g_return_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox));
    g_return_if_fail (msgno > 0);

    msg_info = &g_array_index(LIBBALSA_MAILBOX_MBOX(mailbox)->messages_info,
			      struct message_info, msgno - 1);

    msg_info->flags |= set;
    msg_info->flags &= ~clear;

    libbalsa_mailbox_local_queue_sync(LIBBALSA_MAILBOX_LOCAL(mailbox));
}

static guint
libbalsa_mailbox_mbox_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMbox *mbox = (LibBalsaMailboxMbox *) mailbox;

    return mbox->messages_info ? mbox->messages_info->len : 0;
}
