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

/* to compile this on BSD/Darwin */
#undef _POSIX_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libbalsa.h"
#include "misc.h"
#include "libbalsa_private.h"
#include "libbalsa-conf.h"
#include "mime-stream-shared.h"
#include <glib/gi18n.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mbox-mh"

struct message_info {
    LibBalsaMailboxLocalMessageInfo local_info;
    LibBalsaMessageFlag orig_flags;     /* Has only real flags */
    gint fileno;
};

#define REAL_FLAGS(flags) (flags & LIBBALSA_MESSAGE_FLAGS_REAL)

static void libbalsa_mailbox_mh_finalize(GObject * object);
static void libbalsa_mailbox_mh_load_config(LibBalsaMailbox * mailbox,
                                            const gchar * prefix);

static GMimeStream *libbalsa_mailbox_mh_get_message_stream(LibBalsaMailbox *
							   mailbox,
							   guint msgno,
							   gboolean peek);
static gint lbm_mh_check_files(const gchar * path, gboolean create);
static void lbm_mh_set_path(LibBalsaMailboxLocal * mailbox,
                            const gchar * path);
static void lbm_mh_remove_files(LibBalsaMailboxLocal *mailbox);
static LibBalsaMailboxLocalMessageInfo
    *lbm_mh_get_info(LibBalsaMailboxLocal * local, guint msgno);
static LibBalsaMailboxLocalAddMessageFunc lbm_mh_add_message;

static gboolean libbalsa_mailbox_mh_open(LibBalsaMailbox * mailbox,
					 GError **err);
static void libbalsa_mailbox_mh_close_mailbox(LibBalsaMailbox * mailbox,
                                              gboolean expunge);
static void libbalsa_mailbox_mh_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_mh_sync(LibBalsaMailbox * mailbox,
                                         gboolean expunge);
static struct message_info *lbm_mh_message_info_from_msgno(
						  LibBalsaMailboxMh * mailbox,
						  guint msgno);
static gboolean libbalsa_mailbox_mh_fetch_message_structure(LibBalsaMailbox
                                                            * mailbox,
                                                            LibBalsaMessage
                                                            * message,
                                                            LibBalsaFetchFlag
                                                            flags);
static guint libbalsa_mailbox_mh_total_messages(LibBalsaMailbox * mailbox);

static const guint8 zero[1] = {0};

struct _LibBalsaMailboxMh {
    LibBalsaMailboxLocal parent;

    GHashTable* messages_info;
    GPtrArray* msgno_2_msg_info;
    gchar* sequences_filename;
    time_t mtime_sequences;
    guint last_fileno;
};

G_DEFINE_TYPE(LibBalsaMailboxMh,
              libbalsa_mailbox_mh,
              LIBBALSA_TYPE_MAILBOX_LOCAL)

static void
libbalsa_mailbox_mh_class_init(LibBalsaMailboxMhClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    object_class->finalize = libbalsa_mailbox_mh_finalize;

    libbalsa_mailbox_class->load_config = libbalsa_mailbox_mh_load_config;
    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_mh_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_mh_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_mh_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_mh_sync;
    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_mh_close_mailbox;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_mh_fetch_message_structure;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_mh_total_messages;

    libbalsa_mailbox_local_class->check_files  = lbm_mh_check_files;
    libbalsa_mailbox_local_class->set_path     = lbm_mh_set_path;
    libbalsa_mailbox_local_class->remove_files = lbm_mh_remove_files;
    libbalsa_mailbox_local_class->get_info     = lbm_mh_get_info;
    libbalsa_mailbox_local_class->add_message  = lbm_mh_add_message;
}

static void
libbalsa_mailbox_mh_init(LibBalsaMailboxMh * mailbox)
{
    /* mh->sequences_file = NULL; */
}

