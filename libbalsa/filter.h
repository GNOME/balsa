/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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
 * filter.h
 * 
 * Header for libfilter, the mail filtering porting of balsa
 *
 * Authors:  Joel Becker - Emmanuel Allaud
 */

#ifndef __FILTER_H__
#define __FILTER_H__

#include <glib.h>

#include "libbalsa.h"

typedef struct _LibBalsaConditionRegex LibBalsaConditionRegex;

/* Conditions definition :
 * a condition is the basic component of a filter
 * It can be of type (mimic the old filter types) :
 * - CONDITION_SIMPLE : matches when a string is contained in 
 *   one of the specified fields
 * - CONDITION_REGEX : matches when one of the reg exs matches on one
 *   of the specified fields
 * - CONDITION_EXEC : matches when the execution of the given command
 *   with parameters (FIXME : what are they???, see proposition for filter command) returns 1
 *
 * A condition has attributes :
 * - match_fields : a gint specifying on which fields to apply the condition
 * - condition_not : a gboolean to negate (logical not) the condition
 */

/* condition match types */

typedef enum {
    CONDITION_NONE,
    CONDITION_SIMPLE,
    CONDITION_REGEX,
    CONDITION_DATE,
    CONDITION_FLAG
} ConditionMatchType;

typedef struct _LibBalsaCondition {
    ConditionMatchType type;
    gboolean condition_not;

    /* The match type fields */
    union _match {
	gchar * string;	           /* for CONDITION_SIMPLE,CONDITION_DATE */
	GSList * regexs;           /* for CONDITION_REGEX */
	struct {
	    time_t date_low,date_high; /* for CONDITION_DATE            */
                                       /* (date_high==0=>no high limit) */
	} interval;
	LibBalsaMessageFlag flags;
    } match;
    guint match_fields;         /* Contains the flag mask for CONDITION_FLAG type */
    gchar * user_header;        /* This is !=NULL and gives the name of the user
				   header against which we make the match */
} LibBalsaCondition;

/* Filter definition :
 * a filter is defined by 
 * - a list of conditions and a gint conditions_op
 *   specifying the logical op to apply on the result of the condition match
 * - an action to perform on match : move, copy, print or trash the
 *   matching message, emit a sound, popup a text, execute a command
 */

typedef enum {
    FILTER_NOTHING,
    FILTER_COPY,
    FILTER_MOVE,
    FILTER_PRINT,
    FILTER_RUN,
    FILTER_TRASH              /* Must be the last one */
} FilterActionType;

typedef enum {
    FILTER_NOOP,
    FILTER_OP_OR,
    FILTER_OP_AND             /* Must be the last one */
} FilterOpType;

/*
 * filter error codes
 * (not an enum cause they *have* to match filter_errlist)
 */

#define FILTER_NOERR         0
#define FILTER_EFILESYN      1
#define FILTER_ENOMEM        2
#define FILTER_EREGSYN       3
#define FILTER_EINVALID      4

/*
 * Filter errors set the variable filter_errno (like errno)
 * See policy to use it in filter-error.c
 */
extern gint filter_errno;

typedef struct _LibBalsaFilter {

    gchar *name;
    FilterOpType conditions_op;
    gint flags;

    GSList * conditions;

    /* The notification fields : NULL signifies no notification */
    gchar * sound;
    gchar * popup_text;

    /* The action */
    FilterActionType action;
    /* action_string depends on action : 
     * - if action is FILTER_MOVE, or FILTER_COPY, action_string is
     *   the URL (this is mandatory because it determines UNIQUELY
     *   the mailbox, unlike the name) of the mailbox to move/copy 
     *   the matching message
     * - if action is FILTER_RUN, action_string is the command to run
     *   for now this is the way to specify parameters (replaced by
     *   pieces of the matching message) for the running command,
     *   proposition : %f,%t,%c,%s are replaced by the corresponding
     *   header (from,to,cc,subject) field of the matching message on
     *   the command line with enclosing quotes if necessary, e.g. :
     *   command_to_run %t %s -----> command_to_run manu@wanadoo.fr
     *   "about filters" If you want the body, we must find a way to
     *   pipe it to the std input of the command (FIXME what do we do
     *   for different parts, attachments and so on?)
     * - if action is FILTER_TRASH it's NULL
     * - FIXME if action is FILTER_PRINT it could be the print command ?
     */
    gchar * action_string;

    /* The following fields are used when the filter runs */

    /* List of matching messages */
    GList * matching_messages;

} LibBalsaFilter;

