/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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

#include "config.h"

#define _ISOC99_SOURCE 1
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libbalsa.h"
#include "libbalsa-marshal.h"
#include "message.h"
#include "misc.h"

#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 

#include "mailbox-filter.h"
#include "libbalsa_private.h"

#include "libbalsa-marshal.h"

/* Class functions */
static void libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass);
static void libbalsa_mailbox_init(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_dispose(GObject * object);
static void libbalsa_mailbox_finalize(GObject * object);

static void libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox
							   * mailbox,
							   gboolean flag);
static void libbalsa_mailbox_real_release_message (LibBalsaMailbox * mailbox,
						   LibBalsaMessage * message);
static gboolean libbalsa_mailbox_real_change_msgs_flags(LibBalsaMailbox *mbox,
                                                      GList *messages,
                                                      LibBalsaMessageFlag set,
                                                      LibBalsaMessageFlag clr);
static void libbalsa_mailbox_real_sort(LibBalsaMailbox* mbox,
                                       GArray *sort_array);
static gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox  *mailbox,
						LibBalsaCondition *condition);
static void libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static gboolean libbalsa_mailbox_real_close_backend (LibBalsaMailbox *
						     mailbox);

/* Callbacks */
static void messages_status_changed_cb(LibBalsaMailbox * mb,
				       GList * messages,
				       gint changed_flag);

/* SIGNALS MEANINGS :
   - MESSAGES_STATUS_CHANGED : notification signal sent by messages of the
   mailbox to tell that their status have changed.
   - CHANGED: notification signal sent by the mailbox to allow the
   frontend to keep in sync. This signal is used when messages are added
   to or removed from the mailbox. This is used when eg the mailbox
   loads new messages (check new mails) or the mailbox is expunged.
*/

enum {
    MESSAGES_STATUS_CHANGED,
    CHANGED,
    PROGRESS_NOTIFY,
    SET_UNREAD_MESSAGES_FLAG,
    LAST_SIGNAL
};

enum {
    ROW_CHANGED,
    ROW_DELETED,
    ROW_HAS_CHILD_TOGGLED,
    ROW_INSERTED,
    ROWS_REORDERED,
    LAST_MODEL_SIGNAL
};

static GObjectClass *parent_class = NULL;
static guint libbalsa_mailbox_signals[LAST_SIGNAL];
static guint libbalsa_mbox_model_signals[LAST_MODEL_SIGNAL];

/* GtkTreeModel function prototypes */
static void  mbox_model_init(GtkTreeModelIface *iface);

/* GtkTreeDragSource function prototypes */
static void  mbox_drag_source_init(GtkTreeDragSourceIface *iface);

/* GtkTreeSortable function prototypes */
static void  mbox_sortable_init(GtkTreeSortableIface *iface);

GType
libbalsa_mailbox_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailbox),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_init
	};

	static const GInterfaceInfo mbox_model_info = {
	    (GInterfaceInitFunc) mbox_model_init,
	    NULL,
	    NULL
	};
    
	static const GInterfaceInfo mbox_drag_source_info = {
	    (GInterfaceInitFunc) mbox_drag_source_init,
	    NULL,
	    NULL
	};

	static const GInterfaceInfo mbox_sortable_info = {
	    (GInterfaceInitFunc) mbox_sortable_init,
	    NULL,
	    NULL
	};
    
        mailbox_type =
            g_type_register_static(G_TYPE_OBJECT, "LibBalsaMailbox",
                                   &mailbox_info, 0);
	g_type_add_interface_static(mailbox_type,
				    GTK_TYPE_TREE_MODEL,
				    &mbox_model_info);
	g_type_add_interface_static(mailbox_type,
				    GTK_TYPE_TREE_DRAG_SOURCE,
				    &mbox_drag_source_info);
	g_type_add_interface_static(mailbox_type,
				    GTK_TYPE_TREE_SORTABLE,
				    &mbox_sortable_info);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    /* Notification signal thrown by a list of messages to indicate their
       owning mailbox that they have changed its state.
       The integer parameter indicates which flag has changed. */
    libbalsa_mailbox_signals[MESSAGES_STATUS_CHANGED] =
	g_signal_new("messages-status-changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     messages_status_changed),
                     NULL, NULL,
                     libbalsa_VOID__POINTER_INT, G_TYPE_NONE, 2,
                     G_TYPE_POINTER, G_TYPE_INT);

    /* This signal is emitted by the mailbox when new messages are
       retrieved (check mail or opening of the mailbox). This is used
       by GUI to sync on the mailbox content (see BalsaIndex)
    */   
    libbalsa_mailbox_signals[CHANGED] =
	g_signal_new("changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);


    libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG] =
	g_signal_new("set-unread-messages-flag",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     set_unread_messages_flag),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1,
                     G_TYPE_BOOLEAN);

    libbalsa_mailbox_signals[PROGRESS_NOTIFY] =
	g_signal_new("progress-notify",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     progress_notify),
                     NULL, NULL,
                     libbalsa_VOID__INT_INT_INT_STRING, G_TYPE_NONE,
                     4, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);

    object_class->dispose = libbalsa_mailbox_dispose;
    object_class->finalize = libbalsa_mailbox_finalize;

    klass->open_mailbox = NULL;
    klass->close_mailbox = NULL;
    klass->set_unread_messages_flag =
	libbalsa_mailbox_real_set_unread_messages_flag;
    klass->progress_notify = NULL;

    klass->changed = NULL;
    klass->messages_status_changed = messages_status_changed_cb;

    klass->get_message = NULL;
    klass->prepare_threading = NULL;
    klass->fetch_message_structure = NULL;
    klass->release_message = libbalsa_mailbox_real_release_message;
    klass->get_message_part = NULL;
    klass->get_message_stream = NULL;
    klass->change_message_flags = NULL;
    klass->change_msgs_flags    = libbalsa_mailbox_real_change_msgs_flags;
    klass->set_threading = NULL;
    klass->update_view_filter = NULL;
    klass->sort = libbalsa_mailbox_real_sort;
    klass->check = NULL;
    klass->message_match = NULL;
    klass->mailbox_match = NULL;
    klass->can_match = libbalsa_mailbox_real_can_match;
    klass->save_config  = libbalsa_mailbox_real_save_config;
    klass->load_config  = libbalsa_mailbox_real_load_config;
    klass->close_backend  = libbalsa_mailbox_real_close_backend;
    klass->total_messages = NULL;
}

static void
libbalsa_mailbox_init(LibBalsaMailbox * mailbox)
{
    mailbox->lock = FALSE;
    mailbox->is_directory = FALSE;

    mailbox->config_prefix = NULL;
    mailbox->name = NULL;
    mailbox->url = NULL;

    mailbox->open_ref = 0;
    mailbox->has_unread_messages = FALSE;
    mailbox->unread_messages = 0;

    mailbox->readonly = FALSE;
    mailbox->disconnected = FALSE;

    mailbox->filters=NULL;
    mailbox->view=NULL;
    /* mailbox->stamp is incremented before we use it, so it won't be
     * zero for a long, long time... */
    mailbox->stamp = g_random_int() / 2;
}

/*
 * libbalsa_mailbox_dispose:
 *
 * called just before finalize, when ref_count is about to be
 * decremented to 0
 */
static void
libbalsa_mailbox_dispose(GObject * object)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);

    while (mailbox->open_ref > 0)
	libbalsa_mailbox_close(mailbox);

    G_OBJECT_CLASS(parent_class)->dispose(object);
}

/* libbalsa_mailbox_finalize:
   destroys mailbox. Must leave it in sane state.
*/
static void
libbalsa_mailbox_finalize(GObject * object)
{
    LibBalsaMailbox *mailbox;
    g_return_if_fail(object != NULL);

    mailbox = LIBBALSA_MAILBOX(object);


    g_free(mailbox->config_prefix);
    mailbox->config_prefix = NULL;
    g_free(mailbox->name);
    mailbox->name = NULL;
    g_free(mailbox->url);
    mailbox->url = NULL;

    /* The LibBalsaMailboxView is owned by balsa_app.mailbox_views. */
    mailbox->view = NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/* Create a new mailbox by loading it from a config entry... */
LibBalsaMailbox *
libbalsa_mailbox_new_from_config(const gchar * prefix)
{
    gchar *type_str;
    GType type;
    gboolean got_default;
    LibBalsaMailbox *mailbox;

    gnome_config_push_prefix(prefix);
    type_str = gnome_config_get_string_with_default("Type", &got_default);

    if (got_default) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Cannot load mailbox %s"), prefix);
	gnome_config_pop_prefix();
	return NULL;
    }
    type = g_type_from_name(type_str);
    if (type == 0) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("No such mailbox type: %s"), type_str);
	g_free(type_str);
	gnome_config_pop_prefix();
	return NULL;
    }

    /* Handle Local mailboxes. 
     * They are now separate classes for each type 
     * FIXME: This should be removed in som efuture release.
     */
    if ( type == LIBBALSA_TYPE_MAILBOX_LOCAL ) {
	gchar *path = gnome_config_get_string("Path");
	type = libbalsa_mailbox_type_from_path(path);
	if (type != G_TYPE_OBJECT)
	    gnome_config_set_string("Type", g_type_name(type));
	else
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
		                 _("Bad local mailbox path \"%s\""), path);
    }
    mailbox = (type != G_TYPE_OBJECT ? g_object_new(type, NULL) : NULL);
    if (mailbox == NULL)
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("Could not create a mailbox of type %s"),
			     type_str);
    else 
	libbalsa_mailbox_load_config(mailbox, prefix);

    gnome_config_pop_prefix();
    g_free(type_str);

    return mailbox;
}

