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
#include <string.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbox-filter.h"
#include "misc.h"

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

static void libbalsa_mailbox_local_close_mailbox(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_local_message_match(LibBalsaMailbox *
						     mailbox, guint msgno,
						     LibBalsaMailboxSearchIter
						     * iter);

static void libbalsa_mailbox_local_set_threading(LibBalsaMailbox *mailbox,
						 LibBalsaMailboxThreadingType
						 thread_type);
static void lbm_local_update_view_filter(LibBalsaMailbox * mailbox,
                                         LibBalsaCondition *view_filter);

static void libbalsa_mailbox_local_prepare_threading(LibBalsaMailbox *mailbox, 
                                                     guint lo, guint hi);

static void libbalsa_mailbox_local_fetch_structure(LibBalsaMailbox *mailbox,
                                                   LibBalsaMessage *message,
                                                   LibBalsaFetchFlag flags);
static void libbalsa_mailbox_local_fetch_headers(LibBalsaMailbox *mailbox,
                                                 LibBalsaMessage *message);
static void libbalsa_mailbox_local_release_message(LibBalsaMailbox *
						   mailbox,
						   LibBalsaMessage *
						   message);

static const gchar* libbalsa_mailbox_local_get_msg_part(LibBalsaMessage *msg,
                                                        LibBalsaMessageBody *,
                                                        ssize_t *sz);

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

    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_local_close_mailbox;
    libbalsa_mailbox_class->message_match = 
        libbalsa_mailbox_local_message_match;
    libbalsa_mailbox_class->set_threading =
	libbalsa_mailbox_local_set_threading;
    libbalsa_mailbox_class->update_view_filter =
        lbm_local_update_view_filter;
    libbalsa_mailbox_class->prepare_threading =
        libbalsa_mailbox_local_prepare_threading;
    libbalsa_mailbox_class->fetch_message_structure = 
        libbalsa_mailbox_local_fetch_structure;
    libbalsa_mailbox_class->fetch_headers = 
        libbalsa_mailbox_local_fetch_headers;
    libbalsa_mailbox_class->release_message =
	        libbalsa_mailbox_local_release_message;
    libbalsa_mailbox_class->get_message_part = 
        libbalsa_mailbox_local_get_msg_part;
    klass->load_message = NULL;
    klass->remove_files = NULL;
}

static void
libbalsa_mailbox_local_init(LibBalsaMailboxLocal * mailbox)
{
    mailbox->sync_id   = 0;
    mailbox->sync_time = 0;
    mailbox->sync_cnt  = 0;
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

static void libbalsa_mailbox_link_message(LibBalsaMailboxLocal * mailbox,
					  LibBalsaMessage * msg);
static LibBalsaMessage *
libbalsa_mailbox_local_load_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message;

    g_return_val_if_fail(mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox), NULL);
    g_return_val_if_fail(msgno > 0, NULL);
    g_return_val_if_fail(msgno <= libbalsa_mailbox_total_messages(mailbox),
			 NULL);

    message =
	LIBBALSA_MAILBOX_LOCAL_GET_CLASS(mailbox)->load_message(mailbox,
								msgno);
    g_return_val_if_fail(message != NULL, NULL);

    message->msgno = msgno;
    libbalsa_mailbox_link_message(LIBBALSA_MAILBOX_LOCAL(mailbox), message);
    if(LIBBALSA_MESSAGE_IS_UNREAD(message) && mailbox->first_unread == 0)
        mailbox->first_unread = msgno;
    return message;
}

/* Threading info. */
typedef struct {
    gchar *message_id;
    GList *refs_for_threading;
} LibBalsaMailboxLocalInfo;

static void
lbm_local_free_info(LibBalsaMailboxLocalInfo *info)
{
    g_free(info->message_id);
    info->message_id = NULL;
    g_list_foreach(info->refs_for_threading, (GFunc) g_free, NULL);
    g_list_free(info->refs_for_threading);
    info->refs_for_threading = NULL;
}
    
