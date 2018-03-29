/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __LIBBALSA_MESSAGE_H__
#define __LIBBALSA_MESSAGE_H__

#ifndef BALSA_VERSION
#   error "Include config.h before this file."
#endif

#include <glib.h>

#include <stdio.h>
#include <time.h>

#include <gmime/gmime.h>

#ifdef HAVE_GPGME
#   include "rfc3156.h"
#endif

#define MESSAGE_COPY_CONTENT 1

#define LIBBALSA_TYPE_MESSAGE libbalsa_message_get_type()

G_DECLARE_FINAL_TYPE(LibBalsaMessage,
                     libbalsa_message,
                     LIBBALSA,
                     MESSAGE,
                     GObject)

typedef enum _LibBalsaMessageFlag LibBalsaMessageFlag;

enum _LibBalsaMessageFlag {
    LIBBALSA_MESSAGE_FLAG_NEW     = 1 << 0,
    LIBBALSA_MESSAGE_FLAG_DELETED = 1 << 1,
    LIBBALSA_MESSAGE_FLAG_REPLIED = 1 << 2,
    LIBBALSA_MESSAGE_FLAG_FLAGGED = 1 << 3,
    LIBBALSA_MESSAGE_FLAG_RECENT  = 1 << 4,
    LIBBALSA_MESSAGE_FLAG_SELECTED= 1 << 5,     /* pseudo flag */
    LIBBALSA_MESSAGE_FLAG_INVALID = 1 << 6      /* pseudo flag */
};

#define LIBBALSA_MESSAGE_FLAGS_REAL \
    (LIBBALSA_MESSAGE_FLAG_NEW | \
     LIBBALSA_MESSAGE_FLAG_DELETED | \
     LIBBALSA_MESSAGE_FLAG_REPLIED | \
     LIBBALSA_MESSAGE_FLAG_FLAGGED | \
     LIBBALSA_MESSAGE_FLAG_RECENT)

typedef enum _LibBalsaMessageStatus LibBalsaMessageStatus;
enum _LibBalsaMessageStatus {
    LIBBALSA_MESSAGE_STATUS_UNREAD,
    LIBBALSA_MESSAGE_STATUS_DELETED,
    LIBBALSA_MESSAGE_STATUS_FLAGGED,
    LIBBALSA_MESSAGE_STATUS_REPLIED,
    LIBBALSA_MESSAGE_STATUS_ICONS_NUM
};


#ifdef HAVE_GPGME
typedef enum _LibBalsaMsgProtectState LibBalsaMsgProtectState;

enum _LibBalsaMsgProtectState {
    LIBBALSA_MSG_PROTECT_NONE,
    LIBBALSA_MSG_PROTECT_SIGN_UNKNOWN,
    LIBBALSA_MSG_PROTECT_SIGN_GOOD,
    LIBBALSA_MSG_PROTECT_SIGN_NOTRUST,
    LIBBALSA_MSG_PROTECT_SIGN_BAD,
    LIBBALSA_MSG_PROTECT_CRYPT
};
#endif

typedef enum _LibBalsaMessageAttach LibBalsaMessageAttach;
enum _LibBalsaMessageAttach {
    LIBBALSA_MESSAGE_ATTACH_ATTACH,
#ifdef HAVE_GPGME
    LIBBALSA_MESSAGE_ATTACH_GOOD,
    LIBBALSA_MESSAGE_ATTACH_NOTRUST,
    LIBBALSA_MESSAGE_ATTACH_BAD,
    LIBBALSA_MESSAGE_ATTACH_SIGN,
    LIBBALSA_MESSAGE_ATTACH_ENCR,
#endif
    LIBBALSA_MESSAGE_ATTACH_ICONS_NUM
};


/*
 * Message info used by mailbox;
 *
 *   headers->from
 *   headers->date
 *   headers->to_list
 *   headers->content_type
 *   subj
 *   length
 *   lines_len (scrap?)
 *
 * loaded by driver:
 *   flags
 *
 * needed for threading local mailboxes:
 *   message_id
 *   references
 *   in_reply_to
 */
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
    InternetAddressList *from;
    InternetAddressList *reply_to;
    InternetAddressList *dispnotify_to;

    /* primary, secondary, and blind recipent lists */
    InternetAddressList *to_list;
    InternetAddressList *cc_list;
    InternetAddressList *bcc_list;

    /* Mime type */
    GMimeContentType *content_type;

    /* File Carbon Copy Mailbox URL */
    gchar *fcc_url;

    /* other headers */
    GList *user_hdrs;
};

#define LIBBALSA_MESSAGE_HAS_FLAG(message, mask) \
    ((libbalsa_message_get_flags(LIBBALSA_MESSAGE(message)) & mask) != 0)
