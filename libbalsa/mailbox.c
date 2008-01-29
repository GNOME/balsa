/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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

#define _XOPEN_SOURCE 500
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "libbalsa-marshal.h"
#include "libbalsa.h"
#include "libbalsa-conf.h"
#include "mailbox-filter.h"
#include "message.h"
#include "misc.h"
#include "libbalsa_private.h"
#include <glib/gi18n.h>

/* Class functions */
static void libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass);
static void libbalsa_mailbox_init(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_dispose(GObject * object);
static void libbalsa_mailbox_finalize(GObject * object);

static void libbalsa_mailbox_real_release_message (LibBalsaMailbox * mailbox,
                                                   LibBalsaMessage * message);
static gboolean
libbalsa_mailbox_real_messages_copy(LibBalsaMailbox * mailbox,
                                    GArray * msgnos,
                                    LibBalsaMailbox * dest, GError **err);
static gboolean libbalsa_mailbox_real_can_do(LibBalsaMailbox* mbox,
                                             enum LibBalsaMailboxCapability c);
static void libbalsa_mailbox_real_sort(LibBalsaMailbox* mbox,
                                       GArray *sort_array);
static gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox  *mailbox,
                                                LibBalsaCondition *condition);
static void libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
                                              const gchar * group);
static void libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
                                              const gchar * group);
static gboolean libbalsa_mailbox_real_close_backend (LibBalsaMailbox *
                                                     mailbox);
#if BALSA_USE_THREADS
static void libbalsa_mailbox_real_lock_store(LibBalsaMailbox * mailbox,
                                             gboolean lock);
#endif                          /* BALSA_USE_THREADS */

/* SIGNALS MEANINGS :
   - CHANGED: notification signal sent by the mailbox to allow the
   frontend to keep in sync. This signal is used when messages are added
   to or removed from the mailbox. This is used when eg the mailbox
   loads new messages (check new mails) or the mailbox is expunged.
   Also when the unread message count might have changed.
   - MESSAGE_EXPUNGED: sent when a message is expunged.  This signal is
   used to update lists of msgnos when messages are renumbered.
*/

enum {
    CHANGED,
    MESSAGE_EXPUNGED,
    PROGRESS_NOTIFY,
    LAST_SIGNAL
};

enum {
    ROW_CHANGED,
    ROW_DELETED,
    ROW_HAS_CHILD_TOGGLED,
    ROW_INSERTED,
    ROWS_REORDERED,
    LAST_MODEL_SIGNAL
};

static GObjectClass *parent_class = NULL;
static guint libbalsa_mailbox_signals[LAST_SIGNAL];
static guint libbalsa_mbox_model_signals[LAST_MODEL_SIGNAL];

/* GtkTreeModel function prototypes */
static void  mbox_model_init(GtkTreeModelIface *iface);

/* GtkTreeDragSource function prototypes */
static void  mbox_drag_source_init(GtkTreeDragSourceIface *iface);

/* GtkTreeSortable function prototypes */
static void  mbox_sortable_init(GtkTreeSortableIface *iface);

GType
libbalsa_mailbox_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
        static const GTypeInfo mailbox_info = {
            sizeof(LibBalsaMailboxClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) libbalsa_mailbox_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibBalsaMailbox),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) libbalsa_mailbox_init
        };

        static const GInterfaceInfo mbox_model_info = {
            (GInterfaceInitFunc) mbox_model_init,
            NULL,
            NULL
        };
    
        static const GInterfaceInfo mbox_drag_source_info = {
            (GInterfaceInitFunc) mbox_drag_source_init,
            NULL,
            NULL
        };

        static const GInterfaceInfo mbox_sortable_info = {
            (GInterfaceInitFunc) mbox_sortable_init,
            NULL,
            NULL
        };
    
        mailbox_type =
            g_type_register_static(G_TYPE_OBJECT, "LibBalsaMailbox",
                                   &mailbox_info, 0);
        g_type_add_interface_static(mailbox_type,
                                    GTK_TYPE_TREE_MODEL,
                                    &mbox_model_info);
        g_type_add_interface_static(mailbox_type,
                                    GTK_TYPE_TREE_DRAG_SOURCE,
                                    &mbox_drag_source_info);
        g_type_add_interface_static(mailbox_type,
                                    GTK_TYPE_TREE_SORTABLE,
                                    &mbox_sortable_info);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    /* This signal is emitted by the mailbox when new messages are
       retrieved (check mail or opening of the mailbox). This is used
       by GUI to sync on the mailbox content (see BalsaIndex)
    */   
    libbalsa_mailbox_signals[CHANGED] =
        g_signal_new("changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    libbalsa_mailbox_signals[MESSAGE_EXPUNGED] =
        g_signal_new("message-expunged",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     message_expunged),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1,
                     G_TYPE_INT);

    libbalsa_mailbox_signals[PROGRESS_NOTIFY] =
        g_signal_new("progress-notify",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     progress_notify),
                     NULL, NULL,
                     libbalsa_VOID__INT_INT_INT_STRING, G_TYPE_NONE,
                     4, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);

    object_class->dispose = libbalsa_mailbox_dispose;
    object_class->finalize = libbalsa_mailbox_finalize;

    /* Signals */
    klass->progress_notify = NULL;
    klass->changed = NULL;
    klass->message_expunged = NULL;

    /* Virtual functions */
    klass->open_mailbox = NULL;
    klass->close_mailbox = NULL;
    klass->get_message = NULL;
    klass->prepare_threading = NULL;
    klass->fetch_message_structure = NULL;
    klass->fetch_headers           = NULL;
    klass->release_message = libbalsa_mailbox_real_release_message;
    klass->get_message_part = NULL;
    klass->get_message_stream = NULL;
    klass->messages_change_flags = NULL;
    klass->messages_copy  = libbalsa_mailbox_real_messages_copy;
    klass->can_do = libbalsa_mailbox_real_can_do;
    klass->set_threading = NULL;
    klass->update_view_filter = NULL;
    klass->sort = libbalsa_mailbox_real_sort;
    klass->check = NULL;
    klass->message_match = NULL;
    klass->can_match = libbalsa_mailbox_real_can_match;
    klass->save_config  = libbalsa_mailbox_real_save_config;
    klass->load_config  = libbalsa_mailbox_real_load_config;
    klass->close_backend  = libbalsa_mailbox_real_close_backend;
    klass->total_messages = NULL;
    klass->duplicate_msgnos = NULL;
#if BALSA_USE_THREADS
    klass->lock_store  = libbalsa_mailbox_real_lock_store;
#endif                          /* BALSA_USE_THREADS */
}

static void
libbalsa_mailbox_init(LibBalsaMailbox * mailbox)
{
    mailbox->lock = FALSE;
    mailbox->is_directory = FALSE;

    mailbox->config_prefix = NULL;
    mailbox->name = NULL;
    mailbox->url = NULL;

    mailbox->open_ref = 0;
    mailbox->has_unread_messages = FALSE;
    mailbox->unread_messages = 0;

    mailbox->readonly = FALSE;
    mailbox->disconnected = FALSE;

    mailbox->filters=NULL;
    mailbox->filters_loaded = FALSE;
    mailbox->view=NULL;
    /* mailbox->stamp is incremented before we use it, so it won't be
     * zero for a long, long time... */
    mailbox->stamp = g_random_int() / 2;

    mailbox->no_reassemble = FALSE;
}

/*
 * libbalsa_mailbox_dispose:
 *
 * called just before finalize, when ref_count is about to be
 * decremented to 0
 */
static void
libbalsa_mailbox_dispose(GObject * object)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);

    while (mailbox->open_ref > 0)
        libbalsa_mailbox_close(mailbox, FALSE);

    G_OBJECT_CLASS(parent_class)->dispose(object);
}


static gchar*
get_from_field(LibBalsaMessage *message)
{
    gboolean append_dots = FALSE;
    const gchar *name_str = NULL;
    gchar *from;
    const InternetAddressList *address_list = NULL;

    g_return_val_if_fail(message->mailbox, NULL);
    if (message->mailbox->view &&
        message->mailbox->view->show == LB_MAILBOX_SHOW_TO) {
        if (message->headers && message->headers->to_list) {
            address_list = message->headers->to_list;
            append_dots = internet_address_list_length(address_list) > 1;
        }
    } else {
        if (message->headers && message->headers->from)
            address_list = message->headers->from;
    }
    name_str = libbalsa_address_get_name_from_list(address_list);
    if(!name_str)           /* !addy, or addy contained no name/address */
        name_str = "";
    
    from = append_dots ? g_strconcat(name_str, ",...", NULL)
                       : g_strdup(name_str);
    libbalsa_utf8_sanitize(&from, TRUE, NULL);
    return from;
}

static void
lbm_index_entry_populate_from_msg(LibBalsaMailboxIndexEntry * entry,
                                  LibBalsaMessage * msg)
{
    entry->from          = get_from_field(msg);
    entry->subject       = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(msg));
    entry->msg_date      = msg->headers->date;
    entry->internal_date = 0; /* FIXME */
    entry->status_icon   = libbalsa_get_icon_from_flags(msg->flags);
    entry->attach_icon   = libbalsa_message_get_attach_icon(msg);
    entry->size          = msg->length;
    entry->unseen        = LIBBALSA_MESSAGE_IS_UNREAD(msg);
#ifdef BALSA_USE_THREADS
    entry->idle_pending  = 0;
#endif                          /*BALSA_USE_THREADS */
    libbalsa_mailbox_msgno_changed(msg->mailbox, msg->msgno);
}

LibBalsaMailboxIndexEntry*
libbalsa_mailbox_index_entry_new_from_msg(LibBalsaMessage *msg)
{
    LibBalsaMailboxIndexEntry *entry = g_new(LibBalsaMailboxIndexEntry,1);
    lbm_index_entry_populate_from_msg(entry, msg);
    return entry;
}

#ifdef BALSA_USE_THREADS
static LibBalsaMailboxIndexEntry*
lbm_index_entry_new_pending(void)
{
    LibBalsaMailboxIndexEntry *entry = g_new(LibBalsaMailboxIndexEntry,1);
    entry->idle_pending = 1;
    return entry;
}
#endif                          /*BALSA_USE_THREADS */

void
libbalsa_mailbox_index_entry_free(LibBalsaMailboxIndexEntry *entry)
{
    if(entry) {
#ifdef BALSA_USE_THREADS
        if (!entry->idle_pending)
#endif                          /*BALSA_USE_THREADS */
        {
            g_free(entry->from);
            g_free(entry->subject);
        }
        g_free(entry);
    }
}

#ifdef BALSA_USE_THREADS
#  define VALID_ENTRY(entry) \
    ((entry) && !((LibBalsaMailboxIndexEntry *) (entry))->idle_pending)
#else                           /*BALSA_USE_THREADS */
#  define VALID_ENTRY(entry) ((entry) != NULL)
#endif                          /*BALSA_USE_THREADS */

void
libbalsa_mailbox_index_set_flags(LibBalsaMailbox *mailbox,
                                 unsigned msgno, LibBalsaMessageFlag f)
{
    LibBalsaMailboxIndexEntry *entry;

    if (msgno > mailbox->mindex->len)
        return;

    entry = g_ptr_array_index(mailbox->mindex, msgno-1);
    if (VALID_ENTRY(entry)) {
        entry->status_icon = 
            libbalsa_get_icon_from_flags(f);
        entry->unseen = f & LIBBALSA_MESSAGE_FLAG_NEW;
        libbalsa_mailbox_msgno_changed(mailbox, msgno);
    }
}

/* libbalsa_mailbox_finalize:
   destroys mailbox. Must leave it in sane state.
*/

static void
libbalsa_mailbox_finalize(GObject * object)
{
    LibBalsaMailbox *mailbox;

    g_return_if_fail(object != NULL);

    mailbox = LIBBALSA_MAILBOX(object);

    g_free(mailbox->config_prefix);
    mailbox->config_prefix = NULL;

    g_free(mailbox->name);
    mailbox->name = NULL;

    g_free(mailbox->url);
    mailbox->url = NULL;

    libbalsa_condition_unref(mailbox->view_filter);
    mailbox->view_filter = NULL;

    libbalsa_condition_unref(mailbox->persistent_view_filter);
    mailbox->persistent_view_filter = NULL;

    g_slist_foreach(mailbox->filters, (GFunc) g_free, NULL);
    g_slist_free(mailbox->filters);
    mailbox->filters = NULL;
    mailbox->filters_loaded = FALSE;

#ifdef BALSA_USE_THREADS
    if (mailbox->msgnos_pending) {
        g_array_free(mailbox->msgnos_pending, TRUE);
        mailbox->msgnos_pending = NULL;
    }
#endif                          /*BALSA_USE_THREADS */

    /* The LibBalsaMailboxView is owned by balsa_app.mailbox_views. */
    mailbox->view = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/* Create a new mailbox by loading it from a config entry... */
LibBalsaMailbox *
libbalsa_mailbox_new_from_config(const gchar * group)
{
    gchar *type_str;
    GType type;
    gboolean got_default;
    LibBalsaMailbox *mailbox;

    libbalsa_conf_push_group(group);
    type_str = libbalsa_conf_get_string_with_default("Type", &got_default);

    if (got_default) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Cannot load mailbox %s"), group);
        libbalsa_conf_pop_group();
        return NULL;
    }
    type = g_type_from_name(type_str);
    if (type == 0) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("No such mailbox type: %s"), type_str);
        g_free(type_str);
        libbalsa_conf_pop_group();
        return NULL;
    }

    /* Handle Local mailboxes. 
     * They are now separate classes for each type 
     * FIXME: This should be removed in som efuture release.
     */
    if ( type == LIBBALSA_TYPE_MAILBOX_LOCAL ) {
        gchar *path = libbalsa_conf_get_string("Path");
        type = libbalsa_mailbox_type_from_path(path);
        if (type != G_TYPE_OBJECT)
            libbalsa_conf_set_string("Type", g_type_name(type));
        else
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Bad local mailbox path \"%s\""), path);
    }
    mailbox = (type != G_TYPE_OBJECT ? g_object_new(type, NULL) : NULL);
    if (mailbox == NULL)
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Could not create a mailbox of type %s"),
                             type_str);
    else
	LIBBALSA_MAILBOX_GET_CLASS(mailbox)->load_config(mailbox, group);

    libbalsa_conf_pop_group();
    g_free(type_str);

    return mailbox;
}

static void
libbalsa_mailbox_free_mindex(LibBalsaMailbox *mailbox)
{
    if(mailbox->mindex) {
        unsigned i;
        /* we could have used g_ptr_array_foreach but it is >=2.4.0 */
        for(i=0; i<mailbox->mindex->len; i++)
            libbalsa_mailbox_index_entry_free
                (g_ptr_array_index(mailbox->mindex, i));
        g_ptr_array_free(mailbox->mindex, TRUE);
        mailbox->mindex = NULL;
    }
}

static gboolean lbm_set_threading(LibBalsaMailbox * mailbox,
                                  LibBalsaMailboxThreadingType
                                  thread_type);
