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
 * Author:  Emmanuel ALLAUD
 */

#include <ctype.h>
/* FIXME: */
#include "src/balsa-app.h"
#include "libbalsa.h"
#include "libbalsa_private.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#include "filter.h"
#include "filter-funcs.h"
#include "filter-private.h"
#include "misc.h"

/* FIXME : filtering takes time, we should notify progress to user */

/* in_string returns TRUE if s2 is a substring of s1
 * in_string is case insensitive */
static gboolean
in_string(const gchar * s1,const gchar * s2)
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
	for (;*s1 && toupper((int)*s2)!=toupper((int)*s1); s1++);
	if (*s1) {
	    /* We found the first char let see if this potential match is an actual one */
	    q=++s1;
	    p=s2+1;
	    while (*q && *p && toupper(*p)==toupper(*q)) {
		p++;
		q++;
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

gboolean
match_condition(LibBalsaCondition* cond, LibBalsaMessage * message)
{
    gboolean match = FALSE;
    gchar * str;
    GSList * regexs;
    LibBalsaConditionRegex* regex;
    GString * body;

    g_return_val_if_fail(cond,0); 

    switch (cond->type) {
    case CONDITION_SIMPLE:
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO)) {
	    str=libbalsa_make_string_from_list(message->to_list);
	    match=in_string(str,cond->match.string);
	    g_free(str);
            if(match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM)) {
	    str=libbalsa_address_to_gchar(message->from,0);
	    match=in_string(str,cond->match.string);
	    g_free(str);
	    if (match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT)) {
	    if (in_string(LIBBALSA_MESSAGE_GET_SUBJECT(message),
                          cond->match.string)) { 
                match = TRUE;
                break;
            }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC)) {
	    str=libbalsa_make_string_from_list(message->cc_list);
	    match=in_string(str,cond->match.string);
	    g_free(str);
	    if (match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY)) {
	    if (!libbalsa_message_body_ref(message)) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Unable to load message body to "
                                       "match filter"));
                return FALSE;  /* We don't want to match if an error occured */
	    }
	    body=content2reply(message,NULL,0,FALSE,FALSE);
	    libbalsa_message_body_unref(message);
	    if (body) {
		if (body->str) match=in_string(body->str,cond->match.string);
		g_string_free(body,TRUE);
		if (match) break;
	    }
	}
	break;
    case CONDITION_REGEX:
	g_assert(cond->match.regexs); 
	regexs=cond->match.regexs;
	for (;regexs;regexs=g_slist_next(regexs)) {
	    regex=(LibBalsaConditionRegex*) regexs->data;
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO)) {
		str=libbalsa_make_string_from_list(message->to_list);
		if (str) match=REGEXEC(*(regex->compiled),str)==0;
		g_free(str);
		if (match) break;
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM)) {
		str=libbalsa_address_to_gchar(message->from,0);
		if (str) match=REGEXEC(*(regex->compiled),str)==0;
		g_free(str);
		if (match) break;
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT)) {
		str=(gchar *)LIBBALSA_MESSAGE_GET_SUBJECT(message);
		if (str) match=REGEXEC(*(regex->compiled),str)==0;
		if (match) break;
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC)) {
		str=libbalsa_make_string_from_list(message->cc_list);
		if (str) match=REGEXEC(*(regex->compiled),str)==0;
		g_free(str);
		if (match) break;
	    }
	    if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY)) {
		if (!libbalsa_message_body_ref(message)) {
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                         _("Unable to load message body "
                                           "to match filter"));
		    return FALSE;
		}
		body=content2reply(message,NULL,0,FALSE,FALSE);
		libbalsa_message_body_unref(message);
		if (body && body->str) 
                    match = REGEXEC(*(regex->compiled),body->str)==0;
		if (body) g_string_free(body,TRUE);
		if (match) break;
	    }
	}
        break;
    case CONDITION_FLAG:
        match = (message->flags & cond->match.flags);
        break;
    case CONDITION_DATE:
        match = message->date>=cond->match.interval.date_low 
	       && (cond->match.interval.date_high==0 || 
                   message->date<=cond->match.interval.date_high);
    case CONDITION_NONE:
        break;
    }
    /* To avoid warnings */
    return cond->condition_not ? !match : match;
}

gint
match_conditions(FilterOpType op, GSList * cond, LibBalsaMessage * message)
{
    g_assert((op!=FILTER_NOOP) && cond);
    
    if (op==FILTER_OP_OR) {
	for (;cond &&!match_condition((LibBalsaCondition*)cond->data, message);
	     cond=g_slist_next(cond));
	return cond!=NULL;
    }
    for (;cond && match_condition((LibBalsaCondition*) cond->data,message);
	 cond=g_slist_next(cond));
    return cond==NULL;	      
}

/* libbalsa_filter_build_imap_query:
   returns an IMAP compatible query (RFC-2060)
   coresponding to given list of conditions.
*/
static GString*
extend_query(GString* query, const char* imap_str, const char* str,
             FilterOpType op)
{
    const char* prepstr =
        (op==FILTER_OP_OR && query->str[0]!='\0') ? "OR ": NULL;
    if(query->str[0]!='\0')
        g_string_prepend_c(query, ' ');
    g_string_prepend_c(query, '"');
    g_string_prepend(query, str);
    g_string_prepend(query, " \"");
    g_string_prepend(query, imap_str);
    if(prepstr) 
        g_string_prepend(query, prepstr);
    return query;
}

