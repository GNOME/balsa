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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

enum {
    REMOVE_FILES,
    LAST_SIGNAL
};
static guint libbalsa_mailbox_local_signals[LAST_SIGNAL];

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_local_class_init(LibBalsaMailboxLocalClass *klass);
static void libbalsa_mailbox_local_init(LibBalsaMailboxLocal * mailbox);
static void libbalsa_mailbox_local_destroy(GtkObject * object);

static void libbalsa_mailbox_local_open(LibBalsaMailbox * mailbox,
					gboolean append);
static void libbalsa_mailbox_local_check(LibBalsaMailbox * mailbox);

static void libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);
static void libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);

GtkType libbalsa_mailbox_local_get_type(void)
{
    static GtkType mailbox_type = 0;

    if (!mailbox_type) {
	static const GtkTypeInfo mailbox_info = {
	    "LibBalsaMailboxLocal",
	    sizeof(LibBalsaMailboxLocal),
	    sizeof(LibBalsaMailboxLocalClass),
	    (GtkClassInitFunc) libbalsa_mailbox_local_class_init,
	    (GtkObjectInitFunc) libbalsa_mailbox_local_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	mailbox_type =
	    gtk_type_unique(libbalsa_mailbox_get_type(), &mailbox_info);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_local_class_init(LibBalsaMailboxLocalClass * klass)
{
    GtkObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = GTK_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = gtk_type_class(libbalsa_mailbox_get_type());

    libbalsa_mailbox_local_signals[REMOVE_FILES] =
	gtk_signal_new("remove-files",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxLocalClass,
					 remove_files),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    gtk_object_class_add_signals(object_class, libbalsa_mailbox_local_signals,
				 LAST_SIGNAL);


    object_class->destroy = libbalsa_mailbox_local_destroy;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_local_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_local_check;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_local_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_local_load_config;

    klass->remove_files = NULL;
}

static void
libbalsa_mailbox_local_init(LibBalsaMailboxLocal * mailbox)
{
    mailbox->path = NULL;
}

GtkObject *
libbalsa_mailbox_local_new(const gchar * path, gboolean create)
{
    int magic_type;

    libbalsa_lock_mutt();
    magic_type = mx_get_magic(path);
    libbalsa_unlock_mutt();

    switch (magic_type) {
    case M_MBOX:
    case M_MMDF:
	return libbalsa_mailbox_mbox_new(path, create);
	break;
    case M_MH:
	return libbalsa_mailbox_mh_new(path, create);
	break;
    case M_MAILDIR:
	return libbalsa_mailbox_maildir_new(path, create);
	break;
    case M_IMAP:
	g_warning("Got IMAP as type for local mailbox\n");
	return NULL;
    default:			/* mailbox non-existent or unreadable */
	if ( create ) 
	    return libbalsa_mailbox_mbox_new(path, TRUE);
	g_warning("Unknown mailbox type\n");
	return NULL;
    }
}

/* libbalsa_mailbox_local_set_path:
   returrns errno on error, 0 on success
   FIXME: Needs to work for maildir and mh
*/
gint
libbalsa_mailbox_local_set_path(LibBalsaMailboxLocal * mailbox,
				const gchar * path)
{
    int i;
    /* rename */
    g_return_val_if_fail(path, 0);

    if ( LIBBALSA_MAILBOX_LOCAL(mailbox)->path != NULL ) {
	if (g_strcasecmp(path, LIBBALSA_MAILBOX_LOCAL(mailbox)->path) == 0)
	    return 0;
	if ((i = rename(LIBBALSA_MAILBOX_LOCAL(mailbox)->path, path)) != 0)
	    return i;
    }

    /* update mailbox data */
    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));
    g_free(mailbox->path);
    mailbox->path = g_strdup(path);
    libbalsa_notify_register_mailbox(LIBBALSA_MAILBOX(mailbox));
    return 0;
}

void
libbalsa_mailbox_local_remove_files(LibBalsaMailboxLocal *mailbox)
{
    g_return_if_fail (LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_local_signals[REMOVE_FILES]);

}

static void
libbalsa_mailbox_local_destroy(GtkObject * object)
{
    LibBalsaMailboxLocal *mailbox;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(object));

    mailbox = LIBBALSA_MAILBOX_LOCAL(object);

    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));

    g_free(mailbox->path);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

