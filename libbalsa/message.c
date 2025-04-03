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

/* DESIGN NOTES.
   MESSAGE_COPY_CONTENT define is an attempt to reduce memory usage of balsa.
   When it is defined, The message date is stored in one place only (in
   libmutt structures). This should reduce memory usage to some extent.
   However, it is not implemented very extensively at the present moment
   and the memory usage reduction is hardly noticeable.
   - Lack of inline functions in C increases program complexity. This cost
   can be accepted.
   - thorough analysis of memory usage is needed.
   2022-03-21: All code conditional on MESSAGE_COPY_CONTENT is removed.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
#   include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <glib.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "identity.h"

/* needed for truncate_string */
#include "misc.h"

#include "mime-stream-shared.h"
#include <glib/gi18n.h>

#include <gmime/gmime.h>

static void libbalsa_message_dispose(GObject * object);
static void libbalsa_message_finalize(GObject * object);
static gboolean libbalsa_message_is_signed(LibBalsaMessage * message);
static gboolean libbalsa_message_is_encrypted(LibBalsaMessage * message);

struct _LibBalsaMessage {
    GObject object;

    /* the mailbox this message belongs to */
    LibBalsaMailbox *mailbox;

    /* headers */
    LibBalsaMessageHeaders *headers;

    GMimeMessage *mime_msg;

    /* subject line; we still need it here for sending;
     * although _SET_SUBJECT might resolve it(?)
     * but we can set to to NULL unless there is no mailbox, like
     * on sending. */
    gchar *subj;

    /* replied message ID's */
    GList *references;

    /* replied message ID; from address on date */
    GList *in_reply_to;

    /* message ID */
    gchar *message_id;

    /* sender identity, required for choosing a forced GnuPG or S/MIME key */
    LibBalsaIdentity *ident;

    /* a forced multipart subtype or NULL for mixed; used only for
     * sending */
    gchar *subtype;

    /* additional message content type parameters; used only for sending */
    GList *parameters;

    gchar *tempdir;     /* to hold named parts */

    /* message body */
    LibBalsaMessageBody *body_list;

    /* end of pointers, begin 64-bit ints */
    gint64 length;   /* byte len */

    /* end of 64-bit ints, begin ints */
    /* GnuPG or S/MIME sign and/or encrypt message (sending), or status of received message */
    guint crypt_mode;

    guint body_ref;

    /* flags */
    LibBalsaMessageFlag flags;

    guint msgno;     /* message no; always copy for faster sorting;
                      * counting starts at 1. */

    /* GPG sign and/or encrypt message (sending) */
    guint gpg_mode;

    /* end of ints, begin bit fields */
    unsigned has_all_headers : 1;

    /* Indicate that uid's should always be trusted when signing a message */
    unsigned always_trust : 1;

    /* attach the GnuPG public key to the message (sending) */
    unsigned att_pubkey : 1;

    /* request a DSN (sending) */
    unsigned request_dsn : 1;
};

G_DEFINE_TYPE(LibBalsaMessage,
              libbalsa_message,
              G_TYPE_OBJECT)

static void
libbalsa_message_init(LibBalsaMessage * message)
{
    message->headers = g_new0(LibBalsaMessageHeaders, 1);
    message->mailbox = NULL;
    message->subj = NULL;
    message->references = NULL;
    message->in_reply_to = NULL;
    message->message_id = NULL;
    message->subtype = NULL;
    message->parameters = NULL;
    message->body_ref = 0;
    message->body_list = NULL;
    message->has_all_headers = 0;
    message->crypt_mode = LIBBALSA_PROTECT_NONE;
    message->ident = NULL;
    message->body_list = NULL;
    message->body_ref = 0;
    message->flags = LIBBALSA_MESSAGE_FLAG_NONE;
    message->gpg_mode = 0;
    message->has_all_headers = 0;
    message->att_pubkey = 0;
    message->request_dsn = 0;
}


static void
libbalsa_message_class_init(LibBalsaMessageClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = libbalsa_message_dispose;
    object_class->finalize = libbalsa_message_finalize;
}

LibBalsaMessage *
libbalsa_message_new(void)
{
    LibBalsaMessage *message;

    message = g_object_new(LIBBALSA_TYPE_MESSAGE, NULL);

    return message;
}

/* libbalsa_message_dispose:
   dispose methods must leave object in 'sane' state. 
   This means NULLifing released pointers.
*/
static void
libbalsa_message_dispose(GObject * object)
{
    LibBalsaMessage *message = LIBBALSA_MESSAGE(object);

    g_clear_object(&message->mime_msg);
    g_clear_object(&message->ident);

    G_OBJECT_CLASS(libbalsa_message_parent_class)->dispose(object);
}

/* libbalsa_message_finalize: */
static void
libbalsa_message_finalize(GObject * object)
{
    LibBalsaMessage *message = LIBBALSA_MESSAGE(object);

    libbalsa_message_headers_destroy(message->headers);

    g_list_free_full(message->references, g_free);
    g_list_free_full(message->in_reply_to, g_free);
    g_list_free_full(message->parameters, (GDestroyNotify) g_strfreev);

    g_free(message->message_id);
    g_free(message->subtype);
    g_free(message->subj);

    libbalsa_message_body_free(message->body_list);

    if (message->tempdir) {
        if (rmdir(message->tempdir) != 0)
            g_warning("Could not remove %s", message->tempdir);
        g_free(message->tempdir);
    }

    G_OBJECT_CLASS(libbalsa_message_parent_class)->finalize(object);
}


static void
lb_message_headers_extra_destroy(LibBalsaMessageHeaders *headers)
{
    if (headers == NULL)
	return;

    FREE_HEADER_LIST(headers->user_hdrs);
    headers->user_hdrs = NULL;

    g_free(headers->fcc_url);
    headers->fcc_url = NULL;
}


