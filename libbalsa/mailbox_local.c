/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "libbalsa-conf.h"
#include "filter-funcs.h"
#include "mailbox-filter.h"
#include "misc.h"
#include <glib/gi18n.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mbox-local"

typedef struct _LibBalsaMailboxLocalPrivate LibBalsaMailboxLocalPrivate;
struct _LibBalsaMailboxLocalPrivate {
    guint sync_id;  /* id of the idle mailbox sync job  */
    guint sync_time; /* estimated time of sync job execution (in seconds),
                      * used to  throttle frequency of large mbox syncing. */
    guint sync_cnt; /* we do not want to rely on the time of last sync since
                     * some sync can be faster than others. Instead, we
                     * average the syncing time for mailbox. */
    guint save_tree_id; /* id of the idle mailbox save-tree job */
    guint load_messages_id; /* id of the idle load-messages job */
    guint set_threading_id; /* id of the idle set-threading job */
    GPtrArray *threading_info;
    LibBalsaMailboxLocalPool message_pool[LBML_POOL_SIZE];
    guint pool_seqno;
    gboolean messages_loaded;
};

static void libbalsa_mailbox_local_finalize(GObject * object);

static void libbalsa_mailbox_local_changed(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);
static void libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);

static void libbalsa_mailbox_local_close_mailbox(LibBalsaMailbox * mailbox,
                                                 gboolean expunge);
static LibBalsaMessage *libbalsa_mailbox_local_get_message(LibBalsaMailbox
                                                           * mailbox,
                                                           guint msgno);
static gboolean libbalsa_mailbox_local_message_match(LibBalsaMailbox *
						     mailbox, guint msgno,
						     LibBalsaMailboxSearchIter
						     * iter);

static void libbalsa_mailbox_local_set_threading(LibBalsaMailbox *mailbox,
						 LibBalsaMailboxThreadingType
						 thread_type);
static void lbm_local_update_view_filter(LibBalsaMailbox * mailbox,
                                         LibBalsaCondition *view_filter);

static gboolean libbalsa_mailbox_local_prepare_threading(LibBalsaMailbox *
                                                         mailbox,
                                                         guint start);

static gboolean libbalsa_mailbox_local_fetch_structure(LibBalsaMailbox *
                                                       mailbox,
                                                       LibBalsaMessage *
                                                       message,
                                                       LibBalsaFetchFlag
                                                       flags);
static void libbalsa_mailbox_local_fetch_headers(LibBalsaMailbox *mailbox,
                                                 LibBalsaMessage *message);

static gboolean libbalsa_mailbox_local_get_msg_part(LibBalsaMessage *msg,
						    LibBalsaMessageBody *,
                                                    GError **err);

static void lbm_local_sort(LibBalsaMailbox * mailbox, GArray *sort_array);
static guint
libbalsa_mailbox_local_add_messages(LibBalsaMailbox          * mailbox,
                                    LibBalsaAddMessageIterator msg_iterator,
                                    gpointer                   iter_data,
                                    GError                  ** err);

static GArray *libbalsa_mailbox_local_duplicate_msgnos(LibBalsaMailbox *
                                                       mailbox);
static gboolean
libbalsa_mailbox_local_messages_change_flags(LibBalsaMailbox * mailbox,
                                             GArray * msgnos,
                                             LibBalsaMessageFlag set,
                                             LibBalsaMessageFlag clear);
static gboolean libbalsa_mailbox_local_msgno_has_flags(LibBalsaMailbox *
                                                       mailbox,
                                                       guint msgno,
                                                       LibBalsaMessageFlag
                                                       set,
                                                       LibBalsaMessageFlag
                                                       unset);
static void libbalsa_mailbox_local_test_can_reach(LibBalsaMailbox          * mailbox,
                                                  LibBalsaCanReachCallback * cb,
                                                  gpointer                   cb_data);
static gboolean lbm_local_cache_message(LibBalsaMailboxLocal * local,
                                        guint                  msgno,
                                        LibBalsaMessage      * message);
static gboolean lbml_set_threading_idle_cb(LibBalsaMailboxLocal *local);
static void libbalsa_mailbox_local_cache_message(LibBalsaMailbox * mailbox,
                                                 guint             msgno,
                                                 LibBalsaMessage * message);
static void libbalsa_mailbox_local_check(LibBalsaMailbox *mailbox);

/* LibBalsaMailboxLocal class method: */
static void lbm_local_real_remove_files(LibBalsaMailboxLocal * local);

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(LibBalsaMailboxLocal, libbalsa_mailbox_local,
                                    LIBBALSA_TYPE_MAILBOX)

static void
libbalsa_mailbox_local_class_init(LibBalsaMailboxLocalClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    object_class->finalize = libbalsa_mailbox_local_finalize;

    libbalsa_mailbox_class->changed =
	libbalsa_mailbox_local_changed;
    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_local_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_local_load_config;

    libbalsa_mailbox_class->close_mailbox =
	libbalsa_mailbox_local_close_mailbox;
    libbalsa_mailbox_class->get_message =
	libbalsa_mailbox_local_get_message;
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
    libbalsa_mailbox_class->get_message_part = 
        libbalsa_mailbox_local_get_msg_part;
    libbalsa_mailbox_class->sort = lbm_local_sort;
    libbalsa_mailbox_class->add_messages =
        libbalsa_mailbox_local_add_messages;
    libbalsa_mailbox_class->messages_change_flags =
        libbalsa_mailbox_local_messages_change_flags;
    libbalsa_mailbox_class->msgno_has_flags =
        libbalsa_mailbox_local_msgno_has_flags;
    libbalsa_mailbox_class->duplicate_msgnos =
        libbalsa_mailbox_local_duplicate_msgnos;
    libbalsa_mailbox_class->test_can_reach =
        libbalsa_mailbox_local_test_can_reach;
    libbalsa_mailbox_class->cache_message =
        libbalsa_mailbox_local_cache_message;
    libbalsa_mailbox_class->check =
        libbalsa_mailbox_local_check;

    klass->check_files  = NULL;
    klass->set_path     = NULL;
    klass->remove_files = lbm_local_real_remove_files;
}

static void
libbalsa_mailbox_local_init(LibBalsaMailboxLocal * local)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    priv->sync_id   = 0;
    priv->sync_time = 0;
    priv->sync_cnt  = 0;
    priv->save_tree_id = 0;
}

LibBalsaMailbox *
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
        g_warning("IMAP path given as a path to local mailbox.");
        return NULL;
    } else {		/* mailbox non-existent or unreadable */
	if(create)
	    return libbalsa_mailbox_mbox_new(path, TRUE);
        else {
            g_warning("Unknown mailbox type");
            return NULL;
        }
    }
}

/* libbalsa_mailbox_local_set_path:
   returrns errno on error, 0 on success
   FIXME: proper suport for maildir and mh
*/
gint
libbalsa_mailbox_local_set_path(LibBalsaMailboxLocal * local,
                                const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(local);
    int i;
    LibBalsaMailboxLocalClass *klass;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox), -1);
    g_return_val_if_fail(path != NULL, -1);

    klass = LIBBALSA_MAILBOX_LOCAL_GET_CLASS(mailbox);
    g_assert(klass != NULL);

    if (libbalsa_mailbox_get_url(mailbox) != NULL) {
        const gchar *cur_path = libbalsa_mailbox_local_get_path(mailbox);
        if (strcmp(path, cur_path) == 0)
            return 0;
        else if (access(path, F_OK) == 0)       /* 0 == file does exist */
            return EEXIST;
        else
            i = rename(cur_path, path);
    } else
        i = klass->check_files(path, create);

    /* update mailbox data */
    if (i == 0) {
        gchar *url;

        if (klass->set_path != NULL)
            klass->set_path(local, path);

        url = g_strconcat("file://", path, NULL);
        libbalsa_mailbox_set_url(mailbox, url);
        g_free(url);

        return 0;
    } else
        return errno ? errno : -1;
}

void
libbalsa_mailbox_local_remove_files(LibBalsaMailboxLocal * local)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(local));

    LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->remove_files(local);
}

/* libbalsa_mailbox_load_message:
   MAKE sure the mailbox is LOCKed before entering this routine.
*/

