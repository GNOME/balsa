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

/* NOTES:

   CACHING: persistent cache is implemented using a directory. 

   CONNECTIONS: there is always one connection per opened mailbox to
   keep track of untagged responses. Understand idea of untagged
   responses particularly for shared mailboxes before you try messing
   with this.
*/
#include "config.h"
#include <dirent.h>
#include <string.h>
#include <ctype.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include <gnome.h> /* for gnome-i18n.h, gnome-config and gnome-util */

#include "filter-funcs.h"
#include "filter.h"
#include "mailbox-filter.h"
#include "message.h"
#include "libbalsa_private.h"
#include "misc.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap-server.h"


struct _LibBalsaMailboxImap {
    LibBalsaMailboxRemote mailbox;
    ImapMboxHandle *handle;     /* stream that has this mailbox selected */
    guint handle_refs;		/* reference counter */

    gchar *path;		/* Imap local path (third part of URL) */
    ImapAuthType auth_type;	/* accepted authentication type */
    ImapUID      uid_validity;

    GArray* messages_info;
    gboolean opened;

    /* Hash table containing the messages matching the conditions
     */
    GHashTable * matching_messages;
    int op;
    GSList * conditions;
};

struct _LibBalsaMailboxImapClass {
    LibBalsaMailboxRemoteClass klass;
};

struct message_info {
    LibBalsaMessage *message;
};

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_imap_finalize(GObject * object);
static void libbalsa_mailbox_imap_class_init(LibBalsaMailboxImapClass *
					     klass);
static void libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox);
static gboolean libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox);
static GMimeStream *libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox *
							     mailbox,
							     LibBalsaMessage *
							     message);
static void libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_imap_message_match(LibBalsaMailbox* mailbox,
						    LibBalsaMessage * msg,
						    LibBalsaCondition *ct);
static void hash_table_to_list_func(gpointer key,
				    gpointer value,
				    gpointer data);
static GList * hash_table_to_list(GHashTable * hash);

static void run_filters_on_reception(LibBalsaMailboxImap * mbox);
static gboolean libbalsa_mailbox_real_imap_match(LibBalsaMailboxImap * mbox,
						 GSList * filters_list,
						 gboolean only_recent);
static void libbalsa_mailbox_imap_mbox_match(LibBalsaMailbox * mbox,
					     GSList * filters_list);
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
static void libbalsa_mailbox_imap_prepare_threading(LibBalsaMailbox *mailbox, 
                                                    guint lo, guint hi);
static void libbalsa_mailbox_imap_fetch_structure(LibBalsaMailbox *mailbox,
                                                  LibBalsaMessage *message,
                                                  LibBalsaFetchFlag flags);
static const gchar* libbalsa_mailbox_imap_get_msg_part(LibBalsaMessage *msg,
                                                       LibBalsaMessageBody *,
                                                       ssize_t *);

static int libbalsa_mailbox_imap_add_message(LibBalsaMailbox * mailbox,
					     LibBalsaMessage * message );

void libbalsa_mailbox_imap_change_message_flags(LibBalsaMailbox * mailbox,
						guint msgno,
						LibBalsaMessageFlag set,
						LibBalsaMessageFlag clear);
static gboolean lbm_imap_change_msgs_flags(LibBalsaMailbox * mailbox,
                                           GList *messages,
                                           LibBalsaMessageFlag set,
                                           LibBalsaMessageFlag clear);
static void libbalsa_mailbox_imap_set_threading(LibBalsaMailbox *mailbox,
						LibBalsaMailboxThreadingType
						thread_type);
static void lbm_imap_update_view_filter(LibBalsaMailbox   *mailbox,
                                        LibBalsaCondition *view_filter);
static void libbalsa_mailbox_imap_sort(LibBalsaMailbox *mailbox,
                                       GArray *array);
static guint libbalsa_mailbox_imap_total_messages(LibBalsaMailbox *
						  mailbox);

static void server_settings_changed(LibBalsaServer * server,
				    LibBalsaMailbox * mailbox);
static void server_user_settings_changed_cb(LibBalsaServer * server,
					    gchar * string,
					    LibBalsaMailbox * mailbox);
static void server_host_settings_changed_cb(LibBalsaServer * server,
					    gchar * host,
#ifdef USE_SSL
					    gboolean use_ssl,
#endif
					    LibBalsaMailbox * mailbox);

static struct message_info *message_info_from_msgno(
						  LibBalsaMailboxImap * mimap,
						  guint msgno)
{
    struct message_info *msg_info = 
	&g_array_index(mimap->messages_info, struct message_info, msgno - 1);
    return msg_info;
}

#define IMAP_MESSAGE_UID(msg) \
        (imap_mbox_handle_get_msg \
                (LIBBALSA_MAILBOX_IMAP((msg)->mailbox)->handle,\
                 (msg)->msgno)->uid)

#define IMAP_MAILBOX_UID_VALIDITY(mailbox) (LIBBALSA_MAILBOX_IMAP(mailbox)->uid_validity)

GType
libbalsa_mailbox_imap_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxImapClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_imap_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxImap),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_imap_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX_REMOTE,
	                           "LibBalsaMailboxImap",
			           &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_imap_class_init(LibBalsaMailboxImapClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_mailbox_imap_finalize;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_imap_open;
    libbalsa_mailbox_class->close_mailbox = libbalsa_mailbox_imap_close;

    libbalsa_mailbox_class->check = libbalsa_mailbox_imap_check;
    libbalsa_mailbox_class->message_match =
	libbalsa_mailbox_imap_message_match;
    libbalsa_mailbox_class->mailbox_match =
	libbalsa_mailbox_imap_mbox_match;
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
    libbalsa_mailbox_class->get_message_part = 
        libbalsa_mailbox_imap_get_msg_part;
    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_imap_get_message_stream;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_imap_add_message;
    libbalsa_mailbox_class->change_message_flags =
	libbalsa_mailbox_imap_change_message_flags;
    libbalsa_mailbox_class->change_msgs_flags =
	lbm_imap_change_msgs_flags;
    libbalsa_mailbox_class->set_threading =
	libbalsa_mailbox_imap_set_threading;
    libbalsa_mailbox_class->update_view_filter =
        lbm_imap_update_view_filter;
    libbalsa_mailbox_class->sort = libbalsa_mailbox_imap_sort;
    libbalsa_mailbox_class->total_messages =
	libbalsa_mailbox_imap_total_messages;
}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox)
{
    mailbox->path = NULL;
    mailbox->auth_type = AuthCram;	/* reasonable default */
    mailbox->matching_messages = NULL;
    mailbox->op = FILTER_NOOP;
    mailbox->conditions = NULL;
    mailbox->handle = NULL;
    mailbox->handle_refs = 0;
}

/* libbalsa_mailbox_imap_finalize:
   NOTE: we have to close mailbox ourselves without waiting for
   LibBalsaMailbox::finalize because we want to destroy server as well,
   and close requires server for proper operation.  
*/
static void
libbalsa_mailbox_imap_finalize(GObject * object)
{
    LibBalsaMailboxImap *mailbox;
    LibBalsaMailboxRemote *remote;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(object));

    mailbox = LIBBALSA_MAILBOX_IMAP(object);

    while (LIBBALSA_MAILBOX(mailbox)->open_ref > 0)
	libbalsa_mailbox_close(LIBBALSA_MAILBOX(object));

    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));

    remote = LIBBALSA_MAILBOX_REMOTE(object);
    g_free(mailbox->path); mailbox->path = NULL;

    if(remote->server) {
	g_object_unref(G_OBJECT(remote->server));
	remote->server = NULL;
    }

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

