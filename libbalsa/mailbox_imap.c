/* -*-mode:c; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

/* NOTES:

   CACHING: persistent cache is implemented using a directory. 

   CONNECTIONS: there is always one connection per opened mailbox to
   keep track of untagged responses. Understand idea of untagged
   responses particularly for shared mailboxes before you try messing
   with this.
*/
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */


#include <stdlib.h>
#include <string.h>

/* for open() */
#include <sys/stat.h>
#include <fcntl.h>

/* for uint32_t */
#include <stdint.h>

#include "filter-funcs.h"
#include "filter.h"
#include "imap-commands.h"
#include "imap-handle.h"
#include "imap-server.h"
#include "libbalsa-conf.h"
#include "libbalsa_private.h"
#include "libimap.h"
#include "mailbox-filter.h"
#include "message.h"
#include "mime-stream-shared.h"
#include "misc.h"
#include "server.h"

#include <glib/gi18n.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mbox-imap"

#define ENABLE_CLIENT_SIDE_SORT 1

struct _LibBalsaMailboxImap {
    LibBalsaMailboxRemote mailbox;
    ImapMboxHandle *handle;     /* stream that has this mailbox selected */
    guint handle_refs;		/* reference counter */
    gint search_stamp;		/* search result validator */

    gchar *path;		/* Imap local path (third part of URL) */
    ImapUID      uid_validity;

    GArray* messages_info;
    GPtrArray *msgids; /* message-ids */

    GArray *sort_ranks;
    guint unread_update_id;
    LibBalsaMailboxSortFields sort_field;
    unsigned opened:1;

    ImapAclType rights;     /* RFC 4314 'myrights' */
    GList *acls;            /* RFC 4314 acl's */

    gboolean disconnected;
    struct ImapCacheManager *icm;

    GArray *expunged_seqnos;
    guint expunged_idle_id;
};

struct message_info {
    LibBalsaMessage *message;
    LibBalsaMessageFlag user_flags;
};

static off_t ImapCacheSize = 30*1024*1024; /* 30MB */

 /* issue message if downloaded part has more than this size */
static unsigned SizeMsgThreshold = 50*1024;
static void libbalsa_mailbox_imap_dispose(GObject * object);
static void libbalsa_mailbox_imap_finalize(GObject * object);
static gboolean libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox,
					   GError **err);
static void libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox,
                                        gboolean expunge);
static GMimeStream *libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox *
							     mailbox,
							     guint msgno,
							     gboolean peek);
static void libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox);

static void
libbalsa_mailbox_imap_search_iter_free(LibBalsaMailboxSearchIter * iter);
static gboolean libbalsa_mailbox_imap_message_match(LibBalsaMailbox* mailbox,
						    guint msgno,
						    LibBalsaMailboxSearchIter
						    * search_iter);
static gboolean libbalsa_mailbox_imap_can_match(LibBalsaMailbox  *mbox,
						LibBalsaCondition *condition);
static void libbalsa_mailbox_imap_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

static gboolean libbalsa_mailbox_imap_sync(LibBalsaMailbox * mailbox,
                                           gboolean expunge);
static LibBalsaMessage* libbalsa_mailbox_imap_get_message(LibBalsaMailbox*
							  mailbox,
							  guint msgno);
static gboolean libbalsa_mailbox_imap_prepare_threading(LibBalsaMailbox *
                                                        mailbox,
                                                        guint start);
static gboolean libbalsa_mailbox_imap_fetch_structure(LibBalsaMailbox *
                                                      mailbox,
                                                      LibBalsaMessage *
                                                      message,
                                                      LibBalsaFetchFlag
                                                      flags);
static void libbalsa_mailbox_imap_fetch_headers(LibBalsaMailbox *mailbox,
                                                LibBalsaMessage *message);
static gboolean libbalsa_mailbox_imap_get_msg_part(LibBalsaMessage *msg,
						   LibBalsaMessageBody *,
                                                   GError **err);
static GArray *libbalsa_mailbox_imap_duplicate_msgnos(LibBalsaMailbox *
						      mailbox);

static guint libbalsa_mailbox_imap_add_messages(LibBalsaMailbox *mailbox,
						LibBalsaAddMessageIterator mi,
						void *mi_arg,
						GError ** err);

static gboolean lbm_imap_messages_change_flags(LibBalsaMailbox * mailbox,
                                               GArray * seqno,
                                              LibBalsaMessageFlag set,
                                              LibBalsaMessageFlag clear);
static gboolean libbalsa_mailbox_imap_msgno_has_flags(LibBalsaMailbox *
                                                      mailbox, guint seqno,
                                                      LibBalsaMessageFlag
                                                      set,
                                                      LibBalsaMessageFlag
                                                      unset);
static gboolean libbalsa_mailbox_imap_can_do(LibBalsaMailbox* mbox,
                                             enum LibBalsaMailboxCapability c);

static void libbalsa_mailbox_imap_set_threading(LibBalsaMailbox *mailbox,
						LibBalsaMailboxThreadingType
						thread_type);
static void lbm_imap_update_view_filter(LibBalsaMailbox   *mailbox,
                                        LibBalsaCondition *view_filter);
static void libbalsa_mailbox_imap_sort(LibBalsaMailbox *mailbox,
                                       GArray *array);
static guint libbalsa_mailbox_imap_total_messages(LibBalsaMailbox *
						  mailbox);
static gboolean libbalsa_mailbox_imap_messages_copy(LibBalsaMailbox *
						    mailbox,
						    GArray * msgnos,
						    LibBalsaMailbox *
						    dest,
                                                    GError **err);
static void libbalsa_mailbox_imap_parse_set_headers(LibBalsaMessage *message,
													const gchar     *header_str);

static void server_host_settings_changed_cb(LibBalsaServer * server,
					    LibBalsaMailbox * mailbox);
static void imap_cache_manager_free(struct ImapCacheManager *icm);


static struct message_info *message_info_from_msgno(
						  LibBalsaMailboxImap * mimap,
						  guint msgno)
{
    struct message_info *msg_info;

    if (msgno > mimap->messages_info->len) {
        g_debug("%s msgno %d > messages_info len %d",
                libbalsa_mailbox_get_name(LIBBALSA_MAILBOX(mimap)), msgno,
                mimap->messages_info->len);
        msg_info = NULL;
    } else
        msg_info =
            &g_array_index(mimap->messages_info, struct message_info,
                           msgno - 1);

    return msg_info;
}

#define IMAP_MAILBOX_UID_VALIDITY(mailbox) (LIBBALSA_MAILBOX_IMAP(mailbox)->uid_validity)

G_DEFINE_TYPE(LibBalsaMailboxImap, libbalsa_mailbox_imap, LIBBALSA_TYPE_MAILBOX_REMOTE)

static void
libbalsa_mailbox_imap_class_init(LibBalsaMailboxImapClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    object_class->dispose = libbalsa_mailbox_imap_dispose;
    object_class->finalize = libbalsa_mailbox_imap_finalize;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_imap_open;
    libbalsa_mailbox_class->close_mailbox = libbalsa_mailbox_imap_close;

    libbalsa_mailbox_class->check = libbalsa_mailbox_imap_check;

    libbalsa_mailbox_class->search_iter_free =
	libbalsa_mailbox_imap_search_iter_free;
    libbalsa_mailbox_class->message_match =
	libbalsa_mailbox_imap_message_match;
    libbalsa_mailbox_class->can_match =
	libbalsa_mailbox_imap_can_match;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_imap_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_imap_load_config;
    libbalsa_mailbox_class->sync = libbalsa_mailbox_imap_sync;
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_imap_get_message;
    libbalsa_mailbox_class->prepare_threading =
        libbalsa_mailbox_imap_prepare_threading;
    libbalsa_mailbox_class->fetch_message_structure = 
        libbalsa_mailbox_imap_fetch_structure;
    libbalsa_mailbox_class->fetch_headers = 
        libbalsa_mailbox_imap_fetch_headers;
    libbalsa_mailbox_class->get_message_part = 
        libbalsa_mailbox_imap_get_msg_part;
    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_imap_get_message_stream;
    libbalsa_mailbox_class->duplicate_msgnos =
        libbalsa_mailbox_imap_duplicate_msgnos;
    libbalsa_mailbox_class->add_messages = libbalsa_mailbox_imap_add_messages;
    libbalsa_mailbox_class->messages_change_flags =
	lbm_imap_messages_change_flags;
    libbalsa_mailbox_class->msgno_has_flags =
	libbalsa_mailbox_imap_msgno_has_flags;
    libbalsa_mailbox_class->can_do =
	libbalsa_mailbox_imap_can_do;
    libbalsa_mailbox_class->set_threading =
	libbalsa_mailbox_imap_set_threading;
    libbalsa_mailbox_class->update_view_filter =
        lbm_imap_update_view_filter;
    libbalsa_mailbox_class->sort = libbalsa_mailbox_imap_sort;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_imap_total_messages;
    libbalsa_mailbox_class->messages_copy =
	libbalsa_mailbox_imap_messages_copy;
}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox)
{
    mailbox->path = NULL;
    mailbox->handle = NULL;
    mailbox->handle_refs = 0;
    mailbox->sort_ranks = g_array_new(FALSE, FALSE, sizeof(guint));
    mailbox->sort_field = -1;	/* Initially invalid. */
    mailbox->disconnected = FALSE;

    mailbox->expunged_seqnos = g_array_new(FALSE, FALSE, sizeof(guint));
    mailbox->expunged_idle_id = 0;
}

static void
libbalsa_mailbox_imap_dispose(GObject * object)
{
    LibBalsaMailboxImap *mimap;
    LibBalsaServer *server;

    mimap = LIBBALSA_MAILBOX_IMAP(object);
    server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(object);
    if (server != NULL) {
        g_signal_handlers_disconnect_matched(server,
                                             G_SIGNAL_MATCH_DATA, 0,
                                             (GQuark) 0, NULL, NULL,
                                             mimap);
    }

    if (mimap->unread_update_id != 0) {
        g_source_remove(mimap->unread_update_id);
        mimap->unread_update_id = 0;
    }

    if (mimap->expunged_idle_id != 0) {
        /* Should have been removed at close time, but to be on the safe
         * side: */
        g_source_remove(mimap->expunged_idle_id);
        mimap->expunged_idle_id = 0;
    }

    G_OBJECT_CLASS(libbalsa_mailbox_imap_parent_class)->dispose(object);
}

static void
libbalsa_mailbox_imap_finalize(GObject * object)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(object);

    g_free(mimap->path);
    g_array_free(mimap->sort_ranks, TRUE);
    g_array_free(mimap->expunged_seqnos, TRUE);
    g_list_free_full(mimap->acls, (GDestroyNotify) imap_user_acl_free);
    if (mimap->icm != NULL)
        imap_cache_manager_free(mimap->icm);

    G_OBJECT_CLASS(libbalsa_mailbox_imap_parent_class)->finalize(object);
}

LibBalsaMailbox*
libbalsa_mailbox_imap_new(void)
{
    LibBalsaMailbox *mailbox;
    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_IMAP, NULL);

    return mailbox;
}

/* libbalsa_mailbox_imap_update_url:
   this is to be used only by mailboxImap functions, with exception
   for the folder scanner, which has to go around libmutt limitations.
*/
void
libbalsa_mailbox_imap_update_url(LibBalsaMailboxImap* mailbox)
{
    LibBalsaServer *s = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
    gchar *url;

    url = libbalsa_imap_url(s, mailbox->path);
    libbalsa_mailbox_set_url(LIBBALSA_MAILBOX(mailbox), url);
    g_free(url);
}

void
libbalsa_mailbox_imap_set_path(LibBalsaMailboxImap* mailbox, const gchar* path)
{
    g_return_if_fail(mailbox);
    g_free(mailbox->path);
    mailbox->path = g_strdup(path);
    libbalsa_mailbox_imap_update_url(mailbox);
}

gboolean
libbalsa_imap_get_quota(LibBalsaMailboxImap * mailbox,
                        gulong *max_kbyte, gulong *used_kbyte)
{
    g_return_val_if_fail(LIBBALSA_MAILBOX_IMAP(mailbox), FALSE);

    return imap_mbox_get_quota(mailbox->handle, mailbox->path,
                               max_kbyte, used_kbyte) == IMR_OK;
}

/* converts ACL's to a standard RFC 4314 acl string */
static char *
imap_acl_to_str(ImapAclType acl)
{
    GString *rights;
    /* include "cd" for RFC 2086 support: */
    static const char * flags = "lrswipkxteacd";
    unsigned n;

    rights = g_string_new("");
    for (n = 0; n < strlen(flags); n++)
        if (acl & (1 << n))
            rights = g_string_append_c(rights, flags[n]);
    return g_string_free(rights, FALSE);
}

gchar *
libbalsa_imap_get_rights(LibBalsaMailboxImap * mailbox)
{
    g_return_val_if_fail(LIBBALSA_MAILBOX_IMAP(mailbox), NULL);

    if (mailbox->rights == IMAP_ACL_NONE)
        return NULL;
    else
        return imap_acl_to_str(mailbox->rights);
}

gchar **
libbalsa_imap_get_acls(LibBalsaMailboxImap * mailbox)
{
    gchar ** acls;
    guint n;
    GList *p;

    g_return_val_if_fail(LIBBALSA_MAILBOX_IMAP(mailbox), NULL);

    if (!mailbox->acls)
        return NULL;
    acls = g_new0(char *, 2 * g_list_length(mailbox->acls) + 1);
    n = 0;
    for (p = g_list_first(mailbox->acls); p; p = g_list_next(p), n += 2) {
        acls[n] = g_strdup(((ImapUserAclType *)p->data)->uid);
        acls[n + 1] = imap_acl_to_str(((ImapUserAclType *)p->data)->acl);
    }
    return acls;
}

const gchar*
libbalsa_mailbox_imap_get_path(LibBalsaMailboxImap * mailbox)
{
    return mailbox->path;
}

static void
server_host_settings_changed_cb(LibBalsaServer * server,
				LibBalsaMailbox * mailbox)
{
    libbalsa_mailbox_imap_update_url(LIBBALSA_MAILBOX_IMAP(mailbox));
}

static gchar*
get_cache_dir(gboolean is_persistent)
{
    gchar *fname;
    if (is_persistent) {
        fname = g_build_filename(g_get_user_cache_dir(), "balsa", "imap-cache", NULL);
    } else {
        fname = g_strconcat(g_get_tmp_dir(),
                            G_DIR_SEPARATOR_S "balsa-",
                            g_get_user_name(), NULL);
    }

    return fname;
}

static gchar*
get_header_cache_path(LibBalsaMailboxImap *mimap)
{
    LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(mimap);
    LibBalsaServer *server = libbalsa_mailbox_remote_get_server(remote);
    gchar *cache_dir;
    gchar *header_file;
    gchar *encoded_path;

    header_file = g_strdup_printf("%s@%s-%s-%u-headers2",
                                  libbalsa_server_get_user(server),
                                  libbalsa_server_get_host(server),
                                  (mimap->path != NULL ? mimap->path : "INBOX"),
                                  mimap->uid_validity);
    encoded_path = libbalsa_urlencode(header_file);
    g_free(header_file);

    cache_dir = get_cache_dir(TRUE); /* FIXME */
    header_file = g_build_filename(cache_dir, encoded_path, NULL);
    g_free(encoded_path);
    g_free(cache_dir);

    return header_file;
}

static gchar**
get_cache_name_pair(LibBalsaMailboxImap *mimap, const gchar *type,
                    ImapUID uid)
{
    LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(mimap);
    LibBalsaServer *server = libbalsa_mailbox_remote_get_server(remote);
    LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
    gboolean is_persistent = libbalsa_imap_server_has_persistent_cache(imap_server);
    gchar **res = g_malloc(3*sizeof(gchar*));
    ImapUID uid_validity = mimap->uid_validity;
    gchar *fname;

    res[0] = get_cache_dir(is_persistent);
    fname = g_strdup_printf("%s@%s-%s-%u-%u-%s",
                            libbalsa_server_get_user(server),
                            libbalsa_server_get_host(server),
                            (mimap->path != NULL ? mimap->path : "INBOX"),
                            uid_validity, uid, type);
    res[1] = libbalsa_urlencode(fname);
    g_free(fname);
    res[2] = NULL;

    return res;
}

/* clean_cache:
   removes unused entries from the cache file.
*/
struct file_info {
    char  *name;
    off_t  size;
    time_t time;
};
static gint
cmp_by_time (gconstpointer  a, gconstpointer  b)
{
    return ((const struct file_info*)b)->time
        -((const struct file_info*)a)->time;
}

