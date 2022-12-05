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


#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libbalsa.h"
#include "libbalsa-conf.h"
#include "misc.h"
#include "libbalsa_private.h"
#include "mime-stream-shared.h"
#include <glib/gi18n.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mbox-maildir"

struct message_info {
    LibBalsaMailboxLocalMessageInfo local_info;
    LibBalsaMessageFlag orig_flags;     /* Has only real flags */
    char *key;
    const char *subdir;
    char *filename;

    /* The message's order when parsing; needed for saving the message
     * tree in a form that will match the msgnos when the mailbox is
     * reopened. */
    guint fileno;
};
#define REAL_FLAGS(flags) ((flags) & LIBBALSA_MESSAGE_FLAGS_REAL)
#define FLAGS_REALLY_DIFFER(orig_flags, flags) \
    ((((orig_flags) ^ (flags)) & LIBBALSA_MESSAGE_FLAGS_REAL) != 0)
#define FLAGS_CHANGED(msg_info) \
    FLAGS_REALLY_DIFFER(msg_info->orig_flags, msg_info->local_info.flags)

/* Object class method */
static void libbalsa_mailbox_maildir_finalize(GObject * object);

/* Mailbox class methods */
static void libbalsa_mailbox_maildir_load_config(LibBalsaMailbox * mailbox,
						 const gchar * prefix);

static GMimeStream *libbalsa_mailbox_maildir_get_message_stream(LibBalsaMailbox *
							   mailbox,
								guint msgno,
								gboolean peek);

static gboolean libbalsa_mailbox_maildir_open(LibBalsaMailbox * mailbox,
					      GError **err);
static void libbalsa_mailbox_maildir_close_mailbox(LibBalsaMailbox *
                                                   mailbox,
                                                   gboolean expunge);
static void libbalsa_mailbox_maildir_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_maildir_sync(LibBalsaMailbox * mailbox,
                                              gboolean expunge);
static gboolean
libbalsa_mailbox_maildir_fetch_message_structure(LibBalsaMailbox * mailbox,
						 LibBalsaMessage * message,
						 LibBalsaFetchFlag flags);
static guint
libbalsa_mailbox_maildir_total_messages(LibBalsaMailbox * mailbox);

/* LibBalsaMailboxLocal class methods */
static gint lbm_maildir_check_files(const gchar * path, gboolean create);
static void lbm_maildir_set_path(LibBalsaMailboxLocal * local,
                                 const gchar * path);
static void lbm_maildir_remove_files(LibBalsaMailboxLocal * local);
static guint lbm_maildir_fileno(LibBalsaMailboxLocal * local, guint msgno);
static LibBalsaMailboxLocalMessageInfo
    *lbm_maildir_get_info(LibBalsaMailboxLocal * local, guint msgno);
static LibBalsaMailboxLocalAddMessageFunc lbm_maildir_add_message;

/* util functions */
static struct message_info *message_info_from_msgno(LibBalsaMailboxMaildir
                                                    * mdir, guint msgno);
static LibBalsaMessageFlag parse_filename(const gchar *subdir,
					  const gchar *filename);
static void free_message_info(struct message_info *msg_info);
static int libbalsa_mailbox_maildir_open_temp (const gchar *dest_path,
					  char **name_used);

struct _LibBalsaMailboxMaildir {
    LibBalsaMailboxLocal parent;

    GHashTable* messages_info;
    GPtrArray* msgno_2_msg_info;
    gchar *curdir;
    gchar *newdir;
    gchar *tmpdir;
};

G_DEFINE_TYPE(LibBalsaMailboxMaildir,
              libbalsa_mailbox_maildir,
              LIBBALSA_TYPE_MAILBOX_LOCAL)