static void lbm_set_threading(LibBalsaMailbox * mailbox,
			      LibBalsaMailboxThreadingType thread_type);
gboolean
libbalsa_mailbox_open(LibBalsaMailbox * mailbox)
{
    gboolean retval;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);

    if (mailbox->open_ref > 0) {
	mailbox->open_ref++;
	retval = TRUE;
    } else {
	mailbox->open_ref++;
	mailbox->msg_tree = g_node_new(NULL);
	mailbox->stamp++;
	retval =
	    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->open_mailbox(mailbox);
	if (retval) {
	    if (mailbox->view)
		lbm_set_threading(mailbox, mailbox->view->threading_type);
	} else {
	    mailbox->open_ref = 0;
	    g_node_destroy(mailbox->msg_tree);
	    mailbox->msg_tree = NULL;
	}
    }

    UNLOCK_MAILBOX(mailbox);

    return retval;
}

/* libbalsa_mailbox_is_valid:
   mailbox is valid when:
   a). it is closed, b). it is open and has proper client context.
*/
gboolean
libbalsa_mailbox_is_valid(LibBalsaMailbox * mailbox)
{
    if(mailbox->open_ref == 0) return TRUE;
    if(MAILBOX_CLOSED(mailbox)) return FALSE;
    return TRUE;
}

gboolean
libbalsa_mailbox_is_open(LibBalsaMailbox *mailbox)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    

    return mailbox->open_ref>0; // this will break unlisted mailbox types
}
    
void
libbalsa_mailbox_close(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(MAILBOX_OPEN(mailbox));

    LOCK_MAILBOX(mailbox);

    if (--mailbox->open_ref == 0) {
	LIBBALSA_MAILBOX_GET_CLASS(mailbox)->close_mailbox(mailbox);
	g_node_destroy(mailbox->msg_tree);
	mailbox->msg_tree = NULL;
    }

    mailbox->stamp++;
    UNLOCK_MAILBOX(mailbox);
}

void
libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					  gboolean has_unread)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG],
		  0, has_unread);
}

/* libbalsa_mailbox_progress_notify:
   there has been a progress in current operation.
*/
void
libbalsa_mailbox_progress_notify(LibBalsaMailbox * mailbox,
                                 int type, int prog, int tot, const gchar* msg)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[PROGRESS_NOTIFY],
		  0, type, prog, tot, msg);
}

#define LIBBALSA_MAILBOX_COUNT "libbalsa-mailbox-count"

void
libbalsa_mailbox_check(LibBalsaMailbox * mailbox)
{
    guint added = 0;

    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    LOCK_MAILBOX(mailbox);

    g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_COUNT, &added);
    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->check(mailbox);
    g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_COUNT, NULL);
    if (added)
	lbm_set_threading(mailbox, mailbox->view->threading_type);

    UNLOCK_MAILBOX(mailbox);

#ifdef BALSA_USE_THREADS
    pthread_testcancel();
#endif
}

/* libbalsa_mailbox_message_match:
   Tests if the given message matches the conditions : this is used
   by the search code. It is a "virtual method", indeed IMAP has a
   special way to implement it for speed/bandwidth reasons
 */
gboolean
libbalsa_mailbox_message_match(LibBalsaMailbox * mailbox,
			       LibBalsaMessage * message,
			       LibBalsaCondition *condition)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->message_match(mailbox,
							      message,
							      condition);
}

/* libbalsa_mailbox_match:
   Compute the messages matching the filters.
   Virtual method : it is redefined by IMAP
 */
void
libbalsa_mailbox_match(LibBalsaMailbox * mailbox, GSList * filters_list)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->mailbox_match(mailbox,
						       filters_list);
}

gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox  *mailbox,
					 LibBalsaCondition *condition)
{
    /* By default : all filters is OK */
    return TRUE;
}

gboolean
libbalsa_mailbox_can_match(LibBalsaMailbox * mailbox,
                           LibBalsaCondition *condition)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->can_match(mailbox,
							  condition);
}

/* Helper function to run the "on reception" filters on a mailbox */

void
libbalsa_mailbox_run_filters_on_reception(LibBalsaMailbox * mailbox,
                                          GSList * filters)
{
    if (filters)
	g_warning(" %s accesses list of messages directly, reimplement", __func__);
#if 0
    GList * new_messages;
    gboolean free_filters = FALSE;

    g_return_if_fail(mailbox!=NULL);

    if (!filters) {
	if (!mailbox->filters)
	    config_mailbox_filters_load(mailbox);
	
	filters = libbalsa_mailbox_filters_when(mailbox->filters,
						FILTER_WHEN_INCOMING);
	free_filters = TRUE;
    }

    /* We apply filter if needed */
    if (filters) {
	LOCK_MAILBOX(mailbox);
	new_messages = libbalsa_extract_new_messages(mailbox->message_list);
	if (new_messages) {
	    if (filters_prepare_to_run(filters)) {
		libbalsa_filter_match(filters, new_messages, TRUE);
		UNLOCK_MAILBOX(mailbox);
		libbalsa_filter_apply(filters);
	    }
	    else UNLOCK_MAILBOX(mailbox);
	    g_list_free(new_messages);
	}
	else UNLOCK_MAILBOX(mailbox);
	if (free_filters)
	    g_slist_free(filters);
    }
#endif
}

void
libbalsa_mailbox_save_config(LibBalsaMailbox * mailbox,
			     const gchar * prefix)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    /* These are incase this section was used for another
     * type of mailbox that has now been deleted...
     */
    g_free(mailbox->config_prefix);
    mailbox->config_prefix = g_strdup(prefix);
    gnome_config_private_clean_section(prefix);
    gnome_config_clean_section(prefix);

    gnome_config_push_prefix(prefix);
    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->save_config(mailbox, prefix);
    gnome_config_pop_prefix();
}

void
libbalsa_mailbox_load_config(LibBalsaMailbox * mailbox,
			     const gchar * prefix)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gnome_config_push_prefix(prefix);
    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->load_config(mailbox, prefix);
    gnome_config_pop_prefix();
}

static void
libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean flag)
{
    mailbox->has_unread_messages = flag;
}

static void
libbalsa_mailbox_real_release_message(LibBalsaMailbox * mailbox,
				      LibBalsaMessage * message)
{
    /* Default is noop. */
}

/* libbalsa_mailbox_real_change_msgs_flags() default implementation uses
   non-transactional interface.
*/
static gboolean
libbalsa_mailbox_real_change_msgs_flags(LibBalsaMailbox *mbox,
                                        GList *messages,
                                        LibBalsaMessageFlag set,
                                        LibBalsaMessageFlag clr)
{
    while(messages) {
        LibBalsaMessage * message = LIBBALSA_MESSAGE(messages->data);
        libbalsa_mailbox_change_message_flags(mbox, message->msgno, set, clr);
        messages = g_list_next(messages);
    }
    return TRUE;
}

static gint mbox_compare_func(const SortTuple * a,
                              const SortTuple * b,
                              LibBalsaMailbox * mbox);

static void
libbalsa_mailbox_real_sort(LibBalsaMailbox* mbox, GArray *sort_array)
{
    /* Sort the array */
    g_array_sort_with_data(sort_array,
			   (GCompareDataFunc) mbox_compare_func, mbox);
}

static void
libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gnome_config_set_string("Type",
			    g_type_name(G_OBJECT_TYPE(mailbox)));
    gnome_config_set_string("Name", mailbox->name);
}

static void
libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_free(mailbox->config_prefix);
    mailbox->config_prefix = g_strdup(prefix);

    g_free(mailbox->name);
    mailbox->name = gnome_config_get_string("Name=Mailbox");
}

static gboolean
libbalsa_mailbox_real_close_backend(LibBalsaMailbox * mailbox)
{
    return TRUE;		/* Default is noop. */
}

