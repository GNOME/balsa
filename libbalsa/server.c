/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#include "config.h"

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "libbalsa-marshal.h"

#include <libgnome/libgnome.h> 
#include <string.h>
#include <stdlib.h>

#ifdef USE_TLS
#include <openssl/err.h>
#endif

static GObjectClass *parent_class = NULL;
static void libbalsa_server_class_init(LibBalsaServerClass * klass);
static void libbalsa_server_init(LibBalsaServer * server);
static void libbalsa_server_finalize(GObject * object);

static void libbalsa_server_real_set_username(LibBalsaServer * server,
					      const gchar * username);
static void libbalsa_server_real_set_password(LibBalsaServer * server,
					      const gchar * passwd);
static void libbalsa_server_real_set_host(LibBalsaServer * server,
					  const gchar * host,
                                          gboolean use_ssl);
/* static gchar* libbalsa_server_real_get_password(LibBalsaServer *server); */

enum {
    SET_USERNAME,
    SET_PASSWORD,
    SET_HOST,
    GET_PASSWORD,
    LAST_SIGNAL
};

static guint libbalsa_server_signals[LAST_SIGNAL];

GType
libbalsa_server_get_type(void)
{
    static GType server_type = 0;

    if (!server_type) {
        static const GTypeInfo server_info = {
            sizeof(LibBalsaServerClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) libbalsa_server_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibBalsaServer),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) libbalsa_server_init
        };

        server_type =
            g_type_register_static(G_TYPE_OBJECT, "LibBalsaServer",
                                   &server_info, 0);
    }

    return server_type;
}

static void
libbalsa_server_class_init(LibBalsaServerClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_server_finalize;

    libbalsa_server_signals[SET_USERNAME] =
	g_signal_new("set-username",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
				     set_username),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__STRING,
                     G_TYPE_NONE, 1,
		     G_TYPE_STRING);
    libbalsa_server_signals[SET_PASSWORD] =
	g_signal_new("set-password",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
				     set_password),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__STRING,
                     G_TYPE_NONE, 1,
		     G_TYPE_STRING);
    libbalsa_server_signals[SET_HOST] =
	g_signal_new("set-host",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
                                     set_host),
                     NULL, NULL,
#ifdef USE_SSL
                     libbalsa_VOID__POINTER_INT,
                     G_TYPE_NONE, 2,
                     G_TYPE_POINTER, G_TYPE_INT
#else /* USE_SSL */
		     g_cclosure_marshal_VOID__POINTER,
		     G_TYPE_NONE, 1,
		     G_TYPE_POINTER
#endif /* USE_SSL */
		     );


    libbalsa_server_signals[GET_PASSWORD] =
	g_signal_new("get-password",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
			             get_password),
                     NULL, NULL,
		     libbalsa_POINTER__OBJECT,
                     G_TYPE_POINTER, 1,
                     LIBBALSA_TYPE_MAILBOX);

    klass->set_username = libbalsa_server_real_set_username;
    klass->set_password = libbalsa_server_real_set_password;
    klass->set_host = libbalsa_server_real_set_host;
    klass->get_password = NULL;	/* libbalsa_server_real_get_password; */
}

static void
libbalsa_server_init(LibBalsaServer * server)
{
    server->host = NULL;
    server->user = NULL;
    server->passwd = NULL;
    server->remember_passwd = TRUE;
    server->use_ssl         = FALSE;
    server->tls_mode        = LIBBALSA_TLS_ENABLED;
}

/* leave object in sane state (NULLified fields) */
static void
libbalsa_server_finalize(GObject * object)
{
    LibBalsaServer *server;

    g_return_if_fail(LIBBALSA_IS_SERVER(object));

    server = LIBBALSA_SERVER(object);

    g_free(server->host);   server->host = NULL;
    g_free(server->user);   server->user = NULL;
    g_free(server->passwd); server->passwd = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

GObject *
libbalsa_server_new(LibBalsaServerType type)
{
    LibBalsaServer *server;
    if (type == LIBBALSA_SERVER_IMAP)
	g_warning("Creating a LibBalsaServer with type LIBBALSA_SERVER_IMAP,"
		  "please use libbalsa_imap_server_new");
    server = g_object_new(LIBBALSA_TYPE_SERVER, NULL);
    server->type = type;

    return G_OBJECT(server);
}

void
libbalsa_server_set_username(LibBalsaServer * server,
			     const gchar * username)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_signal_emit(G_OBJECT(server),
		  libbalsa_server_signals[SET_USERNAME], 0, username);
}

void
libbalsa_server_set_password(LibBalsaServer * server, const gchar * passwd)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_signal_emit(G_OBJECT(server),
		  libbalsa_server_signals[SET_PASSWORD], 0, passwd);
}

void
libbalsa_server_set_host(LibBalsaServer * server, const gchar * host,
                         gboolean use_ssl)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_signal_emit(G_OBJECT(server), libbalsa_server_signals[SET_HOST],
		  0, host, use_ssl);

}

gchar *
libbalsa_server_get_password(LibBalsaServer * server,
			     LibBalsaMailbox * mbox)
{
    gchar *retval = NULL;

    g_return_val_if_fail(server != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    g_signal_emit(G_OBJECT(server), libbalsa_server_signals[GET_PASSWORD],
                  0, mbox, &retval);
    return retval;
}

static void
libbalsa_server_real_set_username(LibBalsaServer * server,
				  const gchar * username)
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(server->user);
    server->user = g_strdup(username);
}

static void
libbalsa_server_real_set_password(LibBalsaServer * server,
				  const gchar * passwd)
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(server->passwd);
    if(passwd && passwd[0])
	server->passwd = g_strdup(passwd);
    else server->passwd = NULL;
}

