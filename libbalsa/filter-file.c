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

 * filter-file.c
 *
 * File functions of the mail filter portion of balsa.
 *
 */

#include "config.h"

/* define _XOPEN_SOURCE to make strptime visible */
#define _XOPEN_SOURCE
/* extensions  needed additonally on Solaris for strptime */
#define __EXTENSIONS__
#include <stdlib.h>
#include <stdio.h>
/* yellow dog has crappy libc and needs pthread.h to be included here */
#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#endif
#include <string.h>
#include <time.h>

#include <libgnome/libgnome.h>

#include <sys/types.h>
#include "filter-file.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include "mailbox-filter.h"

/* Load the header of a filter filter (you have to separately load the
 * associated conditions) Filter is marked as invalid Position
 * filter_errno.
 */

LibBalsaFilter*
libbalsa_filter_new_from_config(void)
{
    LibBalsaFilter * newf = libbalsa_filter_new();
    gchar *str, *p;
    /* First we load the fixed part of the filter */
    newf->name          = gnome_config_get_string("Name");
    p = str             = gnome_config_get_string("Condition");
    newf->sound         = gnome_config_get_string("Sound");
    newf->popup_text    = gnome_config_get_string("Popup-text");
    newf->action        = gnome_config_get_int("Action-type");
    newf->action_string = gnome_config_get_string("Action-string");
    newf->condition     = libbalsa_condition_new_from_string(&p);
    g_free(str);
    if (newf->sound[0]=='\0') {
	g_free(newf->sound);
	newf->sound=NULL;
    }
    if (newf->popup_text[0]=='\0') {
	g_free(newf->popup_text);
	newf->popup_text=NULL;
    }

    return newf;
}

/*
 * libbalsa_conditions_save_config saves a list of conditions using
 * prefix and filter_section_name to create the name of the section We
 * clean all preceding saved conditions to keep the config file clean
 * and coherent. */

/*
 * void libbalsa_filter_save_config(filter * f)
 *
 * Saves the filter into the config file.
 *
 * Arguments:
 *    filter * filter - the filter to save
 *
 */

void
libbalsa_filter_save_config(LibBalsaFilter * fil)
{
    gchar *str = libbalsa_condition_to_string(fil->condition);
    gnome_config_set_string("Name",          fil->name);
    gnome_config_set_string("Condition",     str);
    gnome_config_set_string("Sound",         fil->sound);
    gnome_config_set_string("Popup-text",    fil->popup_text);
    gnome_config_set_int("Action-type",      fil->action);
    gnome_config_set_string("Action-string", fil->action_string);
    g_free(str);
}

/* Loads the filters associated to the mailbox
 * Note : does not clean current filters list (normally the caller did it)
 * Position filter_errno
 */

void
libbalsa_mailbox_filters_load_config(LibBalsaMailbox* mbox)
{
    gint i,nb_filters;
    gchar **filters_names=NULL;
    LibBalsaFilter* fil;
    gboolean def;
    GSList * lst;

    /* We load the associated filters */
    gnome_config_get_vector_with_default(MAILBOX_FILTERS_KEY,&nb_filters,
					 &filters_names,&def);
    if (!def) {
	for(i=0;i<nb_filters;i++) {
	    fil = libbalsa_filter_get_by_name(filters_names[i]);
	    if (fil) {
		LibBalsaMailboxFilter* mf = g_new(LibBalsaMailboxFilter,1);

		mf->actual_filter = fil;
		mbox->filters=g_slist_prepend(mbox->filters, mf);
	    }
	    else
		libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				     _("Invalid filters %s for mailbox %s"),
                                     filters_names[i], mbox->name);
	}
	mbox->filters=g_slist_reverse(mbox->filters);
    }
    g_strfreev(filters_names);
    if (!def) {
	gnome_config_get_vector_with_default(MAILBOX_FILTERS_WHEN_KEY,
                                             &nb_filters,&filters_names,&def);
	if (def)
	    for(lst=mbox->filters;lst;lst=g_slist_next(lst))
		FILTER_WHEN_SETFLAG((LibBalsaMailboxFilter*)lst->data,
				    FILTER_WHEN_NEVER);
	else {
	    lst=mbox->filters;
	    for (i=0;i<nb_filters && lst;i++) {
		((LibBalsaMailboxFilter*)lst->data)->when = 
                    atoi(filters_names[i]);
		lst=g_slist_next(lst);
	    }
	}
	g_strfreev(filters_names);
    }
    filter_errno=FILTER_NOERR;
}

