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

#define _POSIX_SOURCE 1
#include <gnome.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "libbalsa.h"
#include "misc.h"
#include "libbalsa_private.h"

struct message_info {
    GMimeMessage *mime_message;
    char *key;
    const char *subdir;
    char *filename;
    LibBalsaMessageFlag flags;
    LibBalsaMessageFlag orig_flags;
    LibBalsaMessage *message;
};

static LibBalsaMailboxLocalClass *parent_class = NULL;

static void libbalsa_mailbox_maildir_class_init(LibBalsaMailboxMaildirClass *klass);
static void libbalsa_mailbox_maildir_init(LibBalsaMailboxMaildir * mailbox);
static void libbalsa_mailbox_maildir_finalize(GObject * object);

static GMimeStream *libbalsa_mailbox_maildir_get_message_stream(LibBalsaMailbox *
							   mailbox,
							   LibBalsaMessage *
							   message);
static void libbalsa_mailbox_maildir_remove_files(LibBalsaMailboxLocal *mailbox);

static gboolean libbalsa_mailbox_maildir_open(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_maildir_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_maildir_close_backend(LibBalsaMailbox * mailbox);
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
static void libbalsa_mailbox_maildir_release_message(LibBalsaMailbox *
						     mailbox,
						     LibBalsaMessage *
						     message);
static LibBalsaMessage *libbalsa_mailbox_maildir_load_message(
				    LibBalsaMailbox * mailbox, guint msgno);
static int libbalsa_mailbox_maildir_add_message(LibBalsaMailbox * mailbox,
						LibBalsaMessage * message );
static void libbalsa_mailbox_maildir_change_message_flags(LibBalsaMailbox * mailbox,
						     guint msgno,
						     LibBalsaMessageFlag set,
						     LibBalsaMessageFlag clear);

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

    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_maildir_get_message_stream;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_maildir_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_maildir_check;

    libbalsa_mailbox_class->sync = libbalsa_mailbox_maildir_sync;
    libbalsa_mailbox_class->close_backend = libbalsa_mailbox_maildir_close_backend;
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_maildir_get_message;
    libbalsa_mailbox_class->fetch_message_structure =
	libbalsa_mailbox_maildir_fetch_message_structure;
    libbalsa_mailbox_class->release_message =
	libbalsa_mailbox_maildir_release_message;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_maildir_add_message;
    libbalsa_mailbox_class->change_message_flags =
	libbalsa_mailbox_maildir_change_message_flags;

    libbalsa_mailbox_local_class->remove_files = 
	libbalsa_mailbox_maildir_remove_files;
    libbalsa_mailbox_local_class->load_message =
        libbalsa_mailbox_maildir_load_message;

}

static void
libbalsa_mailbox_maildir_init(LibBalsaMailboxMaildir * mailbox)
{
}

gint
libbalsa_mailbox_maildir_create(const gchar * path, gboolean create)
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
    return(0);
}

GObject *
libbalsa_mailbox_maildir_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;
    LibBalsaMailboxMaildir *mdir;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MAILDIR, NULL);

    mailbox->is_directory = TRUE;

    LIBBALSA_MAILBOX(mailbox)->url = g_strconcat("file://", path, NULL);


    if(libbalsa_mailbox_maildir_create(path, create) < 0) {
	g_object_unref(G_OBJECT(mailbox));
	return NULL;
    }

    mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
    mdir->curdir = g_strdup_printf("%s/cur", path);
    mdir->newdir = g_strdup_printf("%s/new", path);

    libbalsa_notify_register_mailbox(mailbox);

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

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static GMimeStream *
libbalsa_mailbox_maildir_get_message_stream(LibBalsaMailbox *mailbox, 
                                       LibBalsaMessage *message)
{
    GMimeStream *stream = NULL;
    struct message_info *msg_info;
    gchar *filename;
    const gchar *path;
    int fd;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), NULL);
    g_return_val_if_fail (LIBBALSA_IS_MESSAGE(message), NULL);

    msg_info = message_info_from_msgno(mailbox, message->msgno);
    if (!msg_info)
	return NULL;

    path = libbalsa_mailbox_local_get_path(mailbox);
    filename = g_build_filename(path, msg_info->subdir,
				msg_info->filename, NULL);

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
    int new_messages = 0;
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
	    msg_info->key=key;
	    msg_info->filename=g_strdup(filename);
	    msg_info->orig_flags = flags;
	    new_messages++;
	} else
	    g_free(key);
	msg_info->subdir = subdir;
	if (msg_info->orig_flags != flags) {
	    g_warning("Message flags for (%s) changed\n", key);
	    msg_info->orig_flags = flags;
	}
    }
    g_dir_close(dir);

    mailbox->new_messages += new_messages;
}

