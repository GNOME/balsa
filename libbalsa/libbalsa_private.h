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


/* private interfaces to avoid the libbalsa API */
/* address.c: */
gchar *libbalsa_address_to_gchar_p(LibBalsaAddress * address, gint n);
/* misc.c: */
gchar *libbalsa_make_string_from_list_p(const GList * the_list);


#ifdef BALSA_USE_THREADS
#include <pthread.h>
extern pthread_mutex_t mailbox_lock;
void libbalsa_lock_mailbox(LibBalsaMailbox * mailbox);
void libbalsa_unlock_mailbox(LibBalsaMailbox * mailbox);

#define LOCK_MAILBOX(mailbox) libbalsa_lock_mailbox(mailbox)
#define LOCK_MAILBOX_RETURN_VAL(mailbox, val) libbalsa_lock_mailbox(mailbox)
#define UNLOCK_MAILBOX(mailbox) libbalsa_unlock_mailbox(mailbox)

#define HAVE_MAILBOX_LOCKED(mailbox) ((mailbox)->lock > 0 && (mailbox)->thread_id == pthread_self())

#else

/* Non-threaded locking mechanism */
#define LOCK_MAILBOX(mailbox)\
do {\
  if (mailbox->lock)\
    {\
      g_print (_("*** ERROR: Mailbox Lock Exists: %s ***\n"), __PRETTY_FUNCTION__);\
      return;\
    }\
  else\
    mailbox->lock++;\
} while (0)


#define LOCK_MAILBOX_RETURN_VAL(mailbox, val)\
do {\
  if (mailbox->lock)\
    {\
      g_print (_("*** ERROR: Mailbox Lock Exists: %s ***\n"), __PRETTY_FUNCTION__);\
      return (val);\
    }\
  else\
    mailbox->lock++;\
} while (0)

#define UNLOCK_MAILBOX(mailbox)          ((mailbox)->lock--)

#define MAILBOX_IS_LOCKED(mailbox) ((mailbox)->lock > 0)

#endif


#endif				/* __LIBBALSA_PRIVATE_H__ */