static void
libbalsa_mailbox_local_finalize(GObject * object)
{
    LibBalsaMailboxLocal *ml;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(object));

    ml = LIBBALSA_MAILBOX_LOCAL(object);
    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(object));
    if(ml->sync_id) {
        g_source_remove(ml->sync_id);
        ml->sync_id = 0;
    }

    if (ml->threading_info) {
	/* The memory owned by ml->threading_info was freed on closing,
	 * so we free only the array itself. */
	g_array_free(ml->threading_info, TRUE);
	ml->threading_info = NULL;
    }

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
libbalsa_mailbox_local_close_mailbox(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxLocal *local  = LIBBALSA_MAILBOX_LOCAL(mailbox);

    if(local->sync_id) {
        g_source_remove(local->sync_id);
        local->sync_id = 0;
    }

    if (local->threading_info) {
	/* Free the memory owned by local->threading_info, but neither
	 * free nor truncate the array. */
	guint i;
	for (i = 0; i < local->threading_info->len; i++)
	    lbm_local_free_info(&g_array_index(local->threading_info,
					       LibBalsaMailboxLocalInfo,
					       i));
    }

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
	LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox(mailbox);
}

/* Search iters */
static gboolean
libbalsa_mailbox_local_message_match(LibBalsaMailbox * mailbox,
				     guint msgno,
				     LibBalsaMailboxSearchIter * iter)
{
    LibBalsaMessage *message =
	libbalsa_mailbox_get_message(mailbox, msgno);
    gboolean res = libbalsa_condition_matches(iter->condition, message);
    g_object_unref(message);
    return res;
}

/* libbalsa_mailbox_link_message:
   MAKE sure the mailbox is LOCKed before entering this routine.
*/ 
static void
libbalsa_mailbox_link_message(LibBalsaMailboxLocal *mailbox,
                              LibBalsaMessage*msg)
{
    LibBalsaMailbox *mbx = LIBBALSA_MAILBOX(mailbox);
    gchar *id;

    msg->mailbox = mbx;

    if (LIBBALSA_MESSAGE_IS_UNREAD(msg)
	&& !LIBBALSA_MESSAGE_IS_DELETED(msg))
        mbx->unread_messages++;
    if(!mbx->view_filter ||
       libbalsa_condition_matches(mbx->view_filter, msg))
        libbalsa_mailbox_msgno_inserted(mbx, msg->msgno);
    if (libbalsa_message_is_partial(msg, &id)) {
	libbalsa_mailbox_try_reassemble(mbx, id);
	g_free(id);
    }
}

/*
 * private 
 * PS: called by mail_progress_notify_cb:
 * loads incrementally new messages, if any.
 *  Mailbox lock MUST BE HELD before calling this function.
 */
void
libbalsa_mailbox_local_load_messages(LibBalsaMailbox *mailbox, guint msgno)
{
    LibBalsaMailboxLocal *local;
    guint new_messages = 0;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));
    local = (LibBalsaMailboxLocal *) mailbox;
    if (!mailbox->msg_tree)
	/* Mailbox is closed, or no view has been created. */
	return;

    /* FIXME: do not create and destroy messages in vain! */
    while (++msgno <= libbalsa_mailbox_total_messages(mailbox)) {
	LibBalsaMessage *msg =
	    libbalsa_mailbox_local_load_message(mailbox, msgno);

	if (!msg)
	    continue;

	++new_messages;
	if (local->threading_info) {
	    LibBalsaMailboxLocalInfo info_val = { 0 };
	    LibBalsaMailboxLocalInfo *info;

	    while (local->threading_info->len < msgno)
		g_array_append_val(local->threading_info, info_val);
	    info = &g_array_index(local->threading_info,
				  LibBalsaMailboxLocalInfo, msgno - 1);
	    g_assert(info->message_id == NULL);
	    info->message_id = g_strdup(msg->message_id);
	    g_assert(info->refs_for_threading == NULL);
	    info->refs_for_threading =
		libbalsa_message_refs_for_threading(msg);
	    /* While we have the message, force mailbox to create
	     * the index entry: */
	    libbalsa_mailbox_msgno_get_subject(mailbox, msgno);
	}
	g_object_unref(msg);
    }

    if (new_messages) {
	libbalsa_mailbox_set_unread_messages_flag(mailbox,
						  mailbox->
						  unread_messages > 0);
	libbalsa_mailbox_run_filters_on_reception(mailbox);
    }
}

/*
 * Threading
 */

static void lbml_threading_jwz(LibBalsaMailbox * mailbox,
			       GNode * new_tree);
static void lbml_threading_simple(LibBalsaMailbox * mailbox,
				  GNode * new_tree,
				  LibBalsaMailboxThreadingType th_type);

