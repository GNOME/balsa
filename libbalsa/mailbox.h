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

#ifndef __LIBBALSA_MAILBOX_H__
#define __LIBBALSA_MAILBOX_H__

#include <gdk/gdk.h>

#include "libbalsa.h"

#include <gmime/gmime.h>

#define LIBBALSA_TYPE_MAILBOX \
    (libbalsa_mailbox_get_type())
#define LIBBALSA_MAILBOX(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), LIBBALSA_TYPE_MAILBOX, LibBalsaMailbox))
#define LIBBALSA_MAILBOX_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), LIBBALSA_TYPE_MAILBOX, \
                              LibBalsaMailboxClass))
#define LIBBALSA_IS_MAILBOX(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LIBBALSA_TYPE_MAILBOX))
#define LIBBALSA_IS_MAILBOX_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), LIBBALSA_TYPE_MAILBOX))
#define LIBBALSA_MAILBOX_GET_CLASS(mailbox) \
    (G_TYPE_INSTANCE_GET_CLASS ((mailbox), LIBBALSA_TYPE_MAILBOX, \
				LibBalsaMailboxClass))

#define MAILBOX_OPEN(mailbox)     libbalsa_mailbox_is_open(mailbox)

#define MAILBOX_CLOSED(mailbox)   (!libbalsa_mailbox_is_open(mailbox))

#define RETURN_IF_MAILBOX_CLOSED(mailbox)\
do {\
  if (MAILBOX_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      UNLOCK_MAILBOX (mailbox);\
      return;\
    }\
} while (0)
#define RETURN_VAL_IF_CONTEXT_CLOSED(mailbox, val)\
do {\
  if (MAILBOX_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      UNLOCK_MAILBOX (mailbox);\
      return (val);\
    }\
} while (0)


/*
 * enums
 */
typedef enum {
    MAILBOX_SORT_DATE = 1,
    MAILBOX_SORT_SIZE = 2,
    MAILBOX_SORT_SUBJECT = 3,
    MAILBOX_SORT_FROM = 4,
    MAILBOX_SORT_ORDER = 5,
    MAILBOX_SORT_THREADS = 6,
    MAILBOX_SORT_RECEIVED = 7,
    MAILBOX_SORT_TO = 8,
    MAILBOX_SORT_SCORE = 9,
    MAILBOX_SORT_ALIAS = 10,
    MAILBOX_SORT_ADDRESS = 11,
    MAILBOX_SORT_MASK = 0xf,
    MAILBOX_SORT_REVERSE = (1 << 4),
    MAILBOX_SORT_LAST = (1 << 5)
} LibBalsaMailboxSort;

typedef enum {
    LIBBALSA_NTFY_SOURCE,
    LIBBALSA_NTFY_FINISHED,
    LIBBALSA_NTFY_MSGINFO,
    LIBBALSA_NTFY_PROGRESS,
    LIBBALSA_NTFY_UPDATECONFIG,
    LIBBALSA_NTFY_ERROR
} LibBalsaMailboxNotify;


/* MBG: If this enum is changed (even just the order) make sure to
 * update pref-manager.c so the preferences work correctly */
typedef enum {
    LB_MAILBOX_THREADING_FLAT,
    LB_MAILBOX_THREADING_SIMPLE,
    LB_MAILBOX_THREADING_JWZ
} LibBalsaMailboxThreadingType;

typedef enum {
    LB_MAILBOX_SORT_TYPE_ASC,
    LB_MAILBOX_SORT_TYPE_DESC
} LibBalsaMailboxSortType;

typedef enum {
    LB_MAILBOX_SORT_NATURAL,
    LB_MAILBOX_SORT_NO,
    LB_MAILBOX_SORT_FROM,
    LB_MAILBOX_SORT_SUBJECT,
    LB_MAILBOX_SORT_DATE,
    LB_MAILBOX_SORT_SIZE,
    LB_MAILBOX_SORT_SENDER
} LibBalsaMailboxSortFields;

typedef enum {
    LB_MAILBOX_SHOW_UNSET = 0,
    LB_MAILBOX_SHOW_FROM,
    LB_MAILBOX_SHOW_TO
} LibBalsaMailboxShow;

/*
 * structures
 */
typedef struct _LibBalsaMailboxClass LibBalsaMailboxClass;

typedef struct _LibBalsaMailboxView LibBalsaMailboxView;
struct _LibBalsaMailboxView {
    LibBalsaAddress *mailing_list_address;
    gchar *identity_name;
    LibBalsaMailboxThreadingType threading_type;
    LibBalsaMailboxSortType      sort_type;
    LibBalsaMailboxSortFields    sort_field;
    LibBalsaMailboxShow          show;
    gboolean exposed;
    gboolean open;
};

struct _LibBalsaMailbox {
    GObject object;
    
    gchar *config_prefix;       /* unique string identifying mailbox */
                                /* in the config file                */
    gchar *name;                /* displayed name for a special mailbox; */
                                /* Isn't it a GUI thing?                 */
    gchar *url; /* Unique resource locator, file://, imap:// etc */
    guint open_ref;
    
    gboolean lock;
    gboolean is_directory;
    gboolean readonly;
    gboolean disconnected;

    glong messages; /* NOTE: this is used for internal msg counting;
		     * it is often different from g_list_count(messages) */  
    glong new_messages;
    GList *message_list;

    /* info fields */
    gboolean has_unread_messages;
    glong unread_messages;	/* number of unread messages in the mailbox */
    glong total_messages;	/* total number of messages in the mailbox  */

    /* Associated filters (struct mailbox_filter) */
    GSList * filters;

    LibBalsaMailboxView *view;
};

struct _LibBalsaMailboxClass {
    GObjectClass parent_class;