static void
lbml_message_pool_take_message(LibBalsaMailboxLocal * local,
                               LibBalsaMessage * message)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    LibBalsaMailboxLocalPool *item, *oldest;

    ++priv->pool_seqno;

    for (item = oldest = &priv->message_pool[0];
         item < &priv->message_pool[LBML_POOL_SIZE];
         item++) {
        if (item->pool_seqno < oldest->pool_seqno)
            oldest = item;
    }

    if (oldest->message != NULL)
        g_object_unref(oldest->message);
    oldest->message = message; /* we take over the reference count */
    oldest->pool_seqno = priv->pool_seqno;
}

static void
lbm_local_get_message_with_msg_info(LibBalsaMailboxLocal * local,
                                    guint msgno,
                                    LibBalsaMailboxLocalMessageInfo *
                                    msg_info)
{
    LibBalsaMessage *message;

    msg_info->message = message = libbalsa_message_new();
    g_object_add_weak_pointer(G_OBJECT(message),
                              (gpointer *) & msg_info->message);

    libbalsa_message_set_flags(message, msg_info->flags & LIBBALSA_MESSAGE_FLAGS_REAL);
    libbalsa_message_set_mailbox(message, LIBBALSA_MAILBOX(local));
    libbalsa_message_set_msgno(message, msgno);
    libbalsa_message_load_envelope(message);
    lbm_local_cache_message(local, msgno, message);
    lbml_message_pool_take_message(local, message);
}

static gboolean message_match_real(LibBalsaMailbox * mailbox, guint msgno,
                                   LibBalsaCondition * cond);
static void
libbalsa_mailbox_local_load_message(LibBalsaMailboxLocal * local,
                                    GNode ** sibling, guint msgno,
                                    LibBalsaMailboxLocalMessageInfo *
                                    msg_info)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(local);
    LibBalsaCondition *view_filter;
    gboolean match;

    msg_info->loaded = TRUE;

    if ((msg_info->flags & LIBBALSA_MESSAGE_FLAG_NEW)
        && !(msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
        libbalsa_mailbox_add_to_unread_messages(mailbox, 1);
        libbalsa_mailbox_set_first_unread(mailbox, msgno);
    }

    if (msg_info->flags & LIBBALSA_MESSAGE_FLAG_RECENT) {
        gchar *id;

        if (!msg_info->message)
            lbm_local_get_message_with_msg_info(local, msgno, msg_info);

        if (libbalsa_message_is_partial(msg_info->message, &id)) {
            libbalsa_mailbox_try_reassemble(mailbox, id);
            g_free(id);
        }
    }

    view_filter = libbalsa_mailbox_get_view_filter(mailbox, FALSE);
    if (view_filter == NULL)
        match = TRUE;
    else if (!libbalsa_condition_is_flag_only(view_filter,
                                              mailbox, msgno, &match))
        match = message_match_real(mailbox, msgno, view_filter);

    if (match)
        libbalsa_mailbox_msgno_inserted(mailbox, msgno, libbalsa_mailbox_get_msg_tree(mailbox),
                                        sibling);
}

/* Threading info. */
typedef struct {
    gchar *message_id;
    GList *refs_for_threading;
    gchar *sender;
} LibBalsaMailboxLocalInfo;

static void
lbm_local_free_info(LibBalsaMailboxLocalInfo * info)
{
    if (info != NULL) {
        g_free(info->message_id);
        g_list_free_full(info->refs_for_threading, g_free);
        g_free(info->sender);
        g_free(info);
    }
}

static void
libbalsa_mailbox_local_finalize(GObject * object)
{
    LibBalsaMailboxLocal *local = (LibBalsaMailboxLocal *) object;
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    if (priv->sync_id != 0)
        g_source_remove(priv->sync_id);

    if (priv->save_tree_id != 0)
        g_source_remove(priv->save_tree_id);

    if (priv->threading_info != NULL) {
	/* The memory owned by priv->threading_info was freed on closing,
	 * so we free only the array itself. */
	g_ptr_array_free(priv->threading_info, TRUE);
    }

    if (priv->load_messages_id != 0)
        g_source_remove(priv->load_messages_id);

    if (priv->set_threading_id != 0)
        g_source_remove(priv->set_threading_id);

    G_OBJECT_CLASS(libbalsa_mailbox_local_parent_class)->finalize(object);
}

static void lbm_local_queue_save_tree(LibBalsaMailboxLocal * local);

static void
libbalsa_mailbox_local_changed(LibBalsaMailbox * mailbox)
{
    lbm_local_queue_save_tree(LIBBALSA_MAILBOX_LOCAL(mailbox));
}

static void
libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
				   const gchar * prefix)
{
    LibBalsaMailboxLocal *local;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    libbalsa_conf_set_string("Path", libbalsa_mailbox_local_get_path(local));

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
				   const gchar * prefix)
{
    gchar *path;
    gchar *url;

    path = libbalsa_conf_get_string("Path");
    url = g_strconcat("file://", path, NULL);
    g_free(path);
    libbalsa_mailbox_set_url(mailbox, url);
    g_free(url);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->load_config(mailbox, prefix);
}

/*
 * Save and restore the message tree.
 */

typedef struct {
    GArray * array;
    guint (*fileno)(LibBalsaMailboxLocal * local, guint msgno);
    LibBalsaMailboxLocal *local;
} LibBalsaMailboxLocalSaveTreeInfo;

typedef struct {
    guint msgno;
    union {
        guint parent;
        guint total;
    } value;
} LibBalsaMailboxLocalTreeInfo;

/*
 * Save one item; return TRUE on error, to terminate the traverse.
 */
static gboolean
lbm_local_save_tree_item(guint msgno, guint a,
                         LibBalsaMailboxLocalSaveTreeInfo * save_info)
{
    LibBalsaMailboxLocalTreeInfo info;

    if (msgno == 0) {
        info.msgno = msgno;
        info.value.total = a;
    } else if (save_info->fileno) {
        info.msgno = save_info->fileno(save_info->local, msgno);
        info.value.parent = save_info->fileno(save_info->local, a);
    } else {
        info.msgno = msgno;
        info.value.parent = a;
    }

    return g_array_append_val(save_info->array, info) == NULL;
}

static gboolean
lbm_local_save_tree_func(GNode * node, gpointer data)
{
    return node->parent ?
        lbm_local_save_tree_item(GPOINTER_TO_UINT(node->data),
                                 GPOINTER_TO_UINT(node->parent->data),
                                 data) :
        FALSE;
}

static gchar *
lbm_local_get_cache_filename(LibBalsaMailboxLocal * local)
{
    gchar *encoded_path;
    gchar *filename;
    
    encoded_path =
        libbalsa_urlencode(libbalsa_mailbox_local_get_path(local));
    filename =
        g_build_filename(g_get_user_state_dir(), "balsa", encoded_path, NULL);
    g_free(encoded_path);

    return filename;
}

static void
lbm_local_save_tree(LibBalsaMailboxLocal * local)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(local);
    GNode *msg_tree;
    gchar *filename;
    LibBalsaMailboxLocalSaveTreeInfo save_info;
    GError *err = NULL;

    if ((msg_tree = libbalsa_mailbox_get_msg_tree(mailbox)) == NULL ||
        !libbalsa_mailbox_get_msg_tree_changed(mailbox))
        return;
    libbalsa_mailbox_set_msg_tree_changed(mailbox, FALSE);

    filename = lbm_local_get_cache_filename(local);

    if (msg_tree->children == NULL
        || (libbalsa_mailbox_get_threading_type(mailbox) ==
            LB_MAILBOX_THREADING_FLAT
            && libbalsa_mailbox_get_sort_field(mailbox) ==
            LB_MAILBOX_SORT_NO)) {
        unlink(filename);
        g_free(filename);
        return;
    }

    save_info.fileno = LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->fileno;
    save_info.local = local;
    save_info.array =
        g_array_new(FALSE, FALSE, sizeof(LibBalsaMailboxLocalTreeInfo));
    lbm_local_save_tree_item(0, libbalsa_mailbox_get_total(mailbox),
                             &save_info);

    /* Pre-order is required for the file to be created correctly. */
    g_node_traverse(msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) lbm_local_save_tree_func,
                    &save_info);

    if (!g_file_set_contents(filename, save_info.array->data,
                             save_info.array->len *
                             sizeof(LibBalsaMailboxLocalTreeInfo), &err)) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Failed to save cache file “%s”: %s."),
                             filename, err->message);
        g_error_free(err);
    }
    g_array_free(save_info.array, TRUE);
    g_free(filename);
}