static void
libbalsa_mailbox_maildir_class_init(LibBalsaMailboxMaildirClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    object_class->finalize = libbalsa_mailbox_maildir_finalize;

    libbalsa_mailbox_class->load_config =
	        libbalsa_mailbox_maildir_load_config;
    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_maildir_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_maildir_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_maildir_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_maildir_sync;
    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_maildir_close_mailbox;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_maildir_fetch_message_structure;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_maildir_total_messages;

    libbalsa_mailbox_local_class->check_files  = lbm_maildir_check_files;
    libbalsa_mailbox_local_class->set_path     = lbm_maildir_set_path;
    libbalsa_mailbox_local_class->remove_files = lbm_maildir_remove_files;
    libbalsa_mailbox_local_class->fileno       = lbm_maildir_fileno;
    libbalsa_mailbox_local_class->get_info     = lbm_maildir_get_info;
    libbalsa_mailbox_local_class->add_message  = lbm_maildir_add_message;
}

static void
libbalsa_mailbox_maildir_init(LibBalsaMailboxMaildir * mdir)
{
}

static gint
lbm_maildir_check_files(const gchar * path, gboolean create)
{
    g_return_val_if_fail(path != NULL, -1);

    if (access(path, F_OK) == 0) {
        /* File exists. Check if it is a maildir... */
        if (libbalsa_mailbox_type_from_path(path) !=
            LIBBALSA_TYPE_MAILBOX_MAILDIR) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Mailbox %s does not appear to be a Maildir mailbox."),
                                 path);
            return -1;
        }
    } else if (create) {
        char tmp[_POSIX_PATH_MAX];

        if (mkdir(path, S_IRWXU)) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Could not create a Maildir directory at %s (%s)"),
                                 path, g_strerror(errno));
            return -1;
        }

        snprintf(tmp, sizeof(tmp), "%s/cur", path);
        if (mkdir(tmp, S_IRWXU)) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Could not create a Maildir at %s (%s)"),
                                 path, g_strerror(errno));
            rmdir(path);
            return -1;
        }

        snprintf(tmp, sizeof(tmp), "%s/new", path);
        if (mkdir(tmp, S_IRWXU)) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Could not create a Maildir at %s (%s)"),
                                 path, g_strerror(errno));
            snprintf(tmp, sizeof(tmp), "%s/cur", path);
            rmdir(tmp);
            rmdir(path);
            return -1;
        }

        snprintf(tmp, sizeof(tmp), "%s/tmp", path);
        if (mkdir(tmp, S_IRWXU)) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Could not create a Maildir at %s (%s)"),
                                 path, g_strerror(errno));
            snprintf(tmp, sizeof(tmp), "%s/cur", path);
            rmdir(tmp);
            snprintf(tmp, sizeof(tmp), "%s/new", path);
            rmdir(tmp);
            rmdir(path);
            return -1;
        }
    } else
        return -1;

    return 0;
}

static void
lbm_maildir_set_subdirs(LibBalsaMailboxMaildir * mdir, const gchar * path)
{
    g_free(mdir->curdir);
    mdir->curdir = g_build_filename(path, "cur", NULL);
    g_free(mdir->newdir);
    mdir->newdir = g_build_filename(path, "new", NULL);
    g_free(mdir->tmpdir);
    mdir->tmpdir = g_build_filename(path, "tmp", NULL);
}

static void
lbm_maildir_set_path(LibBalsaMailboxLocal * local, const gchar * path)
{
    lbm_maildir_set_subdirs(LIBBALSA_MAILBOX_MAILDIR(local), path);
}

LibBalsaMailbox *
libbalsa_mailbox_maildir_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MAILDIR, NULL);

    if (libbalsa_mailbox_local_set_path(LIBBALSA_MAILBOX_LOCAL(mailbox),
                                        path, create) != 0) {
        g_object_unref(mailbox);
        return NULL;
    }

    return mailbox;
}

static void
libbalsa_mailbox_maildir_finalize(GObject * object)
{
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(object);
    g_free(mdir->curdir);
    mdir->curdir = NULL;
    g_free(mdir->newdir);
    mdir->newdir = NULL;
    g_free(mdir->tmpdir);
    mdir->tmpdir = NULL;

    G_OBJECT_CLASS(libbalsa_mailbox_maildir_parent_class)->finalize(object);
}

static void
libbalsa_mailbox_maildir_load_config(LibBalsaMailbox * mailbox,
				     const gchar * prefix)
{
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    gchar *path;

    path = libbalsa_conf_get_string("Path");
    lbm_maildir_set_subdirs(mdir, path);
    g_free(path);

    LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_maildir_parent_class)->load_config(mailbox, prefix);
}

