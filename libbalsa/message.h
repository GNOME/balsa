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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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

#include "rfc3156.h"

#define LIBBALSA_TYPE_MESSAGE libbalsa_message_get_type()

G_DECLARE_FINAL_TYPE(LibBalsaMessage,
                     libbalsa_message,
                     LIBBALSA,
                     MESSAGE,
                     GObject)

typedef enum _LibBalsaMessageFlag LibBalsaMessageFlag;

enum _LibBalsaMessageFlag {
    LIBBALSA_MESSAGE_FLAG_NONE     = 0,
    LIBBALSA_MESSAGE_FLAG_NEW      = 1 << 0,
    LIBBALSA_MESSAGE_FLAG_DELETED  = 1 << 1,
    LIBBALSA_MESSAGE_FLAG_REPLIED  = 1 << 2,
    LIBBALSA_MESSAGE_FLAG_FLAGGED  = 1 << 3,
    LIBBALSA_MESSAGE_FLAG_RECENT   = 1 << 4,
    LIBBALSA_MESSAGE_FLAG_SELECTED = 1 << 5,     /* pseudo flag */
    LIBBALSA_MESSAGE_FLAG_INVALID  = 1 << 6      /* pseudo flag */
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

typedef enum _LibBalsaMessageAttach LibBalsaMessageAttach;
enum _LibBalsaMessageAttach {
    LIBBALSA_MESSAGE_ATTACH_ATTACH,
    LIBBALSA_MESSAGE_ATTACH_GOOD,
    LIBBALSA_MESSAGE_ATTACH_NOTRUST,
    LIBBALSA_MESSAGE_ATTACH_BAD,
    LIBBALSA_MESSAGE_ATTACH_SIGN,
    LIBBALSA_MESSAGE_ATTACH_ENCR,
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
    InternetAddressList *from;		/* may actually be a list */
    InternetAddressList *sender;
    InternetAddressList *reply_to;
    InternetAddressList *dispnotify_to;

    /* primary, secondary, and blind recipient lists */
    InternetAddressList *to_list;
    InternetAddressList *cc_list;
    InternetAddressList *bcc_list;

    /* Mime type */
    GMimeContentType *content_type;

    /* File Carbon Copy Mailbox URL */
    gchar *fcc_url;

