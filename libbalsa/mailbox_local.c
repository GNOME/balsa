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

static void libbalsa_mailbox_local_set_threading(LibBalsaMailbox *mailbox,
						 LibBalsaMailboxThreadingType
						 thread_type);
static void libbalsa_mailbox_local_close_mailbox(LibBalsaMailbox * mailbox);

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
    libbalsa_mailbox_class->set_threading =
	libbalsa_mailbox_local_set_threading;
    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_local_close_mailbox;
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
    for (msgno = mailbox->messages + 1; mailbox->new_messages > 0; msgno++) {
	message = libbalsa_mailbox_load_message(mailbox, msgno);
	if (!message)
		continue;
	libbalsa_message_headers_update(message);
	libbalsa_mailbox_link_message(LIBBALSA_MAILBOX_LOCAL(mailbox),
                                      message);
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

static void
libbalsa_mailbox_local_close_mailbox(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxLocal *mbox = LIBBALSA_MAILBOX_LOCAL(mailbox);

    g_list_free(mbox->msg_list);
    mbox->msg_list = NULL;

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
	LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox(mailbox);
}

/*
 * Threading
 */

static void lbml_threading_jwz(LibBalsaMailbox * mailbox);
static void lbml_threading_simple(LibBalsaMailbox * mailbox,
				  LibBalsaMailboxThreadingType th_type);

static void
libbalsa_mailbox_local_set_threading(LibBalsaMailbox * mailbox,
				     LibBalsaMailboxThreadingType
				     thread_type)
{
    int i;
    if (mailbox->msg_tree)
	g_node_destroy(mailbox->msg_tree);
    mailbox->msg_tree = g_node_new(NULL);
    for (i = 1; i <= mailbox->total_messages; i++)
	g_node_append_data(mailbox->msg_tree, GINT_TO_POINTER(i));
    mailbox->stamp++;

    /* Since we're starting out with a flat list, we'll do nothing for
     * LB_MAILBOX_THREADING_FLAT. The threading code can actually handle
     * starting with a threaded list, which should be faster when we're
     * making only incremental changes, but we'll leave it this way for
     * now.  */
    if (thread_type == LB_MAILBOX_THREADING_JWZ)
	lbml_threading_jwz(mailbox);
    else if (thread_type == LB_MAILBOX_THREADING_SIMPLE)
	lbml_threading_simple(mailbox, thread_type);
}

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
    LibBalsaMessage **msg_array;
    gint msg_array_len;
    GHashTable *id_table;
    GHashTable *subject_table;
    GSList *message_list;
};
typedef struct _ThreadingInfo ThreadingInfo;

static void lbml_make_msg_array(ThreadingInfo * ti);
static void lbml_set_parent(GNode * node, ThreadingInfo * ti);
static void lbml_insert_node(GNode * node, LibBalsaMessage * message,
			     ThreadingInfo * ti);
static GNode *lbml_find_parent(LibBalsaMessage * message,
			       ThreadingInfo * ti);
static gboolean lbml_prune(GNode * node, GNode * root);
static void lbml_subject_gather(GNode * node, ThreadingInfo * ti);
static void lbml_subject_merge(GNode * node, ThreadingInfo * ti);
static const gchar *lbml_chop_re(const gchar * str);
static GNode *lbml_unlink(GNode * node);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
static void lbml_clear_empty(LibBalsaMailbox * mailbox);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

static void
lbml_threading_jwz(LibBalsaMailbox * mailbox)
{
    ThreadingInfo ti;
    gint i;

    ti.mailbox = mailbox;
    ti.id_table = g_hash_table_new(g_str_hash, g_str_equal);
    lbml_make_msg_array(&ti);

    for (i = 1; i <= ti.msg_array_len; i++) {
	GNode *node =
	    g_node_find(mailbox->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL,
			GINT_TO_POINTER(i));
	if (node)
	    lbml_set_parent(node, &ti);
	else
	    g_warning("Did not find message %d\n", i);
    }

    g_node_traverse(mailbox->msg_tree, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbml_prune, mailbox->msg_tree);

    ti.subject_table = g_hash_table_new(g_str_hash, g_str_equal);
    g_node_children_foreach(mailbox->msg_tree, G_TRAVERSE_ALL,
			    (GNodeForeachFunc) lbml_subject_gather, &ti);
    g_node_children_foreach(mailbox->msg_tree, G_TRAVERSE_ALL,
			    (GNodeForeachFunc) lbml_subject_merge, &ti);

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    lbml_clear_empty(mailbox);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    g_hash_table_destroy(ti.subject_table);
    g_hash_table_destroy(ti.id_table);
    g_free(ti.msg_array);
}

