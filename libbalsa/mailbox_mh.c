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
    LibBalsaMessageFlag flags;
    LibBalsaMessageFlag orig_flags;
    LibBalsaMessage *message;
    gint fileno;
};

static LibBalsaMailboxLocalClass *parent_class = NULL;

static void libbalsa_mailbox_mh_class_init(LibBalsaMailboxMhClass *klass);
static void libbalsa_mailbox_mh_init(LibBalsaMailboxMh * mailbox);

static GMimeStream *libbalsa_mailbox_mh_get_message_stream(LibBalsaMailbox *
							   mailbox,
							   LibBalsaMessage *
							   message);
static void libbalsa_mailbox_mh_remove_files(LibBalsaMailboxLocal *mailbox);

static gboolean libbalsa_mailbox_mh_open(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_mh_close_mailbox(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_mh_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mh_sync(LibBalsaMailbox * mailbox,
                                         gboolean expunge);
static struct message_info *lbm_mh_message_info_from_msgno(
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
static LibBalsaMessage *libbalsa_mailbox_mh_load_message(LibBalsaMailbox *
							 mailbox,
							 guint msgno);
static int libbalsa_mailbox_mh_add_message(LibBalsaMailbox * mailbox,
					   LibBalsaMessage * message );
static void libbalsa_mailbox_mh_change_message_flags(LibBalsaMailbox * mailbox,
						     guint msgno,
						     LibBalsaMessageFlag set,
						     LibBalsaMessageFlag clear);
static guint libbalsa_mailbox_mh_total_messages(LibBalsaMailbox * mailbox);


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

    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_mh_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_mh_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_mh_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_mh_sync;
    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_mh_close_mailbox;
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_mh_get_message;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_mh_fetch_message_structure;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_mh_add_message;
    libbalsa_mailbox_class->change_message_flags =
	libbalsa_mailbox_mh_change_message_flags;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_mh_total_messages;

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
	    if ((i = creat (tmp, S_IRWXU)) == -1) {
		libbalsa_information
		    (LIBBALSA_INFORMATION_WARNING, 
		     _("Could not create MH structure at %s (%s)"),
		     path, strerror(errno));
		rmdir (path);
		return (-1);
	    } else close(i);   	    
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
    
    libbalsa_notify_register_mailbox(mailbox);
    
    return G_OBJECT(mailbox);
}

#define MH_BASENAME(msgno) \
    g_strdup_printf((msgno->orig_flags & LIBBALSA_MESSAGE_FLAG_DELETED) ? \
		    ",%d" : "%d", msg_info->fileno)

static GMimeStream *
libbalsa_mailbox_mh_get_message_stream(LibBalsaMailbox * mailbox,
				       LibBalsaMessage * message)
{
    GMimeStream *stream;
    struct message_info *msg_info;
    gchar *tmp;

    g_return_val_if_fail(MAILBOX_OPEN(mailbox), NULL);

    msg_info = lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox),
					      message->msgno);
    tmp = MH_BASENAME(msg_info);
    stream = _libbalsa_mailbox_local_get_message_stream(mailbox, tmp, NULL);
    g_free(tmp);

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
static gboolean
lbm_mh_check_filename(const gchar * s)
{
    for (; *s; s++) {
	if (!g_ascii_isdigit(*s))
	    return FALSE;
    }
    return TRUE;
}

static gint
lbm_mh_compare_fileno(const struct message_info ** a,
		      const struct message_info ** b)
{
    return (*a)->fileno - (*b)->fileno;
}

static void
lbm_mh_parse_mailbox(LibBalsaMailboxMh * mh)
{
    const gchar *path;
    GDir *dir;
    const gchar *filename;

    path = libbalsa_mailbox_local_get_path(mh);

    if ((dir = g_dir_open(path, 0, NULL)) == NULL)
	return;

    while ((filename = g_dir_read_name(dir)) != NULL) {
	LibBalsaMessageFlag delete_flag = 0;
	guint fileno;

	if (filename[0] == ',') {
	    filename++;
	    delete_flag = LIBBALSA_MESSAGE_FLAG_DELETED;
	}
	if (lbm_mh_check_filename(filename) == FALSE)
	    continue;

	sscanf(filename, "%d", &fileno);
	if (fileno > mh->last_fileno)
	    mh->last_fileno = fileno;

	if (mh->messages_info) {
	    struct message_info *msg_info =
		g_hash_table_lookup(mh->messages_info,
				    GINT_TO_POINTER(fileno));
	    if (!msg_info) {
		msg_info = g_new0(struct message_info, 1);
		g_hash_table_insert(mh->messages_info,
				    GINT_TO_POINTER(fileno), msg_info);
		g_ptr_array_add(mh->msgno_2_msg_info, msg_info);
		msg_info->fileno = fileno;
	    }
	    msg_info->orig_flags = delete_flag;
	}
    }
    g_dir_close(dir);

    if (mh->msgno_2_msg_info)
	g_ptr_array_sort(mh->msgno_2_msg_info,
			 (GCompareFunc) lbm_mh_compare_fileno);
}

static const gchar *LibBalsaMailboxMhUnseen = "unseen:";
static const gchar *LibBalsaMailboxMhFlagged = "flagged:";
static const gchar *LibBalsaMailboxMhReplied = "replied:";

static void
lbm_mh_set_flag(LibBalsaMailboxMh * mh, guint fileno, LibBalsaMessageFlag flag)
{
    struct message_info *msg_info;

    if (!fileno)
	return;

    msg_info = g_hash_table_lookup(mh->messages_info, GINT_TO_POINTER(fileno));

    if (!msg_info) {
	g_print("MH sequence info for nonexistent message %d\n", fileno);
	return;
    }

    msg_info->orig_flags |= flag;
}

static void
lbm_mh_handle_seq_line(LibBalsaMailboxMh * mh, gchar * line)
{
    LibBalsaMessageFlag flag;
    gchar **sequences, **seq;

    if (libbalsa_str_has_prefix(line, LibBalsaMailboxMhUnseen))
	flag = LIBBALSA_MESSAGE_FLAG_NEW;
    else if (libbalsa_str_has_prefix(line, LibBalsaMailboxMhFlagged))
	flag = LIBBALSA_MESSAGE_FLAG_FLAGGED;
    else if (libbalsa_str_has_prefix(line, LibBalsaMailboxMhReplied))
	flag = LIBBALSA_MESSAGE_FLAG_REPLIED;
    else			/* unknown sequence */
	return;

    line = strchr(line, ':') + 1;
    sequences = g_strsplit(line, " ", 0);

    for (seq = sequences; *seq; seq++) {
	guint end = 0;
	guint fileno;

	if (!**seq)
	    continue;

	line = strchr(*seq, '-');
	if (line) {
	    *line++ = '\0';
	    sscanf(line, "%d", &end);
	}
	sscanf(*seq, "%d", &fileno);
	do
	    lbm_mh_set_flag(mh, fileno, flag);
	while (++fileno <= end);
    }

    g_strfreev(sequences);
}

static void
lbm_mh_parse_sequences(LibBalsaMailboxMh * mailbox)
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

	lbm_mh_handle_seq_line(mailbox, line->data);
	line->len = 0;
    } while (!g_mime_stream_eos(gmime_stream_buffer));
    g_mime_stream_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);
}