static void
libbalsa_mailbox_local_set_threading(LibBalsaMailbox * mailbox,
				     LibBalsaMailboxThreadingType
				     thread_type)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    if (!local->threading_info)
	local->threading_info =
	    g_array_new(FALSE, FALSE, sizeof(LibBalsaMailboxLocalInfo));

    if(!mailbox->msg_tree) { /* first reference */
	/* libbalsa_mailbox_local_load_messages may result in applying
	 * filters, and related code assumes that the gdk lock isn't
	 * held in subthreads. */
	gboolean is_sub_thread = libbalsa_am_i_subthread();
	if (is_sub_thread)
	    gdk_threads_leave();
        mailbox->msg_tree = g_node_new(NULL);
        libbalsa_mailbox_local_load_messages(mailbox, 0);
	if (is_sub_thread)
	    gdk_threads_enter();
    }

    if (thread_type == LB_MAILBOX_THREADING_JWZ)
	lbml_threading_jwz(mailbox, mailbox->msg_tree);
    else
	lbml_threading_simple(mailbox, mailbox->msg_tree, thread_type);
}

void
libbalsa_mailbox_local_msgno_removed(LibBalsaMailbox * mailbox,
				     guint msgno)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    /* local might not have a threading-info array, and even if it does,
     * it might not be populated; we check both. */
    if (local->threading_info && local->threading_info->len) {
	lbm_local_free_info(&g_array_index(local->threading_info,
			    LibBalsaMailboxLocalInfo, msgno - 1));
	g_array_remove_index(local->threading_info, msgno - 1);
    }

    libbalsa_mailbox_msgno_removed(mailbox, msgno);
}

static void
lbm_local_update_view_filter(LibBalsaMailbox * mailbox,
                             LibBalsaCondition *view_filter)
{
    LibBalsaMailboxSearchIter *iter_view;
    guint msgno;

    iter_view = libbalsa_mailbox_search_iter_new(view_filter);
    for (msgno = 1;
	 msgno <= libbalsa_mailbox_total_messages(mailbox);
	 msgno++)
	libbalsa_mailbox_msgno_filt_check(mailbox, msgno, iter_view);
    libbalsa_mailbox_search_iter_free(iter_view);

    printf("%s finished\n", __func__);
    if(mailbox->view_filter)
        libbalsa_condition_free(mailbox->view_filter);
    mailbox->view_filter = view_filter;
}

static void
libbalsa_mailbox_local_prepare_threading(LibBalsaMailbox *mailbox, 
                                        guint lo, guint hi)
{
    g_warning("%s not implemented yet.\n", __func__);
}

/* fetch message structure method: all local mailboxes have their own
 * methods, which ensure that message->mime_msg != NULL, then chain up
 * to this one.
 */

static void
libbalsa_mailbox_local_fetch_structure(LibBalsaMailbox *mailbox,
                                      LibBalsaMessage *message,
                                      LibBalsaFetchFlag flags)
{
    GMimeMessage *mime_message = message->mime_msg;
    g_assert(mime_message != NULL);
    g_assert(mime_message->mime_part != NULL);

    if(flags & LB_FETCH_STRUCTURE) {
        LibBalsaMessageBody *body = libbalsa_message_body_new(message);
        libbalsa_message_body_set_mime_body(body,
                                            mime_message->mime_part);
        libbalsa_message_append_part(message, body);
        libbalsa_message_headers_from_gmime(message->headers, mime_message);
    }
    if(flags & LB_FETCH_RFC822_HEADERS) {
        message->headers->user_hdrs = 
            libbalsa_message_user_hdrs_from_gmime(mime_message);
        message->has_all_headers = 1;
    }
}

static void
libbalsa_mailbox_local_release_message(LibBalsaMailbox * mailbox,
				       LibBalsaMessage * message)
{
    if (message->mime_msg) {
	g_object_unref(message->mime_msg);
	message->mime_msg = NULL;
    }
}

static void
libbalsa_mailbox_local_fetch_headers(LibBalsaMailbox * mailbox,
				     LibBalsaMessage * message)
{
    g_return_if_fail(message->headers->user_hdrs == NULL);

    if (message->mime_msg)
	message->headers->user_hdrs =
	    libbalsa_message_user_hdrs_from_gmime(message->mime_msg);
    else {
	libbalsa_mailbox_fetch_message_structure(mailbox, message,
						 LB_FETCH_RFC822_HEADERS);
	libbalsa_mailbox_local_release_message(mailbox, message);
    }
}

static const gchar*
libbalsa_mailbox_local_get_msg_part(LibBalsaMessage *msg,
                                    LibBalsaMessageBody *part, ssize_t *len)
{
    g_return_val_if_fail(part->mime_part, NULL);

    if (GMIME_IS_PART(part->mime_part))
	return g_mime_part_get_content(GMIME_PART(part->mime_part), len);

    *len = -1;
    return NULL;
}

