/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
static void libbalsa_mailbox_class_init (LibBalsaMailboxClass *klass);
static void libbalsa_mailbox_init(LibBalsaMailbox *mailbox);
static void libbalsa_mailbox_destroy (GtkObject *object);

static void libbalsa_mailbox_real_close(LibBalsaMailbox *mailbox);
static void libbalsa_mailbox_real_set_unread_messages_flag(LibBalsaMailbox *mailbox, gboolean flag);

static void libbalsa_mailbox_real_save_config(LibBalsaMailbox *mailbox, const gchar *prefix);
static void libbalsa_mailbox_real_load_config(LibBalsaMailbox *mailbox, const gchar *prefix);

/* Callbacks */
static void message_status_changed_cb (LibBalsaMessage *message, LibBalsaMailbox *mb );

static LibBalsaMessage *translate_message (HEADER * cur);

enum {
	OPEN_MAILBOX,
	CLOSE_MAILBOX,
	MESSAGE_STATUS_CHANGED,
	MESSAGE_NEW,
	MESSAGE_DELETE,
	GET_MESSAGE_STREAM,
	CHECK,
	SET_UNREAD_MESSAGES_FLAG,
	SAVE_CONFIG,
	LOAD_CONFIG,
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
		gtk_signal_new ("open-mailbox",
				GTK_RUN_FIRST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, open_mailbox),
				gtk_marshal_NONE__BOOL,
				GTK_TYPE_NONE, 1, GTK_TYPE_BOOL);

	libbalsa_mailbox_signals[CLOSE_MAILBOX] =
		gtk_signal_new ("close-mailbox",
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
	   which prevents the signal being connected to them */
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

	libbalsa_mailbox_signals[SAVE_CONFIG] =
		gtk_signal_new ("save-config",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, save_config),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

	libbalsa_mailbox_signals[LOAD_CONFIG] =
		gtk_signal_new ("load-config",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (LibBalsaMailboxClass, load_config),
				gtk_marshal_NONE__POINTER,
				GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

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
	klass->save_config = libbalsa_mailbox_real_save_config;
	klass->load_config = libbalsa_mailbox_real_load_config;
}

static void
libbalsa_mailbox_init(LibBalsaMailbox *mailbox)
{
	mailbox->lock = FALSE;
	mailbox->is_directory = FALSE;

	mailbox->config_prefix = NULL;
	mailbox->name = NULL;
	CLIENT_CONTEXT (mailbox) = NULL;

	mailbox->open_ref = 0;
	mailbox->messages = 0;
	mailbox->new_messages = 0;
	mailbox->has_unread_messages = FALSE;
	mailbox->unread_messages = 0;
	mailbox->total_messages = 0;
	mailbox->message_list = NULL;

	mailbox->readonly = FALSE;
}

/* libbalsa_mailbox_destroy:
   destroys mailbox. Must leave it in sane state.
*/
static void 
libbalsa_mailbox_destroy (GtkObject *object)
{
	LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(object);

	if (!mailbox)
		return;

	if (CLIENT_CONTEXT (mailbox) != NULL)
		while (mailbox->open_ref > 0)
			libbalsa_mailbox_close(mailbox);

	g_free(mailbox->name);              mailbox->name = NULL;	
	g_free(mailbox->config_prefix);     mailbox->config_prefix = NULL;	

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		(*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

/* Create a new mailbox by loading it from a config entry... */
LibBalsaMailbox* 
libbalsa_mailbox_new_from_config(const gchar *prefix)
{
	gchar *type_str;
	GtkType type;
	gboolean got_default;
	LibBalsaMailbox *mailbox;

	gnome_config_push_prefix(prefix);
	type_str = gnome_config_get_string_with_default("Type", &got_default);

	if ( got_default == TRUE ) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("Cannot load mailbox %s"), prefix);
		return NULL;
	}
	
	type = gtk_type_from_name(type_str);
	if ( type == 0 ) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("No such mailbox type: %s"), type_str);
		g_free(type_str);
		return NULL;
	}
	
	mailbox = gtk_type_new(type);
	if ( mailbox == NULL ) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("Could not create a mailbox of type %s"), type_str);
		g_free(type_str);
		return NULL;
	}

	libbalsa_mailbox_load_config(mailbox, prefix);

	gnome_config_pop_prefix();
	g_free(type_str);

	return mailbox;
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
	g_return_if_fail(mailbox != NULL);
	g_return_if_fail(LIBBALSA_IS_MAILBOX(mailbox));

	gtk_signal_emit(GTK_OBJECT(mailbox), libbalsa_mailbox_signals[SET_UNREAD_MESSAGES_FLAG], has_unread);
}

void
libbalsa_mailbox_check (LibBalsaMailbox *mailbox)
{
	g_return_if_fail (mailbox != NULL);
	g_return_if_fail (LIBBALSA_IS_MAILBOX(mailbox));

	gtk_signal_emit (GTK_OBJECT(mailbox), libbalsa_mailbox_signals[CHECK]);
}

void
libbalsa_mailbox_save_config (LibBalsaMailbox *mailbox, const gchar *prefix)
{
	g_return_if_fail (mailbox != NULL);
	g_return_if_fail (LIBBALSA_IS_MAILBOX(mailbox));

	/* These are incase this section was used for another
	 * type of mailbox that has now been deleted...
	 */
	gnome_config_private_clean_section(prefix);
	gnome_config_clean_section(prefix);

	gnome_config_push_prefix(prefix);
	gtk_signal_emit (GTK_OBJECT(mailbox), libbalsa_mailbox_signals[SAVE_CONFIG], prefix);
	gnome_config_pop_prefix();
}