static void
lbml_make_msg_array(ThreadingInfo * ti)
{
    GList *list;
    LibBalsaMessage **msg;

    ti->msg_array_len =
	g_list_length(LIBBALSA_MAILBOX_LOCAL(ti->mailbox)->msg_list);
    msg = ti->msg_array = g_new(LibBalsaMessage *, ti->msg_array_len);

    for (list = LIBBALSA_MAILBOX_LOCAL(ti->mailbox)->msg_list; list;
	 list = list->next)
	*msg++ = list->data;
}

static LibBalsaMessage *
lbml_get_message(GNode * node, ThreadingInfo * ti)
{
    gint msgno;

    msgno = GPOINTER_TO_INT(node->data);

    g_return_val_if_fail(msgno <= ti->msg_array_len, NULL);

    return msgno <= 0 ? NULL : ti->msg_array[msgno - 1];
}

static void
lbml_set_parent(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMessage *message;
    GNode *parent;
    GNode *child;

    message = lbml_get_message(node, ti);
    if (!message || !message->message_id)
	return;

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
	return;

    for (child = node->children; child; child = child->next)
	if (child == parent || g_node_is_ancestor(child, parent)) {
	    /* Prepending node to parent would create a
	     * loop; in lbml_find_parent, we just omit making
	     * the link, but here we really want to link the
	     * message's node to its parent, so we'll fix
	     * the tree: unlink the offending child and prepend it
	     * to the node's parent. */
	    g_node_append(node->parent, lbml_unlink(child));
	    break;
	}

    g_node_prepend(parent, lbml_unlink(node));
}

static GNode *
lbml_find_parent(LibBalsaMessage * message, ThreadingInfo * ti)
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
    GNode *parent = ti->mailbox->msg_tree;
    GList *reference;

    for (reference = message->references_for_threading; reference;
	 reference = g_list_next(reference)) {
	gchar *id = reference->data;
	GNode *foo = g_hash_table_lookup(ti->id_table, id);

	if (foo == NULL) {
	    foo = g_node_new(NULL);
	    g_hash_table_insert(ti->id_table, id, foo);
	}

	/* Avoid nasty surprises. */
	if (foo != parent && !g_node_is_ancestor(foo, parent)) {
	    if (foo->parent == ti->mailbox->msg_tree)
		/* foo has the default parent; we'll unlink it, so that
		 * it can be linked to its rightful parent. */
		g_node_unlink(foo);
	    if (G_NODE_IS_ROOT(foo))
		g_node_prepend(parent, foo);
	}

	parent = foo;
    }
    return parent;
}

static void
lbml_insert_node(GNode * node, LibBalsaMessage * message,
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
	LibBalsaMessage *old_message;

	old_message = lbml_get_message(old_node, ti);
	if (old_message && LIBBALSA_MESSAGE_IS_REPLIED(old_message))
	    return;		/* ...without changing hash table entry. */

	if (!old_message
	    /* old_node is a place-holder... */
	    || LIBBALSA_MESSAGE_IS_REPLIED(message)) {
	    /* ...or we want to thread off this instance */
	    while (old_node->children)
		g_node_append(node, lbml_unlink(old_node->children));
	}
    }

    g_hash_table_insert(ti->id_table, message->message_id, node);
}

/* helper (why doesn't g_node_unlink return the node?!) */
static GNode *
lbml_unlink(GNode * node)
{
    g_node_unlink(node);
    return node;
}

static gboolean
lbml_prune(GNode * node, GNode * root)
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

    if (node->data != NULL || node == root)
	return FALSE;

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (node->children != NULL
	&& (node->parent != root || node->children->next == NULL))
	do
	    g_node_prepend(node->parent, lbml_unlink(node->children));
	while (node->children);

    if (node->children == NULL)
	g_node_destroy(node);
#else				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    while (node->children)
	g_node_prepend(node->parent, lbml_unlink(node->children));

    g_node_destroy(node);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    return FALSE;
}

static void
lbml_subject_gather(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMessage *message, *old_message;
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
     *     + If both are dummies, append one's children to the other, and 
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
    g_return_if_fail(message != NULL);

    subject = LIBBALSA_MESSAGE_GET_SUBJECT(message);
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
    g_return_if_fail(old_message != NULL);

    old_subject = LIBBALSA_MESSAGE_GET_SUBJECT(old_message);

    if (old_subject != lbml_chop_re(old_subject)
	&& subject == chopped_subject)
	g_hash_table_insert(ti->subject_table, (gchar *) chopped_subject,
			    node);
}

