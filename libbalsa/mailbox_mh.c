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

#define _XOPEN_SOURCE          500
#define _XOPEN_SOURCE_EXTENDED 1
#define _POSIX_SOURCE          1
#include <libgnome/gnome-i18n.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <string.h>

#include "libbalsa.h"
#include "misc.h"
#include "libbalsa_private.h"

struct message_info {
    GMimeMessage *mime_message;
    char *filename;
    LibBalsaMessageFlag flags;
    LibBalsaMessageFlag orig_flags;
    LibBalsaMessage *message;
};

static LibBalsaMailboxLocalClass *parent_class = NULL;

static void libbalsa_mailbox_mh_class_init(LibBalsaMailboxMhClass *klass);
static void libbalsa_mailbox_mh_init(LibBalsaMailboxMh * mailbox);
static void libbalsa_mailbox_mh_finalize(GObject * object);

static GMimeStream *libbalsa_mailbox_mh_get_message_stream(LibBalsaMailbox *
							   mailbox,
							   LibBalsaMessage *
							   message);
static void libbalsa_mailbox_mh_remove_files(LibBalsaMailboxLocal *mailbox);

static gboolean libbalsa_mailbox_mh_open(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_mh_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mh_close_backend(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mh_sync(LibBalsaMailbox * mailbox,
                                         gboolean expunge);
static struct message_info *message_info_from_msgno(
						  LibBalsaMailboxMh * mailbox,
						  guint msgno);
static LibBalsaMessage *libbalsa_mailbox_mh_get_message(LibBalsaMailbox * mailbox,
						     guint msgno);
static void libbalsa_mailbox_mh_fetch_message_structure(LibBalsaMailbox *
							mailbox,
							LibBalsaMessage *
							message,
							LibBalsaFetchFlag
							flags);
static void libbalsa_mailbox_mh_release_message(LibBalsaMailbox * mailbox,
						LibBalsaMessage * message);
static LibBalsaMessage *libbalsa_mailbox_mh_load_message(
				    LibBalsaMailbox * mailbox, guint msgno);
static int libbalsa_mailbox_mh_add_message(LibBalsaMailbox * mailbox,
					   LibBalsaMessage * message );
static void libbalsa_mailbox_mh_change_message_flags(LibBalsaMailbox * mailbox,
						     guint msgno,
						     LibBalsaMessageFlag set,
						     LibBalsaMessageFlag clear);

static void free_messages_info(GArray *messages_info);

GType
libbalsa_mailbox_mh_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxMhClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_mh_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxMh),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_mh_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX_LOCAL,
	                           "LibBalsaMailboxMh",
                                   &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_mh_class_init(LibBalsaMailboxMhClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_mailbox_mh_finalize;

    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_mh_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_mh_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_mh_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_mh_sync;
    libbalsa_mailbox_class->close_backend = libbalsa_mailbox_mh_close_backend;
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_mh_get_message;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_mh_fetch_message_structure;
    libbalsa_mailbox_class->release_message =
	libbalsa_mailbox_mh_release_message;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_mh_add_message;
    libbalsa_mailbox_class->change_message_flags =
	libbalsa_mailbox_mh_change_message_flags;

    libbalsa_mailbox_local_class->load_message =
        libbalsa_mailbox_mh_load_message;
    libbalsa_mailbox_local_class->remove_files = 
	libbalsa_mailbox_mh_remove_files;
}

static void
libbalsa_mailbox_mh_init(LibBalsaMailboxMh * mailbox)
{
}

gint
libbalsa_mailbox_mh_create(const gchar * path, gboolean create) 
{
    GType magic_type;
    gint exists;

    g_return_val_if_fail( path != NULL, -1);
	
    exists = access(path, F_OK);
    if ( exists == 0 ) {
	/* File exists. Check if it is a mh... */
	
	magic_type = libbalsa_mailbox_type_from_path(path);
	if ( magic_type != LIBBALSA_TYPE_MAILBOX_MH ) {
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				 _("Mailbox %s does not appear to be a Mh mailbox."), path);
	    return(-1);
	}
    } else {
	if(create) {
	    char tmp[_POSIX_PATH_MAX];
	    int i;

	    if (mkdir (path, S_IRWXU)) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				     _("Could not create MH directory at %s (%s)"), path, strerror(errno) );
		return (-1);
	    } 
	    snprintf (tmp, sizeof (tmp), "%s/.mh_sequences", path);
	    if ((i = creat (tmp, S_IRWXU)) == -1)
		{
		    libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
					 _("Could not create MH structure at %s (%s)"), path, strerror(errno) );
		    rmdir (path);
		    return (-1);
		}	    	    
	} else 
	    return(-1);
    }
    return(0);
}
    

