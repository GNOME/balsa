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
static GList * libbalsa_message_user_hdrs(LibBalsaMessage * message);

#ifdef DEBUG
static char *mime_content_type2str(int contenttype);
#endif

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

static void
libbalsa_message_init(LibBalsaMessage * message)
{
    message->headers = g_new0(LibBalsaMessageHeaders, 1);
    message->flags = 0;
    message->mailbox = NULL;
    message->remail = NULL;
    message->sender = NULL;
    message->subj = NULL;
    message->references = NULL;
    message->in_reply_to = NULL;
    message->message_id = NULL;
    message->subtype = 0;
    message->parameters = NULL;
    message->body_ref = 0;
    message->body_list = NULL;
    message->references_for_threading = NULL;
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

    g_free(message->remail);
    message->remail = NULL;

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

    g_list_free(message->references_for_threading);
    message->references_for_threading=NULL;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void libbalsa_message_find_charset(GMimeObject *mime_part, gpointer data)
{
	const GMimeContentType *type;
	if (*(gchar **)data)
		return;
	type=g_mime_object_get_content_type(mime_part);
	*(const gchar **)data=g_mime_content_type_get_parameter(type, "charset");
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
    if (headers->reply_to) {
	g_object_unref(headers->reply_to);
	headers->reply_to = NULL;
    }
    if(headers->dispnotify_to) {
	g_object_unref(headers->dispnotify_to);
	headers->dispnotify_to = NULL;
    }

    g_list_foreach(headers->to_list, (GFunc) g_object_unref, NULL);
    g_list_free(headers->to_list);
    headers->to_list = NULL;

    g_list_foreach(headers->cc_list, (GFunc) g_object_unref, NULL);
    g_list_free(headers->cc_list);
    headers->cc_list = NULL;

    g_list_foreach(headers->bcc_list, (GFunc) g_object_unref, NULL);
    g_list_free(headers->bcc_list);
    headers->bcc_list = NULL;

    g_free(headers->fcc_url);
    headers->fcc_url = NULL;

    FREE_HEADER_LIST(headers->user_hdrs);
    headers->user_hdrs = NULL;

    g_free(headers);
}

const gchar *
libbalsa_message_body_charset(LibBalsaMessageBody * body)
{
    gchar *charset = NULL;

    if (GMIME_IS_MULTIPART(body->mime_part))
	g_mime_multipart_foreach(GMIME_MULTIPART(body->mime_part),
			    libbalsa_message_find_charset, &charset);
    else
	libbalsa_message_find_charset(body->mime_part, &charset);
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
    /* FIXME: use function g_mime_charset_canon_name from gmime-2.2 */
    tmp = g_mime_charset_name(charset);
    return g_strdup(tmp);
}

#if NOT_USED
static LibBalsaAddress *
libbalsa_address_new_from_libmutt(ADDRESS * caddr)
{
    LibBalsaAddress *address;
    if (!caddr || (caddr->personal==NULL && caddr->mailbox==NULL))
	return NULL;

    address = libbalsa_address_new();

    /* it will be owned by the caller */

    address->full_name = g_strdup(caddr->personal);
    if (caddr->mailbox)
	address->address_list = g_list_append(address->address_list,
					      g_strdup(caddr->mailbox));

    return address;
}

static GList*
libbalsa_address_list_from_libmutt(ADDRESS *addy)
{
    GList *res = NULL;
    LibBalsaAddress *addr = NULL;
    int in_group = 0;
        
    for (; addy; addy = addy->next) {
        if(in_group) {
            g_return_val_if_fail(addr != NULL, res);
            if(addy->mailbox) {
                addr->address_list = 
                    g_list_append(addr->address_list, 
                                  g_strdup(addy->mailbox));
            } else {
                in_group = 0;
                res = g_list_append(res, addr);
                addr = NULL;
            }
        } else {
            g_return_val_if_fail(addr == NULL,res);
            addr = libbalsa_address_new();
            if(addy->group) {
                in_group = 1;
                addr->full_name = g_strdup(addy->mailbox);
            } else {
                if(addy->personal)
                    addr->full_name = g_strdup(addy->personal);
                addr->address_list =
                    g_list_append(addr->address_list, 
                                  g_strdup(addy->mailbox));
                res = g_list_prepend(res, addr);
                addr = NULL;
            }
        }
    }
    return g_list_reverse(res);
}
#endif

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
    if (!headers->user_hdrs) 
        headers->user_hdrs = libbalsa_message_user_hdrs(message);

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
    GList *res = *(GList **)user_data;
    if (!*value)
	/* Empty header */
	return;
    /* Standard Headers*/
    if (g_ascii_strcasecmp(name, "Subject") == 0)
	return;
    if (g_ascii_strcasecmp(name, "Date") == 0)
	return;
    if (g_ascii_strcasecmp(name, "From") == 0)
	return;
    if (g_ascii_strcasecmp(name, "To") == 0)
	return;
    if (g_ascii_strcasecmp(name, "Cc") == 0)
	return;
    if (g_ascii_strcasecmp(name, "Bcc") == 0)
	return;
    /* Added in libbalsa_message_user_hdrs */
    if (g_ascii_strcasecmp(name, "Return-Path") == 0)
	return;
    if (g_ascii_strcasecmp(name, "Sender") == 0)
	return;
    if (g_ascii_strcasecmp(name, "Mail-Followup-To") == 0)
	return;
    if (g_ascii_strcasecmp(name, "Message-ID") == 0)
	return;
    if (g_ascii_strcasecmp(name, "References") == 0)
	return;
    if (g_ascii_strcasecmp(name, "In-Reply-To") == 0)
	return;
    /* Internal headers */
    if (g_ascii_strcasecmp(name, "Status") == 0)
	return;
    if (g_ascii_strcasecmp(name, "Lines") == 0)
	return;
    res = g_list_prepend(res, libbalsa_create_hdr_pair(name, g_strdup(value)));
    *(GList **)user_data = res;
}

