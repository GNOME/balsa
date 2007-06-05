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
#include "config.h"

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"

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

#define MAILBOX_OPEN(mailbox)     (mailbox->state != LB_MAILBOX_STATE_CLOSED)

#define MAILBOX_CLOSED(mailbox)   (mailbox->state == LB_MAILBOX_STATE_CLOSED)

#define RETURN_IF_MAILBOX_CLOSED(mailbox)\
do {\
  if (MAILBOX_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      libbalsa_unlock_mailbox (mailbox);\
      return;\
    }\
} while (0)
#define RETURN_VAL_IF_CONTEXT_CLOSED(mailbox, val)\
do {\
  if (MAILBOX_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      libbalsa_unlock_mailbox (mailbox);\
      return (val);\
    }\
} while (0)

#define LIBBALSA_MAILBOX_UNTHREADED "libbalsa-mailbox-unthreaded"


typedef enum {
    LB_MAILBOX_SORT_NO, /* == NATURAL */
    LB_MAILBOX_SORT_SUBJECT,
    LB_MAILBOX_SORT_DATE,
    LB_MAILBOX_SORT_SIZE,
    LB_MAILBOX_SORT_SENDER,
    LB_MAILBOX_SORT_THREAD /* this is not exactly sorting flag but
                            * a message index ordering flag. */
} LibBalsaMailboxSortFields;

typedef struct _SortTuple SortTuple;
/* Sorting */
struct _SortTuple {
    guint offset;
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
    LB_MAILBOX_SUBSCRIBE_NO,
    LB_MAILBOX_SUBSCRIBE_YES,
    LB_MAILBOX_SUBSCRIBE_UNSET
} LibBalsaMailboxSubscribe;

typedef enum {
    LB_FETCH_RFC822_HEADERS = 1<<0, /* prepare all rfc822 headers */
    LB_FETCH_STRUCTURE      = 1<<1  /* prepare message structure */
} LibBalsaFetchFlag;

typedef enum {
    LB_MAILBOX_STATE_CLOSED,
    LB_MAILBOX_STATE_OPENING,
    LB_MAILBOX_STATE_OPEN,
    LB_MAILBOX_STATE_CLOSING
} LibBalsaMailboxState;

#ifdef HAVE_GPGME
typedef enum {
    LB_MAILBOX_CHK_CRYPT_NEVER,     /* never auto decrypt/signature check */
    LB_MAILBOX_CHK_CRYPT_MAYBE,     /* auto decrypt/signature check if possible */
    LB_MAILBOX_CHK_CRYPT_ALWAYS     /* always auto decrypt/signature check */
} LibBalsaChkCryptoMode;
#endif

enum LibBalsaMailboxCapability {
    LIBBALSA_MAILBOX_CAN_SORT,
    LIBBALSA_MAILBOX_CAN_THREAD
};

/*
 * structures
 */
typedef struct _LibBalsaMailboxClass LibBalsaMailboxClass;

typedef struct _LibBalsaMailboxView LibBalsaMailboxView;
struct _LibBalsaMailboxView {
    InternetAddressList *mailing_list_address;
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
    LibBalsaMailboxSubscribe     subscribe;
    gboolean exposed;
    gboolean open;
    gboolean in_sync;		/* view is in sync with config */
    gboolean frozen;		/* don't update view if set    */
    gboolean used;		/* keep track of usage         */

#ifdef HAVE_GPGME
    LibBalsaChkCryptoMode gpg_chk_mode;
#endif

    /* Display statistics:
     * - total >= 0                both counts are valid;
     * - total <  0 && unread == 0 unread is known to be zero;
     * - total <  0 && unread >  0 unread is known to be > 0,
     *                             but the count is not valid;
     * - total <  0 && unread <  0 both are unknown.
     */
    int unread;
    int total;
    time_t mtime;       /* Mailbox mtime when counts were cached. */
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

    GPtrArray *mindex;  /* the basic message index used for index
                         * displaying/columns of GtkTreeModel interface
                         * and NOTHING else. */
    GNode *msg_tree; /* the possibly filtered tree of messages;
                      * gdk lock MUST BE HELD when accessing. */
    LibBalsaCondition *view_filter; /* to choose a subset of messages
                                     * to be displayed, e.g., only
                                     * undeleted. */
    LibBalsaCondition *persistent_view_filter;