static void
libbalsa_server_real_set_host(LibBalsaServer * server, const gchar * host,
                              gboolean use_ssl)
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(server->host);
    server->host = g_strdup(host);
    server->use_ssl = use_ssl;
}


#if 0
static gchar *
libbalsa_server_real_get_password(LibBalsaServer * server)
{
    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return g_strdup(server->passwd);
}
#endif


/* libbalsa_server_load_config:
   load the server configuration using gnome-config.
   Try to use sensible defaults. 
   FIXME: Port field is kept here only for compatibility, drop after 1.4.x
   release.
*/
void
libbalsa_server_load_config(LibBalsaServer * server)
{
    gboolean d;
    server->host = gnome_config_get_string("Server");
    if(server->host && strrchr(server->host, ':') == NULL) {
        gint port;
        port = gnome_config_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", server->host, port);
            g_free(server->host);
            server->host = newhost;
        }
    }       
    server->use_ssl = gnome_config_get_bool("SSL=false");
    d=0;
    server->tls_mode = gnome_config_get_int_with_default("TLSMode", &d);
    if(d) server->tls_mode = LIBBALSA_TLS_ENABLED;
    server->user = gnome_config_private_get_string("Username");
    server->remember_passwd = gnome_config_get_bool("RememberPasswd=false");
    if(server->remember_passwd)
        server->passwd = gnome_config_private_get_string("Password");
    if(server->passwd && server->passwd[0] == '\0') {
	g_free(server->passwd);
	server->passwd = NULL;
    }
	
    if (!server->user)
	server->user = g_strdup(getenv("USER"));

    if (server->passwd != NULL) {
	gchar *buff = libbalsa_rot(server->passwd);
	g_free(server->passwd);
	server->passwd = buff;
    }
}

/* libbalsa_server_save_config:
   save config.
   It is bit tricky to decide the value of the remember_passwd field.
   Should empty values be remembered? Even if they are, balsa will 
   still ask for the password if it is empty.
*/
void
libbalsa_server_save_config(LibBalsaServer * server)
{
    gnome_config_set_string("Server", server->host);
    gnome_config_private_set_string("Username", server->user);
    gnome_config_set_bool("RememberPasswd", 
                          server->remember_passwd && server->passwd != NULL);

    if (server->remember_passwd && server->passwd != NULL) {
	gchar *buff = libbalsa_rot(server->passwd);
	gnome_config_private_set_string("Password", buff);
	g_free(buff);
    }
    gnome_config_set_bool("SSL", server->use_ssl);
    gnome_config_set_int("TLSMode", server->tls_mode);
}

void
libbalsa_server_user_cb(ImapUserEventType ue, void *arg, ...)
{
    va_list alist;
    int *ok;
    LibBalsaServer *is = LIBBALSA_SERVER(arg);

    va_start(alist, arg);
    switch(ue) {
    case IME_GET_USER_PASS: {
        gchar *method = va_arg(alist, gchar*);
        gchar **user = va_arg(alist, gchar**);
        gchar **pass = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
        if(!is->passwd) {
            is->passwd = libbalsa_server_get_password(is, NULL);
        }
        *ok = is->passwd != NULL;
        if(*ok) {
            g_free(*user); *user = g_strdup(is->user);
            g_free(*pass); *pass = g_strdup(is->passwd);
            libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                                 /* host, authentication method */
                                 _("Logging in to %s using %s"), 
                                   is->host, method);
        }
        break;
    }
    case IME_GET_USER:  { /* for eg kerberos */
        gchar **user;
        va_arg(alist, gchar*); /* Ignore the method */
        user = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
        *ok = 1; /* consider popping up a dialog window here */
        g_free(*user); *user = g_strdup(is->user);
        break;
    }
    case IME_TLS_VERIFY_ERROR:  {
#ifdef USE_TLS
        long vfy_result;
        SSL *ssl;
        X509 *cert;
        const char *reason;
        ok = va_arg(alist, int*);
        vfy_result = va_arg(alist, long);
        reason =  X509_verify_cert_error_string(vfy_result);
        printf("IMAP:TLS: failed cert verification: %ld : %s.\n",
               vfy_result, reason);
        ssl = va_arg(alist, SSL*);
        cert = SSL_get_peer_certificate(ssl);
	if(cert) {
	    *ok = libbalsa_is_cert_known(cert, vfy_result);
	    X509_free(cert);
	}
#else
        g_warning("TLS error with TLS disabled!?");
#endif
        break;
    }
    case IME_TLS_NO_PEER_CERT: {
        ok = va_arg(alist, int*); *ok = 0;
        printf("IMAP:TLS: Server presented no cert!\n");
        break;
    }
    case IME_TLS_WEAK_CIPHER: {
        ok = va_arg(alist, int*); *ok = 1;
        printf("IMAP:TLS: Weak cipher accepted.\n");
        break;
    }
    default: g_warning("unhandled imap event type! Fix the code."); break;
    }
    va_end(alist);
}

/* Connect the server's "get-password" signal to the callback; if the
 * ask-password callback is connected more than once, the dialog is
 * popped up the corresponding number of times, so we'll ignore the
 * request if a callback is already connected. */
void
libbalsa_server_connect_signals(LibBalsaServer * server, GCallback cb,
                                gpointer cb_data)
{
    if (!g_signal_has_handler_pending(server,
                                      libbalsa_server_signals
                                      [GET_PASSWORD], 0, TRUE))
        g_signal_connect(server, "get-password", cb, cb_data);
}