GObject *
libbalsa_mailbox_mh_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;

    
    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MH, NULL);
    
    mailbox->is_directory = TRUE;
    
    mailbox->url = g_strconcat("file://", path, NULL);
    
    if(libbalsa_mailbox_mh_create(path, create) < 0) {
	g_object_unref(G_OBJECT(mailbox));
	return NULL;
    }
    
    LIBBALSA_MAILBOX_MH(mailbox)->messages_info =
	g_array_new(FALSE, TRUE, sizeof(struct message_info));

    libbalsa_notify_register_mailbox(mailbox);
    
    return G_OBJECT(mailbox);
}

static void
libbalsa_mailbox_mh_finalize(GObject * object)
{
    free_messages_info(LIBBALSA_MAILBOX_MH(object)->messages_info);
    g_array_free(LIBBALSA_MAILBOX_MH(object)->messages_info, TRUE);

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GMimeStream *
libbalsa_mailbox_mh_get_message_stream(LibBalsaMailbox *mailbox, 
                                       LibBalsaMessage *message)
{
    GMimeStream *stream = NULL;
    struct message_info *msg_info;
    gchar *filename;
    const gchar *path;
    int fd;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MH(mailbox), NULL);
    g_return_val_if_fail (MAILBOX_OPEN(mailbox), NULL);
    g_return_val_if_fail (LIBBALSA_IS_MESSAGE(message), NULL);

    msg_info = message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox),
				       message->msgno);
    if (!msg_info)
	return NULL;

    path = libbalsa_mailbox_local_get_path(mailbox);
    filename = g_build_filename(path, msg_info->filename, NULL);

    fd = open(filename, O_RDONLY);
    if (fd == -1)
	return NULL;

    stream = g_mime_stream_fs_new(fd);
    if (!stream) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
			     _("Open of %s failed. Errno = %d, "),
			     filename, errno);
	g_free(filename);
	return NULL;
    }
    g_free(filename);
    return stream;
}

static void
libbalsa_mailbox_mh_remove_files(LibBalsaMailboxLocal *mailbox)
{
    const gchar* path;
    g_return_if_fail(LIBBALSA_IS_MAILBOX_MH(mailbox));
    path = libbalsa_mailbox_local_get_path(mailbox);
    g_print("DELETE MH\n");

    if (!libbalsa_delete_directory_contents(path)) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Could not remove contents of %s:\n%s"),
			     path, strerror(errno));
	return;
    }
    if ( rmdir(path) == -1 ) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Could not remove %s:\n%s"),
			     path, strerror(errno));
    }
}

/* Ignore the garbage files.  A valid MH message consists of only
 * digits.  Deleted message get moved to a filename with a comma before
 * it.
 */
static gboolean check_filename (const gchar *s)
{
  for (; *s; s++)
  {
    if (!g_ascii_isdigit (*s))
      return FALSE;
  }
  return TRUE;
}

static void parse_mailbox(LibBalsaMailboxMh * mailbox)
{
    const gchar *path;
    GDir *dir;
    const gchar *filename;
    guint msgno;
    int new_messages = 0;

    path = libbalsa_mailbox_local_get_path(mailbox);

    if ((dir = g_dir_open(path, 0, NULL)) == NULL)
	return;

    while ((filename = g_dir_read_name(dir)) != NULL)
    {
	struct message_info *msg_info;
	if (check_filename(filename) == FALSE)
	    continue;
	sscanf(filename, "%d", &msgno);
	if (msgno > mailbox->messages_info->len)
	    g_array_set_size(mailbox->messages_info, msgno);
	msg_info = &g_array_index(mailbox->messages_info,
				  struct message_info, msgno - 1);
	if (msg_info->filename == NULL) {
	    msg_info->filename=g_strdup(filename);
	    new_messages++;
	}
    }
    g_dir_close(dir);

    LIBBALSA_MAILBOX(mailbox)->new_messages += new_messages;
}

