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
#endif                          /* defined(HAVE_LIBSECRET) */

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
    CONFIG_CHANGED,
    GET_PASSWORD,
    LAST_SIGNAL
};

static guint libbalsa_server_signals[LAST_SIGNAL];

typedef struct {
    GObject object;
    gchar *protocol; /**< type of the server: imap, pop3, or smtp. */

    gchar *host;
    gchar *user;
    gchar *passwd;
    NetClientCryptMode security;
    gboolean client_cert;
    gchar *cert_file;
    gchar *cert_passphrase;
    /* We include SSL support in UI unconditionally to preserve config
     * between SSL and non-SSL builds. We just fail if SSL is requested
     * in non-SSL build. */
    LibBalsaTlsMode tls_mode;
    unsigned use_ssl:1;
    unsigned remember_passwd:1;
    unsigned try_anonymous:1; /* user wants anonymous access */
} LibBalsaServerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(LibBalsaServer, libbalsa_server, G_TYPE_OBJECT)

static void
libbalsa_server_class_init(LibBalsaServerClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = libbalsa_server_finalize;

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
    klass->set_host     = libbalsa_server_real_set_host;
    klass->get_password = NULL;	/* libbalsa_server_real_get_password; */
}

static void
libbalsa_server_init(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    priv->protocol        = "pop3"; /* Is this a sane default value? */
    priv->host            = NULL;
    priv->user            = NULL;
    priv->passwd          = NULL;
    priv->remember_passwd = TRUE;
    priv->use_ssl         = FALSE;
    priv->tls_mode        = LIBBALSA_TLS_ENABLED;
    priv->security        = NET_CLIENT_CRYPT_STARTTLS;
}

static void
libbalsa_server_finalize(GObject * object)
{
    LibBalsaServer *server = (LibBalsaServer *) object;
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(priv->host);
    g_free(priv->user);
    g_free(priv->cert_file);

    if (priv->passwd != NULL) {
    	memset(priv->passwd, 0, strlen(priv->passwd));
    }
    libbalsa_free_password(priv->passwd);

    if (priv->cert_passphrase != NULL) {
    	memset(priv->cert_passphrase, 0, strlen(priv->cert_passphrase));
    }
    g_free(priv->cert_passphrase);

    G_OBJECT_CLASS(libbalsa_server_parent_class)->finalize(object);
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

    LIBBALSA_SERVER_GET_CLASS(server)->set_username(server, username);
}

void
libbalsa_server_set_password(LibBalsaServer * server,
                             const gchar * passwd)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    libbalsa_free_password(priv->passwd);
    priv->passwd = g_strdup(passwd);
}

void
libbalsa_server_set_host(LibBalsaServer * server, const gchar * host,
                         gboolean use_ssl)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    LIBBALSA_SERVER_GET_CLASS(server)->set_host(server, host, use_ssl);
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
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(priv->user);
    priv->user = g_strdup(username);
}

static void
libbalsa_server_real_set_host(LibBalsaServer * server, const gchar * host,
                              gboolean use_ssl)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(priv->host);
    priv->host = g_strdup(host);
    priv->use_ssl = use_ssl;
}


