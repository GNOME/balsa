/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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

#ifndef __BALSA_SENDMSG_H__
#define __BALSA_SENDMSG_H__

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

  typedef enum
    {
      SEND_NORMAL,
      SEND_REPLY,
      SEND_REPLY_TO_ALL,
      SEND_FORWARD
    }
  SendType;


  typedef struct _BalsaSendmsg BalsaSendmsg;

  struct _BalsaSendmsg
    {
      GtkWidget *to, *from, *subject, *cc, *bcc;
      GtkWidget *text;
      GtkWidget *window;
      Message *orig_message;
      SendType type;
    };

  void sendmsg_window_new (GtkWidget *, Message *, SendType);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __BALSA_SENDMSG_H__ */