GType
libbalsa_mailbox_type_from_path(const gchar * path)
/* libbalsa_get_mailbox_storage_type:
   returns one of LIBBALSA_TYPE_MAILBOX_IMAP,
   LIBBALSA_TYPE_MAILBOX_MAILDIR, LIBBALSA_TYPE_MAILBOX_MH,
   LIBBALSA_TYPE_MAILBOX_MBOX.  G_TYPE_OBJECT on error or a directory.
 */
{
    struct stat st;
    char tmp[_POSIX_PATH_MAX];

    if(strncmp(path, "imap://", 7) == 0)
        return LIBBALSA_TYPE_MAILBOX_IMAP;

    if (stat (path, &st) == -1)
        return G_TYPE_OBJECT;
    
    if (S_ISDIR (st.st_mode)) {
        /* check for maildir-style mailbox */
        snprintf (tmp, sizeof (tmp), "%s/cur", path);
        if (stat (tmp, &st) == 0 && S_ISDIR (st.st_mode))
            return LIBBALSA_TYPE_MAILBOX_MAILDIR;
    
        /* check for mh-style mailbox */
        snprintf (tmp, sizeof (tmp), "%s/.mh_sequences", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

        snprintf (tmp, sizeof (tmp), "%s/.xmhcache", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;
    
        snprintf (tmp, sizeof (tmp), "%s/.mew_cache", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

        snprintf (tmp, sizeof (tmp), "%s/.mew-cache", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

        /* 
         * ok, this isn't an mh folder, but mh mode can be used to read
         * Usenet news from the spool. ;-) 
         */

        snprintf (tmp, sizeof (tmp), "%s/.overview", path);
        if (access (tmp, F_OK) == 0)
            return LIBBALSA_TYPE_MAILBOX_MH;

    }
    else return LIBBALSA_TYPE_MAILBOX_MBOX;

    /* This is not a mailbox */
    return G_TYPE_OBJECT;
}

/* Each of the next three methods emits a signal that will be caught by
 * a GtkTreeView, so the emission must be made holding the gdk lock.
 *
 * We assume that a main-thread call is already locked, but that a
 * subthread call is unlocked and needs to be locked; we try to check
 * those assumptions, to help track down instances when they fail.
 */

#ifdef BALSA_USE_THREADS
static gboolean
lbm_threads_enter(void)
{
    gboolean unlock = g_mutex_trylock(gdk_threads_mutex);
    gboolean is_main_thread =
	(pthread_self() == libbalsa_get_main_thread());

    if (is_main_thread) {
	if (unlock)
	    g_warning("Main thread was unlocked");
    } else {
	if (!unlock) {
#ifdef DEBUG
	    g_print("Sub thread must wait for lock...");
	    gdk_threads_enter();
	    g_print("got it!\n");
#else
	    gdk_threads_enter();
#endif
	    unlock = TRUE;
	}
    }

    return unlock;
}

static void
lbm_threads_leave(gboolean unlock)
{
    if (unlock) {
	gdk_flush();
	gdk_threads_leave();
    }
}
#else
#define lbm_threads_enter() FALSE
#define lbm_threads_leave(unlock)
#endif

void
libbalsa_mailbox_msgno_changed(LibBalsaMailbox * mailbox, guint seqno)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    gboolean unlock;

    unlock = lbm_threads_enter();

    iter.user_data = g_node_find(mailbox->msg_tree, G_PRE_ORDER,
				 G_TRAVERSE_ALL, GUINT_TO_POINTER(seqno));
    g_assert(iter.user_data != NULL);

    iter.stamp = mailbox->stamp;
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_CHANGED], 0,
		  path, &iter);
    gtk_tree_path_free(path);

    lbm_threads_leave(unlock);
}

void
libbalsa_mailbox_msgno_inserted(LibBalsaMailbox *mailbox, guint seqno)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    gboolean unlock;
    guint *added;

    unlock = lbm_threads_enter();

    /* Insert node into the message tree before getting path. */
    iter.user_data = g_node_new(GUINT_TO_POINTER(seqno));
    iter.stamp = mailbox->stamp;
    g_node_append(mailbox->msg_tree, iter.user_data);

    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_INSERTED], 0,
		  path, &iter);
    mailbox->stamp++;
    gtk_tree_path_free(path);

    lbm_threads_leave(unlock);

    added = g_object_get_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_COUNT);
    if (added)
	++*added;
}

void
libbalsa_mailbox_msgno_filt_in(LibBalsaMailbox *mailbox, guint seqno)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    gboolean unlock;

    unlock = lbm_threads_enter();

    /* Insert node into the message tree before getting path. */
    iter.user_data = g_node_new(GUINT_TO_POINTER(seqno));
    iter.stamp = mailbox->stamp;
    g_node_prepend(mailbox->msg_tree, iter.user_data);

    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_INSERTED], 0,
		  path, &iter);
    gtk_tree_path_free(path);

    lbm_threads_leave(unlock);
}

struct remove_data { unsigned seqno; GNode *node; };
static gboolean
decrease_post(GNode *node, gpointer data)
{
    struct remove_data *dt = (struct remove_data*)data;
    unsigned seqno = GPOINTER_TO_UINT(node->data);
    if(seqno == dt->seqno) 
        dt->node = node;
    else if(seqno>dt->seqno)
        node->data = GUINT_TO_POINTER(seqno-1);
    return FALSE;
}

void
libbalsa_mailbox_msgno_removed(LibBalsaMailbox * mailbox, guint seqno)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    struct remove_data dt;
    gboolean unlock;
    GNode *child;
    GNode *parent;

    unlock = lbm_threads_enter();

    dt.seqno = seqno;
    dt.node = NULL;

    g_node_traverse(mailbox->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
                    decrease_post, &dt);
    if (!dt.node) {
	g_warning("msgno %d not found", seqno);
	lbm_threads_leave(unlock);
	return;
    }

    iter.user_data = dt.node;
    iter.stamp = mailbox->stamp;
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);

    /* First promote any children to the node's parent; we'll insert
     * them all before the current node, to keep the path calculation
     * simple. */
    parent = dt.node->parent;
    while ((child = dt.node->children)) {
	guint *promoted;
	/* No need to notify the tree-view about unlinking the child--it
	 * will assume we already did that when we notify it about
	 * destroying the parent. */
	g_node_unlink(child);
	g_node_insert_before(parent, dt.node, child);

	/* Notify the tree-view about the new location of the child. */
	iter.user_data = child;
	g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_INSERTED], 0,
		      path, &iter);
	if (child->children)
	    g_signal_emit(mailbox,
			  libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
			  0, path, &iter);
	gtk_tree_path_next(path);

	promoted =
	    g_object_get_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_COUNT);
	if (promoted)
	    ++*promoted;
    }

    /* Now it's safe to destroy the node. */
    g_node_destroy(dt.node);
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_DELETED], 0, path);

    if (parent->parent && !parent->children) {
	gtk_tree_path_up(path);
	iter.user_data = parent;
	g_signal_emit(mailbox,
		      libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED], 0,
		      path, &iter);
    }
    
    gtk_tree_path_free(path);
    mailbox->stamp++;

    lbm_threads_leave(unlock);
}

void
libbalsa_mailbox_msgno_filt_out(LibBalsaMailbox * mailbox, guint seqno)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    gboolean unlock;
    GNode *child, *parent, *node;

    unlock = lbm_threads_enter();

    node = g_node_find(mailbox->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL, 
                       GUINT_TO_POINTER(seqno));
    if (!node) {
	g_warning("msgno %d not found", seqno);
	lbm_threads_leave(unlock);
	return;
    }

    iter.user_data = node;
    iter.stamp = mailbox->stamp;
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &iter);

    /* First promote any children to the node's parent; we'll insert
     * them all before the current node, to keep the path calculation
     * simple. */
    parent = node->parent;
    while ((child = node->children)) {
	/* No need to notify the tree-view about unlinking the child--it
	 * will assume we already did that when we notify it about
	 * destroying the parent. */
	g_node_unlink(child);
	g_node_insert_before(parent, node, child);

	/* Notify the tree-view about the new location of the child. */
	iter.user_data = child;
	g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_INSERTED], 0,
		      path, &iter);
	if (child->children)
	    g_signal_emit(mailbox,
			  libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
			  0, path, &iter);
	gtk_tree_path_next(path);
    }

    /* Now it's safe to destroy the node. */
    g_node_destroy(node);
    g_signal_emit(mailbox, libbalsa_mbox_model_signals[ROW_DELETED], 0, path);

    if (parent->parent && !parent->children) {
	gtk_tree_path_up(path);
	iter.user_data = parent;
	g_signal_emit(mailbox,
		      libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED], 0,
		      path, &iter);
    }
    
    gtk_tree_path_free(path);
    mailbox->stamp++;

    lbm_threads_leave(unlock);
}

gboolean
libbalsa_mailbox_find(LibBalsaMailbox *mailbox, GtkTreeIter *pos,
                      gboolean search_forward,
                      LibBalsaCondition *condition,
                      GList *exclude)
{
    /* finds first message matching the condition. */
    return FALSE;
}
/* Find a message in the tree-model, by its message number. */
gboolean
libbalsa_mailbox_msgno_find(LibBalsaMailbox * mailbox, guint seqno,
			    GtkTreePath ** path, GtkTreeIter * iter)
{
    GtkTreeIter tmp_iter;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    tmp_iter.user_data =
	g_node_find(mailbox->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL,
		    GINT_TO_POINTER(seqno));

    if (!tmp_iter.user_data)
	return FALSE;

    tmp_iter.stamp = mailbox->stamp;

    if (path)
	*path =
	    gtk_tree_model_get_path(GTK_TREE_MODEL(mailbox), &tmp_iter);
    if (iter)
	*iter = tmp_iter;

    return TRUE;
}

/* Callback for the "messages-status-changed" signal.
 * mb:          the mailbox--must not be NULL;
 * messages:    the list of messages--must not be NULL;
 * flag:        the flag that changed.
 */
static void
messages_status_changed_cb(LibBalsaMailbox * mb, GList * messages,
                           gint flag)
{
    gint new_in_list = 0;
    gint nb_in_list = 0;
    gboolean new_state;
    GList *lst;

    switch (flag) {
    case LIBBALSA_MESSAGE_FLAG_DELETED:
        /* Deleted state has changed, update counts */
        new_state = (LIBBALSA_MESSAGE_IS_DELETED(messages->data));

        for (lst = messages; lst; lst = g_list_next(lst)) {
            nb_in_list++;
	    if (LIBBALSA_MESSAGE_IS_UNREAD(lst->data))
                new_in_list++;
        }

        if (new_state) {
            /* messages have been deleted */
            if (new_in_list) {
                mb->unread_messages -= new_in_list;
                if (mb->unread_messages <= 0)
                    libbalsa_mailbox_set_unread_messages_flag(mb, FALSE);
            }
        } else {
            /* message has been undeleted */
            if (new_in_list) {
                mb->unread_messages += new_in_list;
                libbalsa_mailbox_set_unread_messages_flag(mb, TRUE);
            }
        }
        break;
    case LIBBALSA_MESSAGE_FLAG_NEW:
	if (LIBBALSA_MESSAGE_IS_UNREAD(messages->data)) {
            gboolean unread_before = mb->unread_messages > 0;

            /* Count only messages with the deleted flag not set */
            for (lst = messages; lst; lst = g_list_next(lst))
		if (!LIBBALSA_MESSAGE_IS_DELETED(lst->data))
                    mb->unread_messages++;
            if (!unread_before && mb->unread_messages > 0)
                libbalsa_mailbox_set_unread_messages_flag(mb, TRUE);
        } else {
            /* Count only messages with the deleted flag not set */
            for (lst = messages; lst; lst = g_list_next(lst))
		if (!LIBBALSA_MESSAGE_IS_DELETED(lst->data))
                    mb->unread_messages--;
            if (mb->unread_messages <= 0)
                libbalsa_mailbox_set_unread_messages_flag(mb, FALSE);
        }
    case LIBBALSA_MESSAGE_FLAG_REPLIED:
    case LIBBALSA_MESSAGE_FLAG_FLAGGED:
        break;
    }

    LOCK_MAILBOX(mb);
    for (lst = messages; lst; lst = lst->next) {
	LibBalsaMessage *msg = LIBBALSA_MESSAGE(lst->data);
	libbalsa_message_set_icons(msg);
	libbalsa_mailbox_msgno_changed(mb, msg->msgno);
    }
    UNLOCK_MAILBOX(mb);
}

