/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
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

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

static void libbalsa_message_class_init(LibBalsaMessageClass * klass);
static void libbalsa_message_init(LibBalsaMessage * message);

static void libbalsa_message_destroy(GtkObject * object);

/* Signal Hanlders */
static void libbalsa_message_real_clear_flags(LibBalsaMessage * message);
static void libbalsa_message_real_set_answered_flag(LibBalsaMessage *
						    message, gboolean set);
static void libbalsa_message_real_set_read_flag(LibBalsaMessage * message,
						gboolean set);
static void libbalsa_message_real_set_deleted_flag(LibBalsaMessage *
						   message, gboolean set);
static void libbalsa_message_real_set_flagged(LibBalsaMessage * message,
					      gboolean set);

static gchar *ADDRESS_to_gchar(const ADDRESS * addr);

#ifdef DEBUG
static char *mime_content_type2str(int contenttype);
#endif

enum {
    CLEAR_FLAGS,
    SET_ANSWERED,
    SET_READ,
    SET_DELETED,
    SET_FLAGGED,
    LAST_SIGNAL
};

static GtkObjectClass *parent_class = NULL;
static guint libbalsa_message_signals[LAST_SIGNAL] = { 0 };

GtkType
libbalsa_message_get_type()
{
    static GtkType libbalsa_message_type = 0;

    if (!libbalsa_message_type) {
	static const GtkTypeInfo libbalsa_message_info = {
	    "LibBalsaMessage",
	    sizeof(LibBalsaMessage),
	    sizeof(LibBalsaMessageClass),
	    (GtkClassInitFunc) libbalsa_message_class_init,
	    (GtkObjectInitFunc) libbalsa_message_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	libbalsa_message_type =
	    gtk_type_unique(gtk_object_get_type(), &libbalsa_message_info);
    }

    return libbalsa_message_type;
}

static void
libbalsa_message_init(LibBalsaMessage * message)
{
    message->flags = 0;
    message->msgno = 0;
    message->mailbox = NULL;
    message->remail = NULL;
    message->date = 0;
    message->from = NULL;
    message->sender = NULL;
    message->reply_to = NULL;
    message->subject = NULL;
    message->to_list = NULL;
    message->cc_list = NULL;
    message->bcc_list = NULL;
    message->fcc_mailbox = NULL;
    message->references = NULL;
    message->in_reply_to = NULL;
    message->message_id = NULL;
    message->body_ref = 0;
    message->body_list = NULL;
    message->references_for_threading = NULL;
}


static void
libbalsa_message_class_init(LibBalsaMessageClass * klass)
{
    GtkObjectClass *object_class;

    object_class = (GtkObjectClass *) klass;

    parent_class = gtk_type_class(gtk_object_get_type());

    libbalsa_message_signals[CLEAR_FLAGS] =
	gtk_signal_new("clear-flags",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMessageClass,
					 clear_flags),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    libbalsa_message_signals[SET_ANSWERED] =
	gtk_signal_new("set-answered",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMessageClass,
					 set_answered),
		       gtk_marshal_NONE__BOOL, GTK_TYPE_NONE, 1,
		       GTK_TYPE_BOOL);

    libbalsa_message_signals[SET_READ] =
	gtk_signal_new("set-read",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMessageClass, set_read),
		       gtk_marshal_NONE__BOOL,
		       GTK_TYPE_NONE, 1, GTK_TYPE_BOOL);

    libbalsa_message_signals[SET_DELETED] =
	gtk_signal_new("set-deleted",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMessageClass,
					 set_deleted),
		       gtk_marshal_NONE__BOOL, GTK_TYPE_NONE, 1,
		       GTK_TYPE_BOOL);

    libbalsa_message_signals[SET_FLAGGED] =
	gtk_signal_new("set-flagged",
		       GTK_RUN_FIRST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(LibBalsaMessageClass,
					 set_flagged),
		       gtk_marshal_NONE__BOOL, GTK_TYPE_NONE, 1,
		       GTK_TYPE_BOOL);

    gtk_object_class_add_signals(object_class, libbalsa_message_signals,
				 LAST_SIGNAL);

    object_class->destroy = libbalsa_message_destroy;

    klass->clear_flags = libbalsa_message_real_clear_flags;
    klass->set_answered = libbalsa_message_real_set_answered_flag;
    klass->set_read = libbalsa_message_real_set_read_flag;
    klass->set_deleted = libbalsa_message_real_set_deleted_flag;
    klass->set_flagged = libbalsa_message_real_set_flagged;
}

