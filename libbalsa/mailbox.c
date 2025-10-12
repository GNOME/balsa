/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "libbalsa.h"
#include "libbalsa-conf.h"
#include "mailbox-filter.h"
#include "message.h"
#include "misc.h"
#include "filter-funcs.h"
#include "libbalsa_private.h"
#include <glib/gi18n.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mailbox"

/* Class functions */
static void libbalsa_mailbox_dispose(GObject * object);
static void libbalsa_mailbox_finalize(GObject * object);

static void libbalsa_mailbox_real_release_message (LibBalsaMailbox * mailbox,
                                                   LibBalsaMessage * message);
static gboolean
libbalsa_mailbox_real_messages_copy(LibBalsaMailbox * mailbox,
                                    GArray * msgnos,
                                    LibBalsaMailbox * dest, GError **err);
static gboolean libbalsa_mailbox_real_can_do(LibBalsaMailbox* mailbox,
                                             enum LibBalsaMailboxCapability c);
static void libbalsa_mailbox_real_sort(LibBalsaMailbox* mailbox,
                                       GArray *sort_array);
static gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox  *mailbox,
                                                LibBalsaCondition *condition);
static void libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
                                              const gchar * group);
static void libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
                                              const gchar * group);
static gboolean libbalsa_mailbox_real_close_backend (LibBalsaMailbox *
                                                     mailbox);
static void libbalsa_mailbox_real_lock_store(LibBalsaMailbox * mailbox,
                                             gboolean lock);
static void libbalsa_mailbox_real_cache_message(LibBalsaMailbox * mailbox,
                                                guint msgno,
                                                LibBalsaMessage * message);

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

static guint libbalsa_mailbox_signals[LAST_SIGNAL];
static guint libbalsa_mailbox_model_signals[LAST_MODEL_SIGNAL];

/* GtkTreeModel function prototypes */
static void  mailbox_model_init(GtkTreeModelIface *iface);

/* GtkTreeDragSource function prototypes */
static void  mailbox_drag_source_init(GtkTreeDragSourceIface *iface);

/* GtkTreeSortable function prototypes */
static void  mailbox_sortable_init(GtkTreeSortableIface *iface);

typedef struct _LibBalsaMailboxPrivate LibBalsaMailboxPrivate;
struct _LibBalsaMailboxPrivate {
    GRecMutex rec_mutex;

    gchar *config_prefix;       /* unique string identifying mailbox */
                                /* in the config file                */
    gchar *name;                /* displayed name for a special mailbox; */
                                /* Isn't it a GUI thing?                 */
    gchar *url; /* Unique resource locator, file://, imap:// etc */

    gint stamp; /* used to determine iterators' validity. Increased on each
                 * modification of mailbox. */
    
    guint open_ref;

    GPtrArray *mindex;  /* the basic message index used for index
                         * displaying/columns of GtkTreeModel interface
                         * and NOTHING else. */
    GNode *msg_tree; /* the possibly filtered tree of messages */
    LibBalsaCondition *view_filter; /* to choose a subset of messages
                                     * to be displayed, e.g., only
                                     * undeleted. */
    LibBalsaCondition *persistent_view_filter; /* the part of the view 
                                                * filter that will persist 
                                                * to the next time the
                                                * mailbox is opened */

    /* info fields */
    glong unread_messages; /* number of unread messages in the mailbox */
    unsigned first_unread; /* set to 0 if there is no unread present.
                            * used for automatical scrolling down on opening.
                            */
    /* Associated filters (struct mailbox_filter) */
    GSList * filters;

    LibBalsaMailboxView *view;
    LibBalsaMailboxState state;

    /* Message ids to reassemble */
    GSList *reassemble_ids;

    /* Array of msgnos that need to be displayed. */
    GArray *msgnos_pending;
    /* Array of msgnos that have been changed. */
    GArray *msgnos_changed;

    guint changed_idle_id;
    guint queue_check_idle_id;
    guint need_threading_idle_id;
    guint run_filters_idle_id;
    guint sort_idle_id;

    unsigned readonly : 1;
    unsigned view_filter_pending : 1;  /* a view filter has been set
                                        * but the view has not been updated */
    /* info fields */
    unsigned has_unread_messages : 1;
    /* Associated filters (struct mailbox_filter) */
    unsigned filters_loaded : 1;
    /* Whether to reassemble a message from its parts. */
    unsigned no_reassemble : 1;
    /* Whether the tree has been changed since some event. */
    unsigned msg_tree_changed : 1;
    /* Whether messages have been threaded. */
    unsigned messages_threaded : 1;
    /* Whether a message should be cached. */
    unsigned must_cache_message : 1;
};

#define LBM_GET_INDEX_ENTRY(priv, msgno) \
    ((LibBalsaMailboxIndexEntry *) (((msgno) <= (priv)->mindex->len) ? \
     g_ptr_array_index((priv)->mindex, (msgno) - 1) : NULL))

G_DEFINE_ABSTRACT_TYPE_WITH_CODE(LibBalsaMailbox,
                                 libbalsa_mailbox,
                                 G_TYPE_OBJECT,
                                 G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL,
                                                       mailbox_model_init)
                                 G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_DRAG_SOURCE,
                                                       mailbox_drag_source_init)
                                 G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_SORTABLE,
                                                       mailbox_sortable_init)
                                 G_ADD_PRIVATE(LibBalsaMailbox)
                       )

static void
libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

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
                     NULL, G_TYPE_NONE, 0);

    libbalsa_mailbox_signals[MESSAGE_EXPUNGED] =
        g_signal_new("message-expunged",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     message_expunged),
                     NULL, NULL,
                     NULL, G_TYPE_NONE, 1,
                     G_TYPE_INT);

    libbalsa_mailbox_signals[PROGRESS_NOTIFY] =
        g_signal_new("progress-notify",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     progress_notify),
                     NULL, NULL,
                     NULL, G_TYPE_NONE,
                     3U, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_STRING);

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
    klass->lock_store  = libbalsa_mailbox_real_lock_store;
    klass->test_can_reach = NULL;
    klass->cache_message = libbalsa_mailbox_real_cache_message;
}

static void
libbalsa_mailbox_init(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_rec_mutex_init(&priv->rec_mutex);

    priv->config_prefix = NULL;
    priv->name = NULL;
    priv->url = NULL;

    priv->open_ref = 0;
    priv->has_unread_messages = FALSE;
    priv->unread_messages = 0;

    priv->readonly = FALSE;

    priv->filters=NULL;
    priv->filters_loaded = FALSE;
    priv->view=NULL;
    /* priv->stamp is incremented before we use it, so it won't be
     * zero for a long, long time... */
    priv->stamp = g_random_int() / 2;

    priv->no_reassemble = FALSE;
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (priv->open_ref != 0) {
        g_warning("%s %s open_ref (%d) != 0", __func__, priv->name, priv->open_ref);

        while (priv->open_ref > 0)
            libbalsa_mailbox_close(mailbox, FALSE);
    }

    G_OBJECT_CLASS(libbalsa_mailbox_parent_class)->dispose(object);
}


static gchar*
get_from_field(LibBalsaMailbox *mailbox, LibBalsaMessage *message)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMessageHeaders *headers;
    InternetAddressList *address_list = NULL;
    const gchar *name_str = NULL;
    gboolean append_dots = FALSE;
    gchar *from;

    headers = libbalsa_message_get_headers(message);

    if (headers != NULL) {
        if (priv->view &&
            priv->view->show == LB_MAILBOX_SHOW_TO)
            address_list = headers->to_list;
        else
            address_list = headers->from;
    }

    if (address_list != NULL) {
        gint i, len = internet_address_list_length(address_list);

        for (i = 0; i < len && name_str == NULL; i++) {
            InternetAddress *ia =
                internet_address_list_get_address(address_list, i);
            if (ia->name && *ia->name) {
                name_str = ia->name;
                if (i < len - 1)
                    append_dots = TRUE;
            } else if (INTERNET_ADDRESS_IS_MAILBOX(ia)) {
                name_str = ((InternetAddressMailbox *) ia)->addr;
                if (i < len - 1)
                    append_dots = TRUE;
            } else {
                InternetAddressGroup *g = (InternetAddressGroup *) ia;
                gint gi, glen =
                    internet_address_list_length(g->members);
                for (gi = 0; gi < glen && name_str == NULL; gi++) {
                    InternetAddress *ia2 =
                        internet_address_list_get_address(g->members, gi);
                    if (ia2->name && *ia2->name) {
                        name_str = ia2->name;
                        if (gi < glen - 1)
                            append_dots = TRUE;
                    } else if (INTERNET_ADDRESS_IS_MAILBOX(ia2)) {
                        name_str = ((InternetAddressMailbox *) ia2)->addr;
                        if (gi < glen - 1)
                            append_dots = TRUE;
                    }
                }
            }
        }
    }

    if (name_str == NULL)
        name_str = "";
    from = append_dots ? g_strconcat(name_str, ",…", NULL)
                       : g_strdup(name_str);
    libbalsa_utf8_sanitize(&from, TRUE, NULL);

    return from;
}

static void
lbm_index_entry_populate_from_msg(LibBalsaMailboxIndexEntry * entry,
                                  LibBalsaMessage * message)
{
    LibBalsaMailbox *mailbox = libbalsa_message_get_mailbox(message);

    entry->from          = get_from_field(mailbox, message);
    entry->subject       = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
    entry->msg_date      = libbalsa_message_get_headers(message)->date;
    entry->internal_date = 0; /* FIXME */
    entry->status_icon   = libbalsa_get_icon_from_flags(libbalsa_message_get_flags(message));
    entry->attach_icon   = libbalsa_message_get_attach_icon(message);
    entry->size          = libbalsa_message_get_length(message);
    entry->foreground     = NULL;
    entry->background     = NULL;
    entry->foreground_set = 0;
    entry->background_set = 0;
    entry->unseen        = LIBBALSA_MESSAGE_IS_UNREAD(message);
    entry->idle_pending  = 0;

    libbalsa_mailbox_msgno_changed(mailbox, libbalsa_message_get_msgno(message));
}

static LibBalsaMailboxIndexEntry*
lbm_index_entry_new_pending(void)
{
    LibBalsaMailboxIndexEntry *entry = g_new(LibBalsaMailboxIndexEntry,1);
    entry->idle_pending = 1;
    return entry;
}

static void
lbm_index_entry_free(LibBalsaMailboxIndexEntry *entry)
{
    if (entry != NULL) {
        if (!entry->idle_pending)
        {
            g_free(entry->from);
            g_free(entry->subject);
        }
        g_free(entry);
    }
}

void
libbalsa_mailbox_index_entry_clear(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(msgno > 0);

    if (msgno <= priv->mindex->len) {
        LibBalsaMailboxIndexEntry **entry = (LibBalsaMailboxIndexEntry **)
            & g_ptr_array_index(priv->mindex, msgno - 1);
        lbm_index_entry_free(*entry);
        *entry = NULL;

        libbalsa_mailbox_msgno_changed(mailbox, msgno);
    }
}

#define VALID_ENTRY(entry) \
    ((entry) && !((LibBalsaMailboxIndexEntry *) (entry))->idle_pending)

void
libbalsa_mailbox_index_set_flags(LibBalsaMailbox *mailbox,
                                 unsigned msgno, LibBalsaMessageFlag f)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxIndexEntry *entry;

    if (msgno > priv->mindex->len)
        return;

    entry = g_ptr_array_index(priv->mindex, msgno-1);
    if (VALID_ENTRY(entry)) {
        entry->status_icon = 
            libbalsa_get_icon_from_flags(f);
        entry->unseen = f & LIBBALSA_MESSAGE_FLAG_NEW;
        libbalsa_mailbox_msgno_changed(mailbox, msgno);
    }
}

static void lbm_msgno_changed_expunged_cb(LibBalsaMailbox * mailbox,
                                          guint seqno);
static void lbm_get_index_entry_expunged_cb(LibBalsaMailbox * mailbox,
                                            guint seqno);

static void
libbalsa_mailbox_finalize(GObject * object)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_rec_mutex_clear(&priv->rec_mutex);

    g_free(priv->config_prefix);
    g_free(priv->name);
    g_free(priv->url);

    libbalsa_condition_unref(priv->view_filter);
    libbalsa_condition_unref(priv->persistent_view_filter);

    g_slist_free_full(priv->filters, g_free);

    if (priv->msgnos_pending != NULL) {
        g_signal_handlers_disconnect_by_func(mailbox,
                                             lbm_get_index_entry_expunged_cb,
                                             priv->msgnos_pending);
        g_array_free(priv->msgnos_pending, TRUE);
    }

    if (priv->msgnos_changed != NULL) {
        g_signal_handlers_disconnect_by_func(mailbox,
                                             lbm_msgno_changed_expunged_cb,
                                             priv->msgnos_changed);
        g_array_free(priv->msgnos_changed, TRUE);
    }

    libbalsa_mailbox_view_free(priv->view);

    if (priv->changed_idle_id != 0)
        g_source_remove(priv->changed_idle_id);

    if (priv->queue_check_idle_id != 0)
        g_source_remove(priv->queue_check_idle_id);

    if (priv->need_threading_idle_id != 0)
        g_source_remove(priv->need_threading_idle_id);

    if (priv->run_filters_idle_id != 0)
        g_source_remove(priv->run_filters_idle_id);

    if (priv->sort_idle_id != 0)
        g_source_remove(priv->sort_idle_id);

    G_OBJECT_CLASS(libbalsa_mailbox_parent_class)->finalize(object);
}

/* Create a new mailbox by loading it from a config entry... */
LibBalsaMailbox *
libbalsa_mailbox_new_from_config(const gchar * group, gboolean is_special)
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
                                 _("Bad local mailbox path “%s”"), path);
    } else if ((type == LIBBALSA_TYPE_MAILBOX_IMAP) && !is_special) {
    	g_critical("old-style IMAP mailbox %s should have been converted to IMAP folder", group);
        libbalsa_conf_pop_group();
        g_free(type_str);
        return NULL;
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (priv->mindex != NULL) {
        g_ptr_array_free(priv->mindex, TRUE);
        priv->mindex = NULL;
    }
}

static gboolean lbm_set_threading(LibBalsaMailbox * mailbox);

