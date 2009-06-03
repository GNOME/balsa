/* -*-mode:c; c-basic-offset:4; -*- */
/*
  LibBalsaImapServer is a class for managing connections to one IMAP
  server. Idle connections are disconnected after a timeout, or when
  the user switches to offline mode.
*/

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "imap-server.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#if defined(HAVE_GNOME_KEYRING)
#include <gnome-keyring.h>
#endif

#include "libbalsa.h"
#include "libbalsa-conf.h"
#include "server.h"

#include "imap-handle.h"
#include "imap-commands.h"
#include <glib/gi18n.h>

#ifdef USE_TLS
#define REQ_SSL(s) (LIBBALSA_SERVER(s)->use_ssl)
#else
#define REQ_SSL(s) (0)
#endif

/** wait 60 seconds for packets */
#define IMAP_CMD_TIMEOUT (60*1000)

static LibBalsaServerClass *parent_class = NULL;

struct LibBalsaImapServer_ {
    LibBalsaServer server;
    
    guint connection_cleanup_id;
    gchar *key;
    guint max_connections;
    gboolean offline_mode;
    
#if defined(BALSA_USE_THREADS)
    GMutex *lock; /* protects the following members */
#endif
    guint used_connections;
    GList *used_handles;
    GList *free_handles;
    unsigned persistent_cache:1; /* if TRUE, messages will be cached in
                                    $HOME and preserved between
                                    sessions. If FALSE, messages will be
                                    kept in /tmp and cleaned on exit. */
    unsigned has_fetch_bug:1;
    unsigned use_status:1; /**< server has fast STATUS command */
    unsigned use_idle:1;  /**< IDLE will work: no dummy firewall on the way */
};

typedef struct LibBalsaImapServerClass_ {
    LibBalsaServerClass parent_class;

} LibBalsaImapServerClass;

static void libbalsa_imap_server_class_init(LibBalsaImapServerClass * klass);
static void libbalsa_imap_server_init(LibBalsaImapServer * server);
static void libbalsa_imap_server_finalize(GObject * object);
static gboolean connection_cleanup(gpointer ptr);

/* Poll every 5 minutes - must be shortest ouf of the times here. */
#define CONNECTION_CLEANUP_POLL_PERIOD  (2*60)
/* Cleanup connections more then 10 minutes idle */
#define CONNECTION_CLEANUP_IDLE_TIME    (10*60)
/* Send NOOP after 20 minutes to keep a connection alive */
#define CONNECTION_CLEANUP_NOOP_TIME    (20*60)
/* We try to avoid too many connections per server */
#define MAX_CONNECTIONS_PER_SERVER 20

#if defined(BALSA_USE_THREADS)
static pthread_mutex_t imap_servers_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_SERVERS()   pthread_mutex_lock(&imap_servers_lock)
#define UNLOCK_SERVERS() pthread_mutex_unlock(&imap_servers_lock)
#define LOCK_SERVER(server)    g_mutex_lock((server)->lock)
#define TRYLOCK_SERVER(server) g_mutex_trylock((server)->lock)
#define UNLOCK_SERVER(server)  g_mutex_unlock((server)->lock)
#else
#define LOCK_SERVERS()
#define UNLOCK_SERVERS()
#define LOCK_SERVER(server)
#define TRYLOCK_SERVER(server) TRUE
#define UNLOCK_SERVER(server)
#endif
static GHashTable *imap_servers = NULL;

struct handle_info {
    ImapMboxHandle *handle;
    time_t last_used;
    void *last_user;
};

static int by_handle(gconstpointer a, gconstpointer b)
{
    return ((struct handle_info*)a)->handle != b;
}

static int by_last_user(gconstpointer a, gconstpointer b)
{
    return ((struct handle_info*)a)->last_user != b;
}

GType
libbalsa_imap_server_get_type(void)
{
    static GType server_type = 0;

    if (!server_type) {
        static const GTypeInfo server_info = {
            sizeof(LibBalsaImapServerClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) libbalsa_imap_server_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibBalsaImapServer),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) libbalsa_imap_server_init
        };

        server_type =
            g_type_register_static(LIBBALSA_TYPE_SERVER, "LibBalsaImapServer",
                                   &server_info, 0);
    }

    return server_type;
}

