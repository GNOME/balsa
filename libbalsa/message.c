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

#include <glib.h>
#include <libgnome/gnome-i18n.h> 

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "mailbackend.h"
/* needed for truncate_string */
#include "misc.h"

static void libbalsa_message_class_init(LibBalsaMessageClass * klass);
static void libbalsa_message_init(LibBalsaMessage * message);

static void libbalsa_message_finalize(GObject * object);
static GList * libbalsa_message_user_hdrs(LibBalsaMessage * message);

static gchar *ADDRESS_to_gchar(const ADDRESS * addr);

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
    message->header = NULL;
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
#ifdef HAVE_GPGME
    message->prot_state = LIBBALSA_MSG_PROTECT_NONE;
#endif
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

    g_free(message->in_reply_to);
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
libbalsa_message_pathname(LibBalsaMessage * message)
{
    g_return_val_if_fail(CLIENT_CONTEXT_OPEN(message->mailbox), NULL);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox)->hdrs, NULL);
    g_return_val_if_fail(message->header, NULL);
    return message->header->path;
}

const gchar *
libbalsa_message_body_charset(LibBalsaMessageBody * body)
{
    gchar *charset = NULL;

    while (body) {
	if(body->mutt_body) {
            libbalsa_lock_mutt();
            charset = 
                mutt_get_parameter("charset", body->mutt_body->parameter);
            libbalsa_unlock_mutt();
        } else charset = body->charset;

	if (charset)
	    break;
	
	if (body->parts)
	    charset = (gchar *)libbalsa_message_body_charset(body->parts);

	if (charset)
	    break;

	body = body->next;
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
    char tmp[SHORT_STRING];    /* SHORT_STRING = 128 */

    g_return_val_if_fail(message != NULL, NULL);
    body = message->body_list;
    g_return_val_if_fail(body != NULL, NULL);

    charset = body->charset;
    if (!charset) {
        charset = libbalsa_message_body_charset(body);
        if (!charset)
            return NULL;
    }
    mutt_canonical_charset(tmp, sizeof tmp, charset);
    return g_strdup(tmp);
}

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

/* libbalsa_message_user_hdrs,
 * libbalsa_message_user_hdrs_from_mutt:
 * 
 * returns allocated GList containing (header=>value) ALL headers
 * pairs as generated by g_strsplit. The list has to be freed by the
 * following chunk of code (or something functionally similar):
 * 
 * g_list_foreach(list, (GFunc) g_strfreev, NULL);
 * g_list_free(list);
*/


GList *
libbalsa_message_user_hdrs_from_mutt(MuttHeader* header)
{
    LIST *tmp;
    GList *res = NULL;
    gchar **pair;
    ENVELOPE *env;

    g_assert(header != NULL);
    g_assert(header->env != NULL);
    env = header->env;
    
    if (env->return_path)
	res =
	    g_list_append(res,
			  libbalsa_create_hdr_pair("Return-Path",
					  ADDRESS_to_gchar(env->return_path)));

    if (env->sender)
	res =
	    g_list_append(res,
			  libbalsa_create_hdr_pair("Sender",
						   ADDRESS_to_gchar(env->sender)));

    if (env->mail_followup_to)
	res =
	    g_list_append(res,
			  libbalsa_create_hdr_pair("Mail-Followup-To",
						   ADDRESS_to_gchar(env->
							   mail_followup_to)));

    if (env->message_id)
	res =
	    g_list_append(res,
			  libbalsa_create_hdr_pair("Message-ID",
						  g_strdup(env->message_id)));
    
    for (tmp = env->references; tmp; tmp = tmp->next) {
	res =
	    g_list_append(res,
			  libbalsa_create_hdr_pair("References",
						   g_strdup(tmp->data)));
    }

    for (tmp = env->in_reply_to; tmp; tmp = tmp->next) {
        res = g_list_append(res, libbalsa_create_hdr_pair("In-Reply-To",
                                                          g_strdup(tmp->data)));
    }

    for (tmp = env->userhdrs; tmp; tmp = tmp->next) {
	pair = g_strsplit(tmp->data, ":", 2);
	g_strchug(pair[1]);
	res = g_list_append(res, pair);
    }

    return res;
}


static GList *
libbalsa_message_user_hdrs(LibBalsaMessage * message)
{
    /* message not attached to an mailbox -> no extra headers */
    if(message->mailbox ==NULL) return NULL;

    if(CLIENT_CONTEXT(message->mailbox)->hdrs == NULL) 
	/* oops, mutt closed the mailbox on error, we should do the same */
	return NULL;

    return libbalsa_message_user_hdrs_from_mutt(message->header);
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
    if(!libbalsa_message_body_save_temporary(body, NULL)) return NULL;
    return fopen(body->temp_filename, "r");
}


gboolean
libbalsa_messages_move (GList* messages, LibBalsaMailbox* dest)
{
    gboolean r = TRUE;
    HEADER *cur;
    LibBalsaMessage *message;
    GList *d = NULL;
    GList *p;
    LibBalsaMailboxAppendHandle* handle;

    g_return_val_if_fail(messages, FALSE);

    if (LIBBALSA_MESSAGE(messages->data)->mailbox->readonly) {
	libbalsa_information(
	    LIBBALSA_INFORMATION_ERROR,
	    _("Source mailbox (%s) is readonly. Cannot move messages"),
	    LIBBALSA_MESSAGE(messages->data)->mailbox->name);
	return FALSE;
    }
    
    handle = libbalsa_mailbox_open_append(dest);
    if (!handle) {
	libbalsa_information(
			     LIBBALSA_INFORMATION_ERROR,
			     _("Couldn't open destination mailbox (%s) for writing"),
			     dest->name);
	return FALSE;
    }
    
    libbalsa_lock_mutt();
    for(p=messages; p; 	p=g_list_next(p)) {
	message=(LibBalsaMessage *)(p->data);
	if(CLIENT_CONTEXT_CLOSED(message->mailbox)) continue;
	if(!CLIENT_CONTEXT(message->mailbox)->hdrs) {
            printf("Ugly libmutt error, why did it fastclose mailbox!?");
            break;
        }
	cur = message->header;
	mutt_parse_mime_message(CLIENT_CONTEXT(message->mailbox), cur); 
	if(!mutt_append_message(handle->context,
				CLIENT_CONTEXT(message->mailbox), cur, 
				0,0))
	    d = g_list_prepend(d, message);
	else
	    r = FALSE;
    }
    libbalsa_unlock_mutt();

    /* FIXME : once more is order relevant here ? */
    d = g_list_reverse(d);
    libbalsa_messages_delete(d, TRUE);
    g_list_free(d);

    libbalsa_mailbox_close_append(handle);
    libbalsa_mailbox_check(dest);
    return r;
}

/* FIXME: look at the flags for mutt_append_message */
gboolean
libbalsa_message_copy(LibBalsaMessage * message, LibBalsaMailbox * dest)
{
    gboolean r = TRUE;
    HEADER *cur;
    LibBalsaMailboxAppendHandle* handle;
    g_return_val_if_fail(message->mailbox, FALSE);
    RETURN_VAL_IF_CONTEXT_CLOSED(message->mailbox, FALSE);

    cur = message->header;

    handle = libbalsa_mailbox_open_append(dest);

    if (!handle) {
	libbalsa_information(
	    LIBBALSA_INFORMATION_WARNING,
	    _("Couldn't open destination mailbox (%s) for copying"),
	    dest->name);
	return FALSE;
    }

    libbalsa_lock_mutt();
    mutt_parse_mime_message(CLIENT_CONTEXT(message->mailbox), cur);
    if(mutt_append_message(handle->context,
			   CLIENT_CONTEXT(message->mailbox), cur,
			   0,0))
	r = FALSE;
    libbalsa_unlock_mutt();

    libbalsa_mailbox_close_append(handle);
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

    g_return_val_if_fail(message->mailbox, FALSE);
    RETURN_VAL_IF_CONTEXT_CLOSED(message->mailbox, FALSE);

    if( (outfile = fopen(filename, "w")) == NULL) return FALSE;
    g_return_val_if_fail(outfile, FALSE);

    libbalsa_lock_mutt();
    res = mutt_copy_message(outfile, CLIENT_CONTEXT(message->mailbox), 
			    message->header, 0, 0);
    libbalsa_unlock_mutt();

    fclose(outfile);
    return res != -1;
}

/* libbalsa_messages_copy:
   makes an assumption that all the messages come from the same mailbox.
*/
gboolean
libbalsa_messages_copy (GList * messages, LibBalsaMailbox * dest)
{
    gboolean r=TRUE;
    HEADER *cur;
    LibBalsaMessage *message;
    GList *p;
    LibBalsaMailboxAppendHandle* handle;

    g_return_val_if_fail(messages != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);

    handle = libbalsa_mailbox_open_append(dest);
    if (!handle) {
	libbalsa_information(
	    LIBBALSA_INFORMATION_ERROR,
	    _("Couldn't open destination mailbox (%s) for writing"),
	    dest->name);
	return FALSE;
    }

    libbalsa_lock_mutt();
    for(p=messages; p; 	p=g_list_next(p)) {
	message=(LibBalsaMessage *)(p->data);
	if(CLIENT_CONTEXT_CLOSED(message->mailbox)) continue;
	if(!CLIENT_CONTEXT(message->mailbox)->hdrs) {
            printf("Ugly libmutt error, why did it fastclose mailbox!?");
            break;
        }
	cur = message->header;
	mutt_parse_mime_message(CLIENT_CONTEXT(message->mailbox), cur); 
	if(mutt_append_message(handle->context,
			       CLIENT_CONTEXT(message->mailbox), cur, 
			       0, 0))
	    r = FALSE;
    }
    libbalsa_unlock_mutt();

    libbalsa_mailbox_close_append(handle);
    libbalsa_mailbox_check(dest);
    return r;
}

void
libbalsa_message_reply(LibBalsaMessage * message)
{
    HEADER *cur;
    GList * messages;

    g_return_if_fail(message->mailbox);
    LOCK_MAILBOX(message->mailbox);
    RETURN_IF_CLIENT_CONTEXT_CLOSED(message->mailbox);

    cur = message->header;

    libbalsa_lock_mutt();
    mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_REPLIED, TRUE);
    libbalsa_unlock_mutt();

    message->flags |= LIBBALSA_MESSAGE_FLAG_REPLIED;

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
	if ( (set && (message->flags & LIBBALSA_MESSAGE_FLAG_NEW)) ||
	     (!set && !(message->flags & LIBBALSA_MESSAGE_FLAG_NEW)) )
	    notif_list = g_list_prepend(notif_list, message);
	messages = g_list_next(messages);
    }
    
    if (notif_list) {
	HEADER * cur;
	LibBalsaMailbox * mbox = LIBBALSA_MESSAGE(notif_list->data)->mailbox;
	GList * lst = notif_list;

	LOCK_MAILBOX(mbox);
	RETURN_IF_CLIENT_CONTEXT_CLOSED(mbox);
	libbalsa_lock_mutt();

	while (lst) {
	    message = LIBBALSA_MESSAGE(lst->data);	    
	    cur = message->header;
	    mutt_set_flag(CLIENT_CONTEXT(mbox), cur, M_READ, set);
	    mutt_set_flag(CLIENT_CONTEXT(mbox), cur, M_OLD, FALSE);

	    if (set)
		message->flags &= ~LIBBALSA_MESSAGE_FLAG_NEW;
	    else
		message->flags |= LIBBALSA_MESSAGE_FLAG_NEW;
	    lst = g_list_next(lst);
	}
	libbalsa_unlock_mutt();

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
	if ( (flag && !(message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)) ||
	     (!flag && (message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)) )
	    notif_list = g_list_prepend(notif_list, message);
	messages = g_list_next(messages);
    }
    
    if (notif_list) {
	HEADER * cur;
	LibBalsaMailbox * mbox = LIBBALSA_MESSAGE(notif_list->data)->mailbox;
	GList * lst = notif_list;

	LOCK_MAILBOX(mbox);
	RETURN_IF_CLIENT_CONTEXT_CLOSED(mbox);
	libbalsa_lock_mutt();

	while (lst) {
	    message = LIBBALSA_MESSAGE(lst->data);
	    cur = message->header;
	    mutt_set_flag(CLIENT_CONTEXT(mbox), cur, M_FLAG, flag);
	    if (!flag)
		message->flags &= ~LIBBALSA_MESSAGE_FLAG_FLAGGED;
	    else
		message->flags |= LIBBALSA_MESSAGE_FLAG_FLAGGED;
	    lst = g_list_next(lst);
	}
	libbalsa_unlock_mutt();

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
        if ((del && !(message->flags & LIBBALSA_MESSAGE_FLAG_DELETED))
            || (!del && (message->flags & LIBBALSA_MESSAGE_FLAG_DELETED)))
            notif_list = g_list_prepend(notif_list, message);
        messages = g_list_next(messages);
    }
    if (notif_list) {
        HEADER *cur;
        LibBalsaMailbox *mbox =
            LIBBALSA_MESSAGE(notif_list->data)->mailbox;
        GList *lst = notif_list;

        LOCK_MAILBOX(mbox);
        RETURN_IF_CLIENT_CONTEXT_CLOSED(mbox);
        libbalsa_lock_mutt();

        do {
            message = LIBBALSA_MESSAGE(lst->data);
            g_assert(message->mailbox == mbox);
            cur = message->header;
            mutt_set_flag(CLIENT_CONTEXT(mbox), cur, M_DELETE, del);
            if (del)
                message->flags |= LIBBALSA_MESSAGE_FLAG_DELETED;
            else
                message->flags &= ~LIBBALSA_MESSAGE_FLAG_DELETED;
        } while ((lst = g_list_next(lst)) != NULL);
        libbalsa_unlock_mutt();
        UNLOCK_MAILBOX(mbox);

        /* Emission of notification to the owning mailbox */
        libbalsa_mailbox_messages_status_changed(mbox, notif_list,
                                                 LIBBALSA_MESSAGE_FLAG_DELETED);
        g_list_free(notif_list);
    }
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
   returns TRUE for success, FALSE for failure (broken IMAP connection etc).