static GList *
prepend_header_if_it_exist(GList *list, GMimeMessage *header, const char *name)
{
    const char * value;
    value = g_mime_message_get_header(header, name);
    if (value)
	list = g_list_prepend(list, libbalsa_create_hdr_pair(name, g_strdup(value)));
    return list;
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

    res = prepend_header_if_it_exist(res, message, "Return-Path");
    res = prepend_header_if_it_exist(res, message, "Sender");
    res = prepend_header_if_it_exist(res, message, "Mail-Followup-To");
#if 0
    res = prepend_header_if_it_exist(res, message, "Message-ID");
#else
    value = g_mime_message_get_message_id(message);
    if (value)
	res = g_list_prepend(res, libbalsa_create_hdr_pair("Message-ID",
					  g_strdup_printf("<%s>", value)));
#endif
    
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
				g_mime_utils_8bit_header_decode(value)));
    }

    g_mime_header_foreach(GMIME_OBJECT(message)->headers,
			  prepend_header_misc, &res);

    return g_list_reverse(res);
}


static GList *
libbalsa_message_user_hdrs(LibBalsaMessage * message)
{
    /* message not attached to an mailbox -> no extra headers */
    if(message->mailbox ==NULL) return NULL;

    return libbalsa_message_user_hdrs_from_gmime(message->mime_msg);
}

/* libbalsa_message_get_part_by_id:
   return a message part identified by Content-ID=id
   message must be referenced. (FIXME?)
*/
FILE*
libbalsa_message_get_part_by_id(LibBalsaMessage* msg, const gchar* id)
{
    LibBalsaMessageBody* body = 
	libbalsa_message_body_get_by_id(msg->body_list,	id);
    if(!body) return NULL;
    if(!libbalsa_message_body_save_temporary(body)) return NULL;
    return fopen(body->temp_filename, "r");
}