    /* Signals */
    gboolean (*open_mailbox) (LibBalsaMailbox * mailbox);
    void (*close_mailbox) (LibBalsaMailbox * mailbox);

    void (*messages_added) (LibBalsaMailbox * mailbox,
			    GList * messages);
    void (*messages_removed) (LibBalsaMailbox * mailbox,
			      GList * messages);
    void (*message_append) (LibBalsaMailbox * mailbox,
			    LibBalsaMessage * message);
    void (*messages_status_changed) (LibBalsaMailbox * mailbox,
				     GList * messages,
				     gint flag);
    void (*progress_notify) (LibBalsaMailbox * mailbox, int type,
                             int prog, int tot, const gchar* msg);
    void (*set_unread_messages_flag) (LibBalsaMailbox * mailbox,
				      gboolean flag);

    /* Virtual Functions */
    GMimeStream *(*get_message_stream) (LibBalsaMailbox * mailbox,
				 LibBalsaMessage * message);
    void (*check) (LibBalsaMailbox * mailbox);
    gboolean (*message_match) (LibBalsaMailbox * mailbox,
			       LibBalsaMessage * message,
			       int op, GSList* conditions);
    void (*mailbox_match) (LibBalsaMailbox * mailbox,
			   GSList * filters_list);
    gboolean (*can_match) (LibBalsaMailbox * mailbox,
			   GSList * conditions);
    void (*save_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
    void (*load_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
    gboolean (*sync) (LibBalsaMailbox * mailbox);
    GMimeMessage *(*get_message) (LibBalsaMailbox * mailbox, guint msgno);
    LibBalsaMessage *(*load_message) (LibBalsaMailbox * mailbox, guint msgno);
    int (*add_message) (LibBalsaMailbox * mailbox, GMimeStream *stream,
			LibBalsaMessageFlag flags);
    void (*change_message_flags) (LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear);
    gboolean (*close_backend)(LibBalsaMailbox * mailbox);
};

GType libbalsa_mailbox_get_type(void);

LibBalsaMailbox *libbalsa_mailbox_new_from_config(const gchar * prefix);

/* 
 * open and close a mailbox 
 */
/* XXX these need to return a value if they failed */
gboolean libbalsa_mailbox_open(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_is_valid(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_is_open(LibBalsaMailbox *mailbox);
void libbalsa_mailbox_close(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_link_message(LibBalsaMailbox * mbx, LibBalsaMessage*msg);
void libbalsa_mailbox_load_messages(LibBalsaMailbox * mailbox);

void libbalsa_mailbox_free_messages(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_remove_messages(LibBalsaMailbox * mbox,
				      GList * messages);
void libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean has_unread);
void libbalsa_mailbox_progress_notify(LibBalsaMailbox * mailbox,
                                      int type, int prog, int tot,
                                      const gchar* msg);

GMimeStream *libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
					  LibBalsaMessage * message);
gint libbalsa_mailbox_sync_backend(LibBalsaMailbox * mailbox, gboolean delete);

void libbalsa_mailbox_check(LibBalsaMailbox * mailbox);

/* This function returns TRUE if the mailbox can be matched
   against the given filters (eg : IMAP mailbox can't
   use the SEARCH IMAP command for regex match, so the
   match is done via default filtering funcs->can be slow)
 */
gboolean libbalsa_mailbox_can_match(LibBalsaMailbox * mailbox,
				    GSList * conditions);
gboolean libbalsa_mailbox_message_match(LibBalsaMailbox * mailbox,
					LibBalsaMessage * message,
					int op, GSList* conditions);

/* Virtual function (this function is different for IMAP
 */
void libbalsa_mailbox_match(LibBalsaMailbox * mbox, GSList * filter_list );

/* Default filtering function : this is exported because it is used
   as a fallback for IMAP mailboxes when SEARCH command can not be
   used.
   It is ONLY FOR INTERNAL USE (use libbalsa_mailbox_match instead)
*/
void libbalsa_mailbox_real_mbox_match(LibBalsaMailbox * mbox, GSList * filter_list);

/* Default filtering function (on reception) : this is exported
   because it is used as a fallback for IMAP mailboxes when SEARCH
   command can not be used.
   It is ONLY FOR INTERNAL USE
*/
void libbalsa_mailbox_run_filters_on_reception(LibBalsaMailbox * mailbox,
					       GSList * filters);

void libbalsa_mailbox_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix);
void libbalsa_mailbox_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix);

int libbalsa_mailbox_copy_message(LibBalsaMessage *message, LibBalsaMailbox *dest);
gboolean libbalsa_mailbox_close_backend(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_sync_storage(LibBalsaMailbox * mailbox);
GMimeMessage *libbalsa_mailbox_get_message(LibBalsaMailbox * mailbox, guint msgno);
LibBalsaMessage *libbalsa_mailbox_load_message(LibBalsaMailbox * mailbox, guint msgno);
int libbalsa_mailbox_add_message_stream(LibBalsaMailbox * mailbox,
					GMimeStream *stream,
					LibBalsaMessageFlag flags);
void libbalsa_mailbox_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear);

/*
 * misc mailbox releated functions
 */
GType libbalsa_mailbox_type_from_path(const gchar * filename);
gboolean libbalsa_mailbox_commit(LibBalsaMailbox* mailbox);

void libbalsa_mailbox_messages_status_changed(LibBalsaMailbox * mbox,
					      GList * messages,
					      gint flag);

/*
 * Mailbox views
 */
LibBalsaMailboxView *libbalsa_mailbox_view_new(void);
void libbalsa_mailbox_view_free(LibBalsaMailboxView * view);

#endif				/* __LIBBALSA_MAILBOX_H__ */
