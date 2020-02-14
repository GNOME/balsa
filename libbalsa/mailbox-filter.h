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

#ifndef __MAILBOX_FILTER_H__
#define __MAILBOX_FILTER_H__

#include "filter.h"

#define MAILBOX_FILTERS_SECTION_PREFIX "filters-mailbox-"
#define MAILBOX_FILTERS_URL_KEY "Mailbox-URL"

/*
 * Defines for the when field of mailbox controlling when to automatically run associated filters of a mailbox
 * For now : on incoming mails, before closing a mailbox (to have something like a purge mechanism)
 * FIXME : we need surely more than that
 * We should be able to remember the last message in the mailbox that has been filtered (so that automatic filtering
 * leads to apply filters only on those messages in the mailbox that have never been filtered before)
 */

#define FILTER_WHEN_NEVER    0          /* Ie manual only */
#define FILTER_WHEN_INCOMING 1 << 0
#define FILTER_WHEN_CLOSING  1 << 1

#define FILTER_WHEN_NB       2 /* How many activation types */

/* "filter when" operation macros */
#define FILTER_WHEN_SETFLAG(x, y) (((x)->when) |= (y))
#define FILTER_WHEN_CLRFLAG(x, y) (((x)->when) &= ~(y))
#define FILTER_WHEN_CHKFLAG(x, y) (((x)->when) & (y))

typedef struct _LibBalsaMailboxFilter {
    LibBalsaFilter* actual_filter;
    gint when;
} LibBalsaMailboxFilter;

/* Returns a slist of filters having the corresponding when field */

GSList * libbalsa_mailbox_filters_when(GSList * filters, gint when);

/* Loads the filters associated to the mailbox */
void config_mailbox_filters_load(LibBalsaMailbox * mbox);
/* Saves the filters associated to the mailbox */
gchar * mailbox_filters_section_lookup(const gchar * url);

#endif				/* __MAILBOX_FILTER_H__ */