void
libbalsa_message_headers_destroy(LibBalsaMessageHeaders *headers)
{
    if (headers == NULL)
        return;


    g_free(headers->subject);
    headers->subject = NULL;

    if (headers->from) {
	g_object_unref(headers->from);
	headers->from = NULL;
    }

    if (headers->sender) {
	g_object_unref(headers->sender);
	headers->sender = NULL;
    }

    if (headers->to_list) {
	g_object_unref(headers->to_list);
	headers->to_list = NULL;
    }

    if (headers->content_type) {
	g_object_unref(headers->content_type);
	headers->content_type = NULL;
    }

    if (headers->cc_list) {
	g_object_unref(headers->cc_list);
	headers->cc_list = NULL;
    }

    if (headers->bcc_list) {
	g_object_unref(headers->bcc_list);
	headers->bcc_list = NULL;
    }

    if (headers->reply_to) {
	g_object_unref(headers->reply_to);
	headers->reply_to = NULL;
    }

    if(headers->dispnotify_to) {
	g_object_unref(headers->dispnotify_to);
	headers->dispnotify_to = NULL;
    }

    lb_message_headers_extra_destroy(headers);

#if defined ENABLE_AUTOCRYPT
    if (headers->autocrypt_hdr != NULL) {
    	g_object_unref(headers->autocrypt_hdr);
    	headers->autocrypt_hdr = NULL;
}
#endif

    g_free(headers);
}


const gchar *
libbalsa_message_body_charset(LibBalsaMessageBody *body)
{
    const gchar *charset;

    if (body == NULL)
        return NULL;


    if (body->charset) {
        /* This overrides all! Important for non
         * us-ascii messages over IMAP. */
        return body->charset;
    }


    if (GMIME_IS_PART(body->mime_part)) {
        GMimeContentType *type;

        type = g_mime_object_get_content_type(body->mime_part);
        return g_mime_content_type_get_parameter(type, "charset");
    }

    charset = libbalsa_message_body_charset(body->parts);
    if (charset)
        return charset;


    return libbalsa_message_body_charset(body->next);
}


/* UTF-8-aware header cleaning by Albrecht */
static void
canonize_header_value(gchar *value)
{
    gchar *dptr = value;

    while (*value) {
        if (g_unichar_isspace(g_utf8_get_char(value))) {
            do {
                value = g_utf8_next_char(value);
            } while (g_unichar_isspace(g_utf8_get_char(value)));
            *dptr++ = ' ';
        } else {
            gint bytes = g_utf8_next_char(value) - value;

            do {
                *dptr++ = *value++;
            } while (--bytes > 0);
        }
    }

    *dptr = '\0';
}

/* message_user_hdrs:
   returns allocated GList containing (header=>value) ALL headers pairs
   as generated by g_strsplit.
   The list has to be freed by the following chunk of code:
    FREE_HEADER_LIST(list);
*/
static gchar **
libbalsa_create_hdr_pair(const gchar * name, gchar * value)
{
    gchar **item = g_new(gchar *, 3);

    canonize_header_value(value);
    item[0] = g_strdup(name);
    item[1] = value;
    item[2] = NULL;
    return item;
}


static GList *
libbalsa_message_header_get_helper(LibBalsaMessageHeaders *headers,
                                   const gchar            *find)
{
    GList *list;
    for (list = headers->user_hdrs; list; list = list->next) {
        const gchar *const *tmp = list->data;

        if (g_ascii_strcasecmp(tmp[0], find) == 0)
            return list;
    }
    return NULL;
}


/** libbalsa_message_find_user_hdr:
    returns.... list element matching given header.
 */
static GList *
libbalsa_message_find_user_hdr(LibBalsaMessage *message,
                               const gchar     *find)
{
    LibBalsaMessageHeaders *headers = message->headers;

    g_return_val_if_fail(headers, NULL);
    if (headers->user_hdrs == NULL && message->mailbox)
        libbalsa_mailbox_set_msg_headers(message->mailbox, message);

    return libbalsa_message_header_get_helper(headers, find);
}


/*
 * Public user header methods
 */
const gchar *
libbalsa_message_header_get_one(LibBalsaMessageHeaders *headers,
                                const gchar            *find)
{
    GList *header;
    const gchar *const *pair;

    if ((header = libbalsa_message_header_get_helper(headers, find)) == NULL)
        return NULL;


    pair = header->data;
    return pair[1];
}


GList *
libbalsa_message_header_get_all(LibBalsaMessageHeaders *headers,
                                const gchar            *find)
{
    GList *header;
    const gchar *const *pair;
    GList *res = NULL;

    if ((header = libbalsa_message_header_get_helper(headers, find)) == NULL)
        return NULL;

    pair = header->data;
    for (pair++; *pair; pair++) {
        res = g_list_append(res, g_strdup(*pair));
    }

    return res;
}


const gchar *
libbalsa_message_get_user_header(LibBalsaMessage *message,
                                 const gchar     *name)
{
    GList *header;
    const gchar *const *pair;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    g_return_val_if_fail(name != NULL, NULL);

    if ((header = libbalsa_message_find_user_hdr(message, name)) == NULL)
        return NULL;


    pair = header->data;
    return pair[1];
}


void
libbalsa_message_set_user_header(LibBalsaMessage *message,
                                 const gchar     *name,
                                 const gchar     *value)
{
    LibBalsaMessageHeaders *headers;
    GList *header;

    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(name != NULL);

    headers = message->headers;
    g_return_if_fail(headers != NULL);

    if ((header = libbalsa_message_find_user_hdr(message, name))) {
        headers->user_hdrs =
            g_list_remove_link(headers->user_hdrs, header);
        FREE_HEADER_LIST(header);
    }

    if (value && *value) {
        headers->user_hdrs =
            g_list_prepend(headers->user_hdrs,
                           libbalsa_create_hdr_pair(name,
                                                    g_strdup(value)));
    }
}