int
libbalsa_mailbox_copy_message(LibBalsaMessage * message,
			      LibBalsaMailbox * dest)
{
    int retval;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), -1);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(dest), -1);

    LOCK_MAILBOX_RETURN_VAL(dest, -1);
    g_object_ref(message);	/* Should we also lock its mailbox? */

    retval = LIBBALSA_MAILBOX_GET_CLASS(dest)->add_message(dest, message);
    if (retval > 0 && !LIBBALSA_MESSAGE_IS_DELETED(message)
	&& LIBBALSA_MESSAGE_IS_UNREAD(message))
	dest->has_unread_messages = TRUE;

    g_object_unref(message);
    UNLOCK_MAILBOX(dest);

    return retval;
}

void libbalsa_mailbox_messages_status_changed(LibBalsaMailbox * mbox,
					      GList * messages,
					      gint flag)
{
    g_return_if_fail(mbox && messages);

    g_signal_emit(G_OBJECT(mbox),
		  libbalsa_mailbox_signals[MESSAGES_STATUS_CHANGED], 0,
		  messages, flag);
}

gboolean
libbalsa_mailbox_close_backend(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->close_backend(mailbox);
}

guint
libbalsa_mailbox_total_messages(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(mailbox != NULL, 0);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), 0);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->total_messages(mailbox);
}

gboolean
libbalsa_mailbox_sync_storage(LibBalsaMailbox * mailbox, gboolean expunge)
{
    gboolean retval = TRUE;

    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(!mailbox->readonly, TRUE);

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);

    /* When called in an idle handler, the mailbox might have been
     * closed, so we must check (with the mailbox locked). */
    if (MAILBOX_OPEN(mailbox)) {
	guint promoted = 0;

	g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_COUNT,
			  &promoted);
	retval =
	    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->sync(mailbox, expunge);
	g_object_set_data(G_OBJECT(mailbox), LIBBALSA_MAILBOX_COUNT, NULL);
	if (promoted)
	    lbm_set_threading(mailbox, mailbox->view->threading_type);
	else
	    g_signal_emit(G_OBJECT(mailbox),
			  libbalsa_mailbox_signals[CHANGED], 0);
    }

    UNLOCK_MAILBOX(mailbox);

    return retval;
}

LibBalsaMessage *
libbalsa_mailbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    g_return_val_if_fail(mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(msgno > 0 && msgno <=
			 libbalsa_mailbox_total_messages(mailbox), NULL);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message(mailbox,
							    msgno);
}

void
libbalsa_mailbox_prepare_threading(LibBalsaMailbox *mailbox, 
				   guint lo, guint hi)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(lo > 0);
    g_return_if_fail(hi > 0);

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)
        ->prepare_threading(mailbox, lo, hi);
}

void
libbalsa_mailbox_fetch_message_structure(LibBalsaMailbox *mailbox,
					 LibBalsaMessage *message,
					 LibBalsaFetchFlag flags)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(message != NULL);

    if(message->body_list) /* already fetched no need to refetch it */
        return;
    LIBBALSA_MAILBOX_GET_CLASS(mailbox)
        ->fetch_message_structure(mailbox, message, flags);
}

void
libbalsa_mailbox_release_message(LibBalsaMailbox * mailbox,
				 LibBalsaMessage * message)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(mailbox == message->mailbox);

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)
	->release_message(mailbox, message);
}

const gchar*
libbalsa_mailbox_get_message_part(LibBalsaMessage    *message,
				  LibBalsaMessageBody *part,
                                  ssize_t *sz)
{
    g_return_val_if_fail(message != NULL, NULL);
    g_return_val_if_fail(message->mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(message->mailbox), NULL);
    g_return_val_if_fail(part != NULL, NULL);

    return LIBBALSA_MAILBOX_GET_CLASS(message->mailbox)
        ->get_message_part(message, part, sz);
}

GMimeStream *
libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
				    LibBalsaMessage * message)
{
    GMimeStream *mime_stream;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    g_return_val_if_fail(mailbox != NULL || message->mime_msg != NULL,
			 NULL);

    if (mailbox)
	mime_stream = LIBBALSA_MAILBOX_GET_CLASS(mailbox)->
	    get_message_stream(mailbox, message);
    else {
	mime_stream = g_mime_stream_mem_new();
	g_mime_message_write_to_stream(message->mime_msg, mime_stream);
	g_mime_stream_reset(mime_stream);
    }

    return mime_stream;
}

void
libbalsa_mailbox_change_message_flags(LibBalsaMailbox * mailbox,
				      guint msgno, LibBalsaMessageFlag set,
				      LibBalsaMessageFlag clear)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));
    g_return_if_fail(msgno > 0);

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->change_message_flags(mailbox,
							      msgno, set,
							      clear);
    libbalsa_mailbox_msgno_changed(mailbox, msgno);
}

/* libbalsa_mailbox_change_msgs_flags() changes stored message flags
   and is to be used only internally by libbalsa and it assumes that
   the mailbox is already locked.  FIXME: pass a list of msgno's
   instead of messages: we do not need that much information to flip
   just the message flag (think of "Select All+Toggle Deleted").
   Returns TRUE on success, FALSE on failure.
*/
gboolean
libbalsa_mailbox_change_msgs_flags(LibBalsaMailbox * mailbox,
                                   GList *messages,
                                   LibBalsaMessageFlag set,
                                   LibBalsaMessageFlag clear)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    gboolean res = LIBBALSA_MAILBOX_GET_CLASS(mailbox)
        ->change_msgs_flags(mailbox, messages, set, clear);

    while(messages) {
        LibBalsaMessage *msg = LIBBALSA_MESSAGE(messages->data);
        msg->flags |= set;
        msg->flags &= ~clear;
        libbalsa_mailbox_msgno_changed(mailbox, msg->msgno);
        messages = g_list_next(messages);
    }
    return res;
}

/*
 * Mailbox views.
 *
 * NOTE: call to mailbox_filter_view MUST be followed by a call to
 * libbalsa_mailbox_set_threading that will actually create the
 * message tree.
 */
void
libbalsa_mailbox_set_view_filter(LibBalsaMailbox *mailbox,
                                 LibBalsaCondition *cond,
                                 gboolean update_immediately)
{
    if(update_immediately) {
        LIBBALSA_MAILBOX_GET_CLASS(mailbox)->update_view_filter(mailbox,
                                                                cond);
	lbm_set_threading(mailbox, mailbox->view->threading_type);
    } else {
        if(mailbox->view_filter)
            libbalsa_condition_free(mailbox->view_filter);
        mailbox->view_filter = cond;
    }
}

static void mbox_sort_helper(LibBalsaMailbox * mbox, GNode * parent);
static void
lbm_set_threading(LibBalsaMailbox * mailbox,
		  LibBalsaMailboxThreadingType thread_type)
{
    gboolean unlock;

    if (!mailbox->msg_tree)
	return;

    unlock = lbm_threads_enter();

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->set_threading(mailbox,
						       thread_type);
    if (mailbox->msg_tree->children)
	mbox_sort_helper(mailbox, mailbox->msg_tree);

    /* invalidate iterators */
    mailbox->stamp++;
    g_signal_emit(G_OBJECT(mailbox), libbalsa_mailbox_signals[CHANGED], 0);

    lbm_threads_leave(unlock);
}

void
libbalsa_mailbox_set_threading(LibBalsaMailbox *mailbox,
			       LibBalsaMailboxThreadingType thread_type)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    LOCK_MAILBOX(mailbox);
    lbm_set_threading(mailbox, thread_type);
    UNLOCK_MAILBOX(mailbox);
}

LibBalsaMailboxView *
libbalsa_mailbox_view_new(void)
{
    LibBalsaMailboxView *view = g_new(LibBalsaMailboxView, 1);

    view->mailing_list_address = NULL;
    view->identity_name=NULL;
    view->threading_type = LB_MAILBOX_THREADING_FLAT;
    view->sort_type =  LB_MAILBOX_SORT_TYPE_ASC;
    view->sort_field = LB_MAILBOX_SORT_DATE;
    view->show = LB_MAILBOX_SHOW_UNSET;
    view->exposed = FALSE;
    view->open = FALSE;

    return view;
}

void
libbalsa_mailbox_view_free(LibBalsaMailboxView * view)
{
    if (view->mailing_list_address)
        g_object_unref(view->mailing_list_address);
    g_free(view->identity_name);
    g_free(view);
}


/* =================================================================== *
 * GtkTreeModel implementation functions.                              *
 * Important:
 * do not forget to modify LibBalsaMailbox::stamp on each modification
 * of the message list.
 * =================================================================== */
#define VALID_ITER(iter, tree_model) \
    ((iter)!= NULL && \
     (iter)->user_data != NULL && \
     LIBBALSA_IS_MAILBOX(tree_model) && \
     ((LibBalsaMailbox *) tree_model)->stamp == (iter)->stamp)
