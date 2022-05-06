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

#ifndef __LIBBALSA_PRIVATE_H__
#define __LIBBALSA_PRIVATE_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include <unistd.h>

/* LibBalsaMailboxEntry handling code which is to be used for message
 * index caching.  Mailbox index entry used for caching (almost) all
 * columns provided by GtkTreeModel interface. Size matters. */
struct LibBalsaMailboxIndexEntry_ {
    gchar *from;
    gchar *subject;
    time_t msg_date;
    time_t internal_date;
    unsigned short status_icon;
    unsigned short attach_icon;
    gint64 size;
    gchar *foreground;
    gchar *background;
    unsigned foreground_set:1;
    unsigned background_set:1;
    unsigned unseen:1;
    unsigned idle_pending:1;
} ;

void libbalsa_lock_mailbox(LibBalsaMailbox * mailbox);
void libbalsa_unlock_mailbox(LibBalsaMailbox * mailbox);

#endif				/* __LIBBALSA_PRIVATE_H__ */
