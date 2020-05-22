/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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

#include "libbalsa.h"
#include "server.h"

static void libbalsa_mailbox_remote_dispose(GObject * object);
static void libbalsa_mailbox_remote_test_can_reach(LibBalsaMailbox          * mailbox,
                                                   LibBalsaCanReachCallback * cb,
                                                   gpointer                   cb_data);

typedef struct _LibBalsaMailboxRemotePrivate LibBalsaMailboxRemotePrivate;
struct _LibBalsaMailboxRemotePrivate {
    LibBalsaServer *server;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(LibBalsaMailboxRemote,
                                    libbalsa_mailbox_remote,
                                    LIBBALSA_TYPE_MAILBOX)

static void
libbalsa_mailbox_remote_class_init(LibBalsaMailboxRemoteClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    object_class->dispose = libbalsa_mailbox_remote_dispose;

    libbalsa_mailbox_class->test_can_reach =
        libbalsa_mailbox_remote_test_can_reach;
}

static void
libbalsa_mailbox_remote_init(LibBalsaMailboxRemote * remote)
{
    LibBalsaMailboxRemotePrivate *priv =
        libbalsa_mailbox_remote_get_instance_private(remote);

    priv->server = NULL;
}

static void
libbalsa_mailbox_remote_dispose(GObject * object)
{
    LibBalsaMailboxRemote *remote = (LibBalsaMailboxRemote *) object;
    LibBalsaMailboxRemotePrivate *priv =
        libbalsa_mailbox_remote_get_instance_private(remote);

    /* This will close the mailbox, if it is still open: */
    G_OBJECT_CLASS(libbalsa_mailbox_remote_parent_class)->dispose(object);

    /* Now it is safe to unref the server: */
    g_clear_object(&priv->server);
}

/* Test whether a mailbox is reachable */

static void
libbalsa_mailbox_remote_test_can_reach(LibBalsaMailbox          * mailbox,
                                       LibBalsaCanReachCallback * cb,
                                       gpointer                   cb_data)
{
    LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    LibBalsaMailboxRemotePrivate *priv =
        libbalsa_mailbox_remote_get_instance_private(remote);

    libbalsa_server_test_can_reach_full(priv->server,
                                        cb, cb_data, (GObject *) mailbox);
}

/* Public methods */

void
libbalsa_mailbox_remote_set_server(LibBalsaMailboxRemote *remote, LibBalsaServer *server)
{
    LibBalsaMailboxRemotePrivate *priv =
        libbalsa_mailbox_remote_get_instance_private(remote);

    g_return_if_fail(LIBBALSA_IS_MAILBOX_REMOTE(remote));
    g_return_if_fail(server == NULL || LIBBALSA_IS_SERVER(server));

    g_set_object(&priv->server, server);
}

LibBalsaServer *
libbalsa_mailbox_remote_get_server(LibBalsaMailboxRemote *remote)
{
    LibBalsaMailboxRemotePrivate *priv =
        libbalsa_mailbox_remote_get_instance_private(remote);

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_REMOTE(remote), NULL);

    return priv->server;
}