#define VALIDATE_ITER(iter, tree_model) \
    ((iter)->stamp = ((LibBalsaMailbox *) tree_model)->stamp)
#define INVALIDATE_ITER(iter) ((iter)->stamp = 0)

static GtkTreeModelFlags mbox_model_get_flags  (GtkTreeModel      *tree_model);
static gint         mbox_model_get_n_columns   (GtkTreeModel      *tree_model);
static GType        mbox_model_get_column_type (GtkTreeModel      *tree_model,
						gint               index);
static gboolean     mbox_model_get_iter        (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter,
						GtkTreePath       *path);
static GtkTreePath *mbox_model_get_path        (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter);
static void         mbox_model_get_value       (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter,
						gint               column,
						GValue            *value);
static gboolean     mbox_model_iter_next       (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter);
static gboolean     mbox_model_iter_children   (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter,
						GtkTreeIter       *parent);
static gboolean     mbox_model_iter_has_child  (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter);
static gint         mbox_model_iter_n_children (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter);
static gboolean     mbox_model_iter_nth_child  (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter,
						GtkTreeIter       *parent,
						gint               n);
static gboolean     mbox_model_iter_parent     (GtkTreeModel      *tree_model,
						GtkTreeIter       *iter,
						GtkTreeIter       *child);


static GType mbox_model_col_type[LB_MBOX_N_COLS];

static void
mbox_model_init(GtkTreeModelIface *iface)
{
    iface->get_flags       = mbox_model_get_flags;
    iface->get_n_columns   = mbox_model_get_n_columns;
    iface->get_column_type = mbox_model_get_column_type;
    iface->get_iter        = mbox_model_get_iter;
    iface->get_path        = mbox_model_get_path;
    iface->get_value       = mbox_model_get_value;
    iface->iter_next       = mbox_model_iter_next;
    iface->iter_children   = mbox_model_iter_children;
    iface->iter_has_child  = mbox_model_iter_has_child;
    iface->iter_n_children = mbox_model_iter_n_children;
    iface->iter_nth_child  = mbox_model_iter_nth_child;
    iface->iter_parent     = mbox_model_iter_parent;

    mbox_model_col_type[LB_MBOX_MSGNO_COL]   = G_TYPE_UINT;
    mbox_model_col_type[LB_MBOX_MARKED_COL]  = GDK_TYPE_PIXBUF;
    mbox_model_col_type[LB_MBOX_ATTACH_COL]  = GDK_TYPE_PIXBUF;
    mbox_model_col_type[LB_MBOX_FROM_COL]    = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_SUBJECT_COL] = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_DATE_COL]    = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_SIZE_COL]    = G_TYPE_STRING;
    mbox_model_col_type[LB_MBOX_WEIGHT_COL]  = G_TYPE_UINT;
    mbox_model_col_type[LB_MBOX_MESSAGE_COL] = G_TYPE_POINTER;

    libbalsa_mbox_model_signals[ROW_CHANGED] =
	g_signal_lookup("row-changed",		 GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROW_DELETED] =
	g_signal_lookup("row-deleted",		 GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED] =
	g_signal_lookup("row-has-child-toggled", GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROW_INSERTED] =
	g_signal_lookup("row-inserted",		 GTK_TYPE_TREE_MODEL);
    libbalsa_mbox_model_signals[ROWS_REORDERED] =
	g_signal_lookup("rows-reordered",	 GTK_TYPE_TREE_MODEL);
}

static GtkTreeModelFlags
mbox_model_get_flags(GtkTreeModel *tree_model)
{
    return 0;
}

static gint
mbox_model_get_n_columns(GtkTreeModel *tree_model)
{
    return LB_MBOX_N_COLS;
}

static GType
mbox_model_get_column_type(GtkTreeModel *tree_model, gint index)
{
    g_return_val_if_fail(index>=0 && index <LB_MBOX_N_COLS, G_TYPE_BOOLEAN);
    return mbox_model_col_type[index];
}

static gboolean
mbox_model_get_iter(GtkTreeModel *tree_model,
		    GtkTreeIter  *iter,
		    GtkTreePath  *path)
{
    GtkTreeIter parent;
    const gint *indices;
    gint depth, i;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(tree_model), FALSE);

    indices = gtk_tree_path_get_indices(path);
    depth = gtk_tree_path_get_depth(path);

    g_return_val_if_fail(depth > 0, FALSE);

    if (!mbox_model_iter_nth_child(tree_model, iter, NULL, indices[0]))
	return FALSE;

    for (i = 1; i < depth; i++) {
	parent = *iter;
	if (!mbox_model_iter_nth_child(tree_model, iter, &parent,
				       indices[i]))
	    return FALSE;
    }

    return TRUE;
}

static GtkTreePath *
mbox_model_get_path_helper(GNode * node, GNode * msg_tree)
{
    GtkTreePath *path;
    gint i;

    if (!node->parent)
	return node == msg_tree ? gtk_tree_path_new() : NULL;

    path = mbox_model_get_path_helper(node->parent, msg_tree);
    if (!path)
	return NULL;

    i = g_node_child_position(node->parent, node);
    if (i < 0) {
	gtk_tree_path_free(path);
	return NULL;
    }

    gtk_tree_path_append_index(path, i);

    return path;
}

static GtkTreePath *
mbox_model_get_path(GtkTreeModel * tree_model, GtkTreeIter * iter)
{
    GNode *node, *parent_node;

    g_return_val_if_fail(VALID_ITER(iter, tree_model), NULL);

    node = iter->user_data;
#define SANITY_CHECK
#ifdef SANITY_CHECK
    for (parent_node = node->parent; parent_node;
	 parent_node = parent_node->parent)
	g_return_val_if_fail(parent_node != node, NULL);
#endif

    g_return_val_if_fail(node->parent != NULL, NULL);

    return mbox_model_get_path_helper(node,
				      LIBBALSA_MAILBOX(tree_model)->
				      msg_tree);
}

/* mbox_model_get_value: 
  FIXME: still includes some debugging code in case fetching the
  message failed.
*/
static gchar*
get_from_field(LibBalsaMessage *message)
{
    gboolean append_dots = FALSE;
    const gchar *name_str = NULL;
    gchar *from;
    LibBalsaAddress *addy = NULL;

    g_return_val_if_fail(message->mailbox, NULL);
    if (message->mailbox->view->show == LB_MAILBOX_SHOW_TO) {
	if (message->headers && message->headers->to_list) {
	    GList *list = g_list_first(message->headers->to_list);
	    addy = list->data;
	    append_dots = list->next != NULL;
	}
    } else {
 	if (message->headers && message->headers->from)
	    addy = message->headers->from;
    }
    if (addy)
	name_str = libbalsa_address_get_name(addy);
    if(!name_str)           /* !addy, or addy contained no name/address */
	name_str = "";
    
    from = append_dots ? g_strconcat(name_str, ",...", NULL)
                       : g_strdup(name_str);
    libbalsa_utf8_sanitize(&from, TRUE, NULL);
    return from;
}

static GdkPixbuf *status_icons[LIBBALSA_MESSAGE_STATUS_ICONS_NUM];
static GdkPixbuf *attach_icons[LIBBALSA_MESSAGE_ATTACH_ICONS_NUM];

