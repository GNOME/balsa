/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
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

#ifndef __LIBBALSA_MAILBOX_LOCAL_H__
#define __LIBBALSA_MAILBOX_LOCAL_H__

#define LIBBALSA_TYPE_MAILBOX_LOCAL	       (libbalsa_mailbox_local_get_type())
#define LIBBALSA_MAILBOX_LOCAL(obj)	       (GTK_CHECK_CAST (obj, LIBBALSA_TYPE_MAILBOX_LOCAL, LibBalsaMailboxLocal))
#define LIBBALSA_MAILBOX_LOCAL_CLASS(klass)    (GTK_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_MAILBOX_LOCAL, LibBalsaMailboxLocalClass))
#define LIBBALSA_IS_MAILBOX_LOCAL(obj)	       (GTK_CHECK_TYPE (obj, LIBBALSA_TYPE_MAILBOX_LOCAL))
#define LIBBALSA_IS_MAILBOX_LOCAL_CLASS(klass) (GTK_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_MAILBOX_LOCAL))

GtkType libbalsa_mailbox_local_get_type (void);

typedef enum _LibBalsaMailboxLocalType LibBalsaMailboxLocalType;
typedef struct _LibBalsaMailboxLocal LibBalsaMailboxLocal;
typedef struct _LibBalsaMailboxLocalClass LibBalsaMailboxLocalClass;

enum _LibBalsaMailboxLocalType 
{
	LIBBALSA_MAILBOX_LOCAL_MH,
	LIBBALSA_MAILBOX_LOCAL_MBOX,
	LIBBALSA_MAILBOX_LOCAL_MAILDIR
};

struct _LibBalsaMailboxLocal
{
	LibBalsaMailbox mailbox;
	LibBalsaMailboxLocalType type;

	gchar *path;
};

struct _LibBalsaMailboxLocalClass
{
	LibBalsaMailboxClass klass;
};

GtkObject* libbalsa_mailbox_local_new(const gchar *path, gboolean create);

#endif /* __LIBBALSA_MAILBOX_LOCAL_H__ */
