#ifndef __THREAD_MSGS_H__
#define __THREAD_MSGS_H__

extern int    mail_thread_pipes[2];

typedef struct
{
  int message_type;
  char message_string[160];
  int *mailbox; /* Mailbox *   */
} MailThreadMessage;
  
#define  MSGMAILTHREAD( message, type, string) \
  message = malloc( sizeof( MailThreadMessage )); \
  message->message_type = type; \
  memcpy( message->message_string, string, strlen(string) + 1 ); \
  write( mail_thread_pipes[1], (void *) &message, sizeof(void *) );

#define  MSGMAILTHREAD_SOURCE           0x0001
#define  MSGMAILTHREAD_MSGINFO          0x0002
#define  MSGMAILTHREAD_UPDATECONFIG     0x0003
#define	 MSGMAILTHREAD_ERROR            0x0004
#define  MSGMAILTHREAD_LOAD             0x0005
#define  MSGMAILTHREAD_FINISHED         0x0006

#endif /* __THREAD_MSGS_H__ */