gboolean
libbalsa_mailbox_open(LibBalsaMailbox * mailbox, GError **err)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    gboolean retval;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    libbalsa_lock_mailbox(mailbox);

    if (priv->open_ref > 0) {
        priv->open_ref++;
	libbalsa_mailbox_check(mailbox);
        retval = TRUE;
    } else {
	LibBalsaMailboxState saved_state;

        priv->stamp++;
        if(priv->mindex) g_warning("mindex set - I leak memory");
        priv->mindex =
            g_ptr_array_new_with_free_func((GDestroyNotify) lbm_index_entry_free);

	saved_state = priv->state;
	priv->state = LB_MAILBOX_STATE_OPENING;
        priv->messages_threaded = FALSE;
        retval =
            LIBBALSA_MAILBOX_GET_CLASS(mailbox)->open_mailbox(mailbox, err);
        if(retval) {
            priv->open_ref++;
	    priv->state = LB_MAILBOX_STATE_OPEN;
	} else {
	    priv->state = saved_state;
            libbalsa_mailbox_free_mindex(mailbox);
	}
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

gboolean
libbalsa_mailbox_is_open(LibBalsaMailbox *mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    

    return priv->open_ref>0; /* this will break unlisted mailbox types */
}
    
void
libbalsa_mailbox_close(LibBalsaMailbox * mailbox, gboolean expunge)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(MAILBOX_OPEN(mailbox));

    g_object_ref(mailbox);
    libbalsa_lock_mailbox(mailbox);

    if (--priv->open_ref == 0) {
	priv->state = LB_MAILBOX_STATE_CLOSING;
        /* do not try expunging read-only mailboxes, it's a waste of time */
        expunge = expunge && !priv->readonly;
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->close_mailbox(mailbox, expunge);
        if(priv->msg_tree) {
            g_node_destroy(priv->msg_tree);
            priv->msg_tree = NULL;
        }
        libbalsa_mailbox_free_mindex(mailbox);
        priv->stamp++;
	priv->state = LB_MAILBOX_STATE_CLOSED;

        if (priv->sort_idle_id != 0) {
            g_source_remove(priv->sort_idle_id);
            priv->sort_idle_id = 0;
        }

        if (priv->run_filters_idle_id != 0) {
            g_source_remove(priv->run_filters_idle_id);
            priv->run_filters_idle_id = 0;
        }
    }

    libbalsa_unlock_mailbox(mailbox);
    g_object_unref(mailbox);
}

void
libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox * mailbox,
                                          gboolean has_unread)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->has_unread_messages = (has_unread != FALSE);
    libbalsa_mailbox_changed(mailbox);
}

/* libbalsa_mailbox_progress_notify:
   there has been a progress in current operation.
*/
void
libbalsa_mailbox_progress_notify(LibBalsaMailbox       *mailbox,
								 LibBalsaMailboxNotify  action,
								 gdouble		        fraction,
								 const gchar           *message,
								 ...)
{
	gchar *full_msg;

    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (message != NULL) {
    	va_list args;

    	va_start(args, message);
    	full_msg = g_strdup_vprintf(message, args);
    	va_end(args);
    } else {
    	full_msg = NULL;
    }
    g_signal_emit(mailbox, libbalsa_mailbox_signals[PROGRESS_NOTIFY], 0, (gint) action, fraction, full_msg);
	g_free(full_msg);
}

void
libbalsa_mailbox_check(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    libbalsa_lock_mailbox(mailbox);

    if (priv->queue_check_idle_id) {
	/* Remove scheduled idle callback. */
        g_source_remove(priv->queue_check_idle_id);
        priv->queue_check_idle_id = 0;
    }

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->check(mailbox);

    libbalsa_unlock_mailbox(mailbox);
}

static gboolean
lbm_changed_idle_cb(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_lock_mailbox(mailbox);
    g_signal_emit(mailbox, libbalsa_mailbox_signals[CHANGED], 0);
    priv->changed_idle_id = 0;
    libbalsa_unlock_mailbox(mailbox);

    return G_SOURCE_REMOVE;
}

static void
lbm_changed_schedule_idle(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_lock_mailbox(mailbox);
    if (!priv->changed_idle_id)
        priv->changed_idle_id =
            g_idle_add((GSourceFunc) lbm_changed_idle_cb, mailbox);
    libbalsa_unlock_mailbox(mailbox);
}

void
libbalsa_mailbox_changed(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_lock_mailbox(mailbox);
    if (!g_signal_has_handler_pending
        (mailbox, libbalsa_mailbox_signals[CHANGED], 0, TRUE)) {
        /* No one cares, so don't set any message counts--that might
         * cause priv->view to be created. */
        libbalsa_unlock_mailbox(mailbox);
        return;
    }

    if (MAILBOX_OPEN(mailbox)) {
        /* Both counts are valid. */
        libbalsa_mailbox_set_total(mailbox,
                                   libbalsa_mailbox_total_messages
                                   (mailbox));
        libbalsa_mailbox_set_unread(mailbox, priv->unread_messages);
    } else if (priv->has_unread_messages
               && libbalsa_mailbox_get_unread(mailbox) <= 0) {
        /* Mail has arrived in a closed mailbox since our last check;
         * total is unknown, but priv->has_unread_messages is valid. */
        libbalsa_mailbox_set_total(mailbox, -1);
        libbalsa_mailbox_set_unread(mailbox, 1);
    }

    lbm_changed_schedule_idle(mailbox);
    libbalsa_unlock_mailbox(mailbox);
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    gboolean match;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(msgno <= libbalsa_mailbox_total_messages(mailbox),
                         FALSE);

    if (libbalsa_condition_is_flag_only(search_iter->condition,
                                        mailbox, msgno, &match))
        return match;

    priv->must_cache_message = TRUE;
    match = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->message_match(mailbox, msgno, search_iter);
    priv->must_cache_message = FALSE;

    return match;
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

static gboolean
lbm_run_filters_on_reception_idle_cb(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GSList *filters;
    guint progress_count;
    GSList *lst;
    static LibBalsaCondition *recent_undeleted;
    gchar *text;
    guint total;
    guint progress_total;
    LibBalsaProgress progress;

    libbalsa_lock_mailbox(mailbox);

    if (priv->mindex == NULL) {
        libbalsa_unlock_mailbox(mailbox);
        /* Try again later */
        return TRUE;
    }

    priv->run_filters_idle_id = 0;

    if (!priv->filters_loaded) {
        config_mailbox_filters_load(mailbox);
        priv->filters_loaded = TRUE;
    }

    filters = libbalsa_mailbox_filters_when(priv->filters,
                                            FILTER_WHEN_INCOMING);

    if (filters == NULL) {
        libbalsa_unlock_mailbox(mailbox);
        return FALSE;
    }

    if (!filters_prepare_to_run(filters)) {
        g_slist_free(filters);
        libbalsa_unlock_mailbox(mailbox);
        return FALSE;
    }

    progress_count = 0;
    for (lst = filters; lst; lst = lst->next) {
        LibBalsaFilter *filter = lst->data;

        if (filter->condition
            && !libbalsa_condition_is_flag_only(filter->condition, NULL, 0,
                                                NULL))
            ++progress_count;
    }

    if (!recent_undeleted)
        recent_undeleted =
            libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                            libbalsa_condition_new_flag_enum
                                            (FALSE,
                                             LIBBALSA_MESSAGE_FLAG_RECENT),
                                            libbalsa_condition_new_flag_enum
                                            (TRUE,
                                             LIBBALSA_MESSAGE_FLAG_DELETED));

    text = g_strdup_printf(_("Applying filter rules to %s"), priv->name);
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
            if (use_progress) {
                libbalsa_progress_set_fraction(&progress,
                                               ((gdouble) ++progress_count)
                                               /
                                               ((gdouble) progress_total));
            }
        }

        libbalsa_mailbox_register_msgnos(mailbox, msgnos);
        libbalsa_filter_mailbox_messages(filter, mailbox, msgnos);
        libbalsa_mailbox_unregister_msgnos(mailbox, msgnos);

        g_array_free(msgnos, TRUE);

        libbalsa_mailbox_search_iter_unref(search_iter);
    }

    libbalsa_progress_set_text(&progress, NULL, 0);

    g_slist_free(filters);

    libbalsa_unlock_mailbox(mailbox);

    return FALSE;
}

void
libbalsa_mailbox_run_filters_on_reception(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (priv->run_filters_idle_id == 0) {
        priv->run_filters_idle_id =
            g_idle_add((GSourceFunc) lbm_run_filters_on_reception_idle_cb, mailbox);
    }
}

void
libbalsa_mailbox_save_config(LibBalsaMailbox * mailbox,
                             const gchar * group)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    /* These are incase this section was used for another
     * type of mailbox that has now been deleted...
     */
    g_free(priv->config_prefix);
    priv->config_prefix = g_strdup(group);
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
    libbalsa_message_set_mime_message(message, NULL);
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
    *stream = libbalsa_mailbox_get_message_stream(mailbox, msgno, TRUE);
    if(!*stream) {
	g_warning("Connection broken for message %u", msgno);
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxPrivate *dest_priv = libbalsa_mailbox_get_instance_private(dest);
    gchar *text;
    guint successfully_copied;
    struct MsgCopyData mcd;

    text = g_strdup_printf(_("Copying from %s to %s"), priv->name,
                           dest_priv->name);
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

static gint mailbox_compare_func(const SortTuple * a,
                              const SortTuple * b,
                              LibBalsaMailbox * mailbox);

static void
libbalsa_mailbox_real_sort(LibBalsaMailbox* mailbox, GArray *sort_array)
{
    /* Sort the array */
    g_array_sort_with_data(sort_array,
                           (GCompareDataFunc) mailbox_compare_func, mailbox);
}

static void
libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
                                  const gchar * group)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_conf_set_string("Type",
                            g_type_name(G_OBJECT_TYPE(mailbox)));
    libbalsa_conf_set_string("Name", priv->name);
}

static void
libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
                                  const gchar * group)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_free(priv->config_prefix);
    priv->config_prefix = g_strdup(group);

    g_free(priv->name);
    priv->name = libbalsa_conf_get_string("Name=Mailbox");
}

static gboolean
libbalsa_mailbox_real_close_backend(LibBalsaMailbox * mailbox)
{
    return TRUE;                /* Default is noop. */
}

static void
libbalsa_mailbox_real_lock_store(LibBalsaMailbox * mailbox, gboolean lock)
{
    /* Default is noop. */
}

GType
libbalsa_mailbox_type_from_path(const gchar * path)
/* libbalsa_get_mailbox_storage_type:
   returns one of LIBBALSA_TYPE_MAILBOX_IMAP,
   LIBBALSA_TYPE_MAILBOX_MAILDIR, LIBBALSA_TYPE_MAILBOX_MH,
   LIBBALSA_TYPE_MAILBOX_MBOX.  G_TYPE_OBJECT on error or a directory.
 */
{
    struct stat st;

    if(strncmp(path, "imap://", 7) == 0)
        return LIBBALSA_TYPE_MAILBOX_IMAP;

    if (stat (path, &st) == -1)
        return G_TYPE_OBJECT;
    
    if (S_ISDIR (st.st_mode)) {
        char tmp[_POSIX_PATH_MAX];

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
        /* Minimal check for an mailbox */
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
						      lmm, guint msgno);
/* Does the node (non-NULL) have unseen children? */
static gboolean
lbm_node_has_unseen_child(LibBalsaMailbox * lmm, GNode * node)
{
    for (node = node->children; node; node = node->next) {
	LibBalsaMailboxIndexEntry *entry =
	    /* g_ptr_array_index(priv->mindex, msgno - 1); ?? */
	    lbm_get_index_entry(lmm, GPOINTER_TO_UINT(node->data));
	if ((entry && entry->unseen) || lbm_node_has_unseen_child(lmm, node))
	    return TRUE;
    }
    return FALSE;
}

/* Protects access to priv->msgnos_changed; may be locked
 * with or without the gdk lock, so WE MUST NOT GRAB THE GDK LOCK WHILE
 * HOLDING IT. */

static GMutex msgnos_changed_lock;

static void lbm_update_msgnos(LibBalsaMailbox * mailbox, guint seqno,
                              GArray * msgnos);

static void
lbm_msgno_changed_expunged_cb(LibBalsaMailbox * mailbox, guint seqno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_mutex_lock(&msgnos_changed_lock);
    lbm_update_msgnos(mailbox, seqno, priv->msgnos_changed);
    g_mutex_unlock(&msgnos_changed_lock);
}


static void
lbm_msgno_row_changed(LibBalsaMailbox * mailbox, guint msgno,
                      GtkTreeIter * iter)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (!iter->user_data)
        iter->user_data =
            g_node_find(priv->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL,
                        GUINT_TO_POINTER(msgno));

    if (iter->user_data) {
        GtkTreePath *path;

        iter->stamp = priv->stamp;
        path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), iter);
        g_signal_emit(mailbox, libbalsa_mailbox_model_signals[ROW_CHANGED], 0,
                      path, iter);
        gtk_tree_path_free(path);
    }
}


static gboolean
lbm_msgnos_changed_idle_cb(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    guint i;

    if (!priv->msgnos_changed)
        return FALSE;

    g_debug("%s %s %d requested", __func__, priv->name,
            priv->msgnos_changed->len);

    g_mutex_lock(&msgnos_changed_lock);
    for (i = 0; i < priv->msgnos_changed->len; i++) {
        guint msgno = g_array_index(priv->msgnos_changed, guint, i);
        GtkTreeIter iter;

        if (!MAILBOX_OPEN(mailbox))
            break;

        g_debug("%s %s msgno %d", __func__, priv->name, msgno);
        g_mutex_unlock(&msgnos_changed_lock);
        iter.user_data = NULL;
        lbm_msgno_row_changed(mailbox, msgno, &iter);
        g_mutex_lock(&msgnos_changed_lock);
    }

    g_debug("%s %s %d processed", __func__, priv->name,
            priv->msgnos_changed->len);
    g_array_set_size(priv->msgnos_changed, 0);
    g_mutex_unlock(&msgnos_changed_lock);

    g_object_unref(mailbox);
    return FALSE;
}