/*--------------------------------*/
/*  Start of threading functions  */
/*--------------------------------*/
/*
 * This code includes two message threading functions.
 * The first is the implementation of jwz's algorithm describled at
 * http://www.jwz.org/doc/threading.html . The another is very simple and 
 * trivial one. If you confirm that your mailbox includes every threaded 
 * messages, the later will be enough. Those functions are selectable on
 * each mailbox by setting the 'type' member in BalsaIndex. If you don't need
 * message threading functionality, just specify 'LB_MAILBOX_THREADING_FLAT'. 
 *
 * ymnk@jcraft.com
 */

struct _ThreadingInfo {
    LibBalsaMailbox *mailbox;
    GNode *msg_tree;
    GHashTable *id_table;
    GHashTable *subject_table;
};
typedef struct _ThreadingInfo ThreadingInfo;

static gboolean lbml_set_parent(GNode * node, ThreadingInfo * ti);
static void lbml_insert_node(GNode * node,
			     LibBalsaMailboxLocalInfo * message,
			     ThreadingInfo * ti);
static GNode *lbml_find_parent(LibBalsaMailboxLocalInfo * message,
			       ThreadingInfo * ti);
static gboolean lbml_prune(GNode * node, ThreadingInfo * ti);
static void lbml_subject_gather(GNode * node, ThreadingInfo * ti);
static void lbml_subject_merge(GNode * node, ThreadingInfo * ti);
static const gchar *lbml_chop_re(const gchar * str);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
static void lbml_clear_empty(GNode * msg_tree);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

static void
lbml_info_setup(LibBalsaMailbox * mailbox, GNode * msg_tree,
		ThreadingInfo * ti)
{
    ti->mailbox = mailbox;
    ti->msg_tree = msg_tree;
    ti->id_table = g_hash_table_new(g_str_hash, g_str_equal);
    ti->subject_table = NULL;
}

static void
lbml_info_free(ThreadingInfo * ti)
{
    g_hash_table_destroy(ti->id_table);
    if (ti->subject_table)
	g_hash_table_destroy(ti->subject_table);
}

static void
lbml_threading_jwz(LibBalsaMailbox * mailbox, GNode * msg_tree)
{
    ThreadingInfo ti;

    lbml_info_setup(mailbox, msg_tree, &ti);

    g_node_traverse(msg_tree, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbml_set_parent, &ti);
    g_node_traverse(msg_tree, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbml_prune, &ti);

    ti.subject_table = g_hash_table_new(g_str_hash, g_str_equal);
    g_node_children_foreach(msg_tree, G_TRAVERSE_ALL,
			    (GNodeForeachFunc) lbml_subject_gather, &ti);
    g_node_children_foreach(msg_tree, G_TRAVERSE_ALL,
			    (GNodeForeachFunc) lbml_subject_merge, &ti);

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    lbml_clear_empty(msg_tree);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    lbml_info_free(&ti);
}

static LibBalsaMailboxLocalInfo *
lbml_get_message(GNode * node, ThreadingInfo * ti)
{
    guint msgno = GPOINTER_TO_UINT(node->data);
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(ti->mailbox);
    return msgno > 0 && msgno <= local->threading_info->len ?
	&g_array_index(local->threading_info,
		       LibBalsaMailboxLocalInfo, msgno - 1) : NULL;
}

static gboolean
lbml_set_parent(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMailboxLocalInfo *message;
    GNode *parent;
    GNode *child;

    if (!node->parent)
	return FALSE;

    message = lbml_get_message(node, ti);

    if (!message) /* FIXME assert this? */
	return FALSE;

    if (message->message_id)
	lbml_insert_node(node, message, ti);

    /*
     * Set the parent of this message to be the last element in References.
     * Note that this message may have a parent already: this can happen
     * because we saw this ID in a References field, and presumed a
     * parent based on the other entries in that field. Now that we have
     * the actual message, we can be more definitive, so throw away the
     * old parent and use this new one. Find this Container in the
     * parent's children list, and unlink it.
     *
     * Note that this could cause this message to now have no parent, if
     * it has no references field, but some message referred to it as the
     * non-first element of its references. (Which would have been some
     * kind of lie...)
     *
     * Note that at all times, the various ``parent'' and ``child''
     * fields must be kept inter-consistent.
     */
    /* In this implementation, lbml_find_parent always returns a
     * parent; if the message has no parent, it's the root of the
     * whole mailbox tree. */

    parent = lbml_find_parent(message, ti);

    if (node->parent == parent
	/* Nothing to do... */
	|| node == parent)
	/* This message listed itself as its parent! Oh well... */
	return FALSE;

    child = node->children;
    while (child) {
	GNode *next = child->next;
	if (child == parent || g_node_is_ancestor(child, parent)) {
	    /* Prepending node to parent would create a
	     * loop; in lbml_find_parent, we just omit making
	     * the link, but here we really want to link the
	     * message's node to its parent, so we'll fix
	     * the tree: unlink the offending child and prepend it
	     * to the node's parent. */
	    libbalsa_mailbox_unlink_and_prepend(ti->mailbox, child,
						node->parent);
	}
	child = next;
    }

    libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, parent);

    return FALSE;
}