static void
lbm_mh_parse_both(LibBalsaMailboxMh * mh)
{
    lbm_mh_parse_mailbox(mh);
    if (mh->msgno_2_msg_info)
	lbm_mh_parse_sequences(mh);
}

static void
lbm_mh_free_message_info(struct message_info *msg_info)
{
    if (!msg_info)
	return;
    if (msg_info->message) {
	msg_info->message->mailbox = NULL;
	g_object_unref(msg_info->message);
    }
    g_free(msg_info);
}

static gboolean
libbalsa_mailbox_mh_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(mailbox);
    struct stat st;
    const gchar* path;
   
    path = libbalsa_mailbox_local_get_path(mailbox);

    if (stat(path, &st) == -1) {
	return FALSE;
    }

    mh->mtime = st.st_mtime;
    mh->sequences_filename =
	g_strdup_printf("%s/.mh_sequences", path);

    mh->messages_info =
	g_hash_table_new_full(NULL, NULL, NULL,
		              (GDestroyNotify)lbm_mh_free_message_info);
    mh->msgno_2_msg_info = g_ptr_array_new();
    mh->last_fileno = 0;
    
    if (!mailbox->readonly)
	mailbox->readonly = access (path, W_OK) ? 1 : 0;
    mailbox->unread_messages = 0;
    lbm_mh_parse_both(mh);

    LIBBALSA_MAILBOX_LOCAL(mailbox)->sync_time = 0;
    LIBBALSA_MAILBOX_LOCAL(mailbox)->sync_cnt  = 1;
#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxMh", mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

static int libbalsa_mailbox_mh_open_temp (const gchar *dest_path,
					  char **name_used);
