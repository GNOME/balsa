/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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
/*
 * filter.c
 * 
 * The mail filtering porting of balsa
 *
 * Mostly skeletonic
 *
 * Primary author: Emmanuel ALLAUD
 */

#include <ctype.h>

/* we need _() function, how to get it? */
#include <gnome.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "filter.h"
#include "filter-funcs.h"
#include "filter-private.h"
#include "misc.h"

/* filters_trash_mbox points to a mailbox that is used as Trash by filters
 *  code. */
static LibBalsaMailbox* filters_trash_mbox = NULL;

static UrlToMailboxMapper url_to_mailbox_mapper = NULL;
static GSList** filter_list = NULL;
void
libbalsa_filters_set_trash(LibBalsaMailbox* new_trash)
{
    filters_trash_mbox = new_trash;
}

void
libbalsa_filters_set_url_mapper(UrlToMailboxMapper u2mm)
{
    url_to_mailbox_mapper = u2mm;
}
void
libbalsa_filters_set_filter_list(GSList** list)
{
    filter_list = list;
}

/* FIXME : filtering takes time, we should notify progress to user */

/* in_string_utf8 returns TRUE if s2 is a substring of s1
 * in_string_utf8 is case insensitive
 * this functions understands utf8 strings (as you might have guessed ;-)
 */
static gboolean
in_string_utf8(const gchar * s1,const gchar * s2)
{
    const gchar * p,* q;

    /* convention : NULL string is contained in anything */
    if (!s2) return TRUE;
    /* s2 is non-NULL, so if s1==NULL we return FALSE :)*/
    if (!s1) return FALSE;
    /* OK both are non-NULL now*/
    /* If s2 is the empty string return TRUE */
    if (!*s2) return TRUE;
    while (*s1) {
	/* We look for the first char of s2*/
	for (;*s1 &&
		 g_unichar_toupper(g_utf8_get_char(s2))!=g_unichar_toupper(g_utf8_get_char(s1));
	     s1 = g_utf8_next_char(s1));
	if (*s1) {
	    /* We found the first char let see if this potential match is an actual one */
	    s1 = g_utf8_next_char(s1);
	    q = s1;
	    p = g_utf8_next_char(s2);
	    while (*q && *p && 
		   g_unichar_toupper(g_utf8_get_char(p))
		   ==g_unichar_toupper(g_utf8_get_char(q))) {
		p = g_utf8_next_char(p);
		q = g_utf8_next_char(q);
	    }
	    /* We have a match if p has reached the end of s2, ie *p==0 */
	    if (!*p) return TRUE;
	}
    }
    return FALSE;
}

/*------------------------------------------------------------------------
  ---- Helper functions (also exported to have a fine-grained API) -------
*/
/* libbalsa_condition_regex_set:
   steals the string.
*/
void
libbalsa_condition_regex_set(LibBalsaConditionRegex * reg, gchar *str)
{
    g_free(reg->string);
    reg->string = str;
}

const gchar*
libbalsa_condition_regex_get(LibBalsaConditionRegex * reg)
{
    return reg->string;
}

void
libbalsa_condition_prepend_regex(LibBalsaCondition* cond,
                                 LibBalsaConditionRegex * new_reg)
{
  cond->match.regexs =
      g_slist_prepend(cond->match.regexs, new_reg);
}