/*
 * Exported filter functions A lot are, to have a fine-grained API so
 * we can use filter engine for a lot of different purpose : search
 * functions, virtual folders..., not only filtering
 */

void libbalsa_condition_regex_set(LibBalsaConditionRegex * reg, gchar *str);
/* returns pointer to internal data, treat with caution! */
const gchar* libbalsa_condition_regex_get(LibBalsaConditionRegex * reg);

void libbalsa_condition_prepend_regex(LibBalsaCondition* cond,
                                      LibBalsaConditionRegex *new_reg);

gint match_condition(LibBalsaCondition* cond,LibBalsaMessage* message,
		     gboolean mbox_locked);

gint match_conditions(FilterOpType op,GSList* cond,LibBalsaMessage* message,
		      gboolean mbox_locked);

gchar* libbalsa_filter_build_imap_query(FilterOpType, GSList* conditions,
					gboolean only_recent);

/* Filtering functions */
/* FIXME : perhaps I should try to use multithreading -> but we must
   therefore use lock very well */
/* prepare_filters_to_run will test all filters for correctness,
   compile regexs if needed
 * Return 
 * - TRUE on success (all filters are valid, ready to be applied)
 * - FALSE if there are invalid filters
 */

gint filters_prepare_to_run(GSList * filters);

/* libbalsa_filter_match run all filters on the list of messages
   each filter is stuffed with the list of its matching messages
   you must call libbalsa_filter_apply after to make the filters
   act on their matching messages (this split is needed for proper
   locking)
 */

void libbalsa_filter_match(GSList * filter_list, GList * messages,
			   gboolean mbox_locked);

/* Sanitize the matching messages of a filters list, ie if a
   message matches several filters, only keep the first match
   Essentially used by IMAP code
 */
void libbalsa_filter_sanitize(GSList * filter_list);

/* libbalsa_filter_apply will let all filters to apply on their
 * matching messages (you must call libbalsa_filters_match before)
 * It returns TRUE if the trash bin has been filled with something
 * this is used to call enable_empty_trash after
 */

gboolean libbalsa_filter_apply(GSList * filter_list);

/* libalsa_extract_new_messages : returns a sublist of the messages list containing all
   "new" messages, ie just retrieved mails
*/

GList * libbalsa_extract_new_messages(GList * messages);

/*
 * libbalsa_filter_get_by_name()
 * search in the filter list the filter of name fname or NULL if unfound
 */

LibBalsaFilter* libbalsa_filter_get_by_name(const gchar* fname);

/*
 * Dialog calls
 */
/* filters_edit_dialog launches (guess what :) the filters edit dialog box
 * to modify the list of all filters
 */
void filters_edit_dialog(void);

/* filter_run_dialog edits and runs the list of filters of the mailbox
 */
void filters_run_dialog(LibBalsaMailbox *mbox);

/* filter_export_dialog to export filters as sieve scripts
 */

void
filters_export_dialog(void);

void libbalsa_filters_set_trash(LibBalsaMailbox* new_trash);
typedef LibBalsaMailbox* (*UrlToMailboxMapper)(const gchar* url);
void libbalsa_filters_set_url_mapper(UrlToMailboxMapper u2mm);
void libbalsa_filters_set_filter_list(GSList** list);

/*
 * Error calls
 */
gchar *filter_strerror(gint filter_errnum);
void filter_perror(const gchar * s);

#endif				/* __FILTER_H__ */