/* libbalsa_mailbox_local_open:
   THREADING: it is always called from a signal handler so the gdk lock
   is held on entry. The lock is released on the time-consuming open
   operation and taken back when job is finished.
   Order is crucial to avoid deadlocks.
*/
static void
libbalsa_mailbox_local_open(LibBalsaMailbox * mailbox, gboolean append)
{
    struct stat st;
    LibBalsaMailboxLocal *local;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    LOCK_MAILBOX(mailbox);

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
	if (append) {
	    /* we need the mailbox to be opened fresh i think */
	    libbalsa_lock_mutt();
	    mx_close_mailbox(CLIENT_CONTEXT(mailbox), NULL);
	    libbalsa_unlock_mutt();
	} else {
	    /* incriment the reference count */
	    mailbox->open_ref++;

	    UNLOCK_MAILBOX(mailbox);
	    return;
	}
    }

    if (stat(local->path, &st) == -1) {
	UNLOCK_MAILBOX(mailbox);
	return;
    }

    gdk_threads_leave();
    libbalsa_lock_mutt();
    if (append)
	CLIENT_CONTEXT(mailbox) =
	    mx_open_mailbox(local->path, M_APPEND, NULL);
    else
	CLIENT_CONTEXT(mailbox) = mx_open_mailbox(local->path, 0, NULL);
    libbalsa_unlock_mutt();
    
    if (!CLIENT_CONTEXT_OPEN(mailbox)) {
	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
	return;
    }
    mailbox->readonly = CLIENT_CONTEXT(mailbox)->readonly;
    mailbox->messages = 0;
    mailbox->total_messages = 0;
    mailbox->unread_messages = 0;
    mailbox->new_messages = CLIENT_CONTEXT(mailbox)->msgcount;
    mailbox->open_ref++;
    UNLOCK_MAILBOX(mailbox);
    gdk_threads_enter();
    libbalsa_mailbox_load_messages(mailbox);
    
    /* increment the reference count */
#ifdef DEBUG
    g_print(_("LibBalsaMailboxLocal: Opening %s Refcount: %d\n"),
	    mailbox->name, mailbox->open_ref);
#endif
}

static void
libbalsa_mailbox_local_check(LibBalsaMailbox * mailbox)
{
    if (mailbox->open_ref == 0) {
	if (libbalsa_notify_check_mailbox(mailbox))
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
    } else {
	gint i = 0;
	gint index_hint;

	LOCK_MAILBOX(mailbox);

	index_hint = CLIENT_CONTEXT(mailbox)->vcount;

	libbalsa_lock_mutt();
	i = mx_check_mailbox(CLIENT_CONTEXT(mailbox), &index_hint, 0);
	libbalsa_unlock_mutt();

	if (i < 0) {
	    g_print("mx_check_mailbox() failed on %s\n", mailbox->name);
	} 
	if (i == M_NEW_MAIL || i == M_REOPENED) {
	    mailbox->new_messages =
		CLIENT_CONTEXT(mailbox)->msgcount - mailbox->messages;
	    UNLOCK_MAILBOX(mailbox);
	    libbalsa_mailbox_load_messages(mailbox);
	} else {
	    UNLOCK_MAILBOX(mailbox);
	}
    }
}

static void
libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
				   const gchar * prefix)
{
    LibBalsaMailboxLocal *local;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    gnome_config_set_string("Path", local->path);

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
				   const gchar * prefix)
{
    LibBalsaMailboxLocal *local;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    g_free(local->path);

    local->path = gnome_config_get_string("Path");

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);

    libbalsa_notify_register_mailbox(mailbox);
}