GObject *
libbalsa_mailbox_imap_new(void)
{
    LibBalsaMailbox *mailbox;
    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_IMAP, NULL);

    return G_OBJECT(mailbox);
}

/* libbalsa_mailbox_imap_update_url:
   this is to be used only by mailboxImap functions, with exception
   for the folder scanner, which has to go around libmutt limitations.
*/
void
libbalsa_mailbox_imap_update_url(LibBalsaMailboxImap* mailbox)
{
    LibBalsaServer *s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    g_free(LIBBALSA_MAILBOX(mailbox)->url);
    LIBBALSA_MAILBOX(mailbox)->url = libbalsa_imap_url(s, mailbox->path);
}

/* Unregister an old notification and add a current one */
static void
server_settings_changed(LibBalsaServer * server, LibBalsaMailbox * mailbox)
{
    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));

    if (server->user && server->passwd && server->host)
	libbalsa_notify_register_mailbox(mailbox);
}

void
libbalsa_mailbox_imap_set_path(LibBalsaMailboxImap* mailbox, const gchar* path)
{
    g_return_if_fail(mailbox);
    g_free(mailbox->path);
    mailbox->path = g_strdup(path);
    libbalsa_mailbox_imap_update_url(mailbox);

    g_return_if_fail(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));
    server_settings_changed(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
			    LIBBALSA_MAILBOX(mailbox));
}

const gchar*
libbalsa_mailbox_imap_get_path(LibBalsaMailboxImap * mailbox)
{
    return mailbox->path;
}

static void
server_user_settings_changed_cb(LibBalsaServer * server, gchar * string,
				LibBalsaMailbox * mailbox)
{
    server_settings_changed(server, mailbox);
}

static void
server_host_settings_changed_cb(LibBalsaServer * server, gchar * host,
#ifdef USE_SSL
					    gboolean use_ssl,
#endif
				LibBalsaMailbox * mailbox)
{
    libbalsa_mailbox_imap_update_url(LIBBALSA_MAILBOX_IMAP(mailbox));
    server_settings_changed(server, mailbox);
}

static gchar*
get_cache_name(LibBalsaMailboxImap* mailbox, const gchar* type)
{
    gchar* fname, *start;
    LibBalsaServer *s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    gchar* postfix = g_strconcat(".balsa/", s->user, "@", s->host, "-",
                                 (mailbox->path ? mailbox->path : "INBOX"),
                                 "-", type, ".dir", NULL);
    for(start=strchr(postfix+7, '/'); start; start = strchr(start,'/'))
        *start='-';

    fname = gnome_util_prepend_user_home(postfix);
    g_free(postfix);
    return fname;
}

static gchar*
get_cache_name_body(LibBalsaMailboxImap* mailbox, ImapUID uid)
{
    gchar* fname = get_cache_name(mailbox, "body");
    ImapUID uid_validity = LIBBALSA_MAILBOX_IMAP(mailbox)->uid_validity;
    gchar *fn = g_strdup_printf("%s/%u-%u", fname, uid_validity, uid);
    g_free(fname);
    return fn;
}

/* clean_cache:
   removes unused entries from the cache file.
*/
static gboolean
clean_cache(LibBalsaMailbox* mailbox)
{
    DIR* dir;
    struct dirent* key;
    gchar* fname =  get_cache_name(LIBBALSA_MAILBOX_IMAP(mailbox), "body");
    ImapUID uid[2];
    GHashTable *present_uids;
    GList *lst, *remove_list;
    ImapUID uid_validity = LIBBALSA_MAILBOX_IMAP(mailbox)->uid_validity;

    return TRUE; /* FIXME: implement proper cache cleaning scheme */
    dir = opendir(fname);
    if(!dir) {
        g_free(fname);
        return FALSE;
    }

    present_uids = g_hash_table_new(g_direct_hash, g_direct_equal);
    remove_list  = NULL;

#ifdef UID_CACHE_PRESENT
    for(lst = mailbox->message_list; lst; lst = lst->next) {
        ImapUID u = IMAP_MESSAGE_UID(LIBBALSA_MESSAGE(lst->data));
        g_hash_table_insert(present_uids, UID_TO_POINTER(u), &present_uids);
    }
#endif
    while ( (key=readdir(dir)) != NULL) {
        if(sscanf(key->d_name,"%u-%u", &uid[0], &uid[1])!=2)
            continue;
        if( uid[0] != uid_validity 
            || !g_hash_table_lookup(present_uids, UID_TO_POINTER(uid[1]))) {
            remove_list = 
                g_list_prepend(remove_list, UID_TO_POINTER(uid[1]));
        }
    }
    closedir(dir);
    g_hash_table_destroy(present_uids);
    
    for(lst = remove_list; lst; lst = lst->next) {
        unsigned uid = GPOINTER_TO_UINT(lst->data);
        gchar *fn = g_strdup_printf("%s/%u-%u", fname, uid_validity, uid);
        if(unlink(fn))
            libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                                 "Unlinked %s\n", fn);
        else
            libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                                 "Could not unlink %s\n", fn);
        g_free(fn);
    }
    g_list_free(remove_list);
    g_free(fname);
    return TRUE;
}

/* Helper */

void run_filters_on_reception(LibBalsaMailboxImap * mbox)
{
    GSList * filters;
    LibBalsaMailbox * mailbox = LIBBALSA_MAILBOX(mbox);

    if (!mailbox->filters)
	config_mailbox_filters_load(mailbox);
    
    filters = libbalsa_mailbox_filters_when(mailbox->filters,
					    FILTER_WHEN_INCOMING);
    /* We apply filter if needed */
    if (filters) {
	if (filters_prepare_to_run(filters)) {
	    if (!libbalsa_mailbox_real_imap_match(mbox,
						  filters,
						  TRUE))
		libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				     _("IMAP SEARCH command failed for mailbox %s\n"
				       "or query was incompatible with IMAP"
				       " (perhaps you use regular expressions)\n"
				       "falling back to default searching method"),
				     mailbox->url);
	    /* IMAP specific filtering failed, fallback to default
	       filtering function*/
	    libbalsa_mailbox_run_filters_on_reception(mailbox, filters);
	    libbalsa_filter_apply(filters);
	}
	g_slist_free(filters);
    }
}

static ImapMboxHandle *
libbalsa_mailbox_imap_get_handle(LibBalsaMailboxImap *mimap)
{

    g_return_val_if_fail(LIBBALSA_MAILBOX_IMAP(mimap), NULL);

    if(!mimap->handle) {
        LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_SERVER(mimap);
        LibBalsaImapServer *imap_server;
        if (!LIBBALSA_IS_IMAP_SERVER(server))
            return NULL;
        imap_server = LIBBALSA_IMAP_SERVER(server);
        mimap->handle = libbalsa_imap_server_get_handle(imap_server);
	mimap->handle_refs = 1;
    } else
	++mimap->handle_refs;

    return mimap->handle;
}

#define RELEASE_HANDLE(mailbox,handle) \
    libbalsa_imap_server_release_handle( \
		LIBBALSA_IMAP_SERVER(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox)),\
		handle)