gboolean
libbalsa_mailbox_open(LibBalsaMailbox * mailbox, GError **err)
{
    gboolean retval;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    libbalsa_lock_mailbox(mailbox);

    if (mailbox->open_ref > 0) {
        mailbox->open_ref++;
	libbalsa_mailbox_check(mailbox);
        retval = TRUE;
    } else {
	LibBalsaMailboxState saved_state;

        mailbox->stamp++;
        if(mailbox->mindex) g_warning("mindex set - I leak memory");
        mailbox->mindex = g_ptr_array_new();
        if(mailbox->nodes) g_warning("nodes set - I leak memory");
        mailbox->nodes = g_ptr_array_new();

	saved_state = mailbox->state;
	mailbox->state = LB_MAILBOX_STATE_OPENING;
        retval =
            LIBBALSA_MAILBOX_GET_CLASS(mailbox)->open_mailbox(mailbox, err);
        if(retval) {
            mailbox->open_ref++;
	    mailbox->state = LB_MAILBOX_STATE_OPEN;
	} else {
	    mailbox->state = saved_state;
            libbalsa_mailbox_free_mindex(mailbox);
            g_ptr_array_free(mailbox->nodes, TRUE);
            mailbox->nodes = NULL;
	}
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

/* libbalsa_mailbox_is_valid:
   mailbox is valid when:
   a). it is closed, b). it is open and has proper client context.
*/
gboolean
libbalsa_mailbox_is_valid(LibBalsaMailbox * mailbox)
{
    if(mailbox->open_ref == 0) return TRUE;
    if(MAILBOX_CLOSED(mailbox)) return FALSE;
    return TRUE;
}

gboolean
libbalsa_mailbox_is_open(LibBalsaMailbox *mailbox)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return mailbox->open_ref>0; /* this will break unlisted mailbox types */
}

/* 
 * The msg_tree is implemented using GSequence.  The messages at a given
 * level are contained in a GSequence, whose data member is a
 * LibBalsaMailboxSequenceInfo.
 */
typedef struct {
    guint          msgno;
    GSequenceIter *parent;
    GSequence     *children;
    GPtrArray     *nodes;
} LibBalsaMailboxSequenceInfo;

static LibBalsaMailboxSequenceInfo *
lbm_node_info_new(LibBalsaMailbox * mailbox, guint msgno,
                  GSequenceIter * parent)
{
    LibBalsaMailboxSequenceInfo *info =
        g_new(LibBalsaMailboxSequenceInfo, 1);

    info->msgno    = msgno;
    info->parent   = parent;
    info->children = NULL;
    info->nodes    = mailbox->nodes;

    return info;
}

static void
lbm_node_info_free(LibBalsaMailboxSequenceInfo * info)
{
    if (info->children)
        g_sequence_free(info->children);
    if (info->msgno <= info->nodes->len)
        g_ptr_array_index(info->nodes, info->msgno - 1) = NULL;
    g_free(info);
}

guint
libbalsa_mailbox_get_msgno(GSequenceIter * node)
{
    LibBalsaMailboxSequenceInfo *info;

    g_return_val_if_fail(node != NULL, 0);

    info = g_sequence_get(node);

    return info->msgno;
}

GSequenceIter *
libbalsa_mailbox_get_parent(GSequenceIter * node)
{
    LibBalsaMailboxSequenceInfo *info;

    g_return_val_if_fail(node != NULL, NULL);

    info = g_sequence_get(node);

    return info->parent;
}

static void
lbm_node_set_parent(GSequenceIter * node, GSequenceIter * parent)
{
    LibBalsaMailboxSequenceInfo *info = g_sequence_get(node);
    info->parent = parent;
}

static GSequence *
lbm_node_get_children(GSequenceIter * node)
{
    LibBalsaMailboxSequenceInfo *info = g_sequence_get(node);

    if (info->children && g_sequence_get_length(info->children) == 0) {
        g_sequence_free(info->children);
        info->children = NULL;
    }

    return info->children;
}

static GSequence *
lbm_node_init_children(GSequenceIter * node)
{
    LibBalsaMailboxSequenceInfo *info = g_sequence_get(node);
    if (!info->children)
        info->children =
            g_sequence_new((GDestroyNotify) lbm_node_info_free);
    return info->children;
}

static GSequenceIter *
lbm_node_find(LibBalsaMailbox * mailbox, guint msgno)
{
    return msgno <= mailbox->nodes->len ?
        g_ptr_array_index(mailbox->nodes, msgno - 1) : NULL;
}

static gboolean
lbm_nodes_traverse(GSequence * seq, GTraverseType type,
                   LBMTraverseFunc func, gpointer data)
{
    GSequenceIter *node = g_sequence_get_begin_iter(seq);
    GSequenceIter *end  = g_sequence_get_end_iter(seq);

    while (node != end) {
        GSequenceIter *next = g_sequence_iter_next(node);
        GSequence *children = lbm_node_get_children(node);

        if (children) {
            if (type == G_PRE_ORDER) {
                if ((*func) (node, data)
                    || lbm_nodes_traverse(children, type, func, data))
                    return TRUE;
            } else {
                if (lbm_nodes_traverse(children, type, func, data)
                    || (*func) (node, data))
                    return TRUE;
            }
        } else if ((*func) (node, data))
            return TRUE;

        node = next;
    }

    return FALSE;
}

void
libbalsa_mailbox_traverse(LibBalsaMailbox * mailbox, GTraverseType type,
                          LBMTraverseFunc func, gpointer data)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(mailbox->msg_tree != NULL);
    g_return_if_fail(type == G_PRE_ORDER || type == G_POST_ORDER);
    g_return_if_fail(func != NULL);

    lbm_nodes_traverse(mailbox->msg_tree, type, func, data);
}

guint
libbalsa_mailbox_n_nodes(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    if (mailbox->nodes) {
        guint i, n = 0;

        for (i = 0; i < mailbox->nodes->len; i++)
            if (g_ptr_array_index(mailbox->nodes, i))
                ++n;

        return n;
    }

    return libbalsa_mailbox_total_messages(mailbox);
}

static gboolean
lbm_node_is_ancestor(GSequenceIter * node, GSequenceIter * descendant)
{
    while (descendant) {
        if (node == descendant)
            return TRUE;
        descendant = libbalsa_mailbox_get_parent(descendant);
    }

    return FALSE;
}

/* LBMNode iterators */
static GSequenceIter *
lbm_next(GSequenceIter * node)
{
    GSequence *children;
    GSequenceIter *last;
    /* next is:     our first child, if we have one;
     *              else our sibling, if we have one;
     *              else the sibling of our first ancestor who has
     *              one.  */
    if ((children = lbm_node_get_children(node)))
        return g_sequence_get_begin_iter(children);

    do {
        last = node;
        node = g_sequence_iter_next(node);
        if (!g_sequence_iter_is_end(node))
            return node;
        node = libbalsa_mailbox_get_parent(last);
    } while (node);

    return NULL;
}

static GSequenceIter *
lbm_last_descendant(GSequenceIter * node)
{
    GSequence *children;

    while ((children = lbm_node_get_children(node)))
        node = g_sequence_iter_prev(g_sequence_get_end_iter(children));

    return node;
}

static GSequenceIter *
lbm_prev(GSequenceIter * node)
{
    /* previous is: if we have a sibling,
     *                      if it has children, its last descendant;
     *                      else the sibling;
     *              else our parent. */
    if (!g_sequence_iter_is_begin(node))
        return lbm_last_descendant(g_sequence_iter_prev(node));

    if (libbalsa_mailbox_get_parent(node))
        return libbalsa_mailbox_get_parent(node);

    return NULL;
}

static void
lbm_node_cache(LibBalsaMailbox * mailbox, guint msgno, GSequenceIter * node)
{
    while (msgno > mailbox->nodes->len)
        g_ptr_array_add(mailbox->nodes, NULL);
    g_ptr_array_index(mailbox->nodes, msgno - 1) = node;
}

/*
 * End of msg_tree functions.
 */

void
libbalsa_mailbox_close(LibBalsaMailbox * mailbox, gboolean expunge)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(MAILBOX_OPEN(mailbox));

    libbalsa_lock_mailbox(mailbox);

    if (--mailbox->open_ref == 0) {
	mailbox->state = LB_MAILBOX_STATE_CLOSING;
        /* do not try expunging read-only mailboxes, it's a waste of time */
        expunge = expunge && !mailbox->readonly;
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->close_mailbox(mailbox, expunge);
        gdk_threads_enter();
        if(mailbox->msg_tree) {
            g_sequence_free(mailbox->msg_tree);
            mailbox->msg_tree = NULL;
        }
        gdk_threads_leave();
        libbalsa_mailbox_free_mindex(mailbox);
        g_ptr_array_free(mailbox->nodes, TRUE);
        mailbox->nodes = NULL;
        mailbox->stamp++;
	mailbox->state = LB_MAILBOX_STATE_CLOSED;
    }

    libbalsa_unlock_mailbox(mailbox);
}

void
libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox * mailbox,
                                          gboolean has_unread)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    mailbox->has_unread_messages = (has_unread != FALSE);
    libbalsa_mailbox_changed(mailbox);
}

/* libbalsa_mailbox_progress_notify:
   there has been a progress in current operation.
*/
void
libbalsa_mailbox_progress_notify(LibBalsaMailbox * mailbox,
                                 int type, int prog, int tot, const gchar* msg)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_signal_emit(G_OBJECT(mailbox),
                  libbalsa_mailbox_signals[PROGRESS_NOTIFY],
                  0, type, prog, tot, msg);
}

#define LB_MAILBOX_CHECK_ID_KEY "libbalsa-mailbox-check-id"

void
libbalsa_mailbox_check(LibBalsaMailbox * mailbox)
{
    guint id;
    GSList *unthreaded;

    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    libbalsa_lock_mailbox(mailbox);

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(mailbox),
                                            LB_MAILBOX_CHECK_ID_KEY));
    if (id) {
	/* Remove scheduled idle callback. */
	g_source_remove(id);
	g_object_set_data(G_OBJECT(mailbox), LB_MAILBOX_CHECK_ID_KEY,
			  GUINT_TO_POINTER(0));
    }

    unthreaded = NULL;
    if (MAILBOX_OPEN(mailbox))
        g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_UNTHREADED,
                          &unthreaded);
    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->check(mailbox);
    g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_UNTHREADED,
                      unthreaded);
    if (unthreaded) {
        lbm_set_threading(mailbox, mailbox->view->threading_type);
        g_slist_free(unthreaded);
        g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_UNTHREADED,
                          NULL);
    }

    libbalsa_unlock_mailbox(mailbox);

#ifdef BALSA_USE_THREADS
    pthread_testcancel();
#endif
}

void
libbalsa_mailbox_changed(LibBalsaMailbox * mailbox)
{
    if (!g_signal_has_handler_pending
        (mailbox, libbalsa_mailbox_signals[CHANGED], 0, TRUE))
        /* No one cares, so don't set any message counts--that might
         * cause mailbox->view to be created. */
        return;

    if (MAILBOX_OPEN(mailbox)) {
        /* Both counts are valid. */
        libbalsa_mailbox_set_total(mailbox,
                                   libbalsa_mailbox_total_messages
                                   (mailbox));
        libbalsa_mailbox_set_unread(mailbox, mailbox->unread_messages);
    } else if (mailbox->has_unread_messages
               && libbalsa_mailbox_get_unread(mailbox) == 0) {
        /* Mail has arrived in a closed mailbox since our last check;
         * total is unknown, but mailbox->has_unread_messages is valid. */
        libbalsa_mailbox_set_total(mailbox, -1);
        libbalsa_mailbox_set_unread(mailbox, 1);
    }

    gdk_threads_enter();
    g_signal_emit(mailbox, libbalsa_mailbox_signals[CHANGED], 0);
    gdk_threads_leave();
}

/* libbalsa_mailbox_message_match:
 * Tests if message with msgno matches the conditions cached in the
 * search_iter: this is used
   by the search code. It is a "virtual method", indeed IMAP has a
   special way to implement it for speed/bandwidth reasons
 */
gboolean
libbalsa_mailbox_message_match(LibBalsaMailbox * mailbox,
                               guint msgno,
                               LibBalsaMailboxSearchIter * search_iter)
{
    gboolean match;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(msgno <= libbalsa_mailbox_total_messages(mailbox),
                         FALSE);

    if (libbalsa_condition_is_flag_only(search_iter->condition,
                                        mailbox, msgno, &match))
        return match;

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->message_match(mailbox,
                                                              msgno,
                                                              search_iter);
}

gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox  *mailbox,
                                         LibBalsaCondition *condition)
{
    /* By default : all filters is OK */
    return TRUE;
}

gboolean
libbalsa_mailbox_can_match(LibBalsaMailbox * mailbox,
                           LibBalsaCondition *condition)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->can_match(mailbox,
                                                          condition);
}

/* Helper function to run the "on reception" filters on a mailbox */

void
libbalsa_mailbox_run_filters_on_reception(LibBalsaMailbox * mailbox)
{
    GSList *filters;
    guint progress_count;
    GSList *lst;
    static LibBalsaCondition *recent_undeleted;
    gchar *text;
    guint total;
    guint progress_total;
    LibBalsaProgress progress;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (!mailbox->filters_loaded) {
        config_mailbox_filters_load(mailbox);
        mailbox->filters_loaded = TRUE;
    }

    filters = libbalsa_mailbox_filters_when(mailbox->filters,
                                            FILTER_WHEN_INCOMING);

    if (!filters)
        return;
    if (!filters_prepare_to_run(filters)) {
        g_slist_free(filters);
        return;
    }

    progress_count = 0;
    for (lst = filters; lst; lst = lst->next) {
        LibBalsaFilter *filter = lst->data;

        if (filter->condition
            && !libbalsa_condition_is_flag_only(filter->condition, NULL, 0,
                                                NULL))
            ++progress_count;
    }

    libbalsa_lock_mailbox(mailbox);
    if (!recent_undeleted)
        recent_undeleted =
            libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                            libbalsa_condition_new_flag_enum
                                            (FALSE,
                                             LIBBALSA_MESSAGE_FLAG_RECENT),
                                            libbalsa_condition_new_flag_enum
                                            (TRUE,
                                             LIBBALSA_MESSAGE_FLAG_DELETED));

    text = g_strdup_printf(_("Applying filter rules to %s"), mailbox->name);
    total = libbalsa_mailbox_total_messages(mailbox);
    progress_total = progress_count * total;
        libbalsa_progress_set_text(&progress, text, progress_total);
    g_free(text);

    progress_count = 0;
    for (lst = filters; lst; lst = lst->next) {
        LibBalsaFilter *filter = lst->data;
        gboolean use_progress;
        LibBalsaCondition *cond;
        LibBalsaMailboxSearchIter *search_iter;
        guint msgno;
        GArray *msgnos;

        if (!filter->condition)
            continue;

        use_progress = !libbalsa_condition_is_flag_only(filter->condition,
                                                        NULL, 0, NULL);

        cond = libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                               recent_undeleted,
                                               filter->condition);
        search_iter = libbalsa_mailbox_search_iter_new(cond);
        libbalsa_condition_unref(cond);

        msgnos = g_array_new(FALSE, FALSE, sizeof(guint));
        for (msgno = 1; msgno <= total; msgno++) {
            if (libbalsa_mailbox_message_match(mailbox, msgno, search_iter))
                g_array_append_val(msgnos, msgno);
            if (use_progress)
                libbalsa_progress_set_fraction(&progress,
                                               ((gdouble) ++progress_count)
                                               /
                                               ((gdouble) progress_total));
        }
        libbalsa_mailbox_search_iter_free(search_iter);

        libbalsa_mailbox_register_msgnos(mailbox, msgnos);
        libbalsa_filter_mailbox_messages(filter, mailbox, msgnos);
        libbalsa_mailbox_unregister_msgnos(mailbox, msgnos);
        g_array_free(msgnos, TRUE);
    }
    libbalsa_progress_set_text(&progress, NULL, 0);
    libbalsa_unlock_mailbox(mailbox);

    g_slist_free(filters);
}

void
libbalsa_mailbox_save_config(LibBalsaMailbox * mailbox,
                             const gchar * group)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    /* These are incase this section was used for another
     * type of mailbox that has now been deleted...
     */
    g_free(mailbox->config_prefix);
    mailbox->config_prefix = g_strdup(group);
    libbalsa_conf_private_remove_group(group);
    libbalsa_conf_remove_group(group);

    libbalsa_conf_push_group(group);
    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->save_config(mailbox, group);
    libbalsa_conf_pop_group();
}

static void
libbalsa_mailbox_real_release_message(LibBalsaMailbox * mailbox,
                                      LibBalsaMessage * message)
{
    if (message->mime_msg) {
	g_object_unref(message->mime_msg);
	message->mime_msg = NULL;
    }
}

struct MsgCopyData {
    LibBalsaMailbox *src_mailbox;
    GArray *msgnos;
    GMimeStream *stream;
    guint current_idx;
    guint copied_cnt;
    LibBalsaProgress progress;
};