static gint
lbm_mh_check_files(const gchar * path, gboolean create)
{
    g_return_val_if_fail(path != NULL, -1);

    if (access(path, F_OK) == 0) {
        /* File exists. Check if it is a mh... */
        if (libbalsa_mailbox_type_from_path(path) !=
            LIBBALSA_TYPE_MAILBOX_MH) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Mailbox %s does not appear to be an MH mailbox."),
                                 path);
            return -1;
        }
    } else if (create) {
        gint fd;
        gchar *sequences_filename;

        if (mkdir(path, S_IRWXU)) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Could not create MH directory at %s (%s)"),
                                 path, g_strerror(errno));
            return -1;
        }

        sequences_filename = g_build_filename(path, ".mh_sequences", NULL);
        fd = creat(sequences_filename, S_IRWXU);
        g_free(sequences_filename);

        if (fd == -1) {
            libbalsa_information
                (LIBBALSA_INFORMATION_WARNING,
                 _("Could not create MH structure at %s (%s)"),
                 path, g_strerror(errno));
            rmdir(path);
            return -1;
        } else
            close(fd);
    } else
        return -1;

    return 0;
}

static void
lbm_mh_set_sequences_filename(LibBalsaMailboxMh * mh, const gchar * path)
{
    g_free(mh->sequences_filename);
    mh->sequences_filename = g_build_filename(path, ".mh_sequences", NULL);
}

static void
lbm_mh_set_path(LibBalsaMailboxLocal * local, const gchar * path)
{
    lbm_mh_set_sequences_filename(LIBBALSA_MAILBOX_MH(local), path);
}

LibBalsaMailbox *
libbalsa_mailbox_mh_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MH, NULL);

    if (libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL(mailbox),
                                        path, create) != 0) {
	g_object_unref(mailbox);
	return NULL;
    }

    return mailbox;
}

static void
libbalsa_mailbox_mh_finalize(GObject * object)
{
    LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(object);
    g_free(mh->sequences_filename);
    G_OBJECT_CLASS(libbalsa_mailbox_mh_parent_class)->finalize(object);
}

static void
libbalsa_mailbox_mh_load_config(LibBalsaMailbox * mailbox,
                                const gchar * prefix)
{
    LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(mailbox);
    gchar *path;

    path = libbalsa_conf_get_string("Path");
    lbm_mh_set_sequences_filename(mh, path);
    g_free(path);

    LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mh_parent_class)->load_config(mailbox, prefix);
}

#define MH_BASENAME(msg_info) \
    g_strdup_printf((msg_info->orig_flags & LIBBALSA_MESSAGE_FLAG_DELETED) ? \
		    ",%d" : "%d", msg_info->fileno)

static GMimeStream *
libbalsa_mailbox_mh_get_message_stream(LibBalsaMailbox * mailbox,
				       guint msgno, gboolean peek)
{
    GMimeStream *stream;
    struct message_info *msg_info;
    gchar *base_name;

    g_return_val_if_fail(MAILBOX_OPEN(mailbox), NULL);

    msg_info = lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox),
					      msgno);
    base_name = MH_BASENAME(msg_info);
    stream = libbalsa_mailbox_local_get_message_stream(mailbox, base_name, NULL);
    g_free(base_name);

    return stream;
}

static void
lbm_mh_remove_files(LibBalsaMailboxLocal *mailbox)
{
    const gchar* path;
    GError *error = NULL;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_MH(mailbox));
    path = libbalsa_mailbox_local_get_path(mailbox);

    if (!libbalsa_delete_directory(path, &error)) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("Could not remove “%s”: %s"),
			     path, error->message);
	g_clear_error(&error);
    }
    LIBBALSA_MAILBOX_LOCAL_CLASS(libbalsa_mailbox_mh_parent_class)->remove_files(mailbox);
}