LibBalsaMessage *
libbalsa_message_new(void)
{
    LibBalsaMessage *message;

    message = gtk_type_new(LIBBALSA_TYPE_MESSAGE);

    return message;
}

/* libbalsa_message_destroy:
   destroy methods must leave object in 'sane' state. 
   This means NULLifing released pointers.
*/
static void
libbalsa_message_destroy(GtkObject * object)
{
    LibBalsaMessage *message;
    GList *list;

    g_return_if_fail(object != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(object));

    message = LIBBALSA_MESSAGE(object);

    g_free(message->remail);
    message->remail = NULL;

    if (message->from) {
	gtk_object_unref(GTK_OBJECT(message->from));
	message->from = NULL;
    }
    if (message->sender) {
	gtk_object_unref(GTK_OBJECT(message->sender));
	message->sender = NULL;
    }
    if (message->reply_to) {
	gtk_object_unref(GTK_OBJECT(message->reply_to));
	message->reply_to = NULL;
    }
    if(message->dispnotify_to) {
	gtk_object_unref(GTK_OBJECT(message->dispnotify_to));
	message->dispnotify_to = NULL;
    }

    g_list_foreach(message->to_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(message->to_list);
    message->to_list = NULL;

    g_list_foreach(message->cc_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(message->cc_list);
    message->cc_list = NULL;

    g_list_foreach(message->bcc_list, (GFunc) gtk_object_unref, NULL);
    g_list_free(message->bcc_list);
    message->bcc_list = NULL;

    g_free(message->subject);
    message->subject = NULL;

    for (list = message->references; list; list = list->next) {
	if (list->data)
	    g_free(list->data);
    }
    g_list_free(message->references);

    message->references = NULL;

    g_free(message->in_reply_to);
    message->in_reply_to = NULL;
    g_free(message->message_id);
    message->message_id = NULL;


    libbalsa_message_body_free(message->body_list);
    message->body_list = NULL;

    if(message->references_for_threading!=NULL) {
	GList *list=message->references_for_threading;
	for(; list; list=g_list_next(list)){
	    if(list->data)
		g_free(list->data);
	}
	g_list_free (message->references_for_threading);
	message->references_for_threading=NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

const gchar *
libbalsa_message_pathname(LibBalsaMessage * message)
{
    g_return_val_if_fail(message->mailbox, NULL);
    return CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno]->path;
}

const gchar *
libbalsa_message_charset(LibBalsaMessage * message)
{
    gchar *charset = NULL;

    LibBalsaMessageBody *body = message->body_list;
    while (body) {
	charset = libbalsa_message_body_get_parameter(body, "charset");

	if (charset)
	    break;

	body = body->next;
    }

    return charset;
}

/* message_user_hdrs:
   returns allocated GList containing (header=>value) ALL headers pairs
   as generated by g_strsplit.
   The list has to be freed by the following chunk of code:
   for(tmp=list; tmp; tmp=g_list_next(tmp)) 
      g_strfreev(tmp->data);
   g_list_free(list);
*/
static gchar **
create_hdr_pair(const gchar * name, gchar * value)
{
    gchar **item = g_new(gchar *, 3);
    item[0] = g_strdup(name);
    item[1] = value;
    item[2] = NULL;
    return item;
}

GList *
libbalsa_message_user_hdrs(LibBalsaMessage * message)
{
    HEADER *cur;
    LIST *tmp;
    GList *res = NULL;
    gchar **pair;

    g_return_val_if_fail(message->mailbox, NULL);
    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];
    g_assert(cur != NULL);
    g_assert(cur->env != NULL);

    if (cur->env->return_path)
	res =
	    g_list_append(res,
			  create_hdr_pair("Return-Path",
					  ADDRESS_to_gchar(cur->env->
							   return_path)));

    if (cur->env->sender)
	res =
	    g_list_append(res,
			  create_hdr_pair("Sender",
					  ADDRESS_to_gchar(cur->env->
							   sender)));

    if (cur->env->mail_followup_to)
	res =
	    g_list_append(res,
			  create_hdr_pair("Mail-Followup-To",
					  ADDRESS_to_gchar(cur->env->
							   mail_followup_to)));

    if (cur->env->message_id)
	res =
	    g_list_append(res,
			  create_hdr_pair("Message-ID",
					  g_strdup(cur->env->message_id)));

    for (tmp = cur->env->references; tmp; tmp = tmp->next) {
	res =
	    g_list_append(res,
			  create_hdr_pair("References",
					  g_strdup(tmp->data)));
    }

    for (tmp = cur->env->userhdrs; tmp; tmp = tmp->next) {
	pair = g_strsplit(tmp->data, ":", 1);
	g_strchug(pair[1]);
	res = g_list_append(res, pair);
    }

    return res;
}

/* FIXME: look at the flags for mutt_append_message */
gboolean
libbalsa_message_copy(LibBalsaMessage * message, LibBalsaMailbox * dest)
{
    HEADER *cur;

    g_return_val_if_fail(message->mailbox, FALSE);
    RETURN_VAL_IF_CONTEXT_CLOSED(message->mailbox, FALSE);

    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];

    libbalsa_mailbox_open(dest, FALSE);

    if (!CLIENT_CONTEXT(dest) || dest->readonly == TRUE) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _
			     ("Couldn't open destination mailbox (%s) for copying"),
			     dest->name);
	return FALSE;
    }

    libbalsa_lock_mutt();
    mutt_append_message(CLIENT_CONTEXT(dest),
			CLIENT_CONTEXT(message->mailbox), cur, 0, 0);
    libbalsa_unlock_mutt();

    dest->total_messages++;
    if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
	dest->unread_messages++;

    libbalsa_mailbox_close(dest);
    return TRUE;
}

