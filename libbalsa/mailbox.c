/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#include "config.h"

#include <ctype.h>
#include <unistd.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif


#include "libbalsa.h"
#include "mailbackend.h"
#include "misc.h"
#include "message.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#include <libgnome/gnome-defs.h> 
#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 

#include "mailbox-filter.h"
#include "libbalsa_private.h"

/* GTK_CLASS_TYPE for 1.2<->1.3/2.0 GTK+ compatibility */
#ifndef GTK_CLASS_TYPE
#define GTK_CLASS_TYPE(x) (GTK_OBJECT_CLASS(x)->type)
#endif /* GTK_CLASS_TYPE */

/* Class functions */
static void libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass);
static void libbalsa_mailbox_init(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_destroy(GtkObject * object);

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
static void message_status_changed_cb(LibBalsaMessage * message, gboolean set,
				      LibBalsaMailbox * mb);

static void libbalsa_mailbox_sync_backend_real(LibBalsaMailbox * mailbox,
                                               gboolean delete);
static LibBalsaMessage *translate_message(HEADER * cur);

enum {
    OPEN_MAILBOX,
    OPEN_MAILBOX_APPEND,
    CLOSE_MAILBOX,
    MESSAGE_STATUS_CHANGED,
    MESSAGE_NEW,
    MESSAGES_NEW,
    MESSAGE_DELETE,
    MESSAGES_DELETE,
    GET_MESSAGE_STREAM,
    CHECK,
    GET_MATCHING,
    SET_UNREAD_MESSAGES_FLAG,
    SAVE_CONFIG,
    LOAD_CONFIG,
    LAST_SIGNAL
};

static GtkObjectClass *parent_class = NULL;
static guint libbalsa_mailbox_signals[LAST_SIGNAL];

GtkType libbalsa_mailbox_get_type(void)
{
    static GtkType mailbox_type = 0;

    if (!mailbox_type) {
	static const GtkTypeInfo mailbox_info = {
	    "LibBalsaMailbox",
	    sizeof(LibBalsaMailbox),
	    sizeof(LibBalsaMailboxClass),
	    (GtkClassInitFunc) libbalsa_mailbox_class_init,
	    (GtkObjectInitFunc) libbalsa_mailbox_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	mailbox_type =
	    gtk_type_unique(gtk_object_get_type(), &mailbox_info);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass)
{
    GtkObjectClass *object_class;

    object_class = (GtkObjectClass *) klass;

    parent_class = gtk_type_class(gtk_object_get_type());

    libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED] =
	gtk_signal_new("message-status-changed",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 message_status_changed),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       LIBBALSA_TYPE_MESSAGE);

    libbalsa_mailbox_signals[OPEN_MAILBOX] =
	gtk_signal_new("open-mailbox",
		       GTK_RUN_LAST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 open_mailbox),
		       gtk_marshal_BOOL__NONE, GTK_TYPE_BOOL, 0);

    libbalsa_mailbox_signals[OPEN_MAILBOX_APPEND] =
	gtk_signal_new("append-mailbox",
		       GTK_RUN_LAST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 open_mailbox_append),
		       libbalsa_marshal_POINTER__NONE, GTK_TYPE_POINTER, 0);

    libbalsa_mailbox_signals[CLOSE_MAILBOX] =
	gtk_signal_new("close-mailbox",
		       GTK_RUN_LAST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 close_mailbox),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    libbalsa_mailbox_signals[MESSAGE_NEW] =
	gtk_signal_new("message-new",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 message_new),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       LIBBALSA_TYPE_MESSAGE);

    libbalsa_mailbox_signals[MESSAGES_NEW] =
	gtk_signal_new("messages-new",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 messages_new),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);

    libbalsa_mailbox_signals[MESSAGE_DELETE] =
	gtk_signal_new("message-delete",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 message_delete),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       LIBBALSA_TYPE_MESSAGE);

    libbalsa_mailbox_signals[MESSAGES_DELETE] =
	gtk_signal_new("messages-delete",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 messages_delete),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);

    libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG] =
	gtk_signal_new("set-unread-messages-flag",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 set_unread_messages_flag),
		       gtk_marshal_NONE__BOOL, GTK_TYPE_NONE, 1,
		       GTK_TYPE_BOOL);

    /* Virtual functions. Use GTK_RUN_NO_HOOKS
       which prevents the signal being connected to them */
    libbalsa_mailbox_signals[GET_MESSAGE_STREAM] =
	gtk_signal_new("get-message-stream",
		       GTK_RUN_LAST | GTK_RUN_NO_HOOKS,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 get_message_stream),
		       libbalsa_marshal_POINTER__OBJECT, GTK_TYPE_POINTER,
		       1, LIBBALSA_TYPE_MESSAGE);
    libbalsa_mailbox_signals[CHECK] =
	gtk_signal_new("check", GTK_RUN_LAST | GTK_RUN_NO_HOOKS,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass, check),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
    libbalsa_mailbox_signals[GET_MATCHING] =
	gtk_signal_new("get-matching", GTK_RUN_LAST | GTK_RUN_NO_HOOKS,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass, get_matching),
		       libbalsa_marshal_POINTER__INT_POINTER, 
                       GTK_TYPE_POINTER, 2, GTK_TYPE_INT, GTK_TYPE_POINTER);

    libbalsa_mailbox_signals[SAVE_CONFIG] =
	gtk_signal_new("save-config",
		       GTK_RUN_LAST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 save_config),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);

    libbalsa_mailbox_signals[LOAD_CONFIG] =
	gtk_signal_new("load-config",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 load_config),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);

    gtk_object_class_add_signals(object_class, libbalsa_mailbox_signals,
				 LAST_SIGNAL);

    object_class->destroy = libbalsa_mailbox_destroy;

    klass->open_mailbox = NULL;
    klass->close_mailbox = libbalsa_mailbox_real_close;
    klass->set_unread_messages_flag =
	libbalsa_mailbox_real_set_unread_messages_flag;

    klass->message_status_changed = NULL;
    klass->message_new = NULL;
    klass->messages_new = NULL;
    klass->message_delete = NULL;
    klass->messages_delete = NULL;

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
}