static gboolean
lbm_local_restore_tree(LibBalsaMailboxLocal * local, guint * total)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(local);
    const gchar *mailbox_name;
    gchar *filename;
    gchar *name;
    struct stat st;
    gchar *contents;
    gsize length;
    GError *err = NULL;
    GNode *parent, *sibling;
    LibBalsaMailboxLocalTreeInfo *info;
    guint8 *seen;
    LibBalsaMailboxLocalMessageInfo *(*get_info) (LibBalsaMailboxLocal *,
                                                  guint);

    filename = lbm_local_get_cache_filename(local);
    mailbox_name = libbalsa_mailbox_get_name(mailbox);
    name = mailbox_name != NULL ? g_strdup(mailbox_name) :
        g_path_get_basename(libbalsa_mailbox_local_get_path(local));

    if (stat(filename, &st) < 0
        || st.st_mtime < libbalsa_mailbox_get_mtime(mailbox)) {
        /* No error, but we return FALSE so the caller can grab all the
         * message info needed to rethread from scratch. */
        if (libbalsa_mailbox_total_messages(mailbox) > 0)
            g_debug("Cache file for mailbox %s will be created", name);
        g_free(filename);
        g_free(name);
        return FALSE;
    }

    if (!g_file_get_contents(filename, &contents, &length, &err)) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Failed to read cache file %s: %s"),
                             filename, err->message);
        g_error_free(err);
        g_free(filename);
        g_free(name);
        return FALSE;
    }
    g_free(filename);

    info = (LibBalsaMailboxLocalTreeInfo *) contents;
    /* Sanity checks: first the file should have >= 1 record. */
    if (length < sizeof(LibBalsaMailboxLocalTreeInfo)
        /* First record is (0, total): */
        || info->msgno != 0
        /* Total must be > 0 (no file is created for empty tree). */
        || info->value.total == 0
        || info->value.total > libbalsa_mailbox_total_messages(mailbox)) {
        libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                             _("Cache file for mailbox %s "
                               "will be repaired"), name);
        g_free(contents);
        g_free(name);
        return FALSE;
    }
    *total = info->value.total;

    seen = g_new0(guint8, *total);
    parent = libbalsa_mailbox_get_msg_tree(mailbox);
    sibling = NULL;
    get_info = LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->get_info;
    while (++info < (LibBalsaMailboxLocalTreeInfo *) (contents + length)) {
        LibBalsaMailboxLocalMessageInfo *msg_info;
        if (info->msgno == 0 || info->msgno > *total
            || seen[info->msgno - 1]) {
            libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                                 _("Cache file for mailbox %s "
                                   "will be repaired"), name);
            g_free(seen);
            g_free(contents);
            g_free(name);
            return FALSE;
        }
        seen[info->msgno - 1] = TRUE;

        if (sibling
            && info->value.parent == GPOINTER_TO_UINT(sibling->data)) {
            /* This message is the first child of the previous one. */
            parent = sibling;
            sibling = NULL;
        } else {
            /* Find the parent of this message. */
            while (info->value.parent != GPOINTER_TO_UINT(parent->data)) {
                /* Check one level higher. */
                sibling = parent;
                parent = parent->parent;
                if (parent == NULL) {
                    /* We got to the root without finding the parent. */
                    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                                         _("Cache file for mailbox %s "
                                           "will be repaired"), name);
                    g_free(seen);
                    g_free(contents);
                    g_free(name);
                    return FALSE;
                }
            }
        }
        libbalsa_mailbox_msgno_inserted(mailbox, info->msgno,
                                        parent, &sibling);

        msg_info = get_info(local, info->msgno);
        msg_info->loaded = TRUE;

        if (libbalsa_mailbox_msgno_has_flags(mailbox, info->msgno,
                                             LIBBALSA_MESSAGE_FLAG_NEW,
                                             LIBBALSA_MESSAGE_FLAG_DELETED)) {
            libbalsa_mailbox_add_to_unread_messages(mailbox, 1);
            libbalsa_mailbox_set_first_unread(mailbox, info->msgno);
        }
    }

    g_free(seen);
    g_free(contents);
    g_free(name);

    return TRUE;
}

static void
lbm_local_save_tree_real(LibBalsaMailboxLocal * local)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(local);

    libbalsa_lock_mailbox(mailbox);

    if (MAILBOX_OPEN(mailbox) && libbalsa_mailbox_get_msg_tree_changed(mailbox))
        lbm_local_save_tree(local);
    priv->save_tree_id = 0;

    libbalsa_unlock_mailbox(mailbox);
}

static gboolean
lbm_local_save_tree_idle(LibBalsaMailboxLocal * local)
{
    lbm_local_save_tree_real(local);

    return FALSE;
}

static void
lbm_local_queue_save_tree(LibBalsaMailboxLocal * local)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    libbalsa_lock_mailbox((LibBalsaMailbox *) local);
    if (!priv->save_tree_id)
        priv->save_tree_id =
            g_idle_add((GSourceFunc) lbm_local_save_tree_idle, local);
    libbalsa_unlock_mailbox((LibBalsaMailbox *) local);
}

/* 
 * End of save and restore the message tree.
 */

static void
libbalsa_mailbox_local_close_mailbox(LibBalsaMailbox * mailbox,
                                     gboolean expunge)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    LibBalsaMailboxLocalPool *item;

    if(priv->sync_id) {
        g_source_remove(priv->sync_id);
        priv->sync_id = 0;
    }

    /* Restore the persistent view before saving the tree. */
    libbalsa_mailbox_set_view_filter(mailbox,
                                     libbalsa_mailbox_get_view_filter(mailbox, TRUE), TRUE);

    if (priv->set_threading_id != 0) {
        g_source_remove(priv->set_threading_id);
        priv->set_threading_id = 0;
        /* Rethread immediately. */
        libbalsa_mailbox_set_threading(mailbox);
    }

    if (priv->save_tree_id) {
        /* Save immediately. */
        g_source_remove(priv->save_tree_id);
        priv->save_tree_id = 0;
    }
    lbm_local_save_tree(local);

    if (priv->threading_info) {
        guint msgno;
	/* Free the memory owned by priv->threading_info, but neither
	 * free nor truncate the array. */
        for (msgno = priv->threading_info->len; msgno > 0; --msgno) {
            lbm_local_free_info(g_ptr_array_index(priv->threading_info, msgno - 1));
            g_ptr_array_index(priv->threading_info, msgno - 1) = NULL;
        }
    }

    for (item = &priv->message_pool[0];
         item < &priv->message_pool[LBML_POOL_SIZE]; item++) {
        if (item->message) {
            g_object_unref(item->message);
            item->message = NULL;
        }
        item->pool_seqno = 0;
    }
    priv->pool_seqno = 0;

    priv->messages_loaded = FALSE;

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->close_mailbox)
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->close_mailbox(mailbox,
                                                            expunge);
}

/* LibBalsaMailbox get_message class method */

static LibBalsaMessage *
libbalsa_mailbox_local_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    LibBalsaMailboxLocalMessageInfo *msg_info =
        LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->get_info(local, msgno);

    if (msg_info->message == NULL)
        lbm_local_get_message_with_msg_info(local, msgno, msg_info);

    return g_object_ref(msg_info->message);
}

/* Search iters. We do not use the fallback version because it does
   lot of reparsing. Instead, we use LibBalsaMailboxIndex entry
   whenever possible - it is so quite frequently. This is a big
   improvement for mailboxes of several megabytes and few thousand
   messages.
*/

