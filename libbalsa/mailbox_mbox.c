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
    GMimeMessage *mime_message;
    off_t start;
    off_t end;
    char *from;
    LibBalsaMessageFlag flags;
    LibBalsaMessageFlag orig_flags;
};

static LibBalsaMailboxLocalClass *parent_class = NULL;

static void libbalsa_mailbox_mbox_class_init(LibBalsaMailboxMboxClass *klass);
static void libbalsa_mailbox_mbox_init(LibBalsaMailboxMbox * mailbox);
static void libbalsa_mailbox_mbox_finalize(GObject * object);

static GMimeStream *libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *
							     mailbox,
							     LibBalsaMessage *
							     message);
static void libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox);

static gboolean libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mbox_close_backend(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mbox_sync(LibBalsaMailbox * mailbox);
static LibBalsaMessage *libbalsa_mailbox_mbox_get_message(
						  LibBalsaMailbox * mailbox,
						  guint msgno);
static LibBalsaMessage *libbalsa_mailbox_mbox_load_message(
						  LibBalsaMailbox * mailbox,
						  guint msgno);
static int libbalsa_mailbox_mbox_add_message(LibBalsaMailbox * mailbox,
                                             LibBalsaMessage *message );
static void libbalsa_mailbox_mbox_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear);


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

    object_class->finalize = libbalsa_mailbox_mbox_finalize;

    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_mbox_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_mbox_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_mbox_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_mbox_sync;
    libbalsa_mailbox_class->close_backend = libbalsa_mailbox_mbox_close_backend;
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_mbox_get_message;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_mbox_add_message;
    libbalsa_mailbox_class->change_message_flags =
	libbalsa_mailbox_mbox_change_message_flags;

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

static void
libbalsa_mailbox_mbox_finalize(GObject * object)
{
    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
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

static void
parse_mailbox(LibBalsaMailbox * mailbox)
{
    GMimeParser *gmime_parser;
    GArray *messages_info;
    struct message_info msg_info;
    int new_messages = 0;
    LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

    gmime_parser = 
	g_mime_parser_new_with_stream(mbox->gmime_stream);
    g_mime_parser_set_scan_from(gmime_parser, TRUE);
    messages_info = mbox->messages_info;

    msg_info.message = NULL;
    msg_info.start = g_mime_parser_tell(gmime_parser);
    msg_info.mime_message = g_mime_parser_construct_message(gmime_parser);
    while (!g_mime_parser_eos(gmime_parser)) {
	    msg_info.from = g_mime_parser_get_from(gmime_parser);
	    msg_info.end = g_mime_parser_tell(gmime_parser);
	    g_array_append_val(messages_info, msg_info);
	    new_messages++;

	    msg_info.start = g_mime_parser_tell(gmime_parser);
	    msg_info.mime_message =
		g_mime_parser_construct_message(gmime_parser);
    }
    if (msg_info.mime_message) {
	    msg_info.from = g_mime_parser_get_from(gmime_parser);
	    msg_info.end = g_mime_parser_tell(gmime_parser);
	    g_array_append_val(messages_info, msg_info);
	    new_messages++;
    }
    g_object_unref(G_OBJECT(gmime_parser));

    mailbox->new_messages += new_messages;
}

static void free_messages_info(GArray *messages_info)
{
    struct message_info *msg_info;
    guint i;

    for (i=0; i<messages_info->len; i++) {
	msg_info = &g_array_index(messages_info, struct message_info, i);
	g_free(msg_info->from);
	g_mime_object_unref(GMIME_OBJECT(msg_info->mime_message));
    }
    messages_info->len=0;
}

/* libbalsa_mailbox_local_open:
   THREADING: It should never touch gdk lock.
*/
static gboolean
libbalsa_mailbox_mbox_open(LibBalsaMailbox * mailbox)
{
    struct stat st;
    LibBalsaMailboxMbox *mbox;
    const gchar* path;
    int fd;
    GMimeStream *gmime_stream;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MBOX(mailbox), FALSE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);
    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    path = libbalsa_mailbox_local_get_path(mailbox);

    if (MAILBOX_OPEN(mailbox)) {
	/* increment the reference count */
	mailbox->open_ref++;
	UNLOCK_MAILBOX(mailbox);
	return TRUE;
    }

    if (stat(path, &st) == -1) {
	UNLOCK_MAILBOX(mailbox);
	return FALSE;
    }

    fd = open(path, O_RDONLY);
    if (fd == -1) {
	UNLOCK_MAILBOX(mailbox);
	return FALSE;
    }
    gmime_stream = g_mime_stream_fs_new(fd);

    if (mbox_lock(mailbox, gmime_stream)) {
	g_mime_stream_unref(gmime_stream);
	UNLOCK_MAILBOX(mailbox);
	return FALSE;
    }

    mbox->size = st.st_size;
    mbox->mtime = st.st_mtime;
    mbox->gmime_stream = gmime_stream;

    mbox->messages_info =
	g_array_new(FALSE, FALSE, sizeof(struct message_info));

    if (!mailbox->readonly)
	mailbox->readonly = access (path, W_OK) ? 1 : 0;
    mailbox->messages = 0;
    mailbox->total_messages = 0;
    mailbox->unread_messages = 0;
    if (st.st_size != 0)
	parse_mailbox(mailbox);
    mailbox->open_ref++;
    mbox_unlock(mailbox, NULL);
    UNLOCK_MAILBOX(mailbox);
    libbalsa_mailbox_local_load_messages(mailbox);

    /* We run the filters here also because new could have been put
       in the mailbox with another mechanism than Balsa */
    libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);

    /* increment the reference count */
