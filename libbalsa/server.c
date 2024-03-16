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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "libbalsa-conf.h"
#include "net-client-utils.h"
#include <glib/gi18n.h>


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "libbalsa-server"


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

static const SecretSchema cert_pass_schema = {
    "org.gnome.Balsa.CertificatePassword", SECRET_SCHEMA_NONE,
    {
	{ "protocol", SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ "server",   SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ "user",     SECRET_SCHEMA_ATTRIBUTE_STRING },
	{ NULL, 0 }
    }
};
#endif                          /* defined(HAVE_LIBSECRET) */

typedef struct _LibBalsaServerPrivate LibBalsaServerPrivate;

struct _LibBalsaServerPrivate {
    const gchar *protocol; /**< type of the server: imap, pop3, or smtp. */
    gchar *host;
    gchar *user;
    gchar *passwd;
    NetClientCryptMode security;
    gboolean client_cert;
    gchar *cert_file;
    gchar *cert_passphrase;
    gboolean remember_passwd;
    gboolean remember_cert_passphrase;
    NetClientAuthMode auth_mode;
};

static void libbalsa_server_finalize(GObject * object);

static void libbalsa_server_real_set_username(LibBalsaServer *server,
					      const gchar    *username);
static void libbalsa_server_real_set_host(LibBalsaServer    *server,
                                          const gchar       *host,
                                          NetClientCryptMode security);
static gchar *lbs_get_password(LibBalsaServer *server,
                                           gchar          *cert_subject);
static gchar *libbalsa_free_password(gchar *password);

G_DEFINE_TYPE_WITH_PRIVATE(LibBalsaServer, libbalsa_server, G_TYPE_OBJECT)

enum {
    SET_USERNAME,
    SET_HOST,
    CONFIG_CHANGED,
    GET_PASSWORD,
    LAST_SIGNAL
};

static guint libbalsa_server_signals[LAST_SIGNAL];

static void
libbalsa_server_class_init(LibBalsaServerClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = libbalsa_server_finalize;

    libbalsa_server_signals[SET_USERNAME] =
	g_signal_new("set-username",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
				     set_username),
                     NULL, NULL, NULL,
                     G_TYPE_NONE, 1,
		     G_TYPE_STRING);
    libbalsa_server_signals[SET_HOST] =
	g_signal_new("set-host",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
                                     set_host),
                     NULL, NULL, NULL,
                     G_TYPE_NONE, 2,
                     G_TYPE_POINTER, G_TYPE_INT
		     );
    libbalsa_server_signals[CONFIG_CHANGED] =
	g_signal_new("config-changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
		     G_STRUCT_OFFSET(LibBalsaServerClass,
				     config_changed),
                     NULL, NULL, NULL,
                     G_TYPE_NONE, 0);

    libbalsa_server_signals[GET_PASSWORD] =
	g_signal_new("get-password",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
					 0U,
                     NULL, NULL, NULL,
                     G_TYPE_STRING, 1,
					 G_TYPE_BYTE_ARRAY);

    klass->set_username = libbalsa_server_real_set_username;
    klass->set_host = libbalsa_server_real_set_host;
}

static void
libbalsa_server_init(LibBalsaServer * server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    priv->protocol = NULL;
    priv->host = NULL;
    priv->user = NULL;
    priv->passwd = NULL;
    priv->remember_passwd = TRUE;
    priv->security		= NET_CLIENT_CRYPT_STARTTLS;
}

/* leave object in sane state (NULLified fields) */
static void
libbalsa_server_finalize(GObject * object)
{
    LibBalsaServer *server = LIBBALSA_SERVER(object);
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_free(priv->host);
    g_free(priv->user);
    g_free(priv->cert_file);
    priv->passwd = libbalsa_free_password(priv->passwd);
    priv->cert_passphrase = libbalsa_free_password(priv->cert_passphrase);

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

    g_signal_emit(server,
		  libbalsa_server_signals[SET_USERNAME], 0, username);
}

void
libbalsa_server_set_password(LibBalsaServer *server,
                             const gchar    *passwd,
							 gboolean        for_cert)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
	gchar **target;
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    if (for_cert) {
        target = &priv->cert_passphrase;
    } else {
        target = &priv->passwd;
    }

    *target = libbalsa_free_password(*target);
    if ((passwd != NULL) && (passwd[0] != '\0')) {
        *target = g_strdup(passwd);
    }
}

void
libbalsa_server_set_host(LibBalsaServer * server, const gchar * host,
                         NetClientCryptMode security)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_signal_emit(server, libbalsa_server_signals[SET_HOST],
		  0, host, security);

}

void
libbalsa_server_config_changed(LibBalsaServer * server)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_signal_emit(server, libbalsa_server_signals[CONFIG_CHANGED],
                  0);
}