static gboolean
message_match_real(LibBalsaMailbox *mailbox, guint msgno,
                   LibBalsaCondition *cond)
{
    LibBalsaMailboxLocal *local = (LibBalsaMailboxLocal *) mailbox;
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    LibBalsaMessage *message = NULL;
    gboolean match = FALSE;
    gboolean is_refed = FALSE;
    LibBalsaMailboxIndexEntry *entry =
        libbalsa_mailbox_get_index_entry(mailbox, msgno);
    LibBalsaMailboxLocalInfo *info;

    if (priv->threading_info == NULL)
        return FALSE;

    info = (msgno > 0 && msgno <= priv->threading_info->len) ?
        g_ptr_array_index(priv->threading_info, msgno - 1) : NULL;

    /* We may be able to match the msgno from info cached in entry or
     * info; if those are NULL, we'll need to fetch the message, so we
     * fetch it here, and that will also populate entry and info. */
    if (entry == NULL || info == NULL) {
        message = libbalsa_mailbox_get_message(mailbox, msgno);
        if (message == NULL)
            return FALSE;
        lbm_local_cache_message(local, msgno, message);
        entry = libbalsa_mailbox_get_index_entry(mailbox, msgno);
        info  = g_ptr_array_index(priv->threading_info, msgno - 1);
        if (entry == NULL || info == NULL) {
            g_object_unref(message);
            return FALSE;
        }
    }

    if (entry->idle_pending) {
        if (message != NULL)
            g_object_unref(message);
        return FALSE;   /* Can't match. */
    }

    switch (cond->type) {
    case CONDITION_STRING:
        if (CONDITION_CHKMATCH(cond, (CONDITION_MATCH_TO |
                                      CONDITION_MATCH_CC |
                                      CONDITION_MATCH_BODY))) {
            if (!message)
                message = libbalsa_mailbox_get_message(mailbox, msgno);
            if (!message)
                return FALSE;
            is_refed = libbalsa_message_body_ref(message, FALSE);
            if (!is_refed) {
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Unable to load message body to "
                                       "match filter"));
                g_object_unref(message);
                return FALSE;   /* We don't want to match if an error occurred */
            }
        }

        /* do the work */
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO)) {
            LibBalsaMessageHeaders *headers;

            g_assert(is_refed);
            headers = libbalsa_message_get_headers(message);
            if (headers->to_list != NULL) {
                gchar *str =
                    internet_address_list_to_string(headers->to_list, NULL, FALSE);
                match =
                    libbalsa_utf8_strstr(str, cond->match.string.string);
                g_free(str);
                if (match)
                    break;
            }
	}
        if (CONDITION_CHKMATCH(cond, CONDITION_MATCH_FROM)) {
	    if (libbalsa_utf8_strstr(info->sender,
                                     cond->match.string.string)) { 
                match = TRUE;
                break;
            }
        }
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT)) {
	    if (libbalsa_utf8_strstr(entry->subject,
                                     cond->match.string.string)) { 
                match = TRUE;
                break;
            }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC)) {
            LibBalsaMessageHeaders *headers;

            g_assert(is_refed);
            headers = libbalsa_message_get_headers(message);
            if (headers->cc_list != NULL) {
                gchar *str =
                    internet_address_list_to_string(headers->cc_list, NULL, FALSE);
                match =
                    libbalsa_utf8_strstr(str, cond->match.string.string);
                g_free(str);
                if (match)
                    break;
            }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD)) {
            if (cond->match.string.user_header) {
                const gchar *header;

                if (!message)
                    message = libbalsa_mailbox_get_message(mailbox, msgno);
                if (!message)
                    return FALSE;
                header =
                    libbalsa_message_get_user_header(message,
                                                     cond->match.string.
                                                     user_header);
                if (libbalsa_utf8_strstr(header,
                                         cond->match.string.string)) {
                    match = TRUE;
                    break;
                }
            }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY)) {
            GString *body;
            g_assert(is_refed);
            if (libbalsa_message_get_mailbox(message) == NULL) {
                /* No need to body-unref */
                g_object_unref(message);
		return FALSE; /* We don't want to match if an error occurred */
            }
            body = content2reply(libbalsa_message_get_body_list(message),
                                 NULL, 0, FALSE, FALSE);
	    if (body) {
		if (body->str)
                    match = libbalsa_utf8_strstr(body->str,
                                                 cond->match.string.string);
		g_string_free(body,TRUE);
	    }
	}
	break;
    case CONDITION_REGEX:
        break;
    case CONDITION_DATE:
        match = 
            entry->msg_date >= cond->match.date.date_low &&
            (cond->match.date.date_high==0 || 
             entry->msg_date<=cond->match.date.date_high);
        break;
    case CONDITION_FLAG:
        match = libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                                 cond->match.flags, 0);
        break;
    case CONDITION_AND:
        match =
            message_match_real(mailbox, msgno, cond->match.andor.left) &&
            message_match_real(mailbox, msgno, cond->match.andor.right);
        break;
    case CONDITION_OR:
        match =
            message_match_real(mailbox, msgno, cond->match.andor.left) ||
            message_match_real(mailbox, msgno, cond->match.andor.right);
        break;
    /* To avoid warnings */
    case CONDITION_NONE:
        break;
    }
    if (message != NULL) {
        if (is_refed)
            libbalsa_message_body_unref(message);
        g_object_unref(message);
    }

    return cond->negate ? !match : match;
}
static gboolean
libbalsa_mailbox_local_message_match(LibBalsaMailbox * mailbox,
				     guint msgno,
				     LibBalsaMailboxSearchIter * iter)
{
    return message_match_real(mailbox, msgno, iter->condition);
}

/*
 * libbalsa_mailbox_local_cache_message
 *
 *  Caches the message-id, references, and sender,
 *  and passes the message to libbalsa_mailbox_cache_message for caching
 *  other info.
 */
static gboolean
lbm_local_cache_message(LibBalsaMailboxLocal * local,
                        guint msgno,
                        LibBalsaMessage * message)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    LibBalsaMailboxLocalInfo *info;
    LibBalsaMessageHeaders *headers;

    /* If we are not preparing the mailbox for viewing, there is nothing
     * to do. */
    if (priv->threading_info == NULL)
        return FALSE;

    if (priv->threading_info->len < msgno)
        g_ptr_array_set_size(priv->threading_info, msgno);

    if (g_ptr_array_index(priv->threading_info, msgno - 1) != NULL)
        return FALSE;

    info = g_new(LibBalsaMailboxLocalInfo, 1);
    info->message_id = g_strdup(libbalsa_message_get_message_id(message));
    info->refs_for_threading =
        libbalsa_message_refs_for_threading(message);
    info->sender = NULL;

    headers = libbalsa_message_get_headers(message);
    if (headers->from != NULL)
        info->sender = internet_address_list_to_string(headers->from, NULL, FALSE);
    if (info->sender == NULL)
        info->sender = g_strdup("");

    g_ptr_array_index(priv->threading_info, msgno - 1) = info;

    /* Rethread with the new info */
    if (priv->set_threading_id == 0) {
        priv->set_threading_id =
            g_idle_add((GSourceFunc) lbml_set_threading_idle_cb, local);
    }

    return TRUE;
}

static void
libbalsa_mailbox_local_cache_message(LibBalsaMailbox * mailbox,
                                     guint             msgno,
                                     LibBalsaMessage * message)
{
    if (message != NULL &&
        lbm_local_cache_message(LIBBALSA_MAILBOX_LOCAL(mailbox), msgno, message)) {
        LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->
            cache_message(mailbox, msgno, message);
    }
}

static gboolean
lbml_load_messages_idle_cb(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxLocal *local = (LibBalsaMailboxLocal *) mailbox;
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    GNode *msg_tree;
    guint msgno;
    guint new_messages;
    guint lastno;
    GNode *lastn;
    LibBalsaMailboxLocalMessageInfo *(*get_info) (LibBalsaMailboxLocal *,
                                                  guint);

    libbalsa_lock_mailbox(mailbox);
    priv->load_messages_id = 0;

    msg_tree = libbalsa_mailbox_get_msg_tree(mailbox);
    if (msg_tree == NULL) {
	/* Mailbox is closed, or no view has been created. */
        libbalsa_unlock_mailbox(mailbox);
	return FALSE;
    }

    lastno = libbalsa_mailbox_total_messages(mailbox);
    new_messages = 0;
    lastn = g_node_last_child(msg_tree);
    get_info = LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->get_info;
    for (msgno = 1; msgno <= lastno; msgno++) {
        LibBalsaMailboxLocalMessageInfo *msg_info = get_info(local, msgno);

        if (!msg_info->loaded) {
            ++new_messages;
            libbalsa_mailbox_local_load_message(local, &lastn, msgno, msg_info);
            if (msg_info->message != NULL)
                lbm_local_cache_message(local, msgno, msg_info->message);
        }
    }
    priv->messages_loaded = TRUE;

    if (new_messages > 0) {
	libbalsa_mailbox_run_filters_on_reception(mailbox);
	libbalsa_mailbox_set_unread_messages_flag(mailbox,
						  libbalsa_mailbox_get_unread_messages(mailbox) > 0);
    }

    libbalsa_unlock_mailbox(mailbox);

    return FALSE;
}