static gboolean
copy_iterator(LibBalsaMessageFlag *flags, GMimeStream **stream, void * arg)
{
    struct MsgCopyData *mcd = (struct MsgCopyData*)arg;
    guint msgno;
    gboolean (*msgno_has_flags)(LibBalsaMailbox *, guint,
				LibBalsaMessageFlag, LibBalsaMessageFlag);
    LibBalsaMailbox *mailbox = mcd->src_mailbox;
	
    if(mcd->current_idx >= mcd->msgnos->len)
	return FALSE; /* no more messages */
	
    if(mcd->stream) {
	g_object_unref(mcd->stream);
	mcd->stream = NULL;
    }
    msgno_has_flags = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->msgno_has_flags;
    msgno = g_array_index(mcd->msgnos, guint, mcd->current_idx);

    libbalsa_progress_set_fraction(&mcd->progress, 
				   ((gdouble) (mcd->current_idx + 1)) /
				   ((gdouble) mcd->msgnos->len));
    mcd->current_idx++;

    *flags = 0;
    /* Copy flags. */
    if (msgno_has_flags(mailbox, msgno, LIBBALSA_MESSAGE_FLAG_NEW, 0))
	*flags |= LIBBALSA_MESSAGE_FLAG_NEW;
    if (msgno_has_flags
	(mailbox, msgno, LIBBALSA_MESSAGE_FLAG_REPLIED, 0))
	*flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
    if (msgno_has_flags
	(mailbox, msgno, LIBBALSA_MESSAGE_FLAG_FLAGGED, 0))
	*flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
    if (msgno_has_flags
	(mailbox, msgno, LIBBALSA_MESSAGE_FLAG_DELETED, 0))
	*flags |= LIBBALSA_MESSAGE_FLAG_DELETED;

    /* Copy stream */
    *stream = libbalsa_mailbox_get_message_stream(mailbox, msgno);
    if(!*stream) {
	printf("Connection broken for message %u\n",
	       (unsigned)msgno);
	return FALSE;
    }

    return TRUE;
}

/* Default method; imap backend replaces with its own method, optimized
 * for server-side copy, but falls back to this one if it's not a
 * server-side copy. */
static void lbm_queue_check(LibBalsaMailbox * mailbox);
static gboolean
libbalsa_mailbox_real_messages_copy(LibBalsaMailbox * mailbox,
                                    GArray * msgnos,
                                    LibBalsaMailbox * dest, GError ** err)
{
    gchar *text;
    guint successfully_copied;
    struct MsgCopyData mcd;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(dest), FALSE);
    g_return_val_if_fail(dest != mailbox, FALSE);

    text = g_strdup_printf(_("Copying from %s to %s"), mailbox->name,
                           dest->name);
    mcd.progress = LIBBALSA_PROGRESS_INIT;
    libbalsa_progress_set_text(&mcd.progress, text, msgnos->len);
    g_free(text);

    mcd.src_mailbox = mailbox;
    mcd.msgnos = msgnos;
    mcd.stream = NULL;
    mcd.current_idx = 0;
    mcd.copied_cnt = 0;
    successfully_copied =
	LIBBALSA_MAILBOX_GET_CLASS(dest)->add_messages(dest,
						       copy_iterator,
						       &mcd,
						       err);
    if(mcd.stream)
	g_object_unref(mcd.stream);

    libbalsa_progress_set_text(&mcd.progress, NULL, 0);

    if (successfully_copied)
        /* Some messages copied. */
        lbm_queue_check(dest);

    return successfully_copied == msgnos->len;
}

static gint mbox_compare_func(const SortTuple * a,
                              const SortTuple * b,
                              LibBalsaMailbox * mbox);

static void
libbalsa_mailbox_real_sort(LibBalsaMailbox* mbox, GArray *sort_array)
{
    /* Sort the array */
    g_array_sort_with_data(sort_array,
                           (GCompareDataFunc) mbox_compare_func, mbox);
}

static void
libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
                                  const gchar * group)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    libbalsa_conf_set_string("Type",
                            g_type_name(G_OBJECT_TYPE(mailbox)));
    libbalsa_conf_set_string("Name", mailbox->name);
}

static void
libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
                                  const gchar * group)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_free(mailbox->config_prefix);
    mailbox->config_prefix = g_strdup(group);

    g_free(mailbox->name);
    mailbox->name = libbalsa_conf_get_string("Name=Mailbox");
}

static gboolean
libbalsa_mailbox_real_close_backend(LibBalsaMailbox * mailbox)
{
    return TRUE;                /* Default is noop. */
}

#if BALSA_USE_THREADS
static void
libbalsa_mailbox_real_lock_store(LibBalsaMailbox * mailbox, gboolean lock)
{
    /* Default is noop. */
}
#endif                          /* BALSA_USE_THREADS */

GType
libbalsa_mailbox_type_from_path(const gchar * path)
/* libbalsa_get_mailbox_storage_type:
   returns one of LIBBALSA_TYPE_MAILBOX_IMAP,
   LIBBALSA_TYPE_MAILBOX_MAILDIR, LIBBALSA_TYPE_MAILBOX_MH,
   LIBBALSA_TYPE_MAILBOX_MBOX.  G_TYPE_OBJECT on error or a directory.
 */
{
    struct stat st;
    char tmp[_POSIX_PATH_MAX];

    if(strncmp(path, "imap://", 7) == 0)
        return LIBBALSA_TYPE_MAILBOX_IMAP;

    if (stat (path, &st) == -1)
        return G_TYPE_OBJECT;

    if (S_ISDIR (st.st_mode)) {
        /* check for maildir-style mailbox */
        snprintf (tmp, sizeof (tmp), "%s/cur", path);
        if (stat (tmp, &st) == 0 && S_ISDIR (st.st_mode))
            return LIBBALSA_TYPE_MAILBOX_MAILDIR;

        /* check for mh-style mailbox */
        snprintf (tmp, sizeof (tmp), "%s/.mh_sequences", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

        snprintf (tmp, sizeof (tmp), "%s/.xmhcache", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

        snprintf (tmp, sizeof (tmp), "%s/.mew_cache", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

        snprintf (tmp, sizeof (tmp), "%s/.mew-cache", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

        /* 
         * ok, this isn't an mh folder, but mh mode can be used to read
         * Usenet news from the spool. ;-) 
         */

        snprintf (tmp, sizeof (tmp), "%s/.overview", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

    } else {
        /* Minimal check for an mbox */
        gint fd;

        if ((fd = open(path, O_RDONLY)) >= 0) {
            gchar buf[5];
            guint len = read(fd, buf, sizeof buf);
            close(fd);
            if (len == 0
                || (len == sizeof buf
                    && strncmp(buf, "From ", sizeof buf) == 0))
                return LIBBALSA_TYPE_MAILBOX_MBOX;
        }
    }

    /* This is not a mailbox */
    return G_TYPE_OBJECT;
}

/* Each of the next three methods emits a signal that will be caught by
 * a GtkTreeView, so the emission must be made holding the gdk lock.
 */

static LibBalsaMailboxIndexEntry *lbm_get_index_entry(LibBalsaMailbox *
                                                      lmm,
                                                      GSequenceIter *
                                                      node);
/* Does the node (non-NULL) have unseen children? */
static gboolean
lbm_node_has_unseen_child(LibBalsaMailbox * lmm, GSequenceIter * node)
{
    GSequence *children;

    if ((children = lbm_node_get_children(node))) {
        GSequenceIter *end = g_sequence_get_end_iter(children);

        for (node = g_sequence_get_begin_iter(children);
             node != end; node = g_sequence_iter_next(node)) {
            LibBalsaMailboxIndexEntry *entry =
                /* g_ptr_array_index(lmm->mindex, msgno - 1); ?? */
                lbm_get_index_entry(lmm, node);
            if ((entry && entry->unseen)
                || lbm_node_has_unseen_child(lmm, node))
                return TRUE;
        }
    }

    return FALSE;
}

static void
lbm_msgno_changed(LibBalsaMailbox * mailbox, guint seqno,
                  GtkTreeIter * iter)
{
    GtkTreePath *path;

#define DEBUG FALSE
#if DEBUG
    if (!libbalsa_threads_has_lock())
        g_warning("Thread is not holding gdk lock");
#endif

    if (!mailbox->msg_tree)
        return;

    if (iter->user_data == NULL)
        iter->user_data = lbm_node_find(mailbox, seqno);
    /* trying to modify seqno that is not in the tree?  Possible for
     * filtered views... Perhaps there is nothing to worry about.
     */
    if (iter->user_data == NULL)
	return;

    iter->stamp = mailbox->stamp;
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), iter);
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_CHANGED], 0,
                  path, iter);
    gtk_tree_path_free(path);
}

void
libbalsa_mailbox_msgno_changed(LibBalsaMailbox * mailbox, guint seqno)
{
    GtkTreeIter iter;

    gdk_threads_enter();
    if (!mailbox->msg_tree) {
        gdk_threads_leave();
        return;
    }

    iter.user_data = NULL;
    lbm_msgno_changed(mailbox, seqno, &iter);

    /* Parents' style may need to be changed also. */
    while (iter.user_data) {
        GSequenceIter *parent = libbalsa_mailbox_get_parent(iter.user_data);

        iter.user_data = parent;
        if (parent && (seqno = libbalsa_mailbox_get_msgno(parent)) > 0)
            lbm_msgno_changed(mailbox, seqno, &iter);
    }

    gdk_threads_leave();
}

/*
 * Create a node for seqno, after sibling and as a child of parent.
 * If sibling is NULL, the node will be the first child of parent.
 * If parent is NULL, the node will be at the top level, in
 * the GSequence mailbox->msg_tree.
 * Returns the GSequenceIter pointing to the new node.
 */
GSequenceIter *
libbalsa_mailbox_msgno_inserted(LibBalsaMailbox *mailbox, guint seqno,
                                GSequenceIter * parent,
                                GSequenceIter * sibling)
{
    GSList **unthreaded;
    LibBalsaMailboxSequenceInfo *info;
    GSequenceIter *insert;

    if (!libbalsa_threads_has_lock())
        g_warning("Thread is not holding gdk lock");
    if (!mailbox->msg_tree)
        return NULL;

    /* Insert node into the message tree before getting path. */
    info = lbm_node_info_new(mailbox, seqno, parent);

    if (sibling) {
        insert = g_sequence_iter_next(sibling);
        insert = g_sequence_insert_before(insert, info);
    } else {
        GSequence *seq =
            parent ? lbm_node_init_children(parent) : mailbox->msg_tree;
        insert = g_sequence_prepend(seq, info);
    }
    lbm_node_cache(mailbox, seqno, insert);

    if (g_signal_has_handler_pending(mailbox,
                                     libbalsa_mbox_model_signals
                                     [ROW_INSERTED], 0, FALSE)) {
        GtkTreeIter iter;
        GtkTreePath *path;

        iter.user_data = insert;
        iter.stamp     = mailbox->stamp;
        path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);
        g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_INSERTED],
                      0, path, &iter);
        gtk_tree_path_free(path);
    }

    unthreaded =
        g_object_get_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_UNTHREADED);
    if (unthreaded)
        *unthreaded =
            g_slist_prepend(*unthreaded, GUINT_TO_POINTER(seqno));

    mailbox->msg_tree_changed = TRUE;

    return insert;
}

static void
lbm_msgno_filt_in(LibBalsaMailbox *mailbox, guint seqno)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    LibBalsaMailboxSequenceInfo *info;

    /* Insert node into the message tree before getting path. */
    info = lbm_node_info_new(mailbox, seqno, NULL);
    iter.user_data = g_sequence_prepend(mailbox->msg_tree, info);
    iter.stamp = mailbox->stamp;
    lbm_node_cache(mailbox, seqno, iter.user_data);

    /* This item is now the first in the tree: */
    path = gtk_tree_path_new_first();
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_INSERTED], 0,
                  path, &iter);
    gtk_tree_path_free(path);

    mailbox->msg_tree_changed = TRUE;
    g_signal_emit(mailbox, libbalsa_mailbox_signals[CHANGED], 0);
}

static void
lbm_node_remove(LibBalsaMailbox * mailbox, GSequenceIter * node)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    LibBalsaMailboxSequenceInfo *info = g_sequence_get(node);
    GSequenceIter *parent   = info->parent;
    GSequence     *children = info->children;
    GSequenceIter *begin, *end;

    iter.user_data = node;
    iter.stamp = mailbox->stamp;
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);

    /* First promote any children to be the node's siblings; we'll insert
     * them all before the current node, to keep the path calculation
     * simple. */
    if (children
        && (begin = g_sequence_get_begin_iter(children))
        != (end = g_sequence_get_end_iter(children))) {
        g_sequence_move_range(node, begin, end);

        for (; begin != node; begin = g_sequence_iter_next(begin)) {
            LibBalsaMailboxSequenceInfo *child_info =
                g_sequence_get(begin);

            child_info->parent = parent;
            iter.user_data = begin;
            g_signal_emit(mailbox,
                          libbalsa_mbox_model_signals[ROW_INSERTED], 0,
                          path, &iter);
            if (lbm_node_get_children(begin))
                g_signal_emit(mailbox,
                              libbalsa_mbox_model_signals
                              [ROW_HAS_CHILD_TOGGLED], 0, path, &iter);
            gtk_tree_path_next(path);
        }
    }

    /* Now it's safe to destroy the node. */
    g_sequence_remove(node);
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_DELETED], 0, path);

    if (parent && !lbm_node_get_children(parent)) {
        /* We just removed the last child */
        gtk_tree_path_up(path);
        iter.user_data = parent;
        g_signal_emit(mailbox,
                      libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED], 0,
                      path, &iter);
    }
    
    gtk_tree_path_free(path);

    mailbox->stamp++;
    mailbox->msg_tree_changed = TRUE;
    g_signal_emit(mailbox, libbalsa_mailbox_signals[CHANGED], 0);
}

void
libbalsa_mailbox_msgno_removed(LibBalsaMailbox * mailbox, guint seqno)
{
    GSequenceIter *node;

    g_signal_emit(mailbox, libbalsa_mailbox_signals[MESSAGE_EXPUNGED],
                  0, seqno);

    gdk_threads_enter();

    if (!mailbox->msg_tree) {
        gdk_threads_leave();
        return;
    }

    if (seqno <= mailbox->mindex->len) {
        libbalsa_mailbox_index_entry_free(g_ptr_array_index(mailbox->mindex,
                                                            seqno - 1));
        g_ptr_array_remove_index(mailbox->mindex, seqno - 1);
    }

    node = lbm_node_find(mailbox, seqno);
    if (node)
        lbm_node_remove(mailbox, node);
    /* else apparently the view did not include this message, which is ok */

    g_ptr_array_remove_index(mailbox->nodes, seqno - 1);

    while (seqno <= mailbox->nodes->len) {
        if ((node = lbm_node_find(mailbox, seqno)) != NULL) {
            LibBalsaMailboxSequenceInfo *info = g_sequence_get(node);
            --info->msgno;
            g_assert(info->msgno == seqno);
        }
        ++seqno;
    }

    gdk_threads_leave();
}

/*
 * Check whether to filter the message in or out of the view:
 * - if it's in the view and doesn't match the condition, filter it out,
 *   unless it's selected and we don't want to filter out selected
 *   messages;
 * - if it isn't in the view and it matches the condition, filter it in.
 */
void
libbalsa_mailbox_msgno_filt_check(LibBalsaMailbox * mailbox, guint seqno,
                                  LibBalsaMailboxSearchIter * search_iter,
                                  gboolean hold_selected)
{
    gboolean match;
    GSequenceIter *node;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gdk_threads_enter();

    if (!mailbox->msg_tree) {
        gdk_threads_leave();
        return;
    }

    match = search_iter ?
        libbalsa_mailbox_message_match(mailbox, seqno, search_iter) : TRUE;
    if ((node = lbm_node_find(mailbox, seqno)) != NULL) {
        if (!match) {
            gboolean filt_out = hold_selected ?
                libbalsa_mailbox_msgno_has_flags(mailbox, seqno, 0,
                                                 LIBBALSA_MESSAGE_FLAG_SELECTED)
                : TRUE;
            if (filt_out)
                lbm_node_remove(mailbox, node);
        }
    } else {
        if (match)
            lbm_msgno_filt_in(mailbox, seqno);
    }

    gdk_threads_leave();
}