static const gchar *LibBalsaMailboxMhUnseen = "unseen:";
static const gchar *LibBalsaMailboxMhFlagged = "flagged:";
static const gchar *LibBalsaMailboxMhReplied = "replied:";
static void handle_seq_line(LibBalsaMailboxMh * mailbox, gchar *line)
{
    LibBalsaMessageFlag flag;
    gchar **sequences, **seq;
    GArray *messages_info;

    if (g_str_has_prefix(line, LibBalsaMailboxMhUnseen))
	flag = LIBBALSA_MESSAGE_FLAG_NEW;
    else if (g_str_has_prefix(line, LibBalsaMailboxMhFlagged))
	flag = LIBBALSA_MESSAGE_FLAG_FLAGGED;
    else if (g_str_has_prefix(line, LibBalsaMailboxMhReplied))
	flag = LIBBALSA_MESSAGE_FLAG_REPLIED;
    else	/* unknown sequence */
	return;

    line = strchr(line, ':') + 1;
    sequences = g_strsplit(line, " ", 0);

    messages_info=mailbox->messages_info;
    for (seq = sequences; *seq; seq++)
    {
	guint nr;
	struct message_info *msg_info;
	if (!**seq)
	    continue;
	line = strchr(*seq, '-');
	if (line) {
	    guint end;
	    *line++='\0';
            sscanf(line,"%d", &end);
	    if (end > messages_info->len)
		g_array_set_size(messages_info, end);
	    for (sscanf(*seq,"%d", &nr); nr <= end; nr++)
	    {
		if (!nr)
		    continue;
		msg_info = &g_array_index(messages_info,
					  struct message_info, nr - 1);
		msg_info->orig_flags |= flag;
	    }
	} else {
	    sscanf(*seq, "%d", &nr);
	    if (!nr)
		continue;
	    if (nr > messages_info->len)
		g_array_set_size(messages_info, nr);
	    msg_info = &g_array_index(messages_info,
				      struct message_info, nr - 1);
	    msg_info->orig_flags |= flag;
	}
    }
    g_strfreev(sequences);
}

static void read_mh_sequences(LibBalsaMailboxMh * mailbox)
{
    struct stat st;
    gchar* sequences_filename;
    int fd;
    GMimeStream *gmime_stream;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;

    sequences_filename = mailbox->sequences_filename;
    if (stat(sequences_filename, &st) == -1) {
	return;
    }
    mailbox->mtime_sequences = st.st_mtime;

    fd = open(sequences_filename, O_RDONLY);
    gmime_stream = g_mime_stream_fs_new(fd);
    gmime_stream_buffer = g_mime_stream_buffer_new(gmime_stream,
					GMIME_STREAM_BUFFER_BLOCK_READ);
    g_mime_stream_unref(gmime_stream);
    line = g_byte_array_new();
    do {
	g_mime_stream_buffer_readln(gmime_stream_buffer, line);
	g_byte_array_append(line, "", 1);

	handle_seq_line(mailbox, line->data);
	line->len = 0;
    } while (!g_mime_stream_eos(gmime_stream_buffer));
    g_mime_stream_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);
}

static gboolean
libbalsa_mailbox_mh_open(LibBalsaMailbox * mailbox)
{
    struct stat st;
    LibBalsaMailboxMh *mh;
    const gchar* path;
   
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MH(mailbox), FALSE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);
    mh = LIBBALSA_MAILBOX_MH(mailbox);
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

    mh->mtime = st.st_mtime;
    mh->sequences_filename =
	g_strdup_printf("%s/.mh_sequences", path);

    mh->msgno_2_index = g_array_new(FALSE, FALSE, sizeof(int));
    mh->last_msgno = 0;
    mh->last_index = 0;

    if (!mailbox->readonly)
	mailbox->readonly = access (path, W_OK) ? 1 : 0;
    mailbox->messages = 0;
    mailbox->total_messages = 0;
    mailbox->unread_messages = 0;
    read_mh_sequences(mh);
    parse_mailbox(mh);
    mailbox->new_messages = mh->messages_info->len;
    mailbox->open_ref++;
    UNLOCK_MAILBOX(mailbox);
    libbalsa_mailbox_local_load_messages(mailbox);

    /* We run the filters here also because new could have been put
       in the mailbox with another mechanism than Balsa */
    libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);

    /* increment the reference count */