static void
libbalsa_mailbox_local_check(LibBalsaMailbox *mailbox)
{
    LibBalsaMailboxLocal *local = (LibBalsaMailboxLocal *) mailbox;
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    libbalsa_lock_mailbox(mailbox);
    priv->messages_loaded = FALSE;
    if (priv->load_messages_id == 0) {
        priv->load_messages_id =
            g_idle_add((GSourceFunc) lbml_load_messages_idle_cb, mailbox);
    }
    libbalsa_unlock_mailbox(mailbox);
}

/*
 * Threading
 */

static void lbml_thread_messages(LibBalsaMailbox *mailbox, gboolean subject_gather);
static void lbml_threading_flat(LibBalsaMailbox * mailbox);

static void
lbm_local_set_threading_info(LibBalsaMailboxLocal * local)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    if (priv->threading_info == NULL)
        priv->threading_info =
            g_ptr_array_new_with_free_func((GDestroyNotify) lbm_local_free_info);
}

static void
lbml_set_threading(LibBalsaMailbox * mailbox,
                   LibBalsaMailboxThreadingType thread_type)
{
    switch (thread_type) {
    case LB_MAILBOX_THREADING_JWZ:
        lbml_thread_messages(mailbox, TRUE);
        break;
    case LB_MAILBOX_THREADING_SIMPLE:
        lbml_thread_messages(mailbox, FALSE);
        break;
    case LB_MAILBOX_THREADING_FLAT:
        lbml_threading_flat(mailbox);
        break;
    }

    libbalsa_mailbox_set_messages_threaded(mailbox, TRUE);
}

static gboolean
lbml_set_threading_idle_cb(LibBalsaMailboxLocal *local)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(local);
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    libbalsa_lock_mailbox(mailbox);

    if (libbalsa_mailbox_get_msg_tree(mailbox) != NULL) {
        if (!priv->messages_loaded) {
            libbalsa_unlock_mailbox(mailbox);

            return G_SOURCE_CONTINUE;
        }

        lbml_set_threading(mailbox, libbalsa_mailbox_get_threading_type(mailbox));
    }

    priv->set_threading_id = 0;

    libbalsa_unlock_mailbox(mailbox);

    return G_SOURCE_REMOVE;
}

static void
libbalsa_mailbox_local_set_threading(LibBalsaMailbox * mailbox,
                                     LibBalsaMailboxThreadingType
                                     thread_type)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    lbm_local_set_threading_info(local);
    g_debug("before load_messages: time=%lu", (unsigned long) time(NULL));
    if (libbalsa_mailbox_get_msg_tree(mailbox) == NULL) {   /* first reference */
        guint total = 0;
        gboolean natural = (thread_type == LB_MAILBOX_THREADING_FLAT
                            && libbalsa_mailbox_get_sort_field(mailbox) ==
                            LB_MAILBOX_SORT_NO);

        libbalsa_mailbox_set_msg_tree(mailbox, g_node_new(NULL));
        if (lbm_local_restore_tree(local, &total)) {
            priv->messages_loaded = TRUE;
            libbalsa_mailbox_set_messages_threaded(mailbox, TRUE);
        } else {
            /* Bad or no cache file: start over. */
            libbalsa_mailbox_set_msg_tree(mailbox, g_node_new(NULL));
            total = 0;
        }
        libbalsa_mailbox_set_msg_tree_changed(mailbox, FALSE);

        if (total < libbalsa_mailbox_total_messages(mailbox)) {
            gboolean ok = TRUE;

            if (!natural) {
                /* Get message info for all messages that weren't restored,
                 * so we can thread and sort them correctly before the
                 * mailbox is displayed. */
                ok = libbalsa_mailbox_prepare_threading(mailbox, total);
            }
            if (!ok)
                return; /* Something bad happened */
            libbalsa_mailbox_local_check(mailbox);
        }

        g_debug("after load messages: time=%lu", (unsigned long) time(NULL));
        if (natural) {
            /* No need to thread, but we must set the flag. */
            libbalsa_mailbox_set_messages_threaded(mailbox, TRUE);
            return;
        }
    }

    if (libbalsa_mailbox_total_messages(mailbox) == 0) {
        /* Nothing to thread, but we must set the flag. */
        libbalsa_mailbox_set_messages_threaded(mailbox, TRUE);
    } else if (priv->set_threading_id == 0) {
        priv->set_threading_id =
            g_idle_add((GSourceFunc) lbml_set_threading_idle_cb, local);
    }

    g_debug("after threading time=%lu", (unsigned long) time(NULL));

    lbm_local_queue_save_tree(local);
}

void
libbalsa_mailbox_local_msgno_removed(LibBalsaMailbox * mailbox,
				     guint msgno)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);

    /* local might not have a threading-info array, and even if it does,
     * it might not be populated; we check both. */
    if (priv->threading_info != NULL &&
        msgno > 0 && msgno <= priv->threading_info->len)
	g_ptr_array_remove_index(priv->threading_info, msgno - 1);

    libbalsa_mailbox_msgno_removed(mailbox, msgno);
}

static void
lbm_local_update_view_filter(LibBalsaMailbox * mailbox,
                             LibBalsaCondition * view_filter)
{
    guint total;
    LibBalsaProgress progress = LIBBALSA_PROGRESS_INIT;
    LibBalsaMailboxSearchIter *iter_view;
    guint msgno;
    gboolean is_flag_only = TRUE;

    total = libbalsa_mailbox_total_messages(mailbox);
    if (view_filter
        && !libbalsa_condition_is_flag_only(view_filter, NULL, 0, NULL)) {
        gchar *text;

        text = g_strdup_printf(_("Filtering %s"), libbalsa_mailbox_get_name(mailbox));
        libbalsa_progress_set_text(&progress, text, total);
        g_free(text);
        is_flag_only = FALSE;
    }

    iter_view = libbalsa_mailbox_search_iter_new(view_filter);
    for (msgno = 1; msgno <= total; msgno++) {
        libbalsa_mailbox_msgno_filt_check(mailbox, msgno, iter_view,
                                          FALSE);
        libbalsa_progress_set_fraction(&progress, ((gdouble) msgno) /
                                       ((gdouble) total));
    }
    libbalsa_progress_set_text(&progress, NULL, 0);
    libbalsa_mailbox_search_iter_unref(iter_view);

    /* If this is not a flags-only filter, the new mailbox tree is
     * temporary, so we don't want to save it. */
    if (is_flag_only)
        lbm_local_queue_save_tree(LIBBALSA_MAILBOX_LOCAL(mailbox));
}

/*
 * Prepare-threading method: brute force--create and destroy the
 * LibBalsaMessage; the back end is responsible for caching it here and
 * at LibBalsaMailbox.
 */

/* Helper */
static void
lbm_local_prepare_msgno(LibBalsaMailboxLocal * local, guint msgno)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    LibBalsaMessage *message;

    if (msgno > 0 && msgno <= priv->threading_info->len
        && g_ptr_array_index(priv->threading_info, msgno - 1) != NULL)
        return;

    message =
        libbalsa_mailbox_get_message((LibBalsaMailbox *) local, msgno);
    if (message != NULL) {
        lbm_local_cache_message(local, msgno, message);
        g_object_unref(message);
    }
}

/* The class method; prepare messages from start + 1 to the end of the
 * mailbox; return TRUE if successful. */
static gboolean
libbalsa_mailbox_local_prepare_threading(LibBalsaMailbox * mailbox,
                                         guint start)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    guint msgno;
    gchar *text;
    guint total;
    LibBalsaProgress progress = LIBBALSA_PROGRESS_INIT;

    libbalsa_lock_mailbox(mailbox);
    lbm_local_set_threading_info(local);

    text = g_strdup_printf(_("Preparing %s"), libbalsa_mailbox_get_name(mailbox));
    total = libbalsa_mailbox_total_messages(mailbox);
    libbalsa_progress_set_text(&progress, text, total - start);
    g_free(text);

    for (msgno = start + 1; msgno <= total; msgno++) {
        lbm_local_prepare_msgno(local, msgno);
        libbalsa_progress_set_fraction(&progress,
                                       ((gdouble) msgno) / ((gdouble) (total - start)));
    }

    libbalsa_progress_set_text(&progress, NULL, 0);

    libbalsa_unlock_mailbox(mailbox);

    return TRUE;
}