static void libbalsa_imap_server_set_username(LibBalsaServer * server,
                                              const gchar * name)
{
    if(server->host && name) { /* we have been initialized... */
        LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
        
        LOCK_SERVERS();
        g_hash_table_steal(imap_servers, imap_server->key);
        g_free(imap_server->key);
        imap_server->key = g_strdup_printf("%s@%s", name, server->host);
        g_hash_table_insert(imap_servers, imap_server->key, imap_server);
        UNLOCK_SERVERS();
    }
    (parent_class)->set_username(server, name);
}
static void
libbalsa_imap_server_set_host(LibBalsaServer * server,
                              const gchar * host, gboolean use_ssl)
{
    if(server->user && host) { /* we have been initialized... */
        LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
        LOCK_SERVERS();
        g_hash_table_steal(imap_servers, imap_server->key);
        g_free(imap_server->key);
        imap_server->key = g_strdup_printf("%s@%s", server->user, host);
        g_hash_table_insert(imap_servers, imap_server->key, imap_server);
        UNLOCK_SERVERS();
    }
    (parent_class)->set_host(server, host, use_ssl);
}
static void
libbalsa_imap_server_class_init(LibBalsaImapServerClass * klass)
{
    GObjectClass *object_class;
    LibBalsaServerClass *server_class;

    object_class = G_OBJECT_CLASS(klass);
    server_class = LIBBALSA_SERVER_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_imap_server_finalize;

    server_class->set_username = libbalsa_imap_server_set_username;
    server_class->set_host = libbalsa_imap_server_set_host;
#if 0
    klass->get_password = NULL; /* libbalsa_imap_server_real_get_password; */
#endif
}

static void
libbalsa_imap_server_init(LibBalsaImapServer * imap_server)
{
    LIBBALSA_SERVER(imap_server)->protocol = "imap";
    imap_server->key = NULL;
#if defined(BALSA_USE_THREADS)
    imap_server->lock = g_mutex_new();
#endif
    imap_server->max_connections = MAX_CONNECTIONS_PER_SERVER;
    imap_server->used_connections = 0;
    imap_server->used_handles = NULL;
    imap_server->free_handles = NULL;
#if defined(ENABLE_TOUCH_UI)
    imap_server->persistent_cache = FALSE;
#else
    imap_server->persistent_cache = TRUE;
#endif /* ENABLE_TOUCH_UI */
    imap_server->use_idle = TRUE;
    imap_server->connection_cleanup_id = 
        g_timeout_add(CONNECTION_CLEANUP_POLL_PERIOD*1000,
                      connection_cleanup, imap_server);
}

