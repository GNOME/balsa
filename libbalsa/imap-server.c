/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
  LibBalsaImapServer is a class for managing connections to one IMAP
  server. Idle connections are disconnected after a timeout, or when
  the user switches to offline mode.
*/
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "libbalsa.h"
#include "imap-handle.h"
#include "imap-server.h"
#include "imap-commands.h"

#include <libgnome/gnome-config.h>
#include <libgnome/gnome-i18n.h> 

#ifdef USE_TLS
#define REQ_SSL(s) (LIBBALSA_SERVER(s)->use_ssl)
#else
#define REQ_SSL(s) (0)
#endif

static LibBalsaServerClass *parent_class = NULL;

struct LibBalsaImapServer_ {
        LibBalsaServer server;

        guint connection_cleanup_id;
        gchar *key;
        guint max_connections;
        gboolean offline_mode;

        GMutex *lock; /* protects the following members */
        guint used_connections;
        GList *used_handles;
        GList *free_handles;
};

typedef struct LibBalsaImapServerClass_ {
    LibBalsaServerClass parent_class;

} LibBalsaImapServerClass;

static void libbalsa_imap_server_class_init(LibBalsaImapServerClass * klass);
static void libbalsa_imap_server_init(LibBalsaImapServer * server);
static void libbalsa_imap_server_finalize(GObject * object);
static gboolean connection_cleanup(gpointer ptr);

/* Poll every 5 minutes */
#define CONNECTION_CLEANUP_POLL_PERIOD  (5*60)
/* Cleanup connections more then 10 minutes idle */
#define CONNECTION_CLEANUP_IDLE_TIME    (10*60)
/* Send NOOP after 20 minutes to keep a connection alive */
#define CONNECTION_CLEANUP_NOOP_TIME    (20*60)
/* We try to avoid too many connections per server */
#define MAX_CONNECTIONS_PER_SERVER 20

G_LOCK_DEFINE_STATIC(imap_servers);
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
    LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
    G_LOCK(imap_servers);
    g_hash_table_steal(imap_servers, imap_server->key);
    g_free(imap_server->key);
    imap_server->key = g_strdup_printf("%s@%s", name, server->host);
    g_hash_table_insert(imap_servers, imap_server->key, imap_server);
    G_UNLOCK(imap_servers);
    (parent_class)->set_username(server, name);
}
static void libbalsa_imap_server_set_host(LibBalsaServer * server,
                      const gchar * host, gboolean use_ssl)
{
    LibBalsaImapServer *imap_server = LIBBALSA_IMAP_SERVER(server);
    G_LOCK(imap_servers);
    g_hash_table_steal(imap_servers, imap_server->key);
    g_free(imap_server->key);
    imap_server->key = g_strdup_printf("%s@%s", server->user, host);
    g_hash_table_insert(imap_servers, imap_server->key, imap_server);
    G_UNLOCK(imap_servers);
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
    klass->set_password = libbalsa_imap_server_real_set_password;
    klass->get_password = NULL; /* libbalsa_imap_server_real_get_password; */
#endif
}

static void
libbalsa_imap_server_init(LibBalsaImapServer * imap_server)
{
    LibBalsaServer *server = LIBBALSA_SERVER(imap_server);
    server->type = LIBBALSA_SERVER_IMAP;
    imap_server->key = NULL;
    imap_server->lock = g_mutex_new();
    imap_server->max_connections = MAX_CONNECTIONS_PER_SERVER;
    imap_server->used_connections = 0;
    imap_server->used_handles = NULL;
    imap_server->free_handles = NULL;
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

    G_LOCK(imap_servers);
    g_hash_table_remove(imap_servers, imap_server->key);
    G_UNLOCK(imap_servers);
    
    g_source_remove(imap_server->connection_cleanup_id);

#if 0
    g_mutex_lock(imap_server->lock);
#endif
    libbalsa_imap_server_force_disconnect(imap_server);
    g_mutex_free(imap_server->lock);
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
      const gchar *login = g_strstr_len(buffer, length, "LOGIN ");
      if (login) {
        const gchar *user = login + 6;
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
    if(!g_mutex_trylock(imap_server->lock))
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

        if (info->last_used < idle_marker) {
            if (imap_mbox_handle_noop(info->handle) != IMR_OK)
                libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                     _("Could not send \"%s\" to \"%s\""),
                                     "NOOP",
                                     LIBBALSA_SERVER(imap_server)->host);
        }
    }

    g_mutex_unlock(imap_server->lock);
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
        G_LOCK(imap_servers);
        if (!imap_servers)
            imap_servers = g_hash_table_new(g_str_hash, g_str_equal);
        G_UNLOCK(imap_servers);
    }

    /* lookup username@host */
    key = g_strdup_printf("%s@%s", username, host);
    G_LOCK(imap_servers);
    imap_server = g_hash_table_lookup(imap_servers, key);
    if (!imap_server) {
        imap_server = g_object_new(LIBBALSA_TYPE_IMAP_SERVER, NULL);
        imap_server->key = key;
        g_hash_table_insert(imap_servers, key, imap_server);
    } else {
        g_free(key);
        g_object_ref(imap_server);
    }
    G_UNLOCK(imap_servers);
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

