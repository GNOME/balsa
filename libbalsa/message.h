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

#ifndef __LIBBALSA_MESSAGE_H__
#define __LIBBALSA_MESSAGE_H__

#include <glib.h>

#include <stdio.h>
#include <time.h>

#ifdef HAVE_GPGME
#include "rfc3156.h"
#endif

#if ENABLE_ESMTP
#include <auth-client.h>
#endif

/* #define MESSAGE_COPY_CONTENT 1 */
#define LIBBALSA_TYPE_MESSAGE \
    (libbalsa_message_get_type())
#define LIBBALSA_MESSAGE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST(obj, LIBBALSA_TYPE_MESSAGE, LibBalsaMessage))
#define LIBBALSA_MESSAGE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST(klass, LIBBALSA_TYPE_MESSAGE, \
                             LibBalsaMessageClass))
#define LIBBALSA_IS_MESSAGE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE(obj, LIBBALSA_TYPE_MESSAGE))
#define LIBBALSA_IS_MESSAGE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE(klass, LIBBALSA_TYPE_MESSAGE))

typedef struct _LibBalsaMessageClass LibBalsaMessageClass;
typedef enum _LibBalsaMessageFlag LibBalsaMessageFlag;
typedef enum _LibBalsaMsgCreateResult LibBalsaMsgCreateResult;

enum _LibBalsaMessageFlag {
    LIBBALSA_MESSAGE_FLAG_NEW = 1 << 1,
    LIBBALSA_MESSAGE_FLAG_DELETED = 1 << 2,
    LIBBALSA_MESSAGE_FLAG_REPLIED = 1 << 3,
    LIBBALSA_MESSAGE_FLAG_FLAGGED = 1 << 4
};

enum _LibBalsaMsgCreateResult {
    LIBBALSA_MESSAGE_CREATE_OK,
#ifdef HAVE_GPGME
    LIBBALSA_MESSAGE_SIGN_ERROR,
    LIBBALSA_MESSAGE_ENCRYPT_ERROR,
#endif
    LIBBALSA_MESSAGE_CREATE_ERROR,
    LIBBALSA_MESSAGE_SEND_ERROR
};

#ifdef HAVE_GPGME
#define  LIBBALSA_MESSAGE_SIGNATURE_UNKNOWN     0
#define  LIBBALSA_MESSAGE_SIGNATURE_GOOD        1
#define  LIBBALSA_MESSAGE_SIGNATURE_NOTRUST     2
#define  LIBBALSA_MESSAGE_SIGNATURE_BAD         3
#endif

/*
 * LibBalsaMessageHeaders contains all headers which are used to display both
 * a "main" message's headers as well as those from an embedded message/rfc822
 * part.
 */
struct _LibBalsaMessageHeaders {
    /* message composition date */
    time_t date;

    /* subject (for embedded messages only) */
    gchar *subject;

    /* from, reply, and disposition notification addresses */
    LibBalsaAddress *from;
    LibBalsaAddress *reply_to;
    LibBalsaAddress *dispnotify_to;

    /* primary, secondary, and blind recipent lists */
    GList *to_list;
    GList *cc_list;
    GList *bcc_list;

    /* File Carbon Copy Mailbox URL */
    gchar *fcc_url;

    /* other headers */
    GList *user_hdrs;
};

/** FREE_HEADER_LIST() frees user_hdrs */
#define FREE_HEADER_LIST(l) do{ g_list_foreach((l),(GFunc)g_strfreev,NULL);\
                                g_list_free(l); } while(0)

struct _LibBalsaMessage {
    GObject object;

    /* the mailbox this message belongs to */
    LibBalsaMailbox *mailbox;

    /* flags */
    LibBalsaMessageFlag flags;

    /* headers */
    MuttHeader* header;
    LibBalsaMessageHeaders *headers;
    int updated; /** whether complete headers have been fetched */

    /* remail header if any */
    gchar *remail;

    /* sender address */
    LibBalsaAddress *sender;