/* Search iters */
LibBalsaMailboxSearchIter *
libbalsa_mailbox_search_iter_new(LibBalsaCondition * condition)
{
    LibBalsaMailboxSearchIter *iter;

    if (!condition)
        return NULL;

    iter = g_new(LibBalsaMailboxSearchIter, 1);
    iter->mailbox = NULL;
    iter->stamp = 0;
    iter->condition = libbalsa_condition_ref(condition);
    iter->user_data = NULL;

    return iter;
}

/* Create a LibBalsaMailboxSearchIter for a mailbox's view_filter. */
LibBalsaMailboxSearchIter *
libbalsa_mailbox_search_iter_view(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return libbalsa_mailbox_search_iter_new(mailbox->view_filter);
}

void
libbalsa_mailbox_search_iter_free(LibBalsaMailboxSearchIter * search_iter)
{
    LibBalsaMailbox *mailbox;

    if (!search_iter)
        return;

    mailbox = search_iter->mailbox;
    if (mailbox && LIBBALSA_MAILBOX_GET_CLASS(mailbox)->search_iter_free)
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->search_iter_free(search_iter);

    libbalsa_condition_unref(search_iter->condition);
    g_free(search_iter);
}

/* Find a message in the tree-model, by its message number. */
gboolean
libbalsa_mailbox_msgno_find(LibBalsaMailbox * mailbox, guint seqno,
                            GtkTreePath ** path, GtkTreeIter * iter)
{
    GtkTreeIter tmp_iter;

    if (!libbalsa_threads_has_lock())
        g_warning("Thread is not holding gdk lock");
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(seqno > 0, FALSE);

    if (!mailbox->msg_tree || !(tmp_iter.user_data =
                                lbm_node_find(mailbox, seqno)))
        return FALSE;

    tmp_iter.stamp = mailbox->stamp;

    if (path)
        *path =
            gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &tmp_iter);
    if (iter)
        *iter = tmp_iter;

    return TRUE;
}

struct AddMessageData {
    GMimeStream *stream;
    LibBalsaMessageFlag flags;
    gboolean processed;
};

static gboolean
msg_iterator(LibBalsaMessageFlag *flg, GMimeStream **stream, void *arg)
{
    struct AddMessageData * amd = (struct AddMessageData*)arg;
    gboolean res = !amd->processed;
    amd->processed = TRUE;
    *flg = amd->flags;
    *stream = amd->stream;
 /* Make sure ::add_messages does not destroy the stream. */
    g_object_ref(amd->stream);
    return res;
}

gboolean
libbalsa_mailbox_add_message(LibBalsaMailbox * mailbox,
                             GMimeStream * stream,
                             LibBalsaMessageFlag flags, GError ** err)
{
    guint retval;
    struct AddMessageData amd;
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    libbalsa_lock_mailbox(mailbox);

    amd.stream = stream;
    amd.flags  = flags;
    amd.processed = FALSE;
    retval =
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->add_messages(mailbox, 
							  msg_iterator, &amd,
							  err);
    if (retval) {
        if (!(flags & LIBBALSA_MESSAGE_FLAG_DELETED)
            && (flags & LIBBALSA_MESSAGE_FLAG_NEW))
            libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
        lbm_queue_check(mailbox);
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

guint
libbalsa_mailbox_add_messages(LibBalsaMailbox * mailbox,
			      LibBalsaAddMessageIterator msg_iterator,
			      void *arg,
			      GError ** err)
{
    guint retval;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    libbalsa_lock_mailbox(mailbox);

    retval =
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->add_messages(mailbox, 
							  msg_iterator, arg,
							  err);

    if (retval) {
#ifdef FIXED
	/* this is something that should be returned/taken care of by
	   add_messages? */
        if (!(flags & LIBBALSA_MESSAGE_FLAG_DELETED)
            && (flags & LIBBALSA_MESSAGE_FLAG_NEW))
            libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
#endif
        lbm_queue_check(mailbox);
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

gboolean
libbalsa_mailbox_close_backend(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->close_backend(mailbox);
}

guint
libbalsa_mailbox_total_messages(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(mailbox != NULL, 0);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->total_messages(mailbox);
}

gboolean
libbalsa_mailbox_sync_storage(LibBalsaMailbox * mailbox, gboolean expunge)
{
    gboolean retval = TRUE;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(!mailbox->readonly, TRUE);

    libbalsa_lock_mailbox(mailbox);

    /* When called in an idle handler, the mailbox might have been
     * closed, so we must check (with the mailbox locked). */
    if (MAILBOX_OPEN(mailbox)) {
        GSList *unthreaded = NULL;

        g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_UNTHREADED,
                          &unthreaded);
        retval =
            LIBBALSA_MAILBOX_GET_CLASS(mailbox)->sync(mailbox, expunge);
        g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_UNTHREADED,
                          unthreaded);
        if (unthreaded) {
            lbm_set_threading(mailbox, mailbox->view->threading_type);
            g_slist_free(unthreaded);
            g_object_set_data(G_OBJECT(mailbox),
                              LIBBALSA_MAILBOX_UNTHREADED, NULL);
        } else
            libbalsa_mailbox_changed(mailbox);
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

LibBalsaMessage *
libbalsa_mailbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message;

    g_return_val_if_fail(mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    libbalsa_lock_mailbox(mailbox);

    if (!MAILBOX_OPEN(mailbox)) {
        g_message(_("libbalsa_mailbox_get_message: mailbox %s is closed"),
                  mailbox->name);
        libbalsa_unlock_mailbox(mailbox);
        return NULL;
    }

#ifdef BALSA_USE_THREADS
    if( !(msgno > 0 && msgno <= libbalsa_mailbox_total_messages(mailbox)) ) {
	libbalsa_unlock_mailbox(mailbox);
	g_warning("get_message: msgno %d out of range", msgno);
	return NULL;
    }
#else                           /* BALSA_USE_THREADS */
    g_return_val_if_fail(msgno > 0 && msgno <=
                         libbalsa_mailbox_total_messages(mailbox), NULL);
#endif                          /* BALSA_USE_THREADS */

    message = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message(mailbox,
                                                               msgno);
    libbalsa_unlock_mailbox(mailbox);

    return message;
}

gboolean
libbalsa_mailbox_prepare_threading(LibBalsaMailbox * mailbox, guint start)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->prepare_threading(mailbox,
                                                                  start);
}

gboolean
libbalsa_mailbox_fetch_message_structure(LibBalsaMailbox *mailbox,
                                         LibBalsaMessage *message,
                                         LibBalsaFetchFlag flags)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(message != NULL, FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)
        ->fetch_message_structure(mailbox, message, flags);
}

void
libbalsa_mailbox_release_message(LibBalsaMailbox * mailbox,
                                 LibBalsaMessage * message)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(mailbox == message->mailbox);

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)
        ->release_message(mailbox, message);
}

void
libbalsa_mailbox_set_msg_headers(LibBalsaMailbox *mailbox,
                                 LibBalsaMessage *message)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(message != NULL);

    if(!message->has_all_headers) {
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->fetch_headers(mailbox, message);
        message->has_all_headers = 1;
    }
}

gboolean
libbalsa_mailbox_get_message_part(LibBalsaMessage    *message,
                                  LibBalsaMessageBody *part,
                                  GError **err)
{
    g_return_val_if_fail(message != NULL, FALSE);
    g_return_val_if_fail(message->mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(message->mailbox), FALSE);
    g_return_val_if_fail(part != NULL, FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(message->mailbox)
        ->get_message_part(message, part, err);
}

GMimeStream *
libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox, guint msgno)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(msgno <= libbalsa_mailbox_total_messages(mailbox),
                         NULL);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message_stream(mailbox,
                                                                   msgno);
}

/* libbalsa_mailbox_change_msgs_flags() changes stored message flags
   and is to be used only internally by libbalsa.
*/
gboolean
libbalsa_mailbox_messages_change_flags(LibBalsaMailbox * mailbox,
                                       GArray * msgnos,
                                       LibBalsaMessageFlag set,
                                       LibBalsaMessageFlag clear)
{
    gboolean retval;
    guint i;
    gboolean real_flag;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    real_flag = (set | clear) & LIBBALSA_MESSAGE_FLAGS_REAL;
    g_return_val_if_fail(!mailbox->readonly || !real_flag, FALSE);

    if (msgnos->len == 0)
	return TRUE;

    if (real_flag)
	libbalsa_lock_mailbox(mailbox);

    retval = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->
	messages_change_flags(mailbox, msgnos, set, clear);

    if (retval && mailbox->view_filter) {
        LibBalsaMailboxSearchIter *iter_view =
            libbalsa_mailbox_search_iter_view(mailbox);
        for (i = 0; i < msgnos->len; i++) {
            guint msgno = g_array_index(msgnos, guint, i);
            libbalsa_mailbox_msgno_filt_check(mailbox, msgno, iter_view,
                                              TRUE);
        }
        libbalsa_mailbox_search_iter_free(iter_view);
    }

    if (real_flag)
	libbalsa_unlock_mailbox(mailbox);

    if (set & LIBBALSA_MESSAGE_FLAG_DELETED && retval)
        libbalsa_mailbox_changed(mailbox);

    return retval;
}

gboolean
libbalsa_mailbox_msgno_change_flags(LibBalsaMailbox * mailbox,
                                    guint msgno,
                                    LibBalsaMessageFlag set,
                                    LibBalsaMessageFlag clear)
{
    gboolean retval;
    GArray *msgnos = g_array_sized_new(FALSE, FALSE, sizeof(guint), 1);

    g_array_append_val(msgnos, msgno);
    libbalsa_mailbox_register_msgnos(mailbox, msgnos);
    retval =
        libbalsa_mailbox_messages_change_flags(mailbox, msgnos, set,
                                               clear);
    libbalsa_mailbox_unregister_msgnos(mailbox, msgnos);
    g_array_free(msgnos, TRUE);

    return retval;
}

/* Copy messages with msgnos in the list from mailbox to dest. */
gboolean
libbalsa_mailbox_messages_copy(LibBalsaMailbox * mailbox, GArray * msgnos,
                               LibBalsaMailbox * dest, GError **err)
{
    gboolean retval;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(msgnos->len > 0, TRUE);

    libbalsa_lock_mailbox(mailbox);
    retval = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->
	messages_copy(mailbox, msgnos, dest, err);
    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

/* Move messages with msgnos in the list from mailbox to dest. */
gboolean
libbalsa_mailbox_messages_move(LibBalsaMailbox * mailbox,
                               GArray * msgnos,
                               LibBalsaMailbox * dest, GError **err)
{
    gboolean retval;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(msgnos->len > 0, TRUE);

    libbalsa_lock_mailbox(mailbox);
    if (libbalsa_mailbox_messages_copy(mailbox, msgnos, dest, err)) {
        retval = libbalsa_mailbox_messages_change_flags
            (mailbox, msgnos, LIBBALSA_MESSAGE_FLAG_DELETED,
             (LibBalsaMessageFlag) 0);
	if(!retval)
	    g_set_error(err,LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_COPY_ERROR,
			_("Removing messages from source mailbox failed"));
    } else
        retval = FALSE;
    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

/*
 * Mailbox views.
 *
 * NOTE: call to update_view_filter MUST be followed by a call to
 * lbm_set_threading that will actually create the
 * message tree.
 *
 * Returns TRUE if the message tree was updated.
 */
gboolean
libbalsa_mailbox_set_view_filter(LibBalsaMailbox *mailbox,
                                 LibBalsaCondition *cond,
                                 gboolean update_immediately)
{
    gboolean retval = update_immediately;

    libbalsa_lock_mailbox(mailbox);

    libbalsa_condition_unref(mailbox->view_filter);
    mailbox->view_filter = libbalsa_condition_ref(cond);

    if (update_immediately) {
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->update_view_filter(mailbox,
                                                                cond);
        retval = lbm_set_threading(mailbox, mailbox->view->threading_type);
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

void
libbalsa_mailbox_make_view_filter_persistent(LibBalsaMailbox * mailbox)
{
    libbalsa_condition_unref(mailbox->persistent_view_filter);
    mailbox->persistent_view_filter =
        libbalsa_condition_ref(mailbox->view_filter);
}

/* Test message flags. */
gboolean
libbalsa_mailbox_msgno_has_flags(LibBalsaMailbox * mailbox, guint msgno,
                                 LibBalsaMessageFlag set,
                                 LibBalsaMessageFlag unset)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(msgno > 0, FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->msgno_has_flags(mailbox,
                                                                msgno, set,
                                                                unset);
}

/* Inquire method: check whether mailbox driver can perform operation
   in question. In principle, all operations should be supported but
   some of them may be expensive under certain circumstances and are
   best avoided. */
static gboolean
libbalsa_mailbox_real_can_do(LibBalsaMailbox* mbox,
                             enum LibBalsaMailboxCapability cap)
{
    return TRUE;
}

gboolean
libbalsa_mailbox_can_do(LibBalsaMailbox *mailbox,
                        enum LibBalsaMailboxCapability cap)
{
    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->can_do(mailbox, cap);
}

static void lbm_sort_seq(LibBalsaMailbox * mbox, GSequence * seq);
static gboolean
lbm_set_threading(LibBalsaMailbox * mailbox,
                  LibBalsaMailboxThreadingType thread_type)
{
    if (!MAILBOX_OPEN(mailbox))
        return FALSE;

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->set_threading(mailbox,
                                                       thread_type);
    gdk_threads_enter();
    if (mailbox->msg_tree)
        lbm_sort_seq(mailbox, mailbox->msg_tree);

    libbalsa_mailbox_changed(mailbox);
    gdk_threads_leave();

    return TRUE;
}

void
libbalsa_mailbox_set_threading(LibBalsaMailbox *mailbox,
                               LibBalsaMailboxThreadingType thread_type)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    libbalsa_lock_mailbox(mailbox);
    lbm_set_threading(mailbox, thread_type);
    libbalsa_unlock_mailbox(mailbox);
}

/* =================================================================== *
 * Mailbox view methods                                                *
 * =================================================================== */

GHashTable *libbalsa_mailbox_view_table;
static LibBalsaMailboxView libbalsa_mailbox_view_default = {
    NULL,			/* mailing_list_address */
    NULL,			/* identity_name        */
    LB_MAILBOX_THREADING_FLAT,	/* threading_type       */
    0,				/* filter               */
    LB_MAILBOX_SORT_TYPE_ASC,	/* sort_type            */
    LB_MAILBOX_SORT_NO,         /* sort_field           */
    LB_MAILBOX_SHOW_UNSET,	/* show                 */
    LB_MAILBOX_SUBSCRIBE_UNSET,	/* subscribe            */
    0,				/* exposed              */
    0,				/* open                 */
    1,				/* in_sync              */
    0,				/* frozen		*/
    0,				/* used 		*/
#ifdef HAVE_GPGME
    LB_MAILBOX_CHK_CRYPT_MAYBE, /* gpg_chk_mode         */
#endif
    -1,                         /* total messages	*/
    -1,                         /* unread messages	*/
    0                           /* mod time             */
};

LibBalsaMailboxView *
libbalsa_mailbox_view_new(void)
{
    LibBalsaMailboxView *view;

    view = g_memdup(&libbalsa_mailbox_view_default,
		    sizeof libbalsa_mailbox_view_default);

    return view;
}

void
libbalsa_mailbox_view_free(LibBalsaMailboxView * view)
{
    if (view->mailing_list_address)
        internet_address_list_destroy(view->mailing_list_address);
    g_free(view->identity_name);
    g_free(view);
}

/* helper */
static LibBalsaMailboxView *
lbm_get_view(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxView *view;

    if (!mailbox)
	return &libbalsa_mailbox_view_default;

    view = mailbox->view;
    if (!view) {
        view =
            g_hash_table_lookup(libbalsa_mailbox_view_table, mailbox->url);
        if (!view) {
            view = libbalsa_mailbox_view_new();
            g_hash_table_insert(libbalsa_mailbox_view_table,
                                g_strdup(mailbox->url), view);
        }
        mailbox->view = view;
    }

    return view;
}

/* Set methods; NULL mailbox is valid, and changes the default value. */

gboolean
libbalsa_mailbox_set_identity_name(LibBalsaMailbox * mailbox,
				   const gchar * identity_name)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen
	&& (!view->identity_name
	    || strcmp(view->identity_name, identity_name))) {
	g_free(view->identity_name);
	view->identity_name = g_strdup(identity_name);
	if (mailbox)
	    view->in_sync = 0;
	return TRUE;
    } else
	return FALSE;
}

void
libbalsa_mailbox_set_threading_type(LibBalsaMailbox * mailbox,
				 LibBalsaMailboxThreadingType
				 threading_type)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->threading_type != threading_type) {
	view->threading_type = threading_type;
	if (mailbox)
	    view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_sort_type(LibBalsaMailbox * mailbox,
			    LibBalsaMailboxSortType sort_type)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->sort_type != sort_type) {
	view->sort_type = sort_type;
	if (mailbox)
	    view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_sort_field(LibBalsaMailbox * mailbox,
			     LibBalsaMailboxSortFields sort_field)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->sort_field != sort_field) {
	view->sort_field = sort_field;
	if (mailbox)
	    view->in_sync = 0;
    }
}

