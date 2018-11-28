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

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "libbalsa-conf.h"
#include "net-client-utils.h"
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

static GObjectClass *parent_class = NULL;
static void libbalsa_server_class_init(LibBalsaServerClass * klass);
static void libbalsa_server_init(LibBalsaServer * server);
static void libbalsa_server_finalize(GObject * object);

static void libbalsa_server_real_set_username(LibBalsaServer * server,
					      const gchar * username);
static void libbalsa_server_real_set_host(LibBalsaServer     *server,
					  	  	  	  	  	  const gchar        *host,
										  NetClientCryptMode  security);
static gchar *libbalsa_server_get_password(LibBalsaServer *server,
										   gchar          *cert_subject);
static gchar *libbalsa_free_password(gchar *password);

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
    server->protocol = "pop3"; /* Is this a sane default value? */
    server->host = NULL;
    server->user = NULL;
    server->passwd = NULL;
    server->remember_passwd = TRUE;
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
    server->passwd = libbalsa_free_password(server->passwd);

    g_free(server->cert_file);
    server->cert_file = NULL;
    server->cert_passphrase = libbalsa_free_password(server->cert_passphrase);

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
libbalsa_server_set_password(LibBalsaServer *server,
                             const gchar    *passwd,
							 gboolean        for_cert)
{
	gchar **target;
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    if (for_cert) {
    	target = &server->cert_passphrase;
    } else {
    	target = &server->passwd;
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

    g_signal_emit(G_OBJECT(server), libbalsa_server_signals[SET_HOST],
		  0, host, security);

}

void
libbalsa_server_config_changed(LibBalsaServer * server)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_signal_emit(G_OBJECT(server), libbalsa_server_signals[CONFIG_CHANGED],
                  0);
}

static gchar *
libbalsa_server_get_password(LibBalsaServer *server,
			     	 	 	 gchar          *cert_subject)
{
    gchar *retval = NULL;

    g_return_val_if_fail(server != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    g_signal_emit(G_OBJECT(server), libbalsa_server_signals[GET_PASSWORD], 0, cert_subject, &retval);
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
	NetClientCryptMode  security)
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(server->host);
    server->host = g_strdup(host);
    server->security = security;
}

