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

#ifndef __LIBBALSA_SERVER_H__
#define __LIBBALSA_SERVER_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "imap/libimap.h"
#include "libbalsa.h"
#include "net-client.h"

#if defined(HAVE_LIBSECRET)
#include <libsecret/secret.h>
extern const SecretSchema *LIBBALSA_SERVER_SECRET_SCHEMA;
#define libbalsa_free_password secret_password_free
#else
#define libbalsa_free_password g_free
#endif                          /* defined(HAVE_LIBSECRET) */

#define LIBBALSA_TYPE_SERVER (libbalsa_server_get_type())

G_DECLARE_DERIVABLE_TYPE(LibBalsaServer,
                         libbalsa_server,
                         LIBBALSA,
                         SERVER,
                         GObject)

typedef enum {
    LIBBALSA_TLS_DISABLED,
    LIBBALSA_TLS_ENABLED,
    LIBBALSA_TLS_REQUIRED
} LibBalsaTlsMode;

struct _LibBalsaServerClass {
    GObjectClass parent_class;

    void (*set_username) (LibBalsaServer * server, const gchar * name);
    void (*set_host) (LibBalsaServer * server,
		      const gchar * host, gboolean use_ssl);
    void (*config_changed) (LibBalsaServer * server);
    gchar *(*get_password) (LibBalsaServer * server);
};

LibBalsaServer *libbalsa_server_new(void);

void libbalsa_server_set_username(LibBalsaServer * server,
				  const gchar * username);
void libbalsa_server_set_password(LibBalsaServer * server,
				  const gchar * passwd);
void libbalsa_server_set_host(LibBalsaServer * server, const gchar * host,
                              gboolean use_ssl);
gchar *libbalsa_server_get_password(LibBalsaServer * server,
				    LibBalsaMailbox * mbox);

void libbalsa_server_config_changed(LibBalsaServer * server);
void libbalsa_server_load_config(LibBalsaServer * server);
void libbalsa_server_save_config(LibBalsaServer * server);


void libbalsa_server_user_cb(ImapUserEventType ue, void *arg, ...);

/* NetClient related signal handlers */
gchar **libbalsa_server_get_auth(NetClient *client,
								 gboolean   need_passwd,
								 gpointer   user_data);
gboolean libbalsa_server_check_cert(NetClient           *client,
           	   	   	   	   	   	    GTlsCertificate     *peer_cert,
									GTlsCertificateFlags errors,
									gpointer             user_data);
gchar *libbalsa_server_get_cert_pass(NetClient        *client,
									 const GByteArray *cert_der,
									 gpointer          user_data);

void libbalsa_server_connect_get_password(LibBalsaServer * server, GCallback cb,
                                          gpointer cb_data);

/* Check whether a server can be reached */

void libbalsa_server_test_can_reach(LibBalsaServer           * server,
                                    LibBalsaCanReachCallback * cb,
                                    gpointer                   cb_data);

/* Private: used only by LibBalsaMailboxRemote */
void libbalsa_server_test_can_reach_full(LibBalsaServer           * server,
                                         LibBalsaCanReachCallback * cb,
                                         gpointer                   cb_data,
                                         GObject                  * source_object);

/*
 * Getters
 */

LibBalsaTlsMode libbalsa_server_get_tls_mode(LibBalsaServer * server);
NetClientCryptMode libbalsa_server_get_security(LibBalsaServer * server);
gboolean libbalsa_server_get_use_ssl(LibBalsaServer * server);
gboolean libbalsa_server_get_client_cert(LibBalsaServer * server);
gboolean libbalsa_server_get_try_anonymous(LibBalsaServer * server);
gboolean libbalsa_server_get_remember_passwd(LibBalsaServer * server);
const gchar * libbalsa_server_get_user(LibBalsaServer * server);
const gchar * libbalsa_server_get_host(LibBalsaServer * server);
const gchar * libbalsa_server_get_protocol(LibBalsaServer * server);
const gchar * libbalsa_server_get_cert_file(LibBalsaServer * server);
const gchar * libbalsa_server_get_cert_passphrase(LibBalsaServer * server);
const gchar * libbalsa_server_get_passwd(LibBalsaServer * server);

/*
 * Setters
 */

void libbalsa_server_set_tls_mode(LibBalsaServer * server, LibBalsaTlsMode tls_mode);
void libbalsa_server_set_security(LibBalsaServer * server, NetClientCryptMode security);
void libbalsa_server_set_try_anonymous(LibBalsaServer * server, gboolean try_anonymous);
void libbalsa_server_set_remember_passwd(LibBalsaServer * server, gboolean remember_passwd);
void libbalsa_server_set_client_cert(LibBalsaServer * server, gboolean client_cert);
void libbalsa_server_set_protocol(LibBalsaServer * server, const gchar * protocol);
void libbalsa_server_set_cert_file(LibBalsaServer * server, const gchar * cert_file);
void libbalsa_server_set_cert_passphrase(LibBalsaServer * server, const gchar * cert_passphrase);

#endif				/* __LIBBALSA_SERVER_H__ */