static void
clean_dir(const char *dir_name, off_t cache_size)
{
    GDir* dir;
    const gchar *entry;
    GList *list, *lst;
    off_t sz;

    dir = g_dir_open(dir_name, 0U, NULL);	/* do not notify the user about errors */
    if (!dir)
        return;

    list = NULL;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        struct stat st;
        struct file_info *fi;
        gchar *fname = g_build_filename(dir_name, entry, NULL);

        if(stat(fname, &st) == -1 || !S_ISREG(st.st_mode)) {
	    g_free(fname);
            continue;
	}
        fi = g_new(struct file_info,1);
        fi->name = fname;
        fi->size = st.st_size;
        fi->time = st.st_atime;
        list = g_list_prepend(list, fi);
    }
    g_dir_close(dir);

    list = g_list_sort(list, cmp_by_time);
    sz = 0;
    for(lst = list; lst; lst = lst->next) {
        struct file_info *fi = (struct file_info*)(lst->data);
        sz += fi->size;
        if(sz>cache_size) {
            g_debug("removing %s", fi->name);
            unlink(fi->name);
        }
        g_free(fi->name);
        g_free(fi);
    }
    g_list_free(list);
}

static gboolean
clean_cache(LibBalsaMailbox* mailbox)
{
    LibBalsaServer *s= LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
    gboolean is_persistent =
        libbalsa_imap_server_has_persistent_cache(LIBBALSA_IMAP_SERVER(s));
    gchar* dir;

    dir = get_cache_dir(is_persistent);
    clean_dir(dir, ImapCacheSize);
    g_free(dir);
 
    return TRUE;
}

static struct ImapCacheManager*imap_cache_manager_new_from_file(const char *header_cache_path);
static struct ImapCacheManager *icm_store_cached_data(ImapMboxHandle *h);
static void icm_restore_from_cache(ImapMboxHandle *h,
                                   struct ImapCacheManager *icm);
static gboolean icm_save_to_file(struct ImapCacheManager *icm,
				 const gchar *path);

static ImapResult
mi_reconnect(ImapMboxHandle *h)
{
    struct ImapCacheManager *icm = icm_store_cached_data(h);
    ImapResult r;
    unsigned old_cnt = imap_mbox_handle_get_exists(h);
    unsigned old_next = imap_mbox_handle_get_uidnext(h);

    r = imap_mbox_handle_reconnect(h, NULL);
    if(r==IMAP_SUCCESS) icm_restore_from_cache(h, icm);
    imap_cache_manager_free(icm);
    if(imap_mbox_handle_get_exists(h) != old_cnt ||
       imap_mbox_handle_get_uidnext(h) != old_next)
	g_signal_emit_by_name(h, "exists-notify", 0);
    return r;
}
/* ImapIssue macro handles reconnecting. We might issue a
   LIBBALSA_INFORMATION_MESSAGE here but it would be overwritten by
   login information... */
#define II(rc,h,ctx,name,line) \
   {int trials=2;do{\
    if(imap_mbox_is_disconnected(h) &&mi_reconnect(h)!=IMAP_SUCCESS)\
        {rc=IMR_NO;break;};\
    rc=line; \
    if(imap_handle_op_cancelled(h))\
        break;\
    else if(rc==IMR_SEVERED)                             \
    libbalsa_information(LIBBALSA_INFORMATION_WARNING, \
    /* Translators: #1 context (mailbox, server); #2 mailbox or server name */ \
    _("IMAP %s %s: connection has been severed. Reconnecting…"), ctx, name); \
    else if(rc==IMR_PROTOCOL)                               \
    libbalsa_information(LIBBALSA_INFORMATION_WARNING, \
    /* Translators: #1 context (mailbox, server); #2 mailbox or server name */ \
    _("IMAP %s %s: protocol error. Try enabling bug workarounds."), ctx, name);\
    else if(rc==IMR_BYE) {char *text = imap_mbox_handle_get_last_msg(h); \
    libbalsa_information(LIBBALSA_INFORMATION_WARNING, \
    /* Translators: #1 context (mailbox, server); #2 mailbox or server name; #3 error message */ \
    _("IMAP %s %s: server has shut down the connection: %s. Reconnecting…"), ctx, name, text); \
    g_free(text);}\
    else break;}while(trials-->0);}

/* helper macro for calling II in the context of a mailbox */
#define II_mbx(rc, h, mbx, line)	II(rc, h, _("mailbox"), libbalsa_mailbox_get_name(mbx), line)

static ImapMboxHandle *
libbalsa_mailbox_imap_get_handle(LibBalsaMailboxImap *mimap, GError **err)
{

    g_return_val_if_fail(LIBBALSA_MAILBOX_IMAP(mimap), NULL);

    if(!mimap->handle) {
        LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mimap);
        LibBalsaImapServer *imap_server;
        if (!LIBBALSA_IS_IMAP_SERVER(server))
            return NULL;
        imap_server = LIBBALSA_IMAP_SERVER(server);
        mimap->handle = libbalsa_imap_server_get_handle(imap_server, err);
	mimap->handle_refs = 1;
    } else
	++mimap->handle_refs;

    return mimap->handle;
}

#define RELEASE_HANDLE(mailbox,handle) \
    libbalsa_imap_server_release_handle( \
		LIBBALSA_IMAP_SERVER(LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox)),\
		handle)

static void
lbimap_update_flags(LibBalsaMessage *message, ImapMessage *imsg)
{
    LibBalsaMessageFlag flags = 0;

    if (!IMSG_FLAG_SEEN(imsg->flags))
        flags |= LIBBALSA_MESSAGE_FLAG_NEW;
    if (IMSG_FLAG_DELETED(imsg->flags))
        flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
    if (IMSG_FLAG_FLAGGED(imsg->flags))
        flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
    if (IMSG_FLAG_ANSWERED(imsg->flags))
        flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
    if (IMSG_FLAG_RECENT(imsg->flags))
        flags |= LIBBALSA_MESSAGE_FLAG_RECENT;

    libbalsa_message_set_flags(message, flags);
}

/* mi_get_imsg is a thin wrapper around imap_mbox_handle_get_msg().
   We wrap around imap_mbox_handle_get_msg() in case the libimap data
   was invalidated by eg. disconnect.
*/
struct collect_seq_data {
    unsigned *msgno_arr;
    unsigned cnt;
    unsigned needed_msgno;
    unsigned has_it;
};

static const unsigned MAX_CHUNK_LENGTH = 20; 
static gboolean
collect_seq_cb(GNode *node, gpointer data)
{
    /* We prefetch envelopes in chunks to save on RTTs.
     * Try to get the messages both before and after the message. */
    struct collect_seq_data *csd = (struct collect_seq_data*)data;
    unsigned msgno = GPOINTER_TO_UINT(node->data);
    if(msgno==0) /* root node */
        return FALSE;
    csd->msgno_arr[(csd->cnt++) % MAX_CHUNK_LENGTH] = msgno;
    if(csd->has_it>0) csd->has_it++;
    if(csd->needed_msgno == msgno)
        csd->has_it = 1;
    /* quit if we have enough messages and at least half of them are
     * after message in question. */
    return csd->cnt >= MAX_CHUNK_LENGTH && csd->has_it*2>MAX_CHUNK_LENGTH;
}

static int
cmp_msgno(const void* a, const void *b)
{
    return (*(unsigned*)a) - (*(unsigned*)b);
}

static ImapMessage*
mi_get_imsg(LibBalsaMailboxImap *mimap, unsigned msgno)
{
    ImapMessage* imsg;
    struct collect_seq_data csd;
    ImapResponse rc;
    GNode *msg_tree;

    /* This test too weak: I can imagine unsolicited ENVELOPE
     * responses sent from server that wil create the ImapMessage
     * structure but message size or UID etc will not be available. */
    if( (imsg = imap_mbox_handle_get_msg(mimap->handle, msgno)) 
        != NULL && imsg->envelope) return imsg;
    csd.needed_msgno = msgno;
    csd.msgno_arr    = g_malloc(MAX_CHUNK_LENGTH*sizeof(csd.msgno_arr[0]));
    csd.cnt          = 0;
    csd.has_it       = 0;

    msg_tree = libbalsa_mailbox_get_msg_tree(LIBBALSA_MAILBOX(mimap));
    if (msg_tree != NULL) {
        g_node_traverse(msg_tree,
                        G_PRE_ORDER, G_TRAVERSE_ALL, -1, collect_seq_cb,
                        &csd);
        if(csd.cnt>MAX_CHUNK_LENGTH) csd.cnt = MAX_CHUNK_LENGTH;
        qsort(csd.msgno_arr, csd.cnt, sizeof(csd.msgno_arr[0]), cmp_msgno);
    } else {
        /* It may happen that we want to perform an automatic
           operation on a mailbox without view (like filtering on
           reception). The searching will be done server side but
           current eg. _copy() instructions will require that
           LibBalsaMessage object are present, and these require that
           some basic information is fetched from the server.  */
        unsigned i, total_msgs = mimap->messages_info->len;
        csd.cnt = msgno+MAX_CHUNK_LENGTH>total_msgs
            ? total_msgs-msgno+1 : MAX_CHUNK_LENGTH;
        for(i=0; i<csd.cnt; i++) csd.msgno_arr[i] = msgno+i;
    }
    II_mbx(rc,mimap->handle,LIBBALSA_MAILBOX(mimap),
       imap_mbox_handle_fetch_set(mimap->handle, csd.msgno_arr,
                                  csd.cnt,
                                  IMFETCH_FLAGS |
                                  IMFETCH_UID |
                                  IMFETCH_ENV |
                                  IMFETCH_RFC822SIZE |
                                  IMFETCH_CONTENT_TYPE));
    g_free(csd.msgno_arr);
    if (rc != IMR_OK)
        return FALSE;
    return imap_mbox_handle_get_msg(mimap->handle, msgno);
}

/* Forward reference. */
static void lbm_imap_get_unseen(LibBalsaMailboxImap * mimap);

/** imap_flags_cb() is called by the imap backend when flags are
   fetched. Note that we may not have yet the preprocessed data in
   LibBalsaMessage.  We ignore the info in this case.
   OBSERVE: it must not trigger any IMAP activity under NO circumstances!
*/
static gboolean
idle_unread_update_cb(LibBalsaMailbox *mailbox)
{
    glong unread;

    libbalsa_lock_mailbox(mailbox);
    unread = libbalsa_mailbox_get_unread_messages(mailbox);
    
    lbm_imap_get_unseen(LIBBALSA_MAILBOX_IMAP(mailbox));
    if(unread != libbalsa_mailbox_get_unread_messages(mailbox))
        libbalsa_mailbox_set_unread_messages_flag(mailbox,
                                                  libbalsa_mailbox_get_unread_messages(mailbox)>0);
    LIBBALSA_MAILBOX_IMAP(mailbox)->unread_update_id = 0;
    libbalsa_unlock_mailbox(mailbox);
    return FALSE;
}

static void
imap_flags_cb(unsigned cnt, const unsigned seqno[], LibBalsaMailboxImap *mimap)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mimap);
    unsigned i;

    libbalsa_lock_mailbox(mailbox);
    for(i=0; i<cnt; i++) {
        struct message_info *msg_info = 
            message_info_from_msgno(mimap, seqno[i]);
        if(msg_info && msg_info->message) {
            LibBalsaMessageFlag flags;
            LibBalsaMessageFlag new_flags;
            /* since we are talking here about updating just received,
               usually unsolicited flags from the server, we do not
               need to go to great lengths to assure that the
               connection is up. */
            ImapMessage *imsg = 
                imap_mbox_handle_get_msg(mimap->handle, seqno[i]);
            if(!imsg) continue;

            flags = libbalsa_message_get_flags(msg_info->message);
            lbimap_update_flags(msg_info->message, imsg);
            new_flags = libbalsa_message_get_flags(msg_info->message);
            if (flags == new_flags)
                continue;

	    libbalsa_mailbox_index_set_flags(mailbox, seqno[i], new_flags);
	    ++mimap->search_stamp;
        }
    }
    if (mimap->unread_update_id == 0)
        mimap->unread_update_id =
            g_idle_add((GSourceFunc)idle_unread_update_cb, mailbox);
    libbalsa_unlock_mailbox(mailbox);
}

static gboolean
imap_exists_idle(gpointer data)
{
    LibBalsaMailboxImap *mimap = (LibBalsaMailboxImap*)data;
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mimap);
    unsigned cnt;
    GNode *msg_tree;

    libbalsa_lock_mailbox(mailbox);

    mimap->sort_field = -1;	/* Invalidate. */

    if(mimap->handle && /* was it closed in meantime? */
       (cnt = imap_mbox_handle_get_exists(mimap->handle))
       != mimap->messages_info->len) {
        unsigned i;
        struct message_info a = {0};
        GNode *sibling = NULL;

        if(cnt<mimap->messages_info->len) {
            /* remove messages; we probably missed some EXPUNGE responses
               - the only sensible scenario is that the connection was
               severed. Still, we need to recover from this somehow... -
               We invalidate all the cache now. */
            g_debug("%s: expunge ignored? Had %u messages and now only %u. "
            		"Bug in the program or broken connection",
                   __func__, mimap->messages_info->len, cnt);
            for(i=0; i<mimap->messages_info->len; i++) {
                gchar *msgid;
                struct message_info *msg_info =
                    message_info_from_msgno(mimap, i+1);
                if(!msg_info)
                    continue;
                if(msg_info->message)
                    g_object_unref(msg_info->message);
                msg_info->message = NULL;
                msgid = g_ptr_array_index(mimap->msgids, i);
                if(msgid) { 
                    g_free(msgid);
                    g_ptr_array_index(mimap->msgids, i) = NULL;
                }
                libbalsa_mailbox_index_entry_clear(mailbox, i + 1);
            }
            for(i=mimap->messages_info->len; i>cnt; i--) {
                g_array_remove_index(mimap->messages_info, i-1);
                g_ptr_array_remove_index(mimap->msgids, i-1);
                libbalsa_mailbox_msgno_removed(mailbox, i);
            }
        } 

        msg_tree = libbalsa_mailbox_get_msg_tree(LIBBALSA_MAILBOX(mimap));
        if (msg_tree != NULL)
            sibling = g_node_last_child(msg_tree);
        for(i=mimap->messages_info->len+1; i <= cnt; i++) {
            g_array_append_val(mimap->messages_info, a);
            g_ptr_array_add(mimap->msgids, NULL);
            libbalsa_mailbox_msgno_inserted(mailbox, i, msg_tree,
                                            &sibling);
        }
        ++mimap->search_stamp;
        
	libbalsa_mailbox_run_filters_on_reception(mailbox);
	lbm_imap_get_unseen(LIBBALSA_MAILBOX_IMAP(mailbox));    
    }

    libbalsa_unlock_mailbox(mailbox);
    g_object_unref(mailbox);

    return FALSE;
}

static void
imap_exists_cb(ImapMboxHandle *handle, LibBalsaMailboxImap *mimap)
{
    g_idle_add(imap_exists_idle, g_object_ref(mimap));
}

static gboolean
imap_expunge_idle(gpointer user_data)
{
    LibBalsaMailboxImap *mimap = user_data;
    guint j;

    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mimap);

    libbalsa_lock_mailbox(mailbox);

    for (j = 0; j < mimap->expunged_seqnos->len; j++) {
        guint seqno = g_array_index(mimap->expunged_seqnos, guint, j);
        struct message_info *msg_info;
        guint i;

        libbalsa_mailbox_msgno_removed(mailbox, seqno);

        msg_info = message_info_from_msgno(mimap, seqno);
        if (msg_info != NULL) {
            if (msg_info->message != NULL)
                g_object_unref(msg_info->message);
            g_array_remove_index(mimap->messages_info, seqno - 1);
        }

        if (seqno <= mimap->msgids->len) {
            gchar *msgid;

            msgid = g_ptr_array_index(mimap->msgids, seqno - 1);
            g_free(msgid);
            g_ptr_array_remove_index(mimap->msgids, seqno - 1);
        }

        for (i = seqno - 1; i < mimap->messages_info->len; i++) {
            struct message_info *info =
                &g_array_index(mimap->messages_info, struct message_info, i);

            g_assert(info != NULL);
            if (info->message != NULL)
                libbalsa_message_set_msgno(info->message, i + 1);
        }
    }

    ++mimap->search_stamp;
    mimap->sort_field = -1;     /* Invalidate. */

    mimap->expunged_idle_id = 0;
    g_array_set_size(mimap->expunged_seqnos, 0);
    libbalsa_unlock_mailbox(mailbox);

    return G_SOURCE_REMOVE;
}

