/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

/* DESIGN NOTES.
   MESSAGE_COPY_CONTENT define is an attempt to reduce memory usage of balsa.
   When it is defined, The message date is stored in one place only (in
   libmutt structures). This should reduce memory usage to some extent.
   However, it is not implemented very extensively at the present moment
   and the memory usage reduction is hardly noticeable.
   - Lack of inline functions in C increases program complexity. This cost
   can be accepted.
   - thorough analysis of memory usage is needed.
*/
   
#include "config.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <glib.h>
#include <libgnome/gnome-i18n.h> 

#include "libbalsa.h"
#include "libbalsa_private.h"

/* needed for truncate_string */
#include "misc.h"

#include <gmime/gmime.h>

static void libbalsa_message_class_init(LibBalsaMessageClass * klass);
static void libbalsa_message_init(LibBalsaMessage * message);

static void libbalsa_message_finalize(GObject * object);


static GObjectClass *parent_class = NULL;

GType
libbalsa_message_get_type()
{
    static GType libbalsa_message_type = 0;

    if (!libbalsa_message_type) {
	static const GTypeInfo libbalsa_message_info = {
	    sizeof(LibBalsaMessageClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_message_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMessage),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_message_init
	};

	libbalsa_message_type =
	    g_type_register_static(G_TYPE_OBJECT, "LibBalsaMessage",
                                   &libbalsa_message_info, 0);
    }

    return libbalsa_message_type;
}

int message_cnt = 0;
static void
libbalsa_message_init(LibBalsaMessage * message)
{
    message->headers = g_new0(LibBalsaMessageHeaders, 1);
    message->flags = 0;
    message->mailbox = NULL;
    message->sender = NULL;
    message->subj = NULL;
    message->references = NULL;
    message->in_reply_to = NULL;
    message->message_id = NULL;
    message->subtype = 0;
    message->parameters = NULL;
    message->body_ref = 0;
    message->body_list = NULL;
    message->has_all_headers = 0;
#ifdef HAVE_GPGME
    message->prot_state = LIBBALSA_MSG_PROTECT_NONE;
#endif
    /* printf("%p message created.\n", message); */ message_cnt++;
}


static void
libbalsa_message_class_init(LibBalsaMessageClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_message_finalize;
}

LibBalsaMessage *
libbalsa_message_new(void)
{
    LibBalsaMessage *message;

    message = g_object_new(LIBBALSA_TYPE_MESSAGE, NULL);

    return message;
}

/* libbalsa_message_finalize:
   finalize methods must leave object in 'sane' state. 
   This means NULLifing released pointers.
*/
static void
libbalsa_message_finalize(GObject * object)
{
    LibBalsaMessage *message;

    g_return_if_fail(object != NULL);
    g_return_if_fail(LIBBALSA_IS_MESSAGE(object));

    message = LIBBALSA_MESSAGE(object);

    libbalsa_message_headers_destroy(message->headers);
    message->headers = NULL;

    if (message->sender) {
	g_object_unref(message->sender);
	message->sender = NULL;
    }

#if MESSAGE_COPY_CONTENT
    g_free(message->subj);
    message->subj = NULL;
#endif
    g_list_foreach(message->references, (GFunc) g_free, NULL);
    g_list_free(message->references);
    message->references = NULL;

    g_list_foreach(message->in_reply_to, (GFunc) g_free, NULL);
    g_list_free(message->in_reply_to);
    message->in_reply_to = NULL;

    g_free(message->message_id);
    message->message_id = NULL;

    g_free(message->subtype);
    message->subtype = NULL;

    g_list_foreach(message->parameters, (GFunc) g_strfreev, NULL);
    g_list_free(message->parameters);
    message->parameters = NULL;


    libbalsa_message_body_free(message->body_list);
    message->body_list = NULL;

    if (message->mime_msg) {
	g_object_unref(message->mime_msg);
	message->mime_msg = NULL;
    }
    G_OBJECT_CLASS(parent_class)->finalize(object);
    /* printf("%p message finalized.\n", message); */  message_cnt--;
}

static void
libbalsa_message_find_charset(GMimeObject * mime_part,
                              const gchar ** charset)
{
    const GMimeContentType *type;

    if (*charset)
        return;

    type = g_mime_object_get_content_type(mime_part);
    *charset = g_mime_content_type_get_parameter(type, "charset");
}