gboolean
libbalsa_mailbox_set_show(LibBalsaMailbox * mailbox, LibBalsaMailboxShow show)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->show != show) {
	/* Don't set not in sync if we're just replacing UNSET with the
	 * default. */
	if (mailbox && view->show != LB_MAILBOX_SHOW_UNSET)
	    view->in_sync = 0;
	view->show = show;
	return TRUE;
    } else
	return FALSE;
}

gboolean
libbalsa_mailbox_set_subscribe(LibBalsaMailbox * mailbox,
                               LibBalsaMailboxSubscribe subscribe)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->subscribe != subscribe) {
	/* Don't set not in sync if we're just replacing UNSET with the
	 * default. */
	if (mailbox && view->subscribe != LB_MAILBOX_SUBSCRIBE_UNSET)
	    view->in_sync = 0;
	view->subscribe = subscribe;
	return TRUE;
    } else
	return FALSE;
}

void
libbalsa_mailbox_set_exposed(LibBalsaMailbox * mailbox, gboolean exposed)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->exposed != exposed) {
	view->exposed = exposed ? 1 : 0;
	if (mailbox)
	    view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_open(LibBalsaMailbox * mailbox, gboolean open)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->open != open) {
	view->open = open ? 1 : 0;
	if (mailbox)
	    view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_filter(LibBalsaMailbox * mailbox, gint filter)
{
    LibBalsaMailboxView *view = lbm_get_view(mailbox);

    if (!view->frozen && view->filter != filter) {
	view->filter = filter;
	if (mailbox)
	    view->in_sync = 0;
    }
}

/* Freeze or unfreeze the view: no changes are made while the view is
 * frozen;
 * - changing the default is not allowed;
 * - no action needed if the view is NULL. */
void
libbalsa_mailbox_set_frozen(LibBalsaMailbox * mailbox, gboolean frozen)
{
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox != NULL);

    view = mailbox->view;
    if (view)
	view->frozen = frozen ? 1 : 0;
}

#ifdef HAVE_GPGME
gboolean 
libbalsa_mailbox_set_crypto_mode(LibBalsaMailbox * mailbox,
                                LibBalsaChkCryptoMode gpg_chk_mode)
{
    LibBalsaMailboxView *view;

    g_return_val_if_fail(mailbox != NULL && mailbox->view != NULL, FALSE);

    view = mailbox->view;
    if (view->gpg_chk_mode != gpg_chk_mode) {
	view->gpg_chk_mode = gpg_chk_mode;
	return TRUE;
    } else
	return FALSE;
}
#endif

void
libbalsa_mailbox_set_unread(LibBalsaMailbox * mailbox, gint unread)
{
    LibBalsaMailboxView *view;

    /* Changing the default is not allowed. */
    g_return_if_fail(mailbox != NULL);

    view = lbm_get_view(mailbox);
    view->used = 1;

    if (!view->frozen && view->unread != unread) {
	view->unread = unread;
        view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_total(LibBalsaMailbox * mailbox, gint total)
{
    LibBalsaMailboxView *view;

    /* Changing the default is not allowed. */
    g_return_if_fail(mailbox != NULL);

    view = lbm_get_view(mailbox);

    if (!view->frozen && view->total != total) {
	view->total = total;
        view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_mtime(LibBalsaMailbox * mailbox, time_t mtime)
{
    LibBalsaMailboxView *view;

    /* Changing the default is not allowed. */
    g_return_if_fail(mailbox != NULL);

    view = lbm_get_view(mailbox);

    if (!view->frozen && view->mtime != mtime) {
	view->mtime = mtime;
        view->in_sync = 0;
    }
}

/* End of set methods. */

/* Get methods; NULL mailbox is valid, and returns the default value. */

InternetAddressList *
libbalsa_mailbox_get_mailing_list_address(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->mailing_list_address :
	libbalsa_mailbox_view_default.mailing_list_address;
}

const gchar *
libbalsa_mailbox_get_identity_name(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->identity_name :
	libbalsa_mailbox_view_default.identity_name;
}


LibBalsaMailboxThreadingType
libbalsa_mailbox_get_threading_type(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->threading_type :
	libbalsa_mailbox_view_default.threading_type;
}

LibBalsaMailboxSortType
libbalsa_mailbox_get_sort_type(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->sort_type : libbalsa_mailbox_view_default.sort_type;
}

LibBalsaMailboxSortFields
libbalsa_mailbox_get_sort_field(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->sort_field :
	libbalsa_mailbox_view_default.sort_field;
}

LibBalsaMailboxShow
libbalsa_mailbox_get_show(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->show : libbalsa_mailbox_view_default.show;
}

LibBalsaMailboxSubscribe
libbalsa_mailbox_get_subscribe(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->subscribe : libbalsa_mailbox_view_default.subscribe;
}

gboolean
libbalsa_mailbox_get_exposed(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->exposed : libbalsa_mailbox_view_default.exposed;
}

gboolean
libbalsa_mailbox_get_open(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->open : libbalsa_mailbox_view_default.open;
}

gint
libbalsa_mailbox_get_filter(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->filter : libbalsa_mailbox_view_default.filter;
}

gboolean
libbalsa_mailbox_get_frozen(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->frozen : FALSE;
}

#ifdef HAVE_GPGME
LibBalsaChkCryptoMode
libbalsa_mailbox_get_crypto_mode(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->gpg_chk_mode :
	libbalsa_mailbox_view_default.gpg_chk_mode;
}
#endif

gint
libbalsa_mailbox_get_unread(LibBalsaMailbox * mailbox)
{
    if (mailbox && mailbox->view) {
        mailbox->view->used = 1;
	return mailbox->view->unread;
    } else 
        return libbalsa_mailbox_view_default.unread;
}

gint
libbalsa_mailbox_get_total(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->total : libbalsa_mailbox_view_default.total;
}

time_t
libbalsa_mailbox_get_mtime(LibBalsaMailbox * mailbox)
{
    return (mailbox && mailbox->view) ?
	mailbox->view->mtime : libbalsa_mailbox_view_default.mtime;
}

/* End of get methods. */

/* =================================================================== *
 * GtkTreeModel implementation functions.                              *
 * Important:
 * do not forget to modify LibBalsaMailbox::stamp on each modification
 * of the message list.
 * =================================================================== */

/* Iterator invalidation macros. */
#define VALID_ITER(iter, tree_model) \
    ((iter)!= NULL && \
     (iter)->user_data != NULL && \
     LIBBALSA_IS_MAILBOX(tree_model) && \
     ((LibBalsaMailbox *) tree_model)->stamp == (iter)->stamp)
#define VALIDATE_ITER(iter, tree_model) \
    ((iter)->stamp = ((LibBalsaMailbox *) tree_model)->stamp)
#define INVALIDATE_ITER(iter) ((iter)->stamp = 0)

static GtkTreeModelFlags mbox_model_get_flags  (GtkTreeModel      *tree_model);
static gint         mbox_model_get_n_columns   (GtkTreeModel      *tree_model);
static GType        mbox_model_get_column_type (GtkTreeModel      *tree_model,
                                                gint               index);
static gboolean     mbox_model_get_iter        (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreePath       *path);
static GtkTreePath *mbox_model_get_path        (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static void         mbox_model_get_value       (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                gint               column,
                                                GValue            *value);
static gboolean     mbox_model_iter_next       (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static gboolean     mbox_model_iter_children   (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreeIter       *parent);
static gboolean     mbox_model_iter_has_child  (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static gint         mbox_model_iter_n_children (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static gboolean     mbox_model_iter_nth_child  (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreeIter       *parent,
                                                gint               n);
static gboolean     mbox_model_iter_parent     (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreeIter       *child);


static GType mbox_model_col_type[LB_MBOX_N_COLS];

static void
mbox_model_init(GtkTreeModelIface *iface)
{
    iface->get_flags       = mbox_model_get_flags;
    iface->get_n_columns   = mbox_model_get_n_columns;
    iface->get_column_type = mbox_model_get_column_type;
    iface->get_iter        = mbox_model_get_iter;
    iface->get_path        = mbox_model_get_path;
    iface->get_value       = mbox_model_get_value;
    iface->iter_next       = mbox_model_iter_next;
    iface->iter_children   = mbox_model_iter_children;
    iface->iter_has_child  = mbox_model_iter_has_child;
    iface->iter_n_children = mbox_model_iter_n_children;
    iface->iter_nth_child  = mbox_model_iter_nth_child;
    iface->iter_parent     = mbox_model_iter_parent;

    mbox_model_col_type[LB_MBOX_MSGNO_COL]   = G_TYPE_UINT;
    mbox_model_col_type[LB_MBOX_MARKED_COL]  = GDK_TYPE_PIXBUF;
    mbox_model_col_type[LB_MBOX_ATTACH_COL]  = GDK_TYPE_PIXBUF;
    mbox_model_col_type[LB_MBOX_FROM_COL]    = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_SUBJECT_COL] = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_DATE_COL]    = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_SIZE_COL]    = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_WEIGHT_COL]  = G_TYPE_UINT;
    mbox_model_col_type[LB_MBOX_STYLE_COL]   = G_TYPE_UINT;

    libbalsa_mbox_model_signals[ROW_CHANGED] =
        g_signal_lookup("row-changed",           GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROW_DELETED] =
        g_signal_lookup("row-deleted",           GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED] =
        g_signal_lookup("row-has-child-toggled", GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROW_INSERTED] =
        g_signal_lookup("row-inserted",          GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROWS_REORDERED] =
        g_signal_lookup("rows-reordered",        GTK_TYPE_TREE_MODEL);
}

static GtkTreeModelFlags
mbox_model_get_flags(GtkTreeModel *tree_model)
{
    return 0;
}

static gint
mbox_model_get_n_columns(GtkTreeModel *tree_model)
{
    return LB_MBOX_N_COLS;
}

static GType
mbox_model_get_column_type(GtkTreeModel *tree_model, gint index)
{
    g_return_val_if_fail(index>=0 && index <LB_MBOX_N_COLS, G_TYPE_BOOLEAN);
    return mbox_model_col_type[index];
}

static gboolean
mbox_model_get_iter(GtkTreeModel *tree_model,
                    GtkTreeIter  *iter,
                    GtkTreePath  *path)
{
    GtkTreeIter parent;
    const gint *indices;
    gint depth, i;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(tree_model), FALSE);

    indices = gtk_tree_path_get_indices(path);
    depth = gtk_tree_path_get_depth(path);

    g_return_val_if_fail(depth > 0, FALSE);

    if (!mbox_model_iter_nth_child(tree_model, iter, NULL, indices[0]))
        return FALSE;

    for (i = 1; i < depth; i++) {
        parent = *iter;
        if (!mbox_model_iter_nth_child(tree_model, iter, &parent,
                                       indices[i]))
            return FALSE;
    }

    return TRUE;
}

static GtkTreePath *
mbox_model_get_path_helper(GSequenceIter * node)
{
    GtkTreePath *path = gtk_tree_path_new();

    do 
	gtk_tree_path_prepend_index(path, g_sequence_iter_get_position(node));
    while ((node = libbalsa_mailbox_get_parent(node)) != NULL);

    return path;
}

static GtkTreePath *
mbox_model_get_path(GtkTreeModel * tree_model, GtkTreeIter * iter)
{
    if (!libbalsa_threads_has_lock())
        g_warning("Thread is not holding gdk lock");
    g_return_val_if_fail(VALID_ITER(iter, tree_model), NULL);

    return mbox_model_get_path_helper(iter->user_data);
}

/* mbox_model_get_value: 
  FIXME: still includes some debugging code in case fetching the
  message failed.
*/

static GdkPixbuf *status_icons[LIBBALSA_MESSAGE_STATUS_ICONS_NUM];
static GdkPixbuf *attach_icons[LIBBALSA_MESSAGE_ATTACH_ICONS_NUM];

static gchar *
libbalsa_size_to_gchar(glong length)
{
    gchar retsize[32];

    /* length is long */
    if (length <= 32768) {
        g_snprintf (retsize, sizeof(retsize), "%ld", length);
    } else if (length <= (100*1024)) {
        float tmp = (float)length/1024.0;
        g_snprintf (retsize, sizeof(retsize), "%.1fK", tmp);
    } else if (length <= (1024*1024)) {
        g_snprintf (retsize, sizeof(retsize), "%ldK", length/1024);
    } else {
        float tmp = (float)length/(1024.0*1024.0);
        g_snprintf (retsize, sizeof(retsize), "%.1fM", tmp);
    }

    return g_strdup(retsize);
}

#ifdef BALSA_USE_THREADS
/* Protects access to mailbox->msgnos_pending; may be locked 
 * with or without the gdk lock, so WE MUST NOT GRAB THE GDK LOCK WHILE
 * HOLDING IT. */
static pthread_mutex_t get_index_entry_lock = PTHREAD_MUTEX_INITIALIZER;

static void lbm_update_msgnos(LibBalsaMailbox * mailbox, guint seqno,
                              GArray * msgnos);
static void
lbm_get_index_entry_expunged_cb(LibBalsaMailbox * mailbox, guint seqno)
{
    pthread_mutex_lock(&get_index_entry_lock);
    lbm_update_msgnos(mailbox, seqno, mailbox->msgnos_pending);
    pthread_mutex_unlock(&get_index_entry_lock);
}

static void
lbm_get_index_entry_real(LibBalsaMailbox * mailbox)
{
    guint i;

#if DEBUG
    g_print("%s %s %d requested, ", __func__, mailbox->name,
            mailbox->msgnos_pending->len);
#endif
    pthread_mutex_lock(&get_index_entry_lock);
    for (i = 0; i < mailbox->msgnos_pending->len; i++) {
        guint msgno = g_array_index(mailbox->msgnos_pending, guint, i);
        LibBalsaMessage *message;

        if (!MAILBOX_OPEN(mailbox))
            break;

        pthread_mutex_unlock(&get_index_entry_lock);
        if ( (message = libbalsa_mailbox_get_message(mailbox, msgno)) ) {
            libbalsa_mailbox_cache_message(mailbox, msgno, message);
            g_object_unref(message);
        }
        pthread_mutex_lock(&get_index_entry_lock);
    }

#if DEBUG
    g_print("%d processed\n", mailbox->msgnos_pending->len);
#endif
    mailbox->msgnos_pending->len = 0;
    pthread_mutex_unlock(&get_index_entry_lock);

    g_object_unref(mailbox);
}
#endif                          /*BALSA_USE_THREADS */

static LibBalsaMailboxIndexEntry *
lbm_get_index_entry(LibBalsaMailbox * lmm, GSequenceIter * node)
{
    guint msgno = libbalsa_mailbox_get_msgno(node);
    LibBalsaMailboxIndexEntry *entry;

    if (!lmm->mindex)
        return NULL;

    while (lmm->mindex->len < msgno )
        g_ptr_array_add(lmm->mindex, NULL);

    entry = g_ptr_array_index(lmm->mindex, msgno - 1);
#ifdef BALSA_USE_THREADS
    if (entry)
        return entry->idle_pending ? NULL : entry;

    pthread_mutex_lock(&get_index_entry_lock);
    if (!lmm->msgnos_pending) {
        lmm->msgnos_pending = g_array_new(FALSE, FALSE, sizeof(guint));
        g_signal_connect(lmm, "message-expunged",
                         G_CALLBACK(lbm_get_index_entry_expunged_cb), NULL);
    }

    if (!lmm->msgnos_pending->len) {
        pthread_t get_index_entry_thread;

        g_object_ref(lmm);
        pthread_create(&get_index_entry_thread, NULL,
                       (void *) lbm_get_index_entry_real, lmm);
        pthread_detach(get_index_entry_thread);
    }

    g_array_append_val(lmm->msgnos_pending, msgno);
    /* Make sure we have a "pending" index entry before releasing the
     * lock. */
    g_ptr_array_index(lmm->mindex, msgno - 1) =
        lbm_index_entry_new_pending();
    pthread_mutex_unlock(&get_index_entry_lock);
#else                           /*BALSA_USE_THREADS */
    if (!entry) {
        LibBalsaMessage *message =
            libbalsa_mailbox_get_message(lmm, msgno);
        if (message) {
            libbalsa_mailbox_cache_message(lmm, msgno, message);
            g_object_unref(message);
            entry = g_ptr_array_index(lmm->mindex, msgno - 1);
        }
    }
#endif                          /*BALSA_USE_THREADS */

    return entry;
}

gchar *libbalsa_mailbox_date_format;
static void
mbox_model_get_value(GtkTreeModel *tree_model,
                     GtkTreeIter  *iter,
                     gint column,
                     GValue *value)
{
    LibBalsaMailbox* lmm = LIBBALSA_MAILBOX(tree_model);
    LibBalsaMailboxIndexEntry* msg = NULL;
    guint msgno;
    gchar *tmp;

    g_return_if_fail(VALID_ITER(iter, tree_model));
    g_return_if_fail(column >= 0 &&
                     column < (int) ELEMENTS(mbox_model_col_type));

    g_value_init (value, mbox_model_col_type[column]);
    msgno = libbalsa_mailbox_get_msgno(iter->user_data);

    if(column == LB_MBOX_MSGNO_COL) {
        g_value_set_uint(value, msgno);
        return;
    }
    /* gtk2-2.3.5 can in principle do it  but we want to be sure.
     */
    g_return_if_fail(msgno<=libbalsa_mailbox_total_messages(lmm));
    /* With gtk-2.8.0, we can finally use fixed-row-height model
       without workarounds. */
#if GTK_CHECK_VERSION(2,8,0)
    msg = lbm_get_index_entry(lmm, iter->user_data);
#else 
    { GdkRectangle a, b, c, d; 
    /* assumed that only one view is showing the mailbox */
    GtkTreeView *tree = g_object_get_data(G_OBJECT(tree_model), "tree-view");

    if (GTK_WIDGET_REALIZED(GTK_WIDGET(tree))) {
        GtkTreePath *path;
        GtkTreeViewColumn *col;

        path = gtk_tree_model_get_path(tree_model, iter);
        col = gtk_tree_view_get_column(tree, ((column == LB_MBOX_WEIGHT_COL
                                               || column == LB_MBOX_STYLE_COL)
                                              ? LB_MBOX_FROM_COL : column));
        gtk_tree_view_get_visible_rect(tree, &a);
        gtk_tree_view_get_cell_area(tree, path, col, &b);
        gtk_tree_view_widget_to_tree_coords(tree, b.x, b.y, &c.x, &c.y);
        gtk_tree_view_widget_to_tree_coords(tree, b.x + b.width,
                                            b.y + b.height,
                                            &c.width, &c.height);
        c.width -= c.x; c.height -= c.y;
        if (gdk_rectangle_intersect(&a, &c, &d))
            msg = lbm_get_index_entry(lmm, iter->user_data);
        gtk_tree_path_free(path);
    }
    }
#endif
    switch(column) {
        /* case LB_MBOX_MSGNO_COL: handled above */
    case LB_MBOX_MARKED_COL:
        if (msg && msg->status_icon < LIBBALSA_MESSAGE_STATUS_ICONS_NUM)
            g_value_set_object(value, status_icons[msg->status_icon]);
        break;
    case LB_MBOX_ATTACH_COL:
        if (msg && msg->attach_icon < LIBBALSA_MESSAGE_ATTACH_ICONS_NUM)
            g_value_set_object(value, attach_icons[msg->attach_icon]);
        break;
    case LB_MBOX_FROM_COL:
	if(msg) {
            if (msg->from)
                g_value_set_string(value, msg->from);
            else
                g_value_set_static_string(value, _("from unknown"));
        } else
            g_value_set_static_string(value, _("Loading..."));
        break;
    case LB_MBOX_SUBJECT_COL:
        if(msg) g_value_set_string(value, msg->subject);
        break;
    case LB_MBOX_DATE_COL:
        if(msg) {
            tmp = libbalsa_date_to_utf8(&msg->msg_date,
		                        libbalsa_mailbox_date_format);
            g_value_take_string(value, tmp);
        }
        break;
    case LB_MBOX_SIZE_COL:
        if(msg) {
            tmp = libbalsa_size_to_gchar(msg->size);
            g_value_take_string(value, tmp);
        }
        else g_value_set_static_string(value, "          ");
        break;
    case LB_MBOX_WEIGHT_COL:
        g_value_set_uint(value, msg && msg->unseen
                         ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
        break;
    case LB_MBOX_STYLE_COL:
        g_value_set_uint(value, msg &&
			 lbm_node_has_unseen_child(lmm, iter->user_data) ?
                         PANGO_STYLE_OBLIQUE : PANGO_STYLE_NORMAL);
        break;
    }
}

static gboolean
mbox_model_iter_next(GtkTreeModel      *tree_model,
                     GtkTreeIter       *iter)
{
    GSequenceIter *node;

    g_return_val_if_fail(VALID_ITER(iter, tree_model), FALSE);

    node = iter->user_data;
    if (node) {
        node = g_sequence_iter_next(node);
        if (!g_sequence_iter_is_end(node)) {
            iter->user_data = node;
            VALIDATE_ITER(iter, tree_model);
            return TRUE;
        }
    }

    INVALIDATE_ITER(iter);
    return FALSE;
}

static gboolean
mbox_model_iter_children(GtkTreeModel      *tree_model,
                         GtkTreeIter       *iter,
                         GtkTreeIter       *parent)
{
    GSequence *seq;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(parent == NULL ||
                         VALID_ITER(parent, tree_model), FALSE);

    seq = parent ? lbm_node_get_children(parent->user_data)
                 : LIBBALSA_MAILBOX(tree_model)->msg_tree;

    if (seq && g_sequence_get_length(seq) > 0) {
        iter->user_data = g_sequence_get_begin_iter(seq);
        VALIDATE_ITER(iter, tree_model);
        return TRUE;
    } else
        return FALSE;
}

static gboolean
mbox_model_iter_has_child(GtkTreeModel  * tree_model,
                          GtkTreeIter   * iter)
{
    g_return_val_if_fail(VALID_ITER(iter, LIBBALSA_MAILBOX(tree_model)),
                         FALSE);

    return lbm_node_get_children(iter->user_data) != NULL;
}

static gint
mbox_model_iter_n_children(GtkTreeModel      *tree_model,
                           GtkTreeIter       *iter)
{
    GSequence     *seq;

    g_return_val_if_fail(iter == NULL || VALID_ITER(iter, tree_model), 0);

    seq = iter ? lbm_node_get_children(iter->user_data)
               : LIBBALSA_MAILBOX(tree_model)->msg_tree;

    return seq ? g_sequence_get_length(seq) : 0;
}

static gboolean
mbox_model_iter_nth_child(GtkTreeModel  * tree_model,
                          GtkTreeIter   * iter,
                          GtkTreeIter   * parent,
                          gint            n)
{
    GSequence     *seq;
    GSequenceIter *node;

    INVALIDATE_ITER(iter);
    if(!LIBBALSA_MAILBOX(tree_model)->msg_tree) 
        return FALSE; /* really, this should be never called when
                       * msg_tree == NULL but the FALSE response is
                       * fair as well and only a bit dirtier.
                       * I have more critical problems to debug now. */
    g_return_val_if_fail(parent == NULL
                         ||VALID_ITER(parent, tree_model), FALSE);

    seq = parent ? lbm_node_get_children(parent->user_data)
                 : LIBBALSA_MAILBOX(tree_model)->msg_tree;
    if(!seq)  /* the tree has been destroyed already (mailbox has been
               * closed), there is nothing to iterate over. This happens
               * only if mailbox is closed but a view is still active. 
               */
        return FALSE;

    node = g_sequence_get_iter_at_pos(seq, n);
    if (g_sequence_iter_is_end(node))
        node = NULL;

    if (node) {
        iter->user_data = node;
        VALIDATE_ITER(iter, tree_model);
        return TRUE;
    } else
        return FALSE;
}

static gboolean
mbox_model_iter_parent(GtkTreeModel     * tree_model,
                       GtkTreeIter      * iter,
                       GtkTreeIter      * child)
{
    GSequenceIter *node;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(iter != NULL, FALSE);
    g_return_val_if_fail(VALID_ITER(child, tree_model), FALSE);

    node = child->user_data;
    node = libbalsa_mailbox_get_parent(node);
    if (node) {
        iter->user_data = node;
        VALIDATE_ITER(iter, tree_model);
        return TRUE;
    } else
        return FALSE;
}

/* Set icons used in tree view. */
static void
libbalsa_mailbox_set_icon(GdkPixbuf * pixbuf, GdkPixbuf ** pixbuf_store)
{
    if (*pixbuf_store)
        g_object_unref(*pixbuf_store);
    *pixbuf_store = pixbuf;
}

/* Icons for status column. */
void
libbalsa_mailbox_set_unread_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &status_icons
                              [LIBBALSA_MESSAGE_STATUS_UNREAD]);
}

void libbalsa_mailbox_set_trash_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &status_icons
                              [LIBBALSA_MESSAGE_STATUS_DELETED]);
}