/* Called with mailbox locked. */
static void
libbalsa_mailbox_mh_check(LibBalsaMailbox * mailbox)
{
    struct stat st, st_sequences;
    LibBalsaMailboxMh *mh;
    const gchar *path;
    int modified = 0;
    guint last_msgno;

    g_assert(LIBBALSA_IS_MAILBOX_MH(mailbox));

    if (!MAILBOX_OPEN(mailbox)) {
	if (libbalsa_notify_check_mailbox(mailbox))
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
	return;
    }

    mh = LIBBALSA_MAILBOX_MH(mailbox);

    path = libbalsa_mailbox_local_get_path(mailbox);
    if (stat(path, &st) == -1)
	return;

    /* create .mh_sequences when there isn't one. */
    if (stat(mh->sequences_filename, &st_sequences) == -1) {
	if (errno == ENOENT) {
	    gchar *tmp;
	    int fd = libbalsa_mailbox_mh_open_temp(path, &tmp);
	    if (fd != -1) {
		close(fd);
		if (libbalsa_safe_rename(tmp,
					 mh->sequences_filename) == -1)
		    unlink(tmp);
		g_free(tmp);
	    }
	    if (stat(mh->sequences_filename, &st_sequences) == -1)
		modified = 1;
	} else
	    modified = 1;
    }

    if (st.st_mtime != mh->mtime)
	modified = 1;

    if (st_sequences.st_mtime != mh->mtime_sequences)
	modified = 1;

    if (!modified)
	return;

    mh->mtime = st.st_mtime;
    mh->mtime_sequences = st_sequences.st_mtime;

    last_msgno = mh->msgno_2_msg_info->len;
    lbm_mh_parse_both(mh);

    libbalsa_mailbox_local_load_messages(mailbox, last_msgno);
}

static void
libbalsa_mailbox_mh_close_mailbox(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(mailbox);
    guint len = 0;

    if (mh->msgno_2_msg_info)
	len = mh->msgno_2_msg_info->len;
    libbalsa_mailbox_mh_sync(mailbox, TRUE);

    if (mh->messages_info) {
	g_hash_table_destroy(mh->messages_info);
	mh->messages_info = NULL;
    }
    if (mh->msgno_2_msg_info) {
	if (mh->msgno_2_msg_info->len != len)
	    g_signal_emit_by_name(mailbox, "changed");
	g_ptr_array_free(mh->msgno_2_msg_info, TRUE);
	mh->msgno_2_msg_info = NULL;
    }
    g_free(mh->sequences_filename);
    mh->sequences_filename = NULL;

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
	LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox(mailbox);
}