static void
imap_expunge_cb(ImapMboxHandle *handle, unsigned seqno,
                LibBalsaMailboxImap *mimap)
{
    ImapMessage *imsg;
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mimap);

    libbalsa_lock_mailbox(mailbox);

    /* Use imap_mbox_handle_get_msg(mimap->handle, seqno)->uid, not
     * IMAP_MESSAGE_UID(msg_info->message), as the latter may try to
     * fetch the message from the server. */
    if ((imsg = imap_mbox_handle_get_msg(mimap->handle, seqno))) {
	gchar **pair = get_cache_name_pair(mimap, "body", imsg->uid);
        gchar *fn = g_build_filename(pair[0], pair[1], NULL);
        unlink(fn); /* ignore error; perhaps the message 
                     * was not in the cache.  */
        g_free(fn);
        g_strfreev(pair);
    }

    g_array_append_val(mimap->expunged_seqnos, seqno);
    if (mimap->expunged_idle_id == 0)
        mimap->expunged_idle_id = g_idle_add(imap_expunge_idle, mimap);

    libbalsa_unlock_mailbox(mailbox);
}

static void
libbalsa_mailbox_imap_release_handle(LibBalsaMailboxImap * mimap)
{
    g_assert(mimap->handle != NULL);
    g_assert(mimap->handle_refs > 0);

    if (--mimap->handle_refs == 0) {
	/* Only selected handles have these signal handlers, but we'll
	 * disconnect them anyway. */
	g_signal_handlers_disconnect_matched(mimap->handle,
					     G_SIGNAL_MATCH_DATA,
					     0, 0, NULL, NULL, mimap);
        imap_handle_set_flagscb(mimap->handle, NULL, NULL);
	RELEASE_HANDLE(mimap, mimap->handle);
	mimap->handle = NULL;
    }
}

static ImapMboxHandle *
libbalsa_mailbox_imap_get_selected_handle(LibBalsaMailboxImap *mimap,
					  GError **err)
{
    LibBalsaServer *server;
    LibBalsaImapServer *imap_server;
    ImapResponse rc;
    unsigned uidval;
    gboolean readonly = FALSE;

    server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mimap);
    if (!LIBBALSA_IS_IMAP_SERVER(server))
	return NULL;
    imap_server = LIBBALSA_IMAP_SERVER(server);
    if(!mimap->handle) {
        mimap->handle = 
	    libbalsa_imap_server_get_handle_with_user(imap_server,
						      mimap, err);
        if (!mimap->handle)
            return NULL;
    }
    II_mbx(rc,mimap->handle,LIBBALSA_MAILBOX(mimap),
       imap_mbox_select(mimap->handle, mimap->path, &readonly));
    libbalsa_mailbox_set_readonly(LIBBALSA_MAILBOX(mimap), readonly);
    if (rc != IMR_OK) {
	gchar *msg = imap_mbox_handle_get_last_msg(mimap->handle);
	g_set_error(err, LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_OPEN_ERROR,
		    "%s", msg);
	g_free(msg);
	RELEASE_HANDLE(mimap, mimap->handle);
        mimap->handle = NULL;
	return NULL;
    }

    /* check if we have RFC 4314 acl's for the selected mailbox */
    if (imap_mbox_get_my_rights(mimap->handle, &mimap->rights, FALSE) ==
        IMR_OK) {
        if (!IMAP_RIGHTS_CAN_WRITE(mimap->rights))
            libbalsa_mailbox_set_readonly(LIBBALSA_MAILBOX(mimap), TRUE);
        if (mimap->rights & IMAP_ACL_ADMIN)
            imap_mbox_get_acl(mimap->handle, mimap->path, &mimap->acls);
    }

    /* test validity */
    uidval = imap_mbox_handle_get_validity(mimap->handle);
    if (mimap->uid_validity != uidval) {
	mimap->uid_validity = uidval;
	/* FIXME: update/remove msg uids */
    }

    imap_handle_set_flagscb(mimap->handle, (ImapFlagsCb)imap_flags_cb, mimap);
    g_signal_connect(mimap->handle,
                     "exists-notify", G_CALLBACK(imap_exists_cb),
                     mimap);
    g_signal_connect(mimap->handle,
                     "expunge-notify", G_CALLBACK(imap_expunge_cb),
                     mimap);
    mimap->handle_refs = 1;
    return mimap->handle;
}

/* Get the list of unseen messages from the server and set
 * the unread-messages count. */
static void
lbm_imap_get_unseen(LibBalsaMailboxImap * mimap)
{
    LibBalsaMailbox *mailbox;
    guint i, count, total;
    guint first_unread;

    if (!mimap->handle)
	return;

    mailbox = LIBBALSA_MAILBOX(mimap);
    total = imap_mbox_handle_get_exists(mimap->handle);
    first_unread = total;
    for(i=count=0; i<total; i++) {
        if(imap_mbox_handle_msgno_has_flags(mimap->handle,
                                            i+1,
                                            0, IMSGF_SEEN|IMSGF_DELETED)) {
            count++;
            if (first_unread > i)
                first_unread = i + 1;
        }
    }
    if (count == 0)
        first_unread = 0;

    libbalsa_mailbox_set_first_unread(mailbox, first_unread);
    libbalsa_mailbox_clear_unread_messages(mailbox);
    libbalsa_mailbox_add_to_unread_messages(mailbox, count);
    libbalsa_mailbox_set_unread_messages_flag(mailbox, count > 0);
}

/* libbalsa_mailbox_imap_open:
   opens IMAP mailbox. On failure leaves the object in sane state.
*/
static gboolean
libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox, GError **err)
{
    LibBalsaMailboxImap *mimap;
    unsigned i;
    guint total_messages;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    mimap->handle = libbalsa_mailbox_imap_get_selected_handle(mimap, err);
    if (!mimap->handle) {
        mimap->opened       = FALSE;
        mimap->disconnected = TRUE;
	return FALSE;
    }

    mimap->opened       = TRUE;
    mimap->disconnected = FALSE;
    total_messages = imap_mbox_handle_get_exists(mimap->handle);
    mimap->messages_info = g_array_sized_new(FALSE, TRUE,
					     sizeof(struct message_info),
					     total_messages);
    mimap->msgids = g_ptr_array_sized_new(total_messages);
    for(i=0; i < total_messages; i++) {
	struct message_info a = {0};
	g_array_append_val(mimap->messages_info, a);
	g_ptr_array_add(mimap->msgids, NULL);
    }
    if (mimap->icm == NULL) { /* Try restoring from file... */
	gchar *header_cache_path = get_header_cache_path(mimap);
	mimap->icm = imap_cache_manager_new_from_file(header_cache_path);
	g_free(header_cache_path);
    }
    if (mimap->icm != NULL) {
        icm_restore_from_cache(mimap->handle, mimap->icm);
        imap_cache_manager_free(mimap->icm);
        mimap->icm = NULL;
    }

    libbalsa_mailbox_set_first_unread(mailbox,
                                      imap_mbox_handle_first_unseen(mimap->handle));
    libbalsa_mailbox_run_filters_on_reception(mailbox);
    lbm_imap_get_unseen(mimap);
    if (mimap->search_stamp)
	++mimap->search_stamp;
    else
	mimap->search_stamp = libbalsa_mailbox_get_stamp(mailbox);

    g_debug("%s: Opening %s Refcount: %d",
	    __func__, libbalsa_mailbox_get_name(mailbox),
            libbalsa_mailbox_get_open_ref(mailbox));
    return TRUE;
}

static void
free_messages_info(LibBalsaMailboxImap * mbox)
{
    guint i;
    GArray *messages_info = mbox->messages_info;

    if(messages_info->len != mbox->msgids->len)
	g_warning("free_messages_info: array sizes do not match.");
    for (i = 0; i < messages_info->len; i++) {
	gchar *msgid;
	struct message_info *msg_info =
	    &g_array_index(messages_info, struct message_info, i);
        if (msg_info->message != NULL) {
            libbalsa_message_set_mailbox(msg_info->message, NULL);
	    g_object_unref(msg_info->message);
	}
	msgid = g_ptr_array_index(mbox->msgids, i);
	if(msgid) g_free(msgid);
    }
    g_array_free(mbox->messages_info, TRUE);
    mbox->messages_info = NULL;
    g_ptr_array_free(mbox->msgids, TRUE);
    mbox->msgids = NULL;
}

static void
libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox, gboolean expunge)
{
    LibBalsaMailboxRemote *remote   = LIBBALSA_MAILBOX_REMOTE(mailbox);
    LibBalsaServer *server          = libbalsa_mailbox_remote_get_server(remote);
    LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
    gboolean is_persistent = libbalsa_imap_server_has_persistent_cache(imap_server);
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    mimap->opened = FALSE;
    mimap->icm = icm_store_cached_data(mimap->handle);

    /* we do not attempt to reconnect here */
    if (expunge) {
	if (is_persistent) { /* We appreciate expunge info to simplify
				next resync. */
	    imap_mbox_expunge_a(mimap->handle);
	}
	imap_mbox_close(mimap->handle);
    } else
	imap_mbox_unselect(mimap->handle);

    /* We have received last notificiations, we can save the cache now. */
    if(is_persistent) {
	/* Implement only for persistent. Cache dir is shared for all
	   non-persistent caches. */
	gchar *header_file = get_header_cache_path(mimap);
	icm_save_to_file(mimap->icm, header_file);
	g_free(header_file);
    }
    clean_cache(mailbox);

    if (mimap->expunged_idle_id != 0) {
        g_source_remove(mimap->expunged_idle_id);
        mimap->expunged_idle_id = 0;
        g_array_set_size(mimap->expunged_seqnos, 0);
    }

    free_messages_info(mimap);
    libbalsa_mailbox_imap_release_handle(mimap);
    mimap->sort_field = -1;	/* Invalidate. */

    libbalsa_mailbox_set_view_filter(mailbox, NULL, FALSE);
}

static FILE*
get_cache_stream(LibBalsaMailboxImap *mimap, guint uid, gboolean peek)
{
    FILE *stream;
    gchar **pair, *path;

    pair = get_cache_name_pair(mimap, "body", uid);
    path = g_build_filename(pair[0], pair[1], NULL);
    stream = fopen(path, "rb");
    if(!stream) {
        FILE *cache;
	ImapResponse rc;

        g_mkdir_with_parents(pair[0], S_IRUSR|S_IWUSR|S_IXUSR);
#if 0
        if(msg->length>(signed)SizeMsgThreshold)
            libbalsa_information(LIBBALSA_INFORMATION_MESSAGE, 
                                 _("Downloading %ld kB"),
                                 msg->length/1024);
#endif
        cache = fopen(path, "wb");
        if(cache) {
	    int ferr;
            II_mbx(rc,mimap->handle,LIBBALSA_MAILBOX(mimap),
               imap_mbox_handle_fetch_rfc822_uid(mimap->handle, uid, peek, cache));
	    ferr = ferror(cache);
            fclose(cache);
	    if(ferr || rc != IMR_OK) {
		g_debug("Error fetching RFC822 message, removing cache.");
		unlink(path);
	    }
        }
	stream = fopen(path,"rb");
    }
    g_free(path); 
    g_strfreev(pair);
    return stream;
}

/* libbalsa_mailbox_imap_get_message_stream: 
   Fetch data from cache first, if available.
   When calling imap_fetch_message(), we make use of fact that
   imap_fetch_message doesn't set msg->path field.
*/
static GMimeStream *
libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox * mailbox,
					 guint msgno, gboolean peek)
{
    FILE *stream;
    ImapMessage *imsg;
    LibBalsaMailboxImap *mimap;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);

    libbalsa_lock_mailbox(mailbox);
    /* this may get called when the mailbox is being opened ie,
       open_ref==0 */
    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    if (!mimap->handle) {
        libbalsa_unlock_mailbox(mailbox);
        return NULL;
    }
    imsg = mi_get_imsg(mimap, msgno);
    
    stream = imsg ? get_cache_stream(mimap, imsg->uid, peek) : NULL;

    libbalsa_unlock_mailbox(mailbox);

    return stream ? g_mime_stream_file_new(stream) : NULL;
}

/* libbalsa_mailbox_imap_check:
   checks imap mailbox for new messages.
   Called with the mailbox locked.
*/
struct mark_info {
    const gchar *path;
    gboolean marked;
};

static void
lbm_imap_list_cb(ImapMboxHandle * handle, int delim, ImapMboxFlags flags,
                 char *folder, gpointer data)
{
    struct mark_info *info = data;

    if (strcmp(folder, info->path) == 0
        && IMAP_MBOX_HAS_FLAG(flags, IMLIST_MARKED))
        info->marked = TRUE;
}

static gboolean
lbm_imap_check(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
    ImapMboxHandle *handle;
    gulong id;

    handle = libbalsa_mailbox_imap_get_handle(mimap, NULL);
    if (!handle)
	return FALSE;

    if(libbalsa_imap_server_get_use_status(LIBBALSA_IMAP_SERVER(server))) {
        static struct ImapStatusResult info[] = {
            { IMSTAT_UNSEEN, 0 }, { IMSTAT_NONE, 0 } };
        /* cannot do status on an open mailbox */
        g_return_val_if_fail(!mimap->opened, FALSE);
        if(imap_mbox_status(handle, mimap->path, info) != IMR_OK)
            return FALSE;
        libbalsa_mailbox_imap_release_handle(mimap);
        return info[0].result > 0;
    } else {
        struct mark_info info;
        info.path = mimap->path;
        info.marked = FALSE;

        g_object_ref(handle);
        id = g_signal_connect(handle, "list-response",
                              G_CALLBACK(lbm_imap_list_cb), &info);

        if (imap_mbox_list(handle, mimap->path) != IMR_OK)
            info.marked = FALSE;

        g_signal_handler_disconnect(handle, id);
        libbalsa_mailbox_imap_release_handle(mimap);
        g_object_unref(handle);

        return info.marked;
    }
}