    /* info fields */
    gboolean has_unread_messages;
    glong unread_messages; /* number of unread messages in the mailbox */
    unsigned first_unread; /* set to 0 if there is no unread present.
                            * used for automatical scrolling down on opening.
                            */
    /* Associated filters (struct mailbox_filter) */
    GSList * filters;
    gboolean filters_loaded;

    LibBalsaMailboxView *view;
    LibBalsaMailboxState state;

    /* Whether to reassemble a message from its parts. */
    gboolean no_reassemble;

    /* Whether the tree has been changed since some event. */
    gboolean msg_tree_changed;

#ifdef BALSA_USE_THREADS
    /* Array of msgnos that need to be displayed. */
    GArray *msgnos_pending;
#endif                          /* BALSA_USE_THREADS */
};

/* Search iter */
struct _LibBalsaMailboxSearchIter {
    LibBalsaMailbox *mailbox;
    gint stamp;
    LibBalsaCondition *condition;	
    gpointer user_data;		/* private backend info */
};

struct _LibBalsaMailboxClass {
    GObjectClass parent_class;

    /* Signals */
    void (*changed) (LibBalsaMailbox * mailbox);
    void (*message_expunged) (LibBalsaMailbox * mailbox, guint seqno);
    void (*progress_notify) (LibBalsaMailbox * mailbox, int type,
                             int prog, int tot, const gchar* msg);

    /* Virtual Functions */
    gboolean (*open_mailbox) (LibBalsaMailbox * mailbox, GError **err);
    void (*close_mailbox) (LibBalsaMailbox * mailbox, gboolean expunge);
    LibBalsaMessage *(*get_message) (LibBalsaMailbox * mailbox, guint msgno);
    gboolean (*prepare_threading)(LibBalsaMailbox *mailbox, guint start);
    gboolean (*fetch_message_structure)(LibBalsaMailbox *mailbox,
                                        LibBalsaMessage * message,
                                        LibBalsaFetchFlag flags);
    void (*fetch_headers)(LibBalsaMailbox *mailbox,
                          LibBalsaMessage * message);
    void (*release_message) (LibBalsaMailbox * mailbox,
			     LibBalsaMessage * message);
    gboolean (*get_message_part) (LibBalsaMessage *message,
				  LibBalsaMessageBody *part,
                                  GError **err);
    GMimeStream *(*get_message_stream) (LibBalsaMailbox * mailbox,
                                        guint msgno);

    void (*check) (LibBalsaMailbox * mailbox);

