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

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include <gnome.h> /* for gnome-i18n.h, gnome-config and gnome-util */

#include "filter-funcs.h"
#include "filter.h"
#include "mailbox-filter.h"
#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
#include "imap-handle.h"
#include "imap-commands.h"
#include "imap-server.h"


struct _LibBalsaMailboxImap {
    LibBalsaMailboxRemote mailbox;
    ImapMboxHandle *handle;     /* stream that has this mailbox selected */

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
						    int op,
						    GSList* conditions);
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
static gboolean libbalsa_mailbox_imap_can_match(LibBalsaMailbox * mbox,
						GSList * conditions);
static void libbalsa_mailbox_imap_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

static gboolean libbalsa_mailbox_imap_close_backend(LibBalsaMailbox * mailbox);
static gboolean libbalsa_mailbox_imap_sync(LibBalsaMailbox * mailbox);
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
static void libbalsa_mailbox_imap_set_threading(LibBalsaMailbox *mailbox,
						LibBalsaMailboxThreadingType
						thread_type);

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
    libbalsa_mailbox_class->close_backend =
	libbalsa_mailbox_imap_close_backend;
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
    libbalsa_mailbox_class->set_threading =
	libbalsa_mailbox_imap_set_threading;
}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox)
{
    mailbox->path = NULL;
    mailbox->auth_type = AuthCram;	/* reasonable default */
    mailbox->matching_messages = NULL;
    mailbox->op = FILTER_NOOP;
    mailbox->conditions = NULL;

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

    /* libmutt sometimes forcibly fastclose's mailbox, take precautions */
    RETURN_VAL_IF_CONTEXT_CLOSED(mailbox, FALSE);

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
    }
    return mimap->handle;
}

#define RELEASE_HANDLE(mailbox,handle) \
    libbalsa_imap_server_release_handle( \
		LIBBALSA_IMAP_SERVER(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox)),\
		handle)

static void
lbimap_update_flags(LibBalsaMessage *message, ImapMessage *imsg)
{
}

static void
imap_flags_cb(unsigned seqno, LibBalsaMailboxImap *mimap)
{
    struct message_info *msg_info = message_info_from_msgno(mimap, seqno);
    ImapMessage *imsg  = imap_mbox_handle_get_msg(mimap->handle, seqno);
    lbimap_update_flags(msg_info->message, imsg);
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
    if(mimap->handle)
        RELEASE_HANDLE(mimap, mimap->handle);
    mimap->handle =
        libbalsa_imap_server_get_handle_with_user(imap_server, mimap);
    if (!mimap->handle)
	return NULL;
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
    /* test exist */
    LIBBALSA_MAILBOX(mimap)->total_messages
	= imap_mbox_handle_get_exists(mimap->handle);

    imap_handle_set_flagscb(mimap->handle, (ImapFlagsCb)imap_flags_cb, mimap);
    return mimap->handle;
}

/* libbalsa_mailbox_imap_open:
   opens IMAP mailbox. On failure leaves the object in sane state.
   FIXME:
   should intelligently use auth_type field 
*/
static gboolean
libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mimap;
    LibBalsaServer *server;
    int i;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);

    if (MAILBOX_OPEN(mailbox)) {
	/* increment the reference count */
	mailbox->open_ref++;
	UNLOCK_MAILBOX(mailbox);
	return TRUE;
    }

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    mimap->opened = FALSE;
    mimap->handle = libbalsa_mailbox_imap_get_selected_handle(mimap);
    if (!mimap->handle) {
	mailbox->disconnected = TRUE;
	UNLOCK_MAILBOX(mailbox);
	return FALSE;
    }

    mimap->opened = TRUE;
    mailbox->messages = 0;
    mailbox->unread_messages = 0;
    mailbox->total_messages = imap_mbox_handle_get_exists(mimap->handle);
    mimap->messages_info = g_array_sized_new(FALSE, TRUE, 
					     sizeof(struct message_info),
					     mailbox->total_messages);
    for(i=0; i<mailbox->total_messages; i++) {
	struct message_info a = {0};
	g_array_append_val(mimap->messages_info, a);
    }

    if(mailbox->open_ref == 0)
	    libbalsa_notify_unregister_mailbox(mailbox);
    /* increment the reference count */
    mailbox->open_ref++;

    UNLOCK_MAILBOX(mailbox);
    run_filters_on_reception(mimap);