static GNode *
lbml_find_parent(LibBalsaMailboxLocalInfo * message, ThreadingInfo * ti)
{
    /*
     * For each element in the message's References field:
     *   + Find a Container object for the given Message-ID:
     *     + If there's one in id_table use that;
     *     + Otherwise, make (and index) one with a null Message.
     *   + Link the References field's Containers together in the order
     *     implied by the References header.
     *     + If they are already linked, don't change the existing links.
     *     + Do not add a link if adding that link would introduce a loop:
     *       that is, before asserting A->B, search down the children of B
     *       to see if A is reachable.
     */

    /* The root of the mailbox tree is the default parent. */
    GNode *parent = ti->msg_tree;
    GList *reference;

    for (reference = message->refs_for_threading; reference;
	 reference = reference->next) {
	gchar *id = reference->data;
	GNode *foo = g_hash_table_lookup(ti->id_table, id);

	if (foo == NULL) {
	    foo = g_node_new(NULL);
	    g_hash_table_insert(ti->id_table, id, foo);
	}

	/* Avoid nasty surprises. */
	if (foo != parent && !g_node_is_ancestor(foo, parent))
	    if (!foo->parent || foo->parent == ti->msg_tree)
		libbalsa_mailbox_unlink_and_prepend(ti->mailbox, foo, parent);

	parent = foo;
    }
    return parent;
}

static gboolean
lbml_is_replied(GNode * node, ThreadingInfo * ti)
{
    guint msgno = GPOINTER_TO_UINT(node->data);
    return libbalsa_mailbox_msgno_get_status(ti->mailbox, msgno)
	== LIBBALSA_MESSAGE_STATUS_REPLIED;
}

static void
lbml_insert_node(GNode * node, LibBalsaMailboxLocalInfo * message,
		 ThreadingInfo * ti)
{
    /*
     * If id_table contains an *empty* Container for this ID:
     *   + Store this message in the Container's message slot.
     * else
     *   + Create a new Container object holding this message;
     *   + Index the Container by Message-ID in id_table.
     */
    /* We'll make sure that we thread off a replied-to message, if there
     * is one. */
    GNode *old_node;

    old_node = g_hash_table_lookup(ti->id_table, message->message_id);

    if (old_node) {

	LibBalsaMailboxLocalInfo *old_message;

	old_message = lbml_get_message(old_node, ti);
	if (old_message && lbml_is_replied(old_node, ti))
	    return;		/* ...without changing hash table entry. */

	if (!old_message
	    /* old_node is a place-holder... */
	    || lbml_is_replied(node, ti)) {
	    /* ...or we want to thread off this instance */
	    while (old_node->children)
		libbalsa_mailbox_unlink_and_prepend(ti->mailbox, 
						    old_node->children, node);
	}
    }

    g_hash_table_insert(ti->id_table, message->message_id, node);
}

static gboolean
lbml_prune(GNode * node, ThreadingInfo * ti)
{
    /*
     * Recursively walk all containers under the root set. For each container: 
     *
     * + If it is an empty container with no children, nuke it. 
     * + If the Container has no Message, but does have children, 
     *   remove this container but promote its children to this level 
     *  (that is, splice them in to the current child list.) 
     *
     * Do not promote the children if doing so would promote them to 
     * the root set -- unless there is only one child, in which case, do. 
     */

    if (node->data != NULL || node == ti->msg_tree)
	return FALSE;

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (node->children != NULL
	&& (node->parent != ti->msg_tree || node->children->next == NULL))
	do
	    libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node->children,
						node->parent);
	while (node->children);

    if (node->children == NULL)
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, NULL);
#else				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    while (node->children)
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node->children,
					    node->parent);

    libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, NULL);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    return FALSE;
}