void
libbalsa_server_load_security_config(LibBalsaServer *server)
{
	gboolean not_found;

    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    server->security = libbalsa_conf_get_int_with_default("Security", &not_found);
    if (not_found) {
    	gboolean want_ssl;

    	g_debug("server %s@%s: no security config, try to read old settings", server->protocol, server->host);
    	want_ssl = libbalsa_conf_get_bool_with_default("SSL", &not_found);
    	if (want_ssl && !not_found) {
    		server->security = NET_CLIENT_CRYPT_ENCRYPTED;
    	} else {
    		int want_tls;

    		want_tls = libbalsa_conf_get_int_with_default("TLSMode", &not_found);
    		if (not_found) {
    			server->security = NET_CLIENT_CRYPT_STARTTLS;
    		} else {
    			switch (want_tls) {
    			case 0:
    				server->security = NET_CLIENT_CRYPT_NONE;
    				break;
    			case 1:
    				server->security = NET_CLIENT_CRYPT_STARTTLS_OPT;
    				break;
    			default:
    				server->security = NET_CLIENT_CRYPT_STARTTLS;
    			}
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
store_password_libsecret(const LibBalsaServer *server,
	    				 const gchar          *password,
						 const SecretSchema   *schema)
{
    GError *err = NULL;

    secret_password_store_sync(schema, NULL, _("Balsa passwords"), password, NULL, &err,
    	"protocol", server->protocol,
		"server",   server->host,
		"user",     server->user,
		NULL);
    if (err != NULL) {
    	g_info(_("Error storing password for %s@%s:%s: %s"), server->user, server->protocol, server->host, err->message);
        g_error_free(err);
    } else {
    	g_debug("stored password for %s@%s:%s in secret service", server->user, server->protocol, server->host);
    }
}

static gchar *
load_password_libsecret(const LibBalsaServer *server,
			  	  	    const gchar          *cfg_path,
						const SecretSchema   *schema)
{
    GError *err = NULL;
    gchar *password;

    password = secret_password_lookup_sync(schema, NULL, &err,
    	"protocol", server->protocol,
		"server",   server->host,
		"user",     server->user,
		NULL);
    if (err != NULL) {
        g_info(_("Error looking up password for %s@%s:%s: %s"), server->user, server->protocol, server->host, err->message);
        g_error_free(err);

        /* fall back to the private config file */
        password = libbalsa_conf_private_get_string(cfg_path, TRUE);
        if ((password != NULL) && (password[0] != '\0')) {
        	g_info(_("loaded fallback password from private config file"));
        	/* store a fallback password in the key ring */
        	store_password_libsecret(server, password, schema);
            /* We could in principle clear the password in the config file here but we do not for the backward compatibility.
             * FIXME - Is this really the proper approach, as we leave the obfuscated password in the config file, and the user
             * relies on the message that the password is stored in the Secret Service only.  OTOH, there are better methods for
             * protecting the user's secrets, like full disk encryption. */
        }
    } else {
    	g_debug("loaded password for %s@%s:%s from secret service", server->user, server->protocol, server->host);
    }

    return password;
}

static void
erase_password_libsecret(const LibBalsaServer *server,
						 const SecretSchema   *schema)
{
    GError *err = NULL;
    gboolean removed;

    removed = secret_password_clear_sync(schema, NULL, &err,
    	"protocol", server->protocol,
		"server",   server->host,
		"user",     server->user,
		NULL);
    if (err != NULL) {
    	g_info("error erasing password for %s@%s:%s from secret service: %s", server->user, server->protocol, server->host,
    		err->message);
    	g_error_free(err);
    } else if (removed) {
    	g_debug("erased password for %s@%s:%s from secret service", server->user, server->protocol, server->host);
    } else {
    	g_debug("no password erased for %s@%s:%s from secret service", server->user, server->protocol, server->host);
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
    server->host = libbalsa_conf_get_string("Server");
    if ((server->host != NULL) && (strrchr(server->host, ':') == NULL)) {
        gint port;
        gboolean d;

        port = libbalsa_conf_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", server->host, port);

            g_free(server->host);
            server->host = newhost;
        }
    }
    libbalsa_server_load_security_config(server);
    server->user = libbalsa_conf_private_get_string("Username", FALSE);
    if (server->user == NULL) {
    	server->user = g_strdup(g_get_user_name());
    }

    server->try_anonymous = libbalsa_conf_get_bool("Anonymous=false");
    server->remember_passwd = libbalsa_conf_get_bool("RememberPasswd=false");

    server->passwd = libbalsa_free_password(server->passwd);
    if (server->remember_passwd) {
#if defined(HAVE_LIBSECRET)
    	server->passwd = load_password_libsecret(server, "Password", &server_schema);
#else
    	server->passwd = libbalsa_conf_private_get_string("Password", TRUE);
#endif		/* HAVE_LIBSECRET */
    	if ((server->passwd != NULL) && (server->passwd[0] == '\0')) {
    		server->passwd = libbalsa_free_password(server->passwd);
    	}
    }

    /* client certificate stuff */
    server->client_cert = libbalsa_conf_get_bool("NeedClientCert=false");
    server->cert_file = libbalsa_conf_get_string("UserCertificateFile");
    server->remember_cert_passphrase = libbalsa_conf_get_bool("RememberCertPasswd=false");

    server->cert_passphrase = libbalsa_free_password(server->cert_passphrase);
    if (server->client_cert && server->remember_cert_passphrase) {
#if defined(HAVE_LIBSECRET)
    	server->cert_passphrase = load_password_libsecret(server, "CertificatePassphrase", &cert_pass_schema);
#else
    	server->cert_passphrase = libbalsa_conf_private_get_string("CertificatePassphrase", TRUE);
#endif		/* HAVE_LIBSECRET */
    	if ((server->cert_passphrase != NULL) && (server->cert_passphrase[0] == '\0')) {
    		server->cert_passphrase = libbalsa_free_password(server->cert_passphrase);
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
    libbalsa_conf_set_string("Server", server->host);
    libbalsa_conf_private_set_string("Username", server->user, FALSE);
    libbalsa_conf_set_bool("Anonymous",          server->try_anonymous);

    if (server->remember_passwd && (server->passwd != NULL)) {
    	libbalsa_conf_set_bool("RememberPasswd", TRUE);
#if defined(HAVE_LIBSECRET)
    	store_password_libsecret(server, server->passwd, &server_schema);
#else
    	libbalsa_conf_private_set_string("Password", server->passwd, TRUE);
#endif
    } else {
    	libbalsa_conf_set_bool("RememberPasswd", FALSE);
#if defined(HAVE_LIBSECRET)
    	erase_password_libsecret(server, &server_schema);
#endif		/* HAVE_LIBSECRET */
    	libbalsa_conf_private_set_string("Password", NULL, TRUE);
    }

    libbalsa_conf_set_bool("NeedClientCert", server->client_cert);
    if (server->cert_file != NULL) {
    	libbalsa_conf_set_string("UserCertificateFile", server->cert_file);
    	if (server->remember_cert_passphrase && (server->cert_passphrase != NULL)) {
    		libbalsa_conf_set_bool("RememberCertPasswd", TRUE);
#if defined(HAVE_LIBSECRET)
    		store_password_libsecret(server, server->cert_passphrase, &cert_pass_schema);
#else
    		libbalsa_conf_private_set_string("CertificatePassphrase", server->cert_passphrase, TRUE);
#endif

    	} else {
    		libbalsa_conf_set_bool("RememberCertPasswd", FALSE);
#if defined(HAVE_LIBSECRET)
    		erase_password_libsecret(server, &cert_pass_schema);
#endif		/* HAVE_LIBSECRET */
    		libbalsa_conf_private_set_string("CertificatePassphrase", NULL, TRUE);
    	}
    }

    libbalsa_conf_set_int("Security", server->security);
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
    if (!server->try_anonymous || (strcmp(server->protocol, "imap") == 0)) {
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
	return libbalsa_is_cert_known(peer_cert, errors);
}


gchar *
libbalsa_server_get_cert_pass(NetClient *client,
			  	  	  	  	  gchar     *cert_subject,
							  gpointer   user_data)
{
    LibBalsaServer *server = LIBBALSA_SERVER(user_data);
    gchar *result;

    if ((server->cert_passphrase != NULL) && (server->cert_passphrase[0] != '\0')) {
    	result = g_strdup(server->cert_passphrase);
    } else {
    	result = libbalsa_server_get_password(server, cert_subject);
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
