/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbox-filter.h"

#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 

enum {
    REMOVE_FILES,
    LAST_SIGNAL
};
static guint libbalsa_mailbox_local_signals[LAST_SIGNAL];

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_local_class_init(LibBalsaMailboxLocalClass *klass);
static void libbalsa_mailbox_local_init(LibBalsaMailboxLocal * mailbox);
static void libbalsa_mailbox_local_finalize(GObject * object);

static void libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);
static void libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
					       const gchar * prefix);

GType
libbalsa_mailbox_local_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxLocalClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_local_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxLocal),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_local_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX,
	                           "LibBalsaMailboxLocal",
                                   &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_local_class_init(LibBalsaMailboxLocalClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    libbalsa_mailbox_local_signals[REMOVE_FILES] =
	g_signal_new("remove-files",
                     G_TYPE_FROM_CLASS(object_class),
		     G_SIGNAL_RUN_LAST,
		     G_STRUCT_OFFSET(LibBalsaMailboxLocalClass,
				     remove_files),
                     NULL, NULL,
		     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    object_class->finalize = libbalsa_mailbox_local_finalize;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_local_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_local_load_config;

    klass->remove_files = NULL;
}

static void
libbalsa_mailbox_local_init(LibBalsaMailboxLocal * mailbox)
{
}

GObject *
libbalsa_mailbox_local_new(const gchar * path, gboolean create)
{
    GType magic_type = libbalsa_mailbox_type_from_path(path);

    if(magic_type == LIBBALSA_TYPE_MAILBOX_MBOX)
	return libbalsa_mailbox_mbox_new(path, create);
    else if(magic_type == LIBBALSA_TYPE_MAILBOX_MH)
	return libbalsa_mailbox_mh_new(path, create);
    else if(magic_type == LIBBALSA_TYPE_MAILBOX_MAILDIR)
	return libbalsa_mailbox_maildir_new(path, create);
    else if(magic_type == LIBBALSA_TYPE_MAILBOX_IMAP) {
        g_warning("IMAP path given as a path to local mailbox.\n");
        return NULL;
    } else {		/* mailbox non-existent or unreadable */
	if(create) 
	    return libbalsa_mailbox_mbox_new(path, TRUE);
        else {
            g_warning("Unknown mailbox type\n");
            return NULL;
        }
    }
}

/* libbalsa_mailbox_local_set_path:
   returrns errno on error, 0 on success
   FIXME: proper suport for maildir and mh
*/
gint
libbalsa_mailbox_local_set_path(LibBalsaMailboxLocal * mailbox,
				const gchar * path)
{
    int i = 0;

    g_return_val_if_fail(mailbox, -1);
    g_return_val_if_fail(path, -1);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox), -1);

    if ( LIBBALSA_MAILBOX(mailbox)->url != NULL ) {
	const gchar* cur_path = libbalsa_mailbox_local_get_path(mailbox);
	if (g_ascii_strcasecmp(path, cur_path) == 0)
	    return 0;
	else 
	    i = rename(cur_path, path);
    } else {
	if(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox))
	    i = libbalsa_mailbox_maildir_create(path, TRUE);
	else if(LIBBALSA_IS_MAILBOX_MH(mailbox))
	    i = libbalsa_mailbox_mh_create(path, TRUE);
	else if(LIBBALSA_IS_MAILBOX_MBOX(mailbox))
	    i = libbalsa_mailbox_mbox_create(path, TRUE);	    
    }

    /* update mailbox data */
    if(!i) {
	libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));
	g_free(LIBBALSA_MAILBOX(mailbox)->url);
	LIBBALSA_MAILBOX(mailbox)->url = g_strconcat("file://", path, NULL);
	libbalsa_notify_register_mailbox(LIBBALSA_MAILBOX(mailbox));
	return 0;
    } else
	return errno ? errno : -1;
}

void
libbalsa_mailbox_local_remove_files(LibBalsaMailboxLocal *mailbox)
{
    g_return_if_fail (LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_local_signals[REMOVE_FILES], 0);

}

static void
libbalsa_mailbox_local_finalize(GObject * object)
{
    LibBalsaMailbox *mailbox;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(object));

    mailbox = LIBBALSA_MAILBOX(object);
    libbalsa_notify_unregister_mailbox(mailbox);

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
libbalsa_mailbox_local_save_config(LibBalsaMailbox * mailbox,
				   const gchar * prefix)
{
    LibBalsaMailboxLocal *local;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    gnome_config_set_string("Path", libbalsa_mailbox_local_get_path(local));

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_local_load_config(LibBalsaMailbox * mailbox,
				   const gchar * prefix)
{
    LibBalsaMailboxLocal *local;
    gchar* path;
    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(mailbox));

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    g_free(mailbox->url);

    path = gnome_config_get_string("Path");
    mailbox->url = g_strconcat("file://", path, NULL);
    g_free(path);

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);

    libbalsa_notify_register_mailbox(mailbox);
}

