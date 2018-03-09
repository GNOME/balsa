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

#include "libbalsa.h"
#include "server.h"

static void libbalsa_mailbox_remote_class_init(LibBalsaMailboxRemoteClass *
					       klass);
static void libbalsa_mailbox_remote_init(LibBalsaMailboxRemote * mailbox);
static void libbalsa_mailbox_remote_test_can_reach(LibBalsaMailbox          * mailbox,
                                                   LibBalsaCanReachCallback * cb,
                                                   gpointer                   cb_data);

GType
libbalsa_mailbox_remote_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_remote_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailbox),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_remote_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX,
	                           "LibBalsaMailboxRemote",
                                   &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_remote_class_init(LibBalsaMailboxRemoteClass * klass)
{
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    libbalsa_mailbox_class->test_can_reach =
        libbalsa_mailbox_remote_test_can_reach;
}

static void
libbalsa_mailbox_remote_init(LibBalsaMailboxRemote * mailbox)
{
    mailbox->server = NULL;
}

/* Test whether a mailbox is reachable */

static void
libbalsa_mailbox_remote_test_can_reach(LibBalsaMailbox          * mailbox,
                                       LibBalsaCanReachCallback * cb,
                                       gpointer                   cb_data)
{
    libbalsa_server_test_can_reach_full(LIBBALSA_MAILBOX_REMOTE(mailbox)->server,
                                        cb, cb_data, (GObject *) mailbox);
}

/* Public method */

void 
libbalsa_mailbox_remote_set_server(LibBalsaMailboxRemote *m, LibBalsaServer *s)
{
    g_set_object(&m->server, s);
}
