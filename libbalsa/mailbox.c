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
static gboolean libbalsa_mailbox_real_message_match(LibBalsaMailbox* mailbox,
						    LibBalsaMessage * message,
						    int op,
						    GSList* conditions);
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

static void libbalsa_mailbox_sync_backend_real(LibBalsaMailbox * mailbox,
					       gboolean delete);

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
    OPEN_MAILBOX,
    CLOSE_MAILBOX,
    MESSAGES_STATUS_CHANGED,
    MESSAGES_ADDED,
    MESSAGES_REMOVED,
    GET_MESSAGE_STREAM,
    PROGRESS_NOTIFY,
    CHECK,
    MESSAGE_MATCH,
    MAILBOX_MATCH,
    CAN_MATCH,
    SET_UNREAD_MESSAGES_FLAG,
    SAVE_CONFIG,
    LOAD_CONFIG,
    SYNC,
    CLOSE_BACKEND,
    GET_MESSAGE,
    LOAD_MESSAGE,
    ADD_MESSAGE,
    CHANGE_MESSAGE_FLAGS,
    LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;
static guint libbalsa_mailbox_signals[LAST_SIGNAL];

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

        mailbox_type =
            g_type_register_static(G_TYPE_OBJECT, "LibBalsaMailbox",
                                   &mailbox_info, 0);
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
    
    libbalsa_mailbox_signals[OPEN_MAILBOX] =
	g_signal_new("open-mailbox",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     open_mailbox),
                     NULL, NULL,
                     libbalsa_BOOLEAN__VOID, G_TYPE_BOOLEAN, 0);

    libbalsa_mailbox_signals[CLOSE_MAILBOX] =
	g_signal_new("close-mailbox",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     close_mailbox),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

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

    /* Virtual functions. Use G_SIGNAL_NO_HOOKS
       which prevents the signal being connected to them */
    libbalsa_mailbox_signals[GET_MESSAGE_STREAM] =
	g_signal_new("get-message-stream",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     get_message_stream),
                     NULL, NULL,
                     libbalsa_POINTER__OBJECT, G_TYPE_POINTER, 1,
                     LIBBALSA_TYPE_MESSAGE);

    libbalsa_mailbox_signals[CHECK] =
	g_signal_new("check",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     check),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    libbalsa_mailbox_signals[MESSAGE_MATCH] =
	g_signal_new("message-match",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     message_match),
                     NULL, NULL,
                     libbalsa_BOOLEAN__POINTER_INT_POINTER,
                     G_TYPE_BOOLEAN, 3, G_TYPE_POINTER, G_TYPE_INT,
		     G_TYPE_POINTER);

    libbalsa_mailbox_signals[MAILBOX_MATCH] =
	g_signal_new("mailbox-match",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     mailbox_match),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_POINTER);

    libbalsa_mailbox_signals[CAN_MATCH] =
	g_signal_new("can-match",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     can_match),
                     NULL, NULL,
                     libbalsa_BOOLEAN__POINTER, G_TYPE_BOOLEAN, 1,
                     G_TYPE_POINTER);

    libbalsa_mailbox_signals[SAVE_CONFIG] =
	g_signal_new("save-config",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     save_config),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_POINTER);

    libbalsa_mailbox_signals[LOAD_CONFIG] =
	g_signal_new("load-config",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     load_config),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                     G_TYPE_POINTER);

    libbalsa_mailbox_signals[SYNC] =
	g_signal_new("sync",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     sync),
                     NULL, NULL,
                     libbalsa_BOOLEAN__VOID, G_TYPE_BOOLEAN, 0);

    libbalsa_mailbox_signals[CLOSE_BACKEND] =
	g_signal_new("close-backend",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     close_backend),
                     NULL, NULL,
                     libbalsa_BOOLEAN__VOID, G_TYPE_BOOLEAN, 0);

    libbalsa_mailbox_signals[GET_MESSAGE] =
	g_signal_new("get-message",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     get_message),
                     NULL, NULL,
                     libbalsa_POINTER__INT, G_TYPE_POINTER, 1,
		     G_TYPE_INT);

    libbalsa_mailbox_signals[LOAD_MESSAGE] =
	g_signal_new("load-message",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     load_message),
                     NULL, NULL,
                     libbalsa_POINTER__INT, G_TYPE_POINTER, 1,
		     G_TYPE_INT);

    libbalsa_mailbox_signals[ADD_MESSAGE] =
	g_signal_new("add-message",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     add_message),
                     NULL, NULL,
                     libbalsa_INT__POINTER_INT, G_TYPE_INT, 2,
		     G_TYPE_POINTER, G_TYPE_INT);
    libbalsa_mailbox_signals[CHANGE_MESSAGE_FLAGS] =
	g_signal_new("change-message-flags",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     change_message_flags),
                     NULL, NULL,
                     libbalsa_VOID__INT_INT_INT, G_TYPE_NONE, 3,
		     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

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

    klass->get_message_stream = NULL;
    klass->check = NULL;
    klass->message_match = libbalsa_mailbox_real_message_match;
    klass->mailbox_match = libbalsa_mailbox_real_mbox_match;
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
    mailbox->message_list = NULL;

    mailbox->readonly = FALSE;
    mailbox->disconnected = FALSE;

    mailbox->filters=NULL;
    mailbox->view=NULL;
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
    gboolean res;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[OPEN_MAILBOX], 0, &res);
    return res;
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
#if NOTUSED
    /* be cautious: implement second line of defence */
    g_return_val_if_fail(mailbox->message_list != NULL, FALSE);
