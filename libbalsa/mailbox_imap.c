/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

/* NOTES:
   persistent cache is implemented using a directory. At the moment,
   only message bodies are cached, mailbox scanning is not because it
   requires quite extensive messing with entire libmutt/libbalsa connection
   - some code is present (load_cache/save_to_cache) but not fully
   functional - yet.
*/
#include "config.h"
#include <dirent.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include <gnome.h> /* for gnome-i18n.h, gnome-config and gnome-util */

#include "filter.h"
#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"
#include "mx.h"
#include "imap/message.h"
#include "imap/imap_private.h"
#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#include "imap/imap.h"
#include "mutt_socket.h"

#define IMAP_MESSAGE_UID(msg) (((IMAP_HEADER_DATA*)(msg)->header->data)->uid)

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_imap_destroy(GtkObject * object);
static void libbalsa_mailbox_imap_class_init(LibBalsaMailboxImapClass *
					     klass);
static void libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox);
static gboolean libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox);
static LibBalsaMailboxAppendHandle* 
libbalsa_mailbox_imap_append(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox);
static FILE *libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox *
						      mailbox,
						      LibBalsaMessage *
						      message);
static void libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox);
static GHashTable* libbalsa_mailbox_imap_get_matching(LibBalsaMailbox* mailbox,
                                                      int op,
                                                      GSList* conditions);

static void libbalsa_mailbox_imap_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

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


GtkType libbalsa_mailbox_imap_get_type(void)
{
    static GtkType mailbox_type = 0;

    if (!mailbox_type) {
	static const GtkTypeInfo mailbox_info = {
	    "LibBalsaMailboxImap",
	    sizeof(LibBalsaMailboxImap),
	    sizeof(LibBalsaMailboxImapClass),
	    (GtkClassInitFunc) libbalsa_mailbox_imap_class_init,
	    (GtkObjectInitFunc) libbalsa_mailbox_imap_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	mailbox_type =
	    gtk_type_unique(libbalsa_mailbox_remote_get_type(),
			    &mailbox_info);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_imap_class_init(LibBalsaMailboxImapClass * klass)
{
    GtkObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = GTK_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = gtk_type_class(libbalsa_mailbox_remote_get_type());

    object_class->destroy = libbalsa_mailbox_imap_destroy;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_imap_open;
    libbalsa_mailbox_class->open_mailbox_append = libbalsa_mailbox_imap_append;
    libbalsa_mailbox_class->close_mailbox = libbalsa_mailbox_imap_close;
    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_imap_get_message_stream;

    libbalsa_mailbox_class->check = libbalsa_mailbox_imap_check;
    libbalsa_mailbox_class->get_matching = libbalsa_mailbox_imap_get_matching;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_imap_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_imap_load_config;

    ImapCheckTimeout = 10;
}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox)
{
    LibBalsaMailboxRemote *remote;
    mailbox->path = NULL;
    mailbox->auth_type = AuthCram;	/* reasonable default */

    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    remote->server =
	LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_IMAP));
    gtk_object_ref(GTK_OBJECT(remote->server));
    gtk_object_sink(GTK_OBJECT(remote->server));

    gtk_signal_connect(GTK_OBJECT(remote->server), "set-username",
		       GTK_SIGNAL_FUNC(server_user_settings_changed_cb),
		       (gpointer) mailbox);
    gtk_signal_connect(GTK_OBJECT(remote->server), "set-password",
		       GTK_SIGNAL_FUNC(server_user_settings_changed_cb),
		       (gpointer) mailbox);
    gtk_signal_connect(GTK_OBJECT(remote->server), "set-host",
		       GTK_SIGNAL_FUNC(server_host_settings_changed_cb),
		       (gpointer) mailbox);
}

/* libbalsa_mailbox_imap_destroy:
   NOTE: we have to close mailbox ourselves without waiting for
   LibBalsaMailbox::destroy because we want to destroy server as well,
   and close requires server for proper operation.  
*/
static void
libbalsa_mailbox_imap_destroy(GtkObject * object)
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
	gtk_object_unref(GTK_OBJECT(remote->server));
	remote->server = NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

GtkObject *
libbalsa_mailbox_imap_new(void)
{
    LibBalsaMailbox *mailbox;
    mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_IMAP);

    return GTK_OBJECT(mailbox);
}