#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxMh", mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

static int libbalsa_mailbox_mh_open_temp (const gchar *dest_path,
					  char **name_used);
static void libbalsa_mailbox_mh_check(LibBalsaMailbox * mailbox)
{
    if (mailbox->open_ref == 0) {
	if (libbalsa_notify_check_mailbox(mailbox))
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
    } else {
	struct stat st, st_sequences;
	LibBalsaMailboxMh *mh;
	const gchar *path;
	int modified = 0;

	g_return_if_fail (LIBBALSA_IS_MAILBOX_MH(mailbox));
	mh = LIBBALSA_MAILBOX_MH(mailbox);

	LOCK_MAILBOX(mailbox);

	path = libbalsa_mailbox_local_get_path(mailbox);
	if (stat(path, &st) == -1)
	{
	    UNLOCK_MAILBOX(mailbox);
	    return;
	}

	/* create .mh_sequences when there isn't one. */
	if (stat(mh->sequences_filename, &st_sequences) == -1)
	{
	    if (errno == ENOENT)
	    {
		gchar *tmp;
		int fd = libbalsa_mailbox_mh_open_temp(path, &tmp);
		if (fd != -1)
		{
		    close(fd);
		    if (libbalsa_safe_rename(tmp,
					     mh->sequences_filename) == -1)
			unlink (tmp);
		    g_free(tmp);
		}
		if (stat (mh->sequences_filename, &st_sequences) == -1)
		    modified = 1;
	    }
	    else
		modified = 1;
	}

	if (st.st_mtime != mh->mtime)
	    modified = 1;

	if (st_sequences.st_mtime != mh->mtime_sequences)
	    modified = 1;

	if (!modified) {
	    UNLOCK_MAILBOX(mailbox);
	    return;
	}

	mh->mtime = st.st_mtime;
	mh->mtime_sequences = st_sequences.st_mtime;

	read_mh_sequences(mh);
	parse_mailbox(mh);

	UNLOCK_MAILBOX(mailbox);
	libbalsa_mailbox_local_load_messages(mailbox);
	libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);
    }
}

static void free_messages_info(GArray *messages_info)
{
    struct message_info *msg_info;
    guint i;

    if(!messages_info)
	return;

    for (i=0; i<messages_info->len; i++) {
	msg_info = &g_array_index(messages_info, struct message_info, i);
	g_free(msg_info->filename);
	if (msg_info->mime_message)
	    g_object_remove_weak_pointer(G_OBJECT(msg_info->mime_message),
					 (gpointer) &msg_info->mime_message);
	if (msg_info->message)
	    g_object_unref(msg_info->message);
    }
    messages_info->len=0;
}

static gboolean libbalsa_mailbox_mh_close_backend(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MH(mailbox), FALSE);
    g_array_free(LIBBALSA_MAILBOX_MH(mailbox)->msgno_2_index, TRUE);
    LIBBALSA_MAILBOX_MH(mailbox)->msgno_2_index = NULL;
    free_messages_info(LIBBALSA_MAILBOX_MH(mailbox)->messages_info);

    return TRUE;
}

static int libbalsa_mailbox_mh_open_temp (const gchar *dest_path,
					  char **name_used)
{
    int fd;
    static int counter;
    gchar *filename;

    do {
	filename = g_strdup_printf("%s/%s-%d-%d", dest_path,
	   		      g_get_user_name(), getpid (), counter++);
	fd = open (filename, O_WRONLY | O_EXCL | O_CREAT, 0600);
	if (fd == -1)
	{
	    g_free(filename);
	    if (errno != EEXIST)
		return -1;
	}
    } while (fd == -1);
    *name_used = filename;
    return fd;
}