gboolean
match_condition(LibBalsaCondition* cond, LibBalsaMessage * message,
		gboolean mbox_locked)
{
    gboolean match = FALSE;
    gchar * str;
    GSList * regexs;
    LibBalsaConditionRegex* regex;
    GString * body;

    g_return_val_if_fail(cond, FALSE); 
    g_return_val_if_fail(message->headers != NULL, FALSE); 

    switch (cond->type) {
    case CONDITION_SIMPLE:
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO)) {
	    str=libbalsa_make_string_from_list(message->headers->to_list);
	    match=in_string_utf8(str,cond->match.string);
	    g_free(str);
            if(match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM)
            && message->headers->from) {
	    str=libbalsa_address_to_gchar(message->headers->from,0);
	    match=in_string_utf8(str,cond->match.string);
	    g_free(str);
	    if (match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT)) {
	    if (in_string_utf8(LIBBALSA_MESSAGE_GET_SUBJECT(message),
                          cond->match.string)) { 
                match = TRUE;
                break;
            }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC)) {
	    str=libbalsa_make_string_from_list(message->headers->cc_list);
	    match=in_string_utf8(str,cond->match.string);
	    g_free(str);
	    if (match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD)) {
	    if (cond->user_header) {
		GList * header =
		    libbalsa_message_find_user_hdr(message, cond->user_header);

		if (header) {
		    gchar ** tmp = header->data;
		    if (in_string_utf8(tmp[1],cond->match.string)) {
			match = TRUE;
			break;
		    }
		}
	    }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY)) {
	    gboolean is_refed;
	    
	    if (!message->mailbox)
		return FALSE; /* We don't want to match if an error occured */
	    if (mbox_locked)
		UNLOCK_MAILBOX(message->mailbox);
	    is_refed = libbalsa_message_body_ref(message, FALSE);
 	    if (mbox_locked)
 		LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
	    if (!is_refed) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Unable to load message body to "
                                       "match filter"));
                return FALSE;  /* We don't want to match if an error occured */
	    }
	    body = content2reply(message,NULL,0,FALSE,FALSE);
	    if (mbox_locked)
		UNLOCK_MAILBOX(message->mailbox);
	    libbalsa_message_body_unref(message);
	    if (mbox_locked)
		LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
	    if (body) {
		if (body->str) match = in_string_utf8(body->str,cond->match.string);
		g_string_free(body,TRUE);
	    }
	}
	break;
    case CONDITION_REGEX:
	g_assert(cond->match.regexs); 
	regexs=cond->match.regexs;
	for (;regexs;regexs=g_slist_next(regexs)) {
	    regex=(LibBalsaConditionRegex*) regexs->data;
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO)) {
		str=libbalsa_make_string_from_list(message->headers->to_list);
		if (str) {
		    match=REGEXEC(*(regex->compiled),str)==0;
		    g_free(str);
		    if (match) break;
		}
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM)
                && message->headers->from) {
		str=libbalsa_address_to_gchar(message->headers->from,0);
		if (str) {
		    match=REGEXEC(*(regex->compiled),str)==0;
		    g_free(str);
		    if (match) break;
		}
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT)) {
		const gchar *str = LIBBALSA_MESSAGE_GET_SUBJECT(message);
		if (str) match=REGEXEC(*(regex->compiled),str)==0;
		if (match) break;
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC)) {
		str=libbalsa_make_string_from_list(message->headers->cc_list);
		if (str) {
		    match=REGEXEC(*(regex->compiled),str)==0;
		    g_free(str);
		    if (match) break;
		}
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD)) {
		if (cond->user_header) {
		    GList * header =
			libbalsa_message_find_user_hdr(message, cond->user_header);
		    
		    if (header) {
			gchar ** tmp = header->data;
			if (tmp[1]) {
			    match=REGEXEC(*(regex->compiled),tmp[1])==0;
			    if (match) break;
			}
		    }
		}
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY)) {
		gboolean is_refed;

		if (!message->mailbox)
		    return FALSE;
		if (mbox_locked)
		    UNLOCK_MAILBOX(message->mailbox);
		is_refed = libbalsa_message_body_ref(message, FALSE);
		if (mbox_locked)
		    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
		if (!is_refed) {
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                         _("Unable to load message body "
                                           "to match filter"));
		    return FALSE;
		}
		body=content2reply(message,NULL,0,FALSE,FALSE);
 		if (mbox_locked)
		    UNLOCK_MAILBOX(message->mailbox);
		libbalsa_message_body_unref(message);
		if (mbox_locked)
		    LOCK_MAILBOX_RETURN_VAL(message->mailbox, FALSE);
		if (body && body->str) 
                    match = REGEXEC(*(regex->compiled),body->str)==0;
		g_string_free(body,TRUE);
	    }
	}
        break;
    case CONDITION_FLAG:
        match = LIBBALSA_MESSAGE_HAS_FLAG(message, cond->match.flags);
        break;
    case CONDITION_DATE:
        match = message->headers->date>=cond->match.interval.date_low 
	       && (cond->match.interval.date_high==0 || 
                   message->headers->date<=cond->match.interval.date_high);
    case CONDITION_NONE:
        break;
    }
    /* To avoid warnings */
    return cond->condition_not ? !match : match;
}