*/
gboolean
libbalsa_message_body_ref(LibBalsaMessage * message, gboolean read)
{
    LibBalsaMessageBody *body;
    HEADER *cur;
    MESSAGE *msg;

    g_return_val_if_fail(message, FALSE);
    if (!message->mailbox) return FALSE;

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE); 
    if(CLIENT_CONTEXT(message->mailbox)->hdrs==NULL ||
       (cur = message->header) == NULL) {
	g_warning("Context damaged.");
	UNLOCK_MAILBOX(message->mailbox); 
        return FALSE;
    }


    if (message->body_ref > 0) {
	message->body_ref++;
	UNLOCK_MAILBOX(message->mailbox);
	return TRUE;
    }

    msg = (MESSAGE *)g_malloc (sizeof (MESSAGE));
    msg->fp = libbalsa_mailbox_get_message_stream(message->mailbox, message);
    if(! message->mailbox->disconnected ) {
	if (!msg->fp) { /*FIXME: crude but necessary error handling */
	    message->mailbox->disconnected = TRUE;
	    message->mailbox->readonly = TRUE;
	    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
				 "disconnected mode!");
	    UNLOCK_MAILBOX(message->mailbox);
	    if(CLIENT_CONTEXT_CLOSED(message->mailbox) ||
	       CLIENT_CONTEXT(message->mailbox)->hdrs == NULL) 
		libbalsa_mailbox_close(message->mailbox);
	    return FALSE;
	} 
	/* mx_open_message may have downloaded more headers (IMAP): */
	libbalsa_message_headers_update(message);

	fseek(msg->fp, cur->content->offset, 0);
    } else { /* disconnected mode */
	if(!msg->fp) {
	    g_free(msg);
	    return FALSE;
	}
	rewind(msg->fp);
	/* ugly ugly ugly */
	cur->env = mutt_read_rfc822_header (msg->fp, cur, 1, 0); 
	rewind(msg->fp);
    }
    if (cur->content->type == TYPEMULTIPART) {
	libbalsa_lock_mutt();
        cur->content->parts =
            mutt_parse_multipart(msg->fp,
                                 mutt_get_parameter("boundary",
                                                    cur->content->
                                                    parameter),
                                 cur->content->offset +
                                 cur->content->length,
                                 g_ascii_strcasecmp("digest",
                                                    cur->content->
                                                    subtype) == 0);
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

	fclose(msg->fp);
	g_free(msg);
    }
    UNLOCK_MAILBOX(message->mailbox);
    
    /*
     * emit read message
     */
    if ((message->flags & LIBBALSA_MESSAGE_FLAG_NEW) && read) {
	GList * messages = g_list_prepend(NULL, message);
	
	libbalsa_messages_read(messages, TRUE);
	g_list_free(messages);
    }
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
    HEADER *msg_header;
    gboolean res;
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox), FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox)->hdrs, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
    msg_header = message->header;	
    res= msg_header->content->type == TYPEMULTIPART;
    UNLOCK_MAILBOX(message->mailbox);
    return res;
}

