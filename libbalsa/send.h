/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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


#ifndef __SEND_H__
#define __SEND_H__

#if ENABLE_ESMTP
#include <libesmtp.h>

gboolean libbalsa_process_queue(LibBalsaMailbox* outbox, gint encoding, 
				gchar* smtp_server, auth_context_t smtp_authctx,
				gint tls_mode, gboolean flow);
#else

gboolean libbalsa_process_queue(LibBalsaMailbox* outbox, gint encoding, 
				gboolean flow);

#endif

#ifdef BALSA_USE_THREADS
extern pthread_t send_mail;
extern pthread_mutex_t send_messages_lock;
extern int send_thread_pipes[2];

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

#endif /* BALSA_USE_THREADS */

#endif /* __SEND_H__ */