static gchar *
lbs_get_password(LibBalsaServer *server,
			     	 	 	 gchar          *cert_subject)
{
    gchar *retval = NULL;

    g_return_val_if_fail(server != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    g_signal_emit(server, libbalsa_server_signals[GET_PASSWORD], 0, cert_subject, &retval);
    return retval;
}

static void
libbalsa_server_real_set_username(LibBalsaServer * server,
				  const gchar * username)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_free(priv->user);
    priv->user = g_strdup(username);
}

static void
libbalsa_server_real_set_host(LibBalsaServer * server, const gchar * host,
	NetClientCryptMode  security)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_free(priv->host);
    priv->host = g_strdup(host);
    priv->security = security;
}

void
libbalsa_server_load_security_config(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
	gboolean not_found;

    priv->security = libbalsa_conf_get_int_with_default("Security", &not_found);
    if (not_found) {
        gboolean want_ssl;

        g_debug("server %s@%s: no security config, try to read old settings", priv->protocol, priv->host);
        want_ssl = libbalsa_conf_get_bool_with_default("SSL", &not_found);
        if (want_ssl && !not_found) {
        	priv->security = NET_CLIENT_CRYPT_ENCRYPTED;
        	libbalsa_conf_clean_key("SSL");
        } else {
        	int want_tls;

        	want_tls = libbalsa_conf_get_int_with_default("TLSMode", &not_found);
        	if (not_found) {
        		priv->security = NET_CLIENT_CRYPT_STARTTLS;
        	} else {
        		switch (want_tls) {
        		case 0:
        			priv->security = NET_CLIENT_CRYPT_NONE;
        			break;
        		case 1:
        			priv->security = NET_CLIENT_CRYPT_STARTTLS_OPT;
        			break;
        		default:
        			priv->security = NET_CLIENT_CRYPT_STARTTLS;
        		}
            	libbalsa_conf_clean_key("SSL");
        	}
        }
    }
}


#if defined(HAVE_LIBSECRET)

static gchar *
libbalsa_free_password(gchar *password)
{
	secret_password_free(password);
	return NULL;
}

static void
store_password_libsecret(LibBalsaServer     *server,
						 const gchar        *password,
						 const gchar        *cfg_path,
						 const SecretSchema *schema)
{
	if (libbalsa_conf_use_libsecret()) {
		LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
		GError *err = NULL;

		secret_password_store_sync(schema, NULL, _("Balsa passwords"), password, NULL, &err,
			"protocol", priv->protocol,
			"server",   priv->host,
			"user",     priv->user,
			NULL);
		if (err != NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				/* Translators: #1 user name; #2 protocol (imap, etc); #3 server name; #4 error message */
				_("Error saving credentials for user “%s”, protocol “%s”, server “%s” in Secret Service: %s"),
				priv->user, priv->protocol, priv->host, err->message);
			g_error_free(err);
		} else {
			g_debug("stored password for %s@%s:%s in secret service", priv->user, priv->protocol, priv->host);
			libbalsa_conf_clean_key(cfg_path);
		}
	} else {
		libbalsa_conf_private_set_string(cfg_path, password, TRUE);
	}
}

static gchar *
load_password_libsecret(LibBalsaServer     *server,
						const gchar        *cfg_path,
						const SecretSchema *schema)
{
	gchar *password;

	if (libbalsa_conf_use_libsecret()) {
		LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
		GError *err = NULL;

		password = secret_password_lookup_sync(schema, NULL, &err,
			"protocol", priv->protocol,
			"server",   priv->host,
			"user",     priv->user,
			NULL);
		if (err != NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				/* Translators: #1 user name; #2 protocol (imap, etc); #3 server name; #4 error message */
				_("Error loading credentials for user %s, protocol “%s”, server “%s” from Secret Service: %s"),
				priv->user, priv->protocol, priv->host, err->message);
			g_error_free(err);
		}

		/* check the config file if the returned password is NULL, make sure to remove it from the config file otherwise */
		if (password == NULL) {
			password = libbalsa_conf_private_get_string(cfg_path, TRUE);
			if (password != NULL) {
				store_password_libsecret(server, password, cfg_path, schema);
			}
		} else {
			g_debug("loaded password for %s@%s:%s from secret service", priv->user, priv->protocol, priv->host);
			libbalsa_conf_clean_key(cfg_path);
		}
	} else {
		password = libbalsa_conf_private_get_string(cfg_path, TRUE);
	}

	return password;
}

