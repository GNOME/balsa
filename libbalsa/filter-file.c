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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
/*

 * filter-file.c
 *
 * File functions of the mail filter portion of balsa.
 *
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "filter-file.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include "filter-private.h"
#include "filter-funcs.h"
#include "libbalsa-conf.h"
#include "mailbox-filter.h"
#include <glib/gi18n.h>

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
    newf->name          = libbalsa_conf_get_string("Name");
    p = str             = libbalsa_conf_get_string("Condition");
    newf->sound         = libbalsa_conf_get_string("Sound");
    newf->popup_text    = libbalsa_conf_get_string("Popup-text");
    newf->action        = libbalsa_conf_get_int("Action-type");
    newf->action_string = libbalsa_conf_get_string("Action-string");
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
    libbalsa_conf_set_string("Name",          fil->name);
    libbalsa_conf_set_string("Condition",     str);
    libbalsa_conf_set_string("Sound",         fil->sound);
    libbalsa_conf_set_string("Popup-text",    fil->popup_text);
    libbalsa_conf_set_int("Action-type",      fil->action);
    libbalsa_conf_set_string("Action-string", fil->action_string);
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
    libbalsa_conf_get_vector_with_default(MAILBOX_FILTERS_KEY,&nb_filters,
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
	libbalsa_conf_get_vector_with_default(MAILBOX_FILTERS_WHEN_KEY,
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
    libbalsa_conf_set_vector(MAILBOX_FILTERS_KEY,nb_filters,
                            (const gchar**)filters_names);

    fil=mbox->filters;
    for (i=0;i<nb_filters;i++) {
	filters_names[i]=
            g_strdup_printf("%d",
                            ((LibBalsaMailboxFilter*)fil->data)->when);
	fil=g_slist_next(fil);
    }
    libbalsa_conf_set_vector(MAILBOX_FILTERS_WHEN_KEY,nb_filters,
                            (const gchar**)filters_names);
    for (i=0;i<nb_filters;i++)
	g_free(filters_names[i]);
    g_free(filters_names);
}

/* FIXME: #ifdef HAVE_GNOME ?? */
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

    newc->type = libbalsa_conf_get_int("Type");
    newc->negate = libbalsa_conf_get_bool("Condition-not");
    fields = libbalsa_conf_get_int("Match-fields");

    switch (newc->type) {
    case CONDITION_STRING:
	newc->match.string.fields = fields;
	newc->match.string.string =
	    libbalsa_conf_get_string("Match-string");
	newc->match.string.user_header =
	    CONDITION_CHKMATCH(newc, CONDITION_MATCH_US_HEAD) ?
	    libbalsa_conf_get_string("User-header") : NULL;
	break;
    case CONDITION_REGEX:
	newc->match.regex.fields = fields;
#if 0
	libbalsa_conf_get_vector("Reg-exps", &nbregexs, &regexs);
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
	str = libbalsa_conf_get_string("Low-date");
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
	str = libbalsa_conf_get_string("High-date");
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
	newc->match.flags = libbalsa_conf_get_int("Flags");
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

struct lbc_new_info {
    GList *tmp_list;
    gint err;
};

static gboolean
lbc_new_helper(const gchar * key, const gchar * value, gpointer data)
{
    struct lbc_new_info *info = data;
    LibBalsaCondition *cond;

    libbalsa_conf_push_group(key);

    cond = libbalsa_condition_new_from_config();
    if (cond) {
        if (filter_errno == FILTER_EFILESYN) {
            /* We don't stop the process for syntax error, we
             * just discard the malformed condition we also
             * remember (with err) that a syntax error occurs
             */
            info->err = FILTER_EFILESYN;
            filter_errno = FILTER_NOERR;
            libbalsa_condition_unref(cond);
        } else {
            LibBalsaTempCondition *tmp_cond;

            tmp_cond = g_new(LibBalsaTempCondition, 1);
            tmp_cond->cnd = cond;
            tmp_cond->order = atoi(strrchr(key, ':') + 1);
            info->tmp_list = g_list_prepend(info->tmp_list, tmp_cond);
        }
    }

    libbalsa_conf_pop_group();

    return filter_errno != FILTER_NOERR;
}

LibBalsaCondition *
libbalsa_condition_new_2_0(const gchar * filter_section_name,
                           ConditionMatchType cmt)
{
    struct lbc_new_info info;
    gchar *section_prefix;
    LibBalsaCondition *cond_2_0 = NULL;
    GList *l;

    info.tmp_list = NULL;
    info.err = filter_errno = FILTER_NOERR;

    section_prefix =
        g_strconcat(CONDITION_SECTION_PREFIX, filter_section_name, ":",
                    NULL);
    libbalsa_conf_foreach_group(section_prefix, lbc_new_helper, &info);
    g_free(section_prefix);

    /* We position filter_errno to the last non-critical error */
    if (filter_errno == FILTER_NOERR) {
        filter_errno = info.err;
        /* We sort the list of temp conditions, then
           we create the combined condition. */
        info.tmp_list =
            g_list_sort(info.tmp_list, compare_conditions_order);
        l = info.tmp_list;
        for (; info.tmp_list; info.tmp_list = info.tmp_list->next) {
            LibBalsaTempCondition *tmp =
                (LibBalsaTempCondition *) (info.tmp_list->data);
            LibBalsaCondition *res = 
                libbalsa_condition_new_bool_ptr(FALSE, cmt, tmp->cnd,
                                                cond_2_0);
            libbalsa_condition_unref(tmp->cnd);
            libbalsa_condition_unref(cond_2_0);
            cond_2_0 = res;
            g_free(tmp);
        }
        g_list_free(l);
    }
    /* else we leak the list and structures?? */
    return cond_2_0;
}
/* #endif *//* HAVE_GNOME */