static void
lbm_msgno_changed(LibBalsaMailbox * mailbox, guint seqno,
                  GtkTreeIter * iter)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (libbalsa_am_i_subthread()) {
        g_mutex_lock(&msgnos_changed_lock);
        if (!priv->msgnos_changed) {
            priv->msgnos_changed =
                g_array_new(FALSE, FALSE, sizeof(guint));
            g_signal_connect(mailbox, "message-expunged",
                             G_CALLBACK(lbm_msgno_changed_expunged_cb),
                             NULL);
        }
        if (priv->msgnos_changed->len == 0)
            g_idle_add((GSourceFunc) lbm_msgnos_changed_idle_cb,
                       g_object_ref(mailbox));

        g_array_append_val(priv->msgnos_changed, seqno);
        g_mutex_unlock(&msgnos_changed_lock);

        /* Not calling lbm_msgno_row_changed, so we must make sure
         * iter->user_data is set: */
        if (!iter->user_data)
            iter->user_data =
                g_node_find(priv->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL,
                            GUINT_TO_POINTER(seqno));
        return;
    }

    lbm_msgno_row_changed(mailbox, seqno, iter);
}

void
libbalsa_mailbox_msgno_changed(LibBalsaMailbox * mailbox, guint seqno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter iter;

    if (!priv->msg_tree) {
        return;
    }

    iter.user_data = NULL;
    lbm_msgno_changed(mailbox, seqno, &iter);

    /* Parents' style may need to be changed also. */
    while (iter.user_data) {
        GNode *parent = ((GNode *) iter.user_data)->parent;

        iter.user_data = parent;
        if (parent && (seqno = GPOINTER_TO_UINT(parent->data)) > 0)
            lbm_msgno_changed(mailbox, seqno, &iter);
    }
}

static gboolean
lbm_need_threading_idle_cb(LibBalsaMailbox *mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_lock_mailbox(mailbox);

    lbm_set_threading(mailbox);
    priv->need_threading_idle_id = 0;

    libbalsa_unlock_mailbox(mailbox);

    return FALSE;
}

void
libbalsa_mailbox_msgno_inserted(LibBalsaMailbox *mailbox, guint seqno,
                                GNode * parent, GNode ** sibling)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter iter;
    GtkTreePath *path;

    libbalsa_lock_mailbox(mailbox);

    if (priv->msg_tree == NULL) {
        libbalsa_unlock_mailbox(mailbox);
        return;
    }
#undef SANITY_CHECK
#ifdef SANITY_CHECK
    g_return_if_fail(!g_node_find(priv->msg_tree,
                                  G_PRE_ORDER, G_TRAVERSE_ALL,
                                  GUINT_TO_POINTER(seqno)));
#endif

    /* Insert node into the message tree before getting path. */
    iter.user_data = g_node_new(GUINT_TO_POINTER(seqno));
    iter.stamp = priv->stamp;
    *sibling = g_node_insert_after(parent, *sibling, iter.user_data);

    if (g_signal_has_handler_pending(mailbox,
                                     libbalsa_mailbox_model_signals
                                     [ROW_INSERTED], 0, FALSE)) {
        path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);
        g_signal_emit(mailbox, libbalsa_mailbox_model_signals[ROW_INSERTED],
                      0, path, &iter);
        gtk_tree_path_free(path);
    }

    if (priv->need_threading_idle_id == 0) {
        priv->need_threading_idle_id =
            g_idle_add((GSourceFunc) lbm_need_threading_idle_cb, mailbox);
    }

    priv->msg_tree_changed = TRUE;
    libbalsa_unlock_mailbox(mailbox);
}

static void
libbalsa_mailbox_msgno_filt_in(LibBalsaMailbox *mailbox, guint seqno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter iter;
    GtkTreePath *path;

    if (!priv->msg_tree) {
        return;
    }

    /* Insert node into the message tree before getting path. */
    iter.user_data = g_node_new(GUINT_TO_POINTER(seqno));
    iter.stamp = priv->stamp;
    g_node_prepend(priv->msg_tree, iter.user_data);

    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);
    g_signal_emit(mailbox, libbalsa_mailbox_model_signals[ROW_INSERTED], 0,
                  path, &iter);
    gtk_tree_path_free(path);

    priv->msg_tree_changed = TRUE;
    lbm_changed_schedule_idle(mailbox);
}

/*
 * libbalsa_mailbox_msgno_removed and helpers
 */
struct remove_data {LibBalsaMailbox *mailbox; unsigned seqno; GNode *node; };
static gboolean
decrease_post(GNode *node, gpointer data)
{
    struct remove_data *dt = (struct remove_data*)data;
    unsigned seqno = GPOINTER_TO_UINT(node->data);
    if(seqno == dt->seqno) 
        dt->node = node;
    else if(seqno>dt->seqno) {
        GtkTreeIter iter; 
        node->data = GUINT_TO_POINTER(seqno-1);
        iter.user_data = node;
        lbm_msgno_changed(dt->mailbox, seqno, &iter);
    }
    return FALSE;
}

void
libbalsa_mailbox_msgno_removed(LibBalsaMailbox * mailbox, guint seqno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter iter;
    GtkTreePath *path;
    struct remove_data dt;
    GNode *child;
    GNode *parent;

    g_signal_emit(mailbox, libbalsa_mailbox_signals[MESSAGE_EXPUNGED],
                  0, seqno);

    if (!priv->msg_tree) {
        return;
    }

    dt.mailbox = mailbox;
    dt.seqno = seqno;
    dt.node = NULL;

    g_node_traverse(priv->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    decrease_post, &dt);

    if (seqno <= priv->mindex->len)
        g_ptr_array_remove_index(priv->mindex, seqno - 1);

    priv->msg_tree_changed = TRUE;

    if (!dt.node) {
        /* It's ok, apparently the view did not include this message */
        return;
    }

    iter.user_data = dt.node;
    iter.stamp = priv->stamp;
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);

    /* First promote any children to the node's parent; we'll insert
     * them all before the current node, to keep the path calculation
     * simple. */
    parent = dt.node->parent;
    while ((child = dt.node->children)) {
        /* No need to notify the tree-view about unlinking the child--it
         * will assume we already did that when we notify it about
         * destroying the parent. */
        g_node_unlink(child);
        g_node_insert_before(parent, dt.node, child);

        /* Notify the tree-view about the new location of the child. */
        iter.user_data = child;
        g_signal_emit(mailbox, libbalsa_mailbox_model_signals[ROW_INSERTED], 0,
                      path, &iter);
        if (child->children)
            g_signal_emit(mailbox,
                          libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                          0, path, &iter);
        gtk_tree_path_next(path);
    }

    libbalsa_lock_mailbox(mailbox);
    if (priv->need_threading_idle_id == 0) {
        priv->need_threading_idle_id =
            g_idle_add((GSourceFunc) lbm_need_threading_idle_cb, mailbox);
    }
    libbalsa_unlock_mailbox(mailbox);

    /* Now it's safe to destroy the node. */
    g_node_destroy(dt.node);
    g_signal_emit(mailbox, libbalsa_mailbox_model_signals[ROW_DELETED], 0, path);

    if (parent->parent && !parent->children) {
        gtk_tree_path_up(path);
        iter.user_data = parent;
        g_signal_emit(mailbox,
                      libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED], 0,
                      path, &iter);
    }
    
    gtk_tree_path_free(path);
    priv->stamp++;
}

static void
libbalsa_mailbox_msgno_filt_out(LibBalsaMailbox * mailbox, GNode * node)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter iter;
    GtkTreePath *path;
    GNode *child, *parent;

    if (!priv->msg_tree) {
        return;
    }

    iter.user_data = node;
    iter.stamp = priv->stamp;
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);

    /* First promote any children to the node's parent; we'll insert
     * them all before the current node, to keep the path calculation
     * simple. */
    parent = node->parent;
    while ((child = node->children)) {
        /* No need to notify the tree-view about unlinking the child--it
         * will assume we already did that when we notify it about
         * destroying the parent. */
        g_node_unlink(child);
        g_node_insert_before(parent, node, child);

        /* Notify the tree-view about the new location of the child. */
        iter.user_data = child;
        g_signal_emit(mailbox, libbalsa_mailbox_model_signals[ROW_INSERTED], 0,
                      path, &iter);
        if (child->children)
            g_signal_emit(mailbox,
                          libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                          0, path, &iter);
        gtk_tree_path_next(path);
    }

    /* Now it's safe to destroy the node. */
    g_node_destroy(node);
    g_signal_emit(mailbox, libbalsa_mailbox_model_signals[ROW_DELETED], 0, path);

    if (parent->parent && !parent->children) {
        gtk_tree_path_up(path);
        iter.user_data = parent;
        g_signal_emit(mailbox,
                      libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED], 0,
                      path, &iter);
    }
    
    gtk_tree_path_free(path);
    priv->stamp++;

    priv->msg_tree_changed = TRUE;
    lbm_changed_schedule_idle(mailbox);
}

/*
 * Check whether to filter the message in or out of the view:
 * - if it's in the view and doesn't match the condition, filter it out,
 *   unless it's selected and we don't want to filter out selected
 *   messages;
 * - if it isn't in the view and it matches the condition, filter it in.
 */

static void
lbm_msgno_filt_check(LibBalsaMailbox * mailbox, guint seqno,
                     LibBalsaMailboxSearchIter * search_iter,
                     gboolean hold_selected)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    gboolean match;
    GNode *node;

    match = search_iter ?
        libbalsa_mailbox_message_match(mailbox, seqno, search_iter) : TRUE;
    node = g_node_find(priv->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL,
                       GUINT_TO_POINTER(seqno));
    if (node) {
        if (!match) {
            gboolean filt_out = hold_selected ?
                libbalsa_mailbox_msgno_has_flags(mailbox, seqno, 0,
                                                 LIBBALSA_MESSAGE_FLAG_SELECTED)
                : TRUE;
#if 1
	    /* a hack. The whole filtering idea is bit silly since we
	       keep checking flags (or maybe more!) on all messages so
	       that the time spent on changing the selection grows
	       linearly with the mailbox size!  */
	    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) &&
		!libbalsa_mailbox_imap_is_connected
		(LIBBALSA_MAILBOX_IMAP(mailbox)))
		filt_out = FALSE;
#endif
            if (filt_out)
                libbalsa_mailbox_msgno_filt_out(mailbox, node);
        }
    } else {
        if (match)
            libbalsa_mailbox_msgno_filt_in(mailbox, seqno);
    }
}

typedef struct {
    LibBalsaMailbox           *mailbox;
    guint                      seqno;
    LibBalsaMailboxSearchIter *search_iter;
    gboolean                   hold_selected;
} LibBalsaMailboxMsgnoFiltCheckInfo;

static gboolean
lbm_msgno_filt_check_idle_cb(LibBalsaMailboxMsgnoFiltCheckInfo * info)
{
    libbalsa_lock_mailbox(info->mailbox);
    if (MAILBOX_OPEN(info->mailbox))
        lbm_msgno_filt_check(info->mailbox, info->seqno, info->search_iter,
                             info->hold_selected);
    libbalsa_unlock_mailbox(info->mailbox);

    g_object_unref(info->mailbox);
    libbalsa_mailbox_search_iter_unref(info->search_iter);
    g_free(info);

    return FALSE;
}

void
libbalsa_mailbox_msgno_filt_check(LibBalsaMailbox * mailbox, guint seqno,
                                  LibBalsaMailboxSearchIter * search_iter,
                                  gboolean hold_selected)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (!priv->msg_tree) {
        return;
    }

    if (!libbalsa_am_i_subthread()) {
        lbm_msgno_filt_check(mailbox, seqno, search_iter, hold_selected);
    } else {
        LibBalsaMailboxMsgnoFiltCheckInfo *info;

        info = g_new(LibBalsaMailboxMsgnoFiltCheckInfo, 1);
        info->mailbox = g_object_ref(mailbox);
        info->seqno = seqno;
        info->search_iter = libbalsa_mailbox_search_iter_ref(search_iter);
        info->hold_selected = hold_selected;
        g_idle_add((GSourceFunc) lbm_msgno_filt_check_idle_cb, info);
    }
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
    iter->ref_count = 1;

    return iter;
}

/* Create a LibBalsaMailboxSearchIter for a mailbox's view_filter. */
LibBalsaMailboxSearchIter *
libbalsa_mailbox_search_iter_view(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return libbalsa_mailbox_search_iter_new(priv->view_filter);
}

/* Increment reference count of a LibBalsaMailboxSearchIter, if it is
 * valid */
LibBalsaMailboxSearchIter *
libbalsa_mailbox_search_iter_ref(LibBalsaMailboxSearchIter * search_iter)
{
    if (search_iter)
        ++search_iter->ref_count;

    return search_iter;
}

/* Decrement reference count of a LibBalsaMailboxSearchIter, if it is
 * non-NULL and valid, and free it if it goes to zero */
void
libbalsa_mailbox_search_iter_unref(LibBalsaMailboxSearchIter * search_iter)
{
    LibBalsaMailbox *mailbox;

    if (!search_iter || --search_iter->ref_count > 0)
        return;

    mailbox = search_iter->mailbox;
    if (mailbox && LIBBALSA_MAILBOX_GET_CLASS(mailbox)->search_iter_free)
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->search_iter_free(search_iter);

    libbalsa_condition_unref(search_iter->condition);
    g_free(search_iter);
}

/* GNode iterators; they return the root node when they run out of nodes,
 * and find the appropriate starting node when called with the root. */
static GNode *
lbm_next(GNode * node)
{
    /* next is:     our first child, if we have one;
     *              else our sibling, if we have one;
     *              else the sibling of our first ancestor who has
     *              one.  */
    if (node->children)
        return node->children;

    do {
        if (node->next)
            return node->next;
        node = node->parent;
        g_assert(node != NULL);
    } while (!G_NODE_IS_ROOT(node));

    return node;
}

static GNode *
lbm_last_descendant(GNode * node)
{
    if (node->children) {
        GNode *tmp;

        node = node->children;
        while ((tmp = node->next) || (tmp = node->children))
            node = tmp;
    }
    return node;
}

static GNode *
lbm_prev(GNode * node)
{
    if (G_NODE_IS_ROOT(node))
        return lbm_last_descendant(node);

    /* previous is: if we have a sibling,
     *                      if it has children, its last descendant;
     *                      else the sibling;
     *              else our parent. */
    if (node->prev)
        return lbm_last_descendant(node->prev);

    return node->parent;
}

