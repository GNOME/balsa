#ifndef __THREADS_H__
#define __THREADS_H__

/*  define thread globals */
extern  pthread_t               get_mail_thread;
extern  pthread_mutex_t         mailbox_lock;
extern  int                     checking_mail;
extern  int                     mail_thread_pipes[2];
extern  GIOChannel              *mail_thread_msg_send;
extern  GIOChannel              *mail_thread_msg_receive;

typedef struct
{
  int message_type;
  char message_string[160];
  Mailbox *mailbox;
} MailThreadMessage;
  
/*
 *  Macro to send message:
 *    message is MailThreadMessage *message
 *    type is one of the defined message types below
 *    string is string to send
 */

#define  MSGMAILTHREAD( message, type, string) \
  message = malloc( sizeof( MailThreadMessage )); \
  message->message_type = type; \
  strncpy( message->message_string, string, strlen(string) + 1 ); \
  write( mail_thread_pipes[1], (void *) &message, sizeof(void *) );

#define  MSGMAILTHREAD_SOURCE           0x0001
#define  MSGMAILTHREAD_MSGINFO          0x0002
#define  MSGMAILTHREAD_UPDATECONFIG     0x0003
#define	 MSGMAILTHREAD_ERROR            0x0004
#define  MSGMAILTHREAD_LOAD             0x0005

#endif /* __THREADS_H__ */