static GList *
prepend_header_misc(GList      *res,
                    const char *name,
                    const char *value)
{
    char lcname[28]; /* one byte longer than the longest ignored header */
    static const char ignored_headers[] =
        "subject date from sender to cc bcc "
        "message-id references in-reply-to status lines"
        "disposition-notification-to";
    unsigned i;

    if (value[0] == '\0') {
	/* Empty header */
	return res;
    }

    /* Standard Headers*/
    for (i = 0; i < sizeof(lcname) - 1 && name[i] != '\0'; i++) {
        lcname[i] = tolower(name[i]);
    }
    lcname[i] = '\0';

    if (strstr(ignored_headers, lcname) != NULL)
        return res;

    return
        g_list_prepend(res,
                       libbalsa_create_hdr_pair(name,
                                                g_mime_utils_header_decode_text(libbalsa_parser_options(), value)));
}

/* 
 * libbalsa_message_user_hdrs_from_gmime:
 * 
 * returns allocated GList containing (header=>value) ALL headers
 * pairs as generated by g_strsplit. The list has to be freed by the
 * following chunk of code (or something functionally similar):
 * 
 * g_list_foreach(list, (GFunc) g_strfreev, NULL);
 * g_list_free(list);
*/


GList *
libbalsa_message_user_hdrs_from_gmime(GMimeMessage * message)
{
    GMimeHeaderList *hdrlist;
    gint count;
    gint i;
    GList *res = NULL;
    const char *value;

    g_return_val_if_fail(message != NULL, NULL);

    value = g_mime_message_get_message_id(message);
    if (value)
	res = g_list_prepend(res, libbalsa_create_hdr_pair("Message-ID",
					  g_strdup_printf("<%s>", value)));

    /* FIXME: This duplicates References headers since they are
       already present in LibBalsaMessage::references field.  FWIW,
       mailbox driver does not copy references to user_headers.
    */
    value = g_mime_object_get_header(GMIME_OBJECT(message), "References");
    if (value != NULL) {
#if BALSA_NEEDS_SEPARATE_USER_HEADERS
	GMimeReferences *references, *reference;
	references = g_mime_references_parse(NULL, value);
	for (reference = references; reference != NULL; reference = reference->next) {
	    res =
		g_list_prepend(res,
			       libbalsa_create_hdr_pair("References",
							g_strdup_printf
							("<%s>",
							 reference->
							 msgid)));
	}
	g_mime_references_clear(&references);
#else
        res = g_list_prepend(res,
                             libbalsa_create_hdr_pair("References",
                                                      g_strdup(value)));
#endif
    }

    value = g_mime_object_get_header(GMIME_OBJECT(message), "In-Reply-To");
    if (value) {
        res =
            g_list_prepend(res,
                           libbalsa_create_hdr_pair
                               ("In-Reply-To",
                               g_mime_utils_header_decode_text(libbalsa_parser_options(), value)));
    }

    hdrlist = g_mime_object_get_header_list (GMIME_OBJECT(message));
    count = g_mime_header_list_get_count(hdrlist);
    for (i = 0; i < count; i++) {
        GMimeHeader *header;

        header = g_mime_header_list_get_header_at(hdrlist, i);
        res = prepend_header_misc(res,
                                  g_mime_header_get_name(header),
                                  g_mime_header_get_value(header));
    }

    return g_list_reverse(res);
}


/* libbalsa_message_get_part_by_id:
   return a message part identified by Content-ID=id
   message must be referenced. (FIXME?)
 */
LibBalsaMessageBody *
libbalsa_message_get_part_by_id(LibBalsaMessage *msg,
                                const gchar     *id)
{
    return libbalsa_message_body_get_by_id(msg->body_list, id);
}


/* libbalsa_message_save:
   return TRUE on success and FALSE on failure.
 */
gboolean
libbalsa_message_save(LibBalsaMessage *message,
                      const gchar     *filename)
{
    FILE *outfile;
    int res;
    GMimeStream *msg_stream;
    GMimeStream *out_stream;

    g_return_val_if_fail(message->mailbox, FALSE);

    if ((outfile = fopen(filename, "w")) == NULL)
        return FALSE;

    msg_stream = libbalsa_message_stream(message);
    if (msg_stream == NULL) {
        fclose(outfile);
        unlink(filename);
        return FALSE;
    }

    out_stream = g_mime_stream_file_new(outfile);
    libbalsa_mailbox_lock_store(message->mailbox);
    res = g_mime_stream_write_to_stream(msg_stream, out_stream);
    libbalsa_mailbox_unlock_store(message->mailbox);

    g_object_unref(msg_stream);
    g_object_unref(out_stream);

    return res >= 0;
}

LibBalsaMessageAttach
libbalsa_message_get_attach_icon(LibBalsaMessage * message)
{
    LibBalsaMessageAttach attach_icon;

    if (libbalsa_message_is_encrypted(message)) {
	attach_icon = LIBBALSA_MESSAGE_ATTACH_ENCR;
    } else if (message->crypt_mode != LIBBALSA_PROTECT_NONE ||
	libbalsa_message_is_signed(message)) {
	switch (message->crypt_mode) {
	case LIBBALSA_PROTECT_SIGN_GOOD:
	    attach_icon = LIBBALSA_MESSAGE_ATTACH_GOOD;
            break;
	case LIBBALSA_PROTECT_SIGN_NOTRUST:
	    attach_icon = LIBBALSA_MESSAGE_ATTACH_NOTRUST;
            break;
	case LIBBALSA_PROTECT_SIGN_BAD:
	    attach_icon = LIBBALSA_MESSAGE_ATTACH_BAD;
            break;
	case LIBBALSA_PROTECT_ENCRYPT:
	    attach_icon = LIBBALSA_MESSAGE_ATTACH_ENCR;
            break;
	default:
	    attach_icon = LIBBALSA_MESSAGE_ATTACH_SIGN;
            break;
	}
    } else if (libbalsa_message_has_attachment(message)) {
	attach_icon = LIBBALSA_MESSAGE_ATTACH_ATTACH;
    } else {
	attach_icon = LIBBALSA_MESSAGE_ATTACH_ICONS_NUM;
    }

    return attach_icon;
}