/* Swap data and children. */
static void
lbml_swap(GNode * node1, GNode * node2)
{
    GNode *tmp_node = g_node_new(NULL);
    gpointer tmp_data;

    while (node1->children)
	g_node_prepend(tmp_node, lbml_unlink(node1->children));
    while (node2->children)
	g_node_prepend(node1, lbml_unlink(node2->children));
    while (tmp_node->children)
	g_node_prepend(node2, lbml_unlink(tmp_node->children));
    g_node_destroy(tmp_node);

    tmp_data = node1->data;
    node1->data = node2->data;
    node2->data = tmp_data;
}

static void
lbml_subject_merge(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMessage *message, *message2;
    const gchar *subject, *subject2;
    const gchar *chopped_subject, *chopped_subject2;
    GNode *node2;

    message = lbml_get_message(node, ti);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (!message)
	message = lbml_get_message(node->children, ti);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    g_return_if_fail(message != NULL);

    subject = LIBBALSA_MESSAGE_GET_SUBJECT(message);
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
	    g_node_prepend(node2, lbml_unlink(node->children));
	g_node_destroy(node);
	return;
    }

    if (node->data == NULL)
	/* node2 should be made a child of node, but unlinking node2
	 * could mess up the foreach, so we'll swap them and fall
	 * through to the next case. */
	lbml_swap(node, node2);

    if (node2->data == NULL) {
	g_node_prepend(node2, lbml_unlink(node));
	return;
    }
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    message2 = lbml_get_message(node2, ti);
    subject2 = LIBBALSA_MESSAGE_GET_SUBJECT(message2);
    chopped_subject2 = lbml_chop_re(subject2);

    if ((subject2 == chopped_subject2) && subject != chopped_subject)
	/* Make node a child of node2. */
	g_node_prepend(node2, lbml_unlink(node));
    else if ((subject2 != chopped_subject2)
	     && subject == chopped_subject) {
	/* Make node2 a child of node; as above, swap them to avoid
	 * unlinking node2. */
	lbml_swap(node, node2);
	g_node_prepend(node2, lbml_unlink(node));
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    } else {
	/* Make both node and node2 children of a new empty node; as
	 * above, swap node2 and the new node to avoid unlinking node2.
	 */
	GNode *new_node = g_node_new(NULL);

	while (node2->children)
	    g_node_prepend(new_node, lbml_unlink(node2->children));
	new_node->data = node2->data;
	node2->data = NULL;
	g_node_prepend(node2, lbml_unlink(node));
	g_node_prepend(node2, new_node);
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
lbml_clear_empty(LibBalsaMailbox * mailbox)
{
    GNode *node = mailbox->msg_tree->children;
    while (node) {
	GNode *next = node->next;
	if (!node->data && !node->children)
	    g_node_destroy(node);
	node = next;
    }
}
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

/* yet another message threading function */

static gboolean lbml_add_message(GNode * msg_tree_node, gpointer data);

static void
lbml_threading_simple(LibBalsaMailbox * mailbox,
		      LibBalsaMailboxThreadingType type)
{
    GSList *p;
    ThreadingInfo ti;

    ti.id_table = g_hash_table_new(g_str_hash, g_str_equal);
    ti.message_list = NULL;
    ti.mailbox = mailbox;
    lbml_make_msg_array(&ti);

    g_node_traverse(mailbox->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		    lbml_add_message, &ti);

    for (p = ti.message_list; p; p = g_slist_next(p)) {
	GNode *child = p->data;
	LibBalsaMessage *child_message = lbml_get_message(child, &ti);
	GList *child_refs = child_message->references_for_threading;
	GNode *parent = NULL;

	if (type == LB_MAILBOX_THREADING_SIMPLE && child_refs != NULL)
	    parent = g_hash_table_lookup(ti.id_table,
					 g_list_last(child_refs)->data);
	if (!parent)
	    parent = mailbox->msg_tree;
	if (parent != child->parent)
	    g_node_prepend(parent, lbml_unlink(child));
    }

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    lbml_clear_empty(mailbox);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    g_slist_free(ti.message_list);
    g_hash_table_destroy(ti.id_table);
    g_free(ti.msg_array);
}

static gboolean
lbml_add_message(GNode * node, gpointer data)
{
    LibBalsaMessage *message;
    ThreadingInfo *ti = data;

    if (G_NODE_IS_ROOT(node))
	return FALSE;

    message = lbml_get_message(node, ti);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (!message)
	return FALSE;
#else				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    g_return_val_if_fail(message != NULL, FALSE);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    ti->message_list = g_slist_prepend(ti->message_list, node);

    if (message->message_id)
	g_hash_table_insert(ti->id_table, message->message_id, node);

    return FALSE;
}