/* libbalsa_mailbox_imap_update_url:
   this is to be used only by mailboxImap functions, with exception
   for the folder scanner, which has to go around libmutt limitations.
*/
void
libbalsa_mailbox_imap_update_url(LibBalsaMailboxImap* mailbox)
{
    LibBalsaServer* s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    g_free(LIBBALSA_MAILBOX(mailbox)->url);
    LIBBALSA_MAILBOX(mailbox)->url =  
        g_strdup_printf("imap%s://%s/%s", 
#ifdef USE_SSL
                        s->use_ssl ? "s" : "",
#else
                        "",
#endif
                        s->host, mailbox->path? mailbox->path : "");
}

/* Unregister an old notification and add a current one */
static void
server_settings_changed(LibBalsaServer * server, LibBalsaMailbox * mailbox)
{
    fprintf(stderr, "changing server settings for '%s' (%p)\n",
	    mailbox->url ? mailbox->url : "", mailbox->url);
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
    if (ImapUser)
	safe_free((void **) &ImapUser);	/* because mutt does so */
    ImapUser = strdup(server->user);

    if (ImapPass)
	safe_free((void **) &ImapPass);	/* because mutt does so */
    ImapPass = strdup(server->passwd);
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
    if(msg->flags & LIBBALSA_MESSAGE_FLAG_NEW)
        append_string('n',data, &alen, &len, buf);
    if(msg->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
        append_string('d',data, &alen, &len, buf);
    if(msg->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
        append_string('f',data, &alen, &len, buf);
    if(msg->flags & LIBBALSA_MESSAGE_FLAG_REPLIED)
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

    if(CLIENT_CONTEXT(mailbox)==NULL) {
        printf("No mutt context available to save.\n");
        return TRUE;
    }
    fname = get_cache_name(LIBBALSA_MAILBOX_IMAP(mailbox), "headers");
    libbalsa_assure_balsa_dir();

    printf("Cache file: %s\n", fname);
    dbf = gdbm_open(fname, 0, GDBM_WRITER, S_IRUSR| S_IWUSR, NULL);
    g_free(fname);
    if(!dbf) return FALSE;

    uid[0] = ((IMAP_DATA*)CLIENT_CONTEXT(mailbox)->data)->uid_validity;
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
#endif

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
    if(CLIENT_CONTEXT(mailbox)->hdrs == NULL) {
        g_warning("Client context for mailbox %s closed in an ugly way, why?",
                  mailbox->name);
        return FALSE;
    }

    dir = opendir(fname);
    printf("Attempting to clean IMAP cache for '%s'\n", mailbox->name);
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

/* libbalsa_mailbox_imap_open:
   opens IMAP mailbox. On failure leaves the object in sane state.
   FIXME:
   should intelligently use auth_type field 
*/
static gboolean
libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *imap;
    LibBalsaServer *server;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
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
    imap = LIBBALSA_MAILBOX_IMAP(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    /* try getting password, quit on cancel */
    if (!server->passwd &&
	!(server->passwd = libbalsa_server_get_password(server, mailbox))) {
	mailbox->disconnected = TRUE;
	UNLOCK_MAILBOX(mailbox);
	return FALSE;
    }
    gdk_threads_leave();
    libbalsa_lock_mutt();
    reset_mutt_passwords(server);
    CLIENT_CONTEXT(mailbox) = mx_open_mailbox(mailbox->url, 0, NULL);
    libbalsa_unlock_mutt();

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
	mailbox->readonly = CLIENT_CONTEXT(mailbox)->readonly;
	mailbox->messages = 0;
	mailbox->total_messages = 0;
	mailbox->unread_messages = 0;
	mailbox->new_messages = CLIENT_CONTEXT(mailbox)->msgcount;
        LIBBALSA_MAILBOX_IMAP(mailbox)->uid_validity = 
            ((IMAP_DATA*)CLIENT_CONTEXT(mailbox)->data)->uid_validity;
	if(mailbox->open_ref == 0)
	    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));
	/* increment the reference count */
	mailbox->open_ref++;

	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
	libbalsa_mailbox_load_messages(mailbox);
#ifdef DEBUG
	g_print(_("LibBalsaMailboxImap: Opening %s Refcount: %d\n"),
		mailbox->name, mailbox->open_ref);
#endif
    } else {
	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
    }
    mailbox->disconnected = FALSE;
    return CLIENT_CONTEXT_OPEN(mailbox);
}