#endif
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
	
	return(mimap->handle != NULL);
    };

    return FALSE; // this will break unlisted mailbox types
}
    
void
libbalsa_mailbox_close(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    /* remove messages flagged for deleting, before closing: */
    libbalsa_mailbox_sync_backend(mailbox, TRUE);
    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[CLOSE_MAILBOX], 0);
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

    g_signal_emit(G_OBJECT(mailbox), libbalsa_mailbox_signals[CHECK], 0);
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
libbalsa_mailbox_message_match(LibBalsaMailbox* mailbox,
			       LibBalsaMessage * message,
			       int op, GSList* conditions)
{
    gboolean retval = FALSE;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[MESSAGE_MATCH], 0,
                  message, op, conditions, &retval);
    return retval;
}

/* libbalsa_mailbox_match:
   Compute the messages matching the filters.
   Virtual method : it is redefined by IMAP
 */
void
libbalsa_mailbox_match(LibBalsaMailbox* mailbox,
		       GSList* filters_list)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[MAILBOX_MATCH], 0,
                  filters_list);
}

void libbalsa_mailbox_real_mbox_match(LibBalsaMailbox * mbox,
				      GSList * filter_list)
{
    LOCK_MAILBOX(mbox);
    libbalsa_filter_match(filter_list, mbox->message_list, TRUE);
    UNLOCK_MAILBOX(mbox);
}

gboolean libbalsa_mailbox_real_can_match(LibBalsaMailbox* mailbox,
					 GSList * conditions)
{
    /* By default : all filters is OK */
    return TRUE;
}

gboolean libbalsa_mailbox_can_match(LibBalsaMailbox * mailbox,
				    GSList * conditions)
{
    gboolean retval;

    g_return_val_if_fail(mailbox!=NULL, FALSE);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[CAN_MATCH], 0,
                  conditions, &retval);
    
    return retval;
}

/* Helper function to run the "on reception" filters on a mailbox */

void
libbalsa_mailbox_run_filters_on_reception(LibBalsaMailbox * mailbox, GSList * filters)
{
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
    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[SAVE_CONFIG], 0, prefix);

    gnome_config_pop_prefix();
}

void
libbalsa_mailbox_load_config(LibBalsaMailbox * mailbox,
			     const gchar * prefix)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gnome_config_push_prefix(prefix);
    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[LOAD_CONFIG], 0, prefix);

    gnome_config_pop_prefix();
}

GMimeStream *
libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
				    LibBalsaMessage * message)
{
    GMimeStream *retval = NULL;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    g_return_val_if_fail(message->mailbox == mailbox, NULL);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[GET_MESSAGE_STREAM], 0, 
                  message, &retval);

    return retval;
}


static void
libbalsa_mailbox_real_close(LibBalsaMailbox * mailbox)
{
    static const int RETRIES_COUNT = 100;
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
//	    usleep(100000); /* wait tenth second */
	    libbalsa_mailbox_check(mailbox);
	    LOCK_MAILBOX(mailbox);
	    cnt++;
	}
	if(cnt>=RETRIES_COUNT)
	    g_print("libbalsa_mailbox_real_close: changes to %s lost.\n",
		    mailbox->name);

	libbalsa_mailbox_free_messages(mailbox);
    } else libbalsa_mailbox_sync_backend_real(mailbox, TRUE);

    UNLOCK_MAILBOX(mailbox);
}