static void
lb_message_headers_extra_destroy(LibBalsaMessageHeaders * headers)
{
    FREE_HEADER_LIST(headers->user_hdrs);
    headers->user_hdrs = NULL;

    g_free(headers->fcc_url);
    headers->fcc_url = NULL;
}

void 
libbalsa_message_headers_destroy(LibBalsaMessageHeaders * headers)
{
    if (!headers)
	return;

    g_free(headers->subject);
    headers->subject = NULL;

    if (headers->from) {
	g_object_unref(headers->from);
	headers->from = NULL;
    }

    g_list_foreach(headers->to_list, (GFunc) g_object_unref, NULL);
    g_list_free(headers->to_list);
    headers->to_list = NULL;

    if (headers->content_type) {
	g_mime_content_type_destroy(headers->content_type);
	headers->content_type = NULL;
    }

    g_list_foreach(headers->cc_list, (GFunc) g_object_unref, NULL);
    g_list_free(headers->cc_list);
    headers->cc_list = NULL;

    g_list_foreach(headers->bcc_list, (GFunc) g_object_unref, NULL);
    g_list_free(headers->bcc_list);
    headers->bcc_list = NULL;
    
    if (headers->reply_to) {
	g_object_unref(headers->reply_to);
	headers->reply_to = NULL;
    }

    if(headers->dispnotify_to) {
	g_object_unref(headers->dispnotify_to);
	headers->dispnotify_to = NULL;
    }

    lb_message_headers_extra_destroy(headers);

    g_free(headers);
}

const gchar *
libbalsa_message_body_charset(LibBalsaMessageBody * body)
{
    const gchar *charset = NULL;

    if (body->charset) /* This overrides all! Important for non
                        * us-ascii messages over IMAP. */
        return body->charset;
    if (body->mime_part) {
	if (GMIME_IS_MULTIPART(body->mime_part))
	    g_mime_multipart_foreach(GMIME_MULTIPART(body->mime_part),
				     (GMimePartFunc)
				     libbalsa_message_find_charset, &charset);
	else
	    libbalsa_message_find_charset(body->mime_part, &charset);
    } else {
	do {
	    if (body->charset) {
		charset = body->charset;
		break;
	    }
	    if (body->parts)
		charset = libbalsa_message_body_charset(body->parts);
	} while (!charset && (body = body->next));
    }

    return charset;
}

/* Note: libbalsa_message_charset returns a pointer to a newly allocated
 * string containing the canonical form of the charset field, or NULL.
 * When the pointer is nonNULL, the string must be deallocated with
 * g_free. */
gchar *
libbalsa_message_charset(LibBalsaMessage * message)
{
    LibBalsaMessageBody *body;
    const gchar *charset;
    const char *tmp;
    g_return_val_if_fail(message != NULL, NULL);
    body = message->body_list;
    g_return_val_if_fail(body != NULL, NULL);

    charset = body->charset;
    if (!charset) {
        charset = libbalsa_message_body_charset(body);
        if (!charset)
            return NULL;
    }
    tmp = g_mime_charset_canon_name(charset);
    return g_strdup(tmp);
}


static void
canonize_header_value(gchar *value)
{
#if 0
    /* there was some unclarity whether we should do it or not. So we
       do not for now. */
    int src, dst;

    /* canonize value: replace multiple spaces, tabs by single space */
    for(src=dst=0; value[src]; src++) {
        if( isspace((int)value[src])) {
            value[dst] = ' ';
            if(!isspace((int)value[src+1]))
                dst++;
        } else
            value[dst++] = value[src];
    }
    value[dst] = '\0';
#endif
}

/* message_user_hdrs:
   returns allocated GList containing (header=>value) ALL headers pairs
   as generated by g_strsplit.
   The list has to be freed by the following chunk of code:
    FREE_HEADER_LIST(list);
*/
gchar **
libbalsa_create_hdr_pair(const gchar * name, gchar * value)
{
    gchar **item = g_new(gchar *, 3);

    canonize_header_value(value);
    item[0] = g_strdup(name);
    item[1] = value;
    item[2] = NULL;
    return item;
}