#define LIBBALSA_MESSAGE_IS_UNREAD(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_NEW)
#define LIBBALSA_MESSAGE_IS_DELETED(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_DELETED)
#define LIBBALSA_MESSAGE_IS_REPLIED(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_REPLIED)
#define LIBBALSA_MESSAGE_IS_FLAGGED(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_FLAGGED)
#define LIBBALSA_MESSAGE_IS_RECENT(message) \
    LIBBALSA_MESSAGE_HAS_FLAG(message, LIBBALSA_MESSAGE_FLAG_RECENT)

#define LIBBALSA_MESSAGE_GET_SUBJECT(m) libbalsa_message_get_subject(m)
#define LIBBALSA_MESSAGE_GET_NO(m)      libbalsa_message_get_msgno(m)
#define LIBBALSA_MESSAGE_GET_LENGTH(m)  libbalsa_message_get_length(m)

/*
 * message headers
 */
void   libbalsa_message_headers_destroy(LibBalsaMessageHeaders *headers);
void   libbalsa_message_headers_from_gmime(LibBalsaMessageHeaders *headers,
                                           GMimeMessage           *msg);
void   libbalsa_message_init_from_gmime(LibBalsaMessage *message,
                                        GMimeMessage    *msg);
GList *libbalsa_message_user_hdrs_from_gmime(GMimeMessage *msg);


/*
 * messages
 */
LibBalsaMessage *libbalsa_message_new(void);

gboolean         libbalsa_message_save(LibBalsaMessage *message,
                                       const gchar     *filename);

void libbalsa_message_reply(LibBalsaMessage *message);

void libbalsa_message_append_part(LibBalsaMessage     *message,
                                  LibBalsaMessageBody *body);

gboolean libbalsa_message_body_ref(LibBalsaMessage *message,
                                   gboolean         read,
                                   gboolean         fetch_all_headers);
void libbalsa_message_body_unref(LibBalsaMessage *message);

/*
 * misc message releated functions
 */
const gchar *libbalsa_message_pathname(LibBalsaMessage *message);
const gchar *libbalsa_message_body_charset(LibBalsaMessageBody *body);
gboolean     libbalsa_message_is_multipart(LibBalsaMessage *message);
gboolean     libbalsa_message_is_partial(LibBalsaMessage *message,
                                         gchar          **id);
gboolean     libbalsa_message_has_attachment(LibBalsaMessage *message);

#ifdef HAVE_GPGME
gboolean libbalsa_message_is_pgp_signed(LibBalsaMessage *message);
gboolean libbalsa_message_is_pgp_encrypted(LibBalsaMessage *message);

#endif

const gchar *libbalsa_message_header_get_one(LibBalsaMessageHeaders *headers,
                                             const gchar            *find);
GList       *libbalsa_message_header_get_all(LibBalsaMessageHeaders *headers,
                                             const gchar            *find);
const gchar *libbalsa_message_get_user_header(LibBalsaMessage *message,
                                              const gchar     *name);
void         libbalsa_message_set_user_header(LibBalsaMessage *message,
                                              const gchar     *name,
                                              const gchar     *value);

LibBalsaMessageBody *libbalsa_message_get_part_by_id(LibBalsaMessage *
                                                     message,
                                                     const gchar *id);

void libbalsa_message_set_dispnotify(LibBalsaMessage *message,
                                     InternetAddress *ia);

void libbalsa_message_set_subject(LibBalsaMessage *message,
                                  const gchar     *subject);
void libbalsa_message_set_subject_from_header(LibBalsaMessage *message,
                                              const gchar     *header);

/* use LIBBALSA_MESSAGE_GET_SUBJECT() macro, we may optimize this
   function out if we find a way.
 */
const gchar *libbalsa_message_get_subject(LibBalsaMessage *message);

#ifndef MESSAGE_COPY_CONTENT
guint libbalsa_message_get_lines(LibBalsaMessage *msg);
glong libbalsa_message_get_length(LibBalsaMessage *msg);

#endif
LibBalsaMessageAttach libbalsa_message_get_attach_icon(LibBalsaMessage *
                                                       message);

#define libbalsa_message_date_to_utf8(m, f) libbalsa_date_to_utf8(libbalsa_message_get_headers( \
                                                                      m)->date, (f))
#define libbalsa_message_headers_date_to_utf8(h, f) libbalsa_date_to_utf8((h)->date, (f))

GList       *libbalsa_message_refs_for_threading(LibBalsaMessage *msg);

void         libbalsa_message_load_envelope_from_stream(LibBalsaMessage *message,
                                                        GMimeStream     *stream);
void         libbalsa_message_load_envelope(LibBalsaMessage *message);
gboolean     libbalsa_message_set_headers_from_string(LibBalsaMessage *message,
                                                      const gchar     *str);
