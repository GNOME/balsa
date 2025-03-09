/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __LIBBALSA_MAILBOX_H__
#define __LIBBALSA_MAILBOX_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <gdk/gdk.h>
#include <gmime/gmime.h>

#define LIBBALSA_TYPE_MAILBOX (libbalsa_mailbox_get_type())

G_DECLARE_DERIVABLE_TYPE(LibBalsaMailbox,
                         libbalsa_mailbox,
                         LIBBALSA,
                         MAILBOX,
                         GObject)

#define MAILBOX_OPEN(mailbox) \
        (libbalsa_mailbox_get_state(mailbox) != LB_MAILBOX_STATE_CLOSED)

#define MAILBOX_CLOSED(mailbox) \
        (libbalsa_mailbox_get_state(mailbox) == LB_MAILBOX_STATE_CLOSED)

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
    time_t thread_date;
};

typedef enum {
    LB_MAILBOX_SORT_TYPE_ASC,
    LB_MAILBOX_SORT_TYPE_DESC
} LibBalsaMailboxSortType;

typedef enum {
	LIBBALSA_NTFY_INIT,
	LIBBALSA_NTFY_UPDATE,
	LIBBALSA_NTFY_FINISHED
} LibBalsaMailboxNotify;


/* MBG: If this enum is changed (even just the order) make sure to
 * update pref-manager.c so the preferences work correctly */
typedef enum {
    LB_MAILBOX_THREADING_FLAT,
    LB_MAILBOX_THREADING_SIMPLE, /* JWZ without the subject-gather step */
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

typedef enum {
    LB_MAILBOX_CHK_CRYPT_NEVER,     /* never auto decrypt/signature check */
    LB_MAILBOX_CHK_CRYPT_MAYBE,     /* auto decrypt/signature check if possible */
    LB_MAILBOX_CHK_CRYPT_ALWAYS     /* always auto decrypt/signature check */
} LibBalsaChkCryptoMode;

enum LibBalsaMailboxCapability {
    LIBBALSA_MAILBOX_CAN_SORT,
    LIBBALSA_MAILBOX_CAN_THREAD
};

/*
 * structures
 */

typedef struct _LibBalsaMailboxView LibBalsaMailboxView;
struct _LibBalsaMailboxView {
    gchar *identity_name;
    LibBalsaMailboxThreadingType threading_type;
    gboolean subject_gather;
    /** filter is a frontend-specific code determining used view
     * filter.  GUI usually allows to generate only a subset of all
     * possible LibBalsaCondition's and mapping from arbitary
     * LibBalsaCondition to a GUI configuration is not always
     * possible.  Therefore, we provide this variable for GUI's
     * convinence.  */
    int      filter; 
    LibBalsaMailboxSortType      sort_type;
    LibBalsaMailboxSortFields    sort_field;
    LibBalsaMailboxSortFields    sort_field_prev;
    LibBalsaMailboxShow          show;
    LibBalsaMailboxSubscribe     subscribe;
    gboolean exposed;
    gboolean open;
    gboolean in_sync;		/* view is in sync with config */
    gboolean used;		/* keep track of usage         */

    LibBalsaChkCryptoMode gpg_chk_mode;

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
    int position;       /* Position in the notebook */
};

/* Search iter */
struct _LibBalsaMailboxSearchIter {
    gint ref_count;
    gint stamp;
    LibBalsaMailbox *mailbox;
    LibBalsaCondition *condition;
    gpointer user_data;		/* private backend info */
};

/** Iterates over a list of messages, returning each time it is called
    flags and the stream to a message. It is the responsibility of the
    called to un-ref the stream after use. */
typedef gboolean (*LibBalsaAddMessageIterator)(LibBalsaMessageFlag *,
					       GMimeStream **stream,
					       void *);

struct _LibBalsaMailboxClass {
    GObjectClass parent_class;

    /* Signals */
    void (*changed) (LibBalsaMailbox * mailbox);
    void (*message_expunged) (LibBalsaMailbox * mailbox, guint seqno);
    void (*progress_notify) (LibBalsaMailbox * mailbox, gint action, gdouble fraction, gchar *message);

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
                                        guint msgno, gboolean peek);

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
    guint (*add_messages) (LibBalsaMailbox * mailbox,
			   LibBalsaAddMessageIterator msg_iterator,
			   void *iter_arg, GError ** err);
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
    void (*lock_store) (LibBalsaMailbox * mailbox, gboolean lock);
    void (*test_can_reach) (LibBalsaMailbox          * mailbox,
                            LibBalsaCanReachCallback * cb,
                            gpointer                   cb_data);
    void (*cache_message) (LibBalsaMailbox *mailbox,
                           guint            msgno,
                           LibBalsaMessage *message);
};

LibBalsaMailbox *libbalsa_mailbox_new_from_config(const gchar *prefix,
												  gboolean     is_special);

/* 
 * open and close a mailbox 
 */