#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxMbox", mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

static void libbalsa_mailbox_mbox_check(LibBalsaMailbox * mailbox)
{
    g_return_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox));

    if (mailbox->open_ref == 0) {
	if (libbalsa_notify_check_mailbox(mailbox))
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
    } else {
	struct stat st;
	gchar buffer[10];
	const gchar *path;
	LibBalsaMailboxMbox *mbox;

	LOCK_MAILBOX(mailbox);
		
	mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
	path = libbalsa_mailbox_local_get_path(mailbox);
	if (stat(path, &st) == 0)
	{
	    if (st.st_mtime == mbox->mtime && st.st_size == mbox->size) {
		UNLOCK_MAILBOX(mailbox);
		return;
	    }
	    if (st.st_size == mbox->size)
	    {
		mbox->mtime = st.st_mtime;
		UNLOCK_MAILBOX(mailbox);
		return;
	    }
	    if (mbox_lock(mailbox, NULL) != 0) {
		/* we couldn't lock the mailbox, but nothing serious happened:
		 * probably the new mail arrived: no reason to wait till we can
		 * parse it: we'll get it on the next pass
		 */
		UNLOCK_MAILBOX(mailbox);
		return;
	    }

	    /*
	     * Check to make sure that the only change to the mailbox is that 
	     * message(s) were appended to this file.  The heuristic here is
	     * that we should see the message separator at *exactly* what used
	     * to be the end of the folder.
	     */
	    if (g_mime_stream_seek(mbox->gmime_stream, mbox->size,
				   GMIME_STREAM_SEEK_SET) == -1)
		g_message("libbalsa_notify_check_mailbox: "
			  "g_mime_stream_seek() failed\n");
	    if (g_mime_stream_read(mbox->gmime_stream, buffer,
				   sizeof(buffer)) == -1)
	    {
		g_message("libbalsa_notify_check_mailbox: "
			  "g_mime_stream_read() failed\n");
	    } else {
		if (strncmp ("From ", buffer, 5) == 0)
		{
		    if (g_mime_stream_seek(mbox->gmime_stream, mbox->size,
					   GMIME_STREAM_SEEK_SET) == -1)
			g_message("libbalsa_notify_check_mailbox: "
				  "g_mime_stream_seek() failed\n");
		    parse_mailbox(mailbox);
		    mbox->size = g_mime_stream_tell(mbox->gmime_stream);
		    mbox_unlock(mailbox, NULL);
		    UNLOCK_MAILBOX(mailbox);
		    libbalsa_mailbox_local_load_messages(mailbox);
		    libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);
		    return;
		}
	    }
	    free_messages_info(mbox->messages_info);
	    mailbox->messages=0;
	    g_mime_stream_reset(mbox->gmime_stream);
	    parse_mailbox(mailbox);
	    mbox->size = g_mime_stream_tell(mbox->gmime_stream);
	    mbox_unlock(mailbox, NULL);
	    UNLOCK_MAILBOX(mailbox);
	    libbalsa_mailbox_local_load_messages(mailbox);
	    libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);
	    return;
	}
	/* fatal error */
	g_warning("Mailbox was corrupted!");
    }
}