static int
libbalsa_mailbox_mh_open_temp (const gchar *dest_path,
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

struct line_info {
    gint first, last;
    GMimeStream *line;
};

static void
lbm_mh_print_line(struct line_info * li)
{
    if (li->last == -1)
	return;
    if (li->first != li->last)
	/* interval */
	g_mime_stream_printf(li->line, " %d-%d", li->first, li->last);
    else
	/* single message */
	g_mime_stream_printf(li->line, " %d", li->last);
}

static void
lbm_mh_flag_line(struct message_info *msg_info, LibBalsaMessageFlag flag,
		 struct line_info *li)
{
    if (!(msg_info->flags & flag))
	return;

    if (li->last < msg_info->fileno - 1) {
	lbm_mh_print_line(li);
	li->first = msg_info->fileno;
    }
    li->last = msg_info->fileno;
}

static gboolean
lbm_mh_finish_line(struct line_info *li, GMimeStream * temp_stream,
		   const gchar * flag_string)
{
    if (g_mime_stream_length(li->line) > 0) {
	if (g_mime_stream_write_string(temp_stream, flag_string) == -1
	    || g_mime_stream_reset(li->line) == -1
	    || g_mime_stream_write_to_stream(li->line, temp_stream) == -1
	    || g_mime_stream_write_string(temp_stream, "\n") == -1)
	    return FALSE;
    }

    return TRUE;
}

static gboolean
libbalsa_mailbox_mh_sync(LibBalsaMailbox * mailbox, gboolean expunge)
{
    LibBalsaMailboxMh *mh;
    struct line_info unseen, flagged, replied;
    const gchar *path;
    gchar *tmp;
    guint msgno;
    struct message_info *msg_info;

    int fd;
    GMimeStream *temp_stream;
    gchar* sequences_filename;
    GMimeStream *gmime_stream;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;
    gboolean retval;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MH(mailbox), FALSE);
    mh = LIBBALSA_MAILBOX_MH(mailbox);
    g_return_val_if_fail( mh->messages_info != NULL, FALSE );

    /* build new sequence file lines */
    unseen.first  = unseen.last = -1;
    unseen.line   = g_mime_stream_mem_new();
    flagged.first = flagged.last = -1;
    flagged.line  = g_mime_stream_mem_new();
    replied.first = replied.last = -1;
    replied.line  = g_mime_stream_mem_new();

    path = libbalsa_mailbox_local_get_path(mailbox);

    msgno = 1;
    while (msgno <= mh->msgno_2_msg_info->len) {
	msg_info = lbm_mh_message_info_from_msgno(mh, msgno);

	if (expunge && (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	    /* MH just moves files out of the way when you delete them */
	    /* chbm: not quite, however this is probably a good move for
	       flag deleted */
	    char *tmp = MH_BASENAME(msg_info);
	    char *orig = g_build_filename(path, tmp, NULL);
	    g_free(tmp);
	    unlink(orig);
	    g_free(orig);
	    /* free old information */
	    g_ptr_array_remove(mh->msgno_2_msg_info, msg_info);
	    g_hash_table_remove(mh->messages_info, 
		    		GINT_TO_POINTER(msg_info->fileno));
	    libbalsa_mailbox_msgno_removed(mailbox, msgno);
	} else {
	    lbm_mh_flag_line(msg_info, LIBBALSA_MESSAGE_FLAG_NEW, &unseen);
	    lbm_mh_flag_line(msg_info, LIBBALSA_MESSAGE_FLAG_FLAGGED, &flagged);
	    lbm_mh_flag_line(msg_info, LIBBALSA_MESSAGE_FLAG_REPLIED, &replied);
	    if ((msg_info->flags ^ msg_info->orig_flags) &
		LIBBALSA_MESSAGE_FLAG_DELETED) {
		gchar *tmp;
		gchar *old_file;
		gchar *new_file;

		tmp = MH_BASENAME(msg_info);
		old_file = g_build_filename(path, tmp, NULL);
		g_free(tmp);

		msg_info->orig_flags = msg_info->flags;

		tmp = MH_BASENAME(msg_info);
		new_file = g_build_filename(path, tmp, NULL);
		g_free(tmp);

		if (libbalsa_safe_rename(old_file, new_file) == -1)
		    /* FIXME: report error ... */
		    ;

		g_free(old_file);
		g_free(new_file);
	    } else
		msg_info->orig_flags = msg_info->flags;
	    msgno++;
	}
    }
    lbm_mh_print_line(&unseen);
    lbm_mh_print_line(&flagged);
    lbm_mh_print_line(&replied);

    /* Renumber */
    for (msgno = 1; msgno <= mh->msgno_2_msg_info->len; msgno++) {
	msg_info = lbm_mh_message_info_from_msgno(mh, msgno);
	msg_info->message->msgno = msgno;
    }

    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(mailbox);
    fd = libbalsa_mailbox_mh_open_temp(path, &tmp);
    if (fd == -1)
    {
	g_mime_stream_unref(unseen.line);
	g_mime_stream_unref(flagged.line);
	g_mime_stream_unref(replied.line);
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
	    !libbalsa_str_has_prefix(line->data, LibBalsaMailboxMhUnseen) &&
	    !libbalsa_str_has_prefix(line->data, LibBalsaMailboxMhFlagged) &&
	    !libbalsa_str_has_prefix(line->data, LibBalsaMailboxMhReplied))
	{
	    /* unknown sequence */
	    g_mime_stream_write(temp_stream, line->data, line->len);
	}
    } while (!g_mime_stream_eos(gmime_stream_buffer));
    g_mime_stream_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);

    /* write unseen, flagged and replied sequences */
    if (!lbm_mh_finish_line(&unseen, temp_stream, LibBalsaMailboxMhUnseen) ||
	!lbm_mh_finish_line(&flagged, temp_stream, LibBalsaMailboxMhFlagged) ||
	!lbm_mh_finish_line(&replied, temp_stream, LibBalsaMailboxMhReplied)) {
	unlink(tmp);
	g_free(tmp);
	g_mime_stream_unref(temp_stream);
	g_mime_stream_unref(unseen.line);
	g_mime_stream_unref(flagged.line);
	g_mime_stream_unref(replied.line);
	return FALSE;
    }

    /* close tempfile */
    g_mime_stream_unref(temp_stream);

    /* unlink '.mh_sequences' file */
    unlink(sequences_filename);

    /* rename tempfile to '.mh_sequences' */
    retval = (libbalsa_safe_rename(tmp, sequences_filename) != -1);
    if (!retval)
	unlink (tmp);

    g_free(tmp);
    g_mime_stream_unref(unseen.line);
    g_mime_stream_unref(flagged.line);
    g_mime_stream_unref(replied.line);

    return retval;
}