gboolean
libbalsa_message_has_attachment(LibBalsaMessage * message)
{
    HEADER *msg_header;
    gboolean res;

    g_return_val_if_fail(message, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox), FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox)->hdrs, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
    msg_header = message->header;

    /* FIXME: This is wrong, but less so than earlier versions; a message
              has attachments if main message or one of the parts has 
	      Content-type: multipart/mixed AND members with
	      Content-disposition: attachment. Unfortunately, part list may
	      not be available at this stage. */
    res = (msg_header->content->type==TYPEMULTIPART &&
	    g_ascii_strcasecmp("mixed", msg_header->content->subtype)==0);

    UNLOCK_MAILBOX(message->mailbox);
    return res;
}

#ifdef HAVE_GPGME
gboolean 
libbalsa_message_is_pgp_signed(LibBalsaMessage * message)
{
    HEADER *msg_header;
    gboolean res;

    g_return_val_if_fail(message, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox), FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox)->hdrs, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
    msg_header = message->header;
    res = (msg_header->content->type == TYPEMULTIPART &&
	   g_ascii_strcasecmp("signed", msg_header->content->subtype) == 0);

    UNLOCK_MAILBOX(message->mailbox);
    return res;
}

gboolean 
libbalsa_message_is_pgp_encrypted(LibBalsaMessage * message)
{
    HEADER *msg_header;
    gboolean res;

    g_return_val_if_fail(message, FALSE);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), FALSE);
    g_return_val_if_fail(message->mailbox, FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox), FALSE);
    g_return_val_if_fail(CLIENT_CONTEXT(message->mailbox)->hdrs, FALSE);

    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
    msg_header = message->header;
    res = (msg_header->content->type == TYPEMULTIPART &&
	   g_ascii_strcasecmp("encrypted", msg_header->content->subtype) == 0);

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

    if(msg->header && msg->mailbox) { /* a message in a mailbox... */
        g_return_val_if_fail(CLIENT_CONTEXT_OPEN(msg->mailbox), NULL);
	/* g_print("Returning libmutt's pointer\n"); */
	ret = msg->header->env->subject;
    } else
	ret = msg->subj;

    return ret ? ret : _("(No subject)");
}


