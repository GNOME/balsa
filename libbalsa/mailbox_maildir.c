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
#include <libgnome/gnome-config.h>
#include <libgnome/gnome-i18n.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "libbalsa.h"
#include "misc.h"
#include "libbalsa_private.h"

struct message_info {
    char *key;
    const char *subdir;
    char *filename;
    unsigned msgno; /* FIXME: get rid of this field, it is redundant */
    LibBalsaMessageFlag flags;
    LibBalsaMessageFlag orig_flags;
    LibBalsaMessage *message;
};

static LibBalsaMailboxLocalClass *parent_class = NULL;

static void libbalsa_mailbox_maildir_class_init(LibBalsaMailboxMaildirClass *klass);
static void libbalsa_mailbox_maildir_init(LibBalsaMailboxMaildir * mailbox);
static void libbalsa_mailbox_maildir_finalize(GObject * object);
static void libbalsa_mailbox_maildir_load_config(LibBalsaMailbox * mailbox,
						 const gchar * prefix);

static GMimeStream *libbalsa_mailbox_maildir_get_message_stream(LibBalsaMailbox *
							   mailbox,
							   LibBalsaMessage *
							   message);
static void libbalsa_mailbox_maildir_remove_files(LibBalsaMailboxLocal *mailbox);

static gboolean libbalsa_mailbox_maildir_open(LibBalsaMailbox * mailbox,
					      GError **err);
static void libbalsa_mailbox_maildir_close_mailbox(LibBalsaMailbox *
                                                   mailbox,
                                                   gboolean expunge);
static void libbalsa_mailbox_maildir_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_maildir_sync(LibBalsaMailbox * mailbox,
                                              gboolean expunge);
static struct message_info *message_info_from_msgno(
						  LibBalsaMailbox * mailbox,
						  guint msgno);
static LibBalsaMessage *libbalsa_mailbox_maildir_get_message(LibBalsaMailbox * mailbox,
						     guint msgno);
static void
libbalsa_mailbox_maildir_fetch_message_structure(LibBalsaMailbox * mailbox,
						 LibBalsaMessage * message,
						 LibBalsaFetchFlag flags);
static int libbalsa_mailbox_maildir_add_message(LibBalsaMailbox * mailbox,
						LibBalsaMessage * message );
static void libbalsa_mailbox_maildir_change_message_flags(LibBalsaMailbox * mailbox,
						     guint msgno,
						     LibBalsaMessageFlag set,
						     LibBalsaMessageFlag clear);
static guint libbalsa_mailbox_maildir_total_messages(LibBalsaMailbox *
						     mailbox);

/* util functions */
static LibBalsaMessageFlag parse_filename(const gchar *subdir,
					  const gchar *filename);
static void parse_mailbox(LibBalsaMailbox * mailbox, const gchar *subdir);
static void free_message_info(struct message_info *msg_info);
static int libbalsa_mailbox_maildir_open_temp (const gchar *dest_path,
					  char **name_used);

GType
libbalsa_mailbox_maildir_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxMaildirClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_maildir_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxMaildir),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_maildir_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX_LOCAL,
	                           "LibBalsaMailboxMaildir",
                                   &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_maildir_class_init(LibBalsaMailboxMaildirClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

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
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_maildir_get_message;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_maildir_fetch_message_structure;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_maildir_add_message;
    libbalsa_mailbox_class->change_message_flags =
	libbalsa_mailbox_maildir_change_message_flags;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_maildir_total_messages;

    libbalsa_mailbox_local_class->remove_files = 
	libbalsa_mailbox_maildir_remove_files;
    libbalsa_mailbox_local_class->load_message =
        libbalsa_mailbox_maildir_get_message;
}

static void
libbalsa_mailbox_maildir_init(LibBalsaMailboxMaildir * mailbox)
{
}

static void lbm_maildir_set_subdirs(LibBalsaMailboxMaildir * mdir,
                                    const gchar * path);