/** libbalsa_message_find_user_hdr:
    returns.... list element matching given header.
*/
GList *
libbalsa_message_find_user_hdr(LibBalsaMessage * message, const gchar * find)
{
    GList* list;
    gchar** tmp;
    LibBalsaMessageHeaders *headers = message->headers;
    
    g_return_val_if_fail(headers, NULL);
    if (!headers->user_hdrs && message->mailbox) 
        libbalsa_mailbox_set_msg_headers(message->mailbox, message);

    for (list = headers->user_hdrs; list; list = g_list_next(list)) {
        tmp = list->data;
        
        if (g_ascii_strncasecmp(tmp[0], find, strlen(find)) == 0) 
            return list;
    }
    
    return NULL;
}

static void
prepend_header_misc(const char *name, const char *value,
			 gpointer user_data)
{
    char lcname[17]; /* one byte longer than the longest ignored header */
    static const char ignored_headers[] =
        "subject date from to cc bcc "
        "message-id references in-reply-to status lines";
    unsigned i;
    GList *res = *(GList **)user_data;
    if (!*value)
	/* Empty header */
	return;
    /* Standard Headers*/
    for(i=0; name[i] && i<sizeof(lcname)-1; i++)
        lcname[i] = tolower(name[i]);
    if(name[i]) /* too long to be on the ignored-headers list */
        return;
    lcname[i] = '\0';
    if(strstr(ignored_headers, lcname))
        return;

    res = g_list_prepend(res, libbalsa_create_hdr_pair(name, g_strdup(value)));
    *(GList **)user_data = res;
}

/* libbalsa_message_user_hdrs,
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
    value = g_mime_message_get_header(message, "References");
    if (value) {
	GMimeReferences *references, *reference;
	reference = references = g_mime_references_decode(value);
	while (reference) {
	    res =
		g_list_prepend(res,
			       libbalsa_create_hdr_pair("References",
							g_strdup_printf
							("<%s>",
							 reference->
							 msgid)));
	    reference = reference->next;
	}
	g_mime_references_clear(&references);
    }

    value = g_mime_message_get_header(message, "In-Reply-To");
    if (value) {
	res = g_list_prepend(res, libbalsa_create_hdr_pair("In-Reply-To",
				g_mime_utils_header_decode_text(value)));
    }

    g_mime_header_foreach(GMIME_OBJECT(message)->headers,
			  prepend_header_misc, &res);

    return g_list_reverse(res);
}

/* libbalsa_message_get_part_by_id:
   return a message part identified by Content-ID=id
   message must be referenced. (FIXME?)
*/
GMimeStream *
libbalsa_message_get_part_by_id(LibBalsaMessage* msg, const gchar* id)
{
    LibBalsaMessageBody* body = 
	libbalsa_message_body_get_by_id(msg->body_list,	id);
    if(!body) return NULL;
    return libbalsa_message_body_get_stream(body);
}

/* libbalsa_message_save:
   return TRUE on success and FALSE on failure.
*/
gboolean
libbalsa_message_save(LibBalsaMessage * message, const gchar *filename)
{
    FILE *outfile;
    int res;
    GMimeStream *msg_stream;
    GMimeStream *out_stream;

    g_return_val_if_fail(message->mailbox, FALSE);

    if( (outfile = fopen(filename, "w")) == NULL) return FALSE;
    g_return_val_if_fail(outfile, FALSE);

    msg_stream = libbalsa_mailbox_get_message_stream(message->mailbox, message);
    if (msg_stream == NULL)
	return FALSE;
    out_stream = g_mime_stream_file_new(outfile);
    res = g_mime_stream_write_to_stream(msg_stream, out_stream);

    g_object_unref(msg_stream);
    g_object_unref(out_stream);

    return res >= 0;
}

LibBalsaMessageAttach
libbalsa_message_get_attach_icon(LibBalsaMessage * message)
{
#ifdef HAVE_GPGME
    if (libbalsa_message_is_pgp_encrypted(message))
	return LIBBALSA_MESSAGE_ATTACH_ENCR;
    else if (message->prot_state != LIBBALSA_MSG_PROTECT_NONE ||
	libbalsa_message_is_pgp_signed(message)) {
	switch (message->prot_state) {
	case LIBBALSA_MSG_PROTECT_SIGN_GOOD:
	    return LIBBALSA_MESSAGE_ATTACH_GOOD;
	case LIBBALSA_MSG_PROTECT_SIGN_NOTRUST:
	    return LIBBALSA_MESSAGE_ATTACH_NOTRUST;
	case LIBBALSA_MSG_PROTECT_SIGN_BAD:
	    return LIBBALSA_MESSAGE_ATTACH_BAD;
	case LIBBALSA_MSG_PROTECT_CRYPT:
	    return LIBBALSA_MESSAGE_ATTACH_ENCR;
	default:
	    return LIBBALSA_MESSAGE_ATTACH_SIGN;
	}
    } else
#endif
    if (libbalsa_message_has_attachment(message))
	return LIBBALSA_MESSAGE_ATTACH_ATTACH;
    else
	return LIBBALSA_MESSAGE_ATTACH_ICONS_NUM;
}