static GMimeStream *
libbalsa_mailbox_maildir_get_message_stream(LibBalsaMailbox * mailbox,
					    guint msgno, gboolean peek)
{
    struct message_info *msg_info;

    g_return_val_if_fail(MAILBOX_OPEN(mailbox), NULL);

    msg_info =
        message_info_from_msgno((LibBalsaMailboxMaildir *) mailbox, msgno);
    if (!msg_info)
	return NULL;

    return libbalsa_mailbox_local_get_message_stream(mailbox,
						     msg_info->subdir,
						     msg_info->filename);
}

static void
lbm_maildir_remove_files(LibBalsaMailboxLocal *mailbox)
{
    const gchar* path;
    GError *error = NULL;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox));

    path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);
    g_debug("DELETE MAILDIR");

    if (!libbalsa_delete_directory(path, &error)) {
    	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
    			     _("Could not remove “%s”: %s"),
    			     path, error->message);
    	g_clear_error(&error);
    }
    LIBBALSA_MAILBOX_LOCAL_CLASS(libbalsa_mailbox_maildir_parent_class)->remove_files(mailbox);
}

static LibBalsaMessageFlag parse_filename(const gchar *subdir,
					  const gchar *filename)
{
    const gchar *p;
    LibBalsaMessageFlag flags;

    flags = LIBBALSA_MESSAGE_FLAG_NEW;
    if (strcmp(subdir, "cur") != 0)
	flags |= LIBBALSA_MESSAGE_FLAG_RECENT;

    if ((p = strrchr (filename, ':')) != NULL && strncmp (p + 1, "2,", 2) == 0)
    {
	p += 3;
	while (*p)
	{
	    switch (*p)
	    {
		case 'F':
		    flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
		    break;
	
		case 'S': /* seen */
		    flags &= ~LIBBALSA_MESSAGE_FLAG_NEW;
		    break;

		case 'R': /* replied */
		    flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
		    break;

		case 'T': /* trashed */
		    flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
		    break;
	    }
	    p++;
	}
    }
    return flags;
}

static void
lbm_maildir_parse(LibBalsaMailboxMaildir *mdir,
                  const gchar            *subdir,
                  guint                  *fileno)
{
    const gchar *local_path;
    gchar *path;
    GDir *dir;
    GHashTable *messages_info;
    GPtrArray *msgno_2_msg_info;
    const gchar *filename;
    gchar *key;
    gchar *p;
    LibBalsaMessageFlag flags;
    LibBalsaMailbox *mailbox = (LibBalsaMailbox *) mdir;

    local_path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);
    path = g_build_filename(local_path, subdir, NULL);
    dir = g_dir_open(path, 0, NULL);
    g_free(path);
    if (dir == NULL)
	return;

    messages_info = mdir->messages_info;
    msgno_2_msg_info = mdir->msgno_2_msg_info;
    while ((filename = g_dir_read_name(dir)) != NULL)
    {
	struct message_info *msg_info;
	if (filename[0] == '.')
	    continue;

	key = g_strdup(filename);
	/* strip flags of filename */
	if ((p = strrchr(key, ':')) && !strncmp(p, ":2,", 3))
	    *p = '\0';

	flags = parse_filename(subdir, filename);
	msg_info = g_hash_table_lookup(messages_info, key);
	if (msg_info) {
	    g_free(key);
	    g_free(msg_info->filename);
	    msg_info->filename = g_strdup(filename);
	    if (FLAGS_REALLY_DIFFER(msg_info->orig_flags, flags)) {
		g_debug("Message flags for “%s” changed",
                          msg_info->key);
		msg_info->orig_flags = flags;
	    }
	} else {
	    msg_info = g_new0(struct message_info, 1);
	    g_hash_table_insert(messages_info, key, msg_info);
	    g_ptr_array_add(msgno_2_msg_info, msg_info);
	    msg_info->key=key;
	    msg_info->filename=g_strdup(filename);
	    msg_info->local_info.flags = msg_info->orig_flags = flags;
	    msg_info->fileno = 0;
	}
	msg_info->subdir = subdir;
        if (!msg_info->fileno)
            /* First time we saw this key. */
	    msg_info->fileno = ++*fileno;
    }
    g_dir_close(dir);
}