gint
match_conditions(FilterOpType op, GSList * cond, LibBalsaMessage * message,
		 gboolean mbox_locked)
{
    GSList * lst = cond;
    g_assert((op!=FILTER_NOOP) && cond && message);

    /* First let's see if we exclude deleted messages or not : for that we
       look if the conditions contain an explicit match on the deleted flag */
    while (lst) {
	LibBalsaCondition * c = lst->data;

	if ((c->type==CONDITION_FLAG) &&
	    (c->match.flags & LIBBALSA_MESSAGE_FLAG_DELETED))
	    break;
	 lst = g_slist_next(lst);
    }
    if (!lst && LIBBALSA_MESSAGE_IS_DELETED(message)) return FALSE;

    if (op==FILTER_OP_OR) {
	for (;cond &&!match_condition((LibBalsaCondition*)cond->data, message,
				      mbox_locked);
	     cond=g_slist_next(cond));
	return cond!=NULL;
    }
    for (;cond && match_condition((LibBalsaCondition*) cond->data, message,
				  mbox_locked);
	 cond=g_slist_next(cond));
    return cond==NULL;	      
}

static void
extend_query(GString * query, const gchar * imap_str, gchar * string)
{
    if (query->str[0]!='\0')
	g_string_append_c(query, ' ');
    g_string_append(query, "NOT ");    
    g_string_append(query, imap_str);
    if (string) {
	g_string_append(query, " \"");
	g_string_append(query, string);
	g_string_append_c(query, '"');
    }
}

/* Stupid problem : the IMAP SEARCH command considers AND its default
   logical operator, but balsa filters uses OR. Moreover the OR syntax
   of IMAP SEARCH is rather broken (IMHO), so I have to translate our
   P OR Q to a NOT(NOT P NOT Q) in the SEARCH syntax.
 */
gchar*
libbalsa_filter_build_imap_query(FilterOpType op, GSList* condlist,
				 gboolean only_recent)
{
    GString* query = g_string_new("");
    gchar* str;
    gboolean first = TRUE, match_on_deleted = FALSE;
    gchar str_date[20];
    struct tm * date;

    for (;condlist;condlist = g_slist_next(condlist)) {
        LibBalsaCondition* cond = (LibBalsaCondition*)condlist->data;
	GString * buffer = g_string_new("");

	switch (cond->type) {
	case CONDITION_SIMPLE:
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO))
		extend_query(buffer, "TO", cond->match.string);
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM))
		extend_query(buffer, "FROM", cond->match.string);
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT))
		extend_query(buffer, "SUBJECT", cond->match.string);
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC))
		extend_query(buffer, "CC", cond->match.string);
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY))
		extend_query(buffer, "TEXT", cond->match.string);
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD)) {
		gchar * tmp = g_strdup_printf("HEADER %s", cond->user_header);
		extend_query(buffer, tmp, cond->match.string);
		g_free(tmp);
	    }
	    break;
	case CONDITION_DATE:
	    str = NULL;
	    if (cond->match.interval.date_low) {
		date = localtime(&cond->match.interval.date_low);
		strftime(str_date, sizeof(str_date), "%Y-%m-%d", date);
		str = g_strdup_printf("SENTSINCE %s",str_date);
	    }
	    if (cond->match.interval.date_high) {
		if (str) {
		    g_string_append_c(buffer, '(');
		    g_string_append(buffer, str);
		    g_string_append_c(buffer, ' ');
		    g_free(str);
		}
		date = localtime(&cond->match.interval.date_high);
		strftime(str_date, sizeof(str_date), "%Y-%m-%d", date);
		str = g_strdup_printf("SENTBEFORE %s", str_date);
	    }
	    /* If no date has been put continue (this is not allowed normally
	       but who knows */
	    if (!str)
		continue;
	    g_string_append(buffer, str);
	    g_free(str);
	    if (buffer->str[0]=='(')
		g_string_append_c(buffer, ')');
	    g_string_prepend(buffer, "NOT ");
	    break;
	case CONDITION_FLAG:
	    /* NOTE : nothing about replied flag in the IMAP protocol,
	       so continue if only this flag is present */
	    if (!(cond->match.flags & ~LIBBALSA_MESSAGE_FLAG_REPLIED))
		continue;
	    if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_NEW)
		extend_query(buffer, "NEW", NULL);
	    if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_DELETED) {
		/* Special case : for all others match we exclude
		   deleted filters, but not for this one obviously */
		match_on_deleted = TRUE;
		extend_query(buffer, "DELETED", NULL);
	    }
	    if (cond->match.flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)
		extend_query(buffer, "FLAGGED", NULL);
	case CONDITION_NONE:
	case CONDITION_REGEX:
	default:
	    break;
	}

	if (!first)
	    g_string_append_c(query, ' ');
	else first = FALSE;
	/* See remark above */
	if ((op == FILTER_OP_AND && !cond->condition_not)
	    || (op == FILTER_OP_OR && cond->condition_not)) {
	    g_string_append(query, "NOT (");
	    g_string_append_c(buffer, ')');
	}
	g_string_append(query, buffer->str);
	g_string_free(buffer, TRUE);
    }

    /* See remark above */
    if (op == FILTER_OP_OR) {
	g_string_prepend(query, "NOT (");
	g_string_append_c(query, ')');
    }
    if (!match_on_deleted)
	g_string_prepend(query, "UNDELETED ");
    if (only_recent)
	g_string_prepend(query, "RECENT ");
    str = query->str;
    g_string_free(query, FALSE);
    return str;
}