gboolean
libbalsa_message_move(LibBalsaMessage * message, LibBalsaMailbox * dest)
{
    HEADER *cur;

    g_return_val_if_fail(message != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);
    g_return_val_if_fail(message->mailbox,FALSE);
    RETURN_VAL_IF_CONTEXT_CLOSED(message->mailbox, FALSE);

    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];

    libbalsa_mailbox_open(dest, TRUE);

    if (!CLIENT_CONTEXT(dest) || dest->readonly == TRUE) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _
			     ("Couldn't open destination mailbox (%s) for writing"),
			     dest->name);
	return FALSE;
    }
    if (message->mailbox->readonly == TRUE) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _
			     ("Source mailbox (%s) is readonly. Cannot move message"),
			     message->mailbox->name);
	return FALSE;
    }

    libbalsa_lock_mutt();

    mutt_parse_mime_message(CLIENT_CONTEXT(message->mailbox), cur);

    mutt_append_message(CLIENT_CONTEXT(dest),
			CLIENT_CONTEXT(message->mailbox), cur, 0, 0);

    libbalsa_unlock_mutt();

    dest->total_messages++;

    if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
	dest->unread_messages++;

    libbalsa_mailbox_close(dest);
    libbalsa_message_delete(message);
    return TRUE;
}

gboolean
libbalsa_messages_move (GList* messages, LibBalsaMailbox* dest)
{
    if (libbalsa_messages_copy (messages, dest)) {
        libbalsa_messages_delete (messages);
        return TRUE;
    } else {
        return FALSE;
    }
}

