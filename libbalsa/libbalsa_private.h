#include <gnome.h>

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
