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

static void libbalsa_mailbox_real_close(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox
							   * mailbox,
							   gboolean flag);
static gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox* mailbox,
						GSList * conditions);
static void libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

/* Callbacks */
static void messages_status_changed_cb(LibBalsaMailbox * mb,
				       GList * messages,
				       gint changed_flag);

/* SIGNALS MEANINGS :
   - OPEN_MAILBOX, CLOSE_MAILBOX, SAVE/LOAD_CONFIG, CHECK,
   SET_UNREAD_MESSAGES_FLAG : tell the mailbox to change its properties.
   - MESSAGES_STATUS_CHANGED : notification signal sent by messages of the
   mailbox to tell that their status have changed.
   - MESSAGES_ADDED, MESSAGES_REMOVED : notification signals sent by the mailbox
   to allow the frontend to keep in sync. These signals are used when messages
   are added or removed to the mailbox but not by the user. This is used when
   eg the mailbox loads new messages (check new mails), or when the prefs
   changed (eg "Hide Deleted Messages") and cause the deletion of messages.
*/

enum {
    MESSAGES_STATUS_CHANGED,
    MESSAGES_ADDED,
    MESSAGES_REMOVED,
    PROGRESS_NOTIFY,
    SET_UNREAD_MESSAGES_FLAG,
    LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;
static guint libbalsa_mailbox_signals[LAST_SIGNAL];

/* GtkTreeModel function prototypes */
static void  mbox_model_init(GtkTreeModelIface *iface);

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
    
        mailbox_type =
            g_type_register_static(G_TYPE_OBJECT, "LibBalsaMailbox",
                                   &mailbox_info, 0);
	g_type_add_interface_static(mailbox_type,
				    GTK_TYPE_TREE_MODEL,
				    &mbox_model_info);
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
    libbalsa_mailbox_signals[MESSAGES_ADDED] =
	g_signal_new("messages-added",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     messages_added),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_POINTER);

    /* This signal is emitted by the mailbox when messages are removed
       in order to sync with backend in general (this is different from
       message deletion which is a user action).
       GUI (see BalsaIndex) hooks on this signal to sync on the mailbox
       content.
    */ 
    libbalsa_mailbox_signals[MESSAGES_REMOVED] =
	g_signal_new("messages-removed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     messages_removed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_POINTER);

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
    klass->close_mailbox = libbalsa_mailbox_real_close;
    klass->set_unread_messages_flag =
	libbalsa_mailbox_real_set_unread_messages_flag;
    klass->progress_notify = NULL;

    klass->messages_added = NULL;
    klass->messages_removed = NULL;
    klass->messages_status_changed = messages_status_changed_cb;

    klass->get_message = NULL;
    klass->prepare_threading = NULL;
    klass->fetch_message_structure = NULL;
    klass->get_message_part = NULL;
    klass->get_message_stream = NULL;
    klass->change_message_flags = NULL;
    klass->set_threading = NULL;
    klass->check = NULL;
    klass->message_match = NULL;
    klass->mailbox_match = NULL;
    klass->can_match = libbalsa_mailbox_real_can_match;
    klass->save_config  = libbalsa_mailbox_real_save_config;
    klass->load_config  = libbalsa_mailbox_real_load_config;
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
    mailbox->messages = 0;
    mailbox->new_messages = 0;
    mailbox->has_unread_messages = FALSE;
    mailbox->unread_messages = 0;
    mailbox->total_messages = 0;

    mailbox->readonly = FALSE;
    mailbox->disconnected = FALSE;

    mailbox->filters=NULL;
    mailbox->view=NULL;
    mailbox->msg_tree = g_node_new(NULL);
    do
	mailbox->stamp = g_random_int ();
    while (mailbox->stamp == 0);
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

gboolean
libbalsa_mailbox_open(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->open_mailbox(mailbox);
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
    
    if(LIBBALSA_IS_MAILBOX_MBOX(mailbox)) {
	LibBalsaMailboxMbox *mbox = LIBBALSA_MAILBOX_MBOX(mailbox);
	
	return (mbox->gmime_stream != NULL);
    } else if(LIBBALSA_IS_MAILBOX_MH(mailbox)) {
	LibBalsaMailboxMh *mh = LIBBALSA_MAILBOX_MH(mailbox);
	
	return (mh->msgno_2_index != NULL);
    } else if(LIBBALSA_IS_MAILBOX_MAILDIR(mailbox)) {
	LibBalsaMailboxMaildir *mdir = LIBBALSA_MAILBOX_MAILDIR(mailbox);
	
	return (mdir->msgno_2_msg_info != NULL);
    } else if(LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	LibBalsaMailboxImap *mimap = LIBBALSA_MAILBOX_IMAP(mailbox);
	
	return(mimap->opened);
    };

    return FALSE; // this will break unlisted mailbox types
}
    
void
libbalsa_mailbox_close(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->close_mailbox(mailbox);
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

void
libbalsa_mailbox_check(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->check(mailbox);
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
			       int op, GSList * conditions)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->message_match(mailbox,
							      message, op,
							      conditions);
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

gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox* mailbox,
					 GSList * conditions)
{
    /* By default : all filters is OK */
    return TRUE;
}

gboolean
libbalsa_mailbox_can_match(LibBalsaMailbox * mailbox, GSList * conditions)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->can_match(mailbox,
							  conditions);
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
libbalsa_mailbox_real_close(LibBalsaMailbox * mailbox)
{
    static const int RETRIES_COUNT = 50;
    int check, cnt;
#ifdef DEBUG
    g_print("LibBalsaMailbox: Closing %s Refcount: %d\n", mailbox->name,
	    mailbox->open_ref);
#endif
    LOCK_MAILBOX(mailbox);

    if (mailbox->open_ref == 0) {
	UNLOCK_MAILBOX(mailbox);
	return;
    }

    mailbox->open_ref--;

    if (mailbox->open_ref == 0) {
	/* now close the mail stream and expunge deleted
	 * messages -- the expunge may not have to be done */
	/* We are careful to take/release locks in the correct order here */
	cnt = 0;
	while( cnt < RETRIES_COUNT &&
	       (check = libbalsa_mailbox_close_backend(mailbox)) == FALSE) {
	    UNLOCK_MAILBOX(mailbox);
	    g_print("libbalsa_mailbox_real_close: %d trial failed.\n", cnt);
            usleep(10000); /* wait tenth second */
	    libbalsa_mailbox_check(mailbox);
	    LOCK_MAILBOX(mailbox);
	    cnt++;
	}
	if(cnt>=RETRIES_COUNT)
	    g_print("libbalsa_mailbox_real_close: changes to %s lost.\n",
		    mailbox->name);
#if 0
	libbalsa_mailbox_free_messages(mailbox);
#endif
    }

    UNLOCK_MAILBOX(mailbox);
}

static void
libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean flag)
{
    mailbox->has_unread_messages = flag;
}