static LibBalsaMailboxLocalMessageInfo *
lbm_mh_get_info(LibBalsaMailboxLocal * local, guint msgno)
{
    struct message_info *msg_info;

    msg_info = lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(local),
					      msgno);
    if (msg_info->local_info.flags == LIBBALSA_MESSAGE_FLAG_INVALID)
        msg_info->local_info.flags = msg_info->orig_flags;

    return &msg_info->local_info;
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
lbm_mh_parse_mailbox(LibBalsaMailboxMh * mh, gboolean add_msg_info)
{
    const gchar *path;
    GDir *dir;
    const gchar *filename;

    path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mh);

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

	if (sscanf(filename, "%10d", &fileno) != 1)
            break;     /* FIXME report error? */
	if (fileno > mh->last_fileno)
	    mh->last_fileno = fileno;

	if (add_msg_info && mh->messages_info) {
	    struct message_info *msg_info =
		g_hash_table_lookup(mh->messages_info,
				    GINT_TO_POINTER(fileno));
	    if (!msg_info) {
		msg_info = g_new0(struct message_info, 1);
		msg_info->local_info.flags = LIBBALSA_MESSAGE_FLAG_INVALID;
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
static const gchar *LibBalsaMailboxMhRecent = "recent:";

static void
lbm_mh_set_flag(LibBalsaMailboxMh * mh, guint fileno, LibBalsaMessageFlag flag)
{
    struct message_info *msg_info;

    if (!fileno)
	return;

    msg_info = g_hash_table_lookup(mh->messages_info, GINT_TO_POINTER(fileno));

    if (!msg_info) {
	g_debug("MH sequence info for nonexistent message %d", fileno);
	return;
    }

    msg_info->orig_flags |= flag;
}

static void
lbm_mh_handle_seq_line(LibBalsaMailboxMh * mh, gchar * line)
{
    LibBalsaMessageFlag flag;
    gchar **sequences, **seq;

    if (g_str_has_prefix(line, LibBalsaMailboxMhUnseen))
	flag = LIBBALSA_MESSAGE_FLAG_NEW;
    else if (g_str_has_prefix(line, LibBalsaMailboxMhFlagged))
	flag = LIBBALSA_MESSAGE_FLAG_FLAGGED;
    else if (g_str_has_prefix(line, LibBalsaMailboxMhReplied))
	flag = LIBBALSA_MESSAGE_FLAG_REPLIED;
    else if (g_str_has_prefix(line, LibBalsaMailboxMhRecent))
	flag = LIBBALSA_MESSAGE_FLAG_RECENT;
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
	    if (sscanf(line, "%10d", &end) != 1)
                break; /* FIXME report error? */
	}
	if (sscanf(*seq, "%10d", &fileno) != 1)
            break;     /* FIXME report error? */
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
    const gchar* sequences_filename;
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
    if (fd < 0)
	return;
    gmime_stream = g_mime_stream_fs_new(fd);
    gmime_stream_buffer = g_mime_stream_buffer_new(gmime_stream,
					GMIME_STREAM_BUFFER_BLOCK_READ);
    g_object_unref(gmime_stream);
    line = g_byte_array_new();
    do {
	g_byte_array_set_size(line, 0);
	g_mime_stream_buffer_readln(gmime_stream_buffer, line);
	g_byte_array_append(line, zero, 1);

	lbm_mh_handle_seq_line(mailbox, (gchar *) line->data);
    } while (!g_mime_stream_eos(gmime_stream_buffer));
    g_object_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);
}

static void
lbm_mh_parse_both(LibBalsaMailboxMh * mh)
{
    lbm_mh_parse_mailbox(mh, TRUE);
    if (mh->msgno_2_msg_info)
	lbm_mh_parse_sequences(mh);
}

static void
lbm_mh_free_message_info(struct message_info *msg_info)
{
    if (msg_info == NULL)
	return;

    if (msg_info->local_info.message != NULL) {
        libbalsa_message_set_mailbox(msg_info->local_info.message, NULL);
        libbalsa_message_set_msgno(msg_info->local_info.message, 0);
	g_object_remove_weak_pointer(G_OBJECT(msg_info->local_info.message),
				     (gpointer *) &msg_info->local_info.message);
    }

    g_free(msg_info);
}