void libbalsa_mailbox_set_flagged_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &status_icons
                              [LIBBALSA_MESSAGE_STATUS_FLAGGED]);
}

void libbalsa_mailbox_set_replied_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &status_icons
                              [LIBBALSA_MESSAGE_STATUS_REPLIED]);
}

/* Icons for attachment column. */
void libbalsa_mailbox_set_attach_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &attach_icons
                              [LIBBALSA_MESSAGE_ATTACH_ATTACH]);
}

#ifdef HAVE_GPGME
void libbalsa_mailbox_set_good_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &attach_icons
                              [LIBBALSA_MESSAGE_ATTACH_GOOD]);
}

void libbalsa_mailbox_set_notrust_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &attach_icons
                              [LIBBALSA_MESSAGE_ATTACH_NOTRUST]);
}

void libbalsa_mailbox_set_bad_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &attach_icons
                              [LIBBALSA_MESSAGE_ATTACH_BAD]);
}

void libbalsa_mailbox_set_sign_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &attach_icons
                              [LIBBALSA_MESSAGE_ATTACH_SIGN]);
}

void libbalsa_mailbox_set_encr_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
                              &attach_icons
                              [LIBBALSA_MESSAGE_ATTACH_ENCR]);
}
#endif /* HAVE_GPGME */

/* =================================================================== *
 * GtkTreeDragSource implementation functions.                         *
 * =================================================================== */

static gboolean mbox_row_draggable(GtkTreeDragSource * drag_source,
                                   GtkTreePath * path);
static gboolean mbox_drag_data_delete(GtkTreeDragSource * drag_source,
                                      GtkTreePath * path);
static gboolean mbox_drag_data_get(GtkTreeDragSource * drag_source,
                                   GtkTreePath * path,
                                   GtkSelectionData * selection_data);

static void
mbox_drag_source_init(GtkTreeDragSourceIface * iface)
{
    iface->row_draggable    = mbox_row_draggable;
    iface->drag_data_delete = mbox_drag_data_delete;
    iface->drag_data_get    = mbox_drag_data_get;
}

/* These three methods are apparently never called, so what they return
 * is irrelevant.  The code reflects guesses about what they should
 * return if they were ever called.
 */
static gboolean
mbox_row_draggable(GtkTreeDragSource * drag_source, GtkTreePath * path)
{
    /* All rows are valid sources. */
    return TRUE;
}

static gboolean
mbox_drag_data_delete(GtkTreeDragSource * drag_source, GtkTreePath * path)
{
    /* The "drag-data-received" callback handles deleting messages that
     * are dragged out of the mailbox, so we don't. */
    return FALSE;
}

static gboolean
mbox_drag_data_get(GtkTreeDragSource * drag_source, GtkTreePath * path,
                   GtkSelectionData * selection_data)
{
    /* The "drag-data-get" callback passes the list of selected messages
     * to the GtkSelectionData, so we don't. */
    return FALSE;
}

/* =================================================================== *
 * GtkTreeSortable implementation functions.                           *
 * =================================================================== */

static gboolean mbox_get_sort_column_id(GtkTreeSortable * sortable,
                                        gint * sort_column_id,
                                        GtkSortType * order);
static void mbox_set_sort_column_id(GtkTreeSortable * sortable,
                                    gint sort_column_id,
                                    GtkSortType order);
static void mbox_set_sort_func(GtkTreeSortable * sortable,
                               gint sort_column_id,
                               GtkTreeIterCompareFunc func, gpointer data,
                               GtkDestroyNotify destroy);
static void mbox_set_default_sort_func(GtkTreeSortable * sortable,
                                       GtkTreeIterCompareFunc func,
                                       gpointer data,
                                       GtkDestroyNotify destroy);
static gboolean mbox_has_default_sort_func(GtkTreeSortable * sortable);

static void
mbox_sortable_init(GtkTreeSortableIface * iface)
{
    iface->get_sort_column_id    = mbox_get_sort_column_id;
    iface->set_sort_column_id    = mbox_set_sort_column_id;
    iface->set_sort_func         = mbox_set_sort_func;
    iface->set_default_sort_func = mbox_set_default_sort_func;
    iface->has_default_sort_func = mbox_has_default_sort_func;
}

static gint
mbox_compare_from(LibBalsaMailboxIndexEntry * message_a,
                  LibBalsaMailboxIndexEntry * message_b)
{
    return g_ascii_strcasecmp(message_a->from, message_b->from);
}

static gint
mbox_compare_subject(LibBalsaMailboxIndexEntry * message_a,
                     LibBalsaMailboxIndexEntry * message_b)
{
    return g_ascii_strcasecmp(message_a->subject, message_b->subject);
}

static gint
mbox_compare_date(LibBalsaMailboxIndexEntry * message_a,
                  LibBalsaMailboxIndexEntry * message_b)
{
    return message_a->msg_date - message_b->msg_date;
}

static gint
mbox_compare_size(LibBalsaMailboxIndexEntry * message_a,
                  LibBalsaMailboxIndexEntry * message_b)
{
    return message_a->size - message_b->size;
}