#ifdef DEBUG
    g_print(_("%s: Opening %s Refcount: %d\n"),
	    "LibBalsaMailboxImap", mailbox->name, mailbox->open_ref);
#endif
    mailbox->disconnected = FALSE;
    return TRUE;
}

static void
libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox)
{
    if(mailbox->open_ref == 1) { /* about to close */
        /* FIXME: save headers differently: save_to_cache(mailbox); */
        clean_cache(mailbox);
    }
    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
	(*LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox) 
	    (LIBBALSA_MAILBOX(mailbox));
    if(mailbox->open_ref == 0) {
	LibBalsaMailboxImap * mbox = LIBBALSA_MAILBOX_IMAP(mailbox);

	libbalsa_notify_register_mailbox(mailbox);
	if (mbox->matching_messages) {
	    g_hash_table_destroy(mbox->matching_messages);
	    mbox->matching_messages = NULL;
	}
	if (mbox->conditions) {
	    libbalsa_conditions_free(mbox->conditions);
	    mbox->conditions = NULL;
	    mbox->op = FILTER_NOOP;
	}
    }
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
    stream = fopen(msg_name,"rb");
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
   Only open mailboxes are checked, although closed can be checked too
   with OPTIMAPPASIVE option set.
   NOTE: mx_check_mailbox can close mailbox(). Be cautious.
*/
static void
libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));
    
    if (mailbox->open_ref == 0) {
	if (libbalsa_notify_check_mailbox(mailbox) )
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE);
    } else {
	libbalsa_mailbox_imap_noop(LIBBALSA_MAILBOX_IMAP(mailbox));

	run_filters_on_reception(LIBBALSA_MAILBOX_IMAP(mailbox));
    }
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
						 int op, GSList * conditions,
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
    query = libbalsa_filter_build_imap_query(op, conditions, only_recent);
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
				    int op, GSList* conditions)
{
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
					     flt->conditions))
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
							flt->conditions_op,
							flt->conditions,
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
gboolean libbalsa_mailbox_imap_can_match(LibBalsaMailbox* mailbox,
					 GSList * cnds)
{
    for (; cnds; cnds = g_slist_next(cnds)) {
	LibBalsaCondition * cnd = cnds->data;
	
	if (cnd->type==CONDITION_REGEX) return FALSE;
    }
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
 */

void
libbalsa_mailbox_imap_noop(LibBalsaMailboxImap* mimap)
{
    g_return_if_fail(mimap->handle);
    imap_mbox_handle_noop(mimap->handle);
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

    RELEASE_HANDLE(imap, handle);
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
     * they do not remove the mailbox from the subscription list. */
    imap_mbox_subscribe(handle, mailbox->path, FALSE);
    imap_mbox_delete(handle, mailbox->path);

    RELEASE_HANDLE(mailbox, handle);
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

gboolean libbalsa_mailbox_imap_close_backend(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    mimap->opened=FALSE;
    
    return TRUE;
}

gboolean libbalsa_mailbox_imap_sync(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    g_return_val_if_fail(mimap->opened, FALSE);

    return imap_mbox_expunge(mimap->handle) == IMR_OK;
}

static LibBalsaAddress *
libbalsa_address_new_from_imap_address(ImapAddress *addr)
{
    LibBalsaAddress *address;

    if (!addr || (addr->name==NULL && addr->addr_spec==NULL))
       return NULL;

    address = libbalsa_address_new();

    /* it will be owned by the caller */

    address->full_name = g_strdup(addr->name);
    if (addr->addr_spec)
       address->address_list = g_list_append(address->address_list,
                                             g_strdup(addr->addr_spec));

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

static gboolean
libbalsa_mailbox_imap_load_envelope(LibBalsaMailboxImap *mimap,
				    LibBalsaMessage *message)
{
    struct message_info *msg_info;
    ImapResponse rc;
    ImapEnvelope *envelope;
    int msgno, lo, hi;
    ImapMessage* imsg;
    
    g_return_val_if_fail(mimap->opened, FALSE);
    msg_info = message_info_from_msgno(mimap, message->msgno);
    msgno = message->msgno;
    lo = msgno>30 ? msgno-30 : 1; /* extra care when treating unsigned */
    hi = msgno + 30;
    rc = imap_mbox_handle_fetch_range(mimap->handle, lo, hi, 
                                      IMFETCH_ENV|IMFETCH_RFC822SIZE);
    if (rc != IMR_OK)
	return FALSE;

    imsg = imap_mbox_handle_get_msg(mimap->handle, message->msgno);

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

    envelope = imsg->envelope;
    message->subj = g_mime_utils_8bit_header_decode(envelope->subject);
    message->headers->date = envelope->date;
    message->length        = imsg->rfc822size;
    message->headers->from =
	libbalsa_address_new_from_imap_address(envelope->from);
    message->sender =
	libbalsa_address_new_from_imap_address(envelope->sender);
    message->headers->reply_to =
	libbalsa_address_new_from_imap_address(envelope->replyto);
    message->headers->to_list =
	libbalsa_address_new_list_from_imap_address_list(envelope->to);
    message->headers->cc_list =
	libbalsa_address_new_list_from_imap_address_list(envelope->cc);
    message->headers->bcc_list =
	libbalsa_address_new_list_from_imap_address_list(envelope->bcc);
    libbalsa_message_set_in_reply_to_from_string(message,
						 envelope->in_reply_to);
    if (envelope->message_id) {
	message->message_id =
	    g_mime_utils_decode_message_id(envelope->message_id);
    }

    return TRUE;
}

LibBalsaMessage*
libbalsa_mailbox_imap_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;
    LibBalsaMailboxImap *mimap;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);
    g_return_val_if_fail (msgno > 0, NULL);

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
	    LOCK_MAILBOX_RETURN_VAL(LIBBALSA_MAILBOX(mimap),NULL);
	    libbalsa_message_set_icons(msg);
	    UNLOCK_MAILBOX(LIBBALSA_MAILBOX(mimap));
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
    lbbody->mime_type = g_ascii_strdown(imap_body_get_mime_type(imap_body),-1);
    lbbody->filename  = g_strdup(imap_body->desc);
    lbbody->filename  = g_strdup(imap_body_get_param(imap_body, "name"));
    lbbody->charset   = g_strdup(imap_body_get_param(imap_body, "charset"));
    if(imap_body->envelope)
            g_warning("%s: implement envelope\n", __func__);
    if(imap_body->child) {
        LibBalsaMessageBody *body = libbalsa_message_body_new(lbbody->message);
        lbm_imap_construct_body(body, imap_body->child);
        lbbody->parts = body;
    }
    if(imap_body->next) {
        LibBalsaMessageBody *body = libbalsa_message_body_new(lbbody->message);
        lbm_imap_construct_body(body, imap_body->next);
        lbbody->next = body;
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
            GString *s)
{
    int i = 1;
    for(i=1; body;  body = body->next) {
        if(body->body_type == LIBBALSA_MESSAGE_BODY_TYPE_MULTIPART)
            continue;
        if(body==child) {
            g_string_printf(s, "%u", i);
            return TRUE;
        }
        if(is_child_of(body->parts, child, s)) {
            char buf[12];
            snprintf(buf, sizeof(buf), "%u.", i);
            g_string_prepend(s, buf);
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

    if(!is_child_of(msg->body_list, part, section)) {
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
    if(!part->buffer) {
        struct part_data dt;
        LibBalsaMailboxImap* mimap = LIBBALSA_MAILBOX_IMAP(msg->mailbox);
        gchar *section = get_section_for(msg, part);
        ImapMessage *im = imap_mbox_handle_get_msg(mimap->handle, msg->msgno);
        ImapResponse rc;
        dt.body  = imap_message_get_body_from_section(im, section);
        dt.block = g_malloc(dt.body->octets+1);
        dt.pos   = 0;
        rc = imap_mbox_handle_fetch_body(mimap->handle, msg->msgno, section,
                                         append_str, &dt);
        if(rc != IMR_OK)
            g_error("FIXME: error handling here!\n");
        part->buffer = dt.block;
        part->buflen = dt.body->octets;
        g_free(section);
    }
    *sz = part->buflen;
    return part->buffer;
}

/* libbalsa_mailbox_imap_add_message: 
   can be called for a closed mailbox.
*/
int
libbalsa_mailbox_imap_add_message(LibBalsaMailbox * mailbox,
                                  LibBalsaMessage * message )
{
    ImapMsgFlags imap_flags = IMAP_FLAGS_EMPTY;
    ImapResponse rc;
    gchar *mtext;
    size_t len;
    ImapMboxHandle *handle;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    g_object_ref ( G_OBJECT(message ) );

    if ( LIBBALSA_MESSAGE_IS_UNREAD(message) )
	IMSG_FLAG_SET(imap_flags,IMSGF_SEEN);
    if ( LIBBALSA_MESSAGE_IS_DELETED(message ) ) 
	IMSG_FLAG_SET(imap_flags,IMSGF_DELETED);
    if ( LIBBALSA_MESSAGE_IS_FLAGGED(message) )
	IMSG_FLAG_SET(imap_flags,IMSGF_FLAGGED);
    if ( LIBBALSA_MESSAGE_IS_REPLIED(message) )
	IMSG_FLAG_SET(imap_flags,IMSGF_ANSWERED);

    LOCK_MAILBOX_RETURN_VAL(mailbox, -1);
    
    /* suck alert: use get_message_stream instead */
    mtext = g_mime_message_to_string (message->mime_msg); 
    len = strlen (mtext);

    handle = libbalsa_mailbox_imap_get_handle(LIBBALSA_MAILBOX_IMAP(mailbox));
    rc = imap_mbox_append_str(handle, LIBBALSA_MAILBOX_IMAP(mailbox)->path,
                              imap_flags, len, mtext);
    RELEASE_HANDLE(mailbox, handle);

    g_free (mtext);
    g_object_unref ( G_OBJECT(message ) );    
    UNLOCK_MAILBOX(mailbox);
    
    return rc == IMR_OK ? 1 : -1;
}

void
libbalsa_mailbox_imap_change_message_flags(LibBalsaMailbox * mailbox,
                                           guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    ImapMboxHandle *handle = mimap->handle;

    if (set & LIBBALSA_MESSAGE_FLAG_REPLIED)
	imap_mbox_store_flag(handle, msgno, IMSGF_ANSWERED, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_REPLIED)
	imap_mbox_store_flag(handle, msgno, IMSGF_ANSWERED, 0);

    if (set & LIBBALSA_MESSAGE_FLAG_NEW)
	imap_mbox_store_flag(handle, msgno, IMSGF_SEEN, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_NEW)
	imap_mbox_store_flag(handle, msgno, IMSGF_SEEN, 0);

    if (set & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	imap_mbox_store_flag(handle, msgno, IMSGF_FLAGGED, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	imap_mbox_store_flag(handle, msgno, IMSGF_FLAGGED, 0);

    if (set & LIBBALSA_MESSAGE_FLAG_DELETED)
	imap_mbox_store_flag(handle, msgno, IMSGF_DELETED, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_DELETED)
	imap_mbox_store_flag(handle, msgno, IMSGF_DELETED, 0);

#if 0
    /* This flag can't be turned on again. */
    if (set & LIBBALSA_MESSAGE_FLAG_RECENT)
	imap_mbox_store_flag(handle, msgno, IMSGF_RECENT, 1);
    /* ...or turned off. */
    if (clear & LIBBALSA_MESSAGE_FLAG_RECENT)
	imap_mbox_store_flag(handle, msgno, IMSGF_RECENT, 0);
#endif
    RELEASE_HANDLE(mailbox, handle);
}

static void
libbalsa_mailbox_imap_set_threading(LibBalsaMailbox *mailbox,
				    LibBalsaMailboxThreadingType thread_type)
{
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    GNode * new_tree;
    int msgno;

    switch(thread_type) {
    case LB_MAILBOX_THREADING_FLAT:
	new_tree = g_node_new(NULL);
	for(msgno = 1; msgno <= mailbox->total_messages; msgno++)
	    g_node_append_data(new_tree, GUINT_TO_POINTER(msgno));
	break;
    case  LB_MAILBOX_THREADING_SIMPLE:
    case LB_MAILBOX_THREADING_JWZ:
	imap_mbox_thread(mimap->handle, "REFERENCES");
	new_tree =
            g_node_copy(imap_mbox_handle_get_thread_root(mimap->handle));
	break;
    default:
	g_assert_not_reached();
	new_tree = NULL;
    }
    if(!new_tree) return;
    if(mailbox->msg_tree)
	g_node_destroy(mailbox->msg_tree);
    mailbox->msg_tree = new_tree;

    /* FIXME: do not only just invalidate iterators, force update as well */
    mailbox->stamp++;
}