static void
lbimap_update_flags(LibBalsaMessage *message, ImapMessage *imsg)
{
    message->flags = 0;
    if (!IMSG_FLAG_SEEN(imsg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
    if (IMSG_FLAG_DELETED(imsg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
    if (IMSG_FLAG_FLAGGED(imsg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
    if (IMSG_FLAG_ANSWERED(imsg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
    if (IMSG_FLAG_RECENT(imsg->flags))
	message->flags |= LIBBALSA_MESSAGE_FLAG_RECENT;
}

/** imap_flags_cb() is called by the imap backend when flags are
   fetched. Note that we may not have yet the preprocessed data in
   LibBalsaMessage.  We ignore the info in this case.
*/
static void
imap_flags_cb(unsigned seqno, LibBalsaMailboxImap *mimap)
{
    struct message_info *msg_info = message_info_from_msgno(mimap, seqno);
    ImapMessage *imsg  = imap_mbox_handle_get_msg(mimap->handle, seqno);
    if(msg_info->message) {
        lbimap_update_flags(msg_info->message, imsg);
        libbalsa_message_set_icons(msg_info->message);
        libbalsa_mailbox_msgno_changed(LIBBALSA_MAILBOX(mimap), seqno);
    }
}

static void
imap_exists_cb(ImapMboxHandle *handle, LibBalsaMailboxImap *mimap)
{
    unsigned cnt = imap_mbox_handle_get_exists(mimap->handle);
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mimap);
    if(cnt<mimap->messages_info->len) { /* remove messages */
        printf("%s: expunge ignored?\n", __func__);
    } else { /* new messages arrived */
        unsigned i;
        for(i=mimap->messages_info->len+1; i<=cnt; i++) {
            struct message_info a = {0};
            g_array_append_val(mimap->messages_info, a);
            libbalsa_mailbox_msgno_inserted(mailbox, i);
        }
    }
}

static void
imap_expunge_cb(ImapMboxHandle *handle, unsigned seqno,
                LibBalsaMailboxImap *mimap)
{
    guint i;

    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mimap);
    struct message_info *msg_info = message_info_from_msgno(mimap, seqno);
    libbalsa_mailbox_msgno_removed(mailbox, seqno);
    if(msg_info->message) {
	gchar *fn =
            get_cache_name_body(mimap, IMAP_MESSAGE_UID(msg_info->message));
        unlink(fn); /* ignore error; perhaps the message 
                     * was not in the cache.  */
        g_free(fn);
        g_object_unref(G_OBJECT(msg_info->message));
    }
    g_array_remove_index(mimap->messages_info, seqno-1);

    for (i = seqno - 1; i < mimap->messages_info->len; i++) {
	struct message_info *msg_info =
	    &g_array_index(mimap->messages_info, struct message_info, i);
	if (msg_info->message)
	    msg_info->message->msgno = i + 1;
    }
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
	RELEASE_HANDLE(mimap, mimap->handle);
	mimap->handle = NULL;
    }
}

static ImapMboxHandle *
libbalsa_mailbox_imap_get_selected_handle(LibBalsaMailboxImap *mimap)
{
    LibBalsaServer *server;
    LibBalsaImapServer *imap_server;
    ImapResponse rc;
    unsigned uidval;

    g_return_val_if_fail(LIBBALSA_MAILBOX_IMAP(mimap), NULL);

    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mimap);
    if (!LIBBALSA_IS_IMAP_SERVER(server))
	return NULL;
    imap_server = LIBBALSA_IMAP_SERVER(server);
    if(!mimap->handle) {
        mimap->handle = libbalsa_imap_server_get_handle_with_user(imap_server,
                                                                  mimap);
        if (!mimap->handle)
            return NULL;
    }
    rc = imap_mbox_select(mimap->handle, mimap->path,
			  &(LIBBALSA_MAILBOX(mimap)->readonly));
    if (rc != IMR_OK) {
	RELEASE_HANDLE(mimap, mimap->handle);
        mimap->handle = NULL;
	return NULL;
    }
    /* test validity */
    uidval = imap_mbox_handle_get_validity(mimap->handle);
    if (mimap->uid_validity != uidval) {
	mimap->uid_validity = uidval;
	/* FIXME: update/remove msg uids */
    }

    /* FIXME: these handlers should be disconnected when mailbox is closed. */
    imap_handle_set_flagscb(mimap->handle, (ImapFlagsCb)imap_flags_cb, mimap);
    g_signal_connect(G_OBJECT(mimap->handle),
                     "exists-notify", G_CALLBACK(imap_exists_cb),
                     mimap);
    g_signal_connect(G_OBJECT(mimap->handle),
                     "expunge-notify", G_CALLBACK(imap_expunge_cb),
                     mimap);
    mimap->handle_refs = 1;
    return mimap->handle;
}

/* libbalsa_mailbox_imap_open:
   opens IMAP mailbox. On failure leaves the object in sane state.
*/
static gboolean
libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mimap;
    LibBalsaServer *server;
    unsigned i;
    guint total_messages;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    mimap->handle = libbalsa_mailbox_imap_get_selected_handle(mimap);
    if (!mimap->handle) {
        mimap->opened         = FALSE;
	mailbox->disconnected = TRUE;
	return FALSE;
    }

    mimap->opened         = TRUE;
    mailbox->disconnected = FALSE;
    mailbox->unread_messages = 0;
    total_messages = imap_mbox_handle_get_exists(mimap->handle);
    mimap->messages_info = g_array_sized_new(FALSE, TRUE, 
					     sizeof(struct message_info),
					     total_messages);
    for(i=0; i < total_messages; i++) {
	struct message_info a = {0};
	g_array_append_val(mimap->messages_info, a);
    }

    run_filters_on_reception(mimap);

#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxImap", mailbox->name, mailbox->open_ref);
#endif
    return TRUE;
}

static void
free_messages_info(LibBalsaMailboxImap * mbox)
{
    guint i;
    GArray *messages_info = mbox->messages_info;

    for (i = 0; i < messages_info->len; i++) {
	struct message_info *msg_info =
	    &g_array_index(messages_info, struct message_info, i);
	if (msg_info->message) {
	    msg_info->message->mailbox = NULL;
	    g_object_unref(msg_info->message);
	}
    }
    g_array_free(mbox->messages_info, TRUE);
    mbox->messages_info = NULL;
}

static void
libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mbox = LIBBALSA_MAILBOX_IMAP(mailbox);

    /* FIXME: save headers differently: save_to_cache(mailbox); */
    clean_cache(mailbox);

    mbox->opened = FALSE;

    if (mbox->matching_messages) {
	g_hash_table_destroy(mbox->matching_messages);
	mbox->matching_messages = NULL;
    }
#ifdef FIXME
    if (mbox->condition) {
	libbalsa_condition_free(mbox->condition);
	mbox->conditions = NULL;
	mbox->op = FILTER_NOOP;
    }
#endif
    imap_mbox_unselect(mbox->handle);
    free_messages_info(mbox);
    libbalsa_mailbox_imap_release_handle(mbox);
}

static FILE*
get_cache_stream(LibBalsaMailbox *mailbox, unsigned uid)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    FILE *stream;
    gchar *cache_name, *msg_name;
    unsigned uidval;

    g_assert(mimap->handle);
    uidval = imap_mbox_handle_get_validity(mimap->handle);
    cache_name = get_cache_name(mimap, "body");
    msg_name   = g_strdup_printf("%s/%u-%u", cache_name, uidval, uid);

    stream = fopen(msg_name, "rb");
    if(!stream) {
        FILE *cache;
	ImapResponse rc;

        libbalsa_assure_balsa_dir();
        mkdir(cache_name, S_IRUSR|S_IWUSR|S_IXUSR); /* ignore errors */
        cache = fopen(msg_name, "wb");
        rc = imap_mbox_handle_fetch_rfc822_uid(mimap->handle, uid, cache);
        fclose(cache);

	stream = fopen(msg_name,"rb");
    }
    g_free(msg_name); 
    g_free(cache_name);
    return stream;
}

/* libbalsa_mailbox_imap_get_message_stream: 
   Fetch data from cache first, if available.
   When calling imap_fetch_message(), we make use of fact that
   imap_fetch_message doesn't set msg->path field.
*/
static GMimeStream *
libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox * mailbox,
					 LibBalsaMessage * message)
{
    FILE *stream = NULL;
    GMimeStream *gmime_stream = NULL;
    LibBalsaMailboxImap *mimap;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    RETURN_VAL_IF_CONTEXT_CLOSED(message->mailbox, NULL);

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    stream = get_cache_stream(mailbox, IMAP_MESSAGE_UID(message));
    gmime_stream = g_mime_stream_file_new(stream);
    return gmime_stream;
}