    /* subject line; we still need it here for sending;
     * although _SET_SUBJECT might resolve it(?) 
     * but we can set to to NULL unless there is no mailbox, like
     * on sending. */
    gchar *subj;
#ifdef MESSAGE_COPY_CONTENT
#define LIBBALSA_MESSAGE_GET_SUBJECT(m) \
    ((m)->subj ? (m)->subj : _("(No subject)"))
#else
#define LIBBALSA_MESSAGE_GET_SUBJECT(m) libbalsa_message_get_subject(m)
#endif
#define LIBBALSA_MESSAGE_SET_SUBJECT(m,s) \
        { g_free((m)->subj); (m)->subj = (s); }

    /* replied message ID's */
    GList *references;
    GList *references_for_threading; /* oldest first */

    /* replied message ID; from address on date */
    gchar *in_reply_to;

    /* message ID */
    gchar *message_id;

#ifdef HAVE_GPGME
    /* GPG sign and/or encrypt message (sending) */
    guint gpg_mode;

    /* signature status (received message) */
    gint sig_state;
#endif

    /* a forced multipart subtype or NULL for mixed */
    gchar *subtype;

    /* additional message content type parameters */
    GList *parameters;

    /* message body */
    guint body_ref;
    LibBalsaMessageBody *body_list;
    /*  GList *body_list; */

    gint msgno;     /* message no; always copy for faster sorting. */
#if MESSAGE_COPY_CONTENT
#define LIBBALSA_MESSAGE_GET_NO(m)      ((m)->msgno)
    glong length;   /* byte len */
    gint lines_len; /* line len */
#define LIBBALSA_MESSAGE_GET_LENGTH(m)  ((m)->length)
#define LIBBALSA_MESSAGE_GET_LINES(m) ((m)->lines_len)
#else
#define LIBBALSA_MESSAGE_GET_LENGTH(m) libbalsa_message_get_length(m)
#define LIBBALSA_MESSAGE_GET_LINES(m)  libbalsa_message_get_lines(m)
#define LIBBALSA_MESSAGE_GET_NO(m)  libbalsa_message_get_no(m)
#endif
};

#define LIBBALSA_MESSAGE_HAS_FLAG(message, mask) \
    ((LIBBALSA_MESSAGE(message)->flags & mask) != 0)
#define LIBBALSA_MESSAGE_IS_UNREAD(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_NEW)
#define LIBBALSA_MESSAGE_IS_DELETED(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_DELETED)
#define LIBBALSA_MESSAGE_IS_REPLIED(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_REPLIED)
#define LIBBALSA_MESSAGE_IS_FLAGGED(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_FLAGGED)

#define LIBBALSA_MESSAGE_SET_FLAGS(message, mask) \
    LIBBALSA_MESSAGE(message)->flags |= mask
#define LIBBALSA_MESSAGE_UNSET_FLAGS(message, mask) \
    LIBBALSA_MESSAGE(message)->flags &= ~mask

struct _LibBalsaMessageClass {
    GObjectClass parent_class;

    /* deal with flags being set/unset */
    /* Signal: */
    void (*status_changed) (LibBalsaMessage * message,
			    LibBalsaMessageFlag flag, gboolean);
};


GType libbalsa_message_get_type(void);

/*
 * message headers
 */
void libbalsa_message_headers_destroy(LibBalsaMessageHeaders * headers);
void libbalsa_message_headers_from_mutt(LibBalsaMessageHeaders *headers,
					MuttHeader *mheader);
GList *libbalsa_message_user_hdrs_from_mutt(MuttHeader* header);


/*
 * messages
 */
LibBalsaMessage *libbalsa_message_new(void);

gboolean libbalsa_message_copy(LibBalsaMessage * message,
			       LibBalsaMailbox * dest);
gboolean libbalsa_message_save(LibBalsaMessage * message,
			       const gchar *filename);
gboolean libbalsa_messages_move(GList * messages,
				LibBalsaMailbox * dest);
