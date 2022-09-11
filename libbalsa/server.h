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

#ifndef __LIBBALSA_SERVER_H__
#define __LIBBALSA_SERVER_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "imap/libimap.h"
#include "libbalsa.h"
#include "net-client.h"

#define LIBBALSA_TYPE_SERVER (libbalsa_server_get_type())

G_DECLARE_DERIVABLE_TYPE(LibBalsaServer,
                         libbalsa_server,
                         LIBBALSA,
                         SERVER,
                         GObject)

struct _LibBalsaServerClass {
    GObjectClass parent_class;

    void (*set_username) (LibBalsaServer * server, const gchar * name);
    void (*set_host) (LibBalsaServer * server,
		      const gchar * host, NetClientCryptMode  security);
    void (*config_changed) (LibBalsaServer * server);
};

LibBalsaServer *libbalsa_server_new(void);

void libbalsa_server_set_username(LibBalsaServer * server,
				  const gchar * username);
void libbalsa_server_set_password(LibBalsaServer *server,
				  	  	  	  	  const gchar    *passwd,
								  gboolean        for_cert);
void libbalsa_server_set_host(LibBalsaServer     *server,
							  const gchar        *host,
							  NetClientCryptMode  security);

void libbalsa_server_config_changed(LibBalsaServer * server);
void libbalsa_server_load_config(LibBalsaServer * server);
void libbalsa_server_load_security_config(LibBalsaServer * server);
void libbalsa_server_save_config(LibBalsaServer * server);


/* NetClient related signal handlers */
gchar **libbalsa_server_get_auth(NetClient         *client,
								 NetClientAuthMode  mode,
								 gpointer           user_data);
gboolean libbalsa_server_check_cert(NetClient           *client,
           	   	   	   	   	   	    GTlsCertificate     *peer_cert,
									GTlsCertificateFlags errors,
									gpointer             user_data);
gchar *libbalsa_server_get_cert_pass(NetClient *client,
									 gchar     *cert_subject,
									 gpointer   user_data);

void libbalsa_server_connect_signals(LibBalsaServer * server, GCallback cb,
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

/* Getters */
const gchar * libbalsa_server_get_host(LibBalsaServer *server);
const gchar * libbalsa_server_get_user(LibBalsaServer *server);
const gchar * libbalsa_server_get_cert_file(LibBalsaServer *server);
const gchar * libbalsa_server_get_protocol(LibBalsaServer *server);
const gchar * libbalsa_server_get_password(LibBalsaServer *server);
const gchar * libbalsa_server_get_cert_passphrase(LibBalsaServer *server);
NetClientCryptMode libbalsa_server_get_security(LibBalsaServer *server);
NetClientAuthMode libbalsa_server_get_auth_mode(LibBalsaServer *server);
gboolean libbalsa_server_get_client_cert(LibBalsaServer *server);
gboolean libbalsa_server_get_remember_password(LibBalsaServer *server);
gboolean libbalsa_server_get_remember_cert_passphrase(LibBalsaServer *server);

/* Setters */
void libbalsa_server_set_protocol(LibBalsaServer *server, const gchar *protocol);
void libbalsa_server_set_cert_file(LibBalsaServer *server, const gchar *cert_file);
void libbalsa_server_set_security(LibBalsaServer *server, NetClientCryptMode security);
void libbalsa_server_set_auth_mode(LibBalsaServer *server, NetClientAuthMode auth_mode);
void libbalsa_server_set_remember_password(LibBalsaServer *server, gboolean remember_password);
void libbalsa_server_set_client_cert(LibBalsaServer *server, gboolean client_cert);
void libbalsa_server_set_remember_cert_passphrase(LibBalsaServer *server,
                                                  gboolean remember_cert_passphrase);

#endif				/* __LIBBALSA_SERVER_H__ */