/* leave object in sane state (NULLified fields) */
static void
libbalsa_imap_server_finalize(GObject * object)
{
    LibBalsaServer *server;
    LibBalsaImapServer *imap_server;

    g_return_if_fail(LIBBALSA_IS_IMAP_SERVER(object));

    server = LIBBALSA_SERVER(object);
    imap_server = LIBBALSA_IMAP_SERVER(object);

    LOCK_SERVERS();
    g_hash_table_remove(imap_servers, imap_server->key);
    UNLOCK_SERVERS();
    
    g_source_remove(imap_server->connection_cleanup_id);

#if 0
    LOCK_SERVER(imap_server);
#endif
    libbalsa_imap_server_force_disconnect(imap_server);
#if defined(BALSA_USE_THREADS)
    g_mutex_free(imap_server->lock);
#endif
    g_free(imap_server->key); imap_server->key = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

gint ImapDebug = 0;
#define BALSA_TEST_IMAP 1

static void
monitor_cb(const char *buffer, int length, int direction, void *arg)
{
#if BALSA_TEST_IMAP
  if (ImapDebug) {
    const gchar *passwd = NULL;
    int i;

    if (direction) {
      const gchar *login;
      int login_length;
          
      login = g_strstr_len(buffer, length, "LOGIN ");
      if (login) {
        login_length = 6;
      } else {
        login = g_strstr_len(buffer, length, "AUTHENTICATE PLAIN");
        login_length = 18;
      }

      if (login) {
        const gchar *user = login + login_length;
        passwd = g_strstr_len(user, length - (user - buffer), " ");
        if (passwd) {
          int new_len = ++passwd - buffer;
          if (new_len < length)
            length = new_len;
          else
            passwd = NULL;
        }
      }
    }

    printf("IMAP %c: ", direction ? 'C' : 'S');
    for (i = 0; i < length; i++)
      putchar(buffer[i]);

    if (passwd)
      puts("(password hidden)");

    fflush(NULL);
  }
#endif                          /* BALSA_TEST_IMAP */
  if (direction)
    ((struct handle_info *) arg)->last_used = time(NULL);
}

static void
is_info_cb(ImapMboxHandle *h, ImapResponse rc, const gchar* str, void *arg)
{
    LibBalsaInformationType it;
    LibBalsaServer *is = LIBBALSA_SERVER(arg);
    const gchar *fmt;

    switch(rc) {
    case IMR_ALERT: /* IMAP host name + message */
        fmt = _("IMAP server %s alert:\n%s");
        it = LIBBALSA_INFORMATION_ERROR; 
        break;
    case IMR_BAD: /* IMAP host name + message */
        fmt = _("IMAP server %s error: %s");
        it = LIBBALSA_INFORMATION_ERROR;
        break;
    case IMR_BYE: 
    case IMR_NO: 
        /* IMAP host name + message */
        fmt = _("%s: %s");  it = LIBBALSA_INFORMATION_MESSAGE; break;
    default:
        return;
    }
    libbalsa_information(it, fmt, is->host, str);
}


/* Create a struct handle_info with a new handle. */
static struct handle_info *
lb_imap_server_info_new(LibBalsaServer *server)
{
    ImapMboxHandle *handle;
    struct handle_info *info;
    ImapTlsMode mode;

    /* We do not ask for password now since the authentication might
     * not require it. Instead, we handle authentication requiests in
     * libbalsa_server_user_cb(). */

    handle = imap_mbox_handle_new();
    imap_handle_set_timeout(handle, IMAP_CMD_TIMEOUT);
    info = g_new0(struct handle_info, 1);
    info->handle = handle;
    imap_handle_set_monitorcb(handle, monitor_cb, info);
    imap_handle_set_infocb(handle,    is_info_cb, server);
    imap_handle_set_usercb(handle,    libbalsa_server_user_cb, server);
    switch(server->tls_mode) {
    case LIBBALSA_TLS_DISABLED: mode = IMAP_TLS_DISABLED; break;
    default:
    case LIBBALSA_TLS_ENABLED : mode = IMAP_TLS_ENABLED;  break;
    case LIBBALSA_TLS_REQUIRED: mode = IMAP_TLS_REQUIRED; break;
    }
    imap_handle_set_tls_mode(handle, mode);
    imap_handle_set_option(handle, IMAP_OPT_ANONYMOUS, server->try_anonymous);
    imap_handle_set_option(handle, IMAP_OPT_CLIENT_SORT, TRUE);
#ifdef HAVE_GPGME
    /* binary fetches change encoding and the checksums, and
       signatures, disable them if we ever consider verifying message
       integrity. */
    imap_handle_set_option(handle, IMAP_OPT_BINARY, FALSE);
#else
    imap_handle_set_option(handle, IMAP_OPT_BINARY, TRUE);
#endif
    imap_handle_set_option(handle, IMAP_OPT_IDLE,
                           LIBBALSA_IMAP_SERVER(server)->use_idle);
    return info;
}

/* Clean up and free a struct handle_info. */
static void
lb_imap_server_info_free(struct handle_info *info)
{
    g_object_unref(info->handle);
    g_free(info);
}

/* Check handles periodically; shut down inactive ones, and send NOOP to
 * host to keep active connections alive. */
static void
lb_imap_server_cleanup(LibBalsaImapServer * imap_server)
{
    time_t idle_marker;
    GList *list;

    /* Quit if there is an action going on, eg. an connection is being
     * opened and the user is asked to confirm the certificate or
     * provide password, etc. */
    if(!TRYLOCK_SERVER(imap_server))
        return; 

    idle_marker = time(NULL) - CONNECTION_CLEANUP_IDLE_TIME;

    list = imap_server->free_handles;
    while (list) {
        GList *next = list->next;
        struct handle_info *info = list->data;

        if (info->last_used < idle_marker) {
            imap_server->free_handles =
                g_list_delete_link(imap_server->free_handles, list);
            lb_imap_server_info_free(info);
        }

        list = next;
    }

    idle_marker -=
        CONNECTION_CLEANUP_NOOP_TIME - CONNECTION_CLEANUP_IDLE_TIME;

    for (list = imap_server->used_handles; list; list = list->next) {
        struct handle_info *info = list->data;
        /* We poll selected handles each time (unless IDLE is on
           already).  Remaining handles are just kept alive. */ 
        if ( (imap_mbox_is_selected(info->handle) &&
              !imap_server->use_idle) || 
            info->last_used < idle_marker) {
            /* ignore errors here - the point is to keep the
               connection alive and if there is no connection, noop
               will be, well, no-op. Other operations may possibly
               reconnect. */
            imap_mbox_handle_noop(info->handle);
        }
    }

    UNLOCK_SERVER(imap_server);
}

static gboolean connection_cleanup(gpointer ptr)
{
#ifdef BALSA_USE_THREADS
    pthread_t cleanup_thread;
#endif                          /*BALSA_USE_THREADS */
    LibBalsaImapServer *imap_server;

    g_return_val_if_fail(LIBBALSA_IS_IMAP_SERVER(ptr), FALSE);

    imap_server = LIBBALSA_IMAP_SERVER(ptr);
#ifdef BALSA_USE_THREADS
    pthread_create(&cleanup_thread, NULL,
                   (void *) lb_imap_server_cleanup, imap_server);
    pthread_detach(cleanup_thread);
#else                           /*BALSA_USE_THREADS */
    lb_imap_server_cleanup(imap_server);
#endif                          /*BALSA_USE_THREADS */

    return TRUE;
}

static LibBalsaImapServer* get_or_create(const gchar *username,
                                         const gchar *host)
{
    LibBalsaImapServer *imap_server;
    gchar *key;

    if (!imap_servers) {
        LOCK_SERVERS();
        if (!imap_servers)
            imap_servers = g_hash_table_new(g_str_hash, g_str_equal);
        UNLOCK_SERVERS();
    }

    /* lookup username@host */
    key = g_strdup_printf("%s@%s", username, host);
    LOCK_SERVERS();
    imap_server = g_hash_table_lookup(imap_servers, key);
    if (!imap_server) {
        imap_server = g_object_new(LIBBALSA_TYPE_IMAP_SERVER, NULL);
        imap_server->key = key;
        g_hash_table_insert(imap_servers, key, imap_server);
    } else {
        g_free(key);
        g_object_ref(imap_server);
    }
    UNLOCK_SERVERS();
    return imap_server;
}

/**
 * libbalsa_imap_server_new:
 * @username: username to use to login
 * @host: hostname of server
 *
 * Creates or recycles a #LibBalsaImapServer matching the host+username pair.
 *
 * Return value: A #LibBalsaImapServer
 **/
LibBalsaImapServer* libbalsa_imap_server_new(const gchar *username,
                                             const gchar *host)
{
    return get_or_create(username, host);
}

LibBalsaImapServer*
libbalsa_imap_server_new_from_config(void)
{
    LibBalsaServer tmp_server;
    LibBalsaImapServer *imap_server;
    LibBalsaServer *server;
    gboolean d, d1;
    gint tls_mode, conn_limit;

    tmp_server.host = libbalsa_conf_get_string("Server");
    if(strrchr(tmp_server.host, ':') == NULL) {
        gint port;
        port = libbalsa_conf_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", tmp_server.host, port);
            g_free(tmp_server.host);
            tmp_server.host = newhost;
        }
    }       
    tmp_server.user = libbalsa_conf_private_get_string("Username");
    if (!tmp_server.user)
        tmp_server.user = g_strdup(getenv("USER"));

    imap_server = get_or_create(tmp_server.user, tmp_server.host);
    server = LIBBALSA_SERVER(imap_server);
    if (server->user) {
        g_free(tmp_server.user);
        g_free(tmp_server.host);
    } else {
        server->user = tmp_server.user;
        server->host = tmp_server.host;
    }
    d1 = libbalsa_conf_get_bool_with_default("Anonymous", &d);
    if(!d) server->try_anonymous = !!d1;
    server->use_ssl |= libbalsa_conf_get_bool("SSL=false");
    tls_mode = libbalsa_conf_get_int_with_default("TLSMode", &d);
    if(!d) server->tls_mode = tls_mode;
    conn_limit = libbalsa_conf_get_int_with_default("ConnectionLimit", &d);
    if(!d) imap_server->max_connections = conn_limit;
    d1 = libbalsa_conf_get_bool_with_default("PersistentCache", &d);
    if(!d) imap_server->persistent_cache = !!d1;
    d1 = libbalsa_conf_get_bool_with_default("HasFetchBug", &d);
    if(!d) imap_server->has_fetch_bug = !!d1;
    d1 = libbalsa_conf_get_bool_with_default("UseStatus", &d);
    if(!d) imap_server->use_status = !!d1;
    d1 = libbalsa_conf_get_bool_with_default("UseIdle", &d);
    if(!d) imap_server->use_idle = !!d1;
    if (!server->passwd) {
        server->remember_passwd = libbalsa_conf_get_bool("RememberPasswd=false");
        if(server->remember_passwd) {
#if defined (HAVE_GNOME_KEYRING)
	    GnomeKeyringResult r;
	    server->passwd = NULL;
	    r = gnome_keyring_find_password_sync(LIBBALSA_SERVER_KEYRING_SCHEMA,
						 &server->passwd,
						 "protocol", server->protocol,
						 "server", server->host,
						 "user", server->user,
						 NULL);
	    if(r != GNOME_KEYRING_RESULT_OK) {
		gnome_keyring_free_password(server->passwd);
		server->passwd = NULL;
		printf("Keyring has no password for %s@%s\n",
		       server->user, server->host);
		server->passwd = libbalsa_conf_private_get_string("Password");
		if (server->passwd != NULL) {
		    gchar *buff = libbalsa_rot(server->passwd);
		    libbalsa_free_password(server->passwd);
		    server->passwd = buff;
	            gnome_keyring_store_password_sync
                        (LIBBALSA_SERVER_KEYRING_SCHEMA, NULL,
                         _("Balsa passwords"), server->passwd,
                         "protocol", server->protocol,
                         "server", server->host,
                         "user", server->user,
                         NULL);
		    /* We could in principle clear the password in the
		       config file here but we do not for the backward
		       compatibility. */
		}
	    }
#else
            server->passwd = libbalsa_conf_private_get_string("Password");
	    if (server->passwd != NULL) {
		gchar *buff = libbalsa_rot(server->passwd);
		libbalsa_free_password(server->passwd);
		server->passwd = buff;
	    }
#endif
	}
        if(server->passwd && server->passwd[0] == '\0') {
            libbalsa_free_password(server->passwd);
            server->passwd = NULL;
        }
    }
    return imap_server;
}