/* Helper for mailbox drivers. */
gboolean
libbalsa_message_set_msg_flags(LibBalsaMessage * message,
			       LibBalsaMessageFlag set,
			       LibBalsaMessageFlag clear)
{
    gboolean changed = FALSE;
    
    if (!(message->flags & set)) {
	message->flags |= set;
	changed = TRUE;
    }
    if (message->flags & clear) {
	message->flags &= ~clear;
	changed = TRUE;
    }

    return changed;
}

/* Tell the mailbox driver to change flags. */
static void
libbalsa_message_set_flag(LibBalsaMessage * message,
			  LibBalsaMessageFlag set,
			  LibBalsaMessageFlag clear)
{
    GArray *msgnos;

    if (message->mailbox->readonly) {
	libbalsa_information(
	    LIBBALSA_INFORMATION_WARNING,
	    _("Mailbox (%s) is readonly: cannot change flags."),
	    message->mailbox->name);
	return;
    }

    msgnos = g_array_sized_new(FALSE, FALSE, sizeof(guint), 1);
    g_array_append_val(msgnos, message->msgno);
    libbalsa_mailbox_register_msgnos(message->mailbox, msgnos);
    libbalsa_mailbox_messages_change_flags(message->mailbox, msgnos,
					   set, clear);
    libbalsa_mailbox_unregister_msgnos(message->mailbox, msgnos);
    g_array_free(msgnos, TRUE);
}

void
libbalsa_message_reply(LibBalsaMessage * message)
{
    g_return_if_fail(message->mailbox);
    libbalsa_lock_mailbox(message->mailbox);
    libbalsa_message_set_flag(message, LIBBALSA_MESSAGE_FLAG_REPLIED, 0);
    libbalsa_unlock_mailbox(message->mailbox);
}

/* Assume all messages come from the same mailbox */
void
libbalsa_messages_change_flag(GList * messages,
                              LibBalsaMessageFlag flag,
                              gboolean set)
{
    GArray *msgnos;
    LibBalsaMessage * message = NULL;
    LibBalsaMailbox *mbox;
    
    if (!messages)
	return;

    mbox = LIBBALSA_MESSAGE(messages->data)->mailbox;
    if (mbox->readonly) {
	libbalsa_information(
	    LIBBALSA_INFORMATION_WARNING,
	    _("Mailbox (%s) is readonly: cannot change flags."),
	    mbox->name);
	return;
    }

    /* Construct the list of messages that actually change state */
    msgnos = g_array_new(FALSE, FALSE, sizeof(guint));
    for (; messages; messages = messages->next) {
	message = LIBBALSA_MESSAGE(messages->data);
 	if ( (set && !(message->flags & flag)) ||
             (!set && (message->flags & flag)) )
	    g_array_append_val(msgnos, message->msgno);
    }
    libbalsa_mailbox_register_msgnos(message->mailbox, msgnos);
    
    if (msgnos->len > 0) {
	libbalsa_lock_mailbox(mbox);
	/* RETURN_IF_MAILBOX_CLOSED(mbox); */
        /* set flags for entire set in one transaction */
        if(set)
            libbalsa_mailbox_messages_change_flags(mbox, msgnos,
						   flag, 0);
        else
            libbalsa_mailbox_messages_change_flags(mbox, msgnos,
						   0, flag);
	libbalsa_unlock_mailbox(mbox);
    }

    libbalsa_mailbox_unregister_msgnos(message->mailbox, msgnos);
    g_array_free(msgnos, TRUE);
}