/* Find a message in the tree-model, by its message number. */
gboolean
libbalsa_mailbox_msgno_find(LibBalsaMailbox * mailbox, guint seqno,
                            GtkTreePath ** path, GtkTreeIter * iter)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter tmp_iter;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(seqno > 0, FALSE);

    if (!priv->msg_tree || !(tmp_iter.user_data =
        g_node_find(priv->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL,
                    GINT_TO_POINTER(seqno))))
        return FALSE;

    tmp_iter.stamp = priv->stamp;

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
    if (amd->processed)
        return FALSE;
    amd->processed = TRUE;
    *flg = amd->flags;
 /* Make sure ::add_messages does not destroy the stream. */
    *stream = g_object_ref(amd->stream);
    return TRUE;
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    gboolean retval = TRUE;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(!priv->readonly, TRUE);

    libbalsa_lock_mailbox(mailbox);

    /* When called in an idle handler, the mailbox might have been
     * closed, so we must check (with the mailbox locked). */
    if (MAILBOX_OPEN(mailbox)) {
        retval =
            LIBBALSA_MAILBOX_GET_CLASS(mailbox)->sync(mailbox, expunge);
        libbalsa_mailbox_changed(mailbox);
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

static gboolean lbm_sort_idle_cb(LibBalsaMailbox * mailbox);

static void
lbm_cache_message(LibBalsaMailbox * mailbox, guint msgno,
                  LibBalsaMessage * message)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxIndexEntry *entry;
    gboolean need_sort;

    /* Do we need to cache the message info? */
    if (priv->mindex == NULL)
        return;

    if ((priv->view == NULL || priv->view->position < 0) && !priv->must_cache_message)
        return;

    if (priv->mindex->len < msgno)
        g_ptr_array_set_size(priv->mindex, msgno);

    entry = g_ptr_array_index(priv->mindex, msgno - 1);

    need_sort = TRUE;
    if (entry == NULL) {
        g_ptr_array_index(priv->mindex, msgno - 1) =
            entry = g_new(LibBalsaMailboxIndexEntry, 1);
        lbm_index_entry_populate_from_msg(entry, message);
    } else if (entry->idle_pending) {
        lbm_index_entry_populate_from_msg(entry, message);
    } else {
        need_sort = FALSE;
    }

    if (need_sort && priv->sort_idle_id == 0) {
        priv->sort_idle_id =
            g_idle_add_full(G_PRIORITY_LOW, (GSourceFunc) lbm_sort_idle_cb,
                            mailbox, NULL);
    }
}

LibBalsaMessage *
libbalsa_mailbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMessage *message;

    g_return_val_if_fail(mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    libbalsa_lock_mailbox(mailbox);

    if (!MAILBOX_OPEN(mailbox)) {
        g_debug("libbalsa_mailbox_get_message: mailbox %s is closed",
                  priv->name);
        libbalsa_unlock_mailbox(mailbox);
        return NULL;
    }

    if( !(msgno > 0 && msgno <= libbalsa_mailbox_total_messages(mailbox)) ) {
	libbalsa_unlock_mailbox(mailbox);
	g_warning("get_message: msgno %d out of range", msgno);
	return NULL;
    }

    message = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message(mailbox,
                                                               msgno);
    if (message != NULL)
        /* Cache the message info, if we do not already have it. */
        lbm_cache_message(mailbox, msgno, message);

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
    g_return_if_fail(mailbox == libbalsa_message_get_mailbox(message));

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->release_message(mailbox, message);
}

void
libbalsa_mailbox_set_msg_headers(LibBalsaMailbox *mailbox,
                                 LibBalsaMessage *message)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    if(!libbalsa_message_get_has_all_headers(message)) {
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->fetch_headers(mailbox, message);
        libbalsa_message_set_has_all_headers(message, TRUE);
    }
}

gboolean
libbalsa_mailbox_get_message_part(LibBalsaMessage    *message,
                                  LibBalsaMessageBody *part,
                                  GError **err)
{
    LibBalsaMailbox *mailbox;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    mailbox = libbalsa_message_get_mailbox(message);
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(part != NULL, FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message_part(message, part, err);
}

GMimeStream *
libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox, guint msgno,
				    gboolean peek)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(msgno <= libbalsa_mailbox_total_messages(mailbox),
                         NULL);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message_stream(mailbox,
                                                                   msgno,
								   peek);
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    gboolean retval;
    guint i;
    gboolean real_flag;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    real_flag = (set | clear) & LIBBALSA_MESSAGE_FLAGS_REAL;
    g_return_val_if_fail(!priv->readonly || !real_flag, FALSE);

    if (msgnos->len == 0)
	return TRUE;

    if (real_flag)
	libbalsa_lock_mailbox(mailbox);

    retval = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->
	messages_change_flags(mailbox, msgnos, set, clear);

    if (retval && priv->mindex && priv->view_filter) {
        LibBalsaMailboxSearchIter *iter_view =
            libbalsa_mailbox_search_iter_view(mailbox);
        for (i = 0; i < msgnos->len; i++) {
            guint msgno = g_array_index(msgnos, guint, i);
            libbalsa_mailbox_msgno_filt_check(mailbox, msgno, iter_view,
                                              TRUE);
        }
        libbalsa_mailbox_search_iter_unref(iter_view);
    }

    if (real_flag)
	libbalsa_unlock_mailbox(mailbox);

    if ((set & LIBBALSA_MESSAGE_FLAG_DELETED) && retval)
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
static gboolean
messages_copy_locked(LibBalsaMailbox *mailbox,
                     GArray          *msgnos,
                     LibBalsaMailbox *dest,
                     GError         **err)
{
    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->messages_copy(mailbox, msgnos, dest, err);
}

gboolean
libbalsa_mailbox_messages_copy(LibBalsaMailbox * mailbox, GArray * msgnos,
                               LibBalsaMailbox * dest, GError **err)
{
    gboolean retval;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(msgnos->len > 0, TRUE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(dest), FALSE);
    g_return_val_if_fail(dest != mailbox, FALSE);

    libbalsa_lock_mailbox(mailbox);
    libbalsa_lock_mailbox(dest);
    retval = messages_copy_locked(mailbox, msgnos, dest, err);
    libbalsa_unlock_mailbox(dest);
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
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(dest), FALSE);
    g_return_val_if_fail(dest != mailbox, FALSE);

    libbalsa_lock_mailbox(mailbox);
    libbalsa_lock_mailbox(dest);
    retval = messages_copy_locked(mailbox, msgnos, dest, err);
    if (retval) {
        retval = libbalsa_mailbox_messages_change_flags
            (mailbox, msgnos, LIBBALSA_MESSAGE_FLAG_DELETED,
             (LibBalsaMessageFlag) 0);
	if(!retval)
	    g_set_error(err,LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_COPY_ERROR,
			_("Removing messages from source mailbox failed"));
    }
    libbalsa_unlock_mailbox(dest);
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    gboolean retval = FALSE;

    libbalsa_lock_mailbox(mailbox);

    if (!libbalsa_condition_compare(priv->view_filter, cond))
        priv->view_filter_pending = TRUE;

    libbalsa_condition_unref(priv->view_filter);
    priv->view_filter = libbalsa_condition_ref(cond);

    if (update_immediately && priv->view_filter_pending) {
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->update_view_filter(mailbox,
                                                                cond);
        retval = lbm_set_threading(mailbox);
        priv->view_filter_pending = FALSE;
    }

    libbalsa_unlock_mailbox(mailbox);

    return retval;
}

void
libbalsa_mailbox_make_view_filter_persistent(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_condition_unref(priv->persistent_view_filter);
    priv->persistent_view_filter =
        libbalsa_condition_ref(priv->view_filter);
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
libbalsa_mailbox_real_can_do(LibBalsaMailbox* mailbox,
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


static void lbm_sort(LibBalsaMailbox * mailbox, GNode * parent);

static gboolean
lbm_sort_idle_cb(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_lock_mailbox(mailbox);

    if (!priv->messages_threaded) {
        libbalsa_unlock_mailbox(mailbox);
        return G_SOURCE_CONTINUE;
    }

    if (priv->msg_tree != NULL)
        lbm_sort(mailbox, priv->msg_tree);

    libbalsa_mailbox_changed(mailbox);

    priv->sort_idle_id = 0;
    libbalsa_unlock_mailbox(mailbox);

    return G_SOURCE_REMOVE;
}

static gboolean
lbm_set_threading(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (!MAILBOX_OPEN(mailbox))
        return FALSE;

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->set_threading(mailbox,
                                                       priv->view->threading_type);

    if (libbalsa_mailbox_total_messages(mailbox) > 0 && priv->sort_idle_id == 0)
        priv->sort_idle_id = g_idle_add((GSourceFunc) lbm_sort_idle_cb, mailbox);

    return TRUE;
}

void
libbalsa_mailbox_set_threading(LibBalsaMailbox *mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    libbalsa_lock_mailbox(mailbox);
    lbm_set_threading(mailbox);
    libbalsa_unlock_mailbox(mailbox);
}

/* =================================================================== *
 * Mailbox view methods                                                *
 * =================================================================== */

static LibBalsaMailboxView libbalsa_mailbox_view_default = {
    NULL,			/* identity_name        */
    LB_MAILBOX_THREADING_FLAT,	/* threading_type       */
    FALSE,                      /* subject_gather       */
    0,				/* filter               */
    LB_MAILBOX_SORT_TYPE_ASC,	/* sort_type            */
    LB_MAILBOX_SORT_NO,         /* sort_field           */
    LB_MAILBOX_SORT_NO,         /* sort_field_prev      */
    LB_MAILBOX_SHOW_UNSET,	/* show                 */
    LB_MAILBOX_SUBSCRIBE_UNSET,	/* subscribe            */
    0,				/* exposed              */
    0,				/* open                 */
    1,				/* in_sync              */
    0,				/* used 		*/
    LB_MAILBOX_CHK_CRYPT_MAYBE, /* gpg_chk_mode         */
    -1,                         /* total messages	*/
    -1,                         /* unread messages	*/
    0,                          /* mod time             */
    -1                          /* position             */
};

LibBalsaMailboxView *
libbalsa_mailbox_view_new(void)
{
    LibBalsaMailboxView *view;

    view = g_new(LibBalsaMailboxView, 1);
    *view = libbalsa_mailbox_view_default;

    return view;
}

void
libbalsa_mailbox_view_free(LibBalsaMailboxView * view)
{
    if (view == NULL)
        return;

    g_free(view->identity_name);
    g_free(view);
}

/* helper */
static LibBalsaMailboxView *
lbm_get_view(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (!mailbox)
	return &libbalsa_mailbox_view_default;

    if (!priv->view)
        priv->view = libbalsa_mailbox_view_new();

    return priv->view;
}

/* Set methods; NULL mailbox is valid, and changes the default value. */

gboolean
libbalsa_mailbox_set_identity_name(LibBalsaMailbox * mailbox,
				   const gchar * identity_name)
{
    LibBalsaMailboxView *view;

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    view = lbm_get_view(mailbox);

    if (g_strcmp0(view->identity_name, identity_name) != 0) {
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
                                    LibBalsaMailboxThreadingType threading_type)
{
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));
    view = lbm_get_view(mailbox);

    if (view->threading_type != threading_type) {
	view->threading_type = threading_type;
	if (mailbox != NULL) {
	    view->in_sync = 0;
            libbalsa_mailbox_set_threading(mailbox);
        }
    }
}

void
libbalsa_mailbox_set_subject_gather(LibBalsaMailbox * mailbox,
                                    gboolean          subject_gather)
{
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));
    view = lbm_get_view(mailbox);

    if (view->subject_gather != subject_gather) {
	view->subject_gather = subject_gather;
	if (mailbox != NULL)
	    view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_sort_type(LibBalsaMailbox * mailbox,
			    LibBalsaMailboxSortType sort_type)
{
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));
    view = lbm_get_view(mailbox);

    if (view->sort_type != sort_type) {
	view->sort_type = sort_type;
	if (mailbox)
	    view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_sort_field(LibBalsaMailbox * mailbox,
			     LibBalsaMailboxSortFields sort_field)
{
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));
    view = lbm_get_view(mailbox);

    if (view->sort_field != sort_field) {
	view->sort_field_prev = view->sort_field;
	view->sort_field = sort_field;
	if (mailbox)
	    view->in_sync = 0;
    }
}

gboolean
libbalsa_mailbox_set_show(LibBalsaMailbox * mailbox, LibBalsaMailboxShow show)
{
    LibBalsaMailboxView *view;

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    view = lbm_get_view(mailbox);

    if (view->show != show) {
	/* Don't set not in sync if we're just replacing UNSET with the
	 * default. */
	if (mailbox != NULL && view->show != LB_MAILBOX_SHOW_UNSET)
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
    LibBalsaMailboxView *view;

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    view = lbm_get_view(mailbox);

    if (view->subscribe != subscribe) {
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
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));
    view = lbm_get_view(mailbox);

    if (view->exposed != exposed) {
	view->exposed = exposed ? 1 : 0;
	if (mailbox)
	    view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_open(LibBalsaMailbox * mailbox, gboolean open)
{
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));
    view = lbm_get_view(mailbox);

    if (view->open != open) {
	view->open = open ? 1 : 0;
	if (mailbox != NULL) {
	    if (!open)
                view->position = -1;
	    view->in_sync = 0;
        }
    }
}

void
libbalsa_mailbox_set_filter(LibBalsaMailbox * mailbox, gint filter)
{
    LibBalsaMailboxView *view;

    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));
    view = lbm_get_view(mailbox);

    if (view->filter != filter) {
	view->filter = filter;
	if (mailbox)
	    view->in_sync = 0;
    }
}

gboolean 
libbalsa_mailbox_set_crypto_mode(LibBalsaMailbox * mailbox,
                                LibBalsaChkCryptoMode gpg_chk_mode)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxView *view;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(priv->view != NULL, FALSE);

    view = priv->view;
    if (view->gpg_chk_mode != gpg_chk_mode) {
	view->gpg_chk_mode = gpg_chk_mode;
	return TRUE;
    } else {
	return FALSE;
    }
}