static void
lbm_maildir_parse_subdirs(LibBalsaMailboxMaildir * mdir)
{
    guint msgno, fileno = 0;

    for (msgno = mdir->msgno_2_msg_info->len; msgno > 0; --msgno) {
        struct message_info *msg_info =
            message_info_from_msgno(mdir, msgno);
        msg_info->fileno = 0;
    }

    lbm_maildir_parse(mdir, "cur", &fileno);
    /* We parse "new" after "cur", so that any recent messages will have
     * higher msgnos than any current messages. That ensures that the
     * message tree saved by LibBalsaMailboxLocal is still valid, and
     * that the new messages will be inserted correctly into the tree by
     * libbalsa_mailbox_local_add_messages. */
    lbm_maildir_parse(mdir, "new", &fileno);
}

static gboolean
libbalsa_mailbox_maildir_open(LibBalsaMailbox * mailbox, GError **err)
{
    struct stat st;
    LibBalsaMailboxMaildir *mdir;
    const gchar* path;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), FALSE);

    mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);

    if (stat(path, &st) == -1) {
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Mailbox does not exist."));
	return FALSE;
    }

    mdir->messages_info = g_hash_table_new_full(g_str_hash, g_str_equal,
				  NULL, (GDestroyNotify)free_message_info);
    mdir->msgno_2_msg_info = g_ptr_array_new();

    if (stat(mdir->tmpdir, &st) != -1)
	libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);

    libbalsa_mailbox_set_readonly(mailbox,
	!(access(mdir->curdir, W_OK) == 0 &&
          access(mdir->newdir, W_OK) == 0 &&
          access(mdir->tmpdir, W_OK) == 0));

    libbalsa_mailbox_clear_unread_messages(mailbox);
    lbm_maildir_parse_subdirs(mdir);
    g_debug("%s: Opening %s Refcount: %d",
	    __func__, libbalsa_mailbox_get_name(mailbox),
            libbalsa_mailbox_get_open_ref(mailbox));
    return TRUE;
}

/* Check for a new message in subdir. */
static gboolean
lbm_maildir_check(const gchar * subdir)
{
    GDir *dir;
    const gchar *filename;
    const gchar *p;
    gboolean retval = FALSE;

    if ((dir = g_dir_open(subdir, 0, NULL))) {
	while ((filename = g_dir_read_name(dir))) {
	    if (*filename != '.' && (!(p = strstr(filename, ":2,"))
				     || !(strchr(p + 3, 'S')
					  || strchr(p + 3, 'T')))) {
		/* One new message is enough. */
		retval = TRUE;
		break;
	    }
	}
	g_dir_close(dir);
    }

    return retval;
}

/* Called with mailbox locked. */
static void
libbalsa_mailbox_maildir_check(LibBalsaMailbox * mailbox)
{
    struct stat st;
    LibBalsaMailboxMaildir *mdir;
    guint renumber, msgno;
    struct message_info *msg_info;
    const gchar *path;
    time_t mtime;

    g_assert(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox));

    mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);

    if (stat(mdir->tmpdir, &st) == -1)
	return;

    if ((mtime = libbalsa_mailbox_get_mtime(mailbox)) == 0) {
	/* First check--just cache the mtime. */
	libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);
        return;
    }
    if (st.st_mtime == mtime)
	return;

    libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);

    if (!MAILBOX_OPEN(mailbox)) {
	libbalsa_mailbox_set_unread_messages_flag(mailbox,
						  lbm_maildir_check(mdir->
								    newdir)
						  ||
						  lbm_maildir_check(mdir->
								    curdir));
	return;
    }

    /* Was any message removed? */
    path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);
    renumber = mdir->msgno_2_msg_info->len + 1;
    for (msgno = 1; msgno <= mdir->msgno_2_msg_info->len; ) {
	gchar *filename;

        msg_info = message_info_from_msgno(mdir, msgno);
	filename = g_build_filename(path, msg_info->subdir,
				    msg_info->filename, NULL);
	if (access(filename, F_OK) == 0)
	    msgno++;
	else {
	    g_ptr_array_remove(mdir->msgno_2_msg_info, msg_info);
	    g_hash_table_remove(mdir->messages_info, msg_info->key);
	    libbalsa_mailbox_local_msgno_removed(mailbox, msgno);
	    if (renumber > msgno)
		/* First message that needs renumbering. */
		renumber = msgno;
	}
	g_free(filename);
    }
    for (msgno = renumber; msgno <= mdir->msgno_2_msg_info->len; msgno++) {
	msg_info = message_info_from_msgno(mdir, msgno);
	if (msg_info->local_info.message != NULL)
            libbalsa_message_set_msgno(msg_info->local_info.message, msgno);
    }

    lbm_maildir_parse_subdirs(mdir);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_maildir_parent_class)->check != NULL)
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_maildir_parent_class)->check(mailbox);
}