/* Tell the mailbox driver to change flags. */
void
libbalsa_message_change_flags(LibBalsaMessage * message,
                              LibBalsaMessageFlag set,
                              LibBalsaMessageFlag clear)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(LIBBALSA_IS_MAILBOX(message->mailbox));
    g_return_if_fail(message->msgno > 0);

    if (libbalsa_mailbox_get_readonly(message->mailbox)) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Mailbox (%s) is read-only: "
                               "cannot change flags."),
                             libbalsa_mailbox_get_name(message->mailbox));
        return;
    }

    libbalsa_mailbox_msgno_change_flags(message->mailbox, message->msgno,
                                        set, clear);
}


void
libbalsa_message_reply(LibBalsaMessage *message)
{
    g_return_if_fail(message->mailbox);
    libbalsa_lock_mailbox(message->mailbox);
    libbalsa_message_change_flags(message, LIBBALSA_MESSAGE_FLAG_REPLIED, 0);
    libbalsa_unlock_mailbox(message->mailbox);
}


/* libbalsa_message_body_ref:
   references the structure of given message possibly fetching also all
   headers.
   message parts can be fetched later on.
 */
gboolean
libbalsa_message_body_ref(LibBalsaMessage *message,
                          gboolean         fetch_all_headers)
{
    LibBalsaFetchFlag flags = 0;
    gboolean retval         = TRUE;

    g_return_val_if_fail(message, FALSE);
    if (message->mailbox == NULL)
        return FALSE;

    g_return_val_if_fail(MAILBOX_OPEN(message->mailbox), FALSE);

    libbalsa_lock_mailbox(message->mailbox);

    if (fetch_all_headers && message->has_all_headers == 0)
        flags |= LB_FETCH_RFC822_HEADERS;

    if ((message->body_ref == 0) && !message->body_list) {
        /* not fetched yet */
        flags |= LB_FETCH_STRUCTURE;
    }

    if (flags) {
        retval =
            libbalsa_mailbox_fetch_message_structure(message->mailbox,
                                                     message, flags);
    }

    if (retval)
        message->body_ref++;
    libbalsa_unlock_mailbox(message->mailbox);

    return retval;
}


void
libbalsa_message_body_unref(LibBalsaMessage *message)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    if (message->body_ref == 0)
        return;


    if (message->mailbox)
        libbalsa_lock_mailbox(message->mailbox);
    if (--message->body_ref == 0) {
        libbalsa_message_body_free(message->body_list);
        message->body_list = NULL;
        if (message->mailbox)
            libbalsa_mailbox_release_message(message->mailbox, message);

        /* Free headers that we no longer need. */
        lb_message_headers_extra_destroy(message->headers);
        message->has_all_headers = 0;
    }
    if (message->mailbox)
        libbalsa_unlock_mailbox(message->mailbox);
}


gboolean
libbalsa_message_is_multipart(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->headers->content_type ?
           g_mime_content_type_is_type(message->headers->content_type,
                                       "multipart", "*") : FALSE;
}


gboolean
libbalsa_message_is_partial(LibBalsaMessage *message,
                            gchar          **id)
{
    GMimeContentType *content_type;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    content_type = message->headers->content_type;
    if (content_type == NULL
        || !g_mime_content_type_is_type(content_type,
                                        "message", "partial"))
        return FALSE;


    if (id)
        *id = g_strdup(g_mime_content_type_get_parameter(content_type,
                                                         "id"));

    return TRUE;
}


/* Go through all parts and try to figure out whether it is a message
   with attachments or not. It still yields insatsfactory
   results... */
static gboolean
has_attached_part(LibBalsaMessageBody *body)
{
    LibBalsaMessageBody *lbbody;
    /* the condition matches the one used in add_multipart_mixed() */
    for (lbbody = body; lbbody; lbbody = lbbody->next) {
        g_debug("part %s has disposition %s", lbbody->content_type, lbbody->content_dsp);
        if (!libbalsa_message_body_is_multipart(lbbody) &&
            !libbalsa_message_body_is_inline(lbbody)) {
            return TRUE;
        }

        if (lbbody->parts && has_attached_part(lbbody->parts))
            return TRUE;
    }
    /* no part was an  attachment */
    return FALSE;
}


gboolean
libbalsa_message_has_attachment(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    /* A message has attachments if main message or one of the parts
       has Content-type: multipart/mixed AND members with
       Content-disposition: attachment. Unfortunately, part list may
       not be available at this stage. */
    if (message->body_list == NULL) {
        return message->headers->content_type &&
            g_mime_content_type_is_type(message->headers->content_type,
                                        "multipart", "mixed");
    } else {
        /* use "exact" algorithm */
        return (has_attached_part(message->body_list->next) ||
		has_attached_part(message->body_list->parts));
    }
 }

static gboolean
libbalsa_message_is_signed(LibBalsaMessage * message)
{
	gboolean is_signed = FALSE;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    if (message->headers->content_type != NULL) {
    	GMimeContentType *ct = message->headers->content_type; /* convenience pointer */

    	if (g_mime_content_type_is_type(ct, "multipart", "signed") ||
    		((g_mime_content_type_is_type(ct, "application", "pkcs7-mime") ||
    		  g_mime_content_type_is_type(ct, "application", "x-pkcs7-mime")) &&
    		 g_ascii_strcasecmp(g_mime_content_type_get_parameter(ct,"smime-type" ), "signed-data") == 0)) {
    		is_signed = TRUE;
    	}
    }
    return is_signed;
}

