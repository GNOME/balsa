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

#include <stdio.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "mailbackend.h"

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "misc.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

static void libbalsa_message_class_init (MessageClass *klass);
static void libbalsa_message_init (Message *message);

static void libbalsa_message_real_destroy(GtkObject *object);
static void libbalsa_message_real_clear_flags(Message *message);
static void libbalsa_message_real_set_answered_flag(Message *message, gboolean set);
static void libbalsa_message_real_set_read_flag(Message *message, gboolean set);
static void libbalsa_message_real_set_deleted_flag(Message *message, gboolean set);
char * mime_content_type2str (int contenttype);

enum {
  CLEAR_FLAGS,
  SET_ANSWERED,
  SET_READ,
  SET_DELETED,
  LAST_SIGNAL
};

static GtkObjectClass *parent_class = NULL;
static guint message_signals[LAST_SIGNAL] = { 0 };

GtkType libbalsa_message_get_type()
{
  static GtkType message_type = 0;
  if (!message_type)
    {
      static const GtkTypeInfo message_info =
      {
        "Message",
        sizeof (Message),
        sizeof (MessageClass),
        (GtkClassInitFunc) libbalsa_message_class_init,
        (GtkObjectInitFunc) libbalsa_message_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      message_type = gtk_type_unique(gtk_object_get_type(), &message_info);
    }

  return message_type;
}

static void
libbalsa_message_init (Message *message)
{
  message->flags = 0;
  message->msgno = 0;
  message->mailbox = NULL;
  message->remail = NULL;
  message->date = NULL;
  message->datet = 0;
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
}


static void
libbalsa_message_class_init (MessageClass *klass)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  message_signals[CLEAR_FLAGS] =
    gtk_signal_new ("clear_flags",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET(MessageClass, clear_flags),
                    gtk_marshal_NONE__NONE,
                    GTK_TYPE_NONE, 0);

  message_signals[SET_ANSWERED] =
    gtk_signal_new ("set_answered",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET(MessageClass, set_answered),
                    gtk_marshal_NONE__INT,
                    GTK_TYPE_NONE, 1,
		    GTK_TYPE_BOOL);

  message_signals[SET_READ] =
    gtk_signal_new ("set_read",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET(MessageClass, set_read),
                    gtk_marshal_NONE__INT,
                    GTK_TYPE_NONE, 1,
		    GTK_TYPE_BOOL);

  message_signals[SET_DELETED] =
    gtk_signal_new ("set_deleted",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET(MessageClass, set_deleted),
                    gtk_marshal_NONE__INT,
                    GTK_TYPE_NONE, 1,
		    GTK_TYPE_BOOL);

  gtk_object_class_add_signals(object_class, message_signals, LAST_SIGNAL);

  object_class->destroy = libbalsa_message_real_destroy;

  klass->clear_flags = libbalsa_message_real_clear_flags;
  klass->set_answered = libbalsa_message_real_set_answered_flag;
  klass->set_read = libbalsa_message_real_set_read_flag;
  klass->set_deleted = libbalsa_message_real_set_deleted_flag;
}

/*
 * messages
 */
Message *
message_new(void)
{
  Message *message;

  message = gtk_type_new(LIBBALSA_TYPE_MESSAGE);

  return message;
}

static void
libbalsa_message_real_destroy(GtkObject *object)
{
	Message *message;

	g_return_if_fail(object != NULL);
	g_return_if_fail(LIBBALSA_IS_MESSAGE(object));
	
	message = LIBBALSA_MESSAGE(object);

	g_free (message->remail);
	g_free (message->date);
	address_free (message->from);
	address_free (message->sender);
	address_free (message->reply_to);
	address_list_free(message->to_list);
	address_list_free(message->cc_list);
	address_list_free(message->bcc_list);
	g_free (message->subject);
        g_free (message->references);
	g_free (message->in_reply_to);
	g_free (message->message_id); 
}

void
message_destroy(Message * message)
{
  g_return_if_fail(message != NULL);
  g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

  gtk_object_destroy((GtkObject*)message);
}

const gchar *
message_pathname (Message * message)
{
  return CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno]->path;
}