static gboolean
libbalsa_mailbox_maildir_open(LibBalsaMailbox * mailbox)
{
    struct stat st;
    LibBalsaMailboxMaildir *mdir;
    const gchar* path;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), FALSE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);
    mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
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

    if (!mailbox->readonly)
	mailbox->readonly = access (path, W_OK) ? TRUE : FALSE;
    mailbox->messages = 0;
    mailbox->total_messages = 0;
    mailbox->unread_messages = 0;
    mailbox->new_messages = 0;
    parse_mailbox(mailbox, "cur");
    parse_mailbox(mailbox, "new");
    mailbox->open_ref++;
    UNLOCK_MAILBOX(mailbox);
    libbalsa_mailbox_local_load_messages(mailbox);

    /* We run the filters here also because new could have been put
       in the mailbox with another mechanism than Balsa */
    libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);

    /* increment the reference count */
#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxMaildir", mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

static void libbalsa_mailbox_maildir_check(LibBalsaMailbox * mailbox)
{
    g_return_if_fail (LIBBALSA_IS_MAILBOX_MAILDIR(mailbox));
    
    if (mailbox->open_ref == 0) {
	if (libbalsa_notify_check_mailbox(mailbox))
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
    } else {
	struct stat st, st_cur, st_new;
	const gchar *path;
	int modified = 0;
	LibBalsaMailboxMaildir *mdir;

	LOCK_MAILBOX(mailbox);
	
	mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);

	path = libbalsa_mailbox_local_get_path(mailbox);
	if (stat(path, &st) == -1)
	{
	    UNLOCK_MAILBOX(mailbox);
	    return;
	}

	if (st.st_mtime != mdir->mtime)
	    modified = 1;

	if (stat(mdir->curdir, &st_cur) == -1) {
	    UNLOCK_MAILBOX(mailbox);
	    return;
	}
	if (st_cur.st_mtime != mdir->mtime_cur)
	    modified = 1;

	if (stat(mdir->newdir, &st_new) == -1) {
	    UNLOCK_MAILBOX(mailbox);
	    return;
	}
	if (st_new.st_mtime != mdir->mtime_new)
	    modified = 1;

	if (!modified) {
	    UNLOCK_MAILBOX(mailbox);
	    return;
	}

	mdir->mtime = st.st_mtime;
	mdir->mtime_cur = st_cur.st_mtime;
	mdir->mtime_new = st_new.st_mtime;

	parse_mailbox(mailbox, "cur");
	parse_mailbox(mailbox, "new");

	UNLOCK_MAILBOX(mailbox);
	libbalsa_mailbox_local_load_messages(mailbox);
	libbalsa_mailbox_run_filters_on_reception(mailbox, NULL);
    }
}

static void
free_message_info(struct message_info *msg_info)
{
    if (!msg_info)
	return;
    g_free(msg_info->key);
    g_free(msg_info->filename);
    if (msg_info->mime_message)
	g_object_remove_weak_pointer(G_OBJECT(msg_info->mime_message),
				     (gpointer) & msg_info->mime_message);
    if (msg_info->message)
	g_object_unref(msg_info->message);
}

static gboolean libbalsa_mailbox_maildir_close_backend(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), FALSE);

    g_hash_table_destroy(mdir->messages_info);
    mdir->messages_info = NULL;
    g_ptr_array_free(mdir->msgno_2_msg_info, TRUE);
    mdir->msgno_2_msg_info = NULL;

    return TRUE;
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