static void
mbox_model_get_value(GtkTreeModel *tree_model,
		     GtkTreeIter  *iter,
		     gint column,
		     GValue *value)
{
    LibBalsaMailbox* lmm = LIBBALSA_MAILBOX(tree_model);
    LibBalsaMessage* msg;
    guint msgno;
    gchar *tmp;
    
    g_return_if_fail(VALID_ITER(iter, tree_model));
    g_return_if_fail(column >= 0 &&
		     column < (int) ELEMENTS(mbox_model_col_type));
 
    msgno = GPOINTER_TO_UINT( ((GNode*)iter->user_data)->data );
#ifdef GTK2_FETCHES_ONLY_VISIBLE_CELLS
    msg = libbalsa_mailbox_get_message(lmm, msgno);
#else 
    { GdkRectangle a, b, c, d; 
    /* assumed that only one view is showing the mailbox */
    GtkTreeView *tree = g_object_get_data(G_OBJECT(tree_model), "tree-view");
    GtkTreePath *path = gtk_tree_model_get_path(tree_model, iter);
    GtkTreeViewColumn *col =
	gtk_tree_view_get_column(tree, (column == LB_MBOX_WEIGHT_COL
					? LB_MBOX_FROM_COL : column));
    gtk_tree_view_get_visible_rect(tree, &a);
    gtk_tree_view_get_cell_area(tree, path, col, &b);
    gtk_tree_view_widget_to_tree_coords(tree, b.x, b.y, &c.x, &c.y);
    gtk_tree_view_widget_to_tree_coords(tree, b.x+b.width, b.y+b.height, 
					&c.width, &c.height);
    c.width -= c.x; c.height -= c.y;
    if(gdk_rectangle_intersect(&a, &c, &d) || column == LB_MBOX_MESSAGE_COL) 
	msg = libbalsa_mailbox_get_message(lmm, msgno);
    else { 
	msg = NULL; 
    }
    gtk_tree_path_free(path);
    }
#endif
    g_value_init (value, mbox_model_col_type[column]);
    switch(column) {
    case LB_MBOX_MSGNO_COL:
	g_value_set_uint(value, msgno);  break;
    case LB_MBOX_MARKED_COL:
	if (!msg || msg->status_icon >= LIBBALSA_MESSAGE_STATUS_ICONS_NUM)
	    g_value_set_object(value, NULL);
	else
	    g_value_set_object(value, status_icons[msg->status_icon]);
	break;
    case LB_MBOX_ATTACH_COL:
	if (!msg || msg->attach_icon >= LIBBALSA_MESSAGE_ATTACH_ICONS_NUM)
	    g_value_set_object(value, NULL);
	else
	    g_value_set_object(value, attach_icons[msg->attach_icon]);
	break;
    case LB_MBOX_FROM_COL:
	if(msg) {
	    tmp = get_from_field(msg);
	    g_value_set_string_take_ownership(value, tmp);
	} else g_value_set_string(value, "from unknown");
        break;
    case LB_MBOX_SUBJECT_COL:
	g_value_set_string(value, msg 
			   ? LIBBALSA_MESSAGE_GET_SUBJECT(msg)
			   : "unknown subject");
	    
	break;
    case LB_MBOX_DATE_COL:
	if(msg) {
	    tmp = libbalsa_message_date_to_gchar(msg, "%x %X");
	    g_value_set_string_take_ownership(value, tmp);
	} else g_value_set_string(value, "unknown");
	break;
    case LB_MBOX_SIZE_COL:
	if(msg) {
	    tmp = libbalsa_message_size_to_gchar(msg, FALSE);
	    g_value_set_string_take_ownership(value, tmp);
	} else g_value_set_string(value, "unknown");
	break;
    case LB_MBOX_MESSAGE_COL:
	g_value_set_pointer(value, msg); break;
    case LB_MBOX_WEIGHT_COL:
	g_value_set_uint(value,
			 (msg && !LIBBALSA_MESSAGE_IS_DELETED(msg)
			  && LIBBALSA_MESSAGE_IS_UNREAD(msg)) ?
			 PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	break;
    }
}

static gboolean
mbox_model_iter_next(GtkTreeModel      *tree_model,
		     GtkTreeIter       *iter)
{
    GNode *node;
    g_return_val_if_fail(VALID_ITER(iter, tree_model), FALSE);

    node = iter->user_data;
    if(node && (node = node->next)) {
	iter->user_data = node;
	VALIDATE_ITER(iter, tree_model);
	return TRUE;
    } else {
	INVALIDATE_ITER(iter);
	return FALSE;
    }
}

static gboolean
mbox_model_iter_children(GtkTreeModel      *tree_model,
			 GtkTreeIter       *iter,
			 GtkTreeIter       *parent)
{
    GNode *node;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(parent == NULL ||
			 VALID_ITER(parent, tree_model), FALSE);

    node = parent ? parent->user_data
		  : LIBBALSA_MAILBOX(tree_model)->msg_tree;
    node = node->children;
    if (node) {
	iter->user_data = node;
	VALIDATE_ITER(iter, tree_model);
	return TRUE;
    } else
	return FALSE;
}

static gboolean
mbox_model_iter_has_child(GtkTreeModel	* tree_model,
			  GtkTreeIter	* iter)
{
    GNode *node;

    g_return_val_if_fail(VALID_ITER(iter, LIBBALSA_MAILBOX(tree_model)),
			 FALSE);

    node = iter->user_data;

    return (node->children != NULL);
}

static gint
mbox_model_iter_n_children(GtkTreeModel      *tree_model,
			   GtkTreeIter       *iter)
{
    GNode *node;

    g_return_val_if_fail(iter == NULL || VALID_ITER(iter, tree_model), 0);

    node = iter ? iter->user_data
		: LIBBALSA_MAILBOX(tree_model)->msg_tree;

    return g_node_n_children(node);
}

static gboolean
mbox_model_iter_nth_child(GtkTreeModel	* tree_model,
			  GtkTreeIter	* iter,
			  GtkTreeIter	* parent,
			  gint		  n)
{
    GNode *node;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(parent == NULL
			 || VALID_ITER(parent, tree_model), FALSE);

    node = parent ? parent->user_data
		  : LIBBALSA_MAILBOX(tree_model)->msg_tree;
    if(!node) /* the tree has been destroyed already (mailbox has been
               * closed), there is nothing to iterate over. This happens
               * only if mailbox is closed but a view is still active. 
               */
        return FALSE;
    node = g_node_nth_child(node, n);

    if (node) {
	iter->user_data = node;
	VALIDATE_ITER(iter, tree_model);
	return TRUE;
    } else
	return FALSE;
}

static gboolean
mbox_model_iter_parent(GtkTreeModel	* tree_model,
		       GtkTreeIter	* iter,
		       GtkTreeIter	* child)
{
    GNode *node;

    INVALIDATE_ITER(iter);
    g_return_val_if_fail(iter != NULL, FALSE);
    g_return_val_if_fail(VALID_ITER(child, tree_model), FALSE);

    node = child->user_data;
    node = node->parent;
    if (node && node != LIBBALSA_MAILBOX(tree_model)->msg_tree) {
	iter->user_data = node;
	VALIDATE_ITER(iter, tree_model);
	return TRUE;
    } else
	return FALSE;
}

/* Set icons used in tree view. */
static void
libbalsa_mailbox_set_icon(GdkPixbuf * pixbuf, GdkPixbuf ** pixbuf_store)
{
    if (*pixbuf_store)
	g_object_unref(*pixbuf_store);
    *pixbuf_store = pixbuf;
}

/* Icons for status column. */
void
libbalsa_mailbox_set_unread_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &status_icons
			      [LIBBALSA_MESSAGE_STATUS_UNREAD]);
}

void libbalsa_mailbox_set_trash_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &status_icons
			      [LIBBALSA_MESSAGE_STATUS_DELETED]);
}

void libbalsa_mailbox_set_flagged_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &status_icons
			      [LIBBALSA_MESSAGE_STATUS_FLAGGED]);
}

void libbalsa_mailbox_set_replied_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &status_icons
			      [LIBBALSA_MESSAGE_STATUS_REPLIED]);
}

/* Icons for attachment column. */
void libbalsa_mailbox_set_attach_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &attach_icons
			      [LIBBALSA_MESSAGE_ATTACH_ATTACH]);
}

#ifdef HAVE_GPGME
void libbalsa_mailbox_set_good_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &attach_icons
			      [LIBBALSA_MESSAGE_ATTACH_GOOD]);
}

void libbalsa_mailbox_set_notrust_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &attach_icons
			      [LIBBALSA_MESSAGE_ATTACH_NOTRUST]);
}

void libbalsa_mailbox_set_bad_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &attach_icons
			      [LIBBALSA_MESSAGE_ATTACH_BAD]);
}

void libbalsa_mailbox_set_sign_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &attach_icons
			      [LIBBALSA_MESSAGE_ATTACH_SIGN]);
}

void libbalsa_mailbox_set_encr_icon(GdkPixbuf * pixbuf)
{
    libbalsa_mailbox_set_icon(pixbuf,
			      &attach_icons
			      [LIBBALSA_MESSAGE_ATTACH_ENCR]);
}
#endif /* HAVE_GPGME */

/* =================================================================== *
 * GtkTreeDragSource implementation functions.                         *
 * =================================================================== */

static gboolean mbox_row_draggable(GtkTreeDragSource * drag_source,
				   GtkTreePath * path);
static gboolean mbox_drag_data_delete(GtkTreeDragSource * drag_source,
				      GtkTreePath * path);
static gboolean mbox_drag_data_get(GtkTreeDragSource * drag_source,
				   GtkTreePath * path,
				   GtkSelectionData * selection_data);

static void
mbox_drag_source_init(GtkTreeDragSourceIface * iface)
{
    iface->row_draggable    = mbox_row_draggable;
    iface->drag_data_delete = mbox_drag_data_delete;
    iface->drag_data_get    = mbox_drag_data_get;
}

/* These three methods are apparently never called, so what they return
 * is irrelevant.  The code reflects guesses about what they should
 * return if they were ever called.
 */
static gboolean
mbox_row_draggable(GtkTreeDragSource * drag_source, GtkTreePath * path)
{
    /* All rows are valid sources. */
    return TRUE;
}

static gboolean
mbox_drag_data_delete(GtkTreeDragSource * drag_source, GtkTreePath * path)
{
    /* The "drag-data-received" callback handles deleting messages that
     * are dragged out of the mailbox, so we don't. */
    return FALSE;
}

static gboolean
mbox_drag_data_get(GtkTreeDragSource * drag_source, GtkTreePath * path,
		   GtkSelectionData * selection_data)
{
    /* The "drag-data-get" callback passes the list of selected messages
     * to the GtkSelectionData, so we don't. */
    return FALSE;
}

/* =================================================================== *
 * GtkTreeSortable implementation functions.                           *
 * =================================================================== */

static void mbox_sort(LibBalsaMailbox * mbox);
static gboolean mbox_get_sort_column_id(GtkTreeSortable * sortable,
					gint * sort_column_id,
					GtkSortType * order);
static void mbox_set_sort_column_id(GtkTreeSortable * sortable,
				    gint sort_column_id,
				    GtkSortType order);
static void mbox_set_sort_func(GtkTreeSortable * sortable,
			       gint sort_column_id,
			       GtkTreeIterCompareFunc func, gpointer data,
			       GtkDestroyNotify destroy);
static void mbox_set_default_sort_func(GtkTreeSortable * sortable,
				       GtkTreeIterCompareFunc func,
				       gpointer data,
				       GtkDestroyNotify destroy);
static gboolean mbox_has_default_sort_func(GtkTreeSortable * sortable);

static void
mbox_sortable_init(GtkTreeSortableIface * iface)
{
    iface->get_sort_column_id    = mbox_get_sort_column_id;
    iface->set_sort_column_id    = mbox_set_sort_column_id;
    iface->set_sort_func         = mbox_set_sort_func;
    iface->set_default_sort_func = mbox_set_default_sort_func;
    iface->has_default_sort_func = mbox_has_default_sort_func;
}