void
libbalsa_imap_server_save_config(LibBalsaImapServer *server)
{
    libbalsa_server_save_config(LIBBALSA_SERVER(server));
    libbalsa_conf_set_int("ConnectionLimit", server->max_connections);
    libbalsa_conf_set_bool("PersistentCache", server->persistent_cache);
    libbalsa_conf_set_bool("HasFetchBug", server->has_fetch_bug);
    libbalsa_conf_set_bool("UseStatus",   server->use_status);
    libbalsa_conf_set_bool("UseIdle",     server->use_idle);
}

/* handle_connection_error() releases handle_info data, clears password
   and sets err apriopriately */
static void
handle_connection_error(int rc, struct handle_info *info,
                        LibBalsaServer *server, GError **err)
{
    gchar *msg = imap_mbox_handle_get_last_msg(info->handle);
    switch(rc) {
    case IMAP_AUTH_FAILURE:
        libbalsa_server_set_password(server, NULL); break;
    case IMAP_CONNECT_FAILED:
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_NETWORK_ERROR,
                    _("Cannot connect to %s"), server->host);
        break;
    case IMAP_AUTH_CANCELLED:
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_AUTH_CANCELLED,
                    _("Cannot connect to the server: %s"), msg);
        break;
    default:
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_AUTH_ERROR,
                    _("Cannot connect to the server: %s"), msg);
    }    
    g_free(msg);
    lb_imap_server_info_free(info);
}