gboolean
libbalsa_messages_move (GList* messages, LibBalsaMailbox* dest)
{
    gboolean r = TRUE;
    LibBalsaMessage *message;
    GList *d = NULL;
    GList *p;

    g_return_val_if_fail(messages, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);

    if (LIBBALSA_MESSAGE(messages->data)->mailbox->readonly) {
	libbalsa_information(
	    LIBBALSA_INFORMATION_ERROR,
	    _("Source mailbox (%s) is readonly. Cannot move messages"),
	    LIBBALSA_MESSAGE(messages->data)->mailbox->name);
	return FALSE;
    }
    
    for(p=messages; p; 	p=g_list_next(p)) {
	message=LIBBALSA_MESSAGE(p->data);
	if(message->mailbox==NULL) continue;
	if (libbalsa_mailbox_copy_message(message, dest) != -1)
	    d = g_list_prepend(d, message);
	else
	    r = FALSE;
    }

    if (d)
	    libbalsa_messages_delete (d, TRUE);

    libbalsa_mailbox_check(dest);
    return r;
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

    g_mime_stream_unref(msg_stream);
    g_mime_stream_unref(out_stream);

    return res >= 0;
}

/* libbalsa_messages_copy:
   makes an assumption that all the messages come from the same mailbox.
*/
gboolean
libbalsa_messages_copy (GList * messages, LibBalsaMailbox * dest)
{
    LibBalsaMessage *message;
    GList *p;

    g_return_val_if_fail(messages != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);

    for(p=messages; p; 	p=g_list_next(p)) {
	message=LIBBALSA_MESSAGE(p->data);
	if(message->mailbox==NULL) continue;
	libbalsa_mailbox_copy_message(message, dest);
    }

    libbalsa_mailbox_check(dest);
    return TRUE;
}

static void
libbalsa_message_set_flag(LibBalsaMessage * message, LibBalsaMessageFlag set, LibBalsaMessageFlag clear)
{
    libbalsa_mailbox_change_message_flags(message->mailbox, message->msgno,
					  set, clear);
    message->flags |= set;
    message->flags &= ~clear;
}

void
libbalsa_message_reply(LibBalsaMessage * message)
{
    GList * messages;

    g_return_if_fail(message->mailbox);
    LOCK_MAILBOX(message->mailbox);
    RETURN_IF_MAILBOX_CLOSED(message->mailbox);

    libbalsa_message_set_flag(message, LIBBALSA_MESSAGE_FLAG_REPLIED, 0);

    UNLOCK_MAILBOX(message->mailbox);
    messages = g_list_prepend(NULL, message);
    libbalsa_mailbox_messages_status_changed(message->mailbox, messages,
					     LIBBALSA_MESSAGE_FLAG_REPLIED);
    g_list_free(messages);
}

/* Assume all messages come from the same mailbox */
void
libbalsa_messages_read(GList * messages,
		       gboolean set)
{
    GList * notif_list = NULL;
    LibBalsaMessage * message;

    /* Construct the list of messages that actually change state */
    while (messages) {
	message = LIBBALSA_MESSAGE(messages->data);
	if ( (set && (LIBBALSA_MESSAGE_IS_UNREAD(message))) ||
	     (!set && !(LIBBALSA_MESSAGE_IS_UNREAD(message))) )
	    notif_list = g_list_prepend(notif_list, message);
	messages = g_list_next(messages);
    }
    
    if (notif_list) {
	LibBalsaMailbox * mbox = LIBBALSA_MESSAGE(notif_list->data)->mailbox;
	GList * lst = notif_list;

	LOCK_MAILBOX(mbox);
	RETURN_IF_MAILBOX_CLOSED(mbox);
	while (lst) {
	    message = LIBBALSA_MESSAGE(lst->data);	    
	    if (!set)
		libbalsa_message_set_flag(message, LIBBALSA_MESSAGE_FLAG_NEW, 0);
	    else
		libbalsa_message_set_flag(message, 0, LIBBALSA_MESSAGE_FLAG_NEW);
	    lst = g_list_next(lst);
	}

	UNLOCK_MAILBOX(mbox);
	/* Emission of notification to the owning mailbox */
	libbalsa_mailbox_messages_status_changed(mbox, notif_list,
						 LIBBALSA_MESSAGE_FLAG_NEW);
	g_list_free(notif_list);
    }
}