static void
libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean flag)
{
    mailbox->has_unread_messages = flag;
}

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

/* libbalsa_mailbox_link_message:
   MAKE sure the mailbox is LOCKed before entering this routine.
*/ 
void
libbalsa_mailbox_link_message(LibBalsaMailbox * mailbox, LibBalsaMessage*msg)
{
    msg->mailbox = mailbox;
    mailbox->message_list = g_list_prepend(mailbox->message_list, msg);
    
    if (LIBBALSA_MESSAGE_IS_DELETED(msg))
        return;
    if (LIBBALSA_MESSAGE_IS_UNREAD(msg))
        mailbox->unread_messages++;
    mailbox->total_messages++;
}

/*
 * private 
 * PS: called by mail_progress_notify_cb:
 * loads incrementally new messages, if any.
 *  Mailbox lock MUST NOT BE HELD before calling this function.
 *  gdk_lock MUST BE HELD before calling this function because it is called
 *  from both threading and not threading code and we want to be on the safe
 *  side.
 */
void
libbalsa_mailbox_load_messages(LibBalsaMailbox * mailbox)
{
    glong msgno;
    LibBalsaMessage *message;
    GList *messages=NULL;

    if (MAILBOX_CLOSED(mailbox))
	return;

    LOCK_MAILBOX(mailbox);
    for (msgno = mailbox->messages; mailbox->new_messages > 0; msgno++) {
	message = libbalsa_mailbox_load_message(mailbox, msgno);
	if (!message)
		continue;
	libbalsa_message_headers_update(message);
	libbalsa_mailbox_link_message(mailbox, message);
	messages=g_list_prepend(messages, message);
    }
    UNLOCK_MAILBOX(mailbox);

    if(messages!=NULL){
	messages = g_list_reverse(messages);
	g_signal_emit(G_OBJECT(mailbox),
		      libbalsa_mailbox_signals[MESSAGES_ADDED], 0, messages);
	g_list_free(messages);
    }

    libbalsa_mailbox_set_unread_messages_flag(mailbox,
					      mailbox->unread_messages > 0);
}

/* libbalsa_mailbox_free_messages:
   removes all the messages from the mailbox.
   Messages are unref'ed and not directly destroyed because they migt
   be ref'ed from somewhere else.
   Mailbox lock MUST BE HELD before calling this function.
*/
void
libbalsa_mailbox_free_messages(LibBalsaMailbox * mailbox)
{
    GList *list;
    LibBalsaMessage *message;

    list = g_list_first(mailbox->message_list);

    if(list){
	g_signal_emit(G_OBJECT(mailbox),
		      libbalsa_mailbox_signals[MESSAGES_REMOVED], 0, list);
    }

    while (list) {
	message = list->data;
	list = list->next;

	message->mailbox = NULL;
	g_object_unref(G_OBJECT(message));
    }

    g_list_free(mailbox->message_list);
    mailbox->message_list = NULL;
    mailbox->messages = 0;
    mailbox->total_messages = 0;
    mailbox->unread_messages = 0;
}

/* libbalsa_mailbox_commit:
   commits the data to storage (file, IMAP server, etc).

   While the connection via pointers to mutt's HEADERs may seem
   fragile, it is sufficient to notice that the only situation when
   HEADERs are destroyed are mx_sync_mailbox or mx_close_mailbox(). In
   first case, only deleted messages are destroyed, in the second --
   all of them.

   This simple picture is somewhat blurred by the fact that mutt
   sometimes recovers from errors by mx_fastclose_mailbox(). Hm...

   Returns TRUE on success, FALSE on failure.  */