void
libbalsa_mailbox_set_unread(LibBalsaMailbox * mailbox, gint unread)
{
    LibBalsaMailboxView *view;

    /* Changing the default is not allowed. */
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    view = lbm_get_view(mailbox);
    view->used = 1;

    if (view->unread != unread) {
	view->unread = unread;
        view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_total(LibBalsaMailbox * mailbox, gint total)
{
    LibBalsaMailboxView *view;

    /* Changing the default is not allowed. */
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    view = lbm_get_view(mailbox);

    if (view->total != total) {
	view->total = total;
        view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_mtime(LibBalsaMailbox * mailbox, time_t mtime)
{
    LibBalsaMailboxView *view;

    /* Changing the default is not allowed. */
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    view = lbm_get_view(mailbox);

    if (view->mtime != mtime) {
	view->mtime = mtime;
        view->in_sync = 0;
    }
}

void
libbalsa_mailbox_set_position(LibBalsaMailbox * mailbox, gint position)
{
    LibBalsaMailboxView *view;

    /* Changing the default is not allowed. */
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    view = lbm_get_view(mailbox);
    view->used = 1;

    if (view->position != position) {
        view->position = position;
        view->in_sync = 0;
    }
}

/* End of set methods. */

/* Get methods; NULL mailbox is valid, and returns the default value. */

const gchar *
libbalsa_mailbox_get_identity_name(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->identity_name :
	libbalsa_mailbox_view_default.identity_name;
}


LibBalsaMailboxThreadingType
libbalsa_mailbox_get_threading_type(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->threading_type :
	libbalsa_mailbox_view_default.threading_type;
}

gboolean
libbalsa_mailbox_get_subject_gather(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return (mailbox != NULL && priv->view != NULL) ?
        priv->view->subject_gather :
        libbalsa_mailbox_view_default.subject_gather;
}


LibBalsaMailboxSortType
libbalsa_mailbox_get_sort_type(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->sort_type : libbalsa_mailbox_view_default.sort_type;
}

LibBalsaMailboxSortFields
libbalsa_mailbox_get_sort_field(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->sort_field :
	libbalsa_mailbox_view_default.sort_field;
}

LibBalsaMailboxShow
libbalsa_mailbox_get_show(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->show : libbalsa_mailbox_view_default.show;
}

LibBalsaMailboxSubscribe
libbalsa_mailbox_get_subscribe(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->subscribe : libbalsa_mailbox_view_default.subscribe;
}

gboolean
libbalsa_mailbox_get_exposed(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->exposed : libbalsa_mailbox_view_default.exposed;
}

gboolean
libbalsa_mailbox_get_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->open : libbalsa_mailbox_view_default.open;
}

gint
libbalsa_mailbox_get_filter(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->filter : libbalsa_mailbox_view_default.filter;
}

LibBalsaChkCryptoMode
libbalsa_mailbox_get_crypto_mode(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->gpg_chk_mode :
	libbalsa_mailbox_view_default.gpg_chk_mode;
}

gint
libbalsa_mailbox_get_unread(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    if (mailbox != NULL && priv->view != NULL) {
        priv->view->used = 1;
	return priv->view->unread;
    } else {
        return libbalsa_mailbox_view_default.unread;
    }
}

gint
libbalsa_mailbox_get_total(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->total : libbalsa_mailbox_view_default.total;
}

time_t
libbalsa_mailbox_get_mtime(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
	priv->view->mtime : libbalsa_mailbox_view_default.mtime;
}

gint
libbalsa_mailbox_get_position(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox), 0);

    return (mailbox != NULL && priv->view != NULL) ?
        priv->view->position : libbalsa_mailbox_view_default.position;
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
     ((LibBalsaMailboxPrivate *) libbalsa_mailbox_get_instance_private(((LibBalsaMailbox *) tree_model)))->stamp == (iter)->stamp)
#define VALIDATE_ITER(iter, tree_model) \
    ((iter)->stamp = ((LibBalsaMailboxPrivate *) libbalsa_mailbox_get_instance_private((LibBalsaMailbox *) tree_model))->stamp)
#define INVALIDATE_ITER(iter) ((iter)->stamp = 0)

static GtkTreeModelFlags mailbox_model_get_flags  (GtkTreeModel      *tree_model);
static gint         mailbox_model_get_n_columns   (GtkTreeModel      *tree_model);
static GType        mailbox_model_get_column_type (GtkTreeModel      *tree_model,
                                                gint               index);
static gboolean     mailbox_model_get_iter        (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreePath       *path);
static GtkTreePath *mailbox_model_get_path        (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static void         mailbox_model_get_value       (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                gint               column,
                                                GValue            *value);
static gboolean     mailbox_model_iter_next       (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static gboolean     mailbox_model_iter_children   (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreeIter       *parent);
static gboolean     mailbox_model_iter_has_child  (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static gint         mailbox_model_iter_n_children (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter);
static gboolean     mailbox_model_iter_nth_child  (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreeIter       *parent,
                                                gint               n);
static gboolean     mailbox_model_iter_parent     (GtkTreeModel      *tree_model,
                                                GtkTreeIter       *iter,
                                                GtkTreeIter       *child);


static GType mailbox_model_col_type[LB_MBOX_N_COLS];

static void
mailbox_model_init(GtkTreeModelIface *iface)
{
    iface->get_flags       = mailbox_model_get_flags;
    iface->get_n_columns   = mailbox_model_get_n_columns;
    iface->get_column_type = mailbox_model_get_column_type;
    iface->get_iter        = mailbox_model_get_iter;
    iface->get_path        = mailbox_model_get_path;
    iface->get_value       = mailbox_model_get_value;
    iface->iter_next       = mailbox_model_iter_next;
    iface->iter_children   = mailbox_model_iter_children;
    iface->iter_has_child  = mailbox_model_iter_has_child;
    iface->iter_n_children = mailbox_model_iter_n_children;
    iface->iter_nth_child  = mailbox_model_iter_nth_child;
    iface->iter_parent     = mailbox_model_iter_parent;

    mailbox_model_col_type[LB_MBOX_MSGNO_COL]   = G_TYPE_UINT;
    mailbox_model_col_type[LB_MBOX_MARKED_COL]  = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_ATTACH_COL]  = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_FROM_COL]    = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_SUBJECT_COL] = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_DATE_COL]    = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_SIZE_COL]    = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_WEIGHT_COL]  = G_TYPE_INT;
    mailbox_model_col_type[LB_MBOX_STYLE_COL]   = PANGO_TYPE_STYLE;
    mailbox_model_col_type[LB_MBOX_FOREGROUND_COL]     = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_FOREGROUND_SET_COL] = G_TYPE_BOOLEAN;
    mailbox_model_col_type[LB_MBOX_BACKGROUND_COL]     = G_TYPE_STRING;
    mailbox_model_col_type[LB_MBOX_BACKGROUND_SET_COL] = G_TYPE_BOOLEAN;


    libbalsa_mailbox_model_signals[ROW_CHANGED] =
        g_signal_lookup("row-changed",           GTK_TYPE_TREE_MODEL);
    libbalsa_mailbox_model_signals[ROW_DELETED] =
        g_signal_lookup("row-deleted",           GTK_TYPE_TREE_MODEL);
    libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED] =
        g_signal_lookup("row-has-child-toggled", GTK_TYPE_TREE_MODEL);
    libbalsa_mailbox_model_signals[ROW_INSERTED] =
        g_signal_lookup("row-inserted",          GTK_TYPE_TREE_MODEL);
    libbalsa_mailbox_model_signals[ROWS_REORDERED] =
        g_signal_lookup("rows-reordered",        GTK_TYPE_TREE_MODEL);
}

static GtkTreeModelFlags
mailbox_model_get_flags(GtkTreeModel *tree_model)
{
    return 0;
}

static gint
mailbox_model_get_n_columns(GtkTreeModel *tree_model)
{
    return LB_MBOX_N_COLS;
}

static GType
mailbox_model_get_column_type(GtkTreeModel *tree_model, gint index)
{
    g_return_val_if_fail(index>=0 && index <LB_MBOX_N_COLS, G_TYPE_BOOLEAN);
    return mailbox_model_col_type[index];
}

static gboolean
mailbox_model_get_iter(GtkTreeModel *tree_model,
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

    if (!mailbox_model_iter_nth_child(tree_model, iter, NULL, indices[0]))
        return FALSE;

    for (i = 1; i < depth; i++) {
        parent = *iter;
        if (!mailbox_model_iter_nth_child(tree_model, iter, &parent,
                                       indices[i]))
            return FALSE;
    }

    return TRUE;
}

static GtkTreePath *
mailbox_model_get_path_helper(GNode * node, GNode * msg_tree)
{
    GtkTreePath *path = gtk_tree_path_new();

    while (node->parent) {
	gint i = g_node_child_position(node->parent, node);
	if (i < 0) {
	    gtk_tree_path_free(path);
	    return NULL;
	}
	gtk_tree_path_prepend_index(path, i);
	node = node->parent;
    }

    if (node == msg_tree)
	return path;
    gtk_tree_path_free(path);
    return NULL;
}

static GtkTreePath *
mailbox_model_get_path(GtkTreeModel * tree_model, GtkTreeIter * iter)
{
    LibBalsaMailbox *mailbox = (LibBalsaMailbox *) tree_model;
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GNode *node;
#ifdef SANITY_CHECK
    GNode *parent_node;
#endif

    g_return_val_if_fail(VALID_ITER(iter, tree_model), NULL);

    node = iter->user_data;
#ifdef SANITY_CHECK
    for (parent_node = node->parent; parent_node;
         parent_node = parent_node->parent)
        g_return_val_if_fail(parent_node != node, NULL);
#endif

    g_return_val_if_fail(node->parent != NULL, NULL);

    return mailbox_model_get_path_helper(node, priv->msg_tree);
}

/* mailbox_model_get_value: 
  FIXME: still includes some debugging code in case fetching the
  message failed.
*/

static const char *status_icons[LIBBALSA_MESSAGE_STATUS_ICONS_NUM];
static const char *attach_icons[LIBBALSA_MESSAGE_ATTACH_ICONS_NUM];


/* Protects access to priv->msgnos_pending; */
static GMutex get_index_entry_lock;

static void
lbm_get_index_entry_expunged_cb(LibBalsaMailbox * mailbox, guint seqno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_mutex_lock(&get_index_entry_lock);
    lbm_update_msgnos(mailbox, seqno, priv->msgnos_pending);
    g_mutex_unlock(&get_index_entry_lock);
}

static void
lbm_get_index_entry_real(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    guint i;

    g_debug("%s %s %d requested", __func__, priv->name,
            priv->msgnos_pending->len);
    g_mutex_lock(&get_index_entry_lock);
    for (i = 0; i < priv->msgnos_pending->len; i++) {
        guint msgno = g_array_index(priv->msgnos_pending, guint, i);
        LibBalsaMessage *message;

        if (!MAILBOX_OPEN(mailbox))
            break;

        g_debug("%s %s msgno %d", __func__, priv->name, msgno);
        g_mutex_unlock(&get_index_entry_lock);
        if ((message = libbalsa_mailbox_get_message(mailbox, msgno)))
            /* get-message has cached the message info, so we just unref
             * message. */
            g_object_unref(message);
        g_mutex_lock(&get_index_entry_lock);
    }

    g_debug("%s %s %d processed", __func__, priv->name,
            priv->msgnos_pending->len);
    g_array_set_size(priv->msgnos_pending, 0);
    g_mutex_unlock(&get_index_entry_lock);

    g_object_unref(mailbox);
}


static LibBalsaMailboxIndexEntry *
lbm_get_index_entry(LibBalsaMailbox * lmm, guint msgno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(lmm);
    LibBalsaMailboxIndexEntry *entry;

    if (!priv->mindex)
        return NULL;

    if (priv->mindex->len < msgno )
        g_ptr_array_set_size(priv->mindex, msgno);

    entry = g_ptr_array_index(priv->mindex, msgno - 1);
    if (entry)
        return entry->idle_pending ? NULL : entry;

    g_mutex_lock(&get_index_entry_lock);
    if (!priv->msgnos_pending) {
        priv->msgnos_pending = g_array_new(FALSE, FALSE, sizeof(guint));
        g_signal_connect(lmm, "message-expunged",
                         G_CALLBACK(lbm_get_index_entry_expunged_cb), NULL);
    }

    if (!priv->msgnos_pending->len) {
        GThread *get_index_entry_thread;

        g_object_ref(lmm);
        get_index_entry_thread =
        	g_thread_new("lbm_get_index_entry_real",
        				 (GThreadFunc) lbm_get_index_entry_real,
						 lmm);
        g_thread_unref(get_index_entry_thread);
    }

    g_array_append_val(priv->msgnos_pending, msgno);
    /* Make sure we have a "pending" index entry before releasing the
     * lock. */
    g_ptr_array_index(priv->mindex, msgno - 1) =
        lbm_index_entry_new_pending();
    g_mutex_unlock(&get_index_entry_lock);

    return entry;
}