static gboolean
libbalsa_mailbox_mh_open(LibBalsaMailbox * mailbox, GError **err)
{
    LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(mailbox);
    struct stat st;
    const gchar* path;
   
    path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);

    if (stat(path, &st) == -1) {
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Mailbox does not exist."));
	return FALSE;
    }

    libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);

    mh->messages_info =
	g_hash_table_new_full(NULL, NULL, NULL,
		              (GDestroyNotify)lbm_mh_free_message_info);
    mh->msgno_2_msg_info = g_ptr_array_new();
    mh->last_fileno = 0;
    
    libbalsa_mailbox_set_readonly(mailbox, access(path, W_OK));
    libbalsa_mailbox_clear_unread_messages(mailbox);
    lbm_mh_parse_both(mh);

    g_debug("%s: Opening %s Refcount: %u",
	    __func__, libbalsa_mailbox_get_name(mailbox), libbalsa_mailbox_get_open_ref(mailbox));
    return TRUE;
}

static gboolean
lbm_mh_check(LibBalsaMailboxMh * mh, const gchar * path)
{
    int fd;
    GMimeStream *gmime_stream;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;
    gboolean retval = FALSE;

    if ((fd = open(mh->sequences_filename, O_RDONLY)) < 0)
	return retval;

    gmime_stream = g_mime_stream_fs_new(fd);
    gmime_stream_buffer = g_mime_stream_buffer_new(gmime_stream,
					GMIME_STREAM_BUFFER_BLOCK_READ);
    g_object_unref(gmime_stream);

    line = g_byte_array_new();
    do {
	g_byte_array_set_size(line, 0);
	g_mime_stream_buffer_readln(gmime_stream_buffer, line);
	g_byte_array_append(line, zero, 1);

	if (g_str_has_prefix((gchar *) line->data,
				    LibBalsaMailboxMhUnseen)) {
	    /* Found the "unseen: " line... */
	    gchar *p = (gchar *) line->data + strlen(LibBalsaMailboxMhUnseen);
	    gchar **sequences, **seq;
	    
	    sequences = g_strsplit(p, " ", 0);
	    for (seq = sequences; *seq; seq++) {
		guint end = 0;
		guint fileno;

		if (!**seq)
		    continue;

		p = strchr(*seq, '-');
		if (p) {
		    *p++ = '\0';
		    if (sscanf(p, "%10d", &end) != 1)
                        break; /* FIXME report error? */
		}
		if (sscanf(*seq, "%10d", &fileno) != 1)
                    break; /* FIXME report error? */
		do {
		    p = g_strdup_printf("%s/%d", path, fileno);
		    if (access(p, F_OK) == 0)
			retval = TRUE;
		    g_free(p);
		    /* One undeleted unread message is enough. */
		} while (!retval && ++fileno <= end);
	    }
	    g_strfreev(sequences);
	    break;
	}
    } while (!g_mime_stream_eos(gmime_stream_buffer));

    g_object_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);

    return retval;
}

static int libbalsa_mailbox_mh_open_temp (const gchar *dest_path,
					  char **name_used);