void         libbalsa_message_set_references_from_string(LibBalsaMessage *message,
                                                         const gchar     *str);
void         libbalsa_message_set_in_reply_to_from_string(LibBalsaMessage *message,
                                                          const gchar     *str);
GMimeStream *libbalsa_message_stream(LibBalsaMessage *message);
gboolean     libbalsa_message_copy(LibBalsaMessage *message,
                                   LibBalsaMailbox *dest,
                                   GError         **err);
void libbalsa_message_change_flags(LibBalsaMessage    *message,
                                   LibBalsaMessageFlag set,
                                   LibBalsaMessageFlag clear);

const gchar *libbalsa_message_get_tempdir(LibBalsaMessage *message);

/*
 * Getters
 */
LibBalsaMailbox        *libbalsa_message_get_mailbox(LibBalsaMessage *message);
LibBalsaMessageHeaders *libbalsa_message_get_headers(LibBalsaMessage *message);
LibBalsaMessageBody    *libbalsa_message_get_body_list(LibBalsaMessage *message);
GMimeMessage           *libbalsa_message_get_mime_msg(LibBalsaMessage *message);
LibBalsaMessageFlag     libbalsa_message_get_flags(LibBalsaMessage *message);
const gchar            *libbalsa_message_get_message_id(LibBalsaMessage *message);
glong                   libbalsa_message_get_msgno(LibBalsaMessage *message);
glong                   libbalsa_message_get_length(LibBalsaMessage *message);
gboolean                libbalsa_message_get_has_all_headers(LibBalsaMessage *message);
InternetAddressList    *libbalsa_message_get_sender(LibBalsaMessage *message);
gboolean                libbalsa_message_get_request_dsn(LibBalsaMessage *message);
GList                  *libbalsa_message_get_references(LibBalsaMessage *message);

#ifdef HAVE_GPGME
LibBalsaIdentity *libbalsa_message_get_identity(LibBalsaMessage *message);

#endif
GList                  *libbalsa_message_get_parameters(LibBalsaMessage *message);
const gchar            *libbalsa_message_get_subtype(LibBalsaMessage *message);
guint                   libbalsa_message_get_gpg_mode(LibBalsaMessage *message);
GList                  *libbalsa_message_get_in_reply_to(LibBalsaMessage *message);
gboolean                libbalsa_message_get_att_pubkey(LibBalsaMessage *message);
LibBalsaMsgProtectState libbalsa_message_get_prot_state(LibBalsaMessage *message);
guint                   libbalsa_message_get_body_ref(LibBalsaMessage *message);

/*
 * Setters
 */
void libbalsa_message_set_flags(LibBalsaMessage    *message,
                                LibBalsaMessageFlag flags);
void libbalsa_message_set_mailbox(LibBalsaMessage *message,
                                  LibBalsaMailbox *mailbox);
void libbalsa_message_set_msgno(LibBalsaMessage *message,
                                glong            msgno);
void libbalsa_message_set_has_all_headers(LibBalsaMessage *message,
                                          gboolean         has_all_headers);

#if MESSAGE_COPY_CONTENT
void libbalsa_message_set_length(LibBalsaMessage *message,
                                 glong            length);

#endif

void libbalsa_message_set_mime_msg(LibBalsaMessage *message,
                                   GMimeMessage    *mime_message);
void libbalsa_message_set_sender(LibBalsaMessage     *message,
                                 InternetAddressList *sender);
void libbalsa_message_set_message_id(LibBalsaMessage *message,
                                     const gchar     *message_id);
void libbalsa_message_set_prot_state(LibBalsaMessage        *message,
                                     LibBalsaMsgProtectState prot_state);
void libbalsa_message_set_request_dsn(LibBalsaMessage *message,
                                      gboolean         request_dsn);
void libbalsa_message_set_subtype(LibBalsaMessage *message,
                                  const gchar     *subtype);
void libbalsa_message_set_parameters(LibBalsaMessage *message,
                                     GList           *parameters);
void libbalsa_message_set_body_list(LibBalsaMessage     *message,
                                    LibBalsaMessageBody *body_list);
void libbalsa_message_set_references(LibBalsaMessage *message,
                                     GList           *references);
void libbalsa_message_set_in_reply_to(LibBalsaMessage *message,
                                      GList           *in_reply_to);
void libbalsa_message_set_gpg_mode(LibBalsaMessage *message,
                                   guint            mode);
void libbalsa_message_set_att_pubkey(LibBalsaMessage *message,
                                     gboolean         att_pubkey);

#ifdef HAVE_GPGME
void libbalsa_message_set_identity(LibBalsaMessage  *message,
                                   LibBalsaIdentity *identity);

#endif

#endif                          /* __LIBBALSA_MESSAGE_H__ */
