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
   persistent cache is implemented using a directory. At the moment,
   only message bodies are cached, mailbox scanning is not because it
   requires quite extensive messing with entire libmutt/libbalsa connection
   - some code is present (load_cache/save_to_cache) but not fully
   functional - yet.
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

struct message_info {
    GMimeMessage *mime_message;
    ImapMessage *msg;
    ImapUID uid;
    LibBalsaMessageFlag flags;
    LibBalsaMessageFlag orig_flags;
    LibBalsaMessage *message;
};

#define IMAP_MESSAGE_UID(msg) ( message_info_from_msgno( \
				 LIBBALSA_MAILBOX_IMAP((msg)->mailbox), \
				 (msg)->msgno \
				)->uid)
#define IMAP_MAILBOX_UID_VALIDITY(mailbox) (LIBBALSA_MAILBOX_IMAP(mailbox)->uid_validity)

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
static GMimeMessage *libbalsa_mailbox_imap_get_message(
						  LibBalsaMailbox * mailbox,
						  guint msgno);
static LibBalsaMessage *libbalsa_mailbox_imap_load_message(
						  LibBalsaMailbox * mailbox,
						  guint msgno);
static int libbalsa_mailbox_imap_add_message(LibBalsaMailbox * mailbox,
					     GMimeStream *stream,
					     LibBalsaMessageFlag flags);
void libbalsa_mailbox_imap_change_message_flags(LibBalsaMailbox * mailbox,
						guint msgno,
						LibBalsaMessageFlag set,
						LibBalsaMessageFlag clear);

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
    struct message_info *msg_info = NULL;

    if (msgno >= mimap->msgno_2_msg_info->len) {
	g_ptr_array_set_size(mimap->msgno_2_msg_info,
			     msgno+1);
	msg_info = g_new0(struct message_info, 1);
	g_ptr_array_index(mimap->msgno_2_msg_info, msgno) =
	    msg_info;
    } else
	msg_info = g_ptr_array_index(mimap->msgno_2_msg_info,
			      msgno);
    return msg_info;
}

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
    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_imap_get_message_stream;

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
    libbalsa_mailbox_class->close_backend = libbalsa_mailbox_imap_close_backend;
    libbalsa_mailbox_class->get_message = libbalsa_mailbox_imap_get_message;
    libbalsa_mailbox_class->load_message = libbalsa_mailbox_imap_load_message;
    libbalsa_mailbox_class->add_message = libbalsa_mailbox_imap_add_message;
    libbalsa_mailbox_class->change_message_flags = libbalsa_mailbox_imap_change_message_flags;

}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox)
{
    LibBalsaMailboxRemote *remote;
    mailbox->path = NULL;
    mailbox->auth_type = AuthCram;	/* reasonable default */
    mailbox->matching_messages = NULL;
    mailbox->op = FILTER_NOOP;
    mailbox->conditions = NULL;

    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    remote->server =
	LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_IMAP));

    g_signal_connect(G_OBJECT(remote->server), "set-username",
		     G_CALLBACK(server_user_settings_changed_cb),
		     (gpointer) mailbox);
    g_signal_connect(G_OBJECT(remote->server), "set-password",
		     G_CALLBACK(server_user_settings_changed_cb),
		     (gpointer) mailbox);
    g_signal_connect(G_OBJECT(remote->server), "set-host",
		     G_CALLBACK(server_host_settings_changed_cb),
		     (gpointer) mailbox);
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