/* Called with mailbox locked. */
static void
libbalsa_mailbox_mh_check(LibBalsaMailbox * mailbox)
{
    struct stat st, st_sequences;
    LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(mailbox);
    const gchar *path =
        libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);
    int modified = 0;
    guint renumber, msgno;
    struct message_info *msg_info;
    time_t mtime;

    if (stat(path, &st) == -1)
	return;

    /* create .mh_sequences when there isn't one. */
    if (stat(mh->sequences_filename, &st_sequences) == -1) {
	if (errno == ENOENT) {
	    gchar *name_used;
	    int fd = libbalsa_mailbox_mh_open_temp(path, &name_used);
	    if (fd != -1) {
		close(fd);
		if (libbalsa_safe_rename(name_used,
					 mh->sequences_filename) == -1)
		    unlink(name_used);
		g_free(name_used);
	    }
	    if (stat(mh->sequences_filename, &st_sequences) == -1)
		modified = 1;
	} else
	    modified = 1;
    }

    mtime = libbalsa_mailbox_get_mtime(mailbox);
    if (mtime == 0)
	/* First check--just cache the mtime. */
        libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);
    else if (st.st_mtime > mtime)
	modified = 1;

    if (mh->mtime_sequences == 0)
	/* First check--just cache the mtime. */
	mh->mtime_sequences = st_sequences.st_mtime;
    else if (st_sequences.st_mtime > mh->mtime_sequences)
	modified = 1;

    if (!modified)
	return;

    libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);
    mh->mtime_sequences = st_sequences.st_mtime;

    if (!MAILBOX_OPEN(mailbox)) {
	libbalsa_mailbox_set_unread_messages_flag(mailbox,
						  lbm_mh_check(mh, path));
	return;
    }

    /* Was any message removed? */
    renumber = mh->msgno_2_msg_info->len + 1;
    for (msgno = 1; msgno <= mh->msgno_2_msg_info->len; ) {
	gchar *base_name, *filename;

	msg_info = lbm_mh_message_info_from_msgno(mh, msgno);
	base_name = MH_BASENAME(msg_info);
	filename = g_build_filename(path, base_name, NULL);
	g_free(base_name);
	if (access(filename, F_OK) == 0)
	    msgno++;
	else {
	    g_ptr_array_remove(mh->msgno_2_msg_info, msg_info);
	    g_hash_table_remove(mh->messages_info, 
		    		GINT_TO_POINTER(msg_info->fileno));
	    libbalsa_mailbox_local_msgno_removed(mailbox, msgno);
	    if (renumber > msgno)
		/* First message that needs renumbering. */
		renumber = msgno;
	}
	g_free(filename);
    }
    for (msgno = renumber; msgno <= mh->msgno_2_msg_info->len; msgno++) {
	msg_info = lbm_mh_message_info_from_msgno(mh, msgno);
	if (msg_info->local_info.message != NULL)
	    libbalsa_message_set_msgno(msg_info->local_info.message, msgno);
    }

    lbm_mh_parse_both(mh);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mh_parent_class)->check != NULL)
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mh_parent_class)->check(mailbox);
}

static void
libbalsa_mailbox_mh_close_mailbox(LibBalsaMailbox * mailbox,
                                  gboolean expunge)
{
    LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(mailbox);
    guint len;

    len = mh->msgno_2_msg_info->len;
    libbalsa_mailbox_mh_sync(mailbox, expunge);
    if (mh->msgno_2_msg_info->len != len)
        libbalsa_mailbox_changed(mailbox);

    g_hash_table_destroy(mh->messages_info);
    mh->messages_info = NULL;

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mh_parent_class)->close_mailbox)
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mh_parent_class)->close_mailbox(mailbox,
                                                            expunge);

    /* Now it's safe to free the message info. */
    g_ptr_array_free(mh->msgno_2_msg_info, TRUE);
    mh->msgno_2_msg_info = NULL;
}

