/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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
#include <stdarg.h>
#include <ctype.h>
/* this should be removed.  it is only used for _() for internationalzation */
#include <gnome.h>

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <dirent.h>

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
static void libbalsa_mailbox_class_init (LibBalsaMailboxClass *klass);
static void libbalsa_mailbox_init(LibBalsaMailbox *mailbox);
static void libbalsa_mailbox_destroy (GtkObject *object);

static void libbalsa_mailbox_real_close(LibBalsaMailbox *mailbox);
static void libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox *mailbox, gboolean flag);

/* Callbacks */
static void message_status_changed_cb (LibBalsaMessage *message, LibBalsaMailbox *mb );

static LibBalsaMessage *translate_message (HEADER * cur);

/* Marshalling function */
static void libbalsa_marshal_POINTER__OBJECT (GtkObject * object,
					      GtkSignalFunc func,
					      gpointer func_data, GtkArg * args);

enum {
	OPEN_MAILBOX,
	CLOSE_MAILBOX,
	MESSAGE_STATUS_CHANGED,
	MESSAGE_NEW,
	MESSAGE_DELETE,
	GET_MESSAGE_STREAM,
	CHECK,
	SET_UNREAD_MESSAGES_FLAG,
	LAST_SIGNAL
};

static GtkObjectClass *parent_class = NULL;
static guint libbalsa_mailbox_signals[LAST_SIGNAL];

GtkType
libbalsa_mailbox_get_type (void)
{
	static GtkType mailbox_type = 0;

	if (!mailbox_type)
	{
		static const GtkTypeInfo mailbox_info =
		{
			"LibBalsaMailbox",
			sizeof (LibBalsaMailbox),
			sizeof (LibBalsaMailboxClass),
			(GtkClassInitFunc) libbalsa_mailbox_class_init,
			(GtkObjectInitFunc) libbalsa_mailbox_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		mailbox_type = gtk_type_unique (gtk_object_get_type(), &mailbox_info);
	}

	return mailbox_type;
}

static void
libbalsa_mailbox_class_init (LibBalsaMailboxClass *klass)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass*) klass;

	parent_class = gtk_type_class(gtk_object_get_type());

	libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED] =
		gtk_signal_new ("message-status-changed",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET(LibBalsaMailboxClass, message_status_changed),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, LIBBALSA_TYPE_MESSAGE);

	libbalsa_mailbox_signals[OPEN_MAILBOX] =
		gtk_signal_new ("open_mailbox",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, open_mailbox),
				gtk_marshal_NONE__BOOL,
				GTK_TYPE_NONE, 1, GTK_TYPE_BOOL);

	libbalsa_mailbox_signals[CLOSE_MAILBOX] =
		gtk_signal_new ("close_mailbox",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, close_mailbox),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	libbalsa_mailbox_signals[MESSAGE_NEW] =
		gtk_signal_new ("message-new",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, message_new),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, LIBBALSA_TYPE_MESSAGE);

	libbalsa_mailbox_signals[MESSAGE_DELETE] =
		gtk_signal_new ("message-delete",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, message_delete),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, LIBBALSA_TYPE_MESSAGE);

	libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG] =
		gtk_signal_new ("set-unread-messages-flag",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, set_unread_messages_flag),
				gtk_marshal_NONE__BOOL,
				GTK_TYPE_NONE, 1, GTK_TYPE_BOOL);

	/* Virtual functions. Use GTK_RUN_NO_HOOKS
	   which prevents the signal being connected to */
	libbalsa_mailbox_signals[GET_MESSAGE_STREAM] =
		gtk_signal_new ("get-message-stream",
				GTK_RUN_LAST|GTK_RUN_NO_HOOKS,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, get_message_stream),
				libbalsa_marshal_POINTER__OBJECT,
				GTK_TYPE_POINTER, 1, LIBBALSA_TYPE_MESSAGE);
	libbalsa_mailbox_signals[CHECK] =
		gtk_signal_new ("check",
				GTK_RUN_LAST|GTK_RUN_NO_HOOKS,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, check),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	gtk_object_class_add_signals (object_class, libbalsa_mailbox_signals, LAST_SIGNAL);

	object_class->destroy = libbalsa_mailbox_destroy;

	klass->open_mailbox = NULL;
	klass->close_mailbox = libbalsa_mailbox_real_close;
	klass->set_unread_messages_flag = libbalsa_mailbox_real_set_unread_messages_flag;

	klass->message_status_changed = NULL;
	klass->message_new = NULL;
	klass->message_delete = NULL;

	klass->get_message_stream = NULL;
	klass->check = NULL;

}

