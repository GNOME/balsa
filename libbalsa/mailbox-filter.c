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
 * mailbox_filter.h
 * 
 * Header defining filters associated to mailbox
 * Basically it's a filter plus fields related to automatic running
 * Author : Emmanuel Allaud
 */


#include "mailbox-filter.h"

/* Returns a slist of filters having the corresponding when field
 * There is no copy, the new list references object of the source list
 */

GSList* 
libbalsa_mailbox_filters_when(GSList * filters, gint when)
{
    GSList * lst=NULL;

    for (; filters; filters=g_slist_next(filters))
	if (((LibBalsaMailboxFilter*)filters->data)->when==when) {
	    lst=g_slist_prepend(lst,((LibBalsaMailboxFilter*)filters->data)->actual_filter);
	}
    lst=g_slist_reverse(lst);
}