void
reset_mutt_passwords(LibBalsaServer* server)
{
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

#ifdef CACHE_IMAP_HEADERS_TOO
/* format of the header cache:
   entries are divided by '\0' characters. The list is delimited by
   header.size.
*/
static LibBalsaMessage*
message_from_header(datum header)
{
    LibBalsaMessage *msg = libbalsa_message_new();
    int curpos = 0;

    g_return_val_if_fail(header.dptr, msg); /* corrupted entry */
    while(curpos<header.dsize) {
        switch(header.dptr[curpos++]) {
        case 'N': msg->msgno     = atoi(header.dptr+curpos);     break;
        case 'l': msg->length    = atoi(header.dptr+curpos);     break;
        case 'L': msg->lines_len = atoi(header.dptr+curpos);     break;
        case 'n': msg->flags |= LIBBALSA_MESSAGE_FLAG_NEW;       break;
        case 'd': msg->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;   puts("d");break;
        case 'f': msg->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;   break;
        case 'r': msg->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;   break;
        case 'D': msg->date = atol(header.dptr+curpos);          break;
        case 'F':
            msg->from = libbalsa_address_new_from_string(header.dptr+curpos);
            break;
        case 'S':
            msg->sender = libbalsa_address_new_from_string(header.dptr+curpos);
            break;
        case 'E':
            msg->reply_to = 
                libbalsa_address_new_from_string(header.dptr+curpos);
            break;
        case 'P':
            msg->dispnotify_to = 
                libbalsa_address_new_from_string(header.dptr+curpos);
            break;
        case 'T':
            msg->to_list = 
                g_list_prepend(msg->to_list, 
                              libbalsa_address_new_from_string(header.dptr
                                                               +curpos));
            break;
        case 'C':
            msg->cc_list = 
                g_list_prepend(msg->cc_list, 
                              libbalsa_address_new_from_string(header.dptr
                                                               +curpos));
            break;
        case 'B':
            msg->bcc_list = 
                g_list_prepend(msg->bcc_list, 
                              libbalsa_address_new_from_string(header.dptr
                                                               +curpos));
            break;
        case 'M':
            msg->fcc_mailbox = g_strdup(header.dptr+curpos);
            break;
        case 'U':
            msg->subj = g_strdup(header.dptr+curpos);
            break;
        case 'I':
            msg->message_id = g_strdup(header.dptr+curpos);
            break;
        case 'R':
            msg->references_for_threading =
		g_list_prepend(msg->references_for_threading,
			       g_strdup(header.dptr+curpos));
            break;
            
        }
        curpos += strlen(header.dptr+curpos)+1;
    }
    msg->references =
	g_list_reverse(g_list_copy(msg->references_for_threading));
    msg->to_list = g_list_reverse(msg->to_list);
    msg->cc_list = g_list_reverse(msg->cc_list);
    msg->bcc_list = g_list_reverse(msg->bcc_list);
    return msg;
}

static void
append_string(char prefix, char* data, int* alen, int* len, const gchar* str)
{
    int slen = strlen(str)+2; /* prefix char and separating '\0' */

    if(*len + slen>*alen)
        data = realloc(data, *alen += (slen > 1024 ? slen : 1024));
    data[*len] = prefix;
    strcpy(&data[1+*len], str);
    *len += slen;
}
    
static void
setdatum_from_message(datum* header, LibBalsaMessage* msg)
{
    char* data;
    int len = 0, alen;
    char buf[20];
    gchar *tmp;
    GList* lst;

    data = malloc(alen = 1024);
    sprintf(buf, "%d", msg->msgno);     
    append_string('N',data, &alen, &len, buf);
    sprintf(buf, "%d", msg->length);    
    append_string('l',data, &alen, &len, buf);
    sprintf(buf, "%d", msg->lines_len); 
    append_string('L',data, &alen, &len, buf);
    buf[0] ='\0';
    if (LIBBALSA_MESSAGE_IS_UNREAD(msg))
        append_string('n',data, &alen, &len, buf);
    if (LIBBALSA_MESSAGE_IS_DELETED(msg))
        append_string('d',data, &alen, &len, buf);
    if (LIBBALSA_MESSAGE_IS_FLAGGED(msg))
        append_string('f',data, &alen, &len, buf);
    if (LIBBALSA_MESSAGE_IS_REPLIED(msg))
        append_string('r',data, &alen, &len, buf);
    sprintf(buf, "%ld", msg->date); 
    append_string('D',data, &alen, &len, buf);

    if(msg->from) {
        tmp = libbalsa_address_to_gchar(msg->from, 0);
        append_string('F',data, &alen, &len, tmp); g_free(tmp);
    }
    if(msg->sender) {
        tmp = libbalsa_address_to_gchar(msg->sender, 0);
        append_string('S',data, &alen, &len, tmp); g_free(tmp);
    }
    if(msg->reply_to) {
        tmp = libbalsa_address_to_gchar(msg->reply_to, 0);
        append_string('E',data, &alen, &len, tmp); g_free(tmp);
    }
    if(msg->dispnotify_to) {
        tmp = libbalsa_address_to_gchar(msg->dispnotify_to, 0);
        append_string('P',data, &alen, &len, tmp); g_free(tmp);
    }
    for(lst = msg->to_list; lst; lst = lst->next) {
        tmp = libbalsa_address_to_gchar(lst->data, 0);
        append_string('T',data, &alen, &len, tmp); g_free(tmp);
    }
    for(lst = msg->cc_list; lst; lst = lst->next) {
        tmp = libbalsa_address_to_gchar(lst->data, 0);
        append_string('C',data, &alen, &len, tmp); g_free(tmp);
    }
    for(lst = msg->bcc_list; lst; lst = lst->next) {
        tmp = libbalsa_address_to_gchar(lst->data, 0);
        append_string('B',data, &alen, &len, tmp); g_free(tmp);
    }
    if(msg->fcc_mailbox) 
        append_string('M',data, &alen, &len, msg->fcc_mailbox);
    if(msg->subj)       append_string('U',data, &alen, &len, msg->subj);
    if(msg->message_id) append_string('I',data, &alen, &len, msg->message_id);
    for(lst = msg->references; lst; lst = lst->next)
        append_string('R',data, &alen, &len, lst->data); 

    header->dptr  = data;
    header->dsize = len;
}

/* load_cache:
   loads data from cache, if available.
   returns TRUE if complete data was available, FALSE else.
*/
static gboolean
load_cache(LibBalsaMailbox* mailbox)
{
    GDBM_FILE dbf;
    datum key, nextkey, header;
    LibBalsaMessage* message;
    gchar* fname =  get_cache_name(LIBBALSA_MAILBOX_IMAP(mailbox), "headers");

    dbf = gdbm_open(fname, 0, GDBM_READER, 0, NULL);
    g_free(fname);
    printf("Attempting to load data from cache for '%s'\n", mailbox->name);
    if(!dbf) return FALSE;

    key = gdbm_firstkey(dbf);
    while (key.dptr) {
        header = gdbm_fetch(dbf, key);
        message = message_from_header(header);
        free(header.dptr);
        libbalsa_mailbox_link_message(mailbox, message);
        nextkey = gdbm_nextkey(dbf, key);
        free(key.dptr); key = nextkey;
    }

    gdbm_close(dbf);
    return TRUE;
}

static gboolean
save_to_cache(LibBalsaMailbox* mailbox)
{
    GDBM_FILE dbf;
    datum key, rec;
    GList* lst;
    ImapUID uid[2];
    gchar* fname;

    if(mimap==NULL) {
        printf("No mutt context available to save.\n");
        return TRUE;
    }
    fname = get_cache_name(LIBBALSA_MAILBOX_IMAP(mailbox), "headers");
    libbalsa_assure_balsa_dir();

    printf("Cache file: %s\n", fname);
    dbf = gdbm_open(fname, 0, GDBM_WRITER, S_IRUSR| S_IWUSR, NULL);
    g_free(fname);
    if(!dbf) return FALSE;

    uid[0] = IMAP_MAILBOX_UID_VALIDITY(mailbox);
    key.dptr  = (char*)uid;
    key.dsize = sizeof(ImapUID)*2;
    for(lst = mailbox->message_list; lst; lst = lst->next) {
        setdatum_from_message(&rec, LIBBALSA_MESSAGE(lst->data));
        uid[1] = IMAP_MESSAGE_UID(LIBBALSA_MESSAGE(lst->data));
        gdbm_store(dbf, key, rec, GDBM_REPLACE);
        free(rec.dptr);
    }
    gdbm_close(dbf);
    printf("Cache data saved for mailbox %s\n", mailbox->name);
    return TRUE;
}
#endif /* CACHE_IMAP_HEADERS_TOO */

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

    for(lst = mailbox->message_list; lst; lst = lst->next) {
        ImapUID u = IMAP_MESSAGE_UID(LIBBALSA_MESSAGE(lst->data));
        g_hash_table_insert(present_uids, UID_TO_POINTER(u), &present_uids);
    }
                            
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

static void
monitor_cb(const char *buffer, int length, int direction, void *arg)
{
#if 1
  int i;
  printf("%c: ", direction ? 'C' : 'S');
  for(i=0; i<length; i++) putchar(buffer[i]);
  fflush(NULL);
#endif
}

static void
set_status(const gchar* str, void *arg)
{
  puts(str);
}

static void
flags_cb(unsigned seqno, void* h)
{
  char seq[12];
  ImapMessage *msg = imap_mbox_handle_get_msg((ImapMboxHandle*)h, seqno);
  if(!msg) return;
  sprintf(seq, "%4d:%c%c%c%c%c", seqno, 
  (IMSG_FLAG_ANSWERED(msg->flags) ? 'A' : '-'),
  (IMSG_FLAG_FLAGGED(msg->flags)  ? 'F' : '-'),
  (IMSG_FLAG_DELETED(msg->flags)  ? 'D' : '-'),
  (IMSG_FLAG_SEEN(msg->flags)     ? '-' : 'N'),
  (IMSG_FLAG_DRAFT(msg->flags)    ? 'u' : '-'));
}

ImapMboxHandle *
libbalsa_mailbox_imap_get_handle(LibBalsaMailboxImap *mimap,
						 LibBalsaServer *server)
{
    ImapMboxHandle *handle = NULL;

    g_return_val_if_fail(mimap != NULL || server != NULL, NULL);

    if (mimap && mimap->handle ) {
	handle = mimap->handle;
	g_object_ref(handle);
    } else if (server
	       && (handle = g_object_get_data(G_OBJECT(server),
					      "imap-handle")) != NULL) {
	g_object_ref(handle);
    } else {
	ImapResult rc;
	if (mimap)
	    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mimap);

	handle = imap_mbox_handle_new();
	imap_handle_set_monitorcb(handle, monitor_cb, NULL);
	imap_handle_set_infocb(handle,    set_status, NULL);
	imap_handle_set_alertcb(handle,   set_status, NULL);
	imap_handle_set_flagscb(handle,   flags_cb, handle);
	rc=imap_mbox_handle_connect(handle,
				    server->host, 143,
				    server->user, server->passwd);
	if(rc != IMAP_SUCCESS) {
	    g_object_unref(handle);
	    return NULL;
	}

#if SHARED_CONNECTION
	g_object_ref(handle);
	g_object_set_data_full(G_OBJECT(server), "imap-handle",
			       handle, g_object_unref);
#endif
    }
    return handle;
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
    ImapMboxHandle* handle;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);

    if (MAILBOX_OPEN(mailbox)) {
	/* increment the reference count */
	mailbox->open_ref++;
	UNLOCK_MAILBOX(mailbox);
	return TRUE;
    }

    /* FIXME: temporarily disabled, until better way of loading headers
       is invented. */