static void
libbalsa_mailbox_init(LibBalsaMailbox *mailbox)
{
	mailbox->lock = FALSE;
	mailbox->is_directory = FALSE;

	mailbox->name = NULL;
	CLIENT_CONTEXT (mailbox) = NULL;

	mailbox->open_ref = 0;
	mailbox->messages = 0;
	mailbox->new_messages = 0;
	mailbox->has_unread_messages = FALSE;
	mailbox->unread_messages = 0;
	mailbox->total_messages = 0;
	mailbox->message_list = NULL;

}

static void 
libbalsa_mailbox_destroy (GtkObject *object)
{
	LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);

	if (!mailbox)
		return;

	if (CLIENT_CONTEXT (mailbox) != NULL)
		while (mailbox->open_ref > 0)
			libbalsa_mailbox_close(mailbox);

	g_free(mailbox->name);

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		(*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

void 
libbalsa_mailbox_open(LibBalsaMailbox *mailbox, gboolean append)
{
	g_return_if_fail(mailbox != NULL);
	g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

	gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[OPEN_MAILBOX], append);
}

void 
libbalsa_mailbox_close(LibBalsaMailbox *mailbox)
{
	g_return_if_fail(mailbox != NULL);
	g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

	gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[CLOSE_MAILBOX]);
}

void
libbalsa_mailbox_set_unread_messages_flag(LibBalsaMailbox *mailbox, gboolean has_unread)
{
	if ( has_unread != mailbox->has_unread_messages )
		gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG], has_unread);
}

void
libbalsa_mailbox_check (LibBalsaMailbox *mailbox)
{
	g_return_if_fail (mailbox != NULL);
	g_return_if_fail (LIBBALSA_IS_MAILBOX(mailbox));

	gtk_signal_emit (GTK_OBJECT(mailbox), libbalsa_mailbox_signals[CHECK]);
}

FILE*
libbalsa_mailbox_get_message_stream(LibBalsaMailbox *mailbox, LibBalsaMessage *message)
{
	FILE *retval = NULL;

	g_return_val_if_fail(LIBBALSA_IS_MAILBOX(mailbox), NULL);
	g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
	g_return_val_if_fail(message->mailbox == mailbox, NULL);

	gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[GET_MESSAGE_STREAM], message, &retval);

	return retval;
}

static void 
libbalsa_mailbox_real_close(LibBalsaMailbox *mailbox)
{
	int check;
#ifdef DEBUG
	g_print ("LibBalsaMailbox: Closing %s Refcount: %d\n", mailbox->name, mailbox->open_ref);
#endif
	LOCK_MAILBOX (mailbox);

	if (mailbox->open_ref == 0) {
		UNLOCK_MAILBOX (mailbox);
		return;
	}

	mailbox->open_ref--;

	if (mailbox->open_ref == 0)
	{
		libbalsa_mailbox_free_messages (mailbox);
		mailbox->messages = 0;
		mailbox->total_messages = 0;
		mailbox->unread_messages = 0;
  
		/* now close the mail stream and expunge deleted
		 * messages -- the expunge may not have to be done */
		if (CLIENT_CONTEXT_OPEN (mailbox))
		{
			while( (check=mx_close_mailbox (CLIENT_CONTEXT (mailbox), NULL) )) {
				UNLOCK_MAILBOX (mailbox);
				g_print("libbalsa_mailbox_real_close: close failed, retrying...\n");
				libbalsa_mailbox_check(mailbox);
				LOCK_MAILBOX (mailbox);
			}
			free (CLIENT_CONTEXT (mailbox));
			CLIENT_CONTEXT (mailbox) = NULL;
		}
	}

	UNLOCK_MAILBOX (mailbox);
}

