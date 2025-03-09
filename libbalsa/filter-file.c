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
    gint i, nb_filters;
    gchar **filters_names = NULL;
    gboolean def;
    GSList *lst;
    GSList *filters;

    filters = libbalsa_mailbox_get_filters(mbox);

    /* We load the associated filters */
    libbalsa_conf_get_vector_with_default(MAILBOX_FILTERS_KEY,
                                          &nb_filters, &filters_names, &def);
    if (!def) {
	for(i = 0; i < nb_filters; i++) {
            LibBalsaFilter *fil;

	    fil = libbalsa_filter_get_by_name(filters_names[i]);
	    if (fil != NULL) {
		LibBalsaMailboxFilter *mf = g_new(LibBalsaMailboxFilter, 1);

		mf->actual_filter = fil;
                mf->when = FILTER_WHEN_NEVER; /* 0 */
		filters = g_slist_prepend(filters, mf);
	    }
	    else
		libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				     _("Invalid filters %s for mailbox %s"),
                                     filters_names[i], libbalsa_mailbox_get_name(mbox));
	}
	filters = g_slist_reverse(filters);
    }
    g_strfreev(filters_names);

    if (!def) {
	libbalsa_conf_get_vector_with_default(MAILBOX_FILTERS_WHEN_KEY,
                                              &nb_filters, &filters_names, &def);
	if (def) {
	    for(lst = filters; lst != NULL; lst = lst->next)
		FILTER_WHEN_SETFLAG((LibBalsaMailboxFilter*)lst->data,
				    FILTER_WHEN_NEVER);
        } else {
	    lst = filters;
	    for (i = 0; i < nb_filters && lst != NULL; i++) {
		LibBalsaMailboxFilter *mf = lst->data;

                mf->when = atoi(filters_names[i]);
		lst = lst->next;
	    }
	}
	g_strfreev(filters_names);
	libbalsa_mailbox_set_filters(mbox, filters);
    }
    filter_errno=FILTER_NOERR;
}

/* Saves the filters associated to the mailbox
 */

void
libbalsa_mailbox_filters_save_config(LibBalsaMailbox * mbox)
{
    GSList *filters = libbalsa_mailbox_get_filters(mbox);
    GPtrArray *names;
    GSList *fil;

    names = g_ptr_array_new();

    for (fil = filters; fil != NULL; fil = fil->next)
        g_ptr_array_add(names, ((LibBalsaMailboxFilter*)fil->data)->actual_filter->name);
    libbalsa_conf_set_vector(MAILBOX_FILTERS_KEY, names->len, (const char **) names->pdata);

    g_ptr_array_set_size(names, 0);
    g_ptr_array_set_free_func(names, g_free);

    for (fil = filters; fil != NULL; fil = fil->next)
        g_ptr_array_add(names, g_strdup_printf("%d", ((LibBalsaMailboxFilter*)fil->data)->when));
    libbalsa_conf_set_vector(MAILBOX_FILTERS_WHEN_KEY, names->len, (const char **) names->pdata);

    g_ptr_array_free(names, TRUE);
}