static gint
mbox_compare_msgno(LibBalsaMessage * message_a,
		   LibBalsaMessage * message_b)
{
    glong msgno_a, msgno_b;

    g_return_val_if_fail(message_a && message_b, 0);

    msgno_a = LIBBALSA_MESSAGE_GET_NO(message_a);
    msgno_b = LIBBALSA_MESSAGE_GET_NO(message_b);

    return msgno_a-msgno_b;
}

static gint
mbox_compare_from(LibBalsaMessage * message_a,
		  LibBalsaMessage * message_b)
{
    gchar *from_a = get_from_field(message_a);
    gchar *from_b = get_from_field(message_b);
    gboolean retval = g_ascii_strcasecmp(from_a, from_b);

    g_free(from_a);
    g_free(from_b);

    return retval;
}

static gint
mbox_compare_subject(LibBalsaMessage * message_a,
		     LibBalsaMessage * message_b)
{
    const gchar *subject_a = LIBBALSA_MESSAGE_GET_SUBJECT(message_a);
    const gchar *subject_b = LIBBALSA_MESSAGE_GET_SUBJECT(message_b);

    return g_ascii_strcasecmp(subject_a, subject_b);
}

static gint
mbox_compare_date(LibBalsaMessage * message_a,
		  LibBalsaMessage * message_b)
{
    g_return_val_if_fail(message_a && message_b && message_a->headers
			 && message_b->headers, 0);
    return message_a->headers->date - message_b->headers->date;
}

static gint
mbox_compare_size(LibBalsaMessage * message_a,
		  LibBalsaMessage * message_b)
{
    glong size_a, size_b;

    g_return_val_if_fail(message_a && message_b, 0);

    if (FALSE /* balsa_app.line_length */) {
        size_a = LIBBALSA_MESSAGE_GET_LINES(message_a);
        size_b = LIBBALSA_MESSAGE_GET_LINES(message_b);
    } else {
        size_a = LIBBALSA_MESSAGE_GET_LENGTH(message_a);
        size_b = LIBBALSA_MESSAGE_GET_LENGTH(message_b);
    }

    return size_a - size_b;
}

static gint
mbox_compare_func(const SortTuple * a,
		  const SortTuple * b,
		  LibBalsaMailbox * mbox)
{
    LibBalsaMessage *message_a;
    LibBalsaMessage *message_b;
    gint retval;

    message_a =
	libbalsa_mailbox_get_message(mbox,
				     GPOINTER_TO_UINT(a->node->data));
    message_b =
	libbalsa_mailbox_get_message(mbox,
				     GPOINTER_TO_UINT(b->node->data));

    switch (mbox->view->sort_field) {
    case LB_MAILBOX_SORT_NO:
	retval = mbox_compare_msgno(message_a, message_b);
	break;
    case LB_MAILBOX_SORT_SENDER:
	retval = mbox_compare_from(message_a, message_b);
	break;
    case LB_MAILBOX_SORT_SUBJECT:
	retval = mbox_compare_subject(message_a, message_b);
	break;
    case LB_MAILBOX_SORT_DATE:
	retval = mbox_compare_date(message_a, message_b);
	break;
    case LB_MAILBOX_SORT_SIZE:
	retval = mbox_compare_size(message_a, message_b);
	break;
    default:
	retval = 0;
	break;
    }

    if (mbox->view->sort_type == LB_MAILBOX_SORT_TYPE_DESC) {
	if (retval > 0)
	    retval = -1;
	else if (retval < 0)
	    retval = 1;
    }

    return retval;
}

static void
mbox_sort_helper(LibBalsaMailbox * mbox,
		 GNode           * parent)
{
    GtkTreeIter iter;
    GArray *sort_array;
    GNode *node;
    GNode *tmp_node;
    gint list_length;
    gint i;
    gint *new_order;
    GtkTreePath *path;

    node = parent->children;
    g_assert(node != NULL);
    if (node->next == NULL) {
	if (node->children)
	    mbox_sort_helper(mbox, node);
	return;
    }

    list_length = 0;
    for (tmp_node = node; tmp_node; tmp_node = tmp_node->next)
	list_length++;

    sort_array =
	g_array_sized_new(FALSE, FALSE, sizeof(SortTuple), list_length);

    i = 0;
    for (tmp_node = node; tmp_node; tmp_node = tmp_node->next) {
	SortTuple tuple;

	tuple.offset = i;
	tuple.node = tmp_node;
	g_array_append_val(sort_array, tuple);
	i++;
    }

    LIBBALSA_MAILBOX_GET_CLASS(mbox)->sort(mbox, sort_array);

    for (i = 0; i < list_length - 1; i++) {
	g_array_index(sort_array, SortTuple, i).node->next =
	    g_array_index(sort_array, SortTuple, i + 1).node;
	g_array_index(sort_array, SortTuple, i + 1).node->prev =
	    g_array_index(sort_array, SortTuple, i).node;
    }
    g_array_index(sort_array, SortTuple, list_length - 1).node->next =
	NULL;
    g_array_index(sort_array, SortTuple, 0).node->prev = NULL;
    parent->children = g_array_index(sort_array, SortTuple, 0).node;
    /* Let the world know about our new order */
    new_order = g_new(gint, list_length);
    for (i = 0; i < list_length; i++)
	new_order[i] = g_array_index(sort_array, SortTuple, i).offset;

    iter.stamp = mbox->stamp;
    iter.user_data = parent;
    path = parent->parent ? mbox_model_get_path(GTK_TREE_MODEL(mbox), &iter)
			  : gtk_tree_path_new();
    gtk_tree_model_rows_reordered(GTK_TREE_MODEL(mbox),
				  path, &iter, new_order);
    gtk_tree_path_free(path);
    g_free(new_order);
    g_array_free(sort_array, TRUE);

    for (tmp_node = parent->children; tmp_node; tmp_node = tmp_node->next)
	if (tmp_node->children)
	    mbox_sort_helper(mbox, tmp_node);
}

static void
mbox_sort(LibBalsaMailbox * mbox)
{
    if (mbox->msg_tree->children)
	mbox_sort_helper(mbox, mbox->msg_tree);
}

/* called from gtk-tree-view-column */
static gboolean
mbox_get_sort_column_id(GtkTreeSortable * sortable,
			gint            * sort_column_id,
			GtkSortType     * order)
{
    LibBalsaMailbox *mbox = (LibBalsaMailbox *) sortable;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(sortable), FALSE);

    if (sort_column_id) {
	switch (mbox->view->sort_field) {
	default:
	case LB_MAILBOX_SORT_NATURAL:
	case LB_MAILBOX_SORT_NO:
	    *sort_column_id = LB_MBOX_MSGNO_COL;
	    break;
	case LB_MAILBOX_SORT_SENDER:
	    *sort_column_id = LB_MBOX_FROM_COL;
	    break;
	case LB_MAILBOX_SORT_SUBJECT:
	    *sort_column_id = LB_MBOX_SUBJECT_COL;
	    break;
	case LB_MAILBOX_SORT_DATE:
	    *sort_column_id = LB_MBOX_DATE_COL;
	    break;
	case LB_MAILBOX_SORT_SIZE:
	    *sort_column_id = LB_MBOX_SIZE_COL;
	    break;
	}
    }

    if (order)
	*order = (mbox->view->sort_type ==
		  LB_MAILBOX_SORT_TYPE_DESC ? GTK_SORT_DESCENDING :
		  GTK_SORT_ASCENDING);

    return TRUE;
}

/* called from gtk-tree-view-column */
static void
mbox_set_sort_column_id(GtkTreeSortable * sortable,
			gint              sort_column_id,
			GtkSortType       order)
{
    LibBalsaMailbox *mbox = (LibBalsaMailbox *) sortable;
    LibBalsaMailboxView *view;
    LibBalsaMailboxSortFields new_field;
    LibBalsaMailboxSortType new_type;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(sortable));

    view = mbox->view;

    switch (sort_column_id) {
    case LB_MBOX_MSGNO_COL:
	new_field = LB_MAILBOX_SORT_NO;
	break;
    case LB_MBOX_FROM_COL:
	new_field = LB_MAILBOX_SORT_SENDER;
	break;
    case LB_MBOX_SUBJECT_COL:
	new_field = LB_MAILBOX_SORT_SUBJECT;
	break;
    case LB_MBOX_DATE_COL:
	new_field = LB_MAILBOX_SORT_DATE;
	break;
    case LB_MBOX_SIZE_COL:
	new_field = LB_MAILBOX_SORT_SIZE;
	break;
    default:
	new_field = LB_MAILBOX_SORT_NATURAL;
	break;
    }

    new_type = (order == GTK_SORT_DESCENDING ? LB_MAILBOX_SORT_TYPE_DESC :
		LB_MAILBOX_SORT_TYPE_ASC);

    if (view->sort_field == new_field && view->sort_type == new_type)
	return;

    view->sort_field = new_field;
    view->sort_type = new_type;

    gtk_tree_sortable_sort_column_changed(sortable);

    mbox_sort(mbox);

    g_signal_emit(mbox, libbalsa_mailbox_signals[CHANGED], 0);
}

static void
mbox_set_sort_func(GtkTreeSortable * sortable,
		   gint sort_column_id,
		   GtkTreeIterCompareFunc func,
		   gpointer data, GtkDestroyNotify destroy)
{
    g_warning("%s called but not implemented.\n", __func__);
}

static void
mbox_set_default_sort_func(GtkTreeSortable * sortable,
			   GtkTreeIterCompareFunc func,
			   gpointer data, GtkDestroyNotify destroy)
{
    g_warning("%s called but not implemented.\n", __func__);
}

/* called from gtk-tree-view-column */
static gboolean
mbox_has_default_sort_func(GtkTreeSortable * sortable)
{
    return FALSE;
}