static gboolean libbalsa_mailbox_mbox_close_backend(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMbox *mbox;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox), FALSE);
    
    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

    free_messages_info(mbox->messages_info);
    g_array_free(mbox->messages_info, TRUE);
    mbox->messages_info = NULL;
    g_mime_stream_unref(mbox->gmime_stream);
    mbox->gmime_stream = NULL; // chbm: is this correct ?

    return TRUE;
}

static gboolean libbalsa_mailbox_mbox_sync(LibBalsaMailbox * mailbox)
{
    int fd;
    const gchar *path;
    struct stat st;
    gint messages;
    struct message_info *msg_info;
    off_t offset;
    int first;
    int i;
    int j;
    GMimeStream *temp_stream;
    GMimeStream *gmime_stream;
    GMimeStream *mbox_stream;
    gchar *tempfile;
    GError *error = NULL;
    gchar buffer[5];
    gboolean save_failed;
    GMimeParser *gmime_parser;
    LibBalsaMailboxMbox *mbox;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MBOX(mailbox), FALSE);

    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
    
    /* find the first deleted/changed message. We save a lot of time by only
     * rewriting the mailbox from the point where it has actually changed.
     */
    msg_info = NULL;
    messages = mbox->messages_info->len;
    for (i = 0; i < messages; i++)
    {
	msg_info = &g_array_index(mbox->messages_info,
		       struct message_info, i);
	if (msg_info->flags != msg_info->orig_flags)
	    break;
    }
    if (i == messages) {
	g_message("No modified messages.\n");
	return TRUE;
    }

    /* save the index of the first changed/deleted message */
    first = i; 
    /* where to start overwriting */
    offset = msg_info->start;

    path = libbalsa_mailbox_local_get_path(mailbox);
    /* open in read-write mode */
    fd = open(path, O_RDWR);
    if (fd == -1) {
	return FALSE;
    }
    gmime_stream = g_mime_stream_fs_new(fd);

    /* lock mailbox file */
    if (mbox_lock(mailbox, gmime_stream) != 0) {
	g_mime_stream_unref(gmime_stream);
	return FALSE;
    }

    /* Check to make sure that the file hasn't changed on disk */
    if (stat(path, &st) != 0)
    {
	mbox_unlock(mailbox, gmime_stream);
	g_mime_stream_unref(gmime_stream);
	return FALSE;
    }
    if (st.st_size != mbox->size)
    {
	mbox_unlock(mailbox, gmime_stream);
	g_mime_stream_unref(gmime_stream);
	return FALSE;
    }
    /* Create a temporary file to write the new version of the mailbox in. */
    i = g_file_open_tmp("balsa-tmp-mbox-XXXXXX", &tempfile, &error);
    if (i == -1)
    {
	g_warning("Could not create temporary file: %s", error->message);
	g_error_free (error);
	mbox_unlock(mailbox, gmime_stream);
	g_mime_stream_unref(gmime_stream);
	return FALSE;
    }
    temp_stream = g_mime_stream_fs_new(i);

    mbox_stream = mbox->gmime_stream;
    for (i = first, j = 0; i < messages; i++)
    {
	msg_info = &g_array_index(mbox->messages_info,
		       struct message_info, i);
	if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED) == 0)
	{
	    j++;

	    /*
	     * back up some information which is needed to restore offsets when
	     * something fails.
	     */

	    /* save the new offset for this message.  we add `offset' because
	     * the temporary file only contains saved message which are located
	     * after `offset' in the real mailbox
	     */
	    if (msg_info->flags == msg_info->orig_flags)
	    {
		g_mime_stream_set_bounds(mbox_stream,
			msg_info->start, msg_info->end);
		g_mime_stream_seek(mbox_stream, msg_info->start,
				   GMIME_STREAM_SEEK_SET);
		if (g_mime_stream_write_to_stream(mbox_stream,
						  temp_stream) <= 0)
		{
		    g_mime_stream_unref(temp_stream);
		    g_mime_stream_set_bounds(mbox_stream, 0, -1);
		    unlink(tempfile);
		    g_free(tempfile);
		    mbox_unlock(mailbox, gmime_stream);
		    g_mime_stream_unref(gmime_stream);
		    return FALSE;
		}
	    } else {
		/* write From_ & message */
		if (g_mime_stream_write_string(temp_stream,
					       msg_info->from) == -1
		    || g_mime_stream_write_string(temp_stream, "\n") == -1
		    || g_mime_message_write_to_stream(msg_info->mime_message,
						      temp_stream) == -1
		    || g_mime_stream_write_string(temp_stream, "\n") == -1)
		{
		    g_mime_stream_unref(temp_stream);
		    unlink(tempfile);
		    mbox_unlock(mailbox, gmime_stream);
		    g_mime_stream_unref(gmime_stream);
		    return FALSE;
		}
	    }
	}
    }
    g_mime_stream_set_bounds(mbox_stream, 0, -1);
    if (g_mime_stream_flush(temp_stream) == -1)
    {
	g_warning("can't flush temporary copy\n");
	g_mime_stream_unref(temp_stream);
	unlink(tempfile);
	g_free(tempfile);
	mbox_unlock(mailbox, gmime_stream);
	g_mime_stream_unref(gmime_stream);
	return FALSE;
    }

    save_failed = TRUE;
    if (g_mime_stream_reset(temp_stream) == -1) {
	    g_warning("mbox_sync: can't rewind temporary copy.\n");
    } else
	if (g_mime_stream_seek(gmime_stream, offset, GMIME_STREAM_SEEK_SET) == -1
	    || g_mime_stream_read(gmime_stream, buffer, 5) == -1
	    || strncmp ("From ", buffer, 5) != 0)
	    {
		g_warning("mbox_sync: message not in expected position.\n");
	    } else {
		if (g_mime_stream_seek(gmime_stream, offset,
				       GMIME_STREAM_SEEK_SET) != -1
		    && g_mime_stream_write_to_stream(temp_stream, gmime_stream) != -1)
		    {
			save_failed = FALSE;
			mbox->size = g_mime_stream_tell(gmime_stream);
			ftruncate(GMIME_STREAM_FS(gmime_stream)->fd, 
				  mbox->size);
		    }
	    }
    g_mime_stream_unref(temp_stream);
    mbox_unlock(mailbox, gmime_stream);
    if (g_mime_stream_flush(gmime_stream) == -1 || save_failed)
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
	g_mime_stream_unref(gmime_stream);
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

    /* update the rewritten messages */
    if (g_mime_stream_seek(gmime_stream, offset, GMIME_STREAM_SEEK_SET) == -1)
    {
	g_warning("Can't update message info");
	g_mime_stream_unref(gmime_stream);
	return FALSE;
    }
    gmime_parser = 
	g_mime_parser_new_with_stream(gmime_stream);
    g_mime_parser_set_scan_from(gmime_parser, TRUE);
    for (i = first, j = first; i < messages; i++)
    {
	msg_info = &g_array_index(mbox->messages_info,
				  struct message_info, j);
        g_assert(msg_info->message);
	g_free(msg_info->from);
	g_mime_object_unref(GMIME_OBJECT(msg_info->mime_message));
	if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED) == 0)
	{
	    LibBalsaMessageBody *body;
	    msg_info->message->msgno = j + 1;
	    msg_info->start = g_mime_parser_tell(gmime_parser);
	    msg_info->mime_message =
		g_mime_parser_construct_message(gmime_parser);
	    msg_info->from = g_mime_parser_get_from(gmime_parser);
	    msg_info->end = g_mime_parser_tell(gmime_parser);
	    msg_info->orig_flags = msg_info->flags;
	    msg_info->message->mime_msg = msg_info->mime_message;
	    j++;
	    if (msg_info->message->body_list) {
		/*
		 * FIXME: the mime_part contents can have moved, reinit the
		 * message parts info
		 */
		libbalsa_message_body_free(msg_info->message->body_list);
		msg_info->message->body_list = NULL;
		body = libbalsa_message_body_new(msg_info->message);
		libbalsa_message_body_set_mime_body(body,
					msg_info->message->mime_msg->mime_part);
		libbalsa_message_append_part(msg_info->message, body);
	    }

	} else {
	    g_array_remove_index(mbox->messages_info, j);
	}
    }
    g_object_unref(G_OBJECT(gmime_parser));
    mailbox->messages = j;
    g_mime_stream_unref(gmime_stream);
    return TRUE;
}

