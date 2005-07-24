/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#ifndef __THREADS_H__
#define __THREADS_H__
/* FIXME: mailbox_lock is really an internal libbalsa mutex. */
extern pthread_mutex_t mailbox_lock;

/*  define thread globals */
extern pthread_t get_mail_thread;
extern int checking_mail;
extern int mail_thread_pipes[2];
extern GIOChannel *mail_thread_msg_send;
extern GIOChannel *mail_thread_msg_receive;
extern GIOChannel *send_thread_msg_send;
extern GIOChannel *send_thread_msg_receive;

extern GtkWidget *send_progress_message;
extern GtkWidget *send_dialog;
extern GtkWidget *send_dialog_bar;

typedef struct {
    LibBalsaMailboxNotify message_type;
    char message_string[160];
    LibBalsaMailbox *mailbox;
    int num_bytes, tot_bytes;
} MailThreadMessage;

/*
 *  Macro to send message:
 *    message is MailThreadMessage *message
 *    type is one of the defined message types below
 *    string is string to send
 */

#define  MSGMAILTHREAD( message, type, mbox, string, num, tot) \
  message = g_new(MailThreadMessage, 1); \
  message->message_type = type; \
  message->mailbox = mbox; \
  strncpy( message->message_string, string, sizeof(message->message_string)); \
  message->message_string[sizeof(message->message_string)-1]='\0';\
  message->num_bytes=num;\
  message->tot_bytes=tot;\
  if (write( mail_thread_pipes[1], (void *) &message, sizeof(void *) ) \
      != sizeof(void *)) \
    g_warning("pipe error");

#endif				/* __THREADS_H__ */