/* libbalsa_message_body_ref:
   references the structure of given message possibly fetching also all
   headers. 
   message parts can be fetched later on.
*/
gboolean
libbalsa_message_body_ref(LibBalsaMessage * message, gboolean read,
                          gboolean fetch_all_headers)
{
    LibBalsaFetchFlag flags = 0;
    gboolean retval = TRUE;

    g_return_val_if_fail(message, FALSE);
    if (!message->mailbox) return FALSE;
    g_return_val_if_fail(MAILBOX_OPEN(message->mailbox), FALSE);

    libbalsa_lock_mailbox(message->mailbox);

    if(fetch_all_headers && !message->has_all_headers)
        flags |=  LB_FETCH_RFC822_HEADERS;

    if (message->body_ref == 0 && !message->body_list)
        /* not fetched yet */
        flags |= LB_FETCH_STRUCTURE;

    if (flags)
        retval =
            libbalsa_mailbox_fetch_message_structure(message->mailbox,
                                                     message, flags);
    if (retval)
	message->body_ref++;
    libbalsa_unlock_mailbox(message->mailbox);
    
    return retval;
}


void
libbalsa_message_body_unref(LibBalsaMessage * message)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    if (message->body_ref == 0)
	return;

   if(message->mailbox) { libbalsa_lock_mailbox(message->mailbox); }
   if (--message->body_ref == 0) {
	libbalsa_message_body_free(message->body_list);
	message->body_list = NULL;
	if (message->mailbox)
	    libbalsa_mailbox_release_message(message->mailbox, message);

	/* Free headers that we no longer need. */
	lb_message_headers_extra_destroy(message->headers);
	message->has_all_headers = 0;
   }
   if(message->mailbox) { libbalsa_unlock_mailbox(message->mailbox); }
}

gboolean
libbalsa_message_is_multipart(LibBalsaMessage * message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->headers->content_type ?
	g_mime_content_type_is_type(message->headers->content_type,
				    "multipart", "*") : FALSE;
}

gboolean
libbalsa_message_is_partial(LibBalsaMessage * message, gchar ** id)
{
    const GMimeContentType *content_type;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    content_type = message->headers->content_type;
    if (!content_type
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
    for(lbbody=body; lbbody; lbbody = lbbody->next) {
        /* printf("part %s has disposition %s\n",
               lbbody->content_type, lbbody->content_dsp); */
        if(!libbalsa_message_body_is_multipart(lbbody) &&
           !libbalsa_message_body_is_inline(lbbody) ) {
            /* puts("Attachment found!"); */
            return TRUE;
        }
        if(lbbody->parts && has_attached_part(lbbody->parts))
            return TRUE;
    }
    /* no part was an  attachment */
    return FALSE;
}

gboolean
libbalsa_message_has_attachment(LibBalsaMessage * message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    /* A message has attachments if main message or one of the parts
       has Content-type: multipart/mixed AND members with
       Content-disposition: attachment. Unfortunately, part list may
       not be available at this stage. */
    if(!message->body_list) {
        return message->headers->content_type &&
            g_mime_content_type_is_type(message->headers->content_type,
                                        "multipart", "mixed");
    } else {
        /* use "exact" algorithm */
        return (has_attached_part(message->body_list->next) ||
		has_attached_part(message->body_list->parts));
    }
 }

#ifdef HAVE_GPGME
gboolean
libbalsa_message_is_pgp_signed(LibBalsaMessage * message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->headers->content_type ?
	g_mime_content_type_is_type(message->headers->content_type,
				    "multipart", "signed") : FALSE;
}

gboolean
libbalsa_message_is_pgp_encrypted(LibBalsaMessage * message)
{
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);

    return message->headers->content_type ?
	g_mime_content_type_is_type(message->headers->content_type,
				    "multipart", "encrypted") : FALSE;
}
#endif

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
libbalsa_message_set_dispnotify(LibBalsaMessage *message, 
				LibBalsaAddress *address)
{
    g_return_if_fail(message);
    if(message->headers->dispnotify_to) 
	g_object_unref(message->headers->dispnotify_to);
    message->headers->dispnotify_to = address;
    if(address)
	g_object_ref(message->headers->dispnotify_to);
}

#ifndef MESSAGE_COPY_CONTENT
/* libbalsa_message_get_subject:
   get constant pointer to the subject of the message; 
*/
const gchar *
libbalsa_message_get_subject(LibBalsaMessage* msg)
{
    const gchar *ret;
    if(!msg->subj &&
       msg->mime_msg && msg->mailbox) { /* a message in a mailbox... */
        g_return_val_if_fail(MAILBOX_OPEN(msg->mailbox), NULL);
        ret = g_mime_message_get_subject(msg->mime_msg);
	if (ret)
	    LIBBALSA_MESSAGE_SET_SUBJECT(msg, (gchar*)
				 ret = g_mime_utils_header_decode_text(ret));
    } else
	ret = msg->subj;

    return ret ? ret : _("(No subject)");
}