/* libbalsa_mailbox_imap_check:
   checks imap mailbox for new messages.
   Called with the mailbox locked.
*/
static void
libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox)
{
    g_assert(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    if (!MAILBOX_OPEN(mailbox)) {
	if (libbalsa_notify_check_mailbox(mailbox))
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
	return;
    }

    if (LIBBALSA_MAILBOX_IMAP(mailbox)->handle)
	libbalsa_mailbox_imap_noop(LIBBALSA_MAILBOX_IMAP(mailbox));
    else
	g_warning("mailbox has open_ref>0 but no handle!\n");

    run_filters_on_reception(LIBBALSA_MAILBOX_IMAP(mailbox));
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
        printf("Could not find UID: %u in message list\n", uid);
}

/* Gets the messages matching the conditions via the IMAP search command
   error is put to TRUE if an error occured
*/

GHashTable * libbalsa_mailbox_imap_get_matchings(LibBalsaMailboxImap* mbox,
						 LibBalsaCondition *ct,
						 gboolean only_recent,
						 gboolean * err)
{
    gchar* query;
    ImapResult rc = IMR_NO;
    ImapSearchData * cbdata;

    *err = FALSE;
    
    cbdata = g_new( ImapSearchData, 1 );
    cbdata->uids = g_hash_table_new(NULL, NULL); 
    cbdata->res  = g_hash_table_new(NULL, NULL);
    query = libbalsa_condition_build_imap_query(ct /* FIXME: ONLY RECENT! */);
    if (query) {
#ifdef UID_SEARCH_IMPLEMENTED
	for(msgs= LIBBALSA_MAILBOX(mbox)->message_list; msgs;
	    msgs = msgs->next){
	    LibBalsaMessage *m = LIBBALSA_MESSAGE(msgs->data);
	    ImapUID uid = IMAP_MESSAGE_UID(m);
	    g_hash_table_insert(cbdata->uids, GUINT_TO_POINTER(uid), m);
	}
#else	
        g_warning("Search results ignored. Fixme!");
#endif
        rc = imap_mbox_uid_search(mbox->handle, query, 
                                  (void(*)(unsigned,void*))imap_matched,
                                  cbdata);
        g_free(query);
    }
    g_hash_table_destroy(cbdata->uids);
    /* Clean up on error */
    if (rc != IMR_OK) {
	g_hash_table_destroy(cbdata->res);
	cbdata->res = NULL;
	*err = TRUE;
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     _("IMAP SEARCH command failed for mailbox %s\n"
			       "falling back to default searching method"),
			     LIBBALSA_MAILBOX(mbox)->url);
    };
    return cbdata->res;
}

/* This function download the UID via the SEARCH command if necessary
   ie if the search has not already been done (eg "search" will 
   firstly download the matching messages, and "search again" will
   only look in the already dowloaded hash table) and once this is done
   it will lookup the given message (falling back to filter methods if
   the IMAP command has failed).
 */

static gboolean
libbalsa_mailbox_imap_message_match(LibBalsaMailbox* mbox,
				    LibBalsaMessage * message,
				    LibBalsaCondition *condition)
{
#if 0
    /* FIXME: whole searching code needs to be fixed */
    LibBalsaMailboxImap * mailbox = LIBBALSA_MAILBOX_IMAP(mbox);
    gboolean error;
    GSList * cnds;

    if (!mailbox->matching_messages || op!=mailbox->op
	|| !mailbox->conditions
	|| !libbalsa_conditions_compare(conditions, mailbox->conditions)) {
	/* New search, build the matching messages hash table */
	mailbox->op = op;
	libbalsa_conditions_free(mailbox->conditions);
	mailbox->conditions = NULL;
	/* We copy the conditions */
	for (cnds = conditions;cnds;cnds = g_slist_next(cnds))
	    mailbox->conditions =
		g_slist_prepend(mailbox->conditions,
				libbalsa_condition_clone(cnds->data));
 	if (mailbox->matching_messages)
 	    g_hash_table_destroy(mailbox->matching_messages);
 	mailbox->matching_messages =
 	    libbalsa_mailbox_imap_get_matchings(mailbox, op, conditions,
 						FALSE, &error);
     }

    if (!error)
	return g_hash_table_lookup(mailbox->matching_messages, message)!=NULL;
    /* On error fall back to the default match_conditions function 
       BE CAREFUL HERE : the mailbox must NOT BE LOCKED here
    */
    else return match_conditions(op, conditions, message, FALSE);
#endif
    return FALSE;
}

static void hash_table_to_list_func(gpointer key,
				    gpointer value,
				    gpointer data)
{
    GList ** list = (GList **)data;

    *list = g_list_prepend(*list, value);
}

/* Transform a hash_table to a glist, do not preserve order
   in fact it reverses the order
*/
static GList * hash_table_to_list(GHashTable * hash)
{
    GList * list = NULL;

    g_hash_table_foreach(hash, hash_table_to_list_func, &list);
    return list;
}

gboolean libbalsa_mailbox_real_imap_match(LibBalsaMailboxImap * mbox,
					  GSList * filter_list,
					  gboolean only_recent)
{
    GSList * lst;
    LibBalsaFilter * flt;
    GList * matching;

    /* First check if we can use IMAP specific filtering funcs*/
    for (lst = filter_list; lst; lst = g_slist_next(lst)) {
	flt = lst->data;
	if (!libbalsa_mailbox_imap_can_match(LIBBALSA_MAILBOX(mbox),
					     flt->condition))
	    return FALSE;
    }

    /*
      For each filter we dowload the matching messages, via the corresponding
      function from imap mailbox.
    */
    for (lst = filter_list;lst;lst = g_slist_next(lst)) {
	gboolean error;
	GHashTable * matchings;
	
	flt = lst->data;
	matchings = libbalsa_mailbox_imap_get_matchings(mbox,
							flt->condition,
							only_recent,
							&error);
	if (error) return FALSE;
	
	if (matchings)
	    flt->matching_messages =
		hash_table_to_list(matchings);
    }

    libbalsa_filter_sanitize(filter_list);
    /* Now ref all matching messages to be sure they are still there
       when we want to apply the filter actions on them
    */
    for (lst = filter_list; lst; lst = g_slist_next(lst)) {
	flt = lst->data;
	for (matching = flt->matching_messages; matching;
	     matching = g_list_next(matching))
	    g_object_ref(matching->data);
    }
    return TRUE;
}