static void
maildir_sync(gchar *key, struct message_info *msg_info, const gchar *path)
{
    gboolean move = FALSE;

    if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED) != 0) {
	/* skip this block if the message should only be renamed */
	gchar *orig = g_strdup_printf("%s/%s/%s", path, msg_info->subdir, 
				      msg_info->filename);
	unlink (orig);
	g_free(orig);
	libbalsa_mailbox_msgno_removed(msg_info->message->mailbox,
				       msg_info->message->msgno);
	return;
    }

    if (strcmp(msg_info->subdir, "cur")!=0)
	move = TRUE;
    if (msg_info->flags != msg_info->orig_flags)
	move = TRUE;

    if (move) {
	gchar new_flags[10];
	int len=0;
	gchar *new;
	gchar *orig;

	if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED) != 0)
	    new_flags[len++] = 'F';
	if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_NEW) == 0)
	    new_flags[len++] = 'S';
	if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_REPLIED) != 0)
	    new_flags[len++] = 'R';
	if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED) != 0)
	    new_flags[len++] = 'T';

	new_flags[len] = '\0';

	new = g_strdup_printf("%s/cur/%s%s%s", path, msg_info->key,
			      (len?":2,":""), new_flags);
	orig = g_strdup_printf("%s/%s/%s", path, msg_info->subdir, 
			       msg_info->filename);
	rename(orig, new); /* FIXME: change to safe_rename??? */
	g_free(orig);
	g_free(new);
	new = g_strdup_printf("%s%s%s", msg_info->key, (len?":2,":""), new_flags);
	g_free(msg_info->filename);
	msg_info->subdir = "cur";
	msg_info->filename = new;
	msg_info->orig_flags = msg_info->flags;
    }
}

static gboolean
libbalsa_mailbox_maildir_sync(LibBalsaMailbox * mailbox,
                              gboolean expunge)
{
    /*
     * foreach message_info
     *  mark_move if subdir == "new"
     *  mark_move if flags changed
     *  move/rename and record change if mark_move
     * record mtime of dirs
     */
    gchar *path;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), FALSE);

    path = g_strdup(libbalsa_mailbox_local_get_path(mailbox));
    g_hash_table_foreach(LIBBALSA_MAILBOX_MAILDIR(mailbox)->messages_info,
			 (GHFunc)maildir_sync, path);
    g_free(path);

    /* FIXME: record mtime of dirs */

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

static LibBalsaMessage*
libbalsa_mailbox_maildir_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;

    msg_info = message_info_from_msgno(mailbox, msgno);

    if (!msg_info->message)
	msg_info->message =
	    libbalsa_mailbox_maildir_load_message(mailbox, msgno);

    return msg_info->message;
}

static void
libbalsa_mailbox_maildir_fetch_message_structure(LibBalsaMailbox * mailbox,
						 LibBalsaMessage * message,
						 LibBalsaFetchFlag flags)
{
    if (!message->mime_msg) {
	struct message_info *msg_info;

	msg_info = message_info_from_msgno(mailbox, message->msgno);

	if (!msg_info->mime_message) {
	    msg_info->mime_message =
		_libbalsa_mailbox_local_get_mime_message(mailbox,
							 msg_info->subdir,
							 msg_info->
							 filename);
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
libbalsa_mailbox_maildir_release_message(LibBalsaMailbox * mailbox,
					 LibBalsaMessage * message)
{
    if (message->mime_msg) {
	g_object_unref(message->mime_msg);
	message->mime_msg = NULL;
    }
}

static LibBalsaMessage*
libbalsa_mailbox_maildir_load_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message;
    struct message_info *msg_info;
    const gchar *path;
    gchar *filename;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), NULL);
    g_return_val_if_fail (msgno > 0, NULL);

    mailbox->new_messages--;

    msg_info = message_info_from_msgno(mailbox, msgno);

    if (!msg_info)
	return NULL;

    mailbox->messages++;

    msg_info->message = message = libbalsa_message_new();
    path = libbalsa_mailbox_local_get_path(mailbox);
    filename = g_build_filename(path, msg_info->subdir,
				msg_info->filename, NULL);
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

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MAILDIR(mailbox), -1);

    LOCK_MAILBOX_RETURN_VAL(mailbox, -1);
    g_object_ref ( G_OBJECT(message ) );

    /* open tempfile */
    path = libbalsa_mailbox_local_get_path(mailbox);
    fd = libbalsa_mailbox_maildir_open_temp(path, &tmp);
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

    new_filename = strrchr(tmp, '/');
    if (new_filename)
	new_filename++;
    else
	new_filename = tmp;
    msg_info = g_new0(struct message_info, 1);
    msg_info->subdir = "tmp";
    msg_info->key = g_strdup(new_filename);
    msg_info->filename = g_strdup(new_filename);
    msg_info->flags = message->flags;
    maildir_sync(msg_info->key, msg_info, path);
    if (MAILBOX_OPEN(mailbox)) {
	    LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
	    g_hash_table_insert(mdir->messages_info, msg_info->key, msg_info);
	    g_ptr_array_add(mdir->msgno_2_msg_info, msg_info);
	    mailbox->new_messages++;
    }
    g_free(tmp);

    g_object_unref ( G_OBJECT(message ) );  
    UNLOCK_MAILBOX(mailbox);

    return 1;
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
}