static void 
libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox *mailbox, gboolean flag)
{
	mailbox->has_unread_messages = flag;
}

void
libbalsa_mailbox_sort (LibBalsaMailbox * mailbox, LibBalsaMailboxSort sort)
{
	mutt_sort_headers (CLIENT_CONTEXT (mailbox), sort);
}

/*
 * private 
 * PS: called by mail_progress_notify_cb:
 * loads incrementally new messages, if any.
 */
void
libbalsa_mailbox_load_messages (LibBalsaMailbox * mailbox)
{
	glong msgno;
	LibBalsaMessage *message;
	HEADER *cur = 0;

	if ( CLIENT_CONTEXT_CLOSED(mailbox) )
		return;

	for (msgno = mailbox->messages; mailbox->new_messages > 0; msgno++) {
		cur = CLIENT_CONTEXT (mailbox)->hdrs[msgno];

		if (!cur)
			continue;

		if (cur->env->subject && 
		    !strcmp (cur->env->subject, 
			     "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA")) {
			mailbox->new_messages--;
			mailbox->messages++;
			continue;
		}

		message = translate_message (cur);
		message->mailbox = mailbox;
		message->msgno = msgno;

		gtk_signal_connect ( GTK_OBJECT (message), "clear-flags",
				     GTK_SIGNAL_FUNC(message_status_changed_cb),
				     mailbox);
		gtk_signal_connect ( GTK_OBJECT (message), "set-answered",
				     GTK_SIGNAL_FUNC(message_status_changed_cb),
				     mailbox);
		gtk_signal_connect ( GTK_OBJECT (message), "set-read",
				     GTK_SIGNAL_FUNC(message_status_changed_cb),
				     mailbox);
		gtk_signal_connect ( GTK_OBJECT (message), "set-deleted",
				     GTK_SIGNAL_FUNC(message_status_changed_cb),
				     mailbox);
		gtk_signal_connect ( GTK_OBJECT (message), "set-flagged",
				     GTK_SIGNAL_FUNC(message_status_changed_cb),
				     mailbox);

		mailbox->messages++;
		mailbox->total_messages++;

		if (!cur->read)	{
			message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
        
			mailbox->unread_messages++;

		}

		if (cur->deleted)
			message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;

		if (cur->flagged)
			message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;

		if (cur->replied)
			message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;

		mailbox->message_list = g_list_append (mailbox->message_list, message);
		mailbox->new_messages--;
     
		gtk_signal_emit (GTK_OBJECT(mailbox), libbalsa_mailbox_signals[MESSAGE_NEW], message);
	}

	if (mailbox->unread_messages > 0)
		libbalsa_mailbox_set_unread_messages_flag(mailbox,TRUE);
	else
		libbalsa_mailbox_set_unread_messages_flag(mailbox,FALSE);
}


void
libbalsa_mailbox_free_messages (LibBalsaMailbox * mailbox)
{
	GList *list;
	LibBalsaMessage *message;

	list = g_list_first (mailbox->message_list);

	while (list) {
		message = list->data;
		list = list->next;

		gtk_signal_emit( GTK_OBJECT(mailbox), libbalsa_mailbox_signals[MESSAGE_DELETE], message);
		gtk_object_destroy (GTK_OBJECT(message));
	}

	g_list_free (mailbox->message_list);
	mailbox->message_list = NULL;
}

LibBalsaMailboxType
libbalsa_mailbox_valid (gchar * filename)
{
	struct stat st;

	if (stat (filename, &st) == -1)
		return MAILBOX_UNKNOWN;

	switch (mx_get_magic (filename)) {
	case M_MBOX:
		return MAILBOX_MBOX;
		break;
	case M_MMDF:
		return MAILBOX_MBOX;
		break;
	case M_MH:
		return MAILBOX_MH;
		break;
	case M_MAILDIR:
		return MAILBOX_MAILDIR;
		break;
	case M_IMAP:
		return MAILBOX_IMAP;
		break;
	default:
		return MAILBOX_UNKNOWN;
		break;
	}
}