const gchar *
message_charset (Message *message)
{
   gchar * charset = NULL;

   GList * body_list = message->body_list;
   while( body_list ) {
      Body * bd = (Body*)body_list->data;
      g_assert(bd != NULL && bd->mutt_body);
      if( (charset = mutt_get_parameter("charset", bd->mutt_body->parameter) ))
	 break;
      body_list = g_list_next (body_list);
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
static gchar**
create_hdr_pair(const gchar * name, gchar* value) {
    gchar ** item = g_malloc(sizeof(gchar*)*3);
    item[0] = g_strdup(name);
    item[1] = value;
    item[2] = NULL;
    return item;
}

GList *
message_user_hdrs(Message *message) 
{
    HEADER *cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];
    LIST * tmp;
    GList * res = NULL;
    gchar ** pair;
	
    g_assert(cur != NULL);
    g_assert(cur->env != NULL);
    res = g_list_append(res, create_hdr_pair(
	"Return-Path", ADDRESS_to_gchar(cur->env->return_path) ) );
    
    res = g_list_append(res, create_hdr_pair(
	"Sender", ADDRESS_to_gchar(cur->env->sender) ) );
    res = g_list_append(res, create_hdr_pair(
	"Mail-Followup-To", ADDRESS_to_gchar(cur->env->mail_followup_to) ) );
    res = g_list_append(res, create_hdr_pair(
	"Message-ID", g_strdup(cur->env->message_id) ) );

    for(tmp = cur->env->references; tmp; tmp = tmp->next) {
	pair = g_strsplit(tmp->data,":",2);
	res = g_list_append(res, pair);
    }

    for(tmp = cur->env->userhdrs; tmp; tmp = tmp->next) {
	pair = g_strsplit(tmp->data,":",2);
	res = g_list_append(res, pair);
    }
    
   return res;
}

/* TODO and/or FIXME look at the flags for mutt_append_message */
void
message_copy (Message * message, Mailbox * dest)
{
  HEADER *cur;

  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  mailbox_open_ref (dest);

  mutt_append_message (CLIENT_CONTEXT (dest),
		       CLIENT_CONTEXT (message->mailbox),
		       cur, 0, 0);

  dest->total_messages++;
  if (message->flags & MESSAGE_FLAG_NEW ) dest->unread_messages++;

  /*PKGW test: commented out why? */
  //send_watcher_append_message (dest, message);
  mailbox_open_unref (dest);
}

void
message_move (Message * message, Mailbox * dest)
{
  HEADER *cur;

  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);
  
  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  mailbox_open_append (dest);

  mutt_parse_mime_message (CLIENT_CONTEXT (message->mailbox), cur);

  mutt_append_message (CLIENT_CONTEXT (dest),
		       CLIENT_CONTEXT (message->mailbox),
		       cur, 0, 0);
  
  dest->total_messages++;
  if (message->flags & MESSAGE_FLAG_NEW ) dest->unread_messages++;
  
  /*PKGW test: commented out why? */
  //send_watcher_append_message (dest, message);
  
  mailbox_open_unref (dest);
  
  message_delete (message);
}

static void
libbalsa_message_real_clear_flags(Message *message)
{
#if 0
  char tmp[BUFFER_SIZE];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_REPLIED, 1);
  sprintf (tmp, "%ld", message->msgno);
  mail_clearflag (CLIENT_STREAM (message->mailbox), tmp, "\\DELETED");
  mail_clearflag (CLIENT_STREAM (message->mailbox), tmp, "\\ANSWERED");
#endif

  LOCK_MAILBOX (message->mailbox);

  message->flags = 0;
  send_watcher_mark_clear_message (message->mailbox, message);

  UNLOCK_MAILBOX (message->mailbox);
}

static void
libbalsa_message_real_set_answered_flag(Message *message, gboolean set)
{
  HEADER *cur;

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_REPLIED, TRUE);
  send_watcher_mark_answer_message (message->mailbox, message);
  message->flags |= MESSAGE_FLAG_REPLIED;

  UNLOCK_MAILBOX (message->mailbox);
}

static void
libbalsa_message_real_set_read_flag(Message *message, gboolean set)
{

  HEADER *cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  LOCK_MAILBOX (message->mailbox);
  RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);

  if (set && (message->flags & MESSAGE_FLAG_NEW)) {
    mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_READ, TRUE);
    mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_OLD, FALSE);

    message->flags &= ~MESSAGE_FLAG_NEW;
    message->mailbox->unread_messages-- ;

    if (message->mailbox->unread_messages <= 0) 
      message->mailbox->has_unread_messages = FALSE;

    send_watcher_mark_read_message (message->mailbox, message);
  } else if (!set){
    mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_READ, TRUE);

    message->flags |= MESSAGE_FLAG_NEW;
    message->mailbox->unread_messages++;
    message->mailbox->has_unread_messages = TRUE;
    send_watcher_mark_unread_message (message->mailbox, message);
  }

  UNLOCK_MAILBOX (message->mailbox);
}

static void
libbalsa_message_real_set_deleted_flag(Message *message, gboolean set)
{
    HEADER *cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];
    
    LOCK_MAILBOX (message->mailbox);
    RETURN_IF_CLIENT_CONTEXT_CLOSED (message->mailbox);
    
    if (set && !(message->flags & MESSAGE_FLAG_DELETED)) {
	mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_DELETE, TRUE);
	
	message->flags |= MESSAGE_FLAG_DELETED;
	if (message->flags & MESSAGE_FLAG_NEW) {
          message->mailbox->unread_messages--;

          if (message->mailbox->unread_messages <= 0) 
            message->mailbox->has_unread_messages = FALSE;
        }
        
	message->mailbox->total_messages--;

	send_watcher_mark_delete_message( message->mailbox, message );
    } else if (!set){
	mutt_set_flag (CLIENT_CONTEXT (message->mailbox), cur, M_DELETE, FALSE);
	
	message->flags &= ~MESSAGE_FLAG_DELETED;
	if (message->flags & MESSAGE_FLAG_NEW)
	    message->mailbox->unread_messages++;
	message->mailbox->total_messages++;

	send_watcher_mark_undelete_message( message->mailbox, message );
    }
    
    UNLOCK_MAILBOX (message->mailbox);
}

