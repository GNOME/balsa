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

#ifndef __THREAD_MSGS_H__
#define __THREAD_MSGS_H__

extern int    mail_thread_pipes[2];

typedef struct
{
  int message_type;
  char message_string[160];
  int *mailbox; /* Mailbox *   */
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
  MSGMAILTHREAD_LOAD,
  MSGMAILTHREAD_FINISHED
};

#endif /* __THREAD_MSGS_H__ */