/*--------- Filtering functions -------------------------------*/

gint
filters_prepare_to_run(GSList * filters)
{
    LibBalsaFilter* fil;
    gboolean ok=TRUE;

    for(;filters;filters=g_slist_next(filters)) {
	fil=(LibBalsaFilter*) filters->data;
	if (!FILTER_CHKFLAG(fil,FILTER_VALID)) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Invalid filter: %s"),fil->name);
	    ok=FALSE;
	}
	else if (!FILTER_CHKFLAG(fil,FILTER_COMPILED))
	    ok=libbalsa_filter_compile_regexs(fil);
    }

    return ok;
}

/*
 * Run all filters until one matches (so order of filter is important)
 * filters must be valid and compiled (ie filters_prepare_to_run have been called before)
 * In general you'll call this function with mailbox lock held (ie you locked the mailbox
 * you're filtering before calling this function)
 */

void
libbalsa_filter_match(GSList * filter_list, GList * messages,
		      gboolean mbox_locked)
{
    gint match;
    GSList * lst;
    LibBalsaFilter * filt=NULL;

    if (!filter_list || ! messages) return;

    for (;messages;messages=g_list_next(messages)) {

	match=0;
	for (lst=filter_list;!match &&  lst;lst=g_slist_next(lst)) {
	    filt=(LibBalsaFilter*)lst->data;
	    match = 
		match_conditions(filt->conditions_op, filt->conditions,
				 LIBBALSA_MESSAGE(messages->data), mbox_locked);
	}
	if (match && !g_list_find(filt->matching_messages, messages->data)) {
	    /* We hold a reference on the matching messages, to be sure they 
	       are still there when we do actions of filter */
	    g_object_ref(messages->data);
	    filt->matching_messages = 
		g_list_prepend(filt->matching_messages,
			       LIBBALSA_MESSAGE(messages->data));
	}
    }
}

void libbalsa_filter_sanitize(GSList * filter_list)
{
    GSList * lst,* lst2;
    GList * matching;
    LibBalsaFilter * flt,* flt2;

    /* Now we traverse all matching messages so that we only keep the first
       match : ie if a message matches several filters, it is kept only by
       the first one as a matching message.
    */
    for (lst = g_slist_next(filter_list); lst; lst = g_slist_next(lst)) {
	flt = lst->data;
	for (matching = flt->matching_messages; matching;) {
	    for (lst2 = filter_list; lst2 != lst; lst2 = g_slist_next(lst2)) {
		flt2 = lst2->data;
		/* We found that this message matches a previous filter, get
		   rid of it and stop the search */
		if (g_list_find(flt2->matching_messages, matching->data)) {
		    /* Don't forget to pass to the next message before deleting
		       the current one */
		    GList * tmp = g_list_next(matching);

		    flt->matching_messages = 
			g_list_delete_link(flt->matching_messages, matching);
		    matching = tmp;
		    break;
		}
	    }
	    /* Pass to the next message only if the current one has not been
	       found, else matching has already been made pointing to the next
	       message */
	    if (lst2==lst)
		matching = g_list_next(matching);
	}
    }
}

/* Apply all filters on their matching messages (call
 * libbalsa_filter_match before) returns TRUE if the trash bin has
 * been filled FIXME: Should position filter_errno on errors (bad
 * command action,bad destination mailbox...)
 */

