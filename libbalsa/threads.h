#ifndef __THREADS_H__
#define __THREADS_H__

/*
 * thread globals
 */

extern pthread_t              send_mail;
extern pthread_mutex_t        mailbox_lock;
extern pthread_mutex_t        send_messages_lock;
extern int                    checking_mail;
extern int                    sending_mail;
extern int                    mail_thread_pipes[2];
extern int                    send_thread_pipes[2];
extern GIOChannel             *mail_thread_msg_send;
extern GIOChannel             *mail_thread_msg_receive;
extern GIOChannel             *send_thread_msg_send;
extern GIOChannel             *send_thread_msg_receive;

typedef struct
{
  int message_type;
  char message_string[160];
  int *mailbox; /*  Mailbox *  */
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

typedef struct
{
  int message_type;
  char message_string[256];
  HEADER *msg;
  Mailbox *mbox;
} SendThreadMessage;

#define  MSGSENDTHREAD(t_message, type, string, s_msg, s_mbox) \
  t_message = malloc( sizeof( SendThreadMessage )); \
  t_message->message_type = type; \
  strncpy(t_message->message_string, string, sizeof(t_message->message_string)); \
  t_message->msg = s_msg; \
  t_message->mbox = s_mbox; \
  write( send_thread_pipes[1], (void *) &t_message, sizeof(void *) );

#define  MSGSENDTHREADERROR             0x0001
#define  MSGSENDTHREADPOSTPONE          0x0002
#define  MSGSENDTHREADLOAD              0x0003

#endif /* __THREADS_H__ */