static gboolean
libbalsa_message_is_encrypted(LibBalsaMessage * message)
{
	gboolean encrypted = FALSE;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    if (message->headers->content_type != NULL) {
    	GMimeContentType *ct = message->headers->content_type; /* convenience pointer */

    	if (g_mime_content_type_is_type(ct, "multipart", "encrypted") ||
    		((g_mime_content_type_is_type(ct, "application", "pkcs7-mime") ||
    		  g_mime_content_type_is_type(ct, "application", "x-pkcs7-mime")) &&
    		 g_ascii_strcasecmp(g_mime_content_type_get_parameter(ct,"smime-type" ), "enveloped-data") == 0)) {
    		encrypted = TRUE;
    	}
    }
    return encrypted;
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

/* libbalsa_message_set_dispnotify:
   sets a disposition notify to a given address
   address can be NULL.
*/
void
libbalsa_message_set_dispnotify(LibBalsaMessage * message,
                                InternetAddress * ia)
{
    g_return_if_fail(message);

    if (message->headers->dispnotify_to != NULL) {
    	g_object_unref(message->headers->dispnotify_to);
    }
    if (ia) {
	message->headers->dispnotify_to = internet_address_list_new ();
	internet_address_list_add (message->headers->dispnotify_to, ia);
    } else {
	message->headers->dispnotify_to = NULL;
    }
}

/* libbalsa_message_get_subject:
   get constant pointer to the subject of the message; 
*/
const gchar *
libbalsa_message_get_subject(LibBalsaMessage *message)
{
    return message->subj != NULL ? message->subj : _("(No subject)");
}

/* Populate headers from mime_msg, but only the members that are needed
 * all the time. */

static InternetAddressList *
lb_message_address_list_ref(InternetAddressList * list)
{
    if (list != NULL && internet_address_list_length(list) > 0) {
        return g_object_ref(list);
    }

    return NULL;
}

static InternetAddressList *
lb_message_recipients(GMimeMessage   * message,
                      GMimeAddressType type)
{
    return lb_message_address_list_ref(g_mime_message_get_addresses(message, type));
}

static void
lb_message_headers_basic_from_gmime(LibBalsaMessageHeaders *headers,
                                    GMimeMessage           *mime_msg)
{
    if (headers->from == NULL) {
        headers->from =
            lb_message_address_list_ref(g_mime_message_get_from(mime_msg));
    }

    if (headers->sender == NULL) {
        headers->sender =
            lb_message_address_list_ref(g_mime_message_get_addresses(mime_msg, GMIME_ADDRESS_TYPE_SENDER));
    }

    if (headers->date == 0) {
        GDateTime *datetime;

        datetime = g_mime_message_get_date(mime_msg);
        if (datetime != NULL)
            headers->date = g_date_time_to_unix(datetime);
    }

    if (headers->to_list == NULL) {
        headers->to_list =
            lb_message_recipients(mime_msg, GMIME_ADDRESS_TYPE_TO);
    }

    if (headers->content_type == NULL) {
        g_set_object(&headers->content_type,
                     g_mime_object_get_content_type(mime_msg->mime_part));
    }
}

/* Populate headers from mime_msg, but only the members not handled in
 * lb_message_headers_basic_from_gmime. */
static void
lb_message_headers_extra_from_gmime(LibBalsaMessageHeaders *headers,
                                    GMimeMessage           *mime_msg)
{
    if (headers->reply_to == NULL) {
        headers->reply_to =
            lb_message_address_list_ref(g_mime_message_get_reply_to(mime_msg));
    }

    if (headers->dispnotify_to == NULL) {
        const gchar *value =
            g_mime_object_get_header(GMIME_OBJECT(mime_msg), "Disposition-Notification-To");

        if (value != NULL)
            headers->dispnotify_to =
                internet_address_list_parse(libbalsa_parser_options(), value);
    }

    if (headers->cc_list == NULL) {
        headers->cc_list =
            lb_message_recipients(mime_msg, GMIME_ADDRESS_TYPE_CC);
    }

    if (headers->bcc_list == NULL) {
        headers->bcc_list =
            lb_message_recipients(mime_msg, GMIME_ADDRESS_TYPE_BCC);
    }

    /* Get fcc from message */
    if (headers->fcc_url == NULL) {
	headers->fcc_url =
	    g_strdup(g_mime_object_get_header(GMIME_OBJECT(mime_msg), "X-Balsa-Fcc"));
    }
}

/* Populate headers from the info in mime_msg. */
void
libbalsa_message_headers_from_gmime(LibBalsaMessageHeaders *headers,
				    GMimeMessage *mime_msg)
{
    lb_message_headers_basic_from_gmime(headers, mime_msg);
    lb_message_headers_extra_from_gmime(headers, mime_msg);
#if defined ENABLE_AUTOCRYPT
    if (headers->autocrypt_hdr == NULL) {
    	GDateTime *reftime;

    	/* set the reference date far in the future so we can ignore messages w/o Date: header */
    	reftime = g_date_time_new_from_unix_utc(time(NULL) + (365 * 24 * 60 * 60));
    	headers->autocrypt_hdr = g_mime_message_get_autocrypt_header(mime_msg, reftime);
    	/* remember if the message really included an Autocrypt header, or if GMime
    	 * returned an incomplete one (see
    	 * https://developer.gnome.org/gmime/stable/GMimeMessage.html#g-mime-message-get-autocrypt-header).
    	 */
    	headers->autocrypt_hdr_present =
    		(g_mime_object_get_header(GMIME_OBJECT(mime_msg), "autocrypt") != NULL);
    	g_date_time_unref(reftime);
    }
#endif
}

/* Populate message and message->headers from the info in mime_msg,
 * but only the members that are needed all the time. */
void
libbalsa_message_init_from_gmime(LibBalsaMessage * message,
				 GMimeMessage *mime_msg)
{
    const gchar *header;

    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(GMIME_IS_MESSAGE(mime_msg));

    header = g_mime_message_get_subject(mime_msg);
    libbalsa_message_set_subject_from_header(message, header);

    header = g_mime_object_get_header(GMIME_OBJECT(mime_msg), "Content-Length");
    if (header) {
        char *endptr;

        message->length = strtoll(header, &endptr, 10);
        if (*endptr != '\0') {
            message->length = -1;
            g_debug("Bad Content-Length header: “%s”; using -1", header);
        }
    }

    header = g_mime_message_get_message_id(mime_msg);
    if (header)
        message->message_id = g_strdup(header);

    header = g_mime_object_get_header(GMIME_OBJECT(mime_msg), "References");
    if (header)
        libbalsa_message_set_references_from_string(message, header);

    header = g_mime_object_get_header(GMIME_OBJECT(mime_msg), "In-Reply-To");
    if (header)
        libbalsa_message_set_in_reply_to_from_string(message, header);

    lb_message_headers_basic_from_gmime(message->headers, mime_msg);
}


/* Create a newly allocated list of references for threading.
 * This is a deep copy, with its own strings: deallocate with
 * g_free and g_list_free. */
GList *
libbalsa_message_refs_for_threading(LibBalsaMessage *message)
{
    GList *tmp;
    GList *foo;

    g_return_val_if_fail(message != NULL, NULL);

    if (message->in_reply_to != NULL && message->in_reply_to->next != NULL)
        return NULL;

    tmp = g_list_copy(message->references);

    if (message->in_reply_to) {
        /* some mailers provide in_reply_to but no references, and
         * some apparently provide both but with the references in
         * the wrong order; we'll just make sure it's the last item
         * of this list */
        foo = g_list_find_custom(tmp, message->in_reply_to->data,
                                 (GCompareFunc) strcmp);

        if (foo != NULL)
            tmp = g_list_delete_link(tmp, foo);
        tmp = g_list_append(tmp, message->in_reply_to->data);
    }

    for (foo = tmp; foo != NULL; foo = foo->next)
        foo->data = g_strdup((gchar *) foo->data);

    return tmp;
}

static GList *
references_decode(const gchar * str)
{
    GMimeReferences *references;
    GList *list = NULL;
    gint length;
    gint i;

    references = g_mime_references_parse(NULL, str);
    length = g_mime_references_length(references);
    for (i = 0; i < length; i++) {
        const gchar *message_id;

        message_id = g_mime_references_get_message_id(references, i);
        list = g_list_prepend(list, g_strdup(message_id));
    }
    g_mime_references_free(references);

    return g_list_reverse(list);
}

void
libbalsa_message_set_references_from_string(LibBalsaMessage * message,
                                            const gchar     * str)
{
    /* Empty references are acceptable but require no action. Similarly,
     * if references were set already, there is no reason to set them
     * again - they are immutable anyway. */
    if (message->references == NULL && str != NULL)
        message->references = references_decode(str);
}

void
libbalsa_message_set_in_reply_to_from_string(LibBalsaMessage * message,
                                             const gchar     * str)
{
    if (message->in_reply_to == NULL && str != NULL) {
        /* FIXME for Balsa's old non-compliant header */
        gchar *p = strrchr(str, ';');
        p = p ? g_strndup(str, p - str) : g_strdup(str);
        message->in_reply_to = references_decode(p);
        g_free(p);
    }
}

/* set a header, if (all) or if it's needed all the time:
 *   headers->from
 *   headers->date
 *   headers->to_list
 *   headers->content_type
 *   subj
 *   length
 *
 * needed for threading local mailboxes:
 *   message_id
 *   references
 *   in_reply_to
 */
static gboolean
lbmsg_set_header(LibBalsaMessage *message,
                 const gchar     *name,
                 const gchar     *value,
                 gboolean         all)
{
    gchar *val = NULL;
    LibBalsaMessageHeaders *headers = message->headers;

    if (libbalsa_text_attr_string(value)) {
        /* Broken header: force it to utf8 using Balsa's fallback
         * charset, then rfc2047-encode it for passing to the
         * appropriate GMime decoder. */
        gchar *tmp = g_strdup(value);
        libbalsa_utf8_sanitize(&tmp, TRUE, NULL);
        val = g_mime_utils_header_encode_text(NULL, tmp, NULL);
        g_free(tmp);
        g_debug("%s: non-ascii “%s” header “%s” encoded as “%s”",
                __func__, name, value, val);
        value = val;
    }

    if (g_ascii_strcasecmp(name, "Subject") == 0) {
        if (strcmp(value,
                    "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA") == 0) {
            g_free(val);
            return FALSE;
        }
        libbalsa_message_set_subject_from_header(message, value);
    } else if (g_ascii_strcasecmp(name, "Date") == 0) {
        GDateTime *datetime;

        datetime = g_mime_utils_header_decode_date(value);
        if (datetime != NULL) {
            headers->date = g_date_time_to_unix(datetime);
            g_date_time_unref(datetime);
        }
    } else if ((headers->from == NULL) &&
               (g_ascii_strcasecmp(name, "From") == 0)) {
        headers->from = internet_address_list_parse(libbalsa_parser_options(), value);
    } else if ((headers->to_list == NULL) &&
               (g_ascii_strcasecmp(name, "To") == 0)) {
        headers->to_list = internet_address_list_parse(libbalsa_parser_options(), value);
    } else if (g_ascii_strcasecmp(name, "In-Reply-To") == 0) {
        libbalsa_message_set_in_reply_to_from_string(message, value);
    } else if ((message->message_id == NULL) &&
               (g_ascii_strcasecmp(name, "Message-ID") == 0)) {
        message->message_id = g_mime_utils_decode_message_id(value);
    } else if (g_ascii_strcasecmp(name, "References") == 0) {
        libbalsa_message_set_references_from_string(message, value);
    } else if ((headers->content_type == NULL) &&
               (g_ascii_strcasecmp(name, "Content-Type") == 0)) {
        headers->content_type = g_mime_content_type_parse(libbalsa_parser_options(), value);
    } else if ((headers->dispnotify_to == NULL) &&
               (g_ascii_strcasecmp(name, "Disposition-Notification-To") == 0)) {
        headers->dispnotify_to = internet_address_list_parse(libbalsa_parser_options(), value);
    } else if (g_ascii_strcasecmp(name, "Content-Length") == 0) {
        char *endptr;

        message->length = strtoll(value, &endptr, 10);
        if (*endptr != '\0') {
            message->length = -1;
            g_debug("Bad Content-Length header: “%s”; using -1", value);
        }
    } else if (all) {
        headers->user_hdrs =
            g_list_prepend(headers->user_hdrs,
                           libbalsa_create_hdr_pair(name, g_strdup(value)));
    }

    g_free(val);

    return TRUE;
}


static gboolean
lb_message_set_headers_from_string(LibBalsaMessage *message,
                                   const gchar     *lines,
                                   gboolean         all)
{
    gchar *header, *value;
    const gchar *val, *eoh;
    do {
        for (val = lines; *val && *val > 32 && *val < 127 && *val != ':'; val++) {
        }
        if (*val != ':')   /* parsing error */
            return FALSE;

        for (eoh = val + 1; *eoh && (eoh[0] != '\n' || isspace(eoh[1])); eoh++) {
        }
        header = g_strndup(lines, val - lines);
        lines  = eoh;
        for (val = val + 1; *val && isspace(*val); val++) {
            /* strip spaces at front... */
        }
        while (eoh > val && isspace(*eoh)) {
            eoh--;                             /* .. and at the end */
        }
        value = g_strndup(val, eoh - val + 1);

        lbmsg_set_header(message, header, value, all);
        g_free(header);
        g_free(value);
        if (*lines == '\0' || *++lines == '\0')
            break;
    } while (1);
    return TRUE;
}


void
libbalsa_message_load_envelope_from_stream(LibBalsaMessage *message,
                                           GMimeStream     *gmime_stream)
{
    GMimeStream *gmime_stream_filter;
    GMimeFilter *gmime_filter_crlf;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;
    guchar lookahead;

    libbalsa_mime_stream_shared_lock(gmime_stream);

    /* CRLF-filter the message stream; we do not want '\r' in header
     * fields, and finding the empty line that separates the body from
     * the header is simpler if it has no '\r' in it. */
    gmime_stream_filter =
        g_mime_stream_filter_new(gmime_stream);

    gmime_filter_crlf = g_mime_filter_dos2unix_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(gmime_stream_filter),
                             gmime_filter_crlf);
    g_object_unref(gmime_filter_crlf);

    /* Buffer the message stream, so we can read it line by line. */
    gmime_stream_buffer =
        g_mime_stream_buffer_new(gmime_stream_filter,
                                 GMIME_STREAM_BUFFER_BLOCK_READ);
    g_object_unref(gmime_stream_filter);

    /* Read header fields until either:
     * - we find an empty line, or
     * - end of file.
     */
    line = g_byte_array_new();
    do {
        g_mime_stream_buffer_readln(gmime_stream_buffer, line);
        while (g_mime_stream_read(gmime_stream_buffer,
                                  (char *) &lookahead, 1) == 1
               && (lookahead == ' ' || lookahead == '\t')) {
            g_byte_array_append(line, &lookahead, 1);
            g_mime_stream_buffer_readln(gmime_stream_buffer, line);
        }
        if ((line->len == 0) || (line->data[line->len - 1] != '\n')) {
            /* EOF or read error; in either case, message has no body. */
            break;
        }
        line->data[line->len - 1] = '\0'; /* terminate line by overwriting '\n' */
        if (!lb_message_set_headers_from_string(message, (gchar *) line->data, FALSE)) {
            /* Ignore error return caused by malformed header. */
        }
        if (lookahead == '\n') {/* end of header */
            /* Message looks valid--set its length. */
            message->length = g_mime_stream_length(gmime_stream);
            break;
        }
        g_byte_array_set_size(line, 0);
        g_byte_array_append(line, &lookahead, 1);
    } while (TRUE);
    g_byte_array_free(line, TRUE);

    g_object_unref(gmime_stream_buffer);
    g_mime_stream_reset(gmime_stream);
    libbalsa_mime_stream_shared_unlock(gmime_stream);
}