gint
libbalsa_mailbox_maildir_create(const gchar * path, gboolean create,
                                LibBalsaMailboxMaildir * mdir)
{
    gint exists;
    GType magic_type;

    g_return_val_if_fail( path != NULL, -1);

    exists = access(path, F_OK);
    if ( exists == 0 ) {
	/* File exists. Check if it is a maildir... */
	
	magic_type = libbalsa_mailbox_type_from_path(path);
	if ( magic_type != LIBBALSA_TYPE_MAILBOX_MAILDIR ) {
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				 _("Mailbox %s does not appear to be a Maildir mailbox."), path);
	    return(-1);
	}
    } else {
	if(create) {    
	    char tmp[_POSIX_PATH_MAX];
	    
	    if (mkdir (path, S_IRWXU)) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				     _("Could not create a MailDir directory at %s (%s)"), path, strerror(errno) );
		return (-1);
	    }
	    
	    snprintf (tmp, sizeof (tmp), "%s/cur", path);
	    if (mkdir (tmp, S_IRWXU)) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				     _("Could not create a MailDir at %s (%s)"), path, strerror(errno) );
		rmdir (path);
		return (-1);
	    }
	    
	    snprintf (tmp, sizeof (tmp), "%s/new", path);
	    if (mkdir (tmp, S_IRWXU)) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				     _("Could not create a MailDir at %s (%s)"), path, strerror(errno) );
		snprintf (tmp, sizeof (tmp), "%s/cur", path);
		rmdir (tmp);
		rmdir (path);
		return (-1);
	    }
	    
	    snprintf (tmp, sizeof (tmp), "%s/tmp", path);
	    if (mkdir (tmp, S_IRWXU)) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				     _("Could not create a MailDir at %s (%s)"), path, strerror(errno) );
		snprintf (tmp, sizeof (tmp), "%s/cur", path);
		rmdir (tmp);
		snprintf (tmp, sizeof (tmp), "%s/new", path);
		rmdir (tmp);
		rmdir (path);
		return (-1);
	    }
	} else 
	    return(-1);
    }
    lbm_maildir_set_subdirs(mdir, path);
    return(0);
}

static void
lbm_maildir_set_subdirs(LibBalsaMailboxMaildir * mdir, const gchar * path)
{
    mdir->curdir = g_strdup_printf("%s/cur", path);
    mdir->newdir = g_strdup_printf("%s/new", path);
    mdir->tmpdir = g_strdup_printf("%s/tmp", path);
}

GObject *
libbalsa_mailbox_maildir_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;
    LibBalsaMailboxMaildir *mdir;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MAILDIR, NULL);

    mailbox->is_directory = TRUE;

    LIBBALSA_MAILBOX(mailbox)->url = g_strconcat("file://", path, NULL);


    mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    if(libbalsa_mailbox_maildir_create(path, create, mdir) < 0) {
	g_object_unref(G_OBJECT(mailbox));
	return NULL;
    }

    return G_OBJECT(mailbox);
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

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
libbalsa_mailbox_maildir_load_config(LibBalsaMailbox * mailbox,
				     const gchar * prefix)
{
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    gchar *path;

    path = gnome_config_get_string("Path");
    lbm_maildir_set_subdirs(mdir, path);
    g_free(path);

    LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);
}

static GMimeStream *
libbalsa_mailbox_maildir_get_message_stream(LibBalsaMailbox * mailbox,
					    LibBalsaMessage * message)
{
    struct message_info *msg_info;

    g_return_val_if_fail(MAILBOX_OPEN(mailbox), NULL);

    msg_info = message_info_from_msgno(mailbox, message->msgno);
    if (!msg_info)
	return NULL;

    return _libbalsa_mailbox_local_get_message_stream(mailbox,
						      msg_info->subdir,
						      msg_info->filename);
}

static void
libbalsa_mailbox_maildir_remove_files(LibBalsaMailboxLocal *mailbox)
{
    const gchar* path;
    g_return_if_fail(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox));
    path = libbalsa_mailbox_local_get_path(mailbox);
    g_print("DELETE MAILDIR\n");

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

static void parse_mailbox(LibBalsaMailbox * mailbox, const gchar *subdir)
{
    gchar *path;
    GDir *dir;
    GHashTable *messages_info;
    GPtrArray *msgno_2_msg_info;
    const gchar *filename;
    gchar *key;
    gchar *p;
    LibBalsaMessageFlag flags;
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);

    path = g_strdup_printf("%s/%s", libbalsa_mailbox_local_get_path(mailbox),
			   subdir);
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
	if ((p = strrchr (key, ':')) != NULL &&
	    strncmp (p + 1, "2,", 2) == 0)
	{
	    *p = '\0';
	}

	msg_info = g_hash_table_lookup(messages_info, key);

	flags = parse_filename(subdir, filename);
	if (msg_info == NULL) {
	    msg_info = g_new0(struct message_info, 1);
	    g_hash_table_insert(messages_info, key, msg_info);
	    g_ptr_array_add(msgno_2_msg_info, msg_info);
            /* dummy entry in mindex for now */
            g_ptr_array_add(mailbox->mindex, NULL);
	    msg_info->key=key;
	    msg_info->filename=g_strdup(filename);
	    msg_info->flags = msg_info->orig_flags = flags;
	    msg_info->msgno = msgno_2_msg_info->len;
	} else
	    g_free(key);
	msg_info->subdir = subdir;
	if (msg_info->orig_flags != flags) {
	    g_print("Message flags for (%s) changed\n", msg_info->key);
	    msg_info->orig_flags = flags;
	}
    }
    g_dir_close(dir);
}

