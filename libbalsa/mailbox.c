/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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

#include <glib.h>
#include <ctype.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

/* Class functions */
static void libbalsa_mailbox_class_init(LibBalsaMailboxClass * klass);
static void libbalsa_mailbox_init(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_destroy(GtkObject * object);

static void libbalsa_mailbox_real_close(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox
							   * mailbox,
							   gboolean flag);

static void libbalsa_mailbox_real_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_real_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

/* Callbacks */
static void message_status_changed_cb(LibBalsaMessage * message,
				      LibBalsaMailbox * mb);

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
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 message_status_changed),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       LIBBALSA_TYPE_MESSAGE);

    libbalsa_mailbox_signals[OPEN_MAILBOX] =
	gtk_signal_new("open-mailbox",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 open_mailbox),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    libbalsa_mailbox_signals[OPEN_MAILBOX_APPEND] =
	gtk_signal_new("append-mailbox",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 open_mailbox_append),
		       libbalsa_marshal_POINTER__NONE, GTK_TYPE_POINTER, 0);

    libbalsa_mailbox_signals[CLOSE_MAILBOX] =
	gtk_signal_new("close-mailbox",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 close_mailbox),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    libbalsa_mailbox_signals[MESSAGE_NEW] =
	gtk_signal_new("message-new",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 message_new),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       LIBBALSA_TYPE_MESSAGE);

    libbalsa_mailbox_signals[MESSAGES_NEW] =
	gtk_signal_new("messages-new",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 messages_new),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);

    libbalsa_mailbox_signals[MESSAGE_DELETE] =
	gtk_signal_new("message-delete",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 message_delete),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       LIBBALSA_TYPE_MESSAGE);

    libbalsa_mailbox_signals[MESSAGES_DELETE] =
	gtk_signal_new("messages-delete",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 messages_delete),
		       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
		       GTK_TYPE_POINTER);

    libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG] =
	gtk_signal_new("set-unread-messages-flag",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 set_unread_messages_flag),
		       gtk_marshal_NONE__BOOL, GTK_TYPE_NONE, 1,
		       GTK_TYPE_BOOL);

    /* Virtual functions. Use GTK_RUN_NO_HOOKS
       which prevents the signal being connected to them */
    libbalsa_mailbox_signals[GET_MESSAGE_STREAM] =
	gtk_signal_new("get-message-stream",
		       GTK_RUN_LAST | GTK_RUN_NO_HOOKS,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass,
					 get_message_stream),
		       libbalsa_marshal_POINTER__OBJECT, GTK_TYPE_POINTER,
		       1, LIBBALSA_TYPE_MESSAGE);
    libbalsa_mailbox_signals[CHECK] =
	gtk_signal_new("check", GTK_RUN_LAST | GTK_RUN_NO_HOOKS,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxClass, check),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    libbalsa_mailbox_signals[SAVE_CONFIG] =
	gtk_signal_new("save-config",
		       GTK_RUN_LAST,
		       object_class->type,
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
    klass->save_config = libbalsa_mailbox_real_save_config;
    klass->load_config = libbalsa_mailbox_real_load_config;
}

static void
libbalsa_mailbox_init(LibBalsaMailbox * mailbox)
{
    mailbox->lock = FALSE;
    mailbox->is_directory = FALSE;

    mailbox->config_prefix = NULL;
    mailbox->name = NULL;
    CLIENT_CONTEXT(mailbox) = NULL;

    mailbox->open_ref = 0;
    mailbox->messages = 0;
    mailbox->new_messages = 0;
    mailbox->has_unread_messages = FALSE;
    mailbox->unread_messages = 0;
    mailbox->total_messages = 0;
    mailbox->message_list = NULL;

    mailbox->readonly = FALSE;
    mailbox->mailing_list_address = NULL;
}

/* libbalsa_mailbox_destroy:
   destroys mailbox. Must leave it in sane state.
*/
static void
libbalsa_mailbox_destroy(GtkObject * object)
{
    LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);

    if (!mailbox)
	return;

    while (mailbox->open_ref > 0)
	libbalsa_mailbox_close(mailbox);

    if ( mailbox->mailing_list_address )
	gtk_object_unref(GTK_OBJECT(mailbox->mailing_list_address));
    mailbox->mailing_list_address = NULL;

    g_free(mailbox->name);
    mailbox->name = NULL;
    g_free(mailbox->config_prefix);
    mailbox->config_prefix = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
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

void
libbalsa_mailbox_open(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[OPEN_MAILBOX]);
}

void
libbalsa_mailbox_close(LibBalsaMailbox * mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

    gtk_signal_emit(GTK_OBJECT(mailbox),
		    libbalsa_mailbox_signals[CLOSE_MAILBOX]);
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

    ret = mx_close_mailbox(handle->context, NULL);
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
	    usleep(1000);
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
    }

    UNLOCK_MAILBOX(mailbox);
}