/**
 * libbalsa_imap_server_get_handle:
 * @server: A #LibBalsaImapServer
 *
 * Returns a connected handle to the IMAP server, if needed it
 * connects.  If there is no password set, the user is asked to supply
 * one.  Handle is apriopriate for all commands that work in AUTHENTICATED
 * state (LIST, SUBSCRIBE, CREATE, APPEND) but it MUST not be used for
 * select -- use libbalsa_imap_server_get_handle_with_user for that purpose. 
 *
 * Return value: a handle to the server, or %NULL when there are no
 * free connections.
 **/
ImapMboxHandle*
libbalsa_imap_server_get_handle(LibBalsaImapServer *imap_server, GError **err)
{
    LibBalsaServer *server = LIBBALSA_SERVER(imap_server);
    struct handle_info *info = NULL;
    ImapResult rc;

    if (imap_server->offline_mode)
        return NULL;

    LOCK_SERVER(imap_server);
    /* look for free connection */
    if (imap_server->free_handles) {
        GList *conn;
        conn = g_list_find_custom(imap_server->free_handles, NULL,
                                  by_last_user);
        if (!conn)
            conn = g_list_first(imap_server->free_handles);
        info = (struct handle_info*)conn->data;
        imap_server->free_handles =
            g_list_delete_link(imap_server->free_handles, conn);
    }
    /* create if used < max connections */
    if (!info
        && imap_server->used_connections < imap_server->max_connections) {
        UNLOCK_SERVER(imap_server);
        info = lb_imap_server_info_new(server);
        LOCK_SERVER(imap_server);
        /* FIXME: after dropping and reacquiring the lock,
         * (imap_server->used_connections < imap_server->max_connections)
         * might no longer be true--do we care?
        if (imap_server->used_connections >= imap_server->max_connections) {
            lb_imap_server_info_free(info);
            UNLOCK_SERVER(imap_server);
            return NULL;
        }
         */
    }
    if (info) {
        if(imap_mbox_is_disconnected(info->handle)) {
            rc=imap_mbox_handle_connect(info->handle, server->host,
                                        REQ_SSL(server));
            if(rc != IMAP_SUCCESS) {
                handle_connection_error(rc, info, server, err);
                UNLOCK_SERVER(imap_server);
                return NULL;
            }
        }
        /* add handle to used list */
        imap_server->used_handles = g_list_prepend(imap_server->used_handles,
                                                   info);
        imap_server->used_connections++;
    }
    UNLOCK_SERVER(imap_server);

    return info ? info->handle : NULL;
}