/* libbalsa_mailbox_destroy:
   destroys mailbox. Must leave it in sane state.
*/
static void
libbalsa_mailbox_destroy(GtkObject * object)
{
    LibBalsaMailbox *mailbox;
    g_return_if_fail(object);

    mailbox = LIBBALSA_MAILBOX(object);

    /* g_print("Destroying mailbox: %s\n", mailbox->name); */
    while (mailbox->open_ref > 0)
	libbalsa_mailbox_close(mailbox);

    if ( mailbox->mailing_list_address )
	gtk_object_unref(GTK_OBJECT(mailbox->mailing_list_address));
    mailbox->mailing_list_address = NULL;

    g_free(mailbox->config_prefix);
    mailbox->config_prefix = NULL;
    g_free(mailbox->name);
    mailbox->name = NULL;
    g_free(mailbox->url);
    mailbox->url = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

    g_free(mailbox->identity_name);
    mailbox->identity_name = NULL;
}

/* Create a new mailbox by loading it from a config entry... */
LibBalsaMailbox *
libbalsa_mailbox_new_from_config(const gchar * prefix)
{
    gchar *type_str;
    GtkType type;
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
    type = gtk_type_from_name(type_str);
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
    mailbox = gtk_type_new(type);
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

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[OPEN_MAILBOX], &res);
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
    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[CLOSE_MAILBOX]);
    if(mailbox->open_ref <1) mailbox->context = NULL;
}

LibBalsaMailboxAppendHandle* 
libbalsa_mailbox_open_append(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxAppendHandle* res = NULL;
    g_return_val_if_fail(mailbox != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[OPEN_MAILBOX_APPEND], &res);
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

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG],
		    has_unread);
}

