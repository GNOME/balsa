/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#ifndef __LIBBALSA_SERVER_H__
#define __LIBBALSA_SERVER_H__

#include "imap/libimap.h"

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

    gchar *host;
    gchar *user;
    gchar *passwd;
    /* We include SSL support in UI unconditionally to preserve config
     * between SSL and non-SSL builds. We just fail if SSL is requested
     * in non-SSL build. */
    LibBalsaTlsMode tls_mode;
    unsigned use_ssl:1;
    unsigned remember_passwd:1;
};

struct _LibBalsaServerClass {
    GObjectClass parent_class;

    void (*set_username) (LibBalsaServer * server, const gchar * name);
    void (*set_password) (LibBalsaServer * server, const gchar * passwd);
    void (*set_host) (LibBalsaServer * server,
		      const gchar * host, gboolean use_ssl);
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

void libbalsa_server_load_config(LibBalsaServer * server);
void libbalsa_server_save_config(LibBalsaServer * server);


void libbalsa_server_user_cb(ImapUserEventType ue, void *arg, ...);

void libbalsa_server_connect_signals(LibBalsaServer * server, GCallback cb,
                                     gpointer cb_data);
#endif				/* __LIBBALSA_SERVER_H__ */