static void
parse_mailbox_subdirs(LibBalsaMailbox * mailbox)
{
    parse_mailbox(mailbox, "cur");
    parse_mailbox(mailbox, "new");
}

static gboolean
libbalsa_mailbox_maildir_open(LibBalsaMailbox * mailbox, GError **err)
{
    struct stat st;
    LibBalsaMailboxMaildir *mdir;
    const gchar* path;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), FALSE);

    mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    path = libbalsa_mailbox_local_get_path(mailbox);

    if (stat(path, &st) == -1) {
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    _("Mailbox does not exist."));
	return FALSE;
    }

    mdir->messages_info = g_hash_table_new_full(g_str_hash, g_str_equal,
				  NULL, (GDestroyNotify)free_message_info);
    mdir->msgno_2_msg_info = g_ptr_array_new();

    mdir->mtime = st.st_mtime;
    mdir->mtime_cur = 0;
    mdir->mtime_new = 0;
    if (stat(mdir->curdir, &st) != -1) {
	mdir->mtime_cur = st.st_mtime;
    }
    if (stat(mdir->newdir, &st) != -1) {
	mdir->mtime_new = st.st_mtime;
    }

    mailbox->readonly = 
	!(access(mdir->curdir, W_OK) == 0 &&
          access(mdir->newdir, W_OK) == 0 &&
          access(mdir->tmpdir, W_OK) == 0);

    mailbox->unread_messages = 0;
    parse_mailbox_subdirs(mailbox);
    LIBBALSA_MAILBOX_LOCAL(mailbox)->sync_time = 0;
    LIBBALSA_MAILBOX_LOCAL(mailbox)->sync_cnt  = 1;
#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxMaildir", mailbox->name, mailbox->open_ref);
#endif
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
    struct stat st, st_cur, st_new;
    int modified = 0;
    LibBalsaMailboxMaildir *mdir;
    guint last_msgno;

    g_assert(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox));

    mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);

    if (stat(mdir->tmpdir, &st) == -1)
	return;
    if (mdir->mtime == 0)
	/* First check--just cache the mtime. */
	mdir->mtime = st.st_mtime;
    else if (st.st_mtime > mdir->mtime)
	modified = 1;

    if (stat(mdir->curdir, &st_cur) == -1)
	return;
    if (mdir->mtime_cur == 0)
	/* First check--just cache the mtime. */
	mdir->mtime_cur = st_cur.st_mtime;
    else if (st_cur.st_mtime > mdir->mtime_cur)
	modified = 1;

    if (stat(mdir->newdir, &st_new) == -1)
	return;
    if (mdir->mtime_new == 0)
	/* First check--just cache the mtime. */
	mdir->mtime_new = st_new.st_mtime;
    else if (st_new.st_mtime > mdir->mtime_new)
	modified = 1;

    if (!modified)
	return;

    mdir->mtime = st.st_mtime;
    mdir->mtime_cur = st_cur.st_mtime;
    mdir->mtime_new = st_new.st_mtime;

    if (!MAILBOX_OPEN(mailbox)) {
	libbalsa_mailbox_set_unread_messages_flag(mailbox,
						  lbm_maildir_check(mdir->
								    newdir)
						  ||
						  lbm_maildir_check(mdir->
								    curdir));
	return;
    }

    last_msgno = mdir->msgno_2_msg_info->len;
    parse_mailbox_subdirs(mailbox);

    libbalsa_mailbox_local_load_messages(mailbox, last_msgno);
}

static void
free_message_info(struct message_info *msg_info)
{
    if (!msg_info)
	return;
    g_free(msg_info->key);
    g_free(msg_info->filename);
    if (msg_info->message) {
	msg_info->message->mailbox = NULL;
	msg_info->message->msgno   = 0;
	g_object_remove_weak_pointer(G_OBJECT(msg_info->message),
				     (gpointer) & msg_info->message);
    }
    g_free(msg_info);
}

