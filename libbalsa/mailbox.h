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
#include <gmime/gmime.h>

#include "libbalsa.h"
#include "filter.h"

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



typedef enum {
    LB_MAILBOX_SORT_NATURAL,
    LB_MAILBOX_SORT_NO,
    LB_MAILBOX_SORT_FROM,
    LB_MAILBOX_SORT_SUBJECT,
    LB_MAILBOX_SORT_DATE,
    LB_MAILBOX_SORT_SIZE,
    LB_MAILBOX_SORT_SENDER
} LibBalsaMailboxSortFields;

typedef struct _SortTuple SortTuple;
/* Sorting */
struct _SortTuple {
    gint offset;
    GNode *node;
};

typedef enum {
    LB_MAILBOX_SORT_TYPE_ASC,
    LB_MAILBOX_SORT_TYPE_DESC
} LibBalsaMailboxSortType;

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
    LB_MAILBOX_SHOW_UNSET = 0,
    LB_MAILBOX_SHOW_FROM,
    LB_MAILBOX_SHOW_TO
} LibBalsaMailboxShow;

typedef enum {
    LB_FETCH_RFC822_HEADERS = 1<<0, /* prepare all rfc822 headers */
    LB_FETCH_STRUCTURE      = 1<<1  /* prepare message structure */
} LibBalsaFetchFlag;


/*
 * structures
 */
typedef struct _LibBalsaMailboxClass LibBalsaMailboxClass;

typedef struct _LibBalsaMailboxView LibBalsaMailboxView;
struct _LibBalsaMailboxView {
    LibBalsaAddress *mailing_list_address;
    gchar *identity_name;
    LibBalsaMailboxThreadingType threading_type;
    /** filter is a frontend-specific code determining used view
     * filter.  GUI usually allows to generate only a subset of all
     * possible LibBalsaCondition's and mapping from arbitary
     * LibBalsaCondition to a GUI configuration is not always
     * possible.  Therefore, we provide this variable for GUI's
     * convinence.  */
    int      filter; 
    LibBalsaMailboxSortType      sort_type;
    LibBalsaMailboxSortFields    sort_field;
    LibBalsaMailboxShow          show;
    unsigned exposed:1;
    unsigned open:1;
};

struct _LibBalsaMailbox {
    GObject object;
    gint stamp; /* used to determine iterators' validity. Increased on each
                 * modification of mailbox. */
    
    gchar *config_prefix;       /* unique string identifying mailbox */
                                /* in the config file                */
    gchar *name;                /* displayed name for a special mailbox; */
                                /* Isn't it a GUI thing?                 */
    gchar *url; /* Unique resource locator, file://, imap:// etc */
    guint open_ref;
    
    int lock; /* 0 if mailbox is unlocked; */
              /* >0 if mailbox is (recursively locked). */
#ifdef BALSA_USE_THREADS
    pthread_t thread_id; /* id of thread that locked the mailbox */
#endif
    gboolean is_directory;
    gboolean readonly;
    gboolean disconnected;

    GNode *msg_tree; /* the possibly filtered tree of messages */
    LibBalsaCondition *view_filter; /* to choose a subset of messages
                                     * to be displayed, e.g., only
                                     * undeleted. */

    /* info fields */
    gboolean has_unread_messages;
    glong unread_messages; /* number of unread messages in the mailbox */
    unsigned first_unread; /* set to 0 if there is no unread present.
                            * used for automatical scrolling down on opening.
                            */
    /* Associated filters (struct mailbox_filter) */
    GSList * filters;

    LibBalsaMailboxView *view;
};

/* Search iter */
typedef struct {
    GtkTreeIter *iter;		/* input: starting point for search;
				 * output: found message. */
    LibBalsaCondition *condition;	
    gpointer user_data;		/* private backend info */
} LibBalsaMailboxSearchIter;

struct _LibBalsaMailboxClass {
    GObjectClass parent_class;

    /* Signals */
    gboolean (*open_mailbox) (LibBalsaMailbox * mailbox);
    void (*close_mailbox) (LibBalsaMailbox * mailbox);
    void (*changed) (LibBalsaMailbox * mailbox);

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
    LibBalsaMessage *(*get_message) (LibBalsaMailbox * mailbox, guint msgno);
    void (*prepare_threading)(LibBalsaMailbox *mailbox, guint lo, guint hi);
    void (*fetch_message_structure)(LibBalsaMailbox *mailbox,
                                    LibBalsaMessage * message,
                                    LibBalsaFetchFlag flags);
    void (*release_message) (LibBalsaMailbox * mailbox,
			     LibBalsaMessage * message);
    const gchar *(*get_message_part) (LibBalsaMessage     *message,
                                      LibBalsaMessageBody *part, ssize_t*);
    GMimeStream *(*get_message_stream) (LibBalsaMailbox * mailbox,
					LibBalsaMessage * message);