LibBalsaImapServer* libbalsa_imap_server_new_from_config(void)
{
    LibBalsaServer tmp_server;
    LibBalsaImapServer *imap_server;
    LibBalsaServer *server;
    gboolean d;

    tmp_server.host = gnome_config_get_string("Server");
    if(strrchr(tmp_server.host, ':') == NULL) {
        gint port;
        port = gnome_config_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", tmp_server.host, port);
            g_free(tmp_server.host);
            tmp_server.host = newhost;
        }
    }       
    tmp_server.user = gnome_config_private_get_string("Username");
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
    server->use_ssl |= gnome_config_get_bool("SSL=false");
    server->tls_mode = gnome_config_get_int_with_default("TLSMode", &d);
    if(d) server->tls_mode = LIBBALSA_TLS_ENABLED;
    if (!server->passwd) {
        server->remember_passwd = gnome_config_get_bool("RememberPasswd=false");
        if(server->remember_passwd)
            server->passwd = gnome_config_private_get_string("Password");
        if(server->passwd && server->passwd[0] == '\0') {
            g_free(server->passwd);
            server->passwd = NULL;
        }

        if (server->passwd != NULL) {
            gchar *buff = libbalsa_rot(server->passwd);
            g_free(server->passwd);
            server->passwd = buff;
        }
    }
    return imap_server;
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

    g_mutex_lock(imap_server->lock);
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
        g_mutex_unlock(imap_server->lock);
        info = lb_imap_server_info_new(server);
        g_mutex_lock(imap_server->lock);
        /* FIXME: after dropping and reacquiring the lock,
         * (imap_server->used_connections < imap_server->max_connections)
         * might no longer be true--do we care?
        if (imap_server->used_connections >= imap_server->max_connections) {
            lb_imap_server_info_free(info);
            g_mutex_unlock(imap_server->lock);
            return NULL;
        }
         */
    }
    if (info) {
        if(imap_mbox_is_disconnected(info->handle)) {
            rc=imap_mbox_handle_connect(info->handle, server->host,
                                        REQ_SSL(server));
            if(rc != IMAP_SUCCESS) {
                gchar *msg = imap_mbox_handle_get_last_msg(info->handle);
                if(rc == IMAP_AUTH_FAILURE)
                    libbalsa_server_set_password(server, NULL);
                g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                            rc == IMAP_AUTH_FAILURE
                            ? LIBBALSA_MAILBOX_AUTH_ERROR :
                            LIBBALSA_MAILBOX_NETWORK_ERROR,
                            _("Cannot connect to the server: %s"), msg);
                g_free(msg);
                lb_imap_server_info_free(info);
                g_mutex_unlock(imap_server->lock);
                return NULL;
            }
        }
        /* add handle to used list */
        imap_server->used_handles = g_list_prepend(imap_server->used_handles,
                                                   info);
        imap_server->used_connections++;
    }
    g_mutex_unlock(imap_server->lock);

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

    g_mutex_lock(imap_server->lock);
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
        g_mutex_unlock(imap_server->lock);
        info = lb_imap_server_info_new(server);
        if (!info)
            return NULL;
        g_mutex_lock(imap_server->lock);
        /* FIXME: after dropping and reacquiring the lock,
         * (imap_server->used_connections < imap_server->max_connections)
         * might no longer be true--do we care?
        if (imap_server->used_connections >= imap_server->max_connections) {
            lb_imap_server_info_free(info);
            g_mutex_unlock(imap_server->lock);
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
        g_mutex_unlock(imap_server->lock);
        return NULL;
    }

    if (imap_mbox_is_disconnected(info->handle)) {
        rc=imap_mbox_handle_connect(info->handle, server->host,
                                    REQ_SSL(server));
        if(rc != IMAP_SUCCESS) {
            gchar *msg = imap_mbox_handle_get_last_msg(info->handle);
            if(rc == IMAP_AUTH_FAILURE)
                libbalsa_server_set_password(server, NULL);
            g_set_error(err, LIBBALSA_MAILBOX_ERROR,
                        LIBBALSA_MAILBOX_OPEN_ERROR,
                    _("Cannot connect to the server: %s"), msg);
            g_free(msg);
            lb_imap_server_info_free(info);
            g_mutex_unlock(imap_server->lock);
            return NULL;
        }
    }
    /* add handle to used list */
    info->last_user = user;
    imap_server->used_handles = g_list_prepend(imap_server->used_handles,
                                               info);
    imap_server->used_connections++;
    g_mutex_unlock(imap_server->lock);

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

    g_mutex_lock(imap_server->lock);
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
    g_mutex_unlock(imap_server->lock);
}

/**
 * libbalsa_imap_server_set_max_connections:
 * @server: A #LibBalsaImapServer
 * @max: The maximum
 *
 * Sets the maximal open connections allowed, already open connections will
 * be disconnected on release.
 **/
void libbalsa_imap_server_set_max_connections(LibBalsaImapServer *server,
                                              int max)
{
    server->max_connections = max;
}

/**
 * libbalsa_imap_server_force_disconnect:
 * @server: A #LibBalsaImapServer
 *
 * Forces a logout on all connections, used when cleaning up.
 **/
void libbalsa_imap_server_force_disconnect(LibBalsaImapServer *imap_server)
{
    g_mutex_lock(imap_server->lock);
    g_list_foreach(imap_server->used_handles,
                   (GFunc) lb_imap_server_info_free, NULL);
    g_list_free(imap_server->used_handles);
    imap_server->used_handles = NULL;
    g_list_foreach(imap_server->free_handles,
                   (GFunc) lb_imap_server_info_free, NULL);
    g_list_free(imap_server->free_handles);
    imap_server->free_handles = NULL;
    g_mutex_unlock(imap_server->lock);
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
void libbalsa_imap_server_close_all_connections(void)
{
    G_LOCK(imap_servers);
    if(imap_servers) 
        g_hash_table_foreach(imap_servers, close_all_connections_cb, NULL);
    G_UNLOCK(imap_servers);
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
    g_mutex_lock(imap_server->lock);
    result = imap_server->used_connections < imap_server->max_connections
        || imap_server->free_handles;
    g_mutex_unlock(imap_server->lock);
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
