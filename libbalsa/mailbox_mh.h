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

#ifndef __LIBBALSA_MAILBOX_MH_H__
#define __LIBBALSA_MAILBOX_MH_H__

#define LIBBALSA_TYPE_MAILBOX_MH \
    (libbalsa_mailbox_mh_get_type())
#define LIBBALSA_MAILBOX_MH(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIBBALSA_TYPE_MAILBOX_MH, \
                                 LibBalsaMailboxMh))
#define LIBBALSA_MAILBOX_MH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX_MH, \
                              LibBalsaMailboxMhClass))
#define LIBBALSA_IS_MAILBOX_MH(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIBBALSA_TYPE_MAILBOX_MH))
#define LIBBALSA_IS_MAILBOX_MH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX_MH))

typedef struct _LibBalsaMailboxMh LibBalsaMailboxMh;
typedef struct _LibBalsaMailboxMhClass LibBalsaMailboxMhClass;

struct _LibBalsaMailboxMh {
    LibBalsaMailboxLocal parent;

    GHashTable* messages_info;
    GPtrArray* msgno_2_msg_info;
    gchar* sequences_filename;
    time_t mtime_sequences;
    guint last_fileno;
};

struct _LibBalsaMailboxMhClass {
    LibBalsaMailboxLocalClass klass;
};

GType libbalsa_mailbox_mh_get_type(void);
GObject *libbalsa_mailbox_mh_new(const gchar * path, gboolean create);
gint libbalsa_mailbox_mh_create(const gchar * path, gboolean create,
                                LibBalsaMailboxMh * mh);
#endif