static void
libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox)
{
    g_assert(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    if (!MAILBOX_OPEN(mailbox)) {
        libbalsa_mailbox_set_unread_messages_flag(mailbox,
                                                  lbm_imap_check(mailbox));
	return;

    }

    if (LIBBALSA_MAILBOX_IMAP(mailbox)->handle)
	libbalsa_mailbox_imap_noop(LIBBALSA_MAILBOX_IMAP(mailbox));
    else
	g_warning("mailbox has open_ref>0 but no handle!");
}

/* Search iters */

static ImapSearchKey *lbmi_build_imap_query(const LibBalsaCondition * cond,
					    ImapSearchKey * last);
static gboolean
libbalsa_mailbox_imap_message_match(LibBalsaMailbox* mailbox, guint msgno,
				    LibBalsaMailboxSearchIter * search_iter)
{
    LibBalsaMailboxImap *mimap;
    struct message_info *msg_info;
    GHashTable *matchings;

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    msg_info = message_info_from_msgno(mimap, msgno);
    if (!msg_info)
        return FALSE;

    if (msg_info->message)
        g_object_ref(msg_info->message);
    else if (imap_mbox_handle_get_msg(mimap->handle, msgno)
             && imap_mbox_handle_get_msg(mimap->handle, msgno)->envelope)
        /* The backend has already downloaded the data, we can just
        * convert it to LibBalsaMessage. */
        libbalsa_mailbox_imap_get_message(mailbox, msgno);
    if (msg_info->message) {
        if (libbalsa_condition_can_match(search_iter->condition,
                                         msg_info->message)) {
            gboolean retval =
                libbalsa_condition_matches(search_iter->condition,
                                           msg_info->message);
            g_object_unref(msg_info->message);
            return retval;
        }
        g_object_unref(msg_info->message);
    }

    if (search_iter->stamp != mimap->search_stamp && search_iter->mailbox
	&& LIBBALSA_MAILBOX_GET_CLASS(search_iter->mailbox)->
	search_iter_free)
	LIBBALSA_MAILBOX_GET_CLASS(search_iter->mailbox)->
	    search_iter_free(search_iter);

    matchings = search_iter->user_data;
    if (!matchings) {
	ImapSearchKey* query;
	ImapResponse rc;

	matchings = g_hash_table_new(NULL, NULL);
	query = lbmi_build_imap_query(search_iter->condition, NULL);
	II_mbx(rc,mimap->handle,mailbox,
           imap_mbox_filter_msgnos(mimap->handle, query, matchings));
	imap_search_key_free(query);
	if (rc != IMR_OK) {
	    g_hash_table_destroy(matchings);
	    return FALSE;
	}
	search_iter->user_data = matchings;
	search_iter->mailbox = mailbox;
	search_iter->stamp = mimap->search_stamp;
    }

    return g_hash_table_lookup(matchings, GUINT_TO_POINTER(msgno)) != NULL;
}

static void
libbalsa_mailbox_imap_search_iter_free(LibBalsaMailboxSearchIter * iter)
{
    GHashTable *matchings = iter->user_data;

    if (matchings) {
	g_hash_table_destroy(matchings);
	iter->user_data = NULL;
    }
    /* iter->condition and iter are freed in the LibBalsaMailbox method. */
}

/* add_or_query() adds a new term to an set of eqs. that can be or-ed.
   There are at least two ways to do it:
   a). transform a and b to NOT (NOT a NOT b)
   b). transform a and b to OR a b
   We keep it simple.
*/
static ImapSearchKey*
add_or_query(ImapSearchKey *or_query, gboolean neg, ImapSearchKey *new_term)
{
    if(!or_query) return new_term;
    if(neg) {
        imap_search_key_set_next(new_term, or_query);
        return new_term;
    } else return imap_search_key_new_or(FALSE, new_term, or_query);
}

static ImapSearchKey*
lbmi_build_imap_query(const LibBalsaCondition* cond,
                      ImapSearchKey *next)
{
    gboolean neg;
    ImapSearchKey *query = NULL;
    int cnt=0;

    if(!cond) return NULL;
    neg = cond->negate;
    switch (cond->type) {
    case CONDITION_STRING:
        if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO))
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_string
                 (neg, IMSE_S_TO, cond->match.string.string, NULL));
        if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM))
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_string
                 (neg, IMSE_S_FROM, cond->match.string.string, NULL));
        if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT))
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_string
                (neg, IMSE_S_SUBJECT,cond->match.string.string, NULL));
        if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC))
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_string
                 (neg, IMSE_S_CC, cond->match.string.string, NULL));
        if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY))
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_string
                 (neg, IMSE_S_BODY, cond->match.string.string, NULL));
        if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD))
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_string
                 (neg, IMSE_S_HEADER, cond->match.string.string,
                  cond->match.string.user_header));
        if(neg && cnt>1)
            query = imap_search_key_new_not(FALSE, query);
        imap_search_key_set_next(query, next);
        break;
    case CONDITION_DATE: {
        ImapSearchKey *slo = NULL;
        if (cond->match.date.date_low)
            query  = slo = imap_search_key_new_date
                (IMSE_D_SINCE, FALSE, cond->match.date.date_low);
        if (cond->match.date.date_high) {
            query = imap_search_key_new_date
                (IMSE_D_BEFORE, FALSE, cond->match.date.date_high);
            if(slo)
                imap_search_key_set_next(query, slo);
        }
        /* this might be redundant if only one limit was specified. */
        if(query)
            query = imap_search_key_new_not(neg, query);
        break;
    }
    case CONDITION_FLAG:
        if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_flag
                 (neg, IMSGF_ANSWERED));
        if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_NEW)
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_flag
                 (!neg, IMSGF_SEEN));
        if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_DELETED)
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_flag
                 (neg, IMSGF_DELETED));
        if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
            cnt++,query = add_or_query
                (query, neg, imap_search_key_new_flag
                 (neg, IMSGF_FLAGGED));
	if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_RECENT)
	    cnt++, query = add_or_query
		(query, neg, imap_search_key_new_flag
		 (neg, IMSGF_RECENT));
        if(neg && cnt>1)
            query = imap_search_key_new_not(FALSE, query);
        imap_search_key_set_next(query, next);
        break;
    case CONDITION_AND:
        if(neg) {
            query = imap_search_key_new_not
                (TRUE, lbmi_build_imap_query
                 (cond->match.andor.left,
                  lbmi_build_imap_query
                  (cond->match.andor.right, NULL)));
            imap_search_key_set_next(query, next);
        } else
            query = lbmi_build_imap_query
                (cond->match.andor.left,
                 lbmi_build_imap_query
                 (cond->match.andor.right, next));
        break;
    case CONDITION_OR: 
        query = 
            imap_search_key_new_or
            (neg,
             lbmi_build_imap_query(cond->match.andor.left, NULL),
             lbmi_build_imap_query(cond->match.andor.right, NULL));
            imap_search_key_set_next(query, next);
        break;
    case CONDITION_NONE:
    case CONDITION_REGEX:
    default:
        break;
    }
    return query;
}

typedef struct {
    GHashTable * uids;
    GHashTable * res;
} ImapSearchData;

static void
imap_matched(unsigned uid, ImapSearchData* data)
{
    LibBalsaMessage* m = 
        g_hash_table_lookup(data->uids,GUINT_TO_POINTER(uid)); 
    if(m) 
        g_hash_table_insert(data->res, m, m);
    else
        g_debug("Could not find UID: %u in message list", uid);
}

/* Gets the messages matching the conditions via the IMAP search command
   error is put to TRUE if an error occurred
*/

GHashTable * libbalsa_mailbox_imap_get_matchings(LibBalsaMailboxImap* mbox,
						 LibBalsaCondition *ct,
						 gboolean only_recent,
						 gboolean * err)
{
    ImapSearchKey* query;
    ImapResponse rc = IMR_NO;
    ImapSearchData * cbdata;
    GHashTable *result;

    *err = FALSE;
    
    cbdata = g_new( ImapSearchData, 1 );
    cbdata->uids = g_hash_table_new(NULL, NULL); 
    cbdata->res  = g_hash_table_new(NULL, NULL);
    query = lbmi_build_imap_query(ct /* FIXME: ONLY RECENT! */, NULL);
    if (query) {
#ifdef UID_SEARCH_IMPLEMENTED
	for(msgs= LIBBALSA_MAILBOX(mbox)->message_list; msgs;
	    msgs = msgs->next){
	    LibBalsaMessage *m = LIBBALSA_MESSAGE(msgs->data);
	    ImapMessage *imsg = mi_get_imsg(mbox, m->msgno);
            if (imsg) {
                g_hash_table_insert(cbdata->uids,
				    GUINT_TO_POINTER(imsg->uid), m);
            } else
                g_warning("Msg %d out of range", m->msgno);
	}
#else	
        g_warning("Search results ignored. Fixme!");
#endif
        II_mbx(rc,mbox->handle,LIBBALSA_MAILBOX(mbox),
           imap_mbox_uid_search(mbox->handle, query,
                                (void(*)(unsigned,void*))imap_matched,
                                cbdata));
        imap_search_key_free(query);
    }
    g_hash_table_destroy(cbdata->uids);
    /* Clean up on error */
    if (rc != IMR_OK) {
	g_hash_table_destroy(cbdata->res);
	cbdata->res = NULL;
	*err = TRUE;
	g_debug("IMAP SEARCH command failed for mailbox %s, falling back to default searching method",
		libbalsa_mailbox_get_url(LIBBALSA_MAILBOX(mbox)));
    }

    result = cbdata->res;
    g_free(cbdata);

    return result;
}

/* Returns false if the conditions contain regex matches
   User must be informed that regex match on IMAP will
   be done by default filters functions hence leading to
   SLOW match
*/
gboolean libbalsa_mailbox_imap_can_match(LibBalsaMailbox  *mailbox,
					 LibBalsaCondition *condition)
{
#if 0
    GSList *cnds;
    for (cnds =  conditions->cond_list; cnds; cnds = g_slist_next(cnds)) {
	LibBalsaCondition * cnd = cnds->data;
	
	if (cnd->type==CONDITION_REGEX) return FALSE;
    }
#endif
    return TRUE;
}

static void
libbalsa_mailbox_imap_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxImap *mimap;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    libbalsa_conf_set_string("Path", mimap->path);
    libbalsa_conf_set_bool("Migrated", TRUE);

    libbalsa_server_save_config(LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox));

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_imap_parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_imap_parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxImap *mimap;
    LibBalsaMailboxRemote *remote;
    LibBalsaImapServer *imap_server;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    g_free(mimap->path);
    mimap->path = libbalsa_conf_get_string("Path");
    if (!mimap->path) {
	mimap->path = g_strdup("INBOX");
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("No path found for mailbox “%s”, "
			       "using “%s”"),
			     libbalsa_mailbox_get_name(mailbox), mimap->path);
    }

    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    imap_server = libbalsa_imap_server_new_from_config();
    libbalsa_mailbox_remote_set_server(remote, LIBBALSA_SERVER(imap_server));

    g_signal_connect(imap_server, "config-changed",
		     G_CALLBACK(server_host_settings_changed_cb),
		     (gpointer) mailbox);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_imap_parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_imap_parent_class)->load_config(mailbox, prefix);

    libbalsa_mailbox_imap_update_url(mimap);
}

/* libbalsa_mailbox_imap_noop:
 * pings the connection with NOOP for an open IMAP mailbox.
 * this keeps the connections alive.
 * new messages are loaded in exists-notify signal handler.
 *
 * FIXME: shift responsibility for keeping the connection alive to
 * LibBalsaImapServer.
 */

void
libbalsa_mailbox_imap_noop(LibBalsaMailboxImap* mimap)
{
    g_return_if_fail(mimap != NULL);

    if (mimap->handle) /* we do not attempt to reconnect here */
	if (imap_mbox_handle_noop(mimap->handle) != IMR_OK) {
	    /* FIXME: report error... */
	}
}

void
libbalsa_mailbox_imap_force_disconnect(LibBalsaMailboxImap* mimap)
{
    g_return_if_fail(mimap != NULL);

    if (mimap->handle) {/* we do not attempt to reconnect here */
        const gchar *name = libbalsa_mailbox_get_name(LIBBALSA_MAILBOX(mimap));
        g_debug("Disconnecting %s (%u)", name, (unsigned)time(NULL));
        imap_handle_force_disconnect(mimap->handle);
        g_debug("Disconnected %s (%u)", name, (unsigned)time(NULL));
    }
}

void
libbalsa_mailbox_imap_reconnect(LibBalsaMailboxImap* mimap)
{
    g_return_if_fail(mimap != NULL);

    if (mimap->handle &&
        imap_mbox_is_disconnected (mimap->handle)) {
        gboolean readonly;

        g_debug("Reconnecting %s (%u)",
                libbalsa_server_get_host(LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mimap)),
                (unsigned)time(NULL));
        if (imap_mbox_handle_reconnect(mimap->handle, &readonly) == IMAP_SUCCESS) {
        	g_debug("Reconnected %s (%u)",
                    libbalsa_server_get_host(LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mimap)),
                    (unsigned)time(NULL));
        }
        libbalsa_mailbox_set_readonly(LIBBALSA_MAILBOX(mimap), readonly);
    }
}

gboolean
libbalsa_mailbox_imap_is_connected(LibBalsaMailboxImap* mimap)
{
    return mimap->handle && !imap_mbox_is_disconnected(mimap->handle);
}

/* imap_close_all_connections:
   close all connections to leave the place cleanly.
*/
void
libbalsa_imap_close_all_connections(void)
{
    libbalsa_imap_server_close_all_connections();
}

/* libbalsa_imap_rename_subfolder:
   dir+parent determine current name. 
   folder - new name. Can be called for a closed mailbox.
 */
gboolean
libbalsa_imap_rename_subfolder(LibBalsaMailboxImap* imap,
                               const gchar *new_parent, const gchar *folder, 
                               gboolean subscribe,
                               GError **err)
{
    ImapResponse rc;
    ImapMboxHandle* handle;
    gchar *new_path;
    char delim[2];

    handle = libbalsa_mailbox_imap_get_handle(imap, NULL);
    if (!handle) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_RENAME_ERROR,
                    _("Cannot get IMAP handle"));
	return FALSE;
    }

    II_mbx(rc,handle,LIBBALSA_MAILBOX(imap),
       imap_mbox_subscribe(handle, imap->path, FALSE));
    delim[0] = imap_mbox_handle_get_delim(handle, new_parent);
    delim[1] = '\0';
    new_path = g_build_path(delim, new_parent, folder, NULL);
    rc = imap_mbox_rename(handle, imap->path, new_path);
    if (subscribe && rc == IMR_OK)
	rc = imap_mbox_subscribe(handle, new_path, TRUE);
    g_free(new_path);
    if(rc != IMR_OK) {
        gchar *msg = imap_mbox_handle_get_last_msg(handle);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_RENAME_ERROR,
                    "%s", msg);
        g_free(msg);
    }
    libbalsa_mailbox_imap_release_handle(imap);

    return rc == IMR_OK;
}

gboolean
libbalsa_imap_new_subfolder(const gchar *parent, const gchar *folder,
			    gboolean subscribe, LibBalsaServer *server,
                            GError **err)
{
    ImapResponse rc;
    ImapMboxHandle* handle;
    gchar *new_path;

    if (!LIBBALSA_IS_IMAP_SERVER(server))
	return FALSE;
    handle = libbalsa_imap_server_get_handle(LIBBALSA_IMAP_SERVER(server),
					     NULL);
    if (!handle) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_CREATE_ERROR,
                    _("Cannot get IMAP handle"));
	return FALSE;
    }
    if (parent) {
        char delim[2];
        delim[0] = imap_mbox_handle_get_delim(handle, parent);
        delim[1] = '\0';
        new_path = g_build_path(delim, parent, folder, NULL);
    } else
        new_path = g_strdup(folder);
    II(rc,handle,_("server"),libbalsa_server_get_host(server),
       imap_mbox_create(handle, new_path));
    if (subscribe && rc == IMR_OK)
	rc = imap_mbox_subscribe(handle, new_path, TRUE);
    g_free(new_path);
    if(rc != IMR_OK) {
        gchar *msg = imap_mbox_handle_get_last_msg(handle);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_CREATE_ERROR,
                    "%s", msg);
        g_free(msg);
    }

    libbalsa_imap_server_release_handle(LIBBALSA_IMAP_SERVER(server), handle);
    return rc == IMR_OK;
}

gboolean
libbalsa_imap_delete_folder(LibBalsaMailboxImap *mailbox, GError **err)
{
    ImapResponse rc;
    ImapMboxHandle* handle;

    handle = libbalsa_mailbox_imap_get_handle(mailbox, NULL);
    if (!handle)
	return FALSE;

    /* Some IMAP servers (UW2000) do not like removing subscribed mailboxes:
     * they do not remove the mailbox from the subscription list since 
     * the subscription list should be treated as a list of bookmarks,
     * not a list of physically existing mailboxes. */
    imap_mbox_subscribe(handle, mailbox->path, FALSE);
    II_mbx(rc,handle,LIBBALSA_MAILBOX(mailbox),
       imap_mbox_delete(handle, mailbox->path));
    if(rc != IMR_OK) {
        gchar *msg = imap_mbox_handle_get_last_msg(handle);
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_DELETE_ERROR,
                    "%s", msg);
        g_free(msg);
    }

    libbalsa_mailbox_imap_release_handle(mailbox);
    return rc == IMR_OK;
}

gchar *
libbalsa_imap_url(LibBalsaServer * server, const gchar * path)
{
    const gchar *user = libbalsa_server_get_user(server);
    gchar *enc = libbalsa_urlencode(user);
    gchar *url = g_strdup_printf("imap%s://%s@%s/%s",
#ifdef USE_SSL_TO_SET_IMAPS_IN_URL
                                 server->use_ssl ? "s" : "",
#else
                                 "",
#endif
                                 enc, libbalsa_server_get_host(server),
                                 path ? path : "");
    g_free(enc);

    return url;
}

gboolean
libbalsa_mailbox_imap_sync(LibBalsaMailbox * mailbox, gboolean expunge)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    gboolean res = TRUE;

    g_return_val_if_fail(mimap->opened, FALSE);
    /* we are always in sync, we need only to do expunge now and then */
    if(expunge) {
        ImapResponse rc;
        II_mbx(rc,mimap->handle,mailbox,
           imap_mbox_expunge(mimap->handle));
        res = (rc == IMR_OK);
    }
    return res;
}

static InternetAddress*
imap_address_to_gmime_mailbox(ImapAddress *addr)
{
    gchar *tmp = g_mime_utils_header_decode_text(libbalsa_parser_options(), addr->addr_spec);
    InternetAddress *address = internet_address_mailbox_new(NULL, tmp);
    g_free(tmp);
    if (addr->name) {
        tmp = g_mime_utils_header_decode_text(libbalsa_parser_options(), addr->name);
        internet_address_set_name(address, tmp);
        g_free(tmp);
    }
    return address;
}

