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


#include "libbalsa.h"
#include "mailbackend.h"
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
static GHashTable* libbalsa_mailbox_real_get_matching(LibBalsaMailbox* mailbox,
                                                      int op, 
                                                      GSList* conditions);

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
static LibBalsaMessage *translate_message(HEADER * cur);

/* SIGNALS MEANINGS :
   - OPEN_MAILBOX[_APPEND], CLOSE_MAILBOX, SAVE/LOAD_CONFIG, CHECK,
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
    OPEN_MAILBOX_APPEND,
    CLOSE_MAILBOX,
    MESSAGES_STATUS_CHANGED,
    MESSAGES_ADDED,
    MESSAGES_REMOVED,
    GET_MESSAGE_STREAM,
    PROGRESS_NOTIFY,
    CHECK,
    GET_MATCHING,
    SET_UNREAD_MESSAGES_FLAG,
    SAVE_CONFIG,
    LOAD_CONFIG,
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

    libbalsa_mailbox_signals[OPEN_MAILBOX_APPEND] =
	g_signal_new("append-mailbox",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     open_mailbox_append),
                     NULL, NULL,
                     libbalsa_POINTER__VOID, G_TYPE_POINTER, 0);

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

    libbalsa_mailbox_signals[GET_MATCHING] =
	g_signal_new("get-matching",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(LibBalsaMailboxClass,
                                     get_matching),
                     NULL, NULL,
                     libbalsa_POINTER__INT_POINTER, 
                     G_TYPE_POINTER, 2, G_TYPE_INT, G_TYPE_POINTER);

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
    klass->get_matching = libbalsa_mailbox_real_get_matching;
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
    CLIENT_CONTEXT(mailbox) = NULL;

    mailbox->open_ref = 0;
    mailbox->messages = 0;
    mailbox->new_messages = 0;
    mailbox->has_unread_messages = FALSE;
    mailbox->unread_messages = 0;
    mailbox->total_messages = 0;
    mailbox->message_list = NULL;

    mailbox->readonly = FALSE;
    mailbox->disconnected = FALSE;
    mailbox->mailing_list_address = NULL;

    mailbox->filters=NULL;
    mailbox->identity_name=NULL;
    mailbox->threading_type = LB_MAILBOX_THREADING_JWZ;
    mailbox->sort_type =  LB_MAILBOX_SORT_TYPE_ASC;
    mailbox->sort_field = LB_MAILBOX_SORT_DATE;
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

    if ( mailbox->mailing_list_address )
	g_object_unref(mailbox->mailing_list_address);
    mailbox->mailing_list_address = NULL;

    g_free(mailbox->config_prefix);
    mailbox->config_prefix = NULL;
    g_free(mailbox->name);
    mailbox->name = NULL;
    g_free(mailbox->url);
    mailbox->url = NULL;
    g_free(mailbox->identity_name);
    mailbox->identity_name = NULL;

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
    }
    mailbox = g_object_new(type, NULL);
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
    if(CLIENT_CONTEXT_CLOSED(mailbox)) return FALSE;
    /* be cautious: implement second line of defence */
    g_return_val_if_fail(CLIENT_CONTEXT(mailbox)->hdrs != NULL, FALSE);
    return TRUE;
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
    if(mailbox->open_ref <1) mailbox->context = NULL;
}

LibBalsaMailboxAppendHandle* 
libbalsa_mailbox_open_append(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxAppendHandle* res = NULL;
    g_return_val_if_fail(mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[OPEN_MAILBOX_APPEND], 0, &res);
    return res;
}