gchar*
libbalsa_filter_build_imap_query(FilterOpType op, GSList* condlist)
{
    GString* query = g_string_new("");
    gchar* str;
    
    for (;condlist; condlist=g_slist_next(condlist)) {
        LibBalsaCondition* cond = (LibBalsaCondition*)condlist->data;
        if (cond->type != CONDITION_SIMPLE) continue;

	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO))
            query = extend_query(query, "TO", cond->match.string, op);
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM))
            query = extend_query(query, "FROM", cond->match.string, op);
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT))
            query = extend_query(query, "SUBJECT", cond->match.string, op);
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC))
            query = extend_query(query, "CC", cond->match.string, op);
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY))
            query = extend_query(query, "TEXT", cond->match.string, op);
    }
    str = query->str;
    g_string_free(query, FALSE);
    return str;
}

/*--------- Filtering functions -------------------------------*/

/* FIXME : Add error reporting for each filter */

gint
filters_prepare_to_run(GSList * filters)
{
    LibBalsaFilter* fil;
    gboolean ok=TRUE;

    for(;filters;filters=g_slist_next(filters)) {
	fil=(LibBalsaFilter*) filters->data;
	if (!FILTER_CHKFLAG(fil,FILTER_VALID)) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Invalid filter : %s"),fil->name);
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
 * Assume that all messages come from ONE mailbox
 * returns TRUE if the trash bin has been filled
 * FIXME : Should position filter_errno on errors (bad command action,bad destination mailbox...)
 */

gboolean
filters_run_on_messages(GSList * filter_list, GList * messages)
{
    gint match;
    GSList * lst;
    GList * lst_messages;
    LibBalsaFilter * filt=NULL;
    LibBalsaMailbox * source_mbox;
    gboolean result=FALSE;

    if (!filter_list || ! messages) return FALSE;

    source_mbox=LIBBALSA_MESSAGE(messages->data)->mailbox;
    for (;messages;messages=g_list_next(messages)) {

	match=0;
	for (lst=filter_list;!match &&  lst;lst=g_slist_next(lst)) {
	    filt=(LibBalsaFilter*)lst->data;
	    match=match_conditions(filt->conditions_op,filt->conditions,LIBBALSA_MESSAGE(messages->data));	    
	}
	if (match) {
	    /* We hold a reference on the matching messages, to be sure they are still there when we do actions of filter */
	    g_object_ref(messages->data);
	    filt->matching_messages=g_list_prepend(filt->matching_messages,LIBBALSA_MESSAGE(messages->data));
	}
    }

    /* OK we have done all the matching thing, now we take every action for matching messages */

    for (lst=filter_list;lst;lst=g_slist_next(lst)) {
	LibBalsaMailbox *mbox;
 
	filt=(LibBalsaFilter*)lst->data;
	if (FILTER_CHKFLAG(filt,FILTER_SOUND)) {
	    /* FIXME : Emit sound */
	}
	if (FILTER_CHKFLAG(filt,FILTER_POPUP)) {
	    /* FIXME : Print popup text */
	}
	if (filt->matching_messages) {
	    switch (filt->action) {
	    case FILTER_COPY:
                mbox =
                    balsa_find_mailbox_by_name(filt->action_string);
		if (!mbox)
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,_("Bad mailbox name for filter : %s"),filt->name);
		else if (!libbalsa_messages_copy(filt->matching_messages,mbox))
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,_("Error when copying messages"));
		break;
	    case FILTER_TRASH:
		if (!balsa_app.trash || !libbalsa_messages_move(filt->matching_messages,balsa_app.trash))
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,_("Error when trashing messages"));
		else result=TRUE;
		break;
	    case FILTER_MOVE:
                mbox =
                    balsa_find_mailbox_by_name(filt->action_string);
		if (!mbox)
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,_("Bad mailbox name for filter : %s"),filt->name);
		else if (!libbalsa_messages_move(filt->matching_messages,mbox))
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR,_("Error when moving messages"));
		break;
	    case FILTER_PRINT:
		/* FIXME : to be implemented */
		break;
	    case FILTER_RUN:
		/* FIXME : to be implemented */
		break;
	    case FILTER_NOTHING:
		/* Nothing to do */
		break;
	    }
	    /* We unref all messages */
	    for (lst_messages=filt->matching_messages;lst_messages;lst_messages=g_list_next(lst_messages))
		g_object_unref(lst_messages->data);
	    g_list_free(filt->matching_messages);
	    filt->matching_messages=NULL;
	}
    }
    return result;
}

/*--------- End of Filtering functions -------------------------------*/

LibBalsaFilter*
libbalsa_filter_get_by_name(const gchar * fname)
{
    GSList * list;
    gint fnamelen;

    if (!fname || fname[0]=='\0') return NULL;

    fnamelen=strlen(fname);
    for (list=balsa_app.filters;
         list && 
             strncmp(fname,((LibBalsaFilter*)list->data)->name,fnamelen)!=0;
         list=g_slist_next(list))
        ;
    return list ? (LibBalsaFilter*)list->data : NULL;
}
