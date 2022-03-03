/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
    LIBBALSA_MESSAGE_SIGN_ERROR,
    LIBBALSA_MESSAGE_ENCRYPT_ERROR,
    LIBBALSA_MESSAGE_CREATE_ERROR,
    LIBBALSA_MESSAGE_QUEUE_ERROR,
    LIBBALSA_MESSAGE_SAVE_ERROR,
    LIBBALSA_MESSAGE_SEND_ERROR,
    LIBBALSA_MESSAGE_SERVER_ERROR
};

gboolean libbalsa_message_postpone(LibBalsaMessage * message,
				   LibBalsaMailbox * draftbox,
				   gchar ** extra_headers,
				   gboolean flow, 
				   GError **error);

LibBalsaMsgCreateResult libbalsa_message_queue(LibBalsaMessage* message, 
					       LibBalsaMailbox* outbox,
                                               LibBalsaMailbox* fccbox,
                                               LibBalsaSmtpServer *
                                               smtp_server,
					       gboolean flow,
					       GError ** error);
LibBalsaMsgCreateResult libbalsa_message_send(LibBalsaMessage     *message,
                                              LibBalsaMailbox     *outbox,
                                              LibBalsaMailbox     *fccbox,
                                              LibBalsaFccboxFinder finder,
                                              LibBalsaSmtpServer  *smtp_server,
											  gboolean			   show_progress,
                                              GtkWindow           *parent,
                                              gboolean             flow,
					                          GError             **error);
void libbalsa_process_queue(LibBalsaMailbox     *outbox,
                            LibBalsaFccboxFinder finder,
							GSList              *smtp_servers,
							gboolean			 show_progress,
							GtkWindow           *parent);

void libbalsa_auto_send_init(GSourceFunc auto_send_cb);
void libbalsa_auto_send_config(gboolean enable,
					           guint    timeout_minutes);

#endif /* __SEND_H__ */