/* XXX these need to return a value if they failed */
gboolean libbalsa_mailbox_open(LibBalsaMailbox * mailbox, GError **err);
gboolean libbalsa_mailbox_is_open(LibBalsaMailbox *mailbox);
void libbalsa_mailbox_close(LibBalsaMailbox * mailbox, gboolean expunge);

void libbalsa_mailbox_check(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_changed(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean has_unread);
void libbalsa_mailbox_progress_notify(LibBalsaMailbox       *mailbox,
									  LibBalsaMailboxNotify  action,
									  gdouble		         fraction,
									  const gchar           *message,
									  ...)
	G_GNUC_PRINTF(4, 5);

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
						 guint msgno, gboolean peek);


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
LibBalsaMailboxSearchIter
    *libbalsa_mailbox_search_iter_new(LibBalsaCondition * condition);
LibBalsaMailboxSearchIter
    *libbalsa_mailbox_search_iter_view(LibBalsaMailbox * mailbox);
LibBalsaMailboxSearchIter
    *libbalsa_mailbox_search_iter_ref(LibBalsaMailboxSearchIter * iter);
void libbalsa_mailbox_search_iter_unref(LibBalsaMailboxSearchIter * iter);
gboolean libbalsa_mailbox_search_iter_step(LibBalsaMailbox * mailbox,
					   LibBalsaMailboxSearchIter 
					   * search_iter,
					   GtkTreeIter * iter,
					   gboolean forward,
					   guint stop_msgno);

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
gint libbalsa_mailbox_move_duplicates(LibBalsaMailbox * mailbox,
                                      LibBalsaMailbox * dest,
                                      GError ** err);

/*
 * Mailbox views-related functions.
 */
typedef struct LibBalsaMailboxIndexEntry_ LibBalsaMailboxIndexEntry;
void libbalsa_mailbox_index_entry_set_no(LibBalsaMailboxIndexEntry *entry,
                                         unsigned no);
void libbalsa_mailbox_index_entry_clear(LibBalsaMailbox * mailbox,
                                        guint msgno);
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
void libbalsa_mailbox_set_threading(LibBalsaMailbox *mailbox);
void libbalsa_mailbox_set_msg_tree(LibBalsaMailbox * mailbox,
				   GNode * msg_tree);
void libbalsa_mailbox_unlink_and_prepend(LibBalsaMailbox * mailbox,
					 GNode * node, GNode * parent);

/* Mailbox views. */
LibBalsaMailboxView *libbalsa_mailbox_view_new(void);
void libbalsa_mailbox_view_free(LibBalsaMailboxView * view);
gboolean libbalsa_mailbox_set_identity_name(LibBalsaMailbox * mailbox,
					    const gchar * identity_name);
void libbalsa_mailbox_set_threading_type(LibBalsaMailbox * mailbox,
					 LibBalsaMailboxThreadingType
					 threading_type);
void libbalsa_mailbox_set_subject_gather(LibBalsaMailbox * mailbox,
                                         gboolean subject_gather);
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
gboolean libbalsa_mailbox_set_crypto_mode(LibBalsaMailbox * mailbox,
					  LibBalsaChkCryptoMode gpg_chk_mode);
void libbalsa_mailbox_set_unread(LibBalsaMailbox * mailbox, gint unread);
void libbalsa_mailbox_set_total (LibBalsaMailbox * mailbox, gint total);
void libbalsa_mailbox_set_mtime (LibBalsaMailbox * mailbox, time_t mtime);
void libbalsa_mailbox_set_position(LibBalsaMailbox * mailbox, gint position);