static int
libbalsa_mailbox_mh_open_temp (const gchar *dest_path, char **name_used)
{
    *name_used = g_build_filename(dest_path, "mh-XXXXXX", NULL);
    return g_mkstemp(*name_used);
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
    if (msg_info->local_info.flags == LIBBALSA_MESSAGE_FLAG_INVALID)
	msg_info->local_info.flags = msg_info->orig_flags;
    if (!(msg_info->local_info.flags & flag))
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
    struct line_info unseen, flagged, replied, recent;
    const gchar *path;
    gchar *name_used;
    guint msgno;

    int fd;
    int sequences_fd;
    GMimeStream *temp_stream;
    const gchar* sequences_filename;
    gboolean retval = FALSE;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MH(mailbox), FALSE);
    mh = (LibBalsaMailboxMh *) mailbox;
    g_return_val_if_fail( mh->messages_info != NULL, FALSE );

    sequences_filename = mh->sequences_filename;
    sequences_fd = open(sequences_filename, O_RDONLY);
    if (sequences_fd >= 0)
        libbalsa_lock_file(sequences_filename, sequences_fd, FALSE, TRUE, 1);

    /* Check for new mail before flushing any changes out to disk. */
    libbalsa_mailbox_mh_check(mailbox);

    /* build new sequence file lines */
    unseen.first  = unseen.last = -1;
    unseen.line   = g_mime_stream_mem_new();
    flagged.first = flagged.last = -1;
    flagged.line  = g_mime_stream_mem_new();
    replied.first = replied.last = -1;
    replied.line  = g_mime_stream_mem_new();
    recent.first  = recent.last = -1;
    recent.line   = g_mime_stream_mem_new();

    path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);

    msgno = 1;
    while (msgno <= mh->msgno_2_msg_info->len) {
        struct message_info *msg_info = lbm_mh_message_info_from_msgno(mh, msgno);
	if (msg_info->local_info.flags == LIBBALSA_MESSAGE_FLAG_INVALID)
	    msg_info->local_info.flags = msg_info->orig_flags;
	if (libbalsa_mailbox_get_state(mailbox) == LB_MAILBOX_STATE_CLOSING)
	    msg_info->local_info.flags &= ~LIBBALSA_MESSAGE_FLAG_RECENT;

	if (expunge && (msg_info->local_info.flags
                        & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	    /* MH just moves files out of the way when you delete them */
	    /* chbm: not quite, however this is probably a good move for
	       flag deleted */
	    char *base_name = MH_BASENAME(msg_info);
	    char *orig = g_build_filename(path, base_name, NULL);
	    g_free(base_name);
	    unlink(orig);
	    g_free(orig);
	    /* free old information */
	    g_ptr_array_remove(mh->msgno_2_msg_info, msg_info);
	    g_hash_table_remove(mh->messages_info, 
		    		GINT_TO_POINTER(msg_info->fileno));
	    libbalsa_mailbox_local_msgno_removed(mailbox, msgno);
	} else {
	    lbm_mh_flag_line(msg_info, LIBBALSA_MESSAGE_FLAG_NEW, &unseen);
	    lbm_mh_flag_line(msg_info, LIBBALSA_MESSAGE_FLAG_FLAGGED, &flagged);
	    lbm_mh_flag_line(msg_info, LIBBALSA_MESSAGE_FLAG_REPLIED, &replied);
	    lbm_mh_flag_line(msg_info, LIBBALSA_MESSAGE_FLAG_RECENT, &recent);
	    if ((msg_info->local_info.flags ^ msg_info->orig_flags) &
		LIBBALSA_MESSAGE_FLAG_DELETED) {
		gchar *base_name;
		gchar *old_file;
		gchar *new_file;

		base_name = MH_BASENAME(msg_info);
		old_file = g_build_filename(path, base_name, NULL);
		g_free(base_name);

                msg_info->orig_flags =
                    REAL_FLAGS(msg_info->local_info.flags);

		base_name = MH_BASENAME(msg_info);
		new_file = g_build_filename(path, base_name, NULL);
		g_free(base_name);

		if (libbalsa_safe_rename(old_file, new_file) == -1) {
		    /* FIXME: report error ... */
		}

		g_free(old_file);
		g_free(new_file);
	    } else
                msg_info->orig_flags =
                    REAL_FLAGS(msg_info->local_info.flags);
	    msgno++;
	}
    }
    lbm_mh_print_line(&unseen);
    lbm_mh_print_line(&flagged);
    lbm_mh_print_line(&replied);
    lbm_mh_print_line(&recent);

    /* Renumber */
    for (msgno = 1; msgno <= mh->msgno_2_msg_info->len; msgno++) {
        struct message_info *msg_info = lbm_mh_message_info_from_msgno(mh, msgno);
	if (msg_info->local_info.message != NULL)
	    libbalsa_message_set_msgno(msg_info->local_info.message, msgno);
    }

    /* open tempfile */
    path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);
    fd = libbalsa_mailbox_mh_open_temp(path, &name_used);
    if (fd == -1)
    {
        g_free(name_used);
	g_object_unref(unseen.line);
	g_object_unref(flagged.line);
	g_object_unref(replied.line);
	g_object_unref(recent.line);
        if (sequences_fd >= 0)
            libbalsa_unlock_file(sequences_filename, sequences_fd, 1);
        g_warning("MH sync “%s”: cannot open temp file.", path);
	return retval;
    }
    temp_stream = g_mime_stream_fs_new(fd);

    if (sequences_fd >= 0) {
        /* copy unknown sequences */
        GMimeStream *gmime_stream;
        GMimeStream *gmime_stream_buffer;
        GByteArray *line;

        gmime_stream = g_mime_stream_fs_new(sequences_fd);
        gmime_stream_buffer =
            g_mime_stream_buffer_new(gmime_stream,
                                     GMIME_STREAM_BUFFER_BLOCK_READ);
        g_object_unref(gmime_stream);
        line = g_byte_array_new();
        do {
            gchar *buf;

            g_byte_array_set_size(line, 0);
            g_mime_stream_buffer_readln(gmime_stream_buffer, line);
            buf = (gchar *) line->data;
            if (buf &&
                !g_str_has_prefix(buf, LibBalsaMailboxMhUnseen) &&
                !g_str_has_prefix(buf, LibBalsaMailboxMhFlagged) &&
                !g_str_has_prefix(buf, LibBalsaMailboxMhReplied) &&
                !g_str_has_prefix(buf, LibBalsaMailboxMhRecent)) {
                /* unknown sequence */
                g_mime_stream_write(temp_stream, buf, line->len);
            }
        } while (!g_mime_stream_eos(gmime_stream_buffer));
        g_object_unref(gmime_stream_buffer);
        g_byte_array_free(line, TRUE);
    }

    /* write sequences */
    if (!lbm_mh_finish_line(&unseen, temp_stream, LibBalsaMailboxMhUnseen) ||
	!lbm_mh_finish_line(&flagged, temp_stream, LibBalsaMailboxMhFlagged) ||
	!lbm_mh_finish_line(&replied, temp_stream, LibBalsaMailboxMhReplied) ||
	!lbm_mh_finish_line(&recent, temp_stream, LibBalsaMailboxMhRecent)) {
	g_object_unref(temp_stream);
	unlink(name_used);
	g_free(name_used);
	g_object_unref(unseen.line);
	g_object_unref(flagged.line);
	g_object_unref(replied.line);
	g_object_unref(recent.line);
        if (sequences_fd >= 0)
            libbalsa_unlock_file(sequences_filename, sequences_fd, 1);
        g_warning("MH sync “%s”: error finishing sequences line.", path);
	return retval;
    }

    /* close tempfile */
    g_object_unref(temp_stream);

    /* unlink '.mh_sequences' file */
    unlink(sequences_filename);

    /* rename tempfile to '.mh_sequences' */
    retval = (libbalsa_safe_rename(name_used, sequences_filename) != -1);
    if (!retval)
        g_warning("MH sync “%s”: error renaming sequences file.", path);
    if (!retval)
	unlink (name_used);

    /* Record the mtimes; we'll just use the current time--someone else
     * might have changed something since we did, despite the file
     * locking, but we'll find out eventually. */
    libbalsa_mailbox_set_mtime(mailbox, mh->mtime_sequences = time(NULL));

    g_free(name_used);
    g_object_unref(unseen.line);
    g_object_unref(flagged.line);
    g_object_unref(replied.line);
    g_object_unref(recent.line);
    if (sequences_fd >= 0)
        libbalsa_unlock_file(sequences_filename, sequences_fd, 1);
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