/* fetch message structure method: all local mailboxes have their own
 * methods, which ensure that message->mime_msg != NULL, then chain up
 * to this one.
 */

static gboolean
libbalsa_mailbox_local_fetch_structure(LibBalsaMailbox *mailbox,
                                       LibBalsaMessage *message,
                                       LibBalsaFetchFlag flags)
{
    GMimeMessage *mime_message;
    LibBalsaMessageHeaders *headers;

    mime_message = libbalsa_message_get_mime_message(message);
    if (mime_message == NULL || mime_message->mime_part == NULL)
	return FALSE;

    headers = libbalsa_message_get_headers(message);

    if ((flags & LB_FETCH_STRUCTURE) != 0) {
        LibBalsaMessageBody *body = libbalsa_message_body_new(message);
        libbalsa_message_body_set_mime_body(body,
                                            mime_message->mime_part);
        libbalsa_message_append_part(message, body);
        libbalsa_message_headers_from_gmime(headers, mime_message);
    }

    if ((flags & LB_FETCH_RFC822_HEADERS) != 0) {
        headers->user_hdrs = libbalsa_message_user_hdrs_from_gmime(mime_message);
        libbalsa_message_set_has_all_headers(message, TRUE);
    }

    return TRUE;
}

static void
libbalsa_mailbox_local_fetch_headers(LibBalsaMailbox * mailbox,
				     LibBalsaMessage * message)
{
    LibBalsaMessageHeaders *headers;
    GMimeMessage *mime_msg;

    headers = libbalsa_message_get_headers(message);
    g_return_if_fail(headers->user_hdrs == NULL);

    mime_msg = libbalsa_message_get_mime_message(message);
    if (mime_msg != NULL) {
        headers->user_hdrs = libbalsa_message_user_hdrs_from_gmime(mime_msg);
    } else {
	libbalsa_mailbox_fetch_message_structure(mailbox, message,
						 LB_FETCH_RFC822_HEADERS);
	libbalsa_mailbox_release_message(mailbox, message);
    }
}

static gboolean
libbalsa_mailbox_local_get_msg_part(LibBalsaMessage *msg,
                                    LibBalsaMessageBody *part,
                                    GError **err)
{
    g_return_val_if_fail(part->mime_part, FALSE);

    return GMIME_IS_PART(part->mime_part)
        || GMIME_IS_MULTIPART(part->mime_part)
	|| GMIME_IS_MESSAGE_PART(part->mime_part);
}

/*--------------------------------*/
/*  Start of threading functions  */
/*--------------------------------*/
/*
 * Threading is implementated using jwz's algorithm describled at
 * http://www.jwz.org/doc/threading.html. It is implemented with an option
 * to avoid the "subject gather" step, which is needed only if some
 * messages do not have a valid and complete "References" header, and
 * can otherwise be annoying. If you don't need
 * message threading functionality, just specify 'LB_MAILBOX_THREADING_FLAT'. 
 *
 * ymnk@jcraft.com
 */

struct _ThreadingInfo {
    LibBalsaMailbox *mailbox;
    GNode *root;
    GHashTable *id_table;
    GHashTable *subject_table;
    gboolean missing_info;
    gboolean missing_parent;
};
typedef struct _ThreadingInfo ThreadingInfo;

static gboolean lbml_set_parent(GNode * node, ThreadingInfo * ti);
static GNode *lbml_insert_node(GNode * node,
                               LibBalsaMailboxLocalInfo * info,
                               ThreadingInfo * ti);
static GNode *lbml_find_parent(LibBalsaMailboxLocalInfo * info,
			       ThreadingInfo * ti);
static gboolean lbml_prune(GNode * node, ThreadingInfo * ti);
static void lbml_subject_gather(GNode * node, ThreadingInfo * ti);
static void lbml_subject_merge(GNode * node, ThreadingInfo * ti);
static const gchar *lbml_chop_re(const gchar * str);
static gboolean lbml_construct(GNode * node, ThreadingInfo * ti);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
static void lbml_clear_empty(GNode * root);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

static void
lbml_info_setup(LibBalsaMailbox * mailbox, ThreadingInfo * ti)
{
    ti->mailbox = mailbox;
    ti->root = g_node_new(libbalsa_mailbox_get_msg_tree(mailbox));
    ti->id_table = g_hash_table_new(g_str_hash, g_str_equal);
    ti->subject_table = NULL;
    ti->missing_info = FALSE;
    ti->missing_parent = FALSE;
}

static void
lbml_info_free(ThreadingInfo * ti)
{
    g_hash_table_destroy(ti->id_table);
    if (ti->subject_table)
	g_hash_table_destroy(ti->subject_table);
    g_node_destroy(ti->root);
}

static void
lbml_thread_messages(LibBalsaMailbox * mailbox, gboolean subject_gather)
{
    /* This implementation of JWZ's algorithm uses a second tree, rooted
     * at ti.root, for the message IDs.  Each node in the second tree
     * that corresponds to a real message has a pointer to the
     * corresponding msg_tree node in its data field.  Nodes in the
     * mailbox's msg_tree have names beginning with msg_; all other
     * GNodes are in the second tree.  The ti.id_table maps message-id
     * to a node in the second tree. */
    ThreadingInfo ti;

    lbml_info_setup(mailbox, &ti);

    /* Traverse the mailbox's msg_tree, to build the second tree. */
    g_node_traverse(libbalsa_mailbox_get_msg_tree(mailbox),
                    G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbml_set_parent, &ti);
    /* Prune the second tree. */
    g_node_traverse(ti.root, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbml_prune, &ti);

    if (subject_gather) {
        /* Do the evil subject gather and merge on the second tree. */
        ti.subject_table = g_hash_table_new(g_str_hash, g_str_equal);
        g_node_children_foreach(ti.root, G_TRAVERSE_ALL,
                                (GNodeForeachFunc) lbml_subject_gather, &ti);
        g_node_children_foreach(ti.root, G_TRAVERSE_ALL,
                                (GNodeForeachFunc) lbml_subject_merge, &ti);
    }

    /* Traverse the second tree and reparent corresponding nodes in the
     * mailbox's msg_tree. */
    g_node_traverse(ti.root, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    (GNodeTraverseFunc) lbml_construct, &ti);

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    lbml_clear_empty(ti.root);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    lbml_info_free(&ti);

    if (ti.missing_parent && ti.missing_info) {
        /* We need to completely rethread.
         * If any new info is found, a rethreading will be scheduled. */
        libbalsa_mailbox_prepare_threading(mailbox, 0);
    }
}

static LibBalsaMailboxLocalInfo *
lbml_get_info(GNode * node, ThreadingInfo * ti)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(ti->mailbox);
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    guint msgno = GPOINTER_TO_UINT(node->data);
    LibBalsaMailboxLocalInfo *info;

    if (msgno == 0 || msgno > priv->threading_info->len)
        return NULL;

    info = g_ptr_array_index(priv->threading_info, msgno - 1);

    return info;
}

static void
lbml_unlink_and_prepend(GNode * node, GNode * parent)
{
    g_node_unlink(node);
    g_node_prepend(parent, node);
}

static void
lbml_move_children(GNode * node, GNode * parent)
{
    GNode *child;

    while ((child = node->children))
	lbml_unlink_and_prepend(child, parent);
}

static gboolean
lbml_set_parent(GNode * msg_node, ThreadingInfo * ti)
{
    LibBalsaMailboxLocalInfo *info;
    GNode *node;
    GNode *parent;
    GNode *child;

    if (!msg_node->parent)
	return FALSE;

    info = lbml_get_info(msg_node, ti);

    if (info == NULL) {
        /* We may not need the info; just note that it is missing. */
        ti->missing_info = TRUE;
	return FALSE;
    }

    node = lbml_insert_node(msg_node, info, ti);

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

    parent = lbml_find_parent(info, ti);

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
	    lbml_unlink_and_prepend(child, node->parent);
	}
	child = next;
    }

    lbml_unlink_and_prepend(node, parent);

    return FALSE;
}