void
libbalsa_mailbox_load_config (LibBalsaMailbox *mailbox, const gchar *prefix)
{
	g_return_if_fail (mailbox != NULL);
	g_return_if_fail (LIBBALSA_IS_MAILBOX(mailbox));

	gnome_config_push_prefix(prefix);
	gtk_signal_emit (GTK_OBJECT(mailbox), libbalsa_mailbox_signals[LOAD_CONFIG], prefix);
	gnome_config_pop_prefix();
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
			/* We are careful to take/release locks in the correct order here */
			libbalsa_lock_mutt();
			while( (check=mx_close_mailbox (CLIENT_CONTEXT (mailbox), NULL) )) {
				libbalsa_unlock_mutt();
				UNLOCK_MAILBOX (mailbox);
				g_print("libbalsa_mailbox_real_close: close failed, retrying...\n");
				libbalsa_mailbox_check(mailbox);
				LOCK_MAILBOX (mailbox);
				libbalsa_lock_mutt();
			}
			libbalsa_unlock_mutt();
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
	libbalsa_lock_mutt();
	mutt_sort_headers (CLIENT_CONTEXT (mailbox), sort);
	libbalsa_unlock_mutt();
}

static void
libbalsa_mailbox_real_save_config(LibBalsaMailbox *mailbox, const gchar *prefix)
{
	g_return_if_fail ( LIBBALSA_IS_MAILBOX(mailbox) );

	gnome_config_set_string("Type", gtk_type_name(GTK_OBJECT_TYPE(mailbox)));
	gnome_config_set_string("Name", mailbox->name);

	g_free(mailbox->config_prefix);
	mailbox->config_prefix = g_strdup(prefix);
				
}

static void
libbalsa_mailbox_real_load_config(LibBalsaMailbox *mailbox, const gchar *prefix)
{
	g_return_if_fail ( LIBBALSA_IS_MAILBOX(mailbox) );
	
	g_free(mailbox->config_prefix);
	mailbox->config_prefix = g_strdup(prefix);

	g_free(mailbox->name);
	mailbox->name = gnome_config_get_string("Name=Mailbox");
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
	LibBalsaMailboxType ret;

	if (stat (filename, &st) == -1)
		return MAILBOX_UNKNOWN;

	libbalsa_lock_mutt();
	switch (mx_get_magic (filename)) {
	case M_MBOX:
		ret = MAILBOX_MBOX;
		break;
	case M_MMDF:
		ret = MAILBOX_MBOX;
		break;
	case M_MH:
		ret = MAILBOX_MH;
		break;
	case M_MAILDIR:
		ret = MAILBOX_MAILDIR;
		break;
	case M_IMAP:
		ret = MAILBOX_IMAP;
		break;
	default:
		ret = MAILBOX_UNKNOWN;
		break;
	}
	libbalsa_unlock_mutt();

	return ret;
}
/* libbalsa_mailbox_commit_changes:
   commits the changes to the file. note that the msg numbers are changed 
   after commit so one has to re-read messages from mutt structures.
   Actually, re-reading is a wrong approach because it slows down balsa
   like hell. I know, I tried it (re-reading).
*/
gint libbalsa_mailbox_commit_changes( LibBalsaMailbox *mailbox )
{
	GList *message_list;
	GList *tmp_message_list;
	LibBalsaMessage *current_message;
	gint res = 0;

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
#if 0 
	libbalsa_lock_mutt();
	res = 0; /* FIXME: mx_sync_mailbox (CLIENT_CONTEXT(mailbox), NULL); */
	if(res) {
		libbalsa_mailbox_free_messages (mailbox);
		mailbox->messages = 0;
		mailbox->total_messages = 0;
		mailbox->unread_messages = 0;
		libbalsa_mailbox_load_messages (mailbox);
	}
	libbalsa_unlock_mutt();
#endif
	libbalsa_mailbox_close (mailbox);
	return res;
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
		if (g_strncasecmp ("X-Mutt-Fcc:", tmp->data, 11) == 0) {
			p = tmp->data + 11;
			SKIPWS (p);
			
			if (p)
				message->fcc_mailbox = g_strdup(p);
			else 
				message->fcc_mailbox = NULL;
		} else if (g_strncasecmp ("X-Mutt-Fcc:", tmp->data, 18) == 0) {
			/* Is X-Mutt-Fcc correct? */
			p = tmp->data + 18;
			SKIPWS (p);

			message->in_reply_to = g_strdup (p);
		}
		tmp = tmp->next;
	}
	
	message->subject = g_strdup (cenv->subject);
	message->message_id = g_strdup (cenv->message_id);

        for (tmp = cenv->references; tmp != NULL; tmp = tmp->next) {
                message->references = g_list_append (message->references,
                                                     g_strdup(tmp->data));
        }

	/* more! */

	return message;
}

static void message_status_changed_cb (LibBalsaMessage *message, LibBalsaMailbox *mb )
{
	gtk_signal_emit ( GTK_OBJECT(message->mailbox), libbalsa_mailbox_signals[MESSAGE_STATUS_CHANGED], message);
}