/* Saves the filters associated to the mailbox
 */

void
libbalsa_mailbox_filters_save_config(LibBalsaMailbox * mbox)
{
    gint i,nb_filters=0;
    gchar ** filters_names;
    GSList * fil,* names=NULL,* lst;

    /* First we construct a list containing the names of associated filters
     * Note : in all the following we never copy the filters name, so we don't have to (and me must not!) free any gchar *
     * That's why we only free g_slist and gchar **
     */
    for(fil=mbox->filters;fil;fil=g_slist_next(fil)) {
	names=g_slist_prepend(names,
                              ((LibBalsaMailboxFilter*)fil->data)->actual_filter->name);
	nb_filters++;
    }
    names=g_slist_reverse(names);
    /* Second we construct the vector of gchar * */
    filters_names=g_new(gchar *,nb_filters);
    lst=names;
    for(i=0; i<nb_filters; i++) {
	filters_names[i]=(gchar*)lst->data;
	lst=g_slist_next(lst);
    }
    g_slist_free(names);
    gnome_config_set_vector(MAILBOX_FILTERS_KEY,nb_filters,
                            (const gchar**)filters_names);

    fil=mbox->filters;
    for (i=0;i<nb_filters;i++) {
	filters_names[i]=
            g_strdup_printf("%d",
                            ((LibBalsaMailboxFilter*)fil->data)->when);
	fil=g_slist_next(fil);
    }
    gnome_config_set_vector(MAILBOX_FILTERS_WHEN_KEY,nb_filters,
                            (const gchar**)filters_names);
    for (i=0;i<nb_filters;i++)
	g_free(filters_names[i]);
    g_free(filters_names);
}

/* Temporary code for transition from 2.0.x */
static LibBalsaCondition *
libbalsa_condition_new_from_config()
{
    LibBalsaCondition *newc;
#if 0
    gchar **regexs;
    gint nbregexs, i;
    LibBalsaConditionRegex *newreg;
#endif
    struct tm date;
    gchar *str, *p;
    unsigned fields;

    newc = libbalsa_condition_new();

    newc->type = gnome_config_get_int("Type");
    newc->negate = gnome_config_get_bool("Condition-not");
    fields = gnome_config_get_int("Match-fields");

    switch (newc->type) {
    case CONDITION_STRING:
	newc->match.string.fields = fields;
	newc->match.string.string =
	    gnome_config_get_string("Match-string");
	newc->match.string.user_header =
	    CONDITION_CHKMATCH(newc, CONDITION_MATCH_US_HEAD) ?
	    gnome_config_get_string("User-header") : NULL;
	break;
    case CONDITION_REGEX:
	newc->match.regex.fields = fields;
#if 0
	gnome_config_get_vector("Reg-exps", &nbregexs, &regexs);
	for (i = 0; i < nbregexs && (filter_errno == FILTER_NOERR); i++) {
	    newreg = g_new(LibBalsaConditionRegex, 1);
	    newreg->string = regexs[i];
	    newreg->compiled = NULL;
	    newc->match.regexs =
		g_slist_prepend(newc->match.regexs, newreg);
	}
	newc->match.regexs = g_slist_reverse(newc->match.regexs);
	/* Free the array of (gchar*)'s, but not the strings pointed by them */
	g_free(regexs);
#endif
	break;
    case CONDITION_DATE:
	str = gnome_config_get_string("Low-date");
	if (str[0] == '\0')
	    newc->match.date.date_low = 0;
	else {
	    (void) strptime("00:00:00", "%T", &date);
	    p = (gchar *) strptime(str, "%Y-%m-%d", &date);
	    if (!p || *p != '\0')
		filter_errno = FILTER_EFILESYN;
	    else
		newc->match.date.date_low = mktime(&date);
	}
	g_free(str);
	str = gnome_config_get_string("High-date");
	if (str[0] == '\0')
	    newc->match.date.date_high = 0;
	else {
	    (void) strptime("23:59:59", "%T", &date);
	    p = (gchar *) strptime(str, "%Y-%m-%d", &date);
	    if (!p || *p != '\0')
		filter_errno = FILTER_EFILESYN;
	    else
		newc->match.date.date_high = mktime(&date);
	}
	g_free(str);
	break;
    case CONDITION_FLAG:
	newc->match.flags = gnome_config_get_int("Flags");
	break;
    default:
	filter_errno = FILTER_EFILESYN;
    }

    return newc;
}