    void (*search_iter_free) (LibBalsaMailboxSearchIter * iter);
    gboolean (*message_match) (LibBalsaMailbox * mailbox,
			       guint msgno,
			       LibBalsaMailboxSearchIter *search_iter);
    gboolean (*can_match) (LibBalsaMailbox * mailbox,
			   LibBalsaCondition *condition);
    void (*save_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
    void (*load_config) (LibBalsaMailbox * mailbox, const gchar * prefix);
    gboolean (*sync) (LibBalsaMailbox * mailbox, gboolean expunge);
    gboolean (*add_message) (LibBalsaMailbox * mailbox,
                             GMimeStream * stream,
                             LibBalsaMessageFlag flags, GError ** err);
    gboolean (*messages_change_flags) (LibBalsaMailbox * mailbox,
				       GArray *msgnos,
				       LibBalsaMessageFlag set,
				       LibBalsaMessageFlag clear);
    gboolean (*messages_copy) (LibBalsaMailbox * mailbox, GArray *msgnos,
			       LibBalsaMailbox * dest, GError **err);
    /* Test message flags */
    gboolean(*msgno_has_flags) (LibBalsaMailbox * mailbox, guint msgno,
                                LibBalsaMessageFlag set,
                                LibBalsaMessageFlag unset);

    gboolean (*can_do) (LibBalsaMailbox *mailbox,
                        enum LibBalsaMailboxCapability cap);
    void (*set_threading) (LibBalsaMailbox * mailbox,
			   LibBalsaMailboxThreadingType thread_type);
    void (*update_view_filter) (LibBalsaMailbox * mailbox,
                                LibBalsaCondition *view_filter);
    void (*sort) (LibBalsaMailbox * mailbox, GArray *sort_array);
    gboolean (*close_backend)(LibBalsaMailbox * mailbox);
    guint (*total_messages)(LibBalsaMailbox * mailbox);
    GArray *(*duplicate_msgnos) (LibBalsaMailbox * mailbox);
#if BALSA_USE_THREADS
    void (*lock_store) (LibBalsaMailbox * mailbox, gboolean lock);
#endif                          /* BALSA_USE_THREADS */
};

GType libbalsa_mailbox_get_type(void);

LibBalsaMailbox *libbalsa_mailbox_new_from_config(const gchar * prefix);

/* 
 * open and close a mailbox 
 */
/* XXX these need to return a value if they failed */
gboolean libbalsa_mailbox_open(LibBalsaMailbox * mailbox, GError **err);
gboolean libbalsa_mailbox_is_valid(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_is_open(LibBalsaMailbox *mailbox);
void libbalsa_mailbox_close(LibBalsaMailbox * mailbox, gboolean expunge);

void libbalsa_mailbox_check(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_changed(LibBalsaMailbox * mailbox);
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
    needed for client-side message threading.
    msgnos are related to currently set view.
    Returns TRUE if successful; FALSE may mean that the mailbox was
    closed during the operation.
*/
gboolean libbalsa_mailbox_prepare_threading(LibBalsaMailbox * mailbox,
                                            guint start);

/** libbalsa_mailbox_fetch_message_structure() fetches detailed
    message structure for given message. It can also fetch all RFC822
    headers of the message.
*/
gboolean libbalsa_mailbox_fetch_message_structure(LibBalsaMailbox *
                                                  mailbox,
                                                  LibBalsaMessage *
                                                  message,
                                                  LibBalsaFetchFlag flags);

/** libbalsa_mailbox_release_message() is called when the message
    content and structure are no longer needed. It's passed to the
    maildir and mh backends to unref the mime_message, but is a noop
    for other backends.
*/
void libbalsa_mailbox_release_message(LibBalsaMailbox * mailbox,
				      LibBalsaMessage * message);

void libbalsa_mailbox_set_msg_headers(LibBalsaMailbox * mailbox,
				      LibBalsaMessage * message);

/** libbalsa_mailbox_get_message_part() ensures that a selected, single
    part of the message is loaded.
*/
gboolean libbalsa_mailbox_get_message_part(LibBalsaMessage    *message,
					   LibBalsaMessageBody *part,
                                           GError **err);

/** libbalsa_mailbox_get_message_stream() returns a message stream associated
    with full RFC822 text of the message.
*/
GMimeStream *libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
						 guint msgno);


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
LibBalsaMailboxSearchIter *libbalsa_mailbox_search_iter_new(LibBalsaCondition
							    * condition);
LibBalsaMailboxSearchIter *libbalsa_mailbox_search_iter_view(LibBalsaMailbox
							     * mailbox);
gboolean libbalsa_mailbox_search_iter_step(LibBalsaMailbox * mailbox,
					   LibBalsaMailboxSearchIter 
					   * search_iter,
					   GtkTreeIter * iter,
					   gboolean forward,
					   guint stop_msgno);
void libbalsa_mailbox_search_iter_free(LibBalsaMailboxSearchIter * iter);

/* Default filtering function (on reception)
   It is ONLY FOR INTERNAL USE
*/
void libbalsa_mailbox_run_filters_on_reception(LibBalsaMailbox * mailbox);

void libbalsa_mailbox_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix);

gboolean libbalsa_mailbox_add_message(LibBalsaMailbox * mailbox,
                                      GMimeStream * stream,
                                      LibBalsaMessageFlag flags,
                                      GError ** err);
gboolean libbalsa_mailbox_close_backend(LibBalsaMailbox * mailbox);

/* Message number-list methods */
gboolean libbalsa_mailbox_messages_change_flags(LibBalsaMailbox * mailbox,
						GArray * msgnos,
						LibBalsaMessageFlag set,
						LibBalsaMessageFlag clear);
gboolean libbalsa_mailbox_messages_copy(LibBalsaMailbox * mailbox,
					GArray * msgnos,
					LibBalsaMailbox * dest, GError **err);
gboolean libbalsa_mailbox_messages_move(LibBalsaMailbox * mailbox,
					GArray * msgnos,
					LibBalsaMailbox * dest, GError **err);

/*
 * misc mailbox releated functions
 */
GType libbalsa_mailbox_type_from_path(const gchar * filename);

guint libbalsa_mailbox_total_messages(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_can_move_duplicates(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_move_duplicates(LibBalsaMailbox * mailbox,
                                      LibBalsaMailbox * dest,
                                      GError ** err);

/*
 * Mailbox views-related functions.
 */
typedef struct LibBalsaMailboxIndexEntry_ LibBalsaMailboxIndexEntry;
LibBalsaMailboxIndexEntry* libbalsa_mailbox_index_entry_new_from_msg
                           (LibBalsaMessage *msg);
void libbalsa_mailbox_index_entry_set_no(LibBalsaMailboxIndexEntry *entry,
                                         unsigned no);
void libbalsa_mailbox_index_entry_free(LibBalsaMailboxIndexEntry *entry);
void libbalsa_mailbox_index_set_flags(LibBalsaMailbox *mailbox,
				      unsigned msgno, LibBalsaMessageFlag f);
gboolean libbalsa_mailbox_set_view_filter(LibBalsaMailbox * mailbox,
                                          LibBalsaCondition *
                                          filter_condition,
                                          gboolean update_immediately);
void libbalsa_mailbox_make_view_filter_persistent(LibBalsaMailbox *
                                                  mailbox);

gboolean libbalsa_mailbox_can_do(LibBalsaMailbox *mailbox,
                                 enum LibBalsaMailboxCapability cap);

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

/* Mailbox views. */
extern GHashTable *libbalsa_mailbox_view_table;

LibBalsaMailboxView *libbalsa_mailbox_view_new(void);
void libbalsa_mailbox_view_free(LibBalsaMailboxView * view);
gboolean libbalsa_mailbox_set_identity_name(LibBalsaMailbox * mailbox,
					    const gchar * identity_name);
void libbalsa_mailbox_set_threading_type(LibBalsaMailbox * mailbox,
					 LibBalsaMailboxThreadingType
					 threading_type);
void libbalsa_mailbox_set_sort_type(LibBalsaMailbox * mailbox,
				    LibBalsaMailboxSortType sort_type);
void libbalsa_mailbox_set_sort_field(LibBalsaMailbox * mailbox,
				     LibBalsaMailboxSortFields sort_field);
gboolean libbalsa_mailbox_set_show(LibBalsaMailbox * mailbox,
				   LibBalsaMailboxShow show);
gboolean libbalsa_mailbox_set_subscribe(LibBalsaMailbox * mailbox,
                                        LibBalsaMailboxSubscribe
                                        subscribe);
void libbalsa_mailbox_set_exposed(LibBalsaMailbox * mailbox,
				  gboolean exposed);
void libbalsa_mailbox_set_open(LibBalsaMailbox * mailbox, gboolean open);
void libbalsa_mailbox_set_filter(LibBalsaMailbox * mailbox, gint filter);
void libbalsa_mailbox_set_frozen(LibBalsaMailbox * mailbox, gboolean frozen);
#ifdef HAVE_GPGME
gboolean libbalsa_mailbox_set_crypto_mode(LibBalsaMailbox * mailbox,
					  LibBalsaChkCryptoMode gpg_chk_mode);
#endif
void libbalsa_mailbox_set_unread(LibBalsaMailbox * mailbox, gint unread);
void libbalsa_mailbox_set_total (LibBalsaMailbox * mailbox, gint total);
void libbalsa_mailbox_set_mtime (LibBalsaMailbox * mailbox, time_t mtime);

InternetAddressList
    *libbalsa_mailbox_get_mailing_list_address(LibBalsaMailbox * mailbox);
const gchar *libbalsa_mailbox_get_identity_name(LibBalsaMailbox * mailbox);
LibBalsaMailboxThreadingType
libbalsa_mailbox_get_threading_type(LibBalsaMailbox * mailbox);
LibBalsaMailboxSortType libbalsa_mailbox_get_sort_type(LibBalsaMailbox *
						       mailbox);
LibBalsaMailboxSortFields libbalsa_mailbox_get_sort_field(LibBalsaMailbox *
							  mailbox);
LibBalsaMailboxShow libbalsa_mailbox_get_show(LibBalsaMailbox * mailbox);
LibBalsaMailboxSubscribe libbalsa_mailbox_get_subscribe(LibBalsaMailbox *
                                                        mailbox);
gboolean libbalsa_mailbox_get_exposed(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_get_open(LibBalsaMailbox * mailbox);
gint libbalsa_mailbox_get_filter(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_get_frozen(LibBalsaMailbox * mailbox);
#ifdef HAVE_GPGME
LibBalsaChkCryptoMode libbalsa_mailbox_get_crypto_mode(LibBalsaMailbox * mailbox);
#endif
gint libbalsa_mailbox_get_unread(LibBalsaMailbox * mailbox);
gint libbalsa_mailbox_get_total (LibBalsaMailbox * mailbox);
time_t libbalsa_mailbox_get_mtime(LibBalsaMailbox * mailbox);

/** force update of given msgno */
void libbalsa_mailbox_msgno_changed(LibBalsaMailbox  *mailbox, guint seqno);
void libbalsa_mailbox_msgno_inserted(LibBalsaMailbox * mailbox,
                                     guint seqno, GNode * parent,
                                     GNode ** sibling);
void libbalsa_mailbox_msgno_removed(LibBalsaMailbox  *mailbox, guint seqno);
void libbalsa_mailbox_msgno_filt_in(LibBalsaMailbox * mailbox, guint seqno);
void libbalsa_mailbox_msgno_filt_out(LibBalsaMailbox * mailbox, guint seqno);
void libbalsa_mailbox_msgno_filt_check(LibBalsaMailbox * mailbox,
				       guint seqno,
				       LibBalsaMailboxSearchIter
				       * search_iter,
				       gboolean hold_selected);

/* Search */
gboolean libbalsa_mailbox_msgno_find(LibBalsaMailbox * mailbox,
				     guint seqno,
				     GtkTreePath ** path,
				     GtkTreeIter * iter);
/* Manage message flags */
gboolean libbalsa_mailbox_msgno_change_flags(LibBalsaMailbox * mailbox,
                                             guint msgno,
                                             LibBalsaMessageFlag set,
                                             LibBalsaMessageFlag clear);
/* Test message flags */
gboolean libbalsa_mailbox_msgno_has_flags(LibBalsaMailbox * mailbox,
                                          guint seqno,
                                          LibBalsaMessageFlag set,
                                          LibBalsaMessageFlag unset);

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

/* Message numbers and arrays */
void libbalsa_mailbox_register_msgno(LibBalsaMailbox * mailbox,
                                     guint * msgno);
void libbalsa_mailbox_register_msgnos(LibBalsaMailbox * mailbox,
				      GArray * msgnos);
void libbalsa_mailbox_unregister_msgnos(LibBalsaMailbox * mailbox,
					GArray * msgnos);

/* Accessors for LibBalsaMailboxIndexEntry */
LibBalsaMessageStatus libbalsa_mailbox_msgno_get_status(LibBalsaMailbox *
							mailbox,
							guint msgno);
const gchar *libbalsa_mailbox_msgno_get_subject(LibBalsaMailbox * mailbox,
						guint msgno);
void libbalsa_mailbox_msgno_update_attach(LibBalsaMailbox * mailbox,
					  guint msgno,
					  LibBalsaMessage * message);
void libbalsa_mailbox_cache_message(LibBalsaMailbox * mailbox, guint msgno,
                                    LibBalsaMessage * message);

#if BALSA_USE_THREADS

/* Lock and unlock the mail store--currently, a no-op except for mbox.
 */
void libbalsa_mailbox_lock_store  (LibBalsaMailbox * mailbox);
void libbalsa_mailbox_unlock_store(LibBalsaMailbox * mailbox);

#else                           /* BALSA_USE_THREADS */

#define libbalsa_mailbox_lock_store(mailbox)
#define libbalsa_mailbox_unlock_store(mailbox)

#endif                          /* BALSA_USE_THREADS */

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
    LB_MBOX_N_COLS
} LibBalsaMailboxColumn;

extern gchar *libbalsa_mailbox_date_format;

#endif				/* __LIBBALSA_MAILBOX_H__ */