gboolean
libbalsa_messages_copy (GList * messages, LibBalsaMailbox * dest)
{
    HEADER *cur;
    LibBalsaMessage *message;
    GList *p;

    g_return_val_if_fail(messages != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);
    /*RETURN_VAL_IF_CONTEXT_CLOSED(message->mailbox, FALSE);*/

    libbalsa_mailbox_open(dest, TRUE);

    if (!CLIENT_CONTEXT(dest) || dest->readonly == TRUE) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _
			     ("Couldn't open destination mailbox (%s) for writing"),
			     dest->name);
	return FALSE;
    }

    message=(LibBalsaMessage *)(messages->data);

    if (message->mailbox->readonly == TRUE) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _
			     ("Source mailbox (%s) is readonly. Cannot move message"),
			     message->mailbox->name);
	return FALSE;
    }

    libbalsa_lock_mutt();

    p=messages;
    while(p){
	message=(LibBalsaMessage *)(p->data);
	cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];
	mutt_parse_mime_message(CLIENT_CONTEXT(message->mailbox), cur);
	mutt_append_message(CLIENT_CONTEXT(dest),
			    CLIENT_CONTEXT(message->mailbox), cur, 0, 0);

	dest->total_messages++;

	if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
	    dest->unread_messages++;
	p=g_list_next(p);
    }

    libbalsa_unlock_mutt();

    libbalsa_mailbox_close(dest);
    /* libbalsa_messages_delete(messages); */
    return TRUE;
}

static void
libbalsa_message_real_clear_flags(LibBalsaMessage * message)
{
    g_return_if_fail(message->mailbox);
    LOCK_MAILBOX(message->mailbox);
    message->flags = 0;
    UNLOCK_MAILBOX(message->mailbox);
}

static void
libbalsa_message_real_set_answered_flag(LibBalsaMessage * message,
					gboolean set)
{
    HEADER *cur;

    g_return_if_fail(message->mailbox);
    LOCK_MAILBOX(message->mailbox);
    RETURN_IF_CLIENT_CONTEXT_CLOSED(message->mailbox);

    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];

    libbalsa_lock_mutt();
    mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_REPLIED, TRUE);
    libbalsa_unlock_mutt();

    message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;

    UNLOCK_MAILBOX(message->mailbox);
}

static void
libbalsa_message_real_set_read_flag(LibBalsaMessage * message,
				    gboolean set)
{
    HEADER *cur;

    g_return_if_fail(message->mailbox);

    LOCK_MAILBOX(message->mailbox);
    RETURN_IF_CLIENT_CONTEXT_CLOSED(message->mailbox);
    
    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];
    if (set && (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)) {
	libbalsa_lock_mutt();
	mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_READ, TRUE);
	mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_OLD, FALSE);
	libbalsa_unlock_mutt();

	message->flags &= ~LIBBALSA_MESSAGE_FLAG_NEW;
	message->mailbox->unread_messages--;

	if (message->mailbox->unread_messages <= 0)
	    libbalsa_mailbox_set_unread_messages_flag(message->mailbox,
						      FALSE);

    } else if (!set) {
	libbalsa_lock_mutt();
	mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_READ, TRUE);
	libbalsa_unlock_mutt();

	message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
	message->mailbox->unread_messages++;
	libbalsa_mailbox_set_unread_messages_flag(message->mailbox, TRUE);
    }

    UNLOCK_MAILBOX(message->mailbox);
}

static void
libbalsa_message_real_set_flagged(LibBalsaMessage * message, gboolean set)
{
    HEADER *cur;

    g_return_if_fail(message->mailbox);
    LOCK_MAILBOX(message->mailbox);
    RETURN_IF_CLIENT_CONTEXT_CLOSED(message->mailbox);

    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];
    if (!set && (message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)) {
	libbalsa_lock_mutt();
	mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_FLAG,
		      FALSE);
	libbalsa_unlock_mutt();

	message->flags &= ~LIBBALSA_MESSAGE_FLAG_FLAGGED;
    } else if (set) {
	libbalsa_lock_mutt();
	mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_FLAG, TRUE);
	libbalsa_unlock_mutt();

	message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
    }

    UNLOCK_MAILBOX(message->mailbox);
}

static void
libbalsa_message_real_set_deleted_flag(LibBalsaMessage * message,
				       gboolean set)
{
    HEADER *cur;

    g_return_if_fail(message->mailbox);
    
    LOCK_MAILBOX(message->mailbox);
    RETURN_IF_CLIENT_CONTEXT_CLOSED(message->mailbox);

    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];
    if (set && !(message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)) {
	libbalsa_lock_mutt();
	mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_DELETE,
		      TRUE);
	libbalsa_unlock_mutt();

	message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
	if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
	    message->mailbox->unread_messages--;

	    if (message->mailbox->unread_messages <= 0)
		libbalsa_mailbox_set_unread_messages_flag(message->mailbox,
							  FALSE);
	}

	message->mailbox->total_messages--;

    } else if (!set) {
	libbalsa_lock_mutt();
	mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_DELETE,
		      FALSE);
	libbalsa_unlock_mutt();

	message->flags &= ~LIBBALSA_MESSAGE_FLAG_DELETED;
	if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
	    message->mailbox->unread_messages++;
	message->mailbox->total_messages++;

    }

    UNLOCK_MAILBOX(message->mailbox);
}

