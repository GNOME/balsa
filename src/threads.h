/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#ifndef __THREADS_H__
#define __THREADS_H__

/*  define thread globals */
extern  pthread_t               get_mail_thread;
extern  pthread_t               send_mail;
extern  pthread_mutex_t         mailbox_lock;
extern  pthread_mutex_t         send_messages_lock;
extern  int                     checking_mail;
extern  int                     sending_mail;
extern  int                     mail_thread_pipes[2];
extern  int                     send_thread_pipes[2];
extern  GIOChannel              *mail_thread_msg_send;
extern  GIOChannel              *mail_thread_msg_receive;
extern  GIOChannel              *send_thread_msg_send;
extern  GIOChannel              *send_thread_msg_receive;


typedef struct
{
  int message_type;
  char message_string[160];
  Mailbox *mailbox;
  int num_bytes;
  int tot_bytes;
} MailThreadMessage;
  
/*
 *  Macro to send message:
 *    message is MailThreadMessage *message
 *    type is one of the defined message types below
 *    string is string to send
 */

#define  MSGMAILTHREAD( message, type, mbox, string, num, tot) \
  message = malloc( sizeof( MailThreadMessage )); \
  message->message_type = type; \
  message->mailbox = mbox; \
  strncpy( message->message_string, string, strlen(string) + 1 ); \
  message->num_bytes=num;\
  message->tot_bytes=tot;\
  write( mail_thread_pipes[1], (void *) &message, sizeof(void *) );

enum {
  MSGMAILTHREAD_SOURCE,
  MSGMAILTHREAD_MSGINFO,
  MSGMAILTHREAD_UPDATECONFIG,
  MSGMAILTHREAD_ERROR,
  MSGMAILTHREAD_LOAD,
  MSGMAILTHREAD_FINISHED,
  MSGMAILTHREAD_PROGRESS
};

typedef struct
{
  int message_type;
  char message_string[256];
  HEADER *msg;
  Mailbox *mbox;
} SendThreadMessage;

#define  MSGSENDTHREAD (t_message, type, string, s_msg, s_mbox) \
  t_message = malloc( sizeof( SendThreadMessage )); \
  t_message->message_type = type; \
  strncpy(t_message->message_string, string, sizeof(message->message_string)); \
  t_message->msg = s_msg; \
  t_message->mbox = s_mbox; \
  write( send_thread_pipes[1], (void *) &t_message, sizeof(void *) );

enum {
  MSGSENDTHREADERROR,
  MSGSENDTHREADPOSTPONE,
  MSGSENDTHREADLOAD
};

#endif /* __THREADS_H__ */