#if 0
/* Default handler : just call match_conditions
   IMAP is the only mailbox type that implements its own way for that
 */
static gboolean
libbalsa_mailbox_real_message_match(LibBalsaMailbox* mailbox,
				    LibBalsaMessage * message,
				    int op, GSList* conditions)
{
    return match_conditions(op, conditions, message, FALSE);
}
#endif

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

#if 0
void
libbalsa_mailbox_remove_messages(LibBalsaMailbox * mbox, GList * messages)
{
    LOCK_MAILBOX(mbox);
    for (; messages; messages = g_list_next(messages)) {
	LibBalsaMessage * message = LIBBALSA_MESSAGE(messages->data);

	message->mailbox = NULL;
	g_object_unref(G_OBJECT(message));
	mbox->message_list =
		g_list_remove(mbox->message_list, message);
    }
    UNLOCK_MAILBOX(mbox);
}

/* libbalsa_mailbox_sync_backend:
 * use libbalsa_mailbox_sync_backend_real to synchronize the frontend and
 * libbalsa
 */
gint
libbalsa_mailbox_sync_backend(LibBalsaMailbox * mailbox, gboolean delete)
{
    gint res = 0;

    /* only open mailboxes can be commited; lock it instead of opening */
    g_return_val_if_fail(MAILBOX_OPEN(mailbox), 0);
    LOCK_MAILBOX_RETURN_VAL(mailbox, 0);
    libbalsa_mailbox_sync_backend_real(mailbox, delete);
    UNLOCK_MAILBOX(mailbox);
    return res;
}
#endif