gchar **libbalsa_mailbox_date_format;
static void
mailbox_model_get_value(GtkTreeModel *tree_model,
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
                     column < (int) G_N_ELEMENTS(mailbox_model_col_type));
 
    g_value_init (value, mailbox_model_col_type[column]);
    msgno = GPOINTER_TO_UINT( ((GNode*)iter->user_data)->data );

    if(column == LB_MBOX_MSGNO_COL) {
        g_value_set_uint(value, msgno);
        return;
    }
    g_return_if_fail(msgno<=libbalsa_mailbox_total_messages(lmm));
    msg = lbm_get_index_entry(lmm, msgno);
    switch(column) {
        /* case LB_MBOX_MSGNO_COL: handled above */
    case LB_MBOX_MARKED_COL:
        if (msg && msg->status_icon < LIBBALSA_MESSAGE_STATUS_ICONS_NUM)
            g_value_set_static_string(value, status_icons[msg->status_icon]);
        break;
    case LB_MBOX_ATTACH_COL:
        if (msg && msg->attach_icon < LIBBALSA_MESSAGE_ATTACH_ICONS_NUM)
            g_value_set_static_string(value, attach_icons[msg->attach_icon]);
        break;
    case LB_MBOX_FROM_COL:
	if(msg) {
            if (msg->from)
                g_value_set_string(value, msg->from);
            else
                g_value_set_static_string(value, _("from unknown"));
        } else
            g_value_set_static_string(value, _("Loading…"));
        break;
    case LB_MBOX_SUBJECT_COL:
        if(msg) g_value_set_string(value, msg->subject);
        break;
    case LB_MBOX_DATE_COL:
        if(msg) {
            tmp = libbalsa_date_to_utf8(msg->msg_date,
		                        *libbalsa_mailbox_date_format);
            g_value_take_string(value, tmp);
        }
        break;
    case LB_MBOX_SIZE_COL:
        if(msg) {
            if (msg->size != -1) {
                tmp = libbalsa_size_to_gchar(msg->size);
                g_value_take_string(value, tmp);
            } else {
                g_value_set_static_string(value, "?");
            }
        }
        else g_value_set_static_string(value, "          ");
        break;
    case LB_MBOX_WEIGHT_COL:
        g_value_set_int(value, msg != NULL && msg->unseen
                         ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
        break;
    case LB_MBOX_STYLE_COL:
        g_value_set_enum(value, msg != NULL &&
			 lbm_node_has_unseen_child(lmm,
						   (GNode *) iter->user_data)
                         ? PANGO_STYLE_OBLIQUE : PANGO_STYLE_NORMAL);
        break;
    case LB_MBOX_FOREGROUND_COL:
        if(msg) {
            tmp = g_strdup(msg->foreground);
            g_value_take_string(value, tmp);
        }
        break;
    case LB_MBOX_FOREGROUND_SET_COL:
        g_value_set_boolean(value, msg != NULL && msg->foreground_set);
        break;
    case LB_MBOX_BACKGROUND_COL:
        if(msg) {
            tmp = g_strdup(msg->background);
            g_value_take_string(value, tmp);
        }
        break;
    case LB_MBOX_BACKGROUND_SET_COL:
        g_value_set_boolean(value, msg != NULL && msg->background_set);
        break;
    }
}

static gboolean
mailbox_model_iter_next(GtkTreeModel      *tree_model,
                     GtkTreeIter       *iter)
{
    GNode *node;
    g_return_val_if_fail(VALID_ITER(iter, tree_model), FALSE);

    node = iter->user_data;
    if(node && (node = node->next)) {
        iter->user_data = node;
        VALIDATE_ITER(iter, tree_model);
        return TRUE;
    } else {
        INVALIDATE_ITER(iter);
        return FALSE;
    }
}

static gboolean
mailbox_model_iter_children(GtkTreeModel      *tree_model,
                         GtkTreeIter       *iter,
                         GtkTreeIter       *parent)
{
    LibBalsaMailboxPrivate *priv =
        libbalsa_mailbox_get_instance_private(((LibBalsaMailbox *) tree_model));
    GNode *node;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(parent == NULL ||
                         VALID_ITER(parent, tree_model), FALSE);

    node = parent ? parent->user_data : priv->msg_tree;
    node = node->children;
    if (node) {
        iter->user_data = node;
        VALIDATE_ITER(iter, tree_model);
        return TRUE;
    } else
        return FALSE;
}

static gboolean
mailbox_model_iter_has_child(GtkTreeModel  * tree_model,
                          GtkTreeIter   * iter)
{
    GNode *node;

    g_return_val_if_fail(VALID_ITER(iter, LIBBALSA_MAILBOX(tree_model)),
                         FALSE);

    node = iter->user_data;

    return (node->children != NULL);
}

static gint
mailbox_model_iter_n_children(GtkTreeModel      *tree_model,
                           GtkTreeIter       *iter)
{
    LibBalsaMailboxPrivate *priv =
        libbalsa_mailbox_get_instance_private(((LibBalsaMailbox *) tree_model));
    GNode *node;

    g_return_val_if_fail(iter == NULL || VALID_ITER(iter, tree_model), 0);

    node = iter ? iter->user_data : priv->msg_tree;

    return node ? g_node_n_children(node) : 0;
}

static gboolean
mailbox_model_iter_nth_child(GtkTreeModel  * tree_model,
                          GtkTreeIter   * iter,
                          GtkTreeIter   * parent,
                          gint            n)
{
    LibBalsaMailboxPrivate *priv =
        libbalsa_mailbox_get_instance_private(((LibBalsaMailbox *) tree_model));
    GNode *node;

    INVALIDATE_ITER(iter);
    if(!priv->msg_tree) 
        return FALSE; /* really, this should be never called when
                       * msg_tree == NULL but the FALSE response is
                       * fair as well and only a bit dirtier.
                       * I have more critical problems to debug now. */
    g_return_val_if_fail(parent == NULL
                         || VALID_ITER(parent, tree_model), FALSE);

    node = parent ? parent->user_data : priv->msg_tree;
    if(!node) /* the tree has been destroyed already (mailbox has been
               * closed), there is nothing to iterate over. This happens
               * only if mailbox is closed but a view is still active. 
               */
        return FALSE;
    node = g_node_nth_child(node, n);

    if (node) {
        iter->user_data = node;
        VALIDATE_ITER(iter, tree_model);
        return TRUE;
    } else
        return FALSE;
}

static gboolean
mailbox_model_iter_parent(GtkTreeModel     * tree_model,
                       GtkTreeIter      * iter,
                       GtkTreeIter      * child)
{
    LibBalsaMailboxPrivate *priv =
        libbalsa_mailbox_get_instance_private(((LibBalsaMailbox *) tree_model));
    GNode *node;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(iter != NULL, FALSE);
    g_return_val_if_fail(VALID_ITER(child, tree_model), FALSE);

    node = child->user_data;
    node = node->parent;
    if (node && node != priv->msg_tree) {
        iter->user_data = node;
        VALIDATE_ITER(iter, tree_model);
        return TRUE;
    } else
        return FALSE;
}

/* Icons for status column. */
void
libbalsa_mailbox_set_unread_icon(const char *name)
{
    status_icons[LIBBALSA_MESSAGE_STATUS_UNREAD] = name;
}

void libbalsa_mailbox_set_trash_icon(const char *name)
{
    status_icons[LIBBALSA_MESSAGE_STATUS_DELETED] = name;
}

void libbalsa_mailbox_set_flagged_icon(const char *name)
{
    status_icons[LIBBALSA_MESSAGE_STATUS_FLAGGED] = name;
}

void libbalsa_mailbox_set_replied_icon(const char *name)
{
    status_icons[LIBBALSA_MESSAGE_STATUS_REPLIED] = name;
}

/* Icons for attachment column. */
void libbalsa_mailbox_set_attach_icon(const char *name)
{
    attach_icons[LIBBALSA_MESSAGE_ATTACH_ATTACH] = name;
}

void libbalsa_mailbox_set_good_icon(const char *name)
{
    attach_icons[LIBBALSA_MESSAGE_ATTACH_GOOD] = name;
}

void libbalsa_mailbox_set_notrust_icon(const char *name)
{
    attach_icons[LIBBALSA_MESSAGE_ATTACH_NOTRUST] = name;
}

void libbalsa_mailbox_set_bad_icon(const char *name)
{
    attach_icons[LIBBALSA_MESSAGE_ATTACH_BAD] = name;
}

void libbalsa_mailbox_set_sign_icon(const char *name)
{
    attach_icons[LIBBALSA_MESSAGE_ATTACH_SIGN] = name;
}

void libbalsa_mailbox_set_encr_icon(const char *name)
{
    attach_icons[LIBBALSA_MESSAGE_ATTACH_ENCR] = name;
}

/* =================================================================== *
 * GtkTreeDragSource implementation functions.                         *
 * =================================================================== */

static gboolean mailbox_row_draggable(GtkTreeDragSource * drag_source,
                                   GtkTreePath * path);
static gboolean mailbox_drag_data_delete(GtkTreeDragSource * drag_source,
                                      GtkTreePath * path);
static gboolean mailbox_drag_data_get(GtkTreeDragSource * drag_source,
                                   GtkTreePath * path,
                                   GtkSelectionData * selection_data);

static void
mailbox_drag_source_init(GtkTreeDragSourceIface * iface)
{
    iface->row_draggable    = mailbox_row_draggable;
    iface->drag_data_delete = mailbox_drag_data_delete;
    iface->drag_data_get    = mailbox_drag_data_get;
}

/* These three methods are apparently never called, so what they return
 * is irrelevant.  The code reflects guesses about what they should
 * return if they were ever called.
 */
static gboolean
mailbox_row_draggable(GtkTreeDragSource * drag_source, GtkTreePath * path)
{
    /* All rows are valid sources. */
    return TRUE;
}

static gboolean
mailbox_drag_data_delete(GtkTreeDragSource * drag_source, GtkTreePath * path)
{
    /* The "drag-data-received" callback handles deleting messages that
     * are dragged out of the mailbox, so we don't. */
    return FALSE;
}

static gboolean
mailbox_drag_data_get(GtkTreeDragSource * drag_source, GtkTreePath * path,
                   GtkSelectionData * selection_data)
{
    /* The "drag-data-get" callback passes the list of selected messages
     * to the GtkSelectionData, so we don't. */
    return FALSE;
}

/* =================================================================== *
 * GtkTreeSortable implementation functions.                           *
 * =================================================================== */

static gboolean mailbox_get_sort_column_id(GtkTreeSortable * sortable,
                                        gint * sort_column_id,
                                        GtkSortType * order);
static void mailbox_set_sort_column_id(GtkTreeSortable * sortable,
                                    gint sort_column_id,
                                    GtkSortType order);
static void mailbox_set_sort_func(GtkTreeSortable * sortable,
                               gint sort_column_id,
                               GtkTreeIterCompareFunc func, gpointer data,
                               GDestroyNotify destroy);
static void mailbox_set_default_sort_func(GtkTreeSortable * sortable,
                                       GtkTreeIterCompareFunc func,
                                       gpointer data,
                                       GDestroyNotify destroy);
static gboolean mailbox_has_default_sort_func(GtkTreeSortable * sortable);

static void
mailbox_sortable_init(GtkTreeSortableIface * iface)
{
    iface->get_sort_column_id    = mailbox_get_sort_column_id;
    iface->set_sort_column_id    = mailbox_set_sort_column_id;
    iface->set_sort_func         = mailbox_set_sort_func;
    iface->set_default_sort_func = mailbox_set_default_sort_func;
    iface->has_default_sort_func = mailbox_has_default_sort_func;
}

static gint
mailbox_compare_from(LibBalsaMailboxIndexEntry * message_a,
                  LibBalsaMailboxIndexEntry * message_b)
{
    return g_ascii_strcasecmp(message_a->from, message_b->from);
}

static gint
mailbox_compare_subject(LibBalsaMailboxIndexEntry * message_a,
                     LibBalsaMailboxIndexEntry * message_b)
{
    return g_ascii_strcasecmp(message_a->subject, message_b->subject);
}

static gint
mailbox_compare_date(LibBalsaMailboxIndexEntry * message_a,
                  LibBalsaMailboxIndexEntry * message_b)
{
    return message_a->msg_date - message_b->msg_date;
}

/* Thread date stuff */

typedef struct {
    time_t                  thread_date;
    LibBalsaMailboxPrivate *priv;
} LibBalsaMailboxThreadDateInfo;

static gboolean
mailbox_get_thread_date_traverse_func(GNode   *node,
                                      gpointer data)
{
    LibBalsaMailboxThreadDateInfo *info = data;
    guint msgno;
    LibBalsaMailboxIndexEntry *message;

    if ((msgno = GPOINTER_TO_UINT(node->data)) > 0 &&
        msgno <= info->priv->mindex->len &&
        (message = g_ptr_array_index(info->priv->mindex, msgno - 1)) != NULL) {
        time_t msg_date = message->msg_date;

        if (msg_date > info->thread_date)
            info->thread_date = msg_date;

        return FALSE;
    }

    /* Either a bad msgno or a NULL message: stop the traversal before
     * something bad happens. */
    return TRUE;
}

static time_t
mailbox_get_thread_date(const SortTuple *tuple,
                     LibBalsaMailbox *mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (tuple->thread_date == 0) {
        LibBalsaMailboxThreadDateInfo info = { 0, priv };

        g_node_traverse(tuple->node, G_IN_ORDER, G_TRAVERSE_ALL, -1,
                        mailbox_get_thread_date_traverse_func, &info);

        /* Cast away the 'const' qualifier so that we can cache the
         * thread date: */
        ((SortTuple *) tuple)->thread_date = info.thread_date;
    }

    return tuple->thread_date;
}

static gint
mailbox_compare_thread_date(const SortTuple *a,
                         const SortTuple *b,
                         LibBalsaMailbox *mailbox)
{
    return mailbox_get_thread_date(a, mailbox) - mailbox_get_thread_date(b, mailbox);
}

/* End of thread date stuff */

static gint
mailbox_compare_size(LibBalsaMailboxIndexEntry * message_a,
                  LibBalsaMailboxIndexEntry * message_b)
{
    return message_a->size - message_b->size;
}