void
libbalsa_mailbox_check(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[CHECK]);
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

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[GET_MATCHING], 
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
    gnome_config_private_clean_section(prefix);
    gnome_config_clean_section(prefix);

    gnome_config_push_prefix(prefix);
    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[SAVE_CONFIG], prefix);

    gnome_config_pop_prefix();
}

void
libbalsa_mailbox_load_config(LibBalsaMailbox * mailbox,
			     const gchar * prefix)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gnome_config_push_prefix(prefix);
    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[LOAD_CONFIG], prefix);

    gnome_config_pop_prefix();
}

FILE *
libbalsa_mailbox_get_message_stream(LibBalsaMailbox * mailbox,
				    LibBalsaMessage * message)
{
    FILE *retval = NULL;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    g_return_val_if_fail(message->mailbox == mailbox, NULL);

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[GET_MESSAGE_STREAM], message,
		    &retval);

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
        if(match_conditions(op, conditions, msg))
            g_hash_table_insert(ret, msg, msg);
    }
    return ret;
}


static void
libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    gchar *tmp;

    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gnome_config_set_string("Type",
			    gtk_type_name(GTK_OBJECT_TYPE(mailbox)));
    gnome_config_set_string("Name", mailbox->name);

    if ( mailbox->mailing_list_address ) {
	tmp = libbalsa_address_to_gchar(mailbox->mailing_list_address, 0);
	gnome_config_set_string("MailingListAddress", tmp);
	g_free(tmp);
    } else {
	gnome_config_clean_key("MailingListAddress");
    }
    g_free(mailbox->config_prefix);
    mailbox->config_prefix = g_strdup(prefix);

    gnome_config_set_string("Identity", mailbox->identity_name);
}

static void
libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    gboolean def;
    gchar *address;
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    g_free(mailbox->config_prefix);
    mailbox->config_prefix = g_strdup(prefix);

    g_free(mailbox->name);
    mailbox->name = gnome_config_get_string("Name=Mailbox");

    if ( mailbox->mailing_list_address ) 
	gtk_object_unref(GTK_OBJECT(mailbox->mailing_list_address));
    address = gnome_config_get_string_with_default("MailingListAddress", &def);
    if ( def == TRUE ) {
	mailbox->mailing_list_address = NULL;
    } else {
	mailbox->mailing_list_address = 
            libbalsa_address_new_from_string(address);
    }
    g_free(address);

    g_free(mailbox->identity_name);
    mailbox->identity_name = gnome_config_get_string("Identity=Default");
}