static const gchar *
lbml_get_subject(GNode * node, ThreadingInfo * ti)
{
    guint msgno = GPOINTER_TO_UINT(node->data);
    return libbalsa_mailbox_msgno_get_subject(ti->mailbox, msgno);
}

static void
lbml_subject_gather(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMailboxLocalInfo *message, *old_message;
    const gchar *subject = NULL, *old_subject;
    const gchar *chopped_subject = NULL;
    GNode *old;

    /*
     * If any two members of the root set have the same subject, merge them. 
     * This is so that messages which don't have References headers at all 
     * still get threaded (to the extent possible, at least.) 
     *
     * + Construct a new hash table, subject_table, which associates subject 
     *   strings with Container objects. 
     *
     * + For each Container in the root set: 
     *
     *   Find the subject of that sub-tree: 
     *   + If there is a message in the Container, the subject is the subject of
     *     that message. 
     *   + If there is no message in the Container, then the Container will have
     *     at least one child Container, and that Container will have a message.
     *     Use the subject of that message instead. 
     *   + Strip ``Re:'', ``RE:'', ``RE[5]:'', ``Re: Re[4]: Re:'' and so on. 
     *   + If the subject is now "", give up on this 
     *   + Add this Container to the subject_table if: Container. 
     *     + There is no container in the table with this subject, or 
     *     + This one is an empty container and the old one is not: the empty 
     *       one is more interesting as a root, so put it in the table instead. 
     *     + The container in the table has a ``Re:'' version of this subject,
     *       and this container has a non-``Re:'' version of this subject.
     *       The non-re version is the more interesting of the two. 
     *
     * + Now the subject_table is populated with one entry for each subject 
     *   which occurs in the root set. Now iterate over the root set, 
     *   and gather together the difference. 
     * 
     *   For each Container in the root set: 
     * 
     *   Find the subject of this Container (as above.) 
     *   Look up the Container of that subject in the table. 
     *   If it is null, or if it is this container, continue. 
     *   Otherwise, we want to group together this Container and the one 
     *   in the table. There are a few possibilities: 
     *     + If both are dummies, prepend one's children to the other, and 
     *       remove the now-empty container. 
     * 
     *     + If one container is a empty and the other is not, make the 
     *       non-empty one be a child of the empty, and a sibling of the 
     *       other ``real'' messages with the
     *       same subject (the empty's children.) 
     *     + If that container is a non-empty, and that message's subject 
     *       does not begin with ``Re:'', but this message's subject does,
     *       then make this be a child of the other. 
     *     + If that container is a non-empty, and that message's subject 
     *       begins with ``Re:'', but this message's subject does not, 
     *       then make that be a child of this one -- they were misordered. 
     *       (This happens somewhat implicitly, since if there are two 
     *       messages, one with Re: and one without, the one without
     *       will be in the hash table, regardless of the order in which 
     *       they were seen.) 
     * 
     *     + Otherwise, make a new empty container and make both msgs be
     *       a child of it. This catches the both-are-replies and 
     *       neither-are-replies cases, and makes them be siblings instead of
     *       asserting a hierarchical relationship which might not be true. 
     * 
     *     (People who reply to messages without using ``Re:'' and without
     *     using a References line will break this slightly. Those people suck.) 
     * 
     *     (It has occurred to me that taking the date or message number into
     *     account would be one way of resolving some of the ambiguous cases,
     *     but that's not altogether straightforward either.) 
     */

    message = lbml_get_message(node, ti);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (!message)
	message = lbml_get_message(node->children, ti);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    g_assert(message != NULL);

    subject = lbml_get_subject(node, ti);
    if (subject == NULL)
	return;
    chopped_subject = lbml_chop_re(subject);
    if (chopped_subject == NULL
	|| !strcmp(chopped_subject, _("(No subject)")))
	return;

    old = g_hash_table_lookup(ti->subject_table, chopped_subject);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (old == NULL || (node->data == NULL && old->data != NULL)) {
#else				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    if (old == NULL) {
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
	g_hash_table_insert(ti->subject_table, (char *) chopped_subject,
			    node);
	return;
    }

    old_message = lbml_get_message(old, ti);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (!old_message)
	old_message = lbml_get_message(old->children, ti);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    g_assert(old_message != NULL);

    old_subject = lbml_get_subject(old, ti);

    if (old_subject != lbml_chop_re(old_subject)
	&& subject == chopped_subject)
	g_hash_table_insert(ti->subject_table, (gchar *) chopped_subject,
			    node);
}

