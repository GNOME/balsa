#ifndef __THREADS_H__
#define __THREADS_H__

/*  define thread globals */
pthread_t			get_mail_thread;
pthread_mutex_t			mailbox_lock;
int				checking_mail;
int				mail_thread_pipes[2];
GIOChannel 		*mail_thread_msg_send;
GIOChannel 		*mail_thread_msg_receive;

#define  MSGMAILTHREAD_SOURCE 		0x0001
#define  MSGMAILTHREAD_MSGINFO		0x0002
#define  MSGMAILTHREAD_UPDATECONFIG	0x0003
#define	 MSGMAILTHREAD_ERROR		0x0004
#define  MSGMAILTHREAD_LOAD			0x0005

#endif /* __THREADS_H__ */