/* Helpers for set-threading-type. */
void
libbalsa_mailbox_unlink_and_prepend(LibBalsaMailbox * mailbox,
				    GNode * node, GNode * parent)
{
    GtkTreeIter iter;
    GtkTreePath *path;
    GNode *current_parent;

    iter.stamp = mailbox->stamp;

    path = mbox_model_get_path_helper(node, mailbox->msg_tree);
    current_parent = node->parent;
    g_node_unlink(node);
    if (path) {
	/* The node was in mailbox->msg_tree. */
	g_signal_emit(mailbox,
		      libbalsa_mbox_model_signals[ROW_DELETED], 0, path);
	if (!current_parent->children) {
	    /* It was the last child. */
	    gtk_tree_path_up(path);
	    iter.user_data = current_parent;
	    g_signal_emit(mailbox,
			  libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
			  0, path, &iter);
	}
	gtk_tree_path_free(path);
    }

    if (!parent) {
	g_node_destroy(node);
	return;
    }

    g_node_prepend(parent, node);
    path = mbox_model_get_path_helper(parent, mailbox->msg_tree);
    if (path) {
	/* The parent is in mailbox->msg_tree. */
	if (!node->next) {
	    /* It is the first child. */
	    iter.user_data = parent;
	    g_signal_emit(mailbox,
			  libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
			  0, path, &iter);
	}
	gtk_tree_path_down(path);
	iter.user_data = node;
	g_signal_emit(mailbox,
		      libbalsa_mbox_model_signals[ROW_INSERTED], 0,
		      path, &iter);
	if (node->children)
	    g_signal_emit(mailbox,
			  libbalsa_mbox_model_signals[ROW_HAS_CHILD_TOGGLED],
			  0, path, &iter);
	gtk_tree_path_free(path);
    }
}

struct lbm_update_msg_tree_info {
    LibBalsaMailbox *mailbox;
    GNode *new_tree;
    GNode **nodes;
};

/* GNodeTraverseFunc for making a reverse lookup array into a tree. */
static gboolean
lbm_update_msg_tree_populate(GNode * node, 
			     struct lbm_update_msg_tree_info *mti)
{
    guint msgno;

    msgno = GPOINTER_TO_UINT(node->data);
    g_assert(msgno <= libbalsa_mailbox_total_messages(mti->mailbox));

    mti->nodes[msgno] = node;

    return FALSE;
}

/* GNodeTraverseFunc for pruning the current tree; mti->nodes is a
 * reverse lookup array into the new tree, so a NULL value is a node
 * that doesn't appear in the new tree. */
static gboolean
lbm_update_msg_tree_prune(GNode * node,
			  struct lbm_update_msg_tree_info *mti)
{
    guint msgno;

    msgno = GPOINTER_TO_UINT(node->data);
    g_assert(msgno <= libbalsa_mailbox_total_messages(mti->mailbox));
    if (!mti->nodes[msgno]) {
	/* It's a bottom-up traverse, so the node's remaining children
	 * are all in the new tree; we'll promote them to be children of
	 * the node's parent, which might even be where they finish up. */
	while (node->children)
	    libbalsa_mailbox_unlink_and_prepend(mti->mailbox,
						node->children,
						node->parent);
	/* Now we can destroy the node. */
	libbalsa_mailbox_unlink_and_prepend(mti->mailbox, node, NULL);
    }

    return FALSE;
}

/* GNodeTraverseFunc for checking parent-child relationships; mti->nodes
 * is a reverse lookup array into the old tree, so a NULL value means a
 * node that isn't in the current tree, and we insert one; because the
 * traverse is top-down, a missing parent will have been inserted before
 * we get to the child. */
static gboolean
lbm_update_msg_tree_move(GNode * new_node,
			 struct lbm_update_msg_tree_info *mti)
{
    guint msgno;
    GNode *node;
    GNode *parent;

    if (!new_node->parent)
	return FALSE;

    msgno = GPOINTER_TO_UINT(new_node->data);
    g_assert(msgno <= libbalsa_mailbox_total_messages(mti->mailbox));
    node = mti->nodes[msgno];
    if (!node)
	mti->nodes[msgno] = node = g_node_new(new_node->data);

    msgno = GPOINTER_TO_UINT(new_node->parent->data);
    g_assert(msgno <= libbalsa_mailbox_total_messages(mti->mailbox));
    parent = mti->nodes[msgno];
    g_assert(parent != NULL);

    if (parent != node->parent)
	libbalsa_mailbox_unlink_and_prepend(mti->mailbox, node, parent);

    return FALSE;
}

static void
lbm_update_msg_tree(LibBalsaMailbox * mailbox, GNode * new_tree)
{
    struct lbm_update_msg_tree_info mti;

    mti.mailbox = mailbox;
    mti.new_tree = new_tree;
    mti.nodes =
	g_new0(GNode *, 1 + libbalsa_mailbox_total_messages(mailbox));

    /* Populate the nodes array with nodes in the new tree. */
    g_node_traverse(new_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbm_update_msg_tree_populate, &mti);
    /* Remove deadwood. */
    g_node_traverse(mailbox->msg_tree, G_POST_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbm_update_msg_tree_prune, &mti);

    /* Clear the nodes array and repopulate it with nodes in the current
     * tree. */
    memset(mti.nodes, 0,
	   sizeof(GNode *) * (1 + libbalsa_mailbox_total_messages(mailbox)));
    g_node_traverse(mailbox->msg_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbm_update_msg_tree_populate, &mti);
    /* Check parent-child relationships. */
    g_node_traverse(new_tree, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		    (GNodeTraverseFunc) lbm_update_msg_tree_move, &mti);

    g_free(mti.nodes);
}

static void
lbm_set_msg_tree(LibBalsaMailbox * mailbox)
{
    GtkTreeIter iter;
    GNode *node;
    GtkTreePath *path;

    path = gtk_tree_path_new();
    gtk_tree_path_down(path);

    iter.stamp = ++mailbox->stamp;

    for (node = mailbox->msg_tree->children; node; node = node->next) {
	iter.user_data = node;
	g_signal_emit(mailbox,
		      libbalsa_mbox_model_signals[ROW_INSERTED], 0, path,
		      &iter);
	if (node->children)
	    g_signal_emit(mailbox,
			  libbalsa_mbox_model_signals
			  [ROW_HAS_CHILD_TOGGLED], 0, path, &iter);
	gtk_tree_path_next(path);
    }

    gtk_tree_path_free(path);
}

void
libbalsa_mailbox_set_msg_tree(LibBalsaMailbox * mailbox, GNode * new_tree)
{
    if (mailbox->msg_tree->children) {
	lbm_update_msg_tree(mailbox, new_tree);
	g_node_destroy(new_tree);
    } else if (new_tree->children) {
	/* msg_tree has never been populated */
	g_node_destroy(mailbox->msg_tree);
	mailbox->msg_tree = new_tree;
	mailbox->stamp++;
	lbm_set_msg_tree(mailbox);
    }
}

static GMimeMessage *
lbm_get_mime_msg(LibBalsaMailbox * mailbox, LibBalsaMessage * msg)
{
    GMimeMessage *mime_msg;

    libbalsa_mailbox_fetch_message_structure(mailbox, msg,
					     LB_FETCH_RFC822_HEADERS);
    mime_msg = msg->mime_msg;
    if (mime_msg)
	g_mime_object_ref(GMIME_OBJECT(mime_msg));
    else {
	GMimeStream *stream;
	GMimeParser *parser;

	stream = libbalsa_mailbox_get_message_stream(mailbox, msg);
	parser = g_mime_parser_new_with_stream(stream);
	g_mime_stream_unref(stream);
	mime_msg = g_mime_parser_construct_message(parser);
	g_object_unref(parser);
    }
    libbalsa_mailbox_release_message(mailbox, msg);

    return mime_msg;
}

/* Try to reassemble messages of type message/partial with the given id;
 * if successful, flag the parts, so we don't keep creating the whole
 * message. */
void
libbalsa_mailbox_try_reassemble(LibBalsaMailbox * mailbox,
				const gchar * id)
{
    guint msgno;
    LibBalsaMessage *message;
    GMimeMessage *mime_message;
    GPtrArray *partials = g_ptr_array_new();
    guint total = (guint) - 1;
    GList *messages = NULL;

    for (msgno = 1; msgno <= libbalsa_mailbox_total_messages(mailbox);
	 msgno++) {
	message = libbalsa_mailbox_get_message(mailbox, msgno);
	gchar *tmp_id;

	if (!libbalsa_message_is_partial(message, &tmp_id)
	    || LIBBALSA_MESSAGE_IS_FLAGGED(message))
	    continue;

	if (strcmp(tmp_id, id) == 0) {
	    GMimeMessagePartial *partial;

	    mime_message = lbm_get_mime_msg(mailbox, message);
	    partial = GMIME_MESSAGE_PARTIAL(mime_message->mime_part);
	    g_ptr_array_add(partials, partial);
	    if (g_mime_message_partial_get_total(partial) > 0)
		total = g_mime_message_partial_get_total(partial);
	    g_mime_object_unref(GMIME_OBJECT(mime_message));

	    messages = g_list_prepend(messages, message);
	}

	g_free(tmp_id);
    }

    if (partials->len == total) {
	mime_message =
	    g_mime_message_partial_reconstruct_message((GMimeMessagePartial
							**) partials->
						       pdata, total);
	message = libbalsa_message_new();
	message->mime_msg = mime_message;
	libbalsa_mailbox_copy_message(message, mailbox);
	g_object_unref(message);

	libbalsa_messages_change_flag(messages,
				      LIBBALSA_MESSAGE_FLAG_FLAGGED,
				      TRUE);
    }

    g_ptr_array_free(partials, TRUE);
    g_list_free(messages);
}
