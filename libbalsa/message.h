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

#ifndef __LIBBALSA_MESSAGE_H__
#define __LIBBALSA_MESSAGE_H__

#include <glib.h>
#include <gtk/gtk.h>

#include <time.h>

#include "libbalsa.h"

#define LIBBALSA_TYPE_MESSAGE                      (libbalsa_message_get_type())
#define LIBBALSA_MESSAGE(obj)                      (GTK_CHECK_CAST(obj, LIBBALSA_TYPE_MESSAGE, LibBalsaMessage))
#define LIBBALSA_MESSAGE_CLASS(klass)              (GTK_CHECK_CLASS_CAST(klass, LIBBALSA_TYPE_MESSAGE, LibBalsaMessageClass))
#define LIBBALSA_IS_MESSAGE(obj)                   (GTK_CHECK_TYPE(obj, LIBBALSA_TYPE_MESSAGE))
#define LIBBALSA_IS_MESSAGE_CLASS(klass)           (GTK_CHECK_CLASS_TYPE(klass, LIBBALSA_TYPE_MESSAGE))

typedef struct _LibBalsaMessageClass LibBalsaMessageClass;
typedef enum _LibBalsaMessageFlag LibBalsaMessageFlag;

enum _LibBalsaMessageFlag {
    LIBBALSA_MESSAGE_FLAG_NEW = 1 << 1,
    LIBBALSA_MESSAGE_FLAG_DELETED = 1 << 2,
    LIBBALSA_MESSAGE_FLAG_REPLIED = 1 << 3,
    LIBBALSA_MESSAGE_FLAG_FLAGGED = 1 << 4
};

struct _LibBalsaMessage {
    GtkObject object;

    /* the mailbox this message belongs to */
    LibBalsaMailbox *mailbox;

    /* flags */
    LibBalsaMessageFlag flags;

    /* the ordered numberic index of this message in 
     * the mailbox beginning from 1, not 0 */
    glong msgno;

    /* remail header if any */
    gchar *remail;

    /* message composition date */
    time_t date;

    /* from, sender, and reply addresses */
    LibBalsaAddress *from;
    LibBalsaAddress *sender;
    LibBalsaAddress *reply_to;

    /* subject line */
    gchar *subject;

    /* primary, secondary, and blind recipent lists */
    GList *to_list;
    GList *cc_list;
    GList *bcc_list;

    /* File Carbon Copy Mailbox */
    gchar *fcc_mailbox;

    /* replied message ID's */
    GList *references;

    /* replied message ID; from address on date */
    gchar *in_reply_to;

    /* message ID */
    gchar *message_id;

    /* message body */
    guint body_ref;
    LibBalsaMessageBody *body_list;
    /*  GList *body_list; */
};

struct _LibBalsaMessageClass {
    GtkObjectClass parent_class;

    /* deal with flags being set/unset */
    /* Signal: */
    void (*status_changed) (LibBalsaMessage * message,
			    LibBalsaMessageFlag flag, gboolean);

    /* Virtual Functions: */
    void (*clear_flags) (LibBalsaMessage * message);
    void (*set_answered) (LibBalsaMessage * message, gboolean set);
    void (*set_read) (LibBalsaMessage * message, gboolean set);
    void (*set_deleted) (LibBalsaMessage * message, gboolean set);
    void (*set_flagged) (LibBalsaMessage * message, gboolean set);
};


GtkType libbalsa_message_get_type(void);

/*
 * messages
 */
LibBalsaMessage *libbalsa_message_new(void);

gboolean libbalsa_message_copy(LibBalsaMessage * message,
			       LibBalsaMailbox * dest);
gboolean libbalsa_message_move(LibBalsaMessage * message,
			       LibBalsaMailbox * mailbox);
void libbalsa_message_clear_flags(LibBalsaMessage * message);

void libbalsa_message_read(LibBalsaMessage * message);
void libbalsa_message_unread(LibBalsaMessage * message);
void libbalsa_message_delete(LibBalsaMessage * message);
void libbalsa_message_undelete(LibBalsaMessage * message);

void libbalsa_message_flag(LibBalsaMessage * message);
void libbalsa_message_unflag(LibBalsaMessage * message);

void libbalsa_message_answer(LibBalsaMessage * message);
void libbalsa_message_reply(LibBalsaMessage * message);

void libbalsa_message_append_part(LibBalsaMessage * message,
				  LibBalsaMessageBody * body);

void libbalsa_message_body_ref(LibBalsaMessage * message);
void libbalsa_message_body_unref(LibBalsaMessage * message);

gboolean balsa_send_message(LibBalsaMessage * message,
			    LibBalsaMailbox * outbox, gint encoding);
gboolean balsa_postpone_message(LibBalsaMessage * message,
				LibBalsaMailbox * draftbox,
				LibBalsaMessage * reply_message,
				gchar * fcc);
gboolean libbalsa_message_send(LibBalsaMessage * message,
			       LibBalsaMailbox * outbox, gint encoding);
gboolean libbalsa_message_postpone(LibBalsaMessage * message,
				   LibBalsaMailbox * draftbox,
				   LibBalsaMessage * reply_message,
				   gchar * fcc, gint encoding);

/*
 * misc message releated functions
 */
gchar *libbalsa_message_date_to_gchar(LibBalsaMessage * message,
				      const gchar * date_string);
const gchar *libbalsa_message_pathname(LibBalsaMessage * message);
const gchar *libbalsa_message_charset(LibBalsaMessage * message);
gboolean libbalsa_message_has_attachment(LibBalsaMessage * message);

GList *libbalsa_message_user_hdrs(LibBalsaMessage * message);
gchar *libbalsa_message_get_text_content(LibBalsaMessage * msg,
					 gint line_len);

#endif				/* __LIBBALSA_MESSAGE_H__ */