const gchar *libbalsa_mailbox_get_identity_name(LibBalsaMailbox * mailbox);
LibBalsaMailboxThreadingType
libbalsa_mailbox_get_threading_type(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_get_subject_gather(LibBalsaMailbox * mailbox);
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
LibBalsaChkCryptoMode libbalsa_mailbox_get_crypto_mode(LibBalsaMailbox * mailbox);
gint libbalsa_mailbox_get_unread(LibBalsaMailbox * mailbox);
gint libbalsa_mailbox_get_total (LibBalsaMailbox * mailbox);
time_t libbalsa_mailbox_get_mtime(LibBalsaMailbox * mailbox);
gint libbalsa_mailbox_get_position(LibBalsaMailbox * mailbox);

/** force update of given msgno */
void libbalsa_mailbox_msgno_changed(LibBalsaMailbox  *mailbox, guint seqno);
void libbalsa_mailbox_msgno_inserted(LibBalsaMailbox * mailbox,
                                     guint seqno, GNode * parent,
                                     GNode ** sibling);
void libbalsa_mailbox_msgno_removed(LibBalsaMailbox  *mailbox, guint seqno);
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
void libbalsa_mailbox_set_unread_icon(const char *name);
void libbalsa_mailbox_set_trash_icon(const char *name);
void libbalsa_mailbox_set_flagged_icon(const char *name);
void libbalsa_mailbox_set_replied_icon(const char *name);
void libbalsa_mailbox_set_attach_icon(const char *name);
void libbalsa_mailbox_set_good_icon(const char *name);
void libbalsa_mailbox_set_notrust_icon(const char *name);
void libbalsa_mailbox_set_bad_icon(const char *name);
void libbalsa_mailbox_set_sign_icon(const char *name);
void libbalsa_mailbox_set_encr_icon(const char *name);

/* Partial messages */
void libbalsa_mailbox_try_reassemble(LibBalsaMailbox * mailbox,
				     const gchar * id);

/* Message numbers and arrays */
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

/* Set the foreground and background colors of an array of messages */
void libbalsa_mailbox_set_foreground(LibBalsaMailbox * mailbox,
                                     GArray * msgnos, const gchar * color);
void libbalsa_mailbox_set_background(LibBalsaMailbox * mailbox,
                                     GArray * msgnos, const gchar * color);

/* Lock and unlock the mail store--currently, a no-op except for mbox.
 */
void libbalsa_mailbox_lock_store  (LibBalsaMailbox * mailbox);
void libbalsa_mailbox_unlock_store(LibBalsaMailbox * mailbox);

/* Check whether a mailbox can be reached */
void libbalsa_mailbox_test_can_reach(LibBalsaMailbox          * mailbox,
                                     LibBalsaCanReachCallback * cb,
                                     gpointer                   cb_data);

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
    LB_MBOX_FOREGROUND_COL,
    LB_MBOX_FOREGROUND_SET_COL,
    LB_MBOX_BACKGROUND_COL,
    LB_MBOX_BACKGROUND_SET_COL,
    LB_MBOX_N_COLS
} LibBalsaMailboxColumn;

extern gchar **libbalsa_mailbox_date_format;

/*
 * Getters
 */
GSList * libbalsa_mailbox_get_filters(LibBalsaMailbox * mailbox);
const gchar * libbalsa_mailbox_get_name(LibBalsaMailbox * mailbox);
const gchar * libbalsa_mailbox_get_url(LibBalsaMailbox * mailbox);
glong libbalsa_mailbox_get_unread_messages(LibBalsaMailbox * mailbox);
guint libbalsa_mailbox_get_first_unread(LibBalsaMailbox * mailbox);
LibBalsaCondition * libbalsa_mailbox_get_view_filter(LibBalsaMailbox * mailbox,
                                                     gboolean persistent);
GNode * libbalsa_mailbox_get_msg_tree(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_get_msg_tree_changed(LibBalsaMailbox * mailbox);
LibBalsaMailboxState libbalsa_mailbox_get_state(LibBalsaMailbox * mailbox);
LibBalsaMailboxIndexEntry *libbalsa_mailbox_get_index_entry(LibBalsaMailbox * mailbox,
                                                            guint msgno);
LibBalsaMailboxView * libbalsa_mailbox_get_view(LibBalsaMailbox * mailbox);
gint libbalsa_mailbox_get_stamp(LibBalsaMailbox * mailbox);
guint libbalsa_mailbox_get_open_ref(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_get_readonly(LibBalsaMailbox * mailbox);
const gchar * libbalsa_mailbox_get_config_prefix(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_get_has_unread_messages(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_get_messages_threaded(LibBalsaMailbox * mailbox);
gboolean libbalsa_mailbox_has_sort_pending(LibBalsaMailbox * mailbox);

/*
 * Setters
 */
void libbalsa_mailbox_clear_unread_messages(LibBalsaMailbox * mailbox);
void libbalsa_mailbox_set_filters(LibBalsaMailbox * mailbox, GSList * filters);
void libbalsa_mailbox_set_url(LibBalsaMailbox * mailbox, const gchar * url);
void libbalsa_mailbox_set_first_unread(LibBalsaMailbox * mailbox, guint first);
void libbalsa_mailbox_set_msg_tree_changed(LibBalsaMailbox * mailbox, gboolean changed);
void libbalsa_mailbox_set_readonly(LibBalsaMailbox * mailbox, gboolean readonly);
void libbalsa_mailbox_set_no_reassemble(LibBalsaMailbox * mailbox,
                                        gboolean no_reassemble);
void libbalsa_mailbox_set_name(LibBalsaMailbox * mailbox, const gchar * name);
void libbalsa_mailbox_set_view(LibBalsaMailbox * mailbox, LibBalsaMailboxView * view);
void libbalsa_mailbox_set_has_unread_messages(LibBalsaMailbox * mailbox,
                                              gboolean has_unread_messages);
void libbalsa_mailbox_set_messages_threaded(LibBalsaMailbox * mailbox,
                                          gboolean messages_threaded);
void libbalsa_mailbox_set_config_prefix(LibBalsaMailbox * mailbox,
                                        const gchar * config_prefix);

/*
 * Incrementers
 */
void libbalsa_mailbox_add_to_unread_messages(LibBalsaMailbox * mailbox, glong count);

#endif				/* __LIBBALSA_MAILBOX_H__ */