static InternetAddressList *
internet_address_new_list_from_imap_address(ImapAddress *list,
                                            ImapAddress **tail)
{
    InternetAddress *addr;
    InternetAddressList *res;

    if (!list)
        return NULL;

    res = internet_address_list_new();
    do {
        if (list->addr_spec) {
            addr = imap_address_to_gmime_mailbox(list);
        } else {
            /* Group */
            if (list->name) {
                /* Group head */
                ImapAddress *imap_addr = NULL;
                InternetAddressList *l;
                gchar *tmp = g_mime_utils_header_decode_text(libbalsa_parser_options(), list->name);
                addr = internet_address_group_new(tmp);
                g_free(tmp);
                l = internet_address_new_list_from_imap_address(list->next,
                                                                &imap_addr);
                if (l) {
                    internet_address_group_set_members
                        (INTERNET_ADDRESS_GROUP(addr), l);
                    g_object_unref(l);
                }
                list = imap_addr;
            } else {
                /* tail */
                if (tail)
                    *tail = list;
                return res;
            }

        }
        internet_address_list_add(res, addr);
        g_object_unref(addr);

    } while (list &&  (list = list->next) != NULL);
    return res;
}

static InternetAddressList *
internet_address_new_list_from_imap_address_list(ImapAddress *list)
{
    return internet_address_new_list_from_imap_address(list, NULL);
}

/*
 * Note: We ignore the "Sender" and "Reply-To" elements from the IMAP ENVELOPE response
 * as they may not the contain the actual header values of the RFC 5322 message.  See
 * RFC 3501:
 *    If the Sender or Reply-To lines are absent in the [RFC-2822] header, or are present
 *    but empty, the server sets the corresponding member of the envelope to be the same
 *    value as the from member [...].
 */
static void
lb_set_headers(LibBalsaMessageHeaders *headers, ImapEnvelope *  envelope,
               gboolean is_embedded)
{
    headers->date = envelope->date;
    headers->from =
	internet_address_new_list_from_imap_address_list(envelope->from);
    headers->to_list =
	internet_address_new_list_from_imap_address_list(envelope->to);
    headers->cc_list =
	internet_address_new_list_from_imap_address_list(envelope->cc);
    headers->bcc_list =
	internet_address_new_list_from_imap_address_list(envelope->bcc);

    if(is_embedded) {
        headers->subject =
            g_mime_utils_header_decode_text(libbalsa_parser_options(), envelope->subject);
        libbalsa_utf8_sanitize(&headers->subject, TRUE, NULL);
    }
}

static gboolean
libbalsa_mailbox_imap_load_envelope(LibBalsaMailboxImap *mimap,
				    LibBalsaMessage *message)
{
    ImapEnvelope *envelope;
    ImapMessage* imsg;
    gchar *hdr;
    
    g_return_val_if_fail(mimap->opened, FALSE);
    imsg = mi_get_imsg(mimap, libbalsa_message_get_msgno(message));

    if(!imsg || !imsg->envelope) {/* Connection severed and and restore
                                   *  failed - deal with it! */
        g_debug("load_envelope failed!");
        return FALSE;
    }

    lbimap_update_flags(message, imsg);

    lb_set_headers(libbalsa_message_get_headers(message), imsg->envelope, FALSE);

    if ((hdr = imsg->fetched_header_fields) && *hdr && *hdr != '\r')
	libbalsa_mailbox_imap_parse_set_headers(message, hdr);

    libbalsa_message_set_length(message, imsg->rfc822size);
    envelope = imsg->envelope;
    libbalsa_message_set_subject_from_header(message, envelope->subject);

    libbalsa_message_set_in_reply_to_from_string(message, envelope->in_reply_to);
    if (envelope->message_id != NULL) {
        gchar *message_id = g_mime_utils_decode_message_id(envelope->message_id);
        libbalsa_message_set_message_id(message, message_id);
        g_free(message_id);
    }

    return TRUE;
}

/* converts the backend data to LibBalsaMessage object */
static LibBalsaMessage*
libbalsa_mailbox_imap_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;
    LibBalsaMailboxImap *mimap = (LibBalsaMailboxImap *) mailbox;

    libbalsa_lock_mailbox(mailbox);
    msg_info = message_info_from_msgno(mimap, msgno);
    if (!msg_info) {
        libbalsa_unlock_mailbox(mailbox);
        return NULL;
    }

    if (!msg_info->message) {
        LibBalsaMessage *message;

        message = libbalsa_message_new();
        libbalsa_message_set_msgno(message, msgno);
        libbalsa_message_set_mailbox(message, mailbox);
        if (libbalsa_mailbox_imap_load_envelope(mimap, message)) {
	    gchar *id;
            msg_info->message = message;
            if (libbalsa_message_is_partial(message, &id)) {
		libbalsa_mailbox_try_reassemble(mailbox, id);
		g_free(id);
	    }
	} else
            g_object_unref(message);
    }
    if (msg_info->message)
	g_object_ref(msg_info->message); /* we want to keep one copy */
    libbalsa_unlock_mailbox(mailbox);

    return msg_info->message;
}

static gboolean
libbalsa_mailbox_imap_prepare_threading(LibBalsaMailbox * mailbox,
                                        guint start)
{
    /* Nothing to do. */
    return TRUE;
}

static void
lbm_imap_construct_body(LibBalsaMessageBody *lbbody, ImapBody *imap_body)
{
    const char *str;
    g_return_if_fail(lbbody);
    g_return_if_fail(imap_body);

    switch(imap_body->media_basic) {
    case IMBMEDIA_MULTIPART:
        lbbody->body_type = LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART; break;
    case IMBMEDIA_APPLICATION:
        lbbody->body_type = LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION; break;
    case IMBMEDIA_AUDIO:
        lbbody->body_type = LIBBALSA_MESSAGE_BODY_TYPE_AUDIO; break;
    case IMBMEDIA_IMAGE:
        lbbody->body_type = LIBBALSA_MESSAGE_BODY_TYPE_IMAGE; break;
    case IMBMEDIA_MESSAGE_RFC822:
    case IMBMEDIA_MESSAGE_OTHER:
        lbbody->body_type = LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE; break;
    case IMBMEDIA_TEXT:
        lbbody->body_type = LIBBALSA_MESSAGE_BODY_TYPE_TEXT; break;
    default:
    case IMBMEDIA_OTHER:
        lbbody->body_type = LIBBALSA_MESSAGE_BODY_TYPE_OTHER; break;
    }

    switch (imap_body->content_dsp) {
    case IMBDISP_INLINE:
	lbbody->content_dsp = GMIME_DISPOSITION_INLINE; break;
    case IMBDISP_ATTACHMENT:
	lbbody->content_dsp = GMIME_DISPOSITION_ATTACHMENT; break;
    case IMBDISP_OTHER:
	lbbody->content_dsp = imap_body->content_dsp_other; break;
    }
    lbbody->content_id = imap_body->content_id;
    lbbody->content_type = imap_body_get_content_type(imap_body);
    /* get the name in the same way as g_mime_part_get_filename() does */
    str = imap_body_get_dsp_param(imap_body, "filename");
    if(!str) str = imap_body_get_param(imap_body, "name");
    if(str) {
        lbbody->filename  =
	    g_mime_utils_header_decode_text(libbalsa_parser_options(), str);
        /* GMime documents that this is a newly allocated UTF-8 string */
    }
    lbbody->charset   = g_strdup(imap_body_get_param(imap_body, "charset"));
    if(imap_body->envelope) {
        lbbody->embhdrs = g_new0(LibBalsaMessageHeaders, 1);
        lb_set_headers(lbbody->embhdrs, imap_body->envelope, TRUE);
    }
    if(imap_body->next) {
        LibBalsaMessageBody *body = libbalsa_message_body_new(lbbody->message);
        lbm_imap_construct_body(body, imap_body->next);
        lbbody->next = body;
    }
    if(imap_body->child) {
        LibBalsaMessageBody *body = libbalsa_message_body_new(lbbody->message);
        lbm_imap_construct_body(body, imap_body->child);
        lbbody->parts = body;
    }
}

static gboolean
get_struct_from_cache(LibBalsaMailbox *mailbox, LibBalsaMessage *message,
                      LibBalsaFetchFlag flags)
{
    GMimeMessage *mime_msg;
    LibBalsaMessageHeaders *headers;

    if ((mime_msg = libbalsa_message_get_mime_message(message)) == NULL) {
        gchar **pair, *filename;
        int fd;
        GMimeStream *stream, *fstream;
        GMimeFilter *filter;
        GMimeParser *mime_parser;
        LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
        ImapMessage *imsg = mi_get_imsg(mimap, libbalsa_message_get_msgno(message));

	if (imsg == NULL)
	    return FALSE;

        pair = get_cache_name_pair(mimap, "body", imsg->uid);

        filename = g_build_filename(pair[0], pair[1], NULL);
        g_strfreev(pair);
        fd = open(filename, O_RDONLY);
        if (fd == -1) {
            g_debug("%s: loading MIME message from %s failed", __func__, filename);
            g_free(filename);
            return FALSE;
        }

        stream = g_mime_stream_fs_new(fd);
        fstream = g_mime_stream_filter_new(stream);
        g_object_unref(stream);

        filter = g_mime_filter_dos2unix_new(FALSE);
        g_mime_stream_filter_add(GMIME_STREAM_FILTER(fstream), filter);
        g_object_unref(filter);

        mime_parser = g_mime_parser_new_with_stream(fstream);
        g_mime_parser_set_format(mime_parser, GMIME_FORMAT_MESSAGE);
        g_object_unref(fstream);

        g_mime_parser_set_format(mime_parser, GMIME_FORMAT_MESSAGE);
        mime_msg = g_mime_parser_construct_message(mime_parser, libbalsa_parser_options());
        g_object_unref(mime_parser);

        if (mime_msg == NULL) {
            g_debug("%s: parsing MIME message from %s failed", __func__, filename);
            g_free(filename);
            return FALSE;
        }
        g_debug("%s: loaded MIME message from %s", __func__, filename);
        g_free(filename);
        libbalsa_message_set_mime_message(message, mime_msg);
    }

    /* follow libbalsa_mailbox_local_fetch_structure here;
     * perhaps create common helper */
    headers = libbalsa_message_get_headers(message);
    if (flags & LB_FETCH_STRUCTURE) {
        LibBalsaMessageBody *body = libbalsa_message_body_new(message);
        libbalsa_message_body_set_mime_body(body,
                                            mime_msg->mime_part);
        libbalsa_message_append_part(message, body);
        libbalsa_message_headers_from_gmime(headers, mime_msg);
    }
    if (flags & LB_FETCH_RFC822_HEADERS) {
        if (headers->user_hdrs == NULL) {
            headers->user_hdrs = libbalsa_message_user_hdrs_from_gmime(mime_msg);
        }
        libbalsa_message_set_has_all_headers(message, TRUE);
    }

    g_object_unref(mime_msg);

    return TRUE;
}

static void
libbalsa_mailbox_imap_parse_set_headers(LibBalsaMessage *message,
										const gchar     *header_str)
{
	GMimeMessage *mime_msg;
	GMimeStream *stream;
	GMimeParser *parser;

	stream = g_mime_stream_mem_new_with_buffer(header_str, strlen(header_str));
	parser = g_mime_parser_new_with_stream(stream);
	mime_msg = g_mime_parser_construct_message(parser, libbalsa_parser_options());
	g_object_unref(parser);
	g_object_unref(stream);
	if (mime_msg != NULL) {
		LibBalsaMessageHeaders *headers = libbalsa_message_get_headers(message);

		libbalsa_message_headers_from_gmime(headers, mime_msg);
		if (headers->user_hdrs == NULL) {
			headers->user_hdrs = libbalsa_message_user_hdrs_from_gmime(mime_msg);
		}
		g_object_unref(mime_msg);
	} else {
		g_debug("%s: parsing header data failed", __func__);
	}
}

static gboolean
libbalsa_mailbox_imap_fetch_structure(LibBalsaMailbox *mailbox,
                                      LibBalsaMessage *message,
                                      LibBalsaFetchFlag flags)
{
    ImapResponse rc;
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    LibBalsaServer *server;
    LibBalsaMessageHeaders *headers;
    guint msgno;
    ImapFetchType ift = 0;

    g_return_val_if_fail(mimap->opened, FALSE);

    /* Work around some server bugs by fetching the RFC2822 form of
       the message. This is used also to save one RTT for one part
       messages with a part that can certainly be displayed, there is
       no reason to fetch the structure and the only part
       separately... Observe, that the only part can be in principle
       something else, like "audio", "*" - we do not prefetch such
       parts yet. Also, we save some RTTS for very small messages by
       fetching them in their entirety. */
    server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
    headers = libbalsa_message_get_headers(message);
    msgno = libbalsa_message_get_msgno(message);
    if(!imap_mbox_handle_can_do(mimap->handle, IMCAP_FETCHBODY) ||
       libbalsa_imap_server_has_bug(LIBBALSA_IMAP_SERVER(server),
                                    ISBUG_FETCH) ||
       LIBBALSA_MESSAGE_GET_LENGTH(message)<8192 ||
        (headers != NULL &&
         (headers->content_type == NULL ||
          !g_mime_content_type_is_type(headers->content_type, "multipart", "*") ||
		  g_mime_content_type_is_type(headers->content_type, "multipart", "signed") ||
		  g_mime_content_type_is_type(headers->content_type, "multipart", "encrypted"))) ){
        /* we could optimize this part a little bit: we do not need to
         * keep reopening the stream. */
        GMimeStream *stream = 
            libbalsa_mailbox_imap_get_message_stream(mailbox, msgno, FALSE);
        if(!stream) /* oops, connection broken or the message disappeared? */
            return FALSE;
        g_object_unref(stream);
    }

    if(get_struct_from_cache(mailbox, message, flags))
        return TRUE;

    if(flags & LB_FETCH_RFC822_HEADERS) ift |= IMFETCH_RFC822HEADERS;
    if(flags & LB_FETCH_STRUCTURE)      ift |= IMFETCH_BODYSTRUCT;

    II_mbx(rc, mimap->handle, mailbox,
       imap_mbox_handle_fetch_range(mimap->handle, msgno, msgno, ift));
    if(rc == IMR_OK) { /* translate ImapData to LibBalsaMessage */
        gchar *hdr;
        ImapMessage *im = imap_mbox_handle_get_msg(mimap->handle, msgno);
	/* in case of msg number discrepancies: */
        g_return_val_if_fail(im != NULL, FALSE);
        if(flags & LB_FETCH_STRUCTURE) {
            LibBalsaMessageBody *body = libbalsa_message_body_new(message);
            lbm_imap_construct_body(body, im->body);
            libbalsa_message_append_part(message, body);
        }
        if( (flags & LB_FETCH_RFC822_HEADERS) &&
            (hdr = im->fetched_header_fields) && *hdr && *hdr != '\r') {
            libbalsa_mailbox_imap_parse_set_headers(message, hdr);
            libbalsa_message_set_has_all_headers(message, TRUE);
        }
        return TRUE;
    }

    return FALSE;
}

static void
libbalsa_mailbox_imap_fetch_headers(LibBalsaMailbox *mailbox,
                                    LibBalsaMessage *message)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    ImapResponse rc;
    guint msgno;

    msgno = libbalsa_message_get_msgno(message);
    /* If message numbers are out of sync with the mail store,
     * just skip the message: */
    if (msgno > imap_mbox_handle_get_exists(mimap->handle))
        return;

    II_mbx(rc,mimap->handle,mailbox,
       imap_mbox_handle_fetch_range(mimap->handle, msgno, msgno,
                                    IMFETCH_RFC822HEADERS));
    if(rc == IMR_OK) { /* translate ImapData to LibBalsaMessage */
        const gchar *hdr;
        ImapMessage *im = imap_mbox_handle_get_msg(mimap->handle, msgno);
        if ((hdr = im->fetched_header_fields) && *hdr && *hdr != '\r')
            libbalsa_mailbox_imap_parse_set_headers(message, hdr);
    }
}

static gboolean
is_child_of(LibBalsaMessageBody *body, LibBalsaMessageBody *child,
            GString *s, gboolean modify)
{
    guint i;
    gboolean do_mod;
    for(i=1U; body; body = body->next) {
        if(body==child) {
            g_string_printf(s, "%u", i);
            return TRUE;
        }
        do_mod = !(body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE &&
                   body->parts &&
                   body->parts->body_type ==
                   LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART);
        if(is_child_of(body->parts, child, s, do_mod)){
            if(modify) {
                char buf[12];
                snprintf(buf, sizeof(buf), "%u.", i);
                g_string_prepend(s, buf);
            }
            return TRUE;
        }
        i++;
    }
    return FALSE;
}