void
message_clear_flags(Message *message)
{
  g_return_if_fail(message != NULL);
  g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

  gtk_signal_emit(GTK_OBJECT(message), message_signals[CLEAR_FLAGS]);
}


void
message_reply (Message * message)
{
  g_return_if_fail(message != NULL);
  g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

  gtk_signal_emit(GTK_OBJECT(message), message_signals[SET_ANSWERED], TRUE);
}


void
message_read(Message *message)
{
  g_return_if_fail(message != NULL);
  g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

  gtk_signal_emit(GTK_OBJECT(message), message_signals[SET_READ], TRUE);
}

void
message_unread(Message *message)
{
  g_return_if_fail(message != NULL);
  g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

  gtk_signal_emit(GTK_OBJECT(message), message_signals[SET_READ], FALSE);
}

void
message_delete(Message *message)
{
  g_return_if_fail(message != NULL);
  g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

  gtk_signal_emit(GTK_OBJECT(message), message_signals[SET_DELETED], TRUE);
}


void
message_undelete(Message *message)
{
  g_return_if_fail(message != NULL);
  g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

  gtk_signal_emit(GTK_OBJECT(message), message_signals[SET_DELETED], FALSE);
}

char *
mime_content_type2str (int contenttype)
{
  switch (contenttype)
    {
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

void
message_body_ref (Message * message)
{
  Body *body;
  HEADER *cur;
  MESSAGE *msg;

  if (!message)
    return;

  cur = CLIENT_CONTEXT (message->mailbox)->hdrs[message->msgno];

  if (message->body_ref > 0)
    {
      message->body_ref++;
      return;
    }

  if (cur == NULL)
    return;

  /*
   * load message body
   */
  msg = mx_open_message (CLIENT_CONTEXT (message->mailbox), cur->msgno);
  if (!msg)
    {
      message->body_ref--;
      return;
    }
  fseek (msg->fp, cur->content->offset, 0);

  if (cur->content->type == TYPEMULTIPART)
    {
      cur->content->parts = mutt_parse_multipart (msg->fp,
		   mutt_get_parameter ("boundary", cur->content->parameter),
				cur->content->offset + cur->content->length,
			 strcasecmp ("digest", cur->content->subtype) == 0);
    }
  if (msg != NULL)
    {
#if 0
      BODY *bdy = cur->content;
#endif
#ifdef DEBUG
      fprintf (stderr, "After loading message\n");
      fprintf (stderr, "header->mime    = %d\n", cur->mime);
      fprintf (stderr, "header->offset  = %ld\n", cur->offset);
      fprintf (stderr, "header->content = %p\n", cur->content);
      fprintf (stderr, " \n\nDumping Message\n");
      fprintf (stderr, "Dumping a %s[%d] message\n",
	       mime_content_type2str (cur->content->type),
	       cur->content->type);
#endif
      body = body_new ();
      body->mutt_body = cur->content;
      message->body_list = g_list_append (message->body_list, body);
#if 0
      if (cur->content->type == TYPEMULTIPART)
	{
	  bdy = cur->content->parts;
	  while (bdy)
	    {
#ifdef DEBUG
	      fprintf (stderr, "h->c->type      = %s[%d]\n", mime_content_type2str (bdy->type), bdy->type);
	      fprintf (stderr, "h->c->subtype   = %s\n", bdy->subtype);
	      fprintf (stderr, "======\n");
#endif
	      body = body_new ();
	      body->mutt_body = bdy;
	      message->body_list = g_list_append (message->body_list, body);
	      bdy = bdy->next;

	    }
	}
#endif
      message->body_ref++;
      mx_close_message (&msg);
    }
  /*
   * emit read message
   */
  if (message->flags & MESSAGE_FLAG_NEW)
    {
      message_read (message);
    }

  if (message->mailbox->type == MAILBOX_IMAP)
    {
      if (MAILBOX_IMAP (message->mailbox)->tmp_file_path)
	g_free (MAILBOX_IMAP (message->mailbox)->tmp_file_path);
      MAILBOX_IMAP (message->mailbox)->tmp_file_path =
	g_strdup (cur->content->filename);
    }
}


void
message_body_unref (Message * message)
{
  GList *list;
  Body *body;

  if (!message)
    return;

  if (message->body_ref == 0)
    return;

  if (--message->body_ref == 0)
    {
      list = message->body_list;
      while (list)
	{
	  body = (Body *) list->data;
	  list = list->next;
	  body_free (body);
	}

      g_list_free (message->body_list);
      message->body_list = NULL;
    }
}