static void
erase_password_libsecret(LibBalsaServer *server,
						 const SecretSchema   *schema)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
    GError *err = NULL;
    gboolean removed;

    removed = secret_password_clear_sync(schema, NULL, &err,
        "protocol", priv->protocol,
		"server",   priv->host,
		"user",     priv->user,
		NULL);
    if (err != NULL) {
        g_info("error erasing password for %s@%s:%s from secret service: %s", priv->user, priv->protocol, priv->host,
        	err->message);
        g_error_free(err);
    } else if (removed) {
        g_debug("erased password for %s@%s:%s from secret service", priv->user, priv->protocol, priv->host);
    } else {
        g_debug("no password erased for %s@%s:%s from secret service", priv->user, priv->protocol, priv->host);
    }
}

#else

static gchar *
libbalsa_free_password(gchar *password)
{
	net_client_free_authstr(password);
	return NULL;
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

    priv->host = libbalsa_conf_get_string("Server");
    if ((priv->host != NULL) && (strrchr(priv->host, ':') == NULL)) {
        gint port;
        gboolean d;

        port = libbalsa_conf_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", priv->host, port);

            g_free(priv->host);
            priv->host = newhost;
        }
    }
    libbalsa_server_load_security_config(server);
    priv->user = libbalsa_conf_private_get_string("Username", FALSE);
    if (priv->user == NULL) {
        priv->user = g_strdup(g_get_user_name());
    }

	priv->auth_mode = libbalsa_conf_get_int("AuthMode=2");  /* default NET_CLIENT_AUTH_USER_PASS */
    if (libbalsa_conf_has_key("Anonymous")) {
    	if (libbalsa_conf_get_bool("Anonymous")) {
    		priv->auth_mode = NET_CLIENT_AUTH_NONE_ANON;
    	}
    	libbalsa_conf_clean_key("Anonymous");
    }
    priv->remember_passwd = libbalsa_conf_get_bool("RememberPasswd=false");

    priv->passwd = libbalsa_free_password(priv->passwd);
    if (priv->remember_passwd) {
#if defined(HAVE_LIBSECRET)
        priv->passwd = load_password_libsecret(server, "Password", &server_schema);
#else
        priv->passwd = libbalsa_conf_private_get_string("Password", TRUE);
#endif		/* HAVE_LIBSECRET */
        if ((priv->passwd != NULL) && (priv->passwd[0] == '\0')) {
        	priv->passwd = libbalsa_free_password(priv->passwd);
        }
    }

    /* client certificate stuff */
    priv->client_cert = libbalsa_conf_get_bool("NeedClientCert=false");
    priv->cert_file = libbalsa_conf_get_string("UserCertificateFile");
    priv->remember_cert_passphrase = libbalsa_conf_get_bool("RememberCertPasswd=false");

    priv->cert_passphrase = libbalsa_free_password(priv->cert_passphrase);
    if (priv->client_cert && priv->remember_cert_passphrase) {
#if defined(HAVE_LIBSECRET)
        priv->cert_passphrase = load_password_libsecret(server, "CertificatePassphrase", &cert_pass_schema);
#else
        priv->cert_passphrase = libbalsa_conf_private_get_string("CertificatePassphrase", TRUE);
#endif		/* HAVE_LIBSECRET */
        if ((priv->cert_passphrase != NULL) && (priv->cert_passphrase[0] == '\0')) {
        	priv->cert_passphrase = libbalsa_free_password(priv->cert_passphrase);
        }
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
    libbalsa_conf_private_set_string("Username", priv->user, FALSE);
    libbalsa_conf_set_int("AuthMode", priv->auth_mode);

    if (priv->remember_passwd && (priv->passwd != NULL)) {
        libbalsa_conf_set_bool("RememberPasswd", TRUE);
#if defined(HAVE_LIBSECRET)
        store_password_libsecret(server, priv->passwd, "Password", &server_schema);
#else
        libbalsa_conf_private_set_string("Password", priv->passwd, TRUE);
#endif
    } else {
        libbalsa_conf_set_bool("RememberPasswd", FALSE);
#if defined(HAVE_LIBSECRET)
        erase_password_libsecret(server, &server_schema);
#endif		/* HAVE_LIBSECRET */
        libbalsa_conf_private_set_string("Password", NULL, TRUE);
    }

    libbalsa_conf_set_bool("NeedClientCert", priv->client_cert);
    if (priv->cert_file != NULL) {
        libbalsa_conf_set_string("UserCertificateFile", priv->cert_file);
        if (priv->remember_cert_passphrase && (priv->cert_passphrase != NULL)) {
        	libbalsa_conf_set_bool("RememberCertPasswd", TRUE);
#if defined(HAVE_LIBSECRET)
        	store_password_libsecret(server, priv->cert_passphrase, "CertificatePassphrase", &cert_pass_schema);
#else
        	libbalsa_conf_private_set_string("CertificatePassphrase", priv->cert_passphrase, TRUE);
#endif

        } else {
        	libbalsa_conf_set_bool("RememberCertPasswd", FALSE);
#if defined(HAVE_LIBSECRET)
        	erase_password_libsecret(server, &cert_pass_schema);
#endif		/* HAVE_LIBSECRET */
        	libbalsa_conf_private_set_string("CertificatePassphrase", NULL, TRUE);
        }
    }

    libbalsa_conf_set_int("Security", priv->security);
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
libbalsa_server_get_auth(NetClient         *client,
						 NetClientAuthMode  mode,
						 gpointer           user_data)
{
    LibBalsaServer *server = LIBBALSA_SERVER(user_data);
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
    gchar **result = NULL;

    g_debug("%s: %p %d %p: encrypted = %d", __func__, client, mode, user_data, net_client_is_encrypted(client));
    if (priv->auth_mode != NET_CLIENT_AUTH_NONE_ANON) {
        result = g_new0(gchar *, 3U);
        result[0] = g_strdup(priv->user);
        switch (mode) {
        case NET_CLIENT_AUTH_USER_PASS:
            if ((priv->passwd != NULL) && (priv->passwd[0] != '\0')) {
            	result[1] = g_strdup(priv->passwd);
            } else {
            	result[1] = lbs_get_password(server, NULL);
            }
            break;
        case NET_CLIENT_AUTH_KERBEROS:
        	break;			/* only user name required */
        default:
        	g_assert_not_reached();
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
	return libbalsa_is_cert_known(peer_cert, errors);
}


gchar *
libbalsa_server_get_cert_pass(NetClient *client,
			  	  	  	  	  gchar     *cert_subject,
							  gpointer   user_data)
{
    LibBalsaServer *server = LIBBALSA_SERVER(user_data);
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);
    gchar *result;

    if ((priv->cert_passphrase != NULL) && (priv->cert_passphrase[0] != '\0')) {
        result = g_strdup(priv->cert_passphrase);
    } else {
        result = lbs_get_password(server, cert_subject);
    }
	return result;
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
    GNetworkMonitor *monitor;
    GSocketConnectable *address;

    info = g_new(CanReachInfo, 1);
    info->cb              = cb;
    info->cb_data         = cb_data;
    info->source_object = g_object_ref(source_object);

    monitor = g_network_monitor_get_default();

    host = net_client_host_only(priv->host);
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

const gchar *
libbalsa_server_get_host(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->host;
}

const gchar *
libbalsa_server_get_user(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->user;
}

const gchar *
libbalsa_server_get_cert_file(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->cert_file;
}

const gchar *
libbalsa_server_get_protocol(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->protocol;
}

const gchar *
libbalsa_server_get_password(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->passwd;
}

const gchar *
libbalsa_server_get_cert_passphrase(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return priv->cert_passphrase;
}

NetClientCryptMode
libbalsa_server_get_security(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), (NetClientCryptMode) 0);

    return priv->security;
}

NetClientAuthMode
libbalsa_server_get_auth_mode(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), (NetClientAuthMode) 0);

    return priv->auth_mode;
}