void
libbalsa_message_load_envelope(LibBalsaMessage *message)
{
    GMimeStream *gmime_stream;

    gmime_stream = libbalsa_message_stream(message);
    if (gmime_stream == NULL)
        return;


    libbalsa_message_load_envelope_from_stream(message, gmime_stream);
    g_object_unref(gmime_stream);
}


GMimeStream *
libbalsa_message_stream(LibBalsaMessage *message)
{
    LibBalsaMailbox *mailbox;
    GMimeStream *mime_stream;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    mailbox = message->mailbox;
    g_return_val_if_fail(mailbox != NULL || message->mime_msg != NULL,
                         NULL);

    if (mailbox)
        return libbalsa_mailbox_get_message_stream(mailbox,
                                                   message->msgno, FALSE);


    mime_stream = g_mime_stream_mem_new();
    g_mime_object_write_to_stream(GMIME_OBJECT(message->mime_msg), NULL,
                                  mime_stream);
    g_mime_stream_reset(mime_stream);

    return mime_stream;
}


gboolean
libbalsa_message_copy(LibBalsaMessage *message,
                      LibBalsaMailbox *dest,
                      GError         **err)
{
    LibBalsaMailbox *mailbox;
    gboolean retval;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(dest), FALSE);
    mailbox = message->mailbox;
    g_return_val_if_fail(mailbox != NULL || message->mime_msg != NULL,
                         FALSE);

    if (mailbox) {
        GArray *msgnos = g_array_sized_new(FALSE, FALSE, sizeof(guint), 1);
        g_array_append_val(msgnos, message->msgno);
        retval =
            libbalsa_mailbox_messages_copy(mailbox, msgnos, dest, err);
        g_array_free(msgnos, TRUE);
    } else {
        GMimeStream *mime_stream = libbalsa_message_stream(message);
        retval = libbalsa_mailbox_add_message(dest, mime_stream,
                                              message->flags, err);
        g_object_unref(mime_stream);
    }

    return retval;
}


