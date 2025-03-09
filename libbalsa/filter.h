/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

#include <time.h>
#include "libbalsa.h"

typedef struct _LibBalsaConditionRegex LibBalsaConditionRegex;

/* Conditions definition :
 * a condition is the basic component of a filter
 * It can be of type (mimic the old filter types) :
 * - CONDITION_STRING : matches when a string is contained in 
 *   one of the specified fields
 * - CONDITION_REGEX : matches when one of the reg exs matches on one
 *   of the specified fields
 * - CONDITION_EXEC : matches when the execution of the given command
 *   with parameters (FIXME : what are they???, see proposition for filter command) returns 1
 *
 * A condition has attributes :
 * - fields : a gint specifying on which fields to apply the condition
 * - negate : a gboolean to negate (logical not) the condition
 */

/* condition match types */

typedef enum {
    CONDITION_NONE,
    CONDITION_STRING, /*  */
    CONDITION_REGEX,
    CONDITION_DATE,
    CONDITION_FLAG,
    CONDITION_AND, /* Condition has a list of subconditions and
                    * matches if all the subconditions match. */
    CONDITION_OR   /* Condition has a list of subconditions and
                    * matches if any subcondition matches. */
} ConditionMatchType;

struct _LibBalsaCondition {
    gint ref_count;
    gboolean negate; /* negate the result of the condition. */
    ConditionMatchType type;

    /* The match type fields */
    union _match {
        /* CONDITION_STRING */
        struct {
            unsigned fields;     /* Contains the header list for
                                  * that this search should look in. */
            gchar * string;	 
            gchar * user_header; /* This is !=NULL and gives the name
                                  * of the user header against which
                                  * we make the match if fields
                                  * includes
                                  * CONDITION_MATCH_US_HEAD. */
        } string;
        /* CONDITION_REGEX */
        struct {
            unsigned fields;     /* Contains the header list for
                                  * that this search should look in. */
            /* GSList * regexs; */
        } regex;
        /* CONDITION_DATE */
	struct {
	    time_t date_low,date_high; /* for CONDITION_DATE            */
                                       /* (date_high==0=>no high limit) */
	} date;
        /* CONDITION_FLAG */
	LibBalsaMessageFlag flags;

        /* CONDITION_AND and CONDITION_OR */
        struct {
            LibBalsaCondition *left, *right;
        } andor;
    } match;
};

LibBalsaCondition* libbalsa_condition_new_from_string(gchar **string);
gchar*             libbalsa_condition_to_string(LibBalsaCondition *cond);
gchar*             libbalsa_condition_to_string_user(LibBalsaCondition *cond);

LibBalsaCondition* libbalsa_condition_new_flag_enum(gboolean negated,
                                                    LibBalsaMessageFlag flgs);

LibBalsaCondition* libbalsa_condition_new_string(gboolean negated,
                                                 unsigned headers,
                                                 gchar *str,
                                                 gchar *user_header);
LibBalsaCondition* libbalsa_condition_new_date(gboolean negated,
                                               time_t *from, time_t *to);
LibBalsaCondition* libbalsa_condition_new_bool_ptr(gboolean negated,
                                                   ConditionMatchType cmt,
                                                   LibBalsaCondition *left,
                                                   LibBalsaCondition *right);
LibBalsaCondition* libbalsa_condition_ref(LibBalsaCondition* cnd);
void               libbalsa_condition_unref(LibBalsaCondition*); 


typedef enum {
    FILTER_NOOP,
    FILTER_OP_OR,
    FILTER_OP_AND             /* Must be the last one */
} FilterOpType;

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
    FILTER_TRASH,
    FILTER_COLOR,
    FILTER_N_TYPES
} FilterActionType;

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
    gint flags;

    LibBalsaCondition *condition; /* A codition, possibly a composite
                                   * one. */

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

/** libbalsa_condition_matches() checks whether given message matches the 
 * condition. */
gboolean libbalsa_condition_matches(LibBalsaCondition* cond,
                                    LibBalsaMessage* message);

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

/* Apply the filter action to the list of messages.
 * It returns TRUE if the trash bin has been filled with something
 * this is used to call enable_empty_trash after
 */

gboolean libbalsa_filter_mailbox_messages(LibBalsaFilter * filt,
					  LibBalsaMailbox * mailbox,
					  GArray * msgnos);

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
void filters_edit_dialog(GtkWindow * parent);

/* filter_run_dialog edits and runs the list of filters of the mailbox
 */
void filters_run_dialog(LibBalsaMailbox * mbox, GtkWindow * parent);

/* filter_export_dialog to export filters as sieve scripts
 */

void filters_export_dialog(GtkWindow * parent);

void libbalsa_filters_set_trash(LibBalsaMailbox* new_trash);
typedef LibBalsaMailbox* (*UrlToMailboxMapper)(const gchar* url);
void libbalsa_filters_set_url_mapper(UrlToMailboxMapper u2mm);
void libbalsa_filters_set_filter_list(GSList** list);

/*
 * Error calls
 */
gchar *filter_strerror(gint filter_errnum);
void filter_perror(const gchar * s);

/* Test */
gboolean libbalsa_condition_can_match(LibBalsaCondition * cond,
				      LibBalsaMessage * message);
gboolean libbalsa_condition_is_flag_only(LibBalsaCondition * cond,
                                         LibBalsaMailbox * mailbox,
                                         guint msgno, gboolean * match);

#endif				/* __FILTER_H__ */