    /* other headers */
    GList *user_hdrs;

#if defined ENABLE_AUTOCRYPT
    /* received Autocrypt header data
     * Note that g_mime_message_get_autocrypt_header() will return an object
     * even if there is no Autocrypt: header, so we must remember if there
     * hase been one */
    GMimeAutocryptHeader *autocrypt_hdr;
    gboolean autocrypt_hdr_present;
#endif
};

/** FREE_HEADER_LIST() frees user_hdrs */
#define FREE_HEADER_LIST(l) do{g_list_free_full((l),(GDestroyNotify)g_strfreev);}while(0)

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


/** @brief Message cryptographic protection flags
 *
 * Bit flags for reporting the cryptographic state of received messages and message bodies, and for creating new messages.  Uses by
 * - _LibBalsaMessage::crypt_mode, _BalsaSendmsg::crypt_mode
 * - libbalsa_message_set_crypt_mode(), libbalsa_message_get_crypt_mode()
 * - libbalsa_message_body_protect_mode(), libbalsa_message_body_signature_state()
 *
 * @{
 */
/* no protection */
#define LIBBALSA_PROTECT_NONE          0x0000U

/* bits to define the protection mode: signed or encrypted */
#define LIBBALSA_PROTECT_SIGN          0x0001U
#define LIBBALSA_PROTECT_ENCRYPT       0x0002U
#define LIBBALSA_PROTECT_MODE          (LIBBALSA_PROTECT_SIGN | LIBBALSA_PROTECT_ENCRYPT)

/* bits to define the protection method */
#define LIBBALSA_PROTECT_OPENPGP       0x0004U	/* RFC 2440 (OpenPGP) */
#define LIBBALSA_PROTECT_SMIME         0x0008U	/* RFC 8551 (S/MIME v4 or earlier) */
#define LIBBALSA_PROTECT_RFC3156       0x0010U	/* RFC 3156 (PGP/MIME) */
#define LIBBALSA_PROTECT_PROTOCOL      (LIBBALSA_PROTECT_OPENPGP | LIBBALSA_PROTECT_SMIME | LIBBALSA_PROTECT_RFC3156)

/* indicate broken structure */
#define LIBBALSA_PROTECT_ERROR         0x0020U

/* cryptographic signature state of a received message - note that the signature state is not really a bit mask, and must be sorted
 * in ascending order, i.e. the best state must have the lowest and the worst the highest value */
#define LIBBALSA_PROTECT_SIGN_GOOD     0x0100U
#define LIBBALSA_PROTECT_SIGN_NOTRUST  0x0200U
#define LIBBALSA_PROTECT_SIGN_BAD      0x0300U
/** @} */


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

gboolean libbalsa_message_save(LibBalsaMessage * message,
			       const gchar *filename);

void libbalsa_message_reply(LibBalsaMessage * message);

void libbalsa_message_append_part(LibBalsaMessage * message,
				  LibBalsaMessageBody * body);

gboolean libbalsa_message_body_ref(LibBalsaMessage * message,
                                   gboolean fetch_all_headers);
void libbalsa_message_body_unref(LibBalsaMessage * message);

/*
 * misc message releated functions
 */
const gchar *libbalsa_message_pathname(LibBalsaMessage * message);
const gchar *libbalsa_message_body_charset(LibBalsaMessageBody * body);
gboolean libbalsa_message_is_multipart(LibBalsaMessage * message);
gboolean libbalsa_message_is_partial(LibBalsaMessage * message,
				     gchar ** id);
gboolean libbalsa_message_has_attachment(LibBalsaMessage * message);

const gchar* libbalsa_message_header_get_one(LibBalsaMessageHeaders* headers,
                                             const gchar *find);
GList* libbalsa_message_header_get_all(LibBalsaMessageHeaders* headers,
                                       const gchar *find);
const gchar *libbalsa_message_get_user_header(LibBalsaMessage * message,
                                              const gchar * name);
void libbalsa_message_set_user_header(LibBalsaMessage * message,
                                      const gchar * name,
                                      const gchar * value);

LibBalsaMessageBody *libbalsa_message_get_part_by_id(LibBalsaMessage *
                                                     message,
                                                     const gchar * id);

void libbalsa_message_set_dispnotify(LibBalsaMessage *message, 
				     InternetAddress *ia);

void libbalsa_message_set_subject(LibBalsaMessage * message,
                                  const gchar * subject);
void libbalsa_message_set_subject_from_header(LibBalsaMessage * message,
                                              const gchar * header);
/* use LIBBALSA_MESSAGE_GET_SUBJECT() macro, we may optimize this
   function out if we find a way.
*/
const gchar* libbalsa_message_get_subject(LibBalsaMessage* message);
LibBalsaMessageAttach libbalsa_message_get_attach_icon(LibBalsaMessage *
						       message);
#define libbalsa_message_date_to_utf8(m, f) \
    libbalsa_date_to_utf8(libbalsa_message_get_headers(m)->date, (f))
#define libbalsa_message_headers_date_to_utf8(h, f) libbalsa_date_to_utf8((h)->date, (f))

GList *libbalsa_message_refs_for_threading(LibBalsaMessage* msg);

void libbalsa_message_load_envelope_from_stream(LibBalsaMessage * message,
                                                GMimeStream * stream);
void libbalsa_message_load_envelope(LibBalsaMessage *message);
void libbalsa_message_set_references_from_string(LibBalsaMessage * message,
						 const gchar *str);
void libbalsa_message_set_in_reply_to_from_string(LibBalsaMessage * message,
						  const gchar *str);
GMimeStream *libbalsa_message_stream(LibBalsaMessage * message);
gboolean libbalsa_message_copy(LibBalsaMessage * message,
                               LibBalsaMailbox * dest, GError ** err);
void libbalsa_message_change_flags(LibBalsaMessage * message,
                                   LibBalsaMessageFlag set,
                                   LibBalsaMessageFlag clear);

const gchar *libbalsa_message_get_tempdir(LibBalsaMessage * message);
/*
 * Getters
 */
LibBalsaMailbox        *libbalsa_message_get_mailbox(LibBalsaMessage *message);
LibBalsaMessageHeaders *libbalsa_message_get_headers(LibBalsaMessage *message);
LibBalsaMessageBody    *libbalsa_message_get_body_list(LibBalsaMessage *message);
GMimeMessage           *libbalsa_message_get_mime_message(LibBalsaMessage *message);
LibBalsaMessageFlag     libbalsa_message_get_flags(LibBalsaMessage *message);
const gchar            *libbalsa_message_get_message_id(LibBalsaMessage *message);
guint                   libbalsa_message_get_msgno(LibBalsaMessage *message);
gint64                  libbalsa_message_get_length(LibBalsaMessage *message);
gboolean                libbalsa_message_get_has_all_headers(LibBalsaMessage *message);
gboolean                libbalsa_message_get_request_dsn(LibBalsaMessage *message);
GList                  *libbalsa_message_get_references(LibBalsaMessage *message);
LibBalsaIdentity       *libbalsa_message_get_identity(LibBalsaMessage *message);
GList                  *libbalsa_message_get_parameters(LibBalsaMessage *message);
const gchar            *libbalsa_message_get_subtype(LibBalsaMessage *message);
guint                   libbalsa_message_get_crypt_mode(LibBalsaMessage *message);
gboolean                libbalsa_message_get_always_trust(LibBalsaMessage *message);
GList                  *libbalsa_message_get_in_reply_to(LibBalsaMessage *message);
gboolean                libbalsa_message_get_attach_pubkey(LibBalsaMessage *message);
guint                   libbalsa_message_get_body_ref(LibBalsaMessage *message);
gboolean				libbalsa_message_has_crypto_content(LibBalsaMessage *message);

/*
 * Setters
 */
void libbalsa_message_set_flags(LibBalsaMessage    *message,
                                LibBalsaMessageFlag flags);
void libbalsa_message_set_mailbox(LibBalsaMessage *message,
                                  LibBalsaMailbox *mailbox);
void libbalsa_message_set_msgno(LibBalsaMessage *message,
                                guint            msgno);
void libbalsa_message_set_has_all_headers(LibBalsaMessage *message,
                                          gboolean         has_all_headers);
void libbalsa_message_set_length(LibBalsaMessage *message,
                                 gint64           length);
void libbalsa_message_set_mime_message(LibBalsaMessage *message,
                                   GMimeMessage    *mime_message);
void libbalsa_message_set_message_id(LibBalsaMessage *message,
                                     const gchar     *message_id);
void libbalsa_message_set_request_dsn(LibBalsaMessage *message,
                                      gboolean         request_dsn);
void libbalsa_message_set_subtype(LibBalsaMessage *message,
                                  const gchar     *subtype);
void libbalsa_message_set_body_list(LibBalsaMessage     *message,
                                    LibBalsaMessageBody *body_list);
void libbalsa_message_set_references(LibBalsaMessage *message,
                                     GList           *references);
void libbalsa_message_set_in_reply_to(LibBalsaMessage *message,
                                      GList           *in_reply_to);
void libbalsa_message_set_crypt_mode(LibBalsaMessage *message,
                                     guint            mode);
void libbalsa_message_set_always_trust(LibBalsaMessage *message,
                                       gboolean         mode);
void libbalsa_message_set_attach_pubkey(LibBalsaMessage *message,
                                     gboolean         att_pubkey);
void libbalsa_message_set_identity(LibBalsaMessage  *message,
                                   LibBalsaIdentity *identity);
void libbalsa_message_add_parameters(LibBalsaMessage *message,
                                     gchar          **parameters);

#endif				/* __LIBBALSA_MESSAGE_H__ */
