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
static void libbalsa_mailbox_mbox_destroy(GtkObject * object);

static FILE *libbalsa_mailbox_mbox_get_message_stream(LibBalsaMailbox *mailbox,
						       LibBalsaMessage *message);
static void libbalsa_mailbox_mbox_remove_files(LibBalsaMailboxLocal *mailbox);

GtkType libbalsa_mailbox_mbox_get_type(void)
{
    static GtkType mailbox_type = 0;

    if (!mailbox_type) {
	static const GtkTypeInfo mailbox_info = {
	    "LibBalsaMailboxMbox",
	    sizeof(LibBalsaMailboxMbox),
	    sizeof(LibBalsaMailboxMboxClass),
	    (GtkClassInitFunc) libbalsa_mailbox_mbox_class_init,
	    (GtkObjectInitFunc) libbalsa_mailbox_mbox_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	mailbox_type =
	    gtk_type_unique(libbalsa_mailbox_local_get_type(), &mailbox_info);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_mbox_class_init(LibBalsaMailboxMboxClass * klass)
{
    GtkObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;
    LibBalsaMailboxLocalClass *libbalsa_mailbox_local_class;

    object_class = GTK_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);
    libbalsa_mailbox_local_class = LIBBALSA_MAILBOX_LOCAL_CLASS(klass);

    parent_class = gtk_type_class(libbalsa_mailbox_local_get_type());

    object_class->destroy = libbalsa_mailbox_mbox_destroy;

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
    gint magic_type;
    gint fd;
    
    g_return_val_if_fail( path != NULL, -1);

    exists = access(path, F_OK);
    if ( exists == 0 ) {
	/* File exists. Check if it is an mbox... */
	
	libbalsa_lock_mutt();
	magic_type = mx_get_magic(path);
	libbalsa_unlock_mutt();
	
	if ( magic_type != M_MBOX ) {
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

GtkObject *
libbalsa_mailbox_mbox_new(const gchar * path, gboolean create)
{
    LibBalsaMailbox *mailbox;


    mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_MBOX);
    
    mailbox->is_directory = FALSE;
	
    mailbox->url = g_strconcat("file://", path, NULL);
	
    if( libbalsa_mailbox_mbox_create(path, create) < 0 ) {
	gtk_object_destroy(GTK_OBJECT(mailbox));
	return NULL;
    }
    
    libbalsa_notify_register_mailbox(mailbox);
    
    return GTK_OBJECT(mailbox);
}

static void
libbalsa_mailbox_mbox_destroy(GtkObject * object)
{

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