/* Assume all messages come from the same mailbox */
void
libbalsa_messages_flag(GList * messages, gboolean flag)
{
    GList * notif_list = NULL;
    LibBalsaMessage * message;

    g_return_if_fail(messages != NULL);
    g_return_if_fail( LIBBALSA_MESSAGE(messages->data)->mailbox);

    /* Construct the list of messages that actually change state */
    while (messages) {
	message = LIBBALSA_MESSAGE(messages->data);
	if ( (flag && !(LIBBALSA_MESSAGE_IS_FLAGGED(message))) ||
	     (!flag && (LIBBALSA_MESSAGE_IS_FLAGGED(message))) )
	    notif_list = g_list_prepend(notif_list, message);
	messages = g_list_next(messages);
    }
    
    if (notif_list) {
	LibBalsaMailbox * mbox = LIBBALSA_MESSAGE(notif_list->data)->mailbox;
	GList * lst = notif_list;

	LOCK_MAILBOX(mbox);
	RETURN_IF_MAILBOX_CLOSED(mbox);

	while (lst) {
	    message = LIBBALSA_MESSAGE(lst->data);
	    if (flag)
		libbalsa_message_set_flag(message, LIBBALSA_MESSAGE_FLAG_FLAGGED, 0);
	    else
		libbalsa_message_set_flag(message, 0, LIBBALSA_MESSAGE_FLAG_FLAGGED);
	    lst = g_list_next(lst);
	}

	UNLOCK_MAILBOX(mbox);
	/* Emission of notification to the owning mailbox */
	libbalsa_mailbox_messages_status_changed(mbox, notif_list,
						 LIBBALSA_MESSAGE_FLAG_FLAGGED);    
	g_list_free(notif_list);
    }
}

/* Slightly optimized version for a list of message [un]deletions
   Assume that all messages are in the same mailbox
*/
void
libbalsa_messages_delete(GList * messages, gboolean del)
{
    LibBalsaMessage *message = NULL;
    GList *notif_list = NULL;

    /* Construct the list of messages that actually change state */
    while (messages) {
	message = LIBBALSA_MESSAGE(messages->data);
        if ((del && !(LIBBALSA_MESSAGE_IS_DELETED(message)))
            || (!del && (LIBBALSA_MESSAGE_IS_DELETED(message))))
	    notif_list = g_list_prepend(notif_list, message);
	messages = g_list_next(messages);
    }
    if (notif_list) {
	LibBalsaMailbox *mbox = LIBBALSA_MESSAGE(notif_list->data)->mailbox;
	GList *lst = notif_list;

	LOCK_MAILBOX(mbox);
	RETURN_IF_MAILBOX_CLOSED(mbox);

	do {
	    message = LIBBALSA_MESSAGE(lst->data);
	    g_assert(message->mailbox == mbox);
	    if (del)
		libbalsa_message_set_flag(message, LIBBALSA_MESSAGE_FLAG_DELETED, 0);
	    else
		libbalsa_message_set_flag(message, 0, LIBBALSA_MESSAGE_FLAG_DELETED);
	} while ((lst = g_list_next(lst)) != NULL);
	UNLOCK_MAILBOX(mbox);

	/* Emission of notification to the owning mailbox */
	libbalsa_mailbox_messages_status_changed(mbox, notif_list,
						 LIBBALSA_MESSAGE_FLAG_DELETED);
	g_list_free(notif_list);
    }
}