/**
 * libbalsa_imap_server_get_handle_with_user:
 * @server: A #LibBalsaImapServer
 * @user: user for handle
 *
 * Returns a connected handle to the IMAP server, if needed it
 * connects.  If there is no password set, the user is asked to supply
 * one.  This function first tries to find a handle last used by
 * @user, then a handle without a user and finally the least recently
 * used. @user is usually a pointer to LibBalsaMailbox.
 *
 * Return value: a handle to the server, or %NULL when there are no free
 * connections.
 **/
ImapMboxHandle*
libbalsa_imap_server_get_handle_with_user(LibBalsaImapServer *imap_server,
                                          gpointer user, GError **err)
{
    LibBalsaServer *server = LIBBALSA_SERVER(imap_server);
    struct handle_info *info = NULL;
    ImapResult rc;

    if (imap_server->offline_mode)
        return NULL;

    LOCK_SERVER(imap_server);
    /* look for free reusable connection */
    if (imap_server->free_handles) {
        GList *conn=NULL;
        if (user)
            conn = g_list_find_custom(imap_server->free_handles, user,
                                      by_last_user);
        if (!conn)
            conn = imap_server->free_handles;
        if (conn) {
            info = (struct handle_info*)conn->data;
            imap_server->free_handles =
                g_list_delete_link(imap_server->free_handles, conn);
        }
    }
    /* create if used < max connections;
     * always leave one connection for actions without user, i.e.
     * those that do not SELECT any mailbox. */
    if (!info
        && imap_server->used_connections < imap_server->max_connections-1) {
        UNLOCK_SERVER(imap_server);
        info = lb_imap_server_info_new(server);
        if (!info)
            return NULL;
        LOCK_SERVER(imap_server);
        /* FIXME: after dropping and reacquiring the lock,
         * (imap_server->used_connections < imap_server->max_connections)
         * might no longer be true--do we care?
        if (imap_server->used_connections >= imap_server->max_connections) {
            lb_imap_server_info_free(info);
            UNLOCK_SERVER(imap_server);
            return NULL;
        }
         */
    }
    /* reuse a free connection */
    if (!info && imap_server->free_handles) {
        GList *conn;
        conn = g_list_first(imap_server->free_handles);
        info = (struct handle_info*)conn->data;
        imap_server->free_handles =
            g_list_delete_link(imap_server->free_handles, conn);
    }
    if(!info) {
        g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                    LIBBALSA_MAILBOX_TOOMANYOPEN_ERROR,
                    _("Exceeded the number of connections per server %s"),
                    server->host);
        UNLOCK_SERVER(imap_server);
        return NULL;
    }

    if (imap_mbox_is_disconnected(info->handle)) {
        rc=imap_mbox_handle_connect(info->handle, server->host,
                                    REQ_SSL(server));
        if(rc != IMAP_SUCCESS) {
            handle_connection_error(rc, info, server, err);
            UNLOCK_SERVER(imap_server);
            return NULL;
        }
    }
    /* add handle to used list */
    info->last_user = user;
    imap_server->used_handles = g_list_prepend(imap_server->used_handles,
                                               info);
    imap_server->used_connections++;
    UNLOCK_SERVER(imap_server);

    return info->handle;
}

