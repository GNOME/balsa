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
#elif defined (HAVE_GNOME_KEYRING)
#include <gnome-keyring.h>

#if defined(HAVE_GNOME_KEYRING_24)
#define LIBBALSA_SERVER_KEYRING_SCHEMA GNOME_KEYRING_NETWORK_PASSWORD
#else
extern const GnomeKeyringPasswordSchema* LIBBALSA_SERVER_KEYRING_SCHEMA;
#endif /* HAVE_GNOME_KEYRING_24 */

#define libbalsa_free_password gnome_keyring_free_password
#else
#define libbalsa_free_password g_free
#endif                          /* defined(HAVE_LIBSECRET) */

#define LIBBALSA_TYPE_SERVER \
    (libbalsa_server_get_type())
#define LIBBALSA_SERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST (obj, LIBBALSA_TYPE_SERVER, LibBalsaServer))
#define LIBBALSA_SERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_SERVER, \
                              LibBalsaServerClass))
#define LIBBALSA_IS_SERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE (obj, LIBBALSA_TYPE_SERVER))
#define LIBBALSA_IS_SERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_SERVER))

GType libbalsa_server_get_type(void);

typedef struct _LibBalsaServerClass LibBalsaServerClass;

typedef enum {
    LIBBALSA_TLS_DISABLED,
    LIBBALSA_TLS_ENABLED,
    LIBBALSA_TLS_REQUIRED
} LibBalsaTlsMode;

struct _LibBalsaServer {
    GObject object;
    const gchar *protocol; /**< type of the server: imap, pop3, or smtp. */

    gchar *host;
    gchar *user;
    gchar *passwd;
    NetClientCryptMode security;
    /* We include SSL support in UI unconditionally to preserve config
     * between SSL and non-SSL builds. We just fail if SSL is requested
     * in non-SSL build. */
    LibBalsaTlsMode tls_mode;
    unsigned use_ssl:1;
    unsigned remember_passwd:1;
    unsigned try_anonymous:1; /* user wants anonymous access */
};

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

void libbalsa_server_connect_signals(LibBalsaServer * server, GCallback cb,
                                     gpointer cb_data);

#endif				/* __LIBBALSA_SERVER_H__ */