static gboolean libbalsa_mailbox_mh_sync(LibBalsaMailbox * mailbox,
                                         gboolean expunge)
{
    LibBalsaMailboxMh *mh;
    gint first_unseen, last_unseen;
    gint first_flagged, last_flagged;
    gint first_replied, last_replied;
    GMimeStream *unseen_line, *flagged_line, *replied_line;
    const gchar *path;
    gchar *tmp;
    gint msg_count;
    gint i;
    struct message_info *msg_info;

    int fd;
    GMimeStream *temp_stream;
    gchar* sequences_filename;
    GMimeStream *gmime_stream;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MH(mailbox), FALSE);
    mh = LIBBALSA_MAILBOX_MH(mailbox);
    g_return_val_if_fail( mh->messages_info != NULL, FALSE );

    /* build new sequence file lines */
    first_unseen = last_unseen = -1;
    first_flagged = last_flagged = -1;
    first_replied = last_replied = -1;
    unseen_line = g_mime_stream_mem_new();
    flagged_line = g_mime_stream_mem_new();
    replied_line = g_mime_stream_mem_new();
    path = libbalsa_mailbox_local_get_path(mailbox);
    msg_count = mh->messages_info->len;
    for (i = 0; i < msg_count; i++)
    {
	msg_info = &g_array_index(mh->messages_info,
		       struct message_info, i);
	if (!msg_info)
	    continue;

	if (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED) {
	    if (!msg_info->filename || msg_info->filename[0] == ',')
		continue;
	    /* MH just moves files out of the way when you delete them */
	    /* chbm: not quite, however this is probably a good move for
	       flag deleted */
 	    char *orig = g_strdup_printf("%s/%s", path, msg_info->filename);
	    unlink(orig);
	    g_free(orig);
	    /* free old information */
	    g_free(msg_info->filename);
	    if (msg_info->mime_message)
		g_object_remove_weak_pointer(G_OBJECT(msg_info->mime_message),
					     (gpointer) 
					     &msg_info->mime_message);
	    msg_info->filename = NULL;
	    msg_info->mime_message = NULL;
	    libbalsa_mailbox_msgno_removed(msg_info->message->mailbox,
					   msg_info->message->msgno);
	    continue;
	}

	if (msg_info->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
	    /* first */
	    if (last_unseen == -1)
		first_unseen = last_unseen = i + 1;
	    else
	    /* middle of interval, maybe last */
	    if (last_unseen == i)
		last_unseen = i + 1;
	    else {
		if (first_unseen != last_unseen) 
		    /* interval */
		    g_mime_stream_printf(unseen_line, " %d-%d",
					 first_unseen, last_unseen);
		else
		    /* single message */
		    g_mime_stream_printf(unseen_line, " %d", last_unseen);
		first_unseen = last_unseen = i + 1;
	    }
	}

	if (msg_info->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED) {
	    /* first */
	    if (last_flagged == -1)
		first_flagged = last_flagged = i + 1;
	    else
	    /* middle of interval, maybe last */
	    if (last_flagged == i)
		last_flagged = i + 1;
	    else {
		if (first_flagged != last_flagged) 
		    /* interval */
		    g_mime_stream_printf(flagged_line, " %d-%d",
					 first_flagged, last_flagged);
		else
		    /* single message */
		    g_mime_stream_printf(flagged_line, " %d", last_flagged);
		first_flagged = last_flagged = i + 1;
	    }
	}

	if (msg_info->flags & LIBBALSA_MESSAGE_FLAG_REPLIED) {
	    /* first */
	    if (last_replied == -1)
		first_replied = last_replied = i + 1;
	    else
	    /* middle of interval, maybe last */
	    if (last_replied == i)
		last_replied = i + 1;
	    else {
		if (first_replied != last_replied) 
		    /* interval */
		    g_mime_stream_printf(replied_line, " %d-%d",
					 first_replied, last_replied);
		else
		    /* single message */
		    g_mime_stream_printf(replied_line, " %d", last_replied);
		first_replied = last_replied = i + 1;
	    }
	}
    }
    /* NEW */
    if (last_unseen != -1)
    {
	if (first_unseen != last_unseen) 
	    /* interval */
	    g_mime_stream_printf(unseen_line, " %d-%d",
				 first_unseen, last_unseen);
	else
	    /* single message */
	    g_mime_stream_printf(unseen_line, " %d", last_unseen);
    }

    /* FLAGGED */
    if (last_flagged != -1)
    {
	if (first_flagged != last_flagged) 
	    /* interval */
	    g_mime_stream_printf(flagged_line, " %d-%d",
				 first_flagged, last_flagged);
	else
	    /* single message */
	    g_mime_stream_printf(flagged_line, " %d", last_flagged);
    }

    /* REPLIED */
    if (last_replied != -1)
    {
	if (first_replied != last_replied) 
	    /* interval */
	    g_mime_stream_printf(replied_line, " %d-%d",
				 first_replied, last_replied);
	else
	    /* single message */
	    g_mime_stream_printf(replied_line, " %d", last_replied);
    }


    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(mailbox);
    fd = libbalsa_mailbox_mh_open_temp(path, &tmp);
    if (fd == -1)
    {
	g_mime_stream_unref(unseen_line);
	g_mime_stream_unref(flagged_line);
	g_mime_stream_unref(replied_line);
	return FALSE;
    }
    temp_stream = g_mime_stream_fs_new(fd);

    /* copy unknown sequences */
    sequences_filename = mh->sequences_filename;
    fd = open(sequences_filename, O_RDONLY);
    gmime_stream = g_mime_stream_fs_new(fd);
    gmime_stream_buffer = g_mime_stream_buffer_new(gmime_stream,
					GMIME_STREAM_BUFFER_BLOCK_READ);
    g_mime_stream_unref(gmime_stream);
    line = g_byte_array_new();
    do {
	line->len = 0;
	g_mime_stream_buffer_readln(gmime_stream_buffer, line);
	if (line->data &&
	    !g_str_has_prefix(line->data, LibBalsaMailboxMhUnseen) &&
	    !g_str_has_prefix(line->data, LibBalsaMailboxMhFlagged) &&
	    !g_str_has_prefix(line->data, LibBalsaMailboxMhReplied))
	{
	    /* unknown sequence */
	    g_mime_stream_write(temp_stream, line->data, line->len);
	}
    } while (!g_mime_stream_eos(gmime_stream_buffer));
    g_mime_stream_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);

    /* write unseen, flagged and replied sequences */
    if (g_mime_stream_length(unseen_line) != 0)
    {
	if (g_mime_stream_write_string(temp_stream,
				       LibBalsaMailboxMhUnseen) == -1
	    || g_mime_stream_reset(unseen_line) == -1
	    || g_mime_stream_write_to_stream(unseen_line, temp_stream) == -1
	    || g_mime_stream_write_string(temp_stream, "\n") == -1)
	{
	    g_mime_stream_unref(temp_stream);
	    unlink (tmp);
	    g_free(tmp);
	    g_mime_stream_unref(unseen_line);
	    g_mime_stream_unref(flagged_line);
	    g_mime_stream_unref(replied_line);
	    return FALSE;
	}
    }
    if (g_mime_stream_length(flagged_line) != 0)
    {
	if (g_mime_stream_write_string(temp_stream,
				       LibBalsaMailboxMhFlagged) == -1
	    || g_mime_stream_reset(flagged_line) == -1
	    || g_mime_stream_write_to_stream(flagged_line, temp_stream) == -1
	    || g_mime_stream_write_string(temp_stream, "\n") == -1)
	{
	    g_mime_stream_unref(temp_stream);
	    unlink (tmp);
	    g_free(tmp);
	    g_mime_stream_unref(unseen_line);
	    g_mime_stream_unref(flagged_line);
	    g_mime_stream_unref(replied_line);
	    return FALSE;
	}
    }
    if (g_mime_stream_length(replied_line) != 0)
    {
	if (g_mime_stream_write_string(temp_stream,
				       LibBalsaMailboxMhReplied) == -1
	    || g_mime_stream_reset(replied_line) == -1
	    || g_mime_stream_write_to_stream(replied_line, temp_stream) == -1
	    || g_mime_stream_write_string(temp_stream, "\n") == -1)
	{
	    g_mime_stream_unref(temp_stream);
	    unlink (tmp);
	    g_free(tmp);
	    g_mime_stream_unref(unseen_line);
	    g_mime_stream_unref(flagged_line);
	    g_mime_stream_unref(replied_line);
	    return FALSE;
	}
    }

    /* close tempfile */
    g_mime_stream_unref(temp_stream);

    /* unlink '.mh_sequences' file */
    unlink(sequences_filename);

    /* rename tempfile to '.mh_sequences' */
    if (libbalsa_safe_rename(tmp, sequences_filename) == -1)
    {
	unlink (tmp);
	g_free(tmp);
	g_mime_stream_unref(unseen_line);
	g_mime_stream_unref(flagged_line);
	g_mime_stream_unref(replied_line);
	return FALSE;
    }

    g_free(tmp);
    g_mime_stream_unref(unseen_line);
    g_mime_stream_unref(flagged_line);
    g_mime_stream_unref(replied_line);
    msg_count = mh->messages_info->len;
    for (; msg_count; msg_count--) {
	msg_info = &g_array_index(mh->messages_info,
		       struct message_info, msg_count);
	if (msg_info && msg_info->filename)
	    break;
    }
    if (msg_count == 0)
	msg_info = &g_array_index(mh->messages_info,
		       struct message_info, msg_count);
    if (msg_count || (msg_info && msg_info->filename))
	    mh->last_index = msg_count + 1;
    else
	    mh->last_index = 0;
    return TRUE;
}