/* libbalsa_mailbox_link_message:
   MAKE sure the mailbox is LOCKed before entering this routine.
*/ 
void
libbalsa_mailbox_link_message(LibBalsaMailbox * mailbox, LibBalsaMessage*msg)
{
    msg->mailbox = mailbox;
    
    gtk_signal_connect_after(GTK_OBJECT(msg), "clear-flags",
                       GTK_SIGNAL_FUNC(message_status_changed_cb),
                       mailbox);
    gtk_signal_connect_after(GTK_OBJECT(msg), "set-answered",
                       GTK_SIGNAL_FUNC(message_status_changed_cb),
                       mailbox);
    gtk_signal_connect_after(GTK_OBJECT(msg), "set-read",
                       GTK_SIGNAL_FUNC(message_status_changed_cb),
                       mailbox);
    gtk_signal_connect_after(GTK_OBJECT(msg), "set-deleted",
                       GTK_SIGNAL_FUNC(message_status_changed_cb),
                       mailbox);
    gtk_signal_connect_after(GTK_OBJECT(msg), "set-flagged",
                       GTK_SIGNAL_FUNC(message_status_changed_cb),
                       mailbox);

    if (msg->flags & LIBBALSA_MESSAGE_FLAG_NEW)
        mailbox->unread_messages++;
    /* take over the ownership */
    gtk_object_ref ( GTK_OBJECT(msg) );
    gtk_object_sink( GTK_OBJECT(msg) );
    mailbox->message_list = g_list_append(mailbox->message_list, msg);
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

    /* drop the lock while we do the grunt work */
    gdk_threads_leave();

    LOCK_MAILBOX(mailbox);
    for (msgno = mailbox->messages; mailbox->new_messages > 0; msgno++) {
	cur = CLIENT_CONTEXT(mailbox)->hdrs[msgno];

	if (!(cur && cur->env && cur->content)) {
            /* we'd better decrement mailbox->new_messages, in case this
             * defective message was included in the count; otherwise,
             * we'll increment msgno too far, and try to access an
             * invalid header */
            mailbox->new_messages--;
	    continue;
        }

	if (cur->env->subject &&
	    !strcmp(cur->env->subject,
		    "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA")) {
	    mailbox->new_messages--;
	    mailbox->messages++;
	    continue;
	}

	message = translate_message(cur);
        libbalsa_mailbox_link_message(mailbox, message);
	messages=g_list_prepend(messages, message);
        mailbox->new_messages--;
        mailbox->messages++;
    }
    UNLOCK_MAILBOX(mailbox);

    /* reaquire the lock, after releasing mailbox and before doing stuff */
    gdk_threads_enter();

    if(messages!=NULL){
	/* FIXME : I keep order as before, but I don't think this is important
	   because we do not rely on this order, do we ?
	*/
	messages = g_list_reverse(messages);
	gtk_signal_emit(GTK_OBJECT(mailbox),
			libbalsa_mailbox_signals[MESSAGES_NEW], messages);
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
      gtk_signal_emit(GTK_OBJECT(mailbox),
		      libbalsa_mailbox_signals[MESSAGES_DELETE], list);
    }

    while (list) {
	message = list->data;
	list = list->next;

	message->mailbox = NULL;
	gtk_object_unref(GTK_OBJECT(message));
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

GtkType
libbalsa_mailbox_type_from_path(const gchar * path)
/* libbalsa_get_mailbox_storage_type:
   returns one of LIBBALSA_TYPE_MAILBOX_IMAP,
   LIBBALSA_TYPE_MAILBOX_MAILDIR, LIBBALSA_TYPE_MAILBOX_MH,
   LIBBALSA_TYPE_MAILBOX_MBOX.  GTK_TYPE_OBJECT on error or a directory.
 */
{
    struct stat st;
    char tmp[_POSIX_PATH_MAX];

    if(strncmp(path, "imap://", 7) == 0)
        return LIBBALSA_TYPE_MAILBOX_IMAP;

    if (stat (path, &st) == -1)
        return GTK_TYPE_OBJECT;
    
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
    return GTK_TYPE_OBJECT;
}

/* libbalsa_mailbox_sync_backend_real
 * synchronize the frontend and libbalsa: build a list of messages
 * marked as deleted, and:
 * 1. emit the "messages-delete" signal, so the frontend can drop them
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
	    p=g_list_append(p, current_message);
            if (delete)
                q = g_list_prepend(q, message_list);
	}
    }

    if(p){
	gtk_signal_emit(GTK_OBJECT(mailbox),
			libbalsa_mailbox_signals[MESSAGES_DELETE],
			p);
	g_list_free(p);
    }

    if (delete) {
        for (list = q; list; list = g_list_next(list)) {
            message_list = list->data;
            current_message =
                LIBBALSA_MESSAGE(message_list->data);
            current_message->mailbox = NULL;
            gtk_object_unref(GTK_OBJECT(current_message));
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

static void
message_status_changed_cb(LibBalsaMessage * message, gboolean set,
                          LibBalsaMailbox * mb)
{
    gtk_signal_emit(GTK_OBJECT(message->mailbox),
		    libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED],
		    message);
}


