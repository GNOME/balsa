#include <gnome.h>

#define LOCK_MAILBOX(mailbox)\
do {\
  pthread_mutex_lock( &mailbox_lock );\
    if ( !mailbox->lock )\
    {\
	  fprintf( stderr, "Locking mailbox %s\n", mailbox->name );\
      mailbox->lock = TRUE;\
      pthread_mutex_unlock( &mailbox_lock );\
      break;\
    }\
  else\
    {\
      fprintf( stderr, "... Mailbox lock collision ..." );\
      pthread_mutex_unlock( &mailbox_lock );\
      usleep( 250 );\
    }\
  } while ( 1 )
  
#define LOCK_MAILBOX_RETURN_VAL(mailbox, val)\
do {\
  pthread_mutex_lock( &mailbox_lock );\
    if ( !mailbox->lock )\
    {\
	  fprintf( stderr, "Locking mailbox \n" );\
      mailbox->lock = TRUE;\
      pthread_mutex_unlock( &mailbox_lock );\
      break;\
    }\
  else\
    {\
      fprintf( stderr, "Mailbox lock collision \n" );\
      pthread_mutex_unlock( &mailbox_lock );\
      usleep( 250 );\
    }\
  } while ( 1 )

#define UNLOCK_MAILBOX(mailbox)\
do {\
  fprintf(stderr, "Unlocking mailbox \n" );\
  pthread_mutex_lock( &mailbox_lock );\
  mailbox->lock = FALSE;\
  pthread_mutex_unlock( &mailbox_lock );\
}  while( 0 )
  
  

#define CLIENT_CONTEXT(mailbox)          (((MailboxPrivate *)((mailbox)->private))->context)
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


/* 
 * private mailbox data
 */
typedef struct
  {
    CONTEXT *context;
    GList *watcher_list;
  }
MailboxPrivate;