#if defined(HAVE_GDBM_H) && defined(CACHE_IMAP_HEADERS_TOO)
      if(load_cache(mailbox)) {
	mailbox->open_ref++;
	UNLOCK_MAILBOX(mailbox);
	return TRUE;
      } 
#endif
    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    /* try getting password, quit on cancel */
    if (!server->passwd &&
	!(server->passwd = libbalsa_server_get_password(server, mailbox))) {
	mailbox->disconnected = TRUE;
	UNLOCK_MAILBOX(mailbox);
	return FALSE;
    }

    handle = libbalsa_mailbox_imap_get_handle(mimap, server);
    if (!handle) {
	mailbox->disconnected = TRUE;
	UNLOCK_MAILBOX(mailbox);
	return FALSE;
    }

    mimap->handle = handle;

    if(imap_mbox_select(handle, mimap->path, &mailbox->readonly) == IMR_OK) {
	mimap->msgno_2_msg_info = g_ptr_array_new();
	mailbox->messages = 0;
	mailbox->total_messages = 0;
	mailbox->unread_messages = 0;
	mailbox->new_messages =
	    imap_mbox_handle_get_exists(mimap->handle);
	if(mailbox->open_ref == 0)
	    libbalsa_notify_unregister_mailbox(mailbox);
	/* increment the reference count */
	mailbox->open_ref++;

	UNLOCK_MAILBOX(mailbox);
	libbalsa_mailbox_load_messages(mailbox);
	run_filters_on_reception(mimap);

#ifdef DEBUG
	g_print(_("%s: Opening %s Refcount: %d\n"),
		"LibBalsaMailboxImap", mailbox->name, mailbox->open_ref);
#endif
    } else {
	UNLOCK_MAILBOX(mailbox);
    }
    mailbox->disconnected = FALSE;
    return MAILBOX_OPEN(mailbox);
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

	/* pb: not needed? the parent class's close_mailbox method
	 * seems to unref and clear the handle using
	 * libbalsa_mailbox_imap_close_backend. */
	if (mbox->handle) {
	    g_object_unref(mbox->handle);
	    mbox->handle = NULL;
	}

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
    unsigned uidval = imap_mbox_handle_get_validity(mimap->handle);

    cache_name = get_cache_name(mimap, "body");
    msg_name   = g_strdup_printf("%s/%u-%u", cache_name, uidval, uid);
    stream = fopen(msg_name,"rb");
    if(!stream) {
        FILE *cache;
	ImapResponse rc;

        imap_mbox_select(mimap->handle, mimap->path, &mailbox->readonly);

        libbalsa_assure_balsa_dir();
        mkdir(cache_name, S_IRUSR|S_IWUSR|S_IXUSR); /* ignore errors */
        cache = fopen(msg_name, "wb");
        rc = imap_mbox_handle_fetch_rfc822_uid(mimap->handle, uid, cache);
        fclose(cache);
    }
    stream = fopen(msg_name,"rb");
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
    ImapMboxHandle* handle;
    ImapSearchData * cbdata;
    GList* msgs;

    *err = FALSE;
    
    cbdata = g_new( ImapSearchData, 1 );
    cbdata->uids = g_hash_table_new(NULL, NULL); 
    cbdata->res  = g_hash_table_new(NULL, NULL);
    query = libbalsa_filter_build_imap_query(op, conditions, only_recent);
    if (query) {
	for(msgs= LIBBALSA_MAILBOX(mbox)->message_list; msgs;
	    msgs = msgs->next){
	    LibBalsaMessage *m = LIBBALSA_MESSAGE(msgs->data);
	    ImapUID uid = IMAP_MESSAGE_UID(m);
	    g_hash_table_insert(cbdata->uids, GUINT_TO_POINTER(uid), m);
	}
	
	handle = libbalsa_mailbox_imap_get_handle(mbox, NULL);
	if (handle) {
	    rc = imap_mbox_uid_search(handle, query, imap_matched, cbdata);
	    g_object_unref(handle);
	    g_free(query);
	}
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
			     _("IMAP SEARCH command failed for mailbox %s\n"
			       "or query was incompatible with IMAP (perhaps you use regular expressions)\n"
			       "falling back to default searching method"),
			     LIBBALSA_MAILBOX(mbox)->url);	
	libbalsa_mailbox_real_mbox_match(mbox, filters_list);
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

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);

    g_free(mimap->path);
    mimap->path = gnome_config_get_string("Path");

    libbalsa_server_load_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

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
    ImapMboxHandle* handle;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);

    handle = libbalsa_mailbox_imap_get_handle(mailbox, NULL);
    if (!handle)
	return FALSE;

    rc = imap_mbox_subscribe(handle, mailbox->path, subscribe);

    g_object_unref(handle);
    return rc == IMAP_SUCCESS;
}