static GNode *
lbml_find_parent(LibBalsaMailboxLocalInfo * info, ThreadingInfo * ti)
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
    GNode *parent = ti->root;
    GList *reference;
    GHashTable *id_table = ti->id_table;
    gboolean has_real_parent = FALSE;

    for (reference = info->refs_for_threading; reference;
	 reference = reference->next) {
	gchar *id = reference->data;
	GNode *foo = g_hash_table_lookup(id_table, id);

	if (foo != NULL) {
            has_real_parent = TRUE;
        } else {
	    foo = g_node_new(NULL);
	    g_hash_table_insert(id_table, id, foo);
	}

	/* Avoid nasty surprises. */
	if (foo != parent && !g_node_is_ancestor(foo, parent))
	    if (!foo->parent || foo->parent == ti->root)
		lbml_unlink_and_prepend(foo, parent);

	parent = foo;
    }

    if (info->refs_for_threading != NULL && !has_real_parent) {
        /* This message appears to have a parent, but we did not find
         * it. */
        ti->missing_parent = TRUE;
    }

    return parent;
}

static gboolean
lbml_is_replied(GNode * msg_node, ThreadingInfo * ti)
{
    guint msgno = GPOINTER_TO_UINT(msg_node->data);
    return libbalsa_mailbox_msgno_get_status(ti->mailbox, msgno)
	== LIBBALSA_MESSAGE_STATUS_REPLIED;
}

static GNode *
lbml_insert_node(GNode * msg_node, LibBalsaMailboxLocalInfo * info,
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
    GNode *node = NULL;
    gchar *id = info->message_id;
    GHashTable *id_table = ti->id_table;

    if (id)
	node = g_hash_table_lookup(id_table, id);

    if (node) {
	GNode *prev_msg_node = node->data;
	/* If this message has not been replied to, or if the container
	 * is empty, store it in the container. If there was a message
	 * in the container already, swap it with this one, otherwise
	 * set the current one to NULL. */
	if (!lbml_is_replied(msg_node, ti) || !prev_msg_node) {
	    node->data = msg_node;
	    msg_node = prev_msg_node;
	}
    }
    /* If we already stored the message in a previously empty container,
     * msg_node is NULL. If either the previous message or the current
     * one has been replied to, msg_node now points to a replied-to
     * message. */
    if (msg_node)
	node = g_node_new(msg_node);

    if (id)
	g_hash_table_insert(id_table, id, node);

    return node;
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

    if (node->data != NULL || node == ti->root)
	return FALSE;

#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (node->children != NULL
	&& (node->parent != ti->root || node->children->next == NULL))
	lbml_move_children(node, node->parent);

    if (node->children == NULL)
	g_node_destroy(node);
#else				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    lbml_move_children(node, node->parent);
    g_node_destroy(node);
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    return FALSE;
}

static const gchar *
lbml_get_subject(GNode * node, ThreadingInfo * ti)
{
    guint msgno = GPOINTER_TO_UINT(((GNode *) node->data)->data);
    return libbalsa_mailbox_msgno_get_subject(ti->mailbox, msgno);
}

static void
lbml_subject_gather(GNode * node, ThreadingInfo * ti)
{
    const gchar *subject = NULL, *old_subject;
    const gchar *chopped_subject = NULL;
    GNode *old;
    GHashTable *subject_table = ti->subject_table;

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

    subject = lbml_get_subject(node, ti);
    if (subject == NULL)
	return;
    chopped_subject = lbml_chop_re(subject);
    if (chopped_subject == NULL
	|| !strcmp(chopped_subject, _("(No subject)")))
	return;

    old = g_hash_table_lookup(subject_table, chopped_subject);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    if (old == NULL || (node->data == NULL && old->data != NULL)) {
	g_hash_table_insert(subject_table, (char *) chopped_subject,
			    node);
	return;
    }
#else				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */
    if (old == NULL) {
	g_hash_table_insert(subject_table, (char *) chopped_subject,
			    node);
	return;
    }
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    old_subject = lbml_get_subject(old, ti);

    if (old_subject != lbml_chop_re(old_subject)
	&& subject == chopped_subject)
	g_hash_table_insert(subject_table, (gchar *) chopped_subject,
			    node);
}

/* Swap data and children. */
static void
lbml_swap(GNode * node1, GNode * node2, ThreadingInfo * ti)
{
    GNode *tmp_node = g_node_new(NULL);
    gpointer tmp_data;

    lbml_move_children(node1, tmp_node);
    lbml_move_children(node2, node1);
    lbml_move_children(tmp_node, node2);
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
	lbml_move_children(node, node2);
	g_node_destroy(node);
	return;
    }

    if (node->data == NULL)
	/* node2 should be made a child of node, but unlinking node2
	 * could mess up the foreach, so we'll swap them and fall
	 * through to the next case. */
	lbml_swap(node, node2, ti);

    if (node2->data == NULL) {
	lbml_unlink_and_prepend(node, node2);
	return;
    }
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

    subject2 = lbml_get_subject(node2, ti);
    chopped_subject2 = lbml_chop_re(subject2);

    if ((subject2 == chopped_subject2) && subject != chopped_subject)
	/* Make node a child of node2. */
	lbml_unlink_and_prepend(node, node2);
    else if ((subject2 != chopped_subject2)
	     && subject == chopped_subject) {
	/* Make node2 a child of node; as above, swap them to avoid
	 * unlinking node2. */
	lbml_swap(node, node2, ti);
	lbml_unlink_and_prepend(node, node2);
#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
    } else {
	/* Make both node and node2 children of a new empty node; as
	 * above, swap node2 and the new node to avoid unlinking node2.
	 */
	GNode *new_node = g_node_new(NULL);

	lbml_move_children(node2, new_node);
	new_node->data = node2->data;
	node2->data = NULL;
	lbml_unlink_and_prepend(node, node2);
	lbml_unlink_and_prepend(new_node, node2);
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

static gboolean
lbml_construct(GNode * node, ThreadingInfo * ti)
{
    GNode *msg_node;

    if (node->parent && (msg_node = node->data)) {
        GNode *msg_parent = node->parent->data;

        if (msg_parent && msg_node->parent != msg_parent
            && !g_node_is_ancestor(msg_node, msg_parent))
            libbalsa_mailbox_unlink_and_prepend(ti->mailbox, msg_node,
                                                msg_parent);
    }

    return FALSE;
}


#ifdef MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT
static void
lbml_clear_empty(GNode * msg_tree)
{
    GNode *node = msg_tree->children;
    while (node) {
	GNode *next = node->next;
	if (!node->data && !node->children)
	    g_node_destroy(node);
	node = next;
    }
}
#endif				/* MAKE_EMPTY_CONTAINER_FOR_MISSING_PARENT */

/*------------------------------*/
/*       Flat threading         */
/*------------------------------*/

static gboolean
lbml_unthread_message(GNode * node, LibBalsaMailbox * mailbox)
{
    if (node->parent != NULL) {
        GNode *msg_tree = libbalsa_mailbox_get_msg_tree(mailbox);

        if (node->parent != msg_tree)
            libbalsa_mailbox_unlink_and_prepend(mailbox, node, msg_tree);
    }

    return FALSE;
}

static void
lbml_threading_flat(LibBalsaMailbox * mailbox)
{
    g_node_traverse(libbalsa_mailbox_get_msg_tree(mailbox),
                    G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbml_unthread_message, mailbox);
}

/*------------------------------*/
/*  End of threading functions  */
/*------------------------------*/

/* Helper for maildir and mh. */
GMimeMessage *
libbalsa_mailbox_local_get_mime_message(LibBalsaMailbox * mailbox,
					 const gchar * name1,
					 const gchar * name2)
{
    GMimeStream *mime_stream;
    GMimeParser *mime_parser;
    GMimeMessage *mime_message;

    mime_stream =
	libbalsa_mailbox_local_get_message_stream(mailbox, name1, name2);
    if (!mime_stream)
	return NULL;

    mime_parser = g_mime_parser_new_with_stream(mime_stream);
    g_mime_parser_set_format(mime_parser, GMIME_FORMAT_MESSAGE);
    mime_message = g_mime_parser_construct_message(mime_parser, libbalsa_parser_options());

    g_object_unref(mime_parser);
    g_object_unref(mime_stream);

    return mime_message;
}

GMimeStream *
libbalsa_mailbox_local_get_message_stream(LibBalsaMailbox * mailbox,
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
lbm_local_sync_real(LibBalsaMailboxLocal * local)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    LibBalsaMailbox *mailbox = (LibBalsaMailbox*)local;
    time_t tstart;

    time(&tstart);
    libbalsa_lock_mailbox(mailbox);
    if (MAILBOX_OPEN(mailbox) &&                   /* mailbox still open */
        !libbalsa_mailbox_sync_storage(mailbox, FALSE))   /* cannot sync */
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Failed to sync mailbox “%s”"),
			     libbalsa_mailbox_get_name(mailbox));
    priv->sync_time += time(NULL)-tstart;
    priv->sync_cnt++;
    libbalsa_unlock_mailbox(mailbox);
    g_object_unref(local);
}