static gint
mailbox_compare_func(const SortTuple * a,
                  const SortTuple * b,
                  LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    guint msgno_a;
    guint msgno_b;
    gint retval;

    msgno_a = GPOINTER_TO_UINT(a->node->data);
    msgno_b = GPOINTER_TO_UINT(b->node->data);
    if (priv->view->sort_field == LB_MAILBOX_SORT_NO)
	retval = msgno_a - msgno_b;
    else {
	LibBalsaMailboxIndexEntry *message_a;
	LibBalsaMailboxIndexEntry *message_b;

	message_a = LBM_GET_INDEX_ENTRY(priv, msgno_a);
	message_b = LBM_GET_INDEX_ENTRY(priv, msgno_b);

	if (!(VALID_ENTRY(message_a) && VALID_ENTRY(message_b)))
	    return 0;

	switch (priv->view->sort_field) {
	case LB_MAILBOX_SORT_SENDER:
	    retval = mailbox_compare_from(message_a, message_b);
	    break;
	case LB_MAILBOX_SORT_SUBJECT:
	    retval = mailbox_compare_subject(message_a, message_b);
	    break;
	case LB_MAILBOX_SORT_DATE:
            retval =
                priv->view->threading_type == LB_MAILBOX_THREADING_FLAT
                ? mailbox_compare_date(message_a, message_b)
                : mailbox_compare_thread_date(a, b, mailbox);
	    break;
	case LB_MAILBOX_SORT_SIZE:
	    retval = mailbox_compare_size(message_a, message_b);
	    break;
	default:
	    retval = 0;
	    break;
	}

        if (retval == 0) {
            /* resolve ties using previous sort column */
            switch (priv->view->sort_field_prev) {
            case LB_MAILBOX_SORT_SENDER:
                retval = mailbox_compare_from(message_a, message_b);
                break;
            case LB_MAILBOX_SORT_SUBJECT:
                retval = mailbox_compare_subject(message_a, message_b);
                break;
	    case LB_MAILBOX_SORT_DATE:
	        retval =
                    priv->view->threading_type == LB_MAILBOX_THREADING_FLAT
                    ? mailbox_compare_date(message_a, message_b)
                    : mailbox_compare_thread_date(a, b, mailbox);
	        break;
            case LB_MAILBOX_SORT_SIZE:
                retval = mailbox_compare_size(message_a, message_b);
                break;
            default:
                retval = 0;
                break;
            }
        }
    }

    if (priv->view->sort_type == LB_MAILBOX_SORT_TYPE_DESC) {
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxIndexEntry *entry;

    if (msgno > priv->mindex->len)
        return FALSE;

    entry = g_ptr_array_index(priv->mindex, msgno - 1);

    return VALID_ENTRY(entry);
}

static void
lbm_sort(LibBalsaMailbox * mailbox, GNode * parent)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GArray *sort_array;
    GPtrArray *node_array;
    GNode *node, *tmp_node, *prev;
    guint i, j;
    gboolean sort_no = priv->view->sort_field == LB_MAILBOX_SORT_NO;
#if !defined(LOCAL_MAILBOX_SORTED_JUST_ONCE_ON_OPENING)
    gboolean can_sort_all = sort_no || LIBBALSA_IS_MAILBOX_IMAP(mailbox);
#else
    gboolean can_sort_all = 1;
#endif
    node = parent->children;
    if (!node)
        return;

    if (node->next == NULL) {
        lbm_sort(mailbox, node);
        return;
    }

    sort_array = g_array_new(FALSE, FALSE, sizeof(SortTuple));
    node_array = g_ptr_array_new();
    for (tmp_node = node; tmp_node; tmp_node = tmp_node->next) {
        SortTuple sort_tuple;
        guint msgno = GPOINTER_TO_UINT(tmp_node->data);

	if (can_sort_all || lbm_has_valid_index_entry(mailbox, msgno)) {
            /* We have the sort fields. */
            sort_tuple.offset = node_array->len;
            sort_tuple.node = tmp_node;
            sort_tuple.thread_date = 0;
            g_array_append_val(sort_array, sort_tuple);
        }
        g_ptr_array_add(node_array, tmp_node);
    }

    if (sort_array->len <= 1) {
        g_array_free(sort_array, TRUE);
        g_ptr_array_free(node_array, TRUE);
        lbm_sort(mailbox, node);
        return;
    }
    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->sort(mailbox, sort_array);

    /* Step through the nodes in original order. */
    prev = NULL;
    for (i = j = 0; i < node_array->len; i++) {
        guint msgno;

        tmp_node = g_ptr_array_index(node_array, i);
        msgno = GPOINTER_TO_UINT(tmp_node->data);
	if (can_sort_all || lbm_has_valid_index_entry(mailbox, msgno)) {
            /* This is one of the nodes we sorted: find out which one
             * goes here. */
            g_assert(j < sort_array->len);
            tmp_node = g_array_index(sort_array, SortTuple, j++).node;
        }
        if (tmp_node->prev != prev) {
            /* Change the links. */
            if (prev)
                prev->next = tmp_node;
            else
                node = parent->children = tmp_node;
            tmp_node->prev = prev;
            priv->msg_tree_changed = TRUE;
        } else
            g_assert(prev == NULL || prev->next == tmp_node);
        prev = tmp_node;
    }
    if (prev != NULL)
        prev->next = NULL;

    /* Let the world know about our new order */
    if (node_array->len > 0) {
        gint *new_order;
        GtkTreeIter iter;
        GtkTreePath *path;

        new_order = g_new(gint, node_array->len);
        i = j = 0;
        for (tmp_node = node; tmp_node; tmp_node = tmp_node->next) {
            guint msgno = GPOINTER_TO_UINT(tmp_node->data);
            new_order[j] = can_sort_all || lbm_has_valid_index_entry(mailbox, msgno)
                ? g_array_index(sort_array, SortTuple, i++).offset
                : j;
            j++;
        }

        iter.stamp = priv->stamp;
        iter.user_data = parent;
        path = parent->parent ? mailbox_model_get_path(GTK_TREE_MODEL(mailbox), &iter)
                              : gtk_tree_path_new();
        gtk_tree_model_rows_reordered(GTK_TREE_MODEL(mailbox),
                                      path, &iter, new_order);
        gtk_tree_path_free(path);
        g_free(new_order);
    }

    g_array_free(sort_array, TRUE);
    g_ptr_array_free(node_array, TRUE);

    for (tmp_node = node; tmp_node; tmp_node = tmp_node->next)
        lbm_sort(mailbox, tmp_node);
}

/* called from gtk-tree-view-column */
static gboolean
mailbox_get_sort_column_id(GtkTreeSortable * sortable,
                        gint            * sort_column_id,
                        GtkSortType     * order)
{
    LibBalsaMailbox *mailbox = (LibBalsaMailbox *) sortable;
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(sortable), FALSE);

    if (sort_column_id) {
        switch (priv->view->sort_field) {
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
        *order = (priv->view->sort_type ==
                  LB_MAILBOX_SORT_TYPE_DESC ? GTK_SORT_DESCENDING :
                  GTK_SORT_ASCENDING);

    return TRUE;
}

/* called from gtk-tree-view-column */
static void
mailbox_set_sort_column_id(GtkTreeSortable * sortable,
                        gint              sort_column_id,
                        GtkSortType       order)
{
    LibBalsaMailbox *mailbox = (LibBalsaMailbox *) sortable;
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxView *view;
    LibBalsaMailboxSortFields new_field;
    LibBalsaMailboxSortType new_type;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(sortable));

    view = priv->view;

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

    if (view->sort_field != new_field) {
        view->sort_field_prev = view->sort_field;
        view->sort_field = new_field;
    }
    view->sort_type = new_type;
    view->in_sync = 0;

    gtk_tree_sortable_sort_column_changed(sortable);

    if (new_field != LB_MAILBOX_SORT_NO) {
        gboolean rc;

        rc = libbalsa_mailbox_prepare_threading(mailbox, 0);

        if (!rc)
            /* Prepare-threading failed--perhaps mailbox was closed. */
            return;
    }
    libbalsa_lock_mailbox(mailbox);
    lbm_sort(mailbox, priv->msg_tree);
    libbalsa_unlock_mailbox(mailbox);

    libbalsa_mailbox_changed(mailbox);
}

static void
mailbox_set_sort_func(GtkTreeSortable * sortable,
                   gint sort_column_id,
                   GtkTreeIterCompareFunc func,
                   gpointer data, GDestroyNotify destroy)
{
    g_warning("%s called but not implemented.", __func__);
}

static void
mailbox_set_default_sort_func(GtkTreeSortable * sortable,
                           GtkTreeIterCompareFunc func,
                           gpointer data, GDestroyNotify destroy)
{
    g_warning("%s called but not implemented.", __func__);
}

/* called from gtk-tree-view-column */
static gboolean
mailbox_has_default_sort_func(GtkTreeSortable * sortable)
{
    return FALSE;
}

/* Helpers for set-threading-type. */
void
libbalsa_mailbox_unlink_and_prepend(LibBalsaMailbox * mailbox,
                                    GNode * node, GNode * parent)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter iter;
    GtkTreePath *path;
    GNode *current_parent;

    g_return_if_fail(node != NULL);
    g_return_if_fail(parent != node);
#ifdef SANITY_CHECK
    g_return_if_fail(!parent || !g_node_is_ancestor(node, parent));
#endif

    iter.stamp = priv->stamp;

    path = mailbox_model_get_path_helper(node, priv->msg_tree);
    current_parent = node->parent;
    g_node_unlink(node);
    if (path) {
        /* The node was in priv->msg_tree. */
        g_signal_emit(mailbox,
                      libbalsa_mailbox_model_signals[ROW_DELETED], 0, path);
        if (!current_parent->children) {
            /* It was the last child. */
            gtk_tree_path_up(path);
            iter.user_data = current_parent;
            g_signal_emit(mailbox,
                          libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                          0, path, &iter);
        }
        gtk_tree_path_free(path);
    }

    if (!parent) {
        g_node_destroy(node);
        return;
    }

    g_node_prepend(parent, node);
    path = mailbox_model_get_path_helper(parent, priv->msg_tree);
    if (path) {
        /* The parent is in priv->msg_tree. */
        if (!node->next) {
            /* It is the first child. */
            iter.user_data = parent;
            g_signal_emit(mailbox,
                          libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                          0, path, &iter);
        }
        gtk_tree_path_down(path);
        iter.user_data = node;
        g_signal_emit(mailbox,
                      libbalsa_mailbox_model_signals[ROW_INSERTED], 0,
                      path, &iter);
        if (node->children)
            g_signal_emit(mailbox,
                          libbalsa_mailbox_model_signals[ROW_HAS_CHILD_TOGGLED],
                          0, path, &iter);
        gtk_tree_path_free(path);

        priv->msg_tree_changed = TRUE;
    }
}

struct lbm_update_msg_tree_info {
    LibBalsaMailbox *mailbox;
    GNode *new_tree;
    GNode **nodes;
    guint total;
};

/* GNodeTraverseFunc for making a reverse lookup array into a tree. */
static gboolean
lbm_update_msg_tree_populate(GNode * node, 
                             struct lbm_update_msg_tree_info *mti)
{
    guint msgno;

    msgno = GPOINTER_TO_UINT(node->data);

    if (msgno < mti->total)
        mti->nodes[msgno] = node;

    return FALSE;
}

/* GNodeTraverseFunc for pruning the current tree; mti->nodes is a
 * reverse lookup array into the new tree, so a NULL value is a node
 * that doesn't appear in the new tree. */
static gboolean
lbm_update_msg_tree_prune(GNode * node,
                          struct lbm_update_msg_tree_info *mti)
{
    guint msgno;

    msgno = GPOINTER_TO_UINT(node->data);
    if (msgno >= mti->total || !mti->nodes[msgno]) {
        /* It's a bottom-up traverse, so the node's remaining children
         * are all in the new tree; we'll promote them to be children of
         * the node's parent, which might even be where they finish up. */
        while (node->children)
            libbalsa_mailbox_unlink_and_prepend(mti->mailbox,
                                                node->children,
                                                node->parent);
        /* Now we can destroy the node. */
        libbalsa_mailbox_unlink_and_prepend(mti->mailbox, node, NULL);
    }

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
    guint msgno;
    GNode *node;
    GNode *parent;

    if (!new_node->parent)
        return FALSE;

    msgno = GPOINTER_TO_UINT(new_node->data);
    if (msgno >= mti->total)
        return FALSE;

    node = mti->nodes[msgno];
    if (!node)
        mti->nodes[msgno] = node = g_node_new(new_node->data);

    msgno = GPOINTER_TO_UINT(new_node->parent->data);
    if (msgno >= mti->total)
        return FALSE;

    parent = mti->nodes[msgno];

    if (parent && parent != node->parent)
        libbalsa_mailbox_unlink_and_prepend(mti->mailbox, node, parent);

    return FALSE;
}

static void
lbm_update_msg_tree(LibBalsaMailbox * mailbox, GNode * new_tree)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    struct lbm_update_msg_tree_info mti;

    mti.mailbox = mailbox;
    mti.new_tree = new_tree;
    mti.total = 1 + libbalsa_mailbox_total_messages(mailbox);
    mti.nodes = g_new0(GNode *, mti.total);

    /* Populate the nodes array with nodes in the new tree. */
    g_node_traverse(new_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) lbm_update_msg_tree_populate, &mti);
    /* Remove deadwood. */
    g_node_traverse(priv->msg_tree, G_POST_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) lbm_update_msg_tree_prune, &mti);

    /* Clear the nodes array and repopulate it with nodes in the current
     * tree. */
    memset(mti.nodes, 0, sizeof(GNode *) * mti.total);
    g_node_traverse(priv->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) lbm_update_msg_tree_populate, &mti);
    /* Check parent-child relationships. */
    g_node_traverse(new_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) lbm_update_msg_tree_move, &mti);

    g_free(mti.nodes);
}

static void
lbm_set_msg_tree(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GtkTreeIter iter;
    GNode *node;
    GtkTreePath *path;

    iter.stamp = ++priv->stamp;

    if (!priv->msg_tree)
        return;

    path = gtk_tree_path_new();
    gtk_tree_path_down(path);

    for (node = priv->msg_tree->children; node; node = node->next) {
        iter.user_data = node;
        g_signal_emit(mailbox,
                      libbalsa_mailbox_model_signals[ROW_INSERTED], 0, path,
                      &iter);
        if (node->children)
            g_signal_emit(mailbox,
                          libbalsa_mailbox_model_signals
                          [ROW_HAS_CHILD_TOGGLED], 0, path, &iter);
        gtk_tree_path_next(path);
    }

    gtk_tree_path_free(path);
}

void
libbalsa_mailbox_set_msg_tree(LibBalsaMailbox * mailbox, GNode * new_tree)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (priv->msg_tree && priv->msg_tree->children) {
        lbm_update_msg_tree(mailbox, new_tree);
        g_node_destroy(new_tree);
    } else {
        if (priv->msg_tree)
            g_node_destroy(priv->msg_tree);
        priv->msg_tree = new_tree;
        lbm_set_msg_tree(mailbox);
    }

    priv->msg_tree_changed = TRUE;
}