#if 0
static void
print_structure(LibBalsaMessageBody *part, LibBalsaMessageBody* m, int ind)
{
    static const char *t[] = {
        "LIBBALSA_MESSAGE_BODY_TYPE_OTHER",
        "LIBBALSA_MESSAGE_BODY_TYPE_AUDIO",
        "LIBBALSA_MESSAGE_BODY_TYPE_APPLICATION",
        "LIBBALSA_MESSAGE_BODY_TYPE_IMAGE",
        "LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE",
        "LIBBALSA_MESSAGE_BODY_TYPE_MODEL",
        "LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART",
        "LIBBALSA_MESSAGE_BODY_TYPE_TEXT",
        "LIBBALSA_MESSAGE_BODY_TYPE_VIDEO" };

    int j,i=1;
    while(part) {
        for(j=0; j<ind; j++) putchar(' ');
        g_debug("%d: %s%s", i++, t[part->body_type],
               part == m ? " <--" : "");
        if(part->parts)
            print_structure(part->parts, m, ind+2);
        part = part->next;
    }
}
#endif
static gchar*
get_section_for(LibBalsaMessage *message, LibBalsaMessageBody *part)
{
    GString *section = g_string_new("");
    LibBalsaMessageBody *parent;

    parent = libbalsa_message_get_body_list(message);
    if (libbalsa_message_body_is_multipart(parent))
	parent = parent->parts;

    if (!is_child_of(parent, part, section, TRUE)) {
        g_warning("Internal error, part %p not found in message %p.",
                  part, message);
        g_string_free(section, TRUE);

        return g_strdup("1");
    }

    return g_string_free(section, FALSE);
}

struct part_data { char *block; unsigned pos; ImapBody *body; };
static void
append_str(unsigned seqno, const char *buf, size_t buflen, void *arg)
{
    struct part_data *dt = (struct part_data*)arg;

    if(dt->pos + buflen > dt->body->octets) {
        g_debug("IMAP server sends too much data but we just "
                "reallocate the block.");
	dt->body->octets = dt->pos + buflen;
	dt->block = g_realloc(dt->block, dt->body->octets);
    }
    memcpy(dt->block + dt->pos, buf, buflen);
    dt->pos += buflen;
}

static const char*
encoding_names(ImapBodyEncoding enc)
{
    switch(enc) {
    case IMBENC_7BIT:   return "7bit";
    default:
    case IMBENC_8BIT:   return "8bit";
    case IMBENC_BINARY: return "binary";
    case IMBENC_BASE64: return "base64";
    case IMBENC_QUOTED: return "quoted-printable";
    }
}
static LibBalsaMessageBody*
get_parent(LibBalsaMessageBody *root, LibBalsaMessageBody *part,
           LibBalsaMessageBody *parent)
{
    while(root) {
        LibBalsaMessageBody *res;
        if(root == part)
            return parent;
        if(root->parts &&
           (res = get_parent(root->parts, part, root)) != NULL)
            return res;
        root = root->next;
    }
    return NULL;
}
static gboolean
lbm_imap_get_msg_part_from_cache(LibBalsaMessage * message,
                                 LibBalsaMessageBody * part,
                                 GError **err)
{
    GMimeStream *partstream = NULL;
    gchar **pair, *part_name;
    LibBalsaMailbox *mailbox = libbalsa_message_get_mailbox(message);
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    FILE *fp;
    gchar *section;
    guint msgno = libbalsa_message_get_msgno(message);
    ImapMessage *imsg = mi_get_imsg(mimap, msgno);

    if (imsg == NULL) {
	g_set_error(err,
		    LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_ACCESS_ERROR,
		    _("Error fetching message from IMAP server: %s"), 
		    imap_mbox_handle_get_last_msg(mimap->handle));
	return FALSE;
    }

   /* look for a part cache */
    section = get_section_for(message, part);
    pair = get_cache_name_pair(mimap, "part", imsg->uid);
    part_name   = g_strconcat(pair[0], G_DIR_SEPARATOR_S,
                              pair[1], "-", section, NULL);
    fp = fopen(part_name,"rb+");
    
    if(!fp) { /* no cache element */
        struct part_data dt;
        ImapFetchBodyOptions ifbo;
        ImapResponse rc;
        LibBalsaMessageBody *parent;

        libbalsa_lock_mailbox(mailbox);
        mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
        
        dt.body  = imap_message_get_body_from_section(imsg, section);
        if(!dt.body) {
            /* This may happen if we reconnect the data dropping the
               body structures but still try refetching the
               message. This can be simulated by randomly
               disconnecting from the IMAP server. */
            g_debug("Cannot find data for section %s", section);
            g_strfreev(pair);
            return FALSE;
        }
        dt.block = g_malloc(dt.body->octets+1);
        dt.pos   = 0;
        if(dt.body->octets>SizeMsgThreshold) {
            LibBalsaServer *s = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
            gchar *hide_id;

            hide_id = g_strdup_printf("LBIMAP_LOAD_%u_%u",
                g_str_hash(libbalsa_server_get_user(s)),
                g_str_hash(libbalsa_server_get_host(s)));
            libbalsa_information_may_hide(LIBBALSA_INFORMATION_MESSAGE, hide_id,
                                 _("Downloading %u kB"),
                                 dt.body->octets/1024);
            g_free(hide_id);
        }
	/* Imap_mbox_handle_fetch_body fetches the MIME headers of the
         * section, followed by the text. We write this unfiltered to
         * the cache. The probably only exception is the main body
         * which has no headers. In this case, we have to fake them.
         * We could and probably should dump there first the headers 
         * that we have already fetched... */
        parent = get_parent(libbalsa_message_get_body_list(message), part, NULL);
        if(parent == NULL)
            ifbo = IMFB_NONE;
        else {
            if(parent->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE)
                ifbo = IMFB_HEADER;
            else
                ifbo = IMFB_MIME;
        }
        rc = IMR_OK;
        if (dt.body->octets > 0)
        II_mbx(rc,mimap->handle,mailbox,
           imap_mbox_handle_fetch_body(mimap->handle, msgno,
                                       section, FALSE, ifbo, append_str, &dt));
        libbalsa_unlock_mailbox(mailbox);
        if(rc != IMR_OK) {
            g_debug("Error fetching imap message no %u section %s",
                    msgno, section);
            g_set_error(err,
                        LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_ACCESS_ERROR,
                        _("Error fetching message from IMAP server: %s"), 
                        imap_mbox_handle_get_last_msg(mimap->handle));
            g_free(dt.block);
            g_free(section);
            g_free(part_name);
            g_strfreev(pair);
            return FALSE;
        }
        g_mkdir_with_parents(pair[0], S_IRUSR|S_IWUSR|S_IXUSR);
        fp = fopen(part_name, "wb+");
        if(!fp) {
            g_set_error(err,
                        LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_ACCESS_ERROR,
                        _("Cannot create temporary file"));
            g_free(dt.block);
            g_free(section);
            g_free(part_name);
            g_strfreev(pair);
            return FALSE;
        }
        if(ifbo == IMFB_NONE || dt.body->octets == 0) {
            fprintf(fp,"MIME-version: 1.0\r\ncontent-type: %s\r\n"
                    "Content-Transfer-Encoding: %s\r\n\r\n",
                    part->content_type ? part->content_type : "text/plain",
                    encoding_names(dt.body->encoding));
        }
        /* Carefully save number of bytes actually read from the file. */
	if (dt.pos) {
            if(fwrite(dt.block, 1, dt.pos, fp) != dt.pos
               || fflush(fp) != 0) {
            fclose(fp);
            /* we do not want to have an incomplete part in the cache
               so that the user still can try again later when the
               problem with writing (disk space?) is removed */
            unlink(part_name);
            g_set_error(err,
                        LIBBALSA_MAILBOX_ERROR, LIBBALSA_MAILBOX_ACCESS_ERROR,
                        _("Cannot write to temporary file %s"), part_name);
            g_free(dt.block);
            g_free(section);
            g_free(part_name);
            g_strfreev(pair);
            return FALSE; /* something better ? */
            }
        }
        g_free(dt.block);
	fseek(fp, 0, SEEK_SET);
    }
    partstream = g_mime_stream_file_new (fp);

    {
        GMimeParser *parser =  
            g_mime_parser_new_with_stream (partstream);
        g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);
        part->mime_part = g_mime_parser_construct_part (parser, libbalsa_parser_options());
        g_object_unref (parser);
    }
    g_object_unref (partstream);
    g_free(section);
    g_free(part_name);
    g_strfreev(pair);

    return TRUE;
}

/* Recursive helper for libbalsa_mailbox_imap_get_msg_part: ensure that
 * we have a mime_part, and if we are in a multipart/signed or
 * multipart/encrypted, ensure that all needed children are also
 * created. */
static gboolean
lbm_imap_get_msg_part(LibBalsaMessage * msg, LibBalsaMessageBody * part,
                      gboolean need_children, GMimeObject * parent_part,
                      GError **err)
{
    g_return_val_if_fail(part, FALSE);

    if (!part->mime_part) {
        GMimeContentType *type =
            g_mime_content_type_parse(libbalsa_parser_options(), part->content_type);
        if (g_mime_content_type_is_type(type, "multipart", "*")) {
            if (g_mime_content_type_is_type(type, "multipart", "signed"))
                part->mime_part =
                    GMIME_OBJECT(g_mime_multipart_signed_new());
            else if (g_mime_content_type_is_type(type, "multipart",
                                                 "encrypted"))
                part->mime_part =
                    GMIME_OBJECT(g_mime_multipart_encrypted_new());
            else
                part->mime_part = GMIME_OBJECT(g_mime_multipart_new());
            g_mime_object_set_content_type(part->mime_part, type);
        } else {
            g_object_unref(type);
            if (!lbm_imap_get_msg_part_from_cache(msg, part, err))
                return FALSE;
        }
    }

    if (parent_part) {
        /* GMime will unref and so will we. */
        g_object_ref(part->mime_part);
	g_mime_multipart_add(GMIME_MULTIPART(parent_part),
			     part->mime_part);
    }

    if (GMIME_IS_MULTIPART_SIGNED(part->mime_part)
        || GMIME_IS_MULTIPART_ENCRYPTED(part->mime_part)
        || GMIME_IS_MESSAGE_PART(part->mime_part))
        need_children = TRUE;

    if (need_children) {
	/* Get the children, if any,... */
        if (GMIME_IS_MULTIPART(part->mime_part)) {
            if(!lbm_imap_get_msg_part(msg, part->parts, TRUE,
                                      part->mime_part, err))
                return FALSE;
        }
	/* ...and siblings. */
        if(part->next &&
           !lbm_imap_get_msg_part(msg, part->next, TRUE, parent_part, err))
            return FALSE;
	/* FIXME if GMIME_IS_MESSAGE_PART? */
    }
    return GMIME_IS_PART(part->mime_part)
        || GMIME_IS_MULTIPART(part->mime_part)
	|| GMIME_IS_MESSAGE_PART(part->mime_part);
}

static gboolean
libbalsa_mailbox_imap_get_msg_part(LibBalsaMessage *msg,
                                   LibBalsaMessageBody *part,
                                   GError **err)
{
    if (part->mime_part)
        return GMIME_IS_PART(part->mime_part)
            || GMIME_IS_MULTIPART(part->mime_part)
            || GMIME_IS_MESSAGE_PART(part->mime_part);

    return lbm_imap_get_msg_part(msg, part, FALSE, NULL, err);
}

/* libbalsa_mailbox_imap_duplicate_msgnos: identify messages with same
   non-empty message-ids. An efficient implementation requires that a
   list of msgids is maintained client side. The algorithm consists of
   four phases:
   a). identify x=largest UID in the message-id hash.
   b). complete the message-id hash from (possibly new) envelopes.
   c). check for any missing message-ids by fetching the header for 
   UID>x.
   d). identify duplicates in the message hash. When a duplicate 
   is encountered, keep the one with lower MSGNO/UID.
*/
static GArray*
libbalsa_mailbox_imap_duplicate_msgnos(LibBalsaMailbox *mailbox)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    unsigned first_to_fetch = 1;
    GHashTable *dupes;
    GArray     *res;
    unsigned i;
    /* a+b. */
    for(i=mimap->msgids->len; i>=1; i--) {
	ImapMessage *imsg;
	gchar *msg_id = g_ptr_array_index(mimap->msgids, i-1);
	if(msg_id) {
            first_to_fetch = i+1;
            break;
	} else {
	    imsg = imap_mbox_handle_get_msg(mimap->handle, i);
	    if(imsg && imsg->envelope)
		g_ptr_array_index(mimap->msgids, i-1) =
		    g_strdup(imsg->envelope->message_id);
	}
    }
    /* c. */
    if(imap_mbox_complete_msgids(mimap->handle, mimap->msgids,
				 first_to_fetch) != IMR_OK)
	return NULL;
    /* d. */
    dupes = g_hash_table_new(g_str_hash, g_str_equal);
    res   = g_array_new(FALSE, FALSE, sizeof(unsigned));
    for(i=1; i<=mimap->msgids->len; i++) {
	gchar *msg_id = g_ptr_array_index(mimap->msgids, i-1);
	if(!msg_id) { /* g_warning("msgid not completed %u", i); */continue; }
	if(!*msg_id || *msg_id == '\r' || *msg_id == '\n') continue;
	if(!g_hash_table_lookup(dupes, msg_id))
	    g_hash_table_insert(dupes, msg_id, GINT_TO_POINTER(1));
	else {
	    g_array_append_val(res, i);
	}
    }
    g_hash_table_destroy(dupes);
    g_debug("total elements: %u", res->len);
    for(i=0; i<res->len; i++)
	g_debug("  %u", GPOINTER_TO_UINT(g_array_index(res, unsigned, i)));
    return res;
}

/** Adds given set of messages to given imap mailbox. 
    Method can be called on a closed mailbox.
    Called with mailbox locked.
*/
#if 0
static guint
libbalsa_mailbox_imap_add_messages(LibBalsaMailbox * mailbox,
				   LibBalsaAddMessageIterator msg_iterator,
				   void *arg, GError ** err)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    LibBalsaMessageFlag flags;
    GMimeStream *stream;
    unsigned successfully_copied = 0;

    while( msg_iterator(&flags, &stream, arg) ) {
	ImapMsgFlags imap_flags = IMAP_FLAGS_EMPTY;
	ImapResponse rc;
	GMimeStream *tmpstream;
	GMimeFilter *crlffilter;
	ImapMboxHandle *handle;
	gint outfd;
	gchar *outfile;
	GMimeStream *outstream;
	gssize len;

	if (!(flags & LIBBALSA_MESSAGE_FLAG_NEW))
	    IMSG_FLAG_SET(imap_flags, IMSGF_SEEN);
	if (flags & LIBBALSA_MESSAGE_FLAG_DELETED)
	    IMSG_FLAG_SET(imap_flags, IMSGF_DELETED);
	if (flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	    IMSG_FLAG_SET(imap_flags, IMSGF_FLAGGED);
	if (flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
	    IMSG_FLAG_SET(imap_flags, IMSGF_ANSWERED);

	tmpstream = g_mime_stream_filter_new(stream);

	crlffilter = g_mime_filter_unix2dos_new(FALSE);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(tmpstream), crlffilter);
	g_object_unref(crlffilter);

	outfd = g_file_open_tmp("balsa-tmp-file-XXXXXX", &outfile, err);
	if (outfd < 0) {
	    g_warning("Could not create temporary file: %s", (*err)->message);
	    g_object_unref(tmpstream);
	    g_object_unref(stream);
	    return successfully_copied;
	}

	handle = libbalsa_mailbox_imap_get_handle(mimap, err);
	if (!handle)
	    /* Perhaps the mailbox was closed and the authentication
	       failed or was cancelled? err is set already, we just
	       return. */
	    return successfully_copied;

	outstream = g_mime_stream_fs_new(outfd);
	libbalsa_mime_stream_shared_lock(stream);
	g_mime_stream_write_to_stream(tmpstream, outstream);
	libbalsa_mime_stream_shared_unlock(stream);
	g_object_unref(tmpstream);
	g_object_unref(stream);

	len = g_mime_stream_tell(outstream);
	g_mime_stream_reset(outstream);

	if (len > (signed) SizeMsgThreshold)
	    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
				 _("Uploading %ld kB"), (long) len / 1024);
	rc = imap_mbox_append_stream(handle, mimap->path,
				     imap_flags, outstream, len);
	if (rc != IMR_OK) {
	    gchar *msg = imap_mbox_handle_get_last_msg(handle);
	    g_set_error(err, LIBBALSA_MAILBOX_ERROR,
			LIBBALSA_MAILBOX_APPEND_ERROR, "%s", msg);
	    g_free(msg);
	}
	libbalsa_mailbox_imap_release_handle(mimap);

	g_object_unref(outstream);
	unlink(outfile);
	g_free(outfile);

	if(rc != IMR_OK)
	    return successfully_copied;

	successfully_copied++;
    }
    return successfully_copied;
}
#else