static LibBalsaMailboxAppendHandle* 
libbalsa_mailbox_imap_append(LibBalsaMailbox * mailbox)
{
    LibBalsaServer *server;
    LibBalsaMailboxAppendHandle* res = g_new0(LibBalsaMailboxAppendHandle,1);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    libbalsa_lock_mutt();
    reset_mutt_passwords(server);
    res->context = mx_open_mailbox(mailbox->url, M_APPEND, NULL);

    if(res->context == NULL) {
	g_free(res);
	res = NULL;
    } else if (res->context->readonly) {
	g_warning("Cannot open dest local mailbox '%s' for writing.", 
		  mailbox->name);
	mx_close_mailbox(res->context, NULL);
	g_free(res);
	res = NULL;
    }
    libbalsa_unlock_mutt();
    return res;
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
    if(mailbox->open_ref == 0)
	libbalsa_notify_register_mailbox(LIBBALSA_MAILBOX(mailbox));
}


/* libbalsa_mailbox_imap_get_message_stream: 
   Fetch data from cache first, if available.
   When calling imap_fetch_message(), we make use of fact that
   imap_fetch_message doesn't set msg->path field.
*/
static FILE *
libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox * mailbox,
					 LibBalsaMessage * message)
{
    FILE *stream = NULL;
    gchar* msg_name, *cache_name;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    RETURN_VAL_IF_CONTEXT_CLOSED(message->mailbox, FALSE);

    cache_name = get_cache_name(LIBBALSA_MAILBOX_IMAP(mailbox), "body");
    msg_name   = g_strdup_printf("%s/%u-%u", cache_name, 
                                 LIBBALSA_MAILBOX_IMAP(mailbox)->uid_validity,
                                 IMAP_MESSAGE_UID(message));
    stream = fopen(msg_name,"rb");
    if(!stream) {
        MESSAGE *msg = safe_calloc(1, sizeof(MESSAGE));
        msg->magic = CLIENT_CONTEXT(mailbox)->magic;
        libbalsa_lock_mutt();
        if (!imap_fetch_message(msg, CLIENT_CONTEXT(mailbox), 
                                message->header->msgno)) 
            stream = msg->fp;
	FREE(&msg);
	if(stream) { /* don't cache negatives */
            FILE * cache;
            libbalsa_assure_balsa_dir();
            mkdir(cache_name, S_IRUSR|S_IWUSR|S_IXUSR); /* ignore errors */
            cache = fopen(msg_name,"wb");
            if(! (cache && mutt_copy_stream(stream, cache) ==0) ) 
		g_warning("Writing to cache file '%s' failed.", msg_name);
            fclose(cache);
	    rewind(stream);
	}
        libbalsa_unlock_mutt();
    }
    g_free(msg_name);
    g_free(cache_name); 
    return stream;
}

/* libbalsa_mailbox_imap_check:
   checks imap mailbox for new messages.
   Only open mailboxes are checked, although closed can be checked too
   with OPTIMAPPASIVE option set.
   NOTE: mx_check_mailbox can close mailbox(). Be cautious.
   We have to set Timeout to 0 because mutt would not allow checking several
   mailboxes in row.
*/
static void
libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox)
{
    if(mailbox->open_ref == 0) {
	if (libbalsa_notify_check_mailbox(mailbox) ) 
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE); 
    } else {
	gint i = 0;
	long newmsg, timeout;
	gint index_hint;
	g_return_if_fail(CLIENT_CONTEXT(mailbox));
	
	LOCK_MAILBOX(mailbox);
	newmsg = CLIENT_CONTEXT(mailbox)->msgcount  
	    - CLIENT_CONTEXT(mailbox)->deleted - mailbox->messages;
	index_hint = CLIENT_CONTEXT(mailbox)->vcount;

	libbalsa_lock_mutt();
	imap_allow_reopen(CLIENT_CONTEXT(mailbox));
	timeout = Timeout; Timeout = -1;
	i = mx_check_mailbox(CLIENT_CONTEXT(mailbox), &index_hint, 0);
	Timeout = timeout;
	libbalsa_unlock_mutt();

	if (i < 0) {
	    g_print("mx_check_mailbox() failed on %s\n", mailbox->name);
	    if(CLIENT_CONTEXT_CLOSED(mailbox)||
	       !CLIENT_CONTEXT(mailbox)->id_hash)
		libbalsa_mailbox_free_messages(mailbox);
            /* send close signall as well? */
	} 
	if (newmsg || i == M_NEW_MAIL || i == M_REOPENED) {
	    mailbox->new_messages =
		CLIENT_CONTEXT(mailbox)->msgcount - mailbox->messages;
	    
	    UNLOCK_MAILBOX(mailbox);
	    libbalsa_mailbox_load_messages(mailbox);
	} else {
            /* update flags here */
	    UNLOCK_MAILBOX(mailbox);
	}
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
        printf("Could not find UID: %ud in message list\n", uid);
}