#if 0
static gchar *
libbalsa_server_real_get_password(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return g_strdup(priv->passwd);
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
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
    gboolean d;

    priv->host = libbalsa_conf_get_string("Server");
    if(priv->host != NULL && strrchr(priv->host, ':') == NULL) {
        gint port;
        port = libbalsa_conf_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", priv->host, port);
            g_free(priv->host);
            priv->host = newhost;
        }
    }

    priv->use_ssl = libbalsa_conf_get_bool("SSL=false");

    d = FALSE;
    priv->tls_mode = libbalsa_conf_get_int_with_default("TLSMode", &d);
    if (d)
        priv->tls_mode = LIBBALSA_TLS_ENABLED;

    d = FALSE;
    priv->security = libbalsa_conf_get_int_with_default("Security", &d);
    if (d) {
    	priv->security = NET_CLIENT_CRYPT_STARTTLS;
    }

    priv->user = libbalsa_conf_private_get_string("Username");
    if (priv->user == NULL)
	priv->user = g_strdup(getenv("USER"));

    priv->try_anonymous = libbalsa_conf_get_bool("Anonymous=false");
    priv->remember_passwd = libbalsa_conf_get_bool("RememberPasswd=false");

    if(priv->remember_passwd) {
#if defined(HAVE_LIBSECRET)
        GError *err = NULL;

        priv->passwd =
            secret_password_lookup_sync(LIBBALSA_SERVER_SECRET_SCHEMA,
                                        NULL, &err,
                                        "protocol", priv->protocol,
                                        "server",   priv->host,
                                        "user",     priv->user,
                                        NULL);
        if (err) {
            libbalsa_free_password(priv->passwd);
            priv->passwd = NULL;
            printf(_("Error looking up password for %s@%s: %s\n"),
                   priv->user, priv->host, err->message);
            printf(_("Falling back\n"));
            g_clear_error(&err);
            priv->passwd = libbalsa_conf_private_get_string("Password");
            if (priv->passwd != NULL) {
                gchar *buff = libbalsa_rot(priv->passwd);
                libbalsa_free_password(priv->passwd);
                priv->passwd = buff;
                secret_password_store_sync
                    (LIBBALSA_SERVER_SECRET_SCHEMA, NULL,
                     _("Balsa passwords"), priv->passwd, NULL, &err,
                     "protocol", priv->protocol,
                     "server",   priv->host,
                     "user",     priv->user,
                     NULL);
                /* We could in principle clear the password in the
                 * config file here but we do not for the backward
                 * compatibility. */
                if (err) {
                    printf(_("Error storing password for %s@%s: %s\n"),
                           priv->user, priv->host, err->message);
                    g_error_free(err);
                }
            }
        }
#else
	priv->passwd = libbalsa_conf_private_get_string("Password");
	if (priv->passwd != NULL) {
	    gchar *buff = libbalsa_rot(priv->passwd);
	    libbalsa_free_password(priv->passwd);
	    priv->passwd = buff;
	}
#endif
    }
    if(priv->passwd && priv->passwd[0] == '\0') {
	libbalsa_free_password(priv->passwd);
	priv->passwd = NULL;
    }

    priv->client_cert = libbalsa_conf_get_bool("NeedClientCert=false");
    priv->cert_file = libbalsa_conf_get_string("UserCertificateFile");
    priv->cert_passphrase = libbalsa_conf_private_get_string("CertificatePassphrase");
    if ((priv->cert_passphrase != NULL) && (priv->cert_passphrase[0] != '\0')) {
        gchar *tmp = libbalsa_rot(priv->cert_passphrase);

        g_free(priv->cert_passphrase);
        priv->cert_passphrase = tmp;
    } else {
    	g_free(priv->cert_passphrase);
    	priv->cert_passphrase = NULL;
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
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    libbalsa_conf_set_string("Server", priv->host);
    libbalsa_conf_private_set_string("Username", priv->user);
    libbalsa_conf_set_bool("Anonymous",          priv->try_anonymous);
    libbalsa_conf_set_bool("RememberPasswd", 
                          priv->remember_passwd && priv->passwd != NULL);

    if (priv->remember_passwd && priv->passwd != NULL) {
#if defined(HAVE_LIBSECRET)
        GError *err = NULL;

        secret_password_store_sync(LIBBALSA_SERVER_SECRET_SCHEMA, NULL,
                                   _("Balsa passwords"), priv->passwd,
                                   NULL, &err,
                                   "protocol", priv->protocol,
                                   "server",   priv->host,
                                   "user",     priv->user,
                                   NULL);
        if (err) {
            printf(_("Error storing password for %s@%s: %s\n"),
                   priv->user, priv->host, err->message);
            g_error_free(err);
        }
#else
	gchar *buff = libbalsa_rot(priv->passwd);
	libbalsa_conf_private_set_string("Password", buff);
	g_free(buff);
#endif                          /* defined(HAVE_LIBSECRET) */
    }

    libbalsa_conf_set_bool("NeedClientCert", priv->client_cert);
    if (priv->cert_file != NULL) {
    	libbalsa_conf_set_string("UserCertificateFile", priv->cert_file);
    }
    if (priv->cert_passphrase != NULL) {
        gchar *tmp = libbalsa_rot(priv->cert_passphrase);

        libbalsa_conf_private_set_string("CertificatePassphrase", tmp);
        g_free(tmp);
    }

    libbalsa_conf_set_bool("SSL", priv->use_ssl);
    libbalsa_conf_set_int("TLSMode", priv->tls_mode);
    libbalsa_conf_set_int("Security", priv->security);
}