static struct message_info *message_info_from_msgno( LibBalsaMailboxMh * mailbox,
						     guint msgno)
{
    struct message_info *msg_info = NULL;
    guint index;

    if (msgno <= mailbox->last_msgno) {
	index = g_array_index(mailbox->msgno_2_index, int, msgno - 1);
	msg_info = &g_array_index(mailbox->messages_info,
				  struct message_info, index);
    } else
    while (msgno > mailbox->last_msgno) {
	for (index = mailbox->last_index;
	     index < mailbox->messages_info->len;
	     index++) {
	    msg_info = &g_array_index( mailbox->messages_info,
				       struct message_info, index);
	    if (msg_info && msg_info->filename)
		break;
	}
	if (msg_info && msg_info->filename) {
	    g_array_append_val(mailbox->msgno_2_index, index);
	    mailbox->last_msgno++;
	    mailbox->last_index = index + 1;
	} else {
	    msg_info = NULL;
	    break;
	}
    }
    return msg_info;
}

static LibBalsaMessage *
libbalsa_mailbox_mh_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;

    msg_info =
	message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox), msgno);

    if (!msg_info->message)
	msg_info->message =
	    libbalsa_mailbox_mh_load_message(mailbox, msgno);

    return msg_info->message;
}

static void
libbalsa_mailbox_mh_fetch_message_structure(LibBalsaMailbox * mailbox,
					    LibBalsaMessage * message,
					    LibBalsaFetchFlag flags)
{
    if (!message->mime_msg) {
	struct message_info *msg_info;

	msg_info = message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox),
					   message->msgno);

	if (!msg_info->mime_message) {
	    msg_info->mime_message =
		_libbalsa_mailbox_local_get_mime_message(mailbox,
							 msg_info->
							 filename, NULL);
	    g_object_add_weak_pointer(G_OBJECT(msg_info->mime_message),
				      (gpointer) & msg_info->mime_message);
	}
	message->mime_msg = msg_info->mime_message;
    }

    LIBBALSA_MAILBOX_CLASS(parent_class)->fetch_message_structure(mailbox,
								  message,
								  flags);
}