void
libbalsa_message_set_subject(LibBalsaMessage *message,
                             const gchar     *subject)
{
    g_free(message->subj);
    message->subj = g_strdup(subject);
    libbalsa_utf8_sanitize(&message->subj, TRUE, NULL);
    canonize_header_value(message->subj);
}


void
libbalsa_message_set_subject_from_header(LibBalsaMessage *message,
                                         const gchar     *header)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    if (header) {
        gchar *subject =
            g_mime_utils_header_decode_text(libbalsa_parser_options(), header);
        libbalsa_message_set_subject(message, subject);
        g_free(subject);
    }
}


const gchar *
libbalsa_message_get_tempdir(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    if (message->tempdir == NULL)
        if (!libbalsa_mktempdir(&message->tempdir))
            g_warning("Could not make tempdir");

    return message->tempdir;
}


/*
 * Getters
 */

LibBalsaMailbox *
libbalsa_message_get_mailbox(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->mailbox;
}


LibBalsaMessageHeaders *
libbalsa_message_get_headers(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->headers;
}


LibBalsaMessageBody *
libbalsa_message_get_body_list(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->body_list;
}


GMimeMessage *
libbalsa_message_get_mime_message(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->mime_msg;
}


LibBalsaMessageFlag
libbalsa_message_get_flags(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), 0);

    return message->flags;
}