gboolean
libbalsa_mailbox_commit(LibBalsaMailbox* mailbox)
{
    int rc;

    if (MAILBOX_CLOSED(mailbox))
	return FALSE;

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);
    /* remove messages flagged for deleting, before committing: */
    libbalsa_mailbox_sync_backend_real(mailbox, TRUE);
    rc = libbalsa_mailbox_sync_storage(mailbox);

    if (rc == TRUE) {
	    UNLOCK_MAILBOX(mailbox);
	    if (mailbox->new_messages) {
		    libbalsa_mailbox_load_messages(mailbox);
		    rc = 0;
	    }
    } else {
        UNLOCK_MAILBOX(mailbox);
    }

    return rc;
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
#endif
/* libbalsa_mailbox_sync_backend_real
 * synchronize the frontend and libbalsa: build a list of messages
 * marked as deleted, and:
 * 1. emit the "messages-removed" signal, so the frontend can drop them
 *    from the BalsaIndex;
 * 2. if delete == TRUE, delete them from the LibBalsaMailbox.
 */
static void
libbalsa_mailbox_sync_backend_real(LibBalsaMailbox * mailbox,
                                   gboolean delete)
{
    GList *list;
    GList *message_list;
    LibBalsaMessage *current_message;
    GList *p=NULL;
    GList *q = NULL;

    for (message_list = mailbox->message_list; message_list;
         message_list = g_list_next(message_list)) {
	current_message = LIBBALSA_MESSAGE(message_list->data);
	if (LIBBALSA_MESSAGE_IS_DELETED(current_message)) {
	    p=g_list_prepend(p, current_message);
            if (delete)
                q = g_list_prepend(q, message_list);
	}
    }

    if (p) {
	UNLOCK_MAILBOX(mailbox);
	g_signal_emit(G_OBJECT(mailbox),
	              libbalsa_mailbox_signals[MESSAGES_REMOVED],0,p);
	LOCK_MAILBOX(mailbox);
	g_list_free(p);
    }

    if (delete) {
        for (list = q; list; list = g_list_next(list)) {
            message_list = list->data;
            current_message =
                LIBBALSA_MESSAGE(message_list->data);
            current_message->mailbox = NULL;
            g_object_unref(G_OBJECT(current_message));
	    mailbox->message_list =
		g_list_remove_link(mailbox->message_list, message_list);
            g_list_free_1(message_list);
	}
        g_list_free(q);
    }
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
}

int libbalsa_mailbox_copy_message(LibBalsaMessage *message, LibBalsaMailbox *dest)
{
    GMimeStream *msg_stream;
    int result;

    msg_stream = libbalsa_mailbox_get_message_stream(message->mailbox, message);
    if (msg_stream == NULL)
	return -1;
    result = libbalsa_mailbox_add_message_stream(dest,
						 msg_stream, message->flags);
    g_mime_stream_unref(msg_stream);
    return result;
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

gboolean libbalsa_mailbox_close_backend(LibBalsaMailbox * mailbox)
{
    gboolean retval = FALSE;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    if (libbalsa_mailbox_sync_storage(mailbox) == FALSE)
	return FALSE;

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[CLOSE_BACKEND], 0, &retval);
    return retval;
}

gboolean libbalsa_mailbox_sync_storage(LibBalsaMailbox * mailbox)
{
    gboolean retval = FALSE;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);
    g_return_val_if_fail(!mailbox->readonly, TRUE);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[SYNC], 0, &retval);
    return retval;
}

GMimeMessage *libbalsa_mailbox_get_message(LibBalsaMailbox * mailbox, guint msgno)
{
    GMimeMessage *retval = NULL;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[GET_MESSAGE], 0, msgno, &retval);
    return retval;
}

LibBalsaMessage *libbalsa_mailbox_load_message(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *retval = NULL;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[LOAD_MESSAGE], 0, msgno, &retval);
    return retval;
}

int libbalsa_mailbox_add_message_stream(LibBalsaMailbox * mailbox,
					GMimeStream *msg,
					LibBalsaMessageFlag flags)
{
    int retval = -1;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    g_mime_stream_reset(msg);
    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[ADD_MESSAGE], 0, msg, flags,
		  &retval);
    return retval;
}

void libbalsa_mailbox_change_message_flags(LibBalsaMailbox * mailbox, guint msgno,
					   LibBalsaMessageFlag set,
					   LibBalsaMessageFlag clear)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[CHANGE_MESSAGE_FLAGS], 0,
		  msgno, set, clear);
}

/*
 * Mailbox views
 */
LibBalsaMailboxView *
libbalsa_mailbox_view_new(void)
{
    LibBalsaMailboxView *view = g_new(LibBalsaMailboxView, 1);

    view->mailing_list_address = NULL;
    view->identity_name=NULL;
    view->threading_type = LB_MAILBOX_THREADING_JWZ;
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