void
libbalsa_message_clear_recent(LibBalsaMessage * message)
{
    GList * messages;

    g_return_if_fail(message->mailbox);
    RETURN_IF_MAILBOX_CLOSED(message->mailbox);

    libbalsa_message_set_flag(message, 0, LIBBALSA_MESSAGE_FLAG_RECENT);

    messages = g_list_prepend(NULL, message);
    libbalsa_mailbox_messages_status_changed(message->mailbox, messages,
					     LIBBALSA_MESSAGE_FLAG_REPLIED);
    g_list_free(messages);
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
   references the structure of given message.
   message parts can be fetched later on.
*/
gboolean
libbalsa_message_body_ref(LibBalsaMessage * message, gboolean read)
{
#ifdef OLD_CODE
    LibBalsaMessageBody *body;
    GMimeMessage *msg;
#endif

    g_return_val_if_fail(message, FALSE);
    if (!message->mailbox) return FALSE;
    g_return_val_if_fail(MAILBOX_OPEN(message->mailbox), FALSE);


    if (message->body_ref > 0) {
	message->body_ref++;
	UNLOCK_MAILBOX(message->mailbox);
	return TRUE;
    }

#ifndef OLD_CODE
    libbalsa_mailbox_fetch_message_structure(message->mailbox, message,
					     LB_FETCH_RFC822_HEADERS
					     |LB_FETCH_STRUCTURE);
    message->body_ref++;
    UNLOCK_MAILBOX(message->mailbox);
#else /* OLD_CODE */
    /*
     * load message body
     */
    if (message->mime_msg) {
	msg = message->mime_msg;
    } else {
	LibBalsaMessage *m;
	g_warning("%s: this path should never be executed!", __func__);
	m = libbalsa_mailbox_get_message(message->mailbox, message->msgno);
	message->mime_msg = msg = m->mime_msg;
	/* clean up potentialy prefetched headers, use headers from mime_msg */
	libbalsa_message_headers_destroy(message->headers);
	message->headers = g_new0(LibBalsaMessageHeaders, 1);
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
	libbalsa_message_headers_update(message);
    }

    if (msg != NULL) {
	body = libbalsa_message_body_new(message);
	libbalsa_message_body_set_mime_body(body,
					    message->mime_msg->mime_part);
	libbalsa_message_append_part(message, body);

	message->body_ref++;
    }
    UNLOCK_MAILBOX(message->mailbox);
    
    /*
     * emit read message
     */
    if ((LIBBALSA_MESSAGE_IS_UNREAD(message)) && read) {
	GList * messages = g_list_prepend(NULL, message);
	
	libbalsa_messages_read(messages, TRUE);
	g_list_free(messages);
    }
#endif /* OLD_CODE */
    return TRUE;
}


#ifdef HAVE_GPGME
static const gchar *
scan_decrypt_file(LibBalsaMessageBody *body)
{
    for (; body; body = body->next) {
	if (body->decrypt_file)
	    return body->decrypt_file;
	if (body->parts) {
	    const gchar *res;
	    if ((res = scan_decrypt_file(body->parts)))
		return res;
	}
    }
    return NULL;
}
#endif


void
libbalsa_message_body_unref(LibBalsaMessage * message)
{
    g_return_if_fail(LIBBALSA_IS_MESSAGE(message));

    if (message->body_ref == 0)
	return;

   if(message->mailbox) { LOCK_MAILBOX(message->mailbox); }
   if (--message->body_ref == 0) {
#ifdef HAVE_GPGME
       /* find tmp files containing a decrypted body */
       const gchar *decrypt_file = scan_decrypt_file(message->body_list);
       if (decrypt_file) {
	   /* FIXME: maybe we should overwrite the tmp file containing the
	      decrypted message parts to really wipe it? */
	   unlink(decrypt_file);
       }
#endif
	libbalsa_message_body_free(message->body_list);
	message->body_list = NULL;
   }
   if(message->mailbox) { UNLOCK_MAILBOX(message->mailbox); }
}

gboolean
libbalsa_message_is_multipart(LibBalsaMessage * message)
{
    const GMimeContentType *content_type;
    gboolean res;
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    if (message->mime_msg == NULL)
	return message->is_multipart;
    g_return_val_if_fail(message->mime_msg, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
    content_type = g_mime_object_get_content_type(GMIME_OBJECT(message->mime_msg->mime_part));
    res = g_mime_content_type_is_type(content_type, "multipart", "*");
    UNLOCK_MAILBOX(message->mailbox);
    return res;
}

gboolean
libbalsa_message_has_attachment(LibBalsaMessage * message)
{
    const GMimeContentType *content_type;
    gboolean res;

    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    if (message->mime_msg == NULL)
	return message->has_attachment;
    g_return_val_if_fail(message->mime_msg, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);

    /* FIXME: This is wrong, but less so than earlier versions; a message
              has attachments if main message or one of the parts has 
	      Content-type: multipart/mixed AND members with
	      Content-disposition: attachment. Unfortunately, part list may
	      not be available at this stage. */
    content_type = g_mime_object_get_content_type(GMIME_OBJECT(message->mime_msg->mime_part));
    res = g_mime_content_type_is_type(content_type, "multipart", "mixed");
    UNLOCK_MAILBOX(message->mailbox);
    return res;
}

#ifdef HAVE_GPGME
gboolean 
libbalsa_message_is_pgp_signed(LibBalsaMessage * message)
{
    const GMimeContentType *content_type;
    gboolean res;
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    if (message->mime_msg == NULL)
	return message->is_pgp_signed;
    g_return_val_if_fail(message->mime_msg, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
    content_type = g_mime_object_get_content_type(GMIME_OBJECT(message->mime_msg->mime_part));
    res = g_mime_content_type_is_type(content_type, "multipart", "signed");
    UNLOCK_MAILBOX(message->mailbox);
    return res;
}

gboolean 
libbalsa_message_is_pgp_encrypted(LibBalsaMessage * message)
{
    const GMimeContentType *content_type;
    gboolean res;
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    if (message->mime_msg == NULL)
	return message->is_pgp_encrypted;
    g_return_val_if_fail(message->mime_msg, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
    content_type = g_mime_object_get_content_type(GMIME_OBJECT(message->mime_msg->mime_part));
    res = g_mime_content_type_is_type(content_type, "multipart", "encrypted");
    UNLOCK_MAILBOX(message->mailbox);
    return res;
}
#endif

gchar *
libbalsa_message_headers_date_to_gchar(LibBalsaMessageHeaders * headers,
				       const gchar * date_string)
{
    struct tm *footime;
    gchar rettime[128];

    g_return_val_if_fail(headers != NULL, NULL);
    g_return_val_if_fail(date_string != NULL, NULL);

    footime = localtime(&headers->date);

    strftime(rettime, sizeof(rettime), date_string, footime);

    return g_strdup(rettime);
}

gchar *
libbalsa_message_size_to_gchar (LibBalsaMessage * message, gboolean lines)
{
    gchar retsize[32];
    glong length;   /* byte len */
    gint lines_len; /* line len */

    g_return_val_if_fail(message != NULL, NULL);
    length    = LIBBALSA_MESSAGE_GET_LENGTH(message);
    lines_len = LIBBALSA_MESSAGE_GET_LINES(message);
    /* lines is int, length is long */
    if (lines)
        g_snprintf (retsize, sizeof(retsize), "%d", lines_len);
    else {
        if (length <= 32768) {
            g_snprintf (retsize, sizeof(retsize), "%ld", length);
        } else if (length <= (100*1024)) {
            float tmp = (float)length/1024.0;
            g_snprintf (retsize, sizeof(retsize), "%.1fK", tmp);
        } else if (length <= (1024*1024)) {
            g_snprintf (retsize, sizeof(retsize), "%ldK", length/1024);
        } else {
            float tmp = (float)length/(1024.0*1024.0);
            g_snprintf (retsize, sizeof(retsize), "%.1fM", tmp);
        }
    }

    return g_strdup(retsize);
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
				 ret = g_mime_utils_8bit_header_decode(ret));
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


LibBalsaAddress *
libbalsa_address_new_from_gmime(const gchar *addr);

LibBalsaAddress *
libbalsa_address_new_from_gmime(const gchar *addr)
{
    LibBalsaAddress *address;

    if (addr==NULL)
	return NULL;
    address = libbalsa_address_new_from_string(g_strdup(addr));
    return address;
}

void
libbalsa_message_headers_from_gmime(LibBalsaMessageHeaders *headers,
				    GMimeMessage *mime_msg)
{
    g_return_if_fail(headers);
    g_return_if_fail(mime_msg != NULL);

    if (!headers->from)
        headers->from = libbalsa_address_new_from_gmime(mime_msg->from);

    if (!headers->reply_to)
        headers->reply_to = libbalsa_address_new_from_gmime(mime_msg->reply_to);

    if (!headers->dispnotify_to)
        headers->dispnotify_to = libbalsa_address_new_from_gmime(g_mime_message_get_header(mime_msg, "Disposition-Notification-To"));

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
	    if (addr)
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

    if (!headers->user_hdrs)
	headers->user_hdrs = libbalsa_message_user_hdrs_from_gmime(mime_msg);

    /* Get fcc from message */
    if (!headers->fcc_url)
        headers->fcc_url = g_strdup(g_mime_message_get_header(mime_msg, "X-Mutt-Fcc"));
}

/* libbalsa_message_headers_update:
 * set up the various message-> headers from the info in
 * message->header->env
 *
 * called when translate_message (libbalsa/mailbox.c) creates the
 * message in the first place, and again when libbalsa_message_body_ref
 * grabs the message body, in case more headers have been downloaded
 */
void
libbalsa_message_headers_update(LibBalsaMessage * message)
{
    int offset;

    g_return_if_fail(message != NULL);
    g_return_if_fail(message->headers != NULL);

    if (message->mime_msg) {
	libbalsa_message_headers_from_gmime(message->headers,
					    message->mime_msg);

	g_mime_message_get_date(message->mime_msg, &message->headers->date,
				&offset);
	if (!message->sender)
	    message->sender =
		libbalsa_address_new_from_gmime(g_mime_message_get_sender
						(message->mime_msg));

	if (!message->in_reply_to) {
	    const gchar *header =
		g_mime_message_get_header(message->mime_msg,
					  "In-Reply-To");
	    libbalsa_message_set_in_reply_to_from_string(message, header);
	}
#ifdef MESSAGE_COPY_CONTENT
	if (!message->subj) {
	    const char *subj;
	    subj = g_mime_message_get_subject(message->mime_msg);
	    message->subj = g_mime_utils_8bit_header_decode(subj);
	}
#endif
	if (!message->message_id) {
	    const char *value =
		g_mime_message_get_message_id(message->mime_msg);
	    if (value)
		message->message_id = g_strdup(value);
	}

	if (!message->references) {
	    const char *value =
		g_mime_message_get_header(message->mime_msg, "References");
	    if (value) {
		libbalsa_message_set_references_from_string(message,
							    value);
	    }
	}
    }	/* if (message->mime_msg) */

    /* more! */
    if (!message->references_for_threading
	&& (!message->in_reply_to || !message->in_reply_to->next)) {
        GList *tmp = g_list_copy(message->references);

        if (message->in_reply_to) {
            /* some mailers provide in_reply_to but no references, and
             * some apparently provide both but with the references in
             * the wrong order; we'll just make sure it's the first item
             * of this list (which will be the last after reversing it,
             * below) */
            GList *foo =
                g_list_find_custom(tmp, message->in_reply_to->data,
                                   (GCompareFunc) strcmp);
                
            if (foo) {
                tmp = g_list_remove_link(tmp, foo);
                g_list_free_1(foo);
            }
            tmp = g_list_prepend(tmp, message->in_reply_to->data);
        }

        message->references_for_threading = g_list_reverse(tmp);
    }

    if (message->mailbox) {
	message->has_attachment = libbalsa_message_has_attachment(message);
	message->is_multipart = libbalsa_message_is_multipart(message);
#ifdef HAVE_GPGME
	message->is_pgp_encrypted = libbalsa_message_is_pgp_encrypted(message);
	message->is_pgp_signed = libbalsa_message_is_pgp_signed(message);
#endif
    }
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
    g_return_if_fail(message->references == NULL);
    g_return_if_fail(str != NULL);

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

/* libbalsa_message_title:
 * create a title (for a message window, for instance)
 *
 * Arguments
 *   message    the message
 *   format     the format string
 *
 * Value
 *   pointer to a newly allocated string containing the title
 *
 * the title consists of the format string, with conversions specified
 * in one of the forms
 *   %c
 *   %wc
 *   %w.dc
 * where:
 *   c specifies the string to be inserted; current choices are:
 *     F        `From' header;
 *     f        `From' mailbox;
 *     s        subject;
 *     %        literal '%' character
 *   w specifies the maximum field width; 
 *   d specifies a number trailing dots to indicate truncation.
 */
gchar *
libbalsa_message_title(LibBalsaMessage * message, const gchar * format)
{
    GString *string = g_string_new("");
    gchar *tmp;
    gchar *tmp1;

    while ((tmp = strchr(format, '%')) != NULL) {
        gint c;
        gint length = 0;
        gint dots = 0;

        while (format < tmp)
            g_string_append_c(string, *format++);

        while (isdigit(c = *++format))
            length = 10 * length + (c - '0');

        if (c == '.')
            while (isdigit(c = *++format))
                dots = 10 * dots + (c - '0');

        switch (c) {
        case 'f':
            tmp = g_strdup(message->headers->from ?
                           libbalsa_address_get_mailbox(message->headers->from, 0)
                           : "");
            break;
        case 'F':
            tmp = message->headers->from ?
                  libbalsa_address_to_gchar(message->headers->from, 0) : g_strdup("");
            break;
        case 's':
            tmp = g_strdup(LIBBALSA_MESSAGE_GET_SUBJECT(message));
            break;
        case '%':
            tmp = g_strdup("%");
            break;
        default:
            tmp = g_strdup("???");
            break;
        }

        tmp1 = libbalsa_truncate_string(tmp, length, dots);
        g_free(tmp);
        if (tmp1) {
            g_string_append(string, tmp1);
            g_free(tmp1);
        }

        if (c)
            ++format;
    }

    if (*format)
        g_string_append(string, format);

    tmp = string->str;
    g_string_free(string, FALSE);
    return tmp;
}

gboolean
libbalsa_message_set_header_from_string(LibBalsaMessage *message, gchar *line)
{
    gchar *name, *value;

    value = strchr(line, ':');
    if (!value) {
	g_warning("Bad header line: %s", line);
	return FALSE;
    }

    *value++ = '\0';
    name = g_strstrip(line);
    value = g_strstrip(value);
    if (g_ascii_strcasecmp(name, "Subject") == 0) {
	if (!strcmp(value, "DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA"))
	    return FALSE;
        message->subj = g_mime_utils_8bit_header_decode(value);
    } else
    if (g_ascii_strcasecmp(name, "Date") == 0) {
	message->headers->date = g_mime_utils_header_decode_date(value, NULL);
    } else
    if (g_ascii_strcasecmp(name, "From") == 0) {
        message->headers->from = libbalsa_address_new_from_string(value);
    } else
    if (g_ascii_strcasecmp(name, "Reply-To") == 0) {
        message->headers->reply_to = libbalsa_address_new_from_string(value);
    } else
    if (g_ascii_strcasecmp(name, "To") == 0) {
	message->headers->to_list =
	    libbalsa_address_new_list_from_string(value);
    } else
    if (g_ascii_strcasecmp(name, "Cc") == 0) {
	message->headers->cc_list =
	    libbalsa_address_new_list_from_string(value);
    } else
    if (g_ascii_strcasecmp(name, "Bcc") == 0) {
	message->headers->bcc_list =
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
	/* check sign/encrypt/attachment */
	GMimeContentType* content_type =
	    g_mime_content_type_new_from_string(value);

	message->is_multipart =
	    g_mime_content_type_is_type(content_type, "multipart", "*");
	message->has_attachment =
	    g_mime_content_type_is_type(content_type, "multipart", "mixed");
#ifdef HAVE_GPGME
	message->is_pgp_encrypted =
	    g_mime_content_type_is_type(content_type, "multipart", "encrypted");
	message->is_pgp_signed =
	    g_mime_content_type_is_type(content_type, "multipart", "signed");
#endif
	g_mime_content_type_destroy(content_type);
    } else


#ifdef MESSAGE_COPY_CONTENT
    if (g_ascii_strcasecmp(name, "Content-Length") == 0) {
	    message->length = atoi(value);
    } else

    if (g_ascii_strcasecmp(name, "Lines") == 0) {
	    message->lines_len = atoi(value);
    } else
#endif
    /* do nothing */;
    return TRUE;
}

gboolean
libbalsa_message_load_envelope_from_file(LibBalsaMessage *message,
					 const char *filename)
{
    int fd;
    GMimeStream *gmime_stream;
    GMimeStream *gmime_stream_buffer;
    GByteArray *line;
    char lookahead;
    gboolean ret = FALSE;

    fd = open(filename, O_RDONLY);
    gmime_stream = g_mime_stream_fs_new(fd);
    gmime_stream_buffer = g_mime_stream_buffer_new(gmime_stream,
					GMIME_STREAM_BUFFER_BLOCK_READ);
    g_mime_stream_unref(gmime_stream);
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
	if (libbalsa_message_set_header_from_string(message,
						    line->data) == FALSE) {
	    ret = FALSE;
	    break;
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
    g_mime_stream_unref(gmime_stream_buffer);
    g_byte_array_free(line, TRUE);
    return ret;
}