static gboolean
libbalsa_mailbox_mh_fetch_message_structure(LibBalsaMailbox * mailbox,
					    LibBalsaMessage * message,
					    LibBalsaFetchFlag flags)
{
    GMimeMessage *mime_msg;

    mime_msg = libbalsa_message_get_mime_message(message);

    if (mime_msg == NULL) {
	struct message_info *msg_info;
	gchar *base_name;

	msg_info =
	    lbm_mh_message_info_from_msgno(LIBBALSA_MAILBOX_MH(mailbox),
					   libbalsa_message_get_msgno(message));

	base_name = MH_BASENAME(msg_info);
	mime_msg = libbalsa_mailbox_local_get_mime_message(mailbox, base_name, NULL);
	g_free(base_name);

        libbalsa_message_set_mime_message(message, mime_msg);

	if (mime_msg != NULL) {
	    g_mime_object_remove_header(GMIME_OBJECT(mime_msg),
				        "Status");
	    g_mime_object_remove_header(GMIME_OBJECT(mime_msg),
				        "X-Status");
            g_object_unref(mime_msg);
	}
    }

    return LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_mh_parent_class)->
        fetch_message_structure(mailbox, message, flags);
}

/* Update .mh_sequences when a new message is added to the mailbox;
 * we'll just add new lines and let the next sync merge them with any
 * existing lines. */