static void
libbalsa_mailbox_maildir_close_mailbox(LibBalsaMailbox * mailbox,
                                       gboolean expunge)
{
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    guint len = 0;

    if (mdir->msgno_2_msg_info)
	len = mdir->msgno_2_msg_info->len;
    libbalsa_mailbox_maildir_sync(mailbox, expunge);

    if (mdir->messages_info) {
	g_hash_table_destroy(mdir->messages_info);
	mdir->messages_info = NULL;
    }
    if (mdir->msgno_2_msg_info) {
	if (mdir->msgno_2_msg_info->len != len)
	    g_signal_emit_by_name(mailbox, "changed");
	g_ptr_array_free(mdir->msgno_2_msg_info, TRUE);
	mdir->msgno_2_msg_info = NULL;
    }
    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
        LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox(mailbox,
                                                            expunge);
}

static int libbalsa_mailbox_maildir_open_temp (const gchar *dest_path,
					  char **name_used)
{
    int fd;
    static int counter;
    gchar *filename;

    do {
	filename = g_strdup_printf("%s/tmp/%s-%d-%d", dest_path,
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

struct sync_info {
    gchar *path;
    GSList *removed_list;
    gboolean expunge;
    gboolean closing;
    gboolean ok;
};
    
static gboolean maildir_sync_add(struct message_info *msg_info,
				 const gchar * path);

static void
maildir_sync(gchar *key, struct message_info *msg_info, struct sync_info *si)
{
    if (si->expunge && (msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	/* skip this block if the message should only be renamed */
	gchar *orig = g_strdup_printf("%s/%s/%s", si->path, msg_info->subdir, 
				      msg_info->filename);
	unlink (orig);
	g_free(orig);
	si->removed_list = g_slist_prepend(si->removed_list, msg_info);
	return;
    }

    if (si->closing)
	msg_info->flags &= ~LIBBALSA_MESSAGE_FLAG_RECENT;
    if (((msg_info->flags & LIBBALSA_MESSAGE_FLAG_RECENT)
	 && strcmp(msg_info->subdir, "new") != 0)
	|| ((!msg_info->flags & LIBBALSA_MESSAGE_FLAG_RECENT)
	    && strcmp(msg_info->subdir, "cur") != 0)
	|| msg_info->flags != msg_info->orig_flags) {
	if (!maildir_sync_add(msg_info, si->path))
	    si->ok = FALSE;
    }
}

static gboolean
maildir_sync_add(struct message_info *msg_info, const gchar * path)
{
    gchar new_flags[10];
    int len;
    const gchar *subdir;
    gchar *new;
    gchar *orig;
    gboolean retval = TRUE;

    len = 0;

    if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED) != 0)
	new_flags[len++] = 'F';
    if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_NEW) == 0)
	new_flags[len++] = 'S';
    if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_REPLIED) != 0)
	new_flags[len++] = 'R';
    if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED) != 0)
	new_flags[len++] = 'T';

    new_flags[len] = '\0';

    subdir = msg_info->flags & LIBBALSA_MESSAGE_FLAG_RECENT ? "new" : "cur";
    new = g_strdup_printf("%s/%s/%s%s%s", path, subdir, msg_info->key,
			  (len ? ":2," : ""), new_flags);
    orig = g_strdup_printf("%s/%s/%s", path, msg_info->subdir,
			   msg_info->filename);
    if (strcmp(orig, new)) {
	if (rename(orig, new) >= 0) /* FIXME: change to safe_rename??? */
	    msg_info->subdir = subdir;
	else
	    retval = FALSE;
    }
    g_free(orig);
    g_free(new);
    new = g_strdup_printf("%s%s%s", msg_info->key, (len ? ":2," : ""),
			  new_flags);
    g_free(msg_info->filename);
    msg_info->filename = new;
    msg_info->orig_flags = msg_info->flags;

    return retval;
}

static void
maildir_renumber(gchar * key, struct message_info *msg_info,
		      struct message_info *removed)
{
    if (msg_info->msgno > removed->msgno) {
	msg_info->msgno--;
	if (msg_info->message)
	    msg_info->message->msgno = msg_info->msgno;
    }
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
    struct sync_info si;
    GSList *l;

    si.path = g_strdup(libbalsa_mailbox_local_get_path(mailbox));
    si.removed_list = NULL;
    si.expunge = expunge;
    si.closing = mailbox->state == LB_MAILBOX_STATE_CLOSING;
    si.ok = TRUE;
    g_hash_table_foreach(mdir->messages_info, (GHFunc) maildir_sync, &si);
    g_free(si.path);
    if (!si.ok) {
	g_slist_free(si.removed_list);
	return FALSE;
    }

