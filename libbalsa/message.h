/* Balsa E-Mail Client
 * Copyright (C) 1999 Stuart Parmenter
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

#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include <glib.h>
#include <gtk/gtk.h>

#include "mailbox.h"
#include "address.h"

#include "libmutt/mutt.h"

typedef enum
{
  MESSAGE_FLAG_NEW = 1 << 1,
  MESSAGE_FLAG_DELETED = 1 << 2,
  MESSAGE_FLAG_REPLIED = 1 << 3,
  MESSAGE_FLAG_FLAGGED = 1 << 4
} MessageFlags;

#define LIBBALSA_TYPE_MESSAGE                      (libbalsa_message_get_type())
#define LIBBALSA_MESSAGE(obj)                      (GTK_CHECK_CAST(obj, LIBBALSA_TYPE_MESSAGE, Message))
#define LIBBALSA_MESSAGE_CLASS(klass)              (GTK_CHECK_CLASS_CAST(klass, LIBBALSA_TYPE_MESSAGE, MessageClass))
#define LIBBALSA_IS_MESSAGE(obj)                   (GTK_CHECK_TYPE(obj, LIBBALSA_TYPE_MESSAGE))
#define LIBBALSA_IS_MESSAGE_CLASS(klass)           (GTK_CHECK_CLASS_TYPE(klass, LIBBALSA_TYPE_MESSAGE))

struct _Message
{
  GtkObject object;



  /* the mailbox this message belongs to */
  Mailbox *mailbox;

  /* flags */
  MessageFlags flags;

  /* the ordered numberic index of this message in 
   * the mailbox beginning from 1, not 0 */
  glong msgno;

  /* remail header if any */
  gchar *remail;

  /* message composition date string */
  gchar *date;
  time_t datet;

  /* from, sender, and reply addresses */
  Address *from;
  Address *sender;
  Address *reply_to;

  /* subject line */
  gchar *subject;

  /* primary, secondary, and blind recipent lists */
  GList *to_list;
  GList *cc_list;
  GList *bcc_list;

  /* File Carbon Copy Mailbox */
  Mailbox *fcc_mailbox;

  /* replied message ID */
  gchar *in_reply_to;

  /* message ID */
  gchar *message_id;

  /* message body */
  guint body_ref;
  GList *body_list;
};

typedef struct _MessageClass MessageClass;

struct _MessageClass
{
  GtkObjectClass parent_class;

  /* deal with flags being set/unset */
  void (* clear_flags)    (Message *message);
  void (* set_answered)   (Message *message, gboolean set);
  void (* set_read)       (Message *message, gboolean set);
  void (* set_deleted)    (Message *message, gboolean set);
};


GtkType libbalsa_message_get_type(void);

/*
 * messages
 */
Message *message_new (void);
void message_free (Message * message);

void message_copy (Message * message, Mailbox * dest);
void message_move (Message * message, Mailbox * mailbox);
void message_clear_flags (Message * message);

void message_read (Message * message);
void message_unread (Message * message);
void message_delete (Message * message);
void message_undelete (Message * message);

void message_answer (Message * message);
void message_reply (Message * message);

void message_body_ref (Message * message);
void message_body_unref (Message * message);


/*
 * misc message releated functions
 */
gchar *message_pathname (Message * message);

#endif /* __MESSAGE_H__ */