guint
libbalsa_message_get_lines(LibBalsaMessage* msg)
{
    /* set the line count */
    const char *value;
    if (!msg->mime_msg)
	return 0;
    value = g_mime_message_get_header(msg->mime_msg, "Lines");
    if (!value)
	return 0;
    return atoi(value);
}
glong
libbalsa_message_get_length(LibBalsaMessage* msg)
{
    /* set the length */
    const char *value;
    if (!msg->mime_msg)
	return 0;
    value = g_mime_message_get_header(msg->mime_msg, "Content-Length");
    if (!value)
	return 0;
    return atoi(value);
}

glong
libbalsa_message_get_no(LibBalsaMessage* msg)
{
    return msg->msgno;
}


#endif


static LibBalsaAddress *
libbalsa_address_new_from_gmime(const gchar *addr)
{
    LibBalsaAddress *address;

    if (addr==NULL)
	return NULL;
    address = libbalsa_address_new_from_string(addr);
    return address;
}

/* Populate headers from mime_msg, but only the members that are needed
 * all the time. */
static void
lb_message_headers_basic_from_gmime(LibBalsaMessageHeaders *headers,
				    GMimeMessage *mime_msg)
{
    g_return_if_fail(headers);
    g_return_if_fail(mime_msg != NULL);

    if (!headers->from)
        headers->from = libbalsa_address_new_from_gmime(mime_msg->from);

    if (!headers->date)
	g_mime_message_get_date(mime_msg, &headers->date, NULL);

    if (!headers->to_list) {
	const InternetAddressList *addy, *start;
	start = g_mime_message_get_recipients(mime_msg,
					      GMIME_RECIPIENT_TYPE_TO);
	for (addy = start; addy; addy = addy->next) {
	    LibBalsaAddress *addr = addr = libbalsa_address_new();
	    addr->full_name = g_strdup(addy->address->name);
	    addr->address_list =
		g_list_append(addr->address_list,
			      g_strdup(addy->address->value.addr));
	    if (addr)
		headers->to_list = g_list_prepend(headers->to_list, addr);
	}
	headers->to_list = g_list_reverse(headers->to_list);
    }

    if (!headers->content_type) {
	/* If we could:
	 * headers->content_type =
	 *     g_mime_content_type_copy
	 *         (g_mime_object_get_content_type(mime_msg->mime_part));
	 */
	const GMimeContentType *content_type;
	gchar *str;

	content_type = g_mime_object_get_content_type(mime_msg->mime_part);
	str = g_mime_content_type_to_string(content_type);
	headers->content_type = g_mime_content_type_new_from_string(str);
	g_free(str);
    }
}

/* Populate headers from mime_msg, but only the members not handled in
 * lb_message_headers_basic_from_gmime. */
static void
lb_message_headers_extra_from_gmime(LibBalsaMessageHeaders *headers,
				    GMimeMessage *mime_msg)
{
    g_return_if_fail(headers);
    g_return_if_fail(mime_msg != NULL);

    if (!headers->reply_to)
        headers->reply_to = libbalsa_address_new_from_gmime(mime_msg->reply_to);

    if (!headers->dispnotify_to)
        headers->dispnotify_to = libbalsa_address_new_from_gmime(g_mime_message_get_header(mime_msg, "Disposition-Notification-To"));

    if (!headers->cc_list) {
	const InternetAddressList *addy, *start;
	start = g_mime_message_get_recipients(mime_msg,
					      GMIME_RECIPIENT_TYPE_CC);
	for (addy = start; addy; addy = addy->next) {
	    LibBalsaAddress *addr = addr = libbalsa_address_new();
	    addr->full_name = g_strdup(addy->address->name);
	    addr->address_list =
		g_list_append(addr->address_list,
			      g_strdup(addy->address->value.addr));
            headers->cc_list = g_list_prepend(headers->cc_list, addr);
	}
	headers->cc_list = g_list_reverse(headers->cc_list);
    }

    if (!headers->bcc_list) {
	const InternetAddressList *addy, *start;
	start = g_mime_message_get_recipients(mime_msg,
					      GMIME_RECIPIENT_TYPE_BCC);
	for (addy = start; addy; addy = addy->next) {
	    LibBalsaAddress *addr = addr = libbalsa_address_new();
	    addr->full_name = g_strdup(addy->address->name);
	    addr->address_list =
		g_list_append(addr->address_list,
			      g_strdup(addy->address->value.addr));
	    if (addr)
		headers->bcc_list =
		    g_list_prepend(headers->bcc_list, addr);
	}
	headers->bcc_list = g_list_reverse(headers->bcc_list);
    }

    /* Get fcc from message */
    if (!headers->fcc_url)
	headers->fcc_url =
	    g_strdup(g_mime_message_get_header(mime_msg, "X-Balsa-Fcc"));
}