void
libbalsa_message_flag(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[SET_FLAGGED], TRUE);
}

void
libbalsa_message_unflag(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[SET_FLAGGED], FALSE);
}

void
libbalsa_message_clear_flags(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[CLEAR_FLAGS]);
}


void
libbalsa_message_reply(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[SET_ANSWERED], TRUE);
}


void
libbalsa_message_read(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[SET_READ], TRUE);
}

void
libbalsa_message_unread(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[SET_READ], FALSE);
}

void
libbalsa_message_delete(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(message->mailbox != NULL);

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[SET_DELETED], TRUE);
}

void
libbalsa_messages_delete(GList * messages)
{
    LibBalsaMessage * message;
    g_return_if_fail(messages != NULL);

    while(messages){
      message=(LibBalsaMessage *)(messages->data);
      gtk_signal_emit(GTK_OBJECT(message),
  	  	      libbalsa_message_signals[SET_DELETED], TRUE);
      messages=g_list_next(messages);
    }
}

void
libbalsa_message_undelete(LibBalsaMessage * message)
{
    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    gtk_signal_emit(GTK_OBJECT(message),
		    libbalsa_message_signals[SET_DELETED], FALSE);
}

#ifdef DEBUG
static char *
mime_content_type2str(int contenttype)
{
    switch (contenttype) {
    case TYPEOTHER:
	return "other";
    case TYPEAUDIO:
	return "audio";
    case TYPEAPPLICATION:
	return "application";
    case TYPEIMAGE:
	return "image";
    case TYPEMULTIPART:
	return "multipart";
    case TYPETEXT:
	return "text";
    case TYPEVIDEO:
	return "video";
    default:
	return "";
    }
}
#endif

/* libbalsa_message_body_ref:
   references the body of given message.
   NO OP for 'loose' messages (i.e not associated with any mailbox).
*/
void
libbalsa_message_body_ref(LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;
    HEADER *cur;
    MESSAGE *msg;

    g_return_if_fail(message);
    if (!message->mailbox) return;

    if (message->body_ref > 0) {
	message->body_ref++;
	return;
    }

    cur = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];
    if (cur == NULL)
	return;

    /*
     * load message body
     */
    libbalsa_lock_mutt();
    msg = mx_open_message(CLIENT_CONTEXT(message->mailbox), cur->msgno);
    libbalsa_unlock_mutt();

    if (!msg) {
	message->body_ref--;
	return;
    }

    fseek(msg->fp, cur->content->offset, 0);

    if (cur->content->type == TYPEMULTIPART) {
	libbalsa_lock_mutt();
	cur->content->parts = mutt_parse_multipart(msg->fp,
						   mutt_get_parameter
						   ("boundary",
						    cur->content->
						    parameter),
						   cur->content->offset +
						   cur->content->length,
						   strcasecmp("digest",
							      cur->
							      content->
							      subtype) ==
						   0);
	libbalsa_unlock_mutt();
    } else
	if (mutt_is_message_type
	    (cur->content->type, cur->content->subtype)) {
	libbalsa_lock_mutt();
	cur->content->parts =
	    mutt_parse_messageRFC822(msg->fp, cur->content);
	libbalsa_unlock_mutt();
    }
    if (msg != NULL) {
#ifdef DEBUG
	fprintf(stderr, "After loading message\n");
	fprintf(stderr, "header->mime    = %d\n", cur->mime);
	fprintf(stderr, "header->offset  = %ld\n", cur->offset);
	fprintf(stderr, "header->content = %p\n", cur->content);
	fprintf(stderr, " \n\nDumping Message\n");
	fprintf(stderr, "Dumping a %s[%d] message\n",
		mime_content_type2str(cur->content->type),
		cur->content->type);
#endif
	body = libbalsa_message_body_new(message);
	libbalsa_message_body_set_mutt_body(body, cur->content);
	libbalsa_message_append_part(message, body);

	message->body_ref++;

	libbalsa_lock_mutt();
	mx_close_message(&msg);
	libbalsa_unlock_mutt();
    }

    /*
     * emit read message
     */
    if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)
	libbalsa_message_read(message);
}