static void
free_message_info(struct message_info *msg_info)
{
    if (!msg_info)
	return;
    g_free(msg_info->key);
    g_free(msg_info->filename);
    if (msg_info->local_info.message != NULL) {
        libbalsa_message_set_mailbox(msg_info->local_info.message, NULL);
        libbalsa_message_set_msgno(msg_info->local_info.message, 0);
	g_object_remove_weak_pointer(G_OBJECT(msg_info->local_info.message),
				     (gpointer *) & msg_info->local_info.message);
    }
    g_free(msg_info);
}

static void
libbalsa_mailbox_maildir_close_mailbox(LibBalsaMailbox * mailbox,
                                       gboolean expunge)
{
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    guint len;

    len = mdir->msgno_2_msg_info->len;
    libbalsa_mailbox_maildir_sync(mailbox, expunge);
    if (mdir->msgno_2_msg_info->len != len)
        libbalsa_mailbox_changed(mailbox);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_maildir_parent_class)->close_mailbox)
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_maildir_parent_class)->close_mailbox(mailbox,
                                                            expunge);

    /* Now it's safe to free the message info. */
    g_hash_table_destroy(mdir->messages_info);
    mdir->messages_info = NULL;

    g_ptr_array_free(mdir->msgno_2_msg_info, TRUE);
    mdir->msgno_2_msg_info = NULL;
}

static gchar *
lbm_mdir_get_key(void)
{
    static int counter;

    return g_strdup_printf("%s-%d-%d", g_get_user_name(), getpid(),
                           counter++);
}