int 
libbalsa_mailbox_close_append(LibBalsaMailboxAppendHandle* handle)
{
    int ret;
    g_return_val_if_fail(handle != NULL, -1);

    libbalsa_lock_mutt();
    ret = mx_close_mailbox(handle->context, NULL);
    libbalsa_unlock_mutt();
    g_free(handle);
    return ret;
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

/* libbalsa_mailbox_get_matching:
 * get a hash table of messages matching given set of conditions.
 */
GHashTable*
libbalsa_mailbox_get_matching(LibBalsaMailbox* mailbox, int op, 
                              GSList* conditions)
{
    GHashTable* retval = NULL;
    g_return_val_if_fail(mailbox != NULL, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), FALSE);

    g_signal_emit(G_OBJECT(mailbox),
		  libbalsa_mailbox_signals[GET_MATCHING], 0,
                  op, conditions, &retval);
    return retval;
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

/* libbalsa_mailbox_load_view:
   load view related data from current config context. Works also for
   nameless, scanned mailboxes.
*/
void
libbalsa_mailbox_load_view(LibBalsaMailbox * mbx)
{
    int def;
    gchar *address;

    if (mbx->mailing_list_address) 
	g_object_unref(mbx->mailing_list_address);
    address = gnome_config_get_string_with_default("MailingListAddress", &def);
    mbx->mailing_list_address = 
	def ? NULL : libbalsa_address_new_from_string(address);
    g_free(address);

    g_free(mbx->identity_name);
    mbx->identity_name = gnome_config_get_string("Identity");

    mbx->threading_type = gnome_config_get_int_with_default("Threading", &def);
    if(def) mbx->threading_type = LB_MAILBOX_THREADING_SIMPLE;
    mbx->sort_type = gnome_config_get_int_with_default("SortType", &def);
    if(def) mbx->sort_type = LB_MAILBOX_SORT_TYPE_ASC;
    mbx->sort_field = gnome_config_get_int_with_default("SortField", &def);
    if(def) mbx->sort_field = LB_MAILBOX_SORT_DATE;
}

/* libbalsa_mailbox_save_view:
   save view related data from current config context. Works also for
   nameless, scanned mailboxes.
*/
void
libbalsa_mailbox_save_view(LibBalsaMailbox * mbx)
{
    if (mbx->mailing_list_address) {
	gchar* tmp = libbalsa_address_to_gchar(mbx->mailing_list_address, 0);
	gnome_config_set_string("MailingListAddress", tmp);
	g_free(tmp);
    } else {
	gnome_config_clean_key("MailingListAddress");
    }
    if(mbx->identity_name)
	gnome_config_set_string("Identity", mbx->identity_name);
    else gnome_config_clean_key("Identity");
    gnome_config_set_int("Threading",   mbx->threading_type);
    gnome_config_set_int("SortType",    mbx->sort_type);
    gnome_config_set_int("SortField",   mbx->sort_field);
}

FILE *
libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
				    LibBalsaMessage * message)
{
    FILE *retval = NULL;

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
	libbalsa_mailbox_free_messages(mailbox);

	/* now close the mail stream and expunge deleted
	 * messages -- the expunge may not have to be done */
	/* We are careful to take/release locks in the correct order here */
	libbalsa_lock_mutt();
	cnt = 0;
	while( CLIENT_CONTEXT_OPEN(mailbox) && cnt < RETRIES_COUNT &&
	       (check = mx_close_mailbox(CLIENT_CONTEXT(mailbox), NULL))) {
	    libbalsa_unlock_mutt();
	    UNLOCK_MAILBOX(mailbox);
	    g_print
		("libbalsa_mailbox_real_close: %d trial failed.\n", cnt);
	    usleep(100000); /* wait tenth second */
	    libbalsa_mailbox_check(mailbox);
	    LOCK_MAILBOX(mailbox);
	    libbalsa_lock_mutt();
	    cnt++;
	}
	libbalsa_unlock_mutt();
	if(cnt>=RETRIES_COUNT)
	    g_print("libbalsa_mailbox_real_close: changes to %s lost.\n",
		    mailbox->name);
	
	if(CLIENT_CONTEXT(mailbox)) {
	    free(CLIENT_CONTEXT(mailbox));
	    CLIENT_CONTEXT(mailbox) = NULL;
	}
    } else libbalsa_mailbox_sync_backend_real(mailbox, TRUE);

    UNLOCK_MAILBOX(mailbox);
}

static void
libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean flag)
{
    mailbox->has_unread_messages = flag;
}

