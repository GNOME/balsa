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

#include <gnome.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "libbalsa.h"
#include "mailbackend.h"

static LibBalsaMailboxLocalClass *parent_class = NULL;

static void libbalsa_mailbox_mbox_class_init(LibBalsaMailboxMboxClass *klass);
static void libbalsa_mailbox_mbox_init(LibBalsaMailboxMbox * mailbox);
static void libbalsa_mailbox_mbox_finalize(GObject * object);

static FILE *libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *mailbox,
						       LibBalsaMessage *message);
static void libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox);

GType libbalsa_mailbox_mbox_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxMboxClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_mbox_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxMbox),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_mbox_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX_LOCAL,
	                           "LibBalsaMailboxMbox",
                                   &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_mbox_class_init(LibBalsaMailboxMboxClass * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_mailbox_mbox_finalize;

    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_mbox_get_message_stream;

    libbalsa_mailbox_local_class->remove_files = 
	libbalsa_mailbox_mbox_remove_files;
}

static void
libbalsa_mailbox_mbox_init(LibBalsaMailboxMbox * mailbox)
{
}

gint
libbalsa_mailbox_mbox_create(const gchar * path, gboolean create)
{
    gint exists; 
    GType magic_type;
    gint fd;
    
    g_return_val_if_fail( path != NULL, -1);

    exists = access(path, F_OK);
    if ( exists == 0 ) {
	/* File exists. Check if it is an mbox... */
	
	magic_type = libbalsa_mailbox_type_from_path(path);
	if ( magic_type != LIBBALSA_TYPE_MAILBOX_MBOX ) {
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING, 
				 _("Mailbox %s does not appear to be an Mbox mailbox."), path);
	    return(-1);
	}
    } else {
	if(!create)
	    return(-1);

	if ((fd = creat(path, S_IRUSR | S_IWUSR)) == -1) {
	    g_warning("An error:\n%s\n occured while trying to "
		      "create the mailbox \"%s\"\n",
		      strerror(errno), path);
	    return -1;
	} else {
	    close(fd);
	}
    }
    return(0);
}

GObject *
libbalsa_mailbox_mbox_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_MBOX, NULL);
    mailbox->is_directory = FALSE;
	
    mailbox->url = g_strconcat("file://", path, NULL);
    if( libbalsa_mailbox_mbox_create(path, create) < 0 ) {
	g_object_unref(G_OBJECT(mailbox));
	return NULL;
    }
    
    libbalsa_notify_register_mailbox(mailbox);
    
    return G_OBJECT(mailbox);
}

static void
libbalsa_mailbox_mbox_finalize(GObject * object)
{
    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static FILE *
libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *mailbox, LibBalsaMessage *message)
{
	FILE *stream = NULL;

	g_return_val_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox), NULL);
	g_return_val_if_fail (LIBBALSA_IS_MESSAGE(message), NULL);

	stream = fopen(libbalsa_mailbox_local_get_path(mailbox), "r");

	return stream;
}

static void
libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox)
{
    g_return_if_fail (LIBBALSA_IS_MAILBOX_MBOX(mailbox));

    if ( unlink(libbalsa_mailbox_local_get_path(mailbox)) == -1 )
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, 
			     _("Could not remove %s:\n%s"), 
			     libbalsa_mailbox_local_get_path(mailbox), 
			     strerror(errno));
}