static gint
mbox_compare_func(const SortTuple * a,
                  const SortTuple * b,
                  LibBalsaMailbox * mbox)
{
    guint msgno_a;
    guint msgno_b;
    gint retval;

    msgno_a = libbalsa_mailbox_get_msgno(a->node);
    msgno_b = libbalsa_mailbox_get_msgno(b->node);
    if (mbox->view->sort_field == LB_MAILBOX_SORT_NO)
	retval = msgno_a - msgno_b;
    else {
	LibBalsaMailboxIndexEntry *message_a;
	LibBalsaMailboxIndexEntry *message_b;

	message_a = g_ptr_array_index(mbox->mindex, msgno_a - 1);
	message_b = g_ptr_array_index(mbox->mindex, msgno_b - 1);

	if (!(VALID_ENTRY(message_a) && VALID_ENTRY(message_b)))
	    return 0;

	switch (mbox->view->sort_field) {
	case LB_MAILBOX_SORT_SENDER:
	    retval = mbox_compare_from(message_a, message_b);
	    break;
	case LB_MAILBOX_SORT_SUBJECT:
	    retval = mbox_compare_subject(message_a, message_b);
	    break;
	case LB_MAILBOX_SORT_DATE:
	    retval = mbox_compare_date(message_a, message_b);
	    break;
	case LB_MAILBOX_SORT_SIZE:
	    retval = mbox_compare_size(message_a, message_b);
	    break;
	default:
	    retval = 0;
	    break;
	}
    }

    if (mbox->view->sort_type == LB_MAILBOX_SORT_TYPE_DESC) {
        retval = -retval;
    }

    return retval;
}

/*
 * Sort part of the mailbox tree.
 *
 * We may not have the sort fields for all nodes, so we build an array
 * for only the nodes where we do have them.
 */
static gboolean
lbm_has_valid_index_entry(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxIndexEntry *entry;

    if (msgno > mailbox->mindex->len)
        return FALSE;

    entry = g_ptr_array_index(mailbox->mindex, msgno - 1);

    return VALID_ENTRY(entry);
}

static void lbm_sort_seq_with_parent(LibBalsaMailbox * mbox,
                                     GSequence       * seq,
                                     GSequenceIter   * parent);

static void
lbm_sort_seq(LibBalsaMailbox * mbox, GSequence * seq)
{
    lbm_sort_seq_with_parent(mbox, seq, NULL);
}

static void
lbm_sort_children(LibBalsaMailbox * mbox, GSequenceIter * parent)
{
    lbm_sort_seq_with_parent(mbox, lbm_node_get_children(parent), parent);
}

static void
lbm_sort_seq_with_parent(LibBalsaMailbox * mbox,
                         GSequence       * seq,
                         GSequenceIter   * parent)
{
    gint seq_length;
    GtkTreeIter iter;
    GArray *sort_array;
    GPtrArray *node_array;
    GSequenceIter *node, *tmp_node, *prev, *end;
    guint i, j;
    gint *new_order;
    GtkTreePath *path;
    gboolean sort_no = mbox->view->sort_field == LB_MAILBOX_SORT_NO;
#if !defined(LOCAL_MAILBOX_SORTED_JUST_ONCE_ON_OPENING)
    gboolean can_sort_all = sort_no || LIBBALSA_IS_MAILBOX_IMAP(mbox);
#else
    gboolean can_sort_all = 1;
#endif

    if (!seq || (seq_length = g_sequence_get_length(seq)) == 0)
        return;

    node = g_sequence_get_begin_iter(seq);
    if (seq_length == 1) {
        lbm_sort_children(mbox, node);
        return;
    }

    sort_array = g_array_new(FALSE, FALSE, sizeof(SortTuple));
    node_array = g_ptr_array_new();
    end = g_sequence_get_end_iter(seq);
    for (tmp_node = node; tmp_node != end;
         tmp_node = g_sequence_iter_next(tmp_node)) {
        SortTuple sort_tuple;
        guint msgno = libbalsa_mailbox_get_msgno(tmp_node);

        if (can_sort_all || lbm_has_valid_index_entry(mbox, msgno)) {
            /* We have the sort fields. */
            sort_tuple.offset = node_array->len;
            sort_tuple.node = tmp_node;
            g_array_append_val(sort_array, sort_tuple);
        }
        g_ptr_array_add(node_array, tmp_node);
    }

    if (sort_array->len <= 1) {
        g_array_free(sort_array, TRUE);
        g_ptr_array_free(node_array, TRUE);
        lbm_sort_children(mbox, node);
        return;
    }
    LIBBALSA_MAILBOX_GET_CLASS(mbox)->sort(mbox, sort_array);

    /* Step through the nodes in original order. */
    prev = NULL;
    for (i = j = 0; i < node_array->len; i++) {
        guint msgno;

        tmp_node = g_ptr_array_index(node_array, i);
        msgno = libbalsa_mailbox_get_msgno(tmp_node);
        if (can_sort_all || lbm_has_valid_index_entry(mbox, msgno)) {
            /* This is one of the nodes we sorted: find out which one
             * goes here. */
            g_assert(j < sort_array->len);
            tmp_node = g_array_index(sort_array, SortTuple, j++).node;
        }
        if ((prev && g_sequence_iter_prev(tmp_node) != prev)
            || (!prev && !g_sequence_iter_is_begin(tmp_node))) {
            g_sequence_move(tmp_node,
                            prev ? g_sequence_iter_next(prev)
                                 : g_sequence_get_begin_iter(seq));
            mbox->msg_tree_changed = TRUE;
        } else
            g_assert(prev == NULL
                     || g_sequence_iter_next(prev) == tmp_node);
        prev = tmp_node;
    }
    node = g_sequence_get_begin_iter(seq);

    /* Let the world know about our new order */
    new_order = g_new(gint, node_array->len);
    i = j = 0;
    for (tmp_node = node; tmp_node != end;
         tmp_node = g_sequence_iter_next(tmp_node)) {
        guint msgno = libbalsa_mailbox_get_msgno(tmp_node);
        new_order[j] =
            can_sort_all || lbm_has_valid_index_entry(mbox, msgno) ?
            g_array_index(sort_array, SortTuple, i++).offset : j;
        j++;
    }

    iter.stamp = mbox->stamp;
    iter.user_data = parent;
    path = parent ? mbox_model_get_path(GTK_TREE_MODEL(mbox), &iter)
                  : gtk_tree_path_new();
    gtk_tree_model_rows_reordered(GTK_TREE_MODEL(mbox), path,
                                  parent ? &iter : NULL,
                                  new_order);
    gtk_tree_path_free(path);
    g_free(new_order);
    g_array_free(sort_array, TRUE);
    g_ptr_array_free(node_array, TRUE);

    for (tmp_node = node; tmp_node != end;
         tmp_node = g_sequence_iter_next(tmp_node))
        lbm_sort_children(mbox, tmp_node);
}

/* called from gtk-tree-view-column */
static gboolean
mbox_get_sort_column_id(GtkTreeSortable * sortable,
                        gint            * sort_column_id,
                        GtkSortType     * order)
{
    LibBalsaMailbox *mbox = (LibBalsaMailbox *) sortable;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(sortable), FALSE);

    if (sort_column_id) {
        switch (mbox->view->sort_field) {
        default:
        case LB_MAILBOX_SORT_NO:
            *sort_column_id = LB_MBOX_MSGNO_COL;
            break;
        case LB_MAILBOX_SORT_SENDER:
            *sort_column_id = LB_MBOX_FROM_COL;
            break;
        case LB_MAILBOX_SORT_SUBJECT:
            *sort_column_id = LB_MBOX_SUBJECT_COL;
            break;
        case LB_MAILBOX_SORT_DATE:
            *sort_column_id = LB_MBOX_DATE_COL;
            break;
        case LB_MAILBOX_SORT_SIZE:
            *sort_column_id = LB_MBOX_SIZE_COL;
            break;
        }
    }

    if (order)
        *order = (mbox->view->sort_type ==
                  LB_MAILBOX_SORT_TYPE_DESC ? GTK_SORT_DESCENDING :
                  GTK_SORT_ASCENDING);

    return TRUE;
}

/* called from gtk-tree-view-column */
static void
mbox_set_sort_column_id(GtkTreeSortable * sortable,
                        gint              sort_column_id,
                        GtkSortType       order)
{
    LibBalsaMailbox *mbox = (LibBalsaMailbox *) sortable;
    LibBalsaMailboxView *view;
    LibBalsaMailboxSortFields new_field;
    LibBalsaMailboxSortType new_type;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(sortable));

    view = mbox->view;

    switch (sort_column_id) {
    default:
    case LB_MBOX_MSGNO_COL:
        new_field = LB_MAILBOX_SORT_NO;
        break;
    case LB_MBOX_FROM_COL:
        new_field = LB_MAILBOX_SORT_SENDER;
        break;
    case LB_MBOX_SUBJECT_COL:
        new_field = LB_MAILBOX_SORT_SUBJECT;
        break;
    case LB_MBOX_DATE_COL:
        new_field = LB_MAILBOX_SORT_DATE;
        break;
    case LB_MBOX_SIZE_COL:
        new_field = LB_MAILBOX_SORT_SIZE;
        break;
    }

    new_type = (order == GTK_SORT_DESCENDING ? LB_MAILBOX_SORT_TYPE_DESC :
                LB_MAILBOX_SORT_TYPE_ASC);

    if (view->sort_field == new_field && view->sort_type == new_type)
        return;

    view->sort_field = new_field;
    view->sort_type = new_type;
    view->in_sync = 0;

    gtk_tree_sortable_sort_column_changed(sortable);

    if (new_field != LB_MAILBOX_SORT_NO) {
        gboolean rc;

        gdk_threads_leave();
        rc = libbalsa_mailbox_prepare_threading(mbox, 0);
        gdk_threads_enter();

        if (!rc)
            /* Prepare-threading failed--perhaps mailbox was closed. */
            return;
    }
    lbm_sort_seq(mbox, mbox->msg_tree);

    libbalsa_mailbox_changed(mbox);
}

static void
mbox_set_sort_func(GtkTreeSortable * sortable,
                   gint sort_column_id,
                   GtkTreeIterCompareFunc func,
                   gpointer data, GtkDestroyNotify destroy)
{
    g_warning("%s called but not implemented.\n", __func__);
}

static void
mbox_set_default_sort_func(GtkTreeSortable * sortable,
                           GtkTreeIterCompareFunc func,
                           gpointer data, GtkDestroyNotify destroy)
{
    g_warning("%s called but not implemented.\n", __func__);
}

/* called from gtk-tree-view-column */
static gboolean
mbox_has_default_sort_func(GtkTreeSortable * sortable)
{
    return FALSE;
}

/* Helpers for set-threading-type. */
void
libbalsa_mailbox_check_parent(LibBalsaMailbox * mailbox,
                              GSequenceIter   * node,
                              GSequenceIter   * parent)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    GSequenceIter *current_parent;
    GSequence *children;
    gboolean first_child = FALSE;

    if (!libbalsa_threads_has_lock())
        g_warning("Thread is not holding gdk lock");

    if (libbalsa_mailbox_get_parent(node) == parent
        || lbm_node_is_ancestor(node, parent))
        return;

    iter.stamp = mailbox->stamp;

    /* Get path before moving the node. */
    path = mbox_model_get_path_helper(node);
    current_parent = libbalsa_mailbox_get_parent(node);

    children = parent ? lbm_node_get_children(parent) : mailbox->msg_tree;
    if (!children) {
        first_child = TRUE;
        children = lbm_node_init_children(parent);
    }

    /* Move the node. */
    g_sequence_move(node, g_sequence_get_begin_iter(children));
    lbm_node_set_parent(node, parent);

    /* Announce the removal from the old location. */
    g_signal_emit(mailbox,
                  libbalsa_mbox_model_signals[ROW_DELETED], 0, path);
    if (current_parent && !lbm_node_get_children(current_parent)) {
        /* It was the last child. */
        gtk_tree_path_up(path);
        iter.user_data = current_parent;
        g_signal_emit(mailbox,
                      libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                      0, path, &iter);
    }
    gtk_tree_path_free(path);

    path = parent ? mbox_model_get_path_helper(parent) : gtk_tree_path_new();
    if (first_child) {
        /* Parent now has a child. */
        iter.user_data = parent;
        g_signal_emit(mailbox,
                      libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                      0, path, &iter);
    }

    /* Announce the new row, and that it has children. */
    gtk_tree_path_down(path);
    iter.user_data = node;
    g_signal_emit(mailbox,
                  libbalsa_mbox_model_signals[ROW_INSERTED], 0,
                  path, &iter);
    if (lbm_node_get_children(node))
        g_signal_emit(mailbox,
                      libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                      0, path, &iter);
    gtk_tree_path_free(path);

    mailbox->msg_tree_changed = TRUE;
}

struct lbm_update_msg_tree_info {
    LibBalsaMailbox *mailbox;
    GNode *new_tree;
    gpointer *nodes;
    guint total;
};

/* GNodeTraverseFunc for making a reverse lookup array into a tree. */
static gboolean
lbm_update_msg_tree_gnode_func(GNode * node, gpointer data)
{
    struct lbm_update_msg_tree_info *mti = data;
    guint msgno;

    msgno = GPOINTER_TO_UINT(node->data);

    if (msgno < mti->total)
        mti->nodes[msgno] = node;

    return FALSE;
}

/* LBMTraverseFunc for making a reverse lookup array into a tree. */
static gboolean
lbm_update_msg_tree_lbmnode_func(GSequenceIter * node, gpointer data)
{
    struct lbm_update_msg_tree_info *mti = data;
    guint msgno;

    msgno = libbalsa_mailbox_get_msgno(node);

    if (msgno < mti->total)
        mti->nodes[msgno] = node;

    return FALSE;
}

/* LBMTraverseFunc for pruning the current tree; mti->nodes is a
 * reverse lookup array into the new tree, so a NULL value is a node
 * that doesn't appear in the new tree. */
static gboolean
lbm_update_msg_tree_prune(GSequenceIter * node, gpointer data)
{
    struct lbm_update_msg_tree_info *mti = data;
    guint msgno;

    msgno = libbalsa_mailbox_get_msgno(node);
    if (msgno >= mti->total || !mti->nodes[msgno])
        /* It's a bottom-up traverse, so the node's remaining children
         * are all in the new tree; we'll promote them to be children of
         * the node's parent, which might even be where they finish up. */
        lbm_node_remove(mti->mailbox, node);

    return FALSE;
}

/* GNodeTraverseFunc for checking parent-child relationships; mti->nodes
 * is a reverse lookup array into the old tree, so a NULL value means a
 * node that isn't in the current tree, and we insert one; because the
 * traverse is top-down, a missing parent will have been inserted before
 * we get to the child. */
static gboolean
lbm_update_msg_tree_move(GNode * new_node,
                         struct lbm_update_msg_tree_info *mti)
{
    guint msgno, parent_msgno;
    GSequenceIter *node;
    GSequenceIter *parent;

    if (!new_node->parent)
        /* Root node of the new tree. */
        return FALSE;

    msgno = GPOINTER_TO_UINT(new_node->data);
    if (msgno >= mti->total)
        return FALSE;

    node = mti->nodes[msgno];

    parent_msgno = GPOINTER_TO_UINT(new_node->parent->data);
    if (parent_msgno >= mti->total)
        return FALSE;

    parent = mti->nodes[parent_msgno];

    if (node) 
        libbalsa_mailbox_check_parent(mti->mailbox, node, parent);
    else
        mti->nodes[msgno] =
            libbalsa_mailbox_msgno_inserted(mti->mailbox, msgno, parent, NULL);

    return FALSE;
}

static void
lbm_update_msg_tree(LibBalsaMailbox * mailbox, GNode * new_tree)
{
    struct lbm_update_msg_tree_info mti;

    mti.mailbox = mailbox;
    mti.new_tree = new_tree;
    mti.total = 1 + libbalsa_mailbox_total_messages(mailbox);
    mti.nodes = g_new0(gpointer, mti.total);

    /* Populate the nodes array with nodes in the new tree. */
    g_node_traverse(new_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    lbm_update_msg_tree_gnode_func, &mti);
    /* Remove deadwood. */
    libbalsa_mailbox_traverse(mailbox, G_POST_ORDER,
                              lbm_update_msg_tree_prune, &mti);

    /* Clear the nodes array and repopulate it with nodes in the current
     * tree. */
    memset(mti.nodes, 0, sizeof(gpointer) * mti.total);
    libbalsa_mailbox_traverse(mailbox, G_PRE_ORDER,
                              lbm_update_msg_tree_lbmnode_func, &mti);
    /* Check parent-child relationships. */
    g_node_traverse(new_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) lbm_update_msg_tree_move, &mti);

    g_free(mti.nodes);
}