/* Load a list of conditions using prefix and filter_section_name to locate
 * the section in config file
 * Will correctly set filter flags
 * Position filter_errno
 */

/* Temporary struct used to ensure that we keep the same order for conditions
   as specified by the user */

typedef struct {
    LibBalsaCondition *cnd;
    gint order;
} LibBalsaTempCondition;

static gint
compare_conditions_order(gconstpointer a, gconstpointer b)
{
    const LibBalsaTempCondition *t1 = a;
    const LibBalsaTempCondition *t2 = b;
    return t2->order - t1->order;
}

#define CONDITION_SECTION_PREFIX "condition-"

LibBalsaCondition *
libbalsa_condition_new_2_0(gchar * prefix, gchar * filter_section_name,
			   ConditionMatchType cmt)
{
    LibBalsaCondition *cond;
    LibBalsaCondition *cond_2_0 = NULL;
    void *iterator;
    gchar *tmp, *condprefix, *key;
    gint pref_len =
	strlen(CONDITION_SECTION_PREFIX) + strlen(filter_section_name) + 1;
    gint err = FILTER_NOERR;
    GList *tmp_list = NULL;
    GList *l;

    tmp =
	g_strconcat(CONDITION_SECTION_PREFIX, filter_section_name, ":",
		    NULL);
    iterator = gnome_config_init_iterator_sections(prefix);
    filter_errno = FILTER_NOERR;

    while ((filter_errno == FILTER_NOERR) &&
	   (iterator = gnome_config_iterator_next(iterator, &key, NULL))) {

	if (strncmp(key, tmp, pref_len) == 0) {
	    condprefix = g_strconcat(prefix, key, "/", NULL);
	    gnome_config_push_prefix(condprefix);
	    g_free(condprefix);
	    cond = libbalsa_condition_new_from_config();
	    if (cond) {
		if (filter_errno == FILTER_EFILESYN) {
		    /* We don't stop the process for syntax error, we
		     * just discard the malformed condition we also
		     * remember (with err) that a syntax error occurs
		     */
		    err = FILTER_EFILESYN;
		    filter_errno = FILTER_NOERR;
		    libbalsa_condition_free(cond);
		} else {
		    LibBalsaTempCondition *tmp =
			g_new(LibBalsaTempCondition, 1);

		    tmp->cnd = cond;
		    tmp->order = atoi(strrchr(key, ':') + 1);
		    tmp_list = g_list_prepend(tmp_list, tmp);
		}
	    }
	    gnome_config_pop_prefix();
	}
	g_free(key);
    }
    g_free(tmp);
    /* We position filter_errno to the last non-critical error */
    if (filter_errno == FILTER_NOERR) {
	LibBalsaTempCondition *tmp;

	filter_errno = err;
	/* We sort the list of temp conditions, then
	   we create the combined condition. */
	tmp_list = g_list_sort(tmp_list, compare_conditions_order);
	l = tmp_list;
	for (; tmp_list; tmp_list = g_list_next(tmp_list)) {
	    tmp = (LibBalsaTempCondition *) (tmp_list->data);
	    cond_2_0 = cond_2_0 ?
		libbalsa_condition_new_bool_ptr(FALSE, cmt, tmp->cnd,
						cond_2_0) : tmp->cnd;
	    g_free(tmp);
	}
	g_list_free(l);
    } /* else we leak the list and structures?? */

    return cond_2_0;
}