static void
libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox * mailbox,
					       gboolean flag)
{
    mailbox->has_unread_messages = flag;
}

void
libbalsa_mailbox_sort(LibBalsaMailbox * mailbox, LibBalsaMailboxSort sort)
{
    libbalsa_lock_mutt();
    mutt_sort_headers(CLIENT_CONTEXT(mailbox), sort);
    libbalsa_unlock_mutt();
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
	mailbox->mailing_list_address = libbalsa_address_new_from_string(address);
    }
    g_free(address);
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

	if (!cur)
	    continue;

	if (cur->env->subject &&
	    !strcmp(cur->env->subject,
		    "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA")) {
	    mailbox->new_messages--;
	    mailbox->messages++;
	    continue;
	}

	message = translate_message(cur);
	message->mailbox = mailbox;
	message->msgno = msgno;

	gtk_signal_connect(GTK_OBJECT(message), "clear-flags",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
	gtk_signal_connect(GTK_OBJECT(message), "set-answered",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
	gtk_signal_connect(GTK_OBJECT(message), "set-read",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
	gtk_signal_connect(GTK_OBJECT(message), "set-deleted",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);
	gtk_signal_connect(GTK_OBJECT(message), "set-flagged",
			   GTK_SIGNAL_FUNC(message_status_changed_cb),
			   mailbox);

	mailbox->messages++;
	mailbox->total_messages++;

	if (!cur->read) {
	    message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;

	    mailbox->unread_messages++;
	}

	if (cur->deleted)
	    message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;

	if (cur->flagged)
	    message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;

	if (cur->replied)
	    message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;

	/* take over the ownership */
	mailbox->message_list =
	    g_list_append(mailbox->message_list, message);
	gtk_object_ref ( GTK_OBJECT(message) );
	gtk_object_sink( GTK_OBJECT(message) );

	mailbox->new_messages--;

	messages=g_list_append(messages, message);
    }
    UNLOCK_MAILBOX(mailbox);

    if(messages!=NULL){
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

GtkType libbalsa_mailbox_type_from_path(const gchar * filename)
{
    struct stat st;
    GtkType ret = 0;

    /*
     * FIXME: Needed? if the file doesn't exist won't
     * we get some unknown value from mx_get_magic 
     */
    if (stat(filename, &st) == -1)
	return 0;

    libbalsa_lock_mutt();
    switch (mx_get_magic(filename)) {
    case M_MBOX:
	ret = LIBBALSA_TYPE_MAILBOX_MBOX;
	break;
    case M_MMDF:
	ret = LIBBALSA_TYPE_MAILBOX_MBOX;
	break;
    case M_MH:
	ret = LIBBALSA_TYPE_MAILBOX_MH;
	break;
    case M_MAILDIR:
	ret = LIBBALSA_TYPE_MAILBOX_MAILDIR;
	break;
    case M_IMAP:
	ret = LIBBALSA_TYPE_MAILBOX_IMAP;
	break;
    case M_KENDRA: /* We don't support KENDRA */
	break;
    }
    libbalsa_unlock_mutt();

    return ret;
}

/* libbalsa_mailbox_commit_changes:
   commits the changes to the file. note that the msg numbers are changed 
   after commit so one has to re-read messages from mutt structures.
   Actually, re-reading is a wrong approach because it slows down balsa
   like hell. I know, I tried it (re-reading).  */
gint
libbalsa_mailbox_commit_changes(LibBalsaMailbox * mailbox)
{
    GList *message_list;
    GList *tmp_message_list;
    LibBalsaMessage *current_message;
    gint res = 0;

    /* only open mailboxes can be commited; lock it instead of opening */
    g_return_val_if_fail(CLIENT_CONTEXT_OPEN(mailbox), 0);
    LOCK_MAILBOX_RETURN_VAL(mailbox, 0);

    /* examine all the message in the mailbox */
    {
	GList *p=NULL;
	message_list = mailbox->message_list;
	while (message_list) {
	    current_message = LIBBALSA_MESSAGE(message_list->data);
	    tmp_message_list = message_list->next;
	    if (current_message->flags & LIBBALSA_MESSAGE_FLAG_DELETED) {
		p=g_list_append(p, current_message);
	    }
	    message_list = tmp_message_list;
	}
	if(p){
	    gtk_signal_emit(GTK_OBJECT(mailbox),
			    libbalsa_mailbox_signals[MESSAGES_DELETE],
			    p);
	    g_list_free(p);
	}
    }

    message_list = mailbox->message_list;
    while (message_list) {
	current_message = LIBBALSA_MESSAGE(message_list->data);
	tmp_message_list = message_list->next;
	if (current_message->flags & LIBBALSA_MESSAGE_FLAG_DELETED) {
	    current_message->mailbox = NULL;
	    gtk_object_unref(GTK_OBJECT(current_message));
	    mailbox->message_list =
		g_list_remove_link(mailbox->message_list, message_list);
	}
	message_list = tmp_message_list;

    }
#if 0
    libbalsa_lock_mutt();
    res = 0; /* FIXME: mx_sync_mailbox (CLIENT_CONTEXT(mailbox), NULL); */
    if (res) {
	libbalsa_mailbox_free_messages(mailbox);
	libbalsa_mailbox_load_messages(mailbox);
    }
    libbalsa_unlock_mutt();
#endif
    UNLOCK_MAILBOX(mailbox);
    return res;
}


/* internal c-client translation:
 * mutt lists can cantain null adresses for address strings like
 * "To: Dear Friends,". We do remove them.
 */
static LibBalsaMessage *
translate_message(HEADER * cur)
{
    LibBalsaMessage *message;
    ADDRESS *addy;
    LibBalsaAddress *addr;
    ENVELOPE *cenv;
    LIST *tmp;
    gchar *p;

    if (!cur)
	return NULL;

    cenv = cur->env;

    message = libbalsa_message_new();

    message->date = cur->date_sent;
    message->from = libbalsa_address_new_from_libmutt(cenv->from);
    message->sender = libbalsa_address_new_from_libmutt(cenv->sender);
    message->reply_to = libbalsa_address_new_from_libmutt(cenv->reply_to);
    message->dispnotify_to = 
	libbalsa_address_new_from_libmutt(cenv->dispnotify_to);

    for (addy = cenv->to; addy; addy = addy->next) {
	addr = libbalsa_address_new_from_libmutt(addy);
	if(addr) message->to_list = g_list_append(message->to_list, addr);
    }

    for (addy = cenv->cc; addy; addy = addy->next) {
	addr = libbalsa_address_new_from_libmutt(addy);
	if(addr) message->cc_list = g_list_append(message->cc_list, addr);
    }

    for (addy = cenv->bcc; addy; addy = addy->next) {
	addr = libbalsa_address_new_from_libmutt(addy);
	if(addr) message->bcc_list = g_list_append(message->bcc_list, addr);
    }

    /* Get fcc from message */
    for (tmp = cenv->userhdrs; tmp;) {
	if (g_strncasecmp("X-Mutt-Fcc:", tmp->data, 11) == 0) {
	    p = tmp->data + 11;
	    SKIPWS(p);

	    if (p)
		message->fcc_mailbox = g_strdup(p);
	    else
		message->fcc_mailbox = NULL;
	} else if (g_strncasecmp("X-Mutt-Fcc:", tmp->data, 18) == 0) {
	    /* Is X-Mutt-Fcc correct? */
	    p = tmp->data + 18;
	    SKIPWS(p);

	    message->in_reply_to = g_strdup(p);
	} else if (g_strncasecmp ("In-Reply-To:", tmp->data, 12) == 0){
	    p = tmp->data + 12;
	    while(*p!='\0'&& *p!='<')p++;
	    if(*p!='\0'){
		message->in_reply_to = g_strdup (p);
		p=message->in_reply_to;
		while(*p!='\0' && *p!='>')p++;
		if(*p=='>')*(p+1)='\0';
	    }
	}
	tmp = tmp->next;
    }

    message->subject = g_strdup(cenv->subject);
    message->message_id = g_strdup(cenv->message_id);

    for (tmp = cenv->references; tmp != NULL; tmp = tmp->next) {
	message->references = g_list_append(message->references,
					    g_strdup(tmp->data));
    }

    /* more! */

    if(cenv->references!=NULL){
	LIST* p=cenv->references;
	while(p!=NULL){
	    message->references_for_threading = 
		g_list_prepend(message->references_for_threading, 
			       g_strdup(p->data));
            p=p->next;
	}
    }

#if 0
    /* According to  RFC 1036 (section 2.2.5), MessageIDs in References header
     * must be in the oldest first order; the direct parent should be last. 
     * It seems, however, some MUAs ignore this rule.
     * For example, one of them adds the direct parent to the head of
     * the References header.
     */
    if(message->in_reply_to != NULL &&
       message->references_for_threading != NULL &&
       1<g_list_length(message->references_for_threading) &&
       strcmp(message->in_reply_to, 
	      (g_list_first(message->references_for_threading))->data)==0){
	GList *foo=message->references_for_threading;
        message->references_for_threading=g_list_remove(foo, foo->data);
    }
#endif

    return message;
}

static void
message_status_changed_cb(LibBalsaMessage * message, LibBalsaMailbox * mb)
{
    gtk_signal_emit(GTK_OBJECT(message->mailbox),
		    libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED],
		    message);
}