void libbalsa_mailbox_imap_mbox_match(LibBalsaMailbox * mbox,
				      GSList * filters_list)
{
    g_return_if_fail(mbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mbox));
    /* For IMAP mailboxes we first try the special imap match functions
       if this fails, we fallback to the default function
     */
    if (!libbalsa_mailbox_real_imap_match(LIBBALSA_MAILBOX_IMAP(mbox),
					  filters_list,
					  FALSE)) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("IMAP SEARCH command failed for mailbox %s\n"),
			     LIBBALSA_MAILBOX(mbox)->url);	
#if 0 
        /* could fall back to standard algorithms but will not */
	libbalsa_mailbox_real_mbox_match(mbox, filters_list);
#endif
    }
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

    gnome_config_set_string("Path", mimap->path);

    libbalsa_server_save_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxImap *mimap;
    LibBalsaMailboxRemote *remote;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    g_free(mimap->path);
    mimap->path = gnome_config_get_string("Path");

    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    remote->server = LIBBALSA_SERVER(libbalsa_imap_server_new_from_config());

    g_signal_connect(G_OBJECT(remote->server), "set-username",
		     G_CALLBACK(server_user_settings_changed_cb),
		     (gpointer) mailbox);
    g_signal_connect(G_OBJECT(remote->server), "set-password",
		     G_CALLBACK(server_user_settings_changed_cb),
		     (gpointer) mailbox);
    g_signal_connect(G_OBJECT(remote->server), "set-host",
		     G_CALLBACK(server_host_settings_changed_cb),
		     (gpointer) mailbox);

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);

    server_settings_changed(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
			    mailbox);
    libbalsa_mailbox_imap_update_url(mimap);
}

gboolean
libbalsa_mailbox_imap_subscribe(LibBalsaMailboxImap * mailbox, 
				     gboolean subscribe)
{
    ImapResult rc;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);

    rc = imap_mbox_subscribe(mailbox->handle, mailbox->path, subscribe);

    return rc == IMR_OK;
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

    if (mimap->handle)
	if (imap_mbox_handle_noop(mimap->handle) != IMR_OK)
	    /* FIXME: report error... */
	    ;
}

/* imap_close_all_connections:
   close all connections to leave the place cleanly.
*/
void
libbalsa_imap_close_all_connections(void)
{
    libbalsa_imap_server_close_all_connections();
}

void libbalsa_mailbox_imap_expunge_notify(LibBalsaMailboxImap* mimap,
					  int seqno)
{
    /* FIXME: do something with the notification */
}

/* libbalsa_imap_rename_subfolder:
   dir+parent determine current name. 
   folder - new name. Can be called for a closed mailbox.
 */
gboolean
libbalsa_imap_rename_subfolder(LibBalsaMailboxImap* imap,
                               const gchar *new_parent, const gchar *folder, 
                               gboolean subscribe)
{
    ImapResult rc;
    ImapMboxHandle* handle;
    gchar *new_path;

    handle = libbalsa_mailbox_imap_get_handle(imap);
    if (!handle)
	return FALSE;

    imap_mbox_subscribe(handle, imap->path, FALSE);
    /* FIXME: should use imap server folder separator */ 
    new_path = g_strjoin("/", new_parent, folder, NULL);
    rc = imap_mbox_rename(handle, imap->path, new_path);
    if (subscribe && rc == IMR_OK)
	rc = imap_mbox_subscribe(handle, new_path, TRUE);
    g_free(new_path);

    libbalsa_mailbox_imap_release_handle(imap);
    return rc == IMR_OK;
}

void
libbalsa_imap_new_subfolder(const gchar *parent, const gchar *folder,
			    gboolean subscribe, LibBalsaServer *server)
{
    ImapResult rc;
    ImapMboxHandle* handle;
    gchar *new_path;

    if (!LIBBALSA_IS_IMAP_SERVER(server))
	return;
    handle = libbalsa_imap_server_get_handle(LIBBALSA_IMAP_SERVER(server));
    if (!handle)
	return;

    /* FIXME: should use imap server folder separator */ 
    new_path = g_strjoin("/", parent, folder, NULL);
    rc = imap_mbox_create(handle, new_path);
    if (subscribe && rc == IMR_OK)
	rc = imap_mbox_subscribe(handle, new_path, TRUE);
    g_free(new_path);

    libbalsa_imap_server_release_handle(LIBBALSA_IMAP_SERVER(server), handle);
}

void
libbalsa_imap_delete_folder(LibBalsaMailboxImap *mailbox)
{
    ImapMboxHandle* handle;

    handle = libbalsa_mailbox_imap_get_handle(mailbox);
    if (!handle)
	return;

    /* Some IMAP servers (UW2000) do not like removing subscribed mailboxes:
     * they do not remove the mailbox from the subscription list since 
     * the subscription list should be treated as a list of bookmarks,
     * not a list of physically existing mailboxes. */
    imap_mbox_subscribe(handle, mailbox->path, FALSE);
    imap_mbox_delete(handle, mailbox->path);

    libbalsa_mailbox_imap_release_handle(mailbox);
}

gchar *
libbalsa_imap_path(LibBalsaServer * server, const gchar * path)
{
    gchar *imap_path = path && *path
        ? g_strdup_printf("imap%s://%s/%s/",
#ifdef USE_SSL
                                       server->use_ssl ? "s" : "",
#else
                                       "",
#endif
                                       server->host, path)
        : g_strdup_printf("imap%s://%s/",
#ifdef USE_SSL
                                       server->use_ssl ? "s" : "",
#else
                                       "",
#endif
                                       server->host);

    return imap_path;
}

gchar *
libbalsa_imap_url(LibBalsaServer * server, const gchar * path)
{
    gchar *enc = libbalsa_urlencode(server->user);
    gchar *url = g_strdup_printf("imap%s://%s@%s/%s",
#ifdef USE_SSL
                                 server->use_ssl ? "s" : "",
#else
                                 "",
#endif
                                 enc, server->host,
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
        res =  imap_mbox_expunge(mimap->handle) == IMR_OK;
    }
    return res;
}

static LibBalsaAddress *
libbalsa_address_new_from_imap_address(ImapAddress *addr)
{
    LibBalsaAddress *address;

    if (!addr || (addr->name==NULL && addr->addr_spec==NULL))
       return NULL;

    address = libbalsa_address_new();

    /* it will be owned by the caller */

    if (addr->name)
	address->full_name = g_mime_utils_header_decode_text(addr->name);
    if (addr->addr_spec)
	address->address_list =
	    g_list_append(address->address_list,
			  g_mime_utils_header_decode_text(addr->
							  addr_spec));

    return address;
}

static GList*
libbalsa_address_new_list_from_imap_address_list(ImapAddress *list)
{
    LibBalsaAddress* addr;
    GList *res = NULL;

    for (; list; list = list->next) {
       addr = libbalsa_address_new_from_imap_address(list);
       if (addr)
           res = g_list_prepend(res, addr);
    }
    return g_list_reverse(res);
}

