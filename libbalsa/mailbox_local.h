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

struct _LibBalsaMailboxLocalPool {
    LibBalsaMessage * message;
    guint pool_seqno;
};
typedef struct _LibBalsaMailboxLocalPool LibBalsaMailboxLocalPool;
#define LBML_POOL_SIZE 32

struct _LibBalsaMailboxLocalMessageInfo {
    LibBalsaMessageFlag flags;          /* May have pseudo-flags */
    LibBalsaMessage *message;
};
typedef struct _LibBalsaMailboxLocalMessageInfo LibBalsaMailboxLocalMessageInfo;

struct _LibBalsaMailboxLocal {
    LibBalsaMailbox mailbox;

    guint sync_id;  /* id of the idle mailbox sync job  */
    guint sync_time; /* estimated time of sync job execution (in seconds),
                      * used to  throttle frequency of large mbox syncing. */
    guint sync_cnt; /* we do not want to rely on the time of last sync since
                     * some sync can be faster than others. Instead, we
                     * average the syncing time for mailbox. */
    guint thread_id;    /* id of the idle mailbox thread job */
    guint save_tree_id; /* id of the idle mailbox save-tree job */
    GPtrArray *threading_info;
    LibBalsaMailboxLocalPool message_pool[LBML_POOL_SIZE];
    guint pool_seqno;
};

struct _LibBalsaMailboxLocalClass {
    LibBalsaMailboxClass klass;

    gint (*check_files)(const gchar * path, gboolean create);
    void (*set_path)(LibBalsaMailboxLocal * local, const gchar * path);
    void (*remove_files)(LibBalsaMailboxLocal * local);
    guint (*fileno)(LibBalsaMailboxLocal * local, guint msgno);
    LibBalsaMailboxLocalMessageInfo *(*get_info)(LibBalsaMailboxLocal * local,
                                                 guint msgno);
};

GObject *libbalsa_mailbox_local_new(const gchar * path, gboolean create);
gint libbalsa_mailbox_local_set_path(LibBalsaMailboxLocal * mailbox,
				     const gchar * path, gboolean create);
void libbalsa_mailbox_local_set_threading_info(LibBalsaMailboxLocal *
                                               local);

#define libbalsa_mailbox_local_get_path(mbox) \
	((const gchar *) (LIBBALSA_MAILBOX(mbox))->url+7)

void libbalsa_mailbox_local_load_messages(LibBalsaMailbox * mailbox,
					  guint last_msgno);
void libbalsa_mailbox_local_cache_message(LibBalsaMailboxLocal * local,
                                          guint msgno,
                                          LibBalsaMessage * message);
void libbalsa_mailbox_local_msgno_removed(LibBalsaMailbox * mailbox,
					  guint msgno);
void libbalsa_mailbox_local_remove_files(LibBalsaMailboxLocal *mailbox);

/* Helpers for maildir and mh. */
GMimeMessage *libbalsa_mailbox_local_get_mime_message(LibBalsaMailbox *
						      mailbox,
						      const gchar * name1,
						      const gchar * name2);
GMimeStream *libbalsa_mailbox_local_get_message_stream(LibBalsaMailbox *
						       mailbox,
						       const gchar * name1,
						       const gchar * name2);

#endif				/* __LIBBALSA_MAILBOX_LOCAL_H__ */
