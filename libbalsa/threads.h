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

#ifndef __LIBBALSA_THREADS_H__
#define __LIBBALSA_THREADS_H__

#include <stdlib.h>		/* for malloc() */

/*
 * thread globals
 */

extern pthread_t send_mail;
extern pthread_mutex_t mailbox_lock;
extern pthread_mutex_t send_messages_lock;
extern int checking_mail;
extern int sending_mail;
extern int mail_thread_pipes[2];
extern int send_thread_pipes[2];
extern GIOChannel *mail_thread_msg_send;
extern GIOChannel *mail_thread_msg_receive;
extern GIOChannel *send_thread_msg_send;
extern GIOChannel *send_thread_msg_receive;
extern GtkWidget *send_progress;
extern GtkWidget *send_progress_message;
extern GtkWidget *send_dialog;
extern GtkWidget *send_dialog_bar;

typedef struct {
    int message_type;
    char message_string[160];
    int *mailbox;		/* Mailbox *   */
    int num_bytes, tot_bytes;
} MailThreadMessage;

#define  MSGMAILTHREAD( message, type, string) \
  message = malloc( sizeof( MailThreadMessage )); \
  message->message_type = type; \
  memcpy( message->message_string, string, strlen(string) + 1 ); \
  write( mail_thread_pipes[1], (void *) &message, sizeof(void *) );

enum {
    MSGMAILTHREAD_SOURCE,
    MSGMAILTHREAD_MSGINFO,
    MSGMAILTHREAD_UPDATECONFIG,
    MSGMAILTHREAD_ERROR,
    MSGMAILTHREAD_FINISHED,
    MSGMAILTHREAD_PROGRESS
};

typedef struct {
    int message_type;
    char message_string[256];
    LibBalsaMessage *msg;
    LibBalsaMailbox *mbox;
    float of_total;
} SendThreadMessage;

#define  MSGSENDTHREAD(t_message, type, string, s_msg, s_mbox, messof) \
  t_message = malloc( sizeof( SendThreadMessage )); \
  t_message->message_type = type; \
  strncpy(t_message->message_string, string, sizeof(t_message->message_string)); \
  t_message->msg = s_msg; \
  t_message->mbox = s_mbox; \
  t_message->of_total = messof; \
  write( send_thread_pipes[1], (void *) &t_message, sizeof(void *) );

enum {
    MSGSENDTHREADERROR,
    MSGSENDTHREADPROGRESS,
    MSGSENDTHREADPOSTPONE,
    MSGSENDTHREADDELETE,
    MSGSENDTHREADFINISHED
};

#endif				/* __LIBBALSA_THREADS_H__ */
