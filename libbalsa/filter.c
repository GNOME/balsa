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
#include <string.h>

#include <libgnome/libgnome.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "filter.h"
#include "filter-funcs.h"
#include "filter-private.h"
#include "misc.h"

/* from libmutt */
#define REGCOMP(X,Y,Z) regcomp(X, Y, REG_WORDS|REG_EXTENDED|(Z))
#define REGEXEC(X,Y) regexec(&X, Y, (size_t)0, (regmatch_t *)0, (int)0)


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
    g_warning("%s: fixme!\n", __func__);
#if 0
  cond->match.regexs =
      g_slist_prepend(cond->match.regexs, new_reg);
#endif  
}

gboolean
libbalsa_condition_matches(LibBalsaCondition* cond,
			   LibBalsaMessage * message)
{
    gboolean match = FALSE;
    gchar * str;
    GString * body;
    gboolean will_ref;

    g_return_val_if_fail(cond, FALSE); 
    g_return_val_if_fail(message->headers != NULL, FALSE); 

    switch (cond->type) {
    case CONDITION_STRING:
        will_ref =
            (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC) ||
             CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY));
        if(will_ref) {
            gboolean is_refed =
                libbalsa_message_body_ref(message, FALSE, FALSE);
            if (!is_refed) {
                libbalsa_information(LIBBALSA_INFORMATION_ERROR,
                                     _("Unable to load message body to "
                                       "match filter"));
                return FALSE;  /* We don't want to match if an error occured */
            }
        }
        /* do the work */
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_TO)) {
	    str=libbalsa_make_string_from_list(message->headers->to_list);
	    match=in_string_utf8(str,cond->match.string.string);
	    g_free(str);
            if(match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_FROM)
            && message->headers->from) {
	    str=libbalsa_address_to_gchar(message->headers->from,0);
	    match=in_string_utf8(str,cond->match.string.string);
	    g_free(str);
	    if (match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_SUBJECT)) {
	    if (in_string_utf8(LIBBALSA_MESSAGE_GET_SUBJECT(message),
                          cond->match.string.string)) { 
                match = TRUE;
                break;
            }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_CC)) {
	    str=libbalsa_make_string_from_list(message->headers->cc_list);
	    match=in_string_utf8(str,cond->match.string.string);
	    g_free(str);
	    if (match) break;
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_US_HEAD)) {
	    if (cond->match.string.user_header) {
		GList * header =
		    libbalsa_message_find_user_hdr(message,
                                                   cond->match.string
                                                   .user_header);

		if (header) {
		    gchar ** tmp = header->data;
		    if (in_string_utf8(tmp[1],cond->match.string.string)) {
			match = TRUE;
			break;
		    }
		}
	    }
	}
	if (CONDITION_CHKMATCH(cond,CONDITION_MATCH_BODY)) {
	    if (!message->mailbox)
		return FALSE; /* We don't want to match if an error occured */
	    body = content2reply(message,NULL,0,FALSE,FALSE);
	    if (body) {
		if (body->str)
                    match = in_string_utf8(body->str,
                                           cond->match.string.string);
		g_string_free(body,TRUE);
	    }
	}
        if(will_ref) libbalsa_message_body_unref(message);
	break;
    case CONDITION_REGEX:
        break;
    case CONDITION_DATE:
        match = message->headers->date>=cond->match.date.date_low 
	       && (cond->match.date.date_high==0 || 
                   message->headers->date<=cond->match.date.date_high);
        break;
    case CONDITION_FLAG:
        match = LIBBALSA_MESSAGE_HAS_FLAG(message, cond->match.flags);
        break;
    case CONDITION_AND:
        match =
	    libbalsa_condition_matches(cond->match.andor.left, message) &&
	    libbalsa_condition_matches(cond->match.andor.right, message);
        break;
    case CONDITION_OR:
        match =
	    libbalsa_condition_matches(cond->match.andor.left, message) ||
	    libbalsa_condition_matches(cond->match.andor.right, message);
        break;
    case CONDITION_NONE:
        break;
    }
    /* To avoid warnings */
    return cond->negate ? !match : match;
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

/* Apply the filter's action to the messages in the list; returns TRUE
 * if message(s) were moved to the trash. */