void libbalsa_mailbox_commit_changes( LibBalsaMailbox *mailbox )
{
	GList *message_list;
	GList *tmp_message_list;
	LibBalsaMessage *current_message;

	libbalsa_mailbox_open (mailbox, FALSE);

	/* examine all the message in the mailbox */
	message_list = mailbox->message_list;
	while (message_list) {
		current_message = LIBBALSA_MESSAGE(message_list->data);
		tmp_message_list =  message_list->next;
		if ( current_message->flags & LIBBALSA_MESSAGE_FLAG_DELETED ) {
			gtk_signal_emit( GTK_OBJECT(mailbox), libbalsa_mailbox_signals[MESSAGE_DELETE], current_message);
			gtk_object_destroy (GTK_OBJECT(current_message));
			mailbox->message_list = g_list_remove_link( mailbox->message_list, message_list);
		}
		message_list = tmp_message_list;
      
	}

	/* [MBG] This should prevent segfaults */
	/*   if (CLIENT_CONTEXT (mailbox) != NULL) */
	/*     mx_sync_mailbox (CLIENT_CONTEXT(mailbox)); */

	libbalsa_mailbox_close (mailbox);
}

/* internal c-client translation */
static LibBalsaMessage *
translate_message (HEADER * cur)
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

	message = libbalsa_message_new ();

	message->date = cur->date_sent;
	message->from = libbalsa_address_new_from_libmutt (cenv->from);
	message->sender = libbalsa_address_new_from_libmutt (cenv->sender);
	message->reply_to = libbalsa_address_new_from_libmutt (cenv->reply_to);

	for (addy = cenv->to; addy; addy = addy->next) {
		addr = libbalsa_address_new_from_libmutt (addy);
		message->to_list = g_list_append (message->to_list, addr);
	}

	for (addy = cenv->cc; addy; addy = addy->next) {
		addr = libbalsa_address_new_from_libmutt (addy);
		message->cc_list = g_list_append (message->cc_list, addr);
	}

	for (addy = cenv->bcc; addy; addy = addy->next)	{
		addr = libbalsa_address_new_from_libmutt (addy);
		message->bcc_list = g_list_append (message->bcc_list, addr);
	}
  
	/* Get fcc from message */
	for (tmp = cenv->userhdrs; tmp; ) {
		if (mutt_strncasecmp ("X-Mutt-Fcc:", tmp->data, 11) == 0) {
			p = tmp->data + 11;
			SKIPWS (p);
			
			if (p)
				message->fcc_mailbox = g_strdup(p);
			else 
				message->fcc_mailbox = NULL;
		} else if (mutt_strncasecmp ("X-Mutt-Fcc:", tmp->data, 18) == 0) {
			/* Is X-Mutt-Fcc correct? */
			p = tmp->data + 18;
			SKIPWS (p);

			message->in_reply_to = g_strdup (p);
		}
		tmp = tmp->next;
	}
	
	message->subject = g_strdup (cenv->subject);
	message->message_id = g_strdup (cenv->message_id);
	
	/* more! */

	return message;
}

static void message_status_changed_cb (LibBalsaMessage *message, LibBalsaMailbox *mb )
{
	gtk_signal_emit ( GTK_OBJECT(message->mailbox), libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED], message);
}

typedef gpointer (*GtkSignal_POINTER__OBJECT)(GtkObject *object, GtkObject *parm, gpointer user_data);

static void libbalsa_marshal_POINTER__OBJECT (GtkObject * object, GtkSignalFunc func, gpointer func_data, GtkArg * args)
{
	GtkSignal_POINTER__OBJECT rfunc;
	gpointer* return_val;
  
	return_val = GTK_RETLOC_POINTER(args[1]);
	rfunc = (GtkSignal_POINTER__OBJECT)func;
	*return_val = (*rfunc)(object, GTK_VALUE_OBJECT(args[0]), func_data);
}