/* libbalsa_mailbox_imap_noop:
 * pings the connection with NOOP for an open IMAP mailbox.
 * this keeps the connections alive.
 */

void
libbalsa_mailbox_imap_noop(LibBalsaMailboxImap* mimap)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(mimap);
    long exists;

    if(MAILBOX_CLOSED(mailbox)) return;
    LOCK_MAILBOX(mailbox);
    imap_mbox_handle_noop(mimap->handle);
    exists = imap_mbox_handle_get_exists(mimap->handle);
    if (exists != mailbox->messages) {
	mailbox->new_messages = exists - mailbox->messages;
	
	if (mimap->matching_messages) {
	    g_hash_table_destroy(mimap->matching_messages);
	    mimap->matching_messages = NULL;
	}
	if (mimap->conditions) {
	    libbalsa_conditions_free(mimap->conditions);
	    mimap->conditions = NULL;
	    mimap->op = FILTER_NOOP;
	}

	UNLOCK_MAILBOX(mailbox);
	libbalsa_mailbox_load_messages(mailbox);
    } else {
	/* update flags here */
	UNLOCK_MAILBOX(mailbox);
    }
}

/* imap_close_all_connections:
   close all connections to leave the place cleanly.
*/
void
libbalsa_imap_close_all_connections(void)
{
#if FIXME
    imap_logout_all();
#endif
}