/* Populate headers from the info in mime_msg. */
void
libbalsa_message_headers_from_gmime(LibBalsaMessageHeaders *headers,
				    GMimeMessage *mime_msg)
{
    lb_message_headers_basic_from_gmime(headers, mime_msg);
    lb_message_headers_extra_from_gmime(headers, mime_msg);
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

#ifdef MESSAGE_COPY_CONTENT
    header = g_mime_message_get_subject(mime_msg);
    if (header) {
	message->subj = g_mime_utils_header_decode_text(header);
        canonize_header_value(message->subj);
	libbalsa_utf8_sanitize(&message->subj, TRUE, NULL);
    }

    header = g_mime_message_get_header(mime_msg, "Content-Length");
    if (header)
	message->length = atoi(header);
#endif
    header = g_mime_message_get_message_id(mime_msg);
    if (header)
	message->message_id = g_strdup(header);

    header = g_mime_message_get_header(mime_msg, "References");
    if (header)
	libbalsa_message_set_references_from_string(message, header);

    header = g_mime_message_get_header(mime_msg, "In-Reply-To");
    if (header)
	libbalsa_message_set_in_reply_to_from_string(message, header);

    lb_message_headers_basic_from_gmime(message->headers, mime_msg);
}

/* Create a newly allocated list of references for threading.
 * This is a deep copy, with its own strings: deallocate with
 * g_free and g_list_free. */
GList *
libbalsa_message_refs_for_threading(LibBalsaMessage * message)
{
    GList *tmp;
    GList *foo;

    g_return_val_if_fail(message != NULL, NULL);

    if (message->in_reply_to && message->in_reply_to->next)
	return NULL;

    tmp = g_list_copy(message->references);

    if (message->in_reply_to) {
	/* some mailers provide in_reply_to but no references, and
	 * some apparently provide both but with the references in
	 * the wrong order; we'll just make sure it's the last item
	 * of this list */
	foo = g_list_find_custom(tmp, message->in_reply_to->data,
				 (GCompareFunc) strcmp);

	if (foo) {
	    tmp = g_list_remove_link(tmp, foo);
	    g_list_free_1(foo);
	}
	tmp = g_list_append(tmp, message->in_reply_to->data);
    }

    for (foo = tmp; foo; foo = foo->next)
	foo->data = g_strdup((gchar *) foo->data);

    return tmp;
}

static GList *
references_decode(const gchar * str)
{
    GMimeReferences *references, *reference;
    GList *list = NULL;

    reference = references = g_mime_references_decode(str);
    while (reference) {
	list = g_list_prepend(list, g_strdup(reference->msgid));
	reference = reference->next;
    }
    g_mime_references_clear(&references);

    return g_list_reverse(list);
}

void
libbalsa_message_set_references_from_string(LibBalsaMessage * message,
					    const gchar *str)
{
 /* Empty references are acceptable but require no action. Similarly,
    if references were set already, there is not reason to set them
    again - they are immutable anyway. */
    if(!message->references && str)
        message->references = references_decode(str);
}