static int
libbalsa_mailbox_maildir_open_temp(const gchar *dest_path,
                                   char **name_used)
{
    int fd;
    gchar *filename;

    do {
	gchar *key = lbm_mdir_get_key();
	filename = g_build_filename(dest_path, "tmp", key, NULL);
	g_free(key);
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

/* Sync: flush flags and expunge. */
static gboolean
maildir_sync_add(struct message_info *msg_info, const gchar * path)
{
    gchar new_flags[10];
    int len;
    const gchar *subdir;
    gchar *filename;
    gchar *new;
    gchar *orig;
    gboolean retval = TRUE;

    len = 0;

    if ((msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_FLAGGED) != 0)
	new_flags[len++] = 'F';
    if ((msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_NEW) == 0)
	new_flags[len++] = 'S';
    if ((msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_REPLIED) != 0)
	new_flags[len++] = 'R';
    if ((msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_DELETED) != 0)
	new_flags[len++] = 'T';

    new_flags[len] = '\0';

    subdir = msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_RECENT ? "new" : "cur";
    filename =
        g_strconcat(msg_info->key, (len ? ":2," : ""), new_flags, NULL);
    new = g_build_filename(path, subdir, filename, NULL);
    orig =
        g_build_filename(path, msg_info->subdir, msg_info->filename, NULL);
    if (strcmp(orig, new)) {
        while (g_file_test(new, G_FILE_TEST_EXISTS)) {
	    g_debug("File “%s” exists, requesting new key.", new);
            g_free(msg_info->key);
            msg_info->key = lbm_mdir_get_key();
            g_free(filename);
            filename =
                g_strconcat(msg_info->key, (len ? ":2," : ""), new_flags,
                            NULL);
            g_free(new);
            new = g_build_filename(path, subdir, filename, NULL);
        }
	if (rename(orig, new) >= 0) /* FIXME: change to safe_rename??? */
	    msg_info->subdir = subdir;
	else {
            g_debug("Rename “%s” “%s”: %s", orig, new,
                      g_strerror(errno));
	    retval = FALSE;
	}
    }
    g_free(orig);
    g_free(new);
    g_free(msg_info->filename);
    msg_info->filename = filename;
    msg_info->orig_flags = REAL_FLAGS(msg_info->local_info.flags);

    return retval;
}

static gboolean
libbalsa_mailbox_maildir_sync(LibBalsaMailbox * mailbox, gboolean expunge)
{
    /*
     * foreach message_info
     *  mark_move if subdir == "new"
     *  mark_move if flags changed
     *  move/rename and record change if mark_move
     * record mtime of dirs
     */
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    const gchar *path = libbalsa_mailbox_local_get_path((LibBalsaMailboxLocal *) mailbox);
    GSList *removed_list = NULL;
    gboolean ok = TRUE;
    GSList *l;
    guint renumber, msgno;
    struct message_info *msg_info;
    guint changes = 0;

    for (msgno = 1; msgno <= mdir->msgno_2_msg_info->len; msgno++) {
	msg_info = message_info_from_msgno(mdir, msgno);

	if (expunge && (msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	    /* skip this block if the message should only be renamed */
	    gchar *orig = g_build_filename(path, msg_info->subdir,
                                           msg_info->filename, NULL);
	    unlink (orig);
	    g_free(orig);
	    removed_list =
		g_slist_prepend(removed_list, GUINT_TO_POINTER(msgno));
	    ++changes;
	    continue;
	}

	if (libbalsa_mailbox_get_state(mailbox) == LB_MAILBOX_STATE_CLOSING)
	    msg_info->local_info.flags &= ~LIBBALSA_MESSAGE_FLAG_RECENT;
	if (((msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_RECENT)
             && strcmp(msg_info->subdir, "new") != 0)
            || (!(msg_info->local_info.flags & LIBBALSA_MESSAGE_FLAG_RECENT)
                && strcmp(msg_info->subdir, "cur") != 0)
            || FLAGS_CHANGED(msg_info)) {
	    if (!maildir_sync_add(msg_info, path))
		ok = FALSE;
	    ++changes;
	}
    }

    if (!ok) {
	g_slist_free(removed_list);
	return FALSE;
    }

    renumber = mdir->msgno_2_msg_info->len + 1;
    for (l = removed_list; l; l = l->next) {
	msgno = GPOINTER_TO_UINT(l->data);
	if (renumber > msgno)
	    renumber = msgno;
	msg_info = message_info_from_msgno(mdir, msgno);

	g_ptr_array_remove(mdir->msgno_2_msg_info, msg_info);
	libbalsa_mailbox_local_msgno_removed(mailbox, msgno);
	/* This will free removed: */
	g_hash_table_remove(mdir->messages_info, msg_info->key);
    }
    g_slist_free(removed_list);
    for (msgno = renumber; msgno <= mdir->msgno_2_msg_info->len; msgno++) {
	msg_info = message_info_from_msgno(mdir, msgno);
	if (msg_info->local_info.message != NULL)
	    libbalsa_message_set_msgno(msg_info->local_info.message, msgno);
    }

    if (changes) {              /* Record mtime of dir. */
        struct stat st;

        /* Reparse, to get the fileno entries right. */
        lbm_maildir_parse_subdirs(mdir);
        libbalsa_mailbox_set_msg_tree_changed(mailbox, TRUE);

        if (stat(mdir->tmpdir, &st) == 0)
            libbalsa_mailbox_set_mtime(mailbox, st.st_mtime);
    }

    return TRUE;
}

static struct message_info *
message_info_from_msgno(LibBalsaMailboxMaildir * mdir, guint msgno)
{
    g_assert(msgno > 0 && msgno <= mdir->msgno_2_msg_info->len);

    return (struct message_info *) g_ptr_array_index(mdir->
                                                     msgno_2_msg_info,
                                                     msgno - 1);
}


static gboolean
libbalsa_mailbox_maildir_fetch_message_structure(LibBalsaMailbox * mailbox,
						 LibBalsaMessage * message,
						 LibBalsaFetchFlag flags)
{
    GMimeMessage *mime_message;

    mime_message = libbalsa_message_get_mime_message(message);

    if (mime_message == NULL) {
	struct message_info *msg_info =
            message_info_from_msgno((LibBalsaMailboxMaildir *) mailbox,
                                    libbalsa_message_get_msgno(message));
        mime_message =
	    libbalsa_mailbox_local_get_mime_message(mailbox,
						    msg_info->subdir,
						    msg_info->filename);
        libbalsa_message_set_mime_message(message, mime_message);
        if (mime_message != NULL)
            g_object_unref(mime_message);
    }

    return LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_maildir_parent_class)->
        fetch_message_structure(mailbox, message, flags);
}

static guint
lbm_maildir_fileno(LibBalsaMailboxLocal * local, guint msgno)
{
    struct message_info *msg_info;

    if (!msgno)
        return 0;

    msg_info =
        message_info_from_msgno((LibBalsaMailboxMaildir *) local, msgno);

    return msg_info->fileno;
}

static LibBalsaMailboxLocalMessageInfo *
lbm_maildir_get_info(LibBalsaMailboxLocal * local, guint msgno)
{
    struct message_info *msg_info;

    msg_info =
        message_info_from_msgno((LibBalsaMailboxMaildir *) local, msgno);

    return &msg_info->local_info;
}

/* Called with mailbox locked. */
static gboolean
lbm_maildir_add_message(LibBalsaMailboxLocal * local,
                        GMimeStream          * stream,
                        LibBalsaMessageFlag    flags,
                        GError              ** err)
{
    LibBalsaMailbox *mailbox = (LibBalsaMailbox *) local;
    const char *path;
    char *tmp;
    int fd;
    GMimeStream *out_stream;
    GMimeStream *in_stream;
    GMimeFilter *crlffilter;
    char *new_filename;
    struct message_info *msg_info;
    gint retval;
    time_t mtime;

    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(local);
    fd = libbalsa_mailbox_maildir_open_temp(path, &tmp);
    if (fd == -1)
	return FALSE;
    out_stream = g_mime_stream_fs_new(fd);

    in_stream = g_mime_stream_filter_new(stream);
    crlffilter = g_mime_filter_dos2unix_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(in_stream), crlffilter);
    g_object_unref(crlffilter);
 
    libbalsa_mime_stream_shared_lock(stream);
    retval = g_mime_stream_write_to_stream(in_stream, out_stream);
    libbalsa_mime_stream_shared_unlock(stream);
    g_object_unref(in_stream);
    g_object_unref(out_stream);

    if (retval < 0) {
	unlink (tmp);
	g_free(tmp);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_COPY_ERROR,
                    _("Data copy error"));
	return FALSE;
    }

    new_filename = strrchr(tmp, '/');
    if (new_filename)
	new_filename++;
    else
	new_filename = tmp;
    msg_info = g_new0(struct message_info, 1);
    msg_info->subdir = "tmp";
    msg_info->key = g_strdup(new_filename);
    msg_info->filename = g_strdup(new_filename);
    msg_info->local_info.flags = flags | LIBBALSA_MESSAGE_FLAG_RECENT;
    retval = maildir_sync_add(msg_info, path);
    free_message_info(msg_info);
    g_free(tmp);

    if ((mtime = libbalsa_mailbox_get_mtime(mailbox)) != 0)
	/* If we checked or synced the mailbox less than 1 second ago,
	 * the cached modification time could be the same as the new
	 * modification time, so we'll invalidate the cached time. */
	libbalsa_mailbox_set_mtime(mailbox, --mtime);

    return retval;
}

static guint
libbalsa_mailbox_maildir_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMaildir *mdir = (LibBalsaMailboxMaildir *) mailbox;

    return mdir->msgno_2_msg_info ? mdir->msgno_2_msg_info->len : 0;
}