static void
lb_set_headers(LibBalsaMessageHeaders *headers, ImapEnvelope *  envelope,
               gboolean is_embedded)
{
    headers->date = envelope->date;
    headers->from = libbalsa_address_new_from_imap_address(envelope->from);
    headers->reply_to =
        libbalsa_address_new_from_imap_address(envelope->replyto);
    headers->to_list =
	libbalsa_address_new_list_from_imap_address_list(envelope->to);
    headers->cc_list =
	libbalsa_address_new_list_from_imap_address_list(envelope->cc);
    headers->bcc_list =
	libbalsa_address_new_list_from_imap_address_list(envelope->bcc);

    if(is_embedded) {
        headers->subject = 
            g_mime_utils_header_decode_text(envelope->subject);
    }
}

struct collect_seq_data {
    unsigned *msgno_arr;
    unsigned first_msgno;
    unsigned cnt;
};

static gboolean
collect_seq_cb(GNode *node, gpointer data)
{
    /* we prefetch envelopes in chunks to save on RTTs */
    static const unsigned MAX_CHUNK_LENGTH = 40; 
    struct collect_seq_data *csd = (struct collect_seq_data*)data;
    unsigned msgno = GPOINTER_TO_UINT(node->data);
    if(csd->msgno_arr == NULL) {
        if( msgno != csd->first_msgno)
            return FALSE;
        csd->msgno_arr =
            g_malloc(MAX_CHUNK_LENGTH*sizeof(csd->msgno_arr[0]));
        csd->cnt = 0;
    }
    csd->msgno_arr[csd->cnt++] = msgno;
    return csd->cnt == MAX_CHUNK_LENGTH;
}

static int
cmp_msgno(const void* a, const void *b)
{
    return (*(unsigned*)a) - (*(unsigned*)b);
}

static gboolean
libbalsa_mailbox_imap_load_envelope(LibBalsaMailboxImap *mimap,
				    LibBalsaMessage *message)
{
    ImapResponse rc;
    ImapEnvelope *envelope;
    struct collect_seq_data csd;
    ImapMessage* imsg;
    
    g_return_val_if_fail(mimap->opened, FALSE);
    if( (imsg = imap_mbox_handle_get_msg(mimap->handle, message->msgno)) 
        == NULL) {
        csd.msgno_arr   = NULL;
        csd.first_msgno = message->msgno;
        csd.cnt         = 0;
        g_node_traverse(LIBBALSA_MAILBOX(mimap)->msg_tree,
                        G_PRE_ORDER, G_TRAVERSE_ALL, -1, collect_seq_cb,
                        &csd);
        qsort(csd.msgno_arr, csd.cnt, sizeof(csd.msgno_arr[0]), cmp_msgno);
        rc = imap_mbox_handle_fetch_set(mimap->handle, csd.msgno_arr,
                                        csd.cnt,
                                        IMFETCH_ENV|IMFETCH_RFC822SIZE);
        g_free(csd.msgno_arr);
        if (rc != IMR_OK)
            return FALSE;
        imsg = imap_mbox_handle_get_msg(mimap->handle, message->msgno);
    }

    g_return_val_if_fail(imsg, FALSE);
    lbimap_update_flags(message, imsg);

    lb_set_headers(message->headers, imsg->envelope, FALSE);
    envelope        = imsg->envelope;
    message->length = imsg->rfc822size;
    message->subj   = g_mime_utils_header_decode_text(envelope->subject);
    message->sender =
	libbalsa_address_new_from_imap_address(envelope->sender);
    libbalsa_message_set_in_reply_to_from_string(message,
						 envelope->in_reply_to);
    if (envelope->message_id) {
	message->message_id =
	    g_mime_utils_decode_message_id(envelope->message_id);
    }
    return TRUE;
}

static LibBalsaMessage*
libbalsa_mailbox_imap_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;
    LibBalsaMailboxImap *mimap;

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    msg_info = message_info_from_msgno(mimap, msgno);

    if (!msg_info) {
	printf("%s returns NULL\n", __func__);
	return NULL;
    }

    if (!msg_info->message) {
        LibBalsaMessage *msg = libbalsa_message_new();
        msg->msgno   = msgno;
        msg->mailbox = mailbox;
        if (libbalsa_mailbox_imap_load_envelope(mimap, msg)) {
	    libbalsa_message_set_icons(msg);
            msg_info->message  = msg;
	} else 
            g_object_unref(G_OBJECT(msg));
    }
    return msg_info->message;
}

static void
libbalsa_mailbox_imap_prepare_threading(LibBalsaMailbox *mailbox, 
                                        guint lo, guint hi)
{
    g_warning("%s not implemented yet.\n", __func__);
}

