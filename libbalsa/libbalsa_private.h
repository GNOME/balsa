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

#ifndef __LIBBALSA_PRIVATE_H__
#define __LIBBALSA_PRIVATE_H__

#include <unistd.h>

/* LibBalsaMailboxEntry handling code which is to be used for message
 * intex caching.  Mailbox index entry used for caching (almost) all
 * columns provided by GtkTreeModel interface. Size matters. */
struct LibBalsaMailboxIndexEntry_ {
    gchar *from;
    gchar *subject;
    time_t msg_date;
    time_t internal_date;
    unsigned short status_icon;
    unsigned short attach_icon;
    unsigned long size;
    unsigned unseen:1;
#define CACHE_UNSEEN_CHILD FALSE
#if CACHE_UNSEEN_CHILD
    /* Code for managing this cached bit is incomplete; if calculating
     * has-unseen-child status on the fly is a performance hit, we'll
     * have to finish it. */
    unsigned has_unseen_child:1;
#endif /* CACHE_UNSEEN_CHILD */
} ;

#ifdef BALSA_USE_THREADS
#include <pthread.h>
extern pthread_mutex_t mailbox_lock;
void libbalsa_lock_mailbox(LibBalsaMailbox * mailbox);
void libbalsa_unlock_mailbox(LibBalsaMailbox * mailbox);
#else
# define libbalsa_lock_mailbox(m)
# define libbalsa_unlock_mailbox(m)
#endif

#endif				/* __LIBBALSA_PRIVATE_H__ */
