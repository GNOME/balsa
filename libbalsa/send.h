/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "libbalsa.h"

typedef LibBalsaMailbox* (*LibBalsaFccboxFinder)(const gchar *url);
typedef enum _LibBalsaMsgCreateResult LibBalsaMsgCreateResult;
enum _LibBalsaMsgCreateResult {
    LIBBALSA_MESSAGE_CREATE_OK,
#ifdef HAVE_GPGME
    LIBBALSA_MESSAGE_SIGN_ERROR,
    LIBBALSA_MESSAGE_ENCRYPT_ERROR,
#endif
    LIBBALSA_MESSAGE_CREATE_ERROR,
    LIBBALSA_MESSAGE_QUEUE_ERROR,
    LIBBALSA_MESSAGE_SAVE_ERROR,
    LIBBALSA_MESSAGE_SEND_ERROR,
    LIBBALSA_MESSAGE_SERVER_ERROR
};

gboolean libbalsa_message_postpone(LibBalsaMessage * message,
				   LibBalsaMailbox * draftbox,
				   LibBalsaMessage * reply_message,
				   gchar ** extra_headers,
				   gboolean flow, 
				   GError **error);


#if ENABLE_ESMTP

LibBalsaMsgCreateResult libbalsa_message_queue(LibBalsaMessage* message, 
					       LibBalsaMailbox* outbox,
                                               LibBalsaMailbox* fccbox,
                                               LibBalsaSmtpServer *
                                               smtp_server,
					       gboolean flow,
					       GError ** error);
LibBalsaMsgCreateResult libbalsa_message_send(LibBalsaMessage * message,
                                              LibBalsaMailbox * outbox,
                                              LibBalsaMailbox * fccbox,
                                              LibBalsaFccboxFinder finder,
                                              LibBalsaSmtpServer *
                                              smtp_server,
                                              GtkWindow * parent,
                                              gboolean flow,
                                              gboolean debug,
					      GError ** error);
gboolean libbalsa_process_queue(LibBalsaMailbox * outbox,
                                LibBalsaFccboxFinder finder,
                                GSList * smtp_servers,
                                GtkWindow * parent,
                                gboolean debug);
#else

LibBalsaMsgCreateResult libbalsa_message_queue(LibBalsaMessage* message, 
					       LibBalsaMailbox* outbox,
                                               LibBalsaMailbox* fccbox,
					       gboolean flow,
					       GError ** error);
LibBalsaMsgCreateResult libbalsa_message_send(LibBalsaMessage* message,
					      LibBalsaMailbox* outbox,  
					      LibBalsaMailbox* fccbox,
                                              LibBalsaFccboxFinder finder, 
					      gboolean flow, gboolean debug,
					      GError ** error);
gboolean libbalsa_process_queue(LibBalsaMailbox* outbox, 
                                LibBalsaFccboxFinder finder,
                                gboolean debug);

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
  t_message = g_new(SendThreadMessage, 1); \
  t_message->message_type = type; \
  strncpy(t_message->message_string, string, sizeof(t_message->message_string)); \
  t_message->msg = s_msg; \
  t_message->mbox = s_mbox; \
  t_message->of_total = messof; \
  if (write( send_thread_pipes[1], (void *) &t_message, sizeof(void *) ) \
      < (ssize_t) sizeof(void *)) \
    g_warning("pipe error");

enum {
    MSGSENDTHREADERROR,
    MSGSENDTHREADPROGRESS,
    MSGSENDTHREADPOSTPONE,
    MSGSENDTHREADDELETE,
    MSGSENDTHREADFINISHED
};

#endif /* BALSA_USE_THREADS */

#endif /* __SEND_H__ */