/* Swap data and children. */
static void
lbml_swap(GNode * node1, GNode * node2, ThreadingInfo * ti)
{
    GNode *tmp_node = g_node_new(NULL);
    gpointer tmp_data;

    while (node1->children)
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node1->children,
					    tmp_node);
    while (node2->children)
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node2->children,
					    node1);
    while (tmp_node->children)
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, tmp_node->children,
					    node2);
    g_node_destroy(tmp_node);

    tmp_data = node1->data;
    node1->data = node2->data;
    node2->data = tmp_data;
}

static void
lbml_subject_merge(GNode * node, ThreadingInfo * ti)
{
    const gchar *subject, *subject2;
    const gchar *chopped_subject, *chopped_subject2;
    GNode *node2;

    subject = lbml_get_subject(node, ti);
    if (subject == NULL)
	return;
    chopped_subject = lbml_chop_re(subject);
    if (chopped_subject == NULL)
	return;

    node2 = g_hash_table_lookup(ti->subject_table, chopped_subject);
    if (node2 == NULL || node2 == node)
	return;

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (node->data == NULL && node2->data == NULL) {
	while (node->children)
	    libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node->children,
						node2);
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, NULL);
	return;
    }

    if (node->data == NULL)
	/* node2 should be made a child of node, but unlinking node2
	 * could mess up the foreach, so we'll swap them and fall
	 * through to the next case. */
	lbml_swap(node, node2, ti);

    if (node2->data == NULL) {
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, node2);
	return;
    }
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    subject2 = lbml_get_subject(node2, ti);
    chopped_subject2 = lbml_chop_re(subject2);

    if ((subject2 == chopped_subject2) && subject != chopped_subject)
	/* Make node a child of node2. */
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, node2);
    else if ((subject2 != chopped_subject2)
	     && subject == chopped_subject) {
	/* Make node2 a child of node; as above, swap them to avoid
	 * unlinking node2. */
	lbml_swap(node, node2, ti);
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, node2);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    } else {
	/* Make both node and node2 children of a new empty node; as
	 * above, swap node2 and the new node to avoid unlinking node2.
	 */
	GNode *new_node = g_node_new(NULL);

	while (node2->children)
	    libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node2->children,
		    				new_node);
	new_node->data = node2->data;
	node2->data = NULL;
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, node2);
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, new_node, node2);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    }
}

/* The more heuristics should be added. */
static const gchar *
lbml_chop_re(const gchar * str)
{
    const gchar *p = str;
    while (*p) {
	while (*p && g_ascii_isspace((int) *p))
	    p++;
	if (!*p)
	    break;

	if (g_ascii_strncasecmp(p, "re:", 3) == 0
	    || g_ascii_strncasecmp(p, "aw:", 3) == 0) {
	    p += 3;
	    continue;
	} else if (g_ascii_strncasecmp(p, _("Re:"), strlen(_("Re:"))) == 0) {
	    /* should "re" be localized ? */
	    p += strlen(_("Re:"));
	    continue;
	}
	break;
    }
    return p;
}

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
static void
lbml_clear_empty(GNode * msg_tree)
{
    GNode *node = msg_tree->children;
    while (node) {
	GNode *next = node->next;
	if (!node->data && !node->children)
	    libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, NULL);
	node = next;
    }
}
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

/* yet another message threading function */

static gboolean lbml_insert_message(GNode * node, ThreadingInfo * ti);
static gboolean lbml_thread_message(GNode * node, ThreadingInfo * ti);

static void
lbml_threading_simple(LibBalsaMailbox * mailbox,
		      GNode * msg_tree,
		      LibBalsaMailboxThreadingType type)
{
    ThreadingInfo ti;

    lbml_info_setup(mailbox, msg_tree, &ti);

    if (type == LB_MAILBOX_THREADING_SIMPLE)
	g_node_traverse(msg_tree, G_POST_ORDER, G_TRAVERSE_ALL,
			-1, (GNodeTraverseFunc) lbml_insert_message, &ti);

    g_node_traverse(msg_tree, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbml_thread_message, &ti);

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    lbml_clear_empty(msg_tree);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    lbml_info_free(&ti);
}

static gboolean
lbml_insert_message(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMailboxLocalInfo *message;

    if (node == ti->msg_tree)
	return FALSE;

    message = lbml_get_message(node, ti);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (!message)
	return FALSE;
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    if (message->message_id)
	g_hash_table_insert(ti->id_table, message->message_id, node);

    return FALSE;
}