guint
libbalsa_message_get_lines(LibBalsaMessage* msg)
{
    return msg->header->lines;
}
glong
libbalsa_message_get_length(LibBalsaMessage* msg)
{
    return msg->header->content->length;
}

glong
libbalsa_message_get_no(LibBalsaMessage* msg)
{
    return msg->header->msgno;
}


#endif

void
libbalsa_message_headers_from_mutt(LibBalsaMessageHeaders *headers,
				   MuttHeader *mheader)
{
    ENVELOPE *cenv;
    LIST *tmp;

    g_return_if_fail(headers);
    
    if (!(cenv = mheader->env))
	return;

    if (!headers->from)
        headers->from =
            libbalsa_address_new_from_libmutt(cenv->from);
    if (!headers->reply_to)
        headers->reply_to =
            libbalsa_address_new_from_libmutt(cenv->reply_to);
    if (!headers->dispnotify_to)
        headers->dispnotify_to =
            libbalsa_address_new_from_libmutt(cenv->dispnotify_to);

    if (!headers->to_list)
        headers->to_list = libbalsa_address_list_from_libmutt(cenv->to);

    if (!headers->cc_list)
        headers->cc_list = libbalsa_address_list_from_libmutt(cenv->cc);

    if (!headers->bcc_list)
        headers->bcc_list = libbalsa_address_list_from_libmutt(cenv->bcc);

    if (!headers->user_hdrs)
        headers->user_hdrs = libbalsa_message_user_hdrs_from_mutt(mheader);

    /* Get fcc from message */
    for (tmp = cenv->userhdrs; tmp; tmp = tmp->next) {
        if (!headers->fcc_url
            && g_ascii_strncasecmp("X-Mutt-Fcc:", tmp->data, 11) == 0) {
            gchar *p = tmp->data + 11;
            SKIPWS(p);

            if (p)
                headers->fcc_url = g_strdup(p);
#if 0                           /* this looks bogus! */
        } else if (g_strncasecmp("X-Mutt-Fcc:", tmp->data, 18) == 0) {
            /* Is X-Mutt-Fcc correct? */
            p = tmp->data + 18;
            SKIPWS(p);

            headers->in_reply_to = g_strdup(p);
#endif
        }
    }
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
    HEADER *cur;
    ENVELOPE *cenv;
    LIST *tmp;

    if (!message || !(cur = message->header) || !(cenv = cur->env))
        return;

    libbalsa_message_headers_from_mutt(message->headers, cur);

    message->headers->date = cur->date_sent;
    if (!message->sender)
        message->sender =
            libbalsa_address_new_from_libmutt(cenv->sender);

    if (!message->in_reply_to && cenv->in_reply_to) {
        gchar* p = g_strdup(cenv->in_reply_to->data);
        
        for (tmp = cenv->in_reply_to->next; tmp; tmp = tmp->next) {
            message->in_reply_to = g_strconcat(p, tmp->data, NULL);
            g_free(p);
            p = message->in_reply_to;
        }
        
        message->in_reply_to = p;
    }

#ifdef MESSAGE_COPY_CONTENT
    if (!message->subj)
        message->subj = g_strdup(cenv->subject);
#endif
    if (!message->message_id)
        message->message_id = g_strdup(cenv->message_id);

    if (!message->references) {
        for (tmp = cenv->references; tmp != NULL; tmp = tmp->next) {
            message->references = g_list_prepend(message->references,
                                                 g_strdup(tmp->data));
        }
        message->references = g_list_reverse(message->references);      
    }
    /* more! */
    /* FIXME: message->references_for_threading is just the reverse of
     * message->references; is there any reason to clutter up the
     * message structure with it, and allocate memory for another set of
     * g_strdup's? */

   if (!message->references_for_threading) {
       GList *tmp = g_list_copy(message->references);

       if (message->in_reply_to) {
           /* some mailers provide in_reply_to but no references, and
            * some apparently provide both but with the references in
            * the wrong order; we'll just make sure it's the first item
            * of this list (which will be the last after reversing it,
            * below) */
           GList *foo =
               g_list_find_custom(tmp, message->in_reply_to,
                                  (GCompareFunc) strcmp);
               
           if (foo) {
               tmp = g_list_remove_link(tmp, foo);
               g_list_free_1(foo);
           }
           tmp = g_list_prepend(tmp, message->in_reply_to);
       }
       message->references_for_threading = g_list_reverse(tmp);
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

