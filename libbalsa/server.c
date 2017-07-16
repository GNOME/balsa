/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "server.h"

#include <string.h>
#include <stdlib.h>

#if defined(HAVE_LIBSECRET)
#include <libsecret/secret.h>
#elif defined(HAVE_GNOME_KEYRING)
#include <gnome-keyring.h>
#endif                          /* defined(HAVE_LIBSECRET) */

#include <openssl/err.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "libbalsa-marshal.h"
#include "libbalsa-conf.h"
#include <glib/gi18n.h>

#if defined(HAVE_LIBSECRET)
static const SecretSchema server_schema = {
    "org.gnome.Balsa.NetworkPassword", SECRET_SCHEMA_NONE,
    {
	{ "protocol", SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ "server",   SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ "user",     SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ NULL, 0 }
    }
};
const SecretSchema *LIBBALSA_SERVER_SECRET_SCHEMA = &server_schema;
#elif defined(HAVE_GNOME_KEYRING) && !defined(HAVE_GNOME_KEYRING_24)
static const GnomeKeyringPasswordSchema server_schema = {
    GNOME_KEYRING_ITEM_GENERIC_SECRET,
    {
	{ "protocol", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
	{ "server",   GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
	{ "user",     GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
	{ NULL, 0 }
    }
};
const GnomeKeyringPasswordSchema* LIBBALSA_SERVER_KEYRING_SCHEMA =
    &server_schema;
#endif                          /* defined(HAVE_LIBSECRET) */

static GObjectClass *parent_class = NULL;
static void libbalsa_server_class_init(LibBalsaServerClass * klass);
static void libbalsa_server_init(LibBalsaServer * server);
static void libbalsa_server_finalize(GObject * object);

static void libbalsa_server_real_set_username(LibBalsaServer * server,
					      const gchar * username);
static void libbalsa_server_real_set_host(LibBalsaServer * server,
					  const gchar * host,
                                          gboolean use_ssl);
/* static gchar* libbalsa_server_real_get_password(LibBalsaServer *server); */

enum {
    SET_USERNAME,
    SET_HOST,
    CONFIG_CHANGED,
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
    libbalsa_server_signals[SET_HOST] =
	g_signal_new("set-host",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
                                     set_host),
                     NULL, NULL,
                     libbalsa_VOID__POINTER_INT,
                     G_TYPE_NONE, 2,
                     G_TYPE_POINTER, G_TYPE_INT
		     );
    libbalsa_server_signals[CONFIG_CHANGED] =
	g_signal_new("config-changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
				     config_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);


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
    klass->set_host = libbalsa_server_real_set_host;
    klass->get_password = NULL;	/* libbalsa_server_real_get_password; */
}

static void
libbalsa_server_init(LibBalsaServer * server)
{
    server->protocol = "pop3"; /* Is this a sane default value? */
    server->host = NULL;
    server->user = NULL;
    server->passwd = NULL;
    server->remember_passwd = TRUE;
    server->use_ssl         = FALSE;
    server->tls_mode        = LIBBALSA_TLS_ENABLED;
    server->security		= NET_CLIENT_CRYPT_STARTTLS;
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
    if (server->passwd != NULL) {
    	memset(server->passwd, 0, strlen(server->passwd));
    }
    libbalsa_free_password(server->passwd); server->passwd = NULL;

    g_free(server->cert_file);
    server->cert_file = NULL;
    if (server->cert_passphrase != NULL) {
    	memset(server->cert_passphrase, 0, strlen(server->cert_passphrase));
    }
    g_free(server->cert_passphrase);
    server->cert_passphrase = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaServer *
libbalsa_server_new(void)
{
    LibBalsaServer *server;

    server = g_object_new(LIBBALSA_TYPE_SERVER, NULL);

    return server;
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
libbalsa_server_set_password(LibBalsaServer * server,
                             const gchar * passwd)
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    libbalsa_free_password(server->passwd);
    if(passwd && passwd[0])
	server->passwd = g_strdup(passwd);
    else server->passwd = NULL;
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

void
libbalsa_server_config_changed(LibBalsaServer * server)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_signal_emit(G_OBJECT(server), libbalsa_server_signals[CONFIG_CHANGED],
                  0);
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
    server->host = libbalsa_conf_get_string("Server");
    if(server->host && strrchr(server->host, ':') == NULL) {
        gint port;
        port = libbalsa_conf_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", server->host, port);
            g_free(server->host);
            server->host = newhost;
        }
    }       
    server->use_ssl = libbalsa_conf_get_bool("SSL=false");
    d = FALSE;
    server->tls_mode = libbalsa_conf_get_int_with_default("TLSMode", &d);
    if(d) server->tls_mode = LIBBALSA_TLS_ENABLED;
    d = FALSE;
    server->security = libbalsa_conf_get_int_with_default("Security", &d);
    if (d) {
    	server->security = NET_CLIENT_CRYPT_STARTTLS;
    }
    server->user = libbalsa_conf_private_get_string("Username");
    if (!server->user)
	server->user = g_strdup(getenv("USER"));

    server->try_anonymous = libbalsa_conf_get_bool("Anonymous=false");
    server->remember_passwd = libbalsa_conf_get_bool("RememberPasswd=false");

    if(server->remember_passwd) {
#if defined(HAVE_LIBSECRET)
        GError *err = NULL;

        server->passwd =
            secret_password_lookup_sync(LIBBALSA_SERVER_SECRET_SCHEMA,
                                        NULL, &err,
                                        "protocol", server->protocol,
                                        "server",   server->host,
                                        "user",     server->user,
                                        NULL);
        if (err) {
            libbalsa_free_password(server->passwd);
            server->passwd = NULL;
            printf(_("Error looking up password for %s@%s: %s\n"),
                   server->user, server->host, err->message);
            printf(_("Falling back\n"));
            g_clear_error(&err);
            server->passwd = libbalsa_conf_private_get_string("Password");
            if (server->passwd != NULL) {
                gchar *buff = libbalsa_rot(server->passwd);
                libbalsa_free_password(server->passwd);
                server->passwd = buff;
                secret_password_store_sync
                    (LIBBALSA_SERVER_SECRET_SCHEMA, NULL,
                     _("Balsa passwords"), server->passwd, NULL, &err,
                     "protocol", server->protocol,
                     "server",   server->host,
                     "user",     server->user,
                     NULL);
                /* We could in principle clear the password in the
                 * config file here but we do not for the backward
                 * compatibility. */
                if (err) {
                    printf(_("Error storing password for %s@%s: %s\n"),
                           server->user, server->host, err->message);
                    g_error_free(err);
                }
            }
        }
#elif defined (HAVE_GNOME_KEYRING)
	GnomeKeyringResult res =
	    gnome_keyring_find_password_sync(LIBBALSA_SERVER_KEYRING_SCHEMA,
					     &server->passwd,
					     "protocol", server->protocol,
					     "server", server->host,
					     "user", server->user,
					     NULL);
	if(res != GNOME_KEYRING_RESULT_OK) {
	    gnome_keyring_free_password(server->passwd);
	    server->passwd = NULL;
	    printf("Error retrieving password from key ring. Falling back\n");
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

    server->client_cert = libbalsa_conf_get_bool("NeedClientCert=false");
    server->cert_file = libbalsa_conf_get_string("UserCertificateFile");
    server->cert_passphrase = libbalsa_conf_private_get_string("CertificatePassphrase");
    if ((server->cert_passphrase != NULL) && (server->cert_passphrase[0] != '\0')) {
        gchar *tmp = libbalsa_rot(server->cert_passphrase);

        g_free(server->cert_passphrase);
        server->cert_passphrase = tmp;
    } else {
    	g_free(server->cert_passphrase);
    	server->cert_passphrase = NULL;
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
    libbalsa_conf_set_string("Server", server->host);
    libbalsa_conf_private_set_string("Username", server->user);
    libbalsa_conf_set_bool("Anonymous",          server->try_anonymous);
    libbalsa_conf_set_bool("RememberPasswd", 
                          server->remember_passwd && server->passwd != NULL);

    if (server->remember_passwd && server->passwd != NULL) {
#if defined(HAVE_LIBSECRET)
        GError *err = NULL;

        secret_password_store_sync(LIBBALSA_SERVER_SECRET_SCHEMA, NULL,
                                   _("Balsa passwords"), server->passwd,
                                   NULL, &err,
                                   "protocol", server->protocol,
                                   "server",   server->host,
                                   "user",     server->user,
                                   NULL);
        if (err) {
            printf(_("Error storing password for %s@%s: %s\n"),
                   server->user, server->host, err->message);
            g_error_free(err);
        }
#elif defined (HAVE_GNOME_KEYRING)
	gnome_keyring_store_password_sync(LIBBALSA_SERVER_KEYRING_SCHEMA,
				     NULL,
				     _("Balsa passwords"),
				     server->passwd,
				     "protocol", server->protocol,
				     "server", server->host,
				     "user", server->user,
				     NULL);
#else
	gchar *buff = libbalsa_rot(server->passwd);
	libbalsa_conf_private_set_string("Password", buff);
	g_free(buff);
#endif                          /* defined(HAVE_LIBSECRET) */
    }

    libbalsa_conf_set_bool("NeedClientCert", server->client_cert);
    if (server->cert_file != NULL) {
    	libbalsa_conf_set_string("UserCertificateFile", server->cert_file);
    }
    if (server->cert_passphrase != NULL) {
        gchar *tmp = libbalsa_rot(server->cert_passphrase);

        libbalsa_conf_private_set_string("CertificatePassphrase", tmp);
        g_free(tmp);
    }

    libbalsa_conf_set_bool("SSL", server->use_ssl);
    libbalsa_conf_set_int("TLSMode", server->tls_mode);
    libbalsa_conf_set_int("Security", server->security);
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
            libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
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
        long vfy_result;
        SSL *ssl;
        X509 *cert;
        ok = va_arg(alist, int*);
        vfy_result = va_arg(alist, long);
        X509_verify_cert_error_string(vfy_result);
#if 0
        printf("IMAP:TLS: failed cert verification: %ld : %s.\n",
               vfy_result, reason);
#endif
        ssl = va_arg(alist, SSL*);
        cert = SSL_get_peer_certificate(ssl);
	if(cert) {
	    *ok = libbalsa_is_cert_known(cert, vfy_result);
	    X509_free(cert);
	}
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
    case IME_TIMEOUT: {
        ok = va_arg(alist, int*); *ok = 1;
        /* *ok = libbalsa_abort_on_timeout(is->host); */
        /* For now, always timeout. The UI needs some work. */
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


gchar **
libbalsa_server_get_auth(NetClient *client,
						 gboolean   need_passwd,
         	 	 	 	 gpointer   user_data)
{
    LibBalsaServer *server = LIBBALSA_SERVER(user_data);
    gchar **result = NULL;

    g_debug("%s: %p %p: encrypted = %d", __func__, client, user_data,
            net_client_is_encrypted(client));
    if (server->try_anonymous == 0U) {
        result = g_new0(gchar *, 3U);
        result[0] = g_strdup(server->user);
        if (need_passwd) {
        	if ((server->passwd != NULL) && (server->passwd[0] != '\0')) {
        		result[1] = g_strdup(server->passwd);
        	} else {
        		result[1] = libbalsa_server_get_password(server, NULL);
        	}
        }
    }
    return result;
}


gboolean
libbalsa_server_check_cert(NetClient           *client,
           	   	   	   	   GTlsCertificate     *peer_cert,
						   GTlsCertificateFlags errors,
						   gpointer             user_data)
{
    GByteArray *cert_der = NULL;
    gboolean result = FALSE;

    /* FIXME - this a hack, simulating the (OpenSSL based) input for libbalsa_is_cert_known().
        If we switch completely to
     * (GnuTLS based) GTlsCertificate/GTlsClientConnection, we can omit this... */
    g_debug("%s: %p %p %u %p", __func__, client, peer_cert, errors, user_data);

    /* create a OpenSSL X509 object from the certificate's DER data */
    g_object_get(G_OBJECT(peer_cert), "certificate", &cert_der, NULL);
    if (cert_der != NULL) {
        X509 *ossl_cert;
        const unsigned char *der_p;

        der_p = (const unsigned char *) cert_der->data;
        ossl_cert = d2i_X509(NULL, &der_p, cert_der->len);
        g_byte_array_unref(cert_der);

        if (ossl_cert != NULL) {
            long vfy_result;

            /* convert the GIO error flags into OpenSSL error flags */
            if ((errors & G_TLS_CERTIFICATE_UNKNOWN_CA) == G_TLS_CERTIFICATE_UNKNOWN_CA) {
                vfy_result = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT;
            } else if ((errors & G_TLS_CERTIFICATE_BAD_IDENTITY) ==
                       G_TLS_CERTIFICATE_BAD_IDENTITY) {
                vfy_result = X509_V_ERR_SUBJECT_ISSUER_MISMATCH;
            } else if ((errors & G_TLS_CERTIFICATE_NOT_ACTIVATED) ==
                       G_TLS_CERTIFICATE_NOT_ACTIVATED) {
                vfy_result = X509_V_ERR_CERT_NOT_YET_VALID;
            } else if ((errors & G_TLS_CERTIFICATE_EXPIRED) == G_TLS_CERTIFICATE_EXPIRED) {
                vfy_result = X509_V_ERR_CERT_HAS_EXPIRED;
            } else if ((errors & G_TLS_CERTIFICATE_REVOKED) == G_TLS_CERTIFICATE_REVOKED) {
                vfy_result = X509_V_ERR_CERT_REVOKED;
            } else {
                vfy_result = X509_V_ERR_APPLICATION_VERIFICATION;
            }

            result = libbalsa_is_cert_known(ossl_cert, vfy_result);
            X509_free(ossl_cert);
        }
    }

    return result;
}


gchar *
libbalsa_server_get_cert_pass(NetClient        *client,
			  	  	  	  	  const GByteArray *cert_der,
							  gpointer          user_data)
{
	/* FIXME - we just return the passphrase from the config, but we may also want to show a dialogue here... */
	return g_strdup(LIBBALSA_SERVER(user_data)->cert_passphrase);
}

/* Test whether a server is reachable */

typedef struct {
    LibBalsaCanReachCallback * cb;
    gpointer                   cb_data;
    GObject                  * source_object;
} CanReachInfo;

static void
libbalsa_server_can_reach_cb(GObject      * monitor,
                             GAsyncResult * res,
                             gpointer       user_data)
{
    CanReachInfo *info = user_data;
    gboolean can_reach;

    can_reach = g_network_monitor_can_reach_finish((GNetworkMonitor *) monitor, res, NULL);
    info->cb(info->source_object, can_reach, info->cb_data);

    g_object_unref(info->source_object);
    g_free(info);
}

void
libbalsa_server_test_can_reach_full(LibBalsaServer           * server,
                                    LibBalsaCanReachCallback * cb,
                                    gpointer                   cb_data,
                                    GObject                  * source_object)
{
    CanReachInfo *info;
    gchar *host;
    gchar *colon;
    GNetworkMonitor *monitor;
    GSocketConnectable *address;

    info = g_new(CanReachInfo, 1);
    info->cb              = cb;
    info->cb_data         = cb_data;
    info->source_object = g_object_ref(source_object);

    monitor = g_network_monitor_get_default();

    host = g_strdup(server->host);
    colon = strchr(host, ':');
    if (colon != NULL) {
    	colon[0] = '\0';
    }
    address = g_network_address_new(host, 0);
    g_free(host);
    g_network_monitor_can_reach_async(monitor, address, NULL,
                                      libbalsa_server_can_reach_cb, info);
    g_object_unref(address);
}

void
libbalsa_server_test_can_reach(LibBalsaServer           * server,
                               LibBalsaCanReachCallback * cb,
                               gpointer                   cb_data)
{
    libbalsa_server_test_can_reach_full(server, cb, cb_data, (GObject *) server);
}
