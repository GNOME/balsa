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

#ifndef __LIBBALSA_MAILBOX_MAILDIR_H__
#define __LIBBALSA_MAILBOX_MAILDIR_H__

#define LIBBALSA_TYPE_MAILBOX_MAILDIR \
    (libbalsa_mailbox_maildir_get_type())
#define LIBBALSA_MAILBOX_MAILDIR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIBBALSA_TYPE_MAILBOX_MAILDIR, \
                                 LibBalsaMailboxMaildir))
#define LIBBALSA_MAILBOX_MAILDIR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX_MAILDIR, \
                              LibBalsaMailboxMaildirClass))
#define LIBBALSA_IS_MAILBOX_MAILDIR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIBBALSA_TYPE_MAILBOX_MAILDIR))
#define LIBBALSA_IS_MAILBOX_MAILDIR_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX_MAILDIR))

typedef struct _LibBalsaMailboxMaildir LibBalsaMailboxMaildir;
typedef struct _LibBalsaMailboxMaildirClass LibBalsaMailboxMaildirClass;

struct _LibBalsaMailboxMaildir {
    LibBalsaMailboxLocal parent;

    GHashTable* messages_info;
    GPtrArray* msgno_2_msg_info;
    time_t mtime;
    time_t mtime_cur;
    time_t mtime_new;
    gchar *curdir;
    gchar *newdir;
    gchar *tmpdir;
};

struct _LibBalsaMailboxMaildirClass {
    LibBalsaMailboxLocalClass klass;
};

GType libbalsa_mailbox_maildir_get_type(void);
GObject *libbalsa_mailbox_maildir_new(const gchar * path, gboolean create);
gint libbalsa_mailbox_maildir_create(const gchar * path, gboolean create,
                                     LibBalsaMailboxMaildir * mdir);

#endif