static void
libbalsa_mailbox_mh_release_message(LibBalsaMailbox * mailbox,
				    LibBalsaMessage * message)
{
    if (message->mime_msg) {
	g_object_unref(message->mime_msg);
	message->mime_msg = NULL;
    }
}

static LibBalsaMessage*
libbalsa_mailbox_mh_load_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message;
    struct message_info *msg_info;
    const gchar *path;
    gchar *filename;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MH(mailbox), NULL);
    g_return_val_if_fail (MAILBOX_OPEN(mailbox), NULL);
    g_return_val_if_fail (msgno > 0, NULL);

    mailbox->new_messages--;

    msg_info = message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox), msgno);

    if (!msg_info)
	return NULL;

    mailbox->messages++;

    msg_info->message = message = libbalsa_message_new();
    path = libbalsa_mailbox_local_get_path(mailbox);
    filename = g_build_filename(path, msg_info->filename, NULL);
    if (libbalsa_message_load_envelope_from_file(message, filename) == FALSE) {
	g_free(filename);
	return NULL;
    }
    g_free(filename);

    message->flags = msg_info->flags = msg_info->orig_flags;

    message->msgno = msgno;
    message->mailbox = mailbox;
    libbalsa_message_set_icons(message);

    return message;
}

static int libbalsa_mailbox_mh_add_message(LibBalsaMailbox * mailbox,
					   LibBalsaMessage * message )
{
    LibBalsaMailboxMh *mh;
    const char *path;
    char *tmp;
    int fd;
    GMimeStream *out_stream;
    int msgno;
    int retries;
    struct message_info *msg_info;
    GMimeStream *in_stream;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MH(mailbox), -1);
    g_return_val_if_fail (LIBBALSA_IS_MESSAGE(message), -1);

    mh = LIBBALSA_MAILBOX_MH(mailbox);

    LOCK_MAILBOX_RETURN_VAL(mailbox, -1);
    g_object_ref ( G_OBJECT(message ) );

    read_mh_sequences(mh);
    parse_mailbox(mh);
    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(mailbox);
    fd = libbalsa_mailbox_mh_open_temp(path, &tmp);
    if (fd == -1)
    {
	UNLOCK_MAILBOX(mailbox);
	return -1;
    }
    out_stream = g_mime_stream_fs_new(fd);
    {
	GMimeStream *tmp = libbalsa_mailbox_get_message_stream( message->mailbox, message );
	GMimeFilter *crlffilter = 
	    g_mime_filter_crlf_new (  GMIME_FILTER_CRLF_DECODE,
				      GMIME_FILTER_CRLF_MODE_CRLF_ONLY );
	in_stream = g_mime_stream_filter_new_with_stream (tmp);
	g_mime_stream_filter_add ( GMIME_STREAM_FILTER (in_stream), crlffilter );
	g_object_unref (crlffilter);
	g_mime_stream_unref (tmp);
    }    
    if (g_mime_stream_write_to_stream( in_stream, out_stream) == -1)
    {
	g_mime_stream_unref(out_stream);
	unlink (tmp);
	g_free(tmp);
	UNLOCK_MAILBOX(mailbox);
	return -1;
    }
    g_mime_stream_unref(out_stream);
    g_mime_stream_unref(in_stream);

    msgno = mh->messages_info->len + 1; 
    retries = 10;
    do {
	/* rename tempfile to message-number-name */
	char *new_filename;
	gint rename_status;

	new_filename = g_strdup_printf("%s/%d", path, msgno);
	rename_status = libbalsa_safe_rename(tmp, new_filename);
	g_free(new_filename);
	if (rename_status != -1)
	    break;
	
	if (errno != EEXIST)
	{
	    unlink (tmp);
	    g_free(tmp);
	    UNLOCK_MAILBOX(mailbox);
	    /* FIXME: report error ... */
	    return -1;
	}
	msgno++;
	retries--;
    } while (retries > 0);
    g_free(tmp);

    if (retries == 0) {
	UNLOCK_MAILBOX(mailbox);
	/* FIXME: report error ... */
	return -1;
    }
    g_array_set_size(mh->messages_info, msgno);
    msg_info = &g_array_index(mh->messages_info, struct message_info, msgno - 1);
    msg_info->filename = g_strdup_printf("%d", msgno);
    msg_info->flags = msg_info->orig_flags = message->flags;
    mailbox->new_messages++;

    g_object_unref ( G_OBJECT(message ) );    
    UNLOCK_MAILBOX(mailbox);

    return 1;
}

static void
libbalsa_mailbox_mh_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    struct message_info *msg_info;

    g_return_if_fail (LIBBALSA_IS_MAILBOX_MH(mailbox));
    g_return_if_fail (MAILBOX_OPEN(mailbox));
    g_return_if_fail (msgno > 0);

    msg_info = message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox), msgno);

    g_return_if_fail (msg_info != NULL);
    msg_info->flags |= set;
    msg_info->flags &= ~clear;
}