static GHashTable*
libbalsa_mailbox_real_get_matching(LibBalsaMailbox* mailbox, 
                                   int op, GSList* conditions)
{
    GHashTable * ret = g_hash_table_new(NULL,NULL);
    GList* msgs;
    printf("real op=%d list=%p\n", op, conditions);
    for(msgs = mailbox->message_list; msgs; msgs = msgs->next) {
        LibBalsaMessage* msg = LIBBALSA_MESSAGE(msgs->data);
        if(match_conditions(op, conditions, msg, FALSE))
            g_hash_table_insert(ret, msg, msg);
    }
    return ret;
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
    
    if (msg->flags & LIBBALSA_MESSAGE_FLAG_DELETED)
        return;
    if (msg->flags & LIBBALSA_MESSAGE_FLAG_NEW)
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
    HEADER *cur = 0;
    GList *messages=NULL;

    if (CLIENT_CONTEXT_CLOSED(mailbox))
	return;

    LOCK_MAILBOX(mailbox);
    for (msgno = mailbox->messages; mailbox->new_messages > 0; msgno++) {
	cur = CLIENT_CONTEXT(mailbox)->hdrs[msgno];

        mailbox->new_messages--;

	if (!(cur && cur->env && cur->content))
	    continue;

        mailbox->messages++;

	if (cur->env->subject &&
	    !strcmp(cur->env->subject,
		    "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA"))
	    continue;

	message = translate_message(cur);
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
    int index_hint;

    if (CLIENT_CONTEXT_CLOSED(mailbox))
	return FALSE;

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);
    /* remove messages flagged for deleting, before committing: */
    libbalsa_mailbox_sync_backend_real(mailbox, TRUE);
    libbalsa_lock_mutt();
    index_hint = CLIENT_CONTEXT(mailbox)->vcount;
    rc = mx_sync_mailbox(CLIENT_CONTEXT(mailbox), &index_hint);
    libbalsa_unlock_mutt();
    if(rc==0) 
        mailbox->messages = CLIENT_CONTEXT(mailbox)->msgcount;
    
    if(rc == M_NEW_MAIL || rc == M_REOPENED) {
        mailbox->new_messages =
            CLIENT_CONTEXT(mailbox)->msgcount - mailbox->messages;
        
	    UNLOCK_MAILBOX(mailbox);
	    libbalsa_mailbox_load_messages(mailbox);
            rc = 0;
    } else {
        UNLOCK_MAILBOX(mailbox);
    }

    return rc ==0;
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
	if (current_message->flags & LIBBALSA_MESSAGE_FLAG_DELETED) {
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
    g_return_val_if_fail(CLIENT_CONTEXT_OPEN(mailbox), 0);
    LOCK_MAILBOX_RETURN_VAL(mailbox, 0);
    libbalsa_mailbox_sync_backend_real(mailbox, delete);
    UNLOCK_MAILBOX(mailbox);
    return res;
}


/* internal c-client translation:
 * mutt lists can contain null adresses for address strings like
 * "To: Dear Friends,". We do remove them.
 */
static LibBalsaMessage *
translate_message(HEADER * cur)
{
    LibBalsaMessage *message;

    if (!cur)
	return NULL;

    message = libbalsa_message_new();
    message->header = cur;
    message->msgno  = cur->msgno;
        /* set the length */
#ifdef MESSAGE_COPY_CONTENT
    message->length = cur->content->length;
    message->lines_len = cur->lines;
#endif
    if (!cur->read)
        message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
    if (cur->deleted)
        message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
    if (cur->flagged)
        message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
    if (cur->replied)
        message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;

    libbalsa_message_headers_update(message);
    return message;
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
        new_state = (LIBBALSA_MESSAGE(messages->data)->flags
                     & LIBBALSA_MESSAGE_FLAG_DELETED);

        for (; messages; messages = g_list_next(messages)) {
            nb_in_list++;
            if (LIBBALSA_MESSAGE(messages->data)->flags
                & LIBBALSA_MESSAGE_FLAG_NEW)
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
        if (LIBBALSA_MESSAGE(messages->data)->flags
            & LIBBALSA_MESSAGE_FLAG_NEW) {
            gboolean unread_before = mb->unread_messages > 0;

            /* Count only messages with the deleted flag not set */
            for (lst = messages; lst; lst = g_list_next(lst))
                if (!(LIBBALSA_MESSAGE(lst->data)->flags
                      & LIBBALSA_MESSAGE_FLAG_DELETED))
                    mb->unread_messages++;
            if (!unread_before && mb->unread_messages > 0)
                libbalsa_mailbox_set_unread_messages_flag(mb, TRUE);
        } else {
            /* Count only messages with the deleted flag not set */
            for (lst = messages; lst; lst = g_list_next(lst))
                if (!(LIBBALSA_MESSAGE(lst->data)->flags
                      & LIBBALSA_MESSAGE_FLAG_DELETED))
                    mb->unread_messages--;
            if (mb->unread_messages <= 0)
                libbalsa_mailbox_set_unread_messages_flag(mb, FALSE);
        }
    case LIBBALSA_MESSAGE_FLAG_REPLIED:
    case LIBBALSA_MESSAGE_FLAG_FLAGGED:
        break;
    }
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