void
libbalsa_mailbox_set_msg_tree(LibBalsaMailbox * mailbox, GNode * new_tree)
{
    gdk_threads_enter();

    if (!mailbox->msg_tree)
        mailbox->msg_tree =
            g_sequence_new((GDestroyNotify) lbm_node_info_free);

    lbm_update_msg_tree(mailbox, new_tree);
    g_node_destroy(new_tree);

    mailbox->msg_tree_changed = TRUE;

    gdk_threads_leave();
}

static GMimeMessage *
lbm_get_mime_msg(LibBalsaMailbox * mailbox, LibBalsaMessage * msg)
{
    GMimeMessage *mime_msg;

    if (!msg->mime_msg)
        libbalsa_mailbox_fetch_message_structure(mailbox, msg,
                                                 LB_FETCH_RFC822_HEADERS);
    mime_msg = msg->mime_msg;
    if (mime_msg)
        g_object_ref(mime_msg);
    else {
        GMimeStream *stream;
        GMimeParser *parser;

        stream = libbalsa_mailbox_get_message_stream(mailbox, msg->msgno);
        parser = g_mime_parser_new_with_stream(stream);
        g_object_unref(stream);
        mime_msg = g_mime_parser_construct_message(parser);
        g_object_unref(parser);
    }
    libbalsa_mailbox_release_message(mailbox, msg);

    return mime_msg;
}

/* Try to reassemble messages of type message/partial with the given id;
 * if successful, delete the parts, so we don't keep creating the whole
 * message. */

static void
lbm_try_reassemble_func(GMimeObject * mime_part, gpointer data)
{
    if (GMIME_IS_MESSAGE_PART(mime_part))
        mime_part = ((GMimeMessagePart *) mime_part)->message->mime_part;
    if (GMIME_IS_MESSAGE_PARTIAL(mime_part)) {
        const GMimeMessagePartial **partial = data;
        *partial = (GMimeMessagePartial *) mime_part;
    }
}

static void
lbm_try_reassemble(LibBalsaMailbox * mailbox, const gchar * id)
{
    gchar *text;
    guint total_messages;
    LibBalsaProgress progress;
    guint msgno;
    GPtrArray *partials = g_ptr_array_new();
    guint total = G_MAXUINT;
    GArray *messages = g_array_new(FALSE, FALSE, sizeof(guint));

    text = g_strdup_printf(_("Searching %s for partial messages"),
                           mailbox->name);
    total_messages = libbalsa_mailbox_total_messages(mailbox);
    libbalsa_progress_set_text(&progress, text, total_messages);
    g_free(text);

    for (msgno = 1; msgno <= total_messages && partials->len < total;
         msgno++) {
        LibBalsaMessage *message;
        gchar *tmp_id;

        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                             LIBBALSA_MESSAGE_FLAG_DELETED,
                                             0)
            || !(message = libbalsa_mailbox_get_message(mailbox, msgno)))
            continue;

        if (!libbalsa_message_is_partial(message, &tmp_id)) {
            g_object_unref(message);
            continue;
        }

        if (strcmp(tmp_id, id) == 0) {
            GMimeMessage *mime_message = lbm_get_mime_msg(mailbox, message);
            GMimeMessagePartial *partial =
                (GMimeMessagePartial *) mime_message->mime_part;

            g_ptr_array_add(partials, partial);
            if (g_mime_message_partial_get_total(partial) > 0)
                total = g_mime_message_partial_get_total(partial);
            g_object_ref(partial);
            g_object_unref(mime_message);

            g_array_append_val(messages, msgno);
        }

        g_free(tmp_id);
        g_object_unref(message);
        libbalsa_progress_set_fraction(&progress, ((gdouble) msgno) /
                                       ((gdouble) total_messages));
    }

    if (partials->len < total) {
        /* Someone might have wrapped a message/partial in a
         * message/multipart. */
        libbalsa_progress_set_fraction(&progress, 0);
        for (msgno = 1; msgno <= total_messages && partials->len < total;
             msgno++) {
            LibBalsaMessage *message;
            GMimeMessage *mime_message;
            GMimeMessagePartial *partial;

            if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                                 LIBBALSA_MESSAGE_FLAG_DELETED,
                                                 0)
                || !(message = libbalsa_mailbox_get_message(mailbox, msgno)))
                continue;

            if (!libbalsa_message_is_multipart(message)) {
                g_object_unref(message);
                continue;
            }

            mime_message = lbm_get_mime_msg(mailbox, message);
            partial = NULL;
            g_mime_multipart_foreach((GMimeMultipart *)
                                     mime_message->mime_part,
                                     (GMimePartFunc)
                                     lbm_try_reassemble_func, &partial);
            if (partial
                && strcmp(g_mime_message_partial_get_id(partial), id) == 0) {
                g_ptr_array_add(partials, partial);
                if (g_mime_message_partial_get_total(partial) > 0)
                    total = g_mime_message_partial_get_total(partial);
                g_object_ref(partial);
                /* We will leave  this message undeleted, as it may have
                 * some warning content.
                 * g_array_append_val(messages, msgno); */
            }
            g_object_unref(mime_message);
            g_object_unref(message);
            libbalsa_progress_set_fraction(&progress, ((gdouble) msgno) /
                                           ((gdouble) total_messages));
        }
    }

    if (partials->len == total) {
        LibBalsaMessage *message = libbalsa_message_new();
        message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;

        libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                             _("Reconstructing message"));
        libbalsa_mailbox_lock_store(mailbox);
        message->mime_msg =
            g_mime_message_partial_reconstruct_message((GMimeMessagePartial
                                                        **) partials->
                                                       pdata, total);
        libbalsa_message_copy(message, mailbox, NULL);
        libbalsa_mailbox_unlock_store(mailbox);

        g_object_unref(message);
        libbalsa_mailbox_messages_change_flags(mailbox, messages,
                                               LIBBALSA_MESSAGE_FLAG_DELETED,
                                               0);
    }

    g_ptr_array_foreach(partials, (GFunc) g_object_unref, NULL);
    g_ptr_array_free(partials, TRUE);
    g_array_free(messages, TRUE);

    libbalsa_progress_set_text(&progress, NULL, 0);
}

#define LBM_TRY_REASSEMBLE_IDS "libbalsa-mailbox-try-reassemble-ids"

static gboolean
lbm_try_reassemble_idle(LibBalsaMailbox * mailbox)
{
    GSList *id, *ids;

    /* Make sure the thread that detected a message/partial has
     * completed. */
    libbalsa_lock_mailbox(mailbox);

    ids = g_object_get_data(G_OBJECT(mailbox), LBM_TRY_REASSEMBLE_IDS);
    for (id = ids; id; id = id->next)
        lbm_try_reassemble(mailbox, id->data);

    g_slist_foreach(ids, (GFunc) g_free, NULL);
    g_slist_free(ids);
    g_object_set_data(G_OBJECT(mailbox), LBM_TRY_REASSEMBLE_IDS, NULL);

    libbalsa_unlock_mailbox(mailbox);

    g_object_unref(mailbox);

    return FALSE;
}

void
libbalsa_mailbox_try_reassemble(LibBalsaMailbox * mailbox,
                                const gchar * id)
{
    GSList *ids;

    if (mailbox->no_reassemble)
        return;

    ids = g_object_get_data(G_OBJECT(mailbox), LBM_TRY_REASSEMBLE_IDS);
    if (!ids) {
        g_object_ref(mailbox);
        g_idle_add((GSourceFunc) lbm_try_reassemble_idle, mailbox);
    }

    if (!g_slist_find_custom(ids, id, (GCompareFunc) strcmp)) {
        ids = g_slist_prepend(ids, g_strdup(id));
        g_object_set_data(G_OBJECT(mailbox), LBM_TRY_REASSEMBLE_IDS, ids);
    }
}

/* Use "message-expunged" signal to update an array of msgnos. */
static void
lbm_update_msgnos(LibBalsaMailbox * mailbox, guint seqno, GArray * msgnos)
{
    guint i, j;

    for (i = j = 0; i < msgnos->len; i++) {
        guint msgno = g_array_index(msgnos, guint, i);
        if (msgno == seqno)
            continue;
        if (msgno > seqno)
            --msgno;
        g_array_index(msgnos, guint, j) = msgno;
        ++j;
    }
    msgnos->len = j;
}

void
libbalsa_mailbox_register_msgnos(LibBalsaMailbox * mailbox,
                                 GArray * msgnos)
{
    g_signal_connect(mailbox, "message-expunged",
                     G_CALLBACK(lbm_update_msgnos), msgnos);
}

void
libbalsa_mailbox_unregister_msgnos(LibBalsaMailbox * mailbox,
                                   GArray * msgnos)
{
    if (mailbox && msgnos)
	g_signal_handlers_disconnect_by_func(mailbox, lbm_update_msgnos,
                                             msgnos);
}

/* Accessors for LibBalsaMailboxIndexEntry */
LibBalsaMessageStatus
libbalsa_mailbox_msgno_get_status(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxIndexEntry *entry =
        g_ptr_array_index(mailbox->mindex, msgno - 1);
    return VALID_ENTRY(entry) ?
        entry->status_icon : LIBBALSA_MESSAGE_STATUS_ICONS_NUM;
}

const gchar *
libbalsa_mailbox_msgno_get_subject(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxIndexEntry *entry =
        g_ptr_array_index(mailbox->mindex, msgno - 1);
    return VALID_ENTRY(entry) ? entry->subject : NULL;
}

/* Update icons, but only if entry has been allocated. */
void
libbalsa_mailbox_msgno_update_attach(LibBalsaMailbox * mailbox,
				     guint msgno, LibBalsaMessage * message)
{
    LibBalsaMailboxIndexEntry *entry;
    LibBalsaMessageAttach attach_icon;

    if (!mailbox || !mailbox->mindex || mailbox->mindex->len < msgno)
	return;

    entry = g_ptr_array_index(mailbox->mindex, msgno - 1);
    if (!VALID_ENTRY(entry))
	return;

    attach_icon = libbalsa_message_get_attach_icon(message);
    if (entry->attach_icon != attach_icon) {
        GtkTreeIter iter;

	entry->attach_icon = attach_icon;
        iter.user_data = NULL;
	lbm_msgno_changed(mailbox, msgno, &iter);
    }
}

/* Queued check. */

static void
lbm_check_real(LibBalsaMailbox * mailbox)
{
    libbalsa_lock_mailbox(mailbox);
    g_object_set_data(G_OBJECT(mailbox), LB_MAILBOX_CHECK_ID_KEY,
                      GUINT_TO_POINTER(0));
    libbalsa_mailbox_check(mailbox);
    libbalsa_unlock_mailbox(mailbox);
    g_object_unref(mailbox);
}

static gboolean
lbm_check_idle(LibBalsaMailbox * mailbox)
{
#ifdef BALSA_USE_THREADS
    pthread_t check_thread;

    pthread_create(&check_thread, NULL, (void *) lbm_check_real, mailbox);
    pthread_detach(check_thread);
#else                           /*BALSA_USE_THREADS */
    lbm_check_real(mailbox);
#endif                          /*BALSA_USE_THREADS */

    return FALSE;
}

static void
lbm_queue_check(LibBalsaMailbox * mailbox)
{
    guint id;

    if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(mailbox),
                                           LB_MAILBOX_CHECK_ID_KEY)))
	/* Idle callback already scheduled. */
        return;

    g_object_ref(mailbox);
    id = g_idle_add((GSourceFunc) lbm_check_idle, mailbox);
    g_object_set_data(G_OBJECT(mailbox), LB_MAILBOX_CHECK_ID_KEY,
                      GUINT_TO_POINTER(id));
}

/* Search mailbox for a message matching the condition in search_iter,
 * starting at iter, either forward or backward, and abandoning the
 * search if message stop_msgno is reached; return value indicates
 * success of the search.
 *
 * On return:
 * if return value is TRUE,  iter points to the matching message;
 * if return value is FALSE, iter is invalid.
 */
gboolean
libbalsa_mailbox_search_iter_step(LibBalsaMailbox * mailbox,
                                  LibBalsaMailboxSearchIter * search_iter,
                                  GtkTreeIter * iter,
                                  gboolean forward,
                                  guint stop_msgno)
{
    GSequenceIter *node;
    gboolean retval = FALSE;
    gint total;

    if (!libbalsa_threads_has_lock())
        g_warning("Thread is not holding gdk lock");

    node = iter->user_data;

    total = libbalsa_mailbox_total_messages(mailbox);
    for (;;) {
        guint msgno;

        if (node)
            node = forward ? lbm_next(node) : lbm_prev(node);
        else
            node = forward ?
                g_sequence_get_begin_iter(mailbox->msg_tree) :
                g_sequence_iter_prev(g_sequence_get_end_iter
                                     (mailbox->msg_tree));
        msgno = node ? libbalsa_mailbox_get_msgno(node) : 0;
        if (msgno == stop_msgno
	    || --total < 0 /* Runaway? */ ) {
            retval = FALSE;
            break;
        }
        if (msgno > 0
            && libbalsa_mailbox_message_match(mailbox, msgno,
                                              search_iter)) {
            iter->user_data = node;
            retval = TRUE;
            break;
        }
    }

    if (retval)
        VALIDATE_ITER(iter, mailbox);
    else
	INVALIDATE_ITER(iter);

    return retval;
}

/* Remove duplicates */

gboolean
libbalsa_mailbox_can_move_duplicates(LibBalsaMailbox * mailbox)
{
    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->duplicate_msgnos != NULL;
}

void
libbalsa_mailbox_move_duplicates(LibBalsaMailbox * mailbox,
                                 LibBalsaMailbox * dest, GError ** err)
{
    GArray *msgnos = NULL;

    if (libbalsa_mailbox_can_move_duplicates(mailbox))
        msgnos =
            LIBBALSA_MAILBOX_GET_CLASS(mailbox)->duplicate_msgnos(mailbox);

    if (mailbox->state == LB_MAILBOX_STATE_CLOSED) {
        /* duplicate msgnos was interrupted */
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_DUPLICATES_ERROR,
                    _("Finding duplicate messages in source mailbox failed"));
        return;
    }

    if (!msgnos)
        return;

    if (msgnos->len > 0) {
        if (mailbox != dest)
            libbalsa_mailbox_messages_move(mailbox, msgnos, dest, err);
        else
            libbalsa_mailbox_messages_change_flags(mailbox, msgnos,
                                                   LIBBALSA_MESSAGE_FLAG_DELETED,
                                                   0);
    }

    g_array_free(msgnos, TRUE);
}

#if BALSA_USE_THREADS
/* Lock and unlock the mail store. NULL mailbox is not an error. */
void 
libbalsa_mailbox_lock_store(LibBalsaMailbox * mailbox)
{
    if (mailbox)
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->lock_store(mailbox, TRUE);
}

void 
libbalsa_mailbox_unlock_store(LibBalsaMailbox * mailbox)
{
    if (mailbox)
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->lock_store(mailbox, FALSE);
}
#endif                          /* BALSA_USE_THREADS */

void
libbalsa_mailbox_cache_message(LibBalsaMailbox * mailbox, guint msgno,
                               LibBalsaMessage * message)
{
    LibBalsaMailboxIndexEntry *entry;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    if (!mailbox->mindex)
        return;

    if (message) {
        while (mailbox->mindex->len < msgno)
            g_ptr_array_add(mailbox->mindex, NULL);

        entry = g_ptr_array_index(mailbox->mindex, msgno - 1);

        if (!entry)
            g_ptr_array_index(mailbox->mindex, msgno - 1) =
                libbalsa_mailbox_index_entry_new_from_msg(message);
#if BALSA_USE_THREADS
        else if (entry->idle_pending)
            lbm_index_entry_populate_from_msg(entry, message);
#endif                          /* BALSA_USE_THREADS */
        else
            return;
    } else if (msgno <= mailbox->mindex->len) {
        libbalsa_mailbox_index_entry_free(g_ptr_array_index
                                          (mailbox->mindex, msgno - 1));
        g_ptr_array_index(mailbox->mindex, msgno - 1) = NULL;
        libbalsa_mailbox_msgno_changed(mailbox, msgno);
    }
}