static LibBalsaMessage*
libbalsa_mailbox_mbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox), NULL);
    g_return_val_if_fail (msgno > 0, NULL);
    g_return_val_if_fail (msgno <= (unsigned)mailbox->total_messages, NULL);

    msg_info = &g_array_index(LIBBALSA_MAILBOX_MBOX(mailbox)->messages_info,
			      struct message_info, msgno-1);

    if (!msg_info)
	return NULL;

    if (!msg_info->message)
	msg_info->message = 
	    libbalsa_mailbox_mbox_load_message(mailbox, msgno);

    return msg_info->message;
}

static LibBalsaMessage*
libbalsa_mailbox_mbox_load_message(LibBalsaMailbox *mailbox,
                                   guint msgno)
{
    LibBalsaMessage *message;
    struct message_info *msg_info;
    const char *header;
    LibBalsaMailboxMbox *mbox;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox), NULL);
    g_return_val_if_fail (msgno > 0, NULL);

    mbox = LIBBALSA_MAILBOX_MBOX(mailbox);

    msg_info = &g_array_index(mbox->messages_info,
			      struct message_info, msgno - 1);

    mailbox->new_messages--;

    if (!msg_info)
	return NULL;

    mailbox->messages++;

    if (msg_info->mime_message->subject &&
	!strcmp(msg_info->mime_message->subject,
		"DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA"))
	return NULL;

    message = libbalsa_message_new();
    msg_info->message = message;
    message->mime_msg = msg_info->mime_message;