    for (l = si.removed_list; l; l = l->next) {
	struct message_info *removed = l->data;

	g_hash_table_foreach(mdir->messages_info,
			     (GHFunc) maildir_renumber, removed);
	g_ptr_array_remove(mdir->msgno_2_msg_info, removed);
	libbalsa_mailbox_local_msgno_removed(LIBBALSA_MAILBOX(mdir),
					     removed->msgno);
	/* This will free removed: */
	g_hash_table_remove(mdir->messages_info, removed->key);
    }
    g_slist_free(si.removed_list);

    /* Record mtime of dirs; we'll just use the current time--someone
     * else might have changed something since we did, but we'll find
     * out eventually. */
    mdir->mtime = mdir->mtime_cur = mdir->mtime_new = time(NULL);

    return TRUE;
}

static struct message_info *message_info_from_msgno(
						  LibBalsaMailbox * mailbox,
						  guint msgno)
{
    struct message_info *msg_info = NULL;

    msg_info = g_ptr_array_index(LIBBALSA_MAILBOX_MAILDIR(mailbox)->msgno_2_msg_info,
			      msgno - 1);
    return msg_info;
}


static void
libbalsa_mailbox_maildir_fetch_message_structure(LibBalsaMailbox * mailbox,
						 LibBalsaMessage * message,
						 LibBalsaFetchFlag flags)
{
    if (!message->mime_msg) {
	struct message_info *msg_info =
	    message_info_from_msgno(mailbox, message->msgno);
	message->mime_msg =
	    _libbalsa_mailbox_local_get_mime_message(mailbox,
						     msg_info->subdir,
						     msg_info->filename);
    }

    LIBBALSA_MAILBOX_CLASS(parent_class)->fetch_message_structure(mailbox,
								  message,
								  flags);
}

static LibBalsaMessage *
libbalsa_mailbox_maildir_get_message(LibBalsaMailbox * mailbox,
                                     guint msgno)
{
    struct message_info *msg_info;
    const gchar *path;
    gchar *filename;

    msg_info = message_info_from_msgno(mailbox, msgno);

    if (!msg_info)
	return NULL;

    if (msg_info->message)
	g_object_ref(msg_info->message);
    else {
	LibBalsaMessage *message;

	msg_info->message = message = libbalsa_message_new();
	g_object_add_weak_pointer(G_OBJECT(message),
				  (gpointer) & msg_info->message);
	path = libbalsa_mailbox_local_get_path(mailbox);
	filename = g_build_filename(path, msg_info->subdir,
				    msg_info->filename, NULL);
	if (!libbalsa_message_load_envelope_from_file(message, filename)) {
	    g_free(filename);
	    /* FIXME: what to do if loading the envelope fails?
	       return NULL; */
	} else
	    g_free(filename);

	message->flags = msg_info->flags;
	message->mailbox = mailbox;
	message->msgno = msgno;
    }

    return msg_info->message;
}

/* Called with mailbox locked. */
static int libbalsa_mailbox_maildir_add_message(LibBalsaMailbox * mailbox,
						LibBalsaMessage * message )
{
    const char *path;
    char *tmp;
    int fd;
    GMimeStream *out_stream;
    char *new_filename;
    struct message_info *msg_info;
    GMimeStream *in_stream;
    gint retval;

    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(mailbox);
    fd = libbalsa_mailbox_maildir_open_temp(path, &tmp);
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

    new_filename = strrchr(tmp, '/');
    if (new_filename)
	new_filename++;
    else
	new_filename = tmp;
    msg_info = g_new0(struct message_info, 1);
    msg_info->subdir = "tmp";
    msg_info->key = g_strdup(new_filename);
    msg_info->filename = g_strdup(new_filename);
    msg_info->flags = message->flags | LIBBALSA_MESSAGE_FLAG_RECENT;
    retval = maildir_sync_add(msg_info, path) ? 1 : -1;
    free_message_info(msg_info);
    g_free(tmp);

    return retval;
}

static void
libbalsa_mailbox_maildir_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    struct message_info *msg_info;

    g_return_if_fail (LIBBALSA_IS_MAILBOX_MAILDIR(mailbox));
    g_return_if_fail (msgno > 0);

    msg_info = message_info_from_msgno(mailbox, msgno);

    g_return_if_fail (msg_info != NULL);
    msg_info->flags |= set;
    msg_info->flags &= ~clear;

    libbalsa_mailbox_index_set_flags(mailbox, msgno, msg_info->flags);
    libbalsa_mailbox_local_queue_sync(LIBBALSA_MAILBOX_LOCAL(mailbox));
}

static guint
libbalsa_mailbox_maildir_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMaildir *mdir = (LibBalsaMailboxMaildir *) mailbox;

    return mdir->msgno_2_msg_info ? mdir->msgno_2_msg_info->len : 0;
}