static void
libbalsa_mailbox_msgno_changed(LibBalsaMailbox * mailbox, guint msgno)
{
    GtkTreeIter iter;

    iter.user_data = g_node_find(mailbox->msg_tree, G_PRE_ORDER,
				 G_TRAVERSE_ALL, GUINT_TO_POINTER(msgno));
    g_assert(iter.user_data != NULL);
    iter.stamp = mailbox->stamp;
    g_signal_emit_by_name(mailbox, "row-changed", NULL, &iter);
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

        for (; messages; messages = g_list_next(messages)) {
            nb_in_list++;
	    if (LIBBALSA_MESSAGE_IS_UNREAD(messages->data))
                new_in_list++;
        }

        if (new_state) {
            /* messages have been deleted */
            mb->total_messages -= nb_in_list;

            if (new_in_list) {
                mb->unread_messages -= new_in_list;
                if (mb->unread_messages <= 0)
                    libbalsa_mailbox_set_unread_messages_flag(mb, FALSE);
            }
        } else {
            /* message has been undeleted */
            mb->total_messages += nb_in_list;
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

int libbalsa_mailbox_copy_message(LibBalsaMessage *message, LibBalsaMailbox *dest)
{
    int retval = LIBBALSA_MAILBOX_GET_CLASS(dest)->add_message ( dest, message );
    if (retval > 0 && LIBBALSA_MESSAGE_IS_UNREAD(message))
	dest->has_unread_messages = TRUE;
    
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

    if (libbalsa_mailbox_sync_storage(mailbox) == FALSE)
	return FALSE;

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->close_backend(mailbox);
}

gboolean
libbalsa_mailbox_sync_storage(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(!mailbox->readonly, TRUE);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->sync(mailbox);
}

LibBalsaMessage*
libbalsa_mailbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    g_return_val_if_fail(mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(msgno > 0, NULL);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message(mailbox, msgno);
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
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    g_return_val_if_fail(message->mailbox == mailbox, NULL);

    return LIBBALSA_MAILBOX_GET_CLASS(mailbox)->get_message_stream(mailbox,
								   message);
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
}

/*
 * Mailbox views
 */
void
libbalsa_mailbox_set_view(LibBalsaMailbox *mailbox,
                          LibBalsaMessageFlag set,
                          LibBalsaMessageFlag clear)
{
    g_warning("Implement me!");
}

/* FIXME: we should inform the treeview that we have rehashed the rows.
 * what the callee does instead is to unset and set the model again
 * for the TreeView. This is somewhat ugly.
 */
void
libbalsa_mailbox_set_threading(LibBalsaMailbox *mailbox,
			       LibBalsaMailboxThreadingType thread_type)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    LIBBALSA_MAILBOX_GET_CLASS(mailbox)->set_threading(mailbox, thread_type);
    
    /* invalidate iterators */
    mailbox->stamp++;
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
mbox_model_get_path(GtkTreeModel	* tree_model,
		    GtkTreeIter		* iter)
{
    GNode *node, *parent_node;
    GtkTreePath *retval;
    gint i;

    g_return_val_if_fail(VALID_ITER(iter, tree_model), NULL);

    node = iter->user_data;
#define SANITY_CHECK
#ifdef SANITY_CHECK
    for (parent_node = node->parent; parent_node;
	 parent_node = parent_node->parent)
	g_return_val_if_fail(parent_node != node, NULL);
#endif
    parent_node = node->parent;

    g_return_val_if_fail(parent_node != NULL, NULL);

    if (parent_node->parent == NULL) {
	g_assert(parent_node == LIBBALSA_MAILBOX(tree_model)->msg_tree);
	retval = gtk_tree_path_new();
    } else {
	GtkTreeIter parent_iter;

	parent_iter.user_data = parent_node;
	VALIDATE_ITER(&parent_iter, tree_model);
	retval = mbox_model_get_path(tree_model, &parent_iter);
    }

    if (retval == NULL)
	return NULL;

    i = g_node_child_position(parent_node, node);
    if (i < 0) {
	gtk_tree_path_free(retval);
	return NULL;
    }

    gtk_tree_path_append_index(retval, i);

    return retval;
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
    /* FIXME: use balsa_app.convert_8bit_codeset */
    libbalsa_utf8_sanitize(&from, TRUE, WEST_EUROPE, NULL);
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
			 (msg && LIBBALSA_MESSAGE_IS_UNREAD(msg)) ?
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
