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
 * mailbox_filter.h
 * 
 * Header defining filters associated to mailbox
 * Basically it's a filter plus fields related to automatic running
 * Author : Emmanuel Allaud
 */

#include "config.h"
#include <string.h>

#ifdef HAVE_GETTEXT
#include <libintl.h>
#ifndef _
#define _(x)  gettext(x)
#endif
#else
#define _(x)  (x)
#endif
#define N_(x) (x)

#include "filter-file.h"
#include "libbalsa-conf.h"
#include "mailbox-filter.h"
#include "misc.h"

/* FIXME : double definition : first is in save-restore.c */
#define BALSA_CONFIG_PREFIX "balsa/"

/* Returns a slist of filters having the corresponding when field
 * There is no copy, the new list references object of the source list
 */

GSList* 
libbalsa_mailbox_filters_when(GSList * filters, gint when)
{
    GSList * lst = NULL;
    for (; filters; filters = g_slist_next(filters))
	if (FILTER_WHEN_CHKFLAG((LibBalsaMailboxFilter*)filters->data,when))
	    lst = g_slist_prepend(lst,((LibBalsaMailboxFilter*)filters->data)->actual_filter);
    lst = g_slist_reverse(lst);

    return lst;
}

/* Looks for a mailbox filters section with MBOX_URL field equals to mbox->url
 * returns the section name or NULL if none found
 * The returned string has to be freed by the caller
 */

gchar*
mailbox_filters_section_lookup(const gchar * name)
{
    gchar * key, *section = NULL;
    void * iterator;

    g_return_val_if_fail(name && name[0],NULL);
    iterator = libbalsa_conf_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while (!section &&
	   (iterator = libbalsa_conf_iterator_next(iterator, &key, NULL))) {
	if (libbalsa_str_has_prefix(key, MAILBOX_FILTERS_SECTION_PREFIX)) {
	    gchar *url;

	    section = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);
	    libbalsa_conf_push_prefix(section);
	    url = libbalsa_conf_get_string(MAILBOX_FILTERS_URL_KEY);
	    libbalsa_conf_pop_prefix();
	    if (strcmp(url, name) != 0) {
		g_free(section);
		section = NULL;
	    }
	    g_free(url);
	}
	g_free(key);
    }
    g_free(iterator);
    return section;
}

void
config_mailbox_filters_load(LibBalsaMailbox * mbox)
{
    gchar * section;

    section = mailbox_filters_section_lookup(mbox->url ? mbox->url : mbox->name);
    if (section) {
	libbalsa_conf_push_prefix(section);
	g_free(section);
	libbalsa_mailbox_filters_load_config(mbox);
	libbalsa_conf_pop_prefix();
    }
}