gboolean
libbalsa_server_get_client_cert(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), FALSE);

    return priv->client_cert;
}

gboolean
libbalsa_server_get_remember_password(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), FALSE);

    return priv->remember_passwd;
}

gboolean
libbalsa_server_get_remember_cert_passphrase(LibBalsaServer *server)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), FALSE);

    return priv->remember_cert_passphrase;
}

/*
 * Setters
 */

void
libbalsa_server_set_protocol(LibBalsaServer *server, const gchar *protocol)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));
    g_return_if_fail(priv->protocol == NULL);

    /* We do not allocate a string for the protocol: */
    priv->protocol = protocol;
}

void
libbalsa_server_set_cert_file(LibBalsaServer *server, const gchar *cert_file)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(priv->cert_file);
    priv->cert_file = g_strdup(cert_file);
}

void
libbalsa_server_set_security(LibBalsaServer *server, NetClientCryptMode security)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->security = security;
}

void
libbalsa_server_set_auth_mode(LibBalsaServer *server, NetClientAuthMode auth_mode)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->auth_mode = auth_mode;
}

void
libbalsa_server_set_remember_password(LibBalsaServer *server, gboolean remember_password)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->remember_passwd = remember_password;
}

void
libbalsa_server_set_client_cert(LibBalsaServer *server, gboolean client_cert)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->client_cert = client_cert;
}

void
libbalsa_server_set_remember_cert_passphrase(LibBalsaServer *server, gboolean remember_cert_passphrase)
{
    LibBalsaServerPrivate *priv = libbalsa_server_get_instance_private(server);

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    priv->remember_cert_passphrase = remember_cert_passphrase;
}