/* libbalsa_imap_rename_subfolder:
   dir+parent determine current name. 
   folder - new name.
 */
gboolean
libbalsa_imap_rename_subfolder(LibBalsaMailboxImap* imap,
                               const gchar *new_parent, const gchar *folder, 
                               gboolean subscribe)
{
    ImapResult rc;
    gchar *new_path;
    ImapMboxHandle* handle;

    handle = libbalsa_mailbox_imap_get_handle(imap, NULL);
    if (!handle)
	return FALSE;

    imap_mbox_subscribe(handle, imap->path, FALSE);
    /* FIXME: should use imap server folder separator */ 
    new_path = g_strjoin("/", new_parent, folder, NULL);
    rc = imap_mbox_rename(handle, imap->path, new_path);
    if (subscribe && rc == IMAP_SUCCESS)
	rc = imap_mbox_subscribe(handle, new_path, TRUE);
    g_free(new_path);

    g_object_unref(handle);
    return rc == IMAP_SUCCESS;
}

void
libbalsa_imap_new_subfolder(const gchar *parent, const gchar *folder,
			    gboolean subscribe, LibBalsaServer *server)
{
    ImapResult rc;
    ImapMboxHandle* handle;
    gchar *new_path;

    handle = libbalsa_mailbox_imap_get_handle(NULL, server);
    if (!handle)
	return;

    /* FIXME: should use imap server folder separator */ 
    new_path = g_strjoin("/", parent, folder, NULL);
    rc = imap_mbox_create(handle, new_path);
    if (subscribe && rc == IMAP_SUCCESS)
	rc = imap_mbox_subscribe(handle, new_path, TRUE);
    g_free(new_path);

    g_object_unref(handle);
}