struct MultiAppendCbData {
    LibBalsaAddMessageIterator msg_iterator;
    void *iterator_data;
    GMimeStream *outstream;
    GList *outfiles;
    GError **err;
    guint copied;
};

static void
macd_clear(struct MultiAppendCbData *macd)
{
    if(macd->outstream) {
	g_object_unref(macd->outstream);
	macd->outstream = NULL;
    }
}

static void
macd_destroy(struct MultiAppendCbData *macd)
{
    GList *outmsgs;
    for(outmsgs = macd->outfiles; outmsgs; outmsgs = outmsgs->next) {
	unlink(outmsgs->data);
	g_free(outmsgs->data);
    }
    g_list_free(macd->outfiles);
}

static size_t
multi_append_cb(char * buf, size_t buflen,
		ImapAppendMultiStage stage,
		ImapMsgFlags *return_flags, void *arg)
{
    struct MultiAppendCbData *macd = (struct MultiAppendCbData*)arg;

    switch(stage) {
    case IMA_STAGE_NEW_MSG: {
	ImapMsgFlags imap_flags = IMAP_FLAGS_EMPTY;
	GMimeStream *tmpstream;
	GMimeFilter *crlffilter;
	gint outfd;
	GMimeStream *stream = NULL;
	gint64 len;
	LibBalsaMessageFlag flags;
	GError**err = macd->err;
	gchar *outf = NULL;

	macd_clear(macd);

	while( macd->msg_iterator(&flags, &stream, macd->iterator_data) &&
	       !stream)
	    ;

	if(!stream) /* No more messages to append! */
	    return 0;

	if (!(flags & LIBBALSA_MESSAGE_FLAG_NEW))
	    IMSG_FLAG_SET(imap_flags, IMSGF_SEEN);
	if (flags & LIBBALSA_MESSAGE_FLAG_DELETED)
	    IMSG_FLAG_SET(imap_flags, IMSGF_DELETED);
	if (flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	    IMSG_FLAG_SET(imap_flags, IMSGF_FLAGGED);
	if (flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
	    IMSG_FLAG_SET(imap_flags, IMSGF_ANSWERED);

	tmpstream = g_mime_stream_filter_new(stream);

        crlffilter = g_mime_filter_unix2dos_new(FALSE);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(tmpstream), crlffilter);
	g_object_unref(crlffilter);

	outfd = g_file_open_tmp("balsa-tmp-file-XXXXXX", &outf, err);
	if (outfd < 0) {
	    g_warning("Could not create temporary file: %s", (*err)->message);
	    g_object_unref(tmpstream);
	    g_object_unref(stream);
	    return 0;
	}

	macd->outstream = g_mime_stream_fs_new(outfd);
	macd->outfiles = g_list_append(macd->outfiles, outf);
	libbalsa_mime_stream_shared_lock(stream);
	g_mime_stream_write_to_stream(tmpstream, macd->outstream);
	libbalsa_mime_stream_shared_unlock(stream);
	g_object_unref(tmpstream);
	g_object_unref(stream);

	len = g_mime_stream_tell(macd->outstream);
	g_mime_stream_reset(macd->outstream);

	if (len > (signed) SizeMsgThreshold)
	    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                                 _("Uploading %ld kB"),
                                 (long int) ((len + 512) / 1024));

	*return_flags = imap_flags;
	macd->copied++;
	return g_mime_stream_length(macd->outstream);
    }
	break;
    case IMA_STAGE_PASS_DATA:
	return g_mime_stream_read(macd->outstream, buf, buflen);
    }
    g_assert_not_reached();
    return 0;
}

struct append_to_cache_data {
    const gchar *user, *host, *path, *cache_dir;
    GList *curr_name;
    unsigned uid_validity;
};

static void
create_cache_copy(const gchar *src, const gchar *cache_dir, const gchar *name)
{
    gchar *fname = libbalsa_urlencode(name);
    gchar *dst = g_build_filename(cache_dir, fname, NULL);

	if (link(src, dst) != 0) {
		/* Link failed possibly because the two caches reside on
		   different file systems. We attempt to copy the cache instead. */
		GFile *srcfile;
		GFile *dstfile;
		GError *error = NULL;

		srcfile = g_file_new_for_path(src);
		dstfile = g_file_new_for_path(dst);
		if (!g_file_copy(srcfile, dstfile, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)) {
			g_warning("%s: copy %s -> %s failed: %s", __func__, src, dst, error->message);
			g_error_free(error);
		}
		g_object_unref(srcfile);
		g_object_unref(dstfile);
    }
    g_free(fname);
    g_free(dst);
}

static void
append_to_cache(unsigned uid, void *arg)
{
    struct append_to_cache_data *atcd = (struct append_to_cache_data*)arg;
    gchar *name = g_strdup_printf("%s@%s-%s-%u-%u-%s",
				  atcd->user, atcd->host, atcd->path,
				  atcd->uid_validity,
				  uid, "body");
    gchar *msg = atcd->curr_name->data;

    atcd->curr_name = g_list_next(atcd->curr_name);

    g_return_if_fail(msg);

    create_cache_copy(msg, atcd->cache_dir, name);
    g_free(name);
}

static guint
libbalsa_mailbox_imap_add_messages(LibBalsaMailbox * mailbox,
				   LibBalsaAddMessageIterator msg_iterator,
				   void *arg, GError ** err)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    ImapMboxHandle *handle = libbalsa_mailbox_imap_get_handle(mimap, err);
    struct MultiAppendCbData macd;
    ImapResponse rc;
    ImapSequence uid_sequence;

    if (!handle) {
	/* Perhaps the mailbox was closed and the authentication
	   failed or was cancelled? err is set already, we just
	   return. */
	return 0;
    }

    macd.msg_iterator = msg_iterator;
    macd.iterator_data = arg;
    macd.outstream = NULL;
    macd.outfiles = NULL;
    macd.err = err;
    macd.copied = 0;
    imap_sequence_init(&uid_sequence);
    rc = imap_mbox_append_multi(handle,	mimap->path,
				multi_append_cb, &macd, &uid_sequence);
    libbalsa_mailbox_imap_release_handle(mimap);
    macd_clear(&macd);

    if(!imap_sequence_empty(&uid_sequence) &&
       g_list_length(macd.outfiles) == imap_sequence_length(&uid_sequence)) {
	/* Hurray, server returned UID data on appended messages! */
	LibBalsaServer *server = libbalsa_mailbox_remote_get_server(LIBBALSA_MAILBOX_REMOTE(mailbox));
	LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
	gboolean is_persistent = libbalsa_imap_server_has_persistent_cache(imap_server);
	struct append_to_cache_data atcd;
	gchar *cache_dir;

	atcd.user = libbalsa_server_get_user(server);
	atcd.host = libbalsa_server_get_host(server);
	atcd.path = mimap->path != NULL ? mimap->path : "INBOX";
	atcd.cache_dir = cache_dir = get_cache_dir(is_persistent);
	atcd.curr_name = macd.outfiles;
	atcd.uid_validity = uid_sequence.uid_validity;

	imap_sequence_foreach(&uid_sequence, append_to_cache, &atcd);
	imap_sequence_release(&uid_sequence);
	g_free(cache_dir);
    }

    macd_destroy(&macd);
    return rc == IMR_OK ? macd.copied : 0;
}
#endif

static void
transform_flags(LibBalsaMessageFlag set, LibBalsaMessageFlag clr,
                ImapMsgFlag *flg_set, ImapMsgFlag *flg_clr)
{
    *flg_set = 0;
    *flg_clr = 0;

    if (set & LIBBALSA_MESSAGE_FLAG_REPLIED)
        *flg_set |= IMSGF_ANSWERED;
    if (clr & LIBBALSA_MESSAGE_FLAG_REPLIED)
	*flg_clr |= IMSGF_ANSWERED;
    if (set & LIBBALSA_MESSAGE_FLAG_NEW)
	*flg_clr |= IMSGF_SEEN;
    if (clr & LIBBALSA_MESSAGE_FLAG_NEW)
	*flg_set |= IMSGF_SEEN;
    if (set & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	*flg_set |= IMSGF_FLAGGED;
    if (clr & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	*flg_clr |= IMSGF_FLAGGED;
    if (set & LIBBALSA_MESSAGE_FLAG_DELETED)
	*flg_set |= IMSGF_DELETED;
    if (clr & LIBBALSA_MESSAGE_FLAG_DELETED)
	*flg_clr |= IMSGF_DELETED;
}

static void
lbm_imap_change_user_flags(LibBalsaMailbox * mailbox, GArray * seqno,
                           LibBalsaMessageFlag set,
			   LibBalsaMessageFlag clear)
{
    gint i;
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    set   &= ~LIBBALSA_MESSAGE_FLAGS_REAL;
    clear &= ~LIBBALSA_MESSAGE_FLAGS_REAL;
    if (!set && !clear)
	return;

    for (i = seqno->len; --i >= 0;) {
	guint msgno = g_array_index(seqno, guint, i);
        struct message_info *msg_info = 
            message_info_from_msgno(mimap, msgno);
        if (msg_info) {
            msg_info->user_flags |= set;
            msg_info->user_flags &= ~clear;
        }
    }
}

static gboolean
lbm_imap_messages_change_flags(LibBalsaMailbox * mailbox, GArray * seqno,
			       LibBalsaMessageFlag set,
			       LibBalsaMessageFlag clear)
{
    ImapMsgFlag flag_set, flag_clr;
    ImapResponse rc = IMR_OK;
    ImapMboxHandle *handle = LIBBALSA_MAILBOX_IMAP(mailbox)->handle;

    if(seqno->len == 0) return TRUE;
    lbm_imap_change_user_flags(mailbox, seqno, set, clear);

    if (!((set | clear) & LIBBALSA_MESSAGE_FLAGS_REAL))
	/* No real flags. */
	return TRUE;

    g_array_sort(seqno, cmp_msgno);
    transform_flags(set, clear, &flag_set, &flag_clr);
    /* Do not use the asynchronous versions until the issues related
       to unsolicited EXPUNGE responses are resolved. The issues are
       pretty much of a theoretical character but we do not want to
       risk the mail store integrity, do we? */
    if (flag_set)
        II_mbx(rc, handle, mailbox,
           imap_mbox_store_flag(handle,
                                seqno->len, (guint *) seqno->data,
                                flag_set, TRUE));
    if (rc && flag_clr)
        II_mbx(rc, handle, mailbox,
           imap_mbox_store_flag(handle,
                                seqno->len, (guint *) seqno->data,
                                flag_clr, FALSE));
    return rc == IMR_OK;
}

static gboolean
libbalsa_mailbox_imap_msgno_has_flags(LibBalsaMailbox * m, unsigned msgno,
                                      LibBalsaMessageFlag set,
                                      LibBalsaMessageFlag unset)
{
    LibBalsaMessageFlag user_set, user_unset;
    ImapMsgFlag flag_set, flag_unset;
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(m);
    ImapMboxHandle *handle;

    user_set   = set   & ~LIBBALSA_MESSAGE_FLAGS_REAL;
    user_unset = unset & ~LIBBALSA_MESSAGE_FLAGS_REAL;
    if (user_set || user_unset) {
        struct message_info *msg_info =
            message_info_from_msgno(mimap, msgno);
        if (!msg_info ||
            (msg_info->user_flags & user_set) != user_set ||
            (msg_info->user_flags & user_unset) != 0)
            return FALSE;
    }

    transform_flags(set, unset, &flag_set, &flag_unset);
    if (!flag_set && !flag_unset)
        return TRUE;

    handle = mimap->handle;
    g_return_val_if_fail(handle, FALSE);
    return imap_mbox_handle_msgno_has_flags(handle, msgno, flag_set,
                                            flag_unset);
}

static gboolean
libbalsa_mailbox_imap_can_do(LibBalsaMailbox* mbox,
                             enum LibBalsaMailboxCapability c)
{
    LibBalsaMailboxImap *mimap;
    if(!mbox)
        return TRUE;
    mimap = LIBBALSA_MAILBOX_IMAP(mbox);
    switch(c) {
#if !ENABLE_CLIENT_SIDE_SORT
    case LIBBALSA_MAILBOX_CAN_SORT:
        return imap_mbox_handle_can_do(mimap->handle, IMCAP_SORT);
#endif
    case LIBBALSA_MAILBOX_CAN_THREAD:
        return imap_mbox_handle_can_do(mimap->handle, IMCAP_THREAD_REFERENCES);
    default:
        return TRUE;
    }
}

static ImapSortKey
lbmi_get_imap_sort_key(LibBalsaMailbox *mbox)
{
    ImapSortKey key = (ImapSortKey) LB_MBOX_FROM_COL;

    switch (libbalsa_mailbox_get_view(mbox)->sort_field) {
    default:
    case LB_MAILBOX_SORT_NO:	  key = IMSO_MSGNO;   break;
    case LB_MAILBOX_SORT_SENDER:    
        key = libbalsa_mailbox_get_view(mbox)->show == LB_MAILBOX_SHOW_TO
            ? IMSO_TO : IMSO_FROM;		      break;
    case LB_MAILBOX_SORT_SUBJECT: key = IMSO_SUBJECT; break;
    case LB_MAILBOX_SORT_DATE:    key = IMSO_DATE;    break;
    case LB_MAILBOX_SORT_SIZE:    key = IMSO_SIZE;    break;
    }

    return key;
}
     
static void
libbalsa_mailbox_imap_set_threading(LibBalsaMailbox *mailbox,
				    LibBalsaMailboxThreadingType thread_type)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    GNode * new_tree = NULL;
    guint msgno;
    ImapSearchKey *filter =
        lbmi_build_imap_query(libbalsa_mailbox_get_view_filter(mailbox, FALSE), NULL);
    ImapResponse rc;
    
    libbalsa_mailbox_get_view(mailbox)->threading_type = thread_type;
    switch(thread_type) {
    case LB_MAILBOX_THREADING_SIMPLE:
    case LB_MAILBOX_THREADING_JWZ:
        II_mbx(rc,mimap->handle,mailbox,
           imap_mbox_thread(mimap->handle, "REFERENCES", filter));
        if(rc == IMR_OK) {
            new_tree =
                g_node_copy(imap_mbox_handle_get_thread_root(mimap->handle));
            break;
		} else {
			LibBalsaServer *s = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
			gchar *hide_id;

			hide_id = g_strdup_printf("LBIMAP_SSTH_%u_%u",
				g_str_hash(libbalsa_server_get_user(s)),
				g_str_hash(libbalsa_server_get_host(s)));
			libbalsa_information_may_hide(LIBBALSA_INFORMATION_WARNING,
				hide_id, _("Server-side threading not supported."));
			g_free(hide_id);
		}
        /* fall through */
    case LB_MAILBOX_THREADING_FLAT:
        if(filter) {
            II_mbx(rc,mimap->handle,mailbox,
               imap_mbox_sort_filter(mimap->handle,
                                     lbmi_get_imap_sort_key(mailbox),
                                     libbalsa_mailbox_get_view(mailbox)->sort_type ==
                                     LB_MAILBOX_SORT_TYPE_ASC,
                                     filter));
            if(rc == IMR_OK)
                new_tree =
                    g_node_copy
                    (imap_mbox_handle_get_thread_root(mimap->handle));
        }
        if(!new_tree) { /* fall back */
            new_tree = g_node_new(NULL);
            for(msgno = 1; msgno <= mimap->messages_info->len; msgno++)
                g_node_append_data(new_tree, GUINT_TO_POINTER(msgno));
        }
        break;
    default:
	g_assert_not_reached();
	new_tree = NULL;
    }
    imap_search_key_free(filter);

    libbalsa_mailbox_set_msg_tree(mailbox, new_tree);
    libbalsa_mailbox_set_messages_threaded(mailbox, TRUE);
}

static void
lbm_imap_update_view_filter(LibBalsaMailbox   *mailbox,
                            LibBalsaCondition *view_filter)
{
}

/* Sorting
 *
 * To avoid multiple server queries when the view is threaded, we sort
 * the whole mailbox, find the rank of each message, and cache the
 * result in LibBalsaMailboxImap::rank.  When libbalsa_mailbox_imap_sort()
 * is called on a subset of messages, it can use their ranks to sort
 * them.  The sort-field is also cached, so we can refresh the ranks
 * when the sort-field is changed.  The cached sort-field is invalidated
 * (set to -1) whenever the cached ranks might be out of date.
 */
static gint
lbmi_compare_func(const SortTuple * a,
		  const SortTuple * b,
		  LibBalsaMailboxImap * mimap)
{
    unsigned seqnoa, seqnob;
    int retval;
    LibBalsaMailbox *mbox = (LibBalsaMailbox *) mimap;

    seqnoa = GPOINTER_TO_UINT(a->node->data);
    g_assert(seqnoa <= mimap->sort_ranks->len);
    seqnob = GPOINTER_TO_UINT(b->node->data);
    g_assert(seqnob <= mimap->sort_ranks->len);
    retval = g_array_index(mimap->sort_ranks, guint, seqnoa - 1) -
	g_array_index(mimap->sort_ranks, guint, seqnob - 1);

    return libbalsa_mailbox_get_view(mbox)->sort_type == LB_MAILBOX_SORT_TYPE_ASC ?
        retval : -retval;
}

static void
libbalsa_mailbox_imap_sort(LibBalsaMailbox *mbox, GArray *array)
{
    LibBalsaMailboxImap *mimap;

    mimap = LIBBALSA_MAILBOX_IMAP(mbox);
    if (mimap->sort_field != libbalsa_mailbox_get_view(mbox)->sort_field) {
	/* Cached ranks are invalid. */
        unsigned *msgno_arr;
        guint i, len;

        len = mimap->messages_info->len;
        msgno_arr = g_malloc(len * sizeof(unsigned));
        for (i = 0; i < len; i++)
            msgno_arr[i] = i + 1;
        if (libbalsa_mailbox_get_view(mbox)->sort_field != LB_MAILBOX_SORT_NO) {
            ImapResponse rc;
	    /* Server-side sort of the whole mailbox. */
            II_mbx(rc, LIBBALSA_MAILBOX_IMAP(mbox)->handle, mbox,
               imap_mbox_sort_msgno(LIBBALSA_MAILBOX_IMAP(mbox)->handle,
                                    lbmi_get_imap_sort_key(mbox), TRUE,
                                    msgno_arr, len)); /* ignore errors */
        }
        g_array_set_size(mimap->sort_ranks, len);
        for (i = 0; i < len; i++)
	    g_array_index(mimap->sort_ranks, guint, msgno_arr[i] - 1) = i;
	g_free(msgno_arr);
	/* Validate the cache. */
        mimap->sort_field = libbalsa_mailbox_get_view(mbox)->sort_field;
    }
    g_array_sort_with_data(array, (GCompareDataFunc) lbmi_compare_func,
                           mimap);
}

static guint
libbalsa_mailbox_imap_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mimap = (LibBalsaMailboxImap *) mailbox;
    guint cnt;

    cnt = mimap->messages_info ? mimap->messages_info->len : 0;
    return cnt;
}