const gchar *
libbalsa_message_get_message_id(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->message_id;
}


guint
libbalsa_message_get_msgno(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), 0);

    return message->msgno;
}


gint64
libbalsa_message_get_length(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), 0);

    return message->length;
}


gboolean
libbalsa_message_get_has_all_headers(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->has_all_headers != 0;
}


gboolean
libbalsa_message_get_request_dsn(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->request_dsn != 0;
}


GList *
libbalsa_message_get_references(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->references;
}


LibBalsaIdentity *
libbalsa_message_get_identity(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->ident;
}



GList *
libbalsa_message_get_parameters(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->parameters;
}


const gchar *
libbalsa_message_get_subtype(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->subtype;
}


guint
libbalsa_message_get_crypt_mode(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), 0);

    return message->crypt_mode;
}


gboolean
libbalsa_message_get_always_trust(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->always_trust != 0;
}


GList *
libbalsa_message_get_in_reply_to(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);

    return message->in_reply_to;
}


gboolean
libbalsa_message_get_attach_pubkey(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->att_pubkey != 0;
}


gboolean
libbalsa_message_has_crypto_content(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return libbalsa_message_body_has_crypto_content(message->body_list);
}


guint
libbalsa_message_get_body_ref(LibBalsaMessage *message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), 0);

    return message->body_ref;
}


/*
 * Setters
 */

void
libbalsa_message_set_flags(LibBalsaMessage    *message,
                           LibBalsaMessageFlag flags)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->flags = flags;
}


void
libbalsa_message_set_mailbox(LibBalsaMessage *message,
                             LibBalsaMailbox *mailbox)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(mailbox == NULL || LIBBALSA_IS_MAILBOX(mailbox));

    message->mailbox = mailbox;
}


void
libbalsa_message_set_msgno(LibBalsaMessage *message,
                           guint            msgno)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->msgno = msgno;
}


void
libbalsa_message_set_has_all_headers(LibBalsaMessage *message,
                                     gboolean         has_all_headers)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->has_all_headers = has_all_headers ? 1 : 0;
}


void
libbalsa_message_set_length(LibBalsaMessage *message,
                            gint64           length)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->length = length;
}


void
libbalsa_message_set_mime_message(LibBalsaMessage *message,
                              GMimeMessage    *mime_message)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(mime_message == NULL || GMIME_IS_MESSAGE(mime_message));

    g_set_object(&message->mime_msg, mime_message);
}


void
libbalsa_message_set_message_id(LibBalsaMessage *message,
                                const gchar     *message_id)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    g_free(message->message_id);
    message->message_id = g_strdup(message_id);
}


void
libbalsa_message_set_request_dsn(LibBalsaMessage *message,
                                 gboolean         request_dsn)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->request_dsn = request_dsn ? 1 : 0;
}


void
libbalsa_message_set_subtype(LibBalsaMessage *message,
                             const gchar     *subtype)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    g_free(message->subtype);
    message->subtype = g_strdup(subtype);
}


void
libbalsa_message_set_body_list(LibBalsaMessage     *message,
                               LibBalsaMessageBody *body_list)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->body_list = body_list;
}


void
libbalsa_message_set_references(LibBalsaMessage *message,
                                GList           *references)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    g_list_free_full(message->references, g_free);
    /* We steal the GList and its data; see libbalsa_message_finalize */
    message->references = references;
}


void
libbalsa_message_set_in_reply_to(LibBalsaMessage *message,
                                 GList           *in_reply_to)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    g_list_free_full(message->in_reply_to, g_free);
    /* We steal the GList and its data; see libbalsa_message_finalize */
    message->in_reply_to = in_reply_to;
}


void
libbalsa_message_set_crypt_mode(LibBalsaMessage *message,
                                guint            mode)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->crypt_mode = mode;
}


void
libbalsa_message_set_always_trust(LibBalsaMessage *message,
                                  gboolean         mode)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->always_trust = mode ? 1 : 0;
}


void
libbalsa_message_set_attach_pubkey(LibBalsaMessage *message,
                                   gboolean         att_pubkey)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->att_pubkey = att_pubkey ? 1 : 0;
}


void
libbalsa_message_set_identity(LibBalsaMessage  *message,
                              LibBalsaIdentity *ident)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
    g_return_if_fail(ident == NULL || LIBBALSA_IS_IDENTITY(ident));

    g_set_object(&message->ident, ident);
}


void
libbalsa_message_add_parameters(LibBalsaMessage *message,
                                gchar          **parameters)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    message->parameters = g_list_prepend(message->parameters, parameters);
}