void
libbalsa_imap_delete_folder(LibBalsaMailboxImap *mailbox)
{
    ImapMboxHandle* handle;

    handle = libbalsa_mailbox_imap_get_handle(mailbox, NULL);
    if (!handle)
	return;

    /* Some IMAP servers (UW2000) do not like removing subscribed mailboxes:
     * they do not remove the mailbox from the subscription list. */
    imap_mbox_subscribe(handle, mailbox->path, FALSE);
    imap_mbox_delete(handle, mailbox->path);

    g_object_unref(handle);
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
    g_object_unref(G_OBJECT(LIBBALSA_MAILBOX_IMAP(mailbox)->handle));
    LIBBALSA_MAILBOX_IMAP(mailbox)->handle = NULL; 
    // chbm: unref ? do we ever keep more than ref to this ? 
    
    return TRUE;
}

gboolean libbalsa_mailbox_imap_sync(LibBalsaMailbox * mailbox)
{
#if FIXME
    libbalsa_lock_mutt();
    index_hint = mimap->vcount;
    rc = mx_sync_mailbox(mimap, &index_hint);
    libbalsa_unlock_mutt();
    if(rc==0) {
        mailbox->messages = mimap->msgcount;
	return TRUE;
    }
    if (rc == M_NEW_MAIL || rc == M_REOPENED) {
	mailbox->new_messages =
	    mimap->msgcount - mailbox->messages;
	return TRUE;
    }
#endif

    return TRUE;//FALSE;
}

