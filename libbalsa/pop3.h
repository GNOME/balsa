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

#ifndef __LIBBALSA_POP3_H__
#define __LIBBALSA_POP3_H__

typedef enum {
    POP_OK = 0,
    POP_CONN_ERR,
    POP_COMMAND_ERR,
    POP_WRITE_ERR,
    POP_PROCMAIL_ERR,
    POP_OPEN_ERR,
    POP_MSG_APPEND,
    POP_HOST_NOT_FOUND,
    POP_CONNECT_FAILED,
    POP_AUTH_FAILED
} PopStatus;

typedef void (*ProgressCallback)(void* m, 
                                 char *msg, int prog, int tot);

PopStatus libbalsa_fetch_pop_mail_direct (LibBalsaMailboxPop3 * mailbox, 
					  const gchar * spoolfile, 
					  gchar* uid, 
					  ProgressCallback prog_cb, void*data);

PopStatus libbalsa_fetch_pop_mail_filter (LibBalsaMailboxPop3 * mailbox, 
					  gchar* uid,
					  ProgressCallback prog_cb, void*data);

const gchar *pop_get_errstr(PopStatus status);

extern gint PopDebug;

#endif				/* __LIBBALSA_NOTIFY_H__ */