/**
 * libbalsa_imap_server_release_handle:
 * @server: A #LibBalsaImapServer
 * @handle: The handle to release
 *
 * Releases the @handle to the connection cache.
 **/
void libbalsa_imap_server_release_handle(LibBalsaImapServer *imap_server,
                                         ImapMboxHandle* handle)
{
    struct handle_info *info = NULL;

    if (!handle)
        return;

    LOCK_SERVER(imap_server);
    /* remove from used list */
    if (imap_server->used_handles) {
        GList *conn;
        conn = g_list_find_custom(imap_server->used_handles, handle, by_handle);
        info = (struct handle_info*)conn->data;
        imap_server->used_handles =
            g_list_delete_link(imap_server->used_handles, conn);
        imap_server->used_connections--;
    }
    /* check max_connections */
    if (imap_server->used_connections >= imap_server->max_connections)
        lb_imap_server_info_free(info);
    else
    /* add to free list */
        imap_server->free_handles = g_list_append(imap_server->free_handles,
                                                  info);
    UNLOCK_SERVER(imap_server);
}

/**
 * libbalsa_imap_server_set_max_connections:
 * @server: A #LibBalsaImapServer
 * @max: The maximum
 *
 * Sets the maximal open connections allowed, already open connections will
 * be disconnected on release.
 **/
void
libbalsa_imap_server_set_max_connections(LibBalsaImapServer *server,
                                         int max)
{
    server->max_connections = max;
    printf("set_max_connections: set to %d\n", max);
}

int
libbalsa_imap_server_get_max_connections(LibBalsaImapServer *server)
{
    return server->max_connections;
}

void
libbalsa_imap_server_enable_persistent_cache(LibBalsaImapServer *server,
                                             gboolean enable)
{
    server->persistent_cache = !!enable;
}
gboolean
libbalsa_imap_server_has_persistent_cache(LibBalsaImapServer *srv)
{
    return srv->persistent_cache;
}

/**
 * libbalsa_imap_server_force_disconnect:
 * @server: A #LibBalsaImapServer
 *
 * Forces a logout on all connections, used when cleaning up.
 **/