GMimeMessage*
libbalsa_mailbox_imap_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    struct message_info *msg_info;
    LibBalsaMailboxImap *mimap;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    msg_info = message_info_from_msgno(mimap, msgno);

    if (!msg_info)
	return NULL;

    if (!msg_info->mime_message)
    {
	GMimeStream *gmime_stream;
	GMimeStream *filter_stream;
	GMimeFilter *filter;
	GMimeParser *gmime_parser;
        ImapMessage *imsg = msg_info->msg;
        FILE *cache;
        
        cache = get_cache_stream(mailbox, imsg->uid);
	g_assert(msg_info->msg);
        
	gmime_stream = g_mime_stream_file_new(cache);
	filter_stream = g_mime_stream_filter_new_with_stream(gmime_stream);
	filter = g_mime_filter_crlf_new( GMIME_FILTER_CRLF_DECODE,
					 GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
	g_object_unref(G_OBJECT(filter));
	gmime_parser = g_mime_parser_new_with_stream(filter_stream);
	g_mime_stream_unref(filter_stream);

	g_mime_parser_set_scan_from(gmime_parser, FALSE);
	msg_info->mime_message = g_mime_parser_construct_message(gmime_parser);

	g_object_unref(G_OBJECT(gmime_parser));
	g_mime_stream_unref(gmime_stream);
    }
    return msg_info->mime_message;
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

static void
libbalsa_mailbox_imap_load_envelope(LibBalsaMailboxImap *mimap,
				    LibBalsaMessage *message)
{
    struct message_info *msg_info;
    char *seq;
    ImapResponse rc;
    ImapEnvelope *envelope;

    msg_info = message_info_from_msgno(mimap, message->msgno);
    seq = g_strdup_printf("%ld", message->msgno+1);
    rc = imap_mbox_select(mimap->handle, mimap->path,
			  &(LIBBALSA_MAILBOX(mimap)->readonly));
    rc = imap_mbox_handle_fetch_env(mimap->handle, seq);
    g_free(seq);

    msg_info->msg = imap_mbox_handle_get_msg(mimap->handle, message->msgno+1);

    if (!IMSG_FLAG_SEEN(msg_info->msg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
    if (IMSG_FLAG_DELETED(msg_info->msg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
    if (IMSG_FLAG_FLAGGED(msg_info->msg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
    if (IMSG_FLAG_ANSWERED(msg_info->msg->flags))
        message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;
    if (IMSG_FLAG_RECENT(msg_info->msg->flags))
	message->flags |= LIBBALSA_MESSAGE_FLAG_RECENT;
    message->flags = msg_info->flags = msg_info->orig_flags;

    envelope = msg_info->msg->envelope;
    message->subj = g_mime_utils_8bit_header_decode(envelope->subject);
    message->headers->date = envelope->date;
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
    message->in_reply_to =
	g_mime_utils_8bit_header_decode(envelope->in_reply_to);
    if (envelope->message_id) {
	gchar *message_id;
	message_id = g_mime_utils_decode_message_id(envelope->message_id);
	message->message_id = g_strdup_printf("<%s>", message_id);
	g_free(message_id);
    }
}

LibBalsaMessage *libbalsa_mailbox_imap_load_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message;
    struct message_info *msg_info;
    LibBalsaMailboxImap *mimap;

    g_return_val_if_fail (LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);

    mailbox->new_messages--;

    mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    msg_info = message_info_from_msgno(mimap, msgno);

    if (!msg_info)
	return NULL;

    mailbox->messages++;

    message = libbalsa_message_new();
    message->msgno = msgno;
    libbalsa_mailbox_imap_load_envelope(mimap, message);

#if 0
    message->mime_msg = msg_info->mime_message;

#ifdef MESSAGE_COPY_CONTENT
    header =
	g_mime_message_get_header(msg_info->mime_message, "Content-Length");
    msg_info->length = 0;
    if (header)
	    msg_info->length=atoi(header);

    header = g_mime_message_get_header(msg_info->mime_message, "Lines");
    msg_info->lines = 0;
    if (header)
	    msg_info->lines=atoi(header);
#endif
#endif

    return message;
}

int libbalsa_mailbox_imap_add_message(LibBalsaMailbox * mailbox,
				      GMimeStream *stream,
				      LibBalsaMessageFlag flags)
{
    ImapMsgFlags imap_flags = IMAP_FLAGS_EMPTY;
    GMimeStream *mem_stream;
    ImapResponse rc;
    gchar *message;
    size_t len;
    ImapMboxHandle *handle;

    if ((flags & LIBBALSA_MESSAGE_FLAG_NEW) == 0)
	IMSG_FLAG_SET(imap_flags,IMSGF_SEEN);
    if (flags & LIBBALSA_MESSAGE_FLAG_DELETED)
	IMSG_FLAG_SET(imap_flags,IMSGF_DELETED);
    if (flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	IMSG_FLAG_SET(imap_flags,IMSGF_FLAGGED);
    if (flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
	IMSG_FLAG_SET(imap_flags,IMSGF_ANSWERED);

    mem_stream = g_mime_stream_mem_new();
    g_mime_stream_write_to_stream(stream, mem_stream);
    message = GMIME_STREAM_MEM(mem_stream)->buffer->data;
    len = g_mime_stream_length(mem_stream);

    /* remove From_ line */
    if (strncmp("From ", message, 5) == 0) {
	message += 5;
	len -= 5;
	while (len && *message!='\n') {
	    len--;
	    message++;
	}
	len--;
	message++;
    }
    if (!len)
	return -1;

    handle = libbalsa_mailbox_imap_get_handle(LIBBALSA_MAILBOX_IMAP(mailbox),
					      NULL);
    rc = imap_mbox_append_str(handle, LIBBALSA_MAILBOX_IMAP(mailbox)->path,
                              imap_flags, len, message);
    g_object_unref(handle);
    g_mime_stream_unref(mem_stream);
    return rc == IMAP_SUCCESS ? 1 : -1;
}

void libbalsa_mailbox_imap_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    int seq = msgno + 1;
    LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
    ImapMboxHandle *handle = mimap->handle;

    imap_mbox_select(handle, mimap->path, &mailbox->readonly);

    if (set & LIBBALSA_MESSAGE_FLAG_REPLIED)
	imap_mbox_store_flag(handle, seq, IMSGF_ANSWERED, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_REPLIED)
	imap_mbox_store_flag(handle, seq, IMSGF_ANSWERED, 0);

    if (set & LIBBALSA_MESSAGE_FLAG_NEW)
	imap_mbox_store_flag(handle, seq, IMSGF_SEEN, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_NEW)
	imap_mbox_store_flag(handle, seq, IMSGF_SEEN, 0);

    if (set & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	imap_mbox_store_flag(handle, seq, IMSGF_FLAGGED, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_FLAGGED)
	imap_mbox_store_flag(handle, seq, IMSGF_FLAGGED, 0);

    if (set & LIBBALSA_MESSAGE_FLAG_DELETED)
	imap_mbox_store_flag(handle, seq, IMSGF_DELETED, 1);
    if (clear & LIBBALSA_MESSAGE_FLAG_DELETED)
	imap_mbox_store_flag(handle, seq, IMSGF_DELETED, 0);

#if 0
    /* This flag can't be turned on again. */
    if (set & LIBBALSA_MESSAGE_FLAG_RECENT)
	imap_mbox_store_flag(handle, seq, IMSGF_RECENT, 1);
    /* ...or turned off. */
    if (clear & LIBBALSA_MESSAGE_FLAG_RECENT)
	imap_mbox_store_flag(handle, seq, IMSGF_RECENT, 0);
#endif
}