int imap_uid_search(CONTEXT* ctx, const char* query, 
                    void(*cb)(unsigned, ImapSearchData*), void*);
static GHashTable*
libbalsa_mailbox_imap_get_matching(LibBalsaMailbox* mailbox, 
                                  int op, GSList* conditions)
{
    gchar* query;
    gboolean match;
    ImapSearchData cbdata;
    GList* msgs;

    cbdata.uids = g_hash_table_new(NULL, NULL);
    cbdata.res  = g_hash_table_new(NULL, NULL);
    query = libbalsa_filter_build_imap_query(op, conditions);
    if(query) {
        for(msgs= mailbox->message_list; msgs; msgs = msgs->next){
            LibBalsaMessage *m = LIBBALSA_MESSAGE(msgs->data);
            unsigned uid = ((IMAP_HEADER_DATA*)m->header->data)->uid;
            g_hash_table_insert(cbdata.uids, GUINT_TO_POINTER(uid), m);
        }
        
        match = imap_uid_search(CLIENT_CONTEXT(mailbox), query,
                                imap_matched, &cbdata);
        g_free(query);
    }
    g_hash_table_destroy(cbdata.uids);
    return cbdata.res;
}

static void
libbalsa_mailbox_imap_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxImap *imap;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    imap = LIBBALSA_MAILBOX_IMAP(mailbox);

    gnome_config_set_string("Path", imap->path);

    libbalsa_server_save_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxImap *imap;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    imap = LIBBALSA_MAILBOX_IMAP(mailbox);

    g_free(imap->path);
    imap->path = gnome_config_get_string("Path");

    libbalsa_server_load_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);

    server_settings_changed(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
			    mailbox);
    libbalsa_mailbox_imap_update_url(LIBBALSA_MAILBOX_IMAP(mailbox));
}

gboolean
libbalsa_mailbox_imap_subscribe(LibBalsaMailboxImap * mailbox, 
				     gboolean subscribe)
{
    gboolean res;
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), FALSE);
    libbalsa_lock_mutt();
    res = (imap_subscribe(LIBBALSA_MAILBOX(mailbox)->url, subscribe) == 0);
    libbalsa_unlock_mutt();
    return res;
}

/* imap_close_all_connections:
   close all connections to leave the place cleanly.
*/
void
libbalsa_imap_close_all_connections(void)
{
    imap_logout_all();
}

/* libbalsa_imap_rename_subfolder:
   dir+parent determine current name. 
   folder - new name.
 */
gboolean
libbalsa_imap_rename_subfolder(LibBalsaMailboxImap* mbox,
                               const gchar *new_parent, const gchar *folder, 
                               gboolean subscribe)
{
    int res;
    LibBalsaMailbox* m = LIBBALSA_MAILBOX(mbox);
    libbalsa_lock_mutt();
    res = (imap_mailbox_rename(m->url, new_parent, folder, subscribe)==0);
    libbalsa_unlock_mutt();
    return res;
}

void
libbalsa_imap_new_subfolder(const gchar *parent, const gchar *folder,
			    gboolean subscribe, LibBalsaServer *server)
{
    gchar *imap_path = g_strdup_printf("imap%s://%s/%s",
#ifdef USE_SSL
				       server->use_ssl ? "s" : "",
#else
				       "",
#endif
				       server->host, parent);

    imap_mailbox_create(imap_path, folder, subscribe);
    g_free(imap_path);
}

void
libbalsa_imap_delete_folder(LibBalsaMailboxImap *mailbox)
{

    /* Some IMAP servers (UW2000) do not like removing subscribed mailboxes:
     * they do not remove the mailbox from the subscription list. */
    imap_subscribe(LIBBALSA_MAILBOX(mailbox)->url, FALSE);
    /*
	should be able to do this using the existing public method
	from libmutt/imap/imap.h:
    imap_delete_mailbox(CLIENT_CONTEXT(LIBBALSA_MAILBOX(mailbox)),
			mailbox->path);
	but it segfaults because ctx->data is NULL
	instead of being a pointer to an IMAP_DATA structure!

	instead we'll use our own new method:
	FIXME: this is one, big, ugly HACK.
    */
    libbalsa_mailbox_imap_open(LIBBALSA_MAILBOX(mailbox));
    imap_mailbox_delete(LIBBALSA_MAILBOX(mailbox)->url);
    libbalsa_mailbox_imap_close(LIBBALSA_MAILBOX(mailbox));
}



