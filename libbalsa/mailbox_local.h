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

#ifndef __LIBBALSA_MAILBOX_LOCAL_H__
#define __LIBBALSA_MAILBOX_LOCAL_H__

#define LIBBALSA_TYPE_MAILBOX_LOCAL \
    (libbalsa_mailbox_local_get_type())
#define LIBBALSA_MAILBOX_LOCAL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIBBALSA_TYPE_MAILBOX_LOCAL, \
                                 LibBalsaMailboxLocal))
#define LIBBALSA_MAILBOX_LOCAL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX_LOCAL, \
                              LibBalsaMailboxLocalClass))
#define LIBBALSA_IS_MAILBOX_LOCAL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIBBALSA_TYPE_MAILBOX_LOCAL))
#define LIBBALSA_IS_MAILBOX_LOCAL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX_LOCAL))
#define LIBBALSA_MAILBOX_LOCAL_GET_CLASS(mailbox) \
    (G_TYPE_INSTANCE_GET_CLASS ((mailbox), LIBBALSA_TYPE_MAILBOX_LOCAL, \
				LibBalsaMailboxLocalClass))

GType libbalsa_mailbox_local_get_type(void);

typedef struct _LibBalsaMailboxLocal LibBalsaMailboxLocal;
typedef struct _LibBalsaMailboxLocalClass LibBalsaMailboxLocalClass;

struct _LibBalsaMailboxLocal {
    LibBalsaMailbox mailbox;

    guint sync_id;  /* id of the idle mailbox sync job  */
    guint sync_time; /* estimated time of sync job execution (in seconds),
                      * used to  throttle frequency of large mbox syncing. */
    guint sync_cnt; /* we do not want to rely on the time of last sync since
                     * some sync can be faster than others. Instead, we
                     * average the syncing time for mailbox. */
};

struct _LibBalsaMailboxLocalClass {
    LibBalsaMailboxClass klass;

    LibBalsaMessage *(*load_message)(LibBalsaMailbox *mb, guint msgno);
    void (*remove_files)(LibBalsaMailboxLocal *mb);
};

GObject *libbalsa_mailbox_local_new(const gchar * path, gboolean create);
gint libbalsa_mailbox_local_set_path(LibBalsaMailboxLocal * mailbox,
				     const gchar * path);

#define libbalsa_mailbox_local_get_path(mbox) \
	((const gchar *) (LIBBALSA_MAILBOX(mbox))->url+7)

void libbalsa_mailbox_local_load_messages(LibBalsaMailbox * mailbox,
					  guint last_msgno);
LibBalsaMessage *libbalsa_mailbox_local_load_message(LibBalsaMailbox * mailbox,
                                                     guint msgno);
void libbalsa_mailbox_local_remove_files(LibBalsaMailboxLocal *mailbox);

/* Helpers for maildir and mh. */
GMimeMessage *_libbalsa_mailbox_local_get_mime_message(LibBalsaMailbox *
						       mailbox,
						       const gchar * name1,
						       const gchar * name2);
GMimeStream *_libbalsa_mailbox_local_get_message_stream(LibBalsaMailbox *
							mailbox,
							const gchar * name1,
							const gchar * name2);

/* Queued sync. */
void libbalsa_mailbox_local_queue_sync(LibBalsaMailboxLocal * local);

#endif				/* __LIBBALSA_MAILBOX_LOCAL_H__ */