static gboolean
lbml_thread_message(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMailboxLocalInfo *message;
    GList *refs;
    GNode *parent = NULL;

    if (node == ti->msg_tree)
	return FALSE;

    message = lbml_get_message(node, ti);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (!message)
	return FALSE;
#else				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    g_assert(message != NULL);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    refs = message->refs_for_threading;
    if (refs)
	parent = g_hash_table_lookup(ti->id_table,
				     g_list_last(refs)->data);
    if (!parent)
	parent = ti->msg_tree;
    if (parent != node->parent)
	libbalsa_mailbox_unlink_and_prepend(ti->mailbox, node, parent);

    return FALSE;
}
/*------------------------------*/
/*  End of threading functions  */
/*------------------------------*/

/* Helper for maildir and mh. */
GMimeMessage *
_libbalsa_mailbox_local_get_mime_message(LibBalsaMailbox * mailbox,
					 const gchar * name1,
					 const gchar * name2)
{
    GMimeStream *mime_stream;
    GMimeParser *mime_parser;
    GMimeMessage *mime_message;

    mime_stream =
	_libbalsa_mailbox_local_get_message_stream(mailbox, name1, name2);
    mime_parser = g_mime_parser_new_with_stream(mime_stream);
    g_mime_parser_set_scan_from(mime_parser, FALSE);
    mime_message = g_mime_parser_construct_message(mime_parser);

    g_object_unref(mime_parser);
    g_mime_stream_unref(mime_stream);

    return mime_message;
}

GMimeStream *
_libbalsa_mailbox_local_get_message_stream(LibBalsaMailbox * mailbox,
					   const gchar * name1,
					   const gchar * name2)
{
    const gchar *path;
    gchar *filename;
    int fd;
    GMimeStream *stream = NULL;

    path = libbalsa_mailbox_local_get_path(mailbox);
    filename = g_build_filename(path, name1, name2, NULL);

    fd = open(filename, O_RDONLY);
    if (fd != -1) {
	stream = g_mime_stream_fs_new(fd);
	if (!stream)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Open of %s failed. Errno = %d, "),
				 filename, errno);
    }
    g_free(filename);

    return stream;
}

/* Queued sync. */

static void
lbml_sync_real(LibBalsaMailboxLocal * local)
{
    LibBalsaMailbox *mailbox = (LibBalsaMailbox*)local;
    time_t tstart;

    time(&tstart);
    libbalsa_lock_mailbox(mailbox);
    if (local->sync_id &&                       /* request still pending */
        MAILBOX_OPEN(mailbox) &&                   /* mailbox still open */
        !libbalsa_mailbox_sync_storage(mailbox, FALSE))   /* cannot sync */
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to sync mailbox \"%s\""),
			     mailbox->name);
    local->sync_id = 0;
    local->sync_time += time(NULL)-tstart;
    local->sync_cnt++;
    libbalsa_unlock_mailbox(mailbox);
    g_object_unref(local);
}

static gboolean
lbml_sync_idle(LibBalsaMailboxLocal * local)
{
#ifdef BALSA_USE_THREADS
    pthread_t sync_thread;

    pthread_create(&sync_thread, NULL, (void *) lbml_sync_real, local);
    pthread_detach(sync_thread);
#else                           /*BALSA_USE_THREADS */
    lbml_sync_real(local);
#endif                          /*BALSA_USE_THREADS */

    return FALSE;
}

void
libbalsa_mailbox_local_queue_sync(LibBalsaMailboxLocal * local)
{
    guint schedule_delay;

    /* The optimal behavior here would be to keep rescheduling
    * requests.  But think of following: the idle handler started and
    * triggered lbml_sync_real thread. While it waits for the lock,
    * another queue request is filed but it is too late for removal of
    * the sync thread. And we get two sync threads executing one after
    * another, etc. So it is better to do sync bit too often... */
    if (local->sync_id) 
        return;
    g_object_ref(G_OBJECT(local));
    /* queue sync job so that the delay is at least five times longer
     * than the syncing time. Otherwise large mailbox owners will be
     * annnoyed. */
    schedule_delay = (local->sync_time*5000)/local->sync_cnt;
    local->sync_id = g_timeout_add_full(G_PRIORITY_LOW, schedule_delay,
                                        (GSourceFunc)lbml_sync_idle,
                                        local, NULL);
}
