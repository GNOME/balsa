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
#include <stdio.h>
/* yellow dog has crappy libc and needs pthread.h to be included here */
#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#endif
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include "filter-file.h"
#include "filter-private.h"
#include "filter-funcs.h"
#include "mailbox-filter.h"
#include <gnome.h>


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
