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

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbox-filter.h"

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
static void libbalsa_mailbox_local_finalize(GObject * object);

static void libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);
static void libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);

static void libbalsa_mailbox_local_real_mbox_match(LibBalsaMailbox *mbox,
                                                   GSList * filter_list);

GType
libbalsa_mailbox_local_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxLocalClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_local_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxLocal),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_local_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX,
	                           "LibBalsaMailboxLocal",
                                   &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_local_class_init(LibBalsaMailboxLocalClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    libbalsa_mailbox_local_signals[REMOVE_FILES] =
	g_signal_new("remove-files",
                     G_TYPE_FROM_CLASS(object_class),
		     G_SIGNAL_RUN_LAST,
		     G_STRUCT_OFFSET(LibBalsaMailboxLocalClass,
				     remove_files),
                     NULL, NULL,
		     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    object_class->finalize = libbalsa_mailbox_local_finalize;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_local_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_local_load_config;

    libbalsa_mailbox_class->mailbox_match = 
        libbalsa_mailbox_local_real_mbox_match;
    klass->remove_files = NULL;
}

static void
libbalsa_mailbox_local_init(LibBalsaMailboxLocal * mailbox)
{
}

GObject *
libbalsa_mailbox_local_new(const gchar * path, gboolean create)
{
    GType magic_type = libbalsa_mailbox_type_from_path(path);

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
	if (g_ascii_strcasecmp(path, cur_path) == 0)
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
	return errno ? errno : -1;
}

void
libbalsa_mailbox_local_remove_files(LibBalsaMailboxLocal *mailbox)
{
    g_return_if_fail (LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_local_signals[REMOVE_FILES], 0);

}

static void
libbalsa_mailbox_local_finalize(GObject * object)
{
    LibBalsaMailbox *mailbox;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(object));

    mailbox = LIBBALSA_MAILBOX(object);
    libbalsa_notify_unregister_mailbox(mailbox);

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
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

static void
libbalsa_mailbox_local_real_mbox_match(LibBalsaMailbox *mbox,
                                       GSList * filter_list)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mbox));
    LOCK_MAILBOX(mbox);
    libbalsa_filter_match(filter_list, 
                          LIBBALSA_MAILBOX_LOCAL(mbox)->msg_list, TRUE);
    UNLOCK_MAILBOX(mbox);
}

#if 0
/* libbalsa_mailbox_free_messages:
   removes all the messages from the mailbox.
   Messages are unref'ed and not directly destroyed because they migt
   be ref'ed from somewhere else.
   Mailbox lock MUST BE HELD before calling this function.
*/
static void
libbalsa_mailbox_free_messages(LibBalsaMailboxLocal *mailbox)
{
    GList *list;
    LibBalsaMessage *message;
    LibBalsaMailbox *mbox = LIBBALSA_MAILBOX(mailbox);

    list = g_list_first(mailbox->msg_list);

    if(list){
	g_signal_emit_by_name(G_OBJECT(mailbox),
                              "messages_removed", list);
    }

    while (list) {
	message = list->data;
	list = list->next;

	message->mailbox = NULL;
	g_object_unref(G_OBJECT(message));
    }

    g_list_free(mailbox->msg_list);
    mailbox->msg_list = NULL;
    mbox->messages = 0;
    mbox->total_messages = 0;
    mbox->unread_messages = 0;
}
#endif

/* libbalsa_mailbox_sync_backend_real
 * synchronize the frontend and libbalsa: build a list of messages
 * marked as deleted, and:
 * 1. emit the "messages-removed" signal, so the frontend can drop them
 *    from the BalsaIndex;
 * 2. if delete == TRUE, delete them from the LibBalsaMailbox.
 *
 * NOTE: current backend should be informed as soon as possible about
 * the changes. The question, whether the changes are to be saved to a
 * permanent storage is storage-dependent and decision is left for the
 * backend to make.
 */
