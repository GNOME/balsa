/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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

#include "libbalsa.h"

static void libbalsa_mailbox_remote_class_init(LibBalsaMailboxRemoteClass *klass);
static void libbalsa_mailbox_remote_init(LibBalsaMailboxRemote *mailbox);

GtkType
libbalsa_mailbox_remote_get_type (void)
{
	static GtkType mailbox_type = 0;

	if (!mailbox_type) {
		static const GtkTypeInfo mailbox_info =	{
			"LibBalsaMailboxRemote",
			sizeof (LibBalsaMailbox),
			sizeof (LibBalsaMailboxClass),
			(GtkClassInitFunc) libbalsa_mailbox_remote_class_init,
			(GtkObjectInitFunc) libbalsa_mailbox_remote_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		mailbox_type = gtk_type_unique(libbalsa_mailbox_get_type(), &mailbox_info);
	}

	return mailbox_type;
}

static void
libbalsa_mailbox_remote_class_init(LibBalsaMailboxRemoteClass *klass)
{
}

static void
libbalsa_mailbox_remote_init(LibBalsaMailboxRemote *mailbox)
{
	mailbox->server = NULL;
}