static void
lbm_imap_construct_body(LibBalsaMessageBody *lbbody, ImapBody *imap_body)
{
    int i;
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
    lbbody->mime_type = imap_body_get_mime_type(imap_body);
    for(i=0; lbbody->mime_type[i]; i++)
        lbbody->mime_type[i] = tolower(lbbody->mime_type[i]);
    /* get the name in the same way as g_mime_part_get_filename() does */
    str = imap_body_get_dsp_param(imap_body, "filename");
    if(!str) str = imap_body_get_param(imap_body, "name");
    lbbody->filename  = g_strdup(str);
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

static void
libbalsa_mailbox_imap_fetch_structure(LibBalsaMailbox *mailbox,
                                      LibBalsaMessage *message,
                                      LibBalsaFetchFlag flags)
{
    ImapResponse rc;
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    g_return_if_fail(mimap->opened);

    rc = imap_mbox_handle_fetch_structure(mimap->handle, message->msgno);
    if(rc == IMR_OK) { /* translate ImapData to LibBalsaMessage */
        ImapMessage *im = imap_mbox_handle_get_msg(mimap->handle,
                                                   message->msgno);
        LibBalsaMessageBody *body = libbalsa_message_body_new(message);
	lbm_imap_construct_body(body, im->body);
        libbalsa_message_append_part(message, body);
    }
}

static gboolean
is_child_of(LibBalsaMessageBody *body, LibBalsaMessageBody *child,
            GString *s, gboolean modify)
{
    int i = 1;
    for(i=1; body; body = body->next) {
        if(body==child) {
            g_string_printf(s, "%u", i);
            return TRUE;
        }

        if(is_child_of(body->parts, child, s,
                       body->body_type != LIBBALSA_MESSAGE_BODY_TYPE_MESSAGE)){
            char buf[12];
            if(modify) {
                snprintf(buf, sizeof(buf), "%u.", i);
                g_string_prepend(s, buf);
            }
            return TRUE;
        }
        i++;
    }
    return FALSE;
}
static gchar*
get_section_for(LibBalsaMessage *msg, LibBalsaMessageBody *part)
{
    GString *section = g_string_new("");
    LibBalsaMessageBody *parent;

    parent = msg->body_list;
    if (libbalsa_message_body_is_multipart(parent))
	parent = parent->parts;

    if(!is_child_of(parent, part, section, TRUE)) {
        g_warning("Internal error, part not found.\n");
        g_string_free(section, TRUE);
        return g_strdup("1");
    }
    return g_string_free(section, FALSE);
}
struct part_data { char *block; unsigned pos; ImapBody *body; };
static void
append_str(const char *buf, int buflen, void *arg)
{
    struct part_data *dt = (struct part_data*)arg;

    if(dt->pos + buflen > dt->body->octets) {
        g_error("IMAP server sends too much data?\n");
        buflen = dt->body->octets-dt->pos;
    }
    memcpy(dt->block + dt->pos, buf, buflen);
    dt->pos += buflen;
}

static const gchar*
libbalsa_mailbox_imap_get_msg_part(LibBalsaMessage *msg,
                                   LibBalsaMessageBody *part,
                                   ssize_t *sz)
{
    const char *bod = NULL;
    size_t len;  
    GMimeStream *partstream = NULL;

    if(!part->mime_part) { /* !part->mime_part */
	gchar *cache_name, *part_name;
	LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(msg->mailbox);
	FILE *fp;
	gchar *section = get_section_for(msg, part);

	cache_name = get_cache_name(mimap, "part");
	part_name   = 
	    g_strdup_printf("%s/%u-%u-%s", cache_name,
			    imap_mbox_handle_get_validity(mimap->handle),
			    IMAP_MESSAGE_UID(msg),
			    section);
	fp = fopen(part_name,"rb+");
        
	if(!fp) { /* no cache element */
	    struct part_data dt;
	    GMimePart *prefilt;
	    GMimeStream *gms;
	    LibBalsaMailboxImap* mimap;
	    ImapMessage *im;
	    ImapResponse rc;

	    LOCK_MAILBOX_RETURN_VAL(msg->mailbox,NULL);
	    mimap = LIBBALSA_MAILBOX_IMAP(msg->mailbox);
	    im = imap_mbox_handle_get_msg(mimap->handle, msg->msgno);

	    dt.body  = imap_message_get_body_from_section(im, section);
	    dt.block = g_malloc(dt.body->octets+1);
	    dt.pos   = 0;
	    rc = imap_mbox_handle_fetch_body(mimap->handle, msg->msgno, section,
					     append_str, &dt);
	    if(rc != IMR_OK)
		g_error("FIXME: error handling here!\n");
	    
	    UNLOCK_MAILBOX(msg->mailbox);
		
	    prefilt = g_mime_part_new_with_type (dt.body->media_basic_name,
						 dt.body->media_subtype);
	    g_mime_part_set_pre_encoded_content ( prefilt,
						  dt.block,
						  dt.body->octets,
						  (GMimePartEncodingType)dt.body->encoding );
	    g_free(dt.block);
	    
	    libbalsa_assure_balsa_dir();
	    mkdir(cache_name, S_IRUSR|S_IWUSR|S_IXUSR); /* ignore errors */
	    fp = fopen(part_name, "wb+");
	    if(!fp) {
                g_free(section); 
                g_free(cache_name);
                g_free(part_name);
		return NULL; /* something better ? */
            }
	    if( (dt.body->media_basic == IMBMEDIA_TEXT) &&
		!g_ascii_strcasecmp( "plain", dt.body->media_subtype ) ) {
		GMimeFilter *crlffilter; 	    

		gms = g_mime_stream_file_new (fp);
		crlffilter = 	    
		    g_mime_filter_crlf_new (  GMIME_FILTER_CRLF_DECODE,
					      GMIME_FILTER_CRLF_MODE_CRLF_ONLY );
		partstream = g_mime_stream_filter_new_with_stream (gms);
		g_mime_stream_filter_add (GMIME_STREAM_FILTER(partstream)
, crlffilter);
		g_object_unref (gms);
		g_object_unref (crlffilter);
	    } else 
		partstream = g_mime_stream_file_new (fp);
	
	    g_mime_part_set_encoding (prefilt, GMIME_PART_ENCODING_8BIT );
	    g_mime_part_write_to_stream (prefilt, partstream );
	    g_mime_stream_flush (partstream);
	    
	    g_object_unref (prefilt);
	    /* aparently the parser doesn't like the stream_filters .. */
	    g_object_unref (partstream);
	    fp = fopen(part_name, "r");
	    partstream = g_mime_stream_file_new (fp);
	} else 
	    partstream = g_mime_stream_file_new (fp);
    
	{
	    GMimeParser *parser =  
		g_mime_parser_new_with_stream (partstream);
	    part->mime_part = g_mime_parser_construct_part (parser);
	    g_object_unref (parser);
	}
	g_object_unref (partstream);
	g_free(section); 
        g_free(cache_name);
        g_free(part_name);
    }
    bod = g_mime_part_get_content (GMIME_PART(part->mime_part), &len);

    *sz = len;
    return bod;
}

/* libbalsa_mailbox_imap_add_message: 
   can be called for a closed mailbox.
   Called with mailbox locked.
*/
int
libbalsa_mailbox_imap_add_message(LibBalsaMailbox * mailbox,
                                  LibBalsaMessage * message )
{
    ImapMsgFlags imap_flags = IMAP_FLAGS_EMPTY;
    ImapResponse rc;
    GMimeStream *stream;
    GMimeStream *tmpstream;
    GMimeFilter *crlffilter;
    ImapMboxHandle *handle;
    gint outfd;
    gchar *outfile;
    GMimeStream *outstream;
    GError *error = NULL;
    gssize len;

    if(message->mailbox &&
       LIBBALSA_IS_MAILBOX_IMAP(message->mailbox) &&
       LIBBALSA_MAILBOX_REMOTE(message->mailbox)->server ==
       LIBBALSA_MAILBOX_REMOTE(mailbox)->server) {
        ImapMboxHandle *handle = 
            LIBBALSA_MAILBOX_IMAP(message->mailbox)->handle;
        g_return_val_if_fail(handle, -1); /* message is there but
                                           * the source mailbox closed! */
        rc = imap_mbox_handle_copy(handle, message->msgno,
                                   LIBBALSA_MAILBOX_IMAP(mailbox)->path);
        return rc == IMR_OK ? 1 : -1;
    }

    if (!LIBBALSA_MESSAGE_IS_UNREAD(message) )
	IMSG_FLAG_SET(imap_flags,IMSGF_SEEN);
    if ( LIBBALSA_MESSAGE_IS_DELETED(message ) ) 
	IMSG_FLAG_SET(imap_flags,IMSGF_DELETED);
    if ( LIBBALSA_MESSAGE_IS_FLAGGED(message) )
	IMSG_FLAG_SET(imap_flags,IMSGF_FLAGGED);
    if ( LIBBALSA_MESSAGE_IS_REPLIED(message) )
	IMSG_FLAG_SET(imap_flags,IMSGF_ANSWERED);

    stream = libbalsa_mailbox_get_message_stream(message->mailbox, message);
    tmpstream = g_mime_stream_filter_new_with_stream(stream);
    g_mime_stream_unref(stream);

    crlffilter =
	g_mime_filter_crlf_new(GMIME_FILTER_CRLF_ENCODE,
			       GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(tmpstream), crlffilter);
    g_object_unref(crlffilter);

    outfd = g_file_open_tmp("balsa-tmp-file-XXXXXX", &outfile, &error);
    if (outfd < 0) {
	g_warning("Could not create temporary file: %s", error->message);
	g_error_free(error);
	g_mime_stream_unref(tmpstream);
	return -1;
    }

    outstream = g_mime_stream_fs_new(outfd);
    g_mime_stream_write_to_stream(tmpstream, outstream);
    g_mime_stream_unref(tmpstream);

    len = g_mime_stream_tell(outstream);
    g_mime_stream_reset(outstream);

    handle = libbalsa_mailbox_imap_get_handle(LIBBALSA_MAILBOX_IMAP(mailbox));
    rc = imap_mbox_append_stream(handle,
				 LIBBALSA_MAILBOX_IMAP(mailbox)->path,
				 imap_flags, outstream, len);
    libbalsa_mailbox_imap_release_handle(LIBBALSA_MAILBOX_IMAP(mailbox));

    g_mime_stream_unref(outstream);
    unlink(outfile);
    g_free(outfile);

    return rc == IMR_OK ? 1 : -1;
}

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

void
libbalsa_mailbox_imap_change_message_flags(LibBalsaMailbox * mailbox,
                                           guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    ImapMboxHandle *handle = mimap->handle;
    ImapMsgFlag flag_set, flag_clr;

    transform_flags(set, clear, &flag_set, &flag_clr);
    if(flag_set)
        imap_mbox_store_flag(handle, msgno, flag_set, 1);
    if(flag_clr)
        imap_mbox_store_flag(handle, msgno, flag_clr, 0);
}

static gboolean
lbm_imap_change_msgs_flags(LibBalsaMailbox * mailbox,
                           GList *messages,
                           LibBalsaMessageFlag set,
                           LibBalsaMessageFlag clear)
{
    unsigned msgcnt = g_list_length(messages);
    unsigned *seqno = g_malloc(msgcnt*sizeof(unsigned));
    unsigned i = 0;
    ImapMsgFlag flag_set, flag_clr;
    ImapResponse rc1 = IMR_OK, rc2 = IMR_OK;

    while(messages) {
        LibBalsaMessage * msg = LIBBALSA_MESSAGE(messages->data);
        seqno[i++] = msg->msgno;
        messages = g_list_next(messages);
    }
    qsort(seqno, msgcnt, sizeof(seqno[0]), cmp_msgno);
    transform_flags(set, clear, &flag_set, &flag_clr);

    if(flag_set)
        rc1 = imap_mbox_store_flag_m(LIBBALSA_MAILBOX_IMAP(mailbox)->handle,
                                     msgcnt, seqno, flag_set, TRUE);
    if(flag_clr)
        rc2 = imap_mbox_store_flag_m(LIBBALSA_MAILBOX_IMAP(mailbox)->handle,
                                     msgcnt, seqno, flag_clr, FALSE);
    return rc1 == IMR_OK && rc2 == IMR_OK;
}

static ImapSortKey
lbmi_get_imap_sort_key(LibBalsaMailbox *mbox)
{
    ImapSortKey key = LB_MBOX_FROM_COL;

    switch (mbox->view->sort_field) {
    default:
    case LB_MAILBOX_SORT_NO:	  key = IMSO_MSGNO;   break;
    case LB_MAILBOX_SORT_SENDER:    
        key = mbox->view->show == LB_MAILBOX_SHOW_TO
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
    GNode * new_tree;
    guint msgno;
    gchar *filter = libbalsa_condition_build_imap_query(mailbox->view_filter);

    mailbox->view->threading_type = thread_type;
    switch(thread_type) {
    case LB_MAILBOX_THREADING_FLAT:
        if(filter) {
            imap_sort_filter(mimap->handle,
                             lbmi_get_imap_sort_key(mailbox),
                             mailbox->view->sort_type ==
			     LB_MAILBOX_SORT_TYPE_ASC,
                             filter);
            new_tree =
                g_node_copy(imap_mbox_handle_get_thread_root(mimap->handle));
        } else {
            new_tree = g_node_new(NULL);
            for(msgno = 1; msgno <= mimap->messages_info->len; msgno++)
                g_node_append_data(new_tree, GUINT_TO_POINTER(msgno));
        }
        break;
    case LB_MAILBOX_THREADING_SIMPLE:
    case LB_MAILBOX_THREADING_JWZ:
	imap_mbox_thread(mimap->handle, "REFERENCES", filter ? filter : "ALL");
	new_tree =
            g_node_copy(imap_mbox_handle_get_thread_root(mimap->handle));
	break;
    default:
	g_assert_not_reached();
	new_tree = NULL;
    }

    if (new_tree)
	libbalsa_mailbox_set_msg_tree(mailbox, new_tree);
}

static void
lbm_imap_update_view_filter(LibBalsaMailbox   *mailbox,
                            LibBalsaCondition *view_filter)
{
    if(mailbox->view_filter)
        libbalsa_condition_free(mailbox->view_filter);
    mailbox->view_filter = view_filter;
    libbalsa_mailbox_imap_set_threading(mailbox,
                                        mailbox->view->threading_type);
}

static gint
lbmi_compare_func(const SortTuple * a,
		  const SortTuple * b,
		  gboolean ascending)
{
    if(ascending)
        return GPOINTER_TO_INT(a->node->data) - GPOINTER_TO_INT(b->node->data);
    else
        return GPOINTER_TO_INT(b->node->data) - GPOINTER_TO_INT(a->node->data);
}

static void
libbalsa_mailbox_imap_sort(LibBalsaMailbox *mbox, GArray *array)
{
    unsigned *msgno_arr, *msgno_map, len, i, no_max;
    GArray *tmp;
    len = array->len;

    if(mbox->view->sort_field == LB_MAILBOX_SORT_NO
	|| mbox->view->sort_field == LB_MAILBOX_SORT_NATURAL) {
        g_array_sort_with_data(array, (GCompareDataFunc)lbmi_compare_func,
                               GINT_TO_POINTER(mbox->view->sort_type ==
                                               LB_MAILBOX_SORT_TYPE_ASC));
        return;
    }
    if(mbox->view->threading_type != LB_MAILBOX_THREADING_FLAT)
        return; /* IMAP threading has an own way of sorting messages.
                 * We could possibly disable sort buttons if threading
                 * is used. */
    msgno_arr = g_malloc(len*sizeof(unsigned));
    no_max = 0;
    for(i=0; i<len; i++) {
        msgno_arr[i] = 
            GPOINTER_TO_UINT(g_array_index(array, SortTuple, i).node->data);
        if(msgno_arr[i]> no_max)
            no_max = msgno_arr[i];
    }
    msgno_map  = g_malloc(no_max*sizeof(unsigned));
    for(i=0; i<len; i++)
        msgno_map[msgno_arr[i]-1] = i;

    qsort(msgno_arr, len, sizeof(msgno_arr[0]), cmp_msgno);
    imap_sort_msgno(LIBBALSA_MAILBOX_IMAP(mbox)->handle,
                    lbmi_get_imap_sort_key(mbox),
                    mbox->view->sort_type == LB_MAILBOX_SORT_TYPE_ASC,
                    msgno_arr, len); /* ignore errors */
    
    tmp = g_array_new(FALSE,FALSE, sizeof(SortTuple));
    g_array_append_vals(tmp, array->data, array->len);
    g_array_set_size(array, 0);    /* truncate */

    for(i=0; i<len; i++)
        g_array_append_val(array, 
                           g_array_index(tmp,SortTuple, 
                                         msgno_map[msgno_arr[i]-1]));
    g_array_free(tmp, TRUE);
    g_free(msgno_arr);
    g_free(msgno_map);
}

static guint
libbalsa_mailbox_imap_total_messages(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mimap = (LibBalsaMailboxImap *) mailbox;

    return mimap->messages_info ? mimap->messages_info->len : 0;
}