static gboolean
lbm_local_sync_idle(LibBalsaMailboxLocal * local)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    GThread *sync_thread;

    priv->sync_id = 0;
    sync_thread =
    	g_thread_new("lbm_local_sync_real", (GThreadFunc) lbm_local_sync_real, local);
    g_thread_unref(sync_thread);

    return G_SOURCE_REMOVE;
}

static void
lbm_local_sync_queue(LibBalsaMailboxLocal * local)
{
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    guint schedule_delay;

    /* The optimal behavior here would be to keep rescheduling
    * requests.  But think of following: the idle handler started and
    * triggered lbm_local_sync_real thread. While it waits for the lock,
    * another queue request is filed but it is too late for removal of
    * the sync thread. And we get two sync threads executing one after
    * another, etc. So it is better to do sync bit too often... */
    if (priv->sync_id) 
        return;
    g_object_ref(local);
    /* queue sync job so that the delay is at least five times longer
     * than the syncing time. Otherwise large mailbox owners will be
     * annnoyed. */
    schedule_delay =
        priv->sync_cnt ? (priv->sync_time * 5000) / priv->sync_cnt : 0;
    priv->sync_id = g_timeout_add_full(G_PRIORITY_LOW, schedule_delay,
                                        (GSourceFunc)lbm_local_sync_idle,
                                        local, NULL);
}

static void
lbm_local_sort(LibBalsaMailbox * mailbox, GArray *sort_array)
{
    LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_local_parent_class)->sort(mailbox, sort_array);
    lbm_local_queue_save_tree(LIBBALSA_MAILBOX_LOCAL(mailbox));
}

static guint
libbalsa_mailbox_local_add_messages(LibBalsaMailbox          * mailbox,
                                    LibBalsaAddMessageIterator msg_iterator,
                                    gpointer                   iter_data,
                                    GError                  ** err)
{
    LibBalsaMessageFlag flag;
    GMimeStream *stream;
    LibBalsaMailboxLocal *local;
    guint cnt;
    LibBalsaMailboxLocalAddMessageFunc *add_message;

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    cnt = 0;
    add_message = LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->add_message;
    while (msg_iterator(&flag, &stream, iter_data)) {
        gboolean success;

        success = (*add_message)(local, stream, flag, err);
        g_object_unref(stream);
        if (!success)
            break;
        cnt++;
    }

    return cnt;
}

#define FLAGS_REALLY_DIFFER(flags0, flags1) \
        (((flags0 ^ flags1) & LIBBALSA_MESSAGE_FLAGS_REAL) != 0)

static gboolean
libbalsa_mailbox_local_messages_change_flags(LibBalsaMailbox * mailbox,
                                             GArray * msgnos,
                                             LibBalsaMessageFlag set,
                                             LibBalsaMessageFlag clear)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    LibBalsaMailboxLocalMessageInfo *(*get_info) (LibBalsaMailboxLocal *,
                                                  guint) =
        LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->get_info;
    guint i;
    guint changed = 0;
    libbalsa_lock_mailbox(mailbox);
    for (i = 0; i < msgnos->len; i++) {
        guint msgno = g_array_index(msgnos, guint, i);
        LibBalsaMailboxLocalMessageInfo *msg_info;
        LibBalsaMessageFlag old_flags;

        if (!(msgno > 0
              && msgno <= libbalsa_mailbox_total_messages(mailbox))) {
            g_warning("msgno %u out of range", msgno);
            continue;
        }

        msg_info = get_info(local, msgno);
        old_flags = msg_info->flags;
        msg_info->flags |= set;
        msg_info->flags &= ~clear;
        if (!FLAGS_REALLY_DIFFER(msg_info->flags, old_flags))
            /* No real flags changed. */
            continue;
        ++changed;

        if (msg_info->message != NULL)
            libbalsa_message_set_flags(msg_info->message, msg_info->flags);

        libbalsa_mailbox_index_set_flags(mailbox, msgno, msg_info->flags);

        if (msg_info->loaded) {
            gboolean was_unread_undeleted, is_unread_undeleted;

            was_unread_undeleted =
                (old_flags & LIBBALSA_MESSAGE_FLAG_NEW)
                && !(old_flags & LIBBALSA_MESSAGE_FLAG_DELETED);
            is_unread_undeleted =
                (msg_info->flags & LIBBALSA_MESSAGE_FLAG_NEW)
                && !(msg_info->flags & LIBBALSA_MESSAGE_FLAG_DELETED);
            libbalsa_mailbox_add_to_unread_messages(mailbox,
                is_unread_undeleted - was_unread_undeleted);
        }
    }
    libbalsa_unlock_mailbox(mailbox);

    if (changed > 0) {
        libbalsa_mailbox_set_unread_messages_flag(mailbox,
                                                  libbalsa_mailbox_get_unread_messages(mailbox)
                                                  > 0);
        lbm_local_sync_queue(local);
    }

    return TRUE;
}

static gboolean
libbalsa_mailbox_local_msgno_has_flags(LibBalsaMailbox * mailbox,
                                       guint msgno,
                                       LibBalsaMessageFlag set,
                                       LibBalsaMessageFlag unset)
{
    LibBalsaMailboxLocal *local = LIBBALSA_MAILBOX_LOCAL(mailbox);
    LibBalsaMailboxLocalMessageInfo *msg_info =
        LIBBALSA_MAILBOX_LOCAL_GET_CLASS(local)->get_info(local, msgno);

    return (msg_info->flags & set) == set && (msg_info->flags & unset) == 0;
}

static GArray *
libbalsa_mailbox_local_duplicate_msgnos(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxLocal *local = (LibBalsaMailboxLocal *) mailbox;
    LibBalsaMailboxLocalPrivate *priv =
        libbalsa_mailbox_local_get_instance_private(local);
    GHashTable *table;
    guint i;
    GArray *msgnos;

    if (!priv->threading_info)
        return NULL;

    /* We need all the message-ids. */
    if (!libbalsa_mailbox_prepare_threading(mailbox, 0))
        return NULL;

    table = g_hash_table_new(g_str_hash, g_str_equal);
    msgnos = g_array_new(FALSE, FALSE, sizeof(guint));

    for (i = 0; i < priv->threading_info->len; i++) {
        LibBalsaMailboxLocalInfo *info;
        gpointer prev;
        guint msgno = i + 1;

        if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno, LIBBALSA_MESSAGE_FLAG_DELETED, 0))
            continue;

        info = g_ptr_array_index(priv->threading_info, i);
        if (info == NULL || info->message_id == NULL)
            continue;

        prev = g_hash_table_lookup(table, info->message_id);
        if (prev == NULL) {
            /* First message with this message-id */
            g_hash_table_insert(table, info->message_id, GUINT_TO_POINTER(msgno));
        } else {
            if (libbalsa_mailbox_msgno_has_flags(mailbox, msgno, LIBBALSA_MESSAGE_FLAG_REPLIED, 0)) {
                /* This message has been replied to, so we keep this one
                 * and mark the previous one as duplicate. */
                g_hash_table_insert(table, info->message_id, GUINT_TO_POINTER(msgno));
                msgno = GPOINTER_TO_UINT(prev);
            }
            g_array_append_val(msgnos, msgno);
        }
    }

    g_hash_table_destroy(table);

    return msgnos;
}

static void
libbalsa_mailbox_local_test_can_reach(LibBalsaMailbox          * mailbox,
                                      LibBalsaCanReachCallback * cb,
                                      gpointer                   cb_data)
{
    cb((GObject *) mailbox, TRUE, cb_data);
}

/* LibBalsaMailboxLocal class method: */
static void
lbm_local_real_remove_files(LibBalsaMailboxLocal * local)
{
    gchar *filename = lbm_local_get_cache_filename(local);
    unlink(filename);
    g_free(filename);
}
