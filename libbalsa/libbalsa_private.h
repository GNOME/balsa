/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */

#include <gnome.h>

#include "libmutt/mutt.h"

#ifndef __LIBBALSA_PRIVATE_H__
#define __LIBBALSA_PRIVATE_H__

LibBalsaAddress* libbalsa_address_new_from_libmutt(ADDRESS *caddr);

#ifdef BALSA_USE_THREADS

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
  
#define CLIENT_CONTEXT(mailbox)          ((CONTEXT*)mailbox->context)
#define CLIENT_CONTEXT_OPEN(mailbox)     (CLIENT_CONTEXT (mailbox) != NULL)
#define CLIENT_CONTEXT_CLOSED(mailbox)   (CLIENT_CONTEXT (mailbox) == NULL)
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

#endif /* __LIBBALSA_PRIVATE_H__ */
