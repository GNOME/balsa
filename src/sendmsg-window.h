/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __BALSA_SENDMSG_H__
#define __BALSA_SENDMSG_H__

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef struct _BalsaSendmsg       BalsaSendmsg;

struct _BalsaSendmsg
{
  GtkWidget *window;
  GtkWidget *to, *from, *subject, *cc, *bcc;
  GtkWidget *text;
  GtkWidget *hscrollbar, *vscrollbar;
  GtkWidget *sendbutton;

  MAILSTREAM *current_stream;
  glong current_mesgno;
};

void sendmsg_window_new(GtkWidget *, gpointer);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __BALSA_SENDMSG_H__ */