static struct message_info *
lbm_mh_message_info_from_msgno(LibBalsaMailboxMh * mh, guint msgno)
{
    struct message_info *msg_info = NULL;

    g_assert(msgno > 0 && msgno <= mh->msgno_2_msg_info->len);
    msg_info = g_ptr_array_index(mh->msgno_2_msg_info, msgno - 1);

    return msg_info;
}

static LibBalsaMessage *
libbalsa_mailbox_mh_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;

    msg_info =
	lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox), msgno);

    if (!msg_info->message)
	libbalsa_mailbox_local_load_message(mailbox, msgno);

    return msg_info->message;
}

static void
libbalsa_mailbox_mh_fetch_message_structure(LibBalsaMailbox * mailbox,
					    LibBalsaMessage * message,
					    LibBalsaFetchFlag flags)
{
    if (!message->mime_msg) {
	struct message_info *msg_info;
	gchar *tmp;

	msg_info =
	    lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox),
					   message->msgno);

	tmp = MH_BASENAME(msg_info);
	message->mime_msg =
	    _libbalsa_mailbox_local_get_mime_message(mailbox, tmp, NULL);
	g_free(tmp);

	g_mime_object_remove_header(GMIME_OBJECT(message->mime_msg),
				    "Status");
	g_mime_object_remove_header(GMIME_OBJECT(message->mime_msg),
				    "X-Status");
    }

    LIBBALSA_MAILBOX_CLASS(parent_class)->fetch_message_structure(mailbox,
								  message,
								  flags);
}

static LibBalsaMessage*
libbalsa_mailbox_mh_load_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage * message;
    struct message_info *msg_info;
    const gchar *path;
    gchar *filename;
    gchar *tmp;

    msg_info =
	lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox), msgno);
    msg_info->message = message = libbalsa_message_new();

    path = libbalsa_mailbox_local_get_path(mailbox);
    tmp = MH_BASENAME(msg_info);
    filename = g_build_filename(path, tmp, NULL);
    g_free(tmp);
    if (libbalsa_message_load_envelope_from_file(message, filename) == FALSE) {
	g_free(filename);
	printf(" no envelope--returning NULL\n");
	return NULL;
    }
    g_free(filename);

    message->flags = msg_info->flags = msg_info->orig_flags;

    return message;
}

/* Called with mailbox locked. */
static int
libbalsa_mailbox_mh_add_message(LibBalsaMailbox * mailbox,
					   LibBalsaMessage * message )
{
    LibBalsaMailboxMh *mh;
    const char *path;
    char *tmp;
    int fd;
    GMimeStream *out_stream;
    int fileno;
    int retries;
    GMimeStream *in_stream;

    mh = LIBBALSA_MAILBOX_MH(mailbox);

    lbm_mh_parse_both(mh);
    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(mailbox);
    fd = libbalsa_mailbox_mh_open_temp(path, &tmp);
    if (fd == -1)
	return -1;
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
	return -1;
    }
    g_mime_stream_unref(out_stream);
    g_mime_stream_unref(in_stream);

    fileno = mh->last_fileno; 
    retries = 10;
    do {
	/* rename tempfile to message-number-name */
	char *new_filename;
	gint rename_status;

	new_filename = g_strdup_printf("%s/%d", path, ++fileno);
	rename_status = libbalsa_safe_rename(tmp, new_filename);
	g_free(new_filename);
	if (rename_status != -1)
	    break;
	
	if (errno != EEXIST)
	{
	    unlink (tmp);
	    g_free(tmp);
	    /* FIXME: report error ... */
	    return -1;
	}
    } while (--retries > 0);
    g_free(tmp);

    if (retries == 0)
	/* FIXME: report error ... */
	return -1;

    mh->last_fileno = fileno;

    return 1;
}

static void
libbalsa_mailbox_mh_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    struct message_info *msg_info;

    msg_info =
	lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox), msgno);

    g_return_if_fail (msg_info != NULL);
    msg_info->flags |= set;
    msg_info->flags &= ~clear;

    libbalsa_mailbox_local_queue_sync(LIBBALSA_MAILBOX_LOCAL(mailbox));
}

static guint
libbalsa_mailbox_mh_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMh *mh = (LibBalsaMailboxMh *) mailbox;

    return mh->msgno_2_msg_info ? mh->msgno_2_msg_info->len : 0;
}
