/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#ifndef __BALSA_MESSAGE_H__
#define __BALSA_MESSAGE_H__

#include <gtk/gtk.h>
#include "c-client.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define BALSA_MESSAGE(obj)          GTK_CHECK_CAST (obj, balsa_message_get_type (), BalsaMessage)
#define BALSA_MESSAGE_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, balsa_message_get_type (), BalsaMessageClass)
#define BALSA_IS_MESSAGE(obj)       GTK_CHECK_TYPE (obj, balsa_message_get_type ())


typedef struct _BalsaMessage BalsaMessage;
typedef struct _BalsaMessageClass BalsaMessageClass;

struct _BalsaMessage
  {
    GtkBin bin;
    
    MAILSTREAM *current_stream;
    glong current_mesgno;
  };

struct _BalsaMessageClass
  {
    GtkBinClass parent_class;
  };

guint balsa_message_get_type (void);
GtkWidget *balsa_message_new ();
void balsa_message_clear (BalsaMessage * bmessage);
void balsa_message_set (BalsaMessage * bmessage,
			MAILSTREAM * stream,
			glong mesgno);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __BALSA_MESSAGE_H__ */
