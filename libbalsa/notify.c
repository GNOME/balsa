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

#include "config.h"

#include <glib.h>

#include "libbalsa.h"

#if NOT_USED
/* For Buffy structure */
#include "mailbackend.h"
#include "buffy.h"
#endif  

/*
 * FIXME
 * chbm: we gotta replace the buffy infrastrucure ! 
 */

/* Holds all the mailboxes which are registered for checking */
/* static GHashTable *notify_hash; */

void
libbalsa_notify_init(void)
{
#if NOT_USED
    /* Hash table uses the actual key as the hash */
    notify_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
#endif
}

void
libbalsa_notify_register_mailbox(LibBalsaMailbox * mailbox)
{
#if NOT_USED
    BUFFY *tmp;
    const gchar *path = NULL;
    gchar *user, *passwd;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
	path = libbalsa_mailbox_local_get_path(mailbox);
	user = passwd = NULL;
    } else if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
	if (server->user && server->passwd) {
	    user = server->user;
	    passwd = server->passwd;
	    path = mailbox->url;
	} else {
	    return;
	}
    } else {
	return;
    }

    libbalsa_lock_mutt();
    tmp = buffy_add_mailbox(path, user, passwd);
    libbalsa_unlock_mutt();
    g_hash_table_insert(notify_hash, mailbox, tmp);
#endif 
}

void
libbalsa_notify_unregister_mailbox(LibBalsaMailbox * mailbox)
{
#if NOT_USED
    BUFFY *bf;
    BUFFY **tmp = &Incoming;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    bf = (BUFFY *) g_hash_table_lookup(notify_hash, mailbox);

    if (bf == NULL)
	return;

    g_hash_table_remove(notify_hash, mailbox);

    /* For some reason buffy_mailbox_remove is not exported by libmutt.
     * So we do it ourselves. Cut-n-paste from buffy.c
     */
    libbalsa_lock_mutt();
    if (!*tmp) {
	libbalsa_unlock_mutt();
	return;			/* strange error */
    }

    if (*tmp == bf) {

	*tmp = (*tmp)->next;

    } else {

	while (*tmp && (*tmp)->next != bf)
	    tmp = &(*tmp)->next;

	if (!*tmp) {
	    libbalsa_unlock_mutt();
	    return;		/* not found again, critical error! */
	}

	(*tmp)->next = bf->next;
    }

    mutt_buffy_free(&bf);

    libbalsa_unlock_mutt();
#endif
}

void
libbalsa_notify_start_check(gboolean imap_check_test(const gchar *path))
{
#if NOT_USED
    /* Might as well use check rather than notify. All notify does is */
    /* write messages for each mailbox */
    libbalsa_lock_mutt();
    mutt_buffy_check(FALSE, imap_check_test);
    libbalsa_unlock_mutt();
#endif
}

gint
libbalsa_notify_check_mailbox(LibBalsaMailbox * mailbox)
{
#if NOT_USED
    BUFFY *bf;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    bf = g_hash_table_lookup(notify_hash, mailbox);

    if (bf == NULL) {
	g_warning
	    ("Did libbalsa_notify_check_mailbox on mailbox '%s' "
             "that isn't registered.", mailbox->name);
	return 0;
    }

    return bf->new;
#endif
    return 0;
}
