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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbackend.h"

#include "mailbox-filter.h"
#include "filter-file.h"
#include <libgnome/gnome-defs.h> 
#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 

enum {
    REMOVE_FILES,
    LAST_SIGNAL
};
static guint libbalsa_mailbox_local_signals[LAST_SIGNAL];

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_local_class_init(LibBalsaMailboxLocalClass *klass);
static void libbalsa_mailbox_local_init(LibBalsaMailboxLocal * mailbox);
static void libbalsa_mailbox_local_destroy(GtkObject * object);

static gboolean libbalsa_mailbox_local_open(LibBalsaMailbox * mailbox);
static LibBalsaMailboxAppendHandle*
libbalsa_mailbox_local_append(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_local_check(LibBalsaMailbox * mailbox);

static void libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);
static void libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);

static void
run_filters_on_reception(LibBalsaMailbox * mailbox);

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
    libbalsa_mailbox_class->open_mailbox_append 
	= libbalsa_mailbox_local_append;
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
}

GtkObject *
libbalsa_mailbox_local_new(const gchar * path, gboolean create)
{
    GtkType magic_type = libbalsa_mailbox_type_from_path(path);

    if(magic_type == LIBBALSA_TYPE_MAILBOX_MBOX)
	return libbalsa_mailbox_mbox_new(path, create);
    else if(magic_type == LIBBALSA_TYPE_MAILBOX_MH)
	return libbalsa_mailbox_mh_new(path, create);
    else if(magic_type == LIBBALSA_TYPE_MAILBOX_MAILDIR)
	return libbalsa_mailbox_maildir_new(path, create);
    else if(magic_type == LIBBALSA_TYPE_MAILBOX_IMAP) {
        g_warning("IMAP path given as a path to local mailbox.\n");
        return NULL;
    } else {		/* mailbox non-existent or unreadable */
	if(create) 
	    return libbalsa_mailbox_mbox_new(path, TRUE);
        else {
            g_warning("Unknown mailbox type\n");
            return NULL;
        }
    }
}

/* libbalsa_mailbox_local_set_path:
   returrns errno on error, 0 on success
   FIXME: proper suport for maildir and mh
*/
gint
libbalsa_mailbox_local_set_path(LibBalsaMailboxLocal * mailbox,
				const gchar * path)
{
    int i = 0;

    g_return_val_if_fail(mailbox, -1);
    g_return_val_if_fail(path, -1);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox), -1);

    if ( LIBBALSA_MAILBOX(mailbox)->url != NULL ) {
	const gchar* cur_path = libbalsa_mailbox_local_get_path(mailbox);
	if (g_strcasecmp(path, cur_path) == 0)
	    return 0;
	else 
	    i = rename(cur_path, path);
    } else {
	if(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox))
	    i = libbalsa_mailbox_maildir_create(path, TRUE);
	else if(LIBBALSA_IS_MAILBOX_MH(mailbox))
	    i = libbalsa_mailbox_mh_create(path, TRUE);
	else if(LIBBALSA_IS_MAILBOX_MBOX(mailbox))
	    i = libbalsa_mailbox_mbox_create(path, TRUE);	    
    }

    /* update mailbox data */
    if(!i) {
	libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));
	g_free(LIBBALSA_MAILBOX(mailbox)->url);
	LIBBALSA_MAILBOX(mailbox)->url = g_strconcat("file://", path, NULL);
	libbalsa_notify_register_mailbox(LIBBALSA_MAILBOX(mailbox));
	return 0;
    } else
	return errno;
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
    LibBalsaMailbox *mailbox;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(object));

    mailbox = LIBBALSA_MAILBOX(object);
    libbalsa_notify_unregister_mailbox(mailbox);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

/* Helper function to run the "on reception" filters on a mailbox */

static void
run_filters_on_reception(LibBalsaMailbox * mailbox)
{
    GSList * filters;
    GList * new_messages;

    if (!mailbox->filters)                                
        config_mailbox_filters_load(mailbox);

    filters = libbalsa_mailbox_filters_when(mailbox->filters,
                                            FILTER_WHEN_INCOMING);
    /* We apply filter if needed */
    if (filters) {
	LOCK_MAILBOX(mailbox);
	new_messages=libbalsa_extract_new_messages(mailbox->message_list);
	if (new_messages) {
	    if (filters_prepare_to_run(filters)) {
		libbalsa_filter_match(filters, new_messages, TRUE);
		UNLOCK_MAILBOX(mailbox);
		libbalsa_filter_apply(filters);
	    }
	    else UNLOCK_MAILBOX(mailbox);
	    g_list_free(new_messages);
	}
	else UNLOCK_MAILBOX(mailbox);
	g_slist_free(filters);
    }
}

/* libbalsa_mailbox_local_open:
   THREADING: it is always called from a signal handler so the gdk lock
   is held on entry. The lock is released on the time-consuming open
   operation and taken back when job is finished.
   Order is crucial to avoid deadlocks.
*/
static gboolean
libbalsa_mailbox_local_open(LibBalsaMailbox * mailbox)
{
    struct stat st;
    LibBalsaMailboxLocal *local;
    const gchar* path;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox), FALSE);

    gdk_threads_leave();

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);
    local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    path = libbalsa_mailbox_local_get_path(mailbox);

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
	/* incriment the reference count */
	mailbox->open_ref++;
	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
	return TRUE;
    }

    if (stat(path, &st) == -1) {
	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
	return FALSE;
    }
    
    libbalsa_lock_mutt();
    CLIENT_CONTEXT(mailbox) = mx_open_mailbox(path, 0, NULL);
    libbalsa_unlock_mutt();
    
    if (!CLIENT_CONTEXT_OPEN(mailbox)) {
	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
	return FALSE;
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
    
    run_filters_on_reception(mailbox);

    /* increment the reference count */
#ifdef DEBUG
    g_print(_("LibBalsaMailboxLocal: Opening %s Refcount: %d\n"),
	    mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

static LibBalsaMailboxAppendHandle*
libbalsa_mailbox_local_append(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxAppendHandle* res;
    g_return_val_if_fail(mailbox, NULL);

    res = g_new0(LibBalsaMailboxAppendHandle,1);
    libbalsa_lock_mutt();
    res->context = mx_open_mailbox(libbalsa_mailbox_local_get_path(mailbox), 
				   M_APPEND, NULL);
    if(res->context == NULL) {
	g_free(res);
	res = NULL;
    } else if (res->context->readonly) {
	g_warning("Cannot open dest local mailbox '%s' for writing.", 
		  mailbox->name);
	mx_close_mailbox(res->context, NULL);
	g_free(res);
	res = NULL;
    }
    libbalsa_unlock_mutt();
    return res;
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
	    run_filters_on_reception(mailbox);

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

    gnome_config_set_string("Path", libbalsa_mailbox_local_get_path(local));

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
				   const gchar * prefix)
{
    LibBalsaMailboxLocal *local;
    gchar* path;
    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    g_free(mailbox->url);

    path = gnome_config_get_string("Path");
    mailbox->url = g_strconcat("file://", path, NULL);
    g_free(path);

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);

    libbalsa_notify_register_mailbox(mailbox);
}