gboolean
libbalsa_filter_mailbox_messages(LibBalsaFilter * filt,
				 LibBalsaMailbox * mailbox,
				 GArray * msgnos)
{
    gboolean result=FALSE;
    LibBalsaMailbox *mbox;
    GError *err = NULL;

    if (msgnos->len == 0)
	return FALSE;

    if (filt->sound)
	gnome_sound_play(filt->sound);
    if (filt->popup_text)
	libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
			     filt->popup_text);

    libbalsa_lock_mailbox(mailbox);

    switch (filt->action) {
    case FILTER_COPY:
	mbox = url_to_mailbox_mapper(filt->action_string);
	if (!mbox)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Bad mailbox name for filter: %s"),
				 filt->name);
	else if (!libbalsa_mailbox_messages_copy(mailbox, msgnos, mbox, &err))
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Error when copying messages: %s"),
                                 err ? err->message : "?");
	else if (mbox == filters_trash_mbox)
	    result = TRUE;
	break;
    case FILTER_TRASH:
	if (!filters_trash_mbox ||
	    !libbalsa_mailbox_messages_move(mailbox, msgnos,
					    filters_trash_mbox, &err))
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Error when trashing messages: %s"),
                                 err ? err->message : "?");
	else
	    result = TRUE;
	break;
    case FILTER_MOVE:
	mbox = url_to_mailbox_mapper(filt->action_string);
	if (!mbox)
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Bad mailbox name for filter: %s"),
				 filt->name);
	else if (!libbalsa_mailbox_messages_move(mailbox, msgnos, mbox, &err))
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("Error when moving messages: %s"),
                                 err ? err->message : "?");
	else if (mbox == filters_trash_mbox)
	    result = TRUE;
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
    g_clear_error(&err);
    libbalsa_unlock_mailbox(mailbox);

    return result;
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

/* Check whether the condition tests the message body, and if so,
 * whether it's already loaded; used by the imap mailbox driver to
 * decide whether to do a server-side match. */
gboolean
libbalsa_condition_can_match(LibBalsaCondition * cond,
			     LibBalsaMessage * message)
{
    if (!message)
	return FALSE;

    switch (cond->type) {
    case CONDITION_STRING:
	return !(CONDITION_CHKMATCH(cond, CONDITION_MATCH_BODY)
		 && message->body_list == NULL)
	    && !(CONDITION_CHKMATCH(cond, CONDITION_MATCH_US_HEAD)
		 && message->mime_msg == NULL);
    case CONDITION_AND:
    case CONDITION_OR:
	return libbalsa_condition_can_match(cond->match.andor.left,
					    message)
	    && libbalsa_condition_can_match(cond->match.andor.right,
					    message);
    default:
	return TRUE;
    }
}

/* Check whether a condition looks only at flags; if it does, test
 * whether the given message's flags match it, and return the result in
 * *match; used by mailbox backends to decide when the full
 * LibBalsaMessage is needed. */
gboolean
libbalsa_condition_is_flag_only(LibBalsaCondition * cond,
                                LibBalsaMailbox * mailbox,
                                guint msgno,
				gboolean * match)
{
    gboolean retval;
    gboolean left_match, right_match;

    switch (cond->type) {
    case CONDITION_FLAG:
        if (match)
            *match =
                libbalsa_mailbox_msgno_has_flags(mailbox, msgno,
                                                 cond->match.flags, 0);
        retval = TRUE;
        break;
    case CONDITION_AND:
        retval =
            libbalsa_condition_is_flag_only(cond->match.andor.left,
                                            mailbox, msgno,
					    &left_match)
            && libbalsa_condition_is_flag_only(cond->match.andor.right,
                                               mailbox, msgno,
                                               &right_match);
        if (retval && match)
            *match = left_match && right_match;
        break;
    case CONDITION_OR:
        retval =
            libbalsa_condition_is_flag_only(cond->match.andor.left,
                                            mailbox, msgno,
					    &left_match)
            && libbalsa_condition_is_flag_only(cond->match.andor.right,
                                               mailbox, msgno,
                                               &right_match);
        if (retval && match)
            *match = left_match || right_match;
        break;
    default:
        return FALSE;
    }

    if (retval && match && cond->negate)
        *match = !*match;

    return retval;
}