gboolean libbalsa_messages_copy(GList * messages,
                                LibBalsaMailbox * dest);

void libbalsa_messages_delete(GList * messages, gboolean del);

void libbalsa_messages_read(GList * messages, gboolean read);
void libbalsa_messages_flag(GList * messages, gboolean flag);

void libbalsa_message_reply(LibBalsaMessage * message);

void libbalsa_message_append_part(LibBalsaMessage * message,
				  LibBalsaMessageBody * body);

gboolean libbalsa_message_body_ref(LibBalsaMessage * message, gboolean read);
void libbalsa_message_body_unref(LibBalsaMessage * message);

LibBalsaMsgCreateResult libbalsa_message_queue(LibBalsaMessage* message, 
					       LibBalsaMailbox* outbox, LibBalsaMailbox* fccbox,
					       gint encoding, gboolean flow);
#if ENABLE_ESMTP
LibBalsaMsgCreateResult libbalsa_message_send(LibBalsaMessage* message,
					      LibBalsaMailbox* outbox,  
					      LibBalsaMailbox* fccbox,
					      gint encoding, gchar* smtp_server,
					      auth_context_t smtp_authctx,
					      gint tls_mode, gboolean flow,
                                              gboolean debug);
#else
LibBalsaMsgCreateResult libbalsa_message_send(LibBalsaMessage* message,
					      LibBalsaMailbox* outbox,  
					      LibBalsaMailbox* fccbox,
					      gint encoding, gboolean flow,
                                              gboolean debug);
#endif
gboolean libbalsa_message_postpone(LibBalsaMessage * message,
				   LibBalsaMailbox * draftbox,
				   LibBalsaMessage * reply_message,
				   gchar * fcc, gint encoding, 
				   gboolean flow);

/*
 * misc message releated functions
 */
gchar *libbalsa_message_headers_date_to_gchar(LibBalsaMessageHeaders * headers,
					      const gchar * date_string);
#define libbalsa_message_date_to_gchar(m,s) \
        libbalsa_message_headers_date_to_gchar((m)->headers,s)
gchar *libbalsa_message_size_to_gchar(LibBalsaMessage * message,
                                      gboolean lines);
gchar *libbalsa_message_title(LibBalsaMessage * message,
                              const gchar * format);
gchar **libbalsa_create_hdr_pair(const gchar * name, gchar * value);

const gchar *libbalsa_message_pathname(LibBalsaMessage * message);
gchar *libbalsa_message_charset(LibBalsaMessage * message);
const gchar *libbalsa_message_body_charset(LibBalsaMessageBody * body);
gboolean libbalsa_message_is_multipart(LibBalsaMessage * message);
gboolean libbalsa_message_has_attachment(LibBalsaMessage * message);
#ifdef HAVE_GPGME
gboolean libbalsa_message_is_pgp_signed(LibBalsaMessage * message);
gboolean libbalsa_message_is_pgp_encrypted(LibBalsaMessage * message);
#endif

GList *libbalsa_message_find_user_hdr(LibBalsaMessage * message, 
                                      const gchar * find);
FILE* libbalsa_message_get_part_by_id(LibBalsaMessage* msg, const gchar* id);

void libbalsa_message_set_dispnotify(LibBalsaMessage *message, 
				     LibBalsaAddress *address);

/* use LIBBALSA_MESSAGE_GET_SUBJECT() macro, we may optimize this
   function out if we find a way.
*/
#ifndef MESSAGE_COPY_CONTENT
const gchar* libbalsa_message_get_subject(LibBalsaMessage* message);
guint libbalsa_message_get_lines(LibBalsaMessage* msg);
glong libbalsa_message_get_length(LibBalsaMessage* msg);
#endif
glong libbalsa_message_get_no(LibBalsaMessage* msg);
void libbalsa_message_headers_update(LibBalsaMessage * message);
#endif				/* __LIBBALSA_MESSAGE_H__ */