/* Copy messages in the list to dest; use server-side copy if mailbox
 * and dest are on the same server, fall back to parent method
 * otherwise.
 */
static gboolean
libbalsa_mailbox_imap_messages_copy(LibBalsaMailbox * mailbox,
				    GArray * msgnos,
				    LibBalsaMailbox * dest, GError **err)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mimap);

    if (LIBBALSA_IS_MAILBOX_IMAP(dest) && LIBBALSA_MAILBOX_REMOTE_GET_SERVER(dest) == server) {
        LibBalsaMailboxImap *mimap_dest = (LibBalsaMailboxImap *) dest;
        gboolean ret;
	ImapMboxHandle *handle = mimap->handle;
	ImapSequence uid_sequence;
	unsigned *seqno = (unsigned*)msgnos->data, *uids;
	unsigned im;
	g_return_val_if_fail(handle, FALSE);

	imap_sequence_init(&uid_sequence);
	/* User server-side copy. */
	g_array_sort(msgnos, cmp_msgno);
	uids = g_new(unsigned, msgnos->len);
	for(im=0; im<msgnos->len; im++) {
	    ImapMessage * imsg = imap_mbox_handle_get_msg(handle, seqno[im]);
	    uids[im] = imsg ? imsg->uid : 0;
	}

	ret = imap_mbox_handle_copy(handle, msgnos->len,
                                    (guint *) msgnos->data,
                                    mimap_dest->path,
				    &uid_sequence)
	    == IMR_OK;
        if(!ret) {
            gchar *msg = imap_mbox_handle_get_last_msg(handle);
            g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_COPY_ERROR,
                        "%s", msg);
            g_free(msg);
        } else if(!imap_sequence_empty(&uid_sequence)) {
	    /* Copy cache files. */
	    GDir *dir;
	    LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
	    gboolean is_persistent =
		libbalsa_imap_server_has_persistent_cache(imap_server);
	    gchar *dir_name = get_cache_dir(is_persistent);
	    gchar *src_prefix = g_strdup_printf("%s@%s-%s-%u-",
						libbalsa_server_get_user(server),
                                                libbalsa_server_get_host(server),
						(mimap->path
						 ? mimap->path : "INBOX"),
						mimap->uid_validity);
	    gchar *encoded_path = libbalsa_urlencode(src_prefix);
	    g_free(src_prefix);
	    dir = g_dir_open(dir_name, 0, NULL);
	    if (dir != NULL) {
		const gchar *filename;
		size_t prefix_length = strlen(encoded_path);
		unsigned nth;
		while ((filename = g_dir_read_name(dir)) != NULL) {
		    unsigned msg_uid;
		    gchar *tail;
		    if(strncmp(encoded_path, filename, prefix_length))
			continue;
		    msg_uid = strtol(filename + prefix_length, &tail, 10);
		    for(im = 0; im<msgnos->len; im++) {
			if(uids[im]>msg_uid) break;
			else if(uids[im]==msg_uid &&
				(nth = imap_sequence_nth(&uid_sequence, im))
				 ) {
			    gchar *src =
				g_build_filename(dir_name, filename, NULL);
			    gchar *dst_prefix =
				g_strdup_printf("%s@%s-%s-%u-%u%s",
						libbalsa_server_get_user(server),
                                                libbalsa_server_get_host(server),
						(mimap_dest->path != NULL ?
						 mimap_dest->path : "INBOX"),
						uid_sequence.uid_validity,
						nth, tail);

			    create_cache_copy(src, dir_name, dst_prefix);
			    g_free(dst_prefix);
			    break;
			}
		    }
		}
		g_dir_close(dir);
	    }
	    g_free(dir_name);
	}
	g_free(uids);
	imap_sequence_release(&uid_sequence);
        return ret;
    }

    /* Couldn't use server-side copy, fall back to default method. */
    return LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_imap_parent_class)->
        messages_copy(mailbox, msgnos, dest, err);
}

void
libbalsa_imap_set_cache_size(off_t cache_size)
{
    ImapCacheSize = cache_size;
}

/** Purges the temporary directory used for non-persistent message
   caching. */
void
libbalsa_imap_purge_temp_dir(off_t cache_size)
{
    gchar *dir_name = get_cache_dir(FALSE);
    clean_dir(dir_name, cache_size);
    g_free(dir_name);
}

/* ===================================================================
   ImapCacheManager implementation.  The main task of the
   ImapCacheManager is to reuse msgno->UID mappings. This is useful
   mostly for operations when the imap connection cannot last as long
   as it should due to external constraints (flaky connection,
   pay-by-the-minute connections). The goal is to extract any UID
   information that the low level libimap handle might have on
   connection close and provide it to the new one whenever one is
   created.

   The general scheme is that libimap does not cache any data
   persistently based on UIDs - this task is left to ImapCacheManager.
 .
   ICM provides two main functions:

   - init_on_select() - preloads ImapMboxHandle msgno-based cache with
     all available information. It may ask ImapMboxHandle to provide
     UID->msgno maps if it considers it necessary.

   - store_cached_data() - extracts all information that is known to
     ImapMboxHandle and can be potentially used in future sessions -
     mostly all ImapMessage and ImapEnvelope structures.

     Current implementation stores the information in memory but an
     implementation storing data on disk is possible, too.

 */
struct ImapCacheManager {
    GHashTable *headers;
    GArray     *uidmap;
    uint32_t    uidvalidity;
    uint32_t    uidnext;
    uint32_t    exists;
};

static struct ImapCacheManager*
imap_cache_manager_new(guint cnt)
{
    struct ImapCacheManager *icm = g_new0(struct ImapCacheManager, 1);
    icm->exists = cnt;
    icm->headers = 
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    icm->uidmap = g_array_sized_new(FALSE,  TRUE, sizeof(uint32_t), cnt);
    return icm;
}

static struct ImapCacheManager*
imap_cache_manager_new_from_file(const char *header_cache_path)
{
    /* The cache data should be transferable between 32- and 64-bit
       systems. */
    uint32_t i;
    ImapUID uid;
    struct ImapCacheManager *icm;
    FILE *f = fopen(header_cache_path, "rb");
    if(!f) {
	return NULL;
    }
    if(fread(&i, sizeof(i), 1, f) != 1) {
	g_debug("Could not read cache table size.");
        fclose(f);
	return NULL;
    }
    
    icm = imap_cache_manager_new(i);
    if(fread(&icm->uidvalidity, sizeof(uint32_t), 1, f) != 1 ||
       fread(&icm->uidnext,     sizeof(uint32_t), 1, f) != 1 ||
       fread(&icm->exists,      sizeof(uint32_t), 1, f) != 1) {
	imap_cache_manager_free(icm);
	g_debug("Couldn't read cache - aborting…");
        fclose(f);
	return NULL;
    }

    i = 0;
    while(fread(&uid, sizeof(uid), 1, f) == 1) {
	if(uid) {
	    uint32_t slen; /* Architecture-independent size */
	    gchar *s;
	    if(fread(&slen, sizeof(slen), 1, f) != 1)
		break;
	    s = g_malloc(slen+1); /* slen would be sufficient? */
	    if(fread(s, 1, slen, f) != slen) {
			g_free(s);
			break;
		}
	    s[slen] = '\0'; /* Unneeded? */
	    g_hash_table_insert(icm->headers, GUINT_TO_POINTER(uid), s);
	}
        g_array_append_val(icm->uidmap, uid);
	i++;
    }
    fclose(f);

    return icm;
}

static void
imap_cache_manager_free(struct ImapCacheManager *icm)
{
    g_hash_table_destroy(icm->headers);
    g_array_free(icm->uidmap, TRUE);
    g_free(icm);
}

/* icm_init_on_select_() preloads header cache of the ImapMboxHandle object.
   It currently handles following cases:
   a). uidvalidity different - entire cache has to be invalidated.
   b). cache->exists == h->exists && cache->uidnext == h->uidnext:
   nothing has changed - feed entire cache.
   else fetch the message numbers for the UIDs in cache.
*/
static void
set_uid(ImapMboxHandle *handle, unsigned seqno, void *arg)
{
    GArray *a = (GArray*)arg;
    g_array_append_val(a, seqno);
}

static void
icm_restore_from_cache(ImapMboxHandle *h, struct ImapCacheManager *icm)
{
    unsigned exists, uidvalidity, uidnext;
    unsigned i;

    if(!icm || ! h)
        return;
    uidvalidity = imap_mbox_handle_get_validity(h);
    exists  = imap_mbox_handle_get_exists(h);
    uidnext = imap_mbox_handle_get_uidnext(h);
    if(icm->uidvalidity != uidvalidity) {
    	g_debug("Different validities old: %u new: %u - cache invalidated",
               icm->uidvalidity, uidvalidity);
        return;
    }

    /* There were some modifications to the mailbox but the situation
     * is not hopeless, we just need to get the seqnos of messages in
     * the cache. */
    if(exists - icm->exists !=  uidnext - icm->uidnext) {
        ImapResponse rc;
        GArray *uidmap = g_array_sized_new(FALSE, TRUE,
                                           sizeof(uint32_t), icm->exists);
        ImapSearchKey *k;
        unsigned lo = icm->uidmap->len+1, hi = 0;
        g_debug("UIDSYNC:Searching range [1:%u]", icm->uidmap->len);
        for(i=1; i<=icm->uidmap->len; i++)
            if(g_array_index(icm->uidmap, uint32_t, i-1)) {lo=i; break; }
        for(i=icm->uidmap->len; i>=lo; i--)
            if(g_array_index(icm->uidmap, uint32_t, i-1)) {hi=i; break; }

        k = imap_search_key_new_range(FALSE, FALSE, lo, hi);
        g_debug("UIDSYNC: Old vs new: exists: %u %u uidnext: %u %u "
               "- syncing uid map for msgno [%u:%u].",
               icm->exists, exists, icm->uidnext, uidnext, lo, hi);
        if(k) {
            g_array_set_size(uidmap, lo-1);
            rc = imap_search_exec(h, TRUE, k, set_uid, uidmap);
            imap_search_key_free(k);
        } else rc = IMR_NO;
        if(rc != IMR_OK) {
            g_array_free(uidmap, TRUE);
            return;
        }
        g_array_free(icm->uidmap, TRUE); icm->uidmap = uidmap;
        g_debug("New uidmap has length: %u", icm->uidmap->len);
    }
    /* One way or another, we have a valid uid->seqno map now;
     * The mailbox data can be resynced easily. */

    for(i=1; i<=icm->exists; i++) {
        uint32_t uid = g_array_index(icm->uidmap, uint32_t, i-1);
        void *data = g_hash_table_lookup(icm->headers,
                                         GUINT_TO_POINTER(uid));
        if(data) /* if uid known */
            imap_mbox_handle_msg_deserialize(h, i, data);
    }
}

/** Stores (possibly persistently) data associated with given handle.
    This allows for quick restore between IMAP sessions and reduces
    synchronization overhead. */
static struct ImapCacheManager*
icm_store_cached_data(ImapMboxHandle *handle)
{
    struct ImapCacheManager *icm;
    unsigned cnt, i;

    if(!handle)
        return NULL;

    cnt = imap_mbox_handle_get_exists(handle);
    icm = imap_cache_manager_new(cnt);
    icm->uidvalidity = imap_mbox_handle_get_validity(handle);
    icm->uidnext     = imap_mbox_handle_get_uidnext(handle);

    for(i=0; i<cnt; i++) {
        void *ptr;
        ImapMessage *imsg = imap_mbox_handle_get_msg(handle, i+1);
        unsigned uid;
        if(imsg && (ptr = imap_message_serialize(imsg)) != NULL) {
            g_hash_table_insert(icm->headers,
                                GUINT_TO_POINTER(imsg->uid), ptr);
            uid = imsg->uid;
        } else uid = 0;
        g_array_append_val(icm->uidmap, uid);
    }
    return icm;
}

static gboolean
icm_save_header(uint32_t uid, gpointer value, FILE *f)
{
    if(fwrite(&uid, sizeof(uid), 1, f) != 1) return FALSE;
    if(uid) {
	uint32_t slen = imap_serialized_message_size(value);
	if(fwrite(&slen, sizeof(slen), 1, f) != 1 ||
           fwrite(value, 1, slen, f) != slen)
            return FALSE;
    }
    return TRUE;
}


static gboolean
icm_save_to_file(struct ImapCacheManager *icm, const gchar *file_name)
{
    gboolean success;
    FILE *f = fopen(file_name, "wb");

    success = f != NULL;
    if(success) {
	uint32_t i = icm->uidmap->len;
	if(fwrite(&i, sizeof(i), 1, f) != 1                       ||
           fwrite(&icm->uidvalidity, sizeof(uint32_t), 1, f) != 1 ||
           fwrite(&icm->uidnext,     sizeof(uint32_t), 1, f) != 1 ||
           fwrite(&icm->exists,      sizeof(uint32_t), 1, f) != 1) {
            success = FALSE;
        } else {
            for(i = 0; i<icm->uidmap->len; i++) {
                uint32_t uid = g_array_index(icm->uidmap, uint32_t, i);
                gpointer value = g_hash_table_lookup(icm->headers,
                                                     GUINT_TO_POINTER(uid));
                if(!icm_save_header(uid, value, f)) {
                    success = FALSE;
                    break;
                }
            }
        }
	fclose(f);
    }
    return success;
}