void
libbalsa_message_set_in_reply_to_from_string(LibBalsaMessage * message,
					     const gchar * str)
{
    g_return_if_fail(message->in_reply_to == NULL);

    if (str) {
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
lbmsg_set_header(LibBalsaMessage *message, const gchar *name,
                 const gchar* value, gboolean all)
{
    if (g_ascii_strcasecmp(name, "Subject") == 0) {
	if (!strcmp(value, "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA"))
	    return FALSE;
#if MESSAGE_COPY_CONTENT
	g_free(message->subj);
        message->subj = g_mime_utils_header_decode_text(value);
	libbalsa_utf8_sanitize(&message->subj, TRUE, NULL);
#endif
    } else
    if (g_ascii_strcasecmp(name, "Date") == 0) {
	message->headers->date = g_mime_utils_header_decode_date(value, NULL);
    } else
    if (g_ascii_strcasecmp(name, "From") == 0) {
        message->headers->from = libbalsa_address_new_from_string(value);
    } else
    if (g_ascii_strcasecmp(name, "To") == 0) {
	message->headers->to_list =
	    libbalsa_address_new_list_from_string(value);
    } else
    if (g_ascii_strcasecmp(name, "In-Reply-To") == 0) {
	libbalsa_message_set_in_reply_to_from_string(message, value);
    } else
    if (g_ascii_strcasecmp(name, "Message-ID") == 0) {
	message->message_id = g_mime_utils_decode_message_id(value);
    } else
    if (g_ascii_strcasecmp(name, "References") == 0) {
	libbalsa_message_set_references_from_string(message, value);
    } else
    if (g_ascii_strcasecmp(name, "Content-Type") == 0) {
	message->headers->content_type =
	    g_mime_content_type_new_from_string(value);
    } else
#ifdef MESSAGE_COPY_CONTENT
    if (g_ascii_strcasecmp(name, "Content-Length") == 0) {
	    message->length = atoi(value);
    } else
#endif
    if (all)
	message->headers->user_hdrs =
	    g_list_append(message->headers->user_hdrs,
			  libbalsa_create_hdr_pair(name, g_strdup(value)));

    return TRUE;
}

static gboolean
lb_message_set_headers_from_string(LibBalsaMessage *message,
				   const gchar *lines, gboolean all)
{
    gchar *header, *value;
    const gchar *val, *eoh;
    do {
        for(val = lines; *val && *val >32 && *val<126 && *val != ':'; val++)
            ;
        if(*val != ':') /* parsing error */
            return FALSE;
        for(eoh = val+1; *eoh && (eoh[0] != '\n' || isspace(eoh[1])); eoh++)
            ;
        header = g_strndup(lines, val-lines);
        lines = eoh;
        for(val=val+1; *val && isspace(*val); val++)
            ;                           /* strip spaces at front... */
        while(eoh>val && isspace(*eoh)) eoh--; /* .. and at the end */
        value  = g_strndup(val, eoh-val+1);
        
        lbmsg_set_header(message, header, value, all);
        g_free(header); g_free(value);
        if(!*lines) break;
        lines++;
    } while(1);
    return TRUE;
}

gboolean
libbalsa_message_set_headers_from_string(LibBalsaMessage *message,
                                         const gchar *lines)
{
    return lb_message_set_headers_from_string(message, lines, TRUE);
}

void
libbalsa_message_load_envelope(LibBalsaMessage *message)
{
    GMimeStream *gmime_stream;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;
    char lookahead;
    gboolean ret = FALSE;

    gmime_stream =
	libbalsa_mailbox_get_message_stream(message->mailbox, message);
    if (!gmime_stream)
	return;
    gmime_stream_buffer = g_mime_stream_buffer_new(gmime_stream,
					GMIME_STREAM_BUFFER_BLOCK_READ);
    g_object_unref(gmime_stream);

    line = g_byte_array_new();
    do {
	g_mime_stream_buffer_readln(gmime_stream_buffer, line);
	while (!g_mime_stream_eos(gmime_stream_buffer)
	       && g_mime_stream_read(gmime_stream_buffer, &lookahead, 1) == 1)
	{
		if (lookahead == ' ' || lookahead == '\t') {
		    g_byte_array_append(line, &lookahead, 1);
		    g_mime_stream_buffer_readln(gmime_stream_buffer, line);
		} else
		    break;
	}
	if (line->len == 0 || line->data[line->len-1]!='\n') {
	    /* read error */
	    ret = FALSE;
	    break;
	}
	line->data[line->len-1]='\0'; /* terminate line by overwriting '\n' */
	if (!lb_message_set_headers_from_string(message, line->data, FALSE)) {
	    /* Ignore error return caused by malformed header. */
	}
	if (lookahead == '\n') {/* end of headers */
	    ret = TRUE;
	    break;
	}
	line->len = 0;
	g_byte_array_append(line, &lookahead, 1);
    } while (!g_mime_stream_eos(gmime_stream_buffer));
    if (ret) {
	/* calculate size */
    }
    g_object_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);
}