void
libbalsa_message_body_unref(LibBalsaMessage * message)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    if (message->body_ref == 0)
	return;

    if (--message->body_ref == 0) {
	libbalsa_message_body_free(message->body_list);
	message->body_list = NULL;
    }
}

gboolean
libbalsa_message_has_attachment(LibBalsaMessage * message)
{
    gboolean ret;
    HEADER *msg_header;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    msg_header = CLIENT_CONTEXT(message->mailbox)->hdrs[message->msgno];

    /* FIXME: Can be simplified into 1 if */
    if (msg_header->content->type != TYPETEXT) {
	ret = TRUE;
    } else {
	if (g_strcasecmp("plain", msg_header->content->subtype) == 0)
	    ret = FALSE;
	else
	    ret = TRUE;
    }

    return ret;
}

gchar *
libbalsa_message_date_to_gchar(LibBalsaMessage * message,
			       const gchar * date_string)
{
    struct tm *footime;
    gchar rettime[128];

    g_return_val_if_fail(message != NULL, NULL);
    g_return_val_if_fail(date_string != NULL, NULL);

    footime = localtime(&message->date);

    strftime(rettime, sizeof(rettime), date_string, footime);

    return g_strdup(rettime);
}

static gchar *
ADDRESS_to_gchar(const ADDRESS * addr)
{
    gchar buf[1024];		/* assume no single address is longer than this */

    buf[0] = '\0';
    libbalsa_lock_mutt();
    rfc822_write_address(buf, sizeof(buf), (ADDRESS *) addr);
    libbalsa_unlock_mutt();
    return g_strdup(buf);
}

void
libbalsa_message_append_part(LibBalsaMessage * message,
			     LibBalsaMessageBody * body)
{
    LibBalsaMessageBody *part;

    g_return_if_fail(message != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    if (message->body_list == NULL) {
	message->body_list = body;
    } else {
	part = message->body_list;
	while (part->next != NULL)
	    part = part->next;
	part->next = body;
    }
}

/* libbalsa_message_get_text_content:
   returns the message content as single string. 
   When modifying make sure it works properly for both messages in mailboxes
   as well as messages created on the fly.

   FIXME: This is a simple-minded version of this function, just to get 
   the stable release out. The full blown code should mention attachments.
   Or we could create separate function for it, so printing would be 
   even nicer.
   content2reply() is also kind of specialized function that should be
   implemented in a more general fashion.  
*/
gchar *
libbalsa_message_get_text_content(LibBalsaMessage * msg, gint line_len)
{
    gchar *res;
    GString *str;
    g_return_val_if_fail(msg, NULL);

    if (msg->mailbox) {
	libbalsa_message_body_ref(msg);
	str = content2reply(msg, NULL, line_len);
	libbalsa_message_body_unref(msg);
    } else {
	LibBalsaMessageBody *body = msg->body_list;
	str = g_string_new("");
	while (body) {
	    if (body->buffer)
		str = g_string_append(str, body->buffer);
	    body = body->next;
	}
	libbalsa_wrap_string(str->str, line_len);
    }
    res = str->str;
    g_string_free(str, FALSE);
    return res;
}

/* libbalsa_message_set_dispnotify:
   sets a disposition notify to a given address
   address can be NULL.
*/
void
libbalsa_message_set_dispnotify(LibBalsaMessage *message, 
				LibBalsaAddress *address)
{
    g_return_if_fail(message);
    if(message->dispnotify_to) 
	gtk_object_unref(GTK_OBJECT(message->dispnotify_to));
    message->dispnotify_to = address;
    if(address)
	gtk_object_ref(GTK_OBJECT(message->dispnotify_to));
}