void libbalsa_imap_server_force_disconnect(LibBalsaImapServer *imap_server)
{
    LOCK_SERVER(imap_server);
    g_list_foreach(imap_server->used_handles,
                   (GFunc) lb_imap_server_info_free, NULL);
    g_list_free(imap_server->used_handles);
    imap_server->used_handles = NULL;
    g_list_foreach(imap_server->free_handles,
                   (GFunc) lb_imap_server_info_free, NULL);
    g_list_free(imap_server->free_handles);
    imap_server->free_handles = NULL;
    UNLOCK_SERVER(imap_server);
}

/**
 * libbalsa_imap_server_close_all_connections:
 *
 * Forces a logout on all connections on all servers, used when cleaning up.
 **/
static void close_all_connections_cb(gpointer key, gpointer value,
                                     gpointer user_data)
{
#if 0
    libbalsa_imap_server_force_disconnect(LIBBALSA_IMAP_SERVER(value));
#else
    libbalsa_imap_server_set_offline_mode(LIBBALSA_IMAP_SERVER(value), TRUE);
#endif
}
void
libbalsa_imap_server_close_all_connections(void)
{
    LOCK_SERVERS();
    if (imap_servers)
        g_hash_table_foreach(imap_servers, close_all_connections_cb, NULL);
    UNLOCK_SERVERS();
    libbalsa_imap_purge_temp_dir(0);
}

/**
 * libbalsa_imap_server_has_free_handles:
 * @server: A #LibBalsaImapServer
 *
 * Returns %TRUE when there are free connections. This does NOT guarantee that
 * libbalsa_imap_server_get_handle() returns a handle.
 *
 * Return value: is %TRUE when there are free connections.
 **/
gboolean libbalsa_imap_server_has_free_handles(LibBalsaImapServer *imap_server)
{
    gboolean result;
    LOCK_SERVER(imap_server);
    result = imap_server->used_connections < imap_server->max_connections
        || imap_server->free_handles;
    UNLOCK_SERVER(imap_server);
    return result;
}

/**
 * libbalsa_imap_server_is_offline:
 * @server: A #LibBalsaImapServer
 *
 * Returns %TRUE when this server is in offline mode.
 *
 * Return value: is %TRUE when this server is in offline mode.
 **/
gboolean libbalsa_imap_server_is_offline(LibBalsaImapServer *server)
{
    return server->offline_mode;
}

/**
 * libbalsa_imap_server_set_offline_mode:
 * @server: A #LibBalsaImapServer
 * @offline: Set to %TRUE to switch to offline mode.
 *
 * When @offline is %TRUE switches @server to offline mode, and disconnects all
 * connections. When @offline is %FALSE, allow new connections.
 **/
void libbalsa_imap_server_set_offline_mode(LibBalsaImapServer *server,
                                           gboolean offline)
{
    server->offline_mode = offline;
    if (offline)
        libbalsa_imap_server_force_disconnect(server);
}

void
libbalsa_imap_server_set_bug(LibBalsaImapServer *server,
                             LibBalsaImapServerBug bug, gboolean hasp)
{
    server->has_fetch_bug = !! hasp;
}

gboolean
libbalsa_imap_server_has_bug(LibBalsaImapServer *server,
                             LibBalsaImapServerBug bug)
{
    return server->has_fetch_bug;
}

void
libbalsa_imap_server_set_use_status(LibBalsaImapServer *server, 
                                    gboolean use_status)
{
    server->use_status = !!use_status;
}
gboolean
libbalsa_imap_server_get_use_status(LibBalsaImapServer *server)
{
    return server->use_status;
}

void
libbalsa_imap_server_set_use_idle(LibBalsaImapServer *server, 
                                  gboolean use_idle)
{
    GList *list;

    server->use_idle = !!use_idle;
    printf("Server will%s use IDLE\n",
           server->use_idle ? "" : " NOT");
    for (list = server->used_handles; list; list = list->next) {
        struct handle_info *info = list->data;
        imap_handle_set_option(info->handle, IMAP_OPT_IDLE,
                               server->use_idle);
    }
}

gboolean
libbalsa_imap_server_get_use_idle(LibBalsaImapServer *server)
{
    return server->use_idle;
}