void
libbalsa_server_user_cb(ImapUserEventType ue, void *arg, ...)
{
    va_list alist;
    int *ok;
    LibBalsaServer *is = LIBBALSA_SERVER(arg);
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(is);

    va_start(alist, arg);
    switch(ue) {
    case IME_GET_USER_PASS: {
        gchar *method = va_arg(alist, gchar*);
        gchar **user = va_arg(alist, gchar**);
        gchar **pass = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
        if(!priv->passwd) {
            priv->passwd = libbalsa_server_get_password(is, NULL);
        }
        *ok = priv->passwd != NULL;
        if(*ok) {
            g_free(*user); *user = g_strdup(priv->user);
            g_free(*pass); *pass = g_strdup(priv->passwd);
            libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                                 /* host, authentication method */
                                 _("Logging in to %s using %s"), 
                                   priv->host, method);
        }
        break;
    }
    case IME_GET_USER:  { /* for eg kerberos */
        gchar **user;
        va_arg(alist, gchar*); /* Ignore the method */
        user = va_arg(alist, gchar**);
        ok = va_arg(alist, int*);
        *ok = 1; /* consider popping up a dialog window here */
        g_free(*user); *user = g_strdup(priv->user);
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
        /* *ok = libbalsa_abort_on_timeout(priv->host); */
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
libbalsa_server_connect_get_password(LibBalsaServer * server, GCallback cb,
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
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
    gchar **result = NULL;

    g_debug("%s: %p %p: encrypted = %d", __func__, client, user_data,
            net_client_is_encrypted(client));
    if (priv->try_anonymous == 0U) {
        result = g_new0(gchar *, 3U);
        result[0] = g_strdup(priv->user);
        if (need_passwd) {
        	if ((priv->passwd != NULL) && (priv->passwd[0] != '\0')) {
        		result[1] = g_strdup(priv->passwd);
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
    LibBalsaServer *server = LIBBALSA_SERVER(user_data);
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

	/* FIXME - we just return the passphrase from the config, but we may also want to show a dialogue here... */
	return g_strdup(priv->cert_passphrase);
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
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
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

    host = g_strdup(priv->host);
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

/*
 * Getters
 */

LibBalsaTlsMode
libbalsa_server_get_tls_mode(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), (LibBalsaTlsMode) 0);

    return priv->tls_mode;
}

NetClientCryptMode
libbalsa_server_get_security(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), (NetClientCryptMode) 0);

    return priv->security;
}

gboolean
libbalsa_server_get_use_ssl(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), FALSE);

    return priv->use_ssl;
}

gboolean
libbalsa_server_get_client_cert(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), FALSE);

    return priv->client_cert;
}

gboolean
libbalsa_server_get_try_anonymous(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), FALSE);

    return priv->try_anonymous;
}

gboolean
libbalsa_server_get_remember_passwd(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), FALSE);

    return priv->remember_passwd;
}

const gchar *
libbalsa_server_get_user(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->user;
}

const gchar *
libbalsa_server_get_host(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->host;
}

const gchar *
libbalsa_server_get_protocol(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->protocol;
}

const gchar *
libbalsa_server_get_cert_file(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->cert_file;
}

const gchar *
libbalsa_server_get_cert_passphrase(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->cert_passphrase;
}

const gchar *
libbalsa_server_get_passwd(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->passwd;
}

/*
 * Setters
 */

void
libbalsa_server_set_tls_mode(LibBalsaServer * server, LibBalsaTlsMode tls_mode)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->tls_mode = tls_mode;
}

void
libbalsa_server_set_security(LibBalsaServer * server, NetClientCryptMode security)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->security = security;
}

void
libbalsa_server_set_try_anonymous(LibBalsaServer * server, gboolean try_anonymous)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->try_anonymous = try_anonymous;
}

void
libbalsa_server_set_remember_passwd(LibBalsaServer * server, gboolean remember_passwd)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->remember_passwd = remember_passwd;
}

void
libbalsa_server_set_client_cert(LibBalsaServer * server, gboolean client_cert)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->client_cert = client_cert;
}

void
libbalsa_server_set_protocol(LibBalsaServer * server, const gchar * protocol)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(priv->protocol);
    priv->protocol = g_strdup(protocol);
}

void
libbalsa_server_set_cert_file(LibBalsaServer * server, const gchar * cert_file)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(priv->cert_file);
    priv->cert_file = g_strdup(cert_file);
}

void
libbalsa_server_set_cert_passphrase(LibBalsaServer * server, const gchar * cert_passphrase)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(priv->cert_passphrase);
    priv->cert_passphrase = g_strdup(cert_passphrase);
}