static GMimeMessage *
lbm_get_mime_msg(LibBalsaMailbox * mailbox, LibBalsaMessage *message)
{
    GMimeMessage *mime_msg;

    mime_msg = libbalsa_message_get_mime_message(message);
    if (mime_msg == NULL) {
        libbalsa_mailbox_fetch_message_structure(mailbox, message,
                                                 LB_FETCH_RFC822_HEADERS);
    }
    mime_msg = libbalsa_message_get_mime_message(message);

    if (mime_msg != NULL) {
        g_object_ref(mime_msg);
    } else {
        GMimeStream *stream;
        GMimeParser *parser;

        stream = libbalsa_mailbox_get_message_stream(mailbox,
                                                     libbalsa_message_get_msgno(message),
						     TRUE);
        parser = g_mime_parser_new_with_stream(stream);
        g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);
        g_object_unref(stream);
        mime_msg = g_mime_parser_construct_message(parser, libbalsa_parser_options());
        g_object_unref(parser);
    }
    libbalsa_mailbox_release_message(mailbox, message);

    return mime_msg;
}

/* Try to reassemble messages of type message/partial with the given id;
 * if successful, delete the parts, so we don't keep creating the whole
 * message. */

static void
lbm_try_reassemble_func(GMimeObject * parent, GMimeObject * mime_part,
                        gpointer data)
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    gchar *text;
    guint total_messages;
    LibBalsaProgress progress;
    guint msgno;
    GPtrArray *partials = g_ptr_array_new();
    guint total = G_MAXUINT;
    GArray *messages = g_array_new(FALSE, FALSE, sizeof(guint));

    text = g_strdup_printf(_("Searching %s for partial messages"),
                           priv->name);
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
                                     lbm_try_reassemble_func, &partial);
            if (g_strcmp0(g_mime_message_partial_get_id(partial), id) == 0) {
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
        GMimeMessage *mime_message;
        LibBalsaMessage *message = libbalsa_message_new();
        libbalsa_message_set_flags(message, LIBBALSA_MESSAGE_FLAG_NEW);

        libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                             _("Reconstructing message"));
        libbalsa_mailbox_lock_store(mailbox);

        mime_message =
            g_mime_message_partial_reconstruct_message((GMimeMessagePartial
                                                        **) partials->
                                                       pdata, total);
        libbalsa_message_set_mime_message(message, mime_message);
        g_object_unref(mime_message);

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

static gboolean
lbm_try_reassemble_idle(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GSList *id;

    /* Make sure the thread that detected a message/partial has
     * completed. */
    libbalsa_lock_mailbox(mailbox);

    for (id = priv->reassemble_ids; id != NULL; id = id->next)
        lbm_try_reassemble(mailbox, id->data);

    g_slist_free_full(priv->reassemble_ids, g_free);
    priv->reassemble_ids = NULL;

    libbalsa_unlock_mailbox(mailbox);

    g_object_unref(mailbox);

    return FALSE;
}

void
libbalsa_mailbox_try_reassemble(LibBalsaMailbox * mailbox,
                                const gchar * id)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    if (priv->no_reassemble)
        return;

    if (priv->reassemble_ids == NULL)
        g_idle_add((GSourceFunc) lbm_try_reassemble_idle, g_object_ref(mailbox));

    if (g_slist_find_custom(priv->reassemble_ids, id, (GCompareFunc) strcmp) == NULL)
        priv->reassemble_ids = g_slist_prepend(priv->reassemble_ids, g_strdup(id));
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
    g_array_set_size(msgnos, j);
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxIndexEntry *entry = LBM_GET_INDEX_ENTRY(priv, msgno);

    return VALID_ENTRY(entry) ?
        entry->status_icon : LIBBALSA_MESSAGE_STATUS_ICONS_NUM;
}

const gchar *
libbalsa_mailbox_msgno_get_subject(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxIndexEntry *entry = LBM_GET_INDEX_ENTRY(priv, msgno);

    return VALID_ENTRY(entry) ? entry->subject : NULL;
}

/* Update icons, but only if entry has been allocated. */
void
libbalsa_mailbox_msgno_update_attach(LibBalsaMailbox * mailbox,
				     guint msgno, LibBalsaMessage * message)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    LibBalsaMailboxIndexEntry *entry;
    LibBalsaMessageAttach attach_icon;
    const gchar *subject;
    gboolean fix_subject;

    if (!mailbox || !priv->mindex || priv->mindex->len < msgno)
	return;

    entry = g_ptr_array_index(priv->mindex, msgno - 1);
    if (!VALID_ENTRY(entry))
	return;

    attach_icon = libbalsa_message_get_attach_icon(message);
    subject = libbalsa_message_get_subject(message);
    fix_subject = (g_strcmp0(entry->subject, subject) != 0);
    if ((entry->attach_icon != attach_icon) || fix_subject) {
        GtkTreeIter iter;

	entry->attach_icon = attach_icon;
	if (fix_subject) {
		g_free(entry->subject);
		entry->subject = g_strdup(subject);
	}
        iter.user_data = NULL;
	lbm_msgno_changed(mailbox, msgno, &iter);
    }
}

/* Queued check. */

static void
lbm_check_real(LibBalsaMailbox * mailbox)
{
    libbalsa_lock_mailbox(mailbox);
    libbalsa_mailbox_check(mailbox);
    libbalsa_unlock_mailbox(mailbox);
    g_object_unref(mailbox);
}

static gboolean
lbm_check_idle(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GThread *check_thread;

    check_thread = g_thread_new("lbm_check_real", (GThreadFunc) lbm_check_real,
                                g_object_ref(mailbox));
    g_thread_unref(check_thread);

    libbalsa_lock_mailbox(mailbox);
    priv->queue_check_idle_id = 0;
    libbalsa_unlock_mailbox(mailbox);

    return FALSE;
}

static void
lbm_queue_check(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    libbalsa_lock_mailbox(mailbox);
    if (!priv->queue_check_idle_id)
        priv->queue_check_idle_id =
            g_idle_add((GSourceFunc) lbm_check_idle, mailbox);
    libbalsa_unlock_mailbox(mailbox);
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
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GNode *node;
    gboolean retval = FALSE;
    gint total;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    node = iter->user_data;
    if (!node)
        node = priv->msg_tree;

    total = libbalsa_mailbox_total_messages(mailbox);
    for (;;) {
        guint msgno;

        node = forward ? lbm_next(node) : lbm_prev(node);
        msgno = GPOINTER_TO_UINT(node->data);
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
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->duplicate_msgnos != NULL;
}

gint
libbalsa_mailbox_move_duplicates(LibBalsaMailbox * mailbox,
                                 LibBalsaMailbox * dest, GError ** err)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    GArray *msgnos = NULL;
    gint retval;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    if (libbalsa_mailbox_can_move_duplicates(mailbox))
        msgnos =
            LIBBALSA_MAILBOX_GET_CLASS(mailbox)->duplicate_msgnos(mailbox);

    if (priv->state == LB_MAILBOX_STATE_CLOSED) {
        /* duplicate msgnos was interrupted */
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_DUPLICATES_ERROR,
                    _("Finding duplicate messages in source mailbox failed"));
        return 0;
    }

    if (!msgnos)
        return 0;

    if (msgnos->len > 0) {
        if (dest && mailbox != dest)
            libbalsa_mailbox_messages_move(mailbox, msgnos, dest, err);
        else
            libbalsa_mailbox_messages_change_flags(mailbox, msgnos,
                                                   LIBBALSA_MESSAGE_FLAG_DELETED,
                                                   0);
    }
    retval = msgnos->len;
    g_array_free(msgnos, TRUE);
    return retval;
}

/* Lock and unlock the mail store. NULL mailbox is not an error. */
void
libbalsa_mailbox_lock_store(LibBalsaMailbox * mailbox)
{
    if (LIBBALSA_IS_MAILBOX(mailbox))
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->lock_store(mailbox, TRUE);
}

void
libbalsa_mailbox_unlock_store(LibBalsaMailbox * mailbox)
{
    if (LIBBALSA_IS_MAILBOX(mailbox))
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->lock_store(mailbox, FALSE);
}

static void
libbalsa_mailbox_real_cache_message(LibBalsaMailbox * mailbox, guint msgno,
                                    LibBalsaMessage * message)
{
    lbm_cache_message(mailbox, msgno, message);
}

void
libbalsa_mailbox_cache_message(LibBalsaMailbox * mailbox, guint msgno,
                               LibBalsaMessage * message)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(msgno > 0);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->cache_message(mailbox, msgno, message);
}

static void
lbm_set_color(LibBalsaMailbox * mailbox, GArray * msgnos,
              const gchar * color, gboolean foreground)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);
    guint i;

    for (i = 0; i < msgnos->len; i++) {
        guint msgno = g_array_index(msgnos, guint, i);
        LibBalsaMailboxIndexEntry *entry;

        if (msgno > priv->mindex->len)
            return;

        entry = g_ptr_array_index(priv->mindex, msgno - 1);
        if (!entry)
            entry = g_ptr_array_index(priv->mindex, msgno - 1) =
                g_new0(LibBalsaMailboxIndexEntry, 1);

        if (foreground) {
            g_free(entry->foreground);
            entry->foreground = g_strdup(color);
            entry->foreground_set = TRUE;
        } else {
            g_free(entry->background);
            entry->background = g_strdup(color);
            entry->background_set = TRUE;
        }
    }
}

void
libbalsa_mailbox_set_foreground(LibBalsaMailbox * mailbox, GArray * msgnos,
                                const gchar * color)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    lbm_set_color(mailbox, msgnos, color, TRUE);
}

void
libbalsa_mailbox_set_background(LibBalsaMailbox * mailbox, GArray * msgnos,
                                const gchar * color)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    lbm_set_color(mailbox, msgnos, color, FALSE);
}

void libbalsa_mailbox_test_can_reach(LibBalsaMailbox          * mailbox,
                                     LibBalsaCanReachCallback * cb,
                                     gpointer                   cb_data)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->test_can_reach(mailbox, cb, cb_data);
}

/*
 * Getters
 */

GSList *
libbalsa_mailbox_get_filters(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return priv->filters;
}

const gchar *
libbalsa_mailbox_get_name(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return priv->name;
}

const gchar *
libbalsa_mailbox_get_url(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return priv->url;
}

glong
libbalsa_mailbox_get_unread_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    return priv->unread_messages;
}

guint
libbalsa_mailbox_get_first_unread(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    return priv->first_unread;
}

LibBalsaCondition *
libbalsa_mailbox_get_view_filter(LibBalsaMailbox * mailbox,
                                 gboolean persistent)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return persistent ?  priv->persistent_view_filter : priv->view_filter;
}

GNode *
libbalsa_mailbox_get_msg_tree(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return priv->msg_tree;
}

gboolean
libbalsa_mailbox_get_msg_tree_changed(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return priv->msg_tree_changed != 0;
}

LibBalsaMailboxState
libbalsa_mailbox_get_state(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    return priv->state;
}

LibBalsaMailboxIndexEntry *
libbalsa_mailbox_get_index_entry(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return LBM_GET_INDEX_ENTRY(priv, msgno);
}

LibBalsaMailboxView *
libbalsa_mailbox_get_view(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return priv->view;
}

gint
libbalsa_mailbox_get_stamp(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    return priv->stamp;
}

guint
libbalsa_mailbox_get_open_ref(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    return priv->open_ref;
}

gboolean
libbalsa_mailbox_get_readonly(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return priv->readonly != 0;
}

const gchar *
libbalsa_mailbox_get_config_prefix(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    return priv->config_prefix;
}

gboolean
libbalsa_mailbox_get_has_unread_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return priv->has_unread_messages != 0;
}

gboolean
libbalsa_mailbox_get_messages_threaded(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return priv->messages_threaded != 0;
}

/* Not exactly a getter: */
gboolean
libbalsa_mailbox_has_sort_pending(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return priv->sort_idle_id != 0;
}

/*
 * Setters
 */

void
libbalsa_mailbox_clear_unread_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->unread_messages = 0;
}

void
libbalsa_mailbox_set_filters(LibBalsaMailbox * mailbox, GSList * filters)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_slist_free_full(priv->filters, g_free);
    priv->filters = filters;
}

void
libbalsa_mailbox_set_url(LibBalsaMailbox * mailbox, const gchar * url)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_free(priv->url);
    priv->url = g_strdup(url);
}

void
libbalsa_mailbox_set_first_unread(LibBalsaMailbox * mailbox, guint first)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (priv->first_unread == 0 || priv->first_unread > first)
        priv->first_unread = first;
}

void
libbalsa_mailbox_set_msg_tree_changed(LibBalsaMailbox * mailbox, gboolean changed)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->msg_tree_changed = !!changed;
}

void
libbalsa_mailbox_set_readonly(LibBalsaMailbox * mailbox, gboolean readonly)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->readonly = !!readonly;
}

void
libbalsa_mailbox_set_no_reassemble(LibBalsaMailbox * mailbox, gboolean no_reassemble)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->no_reassemble = !!no_reassemble;
}

void
libbalsa_mailbox_set_name(LibBalsaMailbox * mailbox, const gchar * name)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_free(priv->name);
    priv->name = g_strdup(name);
}

void
libbalsa_mailbox_set_view(LibBalsaMailbox * mailbox, LibBalsaMailboxView * view)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    libbalsa_mailbox_view_free(priv->view);
    priv->view = view;
}

void
libbalsa_mailbox_set_has_unread_messages(LibBalsaMailbox * mailbox,
                                         gboolean has_unread_messages)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->has_unread_messages = !!has_unread_messages;
}

void
libbalsa_mailbox_set_messages_threaded(LibBalsaMailbox * mailbox,
                                       gboolean messages_threaded)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->messages_threaded = !!messages_threaded;
}

void
libbalsa_mailbox_set_config_prefix(LibBalsaMailbox * mailbox,
                                   const gchar * config_prefix)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_free(priv->config_prefix);
    priv->config_prefix = g_strdup(config_prefix);
}

/*
 * Incrementers
 */

void
libbalsa_mailbox_add_to_unread_messages(LibBalsaMailbox * mailbox, glong count)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    priv->unread_messages += count;
}

/*
 * Locking
 */

void
libbalsa_lock_mailbox(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_rec_mutex_lock(&priv->rec_mutex);
}

void
libbalsa_unlock_mailbox(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPrivate *priv = libbalsa_mailbox_get_instance_private(mailbox);

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_rec_mutex_unlock(&priv->rec_mutex);
}