#ifdef MESSAGE_COPY_CONTENT
    header =
	g_mime_message_get_header(msg_info->mime_message, "Content-Length");
    message->length = 0;
    if (header)
	    message->length = atoi(header);

    header = g_mime_message_get_header(msg_info->mime_message, "Lines");
    message->lines_len = 0;
    if (header)
	    message->lines_len = atoi(header);
#endif

    header = g_mime_message_get_header (msg_info->mime_message, "Status");
    if (header) {
	if (strchr(header, 'R') == NULL) /* not found == not READ */
	    message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
	if (strchr(header, 'r') != NULL) /* found == REPLIED */
	    message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
	if (strchr(header, 'O') != NULL) /* found == RECENT */
	    message->flags |= LIBBALSA_MESSAGE_FLAG_RECENT;
    } else
	    message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
    header = g_mime_message_get_header (msg_info->mime_message, "X-Status");
    if (header) {
	if (strchr(header, 'D') != NULL) /* found == DELETED */
	    message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
	if (strchr(header, 'F') != NULL) /* found == FLAGGED */
	    message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
	if (strchr(header, 'A') != NULL) /* found == REPLIED */
	    message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
    }
    msg_info->orig_flags = msg_info->flags = message->flags;

    message->msgno = msgno;
    message->mailbox = mailbox;
    libbalsa_message_set_icons(message);

    return message;
}