static void
lbm_mh_update_sequences(LibBalsaMailboxMh * mh, gint fileno,
			LibBalsaMessageFlag flags)
{
    FILE *fp;

    fp = fopen(mh->sequences_filename, "a");

    if (!fp)
	return;

    if (flags & LIBBALSA_MESSAGE_FLAG_NEW)
	fprintf(fp, "unseen: %d\n", fileno);
    if (flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	fprintf(fp, "flagged: %d\n", fileno);
    if (flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
	fprintf(fp, "replied: %d\n", fileno);
    if (flags & LIBBALSA_MESSAGE_FLAG_RECENT)
	fprintf(fp, "recent: %d\n", fileno);
    fclose(fp);
}

/* Called with mailbox locked. */
static gboolean
lbm_mh_add_message(LibBalsaMailboxLocal * local,
                   GMimeStream          * stream,
                   LibBalsaMessageFlag    flags,
                   GError              ** err)
{
    LibBalsaMailboxMh *mh;
    const char *path;
    char *name_used;
    int fd;
    GMimeStream *out_stream;
    GMimeFilter *crlffilter;
    int fileno;
    int retries;
    GMimeStream *in_stream;

    mh = LIBBALSA_MAILBOX_MH(local);

    /* Make sure we know the highest message number: */
    lbm_mh_parse_mailbox(mh, FALSE);

    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(local);
    fd = libbalsa_mailbox_mh_open_temp(path, &name_used);
    if (fd == -1) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("Cannot create message"));
	g_free(name_used);
	return FALSE;
    }
    out_stream = g_mime_stream_fs_new(fd);

    crlffilter = g_mime_filter_dos2unix_new(FALSE);
    in_stream = g_mime_stream_filter_new(stream);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(in_stream), crlffilter);
    g_object_unref(crlffilter);

    libbalsa_mime_stream_shared_lock(stream);
    if (g_mime_stream_write_to_stream(in_stream, out_stream) == -1) {
        libbalsa_mime_stream_shared_unlock(stream);
        g_object_unref(in_stream);
	g_object_unref(out_stream);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    _("Data copy error"));
	unlink(name_used);
	g_free(name_used);
	return FALSE;
    }
    g_object_unref(out_stream);
    libbalsa_mime_stream_shared_unlock(stream);
    g_object_unref(in_stream);

    fileno = mh->last_fileno; 
    retries = 10;
    do {
	/* rename tempfile to message-number-name */
	char *new_filename;
	gint rename_status;

	new_filename = g_strdup_printf("%s/%d", path, ++fileno);
	rename_status = libbalsa_safe_rename(name_used, new_filename);
	g_free(new_filename);
	if (rename_status != -1)
	    break;
	
	if (errno != EEXIST) {
            g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_APPEND_ERROR,
                        _("Message rename error"));
	    unlink (name_used);
	    g_free(name_used);
	    return FALSE;
	}
    } while (--retries > 0);
    g_free(name_used);

    if (retries == 0) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_APPEND_ERROR,
                    "Too high activity?");
	return FALSE;
    }
    mh->last_fileno = fileno;

    lbm_mh_update_sequences(mh, fileno,
                            flags | LIBBALSA_MESSAGE_FLAG_RECENT);

    return TRUE;
}

static guint
libbalsa_mailbox_mh_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMh *mh = (LibBalsaMailboxMh *) mailbox;

    return mh->msgno_2_msg_info ? mh->msgno_2_msg_info->len : 0;
}
