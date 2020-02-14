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
 * mailbox_filter.h
 * 
 * Header defining filters associated to mailbox
 * Basically it's a filter plus fields related to automatic running
 * Author : Emmanuel Allaud
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <string.h>

#include "filter-file.h"
#include "libbalsa-conf.h"
#include "mailbox-filter.h"
#include "misc.h"
#include <glib/gi18n.h>

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

/* Looks for a mailbox filters group with MBOX_URL field equals to mbox->url
 * returns the group name or NULL if none found
 * The returned string has to be freed by the caller
 */
struct lbmf_section_lookup_info {
    const gchar *name;
    gchar *group;
};

static gboolean
lbmf_section_lookup_func(const gchar * key, const gchar * value,
                         gpointer data)
{
    struct lbmf_section_lookup_info *info = data;
    gchar *url;

    libbalsa_conf_push_group(key);
    url = libbalsa_conf_get_string(MAILBOX_FILTERS_URL_KEY);
    libbalsa_conf_pop_group();
    if (strcmp(url, info->name) == 0)
        info->group = g_strdup(key);
    g_free(url);

    return info->group != NULL;
}

gchar *
mailbox_filters_section_lookup(const gchar * name)
{
    struct lbmf_section_lookup_info info;

    g_return_val_if_fail(name && name[0], NULL);

    info.name = name;
    info.group = NULL;
    libbalsa_conf_foreach_group(MAILBOX_FILTERS_SECTION_PREFIX,
                                  lbmf_section_lookup_func, &info);

    return info.group;
}

void
config_mailbox_filters_load(LibBalsaMailbox * mbox)
{
    const gchar *url;
    gchar * group;

    url = libbalsa_mailbox_get_url(mbox);
    group = mailbox_filters_section_lookup(url != NULL ? url :
                                           libbalsa_mailbox_get_name(mbox));
    if (group) {
	libbalsa_conf_push_group(group);
	g_free(group);
	libbalsa_mailbox_filters_load_config(mbox);
	libbalsa_conf_pop_group();
    }
}