static void update_message_status_headers(GMimeMessage *message,
					  LibBalsaMessageFlag flags)
{
    gchar new_header[10];
    int len=0;

    if ((flags & LIBBALSA_MESSAGE_FLAG_NEW) == 0)
	new_header[len++] = 'R';
    if ((flags & LIBBALSA_MESSAGE_FLAG_RECENT) == 0)
	new_header[len++] = 'O';
    if (len) {
	new_header[len++] = '\0';
	g_mime_message_set_header(message, "Status", new_header);
    } else
	g_mime_object_remove_header(GMIME_OBJECT(message), "Status");

    len = 0;
    if ((flags & LIBBALSA_MESSAGE_FLAG_REPLIED) != 0)
	new_header[len++] = 'A';
    if ((flags & LIBBALSA_MESSAGE_FLAG_FLAGGED) != 0)
	new_header[len++] = 'F';
    if (len) {
	new_header[len++] = '\0';
	g_mime_message_set_header(message, "X-Status", new_header);
    } else
	g_mime_object_remove_header(GMIME_OBJECT(message), "X-Status");
}


static int libbalsa_mailbox_mbox_add_message(LibBalsaMailbox * mailbox,
                                             LibBalsaMessage *message )
{
    GMimeMessage *msg;
    char date_string[27];
    const char *sender;
    gchar *address;
    const gchar *brack;
    gchar *from = NULL;
    ssize_t flen;
    const char *path;
    int fd;
    GMimeStream *orig = NULL;
    GMimeStream *dest = NULL;
    GMimeFilter* crlffilter;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MBOX(mailbox), FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
                                                                               
    LOCK_MAILBOX_RETURN_VAL(mailbox, -1);
    g_object_ref ( G_OBJECT(message ) );

    msg = message->mime_msg;

    ctime_r(&(message->headers->date), date_string);

    sender = g_mime_message_get_sender(msg);
    if ( (brack = strrchr( sender, '<' )) ) {
        const gchar * a = strrchr ( brack , '>' );
        if (a)
            address = g_strndup(brack + 1, a - brack - 1);
        else
            address = g_strdup("none");
    } else {
        address = g_strdup(sender);
    }
    from = g_strdup_printf ("From %s %s", address, date_string );
    g_free(address);
    
    path = libbalsa_mailbox_local_get_path(mailbox);
    /* open in read-write mode */
    fd = open(path, O_RDWR);
    if (fd < 0) {
        UNLOCK_MAILBOX(mailbox);
        return -1;
    }
    
    lseek (fd, 0, SEEK_END);
    dest = g_mime_stream_fs_new (fd);
    if (!dest)
        goto AMCLEANUP;
    mbox_lock ( mailbox, dest );

    {
	GMimeStream *tmp = 
	    libbalsa_mailbox_get_message_stream( message->mailbox, message );
	if (!tmp)
	    goto AMCLEANUP;
	
	crlffilter = 
	    g_mime_filter_crlf_new (  GMIME_FILTER_CRLF_DECODE,
				      GMIME_FILTER_CRLF_MODE_CRLF_ONLY );
	orig = g_mime_stream_filter_new_with_stream (tmp);
	g_mime_stream_filter_add ( GMIME_STREAM_FILTER (orig), crlffilter );
	g_object_unref (crlffilter);
	g_mime_stream_unref (tmp);
    }

    flen = strlen (from);
    if ( g_mime_stream_write (dest, from, flen) < flen ) {
        libbalsa_information ( LIBBALSA_INFORMATION_ERROR,
			       _("Error copying message to mailbox %s!\n"
				 "Mailbox might have be corrupted!"),
			       mailbox->name );
        goto AMCLEANUP;
    }
    if (g_mime_stream_write_to_stream (orig, dest) < 0) {
        libbalsa_information ( LIBBALSA_INFORMATION_ERROR,
			       _("Error copying message to mailbox %s!\n"
				 "Mailbox might have be corrupted!"),
			       mailbox->name );
        goto AMCLEANUP;
    }
 
 
 AMCLEANUP:
    g_mime_stream_unref ( GMIME_STREAM(orig) );
    g_object_unref ( G_OBJECT(message ) );  
    mbox_unlock (mailbox, dest);
    g_mime_stream_unref ( GMIME_STREAM(dest) );
    UNLOCK_MAILBOX(mailbox);
    g_free(from);
    return 1;
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
    update_message_status_headers(msg_info->mime_message, msg_info->flags);
}