    void (*check) (LibBalsaMailbox * mailbox);

    void (*search_iter_free) (LibBalsaMailboxSearchIter * iter);
    gboolean (*message_match) (LibBalsaMailbox * mailbox,
			       guint msgno,
			       LibBalsaMailboxSearchIter *search_iter);
    void (*mailbox_match) (LibBalsaMailbox * mailbox,
			   GSList * filters_list);
    gboolean (*can_match) (LibBalsaMailbox * mailbox,
			   LibBalsaCondition *condition);
    void (*save_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
    void (*load_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
    gboolean (*sync) (LibBalsaMailbox * mailbox, gboolean expunge);
    int (*add_message) (LibBalsaMailbox * mailbox, LibBalsaMessage * message );
    void (*change_message_flags) (LibBalsaMailbox * mailbox, guint msgno,
                                  LibBalsaMessageFlag set,
                                  LibBalsaMessageFlag clear);
    gboolean (*change_msgs_flags) (LibBalsaMailbox * mailbox,
                                   GList *messages, /* change to list
                                                     * of MSGNOs? */
                                   LibBalsaMessageFlag set,
                                   LibBalsaMessageFlag clear);
    void (*set_threading) (LibBalsaMailbox * mailbox,
			   LibBalsaMailboxThreadingType thread_type);
    void (*update_view_filter) (LibBalsaMailbox * mailbox,
                                LibBalsaCondition *view_filter);
    void (*sort) (LibBalsaMailbox * mailbox, GArray *sort_array);
    gboolean (*close_backend)(LibBalsaMailbox * mailbox);
    guint (*total_messages)(LibBalsaMailbox * mailbox);
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

void libbalsa_mailbox_check(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_remove_messages(LibBalsaMailbox * mbox,
				      GList * messages);
void libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean has_unread);
void libbalsa_mailbox_progress_notify(LibBalsaMailbox * mailbox,
                                      int type, int prog, int tot,
                                      const gchar* msg);

/** Message access functions.
 */

/** libbalsa_mailbox_get_message() returns structure containing
    changed, UTF-8 converted data of the message.  LibBalsaMessage
    will contain only basic information about the message sufficient to
    produce message index unless more information was requested to be
    prefetched.
 */
LibBalsaMessage *libbalsa_mailbox_get_message(LibBalsaMailbox * mailbox,
					      guint msgno);

/** libbalsa_mailbox_prepare_threading() requests prefetching of information
    needed for client-size message threading.
    lo and hi are related to currently set view.
*/
void libbalsa_mailbox_prepare_threading(LibBalsaMailbox *mailbox,
					guint lo, guint hi);

/** libbalsa_mailbox_fetch_message_structure() fetches detailed
    message structure for given message. It can also fetch all RFC822
    headers of the message.
*/
void libbalsa_mailbox_fetch_message_structure(LibBalsaMailbox *mailbox,
					      LibBalsaMessage *message,
					      LibBalsaFetchFlag flags);

/** libbalsa_mailbox_release_message() is called when the message
    content and structure are no longer needed. It's passed to the
    maildir and mh backends to unref the mime_message, but is a noop
    for other backends.
*/
void libbalsa_mailbox_release_message(LibBalsaMailbox * mailbox,
				      LibBalsaMessage * message);

/** libbalsa_mailbox_get_message_stream() returns an allocated block containing
    selected, single part of the message.
*/
const gchar *libbalsa_mailbox_get_message_part(LibBalsaMessage    *message,
                                               LibBalsaMessageBody *part, 
                                               ssize_t *sz);

/** libbalsa_mailbox_get_message_stream() returns a message stream associated
    with full RFC822 text of the message.
*/
GMimeStream *libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
						 LibBalsaMessage * message);


/** libbalsa_mailbox_sync_storage() asks the mailbox to synchronise
    the memory information about messages with disk. Many drivers
    update storage immediately and for them this operation may be
    no-op. When expunge is set, driver is supposed to clean up the mailbox,
    including physical removal of old deleted messages.
*/

gboolean libbalsa_mailbox_sync_storage(LibBalsaMailbox * mailbox,
                                       gboolean expunge);

/* This function returns TRUE if the mailbox can be matched
   against the given filters (eg : IMAP mailbox can't
   use the SEARCH IMAP command for regex match, so the
   match is done via default filtering funcs->can be slow)
 */
gboolean libbalsa_mailbox_can_match(LibBalsaMailbox  *mailbox,
				    LibBalsaCondition *condition);
gboolean libbalsa_mailbox_message_match(LibBalsaMailbox  *mailbox,
					guint msgno,
					LibBalsaMailboxSearchIter *search_iter);

/* Search iter */
LibBalsaMailboxSearchIter *libbalsa_mailbox_search_iter_new(LibBalsaMailbox
							    * mailbox,
							    GtkTreeIter *
							    pos,
							    LibBalsaCondition
							    * condition);
gboolean libbalsa_mailbox_search_iter_step(LibBalsaMailbox * mailbox,
					   LibBalsaMailboxSearchIter * iter,
					   gboolean forward);
void libbalsa_mailbox_search_iter_free(LibBalsaMailbox * mailbox,
				       LibBalsaMailboxSearchIter * iter);

/* Virtual function (this function is different for IMAP
 */
void libbalsa_mailbox_match(LibBalsaMailbox * mbox, GSList * filter_list );

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

int libbalsa_mailbox_copy_message(LibBalsaMessage *message,
				  LibBalsaMailbox *dest);
gboolean libbalsa_mailbox_close_backend(LibBalsaMailbox * mailbox);

void libbalsa_mailbox_change_message_flags(LibBalsaMailbox * mailbox,
					   guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear);
/* */
gboolean libbalsa_mailbox_change_msgs_flags(LibBalsaMailbox * mailbox,
                                            GList *messages,
                                            LibBalsaMessageFlag set,
                                            LibBalsaMessageFlag clear);

                               
/*
 * misc mailbox releated functions
 */
GType libbalsa_mailbox_type_from_path(const gchar * filename);

void libbalsa_mailbox_messages_status_changed(LibBalsaMailbox * mbox,
					      GList * messages,
					      gint flag);
guint libbalsa_mailbox_total_messages(LibBalsaMailbox * mailbox);

/*
 * Mailbox views-related functions.
 */
void libbalsa_mailbox_set_view_filter(LibBalsaMailbox   *mailbox,
                                      LibBalsaCondition *filter_condition,
                                      gboolean update_immediately);

/** libbalsa_mailbox_set_threading() uses backend-optimized threading mode
    to produce a tree of messages. The tree is put in msg_tree and used
    later by GtkTreeModel interface.
    libbalsa_mailbox_set_threading() is the public method;
    libbalsa_mailbox_set_msg_tree and libbalsa_mailbox_unlink_and_prepend
    are helpers for the subclass methods.
*/
void libbalsa_mailbox_set_threading(LibBalsaMailbox *mailbox,
				    LibBalsaMailboxThreadingType thread_type);
void libbalsa_mailbox_set_msg_tree(LibBalsaMailbox * mailbox,
				   GNode * msg_tree);
void libbalsa_mailbox_unlink_and_prepend(LibBalsaMailbox * mailbox,
					 GNode * node, GNode * parent);

LibBalsaMailboxView *libbalsa_mailbox_view_new(void);
void libbalsa_mailbox_view_free(LibBalsaMailboxView * view);

/** force update of given msgno */
void libbalsa_mailbox_msgno_changed(LibBalsaMailbox  *mailbox, guint seqno);
void libbalsa_mailbox_msgno_inserted(LibBalsaMailbox *mailbox, guint seqno);
void libbalsa_mailbox_msgno_removed(LibBalsaMailbox  *mailbox, guint seqno);
void libbalsa_mailbox_msgno_filt_in(LibBalsaMailbox * mailbox, guint seqno);
void libbalsa_mailbox_msgno_filt_out(LibBalsaMailbox * mailbox, guint seqno);

/* Search */
gboolean libbalsa_mailbox_msgno_find(LibBalsaMailbox * mailbox,
				     guint seqno,
				     GtkTreePath ** path,
				     GtkTreeIter * iter);

/* set icons */
void libbalsa_mailbox_set_unread_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_trash_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_flagged_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_replied_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_attach_icon(GdkPixbuf * pixbuf);
#ifdef HAVE_GPGME
void libbalsa_mailbox_set_good_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_notrust_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_bad_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_sign_icon(GdkPixbuf * pixbuf);
void libbalsa_mailbox_set_encr_icon(GdkPixbuf * pixbuf);
#endif /* HAVE_GPGME */

/* Partial messages */
void libbalsa_mailbox_try_reassemble(LibBalsaMailbox * mailbox,
				     const gchar * id);

/* columns ids */
typedef enum {
    LB_MBOX_MSGNO_COL,
    LB_MBOX_MARKED_COL,
    LB_MBOX_ATTACH_COL,
    LB_MBOX_FROM_COL,
    LB_MBOX_SUBJECT_COL,
    LB_MBOX_DATE_COL,
    LB_MBOX_SIZE_COL,
    LB_MBOX_WEIGHT_COL,
    LB_MBOX_STYLE_COL,
    LB_MBOX_MESSAGE_COL,
    LB_MBOX_N_COLS
} LibBalsaMailboxColumn;

#endif				/* __LIBBALSA_MAILBOX_H__ */