static void
libbalsa_mailbox_local_sync_backend_real(LibBalsaMailboxLocal *mailbox,
                                         gboolean delete)
{
    GList *list;
    GList *message_list;
    LibBalsaMessage *current_message;
    GList *p=NULL;
    GList *q = NULL;

    for (message_list = mailbox->msg_list; message_list;
         message_list = g_list_next(message_list)) {
	current_message = LIBBALSA_MESSAGE(message_list->data);
	if (LIBBALSA_MESSAGE_IS_DELETED(current_message)) {
	    p=g_list_prepend(p, current_message);
            if (delete)
                q = g_list_prepend(q, message_list);
	}
    }

    if (p) {
	UNLOCK_MAILBOX(LIBBALSA_MAILBOX(mailbox));
	g_signal_emit_by_name(G_OBJECT(mailbox), "messages-removed",p);
	LOCK_MAILBOX(LIBBALSA_MAILBOX(mailbox));
	g_list_free(p);
    }

    if (delete) {
        for (list = q; list; list = g_list_next(list)) {
            message_list = list->data;
            current_message =
                LIBBALSA_MESSAGE(message_list->data);
            current_message->mailbox = NULL;
            g_object_unref(G_OBJECT(current_message));
	    mailbox->msg_list =
		g_list_remove_link(mailbox->msg_list, message_list);
            g_list_free_1(message_list);
	}
        g_list_free(q);
    }
}

/* libbalsa_mailbox_link_message:
   MAKE sure the mailbox is LOCKed before entering this routine.
*/ 
static void
libbalsa_mailbox_link_message(LibBalsaMailboxLocal *mailbox,
                              LibBalsaMessage*msg)
{
    LibBalsaMailbox *mbx = LIBBALSA_MAILBOX(mailbox);
    msg->mailbox = mbx;
    mailbox->msg_list = g_list_prepend(mailbox->msg_list, msg);

    if (LIBBALSA_MESSAGE_IS_DELETED(msg))
        return;
    if (LIBBALSA_MESSAGE_IS_UNREAD(msg))
        mbx->unread_messages++;
    mbx->total_messages++;
}

/*
 * private 
 * PS: called by mail_progress_notify_cb:
 * loads incrementally new messages, if any.
 *  Mailbox lock MUST NOT BE HELD before calling this function.
 *  gdk_lock MUST BE HELD before calling this function because it is called
 *  from both threading and not threading code and we want to be on the safe
 *  side.
 * 
 *    FIXME: create the msg-tree in a correct way
 */
void
libbalsa_mailbox_local_load_messages(LibBalsaMailbox *mailbox)
{
    glong msgno;
    LibBalsaMessage *message;
    GList *messages=NULL;

    if (MAILBOX_CLOSED(mailbox))
	return;

    LOCK_MAILBOX(mailbox);
    for (msgno = mailbox->messages; mailbox->new_messages > 0; msgno++) {
	message = libbalsa_mailbox_load_message(mailbox, msgno);
	if (!message)
		continue;
	libbalsa_message_headers_update(message);
	libbalsa_mailbox_link_message(LIBBALSA_MAILBOX_LOCAL(mailbox),
                                      message);
	g_node_append_data(mailbox->msg_tree, GINT_TO_POINTER(msgno));
	messages=g_list_prepend(messages, message);
    }
    UNLOCK_MAILBOX(mailbox);

    if(messages!=NULL){
	messages = g_list_reverse(messages);
	g_signal_emit_by_name(G_OBJECT(mailbox), "messages-added", messages);
	g_list_free(messages);
    }

    libbalsa_mailbox_set_unread_messages_flag(mailbox,
					      mailbox->unread_messages > 0);

    
}

/* libbalsa_mailbox_commit:
   commits the data to storage.


   Returns TRUE on success, FALSE on failure.  */
gboolean
libbalsa_mailbox_commit(LibBalsaMailbox *mailbox)
{
    gboolean rc = TRUE;

    if (MAILBOX_CLOSED(mailbox))
	return FALSE;

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);
    /* remove messages flagged for deleting, before committing: */
    libbalsa_mailbox_local_sync_backend_real
        (LIBBALSA_MAILBOX_LOCAL(mailbox), TRUE);
    rc = libbalsa_mailbox_sync_storage(mailbox);

    if (rc == TRUE) {
	    UNLOCK_MAILBOX(mailbox);
	    if (mailbox->new_messages) {
		    libbalsa_mailbox_local_load_messages(mailbox);
		    rc = 0;
	    }
    } else {
        UNLOCK_MAILBOX(mailbox);
    }

    return rc;
}
