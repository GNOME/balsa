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

#include "libmutt/mutt.h"

LibBalsaAddress *libbalsa_address_new_from_libmutt(ADDRESS * caddr);
/* private interfaces to avoid the libbalsa API */
/* address.c: */
gchar *libbalsa_address_to_gchar_p(LibBalsaAddress * address, gint n);
/* misc.c: */
gchar *libbalsa_make_string_from_list_p(const GList * the_list);


#ifdef BALSA_USE_THREADS
#include <pthread.h>
extern pthread_mutex_t mailbox_lock;
/*  #define DEBUG */

#ifdef DEBUG
#define DMSG1(s) fprintf(stderr,s)
#define DMSG2(a,b) fprintf(stderr,a,b)
#else
#define DMSG1(s)
#define DMSG2(a,b)
#endif

#ifndef __GNUC__
#define __PRETTY_FUNCTION__	__FILE__
#endif

#define LOCK_MAILBOX(mailbox)\
do {\
  pthread_mutex_lock( &mailbox_lock );\
    if ( !mailbox->lock )\
    {\
	  DMSG2("Locking mailbox %s\n", mailbox->name );\
      mailbox->lock = TRUE;\
      pthread_mutex_unlock( &mailbox_lock );\
      break;\
    }\
  else\
    {\
      DMSG1("... Mailbox lock collision ..." );\
      pthread_mutex_unlock( &mailbox_lock );\
      usleep( 250 );\
    }\
  } while ( 1 )

#define LOCK_MAILBOX_RETURN_VAL(mailbox, val)\
do {\
  pthread_mutex_lock( &mailbox_lock );\
    if ( !mailbox->lock )\
    {\
	  DMSG1( "Locking mailbox \n" );\
      mailbox->lock = TRUE;\
      pthread_mutex_unlock( &mailbox_lock );\
      break;\
    }\
  else\
    {\
      DMSG1( "Mailbox lock collision \n" );\
      pthread_mutex_unlock( &mailbox_lock );\
      usleep( 250 );\
    }\
  } while ( 1 )

#define UNLOCK_MAILBOX(mailbox)\
do {\
  DMSG1( "Unlocking mailbox \n" );\
  pthread_mutex_lock( &mailbox_lock );\
  mailbox->lock = FALSE;\
  pthread_mutex_unlock( &mailbox_lock );\
}  while( 0 )
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
    mailbox->lock = TRUE;\
} while (0)


#define LOCK_MAILBOX_RETURN_VAL(mailbox, val)\
do {\
  if (mailbox->lock)\
    {\
      g_print (_("*** ERROR: Mailbox Lock Exists: %s ***\n"), __PRETTY_FUNCTION__);\
      return (val);\
    }\
  else\
    mailbox->lock = TRUE;\
} while (0)

#define UNLOCK_MAILBOX(mailbox)          mailbox->lock = FALSE;

#endif

#define CLIENT_CONTEXT(mailbox)          (mailbox->context)
#define CLIENT_CONTEXT_OPEN(mailbox)     (CLIENT_CONTEXT (mailbox) != NULL)
#define CLIENT_CONTEXT_CLOSED(mailbox) \
  (CLIENT_CONTEXT (mailbox) == NULL||CLIENT_CONTEXT (mailbox)->hdrs==NULL)
#define RETURN_IF_CLIENT_CONTEXT_CLOSED(mailbox)\
do {\
  if (CLIENT_CONTEXT_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      UNLOCK_MAILBOX (mailbox);\
      return;\
    }\
} while (0)
#define RETURN_VAL_IF_CONTEXT_CLOSED(mailbox, val)\
do {\
  if (CLIENT_CONTEXT_CLOSED (mailbox))\
    {\
      g_print (_("*** ERROR: Mailbox Stream Closed: %s ***\n"), __PRETTY_FUNCTION__);\
      UNLOCK_MAILBOX (mailbox);\
      return (val);\
    }\
} while (0)

#endif				/* __LIBBALSA_PRIVATE_H__ */