gboolean
libbalsa_filter_apply(GSList * filter_list)
{
    GSList * lst;
    GList * lst_messages;
    LibBalsaFilter * filt=NULL;
    gboolean result=FALSE;
    LibBalsaMailbox *mbox;
    gchar * p;
    guint set_mask, unset_mask;

    g_return_val_if_fail(url_to_mailbox_mapper, FALSE);
    if (!filter_list) return FALSE;
    for (lst=filter_list;lst;lst=g_slist_next(lst)) {
	filt=(LibBalsaFilter*)lst->data;
	if (!filt->matching_messages) continue;
        if (filt->sound)
	    gnome_sound_play(filt->sound);
        if (filt->popup_text)
	    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
				 filt->popup_text);
        switch (filt->action) {
        case FILTER_COPY:
            mbox = url_to_mailbox_mapper(filt->action_string);
            if (!mbox)
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Bad mailbox name for filter: %s"),
                                     filt->name);
            else if (!libbalsa_messages_copy(filt->matching_messages,mbox))
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Error when copying messages"));
            else if (mbox==filters_trash_mbox) result=TRUE;
            break;
        case FILTER_TRASH:
            if (!filters_trash_mbox || 
                !libbalsa_messages_move(filt->matching_messages,
                                        filters_trash_mbox))
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Error when trashing messages"));
            else result=TRUE;
            break;
        case FILTER_MOVE:
            mbox = url_to_mailbox_mapper(filt->action_string);
            if (!mbox)
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Bad mailbox name for filter: %s"),
                                     filt->name);
            else if (!libbalsa_messages_move(filt->matching_messages,mbox))
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Error when moving messages"));
            else if (mbox==filters_trash_mbox) result=TRUE;
            break;
        case FILTER_PRINT:
            /* FIXME : to be implemented */
            break;
        case FILTER_RUN:
            /* FIXME : to be implemented */
            break;
	case FILTER_CHFLAG:
	    p = filt->action_string;
	    set_mask = g_ascii_strtoull (filt->action_string, &p, 10);
	    if (*p++ != '*') {
		g_warning("Malformed message flags mask");
		unset_mask = 0;
	    }
	    else unset_mask =  g_ascii_strtoull(p, &p, 10);
	    for (lst_messages = filt->matching_messages; lst_messages; 
		 lst_messages++) {
		LIBBALSA_MESSAGE_SET_FLAGS(lst_messages->data, set_mask);
		LIBBALSA_MESSAGE_UNSET_FLAGS(lst_messages->data, unset_mask);
	    }
	    break;
        case FILTER_NOTHING:
            /* Nothing to do */
            break;
        }
        /* We unref all messages */
        for (lst_messages=filt->matching_messages; lst_messages;
             lst_messages=g_list_next(lst_messages))
            g_object_unref(lst_messages->data);
        g_list_free(filt->matching_messages);
        filt->matching_messages=NULL;
    }
    return result;
}

/* libbalsa_extract_new_messages : returns a sublist of the messages
   list containing all "new" messages, ie just retrieved mails
*/

GList * libbalsa_extract_new_messages(GList * messages)
{
    GList * extracted=NULL;

    libbalsa_lock_mutt();
    for (;messages;messages=g_list_next(messages)) {
	LibBalsaMessage * message = LIBBALSA_MESSAGE(messages->data);
	HEADER * cur;

	if (!message->header->old) {
	    extracted = g_list_prepend(extracted, message);
	    cur = message->header;
	    mutt_set_flag(CLIENT_CONTEXT(message->mailbox), cur, M_OLD, TRUE);
	}
    }
    libbalsa_unlock_mutt();
    return extracted;
}

/*--------- End of Filtering functions -------------------------------*/

LibBalsaFilter*
libbalsa_filter_get_by_name(const gchar * fname)
{
    GSList * list;
    gint fnamelen;

    g_return_val_if_fail(filter_list, NULL);
    if (!fname || fname[0]=='\0') return NULL;

    fnamelen = strlen(fname);
    for (list = *filter_list;list;list = g_slist_next(list)) {
	gint len = strlen(((LibBalsaFilter*)list->data)->name);

	if (strncmp(fname,((LibBalsaFilter*)list->data)->name,
		    MAX(len,fnamelen))==0)
	    return (LibBalsaFilter*)list->data;
    }
    return NULL;
}
